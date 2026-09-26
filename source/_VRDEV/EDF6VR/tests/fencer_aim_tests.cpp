#include "fencer_aim.h"
#include <cmath>
#include <cstdio>
static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
int main() {
    using edf6vr::FencerAimState; using edf6vr::FencerAimStep; using edf6vr::FencerAimKick;
    const float rate=3.14159265f, smoothing=0.12f;
    // First sample: takes the target as it is.
    { FencerAimState s; FencerAimStep(s,0.5f,0.2f,10.0,rate,smoothing); CHECK(s.valid && std::fabs(s.yaw-0.5f)<1e-6f && std::fabs(s.pitch-0.2f)<1e-6f); }
    // Held on a new target it converges, and never faster than the rate.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,smoothing);
        double t=0; float last=0;
        for(int i=0;i<200;++i) { t+=1.0/90; FencerAimStep(s,1.0f,0,t,rate,smoothing); CHECK(s.yaw>=last-1e-6f); CHECK(s.yaw-last<=rate/90+1e-4f); last=s.yaw; }
        CHECK(std::fabs(s.yaw-1.0f)<0.01f && std::fabs(s.pitch)<1e-6f);
    }
    // The rate cap: a 90 degree step at 180 deg/s takes about half a second.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,0.0f);
        double t=0; int steps=0;
        while(std::fabs(s.yaw-1.5707963f)>0.01f && steps<1000) { t+=1.0/90; FencerAimStep(s,1.5707963f,0,t,rate,0.0f); ++steps; }
        CHECK(steps>=44 && steps<=47);
    }
    // Yaw wraps: from +170 to -170 degrees goes the short way, through 180.
    {
        FencerAimState s; FencerAimStep(s,2.96706f,0,0,rate,0.0f);
        FencerAimStep(s,-2.96706f,0,0.05,rate,0.0f);
        CHECK(s.yaw>2.96706f || s.yaw<-2.96706f);
    }
    // Recoil throws the pitch up and the pursuit brings it back down.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,smoothing);
        FencerAimKick(s,0.1f); CHECK(std::fabs(s.pitch-0.1f)<1e-6f);
        double t=0; for(int i=0;i<90;++i) { t+=1.0/90; FencerAimStep(s,0,0,t,rate,smoothing); }
        CHECK(s.pitch<0.001f && s.pitch>=0);
    }
    // Nonsense is ignored; a long hitch is treated as a tenth of a second.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,smoothing);
        FencerAimStep(s,NAN,0,1,rate,smoothing); CHECK(s.yaw==0);
        FencerAimStep(s,1.0f,0,5.0,rate,0.0f); CHECK(s.yaw<=rate*0.1f+1e-5f && s.yaw>0);
    }
    std::printf("Fencer heavy aim: %d failures\n",failures);
    return failures?1:0;
}
