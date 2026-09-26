#pragma once
#include <cstddef>

// Poses from a text file instead of from a headset.
//
// Nearly every question asked of this mod so far has cost a launch, a headset,
// a mission and a walk to a wall, and the answer has usually been one number.
// That is the wrong price. With the poses coming from a file, the hand can be
// put somewhere exact, the trigger held, and the log read -- and the file can
// be rewritten between one second and the next without restarting anything.
//
// What this does not do is make the game unnecessary. It replaces the player's
// arms, not the game: EDF6 still has to be running for there to be a soldier, a
// weapon and a shot. And it says nothing about how a thing feels to hold, which
// stays a question only a person can answer.
//
// The file is read at most once a second and every key is optional; whatever is
// missing is left exactly as the runtime gave it, so a file with one line in it
// changes one thing. Metres and degrees, in the runtime's own reference space.
//
//   note     = walking forward, hand held still
//   hmd      = 0, 1.70, 0
//   hmdRot   = 0, 0, 0          ; pitch, yaw, roll
//   right    = 0.30, 1.35, -0.45
//   rightRot = -5, 20, 0
//   left     = -0.20, 1.30, -0.40
//   leftRot  = 0, 0, 0
//   triggerR = 1
//   triggerL = 0
//   gripR    = 0
//   stickL   = 0, 1              ; x, y
//   aR = 1   bR = 0   menuR = 0   clickR = 0    ; and the same with L
namespace edf6vr {

struct HmdSample;
struct ControllerState;

// Where to read it from, and whether to read it at all. Called once at startup.
void ArmScriptedPoses(const wchar_t* path) noexcept;
bool ScriptedPosesArmed() noexcept;
// What the file last said, for the log.
const char* ScriptedPoseNote() noexcept;

// Overwrite whatever the runtime reported. All no-ops when unarmed.
//
// The hand is asked for rather than handed over, because the runtime's own hand
// type is private to the session and this has no business knowing it. Each flag
// says whether the script had anything to say about that half of the pose, so a
// file naming only a rotation leaves the position where the runtime put it.
void ScriptHead(HmdSample& sample) noexcept;
bool ScriptHand(int hand,float position[3],float orientation[4],
                bool& gavePosition,bool& gaveTurn) noexcept;
void ScriptControls(ControllerState& controls) noexcept;

}
