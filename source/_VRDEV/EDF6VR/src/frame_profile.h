#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace edf6vr {
// CPU wall time only. Resolve/Copy measure command submission, NOT GPU execution.
enum class PerfStage : std::size_t {
    PresentInterval, PresentHook, Deliver, NativePresent,
    Present1Interval, Present1Hook, Deliver1, NativePresent1,
    RenderFrame, DisplayFrame, DisplayOther,
    NativeResolve, NativeProcess,
    NativeMain, NativeFar, NativeShadow, NativeOther,
    NativeCommand0, NativeCommand1, NativeCommand2, NativeCommand3, NativeCommand4,
    NativeCommand5, NativeCommand6, NativeCommand7, NativeCommand8, NativeCommand9, NativeCommandOther,
    PollEvents, WaitFrame, BeginFrame, LocatePublish, SwapchainSetup,
    AcquireImage, WaitImage, ResolveSetup, ResolveSubmit, CopySubmit, WarpEye,
    ReleaseImage, EndFrame, MirrorCompose, Count
};
constexpr std::size_t kPerfStages=static_cast<std::size_t>(PerfStage::Count);
struct PerfStat {
    std::uint64_t count=0;
    std::int64_t ticks=0, maxTicks=0;
    void Add(std::int64_t elapsed) noexcept;
};
struct PerfBatch {
    std::array<PerfStat,kPerfStages> stages{};
    void Add(PerfStage stage,std::int64_t ticks) noexcept {
        stages[static_cast<std::size_t>(stage)].Add(ticks);
    }
};
struct PerfSnapshot {
    PerfBatch batch{};
    std::uint64_t droppedBatches=0;
    double ticksToMs=0;
};
std::int64_t PerfNow() noexcept;
const char* PerfName(PerfStage stage) noexcept;
// No allocation, logging, or waiting for the reporting thread on the render path.
void SubmitPerf(const PerfBatch& batch) noexcept;
PerfSnapshot DrainPerf() noexcept;

template<class F>
auto MeasureCpu(PerfBatch& batch,PerfStage stage,F call) noexcept -> decltype(call()) {
    struct End {
        PerfBatch& batch;
        PerfStage stage;
        std::int64_t start;
        ~End() noexcept { batch.Add(stage,PerfNow()-start); }
    } end{batch,stage,PerfNow()};
    return call();
}

// Includes early returns. Other = total minus disjoint stages PollEvents..EndFrame.
struct DisplayPerf {
    PerfBatch batch{};
    std::int64_t start=PerfNow();
    ~DisplayPerf() noexcept;
};
}
