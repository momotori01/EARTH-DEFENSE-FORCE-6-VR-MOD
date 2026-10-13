#include "camera_math.h"
#include "image_profile.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
int main() {
    using namespace edf6vr;
    Matrix original{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{123,45,-67,1}}}, out{};
    CHECK(ValidCamera(original));
    HeadAnchor anchor{};
    float rawEye[3]={123.15f,46.58f,-66.9f},anchored[3]{};
    CHECK(AnchorHead(anchor,original,rawEye,anchored));
    const float initial[3]={anchored[0],anchored[1],anchored[2]};
    for(int i=0;i<360;++i) {
        Matrix turning{}; CHECK(RotateCamera(original,float(i-180),float(i%160-80),turning));
        // Hand aim changes body rotation and head bone placement, but the
        // first-person origin must remain fixed while root position is fixed.
        for(int j=0;j<3;++j) rawEye[j]=initial[j]+std::sin(float(i+j))*.25f;
        CHECK(AnchorHead(anchor,turning,rawEye,anchored));
        for(int j=0;j<3;++j) CHECK(std::fabs(anchored[j]-initial[j])<.00001f);
    }
    // Starting VR during a knockdown must not capture a permanent floor-level eye.
    HeadAnchor knockedDown{};
    float lowEye[3]={123,45.2f,-67},untouched[3]={7,8,9};
    CHECK(!AnchorHead(knockedDown,original,lowEye,untouched));
    CHECK(!knockedDown.primed && untouched[1]==8);
    CHECK(AnchorHead(knockedDown,original,initial,anchored));
    CHECK(knockedDown.primed && std::fabs(anchored[1]-46.58f)<.00001f);
    CHECK(AnchorHead(knockedDown,original,lowEye,anchored));
    CHECK(std::fabs(anchored[1]-46.58f)<.00001f);
    auto translated=original; translated.m[3][0]+=.3f; translated.m[3][1]+=.2f; translated.m[3][2]-=.4f;
    CHECK(AnchorHead(anchor,translated,rawEye,anchored));
    CHECK(std::fabs(anchored[0]-initial[0]-.3f)<.00001f);
    CHECK(std::fabs(anchored[1]-initial[1]-.2f)<.00001f);
    CHECK(std::fabs(anchored[2]-initial[2]+.4f)<.00001f);
    CHECK(!std::memcmp(original.m[3],Matrix{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{123,45,-67,1}}}.m[3],16));
    CHECK(RotateCamera(original,90,0,out)); CHECK(std::fabs(out.m[2][0]-1)<1e-5);
    CHECK(RotateCamera(original,0,45,out)); CHECK(std::fabs(out.m[2][1]-0.70710678f)<1e-5);
    CHECK(!std::memcmp(out.m[3],original.m[3],sizeof(original.m[3])));
    CHECK(RotateCamera(original,0,0,out)); CHECK(!std::memcmp(&out,&original,sizeof(out)));
    Matrix tilted{}; CHECK(RotateCamera(original,43,21,tilted));
    for(float y=-180;y<=180;y+=15) for(float p=-85;p<=85;p+=5) {
        CHECK(RotateCamera(tilted,y,p,out)); CHECK(ValidCamera(out));
        CHECK(!std::memcmp(out.m[3],original.m[3],sizeof(original.m[3])));
    }
    out=original; out.m[0][0]=std::numeric_limits<float>::quiet_NaN(); CHECK(!ValidCamera(out));
    out=original; out.m[1][1]=2; CHECK(!ValidCamera(out));
    out=original; out.m[0][0]=-1; CHECK(!ValidCamera(out));
    CHECK(!RotateCamera(original,181,0,out)); CHECK(!RotateCamera(original,0,86,out));
    CHECK(!RotateCamera(original,std::numeric_limits<float>::infinity(),0,out));
    SavedCamera state{}; out=original;
    for(int i=0;i<10000;++i) {
        state.Restore(out); CHECK(!std::memcmp(&out,&original,sizeof(out)));
        CHECK(state.Apply(out,15,10));
    }
    CHECK(state.Restore(out)); CHECK(!std::memcmp(&out,&original,sizeof(out))); CHECK(!state.pending);
    CHECK(state.Apply(out,15,10)); out.m[3][0]=456;
    CHECK(!state.Restore(out)); CHECK(out.m[3][0]==456); // Other game system wins.
    ImageProfile profile{}; char reason[256]{};
    CHECK(!CheckImage(nullptr,profile,reason,sizeof(reason)));
    CHECK(!Readable(nullptr,16));
    auto slot=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    CHECK(slot!=nullptr);
    if(slot) {
        *slot=reinterpret_cast<void*>(1); DWORD old=0; CHECK(VirtualProtect(slot,4096,PAGE_READONLY,&old)!=FALSE);
        bool changed=false; CHECK(ReplacePointer(slot,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),changed));
        CHECK(changed && *slot==reinterpret_cast<void*>(2));
        MEMORY_BASIC_INFORMATION mbi{}; VirtualQuery(slot,&mbi,sizeof(mbi)); CHECK(mbi.Protect==PAGE_READONLY);
        CHECK(!ReplacePointer(slot,reinterpret_cast<void*>(1),reinterpret_cast<void*>(3),changed));
        CHECK(!changed && *slot==reinterpret_cast<void*>(2));
        VirtualFree(slot,0,MEM_RELEASE);
    }
    // Mission or menu without the body: the three states the 0.8.x signal
    // table measured, plus the one that sent the display to the board.
    {
        const unsigned long long now=100000;
        CHECK(!CameraKeepsSceneLive(0,now));          // title: no first person camera, input runs
        CHECK(CameraKeepsSceneLive(now,now));         // mission, everything fresh
        CHECK(!CameraKeepsSceneLive(now,now-922));    // pause menu: camera written, input 922 ms stale
        CHECK(CameraKeepsSceneLive(now,now-16));      // knocked down looking at the sky: body unseen
        CHECK(!CameraKeepsSceneLive(now,0));          // input never read
        CHECK(!CameraKeepsSceneLive(0,0));
        CHECK(CameraKeepsSceneLive(now-5,now));       // input read on another thread just after
        CHECK(!CameraKeepsSceneLive(now,now-kSceneSignalMs));
        CHECK(CameraKeepsSceneLive(now,now-(kSceneSignalMs-1)));
    }
    {
        // SteadyHeave: a walker's heave eased, with the numbers of the game's own
        // clips (research/cockpit/walk_bob.py). What is left over the last bounce,
        // and how far the view got from the cabin.
        auto walk=[](float span,float period,float seconds,float limit,float& left,float& furthest) {
            HeaveSteady s{}; int owner=0; float low=1e9f,high=-1e9f; furthest=0;
            const double dt=1.0/60.0; const int count=static_cast<int>(period*8/dt),tail=static_cast<int>(period/dt);
            for(int i=0;i<count;++i) {
                const float h=10.0f+0.5f*span*std::sin(6.2831853f*static_cast<float>(i*dt)/period);
                const float shift=SteadyHeave(s,&owner,h,i*dt,seconds,limit);
                if(i>=count-tail) { low=std::fmin(low,h+shift); high=std::fmax(high,h+shift); furthest=std::fmax(furthest,std::fabs(shift)); }
            }
            left=high-low;
        };
        float left=0,furthest=0;
        walk(2.134f,3.5f,4.0f,2.0f,left,furthest);       // the Barga forwards: 2.1 m once in 3.5 s, followed over 4 s
        CHECK(left>0.25f && left<0.60f && furthest<1.5f);
        walk(2.134f,1.75f,4.0f,2.0f,left,furthest);      // its clip at twice the pace: less still
        CHECK(left>0.10f && left<0.35f);
        walk(0.043f,0.308f,0.35f,0.05f,left,furthest);   // the Depth Crawler: 4 cm three times a second, the Nix's 0.35 s
        CHECK(left>0.003f && left<0.010f && furthest<0.03f);
        walk(0.203f,2.5f,0.35f,0.35f,left,furthest);     // the Nix's own, for the measure: 9 to 15 cm of 20 are left
        CHECK(left>0.09f && left<0.16f);
        // A landing, a fall: the view is held back by the limit and no more, and is on the cabin soon after.
        HeaveSteady s{}; int crawler=0,barga=0;
        CHECK(SteadyHeave(s,&crawler,10.0f,0.0,0.35f,0.05f)==0 && s.primed);
        CHECK(std::fabs(SteadyHeave(s,&crawler,8.6f,1.0/60,0.35f,0.05f)-0.05f)<1e-5f);
        float shift=1;
        for(int i=2;i<200;++i) shift=SteadyHeave(s,&crawler,8.6f,i/60.0,0.35f,0.05f);
        CHECK(std::fabs(shift)<0.001f);
        // Another machine, a gap in the updates, a clock that went back: from the real height again.
        CHECK(SteadyHeave(s,&barga,40.0f,200/60.0,4.0f,2.0f)==0);
        CHECK(SteadyHeave(s,&barga,41.0f,200/60.0+1.0/60,4.0f,2.0f)<0);
        CHECK(SteadyHeave(s,&barga,42.0f,200/60.0+1.0,4.0f,2.0f)==0);
        CHECK(SteadyHeave(s,&barga,43.0f,1.0,4.0f,2.0f)==0);
        // Turned off, or a height that is no number: nothing, and a fresh start afterwards.
        CHECK(SteadyHeave(s,&barga,43.0f,1.0+1.0/60,0.0f,2.0f)==0 && !s.primed);
        CHECK(SteadyHeave(s,&barga,std::numeric_limits<float>::quiet_NaN(),1.1,4.0f,2.0f)==0 && !s.primed);
        CHECK(SteadyHeave(s,&barga,43.0f,1.2,4.0f,2.0f)==0 && s.primed);
    }
    printf("EDF6VR tests: %d failures\n",failures);
    return failures?1:0;
}
