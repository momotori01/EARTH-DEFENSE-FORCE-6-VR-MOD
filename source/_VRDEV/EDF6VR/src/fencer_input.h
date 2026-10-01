#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace edf6vr {
// This is a pad deflection, not an angular speed. Native sensitivity, weapon
// restrictions, inertia and recoil remain downstream of XInput.
inline float FencerPursuitAxis(float error) noexcept {
    if(!std::isfinite(error)) return 0;
    constexpr float dead=0.0174532925f; // 1 degree settling band for first trial
    const float magnitude=std::fabs(error);
    if(magnitude<=dead) return 0;
    // Clear the standard right-stick dead zone. Full deflection from 5 degrees
    // off: a player holds the stick over until the aim is nearly there. At 30
    // degrees (until 2026-09-29) the last stretch of every turn and every small
    // correction crept in far slower than the weapon can turn. Shorter ramps
    // were tried on 2026-10-01 and dropped: full stick past the band never
    // settled (each step a whole 2 degrees, past and back), and full at 3
    // degrees stuttered with light weapons, which turn fastest. The game's own
    // turn rate, inertia and weight still limit it: this only says how far to push.
    constexpr float span=0.06981317f;   // 4 degrees: full stick at 1 + 4
    const float value=0.28f+0.72f*std::clamp((magnitude-dead)/span,0.0f,1.0f);
    return std::copysign(value,error);
}
inline float FencerYawError(float target,float current) noexcept {
    if(!std::isfinite(target) || !std::isfinite(current)) return 0;
    return std::remainder(target-current,6.28318530718f);
}
struct FencerPadCommand {
    float x=0,y=0,targetPitch=0,targetYaw=0;
    std::uint64_t time=0;
    bool valid=false;
    bool Fresh(std::uint64_t now) const noexcept {
        return valid && now>=time && now-time<100;
    }
};
inline FencerPadCommand FencerPursuit(float pitch,float yaw,float targetPitch,float targetYaw,
                                    std::uint64_t time) noexcept {
    FencerPadCommand out{};
    if(!std::isfinite(pitch) || !std::isfinite(yaw) || !std::isfinite(targetPitch) || !std::isfinite(targetYaw)) return out;
    // Native pad right / up decrease EDF yaw / pitch respectively (default axes).
    out.x=-FencerPursuitAxis(FencerYawError(targetYaw,yaw));
    out.y=-FencerPursuitAxis(targetPitch-pitch);
    out.targetPitch=targetPitch; out.targetYaw=targetYaw;
    out.time=time; out.valid=true;
    return out;
}
}
