#pragma once
#include <cstddef>

namespace edf6vr {
struct Matrix { float m[4][4]; };
// Whether a first person camera just written for a soldier still belongs to a
// mission, when the player's body was not drawn to say so.
//
// The body being drawn is what tells a mission from the title and the pause
// menu, but the game only draws a body it can see. Knocked down, blown away or
// dead and looking at the sky, the arms and legs are out of view, nothing is
// drawn, and the display fell back to the board every time the player looked
// around. The signal table from 0.8.x has the answer without the body: at the
// title no first person camera is ever written; in the pause menu the camera is
// still written but the soldier stops reading its input (922 ms stale when
// measured). A camera written while the input is being read is a mission, in
// whatever direction the player is facing. Times are GetTickCount64 values;
// zero means never.
inline constexpr unsigned long long kSceneSignalMs=750;
inline bool CameraKeepsSceneLive(unsigned long long cameraWrite,
                                 unsigned long long soldierInput) noexcept {
    if(!cameraWrite || !soldierInput) return false;
    const auto gap=cameraWrite>soldierInput?cameraWrite-soldierInput:soldierInput-cameraWrite;
    return gap<kSceneSignalMs;
}
static_assert(sizeof(Matrix) == 64);
bool ValidCamera(const Matrix& m) noexcept;
// Invert the actual affine basis, including small animation/float deviations
// from orthonormality. Transpose-only inversion magnifies them at world scale.
bool InvertCamera(const Matrix& input,Matrix& output) noexcept;
// Row-vector camera world basis. Positive yaw rotates local +Z toward +X.
// Positive pitch rotates local +Z toward +Y. Translation/W are preserved.
bool RotateCamera(const Matrix& input, float yawDegrees, float pitchDegrees,
                  Matrix& output) noexcept;
// Takes the animation out of the head without touching where the soldier goes.
//
// Two things move the first person eye. The soldier travels, and the animation
// swings his head about on top of that: a few centimetres of footstep bob a
// couple of times a second, and a lean back that grows as the weapon is aimed
// upwards. In a headset both of those are the room moving rather than the head,
// and with the weapon on a controller the lean is worse than the bob, because
// pointing the weapon at the sky has no business moving anyone's viewpoint.
//
// The two are easy to separate once the soldier's own transform is in hand: the
// animation is what moves in his frame while his frame stays still. So the head
// is expressed in that frame and smoothed there, and his travel is passed
// through untouched. Travel therefore has no lag at all, by construction rather
// than by tuning - an earlier version smoothed the world position and needed a
// velocity estimate fed forward to stop a five metre a second run lagging by
// more than a metre.
struct HeadSteady {
    float offset[3]{};   // in the soldier's frame
    bool primed=false;
};
// Freeze the initial eye offset in world axes, follow only root translation.
// Aiming/body animation and root rotation cannot orbit or tilt the viewpoint.
struct HeadAnchor { float offset[3]{}; bool primed=false; };
bool AnchorHead(HeadAnchor&,const Matrix& root,const float rawEye[3],float out[3]) noexcept;
// rawEye and out may be the same array. damping 0 leaves the head exactly as the
// game animates it and 1 takes the whole of the movement out. The view is never
// moved more than limitMetres from where the game put it, so a dodge roll or a
// vehicle cannot drag it. Returns how far the head was moved, in metres.
// aimPitchRadians is where the weapon is pointing. The reference offset only
// learns while the weapon is near level, because the lean is a standing offset
// rather than a wobble and any filter left free to follow it simply would.
// Holding it while the weapon is raised is what cancels the lean exactly.
float SteadyHead(HeadSteady&, const Matrix& root, const float rawEye[3],
                 float aimPitchRadians, float damping, float limitMetres,
                 float out[3]) noexcept;

// A big walker's walking heave, eased along one axis and nothing else.
//
// The cabin of a walking machine rides an animated bone, and each pair of steps
// lifts and drops it: the Barga's pelvis by 2.1 m once in 3.5 s (2.7 m walking
// backwards), the Depth Crawler's body by 4 cm three times a second, the Nix's
// by 20 cm (research/cockpit/walk_bob.py, from the game's own clips). The
// machine's own transform carries none of that -- it is where the machine
// stands and how it lies, a slope, a wall, a fall -- so the heave is the
// cabin's height over the machine's origin, along the machine's own up, and
// only that number is followed with a lag. Travel, the ground, every turn and
// tilt pass through untouched, by construction.
//
// `height` now; the return is what to add to it: the followed height minus the
// real one, never further than `limit` from it, so a jump, a landing or a fall
// drags the view by no more than that. A new owner, a gap of a quarter of a
// second or a clock that ran backwards starts again from the real height.
struct HeaveSteady { float height=0; double at=0; bool primed=false; const void* owner=nullptr; };
float SteadyHeave(HeaveSteady&,const void* owner,float height,double now,float seconds,float limit) noexcept;
// Build a camera basis pointing along `forward`, laid out and handed the same
// way as `like`, keeping its translation.
//
// This exists because turning a camera by a yaw and a pitch is not the same as
// aiming it. RotateCamera turns in the camera's own frame, so a yaw applied to
// an already pitched camera tilts as well as turns, and the error grows with the
// angle between the two. It also refuses a pitch past 85 degrees, and refusing
// leaves the camera where it was: point the weapon at the sky and the view went
// with it. Neither can happen when the basis is built rather than nudged.
//
// The handedness is taken from `like` rather than assumed, so nothing here
// depends on which way round the game's convention is.
bool LookAlong(const Matrix& like, const float forwardWorld[3], Matrix& out) noexcept;

struct SavedCamera {
    Matrix original{};
    Matrix applied{};
    bool pending = false;
    // Restore only if no other system has changed the matrix since our write.
    bool Restore(Matrix& current) noexcept;
    bool Apply(Matrix& current, float yaw, float pitch) noexcept;
};
}
