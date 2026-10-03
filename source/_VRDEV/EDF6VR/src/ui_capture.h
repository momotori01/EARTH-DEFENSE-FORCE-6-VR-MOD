#pragma once
#include <d3d11.h>
namespace edf6vr {
// Render-thread capture of native HUD draws; no extra game draw/replay.
bool InstallUiCapture(ID3D11DeviceContext*) noexcept;
bool InsideUiCapture() noexcept;
// Ignore compositor target/state changes after FinishUiCapture. They are not
// native HUD draws and must not leave swapchain references held into next frame.
struct PauseUiCapture { bool previous; PauseUiCapture() noexcept; ~PauseUiCapture() noexcept; };
void EnableUiCapture(bool) noexcept;
void UiCaptureTargets(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,bool backBuffer) noexcept;
void UiCaptureGate() noexcept;
void FinishUiCapture() noexcept;
ID3D11Texture2D* CapturedUi() noexcept;
// World-anchored HUD (player nameplates, follower health, rescue prompts).
// Draws issued inside a world scope go to a separate target, with no depth
// buffer bound so nothing hides them, and are composited into the world eye
// images at the game camera's own projection instead of onto the HUD panel --
// so a nameplate sits over the head it belongs to. Scopes nest; call from the
// render thread around the game's own draw of such a HUD object. Every target
// the game binds inside the scope is replaced, and its own binding is put back
// on exit.
void UiCaptureWorldScope(bool enter) noexcept;
void EnableWorldUi(bool on) noexcept;
ID3D11Texture2D* CapturedWorldUi() noexcept;
struct WorldUiStats { unsigned long long scopes=0,scopesOffThread=0,binds=0,frames=0,exitsLost=0; };
WorldUiStats ReadWorldUiStats() noexcept;
// Why scopes did not redirect, what the game drew into, and (with the probe
// on, a read-back every 5 s) where in the world image the plates landed.
void ReadWorldUiNotes(char* out,size_t size) noexcept;
// The radio subtitles: draws inside a subtitle scope go to a target of their
// own (as the world scope's do), for a cockpit's subtitle screen. Only while
// MarkSubtitleUi has been called in the last half second (a cockpit with that
// screen is in use); otherwise the subtitles stay on the HUD.
void UiCaptureSubtitleScope(bool enter) noexcept;
void EnableSubtitleUi(bool on) noexcept;
void MarkSubtitleUi() noexcept;
bool SubtitleUiActive() noexcept;
ID3D11Texture2D* CapturedSubtitleUi() noexcept;
struct SubtitleUiStats { unsigned long long scopes=0,binds=0,frames=0; };
SubtitleUiStats ReadSubtitleUiStats() noexcept;
void EnableWorldUiProbe(bool on) noexcept;
ID3D11DeviceContext* UiCaptureContext() noexcept;
const char* UiCaptureStatus() noexcept;
}
