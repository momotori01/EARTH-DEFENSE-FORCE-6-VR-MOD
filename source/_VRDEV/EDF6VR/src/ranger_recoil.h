#pragma once
#include <algorithm>
#include <cmath>
namespace edf6vr {
// Apply the same 20-degree scale at every stack level. Shot history still
// makes slower fire accumulate less recoil; there is no low-stack protection.
inline float RangerRecoilSpreadDegrees(float level) noexcept {
    if(!std::isfinite(level)) return 0;
    level=std::clamp(level,0.0f,1.0f);
    return level*20.0f;
}
// One contribution per discharge, independent of pellets and render FPS.
// Each contribution fades linearly over one second. At 60 discharges/sec
// their sum reaches 1 in ~1 sec; all expire within 1 sec of the last shot.
struct RangerRecoil {
    double shots[128]{};
    unsigned next=0,count=0;
    void Shot(double seconds) noexcept {
        if(!std::isfinite(seconds))return;
        shots[next]=seconds;next=(next+1)%128;count=std::min(count+1,128u);
    }
    float Level(double seconds) const noexcept {
        if(!std::isfinite(seconds))return 0;
        double sum=0;
        for(unsigned i=0;i<count;++i) {
            const auto age=seconds-shots[i];
            if(age>=0 && age<1)sum+=(1-age)/30.0;
        }
        return static_cast<float>(std::min(sum,1.0));
    }
};
}
