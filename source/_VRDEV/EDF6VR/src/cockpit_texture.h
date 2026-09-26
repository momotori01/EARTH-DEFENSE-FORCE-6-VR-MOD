#pragma once
#include <d3d11.h>
#include <wrl/client.h>
namespace edf6vr {
// PNG resources authored in the user's designated ChatGPT texture task.
bool LoadCockpitTexture(ID3D11Device*,unsigned id,bool srgb,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>&) noexcept;
}
