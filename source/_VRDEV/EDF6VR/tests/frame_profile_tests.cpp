#include "frame_profile.h"
#include <cstdio>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
static const edf6vr::PerfStat& Stat(const edf6vr::PerfSnapshot& snapshot,edf6vr::PerfStage stage) {
    return snapshot.batch.stages[static_cast<std::size_t>(stage)];
}
static void PartialFrame() {
    edf6vr::DisplayPerf frame;
    edf6vr::MeasureCpu(frame.batch,edf6vr::PerfStage::PollEvents,[]() {});
    // Session not running: never wait/acquire/copy. The total must still be counted.
}
int main() {
    using namespace edf6vr;
    DrainPerf();
    PerfBatch first{},second{};
    first.Add(PerfStage::WaitFrame,10);
    first.Add(PerfStage::WaitFrame,30);
    second.Add(PerfStage::WaitFrame,20);
    second.Add(PerfStage::CopySubmit,5);
    second.Add(PerfStage::CopySubmit,-1); // invalid timing is not a valid sample
    SubmitPerf(first);
    SubmitPerf(second);
    const auto snapshot=DrainPerf();
    CHECK(Stat(snapshot,PerfStage::WaitFrame).count==3);
    CHECK(Stat(snapshot,PerfStage::WaitFrame).ticks==60);
    CHECK(Stat(snapshot,PerfStage::WaitFrame).maxTicks==30);
    CHECK(Stat(snapshot,PerfStage::CopySubmit).count==1);
    CHECK(Stat(snapshot,PerfStage::ResolveSubmit).count==0);
    CHECK(snapshot.droppedBatches==0 && snapshot.ticksToMs>0);
    const auto empty=DrainPerf();
    for(const auto& stat:empty.batch.stages) CHECK(stat.count==0 && stat.ticks==0 && stat.maxTicks==0);
    PartialFrame();
    const auto partial=DrainPerf();
    CHECK(Stat(partial,PerfStage::DisplayFrame).count==1);
    CHECK(Stat(partial,PerfStage::PollEvents).count==1);
    CHECK(Stat(partial,PerfStage::WaitFrame).count==0);
    CHECK(Stat(partial,PerfStage::DisplayOther).count==1);
    CHECK(Stat(partial,PerfStage::DisplayFrame).ticks==
          Stat(partial,PerfStage::PollEvents).ticks+Stat(partial,PerfStage::DisplayOther).ticks);
    PerfBatch returns{};
    CHECK(MeasureCpu(returns,PerfStage::AcquireImage,[]() { return -7; })==-7);
    CHECK(returns.stages[static_cast<std::size_t>(PerfStage::AcquireImage)].count==1);
    printf("frame profiling: %d failures\n",failures);
    return failures?1:0;
}
