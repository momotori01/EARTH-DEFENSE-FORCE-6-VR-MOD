#pragma once
#include "weapon_layer.h"
namespace edf6vr {
// One-shot GPU geometry experiment. Native draws always run once; only the
// selected local weapon's DrawIndexed calls are replayed, without model reentry.
void ConfigureWeaponStereoCapture(bool,const wchar_t*,CaptureLogger,unsigned settleMs=8000,bool lighting=false) noexcept;
// Called AFTER the native lighting Dispatch by the existing guarded call hook.
void ReplayWeaponStereoLighting(ID3D11DeviceContext*,UINT,UINT,UINT) noexcept;
void ConfigureWeaponStereoLive(bool enabled) noexcept; // load-time, after capture configuration
bool WeaponStereoLive() noexcept;
// Current render camera offsets, in metres along the camera right axis.
void SetWeaponStereoEyes(float left,float right) noexcept;
// Origin used to place THIS draw's weapon, before the eye separation is added.
void SetWeaponStereoOrigin(const float centre[3],float nativeEyeOffset) noexcept;
// Native-pose weapons stay in their original draw's camera frame. Offsets are
// relative to that camera, not to an update-thread hand/eye anchor.
void SetWeaponStereoNativeCamera(float leftRelative,float rightRelative) noexcept;
bool BeginWeaponDepthIsolation(ID3D11DeviceContext*) noexcept;
void EndWeaponDepthIsolation() noexcept;
bool CompositeWeaponStereo(ID3D11DeviceContext*,ID3D11Texture2D*,unsigned eye) noexcept;
void FinishWeaponStereoFrame() noexcept; // every Present, including VR-off/menu paths
// How a finished live frame turned out, and whether the live path survives it.
//
// Live: both eyes got the private weapon. Warmup: native weapon still drawn,
// nothing was hidden, nothing can be missing. Incomplete: the native weapon was
// hidden but an eye never received the private one.
//
// One incomplete pair is a hitch -- a network stall or an XR frame that skipped
// an eye -- and must cost that frame only. Turning the path off for good on the
// first one is what brought back the walking wobble 0.79.0 had removed: the
// weapon went back to native drawing, whose camera origin is not the one the
// hand was placed against, for the rest of the session. Only a run of them with
// no complete pair in between means the path cannot work here.
enum class WeaponPairOutcome { Live, Warmup, Incomplete };
inline constexpr unsigned kWeaponPairStrikeLimit=30;
bool KeepWeaponStereo(WeaponPairOutcome outcome,unsigned& strikes) noexcept;
unsigned WeaponStereoSkippedPairs() noexcept;
// Every draw replayed into the private eyes since load; never reset.
unsigned long long WeaponStereoReplays() noexcept;
// Diagnostic: while a pass (0..3) is set on this thread, every DrawIndexed that
// reaches the material loop's two patched sites is counted against it. -1 off.
void SetWeaponSiteProbe(int pass) noexcept;
unsigned long long WeaponSiteProbeCount(int pass) noexcept;
bool WeaponStereoPairReady() noexcept;
void DiscardWeaponStereoFrame() noexcept; // native pair rejected, re-arm next frame
bool WeaponStereoCaptureWanted(bool appendModels=false) noexcept;
bool WeaponStereoCapturePending() noexcept;
bool BeginWeaponStereoCapture(ID3D11DeviceContext*,int pass,unsigned width,unsigned height,bool appendModels=false) noexcept;
void EndWeaponStereoCapture() noexcept;
void PollWeaponStereoCapture(ID3D11DeviceContext*) noexcept;
bool InWeaponStereoReplay() noexcept;
void DrawWeaponStereo(ID3D11DeviceContext*,UINT count,UINT start,INT base) noexcept;
bool InstallWeaponStereoHooks(const ImageProfile&) noexcept;
// The hands go into the same live layer as the weapon: its targets, its depth
// and its lighting, so fingers and grip occlude each other. Available while the
// layer is armed or recording; the hands may arrive before or after the weapon.
bool HandStereoAvailable(ID3D11DeviceContext*) noexcept;
bool ReplayHandStereo(ID3D11DeviceContext*,UINT count,UINT start,INT base) noexcept;
bool HandStereoDrawnThisFrame() noexcept;
}
