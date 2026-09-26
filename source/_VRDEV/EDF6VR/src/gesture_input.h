#pragma once
#include <cmath>
#include <cstdint>
namespace edf6vr {
struct GestureHold {
    std::uint64_t start=0,last=0;
    bool active=false,fired=false;
    std::uint64_t nextPulse=0;
    bool Step(bool held,std::uint64_t now) noexcept {
        if(!held){*this={};return false;}
        if(!active || now<last || now-last>250){start=now;active=true;fired=false;nextPulse=now;}
        last=now;
        if(!fired && now-start>=3000){fired=true;return true;}
        return false;
    }
    bool Pulse(std::uint64_t now) noexcept {
        if(!active || fired || now<nextPulse)return false;
        nextPulse=now+80;return true;
    }
};
inline bool NearTemple(const float* head,const float* hand,float yaw,
    float side,float up,float ahead,float radius,float front) noexcept {
    if(!head || !hand || !std::isfinite(yaw))return false;
    const float dx=hand[0]-head[0],dy=hand[1]-head[1],dz=hand[2]-head[2];
    const float along=dx*std::sin(yaw)+dz*std::cos(yaw);
    const float across=dz*std::sin(yaw)-dx*std::cos(yaw);
    const float x=std::fabs(across)-std::fabs(side),y=dy-up,z=along-ahead;
    return x*x+y*y+z*z<radius*radius && along<front;
}
}
