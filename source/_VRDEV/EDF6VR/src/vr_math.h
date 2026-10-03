#pragma once
#include "camera_math.h"

namespace edf6vr {
struct Quat { float x=0,y=0,z=0,w=1; };
struct Vec3 { float x=0,y=0,z=0; };

// OpenXR uses +X right, +Y up, -Z forward; EDF6 measures world yaw as
// atan2(forward.x, forward.z), so the head has to be turned to face +Z.
// The mapping is the half turn about Y, diag(-1,1,-1), and it must be a
// rotation rather than a mirror: a mirror reverses the sense of horizontal
// rotation, which is exactly the inverted yaw seen in the 2026-09-08 in-game
// test (research/runtime/VR_0.3.0_CONFIRMED.md). Any other rotation about Y
// would behave identically, since recentring absorbs the constant.
Vec3 XrToGame(const Vec3&) noexcept;
Vec3 RotateY(const Vec3&, float radians) noexcept;
// Actual soldier aim back into tracking space. pitchUp is positive upwards.
Vec3 AimToReference(float worldYaw,float pitchUp,float yawOffset) noexcept;
float BinocularFov(float verticalFov,float magnification) noexcept;
Vec3 QuatRotate(const Quat&, const Vec3&) noexcept;
bool NormalizedQuat(const Quat&) noexcept;

struct HeadBasis {
    float yaw=0;        // radians, game convention, before the recentering offset
    float pitchUp=0;    // radians, positive when looking up
    Vec3 forward{};     // game axes
    Vec3 up{};          // game axes
};
// Returns false for a non-finite or non-unit quaternion.
bool HeadBasisFromXr(const Quat&, HeadBasis&) noexcept;

// The headset's own recenter (SteamVR's long press, the Quest's, ...) moves the
// runtime's LOCAL space against STAGE, and nothing else does: both are fixed
// otherwise. So a step between two samples of LOCAL located in STAGE, beyond
// these, is one (openxr_session: WatchRuntimeRecenter). q and -q are the same
// turn; non-finite input is not a step.
bool ReferenceSpaceJumped(const Vec3& positionA,const Quat& a,const Vec3& positionB,const Quat& b,
                          float metres=0.02f,float degrees=2.0f) noexcept;

// a then b, as rotations: QuatRotate(QuatMultiply(a,b),v)==QuatRotate(a,QuatRotate(b,v)).
Quat QuatMultiply(const Quat& a,const Quat& b) noexcept;
// The one-handed weapon trim as a turn of the controller itself, in its own
// axes: yawRight about its up (positive to the player's right), then pitchUp
// about its right (positive upward). The result rides on the controller the
// way a part bolted to it would, so it means the same thing pointed at the
// horizon, straight up or back over the shoulder. Added to the aim's yaw and
// pitch instead, the trim was a turn about the WORLD's axes, which is not
// one fixed relation to the hand: near vertical it swung round the hand, and
// the drawn weapon rolled with it.
Quat TrimAimRotation(const Quat& aim,float yawRight,float pitchUp) noexcept;

// Weapon placement uses the palm's grip frame, swung onto the final aim and
// then roll-trimmed. The transverse offset is in game tracking-space axes.
bool WeaponGripOffset(const Quat& grip,const Vec3& aim,float right,float up,
    float roll,Vec3& offset) noexcept;
// Correct only the lateral support attachment after the original two-hand
// soft-limit/smoothing calculation. Pitch, forward.y and up stay untouched.
// The caller still applies its original yaw/pitch trim afterwards. The grip
// rotation only supplies the weapon's sideways axis; supportGap remains in
// the original aim-pose frame. No grip-origin or model-height correction.
bool CorrectTwoHandLateralYaw(const Quat& grip,const Vec3& supportGap,float right,
    float roll,float yawTrim,float pitchTrim,HeadBasis& aim) noexcept;

// Roll the camera around its own forward row so that its up row leans with the
// head. Forward (row 2) is bit-identical, so the game's aim is untouched.
bool RollCameraToUp(const Matrix& input, const Vec3& upWorld, Matrix& output, float& rollRadians) noexcept;

// Turn the camera about its own right row until its forward row is horizontal,
// and report how far it had to turn. A vehicle's native camera frames the
// machine from behind and above, so it looks down at it; once the eye has been
// moved into the cabin that pitch is no longer framing anything and the player
// is simply left staring at the ground. Right (row 0) is bit-identical, so any
// slope roll the seat carries is kept.
bool LevelCameraPitch(const Matrix& input, Matrix& output, float& pitchRadians) noexcept;

// Signed permutation candidates that map the game's move-input axes (x,z) onto
// the yaw-local world axes. The live mapping is identified from the player's own
// walking instead of being assumed.
inline constexpr int kMoveBasisCount=8;
struct MoveBasis {
    // world = scale * R(yaw) * A * input, with A one of the 8 signed permutations.
    int candidate=-1;
    float scale=0;      // metres per second at full input
    float score[kMoveBasisCount]{};
    float speedSum=0;
    int samples=0;
    bool locked=false;
};
void MoveBasisApply(int candidate,float inX,float inZ,float& outX,float& outZ) noexcept;
void MoveBasisApplyInverse(int candidate,float x,float z,float& inX,float& inZ) noexcept;
// One observation: the move input the game consumed and the yaw-local world
// velocity that followed. Ignores samples that are too small to be informative.
void MoveBasisObserve(MoveBasis&,float inX,float inZ,float localVx,float localVz) noexcept;
bool MoveBasisSolve(MoveBasis&) noexcept;

// Feed-forward controller: converts a wanted world velocity into move input.
// Never scales up beyond the stock walk input so the game keeps its own speed cap.
bool RoomScaleInput(const MoveBasis&,float yaw,float wantWorldX,float wantWorldZ,
                    float& inX,float& inZ) noexcept;
}
