#pragma once
#include <d3d11.h>
#include <openxr/openxr.h>

namespace edf6vr {
enum class MirrorImage : unsigned { Left, Right, Ui, Reticle };
bool InsideDesktopMirror() noexcept;
// Copy while the XR image is acquired/waited. Never retain a runtime image or
// read it after xrReleaseSwapchainImage. Cached resources are private copies.
bool SnapshotMirror(ID3D11DeviceContext*,ID3D11Texture2D*,MirrorImage,DXGI_FORMAT) noexcept;
void InvalidateMirrorImage(MirrorImage) noexcept;
// `presentedAspect` is the width over height the desktop buffer will be shown
// at, which is the window's, not the buffer's. Present stretches one onto the
// other, so a rectangle composed to the buffer's own shape arrives distorted
// whenever the two differ -- as they do once the render rectangle is fitted to
// the headset and stops being 16:9. Zero keeps the buffer's own aspect.
bool RenderDesktopMirror(ID3D11DeviceContext*,ID3D11Texture2D* desktop,MirrorImage world,
    const XrPosef& eye,const XrFovf&,const XrPosef& head,
    const XrCompositionLayerQuad* ui,const XrCompositionLayerQuad* reticle,float horizontalFovDegrees=0,
    float presentedAspect=0) noexcept;
void ReleaseDesktopMirror() noexcept;
// How much larger the HUD panel is drawn on the desktop than in the headset,
// about its own centre. The desktop only: the headset's panel, and the game,
// never see it.
void SetDesktopMirrorUiScale(float scale) noexcept;
}
