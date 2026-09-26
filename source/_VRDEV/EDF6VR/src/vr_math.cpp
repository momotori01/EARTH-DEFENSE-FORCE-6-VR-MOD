#include "vr_math.h"
#include <algorithm>
#include <cmath>

namespace edf6vr {
namespace {
bool Finite(const Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float Dot(const Vec3& a,const Vec3& b) noexcept { return a.x*b.x+a.y*b.y+a.z*b.z; }
// A[candidate] as (row0, row1) over (inX, inZ); bit0 swaps the axes, bits 1/2 flip signs.
void Basis(int candidate,float& sx,float& sz,bool& swap) noexcept {
    swap=(candidate&1)!=0;
    sx=(candidate&2)?-1.0f:1.0f;
    sz=(candidate&4)?-1.0f:1.0f;
}
}

Vec3 XrToGame(const Vec3& v) noexcept { return Vec3{-v.x,v.y,-v.z}; }

float BinocularFov(float verticalFov,float magnification) noexcept {
    if(!std::isfinite(magnification) || magnification<=1 || magnification>40) return verticalFov;
    return 2*std::atan(std::tan(verticalFov*0.5f)/magnification);
}

Vec3 AimToReference(float worldYaw,float pitchUp,float yawOffset) noexcept {
    const float yaw=worldYaw-yawOffset, flat=std::cos(pitchUp);
    return XrToGame(Vec3{std::sin(yaw)*flat,std::sin(pitchUp),std::cos(yaw)*flat});
}

Vec3 RotateY(const Vec3& v,float radians) noexcept {
    const float c=std::cos(radians), s=std::sin(radians);
    return Vec3{v.x*c+v.z*s, v.y, -v.x*s+v.z*c};
}

Vec3 QuatRotate(const Quat& q,const Vec3& v) noexcept {
    // v + 2*q.w*(qv x v) + 2*(qv x (qv x v))
    const Vec3 u{q.x,q.y,q.z};
    const Vec3 t{2*(u.y*v.z-u.z*v.y), 2*(u.z*v.x-u.x*v.z), 2*(u.x*v.y-u.y*v.x)};
    return Vec3{v.x+q.w*t.x+(u.y*t.z-u.z*t.y),
                v.y+q.w*t.y+(u.z*t.x-u.x*t.z),
                v.z+q.w*t.z+(u.x*t.y-u.y*t.x)};
}

bool NormalizedQuat(const Quat& q) noexcept {
    if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)||!std::isfinite(q.w)) return false;
    const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    return std::fabs(n-1.0f)<0.01f;
}

bool HeadBasisFromXr(const Quat& q,HeadBasis& out) noexcept {
    out={};
    if(!NormalizedQuat(q)) return false;
    const Vec3 forward=XrToGame(QuatRotate(q,Vec3{0,0,-1}));
    const Vec3 up=XrToGame(QuatRotate(q,Vec3{0,1,0}));
    if(!Finite(forward)||!Finite(up)) return false;
    const float flat=forward.x*forward.x+forward.z*forward.z;
    // Looking straight up or down leaves yaw undefined; the head's up vector
    // carries the heading there instead. The swap used to happen at flat>1e-6,
    // which is a twentieth of a degree from vertical -- so from there out to a
    // few degrees the yaw was still being taken from two numbers barely above
    // the noise, and it span. A Wing Diver pointing up flies where she aims, so
    // she went with it. Both references are horizontal directions, so they are
    // crossed over as vectors across a band and the angle stays continuous.
    // kSteep/kNear are sin^2 of about 8 and 2 degrees from vertical.
    constexpr float kSteep=0.0194f,kNear=0.0012f;
    // Up leans back over the heading when the nose is up and forward over it
    // when the nose is down, so which end of it points along the heading
    // depends on the sign of the rise.
    const float facing=forward.y>=0?-1.0f:1.0f;
    float fx=forward.x,fz=forward.z;
    if(flat<kSteep) {
        const float rise=up.x*up.x+up.z*up.z;
        if(rise>1e-12f) {
            const float weight=flat<=kNear?0.0f:(flat-kNear)/(kSteep-kNear);
            const float fromFlat=flat>1e-12f?weight/std::sqrt(flat):0.0f;
            const float fromRise=facing*(1-weight)/std::sqrt(rise);
            fx=forward.x*fromFlat+up.x*fromRise;
            fz=forward.z*fromFlat+up.z*fromRise;
        }
    }
    out.yaw=(fx*fx+fz*fz>1e-12f)?std::atan2(fx,fz):std::atan2(facing*up.x,facing*up.z);
    float y=forward.y;
    if(y>1) y=1; else if(y<-1) y=-1;
    out.pitchUp=std::asin(y);
    out.forward=forward; out.up=up;
    return std::isfinite(out.yaw)&&std::isfinite(out.pitchUp);
}

Quat QuatMultiply(const Quat& a,const Quat& b) noexcept {
    return Quat{a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
                a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
                a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
                a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}

Quat TrimAimRotation(const Quat& aim,float yawRight,float pitchUp) noexcept {
    // The controller looks down -Z. Pitch first, about +X: -Z tips toward +Y.
    // Then yaw about +Y by -yawRight: -Z swings toward +X, the right.
    const float hy=-0.5f*yawRight, hp=0.5f*pitchUp;
    const Quat yaw{0,std::sin(hy),0,std::cos(hy)};
    const Quat pitch{std::sin(hp),0,0,std::cos(hp)};
    return QuatMultiply(aim,QuatMultiply(yaw,pitch));
}

bool WeaponGripOffset(const Quat& grip,const Vec3& aim,float right,float up,
    float roll,Vec3& offset) noexcept {
    if(!NormalizedQuat(grip) || !Finite(aim) || std::fabs(Dot(aim,aim)-1)>0.01f
        || !std::isfinite(right) || !std::isfinite(up) || !std::isfinite(roll)) return false;
    const auto rawForward=XrToGame(QuatRotate(grip,{0,0,-1}));
    const auto rawRight=XrToGame(QuatRotate(grip,{1,0,0}));
    const auto rawUp=XrToGame(QuatRotate(grip,{0,1,0}));
    const float c=std::cos(roll),s=std::sin(roll);
    const float r=right*c-up*s,u=right*s+up*c;
    const Vec3 raw{rawRight.x*r+rawUp.x*u,rawRight.y*r+rawUp.y*u,rawRight.z*r+rawUp.z*u};
    const float cosine=Dot(rawForward,aim);
    if(cosine<-0.9999f) return false; // no unique shortest arc at a half turn
    const Vec3 axis{rawForward.y*aim.z-rawForward.z*aim.y,
        rawForward.z*aim.x-rawForward.x*aim.z,rawForward.x*aim.y-rawForward.y*aim.x};
    const Vec3 first{axis.y*raw.z-axis.z*raw.y,axis.z*raw.x-axis.x*raw.z,axis.x*raw.y-axis.y*raw.x};
    const Vec3 second{axis.y*first.z-axis.z*first.y,axis.z*first.x-axis.x*first.z,axis.x*first.y-axis.y*first.x};
    const float scale=1/(1+cosine);
    const Vec3 result{raw.x+first.x+second.x*scale,raw.y+first.y+second.y*scale,raw.z+first.z+second.z*scale};
    if(!Finite(result)) return false;
    offset=result;return true;
}

bool CorrectTwoHandLateralYaw(const Quat& grip,const Vec3& supportGap,float right,
    float roll,float yawTrim,float pitchTrim,HeadBasis& aim) noexcept {
    if(!Finite(supportGap) || !Finite(aim.forward) || !std::isfinite(aim.yaw)
        || !std::isfinite(aim.pitchUp) || !std::isfinite(right) || !std::isfinite(yawTrim)
        || !std::isfinite(pitchTrim) || !std::isfinite(roll) || !NormalizedQuat(grip)) return false;
    if(right==0) return true;
    const float flat2=supportGap.x*supportGap.x+supportGap.z*supportGap.z;
    if(flat2<.0004f || right*right>=flat2*.64f) return false;
    const float baselineYaw=std::atan2(supportGap.x,supportGap.z);
    const float calibratedYaw=aim.yaw-yawTrim;
    const float pitch=std::clamp(aim.pitchUp+pitchTrim,-1.5f,1.5f);
    const float flat=std::cos(pitch),rise=std::sin(pitch);
    const auto wrap=[](float a) {return std::atan2(std::sin(a),std::cos(a));};
    float yaw=calibratedYaw;
    // Only the horizontal projection of WeaponRight is relevant. In particular,
    // do not make the physical stock's support hand meet WeaponUp: its original
    // height and pitch calibration were already correct.
    for(int i=0;i<16;++i) {
        const Vec3 direction{std::sin(yaw)*flat,rise,std::cos(yaw)*flat};
        Vec3 offset{};
        if(!WeaponGripOffset(grip,direction,right,0,roll,offset)) return false;
        const float x=supportGap.x-offset.x,z=supportGap.z-offset.z;
        if(x*x+z*z<.0004f) return false;
        const float next=calibratedYaw+wrap(std::atan2(x,z)-baselineYaw);
        const float change=wrap(next-yaw);yaw=next;
        if(std::fabs(change)<1e-6f) {
            aim.yaw+=wrap(yaw-calibratedYaw);
            const float originalFlat=std::cos(aim.pitchUp);
            aim.forward.x=std::sin(aim.yaw)*originalFlat;
            aim.forward.z=std::cos(aim.yaw)*originalFlat;
            return true;
        }
    }
    return false;
}

bool RollCameraToUp(const Matrix& input,const Vec3& upWorld,Matrix& output,float& rollRadians) noexcept {
    rollRadians=0;
    if(!ValidCamera(input)||!Finite(upWorld)) return false;
    const Vec3 right{input.m[0][0],input.m[0][1],input.m[0][2]};
    const Vec3 up{input.m[1][0],input.m[1][1],input.m[1][2]};
    const float a=Dot(upWorld,right), b=Dot(upWorld,up);
    if(a*a+b*b<0.04f) { output=input; return true; }   // head is aimed along the camera axis
    const float roll=std::atan2(a,b);
    if(!std::isfinite(roll)) return false;
    const float c=std::cos(roll), s=std::sin(roll);
    output=input;
    for(int j=0;j<3;++j) {
        output.m[0][j]=c*input.m[0][j]-s*input.m[1][j];
        output.m[1][j]=s*input.m[0][j]+c*input.m[1][j];
    }
    rollRadians=roll;
    return ValidCamera(output);
}

bool LevelCameraPitch(const Matrix& input,Matrix& output,float& pitchRadians) noexcept {
    pitchRadians=0;
    if(!ValidCamera(input)) return false;
    const float forwardY=input.m[2][1], upY=input.m[1][1];
    // Looking straight up or down with the up row horizontal as well: there is
    // no turn about the right row that levels this, so leave it alone.
    if(forwardY*forwardY+upY*upY<0.04f) { output=input; return true; }
    const float pitch=std::atan2(-forwardY,upY);
    if(!std::isfinite(pitch)) return false;
    const float c=std::cos(pitch), s=std::sin(pitch);
    output=input;
    for(int j=0;j<3;++j) {
        output.m[1][j]=c*input.m[1][j]-s*input.m[2][j];
        output.m[2][j]=s*input.m[1][j]+c*input.m[2][j];
    }
    pitchRadians=pitch;
    return ValidCamera(output);
}

void MoveBasisApply(int candidate,float inX,float inZ,float& outX,float& outZ) noexcept {
    float sx=1,sz=1; bool swap=false; Basis(candidate,sx,sz,swap);
    outX=sx*(swap?inZ:inX);
    outZ=sz*(swap?inX:inZ);
}

void MoveBasisApplyInverse(int candidate,float x,float z,float& inX,float& inZ) noexcept {
    float sx=1,sz=1; bool swap=false; Basis(candidate,sx,sz,swap);
    const float a=x/sx, b=z/sz;
    inX=swap?b:a;
    inZ=swap?a:b;
}

void MoveBasisObserve(MoveBasis& state,float inX,float inZ,float localVx,float localVz) noexcept {
    if(state.locked) return;
    const float inLen=std::sqrt(inX*inX+inZ*inZ), vLen=std::sqrt(localVx*localVx+localVz*localVz);
    if(!std::isfinite(inLen)||!std::isfinite(vLen)||inLen<0.3f||vLen<0.3f) return;
    for(int c=0;c<kMoveBasisCount;++c) {
        float px=0,pz=0; MoveBasisApply(c,inX/inLen,inZ/inLen,px,pz);
        state.score[c]+=px*(localVx/vLen)+pz*(localVz/vLen);
    }
    state.speedSum+=vLen/inLen;
    ++state.samples;
}

bool MoveBasisSolve(MoveBasis& state) noexcept {
    if(state.locked) return true;
    if(state.samples<30) return false;
    int best=0;
    for(int c=1;c<kMoveBasisCount;++c) if(state.score[c]>state.score[best]) best=c;
    // A wrong basis scores near zero; require a clear win before trusting it.
    if(state.score[best]<static_cast<float>(state.samples)*0.8f) return false;
    const float speed=state.speedSum/static_cast<float>(state.samples);
    if(!std::isfinite(speed)||speed<0.5f||speed>40.0f) return false;
    state.candidate=best; state.scale=speed; state.locked=true;
    return true;
}

bool RoomScaleInput(const MoveBasis& state,float yaw,float wantWorldX,float wantWorldZ,
                    float& inX,float& inZ) noexcept {
    inX=0; inZ=0;
    if(!state.locked||state.scale<=0) return false;
    if(!std::isfinite(yaw)||!std::isfinite(wantWorldX)||!std::isfinite(wantWorldZ)) return false;
    const Vec3 local=RotateY(Vec3{wantWorldX,0,wantWorldZ},-yaw);
    MoveBasisApplyInverse(state.candidate,local.x/state.scale,local.z/state.scale,inX,inZ);
    const float len=std::sqrt(inX*inX+inZ*inZ);
    if(!std::isfinite(len)) { inX=0; inZ=0; return false; }
    if(len>1.0f) { inX/=len; inZ/=len; }
    return true;
}
}
