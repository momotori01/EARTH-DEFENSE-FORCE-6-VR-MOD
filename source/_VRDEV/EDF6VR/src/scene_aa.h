#pragma once
#include <d3d11.h>
namespace edf6vr {
void EnableSceneAA(bool) noexcept;
bool SceneAAEnabled() noexcept;
bool ToggleSceneAA() noexcept;
bool InsideSceneAA() noexcept;
// Render thread only. Filter an acquired, single-sample eye after the weapon
// composite and before UI/reticle submission and mirror snapshot. No history.
// enabled is frozen once per frame so both eyes agree on a hotkey transition.
bool ApplySceneAA(ID3D11DeviceContext*,ID3D11Texture2D*,bool enabled) noexcept;
// Render-thread diagnostic counters, no GPU readback or waits.
const char* SceneAAStatus() noexcept;
void ReleaseSceneAA() noexcept;
}
