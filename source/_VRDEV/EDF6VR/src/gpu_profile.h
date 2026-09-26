#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
namespace edf6vr {
enum class GpuStage : unsigned { VrSpan, Resolve, Copy, Warp, Ui, Mirror, Count };
constexpr unsigned kGpuStages=static_cast<unsigned>(GpuStage::Count);
struct GpuStat { std::uint64_t count=0; double ms=0,maxMs=0; };
struct GpuGroup { std::array<GpuStat,kGpuStages> gpu{}; GpuStat frames{}; unsigned width=0,height=0,steps=0; };
struct GpuSnapshot { std::array<GpuGroup,5> phase{}; std::uint64_t skipped=0,invalid=0; };
void EnableGpuProfile(bool) noexcept;
void BeginGpuFrame(ID3D11Device*,ID3D11DeviceContext*,bool scene,unsigned width,unsigned height,unsigned steps,int phase) noexcept;
void EndGpuFrame() noexcept;
void BeginGpuStage(GpuStage) noexcept;
void EndGpuStage(GpuStage) noexcept;
// Poll once, without Flush or waits. Call only on the same render thread.
void PollGpuProfile() noexcept;
void ResetGpuProfile() noexcept;
GpuSnapshot DrainGpuProfile() noexcept;
const char* GpuName(GpuStage) noexcept;
struct GpuScope { GpuStage stage; explicit GpuScope(GpuStage s) noexcept:stage(s) { BeginGpuStage(stage); } ~GpuScope() noexcept { EndGpuStage(stage); } };
}
