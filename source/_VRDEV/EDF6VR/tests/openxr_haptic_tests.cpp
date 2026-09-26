#include "../src/openxr_session.cpp"
#include <vector>
using namespace edf6vr;
static int failures=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
struct Sent { bool stop; XrPath hand; XrDuration duration; float amplitude; DWORD thread; };
static std::vector<Sent> sent;
static XrResult reply=XR_SUCCESS,syncReply=XR_SUCCESS;
static bool synced=false;
static std::string lastLog;
static void CaptureLog(const char* line){lastLog=line;}
static XrResult XRAPI_PTR Apply(XrSession session,const XrHapticActionInfo* info,const XrHapticBaseHeader* h) {
    const auto& buzz=*reinterpret_cast<const XrHapticVibration*>(h);
    CHECK(session==g_session && info->action==g_hapticAction);
    CHECK(buzz.frequency==XR_FREQUENCY_UNSPECIFIED && buzz.duration>0);
    sent.push_back({false,info->subactionPath,buzz.duration,buzz.amplitude,GetCurrentThreadId()});return reply;
}
static XrResult XRAPI_PTR Stop(XrSession,const XrHapticActionInfo* info) {
    sent.push_back({true,info->subactionPath,0,0,GetCurrentThreadId()});return reply;
}
static XrResult XRAPI_PTR Sync(XrSession,const XrActionsSyncInfo*) {synced=true;return syncReply;}
static XrResult XRAPI_PTR Float(XrSession,const XrActionStateGetInfo*,XrActionStateFloat*) {return XR_SUCCESS;}
static XrResult XRAPI_PTR Bool(XrSession,const XrActionStateGetInfo*,XrActionStateBoolean*) {return XR_SUCCESS;}
static XrResult XRAPI_PTR Vector(XrSession,const XrActionStateGetInfo*,XrActionStateVector2f*) {return XR_SUCCESS;}
int main() {
    g_running=true;g_stop=false;g_inputReady=true;g_sessionRunning=true;
    g_session=reinterpret_cast<XrSession>(1);g_hapticAction=reinterpret_cast<XrAction>(2);
    g_handPath[0]=3;g_handPath[1]=4;
    g_api.applyHaptic=&Apply;g_api.stopHaptic=&Stop;
    g_api.syncActions=&Sync;g_api.getActionStateFloat=&Float;
    g_api.getActionStateBoolean=&Bool;g_api.getActionStateVector2f=&Vector;
    // Reproduce the old silent-loss condition with a real competing thread.
    AcquireSRWLockExclusive(&g_frameLock);
    std::thread producer([]{g_openxr.Buzz(0,.24f,.65f);});producer.join();
    CHECK(sent.empty() && g_hapticStats.frameBusy==1 && g_hapticPending[0].pending);
    ReadHands(123); // production frame-owner path, still holding the frame lock
    CHECK(synced && sent.size()==1 && sent.back().thread==GetCurrentThreadId());
    CHECK(sent.back().hand==3 && sent.back().amplitude==.65f);
    CHECK(sent.back().duration<=240000000);
    // Cancellation replaces queued apply; it is not lost under the same lock.
    g_openxr.Buzz(0,.24f,.65f);g_openxr.StopBuzz(0);
    ReadHands(124);CHECK(sent.size()==2 && sent.back().stop);
    ReleaseSRWLockExclusive(&g_frameLock);
    // Stable hold feedback covers every refresh without raising amplitude.
    for(ULONGLONG tick=1000;tick<4000;tick+=80) {
        g_openxr.Buzz(0,.24f,.65f);g_hapticPending[0].until=tick+240;
        FlushHaptics(XR_SUCCESS,tick+20);
        CHECK(!sent.back().stop && sent.back().duration==220000000 && sent.back().amplitude==.65f);
    }
    const auto before=sent.size();
    g_openxr.Buzz(0,.24f,.65f);g_hapticPending[0].until=5000;
    FlushHaptics(XR_SUCCESS,5000);CHECK(sent.size()==before && g_hapticStats.expired==1);
    // Positive NOT_FOCUSED is counted separately from XR_SUCCESS.
    const auto ok=g_hapticStats.ok;
    reply=XR_SESSION_NOT_FOCUSED;g_openxr.Buzz(1,.24f,.65f);ReadHands(125);
    CHECK(g_hapticStats.notFocused==1 && g_hapticStats.ok==ok && sent.back().hand==4);
    CHECK(g_hapticStats.applyResult==XR_SESSION_NOT_FOCUSED);
    reply=XR_ERROR_RUNTIME_FAILURE;g_openxr.Buzz(0,.24f,.65f);ReadHands(126);
    CHECK(g_hapticStats.failed==1 && g_hapticStats.applyResult==XR_ERROR_RUNTIME_FAILURE);
    // A failed input sync must not prevent an explicit stop from being sent.
    syncReply=XR_ERROR_RUNTIME_FAILURE;reply=XR_SUCCESS;g_openxr.StopBuzz(0);ReadHands(127);
    CHECK(sent.back().stop && g_hapticStats.syncResult==XR_ERROR_RUNTIME_FAILURE);
    g_openxr.Buzz(0,.24f,.65f);g_openxr.Buzz(1,.24f,.65f);
    g_mode=XrMode::HeadsetDisplay;g_openxr.Stop();
    CHECK(!g_hapticPending[0].pending && !g_hapticPending[1].pending);
    const auto stoppedSize=sent.size();g_openxr.Buzz(0,.24f,.65f);FlushHaptics(XR_SUCCESS,6000);
    CHECK(sent.size()==stoppedSize && g_hapticStats.rejected==1);
    g_running=true;g_stop=false;g_inputReady=false;g_openxr.Buzz(0,.24f,.65f);
    CHECK((g_hapticStats.gate&4)==4 && !g_hapticPending[0].pending);
    g_inputReady=true;g_handPath[0]=0;g_openxr.Buzz(0,.24f,.65f);ReadHands(128);
    CHECK(g_hapticStats.unavailable==1);
    g_openxr.Buzz(-1,.24f,.65f);g_openxr.Buzz(0,NAN,.65f);g_openxr.StopBuzz(2);
    CHECK(!g_hapticPending[0].pending);
    g_hapticStats={};g_handPath[0]=3;g_hapticNextLog=0;g_log=&CaptureLog;
    syncReply=XR_SUCCESS;reply=XR_ERROR_RUNTIME_FAILURE;
    g_openxr.Buzz(0,.24f,.65f);ReadHands(129);
    CHECK(lastLog.find("apply=1 stop=0 ok=0 notFocused=0 failed=1")!=std::string::npos);
    CHECK(lastLog.find("leftApplyStopResult=1,0,-2")!=std::string::npos);
    g_log=nullptr;
    printf("Haptic owner dispatch, contention, cancellation, expiry and XR results: %d failures (fake XR, no hardware)\n",failures);
    return failures?1:0;
}
