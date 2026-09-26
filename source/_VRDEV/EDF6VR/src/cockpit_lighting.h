#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
namespace edf6vr {
struct CockpitLighting {
    Microsoft::WRL::ComPtr<ID3D11Buffer> system,extra,environment;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cubes,grid;
};
// Render thread only, at the verified main-view native lighting dispatch.
// GPU copies freeze light constants until both cockpit eyes are composed.
bool CaptureCockpitLighting(ID3D11DeviceContext*,std::uint64_t frame,UINT x,UINT y,UINT z) noexcept;
CockpitLighting GetCockpitLighting(ID3D11DeviceContext*,std::uint64_t frame) noexcept;
void DiscardCockpitLighting() noexcept;
void ReleaseCockpitLighting() noexcept;
}
