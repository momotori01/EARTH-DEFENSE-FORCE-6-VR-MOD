#pragma once
#include <algorithm>
#include <cmath>
namespace edf6vr {
// Visual recoil. Placing the weapon at the hand discards the game's own kick
// animation, so the shots are counted instead: each discharge adds one to a
// level that decays, and the weapon and the hand holding it tilt up and move
// back by that level. Sustained fire keeps the level up with a jolt per shot;
// the last shot dies away within a few decay times.
struct RecoilKick {
    double at=0;          // seconds of the last counted discharge
    float level=0;        // at that moment
    static constexpr float kMax=2.5f;
    // The bullets of one shot (a shotgun's pellets) arrive within a millisecond
    // of each other and count once.
    void Shot(double seconds,float decaySeconds) noexcept {
        if(!std::isfinite(seconds)) return;
        if(at>0 && seconds-at<0.008) return;
        level=std::min(Level(seconds,decaySeconds)+1.0f,kMax);
        at=seconds;
    }
    float Level(double seconds,float decaySeconds) const noexcept {
        if(!(level>0) || !std::isfinite(seconds)) return 0;
        const double age=seconds-at;
        if(age<=0) return level;
        if(!(decaySeconds>0.001f)) return 0;
        return level*std::exp(-static_cast<float>(age)/decaySeconds);
    }
};
struct RecoilKickShape { float pitchRadians=0; float backMetres=0; };
// axes rows: right, up, ahead (the hand frame the weapon is placed in). The
// muzzle end tilts up about the palm and the whole thing moves back along the
// barrel, both in proportion to the level.
inline void ApplyRecoilKick(float level,const RecoilKickShape& shape,float axes[3][3],float palm[3]) noexcept {
    if(!(level>0) || !std::isfinite(level)) return;
    const float angle=shape.pitchRadians*level, back=shape.backMetres*level;
    const float c=std::cos(angle), s=std::sin(angle);
    float up[3],ahead[3];
    for(int j=0;j<3;++j) { up[j]=axes[1][j]; ahead[j]=axes[2][j]; }
    for(int j=0;j<3;++j) {
        axes[2][j]=ahead[j]*c+up[j]*s;
        axes[1][j]=up[j]*c-ahead[j]*s;
        palm[j]-=ahead[j]*back;
    }
}
// The same kick carried onto another frame held with the first. A support hand
// on a two-handed weapon turns about the firing hand's palm and moves back
// with it, so it stays on the weapon instead of the weapon kicking through it.
// reference is the firing hand before its kick; with the reference equal to
// the frame itself this is ApplyRecoilKick.
inline void ApplyRecoilKickWith(float level,const RecoilKickShape& shape,const float referenceAxes[3][3],
                                const float referencePalm[3],float axes[3][3],float palm[3]) noexcept {
    if(!(level>0) || !std::isfinite(level)) return;
    const float angle=shape.pitchRadians*level, back=shape.backMetres*level;
    const float c=std::cos(angle), s=std::sin(angle);
    const float* up=referenceAxes[1]; const float* ahead=referenceAxes[2];
    // v = r R + u U + a A turns to r R + (u c + a s) U + (a c - u s) A.
    auto turn=[&](float v[3]) {
        float u=0,a=0;
        for(int j=0;j<3;++j) { u+=v[j]*up[j]; a+=v[j]*ahead[j]; }
        const float du=u*c+a*s-u, da=a*c-u*s-a;
        for(int j=0;j<3;++j) v[j]+=up[j]*du+ahead[j]*da;
    };
    for(int k=0;k<3;++k) turn(axes[k]);
    float offset[3];
    for(int j=0;j<3;++j) offset[j]=palm[j]-referencePalm[j];
    turn(offset);
    for(int j=0;j<3;++j) palm[j]=referencePalm[j]+offset[j]-ahead[j]*back;
}
}
