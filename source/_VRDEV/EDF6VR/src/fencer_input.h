#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace edf6vr {
// The game's own right-stick dead zone: how far the pad must go before the
// Fencer's aim turns at all. Fitted from 5,500 logged pad/look pairs
// (2026-10-24, pads 0.27 to 0.6 on either axis): the look grows in a straight
// line from about 0.24 (0.23 to 0.25 bin by bin) to 0.030 rad a tick at full.
// It was taken as 0.28 (the game "answered 0.286 with a small look"), which is
// past the edge: the slightest error still pushed 0.28 and the game turned
// 0.05 to 0.1 degrees a tick for it, so with no band the right hand's aim
// never came to rest -- pushed past and back by the game's slowest turn, the
// pad flipping between +0.28 and -0.30 every second line of the FENCER log.
// The left hand's aim is the mod's own model of that turn (fencer_dual.h
// FencerGainFor), which took the same 0.28 as exactly zero and so settled.
inline constexpr float kFencerPadDeadzone=0.24f;
// This is a pad deflection, not an angular speed. Native sensitivity, weapon
// restrictions, inertia and recoil remain downstream of XInput.
inline float FencerPursuitAxis(float error) noexcept {
    if(!std::isfinite(error)) return 0;
    const float magnitude=std::fabs(error);
    if(!(magnitude>0)) return 0;
    // From the edge of the game's own right-stick dead zone (kFencerPadDeadzone),
    // so the look it gives back grows from nothing with the error, to full
    // deflection at 5 degrees off: a player holds the stick over until the aim
    // is nearly there.
    // At 30 degrees (until 2026-09-29) the last stretch of every turn and every
    // small correction crept in far slower than the weapon can turn. Shorter
    // ramps were tried on 2026-10-01 and dropped: full stick past a 1-degree
    // band never settled (each step a whole 2 degrees, past and back), and full
    // at 3 degrees stuttered with light weapons, which turn fastest.
    // No band (the user, 2026-10-24: "手振れ補正に１度の動きを無視するみたいな機能
    // ... それ切って"). Until then nothing was pushed within 1 degree and the
    // ramp ran from there, so the game turned by the error less that degree: the
    // aim stopped a degree short and a smaller move of the hand did nothing. The
    // ramp now runs from 0, which turns by the whole error and gently near it
    // (about a quarter of it a tick against 0.46 of the error past the band
    // before).
    // The game's own turn rate, inertia and weight still limit it: this only
    // says how far to push.
    constexpr float span=0.0872664626f;   // 5 degrees: full stick
    const float value=kFencerPadDeadzone+(1.0f-kFencerPadDeadzone)*std::clamp(magnitude/span,0.0f,1.0f);
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
