#pragma once
// Research (2026-10-01): where the GPU's frame goes when a fight gets heavy
// (the Kurul shotgun's sticky pellets, the Deroy). The render thread was seen
// polling Umbra's occlusion-query results 330-580 times a frame (PERF
// native.cmd6.queryResults, ~15 ms a frame), i.e. waiting on the GPU. This
// splits the GPU's own frame with timestamp queries: from one present to the
// next, and within it every run of model draws (the material loop's
// DrawIndexed, weapon_stereo's hook) by eye. What is not a model run is
// lighting, post and the effects (sprites) -- the part a Kurul fight should
// grow, where a Deroy should grow the model runs.
// Off unless [Diagnostics] GpuSplit=1. Render thread only, no waits: results
// are read a few frames later without flushing.
#include <d3d11.h>
#include <cstdint>
namespace edf6vr {
void EnableGpuSplit(bool on) noexcept;
bool GpuSplitEnabled() noexcept;
// Which eye the native world is producing (0, 1; -1 none), from the caller.
void SetGpuSplitEyeSource(int (*eye)() noexcept) noexcept;
// Once per present, before the frame is handed on: ends one frame, starts the next.
void GpuSplitPresent(ID3D11Device* device,ID3D11DeviceContext* context) noexcept;
// Around each model draw.
void GpuSplitDrawBegin(ID3D11DeviceContext* context) noexcept;
void GpuSplitDrawEnd(ID3D11DeviceContext* context) noexcept;
struct GpuSplitScope {
    ID3D11DeviceContext* context;
    explicit GpuSplitScope(ID3D11DeviceContext* c) noexcept:context(c) { GpuSplitDrawBegin(context); }
    ~GpuSplitScope() noexcept { GpuSplitDrawEnd(context); }
};
struct GpuSplitStats {
    std::uint64_t frames=0,skipped=0,disjoint=0;
    double gpuMs=0,gpuMaxMs=0;                   // present to present on the GPU
    double modelMs[3]{},runs[3]{},draws[3]{};     // by eye 0, 1, none
    double modelMaxMs=0,restMaxMs=0;             // the worst frame's model and the rest
};
GpuSplitStats DrainGpuSplit() noexcept;
}
