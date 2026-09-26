#include "frame_profile.h"
#include <atomic>

namespace edf6vr {
namespace {
SRWLOCK g_perfLock=SRWLOCK_INIT;
PerfBatch g_pending{};
std::atomic<std::uint64_t> g_dropped{0};
constexpr const char* kNames[]={
    "present.interval", "present.hook", "present.deliver", "present.native",
    "present1.interval", "present1.hook", "present1.deliver", "present1.native",
    "xr.render", "xr.display", "xr.other",
    "native.resolveVisibility", "native.processVisibility",
    "native.main", "native.far", "native.shadow", "native.other",
    "native.cmd0.begin", "native.cmd1.end", "native.cmd2.viewer", "native.cmd3.depth", "native.cmd4.objects",
    "native.cmd5.queryIssue", "native.cmd6.queryResults", "native.cmd7", "native.cmd8", "native.cmd9.lines", "native.cmdOther",
    "xr.poll", "xr.waitFrame", "xr.beginFrame", "xr.locatePublish", "xr.swapchainSetup",
    "xr.acquire", "xr.waitImage", "d3d.resolveSetup", "d3d.resolveSubmit", "d3d.copySubmit", "d3d.warpEye",
    "xr.release", "xr.endFrame", "mirror.compose"
};
static_assert(sizeof(kNames)/sizeof(kNames[0])==kPerfStages);
}
void PerfStat::Add(std::int64_t elapsed) noexcept {
    if(elapsed<0) return;
    ++count;
    ticks+=elapsed;
    if(elapsed>maxTicks) maxTicks=elapsed;
}
std::int64_t PerfNow() noexcept {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}
const char* PerfName(PerfStage stage) noexcept {
    const auto index=static_cast<std::size_t>(stage);
    return index<kPerfStages?kNames[index]:"unknown";
}
void SubmitPerf(const PerfBatch& batch) noexcept {
    if(!TryAcquireSRWLockExclusive(&g_perfLock)) {
        g_dropped.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    for(std::size_t i=0;i<kPerfStages;++i) {
        auto& out=g_pending.stages[i];
        const auto& in=batch.stages[i];
        out.count+=in.count;
        out.ticks+=in.ticks;
        if(in.maxTicks>out.maxTicks) out.maxTicks=in.maxTicks;
    }
    ReleaseSRWLockExclusive(&g_perfLock);
}
PerfSnapshot DrainPerf() noexcept {
    PerfSnapshot result{};
    AcquireSRWLockExclusive(&g_perfLock);
    result.batch=g_pending;
    g_pending={};
    result.droppedBatches=g_dropped.exchange(0,std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&g_perfLock);
    LARGE_INTEGER frequency{};
    if(QueryPerformanceFrequency(&frequency) && frequency.QuadPart>0)
        result.ticksToMs=1000.0/static_cast<double>(frequency.QuadPart);
    return result;
}
DisplayPerf::~DisplayPerf() noexcept {
    const auto elapsed=PerfNow()-start;
    std::int64_t accounted=0;
    for(std::size_t i=static_cast<std::size_t>(PerfStage::PollEvents);i<kPerfStages;++i)
        accounted+=batch.stages[i].ticks;
    batch.Add(PerfStage::DisplayFrame,elapsed);
    batch.Add(PerfStage::DisplayOther,elapsed-accounted);
    SubmitPerf(batch);
}
}
