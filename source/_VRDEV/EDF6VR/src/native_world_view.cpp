#include "native_world_view.h"
#include <Windows.h>
#include <cmath>

namespace edf6vr {
namespace {
SRWLOCK cacheLock=SRWLOCK_INIT;
NativeWorldViewCache cache;
bool Fresh(std::uint64_t now,std::uint64_t at) noexcept {
    return now>=at && now-at<=NativeWorldViewCache::kMaxAgeMs;
}
bool ValidIpd(float ipd) noexcept {
    // EDF's current world scale is one unit per metre. Refuse corruption or
    // legacy world-scale multipliers; do not silently clamp either eye.
    return std::isfinite(ipd) && ipd>0.0f && ipd<=0.25f;
}
}
bool ValidNativeWorldCamera(const Matrix& m) noexcept {
    for(const auto& row:m.m) for(float v:row) if(!std::isfinite(v)) return false;
    for(unsigned i=0;i<3;++i) if(std::fabs(m.m[i][3])>0.0001f) return false;
    if(std::fabs(m.m[3][3]-1.0f)>0.0001f) return false;
    // A view/camera must be rigid; scaled world/model matrices must not change
    // the physical IPD. Small float roundoff from native matrix products is OK.
    for(unsigned a=0;a<3;++a) for(unsigned b=a;b<3;++b) {
        float dot=0;
        for(unsigned j=0;j<3;++j) dot+=m.m[a][j]*m.m[b][j];
        if(std::fabs(dot-(a==b?1.0f:0.0f))>0.002f) return false;
    }
    const float determinant=
        m.m[0][0]*(m.m[1][1]*m.m[2][2]-m.m[1][2]*m.m[2][1])-
        m.m[0][1]*(m.m[1][0]*m.m[2][2]-m.m[1][2]*m.m[2][0])+
        m.m[0][2]*(m.m[1][0]*m.m[2][1]-m.m[1][1]*m.m[2][0]);
    return std::fabs(determinant-1.0f)<=0.004f;
}
void NativeWorldViewCache::Reset() noexcept { cameras={}; }
void NativeWorldViewCache::Record(void* pointer,const Matrix& matrix,std::uint64_t now) noexcept {
    if(!pointer) return;
    Camera* entry=nullptr;
    Camera* vacant=nullptr;
    Camera* oldest=&cameras[0];
    for(auto& camera:cameras) {
        if(camera.camera==pointer) {entry=&camera;break;}
        if(!camera.camera && !vacant) vacant=&camera;
        if(camera.touchedAt<oldest->touchedAt) oldest=&camera;
    }
    if(!entry) {
        entry=vacant?vacant:oldest;
        *entry={};entry->camera=pointer;
    }
    entry->latestValid=ValidNativeWorldCamera(matrix);
    if(entry->latestValid) entry->latest=matrix;
    entry->latestAt=entry->touchedAt=now;
    // A new original is for future pairs. Never change the current baseline.
}
bool NativeWorldViewCache::Prepare(void* pointer,std::uint64_t frame,unsigned eye,float ipd,
                                   std::uint64_t now,Matrix& original,Matrix& shifted) noexcept {
    if(!pointer || frame==0 || eye>1 || !ValidIpd(ipd) || &original==&shifted) return false;
    Camera* entry=nullptr;
    for(auto& camera:cameras) if(camera.camera==pointer) {entry=&camera;break;}
    if(!entry) return false;
    if(eye==0 && (!entry->pairValid || entry->frame!=frame)) {
        if(frame<=entry->frame || !entry->latestValid || !Fresh(now,entry->latestAt)) return false;
        entry->baseline=entry->latest;entry->frame=frame;entry->pairAt=now;
        entry->ipd=ipd;entry->pairValid=true;
    }
    if(!entry->pairValid || entry->frame!=frame || !Fresh(now,entry->pairAt)
       || std::fabs(entry->ipd-ipd)>0.000001f) return false;
    Matrix eyeMatrix=entry->baseline;
    if(eye==1) for(unsigned j=0;j<3;++j)
        eyeMatrix.m[3][j]-=entry->baseline.m[0][j]*entry->ipd;
    if(!ValidNativeWorldCamera(eyeMatrix)) return false;
    original=entry->baseline;shifted=eyeMatrix;entry->touchedAt=now;
    return true;
}
void RecordNativeWorldCamera(void* camera,const Matrix& matrix) noexcept {
    const auto now=GetTickCount64();
    AcquireSRWLockExclusive(&cacheLock);
    cache.Record(camera,matrix,now);
    ReleaseSRWLockExclusive(&cacheLock);
}
bool PrepareNativeWorldEye(void* camera,std::uint64_t frame,unsigned eye,float ipd,
                           Matrix& original,Matrix& shifted) noexcept {
    const auto now=GetTickCount64();
    AcquireSRWLockExclusive(&cacheLock);
    const bool valid=cache.Prepare(camera,frame,eye,ipd,now,original,shifted);
    ReleaseSRWLockExclusive(&cacheLock);
    return valid;
}
void ResetNativeWorldCameras() noexcept {
    AcquireSRWLockExclusive(&cacheLock);cache.Reset();ReleaseSRWLockExclusive(&cacheLock);
}
}
