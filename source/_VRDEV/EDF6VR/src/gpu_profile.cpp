#include "gpu_profile.h"
#include <atomic>
#include <algorithm>
namespace edf6vr {
namespace {
struct Slot {
    ID3D11Query* disjoint=nullptr;
    ID3D11Query* stamps[kGpuStages*2]{};
    unsigned mask=0,width=0,height=0,steps=0;
    int phase=0;
    bool pending=false;
};
Slot slots[8]; Slot* active=nullptr;
ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr; // borrowed until reset
std::atomic<bool> enabled{false};
std::uint64_t serial=0; LONGLONG previous=0; int previousPhase=-1;
SRWLOCK lock=SRWLOCK_INIT;
GpuSnapshot result{};
void Add(GpuStat& stat,double ms) { ++stat.count; stat.ms+=ms; stat.maxMs=std::max(stat.maxMs,ms); }
void Release(Slot& s) {
    for(auto*& q:s.stamps) { if(q) q->Release(); q=nullptr; }
    if(s.disjoint) s.disjoint->Release(); s={};
}
bool Create(Slot& s) {
    if(s.disjoint) return true;
    D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
    if(FAILED(device->CreateQuery(&desc,&s.disjoint))) return false;
    desc.Query=D3D11_QUERY_TIMESTAMP;
    for(auto*& q:s.stamps) if(FAILED(device->CreateQuery(&desc,&q))) { Release(s); return false; }
    return true;
}
void Invalid() { if(TryAcquireSRWLockExclusive(&lock)) { ++result.invalid; ReleaseSRWLockExclusive(&lock); } }
}
const char* GpuName(GpuStage s) noexcept {
    constexpr const char* names[]={"vr.span","resolve","copy","otherEye","uiAndReticle","mirror.compose"};
    const auto i=static_cast<unsigned>(s); return i<kGpuStages?names[i]:"unknown";
}
void EnableGpuProfile(bool value) noexcept { enabled=value; }
void ResetGpuProfile() noexcept {
    if(active && context) EndGpuFrame();
    for(auto& s:slots) Release(s);
    active=nullptr; context=nullptr; device=nullptr; previous=0; previousPhase=-1; serial=0;
}
void PollGpuProfile() noexcept {
    if(!context) return;
    for(auto& s:slots) {
        if(!s.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        auto hr=context->GetData(s.disjoint,&disjoint,sizeof(disjoint),D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(hr==S_FALSE) continue;
        if(hr!=S_OK || disjoint.Disjoint || !disjoint.Frequency) { s.pending=false; Invalid(); continue; }
        UINT64 ticks[kGpuStages*2]{}; bool waiting=false,bad=false;
        for(unsigned i=0;i<kGpuStages*2;++i) {
            if(!(s.mask&(1u<<(i/2)))) continue;
            hr=context->GetData(s.stamps[i],&ticks[i],sizeof(ticks[i]),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            waiting|=hr==S_FALSE; bad|=FAILED(hr);
        }
        if(waiting && !bad) continue;
        s.pending=false;
        if(bad) { Invalid(); continue; }
        if(!TryAcquireSRWLockExclusive(&lock)) continue;
        auto& group=result.phase[s.phase]; group.width=s.width; group.height=s.height; group.steps=s.steps;
        for(unsigned i=0;i<kGpuStages;++i) if(s.mask&(1u<<i)) {
            if(ticks[i*2+1]>=ticks[i*2]) Add(group.gpu[i],static_cast<double>(ticks[i*2+1]-ticks[i*2])*1000.0/static_cast<double>(disjoint.Frequency));
            else ++result.invalid;
        }
        ReleaseSRWLockExclusive(&lock);
    }
}
void BeginGpuFrame(ID3D11Device* d,ID3D11DeviceContext* c,bool scene,unsigned w,unsigned h,unsigned steps,int phase) noexcept {
    if(active) return;
    if(device!=d || context!=c) { ResetGpuProfile(); device=d; context=c; }
    PollGpuProfile();
    if(!enabled || !scene || !d || !c || phase<0 || phase>4) { previous=0; previousPhase=-1; return; }
    LARGE_INTEGER now{},frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    if(TryAcquireSRWLockExclusive(&lock)) {
        auto& g=result.phase[phase]; g.width=w; g.height=h; g.steps=steps;
        if(previous && phase==previousPhase && frequency.QuadPart>0)
            Add(g.frames,static_cast<double>(now.QuadPart-previous)*1000.0/static_cast<double>(frequency.QuadPart));
        ReleaseSRWLockExclusive(&lock);
    }
    previous=now.QuadPart; previousPhase=phase;
    if((serial++%4)!=0) return; // Sample one in four frames, same rate in A and B.
    for(auto& s:slots) if(!s.pending) {
        if(!Create(s)) { Invalid(); return; }
        s.mask=0; s.width=w; s.height=h; s.steps=steps; s.phase=phase; active=&s;
        context->Begin(s.disjoint); BeginGpuStage(GpuStage::VrSpan); return;
    }
    if(TryAcquireSRWLockExclusive(&lock)) { ++result.skipped; ReleaseSRWLockExclusive(&lock); }
}
void BeginGpuStage(GpuStage stage) noexcept {
    const auto i=static_cast<unsigned>(stage);
    if(active && i<kGpuStages) context->End(active->stamps[i*2]);
}
void EndGpuStage(GpuStage stage) noexcept {
    const auto i=static_cast<unsigned>(stage);
    if(active && i<kGpuStages) { context->End(active->stamps[i*2+1]); active->mask|=1u<<i; }
}
void EndGpuFrame() noexcept {
    if(!active) return;
    EndGpuStage(GpuStage::VrSpan); context->End(active->disjoint); active->pending=true; active=nullptr;
}
GpuSnapshot DrainGpuProfile() noexcept {
    AcquireSRWLockExclusive(&lock); auto snapshot=result; result={}; ReleaseSRWLockExclusive(&lock); return snapshot;
}
}
