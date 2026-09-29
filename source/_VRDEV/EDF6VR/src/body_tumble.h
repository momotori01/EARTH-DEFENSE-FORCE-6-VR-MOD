#pragma once
// The view turning with the body: an opt-in for players who want to feel a Wing
// Diver's back-boost flip, a roll, being swung by a red ant, or a ragdoll fall
// ([FirstPerson] ViewFollowsBody=1; off by default, and then nothing here runs).
//
// The waist bone's full orientation is kept as a reference while the body is
// upright (the spine, waist to neck, within uprightDeg of vertical). Once it
// tips further, the reference freezes and the rotation from it to the waist's
// orientation now is what the body has turned through; that goes onto the view.
// A full orientation rather than just "up", so a 360-degree flip turns smoothly
// through upside down instead of snapping where "up" points straight down.
// Leans the game makes in normal play (running, the flight posture) are left out:
// nothing below startDeg, easing to all of it by fullDeg.
//
// And a spell only turns the view when the body got past startDeg quickly
// (within windowSec of leaving upright): a flip or a roll does that in a split
// second. The Wing Diver's free-fall and flight poses hold the body 90-110 degrees
// over for seconds at a time (hardware, 2026-09-29: 125 spells in 2.5 minutes,
// "the free fall used the ragdoll camera"), and those settle in more slowly.
#include <algorithm>
#include <cmath>

namespace edf6vr {

struct BodyTumble {
    bool haveRef=false;
    float ref[3][3]{};    // waist axes (unit rows, world) at the last upright moment
    float angle=0;        // degrees the body has turned since then
    float applied=0;      // degrees put onto the view
    // The current spell away from upright (for the window and the report).
    bool tipped=false,followed=false,refused=false;
    double tippedAt=0;    // when it left upright
    double reachedAt=-1;  // when it first got past startDeg
    float spellMax=0;
    // The last finished spell, for the log: how long, how fast, how far, followed.
    bool spellDone=false;
    float doneMs=0,doneReachMs=-1,doneMax=0; bool doneFollowed=false;
};

// Unit, square rows in the same handedness (Gram-Schmidt: 0, then 1, then 2).
inline bool TumbleOrthonormal(const float in[3][3],float out[3][3]) noexcept {
    for(int i=0;i<3;++i) {
        float v[3]={in[i][0],in[i][1],in[i][2]};
        for(int k=0;k<i;++k) {
            const float d=v[0]*out[k][0]+v[1]*out[k][1]+v[2]*out[k][2];
            for(int j=0;j<3;++j) v[j]-=d*out[k][j];
        }
        const float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(!(l>1e-5f) || !std::isfinite(l)) return false;
        for(int j=0;j<3;++j) out[i][j]=v[j]/l;
    }
    return true;
}
// R (acting on column vectors) taking each reference axis onto the axis now:
// R v = sum_i now_i (ref_i . v).
inline void TumbleBetween(const float ref[3][3],const float now[3][3],float R[3][3]) noexcept {
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) {
        R[r][c]=0;
        for(int i=0;i<3;++i) R[r][c]+=now[i][r]*ref[i][c];
    }
}
inline float TumbleAngle(const float R[3][3]) noexcept {
    return std::acos(std::clamp((R[0][0]+R[1][1]+R[2][2]-1.0f)*0.5f,-1.0f,1.0f));
}
inline void TumbleIdentity(float R[3][3]) noexcept {
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) R[r][c]=r==c?1.0f:0.0f;
}
// The same turn, s of the way (0 = none, 1 = all), about the same axis.
inline void TumbleScale(const float R[3][3],float s,float out[3][3]) noexcept {
    const float angle=TumbleAngle(R);
    if(s>=1.0f) { for(int r=0;r<3;++r) for(int c=0;c<3;++c) out[r][c]=R[r][c]; return; }
    if(s<=0.0f || angle<1e-4f) { TumbleIdentity(out); return; }
    float axis[3]={R[2][1]-R[1][2],R[0][2]-R[2][0],R[1][0]-R[0][1]};
    const float sine=std::sin(angle);
    if(sine>1e-3f) for(float& a:axis) a/=2.0f*sine;
    else {
        // Near a half turn: the axis from the diagonal, signed off the largest.
        int k=0;
        for(int i=1;i<3;++i) if(R[i][i]>R[k][k]) k=i;
        const float ak=std::sqrt(std::max(0.0f,(R[k][k]+1.0f)*0.5f));
        for(int i=0;i<3;++i) axis[i]=i==k?ak:(ak>1e-5f?(R[i][k]+R[k][i])/(4.0f*ak):0.0f);
    }
    const float l=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
    if(!(l>1e-5f)) { TumbleIdentity(out); return; }
    for(float& a:axis) a/=l;
    const float t=angle*s, c=std::cos(t), sn=std::sin(t), u=1.0f-c;
    const float x=axis[0],y=axis[1],z=axis[2];
    out[0][0]=c+x*x*u;   out[0][1]=x*y*u-z*sn; out[0][2]=x*z*u+y*sn;
    out[1][0]=y*x*u+z*sn; out[1][1]=c+y*y*u;   out[1][2]=y*z*u-x*sn;
    out[2][0]=z*x*u-y*sn; out[2][1]=z*y*u+x*sn; out[2][2]=c+z*z*u;
}
// The rotation to put onto the view this frame (identity when upright or the
// turn is small). waist: the waist bone's rows (world); spine: waist to neck.
inline bool UpdateBodyTumble(BodyTumble& state,const float waist[3][3],const float spine[3],
                             float uprightDeg,float startDeg,float fullDeg,float out[3][3],
                             double clock=0,double windowSec=0) noexcept {
    TumbleIdentity(out);
    float now[3][3]{};
    if(!TumbleOrthonormal(waist,now)) return false;
    const float l=std::sqrt(spine[0]*spine[0]+spine[1]*spine[1]+spine[2]*spine[2]);
    if(!(l>1e-4f) || !std::isfinite(l)) return false;
    const float tilt=std::acos(std::clamp(spine[1]/l,-1.0f,1.0f))*57.29578f;
    if(!state.haveRef || tilt<uprightDeg) {
        if(state.tipped) {
            state.spellDone=true; state.doneMs=static_cast<float>((clock-state.tippedAt)*1000.0);
            state.doneReachMs=state.reachedAt<0?-1.0f:static_cast<float>((state.reachedAt-state.tippedAt)*1000.0);
            state.doneMax=state.spellMax; state.doneFollowed=state.followed;
        }
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) state.ref[i][j]=now[i][j];
        state.haveRef=true; state.angle=0; state.applied=0;
        state.tipped=false; state.followed=false; state.refused=false; state.reachedAt=-1; state.spellMax=0;
        return true;
    }
    if(!state.tipped) { state.tipped=true; state.tippedAt=clock; }
    float R[3][3]{};
    TumbleBetween(state.ref,now,R);
    state.angle=TumbleAngle(R)*57.29578f;
    state.spellMax=std::max(state.spellMax,state.angle);
    if(state.angle>=startDeg && state.reachedAt<0) {
        state.reachedAt=clock;
        if(windowSec<=0 || clock-state.tippedAt<=windowSec) state.followed=true; else state.refused=true;
    }
    if(!state.followed) { state.applied=0; return true; }
    const float s=fullDeg>startDeg?std::clamp((state.angle-startDeg)/(fullDeg-startDeg),0.0f,1.0f):(state.angle>=startDeg?1.0f:0.0f);
    TumbleScale(R,s,out);
    state.applied=state.angle*s;
    return true;
}
inline void TumbleApply(const float R[3][3],const float v[3],float out[3]) noexcept {
    for(int r=0;r<3;++r) out[r]=R[r][0]*v[0]+R[r][1]*v[1]+R[r][2]*v[2];
}

}
