#include "motion_trace.h"
#include "openxr_session.h"
#include "frame_profile.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
namespace edf6vr {
namespace {
constexpr unsigned maxCameras=6000,maxFrames=3000;
struct Camera {
    std::int64_t qpc=0;std::uint64_t id=0,sample=0;XrTime predicted=0;
    unsigned kind=0,valid=0;float yaw=0;Matrix matrix{};XrPosef head{};
};
struct Frame {
    std::uint64_t id=0,beginCamera=0,endCamera=0;
    std::int64_t begin=0,present=0,end=0,viewAt=0,projectionAt=0;
    XrTime predicted=0;XrDuration period=0;
    unsigned viewReads=0,projectionReads=0,thread=0,count=0;int handedness=-1,result=0;
    float view[16]{},projection[16]{};XrPosef submitted[2]{};XrFovf fov[2]{};
    XrPosef latestHead{},located[2]{};std::uint64_t latestSample=0;XrTime latestPredicted=0;
};
SRWLOCK lock=SRWLOCK_INIT;
std::atomic<int> state{0}; // 0 off, 1 armed, 2 collecting, 3 ready, 4 writing, 5 saved, 6 failed
std::atomic<unsigned> drops{0};
Camera cameras[maxCameras]{};Frame frames[maxFrames]{},current{};
unsigned cameraCount=0,frameCount=0;std::uint64_t cameraId=0,frameId=0;
std::int64_t first=0,lastCameraAt=0,frequency=0;
wchar_t directory[MAX_PATH]{};MotionLog logger=nullptr;
XrPosef Pose(const HmdSample& s) {return {{s.orientation.x,s.orientation.y,s.orientation.z,s.orientation.w},{s.position.x,s.position.y,s.position.z}};}
bool Take() {if(TryAcquireSRWLockExclusive(&lock)) return true;++drops;return false;}
void Floats(FILE* f,const float* v,unsigned n) {for(unsigned i=0;i<n;++i) std::fprintf(f,",%.9g",v[i]);}
void PoseFields(FILE* f,const XrPosef& p) {
    const float v[]={p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w,p.position.x,p.position.y,p.position.z};Floats(f,v,7);
}
}
void ConfigureMotionTrace(bool on,const wchar_t* path,MotionLog log) noexcept {
    AcquireSRWLockExclusive(&lock);
    logger=log;directory[0]=0;if(path) wcsncpy_s(directory,path,_TRUNCATE);
    cameraCount=frameCount=0;cameraId=frameId=0;first=lastCameraAt=0;current={};drops=0;
    LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart;
    state=on?1:0;ReleaseSRWLockExclusive(&lock);
}
bool MotionTraceEnabled() noexcept {const auto s=state.load();return s==1 || s==2;}
void TraceCamera(const Matrix& m,const HmdSample& sample,float yaw,unsigned kind) noexcept {
    if(!MotionTraceEnabled() || !Take()) return;
    if(!MotionTraceEnabled()) {ReleaseSRWLockExclusive(&lock);return;}
    const auto now=PerfNow();lastCameraAt=now;++cameraId;
    if(cameraCount<maxCameras) cameras[cameraCount++]={now,cameraId,sample.frame,sample.displayTime,kind,
        sample.orientationValid?1u:0u,yaw,m,Pose(sample)};
    else {++drops;state=3;}
    ReleaseSRWLockExclusive(&lock);
}
void TraceFrameBegin(XrTime predicted,XrDuration period) noexcept {
    if(!MotionTraceEnabled() || !Take()) return;
    const auto now=PerfNow();
    // A recent *applied* player/vehicle camera starts the one-shot, never a title screen.
    if(state==1 && lastCameraAt && now-lastCameraAt<frequency/2) {first=now;state=2;}
    if(state==2) {
        if(current.id) ++drops; // unmatched previous begin must be visible in the report
        current={};current.id=++frameId;current.begin=now;current.beginCamera=cameraId;
        current.predicted=predicted;current.period=period;current.thread=GetCurrentThreadId();
    }
    ReleaseSRWLockExclusive(&lock);
}
void TraceNativeMatrix(const float* m,bool projection,int handedness) noexcept {
    if(state!=2 || !m || !Take()) return;
    if(state==2 && current.id && current.thread==GetCurrentThreadId()) {
        std::memcpy(projection?current.projection:current.view,m,16*sizeof(float));
        if(projection) {++current.projectionReads;current.projectionAt=PerfNow();current.handedness=handedness;}
        else {++current.viewReads;current.viewAt=PerfNow();}
    }
    ReleaseSRWLockExclusive(&lock);
}
void TracePresent() noexcept {
    if(state!=2 || !Take()) return;
    if(state==2 && current.id && current.thread==GetCurrentThreadId()) current.present=PerfNow();
    ReleaseSRWLockExclusive(&lock);
}
void TraceFrameEnd(const XrCompositionLayerProjectionView* views,unsigned count,const HmdSample& latest,XrResult result,const XrView* located) noexcept {
    if(state!=2 || !Take()) return;
    if(state==2 && current.id && current.thread==GetCurrentThreadId()) {
        current.end=PerfNow();current.endCamera=cameraId;current.result=int(result);
        current.latestHead=Pose(latest);current.latestSample=latest.frame;current.latestPredicted=latest.displayTime;
        if(located) for(unsigned i=0;i<2;++i) current.located[i]=located[i].pose;
        current.count=views && count==2?2u:0u;
        for(unsigned i=0;i<current.count;++i) {current.submitted[i]=views[i].pose;current.fov[i]=views[i].fov;}
        if(frameCount<maxFrames) frames[frameCount++]=current;else ++drops;
        if(current.end-first>=frequency*30 || frameCount>=maxFrames) state=3;
        current={};
    }
    ReleaseSRWLockExclusive(&lock);
}
void FinishMotionTrace() noexcept {
    if(!MotionTraceEnabled()) return;AcquireSRWLockExclusive(&lock);
    if(state==1 || state==2) state=frameCount?3:0;
    current={};ReleaseSRWLockExclusive(&lock);
}
void FlushMotionTrace() noexcept {
    int expected=3;if(!state.compare_exchange_strong(expected,4)) return;
    // Synchronize with the last producer. Once state=4 all producers are no-ops.
    AcquireSRWLockExclusive(&lock);ReleaseSRWLockExclusive(&lock);
    bool ok=false;std::filesystem::path prefix;
    try {
        std::filesystem::create_directories(directory);SYSTEMTIME t{};GetLocalTime(&t);
        wchar_t name[96];swprintf_s(name,L"motion_0.111_%04u%02u%02u-%02u%02u%02u-%lu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
        prefix=std::filesystem::path(directory)/name;
        FILE* cf=nullptr;FILE* ff=nullptr;
        if(_wfopen_s(&cf,(prefix.wstring()+L"_cameras.csv").c_str(),L"wb") || !cf) throw 1;
        if(_wfopen_s(&ff,(prefix.wstring()+L"_frames.csv").c_str(),L"wb") || !ff) {std::fclose(cf);throw 2;}
        std::fprintf(cf,"# frequency=%lld drops=%u\nqpc,id,sample,predicted,kind,valid,yaw",frequency,drops.load());
        for(unsigned i=0;i<16;++i) std::fprintf(cf,",camera%u",i);
        std::fprintf(cf,",qx,qy,qz,qw,px,py,pz\n");
        for(unsigned i=0;i<cameraCount;++i) {const auto& c=cameras[i];std::fprintf(cf,"%lld,%llu,%llu,%lld,%u,%u,%.9g",c.qpc,c.id,c.sample,c.predicted,c.kind,c.valid,c.yaw);Floats(cf,&c.matrix.m[0][0],16);PoseFields(cf,c.head);std::fputc('\n',cf);}
        std::fprintf(ff,"# frequency=%lld drops=%u\nid,begin,present,end,predicted,period,beginCamera,endCamera,viewAt,projectionAt,viewReads,projectionReads,thread,handedness,result,count,latestSample,latestPredicted",frequency,drops.load());
        for(const char* label:{"view","projection"}) for(unsigned i=0;i<16;++i) std::fprintf(ff,",%s%u",label,i);
        for(const char* label:{"left","right","latest","locatedLeft","locatedRight"}) for(const char* component:{"qx","qy","qz","qw","px","py","pz"}) std::fprintf(ff,",%s_%s",label,component);
        for(unsigned e=0;e<2;++e) for(unsigned j=0;j<4;++j) std::fprintf(ff,",fov%u_%u",e,j);
        std::fputc('\n',ff);
        for(unsigned i=0;i<frameCount;++i) {const auto& f=frames[i];std::fprintf(ff,"%llu,%lld,%lld,%lld,%lld,%lld,%llu,%llu,%lld,%lld,%u,%u,%u,%d,%d,%u,%llu,%lld",f.id,f.begin,f.present,f.end,f.predicted,f.period,f.beginCamera,f.endCamera,f.viewAt,f.projectionAt,f.viewReads,f.projectionReads,f.thread,f.handedness,f.result,f.count,f.latestSample,f.latestPredicted);Floats(ff,f.view,16);Floats(ff,f.projection,16);PoseFields(ff,f.submitted[0]);PoseFields(ff,f.submitted[1]);PoseFields(ff,f.latestHead);PoseFields(ff,f.located[0]);PoseFields(ff,f.located[1]);
            for(const auto& fov:f.fov) {const float v[]={fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown};Floats(ff,v,4);}std::fputc('\n',ff);}
        ok=std::ferror(cf)==0 && std::ferror(ff)==0;const int c=std::fclose(cf),f=std::fclose(ff);ok=ok && c==0 && f==0;
    } catch(...) {ok=false;}
    state=ok?5:6;
    if(logger) {char line[256];std::snprintf(line,sizeof(line),"MOTIONTRACE saved=%d frames=%u cameras=%u drops=%u; research/runtime/motion_0.111_* CSV pair",ok?1:0,frameCount,cameraCount,drops.load());logger(line);}
}
}
