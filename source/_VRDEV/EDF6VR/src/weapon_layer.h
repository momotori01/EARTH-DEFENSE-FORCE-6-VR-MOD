#pragma once
#include <d3d11.h>
#include "render_capture.h"
#include "image_profile.h"
namespace edf6vr {
// Stage 1: one native weapon submission, one private target set, one saved image.
// Does not replay the model, render both eyes, or enable permanent compositing.
void ConfigureWeaponLayerCapture(bool enabled,const wchar_t* directory,CaptureLogger logger,unsigned settleMs=8000,bool lighting=false) noexcept;
// Only called from the two byte-validated native lighting Dispatch sites.
// Native Dispatch always runs once, then optionally the one-shot private pass.
void DispatchWeaponLighting(ID3D11DeviceContext*,UINT x,UINT y,UINT z) noexcept;
bool InstallWeaponLightingCaptureHooks(const ImageProfile&) noexcept;
bool WeaponLayerCaptureWanted() noexcept;
bool WeaponLayerCapturePending() noexcept;
bool BeginWeaponLayerCapture(ID3D11DeviceContext*,int pass,unsigned width,unsigned height) noexcept;
void EndWeaponLayerCapture() noexcept;
void PollWeaponLayerCapture(ID3D11DeviceContext* immediate) noexcept;
// Shared with the depth/UI survey: private OM binds must not be mistaken for
// native scene transitions. Applies only on this calling thread.
bool InWeaponLayerCapture() noexcept;
}
