#pragma once
#include <algorithm>
#include <cmath>
namespace edf6vr {
// One hand's heavy aim. It follows the controller's direction with inertia (a
// first-order lag of `smoothing` seconds) and never faster than `rateRadians`
// per second; a shot throws it up by the recoil and the same pursuit brings it
// back. Angles are the game's world yaw (atan2(x,z)) and pitch, up positive.
struct FencerAimState { float yaw=0,pitch=0; bool valid=false; double at=0; };
inline float WrapAngle(float a) noexcept { return std::remainder(a,6.28318530718f); }
inline void FencerAimStep(FencerAimState& s,float targetYaw,float targetPitch,double now,
                          float rateRadians,float smoothing) noexcept {
    if(!std::isfinite(targetYaw) || !std::isfinite(targetPitch) || !std::isfinite(now)) return;
    if(!s.valid) { s.yaw=WrapAngle(targetYaw); s.pitch=std::clamp(targetPitch,-1.5f,1.5f); s.valid=true; s.at=now; return; }
    float dt=static_cast<float>(now-s.at);
    s.at=now;
    if(!(dt>0)) return;
    if(dt>0.1f) dt=0.1f;   // a hitch is not a reason to snap
    const float dy=WrapAngle(targetYaw-s.yaw), dp=targetPitch-s.pitch;
    const float k=smoothing>1e-4f?1.0f-std::exp(-dt/smoothing):1.0f;
    float sy=dy*k, sp=dp*k;
    const float mag=std::sqrt(sy*sy+sp*sp), cap=rateRadians*dt;
    if(mag>cap && mag>0) { sy*=cap/mag; sp*=cap/mag; }
    s.yaw=WrapAngle(s.yaw+sy);
    s.pitch=std::clamp(s.pitch+sp,-1.5f,1.5f);
}
inline void FencerAimKick(FencerAimState& s,float pitchRadians) noexcept {
    if(s.valid && std::isfinite(pitchRadians)) s.pitch=std::clamp(s.pitch+pitchRadians,-1.5f,1.5f);
}
}
