#include "aim_hud.h"
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
namespace edf6vr {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
void* source=nullptr;
ULONGLONG sourceAt=0;
bool sourceFrames=false;
AimHudSnapshot latest{};
std::atomic<float> sizeScale{.5f};
using Prepare=void(__fastcall*)(void*,void*);
Prepare original=nullptr;
std::uint64_t calls=0,accepted=0,invalid=0;
// FA060: the record's sight matrix (rows right, up, forward, position at +0x150)
// and half angles (+0x190 across, +0x194 up); the corners are
// (+-1000 tan h, +-1000 tan v, 1000, 1) through that matrix.
bool Frame(const unsigned char* p,AimHudSnapshot& out) noexcept {
    if(out.frameCount>=2) return false;
    const auto* m=reinterpret_cast<const float*>(p+0x150);
    const float h=*reinterpret_cast<const float*>(p+0x190),v=*reinterpret_cast<const float*>(p+0x194);
    for(unsigned i=0;i<16;++i) if(!std::isfinite(m[i]) || std::fabs(m[i])>1000000) return false;
    if(!std::isfinite(h) || !std::isfinite(v) || h<=0 || v<=0 || h>1.5f || v>1.5f) return false;
    const float x=1000*std::tan(h),y=1000*std::tan(v);
    const float local[4][2]={{-x,-y},{x,-y},{x,y},{-x,y}};
    AimHudFrame frame{};frame.half[0]=h;frame.half[1]=v;
    for(unsigned c=0;c<4;++c) for(unsigned j=0;j<3;++j) {
        const float w=local[c][0]*m[j]+local[c][1]*m[4+j]+1000*m[8+j]+m[12+j];
        if(!std::isfinite(w) || std::fabs(w)>10000000) return false;
        frame.corners[c][j]=w;
    }
    out.frames[out.frameCount++]=frame;
    return true;
}
bool Point(const float* p,AimHudSnapshot& out,unsigned kind) noexcept {
    if(out.count==AimHudSnapshot::capacity) return false;
    for(unsigned j=0;j<3;++j) if(!std::isfinite(p[j]) || std::fabs(p[j])>1000000) return false;
    if(!std::isfinite(p[3]) || std::fabs(p[3]-1)>0.01f) return false;
    auto& target=out.points[out.count++];std::memcpy(target.world,p,sizeof(target.world));target.kind=kind;
    return true;
}
void __fastcall Prepared(void* camera,void* context) {
    original(camera,context); // exactly once; no lock crosses game code
    const auto now=GetTickCount64();
    AcquireSRWLockShared(&lock);
    const bool wanted=source==camera && now-sourceAt<250;
    ReleaseSRWLockShared(&lock);
    if(!wanted) return;
    AimHudSnapshot next{};
    const bool ok=ReadNativeAimHud(camera,next);next.at=now;
    AcquireSRWLockExclusive(&lock);
    ++calls;
    if(source==camera && now-sourceAt<250) {
        if(ok) {latest=next;++accepted;} else {latest={};++invalid;}
    }
    ReleaseSRWLockExclusive(&lock);
}
}
bool ReadNativeAimHud(const void* camera,AimHudSnapshot& out) noexcept {
    out={};
    __try {
        if(!Readable(camera,0xB00)) return false;
        auto bytes=static_cast<const unsigned char*>(camera);
        // FC620 copies both slots to render records; FA060 consumes these exact
        // buffers. No target search, lock timers, weapon transforms or writes.
        for(unsigned slot=0;slot<2;++slot) {
            auto p=bytes+0x720+slot*0x1F0;
            const auto count=*reinterpret_cast<const std::uint64_t*>(p+0x1E8);
            const auto capacity=*reinterpret_cast<const std::uint64_t*>(p+0x1E0);
            auto data=*reinterpret_cast<const unsigned char* const*>(p+0x1D8);
            if(count>capacity || count>1024 || (count && !Readable(data,static_cast<std::size_t>(count)*0x30))) {out={};return false;}
            out.lockWeapon|=p[0x140]!=0 || p[0x1B0]!=0 || count!=0;
            for(std::uint64_t i=0;i<count && out.count<AimHudSnapshot::capacity;++i) {
                auto entry=data+i*0x30;
                Point(reinterpret_cast<const float*>(entry+0x10),out,entry[0x21]?1u:0u);
            }
            if(p[0x1B0]) Point(reinterpret_cast<const float*>(p+0x1C0),out,2);
            if(p[0x140]) Frame(p,out);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {out={};return false;}
}
bool InstallAimHud(const ImageProfile& image,bool& changed) noexcept {
    changed=false;
    constexpr unsigned char prepare[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08};
    constexpr unsigned char copy[]={0x40,0x53,0x41,0x57,0x48,0x83,0xEC,0x28,0x0F,0xB6,0x81,0x10,0x01,0x00,0x00};
    if(!image.base || !Readable(image.base+0xFC620,sizeof(copy)) ||
       std::memcmp(image.base+0xFC620,copy,sizeof(copy)) ||
       !Readable(image.base+0xF8210,sizeof(prepare)) || std::memcmp(image.base+0xF8210,prepare,sizeof(prepare)) ||
       !Readable(image.base+kVtableRva+0x18,sizeof(void*))) return false;
    original=reinterpret_cast<Prepare>(image.base+0xF8210);
    return ReplacePointer(reinterpret_cast<void**>(image.base+kVtableRva+0x18),
        reinterpret_cast<void*>(original),reinterpret_cast<void*>(&Prepared),changed);
}
void SetAimHudSource(void* camera,bool active,float scale,bool frames) noexcept {
    AcquireSRWLockExclusive(&lock);
    void* next=active?camera:nullptr;
    if(source!=next) latest={};
    source=next;sourceAt=GetTickCount64();sourceFrames=active&&frames;
    ReleaseSRWLockExclusive(&lock);
    if(std::isfinite(scale) && scale>=.05f && scale<=4) sizeScale=scale;
}
AimHudSnapshot ReadAimHud() noexcept {
    AcquireSRWLockShared(&lock);
    AimHudSnapshot out{};
    const auto now=GetTickCount64();
    if(source && now-sourceAt<250 && now-latest.at<250) out=latest;
    if(!sourceFrames) out.frameCount=0;
    ReleaseSRWLockShared(&lock);return out;
}
float AimHudScale() noexcept {return sizeScale.load();}
bool ProjectAimHudAhead(const Matrix& view,const Matrix& projection,const float world[3],float ndc[2]) noexcept {
    float v[4]{},clip[4]{};
    for(unsigned j=0;j<4;++j) {
        v[j]=view.m[3][j];
        for(unsigned k=0;k<3;++k) v[j]+=world[k]*view.m[k][j];
    }
    v[0]=-v[0];v[2]=-v[2]; // EDF VIEW_CHANGED Ry(pi), identical to native world
    for(unsigned j=0;j<4;++j) for(unsigned k=0;k<4;++k) clip[j]+=v[k]*projection.m[k][j];
    for(float c:clip) if(!std::isfinite(c)) return false;
    if(clip[3]<=.001f) return false;
    ndc[0]=clip[0]/clip[3];ndc[1]=clip[1]/clip[3];
    return std::isfinite(ndc[0]) && std::isfinite(ndc[1]);
}
bool ProjectAimHud(const Matrix& view,const Matrix& projection,const float world[3],float ndc[2]) noexcept {
    float v[4]{},clip[4]{};
    for(unsigned j=0;j<4;++j) {
        v[j]=view.m[3][j];
        for(unsigned k=0;k<3;++k) v[j]+=world[k]*view.m[k][j];
    }
    v[0]=-v[0];v[2]=-v[2]; // EDF VIEW_CHANGED Ry(pi), identical to native world
    for(unsigned j=0;j<4;++j) for(unsigned k=0;k<4;++k) clip[j]+=v[k]*projection.m[k][j];
    for(float c:clip) if(!std::isfinite(c)) return false;
    if(clip[3]<=.001f) return false;
    ndc[0]=clip[0]/clip[3];ndc[1]=clip[1]/clip[3];
    return std::fabs(ndc[0])<1.05f && std::fabs(ndc[1])<1.05f;
}
const char* AimHudStatus() noexcept {
    thread_local char line[256]{};
    AcquireSRWLockShared(&lock);
    std::snprintf(line,sizeof(line),"AIMHUD prepare=%llu accepted=%llu invalid=%llu lockWeapon=%d points=%u frames=%u/%d half=(%.1f,%.1f)deg age=%llums scale=%.2f",
        calls,accepted,invalid,latest.lockWeapon,latest.count,latest.frameCount,sourceFrames?1:0,
        latest.frames[0].half[0]*57.29578f,latest.frames[0].half[1]*57.29578f,
        latest.at?GetTickCount64()-latest.at:0,sizeScale.load());
    ReleaseSRWLockShared(&lock);return line;
}
}
