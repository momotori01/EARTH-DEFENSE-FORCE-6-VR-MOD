#include "render_pose.h"
#include "frame_profile.h"
#include <atomic>
#include <cmath>
#include <limits>
namespace edf6vr {
namespace {
bool Finite(const Vec3& v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
Quat Multiply(Quat a,Quat b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
Quat Conjugate(Quat q) {return {-q.x,-q.y,-q.z,q.w};}
Quat AsQuat(XrQuaternionf q) {return {q.x,q.y,q.z,q.w};}
SRWLOCK historyLock=SRWLOCK_INIT;
RenderCameraHistory history;
std::atomic<bool> enabled{false};
std::atomic<unsigned> generation{0};
std::int64_t frequency=0;
struct Selection {HmdSample head{};std::int64_t at=0;unsigned generation=0;bool valid=false;
                 float yaw=std::numeric_limits<float>::quiet_NaN();};
thread_local Selection selected;
}
void RenderCameraHistory::Record(const Matrix& camera,const HmdSample& head,std::int64_t now,float yawOffset) noexcept {
    if(!ValidCamera(camera)||!head.orientationValid||!head.positionValid||
       !NormalizedQuat(head.orientation)||!Finite(head.position)) return;
    entries[next]={camera,head,now,yawOffset};next=(next+1)%unsigned(entries.size());
    if(count<entries.size()) ++count;
}
bool RenderCameraHistory::Match(const Matrix& view,std::int64_t now,std::int64_t maxAge,HmdSample& out,
                                float* yawOffset) const noexcept {
    // Animation matrices can be very slightly non-orthonormal. A transpose
    // accumulates centimetres of translation error far from world origin.
    Matrix camera{};if(!InvertCamera(view,camera))return false;
    float best=1e30f;bool found=false;
    // Newest first resolves identical static matrices without an artificial lag.
    for(unsigned n=0;n<count;++n) {
        const auto& entry=entries[(next+unsigned(entries.size())-1-n)%unsigned(entries.size())];
        if(now<entry.at || now-entry.at>maxAge) continue;
        float rot=0,pos=0;
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) {const float d=camera.m[i][j]-entry.camera.m[i][j];rot+=d*d;}
        for(int j=0;j<3;++j) {const float d=camera.m[3][j]-entry.camera.m[3][j];pos+=d*d;}
        if(rot>0.002f*0.002f || pos>0.02f*0.02f) continue;
        const float score=4*std::sqrt(rot)+std::sqrt(pos);
        if(score<best) {best=score;out=entry.head;found=true;if(yawOffset) *yawOffset=entry.yawOffset;}
    }
    return found;
}
bool RenderedEyePoses(const HmdSample& head,const XrSpaceLocation& latest,const XrView* views,
                     XrViewStateFlags flags,unsigned count,XrPosef out[2]) noexcept {
    constexpr XrSpaceLocationFlags required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
    constexpr XrViewStateFlags viewRequired=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    if(!views||count!=2||(latest.locationFlags&required)!=required||(flags&viewRequired)!=viewRequired||
       !head.orientationValid||!head.positionValid||!NormalizedQuat(head.orientation)||!Finite(head.position)) return false;
    const Quat current=AsQuat(latest.pose.orientation);
    const Vec3 position{latest.pose.position.x,latest.pose.position.y,latest.pose.position.z};
    if(!NormalizedQuat(current)||!Finite(position)) return false;
    const Quat delta=Multiply(head.orientation,Conjugate(current));
    XrPosef poses[2]{};
    for(unsigned i=0;i<2;++i) {
        const auto& eye=views[i].pose;const Quat eyeQ=AsQuat(eye.orientation);
        const Vec3 offset{eye.position.x-position.x,eye.position.y-position.y,eye.position.z-position.z};
        if(!NormalizedQuat(eyeQ)||!Finite(offset)||offset.x*offset.x+offset.y*offset.y+offset.z*offset.z>0.25f) return false;
        Quat q=Multiply(delta,eyeQ);
        const float norm=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
        q={q.x/norm,q.y/norm,q.z/norm,q.w/norm};
        const Vec3 relative=QuatRotate(delta,offset);
        poses[i]={{q.x,q.y,q.z,q.w},{head.position.x+relative.x,head.position.y+relative.y,head.position.z+relative.z}};
    }
    out[0]=poses[0];out[1]=poses[1];return true;
}
bool ParallelRenderedEyePoses(const HmdSample& head,float physicalIpd,XrPosef out[2]) noexcept {
    if(!out || !head.orientationValid || !head.positionValid ||
       !NormalizedQuat(head.orientation) || !Finite(head.position) ||
       !std::isfinite(physicalIpd) || physicalIpd<=0 || physicalIpd>0.25f) return false;
    const auto& input=head.orientation;
    const float norm=std::sqrt(input.x*input.x+input.y*input.y+input.z*input.z+input.w*input.w);
    const Quat q{input.x/norm,input.y/norm,input.z/norm,input.w/norm};
    const Vec3 half=QuatRotate(q,Vec3{physicalIpd*0.5f,0,0});
    XrPosef poses[2]{};
    for(unsigned eye=0;eye<2;++eye) {
        const float sign=eye?1.0f:-1.0f;
        const Vec3 p{head.position.x+sign*half.x,head.position.y+sign*half.y,head.position.z+sign*half.z};
        if(!Finite(p)) return false;
        poses[eye]={{q.x,q.y,q.z,q.w},{p.x,p.y,p.z}};
    }
    out[0]=poses[0];out[1]=poses[1];return true;
}
void ConfigureRenderedPose(bool on) noexcept {
    enabled=false;ResetRenderedPoseHistory();LARGE_INTEGER f{};QueryPerformanceFrequency(&f);frequency=f.QuadPart;enabled=on;
}
void ResetRenderedPoseHistory() noexcept {
    AcquireSRWLockExclusive(&historyLock);history={};++generation;ReleaseSRWLockExclusive(&historyLock);
    selected={};
}
bool RenderedPoseEnabled() noexcept {return enabled.load();}
void RecordRenderedCamera(const Matrix& camera,const HmdSample& head,float yawOffset) noexcept {
    if(!enabled) return;
    // Separate lock from HookUpdate's g_lock; no callbacks while it is held.
    AcquireSRWLockExclusive(&historyLock);history.Record(camera,head,PerfNow(),yawOffset);ReleaseSRWLockExclusive(&historyLock);
}
void BeginNativeRenderPose() noexcept {selected={};selected.generation=generation.load();}
void PublishRenderedCamera(Matrix& destination,const Matrix& camera,const HmdSample& head,float yawOffset) noexcept {
    RecordRenderedCamera(camera,head,yawOffset);
    destination=camera;
}
void ObserveNativeRenderView(const Matrix& view) noexcept {
    if(!enabled) return;
    selected.valid=false;selected.yaw=std::numeric_limits<float>::quiet_NaN();const auto now=PerfNow();
    AcquireSRWLockShared(&historyLock);
    if(selected.generation==generation.load()) selected.valid=history.Match(view,now,frequency/2,selected.head,&selected.yaw);
    ReleaseSRWLockShared(&historyLock);selected.at=now;
}
bool ConsumeNativeRenderPose(HmdSample& out) noexcept {
    const bool valid=enabled && selected.valid && selected.generation==generation.load() && PerfNow()-selected.at<frequency/4;
    if(valid) out=selected.head;
    selected={};return valid;
}
bool NativeRenderYaw(float& yawOffset) noexcept {
    if(!enabled || !selected.valid || selected.generation!=generation.load() ||
       PerfNow()-selected.at>=frequency/4 || !std::isfinite(selected.yaw)) return false;
    yawOffset=selected.yaw;return true;
}
}
