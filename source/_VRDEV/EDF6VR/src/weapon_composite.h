#pragma once
#include <d3d11.h>
namespace edf6vr {
// Lit linear RGB + normal.a coverage over a finished world eye. No world depth.
bool CompositeWeaponImage(ID3D11Device*,ID3D11DeviceContext*,ID3D11ShaderResourceView* lit,
    ID3D11ShaderResourceView* normal,ID3D11Texture2D* target) noexcept;
void ReleaseWeaponComposite() noexcept;
}
