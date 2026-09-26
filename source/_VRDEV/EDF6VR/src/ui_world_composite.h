#pragma once
#include <d3d11.h>
namespace edf6vr {
// A premultiplied HUD image laid over a finished world eye, stretched to the
// eye image. The two share the game camera's projection, so the HUD lands where
// the game put it relative to the world. No depth: it is always on top.
// scale stretches the overlay about the centre and offsetY moves it down (in
// image heights); 1,1,0 lays it exactly over the eye. What falls outside the
// overlay leaves the world untouched.
bool CompositeOverlayImage(ID3D11Device*,ID3D11DeviceContext*,ID3D11Texture2D* overlay,ID3D11Texture2D* target,
                           float scaleX=1.0f,float scaleY=1.0f,float offsetY=0.0f,bool debugTint=false) noexcept;
void ReleaseOverlayComposite() noexcept;
}
