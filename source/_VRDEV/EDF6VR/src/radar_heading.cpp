#include "radar_heading.h"
#include <cmath>
#include <cstring>
namespace edf6vr {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
const void* owner=nullptr;
float heading=0;
ULONGLONG published=0;
RadarHeadingStats stats{};
using DirectionAngles=float* (__fastcall*)(float*,const float*);
DirectionAngles original=nullptr;
float* __fastcall RadarAngles(float* out,const float* direction,const void* soldier) {
    float* result=original(out,direction);
    ApplyRadarHeading(result,soldier,GetTickCount64());
    return result;
}
}
void PublishRadarHeading(const void* soldier,float yaw,ULONGLONG now) noexcept {
    AcquireSRWLockExclusive(&lock);
    owner=std::isfinite(yaw)?soldier:nullptr;
    heading=owner?std::remainder(yaw,6.28318530718f):0;
    published=now;
    ReleaseSRWLockExclusive(&lock);
}
void ClearRadarHeading() noexcept { PublishRadarHeading(nullptr,0,0); }
bool ApplyRadarHeading(float* angles,const void* soldier,ULONGLONG now) noexcept {
    AcquireSRWLockExclusive(&lock);
    ++stats.calls;
    const bool apply=angles && owner && owner==soldier && now>=published && now-published<250;
    if(apply) {
        stats.nativeYaw=angles[1]; stats.headYaw=heading; ++stats.applied;
        // The radar uses only yaw. A horizontal pitch also bypasses its native
        // pole fallback at 82B743, which would otherwise overwrite our yaw.
        angles[0]=0; angles[1]=heading;
    }
    ReleaseSRWLockExclusive(&lock);
    return apply;
}
RadarHeadingStats ReadRadarHeadingStats() noexcept {
    AcquireSRWLockShared(&lock); const auto result=stats; ReleaseSRWLockShared(&lock);
    return result;
}
bool InstallRadarHeading(const ImageProfile& image,bool& changed) noexcept {
    changed=false;
    if(!image.base) return false;
    // Validate the complete call and stack-local argument setup. RSI is the
    // SoldierBase returned by the dynamic_cast at 82B598, held by native code.
    constexpr unsigned char expected[]={0x48,0x8D,0x55,0xF0,0x48,0x8D,0x4D,0xB0,
        0xE8,0x20,0x2A,0x82,0xFF,0xF3,0x0F,0x10,0x4D,0xB0};
    if(!Readable(image.base+kRadarAngleCall-8,sizeof(expected)) ||
       std::memcmp(image.base+kRadarAngleCall-8,expected,sizeof(expected))) return false;
    void* expectedDraw=reinterpret_cast<void*>(image.base+0x82B4D0);
    if(!Readable(image.base+0x17FCF20,8) ||
       *reinterpret_cast<void**>(image.base+0x17FCF20)!=expectedDraw) return false;
    // Windows x64: add RSI as third (volatile R8) argument, preserving RCX/RDX,
    // all nonvolatile registers, flags and the caller's existing shadow space.
    // No borrowed game memory and no TLS/lock spanning the native draw call.
    unsigned char code[]={0x49,0x89,0xF0,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    auto target=reinterpret_cast<void*>(&RadarAngles);
    std::memcpy(code+5,&target,8);
    auto* bridge=VirtualAlloc(nullptr,sizeof(code),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!bridge) return false;
    std::memcpy(bridge,code,sizeof(code));
    DWORD old=0;
    if(!VirtualProtect(bridge,sizeof(code),PAGE_EXECUTE_READ,&old)) {
        VirtualFree(bridge,0,MEM_RELEASE); return false;
    }
    FlushInstructionCache(GetCurrentProcess(),bridge,sizeof(code));
    original=reinterpret_cast<DirectionAngles>(image.base+kDirectionAngles);
    const bool ok=RedirectCall(image.base+kRadarAngleCall,reinterpret_cast<void*>(original),bridge,changed);
    if(!changed) VirtualFree(bridge,0,MEM_RELEASE);
    // Published executable bridges remain resident, including protection errors.
    return ok;
}
}
