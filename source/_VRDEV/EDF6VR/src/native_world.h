#pragma once
#include "camera_math.h"
#include "cockpit.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace edf6vr {
using NativeWorldLog=void(*)(const char*);
// Requests are published by the logic camera; image ownership stays entirely
// on the render thread. Inactive requests expire if camera updates stop.
void ConfigureNativeWorld(bool enabled,NativeWorldLog log=nullptr) noexcept;
bool NativeWorldEnabled() noexcept;
void RefreshNativeWorld(bool live,float eyeSeparation,float physicalIpd=0) noexcept;
bool NativeWorldLive() noexcept;
float NativeWorldSeparation() noexcept;
int NativeWorldRenderEye() noexcept;
const CockpitPose* NativeWorldCockpit() noexcept; // main viewport only; immutable across this eye pair
std::uint64_t NativeWorldRenderFrame() noexcept;
void BeginNativeWorldEye(std::uint64_t frame,unsigned eye) noexcept;
void BeginNativeWorldView(unsigned mode,bool refreshFromViewer) noexcept;
void ObserveNativeWorldView(const Matrix&) noexcept;
void ObserveNativeWorldProjection(const Matrix&) noexcept;
void EndNativeWorldView() noexcept;
void EndNativeWorldEye(std::uint64_t frame,unsigned eye,ID3D11DeviceContext*,ID3D11Texture2D*) noexcept;
void CancelNativeWorldEye(std::uint64_t frame) noexcept;
struct NativeWorldImages {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> eye[2];
    std::uint64_t frame=0;
    float physicalIpd=0;
    CockpitPose cockpit{};
    Matrix view[2]{},projection[2]{};
    bool cockpitMatched=false;
    bool attempted=false,ready=false;
};
// Both eyes must be from the same producer loop, dimensions, and camera.
// A failed pair is explicitly reported: it must never enter the depth warp.
NativeWorldImages TakeNativeWorldImages(unsigned width,unsigned height) noexcept;
// Render thread only. Discards the pair; device-owned cockpit resources survive
// a transient heartbeat timeout so recovery does not restart costly setup.
void ClearNativeWorldImages() noexcept;
}
