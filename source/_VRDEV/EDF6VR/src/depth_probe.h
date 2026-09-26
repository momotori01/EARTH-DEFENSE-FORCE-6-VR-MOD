#pragma once
#include <Windows.h>
#include "render_capture.h"

struct ID3D11Texture2D;

namespace edf6vr {
// Read-only survey of the depth targets the game binds, to find the one the
// main scene is drawn into. Deriving the second eye by warping the first needs
// that buffer, so this decides whether the method is possible at all.
//
// The technique follows ReShade's generic_depth addon: watch which
// depth-stencil view is bound, count the draws issued while it is bound, and
// choose among textures drawn in the current frame (after dimension checks).
// One OM hook observes bindings; EDF's model hook attributes draws. The same
// module also holds pre-HUD colour and the established UI redirection path.
//
// Off unless the INI asks for it. Install only after the game has its device.
bool InstallDepthProbe(CaptureLogger) noexcept;
bool DepthProbeInstalled() noexcept;
// Called from EDF6's own model draw hook. D3D11 rewrites the draw entries in
// its context dispatch table as its state changes, so counting draws there
// does not hold; the game's own draw does.
void NoteModelDraw() noexcept;
// The depth the scene is drawn into: the backbuffer-shaped target EDF6's own
// model draws land in during the completed frame. Null if none was drawn.
// Borrowed on the render thread between NoteBackBuffer calls only; never Release.
// Inactive textures are reclaimed at later frame boundaries.
ID3D11Texture2D* SceneDepth() noexcept;
// The scene colour as it stood when the scene pass ended, which is before the
// UI was composited onto it. Null unless it matches this frame and scene row.
ID3D11Texture2D* SceneColour() noexcept;
// The swapchain image the game is presenting, told to the probe once a frame so
// it can recognise it among the targets that get bound.
void NoteBackBuffer(ID3D11Texture2D*) noexcept;
// Legacy difference-based UI extraction only. Native world + direct RGBA UI
// needs neither the pre-UI ring nor its synchronous classifier/scene copy.
// Thread-safe request; applies at the next NoteBackBuffer frame boundary.
// Disabling immediately hides legacy images; enabling collects fresh images
// starting in the following frame. HUD gate/bind counts and SceneDepth remain.
// Default true preserves legacy fallback and explicit diagnostic captures.
void SetLegacyPreUiCapture(bool enabled) noexcept;
// The presented image as it stood when the last pass to draw into it was bound.
// The UI is the last thing drawn, so this is the finished picture with no UI on
// it, in the same space as the finished frame: no tone curve between them and
// no threshold to pick, because the difference is only what the UI put there.
// Null until one has been taken.
ID3D11Texture2D* PreUiColour() noexcept;
// How many times the presented image was bound during the last frame.
unsigned PreUiBinds() noexcept;
// Which snapshot was chosen and how much each shares with the finished frame.
const char* PreUiNote() noexcept;

// Capture native 2D HUD after the current frame's camera HUD gate. A bound
// DSV is retained; depth-tested world markers go to the original render target.
// Sample counts/quality match the native targets. The completed RGBA image is
// published at Present; without a captured frame the legacy fallback remains.
void SetUiRedirect(bool on) noexcept;
// The HUD on its own, with alpha, or null when the redirect is not carrying it.
ID3D11Texture2D* UiTexture() noexcept;
const char* UiRedirectNote() noexcept;
// How many snapshots the last frame produced, and each of them, so a dump can
// carry all of them and the choice can be judged off the game rather than in it.
int PreUiHeld() noexcept;
ID3D11Texture2D* PreUiSnapshot(int) noexcept;
// Called from the hook on EDF6 own "should the HUD be drawn" virtual. Records
// which snapshot the game was on when it asked, which is where the HUD begins.
void NoteUiGate(bool drawing) noexcept;
// Calls to that gate during the last frame, and the snapshot index of the first
// one that answered yes, or -1.
const char* UiGateNote() noexcept;

// Writes one line per depth target seen since the last call, then clears the
// table. Safe to call from any thread; call it from the game's log tick.
void ReportDepthProbe(CaptureLogger) noexcept;
}
