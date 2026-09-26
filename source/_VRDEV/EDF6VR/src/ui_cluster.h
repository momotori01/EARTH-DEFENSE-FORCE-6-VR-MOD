#pragma once
#include <d3d11.h>
namespace edf6vr {
// The compact HUD: pieces of the game's HUD picture (radar, armour, weapons)
// cut out of the head-locked panel and drawn again, smaller, close together on
// a small picture of their own that hangs at the panel's corner or on a wrist.
//
// Rectangles are fractions of the HUD picture as the game lays it out (a 16:9
// screen), so they hold at every render size. The cluster canvas is measured
// in the same units: a canvas 0.35 wide is 35% of the HUD's width.
struct UiRect { float u0=0,v0=0,u1=0,v1=0; };
struct UiClusterItem {
    UiRect source;      // what to copy
    float x=0,y=0;      // where its top-left lands on the canvas, 0..1 of the canvas
    float scale=1.0f;   // size relative to the HUD picture (0.5 = half)
};
struct UiClusterLayout {
    UiClusterItem items[8];
    unsigned count=0;
    float canvasWidth=0.35f,canvasHeight=0.32f;   // in HUD-picture units
};
// Clears target and copies every item's source rectangle into it, scaled;
// pixels are copied as they are (colour and alpha), not blended.
bool ComposeUiCluster(ID3D11Device*,ID3D11DeviceContext*,ID3D11Texture2D* source,ID3D11Texture2D* target,
                      const UiClusterLayout&) noexcept;
// Makes the rectangles fully transparent in the panel image.
bool CutUiRects(ID3D11Device*,ID3D11DeviceContext*,ID3D11Texture2D* target,const UiRect* rects,unsigned count) noexcept;
void ReleaseUiCluster() noexcept;
}
