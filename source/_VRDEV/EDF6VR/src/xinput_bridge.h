#pragma once
#include <Windows.h>
#include "image_profile.h"
#include "render_capture.h"

namespace edf6vr {
// Hands EDF6 a gamepad built from the VR controllers.
//
// EDF6 reads a pad through XInput, resolved with GetProcAddress rather than
// imported, so the way in is GetProcAddress itself: it is a normal import of
// EDF.dll and therefore one aligned pointer in the IAT, which ReplacePointer can
// swap the same way it swaps a vtable slot. When the game asks for
// XInputGetState it gets ours; everything else is forwarded untouched.
//
// Doing it here rather than by writing buttons into the soldier means every
// control the game already supports arrives at once, and the player's own key
// bindings keep working, because what reaches the game is an ordinary pad.
//
// The state below is merged with whatever a real pad is reporting, so a physical
// controller still works alongside the headset.
struct PadState {
    unsigned short buttons=0;   // XINPUT_GAMEPAD_* flags
    unsigned char leftTrigger=0, rightTrigger=0;
    short leftX=0, leftY=0, rightX=0, rightY=0;
    bool valid=false;           // false leaves the real pad alone entirely
};

bool InstallXInputBridge(const ImageProfile&,CaptureLogger) noexcept;
bool XInputBridgeInstalled() noexcept;
// For when the game resolved XInput before this plugin loaded, so the IAT swap
// never sees the request. Looks for the resolved pointers in EDF6 writable data
// and swaps them there. Does nothing once the swap is known to be working.
int SweepResolvedXInput(const ImageProfile&,CaptureLogger) noexcept;
// The sweep is off unless this says otherwise. See the note on it: it patches
// copies passing through stacks and costs gigabytes of scanning per pass.
void AllowSweep(bool on) noexcept;
// Which xinput libraries the process holds at this moment. Asked before this
// plugin loads one itself, it says whether the game uses XInput at all.
void ReportXInputModules(const char* when,CaptureLogger) noexcept;
// Overwrite the XInput entry points in every loaded xinput library except one,
// which is kept for reading the real device. Catches the game however and
// whenever it resolved the address, which the import table and the pointer
// search both failed to do.
bool PatchXInputExports(CaptureLogger) noexcept;
// Which module each read of the pad returns into, logged once per distinct
// caller. The point is to find out whether the game is reading at all: an
// overlay polls XInput too, and buttons handed to an overlay reach nothing.
void ReportXInputCallers(CaptureLogger) noexcept;
// Whether to answer "yes, there is a controller here" on the headset's behalf.
// On by default; off leaves the question to the real hardware.
void AllowCapabilities(bool) noexcept;
// Whether this mod intends to supply a pad at all. Independent of whether the
// controllers have been read yet, because the reading is what it unlocks.
void OfferPad(bool) noexcept;
// Read the real device through a private copy of an XInput library instead of a
// system one. The game cannot resolve into a name it does not know, which is what
// decides whether the title screen sees a VR controller.
void UsePrivateSpare(bool) noexcept;
// Whether a physical controller should keep working alongside the headset.
// Keeping one working means leaving an XInput library unpatched for reading it,
// and that library is one the game may resolve through -- which is why the title
// screen sometimes ignored the VR controllers. Off, nothing is spared.
void WantRealPad(bool) noexcept;
// What the game has asked for, and what it is being told. For the log.
const char* XInputBridgeStatus() noexcept;
// Called from the VR side each frame.
void SetPadState(const PadState&) noexcept;
// Called whenever the game asks for the pad, so the state is fresh even when the
// game's own update hooks have stopped, which is what happens in a menu.
using PadProvider=void(*)();
void SetPadProvider(PadProvider) noexcept;
// What the game most recently asked the pad to do, 0 to 1 per motor. Cleared as
// it is read, so a caller sees each request once.
bool TakeRumble(float& left,float& right) noexcept;
}
