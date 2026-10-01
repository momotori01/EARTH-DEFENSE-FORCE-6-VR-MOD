#include "gpu_split.h"
#include <Windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned kSlots=6,kRuns=12;
struct Run { int eye=-1; unsigned draws=0; ComPtr<ID3D11Query> first,last; };
struct Slot {
    ComPtr<ID3D11Query> disjoint,start,end;
    std::array<Run,kRuns> runs; unsigned runCount=0;
    bool open=false,pending=false;
};
std::atomic<bool> enabled{false};
int (*eyeSource)() noexcept=nullptr;
std::array<Slot,kSlots> slots;
Slot* current=nullptr;
ID3D11Device* queryDevice=nullptr;
ID3D11DeviceContext* frameContext=nullptr;   // the immediate context the frame is presented from
SRWLOCK statsLock=SRWLOCK_INIT;
GpuSplitStats stats;
bool MakeQuery(ID3D11Device* device,D3D11_QUERY kind,ComPtr<ID3D11Query>& out) noexcept {
    if(out) return true;
    D3D11_QUERY_DESC desc{kind,0};
    return SUCCEEDED(device->CreateQuery(&desc,&out));
}
// The oldest pending frame, if the GPU has finished it.
void Collect(ID3D11DeviceContext* context) noexcept {
    for(auto& slot:slots) {
        if(!slot.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
        if(context->GetData(slot.disjoint.Get(),&clock,sizeof(clock),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
        UINT64 start=0,end=0;
        if(context->GetData(slot.start.Get(),&start,sizeof(start),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||
           context->GetData(slot.end.Get(),&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
        double model[3]{},runs[3]{},draws[3]{};bool ok=!clock.Disjoint&&clock.Frequency>0&&end>=start;
        for(unsigned r=0;r<slot.runCount&&ok;++r) {
            UINT64 a=0,b=0;
            if(context->GetData(slot.runs[r].first.Get(),&a,sizeof(a),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||
               context->GetData(slot.runs[r].last.Get(),&b,sizeof(b),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) { ok=false; break; }
            const int e=slot.runs[r].eye==0?0:slot.runs[r].eye==1?1:2;
            if(b>=a) model[e]+=1000.0*static_cast<double>(b-a)/static_cast<double>(clock.Frequency);
            runs[e]+=1; draws[e]+=slot.runs[r].draws;
        }
        slot.pending=false;
        AcquireSRWLockExclusive(&statsLock);
        if(!ok) ++stats.disjoint;
        else {
            const double gpu=1000.0*static_cast<double>(end-start)/static_cast<double>(clock.Frequency);
            const double models=model[0]+model[1]+model[2];
            ++stats.frames;stats.gpuMs+=gpu;stats.gpuMaxMs=std::max(stats.gpuMaxMs,gpu);
            for(int e=0;e<3;++e){stats.modelMs[e]+=model[e];stats.runs[e]+=runs[e];stats.draws[e]+=draws[e];}
            if(gpu>=stats.modelMaxMs+stats.restMaxMs){stats.modelMaxMs=models;stats.restMaxMs=gpu-models;}
        }
        ReleaseSRWLockExclusive(&statsLock);
    }
}
}
void EnableGpuSplit(bool on) noexcept { enabled.store(on,std::memory_order_relaxed); }
bool GpuSplitEnabled() noexcept { return enabled.load(std::memory_order_relaxed); }
void SetGpuSplitEyeSource(int (*eye)() noexcept) noexcept { eyeSource=eye; }
void GpuSplitPresent(ID3D11Device* device,ID3D11DeviceContext* context) noexcept {
    if(!device||!context) return;
    if(!enabled.load(std::memory_order_relaxed)) {
        if(current){current->open=false;current=nullptr;}
        return;
    }
    if(queryDevice&&queryDevice!=device) { for(auto& slot:slots) slot=Slot{}; current=nullptr; }
    queryDevice=device;frameContext=context;
    if(current) {   // this frame ends here
        context->End(current->end.Get());context->End(current->disjoint.Get());
        current->open=false;current->pending=true;current=nullptr;
    }
    Collect(context);
    for(auto& slot:slots) {
        if(slot.pending||slot.open) continue;
        if(!MakeQuery(device,D3D11_QUERY_TIMESTAMP_DISJOINT,slot.disjoint)||!MakeQuery(device,D3D11_QUERY_TIMESTAMP,slot.start)||
           !MakeQuery(device,D3D11_QUERY_TIMESTAMP,slot.end)) return;
        slot.runCount=0;
        context->Begin(slot.disjoint.Get());context->End(slot.start.Get());
        slot.open=true;current=&slot;return;
    }
    AcquireSRWLockExclusive(&statsLock);++stats.skipped;ReleaseSRWLockExclusive(&statsLock);   // every slot still waiting
}
void GpuSplitDrawBegin(ID3D11DeviceContext* context) noexcept {
    if(!current||context!=frameContext||!enabled.load(std::memory_order_relaxed)) return;
    const int eye=eyeSource?eyeSource():-1;
    Run* run=current->runCount?&current->runs[current->runCount-1]:nullptr;
    if(run&&run->eye==eye) return;
    if(current->runCount>=kRuns||!queryDevice) return;   // a frame with more runs keeps adding to the last
    run=&current->runs[current->runCount];
    if(!MakeQuery(queryDevice,D3D11_QUERY_TIMESTAMP,run->first)||!MakeQuery(queryDevice,D3D11_QUERY_TIMESTAMP,run->last)) return;
    run->eye=eye;run->draws=0;++current->runCount;
    context->End(run->first.Get());
}
void GpuSplitDrawEnd(ID3D11DeviceContext* context) noexcept {
    if(!current||context!=frameContext||!current->runCount||!enabled.load(std::memory_order_relaxed)) return;
    auto& run=current->runs[current->runCount-1];
    ++run.draws;context->End(run.last.Get());   // the last End before the frame ends wins
}
GpuSplitStats DrainGpuSplit() noexcept {
    AcquireSRWLockExclusive(&statsLock);
    const auto out=stats;stats=GpuSplitStats{};
    ReleaseSRWLockExclusive(&statsLock);
    return out;
}
}
