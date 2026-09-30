#include "motion_trace.h"
#include "render_pose.h"
#include "native_world.h"
#include "native_world_view.h"
#include "native_world_queue.h"
#include "native_world_composite.h"
#include "scene_aa.h"
#include <Windows.h>
#include <intrin.h>
#pragma warning(push)
#pragma warning(disable: 4201) // Upstream PluginVersion intentionally uses an anonymous struct.
#include <PluginAPI.h>
#pragma warning(pop)
#include "image_profile.h"
#include "resource_path_log.h"
#include "ranger_dash.h"
#include "resolution_lock.h"
#include "nameplate_scale.h"
#include "radar_heading.h"
#include "laser_sight.h"
#include "native_crosshair.h"
#include "hand_model.h"
#include "hand_draw.h"
#include "weapon_kick.h"
#include "fencer_aim.h"
#include "ini_defaults.h"
#include "hand_rest_data.inc"
#include "aim_hud.h"
#include "weapon_layer.h"
#include "weapon_stereo.h"
#include "camera_math.h"
#include "first_person.h"
#include "fencer_input.h"
#include "body_tumble.h"
#include "gesture_input.h"
#include "ranger_holster.h"
#include "ranger_recoil.h"
#include "desktop_mirror.h"
#ifdef EDF6VR_AUDIO_RESEARCH
#include "input_continuity.h"
#endif
#include "vehicle_camera.h"
#include "cockpit_draw.h"
#include "diagnostic_points.h"
#include "weapon_hold.h"
#include "vr_math.h"
#include "openxr_session.h"
#include "scripted_pose.h"
#include "render_capture.h"
#include "depth_probe.h"
#include "ui_capture.h"
#include "frame_dump.h"
#include "xinput_bridge.h"
#include "frame_profile.h"
#include "gpu_profile.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <thread>
#include <Xinput.h>

namespace {
using Update=void (__fastcall*)(void*,const void*);
HMODULE g_module=nullptr;
edf6vr::ImageProfile g_image{};
Update g_original=nullptr;
using ModelDraw=void (__fastcall*)(void*,void*,int,void*);
ModelDraw g_modelOriginal=nullptr;
bool g_earlyXrFrame=false;
bool g_nativeStereoProbe=false; // immutable after Settings, before hooks
bool g_nativeWorldRequested=true;
thread_local float g_nativeProducerIpd=0;
std::atomic<bool> g_nativeStereoProbeActive{false};
// EDF6 own answer to whether the HUD should be drawn. Only listened to for now:
// the answer is passed back untouched, so nothing about the game changes.
using UiGate=bool(__fastcall*)(void*);
UiGate g_uiGateOriginal=nullptr;
bool g_uiGateReady=false;
edf6vr::NodeLookup g_nodeLookup=nullptr;
// EDF.dll+570650: fills the local player's move/look input for this frame.
using InputRead=void (__fastcall*)(void*,void*,float);
InputRead g_inputOriginal=nullptr;
SRWLOCK g_lock=SRWLOCK_INIT;
wchar_t g_logPath[MAX_PATH]{};
wchar_t g_iniPath[MAX_PATH]{};
wchar_t g_modDirectory[MAX_PATH]{};
bool g_enabled=false, g_faulted=false;
bool g_fpsEnabled=false, g_fpsReady=false, g_hideBody=true;
edf6vr::EyeSettings g_eyeSettings{};
struct BodyTarget {
    void* camera=nullptr;
    edf6vr::PlayerPose pose{};
    ULONGLONG refreshed=0;
    bool handsOff=false;   // a vehicle's passenger seat: the body goes, nothing in its place
};
BodyTarget g_bodyTarget{};
std::atomic<ULONGLONG> g_drawCalls{0}, g_bodySkipped{0};
std::atomic<DWORD> g_drawThread{0};   // the thread the game issues model draws on
ULONGLONG g_fpsApplied=0;
float g_yaw=15, g_pitch=0, g_speed=30;
constexpr int kKeyCount=7;   // F6..F12
bool g_limiterKey=false;
// F2, F3, F4, F6, F7, F8 and F10 are development keys. Off by default so other
// mods can have them; Diagnostics/DevKeys=1 brings them back.
bool g_devKeys=false;
bool g_dumpKey=false;
bool g_moveKey=false;
int g_windowCorner=0;
void MoveGameWindow() noexcept;
// Reaching for a key to get into VR every time is not how this should end up.
// Once the game is up and the runtime answers, turn it on. Never at load:
// building anything before the renderer exists is what stopped the game from
// starting in 0.4.0, so this waits the same way the present hook does.
bool g_vrAutoStart=true;
bool g_vrUserStopped=false;
ULONGLONG g_autoStartTried=0;
ULONGLONG g_loaded=GetTickCount64();
unsigned g_autoStartAttempts=0;     // F5, polled on its own
bool g_keys[kKeyCount]{};
ULONGLONG g_inputTime=0, g_reportTime=0, g_calls=0, g_applied=0, g_rejected=0;
// seen: when the game last brought this camera through an update. A live
// camera comes back every frame, so a stale entry is one the game has
// destroyed -- see State().
struct CameraState { void* camera=nullptr; edf6vr::SavedCamera saved{}; ULONGLONG seen=0; };
CameraState g_states[8]{};
// How long an entry must go unasked-for before its slot can be taken. Far
// longer than a frame, and longer than the six-second stalls the loading
// screens produce, but short enough that the next mission gets its slot.
constexpr ULONGLONG kCameraSlotStaleMs=5000;

// --- VR ---
bool g_vrReady=false;          // aim profile verified and the input call site redirected
bool g_vrEnabled=false;        // F11
bool g_vrRotation=true, g_vrRoomScale=true, g_vrCameraHeight=true, g_vrMouseYaw=true;
int g_captureFrames=1;
bool g_measureWithoutVr=true;
// 0 quad (the head-locked screen since 0.5.0), 1 world projection with one
// image in both eyes, 2 the same but alternating eyes for real parallax.
int g_stereoMode=0;
bool g_swapEyes=false;
float g_ipdScale=1.0f;
// 1: alternate the eye with the camera update, which is what actually bakes
// the offset into the image. 0: the old present-keyed behaviour, kept only
// so the two can be compared.
int g_eyeSource=1;
float g_warpSteps=64;
float g_warpNearest=0.15f;
float g_nearKnee=1.5f, g_nearScale=1.0f;
bool g_uiRedirect=true;
// The footstep wobble on the head bone, and what is left of it after damping.
edf6vr::HeadSteady g_headSteady{};
edf6vr::HeadAnchor g_headAnchor{};
bool g_headRootLock=true;
// [LeftHanded] LeftHanded: Rangers, Wing Divers and Air Raiders hold and fire the
// gun with the left controller (openxr_session SetHandSwap). The sticks change
// sides too (hardware 2026-09-29: with movement left and reload on the left hand,
// one thumb had both); LeftHandedSticks=0 keeps movement on the left stick.
bool g_leftHanded=false,g_leftHandedSticks=true;
// The gun's sideways trim, mirrored when the gun is in the left hand.
float WeaponRightTrim() noexcept;
// [FirstPerson] ViewFollowsBody (body_tumble.h): off = the view keeps level, as always.
bool g_viewFollowsBody=false;
constexpr float kTumbleUprightDeg=25, kTumbleStartDeg=45, kTumbleFullDeg=75;
constexpr double kTumbleWindowSec=0.35;   // past kTumbleStartDeg this soon after leaving upright, or no turn
edf6vr::BodyTumble g_bodyTumble{};
void* g_bodyTumbleOwner=nullptr;
bool g_bodyTumbling=false;          // last frame put a turn onto the view
unsigned long long g_tumbleFrames=0,g_tumbleEngaged=0;
float g_tumbleAngleMax=0,g_tumbleAppliedMax=0;
// The game's state names seen while the body was tipped, for choosing states later.
char g_tumbleStates[8][96]{};
unsigned g_tumbleStateCount=0;
void* g_headAnchorOwner=nullptr;
unsigned g_headAnchorId=0;
float g_idleRootStepMax=0;
unsigned g_idleRootSamples=0;
float g_headDamping=0.85f, g_headLimit=0.15f, g_headWobble=0;
// Where the room scale walk is measured from. Real movement inside the dead zone
// moves nobody.
float g_roomAnchor[2]{}; bool g_roomAnchored=false;
float g_roomLean[2]{};   // world x and z, how far the view leans from the body
// The weapon points where the right controller points, and the view goes where
// the head goes. Until now those were the same thing.
bool g_controllerAim=true;
// No dead band. It was 2 degrees, to stop hand tremor turning the soldier's
// whole body a step at a time, but on hardware it reads as a patch around
// the hand where the aim does not answer at all, on one hand and on two.
// The default lives here so a reverted INI cannot bring it back.
float g_aimBandDegrees=0.0f;
float g_aimAnchorYaw=0;  // the written yaw, which the controller drags about
bool g_aimAnchored=false;
bool g_handAiming=false; // whether the controller is actually driving it
// Ranger dash: the run direction comes from the body facing at the moment the
// state starts, so for those frames the facing is sent from the head.
//
// The body facing is also the aim, and the aim is what the reticle hangs on and
// what the weapon is placed along, so turning the body would otherwise swing
// both to wherever the player is running. Nothing the player is holding has
// moved, so the angles the hand asked for are kept here and used for everything
// that is drawn, while only the soldier is told to face the head.
bool g_dashFollowsHead=true, g_dashing=false;
std::atomic<bool> g_dashAiming{false};
std::atomic<float> g_dashAimYaw{0};   // read on the render thread
unsigned long long g_dashStarts=0, g_dashFrames=0;
float g_viewTurn[2]{};   // degrees between the aim and the view, yaw then pitch
float g_headForwardWorld[3]{0,0,1};
bool g_reticleFollows=true;
// Aiming with both hands.
//
// The barrel points from the firing hand to the support hand. That is what
// UEVR does (IXRTrackingSystemHook.cpp, TWO_HANDED_RIGHT: the aim is
// normalize(left aim position - right aim position), and the controller's own
// rotation is not used at all) and what STALKER2-UEVR does (basic.lua builds
// the weapon rotation from off_hand_pos - dominant_hand_pos). Neither reads the
// support hand's orientation, and neither should: a hand on a fore-end has a
// position, and its wrist angle means nothing.
//
// Whether both hands are on the weapon is a question about where the left hand
// is relative to the right, which is how fear-vr asks it
// (src/common/two_handed_grip.h): the offset is taken in the firing hand's own
// aim frame, so the zone travels with the weapon instead of sitting in the
// room. Its numbers are a reach along the aim and a distance from that axis;
// here it is a ball, because that is how the player described the area they
// wanted.
//
// Two of that file's findings are taken as read, both of them user reports its
// author recorded. A hard limit on how far the hand line may stray from the
// weapon stops the weapon dead mid-motion, so the excess is compressed towards
// a limit it never reaches. And below about 12cm of separation the line between
// the hands is mostly tracking noise and must not steer anything.
bool g_twoHandAim=true;
// Sit the support hand on the barrel's own line rather than on the line
// between the hands. It solves for the yaw that puts the weapon's lateral
// offset under that hand, and the solution moves with the hand, so the aim
// answers a support-hand nudge with much less turn than it used to: on
// hardware that reads as a dead spot around the front hand. Off by default
// until that gain is back.
bool g_twoHandGripAlign=false;
// Where the support hand really sits against the firing hand, in the firing
// hand's own frame. A gunstock holds both controllers on one rail, so the line
// between them should BE the barrel -- but each controller reports its palm at
// its own place in its own mount, and what is left over tilts the aim. These
// take that out. The GRIP line in the log prints what the rig reads while
// firing; enter it negated.
float g_twoHandLeftRight=0.0f,g_twoHandLeftUp=0.0f,g_twoHandLeftAhead=0.0f;
// Measured, not guessed: with a physical gunstock the fore-grip sat 22cm ahead of
// the firing hand, 5cm to its right and 5cm below it, and held that to within
// three millimetres across every reading.
float g_twoHandAhead=0.22f, g_twoHandRight=0.05f, g_twoHandUp=-0.05f;
float g_twoHandRadius=0.13f, g_twoHandLeaveRadius=0.23f;
float g_twoHandApart=0.12f;
float g_twoHandSoft=0.96f, g_twoHandMax=1.57f;
float g_twoHandSmoothing=0.35f;
bool g_twoHandOn=false;
float g_twoHandDir[3]{};
bool g_twoHandDirValid=false;
float g_twoHandWhere[3]{};      // the left hand in the right hand's aim frame
bool g_twoHandWhereValid=false;
// What the player marks out by holding the trigger, the way the head gesture
// zone was measured: put the hand where the zone should be and shoot.
float g_gripLow[3]{9,9,9}, g_gripHigh[3]{-9,-9,-9}, g_gripSum[3]{};
unsigned g_gripSamples=0;
// The weapon's roll. The game builds its weapon basis from two angles and has
// no roll in it at all, so a wrist turn does nothing; adding one costs nothing
// either, because turning a weapon about the line it fires along cannot change
// where the shot goes.
bool g_weaponRoll=true;
float g_weaponRollSign=1.0f;
float g_weaponRollNow=0;
// Where the weapon sits, and how it is turned, as the player set it.
//
// Angles as well as offsets: a weapon that is in the right place can still be
// pointing a little wrong, and the two are not interchangeable. Turning the
// model does not turn the shot -- the shot follows the aim angles, which these
// do not touch -- so this is a correction to the picture and nothing else.
float g_weaponYaw=0, g_weaponPitch=0, g_weaponRollTrim=0;   // radians

// Adjusting all of that from inside the game.
//
// Numbers in a file cannot be judged without seeing them, and seeing them means
// stopping to edit, restarting, and having forgotten what was wrong. So the
// weapon is moved while looking at it. The game is never paused: this has to
// work in a mission, online, with everything else still running -- so the pad
// is silenced rather than the game stopped, and only this reads the sticks.
enum class Adjusting { Off, Armed, Counting, Active };
Adjusting g_adjust=Adjusting::Off;
double g_adjustSince=0;
edf6vr::GestureHold g_adjustHold{};
bool g_adjustWaiting=false;
// Where the weapon starts, and what both sticks pressed in go back to.
constexpr float kWeaponAheadDefault=0.0f;
constexpr float kWeaponRightDefault=0.0f;
// Where a one-handed grip points, as an angle off the controller's own aim
// pose. Taken from the two-handed stance rather than guessed: with both palms
// on a gunstock rail the support hand measured 0.250 ahead, 0.031 right and
// 0.058 below the firing hand, which puts the barrel 7.1 degrees right of the
// aim pose and 13.0 below it. One hand now points where two hands point.
constexpr float kWeaponYawDefault=7.07f,kWeaponPitchDefault=-12.97f;
constexpr float kWeaponUpDefault=0.0f;
// What to put back if the player changes their mind.
float g_adjustWas[6]{};
bool g_adjustEnabled=true;
float g_adjustMovePerSecond=0.25f;
float g_adjustTurnPerSecond=0.52f;      // radians, about 30 degrees
// The hands (hand_support.h). HandAdjustMode points the weapon adjust gesture
// at the hand model instead, for finding where it sits on the controller.
bool g_handModels=true;
unsigned g_handPassMask=0x3;
float g_handRelaxed=0.15f;
bool g_handCurlInvert=false;
bool g_handAdjust=false;
edf6vr::HandTrim g_handTrim{};
// Visual recoil (weapon_kick.h): one level per hand, fed from the bullet
// constructor on the game thread, read on the render thread.
bool g_recoilKickOn=true;
float g_shotBuzz=0.5f;   // a shot the game did not rumble for (the left gun)
float g_recoilDecaySeconds=0.08f;
edf6vr::RecoilKickShape g_recoilShape{4.0f*3.14159265f/180.0f,0.05f};
// Which hand fired, for the controller buzz. Written from the bullet
// constructor breakpoint, so atomics only.
std::atomic<unsigned long long> g_shotTick[2]{},g_shotCount[2]{};
edf6vr::RecoilKick g_recoilKick[2]{};
SRWLOCK g_recoilLock=SRWLOCK_INIT;
// The weapon, put where the hand is.
bool g_weaponFollowsHand=false;
bool g_weaponStereoLive=false;
// The muzzle flash is drawn from the weapon object's own transform, not from
// the model we move, so it stayed on the body with the shot origin. Moving that
// transform was measured once before to move the flash and nothing else.
bool g_weaponMuzzleFollows=true;
bool g_laserFollowsMuzzle=true;
// Armed by default while the shot origin is still being hunted; see kProbes.
bool g_shotProbe=false;   // the scattergun crashed the game; off unless asked for
// Move the bone the weapon is constrained to, and let the game derive the rest.
// Measured to change nothing: the write survives the tick untouched
// (armDrift=[0.000 0.000]m) and the bullet never moved. Off unless asked for.
bool g_weaponArmBone=false;
unsigned long long g_armBoneWrites=0;
void StepShotProbe(const edf6vr::ControllerState& controls) noexcept;
// The candidate the sweep found. If the shot still leaves from the body this
// was the wrong field and the sweep's other answer, or another sweep, is next.
unsigned long long g_originWrites=0,g_earlyOriginWrites=0;
// No weapon is three metres long, so a farther answer is a stale one.
constexpr float kWeaponMuzzleReach=3.0f;
void ReportPositions(const char* what,void* object,std::size_t span,
                     const float about[3],float within) noexcept;
// One long stride. Beyond this the difference is not walking.
constexpr float kWeaponCatchUpLimit=3.0f;
// Where along the weapon the hand holds it. The point everything is measured
// from sits near the front of the gun, so without this the player is holding the
// muzzle; pushing the weapon forward along the aim brings the grip back to the
// hand.
float g_weaponAhead=0.0f, g_weaponRight=0.0f, g_weaponUp=0.0f;
unsigned long long g_weaponWrites=0;
float g_weaponWas[3]{}, g_weaponPut[3]{};
float g_eyeWorld[3]{};
bool g_eyeWorldValid=false;
void* g_weaponTransform=nullptr;
float g_weaponWant[3]{};
void* g_armsNode=nullptr;
float g_armsWant[3]{};
// The weapon model keeps its position at these three offsets, measured: all
// three sat 0.19m from the arms bone, which is to say they are the same point
// written down three times.
constexpr int kWeaponPlaces=3;
constexpr std::size_t kWeaponPlace[kWeaponPlaces]={0x50,0x140,0x4A0};
void* g_weaponModel=nullptr;
float g_weaponModelWant[kWeaponPlaces][3]{};
// The weapon carries a skeleton of its own -- four nodes, laid out exactly like the
// soldier's. A skinned mesh is placed by the bones handed to its draw, so unlike
// everything tried before them these are not settled earlier in the frame.
constexpr unsigned long long kMaxWeaponBones=16;
unsigned char* g_weaponBones=nullptr;
unsigned long long g_weaponBoneCount=0;
float g_weaponBoneWant[kMaxWeaponBones][3]{};
std::atomic<unsigned long long> g_weaponBoneWrites{0};
// Where the shot should start from. Kept apart from the drawing, because the
// two are wanted at opposite ends of the frame.
float g_muzzleWant[3]{};
bool g_muzzleWantValid=false;
// Where and when a shot last left the tracked weapon.
//
// The game emits the firing sound from the soldier, which in VR is inside the
// listener's head -- measured at 0.07 to 0.12 metres -- so it has no direction
// worth hearing and cannot be given one by any rule about angles. It can be
// given one by being put where the gun is.
std::atomic<ULONGLONG> g_lastFireAt{0};
std::atomic<float> g_lastFireMuzzle[3]{};
// The camera the game was given, for anything that has to be placed against
// the head rather than against the world. Rows 0..2 are its basis; row 2 is
// the direction it looks along.
std::atomic<float> g_headBasis[3][3]{};
std::atomic<ULONGLONG> g_headBasisAt{0};
float g_leverLow=9, g_leverHigh=-1;
unsigned g_leverSeen=0;
struct WeaponHoldCommand {
    void* model=nullptr;
    void* soldier=nullptr;
    void* bodyNodes=nullptr;
    void* armsNode=nullptr;
    void* weaponNodes=nullptr;
    std::uint32_t objectId=0;
    std::uint64_t count=0, serial=0;
    ULONGLONG refreshed=0;
    DWORD updateThread=0;
    edf6vr::WeaponHoldFrame hand{};
    // What the destination above was measured against, so the draw thread can
    // measure it again against a newer hand.
    //
    // The destination is worked out on the game's logic thread, and the picture
    // is drawn later on another one. Between the two the hand has moved, so the
    // weapon was placed where the hand had been -- which is why it stayed steady
    // when only the head moved (the weapon was in the right place in the world
    // and only the camera had moved) and blurred when the hand moved, worse
    // while running. fear-vr keeps a note about this at the same spot: refresh
    // the weapon from the same late-latched basis as the hands, so a moving
    // helicopter cannot leave the model a frame behind.
    float handAxes[3][3]{};   // the tracked hand, in the soldier's frame
    float handPos[3]{};
    float eyeWorld[3]{};      // the eye the destination was measured from, and
                              // what the reach is measured against
    float headRoom[3]{};      // the head sample that eye belongs to
    float yawOffset=0;
    // Where the soldier stood when this destination was worked out.
    //
    // The destination is an absolute world position anchored on his eye as it
    // was on the update thread. The picture is drawn later, from a camera that
    // has by then caught up with the rest of his tick, so the weapon is left
    // behind by exactly the distance he covered in between: a wobble along the
    // direction of travel that grows with speed and sinks the gun back towards
    // the body, and nothing at all when only the head moves. That is the report.
    //
    // bioshock-vr takes the same care from the other end -- its rig is anchored
    // on the camera position of the frame being built, never on a copy taken
    // earlier ("the eye is a PARAMETER because it rides the camera"). We cannot
    // read that camera here, but his own root can be read again at the draw, and
    // the root is locomotion with none of the arm swing that reading a bone
    // would bring.
    // How far the soldier moved during the update that published this, and
    // where the game had the weapon at that moment. The first is the step the
    // lead is a multiple of; the second is where the muzzle flash is drawn from.
    float step[3]{};
    float weaponWas[3]{};
    float muzzleLocal[3]{}; // computed from same-tick native node + attachment
    bool muzzleLocalValid=false;
    float rootWorld[3]{};
    void* weapon=nullptr;
    bool tracked=false;
    unsigned handIndex=1;
    bool fencer=false;   // a Fencer hand (fencer_dual.h): allowed through the Fencer refusal below
    // A Fencer back mount's barrel (fencer_dual.h, FencerBackJoint): the nodes
    // jointIndex..jointEnd are turned about the joint so it sits at jointTarget
    // (its rows in the root's frame) before the weapon is carried. -1: none.
    int jointIndex=-1;
    unsigned jointEnd=0;
    float jointTarget[3][3]{};
    bool latch=true;     // false keeps the destination as published: the heavy aim's lag is the point
};
WeaponHoldCommand g_holdCommand{}; // g_lock protects the command, never a borrow.
// Identity-only rejection for the millions of unrelated native model draws.
// Accepted draws still take g_lock and validate the complete live command.
std::atomic<void*> g_fastDrawWeapon{nullptr},g_fastDrawLeft{nullptr},g_fastDrawBody{nullptr};
WeaponHoldCommand g_leftHoldCommand{};
// RetimeHoldYaw, per draw: tried, turned onto the drawn camera, no matched
// camera, refused as too far; and the largest turn, in degrees.
std::atomic<unsigned> g_yawRetimeDraws{0},g_yawRetimeTurned{0},g_yawRetimeUnknown{0},g_yawRetimeRefused{0};
std::atomic<float> g_yawRetimeMaxDeg{0};
SRWLOCK g_holdBorrowLock=SRWLOCK_INIT; // serializes draw borrows; never acquired by AfterUpdate
SRWLOCK g_holdStatsLock=SRWLOCK_INIT;
thread_local bool g_insideHoldBorrow=false;
struct HoldDrawStats {
    unsigned applied=0,rejected=0,nested=0,changed=0,restoreFaults=0;
    float sourceMin=1e30f,sourceMax=0,targetMin=1e30f,targetMax=0,errorMax=0;
    DWORD updateThread=0,drawThread=0;
    void* palette=nullptr;
    unsigned passMask=0;
    float displacementMax=0;
    float stepMax=0;        // the largest single step of walking seen
    // The renderer's palette root, which the bones are carried from, against
    // the weapon's own animation node root, which the update thread can reach.
    // Read-only, and the one number that says whether those two frames may be
    // used interchangeably. They are copies of one skeleton, so a gap here is
    // the distance between the moments they were taken, nothing else.
    float rootGapMax=0;     // metres
    float rootTurnMax=0;    // degrees
    struct Band { float low=1e30f,high=0; };
    Band standing,walking;   // how far the weapon sat from the eye, in each case
};
HoldDrawStats g_holdStats{};
unsigned long long g_muzzleWrites=0;
float g_weaponSideways[3]{1,0,0};
// Draw the weapon a second time, shifted. The separate-layer plan needs the
// weapon drawn once per eye, and that has two halves: issuing the draw again at
// a moment of our choosing, and issuing it with a camera of our choosing. This
// answers the first half on its own, in the cheapest way there is -- if two guns
// appear, the draw can be re-issued and the state it needs survives.
bool g_weaponDouble=false;
float g_weaponDoubleShift=0.30f;
int g_weaponDoubleMark=0;
unsigned long long g_weaponDoubles=0;
// Hold the weapon still through dashes and rolls.
//
// Only the position was being overridden, so the action animations still turned
// the gun: it rolled and jogged in the hand. Overriding the orientation with a
// fixed one would hold it wrongly, since each weapon sits at its own angle in
// the hand. So the angle is learnt instead. When a weapon is first seen, each
// bone's axes are written down as coordinates in the aim's own frame -- which
// is built from the aim angles and so has no animation in it -- and from then
// on the bones are rebuilt from those coordinates every frame. The weapon keeps
// its own tilt and loses the animation, and nothing needs tuning per weapon
// because it calibrates itself.
void* g_steadyFor=nullptr;
float g_steadyRel[kMaxWeaponBones][3][3]{};
float g_steadyOffset[3]{};
float g_settlingRel[3][3]{};
float g_settlingOffset[3]{};
int g_settled=0;
constexpr int kRemembered=4;
void* g_knownFor[kRemembered]{};
float g_knownRel[kRemembered][3][3]{};
float g_knownOffset[kRemembered][3]{};
int g_knownNext=0;
bool g_steadyValid=false;
float g_weaponBoneBasis[kMaxWeaponBones][3][3]{};
bool g_weaponBasisValid=false;
// The weapon's own shape, written down once while the aim owns it, so that no
// animation afterwards can reach it. The soldier is never seen, so refusing the
// animation outright costs nothing that anyone can look at.
float g_frozenAxes[kMaxWeaponBones][3][3]{};
float g_frozenAt[kMaxWeaponBones][3]{};
unsigned long long g_frozenCount=0;
// Kept per weapon, because a weapon's shape is the one thing about it that is
// certainly not shared. Holding one copy for all of them put the rifle's shape
// on the launcher: its magazine landed wherever the rifle's had been and the
// launcher itself ended up behind the soldier.
constexpr int kShapes=4;
void* g_shapeFor[kShapes]{};
float g_shapeAxes[kShapes][kMaxWeaponBones][3][3]{};
float g_shapeAt[kShapes][kMaxWeaponBones][3]{};
unsigned long long g_shapeCount[kShapes]{};
unsigned long long g_weaponFaults=0;
unsigned long long g_weaponModelWrites=0;
unsigned long long g_weaponHidden=0;
bool g_weaponSeen=false;
bool g_findWeaponModel=true;
void* g_weaponObject=nullptr;
void* g_weaponTransformSeen=nullptr;
// The model that is drawn, kept whether or not anything is being moved. Being
// able to name it is the whole of the layer plan: a weapon that can be left out
// of the world pass is a weapon that can be drawn somewhere else instead.
void* g_weaponModelSeen=nullptr;
// The Air Raider's radio (Weapon_RadioContact), followed through the held
// weapon's draw. Diagnostic only: it comes out as a black field of dots in VR
// and fine when held against the face, and whether it goes through the weapon
// layer at all decides whether that is the layer's lighting or the world's.
std::atomic<void*> g_radioModel{nullptr};
std::atomic<unsigned long long> g_radioHeld{0},g_radioChosen{0},g_radioDraws{0},g_radioFast{0},
    g_radioPrepared{0},g_radioCaptured{0},g_radioReplayed{0};
std::atomic<unsigned> g_radioPasses{0};
std::atomic<unsigned long long> g_radioPassCalls[4]{};   // held draws of the radio, by pass
bool g_weaponHide=false;
void* g_nearestCandidate=nullptr;
int g_nearestHeld=0;
unsigned int g_heldSlot=0;
void* g_soldierSeen=nullptr;
void* g_skeletonSeen=nullptr;
bool g_inputOnlyVr;
ULONGLONG g_handoverAt=0;   // when the input-only session was handed back
int g_frameTrace=0;         // frames still to log step by step
void ArmVrAtFirstFrame() noexcept;
void StartVr() noexcept;
bool Foreground() noexcept;
void TwoHanded(edf6vr::HeadBasis&,const float[3],const float[4]) noexcept;
void ReportDrawnModels() noexcept;
// The pad built from the controllers.
int g_padMode=1;                 // 0 off, 1 head gesture, 2 modifier button
// The zone is a place, not a distance. STALKER2-UEVR's holster system
// (artifact/scripts/gestures/basezones.lua and bodyzone.lua) is built the same
// way: the hand is turned into head-relative coordinates by undoing the HMD's
// yaw, and each zone is a fixed box in that frame -- its right shoulder zone is
// 10..30cm to the right, -10..20 forward, -20..-5 down. No zone is expressed as
// a distance from the head, because a distance cannot tell "beside the ear"
// from "held out in front": both are 30cm away.
//
// Here the zone is the right temple, and the player described it as a ball
// about 20-30cm across centred there, so it is an anchor and a radius rather
// than a box. Two radii so it cannot chatter on the boundary.
float g_gestureRight=0.18f, g_gestureAhead=0.0f, g_gestureUp=0.0f;
float g_gestureRadius=0.15f, g_gestureRadiusLeave=0.19f;
// The front of the ball is cut off. The ball is the right shape everywhere else,
// but its forward half reaches into where the hands are during ordinary play, so
// the player asked for about ten centimetres off the front.
float g_gestureFront=0.05f, g_gestureFrontLeave=0.09f;
bool g_gestureOn=false;
edf6vr::GestureHold g_heightHold{};
// Left temple, stick DOWN: where the compact HUD hangs.
edf6vr::GestureHold g_clusterHold{};
// The compact HUD (ui_cluster.h): radar, armour and weapons cut out of the
// panel and drawn smaller, together, at the panel's corner or on a wrist.
bool g_uiCluster=true; int g_uiClusterPlace=0; int g_uiClusterClass=-1; unsigned g_uiClusterTick=0;
float g_uiClusterWristWidth=0.30f,g_uiClusterMarginRight=0.03f,g_uiClusterMarginBottom=0.05f;
float g_uiClusterRadarScale=0.667f,g_uiClusterArmorScale=0.667f,g_uiClusterWeaponScale=0.5f,g_uiClusterGaugeScale=0.667f;
float g_uiClusterWristInset=-0.05f,g_uiClusterWristRaise=0.02f;

void PublishUiCluster() noexcept;
void PublishUiClusterPlace() noexcept;
int UiClusterClass() noexcept;
void UiClusterWristPose(const float hand[3],const edf6vr::Vec3& head,float pos[3],float quat[4]) noexcept;
std::atomic<bool> g_heightResetRequested{false};
bool g_vehicleReady=false,g_requestPointReady=false;
int g_testRequestPoints=0;
edf6vr::RequestPointBudget g_requestBudget{};
std::atomic<bool> g_vehicleMounted{false};
// Set each camera update while the player sits at one of the Brute's door guns:
// the user wants the barrel's up and down the other way round there.
std::atomic<ULONGLONG> g_bruteGunnerAt{0};
// The right stick as read there, for AssistBruteGun, and whether a push is being
// carried over the bottom (its pitch input turned over while held).
std::atomic<float> g_gunStick[2]{};
std::atomic<bool> g_gunPitchTurned{false};
std::atomic<ULONGLONG> g_vehicleSeen{0};
std::atomic<float> g_vehicleHeadYaw{0};
std::atomic<float> g_moveStickRead[2]{};   // the left stick as read, for the WALKER log
void* g_vehicleCamera=nullptr;
void* g_vehicleObject=nullptr;
// The mission truck being ridden (its AnimationModel), refreshed by the camera
// update: its windows' glass is left out of its draw (TruckGlassScope).
std::atomic<void*> g_truckGlassModel{nullptr};std::atomic<ULONGLONG> g_truckGlassAt{0};
void* g_vehicleSeat=nullptr;
unsigned g_vehicleId=0;
bool g_vehicleReference=false,g_vehiclePositionReference=false;
// Level the view once the eye is inside a cabin. Off restores the native
// camera's own pitch, which frames the machine from above and so looks down.
bool g_vehicleLevelView=true;
bool g_cockpitRequested=true,g_cockpitHooksReady=false;
edf6vr::CockpitRig g_cockpitRig{};
edf6vr::Quat g_vehicleHeadReference{};
edf6vr::VehicleEntryLevel g_vehicleEntryLevel{};
edf6vr::Vec3 g_vehiclePosition{};
bool VehiclePadActive() noexcept {
    const auto at=g_vehicleSeen.load();
    return g_vehicleMounted.load() && at && GetTickCount64()-at<250;
}
bool g_stickButtons=true;        // right stick up = Y, down = L3
// An Index grip reads well above zero from resting a hand on it, so one
// threshold turns a touch into a press. Press high, release low.
float g_gripPress=0.75f, g_gripRelease=0.45f;
float g_gripForcePress=0.30f, g_gripForceRelease=0.10f;
float g_gripSeen[2]{};
int g_stickFrame=1;
float g_moveTurn=0, g_moveSent[2]{};
edf6vr::Vec3 g_lastHeadXr{};
float g_lastHeadYaw=0, g_lastHeadPitch=0;
bool g_gripFromForce=false;
bool g_gripHeld[2]{};
// Input publishes intent; the game thread validates current weapon ownership.
edf6vr::RangerHolster g_rangerHolster{};
std::atomic<bool> g_dualActive{false},g_dualEligible{false},g_dualLeftFire{false};
std::atomic<ULONGLONG> g_dualInputAt{0},g_dualEligibleAt{0};
std::atomic<unsigned> g_dualToggleSerial{0};
std::atomic<bool> g_dualCancel{false};
bool g_dualReady=false,g_dualTriggerCaptured=false;
void ResetRangerDual() noexcept;
bool UpdateRangerDual(void*) noexcept;
void PublishRangerDualHands() noexcept;
bool RangerDualWeapon(void*) noexcept;
#ifdef EDF6VR_AUDIO_RESEARCH
// Retained for offline research tests only; never compiled into the release DLL.
SRWLOCK g_inputContinuityLock=SRWLOCK_INIT;
edf6vr::InputContinuity g_inputContinuity{};
#endif
void PublishControllerPad(const edf6vr::PadState& pad,bool present) noexcept {
#ifdef EDF6VR_AUDIO_RESEARCH
    AcquireSRWLockExclusive(&g_inputContinuityLock);
    g_inputContinuity.Observe(pad,present,GetTickCount64());
    ReleaseSRWLockExclusive(&g_inputContinuityLock);
#else
    (void)present;
#endif
    edf6vr::SetPadState(pad);
}
float g_gestureAway=9;           // how close the right hand got, for the log
// The same offset split along the head's own axes, kept for the log so the
// shape of the zone can be read off a session rather than guessed at.
float g_zoneAhead=0, g_zoneSide=0, g_zoneUp=0;
float g_zoneReach=9;             // distance from the zone's centre
bool g_padHands[2]{};            // whether each controller reported itself
float g_lastTrigger=0;           // the firing trigger, as the pad thread last saw it
float g_stickButtonEdge=0.6f;
bool g_padReady=false;
unsigned long long g_padFrames=0;
char g_padSeen[256]="nothing yet";
unsigned short g_padSeenButtons=0;
// When the player last held a fire control (either trigger or grip). A vehicle
// weapon's shot kicks only if it came while he was firing: on the Brute the
// door gunners are AI teammates who fire all the time, and their shots kept
// the guns shaking with the player's hands off the controls (vehicle_recoil.h).
std::atomic<ULONGLONG> g_playerFireAt{0};
// Each controller's trigger, 0..1 (0 left, 1 right), as BuildPad last read it:
// the Fencer's shield guards while its hand's trigger is held (fencer_dual.h).
std::atomic<float> g_handTrigger[2]{};
unsigned char g_padSeenLeft=0, g_padSeenRight=0;
int g_padSeenStick[3]{};
bool g_padSeenGesture=false;
float g_padSeenClosest=9;
unsigned short g_handBands[8]{};
unsigned short g_zoneBands[8]{};
// min then max, for ahead, side and up.
struct Shape { float low[3]{9,9,9}, high[3]{-9,-9,-9}; };
Shape g_shapeAll, g_shapeFiring;
void Span(Shape& shape,float ahead,float side,float up) noexcept {
    const float v[3]={ahead,side,up};
    for(int i=0;i<3;++i) {
        if(v[i]<shape.low[i]) shape.low[i]=v[i];
        if(v[i]>shape.high[i]) shape.high[i]=v[i];
    }
}
float g_zoneFurthest=0, g_zoneNearest=9;

constexpr unsigned short kPadUp=0x0001, kPadDown=0x0002, kPadLeft=0x0004, kPadRight=0x0008;
constexpr unsigned short kPadStart=0x0010, kPadBack=0x0020;
constexpr unsigned short kPadLeftThumb=0x0040, kPadRightThumb=0x0080;
constexpr unsigned short kPadLeftShoulder=0x0100, kPadRightShoulder=0x0200;
constexpr unsigned short kPadA=0x1000, kPadB=0x2000, kPadX=0x4000, kPadY=0x8000;

short ToAxis(float value) noexcept {
    if(!std::isfinite(value)) return 0;
    const float clamped=value<-1?-1.0f:(value>1?1.0f:value);
    return static_cast<short>(clamped*32767.0f);
}

void Log(const char* format,...) noexcept;
double Now() noexcept;

// Save a number back to the file it was read from.
void RememberIn(const wchar_t* section,const wchar_t* key,float value) noexcept {
    wchar_t text[32]{};
    std::swprintf(text,32,L"%.4f",static_cast<double>(value));
    WritePrivateProfileStringW(section,key,text,g_iniPath);
}
void Remember(const wchar_t* key,float value) noexcept { RememberIn(L"VR",key,value); }
// Hand trims live per controller family: where the hand sits on an Index
// controller is not where it sits on a Quest one. "[Hand.index]" and so on;
// [VR] holds the fallback.
bool HandSection(wchar_t* section,std::size_t count) noexcept {
    const char* kind=edf6vr::g_openxr.ControllerKind();
    if(!kind || !kind[0]) return false;
    std::swprintf(section,count,L"Hand.%hs",kind);
    return true;
}

void RememberWeaponPlacement() noexcept {
    constexpr float kToDegrees=180.0f/3.14159265f;
    Remember(L"WeaponAheadMetres",g_weaponAhead);
    Remember(L"WeaponRightMetres",g_weaponRight);
    Remember(L"WeaponUpMetres",g_weaponUp);
    Remember(L"WeaponYawDegrees",g_weaponYaw*kToDegrees);
    Remember(L"WeaponPitchDegrees",g_weaponPitch*kToDegrees);
    Remember(L"WeaponRollDegrees",g_weaponRollTrim*kToDegrees);
    Log("ADJUST saved ahead=%.3f right=%.3f up=%.3f yaw=%.1f pitch=%.1f roll=%.1f",
        g_weaponAhead,g_weaponRight,g_weaponUp,
        g_weaponYaw*kToDegrees,g_weaponPitch*kToDegrees,g_weaponRollTrim*kToDegrees);
}

void NoteRecoilShot(DWORD64 weapon) noexcept {
    if(!weapon) return;
    const int hand=(g_leftHoldCommand.weapon && weapon==reinterpret_cast<DWORD64>(g_leftHoldCommand.weapon))?0:1;
    g_shotTick[hand].store(GetTickCount64(),std::memory_order_relaxed);
    g_shotCount[hand].fetch_add(1,std::memory_order_relaxed);
    if(!g_recoilKickOn) return;
    const double now=Now();
    AcquireSRWLockExclusive(&g_recoilLock);
    g_recoilKick[hand].Shot(now,g_recoilDecaySeconds);
    ReleaseSRWLockExclusive(&g_recoilLock);
}
// Tilts the hand frame the weapon and hand are placed in by the current kick.
void ApplyRecoilToHand(int hand,float axes[3][3],float palm[3]) noexcept {
    if(!g_recoilKickOn || hand<0 || hand>1) return;
    AcquireSRWLockShared(&g_recoilLock);
    const float level=g_recoilKick[hand].Level(Now(),g_recoilDecaySeconds);
    ReleaseSRWLockShared(&g_recoilLock);
    edf6vr::ApplyRecoilKick(level,g_recoilShape,axes,palm);
}
// Drawn hands kicked by the steadying hand's share (RECOILHAND in the log).
std::atomic<unsigned> g_supportKicks{0};
// The drawn hands kick with the weapons they hold. On a two-handed weapon the
// support hand holds nothing of its own; it takes the firing hand's kick,
// turning about that palm, so both hands stay on the weapon as it jumps.
void KickHands(const bool have[2],float axes[2][3][3],float palm[2][3]) noexcept {
    if(!g_recoilKickOn) return;
    float level[2]{};
    AcquireSRWLockShared(&g_recoilLock);
    const double now=Now();
    for(int h=0;h<2;++h) level[h]=g_recoilKick[h].Level(now,g_recoilDecaySeconds);
    ReleaseSRWLockShared(&g_recoilLock);
    AcquireSRWLockShared(&g_lock);
    const bool leftHolds=g_leftHoldCommand.weapon!=nullptr;
    ReleaseSRWLockShared(&g_lock);
    // axes/palm are the physical hands (the drawn ones); the kick levels are by
    // role, so in left-handed mode the gun's kick goes to the physical left.
    if(edf6vr::g_openxr.HandSwap()) std::swap(level[0],level[1]);
    const int gun=edf6vr::g_openxr.HandSwap()?0:1, other=1-gun;
    const bool steadying=g_twoHandOn && !leftHolds && have[0] && have[1] && level[gun]>0;
    float firing[3][3]{},firingPalm[3]{};
    if(steadying) { std::memcpy(firing,axes[gun],sizeof(firing)); std::memcpy(firingPalm,palm[gun],sizeof(firingPalm)); }
    for(int h=0;h<2;++h) if(have[h]) edf6vr::ApplyRecoilKick(level[h],g_recoilShape,axes[h],palm[h]);
    if(steadying) {
        edf6vr::ApplyRecoilKickWith(level[gun],g_recoilShape,firing,firingPalm,axes[other],palm[other]);
        g_supportKicks.fetch_add(1,std::memory_order_relaxed);
    }
}
void RememberHandPlacement() noexcept {
    wchar_t section[48]=L"VR";
    HandSection(section,48);
    RememberIn(section,L"HandAheadMetres",g_handTrim.ahead);
    RememberIn(section,L"HandRightMetres",g_handTrim.right);
    RememberIn(section,L"HandUpMetres",g_handTrim.up);
    RememberIn(section,L"HandYawDegrees",g_handTrim.yawDegrees);
    RememberIn(section,L"HandPitchDegrees",g_handTrim.pitchDegrees);
    RememberIn(section,L"HandRollDegrees",g_handTrim.rollDegrees);
    Log("ADJUST saved HAND [%ls] ahead=%.3f right=%.3f up=%.3f yaw=%.1f pitch=%.1f roll=%.1f",section,
        g_handTrim.ahead,g_handTrim.right,g_handTrim.up,g_handTrim.yawDegrees,g_handTrim.pitchDegrees,g_handTrim.rollDegrees);
}

// Move the weapon while looking at it.
//
// Reached by putting the left hand beside the head and holding the stick down.
// A quiet left-hand pulse confirms that the uninterrupted hold is being read.
//
// Returns true while editing owns the other controls. Locomotion remains live,
// including during the entry hold, so raising the hand never stops movement.
bool Adjusting(const edf6vr::ControllerState& controls,bool handTracked,
               const float headXr[3],const float leftHandXr[3],float headYaw) noexcept {
    if(!g_adjustEnabled) {
        if(g_adjustHold.active && !g_adjustHold.fired)edf6vr::g_openxr.StopBuzz(0);
        g_adjust=Adjusting::Off;g_adjustHold={};return false;
    }
    const double now=Now();
    const float step=(g_adjustSince>0)?static_cast<float>(std::min(now-g_adjustSince,0.1)):0;
    g_adjustSince=now;

    // The left hand beside the head, judged exactly as the right hand's gesture
    // is: in the head's own frame, with the sideways distance taken absolute so
    // the same numbers describe either temple.
    bool beside=false;
    if(handTracked && headXr && leftHandXr) {
        const float dx=leftHandXr[0]-headXr[0];
        const float dy=leftHandXr[1]-headXr[1];
        const float dz=leftHandXr[2]-headXr[2];
        const float sy=std::sin(headYaw), cy=std::cos(headYaw);
        const float ahead=dx*sy+dz*cy;
        const float side=dz*sy-dx*cy;
        const float ax=std::fabs(side)-std::fabs(g_gestureRight);
        const float ay=dy-g_gestureUp;
        const float az=ahead-g_gestureAhead;
        beside=std::sqrt(ax*ax+ay*ay+az*az)
                 <(g_adjust==Adjusting::Off?g_gestureRadius:g_gestureRadiusLeave)
               && ahead<g_gestureFrontLeave;
    }

    if(g_adjust!=Adjusting::Active) {
        const auto tick=GetTickCount64();
        // Stick right: the weapon. Stick left: the hand. Same place, same hold.
        const bool weaponHeld=beside && controls.stick[0][0]>.6f && std::fabs(controls.stick[0][1])<.6f;
        const bool handHeld=beside && controls.stick[0][0]<-.6f && std::fabs(controls.stick[0][1])<.6f;
        const bool held=weaponHeld || handHeld;
        if(held && g_adjust!=Adjusting::Counting) g_handAdjust=handHeld;
        if(!held && g_adjustHold.active && !g_adjustHold.fired)edf6vr::g_openxr.StopBuzz(0);
        const bool complete=g_adjustHold.Step(held,tick);
        if(!held) {
            if(g_adjust==Adjusting::Counting)Log("ADJUST hold cancelled; nothing changed");
            g_adjust=beside?Adjusting::Armed:Adjusting::Off;
            return false;
        }
        if(g_adjust!=Adjusting::Counting)Log("ADJUST hold %s continuously for three seconds (%s)",g_handAdjust?"LEFT":"RIGHT",g_handAdjust?"hand":"weapon");
        g_adjust=Adjusting::Counting;
        if(g_adjustHold.Pulse(tick))edf6vr::g_openxr.Buzz(0,.24f,.65f);
        if(complete) {
            g_adjust=Adjusting::Active;
            if(g_handAdjust) {
                g_adjustWas[0]=g_handTrim.ahead; g_adjustWas[1]=g_handTrim.right;
                g_adjustWas[2]=g_handTrim.up;    g_adjustWas[3]=g_handTrim.yawDegrees;
                g_adjustWas[4]=g_handTrim.pitchDegrees; g_adjustWas[5]=g_handTrim.rollDegrees;
                Log("ADJUST is moving the HAND model");
            } else {
                g_adjustWas[0]=g_weaponAhead; g_adjustWas[1]=g_weaponRight;
                g_adjustWas[2]=g_weaponUp;    g_adjustWas[3]=g_weaponYaw;
                g_adjustWas[4]=g_weaponPitch; g_adjustWas[5]=g_weaponRollTrim;
            }
            g_adjustWaiting=true;
            edf6vr::g_openxr.Buzz(0,0.30f,1.0f);
            edf6vr::g_openxr.Buzz(1,0.30f,1.0f);
            Log("ADJUST on. Left stick moves it, right stick turns it, triggers raise and lower, "
                "grips roll it, right A keeps it, left A puts it back.");
        }
        return true;
    }

    // Nothing is read until the sticks have been let go.
    //
    // Getting in here means holding the left stick down, and the left stick down
    // is also "move the weapon back" -- so the weapon set off backwards the
    // instant the mode opened, driven by the very gesture that opened it.
    if(g_adjustWaiting) {
        const bool released=std::fabs(controls.stick[0][0])<0.2f
                         && std::fabs(controls.stick[0][1])<0.2f
                         && std::fabs(controls.stick[1][0])<0.2f
                         && std::fabs(controls.stick[1][1])<0.2f
                         && controls.trigger[0]<0.2f && controls.trigger[1]<0.2f;
        if(!released) return true;
        g_adjustWaiting=false;
        edf6vr::g_openxr.Buzz(0,0.05f,0.5f);
    }

    // Both sticks pressed in: back to where it started.
    if(controls.stickClick[0] && controls.stickClick[1] && g_handAdjust) {
        g_handTrim=edf6vr::HandTrim{};
        edf6vr::g_openxr.Buzz(0,0.15f,0.9f);
        edf6vr::g_openxr.Buzz(1,0.15f,0.9f);
        Log("ADJUST hand reset to the controller");
        return true;
    }
    if(controls.stickClick[0] && controls.stickClick[1]) {
        g_weaponAhead=kWeaponAheadDefault;
        g_weaponRight=kWeaponRightDefault;
        g_weaponUp=kWeaponUpDefault;
        constexpr float kDeg=3.14159265f/180.0f;
        g_weaponYaw=kWeaponYawDefault*kDeg;
        g_weaponPitch=kWeaponPitchDefault*kDeg;
        g_weaponRollTrim=0;
        edf6vr::g_openxr.Buzz(0,0.15f,0.9f);
        edf6vr::g_openxr.Buzz(1,0.15f,0.9f);
        Log("ADJUST reset to the starting position");
        return true;
    }

    // The stick means what the player sees, whatever the weapon is rolled to.
    //
    // The offsets live in the weapon's own frame, and that frame is rolled -- so
    // on a weapon rolled halfway over, "right" is left on the screen and "up" is
    // down. A few adjustments in and nobody can predict which way anything will
    // go. The wanted movement is named in the upright frame and turned into the
    // rolled one here, which is one line of trigonometry and the difference
    // between a usable tool and a puzzle.
    if(g_handAdjust) {
        // The hand's trims are in the controller's own frame, so the sticks map
        // straight onto them: no roll to undo.
        constexpr float kToDegrees=180.0f/3.14159265f;
        const float move=g_adjustMovePerSecond*step,turn=g_adjustTurnPerSecond*step*kToDegrees;
        g_handTrim.ahead=std::clamp(g_handTrim.ahead+controls.stick[0][1]*move,-0.3f,0.3f);
        g_handTrim.right=std::clamp(g_handTrim.right+controls.stick[0][0]*move,-0.3f,0.3f);
        g_handTrim.up=std::clamp(g_handTrim.up+(controls.trigger[1]-controls.trigger[0])*move,-0.3f,0.3f);
        g_handTrim.pitchDegrees=std::clamp(g_handTrim.pitchDegrees+controls.stick[1][1]*turn,-180.0f,180.0f);
        g_handTrim.yawDegrees=std::clamp(g_handTrim.yawDegrees+controls.stick[1][0]*turn,-180.0f,180.0f);
        g_handTrim.rollDegrees=std::clamp(g_handTrim.rollDegrees+(controls.squeeze[1]-controls.squeeze[0])*turn,-180.0f,180.0f);
        if(controls.lower[1]) {
            RememberHandPlacement();
            edf6vr::g_openxr.Buzz(1,0.20f,1.0f);
            g_adjust=Adjusting::Off;
        } else if(controls.lower[0]) {
            g_handTrim.ahead=g_adjustWas[0]; g_handTrim.right=g_adjustWas[1]; g_handTrim.up=g_adjustWas[2];
            g_handTrim.yawDegrees=g_adjustWas[3]; g_handTrim.pitchDegrees=g_adjustWas[4]; g_handTrim.rollDegrees=g_adjustWas[5];
            edf6vr::g_openxr.Buzz(0,0.20f,1.0f);
            Log("ADJUST cancelled; the hand is back where it was");
            g_adjust=Adjusting::Off;
        }
        return true;
    }
    const float rollNow=(g_weaponRoll&&std::isfinite(g_weaponRollNow)?g_weaponRollNow:0)
                       +g_weaponRollTrim;
    const float rc=std::cos(rollNow), rs=std::sin(rollNow);
    const float wantRight=controls.stick[0][0]*g_adjustMovePerSecond*step;
    const float wantUp=(controls.trigger[1]-controls.trigger[0])*g_adjustMovePerSecond*step;
    g_weaponAhead=std::clamp(g_weaponAhead+controls.stick[0][1]*g_adjustMovePerSecond*step,-1.5f,1.5f);
    g_weaponRight=std::clamp(g_weaponRight+wantRight*rc+wantUp*rs,-1.0f,1.0f);
    g_weaponUp=std::clamp(g_weaponUp-wantRight*rs+wantUp*rc,-1.0f,1.0f);
    // Right stick: how it is pointed.
    g_weaponPitch=std::clamp(g_weaponPitch+controls.stick[1][1]*g_adjustTurnPerSecond*step,-1.57f,1.57f);
    g_weaponYaw=std::clamp(g_weaponYaw+controls.stick[1][0]*g_adjustTurnPerSecond*step,-1.57f,1.57f);
    // Grips: roll.
    const float twist=controls.squeeze[1]-controls.squeeze[0];
    g_weaponRollTrim=std::clamp(g_weaponRollTrim+twist*g_adjustTurnPerSecond*step,-3.14f,3.14f);

    if(controls.lower[1]) {          // right A
        RememberWeaponPlacement();
        edf6vr::g_openxr.Buzz(1,0.20f,1.0f);
        g_adjust=Adjusting::Off;
    } else if(controls.lower[0]) {   // left A, which the game sees as B
        g_weaponAhead=g_adjustWas[0]; g_weaponRight=g_adjustWas[1];
        g_weaponUp=g_adjustWas[2];    g_weaponYaw=g_adjustWas[3];
        g_weaponPitch=g_adjustWas[4]; g_weaponRollTrim=g_adjustWas[5];
        edf6vr::g_openxr.Buzz(0,0.20f,1.0f);
        Log("ADJUST cancelled; the weapon is back where it was");
        g_adjust=Adjusting::Off;
    }
    return true;
}

// The controllers, turned into an ordinary pad.
//
// Nothing here decides what the game does with a button: the game keeps its own
// key bindings and simply sees a pad. All that is decided here is which control
// on the headset produces which button, which is a question about hands.
SRWLOCK g_fencerPadLock=SRWLOCK_INIT;
edf6vr::FencerPadCommand g_fencerPad{};
std::atomic<float> g_fencerTurn{0};
void PublishFencerPad(const edf6vr::FencerPadCommand& command) noexcept {
    AcquireSRWLockExclusive(&g_fencerPadLock); g_fencerPad=command; ReleaseSRWLockExclusive(&g_fencerPadLock);
}
bool FencerOwnsWeaponPose(void* soldier) noexcept {
    return edf6vr::HasType(g_image,soldier,".?AVHeavyArmor@@");
}
// Fencer's angle state is read only here. The game consumes the synthetic pad
// later, including its equipment-dependent response and accumulated recoil.
bool WriteTrackedAim(void* soldier,float pitch,float yaw,bool tracked) noexcept {
    if(FencerOwnsWeaponPose(soldier)) {
        edf6vr::SoldierAim current{};
        PublishFencerPad(tracked && edf6vr::ReadSoldierAim(soldier,current)
            ?edf6vr::FencerPursuit(current.pitch,current.yaw,pitch,yaw,GetTickCount64())
            :edf6vr::FencerPadCommand{});
        return false;
    }
    return edf6vr::WriteSoldierAim(soldier,pitch,yaw);
}
void BuildPad(const edf6vr::ControllerState& controlsIn,bool handTracked,
              const float headXr[3],const float rightHandXr[3],const float leftHandXr[3],
              float headYaw=0,bool leftTracked=false) {
    // Left-handed: the input arrives with the hands swapped, sticks included;
    // LeftHandedSticks=0 sends the sticks back to their own hands.
    edf6vr::ControllerState controls=controlsIn;
    const bool leftHandedNow=edf6vr::g_openxr.HandSwap();
    if(leftHandedNow && !g_leftHandedSticks) edf6vr::UnswapControllerSticks(controls);
    for(int hand=0;hand<2;++hand) {
        const float force=controls.present[hand]?controls.squeeze[hand]:0;
        const float press=controls.squeezeIsForce[hand]?g_gripForcePress:g_gripPress;
        const float release=controls.squeezeIsForce[hand]?g_gripForceRelease:g_gripRelease;
        g_gripHeld[hand]=g_gripHeld[hand]?(force>release):(force>press);
        g_gripSeen[hand]=force;
        g_gripFromForce=controls.squeezeIsForce[hand];
    }
    const auto dualNow=GetTickCount64();
    const bool dualEligible=g_dualReady && g_dualEligible.load() && g_vrEnabled && g_fpsEnabled
        && dualNow-g_dualEligibleAt.load()<250 && leftTracked && handTracked
        && controls.present[0] && controls.present[1] && g_padMode!=0;
    const bool adjusting=g_adjust==Adjusting::Active;
    const auto shoulder=g_rangerHolster.Step(dualEligible && !adjusting,
        edf6vr::InLeftShoulder(headXr,leftHandXr,headYaw,g_rangerHolster.inside,
            g_gestureRight,g_gestureUp,g_gestureAhead,g_gestureRadius,g_gestureRadiusLeave,
            g_gestureFront,g_gestureFrontLeave,leftHandedNow),g_gripHeld[0]);
    const bool holsterGrip=g_rangerHolster.captured;
    if(shoulder.entered) {
        edf6vr::g_openxr.Buzz(0,.06f,.45f);
        Log("RANGERDUAL left temple holster entered; one haptic notification");
    }
    if(shoulder.toggle) g_dualToggleSerial.fetch_add(1);
    if(!dualEligible || adjusting) g_dualCancel.store(true);
    const bool dualInput=dualEligible && !adjusting;
    g_dualLeftFire.store(dualInput && controls.trigger[0]>.2f);
    g_dualInputAt.store(dualInput?dualNow:0);
    // A trigger at rest does not read zero on every controller (the Touch sat
    // above 0.1), and a capture that waited for zero never let go: the left
    // trigger was dead for the game from the first dual toggle on. Real
    // thresholds, with room between them.
    if((g_dualActive.load() || shoulder.toggle) && controls.trigger[0]>.4f) g_dualTriggerCaptured=true;
    if(controls.trigger[0]<=.25f) g_dualTriggerCaptured=false;
    g_fencerTurn.store(0,std::memory_order_relaxed); // disconnect/adjustment must not retain a turn
    edf6vr::PadState pad{};
    if(g_padMode==0) {
        if((g_heightHold.active && !g_heightHold.fired) || (g_adjustHold.active && !g_adjustHold.fired))
            edf6vr::g_openxr.StopBuzz(0);
        g_heightHold={};
        g_adjustHold={};
        pad.valid=false;                 // the bridge is off; the real pad is alone
        PublishControllerPad(pad,false);
        g_gestureOn=false;
        return;
    }
    // Valid even with no controllers yet. Valid is not "a button is pressed", it
    // is "there is a pad in this slot", and that is the question the game asks at
    // the title screen, before the headset is running, when it decides whether it
    // is a gamepad game or a keyboard one. Answering it late is how the first
    // title screen came to need a real controller.
    pad.valid=true;
    g_lastTrigger=controls.trigger[1];
    g_padHands[0]=controls.present[0];
    g_padHands[1]=controls.present[1];
    if(!controls.present[0] && !controls.present[1]) {
        if((g_heightHold.active && !g_heightHold.fired) || (g_adjustHold.active && !g_adjustHold.fired))
            edf6vr::g_openxr.StopBuzz(0);
        g_heightHold={};
        g_adjustHold={};
        PublishControllerPad(pad,false);
        g_gestureOn=false;
        return;
    }
    float moveX=controls.stick[0][0],moveY=controls.stick[0][1];
    g_moveStickRead[0].store(moveX,std::memory_order_relaxed);g_moveStickRead[1].store(moveY,std::memory_order_relaxed);
    if(g_vrEnabled && VehiclePadActive()) edf6vr::RebaseVehicleStick(g_vehicleHeadYaw.load(),moveX,moveY);
    pad.leftX=ToAxis(moveX);pad.leftY=ToAxis(moveY);
    // Locomotion passes through temple entry/holds/editing. A holster grip
    // cancels pending UP/DOWN holds and takes priority over their use of the stick.
    const bool heightHeld=!holsterGrip && g_vrEnabled && g_fpsEnabled && leftTracked && g_adjust!=Adjusting::Active
        && controls.stick[0][1]>.6f && std::fabs(controls.stick[0][0])<.6f
        && edf6vr::NearTemple(headXr,leftHandXr,headYaw,g_gestureRight,g_gestureUp,
            g_gestureAhead,g_gestureRadiusLeave,g_gestureFrontLeave);
    const auto holdTime=GetTickCount64();
    if(!heightHeld && g_heightHold.active && !g_heightHold.fired)edf6vr::g_openxr.StopBuzz(0);
    if(g_heightHold.Step(heightHeld,holdTime)) {
        g_heightResetRequested.store(true,std::memory_order_release);
        edf6vr::g_openxr.RecenterBoard();   // in menus this is the only recenter within reach
        edf6vr::g_openxr.Buzz(0,.2f,.9f);
        Log("HEIGHT left-temple up held 3 seconds; vertical recenter requested");
    }
    // Stick DOWN in the same place: move the compact HUD on to the next place.
    const bool clusterHeld=!holsterGrip && g_vrEnabled && g_fpsEnabled && leftTracked && g_adjust!=Adjusting::Active
        && controls.stick[0][1]<-.6f && std::fabs(controls.stick[0][0])<.6f
        && edf6vr::NearTemple(headXr,leftHandXr,headYaw,g_gestureRight,g_gestureUp,
            g_gestureAhead,g_gestureRadiusLeave,g_gestureFrontLeave);
    if(!clusterHeld && g_clusterHold.active && !g_clusterHold.fired)edf6vr::g_openxr.StopBuzz(0);
    if(g_clusterHold.Step(clusterHeld,holdTime)) {
        g_uiClusterPlace=(g_uiClusterPlace+1)%3;
        RememberIn(L"Render",L"UiClusterPlace",static_cast<float>(g_uiClusterPlace));
        PublishUiClusterPlace();
        edf6vr::g_openxr.Buzz(0,.2f,.9f);
        Log("UICLUSTER left-temple down held 3 seconds; place=%d (%s)",g_uiClusterPlace,
            g_uiClusterPlace==0?"panel corner":g_uiClusterPlace==1?"right wrist":"left wrist");
    }
    if(g_clusterHold.Pulse(holdTime))edf6vr::g_openxr.Buzz(0,.24f,.65f);
    if(Adjusting(controls,leftTracked && !holsterGrip,headXr,leftHandXr,headYaw)) {
        PublishControllerPad(pad,true);
        g_gestureOn=false;
        return;
    }
    if(g_heightHold.Pulse(holdTime))edf6vr::g_openxr.Buzz(0,.24f,.65f);
    StepShotProbe(controls);

    // Index has A and B on both hands. The user's own layout: right B is X,
    // right A is A, left A is B, left B is Y.
    if(controls.lower[1]) pad.buttons|=kPadA;
    if(controls.upper[1]) pad.buttons|=kPadX;
    if(controls.lower[0] && !g_shotProbe) pad.buttons|=kPadB;
    if(controls.upper[0]) pad.buttons|=kPadY;
    if(controls.menu[0] || controls.menu[1]) pad.buttons|=kPadStart;

    pad.leftTrigger=static_cast<unsigned char>((controls.trigger[0]<0?0:
                        (controls.trigger[0]>1?1:controls.trigger[0]))*255.0f);
    pad.rightTrigger=static_cast<unsigned char>((controls.trigger[1]<0?0:
                        (controls.trigger[1]>1?1:controls.trigger[1]))*255.0f);
    if(g_gripHeld[0] && !shoulder.consume) pad.buttons|=kPadLeftShoulder;
    if(g_dualActive.load() || g_dualTriggerCaptured) pad.leftTrigger=0;
    if(g_gripHeld[1]) pad.buttons|=kPadRightShoulder;
    if(controls.trigger[0]>.5f || controls.trigger[1]>.5f || g_gripHeld[0] || g_gripHeld[1])
        g_playerFireAt.store(GetTickCount64(),std::memory_order_relaxed);
    g_handTrigger[0].store(controls.trigger[0],std::memory_order_relaxed);
    g_handTrigger[1].store(controls.trigger[1],std::memory_order_relaxed);

    // Forward on the stick should mean where the head is looking, not where the
    // weapon is pointing. Which way to turn the stick to get that depends on
    // what the game moves relative to, and that is not settled: if it moves
    // relative to the soldier's facing the stick wants turning one way, and if
    // it moves relative to the camera - which has followed the head since 0.12.2
    // - it was already right and turning it makes it worse. Rather than guess
    // again, all three are selectable and one session decides it.
    // Straight through. The turn that makes forward mean the head is applied
    // where the movement is written to the soldier, not here: a menu wants the
    // stick exactly as the hand is holding it.
    // Left movement was populated before any gesture could consume other inputs.

    // Is the right hand up by the head? Two radii so it cannot chatter on the
    // boundary: once inside it stays on until the hand is clearly away again.
    bool inZone=false;
    if(g_padMode==1 && handTracked) {
        const float dx=rightHandXr[0]-headXr[0];
        const float dy=rightHandXr[1]-headXr[1];
        const float dz=rightHandXr[2]-headXr[2];
        g_gestureAway=std::sqrt(dx*dx+dy*dy+dz*dz);
        // Undo the head's yaw, so the hand is described in the head's own
        // frame. Head yaw is measured the way the rest of the mod measures it,
        // atan2(forward.x, forward.z) after XrToGame, so forward is
        // (sin yaw, 0, cos yaw). XrToGame negates x and z, which puts game
        // right at (-cos yaw, 0, sin yaw).
        const float sy=std::sin(headYaw), cy=std::cos(headYaw);
        g_zoneAhead=dx*sy+dz*cy;
        g_zoneSide=dz*sy-dx*cy;
        g_zoneUp=dy;
        // Measured against the absolute sideways offset, so the zone sits by
        // whichever temple the hand is at. That costs nothing -- a right hand
        // does not wander past the left ear -- and it means a mirrored sign
        // cannot silently kill the gesture.
        const float ax=std::abs(g_zoneSide)-std::abs(g_gestureRight);
        const float ay=g_zoneUp-g_gestureUp;
        const float az=g_zoneAhead-g_gestureAhead;
        g_zoneReach=std::sqrt(ax*ax+ay*ay+az*az);
        inZone=g_zoneReach<(g_gestureOn?g_gestureRadiusLeave:g_gestureRadius)
            && g_zoneAhead<(g_gestureOn?g_gestureFrontLeave:g_gestureFront);
    } else if(g_padMode==2) {
        inZone=controls.squeeze[0]>0.8f && controls.squeeze[1]>0.8f;
    }
    if(inZone!=g_gestureOn) {
        g_gestureOn=inZone;
        // Short buzz on the hand that did it, so entering and leaving are felt
        // rather than guessed at.
        edf6vr::g_openxr.Buzz(1,0.06f,inZone?0.7f:0.35f);
    }

    if(g_gestureOn) {
        // The right stick becomes the d-pad, and the stick clicks become the
        // two buttons a headset has nowhere else to put.
        const float x=controls.stick[1][0], y=controls.stick[1][1];
        if(y>g_stickButtonEdge) pad.buttons|=kPadUp;
        if(y<-g_stickButtonEdge) pad.buttons|=kPadDown;
        if(x<-g_stickButtonEdge) pad.buttons|=kPadLeft;
        if(x>g_stickButtonEdge) pad.buttons|=kPadRight;
        if(controls.stickClick[0]) pad.buttons|=kPadBack;
        if(controls.stickClick[1]) pad.buttons|=kPadStart;
    } else {
        // Sideways is turning, which the game does with the right stick. Up and
        // down are not used on foot, so they carry the two controls that are
        // awkward to reach on an Index: a dash and the button under the thumb.
        pad.rightX=ToAxis(controls.stick[1][0]);
        if(g_stickButtons && !VehiclePadActive()) {
            if(controls.stick[1][1]>g_stickButtonEdge) pad.buttons|=kPadY;
            if(controls.stick[1][1]<-g_stickButtonEdge) pad.buttons|=kPadLeftThumb;
        } else {
            // The Brute's door gun: up and down inverted (the user, 2026-09-27),
            // and turned back over while a push is carried over the bottom
            // (AssistBruteGun). The game's own aim, weight and all, moves it.
            const bool bruteGun=VehiclePadActive()&&GetTickCount64()-g_bruteGunnerAt.load(std::memory_order_relaxed)<250;
            g_gunStick[0].store(bruteGun?controls.stick[1][0]:0.f,std::memory_order_relaxed);
            g_gunStick[1].store(bruteGun?controls.stick[1][1]:0.f,std::memory_order_relaxed);
            const bool turned=bruteGun&&g_gunPitchTurned.load(std::memory_order_relaxed);
            pad.rightY=ToAxis(bruteGun&&!turned?-controls.stick[1][1]:controls.stick[1][1]);
        }
        if(controls.stickClick[0]) pad.buttons|=kPadLeftThumb;
        if(controls.stickClick[1]) pad.buttons|=kPadRightThumb;
    }
    // Fencer uses the game's complete pad look path. Head/body turning is a
    // separate target offset; feeding this synthetic look back to yawOffset
    // would create a self-chasing target and perpetual rotation.
    g_fencerTurn.store(g_gestureOn?0.0f:controls.stick[1][0],std::memory_order_relaxed);
    edf6vr::FencerPadCommand pursuit{};
    AcquireSRWLockShared(&g_fencerPadLock); pursuit=g_fencerPad; ReleaseSRWLockShared(&g_fencerPadLock);
    if(!VehiclePadActive() && g_vrEnabled && g_fpsEnabled && handTracked && !g_gestureOn && pursuit.Fresh(GetTickCount64())) {
        pad.rightX=ToAxis(pursuit.x); pad.rightY=ToAxis(pursuit.y);
    }
    PublishControllerPad(pad,true);
    ++g_padFrames;
    // Everything seen since the last log line, not just this instant: a line
    // sampled every five seconds otherwise reports whatever was happening at
    // that moment, which is almost always nothing.
    g_padSeenButtons|=pad.buttons;
    if(pad.leftTrigger>g_padSeenLeft) g_padSeenLeft=pad.leftTrigger;
    if(pad.rightTrigger>g_padSeenRight) g_padSeenRight=pad.rightTrigger;
    const int stickX=pad.leftX/328, stickY=pad.leftY/328, turnX=pad.rightX/328;
    if(std::abs(stickX)>std::abs(g_padSeenStick[0])) g_padSeenStick[0]=stickX;
    if(std::abs(stickY)>std::abs(g_padSeenStick[1])) g_padSeenStick[1]=stickY;
    if(std::abs(turnX)>std::abs(g_padSeenStick[2])) g_padSeenStick[2]=turnX;
    if(g_gestureOn) g_padSeenGesture=true;
    if(g_zoneReach<g_padSeenClosest) g_padSeenClosest=g_zoneReach;
    // How far the hand is from the centre of the zone, in bands, and separately
    // how far while the right trigger is held. Holding the trigger is the
    // marker: the player puts the hand where they want the zone to be and
    // shoots, so those samples are the zone, measured rather than reasoned
    // about. The furthest of them is the radius the gesture needs, and the
    // SHAPE line below says whether the centre itself is in the right place.
    {
        const float d=g_zoneReach;
        const int band=d<0.08f?0:(d<0.11f?1:(d<0.14f?2:(d<0.17f?3:(d<0.20f?4:
                       (d<0.25f?5:(d<0.32f?6:7))))));
        if(g_handBands[band]<0xFFFF) ++g_handBands[band];
        Span(g_shapeAll,g_zoneAhead,g_zoneSide,g_zoneUp);
        if(controls.trigger[1]>0.5f) {
            if(g_zoneBands[band]<0xFFFF) ++g_zoneBands[band];
            if(d>g_zoneFurthest) g_zoneFurthest=d;
            if(d<g_zoneNearest) g_zoneNearest=d;
            Span(g_shapeFiring,g_zoneAhead,g_zoneSide,g_zoneUp);
        }
    }
    std::snprintf(g_padSeen,sizeof(g_padSeen),
                  "seen buttons=%04X lt=%u rt=%u sticks=(%d,%d,%d) gesture=%d "
                  "zone=%.2fm closest=%.2fm turn=%.0fdeg frame=%d raw=(%d,%d) "
                  "move=(%.2f,%.2f)@%.0fdeg grip=(%.2f,%.2f)%s now=%04X "
                  // What the controllers are actually reporting, before any of
                  // the mapping above. A whole session went by with the probe
                  // never advancing and no button ever reaching the game, and
                  // there was no way to tell whether nothing was pressed or
                  // nothing was arriving.
                  "face=L%d%d%d R%d%d%d",
                  g_padSeenButtons,g_padSeenLeft,g_padSeenRight,
                  g_padSeenStick[0],g_padSeenStick[1],g_padSeenStick[2],
                  g_padSeenGesture?1:0,g_zoneReach,g_padSeenClosest,
                  g_moveTurn,g_stickFrame,
                  static_cast<int>(controls.stick[0][0]*99),
                  static_cast<int>(controls.stick[0][1]*99),
                  g_moveSent[0],g_moveSent[1],g_moveTurn,
                  g_gripSeen[0],g_gripSeen[1],
                  g_gripFromForce?"force":"analogue",pad.buttons,
                  controls.lower[0]?1:0,controls.upper[0]?1:0,controls.menu[0]?1:0,
                  controls.lower[1]?1:0,controls.upper[1]?1:0,controls.menu[1]?1:0);
}
// Called by the pad bridge whenever the game reads the pad, including in menus.
void RefreshPadUnlocked() {
    edf6vr::ControllerState controls{};
    if(!edf6vr::g_openxr.Controls(controls)) {
        // Same reason: the slot has to answer before the headset does.
        BuildPad(controls,false,nullptr,nullptr,nullptr);
        return;
    }
    float leftPos[3]{},leftRot[4]{},rightPos[3]{},rightRot[4]{};
    const bool haveRight=edf6vr::g_openxr.HandPose(1,rightPos,rightRot);
    const bool haveLeft=edf6vr::g_openxr.HandPose(0,leftPos,leftRot);
    const edf6vr::Vec3 rightGame=edf6vr::XrToGame(
        edf6vr::Vec3{rightPos[0],rightPos[1],rightPos[2]});
    const edf6vr::Vec3 leftGame=edf6vr::XrToGame(
        edf6vr::Vec3{leftPos[0],leftPos[1],leftPos[2]});
    const float hand[3]={rightGame.x,rightGame.y,rightGame.z};
    const float other[3]={leftGame.x,leftGame.y,leftGame.z};
    // The game camera stops updating in menus/credits, but OpenXR does not.
    // Use its current head position/yaw for BOTH gesture zones in every screen.
    edf6vr::HmdSample sample{};edf6vr::HeadBasis basis{};
    const bool haveHead=edf6vr::g_openxr.Running() && edf6vr::g_openxr.Sample(sample)
        && sample.positionValid && edf6vr::HeadBasisFromXr(sample.orientation,basis);
    const auto position=edf6vr::XrToGame(sample.position);
    const float head[3]={position.x,position.y,position.z};
    // The compact HUD on a wrist follows that controller, in reference space.
    if(g_uiCluster && g_uiClusterPlace!=0) {
        const bool right=g_uiClusterPlace==1;
        const bool have=haveHead && (right?haveRight:haveLeft);
        float pos[3]{},quat[4]{0,0,0,1};
        if(have) UiClusterWristPose(right?rightPos:leftPos,sample.position,pos,quat);
        edf6vr::g_openxr.SetUiClusterPose(pos,quat,have);
    }
    if(++g_uiClusterTick%120==0 && UiClusterClass()!=g_uiClusterClass) PublishUiCluster();
    BuildPad(controls,haveHead && haveRight,head,hand,other,basis.yaw,haveHead && haveLeft);
}

// XInputGetState and GetStateEx can request input concurrently. Serialize the
// gesture edges without making a reader wait; it can use the last complete pad.
SRWLOCK g_padRefreshLock=SRWLOCK_INIT;
void RefreshPad() {
    if(!TryAcquireSRWLockExclusive(&g_padRefreshLock)) return;
    __try { RefreshPadUnlocked(); }
    __finally { ReleaseSRWLockExclusive(&g_padRefreshLock); }
}

int g_reticlePixels=208;
float g_reticleDistance=15.0f;
float g_reticleScale=1.0f;
ULONGLONG g_headForwardAt=0;
float g_roomDead=0.20f, g_roomSettle=0.3f;
double g_roomStillSince=0;
float g_roomAway=0;
float g_headWobblePeak=0;
bool g_warpHudless=true;
float g_warpDebug=0;
// The HUD on a panel of its own, head locked, at a size and distance we pick.
bool g_uiLayer=false;
float g_uiWidth=0.9f, g_uiDistance=1.4f;
// Alternating which eye the game draws is what turns any error in the warp
// into a flicker: the same eye sees the drawn weapon on one frame and the
// built one on the next, and swaps between them. Holding it still leaves the
// same error as a slightly wrong distance, which is far easier to look at.
bool g_warpAlternate=false;
// Read-only survey of the game's depth targets. Off by default: it patches
// four slots of the D3D11 context vtable, so it only runs when asked for.
bool g_depthProbe=false;
unsigned g_eyeHeld=0;   // camera updates the current eye has waited for a frame
float g_fovScale=1.0f;
bool g_binocularZoom=true;
float g_nativeZoom=1, g_renderZoom=1;
int g_frameEye=0;               // which eye the frame about to be drawn is for
float g_lastRenderFov=0, g_lastIpd=0, g_lastEyeOffset=0;
ULONGLONG g_fovWrites=0;
unsigned long long g_lastEyeFlipPresent=0;
bool g_logGamepad=true;
std::atomic<bool> g_padStop{false};
std::thread g_padThread;
std::atomic<bool> g_captureRequested{false};
int g_limiterState=0;   // 0 unknown, 1 stock 60, 2 internal wait removed
bool g_allowLimiterToggle=true;
bool g_removeFpsLimit=true;
ULONGLONG g_nextLimiterAttempt=0;
void WatchForHang() noexcept;
void PlaceShotOrigin(void* soldier) noexcept;
void ReportBones(void* soldier) noexcept;
void PlaceBoneProbe(void* soldier) noexcept;
void MeasureProbeDrift() noexcept;
void ReportWatch() noexcept;
void WatchTheBone(void* soldier) noexcept;
void ArmWatchFromWatchdog() noexcept;
void DriveTheGame() noexcept;
void KeepAimCentre(void* soldier) noexcept;
bool ArmFireSites() noexcept;
void ReportFireSites() noexcept;
DWORD g_frameLoopThread=0;      // the thread the frame loop and our camera hook share
ULONGLONG g_limiterChanges=0;
ULONGLONG g_missionSeen=0;
bool g_describeSwapChain=true, g_captureDevice=true, g_headsetDisplay=false;
bool g_firingPresentationTrace=false;
float g_vrRoomScaleGain=1.0f, g_vrMaxRoomSpeed=4.0f, g_vrHeightScale=1.0f;
int g_pitchSign=1, g_pitchVotes=0, g_rollSign=1;
bool g_pitchLocked=false;
float g_yawOffset=0;
bool g_haveReference=false;
edf6vr::Vec3 g_referenceXr{};   // head position at the last recenter, game axes
edf6vr::Vec3 g_lastXr{};
edf6vr::MoveBasis g_moveBasis{};
edf6vr::Vec3 g_hmdUpWorld{0,1,0};
edf6vr::HmdSample g_appliedHeadSample{};
float g_hmdHeightOffset=0;
void* g_vrSoldier=nullptr;
void* g_vrSoldierTable=nullptr;
// 0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer, -1 unknown (the Ranger layout then).
int UiClusterClass() noexcept {
    __try {
        if(!g_vrSoldier || !edf6vr::Readable(g_vrSoldier,0x318) || !edf6vr::IsSupportedSoldier(g_image,g_vrSoldier)) return -1;
        if(edf6vr::HasType(g_image,g_vrSoldier,".?AVPaleWing@@")) return 1;
        if(edf6vr::HasType(g_image,g_vrSoldier,".?AVEngineer@@")) return 2;
        if(edf6vr::HasType(g_image,g_vrSoldier,".?AVHeavyArmor@@")) return 3;
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
// Where the game lays each HUD group out (fractions of its 16:9 screen), and
// how they are packed: radar over the armour bar on the left of the canvas,
// the weapon column to their right; the Fencer's two weapon groups go along
// the bottom under radar and armour. Everything sits on the canvas bottom.
void PublishUiCluster() noexcept {
    const int cls=UiClusterClass();
    g_uiClusterClass=cls;
    const float rs=g_uiClusterRadarScale, as=g_uiClusterArmorScale, ws=g_uiClusterWeaponScale, gs=g_uiClusterGaugeScale, gap=0.004f;
    // Rectangles marked by the user on a 16:9 grid over the game's own HUD
    // (research/ui_cluster). Armour and radar are the same for every class.
    const edf6vr::UiRect armor{0.048f,0.104f,0.333f,0.244f}, radar{0.784f,0.0f,0.966f,0.324f};
    auto W=[](const edf6vr::UiRect& r,float s) { return (r.u1-r.u0)*s; };
    auto H=[](const edf6vr::UiRect& r,float s) { return (r.v1-r.v0)*s; };
    struct Placed { edf6vr::UiRect r; float x,y,s; } placed[8]{}; unsigned n=0;
    float canvasW=0,canvasH=0;
    if(cls==3) {
        // Fencer: radar beside the armour bar, the two weapon groups under them.
        const edf6vr::UiRect left{0.004f,0.699f,0.352f,0.972f}, right{0.648f,0.699f,0.995f,0.972f};
        const float rowW=W(radar,rs)+gap+W(armor,as), rowH=std::max(H(radar,rs),H(armor,as));
        const float weaponsW=W(left,ws)+gap+W(right,ws), weaponsH=std::max(H(left,ws),H(right,ws));
        canvasW=std::max(rowW,weaponsW); canvasH=rowH+gap+weaponsH;
        placed[n++]={radar,canvasW-rowW,rowH-H(radar,rs),rs};
        placed[n++]={armor,canvasW-W(armor,as),rowH-H(armor,as),as};
        placed[n++]={left,canvasW-weaponsW,canvasH-H(left,ws),ws};
        placed[n++]={right,canvasW-W(right,ws),canvasH-H(right,ws),ws};
    } else if(cls==1) {
        // Wing Diver: radar over armour with the energy gauge beside the radar;
        // items over weapons to the right. Two corners of the original layout
        // are cut but not drawn (scale 0), as marked.
        const edf6vr::UiRect items{0.75f,0.329f,0.966f,0.443f}, gauge{0.898f,0.443f,0.956f,0.796f}, weapons{0.584f,0.711f,0.898f,0.898f};
        const edf6vr::UiRect unusedRight{0.955f,0.713f,0.997f,0.898f}, unusedBelow{0.898f,0.80f,0.956f,0.898f};
        const float pairW=W(radar,rs)+gap+W(gauge,gs), pairH=std::max(H(radar,rs),H(gauge,gs));
        const float colW=std::max(pairW,W(armor,as)), colH=pairH+gap+H(armor,as);
        const float rightW=std::max(W(items,ws),W(weapons,ws)), rightH=H(items,ws)+gap+H(weapons,ws);
        canvasW=colW+gap+rightW; canvasH=std::max(colH,rightH);
        const float pairY=canvasH-colH;
        placed[n++]={radar,0,pairY+pairH-H(radar,rs),rs};
        placed[n++]={gauge,W(radar,rs)+gap,pairY+pairH-H(gauge,gs),gs};
        placed[n++]={armor,0,canvasH-H(armor,as),as};
        placed[n++]={items,canvasW-W(items,ws),canvasH-rightH,ws};
        placed[n++]={weapons,canvasW-W(weapons,ws),canvasH-H(weapons,ws),ws};
        placed[n++]={unusedRight,0,0,0};
        placed[n++]={unusedBelow,0,0,0};
    } else {
        // Ranger and Air Raider: radar over armour, items over weapons to the right.
        const edf6vr::UiRect items{0.703f,0.329f,0.966f,0.583f};
        // Both reach past the weapon boxes: the reload gauge under them was
        // cut off (user-marked templates, 1.6.1).
        const edf6vr::UiRect weapons=cls==2?edf6vr::UiRect{0.602f,0.608f,0.976f,0.958f}:edf6vr::UiRect{0.602f,0.684f,0.980f,0.959f};
        const float colW=std::max(W(radar,rs),W(armor,as)), colH=H(radar,rs)+gap+H(armor,as);
        const float rightW=std::max(W(items,ws),W(weapons,ws)), rightH=H(items,ws)+gap+H(weapons,ws);
        canvasW=colW+gap+rightW; canvasH=std::max(colH,rightH);
        placed[n++]={radar,0,canvasH-colH,rs};
        placed[n++]={armor,0,canvasH-H(armor,as),as};
        placed[n++]={items,canvasW-W(items,ws),canvasH-rightH,ws};
        placed[n++]={weapons,canvasW-W(weapons,ws),canvasH-H(weapons,ws),ws};
    }
    edf6vr::UiClusterLayout layout{};
    layout.canvasWidth=canvasW; layout.canvasHeight=canvasH;
    for(unsigned i=0;i<n && layout.count<8;++i) layout.items[layout.count++]={placed[i].r,placed[i].x/canvasW,placed[i].y/canvasH,placed[i].s};
    edf6vr::g_openxr.SetUiCluster(g_uiCluster && g_uiLayer,layout);
}
void PublishUiClusterPlace() noexcept {
    edf6vr::g_openxr.SetUiClusterPlace(g_uiClusterPlace,g_uiClusterWristWidth,g_uiClusterMarginRight,g_uiClusterMarginBottom);
}
// In from the controller toward the body, level, then turned to the head and
// kept upright: the quad's +Z looks at the viewer, +Y is up.
void UiClusterWristPose(const float hand[3],const edf6vr::Vec3& head,float pos[3],float quat[4]) noexcept {
    float dx=head.x-hand[0], dz=head.z-hand[2]; float len=std::sqrt(dx*dx+dz*dz);
    if(len<1e-3f) { dx=0; dz=1; len=1; }
    dx/=len; dz/=len;
    pos[0]=hand[0]+dx*g_uiClusterWristInset; pos[1]=hand[1]+g_uiClusterWristRaise; pos[2]=hand[2]+dz*g_uiClusterWristInset;
    float fx=head.x-pos[0], fy=head.y-pos[1], fz=head.z-pos[2]; float fl=std::sqrt(fx*fx+fy*fy+fz*fz);
    if(fl<1e-4f) { fx=0; fy=0; fz=1; fl=1; }
    fx/=fl; fy/=fl; fz/=fl;
    float rx=fz, rz=-fx; float rl=std::sqrt(rx*rx+rz*rz);       // right = up x forward
    if(rl<1e-4f) { rx=1; rz=0; rl=1; }
    rx/=rl; rz/=rl; const float ry=0;
    const float ux=fy*rz-fz*ry, uy=fz*rx-fx*rz, uz=fx*ry-fy*rx;   // up = forward x right
    const float m00=rx,m01=ux,m02=fx, m10=ry,m11=uy,m12=fy, m20=rz,m21=uz,m22=fz;
    const float tr=m00+m11+m22;
    if(tr>0) { const float s=std::sqrt(tr+1)*2; quat[3]=0.25f*s; quat[0]=(m21-m12)/s; quat[1]=(m02-m20)/s; quat[2]=(m10-m01)/s; }
    else if(m00>m11 && m00>m22) { const float s=std::sqrt(1+m00-m11-m22)*2; quat[3]=(m21-m12)/s; quat[0]=0.25f*s; quat[1]=(m01+m10)/s; quat[2]=(m02+m20)/s; }
    else if(m11>m22) { const float s=std::sqrt(1+m11-m00-m22)*2; quat[3]=(m02-m20)/s; quat[0]=(m01+m10)/s; quat[1]=0.25f*s; quat[2]=(m12+m21)/s; }
    else { const float s=std::sqrt(1+m22-m00-m11)*2; quat[3]=(m10-m01)/s; quat[0]=(m02+m20)/s; quat[1]=(m12+m21)/s; quat[2]=0.25f*s; }
}
std::uint32_t g_vrSoldierId=0;
ULONGLONG g_vrSoldierSeen=0;
ULONGLONG g_vrInputCalls=0, g_vrAimWrites=0, g_vrMoveWrites=0;
bool g_recenterRequested=true;
std::atomic<bool> g_vrStartRequested{false}, g_vrStopRequested{false};
LARGE_INTEGER g_frequency{};
double g_previousTime=0;
struct MoveSample { bool have=false; float inX=0, inZ=0, yaw=0, x=0, z=0; double time=0; };
MoveSample g_previousMove{};
float g_lastWrittenPitch=0, g_lastWrittenYaw=0, g_lastRoll=0;
// The same aim as a world direction, which is what the drawn weapon points
// along. Only while the hand aims a non-Fencer soldier; otherwise the weapon
// is built from the two angles above, as before.
float g_lastWrittenAhead[3]{};
bool g_lastWrittenAheadValid=false;
float g_lastRoomInput[2]{};
edf6vr::SoldierAim g_lastAim{};
bool g_lastAimValid=false;
float g_soldierStep[3]{};
ULONGLONG g_soldierAt=0;
float g_lastCameraYaw=0, g_lastCameraPitchUp=0;

#include "log_file.h"
// Mods/Plugins/EDF6VR.patches.txt: every byte this plugin has changed, for
// checking against another mod's list before running the two together.
void WritePatchListBesideLog() noexcept {
    if(!g_logPath[0]) return;
    wchar_t path[MAX_PATH]{}; wcscpy_s(path,g_logPath);
    auto* slash=wcsrchr(path,L'\\'); if(!slash) return;
    wcscpy_s(slash+1,static_cast<std::size_t>(path+MAX_PATH-(slash+1)),L"EDF6VR.patches.txt");
    edf6vr::WritePatchList(path);
}
void LogText(const char* text) noexcept { Log("VR: %s",text); }
void LogRender(const char* text) noexcept { Log("RENDER: %s",text); }
// Read-only diagnostic of the independently loaded Moist/Patcher FPS patch.
// Patcher may load before or after this plugin, so this is read during play
// rather than once at load. It never writes EDF code.
int ReadLimiterState(int& sleepNop,int& branchNop) noexcept {
    sleepNop=0; branchNop=0;
    __try {
        const unsigned char nops[]={0x90,0x90,0x90,0x90,0x90,0x90};
        const unsigned char originalSleep[]={0xFF,0x15,0xBD,0x27,0x5B,0x00};
        const unsigned char originalBranch[]={0x74,0xEA};
        sleepNop=!std::memcmp(g_image.base+0x11A2F65,nops,6);
        branchNop=!std::memcmp(g_image.base+0x11A2F74,nops,2);
        if(sleepNop&&branchNop) return 2;
        if(!std::memcmp(g_image.base+0x11A2F65,originalSleep,6)
           && !std::memcmp(g_image.base+0x11A2F74,originalBranch,2)) return 1;
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// EDF.dll+0x11A2F65 is the Sleep(1) call in the frame loop and +0x11A2F74 the
// backward branch that waits for the next tick. Blanking both is exactly what
// the Moist Patches FPSUnlimiter does; doing it here instead of through a patch
// file keeps the menus at their normal rate, where the cursor moves per frame
// and a few thousand frames a second drives it into a corner.
//
// The bytes live inside the frame loop, so they may only be rewritten from the
// thread running that loop. Our camera hook is called from it, which is why the
// thread is recorded there and checked here.
// camera+0x24 is the vertical field of view in radians. The game rebuilds it
// every update from the constant at EDF.dll+0x1765A20 over camera+0x410
// (F8906..F8916), and the projection builder at 4DD70 turns it into
// cot(fov/2) with tanf, so writing it here only lasts for this frame. The
// ChangeFOV patch shipped with Moist Patches edits the same constant, which is
// what fixes the units as radians.
constexpr std::size_t kCameraFovOffset=0x24;
// The depth turned out to be ordinary normalised depth, near at zero and far
// at one, so turning it back into a distance needs the clip planes. The field
// of view sits at +0x24, and a projection is usually built from fields kept
// together, so the neighbourhood is worth a look.
void* g_lastCamera=nullptr;
// Candidates for telling a mission from a menu, all recorded so one run
// settles which of them actually turns over at the boundary instead of one
// guess per trip into the headset.
ULONGLONG g_soldierInput=0;    // the soldier read its input
ULONGLONG g_cameraWrite=0;     // a first person camera was written
ULONGLONG g_cameraUpdate=0;    // the game updated any camera object
ULONGLONG g_bodyDraw=0;        // the player body model was drawn
unsigned long long g_sceneKeptByCamera=0; // frames kept live by the camera and input, see CameraKeepsSceneLive
char g_cameraType[96]="?";     // what the game calls the camera it is updating
char g_soldierType[96]="?";
// Deciding this automatically has been wrong three times, so there is a way
// to say it by hand while the right signal is found. 0 auto, 1 board, 2 world.
int g_displayForce=0;
bool g_displayKey=false;
// EDF6 keeps the projection together: +0x24 is the live vertical field of
// view, +0x28 and +0x2C the clip planes. Turning the depth buffer back into
// a distance needs those two.
constexpr std::size_t kCameraNearOffset=0x28;
constexpr std::size_t kCameraFarOffset=0x2C;
float g_cameraNear=0.1f, g_cameraFar=1000.0f;

bool WriteCameraFov(void* camera,float radians) noexcept {
    __try {
        if(!edf6vr::Readable(camera,kCameraFovOffset+4,true)) return false;
        if(!std::isfinite(radians) || radians<0.015f || radians>2.8f) return false;
        *reinterpret_cast<float*>(static_cast<unsigned char*>(camera)+kCameraFovOffset)=radians;
        ++g_fovWrites;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool SetLimiterRemoved(bool removed) noexcept {
    __try {
        if(GetCurrentThreadId()!=g_frameLoopThread || !g_frameLoopThread) return false;
        const unsigned char nops[]={0x90,0x90,0x90,0x90,0x90,0x90};
        const unsigned char originalSleep[]={0xFF,0x15,0xBD,0x27,0x5B,0x00};
        const unsigned char originalBranch[]={0x74,0xEA};
        auto sleepCall=g_image.base+0x11A2F65;
        auto branch=g_image.base+0x11A2F74;
        // Refuse unless the current bytes are exactly what we expect to replace.
        const void* wantCall=removed?static_cast<const void*>(originalSleep):nops;
        const void* wantBranch=removed?static_cast<const void*>(originalBranch):nops;
        if(std::memcmp(sleepCall,wantCall,6) || std::memcmp(branch,wantBranch,2)) return false;
        DWORD previous=0;
        if(!VirtualProtect(sleepCall,0x10,PAGE_EXECUTE_READWRITE,&previous)) return false;
        std::memcpy(sleepCall,removed?nops:originalSleep,6);
        std::memcpy(branch,removed?nops:originalBranch,2);
        edf6vr::RecordPatch(sleepCall,6,"frame wait (toggle)"); edf6vr::RecordPatch(branch,2,"frame wait branch (toggle)");
        DWORD restored=0;
        VirtualProtect(sleepCall,0x10,previous,&restored);
        FlushInstructionCache(GetCurrentProcess(),sleepCall,0x10);
        ++g_limiterChanges;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

const char* LimiterName(int state) noexcept {
    return state==2?"removed":state==1?"original":"UNKNOWN_OR_PARTIAL";
}

void UpdateMissionFpsLimit(bool live,ULONGLONG now) noexcept {
    int sleepNop=0,branchNop=0;
    int state=ReadLimiterState(sleepNop,branchNop);
    if(live) {
        g_missionSeen=now;
        if(g_removeFpsLimit && state==1 && now>=g_nextLimiterAttempt) {
            g_nextLimiterAttempt=now+1000;
            if(SetLimiterRemoved(true)) {
                state=2;
                Log("FPSLIMIT mission: internal 60fps wait REMOVED; desktop VSync disabled; XR pacing retained");
            } else Log("FPSLIMIT automatic removal refused: frame-thread/expected-byte validation failed");
        }
        edf6vr::SetPresentUncapped(g_removeFpsLimit && state==2);
    } else {
        // Secondary cameras can update between two valid player cameras.
        // Do not cancel a fresh mission sample; the render-side lease expires.
        if(!g_missionSeen || now-g_missionSeen>=500) edf6vr::SetPresentUncapped(false);
        if(g_missionSeen && now-g_missionSeen>2000) {
            if(state==2 && SetLimiterRemoved(false)) {
                state=1;Log("FPSLIMIT stock wait RESTORED: no live mission");
            }
            g_missionSeen=0;
        }
    }
    g_limiterState=state;
}

// Watches the gamepads from a thread of its own so it still reports in menus,
// where none of the game hooks run. Read-only: it opens XInput late, polls it,
// and writes a line only when something is actually deflected or when the set of
// connected pads changes. A pad resting on a pushed stick drags menu cursors
// into a corner and spins the view in a mission, and both have been seen here
// without ever being confirmed.
void GamepadWatch() noexcept {
    // Stay out of the way while the game starts up.
    Sleep(5000);
    edf6vr::ReportXInputModules("five seconds in, before this plugin loads one",&LogText);
    HMODULE module=LoadLibraryW(L"xinput1_4.dll");
    if(!module) module=LoadLibraryW(L"xinput1_3.dll");
    if(!module) module=LoadLibraryW(L"xinput9_1_0.dll");
    if(!module) { Log("PAD: no XInput library; gamepad watch off"); return; }
    using GetStateFn=DWORD (WINAPI*)(DWORD,XINPUT_STATE*);
    auto getState=reinterpret_cast<GetStateFn>(
        reinterpret_cast<void*>(GetProcAddress(module,"XInputGetState")));
    if(!getState) { Log("PAD: XInputGetState missing; gamepad watch off"); return; }
    constexpr short kDeadzone=6000;   // well past a healthy stick at rest
    unsigned connectedBefore=0;
    bool reported=false;
    char lastBridge[256]{};
    while(!g_padStop.load()) {
        // This thread runs from start up, before VR and while the title screen
        // is still on, which is exactly when the gamepad question is decided. It
        // is therefore where the sweep is retried and where the bridge reports
        // itself, so the log carries the answer without the headset being on.
        edf6vr::SweepResolvedXInput(g_image,&LogText);
        // Retried, since a library the game loads later would otherwise be missed.
        if(GetPrivateProfileIntW(L"VR",L"XInputPatch",1,g_iniPath)!=0)
            edf6vr::PatchXInputExports(&LogText);
        edf6vr::ReportXInputCallers(&LogText);
        ReportDrawnModels();
        ArmVrAtFirstFrame();
        // Every test of the title screen so far has been ambiguous, because
        // nothing recorded whether the game had even started drawing when the
        // player pressed. This says so plainly, with the clock, so the log alone
        // settles what state the test was in.
        {
            static const ULONGLONG began=GetTickCount64();
            const unsigned elapsed=static_cast<unsigned>((GetTickCount64()-began)/1000);
            char line[192];
            std::snprintf(line,sizeof(line),
                          "TITLE t=%us front=%d drawing=%d vr=%d hands=%d%d ours=%04X %s",
                          elapsed,Foreground()?1:0,edf6vr::PresentCaptureInstalled()?1:0,
                          edf6vr::g_openxr.Running()?1:0,
                          g_padHands[0]?1:0,g_padHands[1]?1:0,g_padSeenButtons,
                          edf6vr::XInputBridgeStatus());
            static char last[192]="";
            if(std::strcmp(line,last)) { strncpy_s(last,line,_TRUNCATE); Log("%s",line); }
        }
        edf6vr::FlushMotionTrace();
        WatchForHang();
        DriveTheGame();
        ArmWatchFromWatchdog();
        ArmFireSites();
        ReportWatch();
        ReportFireSites();
        // What the game's own pad poll is doing, read straight out of its data.
        //
        // EDF.dll+1188380 is the poll. Before it reads anything it decides which
        // slot to look at, and it can decide not to look at all:
        //
        //   test  al,al                 ; a predicate
        //   jnz   read
        //   cmp   byte [2136F71],bl     ; and a flag
        //   jnz   read
        //   lea   esi,[r14-1]           ; neither: slot = -1, skip the pad
        //   ...
        //   cmp   dword [2136504],1     ; and when this is 1, slot 0 and slot 1
        //   jne   go                    ; swap over
        //   sete  al
        //
        // Those two are the game's own answer to "is this a gamepad game, and
        // which slot is player one". Reading them says whether the pad is being
        // ignored on purpose, which no amount of work on our side would fix.
        if(g_image.base) {
            const unsigned char gate=*reinterpret_cast<volatile unsigned char*>(
                g_image.base+0x2136F71);
            const unsigned int swap=*reinterpret_cast<volatile unsigned int*>(
                g_image.base+0x2136504);
            static unsigned char lastGate=0xFF;
            static unsigned int lastSwap=0xFFFFFFFF;
            if(gate!=lastGate || swap!=lastSwap) {
                lastGate=gate; lastSwap=swap;
                Log("EDFPAD gate=%02X slotSwap=%u (gate 0 with the predicate false means the "
                    "game skips the pad entirely)",gate,swap);
            }
        }
        {
            const char* bridge=edf6vr::XInputBridgeStatus();
            if(std::strcmp(bridge,lastBridge)) {
                strncpy_s(lastBridge,bridge,_TRUNCATE);
                Log("XINPUT %s",bridge);
            }
        }
        unsigned connected=0;
        char line[512]{};
        int used=0;
        for(DWORD pad=0;pad<4;++pad) {
            XINPUT_STATE state{};
            if(getState(pad,&state)!=ERROR_SUCCESS) continue;
            connected|=1u<<pad;
            const auto& g=state.Gamepad;
            const bool deflected=std::abs(g.sThumbLX)>kDeadzone || std::abs(g.sThumbLY)>kDeadzone
                              || std::abs(g.sThumbRX)>kDeadzone || std::abs(g.sThumbRY)>kDeadzone;
            if(!deflected) continue;
            const int written=snprintf(line+used,sizeof(line)-static_cast<std::size_t>(used),
                "%spad%lu L=(%d,%d) R=(%d,%d) buttons=%04X",used?" | ":"",pad,
                g.sThumbLX,g.sThumbLY,g.sThumbRX,g.sThumbRY,g.wButtons);
            if(written>0) used+=written;
        }
        if(connected!=connectedBefore) {
            Log("PAD: connected mask %X -> %X",connectedBefore,connected);
            connectedBefore=connected;
            reported=false;
        }
        if(used) {
            // One line per spell of deflection, not one every poll.
            if(!reported) Log("PAD HELD: %s",line);
            reported=true;
        } else reported=false;
        Sleep(2000);
    }
}

#include "native_cpu_sample.h"
void ReportPerf(ULONGLONG now) noexcept {
    edf6vr::FlushResourcePathLog();
    if(g_dashStarts) Log("RANGERDASH starts=%llu frames=%llu followHead=%d "
        "(state read from *(soldier+1EF8)+30 RTTI, aim yaw sent from the head while it lasts)",
        g_dashStarts,g_dashFrames,g_dashFollowsHead?1:0);
    const auto cpu=DrainNativeCpu();
    LARGE_INTEGER cpuFrequency{};QueryPerformanceFrequency(&cpuFrequency);
    if(cpu.samples && cpuFrequency.QuadPart>0)
        Log("NATIVECPU mainSamples=%llu cpuMs=%.4f wallMs=%.4f sampleEvery=16 (CPU includes spinning; OS accounting is coarse)",
            cpu.samples,cpu.cpu100ns*.0001,1000.0*static_cast<double>(cpu.wallTicks)/static_cast<double>(cpuFrequency.QuadPart));
    const auto gpu=edf6vr::DrainGpuProfile();
    Log("GPU trialPhase=%d skipped=%llu invalid=%llu scope=VR-only sampleEvery=4 (0 warmup/normal,1 A64,2 B32,3 done,4 abort)",
        edf6vr::WarpTrialPhase(),gpu.skipped,gpu.invalid);
    for(unsigned phase=0;phase<gpu.phase.size();++phase) {
        const auto& group=gpu.phase[phase];
        if(group.frames.count) Log("GPUCOMPARE phase=%u steps=%u size=%ux%u frames=%llu spanMs=%.3f fps=%.3f",
            phase,group.steps,group.width,group.height,group.frames.count,group.frames.ms,
            group.frames.ms>0?1000.0*static_cast<double>(group.frames.count)/group.frames.ms:0.0);
        for(unsigned i=0;i<edf6vr::kGpuStages;++i) {
            const auto& stat=group.gpu[i];
            if(stat.count) Log("GPU stage=%s phase=%u steps=%u n=%llu avgMs=%.4f maxMs=%.4f totalMs=%.4f",
                edf6vr::GpuName(static_cast<edf6vr::GpuStage>(i)),phase,group.steps,stat.count,
                stat.ms/static_cast<double>(stat.count),stat.maxMs,stat.ms);
        }
    }
    const auto perf=edf6vr::DrainPerf();
    // One file open/write for the whole report, not one per stage. This runs on
    // the existing five-second reporting path, never in the Present hook.
    // Frame rate straight from the gaps between presents, so a run can be read
    // without doing arithmetic on the stage table. Present and Present1 are
    // counted separately by thread; whichever the game actually uses wins.
    const auto& viaPresent=perf.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::PresentInterval)];
    const auto& viaPresent1=perf.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::Present1Interval)];
    const auto& gaps=viaPresent.count>=viaPresent1.count?viaPresent:viaPresent1;
    const double spanMs=static_cast<double>(gaps.ticks)*perf.ticksToMs;
    edf6vr::FrameCapture shown{};
    edf6vr::ReadFrameCapture(shown);
    Log("FPS frames=%llu fps=%.2f avgFrameMs=%.3f worstFrameMs=%.3f spanMs=%.1f vrOn=%d display=%s limiter=%s syncInterval=%u",
        gaps.count,
        spanMs>0?static_cast<double>(gaps.count)*1000.0/spanMs:0.0,
        gaps.count?spanMs/static_cast<double>(gaps.count):0.0,
        static_cast<double>(gaps.maxTicks)*perf.ticksToMs,spanMs,g_vrEnabled,
        edf6vr::g_openxr.Mode()==edf6vr::XrMode::HeadsetDisplay?"headset":"off",
        LimiterName(g_limiterState),shown.syncInterval);
    char report[8192]{};
    int used=snprintf(report,sizeof(report),
        "PERF windowEnd=%llu vrOn=%d droppedBatches=%llu cpuOnly=1",
        now,g_vrEnabled,perf.droppedBatches);
    if(used<0 || static_cast<std::size_t>(used)>=sizeof(report)) return;
    for(std::size_t i=0;i<edf6vr::kPerfStages;++i) {
        const auto& stage=perf.batch.stages[i];
        if(!stage.count) continue;
        const auto remaining=sizeof(report)-static_cast<std::size_t>(used);
        const int written=snprintf(report+used,remaining,
            "\nPERF stage=%s n=%llu avgMs=%.4f maxMs=%.4f totalMs=%.4f",
            edf6vr::PerfName(static_cast<edf6vr::PerfStage>(i)),stage.count,
            static_cast<double>(stage.ticks)*perf.ticksToMs/static_cast<double>(stage.count),
            static_cast<double>(stage.maxTicks)*perf.ticksToMs,
            static_cast<double>(stage.ticks)*perf.ticksToMs);
        if(written<0 || static_cast<std::size_t>(written)>=remaining) break;
        used+=written;
    }
    Log("%s",report);
}
// Render thread, once per present. Does nothing unless the headset display
// is running or finishing shutdown. Only shutdown milestones log from here.
void OnGameFrame(void* backBuffer,unsigned width,unsigned height,unsigned sampleCount) {
    void* captureDevice=nullptr; void* captureContext=nullptr;
    if(edf6vr::WeaponLayerCapturePending() && edf6vr::GameDevice(captureDevice,captureContext))
        edf6vr::PollWeaponLayerCapture(static_cast<ID3D11DeviceContext*>(captureContext));
    if(edf6vr::WeaponStereoCapturePending() && edf6vr::GameDevice(captureDevice,captureContext))
        edf6vr::PollWeaponStereoCapture(static_cast<ID3D11DeviceContext*>(captureContext));
    // The first frames of a session are logged step by step. A crash there
    // leaves no fault record of its own -- the process is simply gone -- so the
    // last line written says which step it did not come back from.
    const bool trace=g_frameTrace>0;
    if(trace) { --g_frameTrace; Log("FRAME %d: entering, %ux%u samples=%u",g_frameTrace,width,height,sampleCount); }
    // The probe cannot tell the presented image from any other target on its
    // own, and this is the one place that knows which it is.
    edf6vr::TracePresent();
    edf6vr::NoteBackBuffer(static_cast<ID3D11Texture2D*>(backBuffer));
    if(trace) Log("FRAME %d: back buffer noted",g_frameTrace);
    edf6vr::PauseUiCapture compositorScope;
    edf6vr::g_openxr.RenderFrame(backBuffer,width,height,sampleCount);
    edf6vr::FinishWeaponStereoFrame();
    if(!edf6vr::NativeWorldLive()) edf6vr::ClearNativeWorldImages();
    if(trace) Log("FRAME %d: rendered",g_frameTrace);
}
double Now() noexcept {
    LARGE_INTEGER counter{};
    if(!g_frequency.QuadPart || !QueryPerformanceCounter(&counter)) return 0;
    return static_cast<double>(counter.QuadPart)/static_cast<double>(g_frequency.QuadPart);
}
float ReadFloat(const wchar_t* path,const wchar_t* key,float fallback,float low,float high,const wchar_t* section=L"CameraTest") noexcept {
    wchar_t text[64]{};
    GetPrivateProfileStringW(section,key,L"",text,64,path);
    float value=fallback;
    if(swscanf_s(text,L"%f",&value)!=1 || !std::isfinite(value)) value=fallback;
    return std::clamp(value,low,high);
}
// Fit the rendered width to the headset's own horizontal field of view.
//
// Nothing chooses that field. The vertical is asked of the runtime and written
// into the camera every frame, and the horizontal is then whatever the render
// rectangle's aspect makes of it -- 16:9 of a 106.94 degree vertical is 134.76
// degrees, against a headset that shows 97.05. Measured on this one: 48.5 per
// cent of the width and 85.0 per cent of the height are ever displayed, so
// three pixels in seven are drawn for nobody. Narrowing the rectangle to the
// field the runtime asks for changes no pixel density, so it is not the
// resolution cut that was tried and rejected: the same world, at the same
// sharpness and the same level of detail, minus the part outside the lenses.
//
// The size has to be locked before the renderer is created and the frusta are
// only knowable from a running session, so the measurement is kept beside the
// INI and used from the next start. A headset the file does not describe --
// a different one, or the first run -- falls back to the configured width, and
// falls back wide, never narrow.
bool g_matchHeadsetFov=true;
float g_headsetFovMargin=1.0f;   // degrees kept beyond what the runtime asks
unsigned g_configuredWidth=0,g_configuredHeight=0,g_lockedWidth=0;
float g_fittedHalfHorizontal=0,g_fittedHalfVertical=0;
bool g_headsetFitWritten=false;
wchar_t g_fitPath[MAX_PATH]{};
constexpr float kDegrees=57.29577951f;
void ResolveFitPath() noexcept {
    g_fitPath[0]=0;
    if(!g_iniPath[0]) return;
    wcscpy_s(g_fitPath,g_iniPath);
    auto dot=wcsrchr(g_fitPath,L'.');
    const auto slash=wcsrchr(g_fitPath,L'\\');
    if(dot && (!slash || dot>slash)) *dot=0;
    if(wcslen(g_fitPath)+5>=MAX_PATH) { g_fitPath[0]=0; return; }
    wcscat_s(g_fitPath,L".fit");
}
// The rectangle that covers the runtime's frustum plus a margin on every side,
// at exactly the pixel density the configured rectangle already gives.
//
// Density is what sharpness is. The configured height over the runtime's
// vertical field fixes it -- 2160 lines over 106.94 degrees measured as 800.4
// pixels per unit of tangent -- and both sides of the fitted rectangle are then
// however many pixels that density needs to reach the angles being asked for.
// So the picture is the same picture; what leaves is the part outside the
// lenses. Neither side is ever larger than the configured one: this may save
// work, never quietly ask for more.
bool FittedRect(float halfHorizontal,float halfVertical,unsigned configuredWidth,
                unsigned configuredHeight,float marginDegrees,
                unsigned& width,unsigned& height) noexcept {
    width=configuredWidth; height=configuredHeight;
    if(!(halfHorizontal>0) || !(halfVertical>0) || !configuredWidth || !configuredHeight) return false;
    const float margin=marginDegrees/kDegrees;
    const float wide=halfHorizontal+margin,tall=halfVertical+margin;
    if(wide>=1.5f || tall>=1.5f) return false;
    const float density=static_cast<float>(configuredHeight)/(2*std::tan(halfVertical));
    if(!std::isfinite(density) || density<=0) return false;
    auto span=[&](float half)->unsigned {
        const float exact=density*2*std::tan(half);
        if(!std::isfinite(exact) || exact<640 || exact>7680) return 0;
        return (static_cast<unsigned>(std::ceil(exact))+15)&~15u;
    };
    const unsigned wantWidth=span(wide),wantHeight=span(tall);
    if(!wantWidth || !wantHeight) return false;
    width=wantWidth<configuredWidth?wantWidth:configuredWidth;
    height=wantHeight<configuredHeight?wantHeight:configuredHeight;
    return true;
}
void ReportHeadsetFit(unsigned width,unsigned height) noexcept {
    float halfH=0,halfV=0; const char* name="";
    if(!edf6vr::g_openxr.HeadsetFrustum(halfH,halfV,name) || !width || !height) return;
    float advice=0,separation=0;
    const float renderedVertical=edf6vr::g_openxr.StereoAdvice(advice,separation) && advice>0
        ?advice*0.5f:halfV;
    const float renderedHalfH=std::atan(std::tan(renderedVertical)
        *static_cast<float>(width)/static_cast<float>(height));
    unsigned idealWidth=0,idealHeight=0;
    FittedRect(halfH,halfV,g_configuredWidth,g_configuredHeight,g_headsetFovMargin,idealWidth,idealHeight);
    const float renderedHalfV=renderedVertical;
    Log("FOVFIT match=%d headset=%s need=%.2f/%.2f rendered=%.2f/%.2f headroom=%+.2f/%+.2fdeg "
        "rect=%ux%u ideal=%ux%u configured=%ux%u margin=%.2f",
        g_matchHeadsetFov?1:0,name&&name[0]?name:"unnamed",
        halfH*kDegrees,halfV*kDegrees,renderedHalfH*kDegrees,renderedHalfV*kDegrees,
        (renderedHalfH-halfH)*kDegrees,(renderedHalfV-halfV)*kDegrees,
        width,height,idealWidth,idealHeight,g_configuredWidth,g_configuredHeight,g_headsetFovMargin);
    if(g_headsetFitWritten || !g_fitPath[0] || !(halfH>0) || !(halfV>0)) return;
    // Written once a session, and only from a frustum the runtime has actually
    // reported. Its own file: the personal INI is never rewritten.
    g_headsetFitWritten=true;
    g_fittedHalfHorizontal=halfH; g_fittedHalfVertical=halfV;
    wchar_t text[64]{},wide[128]{};
    MultiByteToWideChar(CP_UTF8,0,name&&name[0]?name:"unnamed",-1,wide,128);
    WritePrivateProfileStringW(L"HeadsetFit",L"System",wide,g_fitPath);
    _snwprintf_s(text,_TRUNCATE,L"%d",static_cast<int>(halfH*kDegrees*100.0f+0.5f));
    WritePrivateProfileStringW(L"HeadsetFit",L"HalfHorizontalCentiDeg",text,g_fitPath);
    _snwprintf_s(text,_TRUNCATE,L"%d",static_cast<int>(halfV*kDegrees*100.0f+0.5f));
    WritePrivateProfileStringW(L"HeadsetFit",L"HalfVerticalCentiDeg",text,g_fitPath);
    Log("FOVFIT measurement saved to the fit file; it takes effect on the next start");
}
edf6vr::IniMergeResult g_iniMerge{};
edf6vr::IniResetResult g_iniReset{};
// The settings generation this build ships ([Settings] Revision in packaging/EDF6VR.ini).
// 2 (for 2.0): an older file is replaced whole once (ResetOlderIni).
constexpr int kSettingsRevision=2;
bool g_iniMergeRan=false;
void MergeShippedIni() noexcept {
    const auto resource=FindResourceW(g_module,MAKEINTRESOURCEW(101),MAKEINTRESOURCEW(10));   // RT_RCDATA
    const auto loaded=resource?LoadResource(g_module,resource):nullptr;
    const auto data=loaded?static_cast<const char*>(LockResource(loaded)):nullptr;
    const auto size=resource?SizeofResource(g_module,resource):0;
    g_iniMergeRan=true;
    if(!data || !size) { g_iniMerge.failed=true; return; }
    g_iniReset=edf6vr::ResetOlderIni(data,size,g_iniPath,kSettingsRevision);
    g_iniMerge=edf6vr::MergeIniDefaults(data,size,g_iniPath);
}
void Settings() noexcept {
    wchar_t path[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(g_module,path,MAX_PATH);
    if(!length || length>=MAX_PATH) return;
    auto slash=wcsrchr(path,L'\\');
    if(!slash) return;
    slash[1]=0;
    wcscpy_s(g_logPath,path); wcscat_s(g_logPath,L"EDF6VR.log");
    wcscpy_s(g_modDirectory,path);
    if(wcscat_s(path,L"EDF6VR.ini")) return;
    wcscpy_s(g_iniPath,path);
    // Before anything is read: the INI is not in the package, so a fresh
    // install gets the shipped one and an update gets only its new keys.
    MergeShippedIni();
    g_enabled=GetPrivateProfileIntW(L"CameraTest",L"StartEnabled",0,path)!=0;
    g_yaw=ReadFloat(path,L"InitialYawDegrees",15,-180,180);
    g_pitch=ReadFloat(path,L"InitialPitchDegrees",0,-85,85);
    g_speed=ReadFloat(path,L"DegreesPerSecond",30,1,120);
    g_fpsEnabled=GetPrivateProfileIntW(L"FirstPerson",L"StartEnabled",0,path)!=0;
    g_hideBody=GetPrivateProfileIntW(L"FirstPerson",L"HideBody",1,path)!=0;
    g_eyeSettings.headUp=ReadFloat(path,L"HeadOffsetUp",0.15f,-0.5f,0.5f,L"FirstPerson");
    g_eyeSettings.fallbackHeight=ReadFloat(path,L"FallbackEyeHeight",1.70f,1,2.2f,L"FirstPerson");
    g_captureFrames=GetPrivateProfileIntW(L"Render",L"CapturePresent",1,path);
    g_measureWithoutVr=GetPrivateProfileIntW(L"Render",L"MeasureWithoutVr",1,path)!=0;
    g_allowLimiterToggle=GetPrivateProfileIntW(L"Render",L"LimiterToggle",1,path)!=0;
    g_removeFpsLimit=GetPrivateProfileIntW(L"Render",L"RemoveFpsLimit",1,path)!=0;
    g_earlyXrFrame=GetPrivateProfileIntW(L"Render",L"EarlyXrFrame",0,path)!=0;
    g_nativeWorldRequested=GetPrivateProfileIntW(L"Render",L"NativeWorldStereo",1,path)!=0;
    g_nativeStereoProbe=g_nativeWorldRequested || g_earlyXrFrame || GetPrivateProfileIntW(L"Render",L"NativeStereoProbe",0,path)!=0;
    g_stereoMode=GetPrivateProfileIntW(L"Render",L"StereoMode",0,path);
    g_warpSteps=ReadFloat(path,L"WarpSteps",64,4,256,L"Render");
    edf6vr::EnableSceneAA(GetPrivateProfileIntW(L"Render",L"SceneAA",0,path)!=0);
    Log("SCENEAA configured=%d controlled by INI; F1 reserved for ClearLoot",edf6vr::SceneAAEnabled()?1:0);
    edf6vr::EnableGpuProfile(GetPrivateProfileIntW(L"Render",L"GpuProfile",0,path)!=0);
    edf6vr::SetWarpTrialEnabled(GetPrivateProfileIntW(L"Render",L"WarpABTest",0,path)!=0);
    g_warpNearest=ReadFloat(path,L"WarpNearestMetres",0.15f,0.05f,5.0f,L"Render");
    g_nearKnee=ReadFloat(path,L"WarpNearKneeMetres",1.5f,0.1f,20.0f,L"Render");
    g_nearScale=ReadFloat(path,L"WarpNearScale",1.0f,0.0f,1.0f,L"Render");
    g_warpHudless=GetPrivateProfileIntW(L"Render",L"WarpKeepUi",1,path)!=0;
    g_warpDebug=static_cast<float>(GetPrivateProfileIntW(L"Render",L"WarpDebug",0,path));
    g_uiLayer=GetPrivateProfileIntW(L"Render",L"UiPanel",0,path)!=0;
    g_uiRedirect=GetPrivateProfileIntW(L"Render",L"UiRedirect",1,path)!=0;
    edf6vr::g_openxr.SetDesktopMirror(GetPrivateProfileIntW(L"Render",L"DesktopMirror",1,path)!=0);
    edf6vr::g_openxr.SetDesktopMirrorFov(ReadFloat(path,L"DesktopMirrorFov",90.f,0.f,140.f,L"Render"));
    edf6vr::SetDesktopMirrorUiScale(ReadFloat(path,L"DesktopMirrorUiScale",1.8f,0.5f,2.5f,L"Render"));
    g_headDamping=ReadFloat(path,L"HeadSteady",0.85f,0.0f,1.0f,L"FirstPerson");
    g_headRootLock=GetPrivateProfileIntW(L"FirstPerson",L"HeadRootLock",1,path)!=0;
    g_viewFollowsBody=GetPrivateProfileIntW(L"FirstPerson",L"ViewFollowsBody",0,path)!=0;
    g_headLimit=ReadFloat(path,L"HeadSteadyLimitMetres",0.15f,0.005f,1.0f,L"FirstPerson");
    g_roomDead=ReadFloat(path,L"RoomScaleDeadZoneMetres",0.20f,0.0f,2.0f,L"VR");
    g_roomSettle=ReadFloat(path,L"RoomScaleSettleSeconds",0.0f,0.0f,5.0f,L"VR");
    g_controllerAim=GetPrivateProfileIntW(L"VR",L"ControllerAim",1,path)!=0;
    g_padMode=GetPrivateProfileIntW(L"VR",L"PadMode",1,path);
    g_gestureRight=ReadFloat(path,L"GestureRightMetres",0.18f,-1.0f,1.0f,L"VR");
    g_gestureAhead=ReadFloat(path,L"GestureAheadMetres",0.0f,-1.0f,1.0f,L"VR");
    g_gestureUp=ReadFloat(path,L"GestureUpMetres",0.0f,-1.0f,1.0f,L"VR");
    g_gestureRadius=ReadFloat(path,L"GestureRadiusMetres",0.15f,0.03f,0.6f,L"VR");
    g_gestureRadiusLeave=ReadFloat(path,L"GestureLeaveRadiusMetres",0.19f,0.03f,0.8f,L"VR");
    g_gestureFront=ReadFloat(path,L"GestureFrontMetres",0.05f,-1.0f,1.0f,L"VR");
    g_gestureFrontLeave=ReadFloat(path,L"GestureFrontLeaveMetres",0.09f,-1.0f,1.0f,L"VR");
    g_testRequestPoints=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Diagnostics",L"MissionRequestPoints",0,path)),0,1000000);
    g_stickButtons=GetPrivateProfileIntW(L"VR",L"RightStickButtons",1,path)!=0;
    g_stickButtonEdge=ReadFloat(path,L"RightStickButtonEdge",0.6f,0.2f,0.95f,L"VR");
    g_gripPress=ReadFloat(path,L"GripPress",0.75f,0.05f,1.0f,L"VR");
    g_gripRelease=ReadFloat(path,L"GripRelease",0.45f,0.02f,1.0f,L"VR");
    g_gripForcePress=ReadFloat(path,L"GripForcePress",0.30f,0.02f,1.0f,L"VR");
    g_gripForceRelease=ReadFloat(path,L"GripForceRelease",0.10f,0.01f,1.0f,L"VR");
    g_stickFrame=GetPrivateProfileIntW(L"VR",L"StickFrame",1,path);
    g_dashFollowsHead=GetPrivateProfileIntW(L"VR",L"DashFollowsHead",1,path)!=0;
    g_reticleFollows=GetPrivateProfileIntW(L"Render",L"ReticleFollowsAim",1,path)!=0;
    g_reticlePixels=GetPrivateProfileIntW(L"Render",L"ReticlePixels",208,path);
    g_reticleDistance=ReadFloat(path,L"ReticleDistanceMetres",15.0f,1.0f,200.0f,L"Render");
    g_reticleScale=ReadFloat(path,L"ReticleScale",1.0f,0.05f,4.0f,L"Render");
    g_aimBandDegrees=ReadFloat(path,L"ControllerAimBandDegrees",0.0f,0.0f,45.0f,L"VR");
    // Anyone who ran an older build has 2.0 sitting in their own INI, and a
    // merge only adds keys it cannot find -- it never revises one. That value
    // was the old default, not a choice, so it is retired once and written
    // back. A band someone actually wants can be any other number.
    if(g_aimBandDegrees>1.999f && g_aimBandDegrees<2.001f) {
        g_aimBandDegrees=0.0f;
        RememberIn(L"VR",L"ControllerAimBandDegrees",0.0f);
        Log("AIMBAND retired the old 2.0 default; no dead band. Set ControllerAimBandDegrees yourself to keep one.");
    }
    g_uiWidth=ReadFloat(path,L"UiPanelWidthMetres",2.2f,0.05f,10.0f,L"Render");
    g_uiDistance=ReadFloat(path,L"UiPanelDistanceMetres",1.4f,0.2f,20.0f,L"Render");
    g_warpAlternate=GetPrivateProfileIntW(L"Render",L"WarpAlternateEye",0,path)!=0;
    g_swapEyes=GetPrivateProfileIntW(L"Render",L"SwapEyes",0,path)!=0;
    g_ipdScale=ReadFloat(path,L"IpdScale",1.0f,0.0f,3.0f,L"Render");
    g_eyeSource=GetPrivateProfileIntW(L"Render",L"EyeSource",1,path);
    g_fovScale=ReadFloat(path,L"FovScale",1.0f,0.4f,1.2f,L"Render");
    g_logGamepad=GetPrivateProfileIntW(L"Diagnostics",L"LogGamepad",1,path)!=0;
    g_depthProbe=GetPrivateProfileIntW(L"Diagnostics",L"DepthProbe",0,path)!=0;
    g_firingPresentationTrace=GetPrivateProfileIntW(L"Diagnostics",L"FiringPresentationTrace",0,path)!=0;
    // Building the second eye needs to know which texture the scene depth is,
    // which is what the survey settles, so mode 4 turns it on regardless.
    if(g_stereoMode>=4) g_depthProbe=true;
    g_describeSwapChain=GetPrivateProfileIntW(L"Render",L"DescribeSwapChain",1,path)!=0;
    g_captureDevice=GetPrivateProfileIntW(L"Render",L"CaptureDevice",1,path)!=0;
    g_headsetDisplay=GetPrivateProfileIntW(L"Render",L"HeadsetDisplay",0,path)!=0;
    g_vrEnabled=GetPrivateProfileIntW(L"VR",L"StartEnabled",0,path)!=0;
    g_vrAutoStart=GetPrivateProfileIntW(L"VR",L"AutoStart",1,path)!=0;
    g_vrRotation=GetPrivateProfileIntW(L"VR",L"HeadRotation",1,path)!=0;
    g_vrRoomScale=GetPrivateProfileIntW(L"VR",L"RoomScaleWalk",1,path)!=0;
    g_vrCameraHeight=GetPrivateProfileIntW(L"VR",L"HeadHeightCameraOnly",1,path)!=0;
    g_leftHanded=GetPrivateProfileIntW(L"LeftHanded",L"LeftHanded",0,path)!=0;
    g_leftHandedSticks=GetPrivateProfileIntW(L"LeftHanded",L"LeftHandedSticks",1,path)!=0;
    g_vrMouseYaw=GetPrivateProfileIntW(L"VR",L"MouseAdjustsYawOffset",1,path)!=0;
    g_vrRoomScaleGain=ReadFloat(path,L"RoomScaleGain",1.0f,0.0f,3.0f,L"VR");
    g_vrMaxRoomSpeed=ReadFloat(path,L"RoomScaleMaxSpeed",4.0f,0.5f,10.0f,L"VR");
    g_vrHeightScale=ReadFloat(path,L"HeadHeightScale",1.0f,0.0f,2.0f,L"VR");
    g_rollSign=(GetPrivateProfileIntW(L"VR",L"RollSign",1,path)<0)?-1:1;
    const int sign=GetPrivateProfileIntW(L"VR",L"PitchSign",0,path);
    if(sign>0) { g_pitchSign=1; g_pitchLocked=true; }
    else if(sign<0) { g_pitchSign=-1; g_pitchLocked=true; }
    if(g_fpsEnabled) g_enabled=false;
}

#include "class_support.h"
#include "ranger_dual.h"
#include "fencer_dual.h"
#include "guide_probe.h"
#include "action_weapon_visibility.h"
#include "tracked_weapon_bounds.h"
#include "audio_health.h"

void StopVr(const char* reason) noexcept {
    PublishActionHands(false);
    g_fastDrawWeapon.store(nullptr);g_fastDrawLeft.store(nullptr);g_fastDrawBody.store(nullptr);
    ResetRangerDual();
    edf6vr::RefreshNativeWorld(false,0);
    edf6vr::SetNativeWorldQueueEnabled(false);
    edf6vr::SetLegacyPreUiCapture(true);
    edf6vr::SetUiRedirect(false);
    g_vehicleMounted=false;g_vehicleSeen=0;g_vehicleReference=false;g_vehiclePositionReference=false;
    edf6vr::ClearCockpitPose();g_cockpitRig={};
    ClearClassPresentation();
    edf6vr::ClearRadarHeading();
    edf6vr::ClearLaserMuzzle();
    g_nativeZoom=g_renderZoom=1;
    edf6vr::g_openxr.SetBinocularZoom(1);
    if(!g_vrEnabled) return;
    // Headset mode needs the callback to finish asynchronous GPU/XR shutdown.
    // Once stopped RenderFrame is a no-op; no more copies or XR frames run.
    if(edf6vr::g_openxr.Mode()!=edf6vr::XrMode::HeadsetDisplay)
        edf6vr::SetFrameCallback(nullptr);
    g_vrEnabled=false;
    PublishFencerPad({}); g_fencerTurn.store(0);
    g_heightResetRequested.store(false,std::memory_order_release);
    g_haveReference=false;
    g_headAnchor={}; g_headAnchorOwner=nullptr;
    g_previousMove=MoveSample{};
    g_hmdHeightOffset=0;
    Log("F11: VR OFF (%s)",reason);
}

void StartVr() noexcept {
    if(!g_vrReady || g_faulted) { Log("F11 REFUSED: VR hooks unavailable or faulted"); return; }
    if(g_captureFrames==1 && !edf6vr::PresentCaptureInstalled()) {
        // Installed here rather than during load: the game is fully up by now,
        // and a failure can only affect this session instead of startup.
        if(!edf6vr::InstallPresentCapture(&LogRender,g_describeSwapChain))
            Log("RENDER: present capture unavailable: %s",edf6vr::PresentCaptureStatus());
    }
    void* device=nullptr;
    void* context=nullptr;
    const bool haveDevice=edf6vr::GameDevice(device,context);
    // Same reasoning as the present hook: the game is fully up by the time VR
    // is switched on, so a bad probe can only spoil the session that asked for it.
    if(g_depthProbe && haveDevice) edf6vr::InstallDepthProbe(&LogRender);
    auto mode=edf6vr::XrMode::TrackingOnly;
    if(g_headsetDisplay && haveDevice && edf6vr::PresentCaptureInstalled())
        mode=edf6vr::XrMode::HeadsetDisplay;
    else if(g_headsetDisplay)
        Log("RENDER: headset display wanted but device=%d presentHook=%d; tracking only",
            haveDevice,edf6vr::PresentCaptureInstalled());
    if(!g_fpsReady) { Log("F11 REFUSED: first person is not available"); return; }
    if(!edf6vr::g_openxr.Running() && !edf6vr::g_openxr.Start(&LogText,mode,device,context)) {
        Log("F11 REFUSED: %s",edf6vr::g_openxr.Status());
        return;
    }
    // Only after the session exists, so no frame arrives before it is ready.
    if(edf6vr::g_openxr.Mode()==edf6vr::XrMode::HeadsetDisplay) {
        edf6vr::g_openxr.SetFovScale(g_fovScale);
        edf6vr::g_openxr.SetDisplayMode(
            g_nativeTactical?edf6vr::XrDisplayMode::Quad:
            g_stereoMode>=2?edf6vr::XrDisplayMode::ProjectionStereo:
            g_stereoMode==1?edf6vr::XrDisplayMode::ProjectionMono:
                            edf6vr::XrDisplayMode::Quad);
        g_frameTrace=3;
        edf6vr::SetFrameCallback(&OnGameFrame);
    }
    g_fpsEnabled=true;
    g_enabled=false;
    g_vrEnabled=true;
    g_recenterRequested=true;
    g_haveReference=false;
    g_previousMove=MoveSample{};
    Log("F11: VR ON. rotation=%d roomScale=%d heightCameraOnly=%d mouseYaw=%d status=%s",
        g_vrRotation,g_vrRoomScale,g_vrCameraHeight,g_vrMouseYaw,edf6vr::g_openxr.Status());
    Log("VR signs: pitchSign=%+d (locked=%d) rollSign=%+d",g_pitchSign,g_pitchLocked,g_rollSign);
    Log("VR stereo: stereoMode=%d swapEyes=%d ipdScale=%.3f fovScale=%.3f (0 quad, 1 world mono, 2+ world stereo)",
        g_stereoMode,g_swapEyes,g_ipdScale,g_fovScale);
    // Said in the hands, because the screen says nothing about when the
    // controllers start working.
    edf6vr::g_openxr.Buzz(0,0.08f,0.6f);
    edf6vr::g_openxr.Buzz(1,0.08f,0.6f);
    Log("VR display: wanted=%d mode=%s gameDevice=%p gameContext=%p presentHook=%d deviceHook=%d",
        g_headsetDisplay,
        edf6vr::g_openxr.Mode()==edf6vr::XrMode::HeadsetDisplay?"headset":"tracking only",
        device,context,edf6vr::PresentCaptureInstalled(),edf6vr::DeviceCaptureInstalled());
}

// The pad exists at the title screen; the controllers did not.
//
// VR is started from HookUpdate, the camera update hook, which only runs once
// the game has a camera -- that is, once it is in the base or a mission. At the
// title screen it never runs, so the session never started, so the controllers
// reported nothing, so the buttons synthesised from them were all zero. That is
// the whole reason the title screen needed a real controller: not the pad
// bridge, which had been reading the game's own poll correctly the entire time.
//
// Tracking-only sessions make their own D3D11 device and drive their own frame
// loop, so nothing about them needs the game to be drawing. One is started here,
// from the watchdog thread, as soon as the plugin is up. StartVr hands it back
// when the headset display becomes possible.
// VR starts at the first frame the game presents, on the thread that presents it.
//
// It used to start from the camera update hook, which does not run at the title
// screen, so the controllers were dead there and the game had to be started with
// a real pad. Standing up a second, tracking-only session for the title and
// handing it back later did fix the controllers, and crashed on the handover:
// stopping one session and starting another changes the runtime, the device and
// the frame callback under a thread that is midway through a game frame.
//
// So there is one session, it displays, and it starts as early as the game will
// allow -- which is the first present, a few seconds in. The title screen
// arrives on the panel, which is where the player would rather have it anyway:
// no waiting for the game before putting the headset on.
// Whether this process owns the foreground window. A game that stops reading a
// pad the moment it loses focus would look exactly like what is happening, and
// starting a VR session is a thing that can take focus away.
bool Foreground() noexcept {
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}

// A refusal that no retry can change. Each attempt builds an OpenXR instance
// inside the present call, which holds the frame for over two seconds, so
// retrying this every three seconds is what a player sees as the game freezing
// every few seconds (1.3.0 report: RTX 4060, game on another adapter).
bool VrStartHopeless() noexcept {
    static bool told=false;
    if(!std::strstr(edf6vr::g_openxr.Status(),"different adapter")) return false;
    if(!told) {
        told=true;
        Log("VR: not retrying. The game is drawn by a different graphics card than the one "
            "SteamVR uses. Plug the monitor into the graphics card, or set EDF6.exe to "
            "High performance in Windows Settings > Display > Graphics, then restart");
    }
    return true;
}

void BootstrapFrame(void*,unsigned,unsigned,unsigned) {
    static ULONGLONG tried=0;
    static unsigned attempts=0;
    const ULONGLONG now=GetTickCount64();
    if(tried && now-tried<3000) return;
    tried=now;
    if(g_faulted || g_vrUserStopped || !g_vrAutoStart || edf6vr::g_openxr.Running()) return;
    if(attempts>=40 || VrStartHopeless()) return;
    ++attempts;
    StartVr();
}

// The present hook is what makes that first frame reach us, and it used to be
// installed inside StartVr -- which could not run until the camera existed. It
// goes in as soon as the game has a device, which it has from the title screen.
bool g_startAtFirstFrame=true;

void ArmVrAtFirstFrame() noexcept {
    if(!g_startAtFirstFrame) return;   // back to starting with the first camera
    if(g_inputOnlyVr || g_faulted || g_vrUserStopped || !g_vrAutoStart) return;
    if(g_captureFrames!=1 || edf6vr::PresentCaptureInstalled()) return;
    void* device=nullptr; void* context=nullptr;
    if(!edf6vr::GameDevice(device,context) || !device) return;
    if(!edf6vr::InstallPresentCapture(&LogRender,g_describeSwapChain)) {
        Log("RENDER: present capture unavailable: %s",edf6vr::PresentCaptureStatus());
        return;
    }
    g_inputOnlyVr=true;      // armed once; the name is kept for the INI-free flag
    edf6vr::SetFrameCallback(&BootstrapFrame);
    Log("VR: armed at the first presented frame, so the title screen is already in the headset");
}

// Where the support hand is, and what it does to the aim.
//
// Called with the firing hand's own basis, which it may replace: everything
// downstream reads yaw, pitchUp, forward and up, so putting the two-handed
// direction in here leaves the rest of the aim path -- the anchor band, the
// write, the reticle -- untouched and unaware.
void TwoHanded(edf6vr::HeadBasis& hand,const float rightAt[3],const float rightRot[4]) noexcept {
    g_twoHandWhereValid=false;
    if(g_dualActive.load()) { g_twoHandOn=false; g_twoHandDirValid=false; return; }
    float leftAt[3]{},leftRot[4]{};
    if(!g_twoHandAim) { g_twoHandOn=false; return; }
    // The line between the hands has to be the line between the CONTROLLERS.
    // HandPose is the aim pose -- a point out in front of the controller,
    // angled its own way -- and the two controllers on a gunstock are not
    // mounted at the same angle, so the line between their aim poses is not
    // the rail. Worse, turning the rig swings both of those points, and the
    // swings partly cancel the turn: the aim answers a movement of the front
    // hand with less than the movement, which is the soft patch around that
    // hand. On this rig the two were read 5 cm apart sideways and 5 cm in
    // height while the controllers were physically in line. Grip is the palm,
    // and the palms are on the rail. Aim poses remain the fallback.
    float originAt[3]{},unusedRot[4]{};
    const bool fromGrip=edf6vr::g_openxr.GripPose(0,leftAt,leftRot)
                     && edf6vr::g_openxr.GripPose(1,originAt,unusedRot);
    if(!fromGrip) {
        if(!edf6vr::g_openxr.HandPose(0,leftAt,leftRot)) { g_twoHandOn=false; return; }
        for(int j=0;j<3;++j) originAt[j]=rightAt[j];
    }

    // Positions come from the palms; the FRAME stays the aim pose's. Only the
    // two positions decide the line, and taking them from the palms is what
    // puts it on the rail. The frame is a different matter: the grip pose's -Z
    // runs down the handle, not where the weapon points, so measuring the zone
    // in it read the support hand 24 cm below the firing hand and 8 cm ahead
    // instead of 23 cm ahead, and the zone never matched. The zone's axes and
    // the numbers calibrated in them belong to the aim pose.
    // Trim the support hand before anything is measured from it, so the zone,
    // the line and the logged mean all agree with one another.
    if(g_twoHandLeftRight!=0 || g_twoHandLeftUp!=0 || g_twoHandLeftAhead!=0) {
        const edf6vr::Vec3 off=edf6vr::QuatRotate(
            edf6vr::Quat{rightRot[0],rightRot[1],rightRot[2],rightRot[3]},
            edf6vr::Vec3{g_twoHandLeftRight,g_twoHandLeftUp,-g_twoHandLeftAhead});
        leftAt[0]+=off.x; leftAt[1]+=off.y; leftAt[2]+=off.z;
    }
    const edf6vr::Quat back{-rightRot[0],-rightRot[1],-rightRot[2],rightRot[3]};
    const edf6vr::Vec3 gap{leftAt[0]-originAt[0],leftAt[1]-originAt[1],leftAt[2]-originAt[2]};
    const edf6vr::Vec3 local=edf6vr::QuatRotate(back,gap);
    // OpenXR looks down -Z, so the reach along the aim is the negated Z.
    g_twoHandWhere[0]=-local.z;      // ahead
    g_twoHandWhere[1]=local.x;       // right
    g_twoHandWhere[2]=local.y;       // up
    g_twoHandWhereValid=true;

    // Not read again here. The controllers are synced once a frame, on the
    // thread that builds the pad, and asking a second time from this one simply
    // failed -- which is why the measurement recorded nothing at all while the
    // zone itself was plainly working. The last state that thread saw is enough.
    if(g_lastTrigger>0.5f) {
        for(int j=0;j<3;++j) {
            if(g_twoHandWhere[j]<g_gripLow[j]) g_gripLow[j]=g_twoHandWhere[j];
            if(g_twoHandWhere[j]>g_gripHigh[j]) g_gripHigh[j]=g_twoHandWhere[j];
            g_gripSum[j]+=g_twoHandWhere[j];
        }
        ++g_gripSamples;
    }

    const float from[3]={g_twoHandWhere[0]-g_twoHandAhead,
                         g_twoHandWhere[1]-g_twoHandRight,
                         g_twoHandWhere[2]-g_twoHandUp};
    float away=0;
    for(int j=0;j<3;++j) away+=from[j]*from[j];
    away=std::sqrt(away);
    const bool inside=away<(g_twoHandOn?g_twoHandLeaveRadius:g_twoHandRadius);

    const float apart=std::sqrt(gap.x*gap.x+gap.y*gap.y+gap.z*gap.z);
    // Too close together and the line between the hands is mostly noise.
    const bool usable=inside && apart>=g_twoHandApart;
    if(usable!=g_twoHandOn) {
        g_twoHandOn=usable;
        edf6vr::g_openxr.Buzz(0,0.06f,usable?0.6f:0.3f);
        Log("TWOHAND %s at ahead=%.3f right=%.3f up=%.3f apart=%.3f source=%s",
            usable?"on":"off",g_twoHandWhere[0],g_twoHandWhere[1],g_twoHandWhere[2],apart,
            fromGrip?"grip":"aim(fallback)");
    }
    if(!g_twoHandOn) { g_twoHandDirValid=false; return; }

    // The barrel runs from the firing hand to the support hand.
    const edf6vr::Vec3 line=edf6vr::XrToGame(
        edf6vr::Vec3{gap.x/apart,gap.y/apart,gap.z/apart});
    float wanted[3]={line.x,line.y,line.z};

    // How far that has strayed from where the weapon is pointed. Past a soft
    // limit the excess is squeezed towards a maximum it never reaches, so a
    // badly placed hand steers less and less rather than stopping the weapon
    // dead at a wall -- fear-vr's SoftLimitedSteerAngle, and its reason.
    const float along=wanted[0]*hand.forward.x+wanted[1]*hand.forward.y+wanted[2]*hand.forward.z;
    const float angle=std::acos(std::clamp(along,-1.0f,1.0f));
    if(std::isfinite(angle) && angle>g_twoHandSoft) {
        const float range=g_twoHandMax-g_twoHandSoft;
        const float over=(angle-g_twoHandSoft)/(range>0.0001f?range:1.0f);
        const float allowed=g_twoHandSoft+range*(1.0f-std::exp(-over));
        // Rotate the weapon's own forward towards the hand line by that much.
        const float mix=(angle>0.0001f)?(allowed/angle):1.0f;
        for(int j=0;j<3;++j) {
            const float f=(j==0?hand.forward.x:(j==1?hand.forward.y:hand.forward.z));
            wanted[j]=f+(wanted[j]-f)*mix;
        }
    }
    // Smoothed, so tracking jitter in either hand does not shake the aim.
    if(!g_twoHandDirValid) { for(int j=0;j<3;++j) g_twoHandDir[j]=wanted[j]; g_twoHandDirValid=true; }
    else for(int j=0;j<3;++j)
        g_twoHandDir[j]+=(wanted[j]-g_twoHandDir[j])*(1.0f-g_twoHandSmoothing);
    float length=0;
    for(int j=0;j<3;++j) length+=g_twoHandDir[j]*g_twoHandDir[j];
    length=std::sqrt(length);
    if(!(length>0.0001f)) return;
    for(int j=0;j<3;++j) g_twoHandDir[j]/=length;

    hand.forward=edf6vr::Vec3{g_twoHandDir[0],g_twoHandDir[1],g_twoHandDir[2]};
    hand.yaw=std::atan2(hand.forward.x,hand.forward.z);
    hand.pitchUp=std::asin(std::clamp(hand.forward.y,-1.0f,1.0f));

    // Preserve the complete original gunstock pitch path above. Only shift the
    // horizontal support attachment; keep the uncorrected smoothing history so
    // this cannot feed back into pitch on the next frame.
    if(!g_twoHandGripAlign) return;
    float palmAt[3]{},gripRotation[4]{};
    const auto* q=edf6vr::g_openxr.GripPose(1,palmAt,gripRotation)?gripRotation:rightRot;
    const edf6vr::Vec3 filteredGap{g_twoHandDir[0]*apart,g_twoHandDir[1]*apart,g_twoHandDir[2]*apart};
    edf6vr::CorrectTwoHandLateralYaw({q[0],q[1],q[2],q[3]},filteredGap,
        WeaponRightTrim(),g_weaponRollTrim,g_weaponYaw,g_weaponPitch,hand);
}

void PollInput() noexcept {
    const auto now=GetTickCount64();
    const float dt=g_inputTime ? std::min(static_cast<float>(now-g_inputTime)*0.001f,0.05f) : 0;
    g_inputTime=now;
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if(pid!=GetCurrentProcessId()) {
        for(int i=0;i<kKeyCount;++i) g_keys[i]=(GetAsyncKeyState(VK_F6+i)&0x8000)!=0;
        g_limiterKey=(GetAsyncKeyState(VK_F5)&0x8000)!=0;
    g_dumpKey=(GetAsyncKeyState(VK_F4)&0x8000)!=0;
    g_moveKey=(GetAsyncKeyState(VK_F3)&0x8000)!=0;
    g_displayKey=(GetAsyncKeyState(VK_F2)&0x8000)!=0;
        return;
    }
    bool pressed[kKeyCount]{};
    for(int i=0;i<kKeyCount;++i) {
        const bool down=(GetAsyncKeyState(VK_F6+i)&0x8000)!=0;
        pressed[i]=down&&!g_keys[i]; g_keys[i]=down;
    }
    if(!g_devKeys) pressed[0]=pressed[1]=pressed[2]=pressed[4]=false;   // F6 F7 F8 F10
    if(pressed[0]) {
        g_heightResetRequested.store(false,std::memory_order_release);
        if(g_vrEnabled) g_vrStopRequested.store(true);
        if(g_fpsEnabled) { g_fpsEnabled=false; g_enabled=false; Log("F6: FPS OFF, body drawing restored"); }
        else { g_enabled=!g_enabled; Log("F6: test %s yaw=%.2f pitch=%.2f",g_enabled?"ON":"OFF",g_yaw,g_pitch); }
    }
    if(pressed[1]) { g_yaw=0; g_pitch=0; Log("F7: angles reset to zero"); }
    // A render resolution larger than the desktop is what buys sharpness in the
    // headset, and the window is then bigger than the screen, which puts the
    // settings menu out of reach. Nothing is wrong with that window: we take the
    // back buffer, not the screen. So walk its corners instead, and whichever
    // part of the menu is wanted can be brought into view.
    const bool displayDown=g_devKeys && (GetAsyncKeyState(VK_F2)&0x8000)!=0;
    if(displayDown && !g_displayKey) {
        g_displayForce=(g_displayForce+1)%3;
        edf6vr::g_openxr.ForceDisplay(g_displayForce);
        Log("F2: display %s",g_displayForce==0?"automatic":(g_displayForce==1?"board":"world"));
    }
    g_displayKey=displayDown;
    const bool moveDown=g_devKeys && (GetAsyncKeyState(VK_F3)&0x8000)!=0;
    if(moveDown && !g_moveKey) MoveGameWindow();
    g_moveKey=moveDown;
    const bool dumpDown=g_devKeys && (GetAsyncKeyState(VK_F4)&0x8000)!=0;
    if(dumpDown && !g_dumpKey) {
        edf6vr::RequestFrameDump();
        Log("F4: frame dump requested");
    }
    g_dumpKey=dumpDown;
    const bool limiterDown=(GetAsyncKeyState(VK_F5)&0x8000)!=0;
    const bool limiterPressed=limiterDown && !g_limiterKey;
    g_limiterKey=limiterDown;
    if(limiterPressed) {
        // Diagnostic only, and only once a mission is running: the menus need
        // the stock rate to stay usable.
        int sleepNop=0, branchNop=0;
        const int state=ReadLimiterState(sleepNop,branchNop);
        if(!g_allowLimiterToggle) Log("F5 REFUSED: LimiterToggle=0");
        else if(state!=1 && state!=2) Log("F5 REFUSED: unexpected limiter bytes (sleepNop=%d branchNop=%d)",sleepNop,branchNop);
        else if(!SetLimiterRemoved(state==1))
            Log("F5 REFUSED: not on the frame loop thread, or the bytes moved");
        else {
            g_removeFpsLimit=state==1;
            edf6vr::SetPresentUncapped(false); // live mission update renews only when enabled
            Log("F5: internal frame wait %s; automatic mission uncap=%d (gameplay above 60 fps UNVERIFIED)",
                state==1?"REMOVED":"RESTORED",g_removeFpsLimit);
        }
    }
    if(pressed[2]) { if(g_vrEnabled) g_vrStopRequested.store(true); g_fpsEnabled=false; g_enabled=true; g_yaw=15; g_pitch=0; Log("F8: rotation test ON, FPS OFF, fixed yaw=15 pitch=0"); }
    if(pressed[3]) {
        if(g_fpsReady && !g_faulted) {
            if(g_fpsEnabled && g_vrEnabled) g_vrStopRequested.store(true);
            g_fpsEnabled=!g_fpsEnabled; g_enabled=false; g_yaw=0; g_pitch=0;
            Log("F9: FPS %s; stock mouse/stick aim, hideBody=%d",g_fpsEnabled?"ON":"OFF",g_hideBody);
        } else Log("F9 REFUSED: FPS hooks unavailable or faulted");
    }
    if(pressed[4]) { g_hideBody=!g_hideBody; Log("F10: own body hiding %s (FPS diagnostic)",g_hideBody?"ON":"OFF"); }
    if(pressed[5]) {
        // OpenXR startup and shutdown can block for a while, so never run them
        // while the render thread may be waiting on our lock.
        if(g_vrEnabled) g_vrStopRequested.store(true); else g_vrStartRequested.store(true);
    }
    if(pressed[6]) { g_recenterRequested=true; edf6vr::g_openxr.RecenterBoard(); Log("F12: VR recenter requested"); }
    if(g_enabled) {
        const int yaw=((GetAsyncKeyState(VK_NUMPAD6)&0x8000)?1:0)-((GetAsyncKeyState(VK_NUMPAD4)&0x8000)?1:0);
        const int pitch=((GetAsyncKeyState(VK_NUMPAD8)&0x8000)?1:0)-((GetAsyncKeyState(VK_NUMPAD2)&0x8000)?1:0);
        g_yaw=std::clamp(g_yaw+static_cast<float>(yaw)*dt*g_speed,-180.0f,180.0f);
        g_pitch=std::clamp(g_pitch+static_cast<float>(pitch)*dt*g_speed,-85.0f,85.0f);
    }
}
CameraState* State(void* camera) noexcept {
    const auto now=GetTickCount64();
    for(auto& state:g_states) if(state.camera==camera) { state.seen=now; return &state; }
    // Reuse entries only after an update has restored their original matrix.
    for(auto& state:g_states) if(!state.camera || !state.saved.pending) {
        state={}; state.camera=camera; state.seen=now; return &state;
    }
    // Every slot is held by another camera that still has a matrix to put
    // back. A camera the game has destroyed never comes back for its
    // restore, and the mission end destroys one every time, so the slots ran
    // out after eight missions and first person stopped being applied for
    // the rest of the run -- the ninth camera of a session got nothing.
    // Nothing announces the destruction, so the slot that has gone longest
    // without an update is taken; its restore is moot, the object is gone.
    CameraState* oldest=nullptr;
    for(auto& state:g_states)
        if(now-state.seen>=kCameraSlotStaleMs && (!oldest || state.seen<oldest->seen)) oldest=&state;
    if(!oldest) return nullptr;
    Log("CAMERA slot taken from %p, last updated %llums ago; its pending restore is dropped",
        oldest->camera,now-oldest->seen);
    *oldest={}; oldest->camera=camera; oldest->seen=now;
    return oldest;
}
void RestoreBeforeUpdate(void* camera) noexcept {
    __try {
        // Only dereference the object passed by the game now; never stale cached objects.
        for(auto& state:g_states) if(state.camera==camera && state.saved.pending) {
            auto matrix=reinterpret_cast<edf6vr::Matrix*>(static_cast<unsigned char*>(camera)+edf6vr::kCameraMatrixOffset);
            if(edf6vr::Readable(matrix,sizeof(*matrix),true)) state.saved.Restore(*matrix);
            else { state.saved.pending=false; g_faulted=true; Log("FAULT: camera became unwritable; test disabled"); }
            break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_faulted=true; Log("FAULT: restore failed; test disabled"); }
}

// The relation between the aim pitch field and the camera's forward Y is a
// property of the game, so it can be measured whether or not we drive the field.
void LearnPitchSign(float pitchField,float forwardY) noexcept {
    if(g_pitchLocked || std::fabs(pitchField)<0.08f || std::fabs(forwardY)<0.05f) return;
    const int observed=((pitchField>0)==(forwardY>0))?1:-1;
    if(observed==g_pitchSign) { if(g_pitchVotes<8) ++g_pitchVotes; }
    else { g_pitchSign=observed; g_pitchVotes=1; }
    if(g_pitchVotes>=5) {
        g_pitchLocked=true;
        Log("VR: aim pitch sign resolved to %+d (pitchField=%.4f cameraForwardY=%.4f)",g_pitchSign,pitchField,forwardY);
    }
}

// Called under g_lock by the camera update. No old object is dereferenced.
void RefreshSoldierOwner(void* soldier) noexcept {
    const auto table=*static_cast<void**>(soldier);
    const auto id=*reinterpret_cast<const std::uint32_t*>(static_cast<unsigned char*>(soldier)+0x314);
    if(soldier==g_vrSoldier && table==g_vrSoldierTable && id==g_vrSoldierId) return;
    g_vrSoldier=soldier; g_vrSoldierTable=table; g_vrSoldierId=id;
    g_moveBasis={}; g_previousMove={}; g_previousTime=0;
    g_lastRoomInput[0]=g_lastRoomInput[1]=0;
    g_lastAimValid=false;
    g_haveReference=false; g_recenterRequested=true;
    g_hmdHeightOffset=0;
    g_headAnchor={}; g_headAnchorOwner=nullptr;
    g_holdCommand={};
    g_leftHoldCommand={};
    ResetRangerDual();
    PublishFencerPad({}); g_fencerTurn.store(0);
    g_nearestCandidate=nullptr; g_nearestHeld=0; g_heldSlot=0;
    g_weaponObject=nullptr; g_weaponTransformSeen=nullptr; g_weaponModelSeen=nullptr;
    g_weaponSeen=false; g_skeletonSeen=nullptr; g_muzzleWantValid=false;
    ClearClassPresentation();
    edf6vr::ClearLaserMuzzle(); edf6vr::ClearRadarHeading();
    Log("PLAYER owner changed: type=%s id=%u slots=%u; movement/weapon state reset",
        edf6vr::TypeName(g_image,soldier),id,edf6vr::WeaponSlotCount(soldier));
}

// Runs inside the soldier update, straight after the game has read the real
// controller, and before the update consumes the values. Everything VR writes
// therefore travels the game's own aim, movement and collision paths.
void ApplyVrInput(void* soldier) noexcept {
    __try {
        ++g_vrInputCalls;
        if(g_requestPointReady && g_testRequestPoints>0 && soldier==g_vrSoldier
            && GetTickCount64()-g_vrSoldierSeen<250 && g_bodyDraw && GetTickCount64()-g_bodyDraw<250
            && edf6vr::Readable(soldier,0x318) && *static_cast<void**>(soldier)==g_vrSoldierTable
            && *reinterpret_cast<const unsigned*>(static_cast<unsigned char*>(soldier)+0x314)==g_vrSoldierId) {
            bool allocated=false;
            const int gift=edf6vr::GrantInitialRequestPoints(g_image,soldier,g_testRequestPoints,g_requestBudget,allocated);
            if(allocated) Log("REQUESTPOINTS initial-only applied allowance=%d reduced=%d reserve=%d owner=%p id=%u owned=%u requests=%u alreadyReady=%u credited=%u; no repeat recharge",
                g_testRequestPoints,gift,g_requestBudget.remaining,soldier,g_vrSoldierId,g_requestBudget.ownedCount,
                g_requestBudget.requests,g_requestBudget.alreadyReady,g_requestBudget.credited);
            static ULONGLONG pointReport=0;
            if(allocated || GetTickCount64()-pointReport>5000) {
                pointReport=GetTickCount64();
                const unsigned count=edf6vr::OwnedWeaponCount(soldier);
                Log("REQUESTPOINTS state initialApplied=%d reserve=%d ownerId=%u owned=%u",
                    g_requestBudget.applied,g_requestBudget.remaining,g_vrSoldierId,count);
                for(unsigned i=0;i<count;++i) {
                    auto w=static_cast<unsigned char*>(edf6vr::OwnedWeaponAt(soldier,i));
                    if(!edf6vr::Readable(w,0xE84)) continue;
                    Log("REQUESTPOINTS owned=%u type=%s owner120=%p reloadType=%d ammo=%d full=%d remaining=%d consumed=%.1f",
                        i,edf6vr::TypeName(g_image,w),*reinterpret_cast<void**>(w+0x120),
                        *reinterpret_cast<int*>(w+0x208),*reinterpret_cast<int*>(w+0xBE8),*reinterpret_cast<int*>(w+0x20C),
                        *reinterpret_cast<int*>(w+0xE68),*reinterpret_cast<float*>(w+0xE80));
                }
            }
        }
        if(!g_vrEnabled || g_faulted || soldier!=g_vrSoldier) return;
        if(GetTickCount64()-g_vrSoldierSeen>250) return;
        if(!edf6vr::IsSupportedSoldier(g_image,soldier)) return;
        if(g_nativeTactical) return; // native aerial targeting consumes unmodified input
        // Even before the first mounted camera update, do not alter aim, room
        // scale or the movement input consumed by a vehicle/seat.
        if(edf6vr::HasMountReference(soldier)) {
            PublishFencerPad({});g_fencerTurn=0;g_handAiming=false;
            return;
        }
        // A recycled owner can reach input before its first camera callback.
        if(!edf6vr::Readable(soldier,0x318) || *static_cast<void**>(soldier)!=g_vrSoldierTable
           || *reinterpret_cast<const std::uint32_t*>(static_cast<unsigned char*>(soldier)+0x314)!=g_vrSoldierId) return;
        edf6vr::HmdSample sample{};
        if(!edf6vr::g_openxr.Sample(sample)) return;
        edf6vr::HeadBasis head{};
        if(!edf6vr::HeadBasisFromXr(sample.orientation,head)) return;
        edf6vr::SoldierAim aim{};
        if(!edf6vr::ReadSoldierAim(soldier,aim)) return;

        const double now=Now();
        const double delta=(g_previousTime>0)?(now-g_previousTime):0;
        g_previousTime=now;
        const float dt=static_cast<float>(std::clamp(delta,0.002,0.1));

        const edf6vr::Vec3 headXr=edf6vr::XrToGame(sample.position);
        g_lastHeadXr=headXr;
        if(g_recenterRequested || !g_haveReference) {
            g_yawOffset=aim.yaw-head.yaw;
            g_referenceXr=headXr;
            g_lastXr=headXr;
            g_haveReference=true;
            g_recenterRequested=false;
            g_hmdHeightOffset=0;
            g_previousMove=MoveSample{};
            Log("VR recenter: yawOffset=%.4f rad, headYaw=%.4f, soldierYaw=%.4f, headRoom=(%.3f,%.3f,%.3f)",
                g_yawOffset,head.yaw,aim.yaw,headXr.x,headXr.y,headXr.z);
        }
        const bool fencer=FencerOwnsWeaponPose(soldier);
        if(fencer) {
            // Physical stick only, 90 degrees/sec reference turning. Native weapon
            // turn speed is still determined by the ordinary pad path below.
            if(g_vrMouseYaw) g_yawOffset-=std::clamp(g_fencerTurn.load(),-1.0f,1.0f)*dt*1.57079633f;
        } else if(g_vrMouseYaw && std::isfinite(aim.lookInput[1])) g_yawOffset+=aim.lookInput[1];

        g_appliedHeadSample=sample;
        g_lastHeadYaw=head.yaw; g_lastHeadPitch=head.pitchUp;
        g_hmdUpWorld=edf6vr::RotateY(head.up,g_yawOffset);
        // Where the head looks, in world axes, for the camera to be built along.
        const edf6vr::Vec3 headForward=edf6vr::RotateY(head.forward,g_yawOffset);
        g_headForwardWorld[0]=headForward.x;
        g_headForwardWorld[1]=headForward.y;
        g_headForwardWorld[2]=headForward.z;
        g_headForwardAt=GetTickCount64();
        // Roll only turns the camera about its own forward row, so flipping it
        // cannot disturb the aim, the movement basis or the head position.
        if(g_rollSign<0) { g_hmdUpWorld.x=-g_hmdUpWorld.x; g_hmdUpWorld.z=-g_hmdUpWorld.z; }
        if(sample.positionValid) {
            if(g_heightResetRequested.exchange(false,std::memory_order_acq_rel)) {
                g_referenceXr.y=headXr.y;
                Log("HEIGHT applied on foot: trackingY=%.3f; yaw and horizontal reference unchanged",headXr.y);
            }
            g_hmdHeightOffset=(headXr.y-g_referenceXr.y)*g_vrHeightScale;
        }

        // Learn how the game's move input maps onto world motion from the player's
        // own walking, and only while room scale is not adding anything of its own.
        if(g_previousMove.have && !g_moveBasis.locked
           && g_lastRoomInput[0]==0 && g_lastRoomInput[1]==0) {
            const double span=now-g_previousMove.time;
            if(span>0.004 && span<0.1) {
                const edf6vr::Vec3 world{(aim.position[0]-g_previousMove.x)/static_cast<float>(span),0,
                                         (aim.position[2]-g_previousMove.z)/static_cast<float>(span)};
                const edf6vr::Vec3 local=edf6vr::RotateY(world,-g_previousMove.yaw);
                edf6vr::MoveBasisObserve(g_moveBasis,g_previousMove.inX,g_previousMove.inZ,local.x,local.z);
                if(edf6vr::MoveBasisSolve(g_moveBasis))
                    Log("VR move basis locked: candidate=%d speed=%.3f m/s samples=%d",
                        g_moveBasis.candidate,g_moveBasis.scale,g_moveBasis.samples);
            }
        }

        float targetYaw=aim.yaw, targetPitch=aim.pitch;
        if(g_vrRotation) {
            targetYaw=head.yaw+g_yawOffset;
            targetPitch=static_cast<float>(g_pitchSign)*head.pitchUp;
            {
                // The rumble the game asked the pad for, sent to the hands. The
                // pad itself is built by the bridge when the game reads it, so
                // that it keeps working when this hook does not run.
                //
                // The game has one pad, so its firing rumble cannot say which
                // gun fired: it came to both hands for the right gun and to
                // neither for the left. While a shot is fresh the rumble goes to
                // the hand that fired -- both when the left hand is steadying the
                // weapon -- and a shot the game did not rumble for gets a buzz of
                // its own. Rumble with no shot behind it (hits, explosions) still
                // reaches both hands.
                float rumbleLeft=0,rumbleRight=0;
                const bool rumbled=edf6vr::TakeRumble(rumbleLeft,rumbleRight);
                const float strength=rumbleLeft>rumbleRight?rumbleLeft:rumbleRight;
                const auto tick=GetTickCount64();
                static unsigned long long seenShots[2]{};
                bool fresh[2]{},newShot[2]{};
                for(int h=0;h<2;++h) {
                    const auto count=g_shotCount[h].load(std::memory_order_relaxed);
                    newShot[h]=count!=seenShots[h]; seenShots[h]=count;
                    fresh[h]=tick-g_shotTick[h].load(std::memory_order_relaxed)<150;
                }
                const bool firing=fresh[0] || fresh[1];
                const bool bothFire=g_twoHandOn;
                auto buzzFiring=[&](float amount) {
                    for(int h=0;h<2;++h)
                        if(fresh[h] || bothFire) edf6vr::g_openxr.Buzz(h,0.08f,amount);
                };
                if(rumbled && strength>0.02f) {
                    if(firing) buzzFiring(strength);
                    else { edf6vr::g_openxr.Buzz(0,0.08f,strength); edf6vr::g_openxr.Buzz(1,0.08f,strength); }
                } else if(g_shotBuzz>0) {
                    for(int h=0;h<2;++h) if(newShot[h]) {
                        edf6vr::g_openxr.Buzz(h,0.06f,g_shotBuzz);
                        if(bothFire) edf6vr::g_openxr.Buzz(1-h,0.06f,g_shotBuzz);
                    }
                }
            }
            g_handAiming=false;
            if(g_controllerAim) {
                float handPosition[3]{}, handRotation[4]{};
                edf6vr::HeadBasis hand{};
                // The aim pose is a pose like any other, so the same conversion
                // the head uses gives the same kind of angles.
                if(edf6vr::g_openxr.HandPose(1,handPosition,handRotation)
                   && edf6vr::HeadBasisFromXr(edf6vr::Quat{handRotation[0],handRotation[1],
                                                           handRotation[2],handRotation[3]},hand)) {
                    if(!fencer) TwoHanded(hand,handPosition,handRotation);
                    // The angle trim goes on the aim itself, not on the picture.
                    //
                    // Turning only the model would put the shot somewhere the
                    // weapon is not pointing, which is worse than the misalignment
                    // it was meant to cure: a weapon whose angle does not match
                    // the hand holding it is unpleasant precisely because the two
                    // disagree. Every gunstock is held at its own angle, and this
                    // is what cancels that, so it has to cancel it for the shot as
                    // well. Applied here, the aim, the drawn weapon and the
                    // reticle all turn together, because all three are built from
                    // these angles.
                    //
                    // Positive yaw points the weapon to the player's right; the
                    // game's yaw grows the other way, hence the subtraction.
                    // Not while both hands are on it. These two are the trim
                    // that squares a ONE-handed grip with its controller, and
                    // on a gunstock the two palms are already on the rail --
                    // the line between them IS the barrel. Adding the one-hand
                    // correction on top of it is what tilted the stance.
                    //
                    // The trim is a turn of the controller in its own axes,
                    // not an amount added to the aim's yaw and pitch. Added to
                    // the angles it turned the barrel about the world's
                    // vertical and the world's horizon, which is a different
                    // relation to the hand wherever the hand points: pointed
                    // up, the 13 degrees of pitch trim went from "below the
                    // hand" to "behind it" as the wrist turned, and on over the
                    // top the angles flip and it jumped to the other side. The
                    // drawn weapon followed, which is the roll at the sky.
                    if(!g_twoHandOn && (g_weaponYaw!=0 || g_weaponPitch!=0)) {
                        edf6vr::HeadBasis trimmed{};
                        if(edf6vr::HeadBasisFromXr(edf6vr::TrimAimRotation(
                               edf6vr::Quat{handRotation[0],handRotation[1],handRotation[2],handRotation[3]},
                               g_weaponYaw,g_weaponPitch),trimmed))
                            hand=trimmed;
                    }
                    // The heading exactly as the controller points it, for a
                    // Ranger or an Air Raider. Within 8 degrees of vertical
                    // HeadBasisFromXr leans on the controller's up, because a
                    // Wing Diver flies along her heading and one made of noise
                    // sent her off; the Fencer's heavy aim chases its target
                    // the same way. For these two the heading is only where the
                    // shot goes, and the lean put it a few degrees off the
                    // barrel with the wrist turned. Only the last hundredth of
                    // a degree, where there is no heading at all, keeps it.
                    if(!fencer && !edf6vr::HasType(g_image,soldier,".?AVPaleWing@@")) {
                        const float flat=hand.forward.x*hand.forward.x+hand.forward.z*hand.forward.z;
                        if(flat>1e-8f) hand.yaw=std::atan2(hand.forward.x,hand.forward.z);
                    }
                    // How far the wrist is turned about the line the weapon
                    // fires along. Measured against the upright that has no roll
                    // in it: the world up with the forward part taken out.
                    if(g_weaponRoll) {
                        const edf6vr::Vec3 f=hand.forward;
                        const float lean=f.y;
                        edf6vr::Vec3 upright{-f.x*lean,1-f.y*lean,-f.z*lean};
                        float size=std::sqrt(upright.x*upright.x+upright.y*upright.y
                                            +upright.z*upright.z);
                        // Roll is measured against an upright that is the world's
                        // up with the aim taken out of it, and that upright
                        // vanishes when the aim is the world's up: point a weapon
                        // straight at the sky and there is no such thing as its
                        // roll. Near enough to vertical the answer is noise, and
                        // it was swinging through a hundred and fifty degrees --
                        // which then swung the weapon, since the offset from the
                        // hand turns with it. Inside that cone the last good
                        // answer is kept.
                        if(size>0.30f) {
                            upright.x/=size; upright.y/=size; upright.z/=size;
                            const edf6vr::Vec3 side{f.y*upright.z-f.z*upright.y,
                                                    f.z*upright.x-f.x*upright.z,
                                                    f.x*upright.y-f.y*upright.x};
                            const float across=side.x*hand.up.x+side.y*hand.up.y+side.z*hand.up.z;
                            const float upright2=upright.x*hand.up.x+upright.y*hand.up.y
                                                +upright.z*hand.up.z;
                            // Negated: the sideways axis was corrected to point
                            // right, and the roll turned with it.
                            const float roll=-std::atan2(across,upright2);
                            if(std::isfinite(roll)) g_weaponRollNow=g_weaponRollSign*roll;
                        }
                    }
                    float wantYaw=hand.yaw+g_yawOffset;
                    // A small band about the written yaw. The soldier turns his
                    // whole body to the aim, and a body turn is a step, so hand
                    // tremor with no band would have him shuffling on the spot
                    // exactly as the room scale jitter did. Wider than a tremor
                    // and narrower than a gesture: past the band the anchor is
                    // dragged, so beyond it the weapon follows one for one.
                    const float band=g_aimBandDegrees*3.14159265f/180.0f;
                    if(!g_aimAnchored) { g_aimAnchorYaw=wantYaw; g_aimAnchored=true; }
                    float swing=wantYaw-g_aimAnchorYaw;
                    while(swing>3.14159265f) swing-=6.28318531f;
                    while(swing<-3.14159265f) swing+=6.28318531f;
                    if(std::fabs(swing)>band)
                        g_aimAnchorYaw=wantYaw-(swing>0?band:-band);
                    targetYaw=g_aimAnchorYaw;
                    targetPitch=static_cast<float>(g_pitchSign)*hand.pitchUp;
                    g_handAiming=true;
                    // The direction itself, for the drawn weapon. Rebuilt from
                    // the yaw and pitch it lost which way the hand faced near
                    // vertical: the yaw there is partly the controller's up
                    // (HeadBasisFromXr's pole blend, which keeps a Wing Diver
                    // from spinning), so it is not the heading of the vector
                    // any more. Turned by what the band held back, which is
                    // nothing unless ControllerAimBandDegrees is set.
                    const edf6vr::Vec3 along=edf6vr::RotateY(hand.forward,targetYaw-hand.yaw);
                    g_lastWrittenAhead[0]=along.x; g_lastWrittenAhead[1]=along.y; g_lastWrittenAhead[2]=along.z;
                }
            }
            // The Ranger's dash leaves along the body's facing at the instant
            // the state is entered, and the facing here is the controller's, so
            // the run went wherever the weapon happened to point. For the length
            // of the dash the head's yaw is sent instead, which puts the run
            // where the player is looking -- the same place walking already
            // goes. The band anchor is carried along so the aim does not have to
            // swing all the way back when the dash ends.
            //
            // g_handAiming is deliberately left alone: it also gates the tracked
            // weapon pose, the laser and the reticle, and none of those should
            // change because the soldier happens to be running.
            const bool dashing=g_dashFollowsHead && g_handAiming && !fencer
                && edf6vr::RangerDashing(g_image,soldier);
            if(dashing) {
                // What the hand asked for, kept for everything that is drawn.
                // The band anchor is left where it was, so the aim goes on
                // following the controller underneath the run.
                g_dashAimYaw.store(targetYaw,std::memory_order_relaxed);
                targetYaw=head.yaw+g_yawOffset;
                ++g_dashFrames;
            }
            g_dashAiming.store(dashing,std::memory_order_release);
            if(dashing!=g_dashing) { g_dashing=dashing; if(dashing) ++g_dashStarts; }
            // Other classes use absolute aim and clear look below. Fencer preserves
            // both native angle fields and the look delta produced by XInput.
            if(WriteTrackedAim(soldier,targetPitch,targetYaw,g_handAiming)) ++g_vrAimWrites;
            // The old persistent muzzle write fed back into weapon selection and
            // later placement. This visual-only borrow leaves gameplay transforms
            // and shot origin with the game; aim-angle/controller logic is unchanged.
            g_lastWrittenPitch=fencer?aim.pitch:targetPitch;
            // Where the weapon sits along the aim. The dash turned the soldier,
            // not the hand, so this stays on the hand's heading.
            g_lastWrittenYaw=fencer?aim.yaw
                :(dashing?g_dashAimYaw.load(std::memory_order_relaxed):targetYaw);
            g_lastWrittenAheadValid=!fencer && g_handAiming;
        }

        if(!g_vrRotation || !fencer) PublishFencerPad({});
        if(fencer) targetYaw=aim.yaw; // locomotion follows current native aim, never the controller target
        float moveX=aim.moveInput[0], moveZ=aim.moveInput[1];
        // Forward on the stick means where the head is looking. The soldier moves
        // relative to his own facing, and his facing is the weapon, so the input
        // is turned by the angle between them here - at the last writer, which is
        // the only place nothing can undo it. Turning the synthesised pad stick
        // instead had no effect at all, in any direction, because this write
        // lands on top of whatever the pad produced.
        //
        // The room scale part is added after, and must not be turned: it is
        // built from a world direction and has already been put into the
        // soldier's frame with his own yaw.
        if(g_stickFrame && g_handAiming) {
            float turn=(head.yaw+g_yawOffset)-aim.yaw;
            while(turn>3.14159265f) turn-=6.28318531f;
            while(turn<-3.14159265f) turn+=6.28318531f;
            if(g_stickFrame==2) turn=-turn;
            const float c=std::cos(turn), sn=std::sin(turn);
            const float x=moveX, z=moveZ;
            moveX=x*c+z*sn;
            moveZ=-x*sn+z*c;
            g_moveTurn=turn*180.0f/3.14159265f;
        } else g_moveTurn=0;
        g_moveSent[0]=moveX; g_moveSent[1]=moveZ;
        g_lastRoomInput[0]=0; g_lastRoomInput[1]=0;
        if(g_vrRoomScale && sample.positionValid) {
            // EDF6 takes a step for almost any movement input, and a step swings
            // the head bone, so a few millimetres of standing sway becomes a
            // permanent shuffle and the view wobbles with it. Damping the head
            // afterwards only hides that; not asking for the step is the fix.
            //
            // So the walk is measured from an anchor rather than from the last
            // frame. Inside the dead zone the anchor does not move and no input
            // is produced at all. Outside it the anchor is dragged along so it
            // trails at exactly the zone radius, and beyond that the character
            // follows one for one with nothing lost. Standing still long enough
            // brings the anchor back under the head, so the full zone is
            // available again in every direction from wherever you stopped.
            if(!g_roomAnchored) {
                g_roomAnchor[0]=headXr.x; g_roomAnchor[1]=headXr.z; g_roomAnchored=true;
            }
            float dx=headXr.x-g_roomAnchor[0], dz=headXr.z-g_roomAnchor[1];
            g_roomAway=std::sqrt(dx*dx+dz*dz);
            if(g_roomAway>4.0f) {   // recentred, respawned, or a vehicle
                g_roomAnchor[0]=headXr.x; g_roomAnchor[1]=headXr.z;
                dx=dz=0; g_roomAway=0;
            }
            float movedX=0, movedZ=0;
            if(g_roomAway>g_roomDead) {
                const float pull=(g_roomAway-g_roomDead)/g_roomAway;
                movedX=dx*pull; movedZ=dz*pull;
                g_roomAnchor[0]+=movedX; g_roomAnchor[1]+=movedZ;
            }
            // Held still for long enough, the zone recentres on the head.
            const float headSpeed=dt>0
                ?std::sqrt((headXr.x-g_lastXr.x)*(headXr.x-g_lastXr.x)
                          +(headXr.z-g_lastXr.z)*(headXr.z-g_lastXr.z))/dt:0.0f;
            const double nowSeconds=Now();
            if(headSpeed>0.08f) g_roomStillSince=nowSeconds;
            else if(g_roomSettle>0 && g_roomStillSince>0
                    && nowSeconds-g_roomStillSince>g_roomSettle) {
                g_roomAnchor[0]=headXr.x; g_roomAnchor[1]=headXr.z;
            }
            // What is left inside the zone is a lean: the feet stay where they
            // are and the view moves with the head, which is what leaning out
            // from behind cover is. The anchor drag above keeps this at the zone
            // radius or less, so the view can never run away from the body.
            const edf6vr::Vec3 lean=edf6vr::RotateY(
                edf6vr::Vec3{headXr.x-g_roomAnchor[0],0,headXr.z-g_roomAnchor[1]},g_yawOffset);
            g_roomLean[0]=lean.x; g_roomLean[1]=lean.z;

            const edf6vr::Vec3 step{movedX,0,movedZ};
            const edf6vr::Vec3 world=edf6vr::RotateY(step,g_yawOffset);
            float vx=world.x/dt*g_vrRoomScaleGain, vz=world.z/dt*g_vrRoomScaleGain;
            const float speed=std::sqrt(vx*vx+vz*vz);
            if(speed>g_vrMaxRoomSpeed) { vx=vx/speed*g_vrMaxRoomSpeed; vz=vz/speed*g_vrMaxRoomSpeed; }
            float roomX=0, roomZ=0;
            if(g_moveBasis.locked && std::isfinite(vx) && std::isfinite(vz)
               && edf6vr::RoomScaleInput(g_moveBasis,targetYaw,vx,vz,roomX,roomZ)) {
                g_lastRoomInput[0]=roomX; g_lastRoomInput[1]=roomZ;
                moveX+=roomX; moveZ+=roomZ;
            }
        }
        if(sample.positionValid) g_lastXr=headXr;

        const float keepPitchLook=g_vrRotation && !fencer?0.0f:aim.lookInput[0];
        const float keepYawLook=g_vrRotation && !fencer?0.0f:aim.lookInput[1];
        if(edf6vr::WriteSoldierInput(soldier,moveX,moveZ,keepPitchLook,keepYawLook)) ++g_vrMoveWrites;
        edf6vr::SoldierAim after{};
        if(edf6vr::ReadSoldierAim(soldier,after)) {
            g_previousMove.have=true;
            g_previousMove.inX=after.moveInput[0];
            g_previousMove.inZ=after.moveInput[1];
            g_previousMove.yaw=after.yaw;
            g_previousMove.x=after.position[0];
            g_previousMove.z=after.position[2];
            g_previousMove.time=now;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        g_faulted=true;
        g_vrEnabled=false;
        Log("FAULT: VR input injection raised an exception; VR disabled until restart");
    }
}

// The game only ever calls 570650 directly, so its address is not required to be
// a Control Flow Guard target; skip the guard check for this one forwarding call.
// Put the arm bone where the hand is, and let the game derive the rest.
//
// This is what the weapon's own data has been saying all along. Every ranger
// weapon carries ModelConstraint "arms_r": the game hangs the weapon model off
// that bone, and it hangs the muzzle flash and the shot off the weapon. That is
// why a rocket launcher fires from the shoulder without anybody telling it to,
// and it is why writing muzzles into twenty-one different fields changed
// nothing -- every one of them is recomputed from this bone before it is read.
//
// So the bone is moved instead of its consequences. If the game derives the
// weapon from it after this runs, then the model, the flash and the shot all
// arrive together and most of the machinery in this file becomes unnecessary.
// If it derives them before, nothing will move and we will know that too --
// which is worth one launch, where guessing at fields was not.
//
// The body is not drawn in first person, so an arm in the wrong place costs
// nothing to look at. WeaponArmBone=0 leaves the bone alone.
// What was last written to the arm bone, and how far the game had moved it back
// by the start of the next tick.
//
// The bone is written every tick -- armBone reached 5693 in one session -- and
// the shot still leaves from the body, with a rocket launcher no different. So
// either the game does not derive the shot from this bone, or it recomputes the
// bone from the animation before anything reads it and our value never survives
// to be used. Those want opposite things done next, and one number separates
// them: read the bone again at the top of the following tick and see whether it
// still says what we said. Only the update thread touches any of this, and it is
// the same thread at both ends, so there is nothing to lock.
void* g_armNodeWritten=nullptr;
float g_armWrote[3]{};
bool g_armWroteValid=false;
float g_armDriftMin=1e30f, g_armDriftMax=0;

bool PlaceArmBone(void* armsNode,const edf6vr::WeaponHoldFrame& hand) noexcept {
    if(!armsNode) return false;
    __try {
        if(!edf6vr::Readable(armsNode,0xB0+64,true)) return false;
        for(int k=0;k<3;++k) {
            float length=0;
            for(int j=0;j<3;++j) {
                if(!std::isfinite(hand.axes[k][j])) return false;
                length+=hand.axes[k][j]*hand.axes[k][j];
            }
            if(!(length>0.9f && length<1.1f)) return false;
        }
        for(int j=0;j<3;++j) if(!std::isfinite(hand.palm[j])) return false;
        auto rows=reinterpret_cast<float*>(static_cast<unsigned char*>(armsNode)+0xB0);
        for(int k=0;k<3;++k) for(int j=0;j<3;++j) rows[k*4+j]=hand.axes[k][j];
        for(int j=0;j<3;++j) rows[0x30/4+j]=hand.palm[j];
        g_armNodeWritten=armsNode;
        for(int j=0;j<3;++j) g_armWrote[j]=hand.palm[j];
        g_armWroteValid=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Where the shot leaves from. Written from both ends of the tick, so that
// whether it is early or late is not one of the unknowns.
// Nothing in here may take g_lock. This is called from AfterUpdate, which is
// already inside it, held exclusively -- and an SRW lock is not recursive, so
// asking for it again, even shared, stops the thread dead and the game with it.
// That is exactly what happened: armBone=1, then a deadlock, and the stack taken
// off the frozen game read KERNELBASE's wait with nothing but this plugin above
// it. The same mistake was made once before in this file. The arm bone is
// written from AfterUpdate itself now, where the frame is in hand and no lock
// is needed.
// How far the game has moved the arm bone back since we wrote it.
void MeasureArmDrift() noexcept {
    if(!g_armWroteValid || !g_armNodeWritten) return;
    __try {
        if(!edf6vr::Readable(g_armNodeWritten,0xB0+64)) return;
        auto rows=reinterpret_cast<const float*>(
            static_cast<unsigned char*>(g_armNodeWritten)+0xB0);
        float drift=0;
        for(int j=0;j<3;++j) {
            const float now=rows[0x30/4+j];
            if(!std::isfinite(now)) return;
            const float d=now-g_armWrote[j];
            drift+=d*d;
        }
        drift=std::sqrt(drift);
        if(!std::isfinite(drift)) return;
        g_armDriftMin=std::min(g_armDriftMin,drift);
        g_armDriftMax=std::max(g_armDriftMax,drift);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void PlaceShotOrigin(void* soldier) noexcept {
    if(!soldier) return;
    // Before the game has had a chance to touch anything this tick.
    MeasureArmDrift();
    MeasureProbeDrift();
    WatchTheBone(soldier);
    KeepAimCentre(soldier);
    PlaceBoneProbe(soldier);
    if(!g_shotProbe || !g_muzzleWantValid) return;
    edf6vr::WeaponPose held{};
    bool found=false;
    for(unsigned slot=0;slot<edf6vr::WeaponSlotCount(soldier) && !found;++slot)
        if(edf6vr::ReadWeaponPose(soldier,slot,held)
           && (!g_weaponModelSeen || held.model==g_weaponModelSeen)) found=true;
    if(!found) return;
    (void)held;
}

// Put the muzzle into aim_center in the instant before the game reads it.
//
// The watch answered. Three addresses in EDF.dll touch aim_center from outside
// the skeleton machinery -- 6BF7EE and 6BF7F3, five bytes apart and so almost
// certainly two halves of one read, and 6EDEF1. The skeleton's own 1100xxx
// addresses are the animation putting the bone back, which is the overwriting
// that made every one of the eleven candidates untestable.
//
// Knowing that changes the problem. Writing the bone from a hook of ours is a
// guess about an order; breaking on the instruction that reads it is not. A
// debug register in execute mode stops the game *before* that instruction runs,
// which is the one moment when a value written to the bone cannot be recomputed
// away, because the read is next.
//
// Three registers, one per address, and a fourth left for the data watch.
// FireSites in the INI takes the addresses as offsets into EDF.dll; empty turns
// the whole thing off.
constexpr int kMaxFireSites=3;
// Where a bullet is built, found by decoding every rip-relative lea in .text
// and keeping the ones that load a bullet vtable:
//
//   BulletBase's own vtables, EDF.dll+0x179E048 and +0x179E0C0, are loaded at
//   22E9F0, 22EF2D and 22EFB4 -- a constructor storing both, three times over.
//   BulletBase::InitParam's vtable, +0x1769028, is loaded at 1002EE, 32EF66 and
//   6D28BC, and the last of those sits among the soldier and weapon code, a few
//   kilobytes from the matrix routines the watch kept catching.
//
// Those are lea instructions inside functions, not function entries, and the
// first attempt recorded rcx at one of them: 435E63D55F800000, which is a float
// pattern and not a pointer. By the time a constructor stores its vtable, the
// argument registers have long since been spent. So .pdata -- which carries the
// bounds of every one of the 97,419 functions in this binary -- was read to find
// the function each address belongs to:
//
//   22E9F0 lies in EDF.dll+0x22E9C0, 0x1B8 bytes, which is BulletBase's
//   constructor; 6D28BC lies in EDF.dll+0x6D2640, 0x140C bytes, in the soldier
//   and weapon code.
//
// Reading the stack at the bullet's constructor then gave the whole chain:
//
//   22E9C0 <- 28A9CA <- 691B85 <- 28A708 <- 11942C3 <- 6979A4
//
// which .pdata turns into EDF.dll+0x696FD0 (0x1349 bytes) and +0x691951 (0x258),
// both in the same few hundred kilobytes as the soldier and weapon code that
// every other trail has led to. Those are where a shot is made, so those are
// where the breakpoints go, and their arguments are the ones that will say where
// a bullet is told to start.
//
// Watching BulletBase's constructor then named its caller: EDF.dll+0x28A9CA,
// which .pdata puts inside EDF.dll+0x28A980, and the call is only 0x4A into
// that function -- the shape of a derived class calling its base first. So
// 28A980 is a real bullet's constructor, and its arguments are the ones worth
// having. It leads the list.
//
// Current sites: owned constructor, component initializer, constructor after
// initialization. Only the initializer may change the source matrix translation.
unsigned g_fireSite[kMaxFireSites]={0x22E9C0,0x231CC0,0x22EA4A};
int g_fireSiteCount=3;
volatile LONG g_fireSiteHits[kMaxFireSites]{};
volatile LONG g_fireSiteMine[kMaxFireSites]{};
float g_fireOriginLift=0;
bool g_watchBullets=true;
bool g_shotFromMuzzle=true;
// The arguments as they stood the first time each site was reached.
struct TracedCall {
    DWORD64 rcx=0,rdx=0,r8=0,r9=0,rsp=0,caller=0;
    // A page of stack, so the whole chain comes back at once. Going up one
    // function per run was going to take all afternoon: the immediate caller of
    // the bullet's constructor turned out to be a 0x2F-byte thunk, and the one
    // that matters is somewhere above it.
    DWORD64 stack[48]{};
    int depth=0;
    volatile bool taken=false;
    bool told=false;
};
TracedCall g_traced[kMaxFireSites]{};
void* g_aimCentreAddress=nullptr;
float g_aimCentreWant[3]{};
bool g_aimCentreWantValid=false;
bool g_fireSitesArmed=false;
char g_fireSiteNote[192]="not read back";

// Runs inside the game's own exception, on its own thread, with the offending
// instruction not yet executed. Three floats and a counter, nothing more.
//
// Note that the addresses this was aimed at were wrong, and wrong in a way the
// hardware guarantees: a data breakpoint is a trap, so the address recorded is
// the instruction AFTER the one that touched the memory. Disassembling the
// bytes settles it -- 6BF7EE is movups [r8+30h],xmm1 and 6BF7F3 is the next
// instruction; 6EDEEA is movups [rcx+0E0h],xmm2 and 6EDEF1 the next. Both are
// writes inside matrix copies, which is the skeleton putting the bone where it
// belongs. No reader of aim_center has been found at all.
LONG CALLBACK OnWatchHit(EXCEPTION_POINTERS* info) noexcept;

#include "shot_origin.h"
#include "casing_origin.h"
#include "hand_support.h"
#include "nameplate_hooks.h"
#include "muzzle_flash.h"
#include "vehicle_recoil.h"

bool ServeFireSite(CONTEXT* context) noexcept {
    bool ours=false;
    for(int i=0;i<g_fireSiteCount;++i) {
        if(!(context->Dr6&(1ull<<i))) continue;
        ours=true;
        InterlockedIncrement(&g_fireSiteHits[i]);
        if(!HandleShotSite(g_fireSite[i],context)) continue;
        InterlockedIncrement(&g_fireSiteMine[i]);

        // The latest call, not the first. Under the Windows calling convention
        // rcx is the object being constructed and rdx, r8 and r9 the first three
        // arguments, so this is where a bullet's initial position will be if it
        // is passed at all.
        //
        // Keeping only the first was no use: the first call through arrives two
        // seconds after arming, while the mission is still building itself, long
        // before anyone pulls a trigger. Keeping the latest means the sample
        // printed after a shot is the shot. Written here, read from the
        // watchdog; nothing in here touches the game.
        g_traced[i].rcx=context->Rcx;
        g_traced[i].rdx=context->Rdx;
        g_traced[i].r8=context->R8;
        g_traced[i].r9=context->R9;
        g_traced[i].rsp=context->Rsp;
        // At a function's entry the return address is sitting at [rsp], and the
        // caller is what this needs next: the bullet arrives at its constructor
        // as cleared memory -- rcx read as eight zero floats -- so whoever calls
        // it is where the position is decided.
        g_traced[i].caller=0;
        g_traced[i].depth=0;
        __try {
            if(context->Rsp) {
                g_traced[i].caller=*reinterpret_cast<const DWORD64*>(context->Rsp);
                auto from=reinterpret_cast<const DWORD64*>(context->Rsp);
                for(int j=0;j<48;++j) g_traced[i].stack[j]=from[j];
                g_traced[i].depth=48;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { g_traced[i].depth=0; }
        g_traced[i].taken=true;
    }
    if(!ours) return false;
    context->Dr6=0;
    // Resume flag, or the breakpoint fires again on the same instruction the
    // moment it is restarted and the game never moves.
    context->EFlags|=0x10000;
    return true;
}

// Armed from the watchdog, like everything that stops a thread here.
// Installed once, and before anything can raise. The bone watch used to be the
// only thing that installed it, so emptying WatchBone would have left the
// instruction breakpoints raising exceptions with nobody to catch them.
// Walk the menus and fire a shot, so a question can be asked without anyone
// sitting through the answer.
//
// Every test so far has cost a launch, a walk through the menus and a shot, and
// the answer has usually been one line of log. The player says the keyboard
// route is nothing but Enter held down until the mission starts, so that is what
// this does -- and the pad this mod already hands the game gets its A button at
// the same time, since which of the two the menus want is not worth a round to
// find out.
//
// It stops the moment a soldier exists, which is the only condition that
// matters and needs no map of the menus. Then, after AutoFireAfterSeconds, it
// pulls the trigger once. AutoDeploy=0 leaves all of it alone.
bool g_autoDeploy=false;
int g_autoFireAfter=6;
ULONGLONG g_missionAt=0;
bool g_autoFired=false;

// A whole press, down and up, through both channels.
//
// Toggling a key on the watchdog's own two-second beat meant Enter was held for
// two seconds at a time, and fifty seconds of that moved the menus not at all.
// A press is a short thing, so this sends one -- and sends it twice over, once
// as injected input and once as a message to the window, because which of the
// two a menu is listening to is not worth a run to find out.
void PressEnter() noexcept {
    INPUT key[2]{};
    key[0].type=INPUT_KEYBOARD;
    key[0].ki.wVk=VK_RETURN;
    key[0].ki.wScan=static_cast<WORD>(MapVirtualKeyW(VK_RETURN,MAPVK_VK_TO_VSC));
    key[1]=key[0];
    key[1].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(1,&key[0],sizeof(INPUT));
    Sleep(20);
    SendInput(1,&key[1],sizeof(INPUT));
    if(HWND window=GetForegroundWindow()) {
        const LPARAM scan=static_cast<LPARAM>(key[0].ki.wScan)<<16;
        PostMessageW(window,WM_KEYDOWN,VK_RETURN,1|scan);
        PostMessageW(window,WM_CHAR,VK_RETURN,1|scan);
        PostMessageW(window,WM_KEYUP,VK_RETURN,1|scan|(1<<30)|(1u<<31));
    }
}

void DriveTheGame() noexcept {
    static bool said=false;
    if(!said) { said=true; Log("AUTO deploy=%d fireAfter=%ds",g_autoDeploy?1:0,g_autoFireAfter); }
    if(!g_autoDeploy) return;
    const auto now=GetTickCount64();
    const bool inMission=g_soldierAt && now-g_soldierAt<2000;
    if(!inMission) {
        // Said once every few seconds while it is still pressing, because
        // "nothing in the log" and "not running" look identical otherwise.
        static ULONGLONG spoke=0;
        if(now-spoke>4000) {
            spoke=now;
            Log("AUTO pressing for a mission: foreground=%d soldierSeenMsAgo=%llu",
                Foreground()?1:0,g_soldierAt?now-g_soldierAt:0);
        }
        // Not in a mission yet. Press, release, press, on the watchdog's own
        // beat -- a button held is a button pressed once.
        // Four presses to the watchdog's one turn, as asked for.
        for(int i=0;i<16;++i) {
            if(Foreground()) PressEnter();
            Sleep(40);
        }
        // And the pad's A, pulsed the same way, for whichever of the two the
        // menus actually read.
        edf6vr::PadState pad{};
        pad.buttons=kPadA;
        pad.valid=true;
        edf6vr::SetPadState(pad);
        Sleep(60);
        pad.buttons=0;
        edf6vr::SetPadState(pad);
        g_missionAt=0;
        g_autoFired=false;
        return;
    }
    if(!g_missionAt) {
        g_missionAt=now;
        edf6vr::SetPadState(edf6vr::PadState{});
        Log("AUTO a soldier exists; the menus are done");
        return;
    }
    if(g_autoFired || now-g_missionAt<static_cast<ULONGLONG>(g_autoFireAfter)*1000) return;
    g_autoFired=true;
    edf6vr::PadState pad{};
    pad.rightTrigger=255;
    pad.valid=true;
    edf6vr::SetPadState(pad);
    Log("AUTO firing one shot");
    Sleep(250);
    edf6vr::SetPadState(edf6vr::PadState{});
}

bool HandlerReady() noexcept {
    static PVOID handler=nullptr;
    if(!handler) handler=AddVectoredExceptionHandler(1,&OnWatchHit);
    return handler!=nullptr;
}

bool ArmFireSites() noexcept {
    if(g_fireSitesArmed || !g_fireSiteCount || !g_image.base || !g_frameLoopThread) return false;
    if(g_frameLoopThread==GetCurrentThreadId()) return false;
    if(!HandlerReady()) return false;
    HANDLE handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT,
                             FALSE,g_frameLoopThread);
    if(!handle) return false;
    bool armed=false;
    if(SuspendThread(handle)!=static_cast<DWORD>(-1)) {
        CONTEXT context{};
        context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
        if(GetThreadContext(handle,&context)) {
            DWORD64 control=context.Dr7;
            for(int i=0;i<g_fireSiteCount;++i) {
                const auto at=reinterpret_cast<DWORD64>(g_image.base)+g_fireSite[i];
                if(i==0) context.Dr0=at;
                else if(i==1) context.Dr1=at;
                else context.Dr2=at;
                control|=1ull<<(i*2);                    // local enable
                control&=~(0xFull<<(16+i*4));            // execute, one byte
            }
            context.Dr7=control;
            context.Dr6=0;
            context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
            armed=SetThreadContext(handle,&context)!=0;
            // Read them back. "Armed" meaning SetThreadContext returned true is
            // not the same as the registers holding what was asked for, and
            // without this there is no way to tell a breakpoint that was never
            // set from one that was never reached.
            if(armed) {
                CONTEXT back{};
                back.ContextFlags=CONTEXT_DEBUG_REGISTERS;
                if(GetThreadContext(handle,&back))
                    std::snprintf(g_fireSiteNote,sizeof(g_fireSiteNote),
                                  "dr0=%llX dr1=%llX dr2=%llX dr3=%llX dr7=%llX base=%p",
                                  static_cast<unsigned long long>(back.Dr0),
                                  static_cast<unsigned long long>(back.Dr1),
                                  static_cast<unsigned long long>(back.Dr2),
                                  static_cast<unsigned long long>(back.Dr3),
                                  static_cast<unsigned long long>(back.Dr7),
                                  static_cast<void*>(g_image.base));
            }
        }
        ResumeThread(handle);
    }
    CloseHandle(handle);
    if(armed) {
        g_fireSitesArmed=true;
        Log("FIRESITE armed at EDF.dll+%X, +%X, +%X on thread %lu | %s",
            g_fireSite[0],g_fireSite[1],g_fireSite[2],g_frameLoopThread,g_fireSiteNote);
    }
    return armed;
}

// Whatever looks like a position in the first bytes of an object.
//
// A bullet's initial position is a float triple somewhere near the start of
// whatever is being built, and the soldier's own position says which triple.
// Read only, and only once per site.
void DescribeTraced(const char* what,DWORD64 pointer) noexcept {
    if(!pointer) return;
    __try {
        constexpr int kSpan=0x200;
        auto at=reinterpret_cast<const float*>(pointer);
        if(!edf6vr::Readable(at,kSpan)) { Log("TRACE %s=%llX unreadable",what,pointer); return; }
        char line[480];
        int used=std::snprintf(line,sizeof(line),"TRACE %s=%llX soldier=(%.1f,%.1f,%.1f):",
                               what,pointer,g_lastAim.position[0],g_lastAim.position[1],
                               g_lastAim.position[2]);
        int found=0;
        for(int i=0;i+2<kSpan/4 && found<6;++i) {
            float away=0;
            bool sane=true;
            for(int j=0;j<3;++j) {
                const float v=at[i+j];
                if(!std::isfinite(v) || v==0) { sane=false; break; }
                const float d=v-g_lastAim.position[j];
                away+=d*d;
            }
            if(!sane || !(away<2500.0f)) continue;   // fifty metres of him
            used+=std::snprintf(line+used,sizeof(line)-used," +%X=(%.2f,%.2f,%.2f)",
                                i*4,at[i],at[i+1],at[i+2]);
            ++found;
            i+=2;
        }
        if(!found) {
            // Nothing recognisable, so show the start of it raw: a layout is
            // easier to guess at from numbers than from their absence.
            used+=std::snprintf(line+used,sizeof(line)-used," none; raw");
            for(int i=0;i<8 && used<static_cast<int>(sizeof(line))-24;++i)
                used+=std::snprintf(line+used,sizeof(line)-used," %.3f",at[i]);
        }
        Log("%s",line);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Compare world and simulation positions immediately after native initialization.
void ReportFireSites() noexcept {
    if(!g_fireSitesArmed) return;
    ReportShotInit();
    static LONG shown[kMaxFireSites]{};
    for(int i=0;i<g_fireSiteCount;++i) {
        const LONG now=g_fireSiteHits[i];
        if(now!=shown[i]) {
            shown[i]=now;
            Log("FIRESITE EDF.dll+%X reached %ld times, %ld of them the player's",
                g_fireSite[i],now,g_fireSiteMine[i]);
        }
        if(g_traced[i].taken) {
            g_traced[i].taken=false;
            char from[96]="?";
            if(g_traced[i].caller && g_image.base) {
                const auto base=reinterpret_cast<DWORD64>(g_image.base);
                if(g_traced[i].caller>base && g_traced[i].caller-base<0x2400000)
                    std::snprintf(from,sizeof(from),"EDF.dll+%llX",g_traced[i].caller-base);
                else
                    std::snprintf(from,sizeof(from),"%llX",g_traced[i].caller);
            }
            Log("TRACE EDF.dll+%X called from %s | rcx=%llX rdx=%llX r8=%llX r9=%llX",
                g_fireSite[i],from,g_traced[i].rcx,g_traced[i].rdx,g_traced[i].r8,
                g_traced[i].r9);
            // Everything on the stack that points into the game's code, which is
            // the chain of callers with the local variables sieved out.
            if(g_image.base && g_traced[i].depth) {
                const auto base=reinterpret_cast<DWORD64>(g_image.base);
                char chain[420];
                int used=std::snprintf(chain,sizeof(chain),"TRACE   chain:");
                int listed=0;
                DWORD64 last=0;
                for(int j=0;j<g_traced[i].depth && listed<10;++j) {
                    const DWORD64 word=g_traced[i].stack[j];
                    if(word<=base || word-base>=0x1754000) continue;   // not .text
                    if(word==last) continue;
                    last=word;
                    used+=std::snprintf(chain+used,sizeof(chain)-used," +%llX",word-base);
                    ++listed;
                    if(used>static_cast<int>(sizeof(chain))-16) break;
                }
                if(listed) Log("%s",chain);
            }
            DescribeTraced("rcx",g_traced[i].rcx);
            DescribeTraced("rdx",g_traced[i].rdx);
            DescribeTraced("r8",g_traced[i].r8);
        }
    }
}

// Who reads the bone, asked of the processor rather than of me.
//
// Eleven bones were written and none of them moved the shot, and the read-back
// says why: every one of them is recomputed within the tick, drifting a metre
// and a half back to the body before anything uses it. So none of the eleven
// was ever really tried, and writing a twelfth at a different moment is just
// another guess about an order nobody here knows.
//
// The processor knows. A debug register set on the bone's own three floats
// stops the game on whoever touches them and hands over the address of the
// instruction that did it. That address is the code that computes or consumes
// the shot's origin, which is the thing this has been hunting for a dozen
// rounds. It disarms itself after a handful of distinct answers, so it costs a
// moment rather than a session.
//
// The handler does no work beyond writing an address into an array: it runs on
// the game's own thread inside an exception, which is no place for file IO. The
// watchdog prints what it collected.
constexpr int kWatchHits=12;
volatile LONG g_watchCount=0;
unsigned long long g_watchRip[kWatchHits]{};
void* g_watchAddress=nullptr;
bool g_watchArmed=false;
int g_watchPrinted=0;
wchar_t g_watchBone[16]=L"aim_center";

LONG CALLBACK OnWatchHit(EXCEPTION_POINTERS* info) noexcept {
    if(!info || !info->ExceptionRecord || !info->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
    if(info->ExceptionRecord->ExceptionCode!=EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    auto context=info->ContextRecord;
    if(ServeFireSite(context)) return EXCEPTION_CONTINUE_EXECUTION;
    if(!(context->Dr6&0x8)) return EXCEPTION_CONTINUE_SEARCH;   // not our register
    context->Dr6=0;
    const auto rip=static_cast<unsigned long long>(context->Rip);
    const LONG have=g_watchCount;
    for(LONG i=0;i<have && i<kWatchHits;++i) if(g_watchRip[i]==rip) return EXCEPTION_CONTINUE_EXECUTION;
    const LONG slot=InterlockedIncrement(&g_watchCount)-1;
    if(slot<kWatchHits) g_watchRip[slot]=rip;
    // Only this watch's own register. Clearing the whole of Dr7 would take the
    // instruction breakpoints in Dr0..Dr2 down with it, and then "never
    // reached" would mean "switched off behind my back" -- which is exactly the
    // sort of thing that has already cost two rounds here.
    if(slot>=kWatchHits-1) {
        context->Dr7&=~((1ull<<6)|(0xFull<<28));
        context->Dr3=0;
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Point the register at the bone, on the thread that runs the game.
bool ArmWatch(void* address,DWORD thread) noexcept {
    if(!address || !thread) return false;
    // Never from the thread being stopped. Suspending yourself is a wait with
    // nobody left to end it, and the stack off the frozen game said exactly
    // that: KERNELBASE, then this plugin, then the game that called into it.
    if(thread==GetCurrentThreadId()) return false;
    HANDLE handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_SET_CONTEXT,
                             FALSE,thread);
    if(!handle) return false;
    bool armed=false;
    if(SuspendThread(handle)!=static_cast<DWORD>(-1)) {
        CONTEXT context{};
        context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
        if(GetThreadContext(handle,&context)) {
            context.Dr3=reinterpret_cast<DWORD64>(address);
            context.Dr6=0;
            // Local enable for DR3, break on read or write, four bytes wide.
            // The first three registers belong to the instruction breakpoints.
            context.Dr7=(context.Dr7&~(0xFull<<28))|(1ull<<6)|(0x3ull<<28)|(0x3ull<<30);
            context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
            armed=SetThreadContext(handle,&context)!=0;
        }
        ResumeThread(handle);
    }
    CloseHandle(handle);
    return armed;
}

// The game's thread only says where the bone is. Arming is somebody else's job.
void WatchTheBone(void* soldier) noexcept {
    if(g_watchArmed || g_watchAddress || !g_watchBone[0] || !soldier || !g_nodeLookup) return;
    void* node=edf6vr::FindNamedBone(soldier,g_nodeLookup,g_watchBone);
    if(!node) return;
    g_watchAddress=static_cast<unsigned char*>(node)+0xB0+0x30;
}

// Where aim_center lives and what we want it to say, kept fresh for the handler.
void KeepAimCentre(void* soldier) noexcept {
    if(!soldier || !g_nodeLookup || !g_muzzleWantValid) return;
    if(!g_aimCentreAddress) {
        void* node=edf6vr::FindNamedBone(soldier,g_nodeLookup,L"aim_center");
        if(!node) return;
        g_aimCentreAddress=static_cast<unsigned char*>(node)+0xB0+0x30;
    }
    for(int j=0;j<3;++j) g_aimCentreWant[j]=g_muzzleWant[j];
    g_aimCentreWantValid=true;
}

// Armed from the watchdog, which is the one thread that is allowed to stop the
// others -- the same arrangement the hang probe already uses.
void ArmWatchFromWatchdog() noexcept {
    if(g_watchArmed || !g_watchAddress || !g_frameLoopThread) return;
    if(!HandlerReady()) { g_watchAddress=nullptr; return; }
    g_watchArmed=ArmWatch(g_watchAddress,g_frameLoopThread);
    char narrow[32]{};
    WideCharToMultiByte(CP_UTF8,0,g_watchBone,-1,narrow,sizeof(narrow)-1,nullptr,nullptr);
    Log("WATCH %s at %p on thread %lu: %s",narrow,g_watchAddress,g_frameLoopThread,
        g_watchArmed?"armed":"could not arm");
    if(!g_watchArmed) g_watchAddress=nullptr;   // try again next time round
}

// Printed from the watchdog, where writing to a file is allowed.
void ReportWatch() noexcept {
    const LONG have=g_watchCount;
    while(g_watchPrinted<have && g_watchPrinted<kWatchHits) {
        const unsigned long long rip=g_watchRip[g_watchPrinted];
        char where[96]="?";
        MEMORY_BASIC_INFORMATION about{};
        if(rip && VirtualQuery(reinterpret_cast<void*>(rip),&about,sizeof(about))
           && about.AllocationBase) {
            wchar_t path[MAX_PATH]{};
            auto module=static_cast<HMODULE>(about.AllocationBase);
            if(GetModuleFileNameW(module,path,MAX_PATH)) {
                const wchar_t* leaf=std::wcsrchr(path,static_cast<wchar_t>(92));
                leaf=leaf?leaf+1:path;
                char narrow[64]{};
                WideCharToMultiByte(CP_UTF8,0,leaf,-1,narrow,sizeof(narrow)-1,nullptr,nullptr);
                std::snprintf(where,sizeof(where),"%s+%llX",narrow,
                              rip-reinterpret_cast<unsigned long long>(module));
            }
        }
        Log("WATCH touched by %s",where);
        ++g_watchPrinted;
    }
}

// The named bones worth trying, in the order they are worth trying.
//
// aim_center and aim_target are not anatomy -- the soldier's skeleton carries
// them alongside koshi and kata_r -- and a node called weapon even less so. The
// rest follow the one thing actually observed: the shot leaves from the waist
// with a rifle and the shoulder with a launcher, which is koshi and kata_r.
//
// Writing a bone is known to stick: arms_r was written every tick and read back
// a tick later unchanged. So this is only a question of which one, and the left
// A button walks it while the left trigger marks the answer.
//
// Names of any length: the lookup carries the long ones through the heap form
// of the game's string, which is what aim_center and aim_target need.
constexpr const wchar_t* kBoneProbe[]={
    nullptr, L"aim_center", L"aim_target", L"weapon", L"koshi", L"kata_r",
    L"mune", L"te_r", L"hara2", L"kawan2_r", L"jowan2_r", L"kubi",
};
constexpr int kBoneProbeCount=static_cast<int>(sizeof(kBoneProbe)/sizeof(kBoneProbe[0]));
int g_boneProbeAt=0;
unsigned long long g_boneProbeWrites=0;

// What was written to the bone being tried, and how much of it was still there
// at the top of the next tick.
//
// arms_r keeps whatever is written to it -- armDrift read 0.000 for a whole
// session -- but that is one bone's habit, not the skeleton's. aim_center is
// where the shot starts from, plain as day: it sits at the shoulder and
// aim_target sits forty metres down the line the player is aiming along. Yet
// writing it moved nothing, and there are only two ways that happens. Either
// the shot is not taken from it, or the game recomputes it every tick and our
// value is gone before anything reads it -- in which case that candidate was
// never really tried at all, and neither were most of the others.
//
// Same measurement as for arms_r, now for whichever bone is being written.
void* g_probeNode=nullptr;
float g_probeWrote[3]{};
bool g_probeWroteValid=false;
float g_probeDriftMin=1e30f, g_probeDriftMax=0;

// Put the muzzle on whichever bone is being tried.
void PlaceBoneProbe(void* soldier) noexcept {
    if(!g_shotProbe || g_boneProbeAt<=0 || g_boneProbeAt>=kBoneProbeCount) return;
    if(!g_muzzleWantValid || !soldier || !g_nodeLookup) return;
    void* node=edf6vr::FindNamedBone(soldier,g_nodeLookup,kBoneProbe[g_boneProbeAt]);
    if(!node || !edf6vr::WriteBonePosition(node,g_muzzleWant)) return;
    ++g_boneProbeWrites;
    g_probeNode=node;
    for(int j=0;j<3;++j) g_probeWrote[j]=g_muzzleWant[j];
    g_probeWroteValid=true;
}

// How far the game has moved it back since.
void MeasureProbeDrift() noexcept {
    if(!g_probeWroteValid || !g_probeNode) return;
    __try {
        if(!edf6vr::Readable(g_probeNode,0xB0+64)) return;
        auto row=reinterpret_cast<const float*>(
            static_cast<unsigned char*>(g_probeNode)+0xB0+0x30);
        float drift=0;
        for(int j=0;j<3;++j) {
            const float now=row[j];
            if(!std::isfinite(now)) return;
            const float d=now-g_probeWrote[j];
            drift+=d*d;
        }
        drift=std::sqrt(drift);
        if(!std::isfinite(drift)) return;
        g_probeDriftMin=std::min(g_probeDriftMin,drift);
        g_probeDriftMax=std::max(g_probeDriftMax,drift);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Left A walks the bones, left trigger marks the one that works. Both are kept
// from the game while this is armed; ShotOriginProbe=0 gives them back.
void StepShotProbe(const edf6vr::ControllerState& controls) noexcept {
    if(!g_shotProbe) return;
    static bool wasA=false, wasTrigger=false;
    const bool a=controls.lower[0];
    const bool pulled=controls.trigger[0]>0.7f;
    if(a && !wasA) {
        g_boneProbeAt=(g_boneProbeAt+1)%kBoneProbeCount;
        // Say how the one being left behind did before moving on.
        if(g_probeWroteValid)
            Log("PROBE that one kept %llu writes, drift=[%.3f %.3f]m",
                g_boneProbeWrites,g_probeDriftMax?g_probeDriftMin:0,g_probeDriftMax);
        g_boneProbeWrites=0;
        g_probeWroteValid=false;
        g_probeNode=nullptr;
        g_probeDriftMin=1e30f;
        g_probeDriftMax=0;
        edf6vr::g_openxr.Buzz(0,0.08f,0.7f);
        char narrow[16]="off";
        if(g_boneProbeAt)
            WideCharToMultiByte(CP_UTF8,0,kBoneProbe[g_boneProbeAt],-1,narrow,
                                sizeof(narrow)-1,nullptr,nullptr);
        Log("PROBE bone #%d of %d: %s",g_boneProbeAt,kBoneProbeCount-1,narrow);
    }
    if(pulled && !wasTrigger && g_boneProbeAt>0) {
        for(int i=0;i<5;++i) { edf6vr::g_openxr.Buzz(0,0.15f,1.0f); edf6vr::g_openxr.Buzz(1,0.15f,1.0f); }
        char narrow[16]{};
        WideCharToMultiByte(CP_UTF8,0,kBoneProbe[g_boneProbeAt],-1,narrow,
                            sizeof(narrow)-1,nullptr,nullptr);
        Log("PROBE FOUND ***** the shot comes from the bone %s ***** writes=%llu",
            narrow,g_boneProbeWrites);
    }
    wasA=a;
    wasTrigger=pulled;
}

__declspec(guard(ignore)) void __fastcall HookInputRead(void* soldier,void* context,float parameter) {
    PlaceShotOrigin(soldier);
    g_inputOriginal(soldier,context,parameter);
    g_soldierInput=GetTickCount64();
    AcquireSRWLockExclusive(&g_lock);
    const bool dualSwitched=UpdateRangerDual(soldier);
    ApplyVrInput(soldier);
    ReleaseSRWLockExclusive(&g_lock);
    if(dualSwitched) PlayRangerDualSwitchSound(soldier);
}

// The warp's numbers are all read again every few seconds, so a value can be
// tried by editing the INI while the game runs. Nothing here installs a hook or
// touches a pointer; these only feed the shader and the eye choice.
// The nameplate size is for the headset: on a flat screen (VR not started,
// F11 off, no headset) the plates stay at the game's own size. Applied when
// the INI is read and whenever VR turns on or off; 1.0 writes the shipped
// bytes back.
float g_nameplateAppliedPlate=-1,g_nameplateAppliedText=-1;
void ApplyNameplateSizeForVr(bool force=false) noexcept {
    if(!g_image.base) return;
    const bool vr=g_vrEnabled && g_worldNameplates;
    const float plate=vr?g_nameplateSize:1.0f,text=vr?g_nameplateTextSize:1.0f;
    if(!force && plate==g_nameplateAppliedPlate && text==g_nameplateAppliedText) return;
    if(edf6vr::ApplyNameplateScale(g_image,plate,text,g_nameplateSizeNote,sizeof(g_nameplateSizeNote))) {
        g_nameplateAppliedPlate=plate; g_nameplateAppliedText=text;
    } else { g_nameplateAppliedPlate=plate; g_nameplateAppliedText=text; }   // a refused signature is not retried every update
    Log("NAMEPLATESIZE vr=%d plate=%.3f text=%.3f (%s)",g_vrEnabled?1:0,plate,text,g_nameplateSizeNote);
}
void ReloadTunables() noexcept {
    if(!g_iniPath[0]) return;
    ReadHandSettings();
    g_vehicleLevelView=GetPrivateProfileIntW(L"VR",L"VehicleLevelView",1,g_iniPath)!=0;
    g_devKeys=GetPrivateProfileIntW(L"Diagnostics",L"DevKeys",0,g_iniPath)!=0;
    g_worldNameplates=GetPrivateProfileIntW(L"Render",L"WorldNameplates",1,g_iniPath)!=0;
    g_worldNameplateMask=static_cast<unsigned>(GetPrivateProfileIntW(L"Render",L"WorldNameplateClasses",7,g_iniPath));
    edf6vr::EnableWorldUi(g_worldNameplates);
    edf6vr::EnableWorldUiProbe(GetPrivateProfileIntW(L"Render",L"WorldNameplateProbe",0,g_iniPath)!=0);
    g_worldNameplateScaleX=ReadFloat(g_iniPath,L"WorldNameplateScaleX",1.0f,0.25f,4.0f,L"Render");
    g_worldNameplateScaleY=ReadFloat(g_iniPath,L"WorldNameplateScaleY",1.0f,0.25f,4.0f,L"Render");
    g_worldNameplateOffsetY=ReadFloat(g_iniPath,L"WorldNameplateOffsetY",0.0f,-0.5f,0.5f,L"Render");
    g_worldNameplateDebug=GetPrivateProfileIntW(L"Render",L"WorldNameplateDebug",0,g_iniPath)!=0;
    g_nameplateSize=ReadFloat(g_iniPath,L"WorldNameplateSize",0.25f,0.05f,4.0f,L"Render");
    g_nameplateTextSize=ReadFloat(g_iniPath,L"WorldNameplateTextSize",1.0f,0.05f,4.0f,L"Render");
    // The plate's own transform in the game, so it is applied with or without
    // the world routing; 1.0 leaves the shipped bytes as they are.
    ApplyNameplateSizeForVr(true);
    edf6vr::SetWorldUiPlacement(g_worldNameplateScaleX,g_worldNameplateScaleY,g_worldNameplateOffsetY,g_worldNameplateDebug);
    g_recoilKickOn=GetPrivateProfileIntW(L"VR",L"RecoilKick",g_recoilKickOn,g_iniPath)!=0;
    g_vehicleRecoilOn=GetPrivateProfileIntW(L"VR",L"VehicleRecoil",g_vehicleRecoilOn,g_iniPath)!=0;
    g_vehicleRecoilScale=ReadFloat(g_iniPath,L"VehicleRecoilScale",g_vehicleRecoilScale,0.0f,4.0f,L"VR");
    g_fencerSplitAim=GetPrivateProfileIntW(L"VR",L"FencerSplitAim",1,g_iniPath)!=0;
    g_fencerHandWeapons=GetPrivateProfileIntW(L"VR",L"FencerHandWeapons",1,g_iniPath)!=0;
    g_fencerDirectAim=GetPrivateProfileIntW(L"VR",L"FencerDirectAim",1,g_iniPath)!=0;
    g_fencerHandRoll=GetPrivateProfileIntW(L"VR",L"FencerHandRoll",1,g_iniPath)!=0;
    g_fencerShoulderOnHead=GetPrivateProfileIntW(L"VR",L"FencerShoulderOnHead",1,g_iniPath)!=0;
    g_fencerShoulderSide=ReadFloat(g_iniPath,L"FencerShoulderSideMetres",0.40f,0.0f,0.8f,L"VR");
    g_fencerShoulderDown=ReadFloat(g_iniPath,L"FencerShoulderDownMetres",0.10f,-0.5f,0.6f,L"VR");
    g_fencerShoulderBack=ReadFloat(g_iniPath,L"FencerShoulderBackMetres",0.25f,-0.3f,0.8f,L"VR");
    g_fencerHandOutward=ReadFloat(g_iniPath,L"FencerHandOutwardMetres",0.05f,-0.5f,0.5f,L"VR");
    g_fencerHandAhead=ReadFloat(g_iniPath,L"FencerHandAheadMetres",-0.027f,-0.5f,0.5f,L"VR");
    g_fencerHandUp=ReadFloat(g_iniPath,L"FencerHandUpMetres",0.022f,-0.5f,0.5f,L"VR");
    g_fencerHeavyAim=GetPrivateProfileIntW(L"VR",L"FencerHeavyAim",1,g_iniPath)!=0;
    g_fencerSwapHands=GetPrivateProfileIntW(L"VR",L"FencerSwapHands",0,g_iniPath)!=0;
    g_shotBuzz=ReadFloat(g_iniPath,L"ShotBuzz",g_shotBuzz,0.0f,1.0f,L"VR");
    g_recoilShape.pitchRadians=ReadFloat(g_iniPath,L"RecoilPitchDegrees",g_recoilShape.pitchRadians*180.0f/3.14159265f,0.0f,30.0f,L"VR")*3.14159265f/180.0f;
    g_recoilShape.backMetres=ReadFloat(g_iniPath,L"RecoilBackMetres",g_recoilShape.backMetres,0.0f,0.2f,L"VR");
    g_recoilDecaySeconds=ReadFloat(g_iniPath,L"RecoilDecayMs",g_recoilDecaySeconds*1000.0f,10.0f,1000.0f,L"VR")/1000.0f;
    const float flashScale=ReadFloat(g_iniPath,L"MuzzleFlashScale",g_muzzleFlashScale.load(),0.1f,1.0f,L"Render");
    if(g_muzzleFlashScale.exchange(flashScale)!=flashScale) Log("MUZZLEFLASH scale=%.2f (live INI)",flashScale);
    const int stereo=GetPrivateProfileIntW(L"Render",L"StereoMode",g_stereoMode,g_iniPath);
    const float steps=ReadFloat(g_iniPath,L"WarpSteps",g_warpSteps,4,256,L"Render");
    const float nearest=ReadFloat(g_iniPath,L"WarpNearestMetres",g_warpNearest,0.05f,5.0f,L"Render");
    const float easeFrom=ReadFloat(g_iniPath,L"WarpNearKneeMetres",g_nearKnee,0.1f,20.0f,L"Render");
    const float easeFloor=ReadFloat(g_iniPath,L"WarpNearScale",g_nearScale,0.0f,1.0f,L"Render");
    const bool keepUi=GetPrivateProfileIntW(L"Render",L"WarpKeepUi",g_warpHudless,g_iniPath)!=0;
    const bool alternate=GetPrivateProfileIntW(L"Render",L"WarpAlternateEye",g_warpAlternate,g_iniPath)!=0;
    const float debug=static_cast<float>(GetPrivateProfileIntW(L"Render",L"WarpDebug",
                                                               static_cast<int>(g_warpDebug),g_iniPath));
    const float fovScale=ReadFloat(g_iniPath,L"FovScale",g_fovScale,0.4f,1.2f,L"Render");
    const float ipdScale=ReadFloat(g_iniPath,L"IpdScale",g_ipdScale,0.0f,3.0f,L"Render");
    const bool swapEyes=GetPrivateProfileIntW(L"Render",L"SwapEyes",g_swapEyes,g_iniPath)!=0;
    const bool uiPanel=GetPrivateProfileIntW(L"Render",L"UiPanel",g_uiLayer,g_iniPath)!=0;
    g_uiCluster=GetPrivateProfileIntW(L"Render",L"UiCluster",1,g_iniPath)!=0;
    g_uiClusterPlace=static_cast<int>(ReadFloat(g_iniPath,L"UiClusterPlace",static_cast<float>(g_uiClusterPlace),0,2,L"Render")+0.5f);
    g_uiClusterWristWidth=ReadFloat(g_iniPath,L"UiClusterWristWidthMetres",0.30f,0.05f,2.0f,L"Render");
    g_uiClusterMarginRight=ReadFloat(g_iniPath,L"UiClusterRight",0.03f,-0.5f,0.5f,L"Render");
    g_uiClusterMarginBottom=ReadFloat(g_iniPath,L"UiClusterBottom",0.05f,-0.5f,0.5f,L"Render");
    g_uiClusterRadarScale=ReadFloat(g_iniPath,L"UiClusterRadarScale",0.667f,0.1f,2.0f,L"Render");
    g_uiClusterArmorScale=ReadFloat(g_iniPath,L"UiClusterArmorScale",0.667f,0.1f,2.0f,L"Render");
    g_uiClusterWeaponScale=ReadFloat(g_iniPath,L"UiClusterWeaponScale",0.5f,0.1f,2.0f,L"Render");
    g_uiClusterGaugeScale=ReadFloat(g_iniPath,L"UiClusterGaugeScale",0.667f,0.1f,2.0f,L"Render");
    g_uiClusterWristInset=ReadFloat(g_iniPath,L"UiClusterWristInsetMetres",-0.05f,-0.3f,0.3f,L"Render");
    g_uiClusterWristRaise=ReadFloat(g_iniPath,L"UiClusterWristRaiseMetres",0.02f,-0.3f,0.3f,L"Render");
    PublishUiClusterPlace();
    PublishUiCluster();
    const float uiWidth=ReadFloat(g_iniPath,L"UiPanelWidthMetres",g_uiWidth,0.05f,10.0f,L"Render");
    const float uiDistance=ReadFloat(g_iniPath,L"UiPanelDistanceMetres",g_uiDistance,0.2f,20.0f,L"Render");
    // The menu board had no setting of its own; the HUD panel keys did not reach it.
    edf6vr::g_openxr.SetBoardWorldLocked(GetPrivateProfileIntW(L"Render",L"BoardWorldLocked",1,g_iniPath)!=0);
    edf6vr::g_openxr.SetBoard(ReadFloat(g_iniPath,L"BoardWidthMetres",2.6f,0.3f,10.0f,L"Render"),
                             ReadFloat(g_iniPath,L"BoardDistanceMetres",2.0f,0.3f,20.0f,L"Render"));
    const float reticleScale=ReadFloat(g_iniPath,L"ReticleScale",g_reticleScale,0.05f,4.0f,L"Render");
    const bool changed=stereo!=g_stereoMode || steps!=g_warpSteps || nearest!=g_warpNearest
                       || easeFrom!=g_nearKnee || easeFloor!=g_nearScale
                       || keepUi!=g_warpHudless || alternate!=g_warpAlternate || debug!=g_warpDebug
                       || fovScale!=g_fovScale || ipdScale!=g_ipdScale || swapEyes!=g_swapEyes
                       || uiPanel!=g_uiLayer || uiWidth!=g_uiWidth || uiDistance!=g_uiDistance
                       || reticleScale!=g_reticleScale;
    g_stereoMode=stereo; g_warpSteps=steps; g_warpNearest=nearest; g_warpHudless=keepUi;
    g_nearKnee=easeFrom; g_nearScale=easeFloor;
    g_warpAlternate=alternate; g_warpDebug=debug; g_fovScale=fovScale; g_ipdScale=ipdScale;
    g_swapEyes=swapEyes;
    g_uiLayer=uiPanel; g_uiWidth=uiWidth; g_uiDistance=uiDistance;
    g_reticleScale=reticleScale;
    if(changed)
        Log("INI reloaded: stereoMode=%d warpSteps=%.0f warpNearest=%.2f keepUi=%d alternate=%d "
            "warpDebug=%.0f fovScale=%.2f ipdScale=%.2f swapEyes=%d "
            "uiPanel=%d uiWidth=%.2f uiDistance=%.2f reticleScale=%.2f",
            g_stereoMode,g_warpSteps,g_warpNearest,g_warpHudless,g_warpAlternate,g_warpDebug,
            g_fovScale,g_ipdScale,g_swapEyes,g_uiLayer,g_uiWidth,g_uiDistance,g_reticleScale);
}

void MoveGameWindow() noexcept {
    edf6vr::FrameCapture frame{};
    if(!edf6vr::ReadFrameCapture(frame) || !frame.window) { Log("F3 REFUSED: the window is not known yet"); return; }
    auto window=static_cast<HWND>(frame.window);
    RECT rect{};
    if(!GetWindowRect(window,&rect)) { Log("F3 REFUSED: GetWindowRect failed"); return; }
    int width=rect.right-rect.left, height=rect.bottom-rect.top;
    // After a resolution change the window and the back buffer disagree, and what
    // the desktop shows is then a stale corner of a window nothing draws into any
    // more. Put the client area back to the size the game is rendering.
    if(frame.width && frame.height) {
        RECT wanted{0,0,static_cast<LONG>(frame.width),static_cast<LONG>(frame.height)};
        const LONG style=GetWindowLongW(window,GWL_STYLE);
        const LONG extended=GetWindowLongW(window,GWL_EXSTYLE);
        if(AdjustWindowRectEx(&wanted,static_cast<DWORD>(style),FALSE,static_cast<DWORD>(extended))) {
            const int wantWidth=wanted.right-wanted.left, wantHeight=wanted.bottom-wanted.top;
            if(wantWidth!=width || wantHeight!=height) {
                SetWindowPos(window,nullptr,0,0,wantWidth,wantHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                Log("F3: window was %dx%d, back buffer is %ux%u, resized to %dx%d",
                    width,height,frame.width,frame.height,wantWidth,wantHeight);
                width=wantWidth; height=wantHeight;
            }
        }
    }
    const int screenWidth=GetSystemMetrics(SM_CXSCREEN), screenHeight=GetSystemMetrics(SM_CYSCREEN);
    // Four corners and the middle, so any part of an oversized window can be
    // reached whatever the difference between it and the screen turns out to be.
    const int overWidth=width>screenWidth?width-screenWidth:0;
    const int overHeight=height>screenHeight?height-screenHeight:0;
    const int spots[5][2]={{0,0},{-overWidth,0},{0,-overHeight},{-overWidth,-overHeight},
                           {-overWidth/2,-overHeight/2}};
    g_windowCorner=(g_windowCorner+1)%5;
    const int x=spots[g_windowCorner][0], y=spots[g_windowCorner][1];
    SetWindowPos(window,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    Log("F3: window %dx%d on a %dx%d screen moved to (%d,%d), spot %d of 5",
        width,height,screenWidth,screenHeight,x,y,g_windowCorner+1);
}

// Published by the validated local camera, consumed under g_lock on draw.
// Identity only: no model/animation matrices pass between threads.
struct FencerStereoSet {
    void* soldier=nullptr;
    std::uint32_t id=0;
    void* weapons[2]{};
    void* models[2]{};
    ULONGLONG refreshed=0;
};
FencerStereoSet g_fencerStereo{};
std::atomic<void*> g_fencerStereoModels[2]{}; // cheap draw-thread rejection before g_lock
std::atomic<unsigned long long> g_fencerStereoBatches{0},g_fencerStereoDepths{0};
void PublishFencerStereo(void* soldier,bool active) noexcept {
    g_fencerStereo={};
    __try {
        if(!active || !FencerOwnsWeaponPose(soldier) || edf6vr::WeaponSlotCount(soldier)!=2) return;
        FencerStereoSet next{}; next.soldier=soldier;
        next.id=*reinterpret_cast<const std::uint32_t*>(static_cast<unsigned char*>(soldier)+0x314);
        for(unsigned i=0;i<2;++i) {
            edf6vr::WeaponPose weapon{};
            if(!edf6vr::ReadWeaponPose(soldier,i,weapon) || !edf6vr::HasType(g_image,weapon.model,".?AVAnimationModel@@")) return;
            next.weapons[i]=weapon.weapon; next.models[i]=weapon.model;
        }
        if(next.models[0]==next.models[1]) return;
        next.refreshed=GetTickCount64(); g_fencerStereo=next;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
bool ValidateFencerStereo(const FencerStereoSet& set,void* model) noexcept {
    __try {
        auto owner=static_cast<unsigned char*>(set.soldier);
        if(!model || !set.refreshed || GetTickCount64()-set.refreshed>250
           || !edf6vr::Readable(owner,0x1984) || !FencerOwnsWeaponPose(owner)
           || *reinterpret_cast<const std::uint32_t*>(owner+0x314)!=set.id
           || edf6vr::WeaponSlotCount(owner)!=2) return false;
        auto entries=*reinterpret_cast<unsigned char**>(owner+0x1970);
        for(unsigned i=0;i<2;++i) if(model==set.models[i]) {
            auto wrapper=*reinterpret_cast<void**>(entries+i*0x150+0x40);
            return edf6vr::Readable(wrapper,8) && *static_cast<void**>(wrapper)==set.weapons[i]
                && model==static_cast<unsigned char*>(set.weapons[i])+edf6vr::kWeaponModelOffset
                && edf6vr::HasType(g_image,model,".?AVAnimationModel@@");
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
struct FencerDrawProbe {
    std::atomic<void*> model{nullptr};
    std::atomic<unsigned long long> draws{0};
};
FencerDrawProbe g_fencerDraw[8];
std::atomic<ULONGLONG> g_fencerProbeAt{0};
// VR first person, whichever class. The positional probe was gated on the
// Fencer because that is where the fault was first heard, but the geometry
// that causes it -- a listener sitting on the model that carries the sound --
// is the same for all three.
std::atomic<ULONGLONG> g_vrAudioAt{0};
float g_soundNearFieldHold=0.0f,g_soundNearFieldFade=0.0f;
// Degrees the pan angle may move in one update. A source cannot really swing
// further than this between two, so nothing plausible is held back.
float g_soundPanStep=25.0f;
// A jump larger than this in one update is impossible for a real source, so a
// voice that makes one is emitted from the model the listener stands inside
// and is walked to the front and left there.
float g_soundPanFlip=45.0f;
// How many of those a voice may make before it stops being placed. One is a
// short sound going past; four in a row is a sound that cannot be placed.
unsigned g_soundPanFlipCount=4;
// Give each voice a position that does not do the impossible, instead of
// arguing with the angle that comes out of one that does.
bool g_soundPlaceVoices=true;
unsigned g_soundFireWindowMs=120;      // a voice starting this soon after a shot is that shot
float g_soundCarriedMetres=2.0f;       // and this close to the listener
unsigned g_soundCarriedAfterMs=150;    // wait this long before asking whether a voice is carried
float g_soundCarriedMoveMetres=0.20f;  // and only ask once the listener has gone this far
// Where the two carried kinds belong, relative to the head.
float g_soundUnderfootMetres=0.6f;     // a voice starting this close to the listener is on him
float g_soundFootDropMetres=1.7f;      // and is heard from the ground, directly below
float g_soundBackBehindMetres=0.35f;   // a sustained carried one is heard from the back
float g_soundBackDownMetres=0.20f;
#ifdef EDF6VR_AUDIO_RESEARCH
#include "fencer_sound_probe.h"
#include "sound_position_probe.h"
#include "sound_lifecycle_probe.h"
#endif
void ObserveFencerDraw(void* model) noexcept {
    if(!model || GetTickCount64()-g_fencerProbeAt.load()>250) return;
    for(auto& slot:g_fencerDraw) if(slot.model.load()==model) { ++slot.draws; return; }
}
void ProbeFencerWeapons(void* soldier,bool fpsApplied) noexcept {
#ifdef EDF6VR_AUDIO_RESEARCH
    g_soundViewMode.store((g_vrEnabled?1u:0u)|(fpsApplied?2u:0u),std::memory_order_relaxed);
    if(g_vrEnabled && fpsApplied) g_vrAudioAt.store(GetTickCount64(),std::memory_order_relaxed);
    else g_vrAudioAt.store(0,std::memory_order_relaxed);
#endif
    if(!FencerOwnsWeaponPose(soldier)) { g_fencerProbeAt.store(0); return; }
    __try {
        if(!edf6vr::Readable(soldier,0x1AC0)) return;
        auto bytes=static_cast<unsigned char*>(soldier);
        const auto now=GetTickCount64(); g_fencerProbeAt.store(now);
        static ULONGLONG last=0;
        const bool report=now-last>=500; if(report) last=now;
        const auto angles=reinterpret_cast<const float*>(bytes+0x1230);
        const auto smooth=reinterpret_cast<const float*>(bytes+0x1240);
        const auto look=reinterpret_cast<const float*>(bytes+0xD60);
        edf6vr::FencerPadCommand pad{};
        AcquireSRWLockShared(&g_fencerPadLock); pad=g_fencerPad; ReleaseSRWLockShared(&g_fencerPadLock);
        if(report) Log("FENCER vr=%d fps=%d nativePose=1 pad=(%.3f,%.3f) fresh=%d target=(%.4f,%.4f) aim=(%.4f,%.4f) smooth=(%.4f,%.4f) gain=%.5f equip=%.4f look=(%.5f,%.5f)",
            g_vrEnabled,fpsApplied,pad.x,pad.y,pad.Fresh(now),pad.targetPitch,pad.targetYaw,angles[0],angles[1],smooth[0],smooth[1],
            *reinterpret_cast<float*>(bytes+0x1260),*reinterpret_cast<float*>(bytes+0x1AB0),look[0],look[1]);
        const auto count=edf6vr::WeaponSlotCount(soldier);
        for(unsigned slot=0;slot<8;++slot) {
            edf6vr::WeaponPose held{};
            if(slot>=count || !edf6vr::ReadWeaponPose(soldier,slot,held)) {
#ifdef EDF6VR_AUDIO_RESEARCH
                ObserveGatlingSound(slot,nullptr);
#endif
                g_fencerDraw[slot].model.store(nullptr); continue;
            }
            if(g_fencerDraw[slot].model.exchange(held.model)!=held.model) g_fencerDraw[slot].draws.store(0);
#ifdef EDF6VR_AUDIO_RESEARCH
            ObserveGatlingSound(slot,held.weapon);
#endif
            if(!report) continue;
            const auto weapon=static_cast<unsigned char*>(held.weapon);
            unsigned flags=0; float inertia=0;
            if(edf6vr::Readable(weapon,0xEF0)) {
                flags=*reinterpret_cast<unsigned*>(weapon+0xEE0);
                inertia=*reinterpret_cast<float*>(weapon+0xEE8);
            }
            const char* type=edf6vr::TypeName(g_image,held.weapon);
            Log("FENCER slot=%u/%u weapon=%p type=%s model=%p draws=%llu pos=(%.3f,%.3f,%.3f) dir=(%.4f,%.4f,%.4f) EE0=%X EE8=%.5f bones=%llu",
                slot,count,held.weapon,type?type:"?",held.model,g_fencerDraw[slot].draws.exchange(0),
                held.world.m[3][0],held.world.m[3][1],held.world.m[3][2],
                held.forward[0],held.forward[1],held.forward[2],flags,inertia,held.nodeCount);
        }
#ifdef EDF6VR_AUDIO_RESEARCH
        ReportFencerContinuity(now);
        DrainSoundPositions(now);
        DrainSoundLifecycle(now);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_fencerProbeAt.store(0); }
}
// The Brute's door gun (the user, 2026-09-27). The game keeps the gun's aim as
// two angles in the seat (RideInfo): +F8 the heading off the nose (0..pi on the
// left door, -pi..0 on the right: heli_gatling_ctrl's 0..180 on canon_barrel)
// and +138 the pitch, positive down (-pi/2..pi/2 on gun_emplacement); the barrel
// points along RotY(heading)*RotX(pitch)*Z in the hull's axes (+X left, +Y up,
// +Z nose), within 3 degrees of the posed barrel (found by watching memory in
// the 11:44 ride: +138 matched 1176 of 1193 samples).
// The game's own stick moves them, with its own weight, which the user wants
// kept as it is (it is the game's difficulty); the stick's up and down are
// inverted for him in BuildPad. A mod-driven aim (12:00) was worse.
// What is added: on entering the seat the heading is set straight out of the
// door, the base he drew. And straight down, the pole of that yaw/pitch aim,
// no longer stops a barrel still pushed down: the heading is mirrored about
// the door's axis (the nose side to the tail side) and the pitch input turned
// over while the stick is held that way, so the game's own motion carries it up
// the other side, round like a clock hand, without the U-turn a changed "up"
// would give (the fixed cameras of old Resident Evil, his comparison). It holds
// only while the stick stays pushed the same way (within 25 degrees): let go or
// turned, the stick reads as before at once (the user: hardly held at all; a
// moment's jolt of the barrel then is acceptable). Pushed down straight out of the
// door there is no other side (it would be inside the fuselage): it stops.
// The player's view is not touched.
struct BruteGunAssist { void* seat=nullptr; ULONGLONG seenAt=0,logAt=0; unsigned flips=0; float pushed[2]{}; };
BruteGunAssist g_bruteAssist;
void AssistBruteGun(const edf6vr::VehicleSeat& seat,unsigned side) noexcept {
    auto* ride=static_cast<unsigned char*>(seat.seat);
    if(!edf6vr::Readable(ride+0xF8,4,true)||!edf6vr::Readable(ride+0x138,4,true)) return;
    auto* heading=reinterpret_cast<float*>(ride+0xF8);auto* pitch=reinterpret_cast<float*>(ride+0x138);
    // Over the bottom at the pole itself: the record reaches pi/2, where the
    // heading no longer moves the barrel. At 1.45 (12:13 rides) the barrel
    // jumped 13 degrees across, then the aim's own weight took it on down.
    constexpr float kPi=3.14159265f,kPole=1.55f,kNearOut=.2f;
    const float s=side?-1.f:1.f,outward=s*kPi*.5f;
    auto& a=g_bruteAssist;
    const auto now=GetTickCount64();
    const bool entered=seat.seat!=a.seat||now-a.seenAt>500;
    a.seat=seat.seat;a.seenAt=now;
    const float x=g_gunStick[0].load(std::memory_order_relaxed),y=g_gunStick[1].load(std::memory_order_relaxed);
    const float push=std::sqrt(x*x+y*y);
    if(entered) {
        Log("GUNNERASSIST seat %u entered: heading %.3f pitch %.3f; heading set straight out of the door (%.3f)",side+1,*heading,*pitch,outward);
        *heading=outward;
        g_gunPitchTurned.store(false,std::memory_order_relaxed);a.flips=0;
    } else if(g_gunPitchTurned.load(std::memory_order_relaxed)) {
        // Carried over the pole only while pushed the same way (within 25
        // degrees); let go or turned, the stick reads as before.
        if(push<.3f||(x*a.pushed[0]+y*a.pushed[1])/push<.9063f) {
            g_gunPitchTurned.store(false,std::memory_order_relaxed);
            Log("GUNNERASSIST seat %u stick (%.2f,%.2f) let go or turned: it reads as before",side+1,x,y);
        }
    } else if(*pitch>kPole&&y>.5f&&std::fabs(*heading-outward)>kNearOut) {
        // At the bottom and still pushed down (stick up turns the barrel down):
        // over to the other side of the door's axis, pitch input turned over.
        const float was=*heading;
        *heading=s*kPi-was;
        g_gunPitchTurned.store(true,std::memory_order_relaxed);++a.flips;
        a.pushed[0]=x/push;a.pushed[1]=y/push;
        Log("GUNNERASSIST seat %u over the bottom: heading %.3f -> %.3f at pitch %.3f",side+1,was,*heading,*pitch);
    }
    if(now-a.logAt>=1000) {
        a.logAt=now;
        Log("GUNNERASSIST seat %u heading %.3f pitch %.3f stick (%.2f,%.2f) turned %d flips %u",side+1,*heading,*pitch,
            x,y,g_gunPitchTurned.load(std::memory_order_relaxed)?1:0,a.flips);
    }
}
// Runs under the existing camera-update lock; no game transforms are written.
bool ApplyVehicleCamera(void* camera,void* source,void* cameraSoldier,
                        edf6vr::Matrix nativeCamera,edf6vr::PlayerPose& rider,bool& detected) noexcept {
    detected=false;
    if(!g_vehicleReady) return false;
    void* soldier=cameraSoldier;
    if(!edf6vr::IsSupportedSoldier(g_image,soldier)) soldier=g_vrSoldier;
    edf6vr::VehicleSeat seat{};
    if(!edf6vr::ReadVehicleSeat(g_image,soldier,seat)) return false;
    // The mission trucks with their own cab inside (MissionTruckRider): the
    // driver from the cab's own eye (PlaceVehicleAnchor; the game leaves the
    // rider's head 1.5 m up in every truck, under the big one's dashboard),
    // the bed's riders from their heads; the body left out with nothing in its
    // place (the user: the hands stayed where the rider boarded; not needed,
    // as in the other vehicles), and the windows' glass left out of the draw
    // (TruckGlassScope).
    bool truckDriver=false;
    const bool missionTruck=!seat.riderHead&&edf6vr::MissionTruckRider(g_image,seat,g_nodeLookup,&truckDriver);
    if(missionTruck&&!truckDriver) seat.riderHead=true;
    // Never attach to another player's camera or a stale cached soldier.
    if(soldier!=cameraSoldier && (soldier!=g_vrSoldier
       || *static_cast<void**>(soldier)!=g_vrSoldierTable
       || *reinterpret_cast<const unsigned*>(static_cast<unsigned char*>(soldier)+0x314)!=g_vrSoldierId)) return false;
    if(source!=soldier && source!=seat.vehicle && source!=seat.cameraOwner) return false;
    detected=true;
    RefreshSoldierOwner(soldier);g_vrSoldierSeen=GetTickCount64();g_soldierAt=g_vrSoldierSeen;
    const bool changed=!g_vehicleSeen.load() || g_vehicleObject!=seat.vehicle || g_vehicleSeat!=seat.seat
        || g_vehicleCamera!=camera; // +314 is the driver id, changes 5->0 on mounting.
    g_vehicleMounted=true;g_vehicleSeen=GetTickCount64();
    VehicleRecoilUpdate(seat.vehicle);
    if(changed) {
        g_vehicleObject=seat.vehicle;g_vehicleSeat=seat.seat;g_vehicleId=seat.vehicleId;g_vehicleCamera=camera;
        edf6vr::ClearCockpitPose();g_cockpitRig={};
        g_vehicleReference=false;g_vehiclePositionReference=false;
        g_haveReference=false;g_previousMove={};g_hmdHeightOffset=0;g_roomLean[0]=g_roomLean[1]=0;
        g_lastRoomInput[0]=g_lastRoomInput[1]=0;g_handAiming=false;PublishFencerPad({});
        g_nativeTactical=false;ClearClassPresentation();
        Log("VEHICLE enter type=%s id=%u vehicle=%p seat=%p seatCamera=%p source=%p soldier=%p riderHead=%d; native sticks, camera-only XYZ, no vehicle scale",
            edf6vr::TypeName(g_image,seat.vehicle),seat.vehicleId,seat.vehicle,seat.seat,seat.cameraOwner,source,soldier,seat.riderHead);
    }
    if(!g_vrEnabled || !g_fpsEnabled || g_faulted || !edf6vr::ValidCamera(nativeCamera)) return false;
    edf6vr::HmdSample sample{};
    if(!edf6vr::g_openxr.Sample(sample) || !sample.orientationValid || !edf6vr::NormalizedQuat(sample.orientation)) return false;
    if(!g_vehicleReference || g_recenterRequested) {
        if(!edf6vr::VehicleHeadingReference(sample.orientation,g_vehicleHeadReference))return false;
        g_vehicleReference=true;
        g_vehicleEntryLevel={};
        g_vehiclePosition=sample.position;g_vehiclePositionReference=sample.positionValid;
        g_recenterRequested=false;
        Log("VEHICLE recenter heading-only, pitch/roll level reference; positionValid=%d",sample.positionValid);
    }
    edf6vr::Vec3 delta{};
    if(sample.positionValid) {
        if(!g_vehiclePositionReference) {g_vehiclePosition=sample.position;g_vehiclePositionReference=true;}
        if(g_heightResetRequested.exchange(false,std::memory_order_acq_rel)) {
            g_vehiclePosition.y=sample.position.y;
            Log("HEIGHT applied in vehicle: trackingY=%.3f; rotation and horizontal reference unchanged",sample.position.y);
        }
        delta={sample.position.x-g_vehiclePosition.x,sample.position.y-g_vehiclePosition.y,sample.position.z-g_vehiclePosition.z};
    }
    auto base=nativeCamera;
    // Bike rider head or a validated seat-specific cabin preset. Unmapped seats
    // retain their native origin. The selected cockpit overrides its generic anchor.
    const bool head=seat.riderHead && edf6vr::ReadPlayerPose(g_image,soldier,g_nodeLookup,g_eyeSettings,rider,true) && rider.headFound;
    const char* anchor=head?"rider-head":"native-seat-fallback";int seatIndex=-1;
    bool anchored=head;
    if(head) for(int j=0;j<3;++j) base.m[3][j]=rider.eye[j];
    else anchored=edf6vr::PlaceVehicleAnchor(g_image,seat,g_nodeLookup,base,anchor,seatIndex);
    // The native chase camera can retain the on-foot/head heading at boarding.
    // Reset to the physical chassis instead and follow its current full basis.
    // Nix below retains its existing animated chest frame. The entry pitch
    // correction is only for the fallback chase camera, never for a chassis.
    const bool chassisFacing=edf6vr::FaceVehicleForward(seat,base);
    float nativePitch=0;
    if(!chassisFacing && anchored && g_vehicleLevelView) {
        edf6vr::Matrix level{};
        if(edf6vr::LevelVehicleAtEntry(g_vehicleEntryLevel,base,level)) {base=level;nativePitch=g_vehicleEntryLevel.pitch;}
    }
    const bool cockpit=g_cockpitRequested&&g_cockpitHooksReady&&edf6vr::NativeWorldEnabled()&&g_stereoMode>=2&&
        edf6vr::PlaceVehicleCockpit(g_image,seat,g_nodeLookup,base,g_cockpitRig);
    if(cockpit) {
        const bool gunner=g_cockpitRig.kind==edf6vr::CockpitKind::ProteusGunner||g_cockpitRig.kind==edf6vr::CockpitKind::TitanGunner||
            g_cockpitRig.kind==edf6vr::CockpitKind::HeliBruteGunner;
        anchor=g_cockpitRig.kind==edf6vr::CockpitKind::Barga?"barga-spherical-cockpit":
            g_cockpitRig.kind==edf6vr::CockpitKind::TruckPickup?"pickup-cab-cockpit":
            g_cockpitRig.kind==edf6vr::CockpitKind::Crawler?"crawler-cockpit":
            g_cockpitRig.kind==edf6vr::CockpitKind::Tank?"tank-driver-cockpit":
            (g_cockpitRig.kind==edf6vr::CockpitKind::CombatNegling||g_cockpitRig.kind==edf6vr::CockpitKind::CombatGrape||
             g_cockpitRig.kind==edf6vr::CockpitKind::CombatCaliban)?"combat-vehicle-cockpit":
            (g_cockpitRig.kind==edf6vr::CockpitKind::HeliNereid||g_cockpitRig.kind==edf6vr::CockpitKind::Heli602||
             g_cockpitRig.kind==edf6vr::CockpitKind::Heli506||g_cockpitRig.kind==edf6vr::CockpitKind::HeliBrute)?"heli-cockpit":
            g_cockpitRig.kind==edf6vr::CockpitKind::TitanGunner?"titan-gunner-cockpit":
            g_cockpitRig.kind==edf6vr::CockpitKind::HeliBruteGunner?"brute-door-gunner-cockpit":
            g_cockpitRig.kind==edf6vr::CockpitKind::ProteusDriver?"proteus-driver-cockpit":
            g_cockpitRig.kind==edf6vr::CockpitKind::ProteusMissile?"proteus-missile-cockpit":
            gunner?"proteus-gunner-cockpit":"nix-chest-cockpit";
        if(!gunner)seatIndex=0; // gun pods keep their real SGO seat number
        anchored=true;
    }
    else edf6vr::ClearCockpitPose();
    // The Brute's door guns (the user, 2026-09-27): the game's own stick, its
    // up and down inverted (BuildPad, while g_bruteGunnerAt is fresh), the base
    // out of the door and a pass over the bottom (AssistBruteGun). Only the gun
    // changes: the player's view is not touched (he watches it from the booth).
    if(seatIndex>=1&&seatIndex<=2&&edf6vr::HasType(g_image,seat.vehicle,".?AVVehicleHelicopter410@@")) {
        g_bruteGunnerAt.store(GetTickCount64(),std::memory_order_relaxed);
        AssistBruteGun(seat,static_cast<unsigned>(seatIndex-1));
    }
    // A passenger seat keeps the game's own camera, which sits inside the
    // rider's own soldier, and the game goes on drawing it: it is left out as
    // the body is on foot, with nothing drawn in its place. A camera well clear
    // of the soldier (a chase view) leaves it drawn.
    bool passenger=false;float passengerGap=-1;
    if(!head&&!cockpit&&!anchored&&std::strcmp(anchor,"native-passenger-seat")==0
       &&edf6vr::ReadPlayerPose(g_image,soldier,g_nodeLookup,g_eyeSettings,rider,true)&&rider.headFound) {
        float gap=0;for(int j=0;j<3;++j){const float d=nativeCamera.m[3][j]-rider.eye[j];gap+=d*d;}
        passengerGap=std::sqrt(gap);passenger=passengerGap<3.f;
        if(!passenger)rider.headFound=false;
    }
    // The pickups' cab is built over the rider: the body is left out, and
    // nothing in its place (hands drawn by the game would show only where a
    // window is behind them: the cab is drawn over the world).
    const bool pickupCab=cockpit&&g_cockpitRig.kind==edf6vr::CockpitKind::TruckPickup&&
        edf6vr::ReadPlayerPose(g_image,soldier,g_nodeLookup,g_eyeSettings,rider,true)&&rider.headFound;
    // A truck's driver at the cab's eye: the rider's own pose still hides the body.
    const bool truckCab=missionTruck&&truckDriver&&!head&&anchored&&!cockpit&&
        edf6vr::ReadPlayerPose(g_image,soldier,g_nodeLookup,g_eyeSettings,rider,true)&&rider.headFound;
    edf6vr::Matrix result{};float yaw=0;
    const auto eyeBase=cockpit?edf6vr::CockpitSeatedCamera(base):base;
    if(!edf6vr::ComposeVehicleCamera(eyeBase,g_vehicleHeadReference,sample.orientation,delta,result,yaw)) return false;
    auto state=State(camera);if(!state) return false;
    // The left stick drives by the machine's own forward (the hull, or a
    // walker's lower body), not by the head, which is free to look round.
    // g_vehicleHeadYaw carries that rebase; unknown leaves the stick as read.
    // A machine that reads the stick on its own hull (the Negling, the Wagon)
    // or on its lower body (every walker) needs none.
    float stickYaw=0;edf6vr::Vec3 stickForward{};edf6vr::WalkerProbe walker{};
    const bool onHull=edf6vr::VehicleStickOnHull(g_image,seat);
    const bool forwardRead=edf6vr::VehicleStickForward(g_image,seat,g_nodeLookup,base,stickForward,&walker);
    g_vehicleHeadYaw=!onHull&&forwardRead&&edf6vr::VehicleStickYaw(nativeCamera,stickForward,stickYaw)?stickYaw:0.f;
    if(walker.valid) {
        // A walker's pelvis and chest off the native camera, once a second,
        // with where it actually walks (travel) and the stick as read.
        static ULONGLONG walkerAt=0;static edf6vr::Vec3 walkerFrom{};
        const auto now=GetTickCount64();
        if(now-walkerAt>=1000) {
            const edf6vr::Vec3 d{walker.position.x-walkerFrom.x,0,walker.position.z-walkerFrom.z};
            const float span=walkerAt&&now-walkerAt<3000?float(now-walkerAt)/1000.f:0.f,flat=std::sqrt(d.x*d.x+d.z*d.z);
            const float speed=span>0?flat/span:0.f;float koshiYaw=0,bodyYaw=0,travelYaw=0;
            const bool koshiOk=edf6vr::VehicleStickYaw(nativeCamera,walker.koshi,koshiYaw);
            const bool bodyOk=edf6vr::VehicleStickYaw(nativeCamera,walker.body,bodyYaw);
            const bool travelOk=speed>.5f&&edf6vr::VehicleStickYaw(nativeCamera,{d.x/flat,0,d.z/flat},travelYaw);
            Log("WALKER asRead=%d stickYaw=%.3f koshi=%.3f body=%.3f travel=%.3f speed=%.1f stick=(%.2f,%.2f)",
                onHull||!forwardRead?1:0,g_vehicleHeadYaw.load(),koshiOk?koshiYaw:99.f,bodyOk?bodyYaw:99.f,travelOk?travelYaw:99.f,speed,
                g_moveStickRead[0].load(std::memory_order_relaxed),g_moveStickRead[1].load(std::memory_order_relaxed));
            walkerAt=now;walkerFrom=walker.position;
        }
    }
    edf6vr::HeadBasis h{};edf6vr::HeadBasisFromXr(sample.orientation,h);g_lastHeadYaw=h.yaw;
    // Projection scale and IPD are identical to on foot. Native zoom/vehicle
    // camera-distance changes never change metres or tracking displacement.
    g_nativeZoom=g_renderZoom=1;edf6vr::g_openxr.SetBinocularZoom(1);
    edf6vr::g_openxr.SetDisplayMode(g_stereoMode>=2?edf6vr::XrDisplayMode::ProjectionStereo:
        g_stereoMode==1?edf6vr::XrDisplayMode::ProjectionMono:edf6vr::XrDisplayMode::Quad);
    float fov=0,ipd=0;
    if(g_stereoMode>0 && edf6vr::g_openxr.StereoAdvice(fov,ipd)) {
        WriteCameraFov(camera,fov);g_lastRenderFov=fov;g_lastIpd=ipd;
        g_lastEyeOffset=g_stereoMode>=2?(((g_frameEye==1)!=g_swapEyes)?1.0f:-1.0f)*ipd*0.5f*g_ipdScale:0;
        if(edf6vr::NativeWorldEnabled() && g_stereoMode>=2)
            g_lastEyeOffset=edf6vr::NativeWorldCameraOffset(((g_frameEye==1)!=g_swapEyes)?1u:0u,ipd*g_ipdScale);
        if(cockpit&&!edf6vr::ConstrainCockpitCamera(base,result,std::fmin(.22f,std::fmax(.10f,ipd*g_ipdScale*.5f+.05f)),g_cockpitRig.kind,edf6vr::CockpitMirrored(g_cockpitRig)))return false;
        for(int j=0;j<3;++j) result.m[3][j]+=result.m[0][j]*g_lastEyeOffset;
    }
    else if(cockpit&&!edf6vr::ConstrainCockpitCamera(base,result,.10f,g_cockpitRig.kind,edf6vr::CockpitMirrored(g_cockpitRig)))return false;
    for(int k=0;k<3;++k) for(int j=0;j<3;++j)
        g_headBasis[k][j].store(result.m[k][j],std::memory_order_relaxed);
    g_headBasisAt.store(GetTickCount64(),std::memory_order_release);
    state->saved.original=nativeCamera;state->saved.applied=result;state->saved.pending=true;
    // Jumping changes translation every tick. Publishing the matrix first lets
    // the native producer consume a camera before its HMD/cabin record exists,
    // rejecting a perfectly valid stereo pair at Present as matchedHead=0.
    if(cockpit) edf6vr::PublishCockpit({result,base,g_cockpitRig,GetTickCount64()});
    edf6vr::PublishRenderedCamera(*reinterpret_cast<edf6vr::Matrix*>(static_cast<unsigned char*>(camera)+edf6vr::kCameraMatrixOffset),result,sample);
    edf6vr::TraceCamera(result,sample,yaw,1);
    g_cameraWrite=GetTickCount64();edf6vr::g_openxr.MarkCameraPose();edf6vr::g_openxr.MarkSceneLive();
    if(head&&!missionTruck) g_bodyTarget={camera,rider,GetTickCount64()};
    else if(head||truckCab||passenger||pickupCab) g_bodyTarget={camera,rider,GetTickCount64(),true};
    if(missionTruck){g_truckGlassModel.store(static_cast<unsigned char*>(seat.vehicle)+0xE40,std::memory_order_release);g_truckGlassAt.store(GetTickCount64(),std::memory_order_relaxed);}
    static ULONGLONG report=0;
    if(changed || GetTickCount64()-report>5000) {
        report=GetTickCount64();
        Log("VEHICLE camera type=%s anchor=%s seatIndex=%d source=%s native=(%.2f,%.2f,%.2f) eye=(%.2f,%.2f,%.2f) hmdDelta=(%.3f,%.3f,%.3f) localYaw=%.3f stickYaw=%.3f hullStick=%d levelled=%d nativePitch=%.1fdeg facing=%s passengerBodyHidden=%d gap=%.2f",
            edf6vr::TypeName(g_image,seat.vehicle),anchor,seatIndex,edf6vr::TypeName(g_image,source),
            nativeCamera.m[3][0],nativeCamera.m[3][1],nativeCamera.m[3][2],result.m[3][0],result.m[3][1],result.m[3][2],delta.x,delta.y,delta.z,yaw,g_vehicleHeadYaw.load(),
            onHull?1:0,!chassisFacing&&anchored&&g_vehicleLevelView?1:0,nativePitch*57.2957795f,
            cockpit?anchor:chassisFacing?"chassis":"native-fallback",passenger?1:0,passengerGap);
        if(missionTruck) {
            // Where the rider's head and the camera sit in the truck (model
            // coordinates: +X its left), and the glass draws left out.
            float head3[3]{},camera3[3]{};std::uint64_t seen=0,skipped=0;edf6vr::TruckGlassCounts(seen,skipped);
            const bool known=rider.headFound&&edf6vr::VehicleLocalPoint(seat,rider.eye,head3);
            const float at[3]={result.m[3][0],result.m[3][1],result.m[3][2]};
            const bool placed=edf6vr::VehicleLocalPoint(seat,at,camera3);
            Log("TRUCK driver=%d riderHead=%s(%.2f,%.2f,%.2f) camera=%s(%.2f,%.2f,%.2f) vehicle-local; glass draws skipped %llu of %llu seen",
                truckDriver?1:0,known?"":"unknown",head3[0],head3[1],head3[2],placed?"":"unknown",camera3[0],camera3[1],camera3[2],skipped,seen);
        }
        if(cockpit&&g_cockpitRig.kind==edf6vr::CockpitKind::Barga)
            Log("BARGA cockpit chest lean %+.2f m forward (upper body; waist-based otherwise)",edf6vr::BargaChestLean());
    }
    return true;
}
// [FirstPerson] ViewFollowsBody: the body's turn since it was last upright
// (body_tumble.h), put onto the camera about the eye.
void ApplyBodyTumble(void* soldier,edf6vr::Matrix& camera) noexcept {
    if(soldier!=g_bodyTumbleOwner) { g_bodyTumble={}; g_bodyTumbleOwner=soldier; g_bodyTumbling=false; }
    float waist[3][3]{},waistAt[3]{},neck[3]{};
    if(!g_nodeLookup || !edf6vr::ReadNamedBoneFrame(soldier,g_nodeLookup,L"koshi",waist,waistAt)
       || !edf6vr::ReadNamedBone(soldier,g_nodeLookup,L"kubi",neck)) { g_bodyTumbling=false; return; }
    const float spine[3]={neck[0]-waistAt[0],neck[1]-waistAt[1],neck[2]-waistAt[2]};
    float R[3][3]{};
    LARGE_INTEGER ticks{},rate{};
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&rate);
    const double clock=rate.QuadPart?static_cast<double>(ticks.QuadPart)/static_cast<double>(rate.QuadPart):0.0;
    if(!edf6vr::UpdateBodyTumble(g_bodyTumble,waist,spine,kTumbleUprightDeg,kTumbleStartDeg,kTumbleFullDeg,R,
                                 clock,kTumbleWindowSec)) {
        g_bodyTumbling=false; return;
    }
    if(g_bodyTumble.spellDone) {
        g_bodyTumble.spellDone=false;
        if(g_bodyTumble.doneMax>=kTumbleStartDeg)
            Log("TUMBLE spell %.0fms, past %.0f after %.0fms, max %.0fdeg, %s",g_bodyTumble.doneMs,kTumbleStartDeg,
                g_bodyTumble.doneReachMs,g_bodyTumble.doneMax,g_bodyTumble.doneFollowed?"view turned":"too slow: view kept level");
    }
    ++g_tumbleFrames;
    g_tumbleAngleMax=std::max(g_tumbleAngleMax,g_bodyTumble.angle);
    g_tumbleAppliedMax=std::max(g_tumbleAppliedMax,g_bodyTumble.applied);
    if(g_bodyTumble.angle>0) {
        // Which state the game is in while the body is tipped, once per name.
        if(const char* name=edf6vr::SoldierStateName(g_image,soldier)) {
            const char* at=std::strstr(name,"State_");
            const char* shown=at?at:name;
            bool known=false;
            for(unsigned i=0;i<g_tumbleStateCount;++i) if(!std::strncmp(g_tumbleStates[i],shown,sizeof(g_tumbleStates[i])-1)) known=true;
            if(!known && g_tumbleStateCount<8) strncpy_s(g_tumbleStates[g_tumbleStateCount++],shown,_TRUNCATE);
        }
    }
    const bool tumbling=g_bodyTumble.applied>0;
    if(tumbling && !g_bodyTumbling) ++g_tumbleEngaged;
    g_bodyTumbling=tumbling;
    if(!tumbling) return;
    for(int i=0;i<3;++i) {
        const float row[3]={camera.m[i][0],camera.m[i][1],camera.m[i][2]};
        float turned[3]{};
        edf6vr::TumbleApply(R,row,turned);
        for(int j=0;j<3;++j) camera.m[i][j]=turned[j];
    }
}
void ReportBodyTumble() noexcept {
    if(!g_viewFollowsBody) return;
    char states[8*98]{};
    for(unsigned i=0;i<g_tumbleStateCount;++i) {
        strncat_s(states,i?" ":"",_TRUNCATE);
        char cut[64]{}; strncpy_s(cut,g_tumbleStates[i],_TRUNCATE);
        if(char* at=std::strchr(cut,'@')) { if(char* end=std::strchr(at+1,'@')) *end=0; }
        strncat_s(states,cut,_TRUNCATE);
    }
    Log("TUMBLE on frames=%llu engaged=%llu bodyTurnMax=%.0fdeg viewTurnMax=%.0fdeg now=%.0f/%.0f states=[%s]",
        g_tumbleFrames,g_tumbleEngaged,g_tumbleAngleMax,g_tumbleAppliedMax,g_bodyTumble.angle,g_bodyTumble.applied,states);
    g_tumbleAngleMax=0; g_tumbleAppliedMax=0;
}
float WeaponRightTrim() noexcept { return edf6vr::g_openxr.HandSwap()?-g_weaponRight:g_weaponRight; }
// Left-handed mode is on while a Ranger, Wing Diver or Air Raider is on foot in
// VR; a Fencer (a weapon in each hand already), a vehicle and the menus keep the
// ordinary hands.
void UpdateLeftHanded(void* soldier) noexcept {
    const char* role=nullptr;
    if(g_leftHanded && g_vrEnabled && !g_vehicleMounted && soldier && edf6vr::IsSupportedSoldier(g_image,soldier)) {
        if(edf6vr::HasType(g_image,soldier,".?AVAssultSoldier@@")) role="Ranger";
        else if(edf6vr::HasType(g_image,soldier,".?AVPaleWing@@")) role="Wing Diver";
        else if(edf6vr::HasType(g_image,soldier,".?AVEngineer@@")) role="Air Raider";
    }
    const bool want=role!=nullptr;
    if(edf6vr::g_openxr.HandSwap()!=want) {
        edf6vr::g_openxr.SetHandSwap(want);
        Log("LEFTHANDED gun hand %s%s%s; sticks %s",want?"LEFT (":"right",want?role:"",want?")":"",
            want&&g_leftHandedSticks?"swapped too":"as they are");
    }
}
#include "presentation_trace.h"
void AfterUpdate(void* camera) noexcept {
    g_holdCommand={}; g_leftHoldCommand={}; // stale tracking/mission/weapon commands cannot survive this update
    __try {
        ++g_calls;
        g_lastCamera=camera;
        g_cameraUpdate=GetTickCount64();
        if(const auto typeName=edf6vr::TypeName(g_image,camera))
            strncpy_s(g_cameraType,typeName,_TRUNCATE);
        if(edf6vr::Readable(camera,kCameraFarOffset+4)) {
            const float nearPlane=*reinterpret_cast<const float*>(
                static_cast<const unsigned char*>(camera)+kCameraNearOffset);
            const float farPlane=*reinterpret_cast<const float*>(
                static_cast<const unsigned char*>(camera)+kCameraFarOffset);
            if(std::isfinite(nearPlane) && std::isfinite(farPlane)
               && nearPlane>0.001f && farPlane>nearPlane*10) {
                g_cameraNear=nearPlane;
                g_cameraFar=farPlane;
            }
        }
        g_frameLoopThread=GetCurrentThreadId();
        PollInput();
        // One eye per presented frame; the other keeps its previous image. Keyed
        // on the present counter so a second camera updating in the same frame
        // cannot flip it twice. Mono modes report -1 and both views read the
        // single swapchain.
        if(g_vrEnabled && g_stereoMode>=2) {
            // The eye offset is baked into the image right here, and this runs at
            // the fixed 60 Hz tick. Present runs on another thread at anything
            // from 37 to 69 Hz, so keying the flip on the present counter lets the
            // two rates beat and submits an image drawn for one eye as the other.
            // Flip with the camera instead; a present with no new camera behind it
            // then simply repeats an eye, which reshows rather than jumps.
            // The camera runs at a flat 60 Hz and the game presents at 38 to 59,
            // so flipping on every update threw away the eyes that were never
            // drawn: at 38 fps 58% of presented frames repeated the eye the
            // previous one had, which left each eye updating near 12 Hz and is
            // what made the weapon, the nearest thing in the view, flicker. Hold
            // the eye until a frame has actually taken it, and the two alternate
            // at whatever rate the game presents. The watchdog only exists so a
            // renderer that stops handing us images cannot freeze one eye.
            if(edf6vr::NativeWorldEnabled() || edf6vr::g_openxr.WarpDebugging() || (g_stereoMode>=4 && !g_warpAlternate)) {
                // The built eye is as fresh as the drawn one, so there is nothing
                // to gain by taking turns, and taking turns is what makes an
                // imperfect shift flicker instead of merely sitting at the wrong
                // distance. While debugging it also keeps one eye on the game.
                g_frameEye=0;
            } else if(g_eyeSource) {
                if(edf6vr::g_openxr.TakeEyeConsumed() || ++g_eyeHeld>12) {
                    g_frameEye^=1;
                    g_eyeHeld=0;
                }
            }
            else {
                edf6vr::FrameCapture presented{};
                edf6vr::ReadFrameCapture(presented);
                if(presented.presents!=g_lastEyeFlipPresent) {
                    g_lastEyeFlipPresent=presented.presents;
                    g_frameEye^=1;
                }
            }
            edf6vr::g_openxr.SetFrameEye(g_frameEye);
        } else edf6vr::g_openxr.SetFrameEye(-1);
        if(g_measureWithoutVr && g_captureFrames==1 && !edf6vr::PresentCaptureInstalled())
            g_captureRequested.store(true);
        auto bytes=static_cast<unsigned char*>(camera);
        bool supportedSoldier=false, valid=false;
        edf6vr::PlayerPose pose{};
        bool fpsApplied=false,vehicleApplied=false;
        float roll=0;
        edf6vr::Matrix before{},after{};
        if(edf6vr::Readable(camera,edf6vr::kSoldierOffset+8)
           && edf6vr::HasType(g_image,camera,".?AVCharacterGhostCamera@@")) {
            const auto source=*reinterpret_cast<void**>(bytes+edf6vr::kSourceOffset);
            const auto soldier=*reinterpret_cast<void**>(bytes+edf6vr::kSoldierOffset);
            auto nativeMatrix=reinterpret_cast<edf6vr::Matrix*>(bytes+edf6vr::kCameraMatrixOffset);
            const bool wasMounted=g_vehicleMounted;
            bool mountDetected=false;
            if(edf6vr::Readable(nativeMatrix,sizeof(*nativeMatrix),true))
                vehicleApplied=ApplyVehicleCamera(camera,source,soldier,*nativeMatrix,pose,mountDetected);
            g_vehicleMounted=mountDetected;
            UpdateLeftHanded(soldier);
            if(!mountDetected) {
                static ULONGLONG mountReport=0;
                void* owner=edf6vr::IsSupportedSoldier(g_image,soldier)?soldier:g_vrSoldier;
                if(owner && edf6vr::IsSupportedSoldier(g_image,owner) && edf6vr::Readable(owner,0x1558)
                    && edf6vr::HasMountReference(owner) && GetTickCount64()-mountReport>5000) {
                    mountReport=GetTickCount64();
                    auto v=*reinterpret_cast<void**>(static_cast<unsigned char*>(owner)+0x1548);
                    auto seat=*reinterpret_cast<void**>(static_cast<unsigned char*>(owner)+0x1540);
                    Log("VEHICLE unmatched source=%p:%s cameraSoldier=%p:%s owner=%p vehicle=%p:%s seat=%p:%s",
                        source,edf6vr::TypeName(g_image,source),soldier,edf6vr::TypeName(g_image,soldier),owner,
                        v,edf6vr::TypeName(g_image,v),seat,edf6vr::TypeName(g_image,seat));
                }
            }
            if(wasMounted && !g_vehicleMounted) {
                g_vehicleSeen=0;g_vehicleReference=false;g_vehiclePositionReference=false;g_vehicleHeadYaw=0;
                // Keep this owner's standing eye offset across a forced dismount.
                // Re-priming here captures the knockdown animation near the floor
                // and permanently pins the FPS camera there after standing up.
                // The normal owner/id guard still resets it for a different soldier.
                g_haveReference=false;g_lastAimValid=false;g_previousMove={};
                edf6vr::ClearCockpitPose();g_cockpitRig={};
                Log("VEHICLE exit; fresh on-foot tracking, standing anchor retained=%d height=%.3f; foot bindings restored",
                    g_headAnchor.primed,g_headAnchor.offset[1]);
            }
            // Same local camera owner; only explicitly supported SoldierBase derivatives.
            supportedSoldier=source && source==soldier && edf6vr::IsSupportedSoldier(g_image,soldier);
            if(const auto typeName=edf6vr::TypeName(g_image,soldier))
                strncpy_s(g_soldierType,typeName,_TRUNCATE);
            auto matrix=reinterpret_cast<edf6vr::Matrix*>(bytes+edf6vr::kCameraMatrixOffset);
            if(supportedSoldier && edf6vr::Readable(soldier,0x318)
               && edf6vr::Readable(matrix,sizeof(*matrix),true)) {
                RefreshSoldierOwner(soldier);
                g_vrSoldierSeen=GetTickCount64();
                before=*matrix; valid=edf6vr::ValidCamera(before);
                float tacticalDistance=0,tacticalHeight=0;
                // The first dismount update still carries the vehicle's high
                // third-person matrix. Restore the walking camera before
                // classifying that matrix as an Air Raider tactical view.
                const bool tactical=!g_vehicleMounted && !wasMounted && valid
                    && IsNativeTacticalView(soldier,before,g_nativeTactical.load(),&tacticalDistance,&tacticalHeight);
                if(tacticalDistance>g_tacticalPeak) { g_tacticalPeak=tacticalDistance; g_tacticalPeakHeight=tacticalHeight; }
                if(tactical!=g_nativeTactical.exchange(tactical)) {
                    g_haveReference=false; g_previousMove={}; g_hmdHeightOffset=0;
                    ClearClassPresentation();
                    edf6vr::g_openxr.SetDisplayMode(tactical?edf6vr::XrDisplayMode::Quad:
                        g_stereoMode>=2?edf6vr::XrDisplayMode::ProjectionStereo:
                        g_stereoMode==1?edf6vr::XrDisplayMode::ProjectionMono:edf6vr::XrDisplayMode::Quad);
                    Log("TACTICAL native=%d camera=(%.2f,%.2f,%.2f) fromSoldier=%.1fm above=%.1fm; native view/input on headset panel",
                        tactical,before.m[3][0],before.m[3][1],before.m[3][2],tacticalDistance,tacticalHeight);
                }
                edf6vr::SoldierAim aim{};
                const bool aimValid=valid && edf6vr::ReadSoldierAim(soldier,aim);
                if(aimValid && !g_vehicleMounted) {
                    LearnPitchSign(aim.pitch,before.m[2][1]);
                    // One update's worth of walking, measured where the aim is.
                    for(int j=0;j<3;++j)
                        g_soldierStep[j]=g_lastAimValid?aim.position[j]-g_lastAim.position[j]:0;
                    g_lastAimValid=true;
                    // When a soldier was last readable. g_soldierSeen only gets
                    // set while first person is applied, so with VR off it stays
                    // null for the whole mission -- and the auto-deploy, which
                    // waited on it, pressed Enter through a mission it had
                    // already started. This does not care whether VR is on.
                    g_soldierAt=GetTickCount64();
                    g_lastAim=aim;
                    g_lastCameraYaw=std::atan2(before.m[2][0],before.m[2][2]);
                    g_lastCameraPitchUp=std::asin(std::clamp(before.m[2][1],-1.0f,1.0f));
                }
                if(g_fpsEnabled && g_fpsReady && !g_faulted && valid && !tactical
                   && !g_vehicleMounted && edf6vr::ReadPlayerPose(g_image,soldier,g_nodeLookup,g_eyeSettings,pose)) {
                    // Before anything else uses it, and before the headset's own
                    // height goes on: that part is the player really moving and
                    // must not be smoothed.
                    if(g_vrEnabled && g_headRootLock && pose.rootValid) {
                        if(g_headAnchorOwner!=soldier || g_headAnchorId!=pose.objectId) {
                            g_headAnchor={}; g_headAnchorOwner=soldier; g_headAnchorId=pose.objectId;
                        }
                        const float raw[3]={pose.eye[0],pose.eye[1],pose.eye[2]};
                        if(edf6vr::AnchorHead(g_headAnchor,pose.root,raw,pose.eye)) {
                            float square=0;
                            for(int j=0;j<3;++j) square+=(raw[j]-pose.eye[j])*(raw[j]-pose.eye[j]);
                            g_headWobble=std::sqrt(square);
                            g_headWobblePeak=std::max(g_headWobblePeak,g_headWobble);
                        }
                        // Turning with the body: the eye goes where the head goes.
                        if(g_viewFollowsBody && g_bodyTumbling) for(int j=0;j<3;++j) pose.eye[j]=raw[j];
                        if(g_previousMove.have && std::fabs(g_previousMove.inX)<.001f && std::fabs(g_previousMove.inZ)<.001f
                           && g_lastRoomInput[0]==0 && g_lastRoomInput[1]==0) {
                            ++g_idleRootSamples;
                            g_idleRootStepMax=std::max(g_idleRootStepMax,std::sqrt(g_soldierStep[0]*g_soldierStep[0]+g_soldierStep[2]*g_soldierStep[2]));
                        }
                    } else if(g_headDamping>0 && pose.rootValid) {
                        // The written aim is where the weapon points, which is
                        // what leans the body.
                        g_headWobble=edf6vr::SteadyHead(g_headSteady,pose.root,pose.eye,
                                                        static_cast<float>(g_pitchSign)
                                                            *g_lastWrittenPitch,
                                                        g_headDamping,g_headLimit,pose.eye);
                        if(g_headWobble>g_headWobblePeak) g_headWobblePeak=g_headWobble;
                    }
                    edf6vr::Matrix fps{};
                    if(auto state=State(camera)) {
                        // Head height moves the camera only; the body and its collision
                        // volume keep the height the game gave them.
                        float eye[3]={pose.eye[0],pose.eye[1],pose.eye[2]};
                        if(g_vrEnabled && g_vrCameraHeight && std::isfinite(g_hmdHeightOffset))
                            eye[1]+=g_hmdHeightOffset;
                        // Real head movement inside the dead zone, as a lean.
                        // Both of these are the player moving rather than the
                        // game animating, so neither goes through the wobble
                        // filter above.
                        if(g_vrEnabled && g_vrRoomScale
                           && std::isfinite(g_roomLean[0]) && std::isfinite(g_roomLean[1])) {
                            eye[0]+=g_roomLean[0];
                            eye[2]+=g_roomLean[1];
                        }
                        // Where the view is actually drawn from, height and lean
                        // included. The weapon is placed against this rather than
                        // against the soldier's own head, or leaning would slide
                        // the gun the wrong way across the view.
                        for(int j=0;j<3;++j) g_eyeWorld[j]=eye[j];
                        g_eyeWorldValid=true;
                        // World projection needs the game to draw the frustum the
                        // runtime asked for, and stereo needs the eye point moved
                        // along the camera right row by half the eye separation.
                        // The game builds its camera from the aim, so with the
                        // aim on the controller the camera points at the weapon.
                        // The view is built to look along the head instead.
                        //
                        // Built, not turned. Turning it by a yaw and a pitch is
                        // not the same as aiming it: the turn happens in the
                        // camera's own frame, so a yaw on an already pitched
                        // camera tilts as well as turning, and the error grows
                        // with the angle between weapon and head. Worse, a pitch
                        // past 85 degrees is refused outright, and a refusal
                        // leaves the camera where it was, so pointing the weapon
                        // at the sky took the view with it.
                        // Read the actual post-update soldier aim, including the
                        // game's clamps. Camera orientation is an independent
                        // presentation input and must not anchor the reticle.
                        {
                            // Through a dash the soldier is facing where the run
                            // is going, which is not where the weapon points, so
                            // the reticle keeps the heading the hand asked for.
                            const float aimYaw=g_dashAiming.load(std::memory_order_acquire)
                                ?g_dashAimYaw.load(std::memory_order_relaxed):aim.yaw;
                            const auto reference=edf6vr::AimToReference(aimYaw,
                                static_cast<float>(g_pitchSign)*aim.pitch,g_yawOffset);
                            const float shot[3]={reference.x,reference.y,reference.z};
                            edf6vr::g_openxr.SetAimDirection(
                                g_vrEnabled && g_handAiming && aimValid?shot:nullptr,true);
                            PublishSecondAim();
                        }
                        edf6vr::Matrix lookFrom=before;
                        g_viewTurn[0]=g_viewTurn[1]=0;
                        if((g_handAiming || FencerOwnsWeaponPose(soldier)) && g_vrEnabled && g_vrRotation
                           && GetTickCount64()-g_headForwardAt<250) {
                            edf6vr::Matrix aimed{};
                            if(edf6vr::LookAlong(before,g_headForwardWorld,aimed)) {
                                lookFrom=aimed;
                                // Only for the log: how far apart the two are.
                                const float cameraYaw=
                                    std::atan2(before.m[2][0],before.m[2][2]);
                                const float cameraPitch=
                                    std::asin(std::clamp(before.m[2][1],-1.0f,1.0f));
                                const float toDegrees=180.0f/3.14159265f;
                                float apart=(g_lastHeadYaw+g_yawOffset)-cameraYaw;
                                while(apart>3.14159265f) apart-=6.28318531f;
                                while(apart<-3.14159265f) apart+=6.28318531f;
                                g_viewTurn[0]=apart*toDegrees;
                                g_viewTurn[1]=(g_lastHeadPitch-cameraPitch)*toDegrees;
                            }
                        }
                        if(g_vrEnabled && g_stereoMode>0
                           && edf6vr::g_openxr.Mode()==edf6vr::XrMode::HeadsetDisplay) {
                            // F890E divides stock FOV by this native camera-setting
                            // value. Read it before replacing +24 with the VR FOV.
                            g_nativeZoom=*reinterpret_cast<const float*>(bytes+0x410);
                            g_renderZoom=!g_dualActive.load() && g_binocularZoom && std::isfinite(g_nativeZoom)
                                && g_nativeZoom>=1 && g_nativeZoom<=40?g_nativeZoom:1;
                            edf6vr::g_openxr.SetBinocularZoom(g_renderZoom);
                            float wantFov=0, ipd=0;
                            if(edf6vr::g_openxr.StereoAdvice(wantFov,ipd)) {
                                g_lastRenderFov=wantFov;
                                g_lastIpd=ipd;
                                WriteCameraFov(camera,wantFov);
                                if(g_stereoMode>=2) {
                                    const float half=ipd*0.5f*g_ipdScale;
                                    const bool right=(g_frameEye==1)!=g_swapEyes;
                                    const float shift=edf6vr::NativeWorldEnabled()
                                        ?edf6vr::NativeWorldCameraOffset(right?1u:0u,ipd*g_ipdScale):(right?half:-half);
                                    g_lastEyeOffset=shift;
                                    // Native GPU screen-right is -row0 after EDF's Ry(pi).
                                    for(int j=0;j<3;++j) eye[j]+=lookFrom.m[0][j]*shift;
                                } else g_lastEyeOffset=0;
                            }
                        }
                        if(edf6vr::PlaceAtEye(lookFrom,eye,fps)) {
                            // This is the pose the frame about to be drawn belongs
                            // to, and it is what should be declared with it.
                            edf6vr::g_openxr.MarkCameraPose();
                            g_cameraWrite=GetTickCount64();
                            // Keeps the world in front of the player while the
                            // body is out of view -- knocked down, blown away,
                            // looking at the sky -- without keeping it for the
                            // pause menu, where the input stops being read.
                            if(edf6vr::CameraKeepsSceneLive(g_cameraWrite,g_soldierInput)) {
                                edf6vr::g_openxr.MarkSceneLive();
                                ++g_sceneKeptByCamera;
                            }
                            if(g_vrEnabled && g_vrRotation) {
                                edf6vr::Matrix rolled{};
                                // Roll turns the camera about its own forward row, so the
                                // aim direction the game shoots along is unchanged.
                                if(edf6vr::RollCameraToUp(fps,g_hmdUpWorld,rolled,roll)) fps=rolled;
                                else roll=0;
                            }
                            if(g_vrEnabled && g_viewFollowsBody) ApplyBodyTumble(soldier,fps);
                            // The eye translation must follow the FINAL rolled
                            // right axis, just like the second native eye.
                            if(g_vrEnabled && edf6vr::NativeWorldEnabled() && g_stereoMode>=4 && !g_swapEyes)
                                for(int j=0;j<3;++j) fps.m[3][j]+=(fps.m[0][j]-lookFrom.m[0][j])*g_lastEyeOffset;
                            if(g_vrEnabled) {
                                edf6vr::TraceCamera(fps,g_appliedHeadSample,g_yawOffset,0);
                                edf6vr::RecordRenderedCamera(fps,g_appliedHeadSample,g_yawOffset);
                            }
                            state->saved.original=before; state->saved.applied=fps;
                            state->saved.pending=true; *matrix=fps;
                            ++g_fpsApplied; fpsApplied=true;
                            g_lastRoll=roll;
                            g_bodyTarget={camera,pose,GetTickCount64()};
                        }
                    }
                } else if(g_enabled && !g_fpsEnabled && !g_faulted && valid) {
                    if(auto state=State(camera)) {
                        if(state->saved.Apply(*matrix,g_yaw,g_pitch)) ++g_applied;
                    }
                }
                after=*matrix;
            }
        }
        // The weapon is put where the hand is.
        //
        // It is drawn on the soldier's arm bones, from an animation driven by
        // the aim angles, so it sits about a metre in front of him and a
        // metre up whatever the player's arm is doing. That is the whole of
        // the "the gun is too big" complaint: it is not too big, it is too
        // close to the eye and in the wrong place.
        //
        // The hand is known in room space and the head is known in both, so
        // the offset between them, turned by the yaw that maps the room onto
        // the soldier, is the offset from the eye in the world.
        // Who the soldier is, whether or not the weapon is being moved.
        //
        // This used to live inside the follow-the-hand block, so turning that
        // off also turned off the search for the weapon's model -- and the
        // search reported "nothing found" when it had not run at all. What
        // identifies the soldier is not a feature; it is a fact about the frame.
        if(fpsApplied && pose.body) {
            auto soldier=static_cast<unsigned char*>(pose.body)-edf6vr::kBodyModelOffset;
            g_soldierSeen=soldier;
            g_skeletonSeen=pose.skeletonResource;
            // The weapon in hand is the one nearest the hand.
            //
            // It used to be whichever slot could be read first, which is not the
            // same question and was often the wrong answer: with the holstered
            // weapon chosen, the one on screen was left to the animation while
            // the one on the soldier's back was flown around, so the two pointed
            // different ways, the visible one rolled with the dodges, and the
            // held-to-hand distance flicked between two values -- one per weapon.
            // A weapon on the back is most of a metre from the hand that holds
            // the other, so the question answers itself geometrically, without
            // depending on anything that lags by a frame.
            //
            // Debounced, because a wrong answer for even one frame hands the
            // weapon back to the animation for that frame.
            unsigned int nearest=0;
            float closest=1e9f;
            edf6vr::WeaponPose chosen{};
            for(unsigned int slot=0;slot<edf6vr::WeaponSlotCount(soldier);++slot) {
                edf6vr::WeaponPose held{};
                if(!edf6vr::ReadWeaponPose(soldier,slot,held)) continue;
                float away=0;
                if(pose.armsFound) {
                    for(int j=0;j<3;++j) {
                        const float d=held.world.m[3][j]-pose.armsWorld.m[3][j];
                        away+=d*d;
                    }
                } else away=static_cast<float>(slot);
                if(away>=closest) continue;
                closest=away;
                nearest=slot;
                chosen=held;
            }
            if(chosen.valid) {
                if(chosen.model==g_nearestCandidate) ++g_nearestHeld;
                else { g_nearestCandidate=chosen.model; g_nearestHeld=0; }
                if(!g_weaponModelSeen || g_nearestHeld>=5) {
                    g_weaponObject=chosen.weapon;
                    g_weaponTransformSeen=chosen.transform;
                    g_weaponModelSeen=chosen.model;
                }
                for(int j=0;j<3;++j) g_weaponWas[j]=chosen.world.m[3][j];
                g_heldSlot=nearest;
            }
            g_weaponSeen=true;
        }
        g_dualEligible.store(g_dualReady && supportedSoldier && valid && fpsApplied && g_vrEnabled && !g_faulted
            && edf6vr::HasType(g_image,g_vrSoldier,".?AVAssultSoldier@@"));
        g_dualEligibleAt.store(GetTickCount64());
        if(!g_dualEligible.load()) ResetRangerDual();
        PublishFencerStereo(g_vrSoldier,supportedSoldier && valid && fpsApplied && g_vrEnabled && !g_faulted);
        for(unsigned i=0;i<2;++i) g_fencerStereoModels[i].store(g_fencerStereo.models[i],std::memory_order_release);
        ProbeFencerWeapons(supportedSoldier && valid?g_vrSoldier:nullptr,fpsApplied);
        g_muzzleWantValid=false;
        if(g_weaponFollowsHand && fpsApplied && g_eyeWorldValid && pose.body && FencerOwnsWeaponPose(g_vrSoldier)
           && g_vrEnabled && !g_faulted && g_handAiming)
            BuildFencerHands(static_cast<unsigned char*>(pose.body)-edf6vr::kBodyModelOffset,pose);
        else if(g_fencerActive.load()) ResetFencerDual();
        if(g_weaponFollowsHand && fpsApplied && g_eyeWorldValid && pose.body
           && !FencerOwnsWeaponPose(g_vrSoldier)) {
            auto soldier=static_cast<unsigned char*>(pose.body)-edf6vr::kBodyModelOffset;
            float handXr[3]{},handRot[4]{};
            // The palm, not the muzzle end of the controller.
            //
            // The aim pose sits out in front of the controller, and anchoring the
            // weapon on it made the weapon turn about a point in mid-air rather
            // than about the hand -- so any wrist rotation slid the grip out of
            // the hand, worst when pointing up, which is the longest lever. A
            // held object turns about the palm.
            if(edf6vr::g_openxr.GripPose(1,handXr,handRot)
               || edf6vr::g_openxr.HandPose(1,handXr,handRot)) {
                const edf6vr::Vec3 handRoom=edf6vr::XrToGame(
                    edf6vr::Vec3{handXr[0],handXr[1],handXr[2]});
                const edf6vr::Vec3 offset=edf6vr::RotateY(
                    edf6vr::Vec3{handRoom.x-g_lastHeadXr.x,
                                 handRoom.y-g_lastHeadXr.y,
                                 handRoom.z-g_lastHeadXr.z},g_yawOffset);
                // Nudges, in the soldier's frame: how far along the aim the
                // grip sits, and how far right and up from the hand itself.
                // Along the aim as it actually points, pitch included. Using the
                // flat heading alone left the offset horizontal, so looking up
                // or down changed where the gun sat relative to the hand.
                const float c=std::cos(g_lastWrittenYaw), sn=std::sin(g_lastWrittenYaw);
                const float aimPitch=static_cast<float>(g_pitchSign)*g_lastWrittenPitch;
                const float flat=std::cos(aimPitch), rise=std::sin(aimPitch);
                float ahead[3]={sn*flat,rise,c*flat};
                // The hand's own direction where there is one. Near vertical
                // the angles above name a direction up to ten degrees from the
                // hand, and the side they err to flips as the wrist turns --
                // the weapon's roll at the sky, for every class.
                if(g_lastWrittenAheadValid) {
                    const float* a=g_lastWrittenAhead;
                    const float size=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
                    if(std::isfinite(size) && size>0.5f)
                        for(int j=0;j<3;++j) ahead[j]=a[j]/size;
                }
                // The weapon's rotation is the hand's rotation. Not a
                // reconstruction of it.
                //
                // fear-vr hands the game one transform for the weapon and builds
                // it in a single line: the position from the grip pose, the
                // rotation from the aim pose, as the quaternion the runtime gave
                // it (stereo_hook.cpp, synchronizedTransform). No angles are
                // taken apart and put back together anywhere in it.
                //
                // Every fault in this code so far came from doing the opposite.
                // Rebuilding the frame from a yaw and a pitch turned the weapon
                // over past vertical, because those angles cannot say which way
                // is up there. Recovering the roll separately gave answers that
                // swung through a hundred and fifty degrees near the vertical,
                // and the offset rode on them. Crossing the aim with the hand's
                // up fixed the flip and then rolled the weapon whenever the
                // soldier turned, because the two vectors were in different
                // frames. A tracked rotation has none of these problems: it is
                // already the answer.
                //
                // XrToGame negates x and z, which is a half turn about the
                // upright, so the three axes come across as a proper basis and
                // stay orthonormal.
                const edf6vr::Quat turned{handRot[0],handRot[1],handRot[2],handRot[3]};
                auto intoSoldier=[&](const edf6vr::Vec3& v) {
                    return edf6vr::RotateY(edf6vr::XrToGame(edf6vr::QuatRotate(turned,v)),g_yawOffset);
                };
                edf6vr::Vec3 handRight=intoSoldier(edf6vr::Vec3{1,0,0});
                edf6vr::Vec3 handUp=intoSoldier(edf6vr::Vec3{0,1,0});
                edf6vr::Vec3 handAhead=intoSoldier(edf6vr::Vec3{0,0,-1});
                // Kept before the two-handed turn and the roll trim touch them.
                // The draw thread rebuilds the destination against a fresh sample
                // of the tracked hand, so the frame it is measured against has to
                // be the tracked hand and nothing else -- measuring against the
                // turned axes and rebuilding against untuned ones would put the
                // turn in twice.
                const edf6vr::Vec3 rawRight=handRight, rawUp=handUp, rawAhead=handAhead;

                // With both hands on the weapon the barrel runs between them, so
                // the whole basis is turned by the shortest arc that takes the
                // hand's own forward onto that line -- which keeps the roll the
                // wrist is holding instead of inventing one.
                {
                    const float wanted[3]={ahead[0],ahead[1],ahead[2]};
                    const float from[3]={handAhead.x,handAhead.y,handAhead.z};
                    const float axis[3]={from[1]*wanted[2]-from[2]*wanted[1],
                                         from[2]*wanted[0]-from[0]*wanted[2],
                                         from[0]*wanted[1]-from[1]*wanted[0]};
                    float sine=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
                    float cosine=0;
                    for(int j=0;j<3;++j) cosine+=from[j]*wanted[j];
                    if(sine>0.0001f) {
                        const float n[3]={axis[0]/sine,axis[1]/sine,axis[2]/sine};
                        const float angle=std::atan2(sine,cosine);
                        const float ca=std::cos(angle), sa=std::sin(angle);
                        auto turn=[&](edf6vr::Vec3& v) {
                            const float dot=n[0]*v.x+n[1]*v.y+n[2]*v.z;
                            const float cross[3]={n[1]*v.z-n[2]*v.y,
                                                  n[2]*v.x-n[0]*v.z,
                                                  n[0]*v.y-n[1]*v.x};
                            v=edf6vr::Vec3{v.x*ca+cross[0]*sa+n[0]*dot*(1-ca),
                                           v.y*ca+cross[1]*sa+n[1]*dot*(1-ca),
                                           v.z*ca+cross[2]*sa+n[2]*dot*(1-ca)};
                        };
                        turn(handRight); turn(handUp); turn(handAhead);
                    }
                }

                // The trim turns it further, about the line it fires along, which
                // cannot move the shot.
                if(g_weaponRollTrim!=0) {
                    const float tc=std::cos(g_weaponRollTrim), ts=std::sin(g_weaponRollTrim);
                    const edf6vr::Vec3 r=handRight, u=handUp;
                    handRight=edf6vr::Vec3{r.x*tc+u.x*ts,r.y*tc+u.y*ts,r.z*tc+u.z*ts};
                    handUp=edf6vr::Vec3{u.x*tc-r.x*ts,u.y*tc-r.y*ts,u.z*tc-r.z*ts};
                }

                const float sideways[3]={handRight.x,handRight.y,handRight.z};
                const float upward[3]={handUp.x,handUp.y,handUp.z};
                for(int j=0;j<3;++j) g_weaponSideways[j]=sideways[j];
                // The weapon's own root goes in the palm, and the offsets are
                // trims on top of that.
                //
                // They used to be the whole of it, so the root sat two thirds of
                // a metre in front of the hand by default and the weapon orbited
                // a point that far away -- a large sphere, which is exactly what
                // it looked like. fear-vr puts the weapon at the grip pose and
                // nowhere else; the trims exist to nudge it from there, not to
                // find it in the first place.
                const float palm[3]={g_eyeWorld[0]+offset.x,
                                     g_eyeWorld[1]+offset.y,
                                     g_eyeWorld[2]+offset.z};
                const float rootAt[3]={
                    palm[0]+ahead[0]*g_weaponAhead+sideways[0]*WeaponRightTrim()+upward[0]*g_weaponUp,
                    palm[1]+ahead[1]*g_weaponAhead+sideways[1]*WeaponRightTrim()+upward[1]*g_weaponUp,
                    palm[2]+ahead[2]*g_weaponAhead+sideways[2]*WeaponRightTrim()+upward[2]*g_weaponUp};
                // The slot being aimed is the one whose fire direction agrees
                // with the angles; the other is holstered.
                // The weapon in hand is the one that was in hand.
                //
                // It used to be chosen afresh every frame by asking which weapon
                // points the way the aim does, within eight degrees. Turn quickly
                // and neither answers -- the game's own weapon direction is a
                // frame behind -- and then nothing was written at all, so the
                // weapon fell back to wherever the animation had it and snapped
                // there for a frame. Measured: over one five-second stretch this
                // ran on two frames out of three hundred, over another on none.
                // That is the flicker, and it was being read as sliding too.
                //
                // The drawn model already says which weapon it is.
                for(unsigned int slot=0;slot<edf6vr::WeaponSlotCount(soldier);++slot) {
                    edf6vr::WeaponPose held{};
                    if(!edf6vr::ReadWeaponPose(soldier,slot,held)) continue;
                    if(edf6vr::HasType(g_image,held.weapon,".?AVWeapon_RadioContact@@")) {
                        g_radioModel.store(held.model,std::memory_order_relaxed);
                        g_radioHeld.fetch_add(1,std::memory_order_relaxed);
                        if(!g_weaponModelSeen || held.model==g_weaponModelSeen) g_radioChosen.fetch_add(1,std::memory_order_relaxed);
                    }
                    if(g_weaponModelSeen && held.model!=g_weaponModelSeen) continue;
                    // Where the game had it, so the log can say whether our
                    // value survived the frame, and so the draw side has
                    // something to recognise the weapon by.
                    for(int j=0;j<3;++j) g_weaponWas[j]=held.world.m[3][j];
                    g_skeletonSeen=pose.skeletonResource;
                    g_weaponObject=held.weapon;
                    g_weaponTransformSeen=held.transform;
                    g_weaponSeen=true;
                    // Publish only identity and the tracked destination. No weapon
                    // bone/model coordinates are sampled or written on this thread.
                    if(held.nodes && held.nodeCount && held.nodeCount<=kMaxWeaponBones && pose.armsFound) {
                        g_holdCommand.model=held.model;
                        g_holdCommand.weapon=held.weapon;
                        g_holdCommand.soldier=soldier;
                        g_holdCommand.bodyNodes=pose.nodeArray;
                        g_holdCommand.objectId=pose.objectId;
                        g_holdCommand.armsNode=pose.armsNode;
                        g_holdCommand.weaponNodes=held.nodes;
                        g_holdCommand.count=held.nodeCount;
                        g_holdCommand.serial=g_calls;
                        g_holdCommand.refreshed=GetTickCount64();
                        g_holdCommand.updateThread=GetCurrentThreadId();
                        // The lateral axis is the controller's left, and that is
                        // not a slip.
                        //
                        // OpenXR is right handed with the controller looking down
                        // -Z; the game is left handed, its weapon matrix built so
                        // that row0 x row1 == row2 -- checked against its own fire
                        // direction in the log, which the cross product reproduces
                        // to five places. Handing it (right, up, forward) taken
                        // straight from the controller gives row0 x row1 == -row2,
                        // a basis of the opposite handedness, so the transform onto
                        // it is a reflection rather than a turn: the mesh comes out
                        // mirrored, its triangles wound the wrong way round, and
                        // backface culling then throws away the side facing the
                        // player and leaves the inside of the far side showing.
                        // That is the missing half of both the rifle and the
                        // launcher, and why it was half rather than an edge.
                        //
                        // XrToGame cannot fix it: it is a half turn about the
                        // upright, and no turn changes handedness -- only a
                        // reflection does. Negating this one row is that
                        // reflection, and it is the only one of the three that
                        // leaves the weapon pointing where the controller points
                        // and its top upwards.
                        const float to[3][3]={{-sideways[0],-sideways[1],-sideways[2]},
                                             {upward[0],upward[1],upward[2]},
                                             {ahead[0],ahead[1],ahead[2]}};
                        for(int j=0;j<3;++j) {
                            g_holdCommand.hand.palm[j]=rootAt[j];
                            for(int k=0;k<3;++k) g_holdCommand.hand.axes[k][j]=to[k][j];
                            g_weaponPut[j]=rootAt[j];
                        }
                        // The raw tracked hand this destination was built from.
                        // Everything the destination adds -- the trims, the two
                        // handed turn, the roll -- is a fixed relation to it, so
                        // the draw thread can rebuild the destination from a
                        // newer hand without repeating any of that reasoning.
                        const edf6vr::Vec3 raw[3]={rawRight,rawUp,rawAhead};
                        for(int k=0;k<3;++k) {
                            g_holdCommand.handAxes[k][0]=raw[k].x;
                            g_holdCommand.handAxes[k][1]=raw[k].y;
                            g_holdCommand.handAxes[k][2]=raw[k].z;
                        }
                        for(int j=0;j<3;++j) {
                            g_holdCommand.handPos[j]=palm[j];
                            g_holdCommand.eyeWorld[j]=g_eyeWorld[j];
                        }
                        g_holdCommand.headRoom[0]=g_lastHeadXr.x;
                        g_holdCommand.headRoom[1]=g_lastHeadXr.y;
                        g_holdCommand.headRoom[2]=g_lastHeadXr.z;
                        g_holdCommand.yawOffset=g_yawOffset;
                        for(int j=0;j<3;++j) {
                            g_holdCommand.step[j]=g_soldierStep[j];
                            g_holdCommand.weaponWas[j]=held.world.m[3][j];
                            g_holdCommand.rootWorld[j]=g_lastAim.position[j];
                        }
                        g_holdCommand.tracked=true;
                        // The bone the weapon hangs from, moved to the same
                        // frame. Out here rather than inside the muzzle flash's
                        // guard, where it sat in 0.50.0 and never once ran:
                        // that guard waits on the draw thread having produced a
                        // muzzle, and the log duly read armBone=0 all session.
                        if(g_weaponArmBone && pose.armsFound
                           && PlaceArmBone(pose.armsNode,g_holdCommand.hand))
                            ++g_armBoneWrites;
                        // And the flash with it. The shot still leaves from
                        // wherever the game decides, which is a separate thing
                        // and not this one.
                        if(g_weaponMuzzleFollows) {
                            float muzzle[3]{};
                            bool have=false;
                            // Resolve in one simulation snapshot, before writing
                            // the attachment. Never combine this world point with
                            // a later render palette during a roll.
                            g_holdCommand.muzzleLocalValid=edf6vr::WeaponLocalPoint(
                                *reinterpret_cast<const edf6vr::Matrix*>(static_cast<const unsigned char*>(held.nodes)+0xB0),
                                held.world.m[3],g_holdCommand.muzzleLocal);
                            have=g_holdCommand.muzzleLocalValid && edf6vr::PlaceWeaponLocalPoint(
                                g_holdCommand.hand,g_holdCommand.muzzleLocal,muzzle);
                            // Only a place the weapon could plausibly be. A stale
                            // one would be written back and read again next time.
                            float away=0;
                            for(int j=0;j<3;++j) away+=(muzzle[j]-rootAt[j])*(muzzle[j]-rootAt[j]);
                            if(have && std::sqrt(away)<kWeaponMuzzleReach) {
                                if(edf6vr::WriteWeaponPosition(held,muzzle)) ++g_muzzleWrites;
                                PlaceShotOrigin(soldier);
                                PlaceBoneProbe(soldier);
                                g_originWrites=g_boneProbeWrites;
                                for(int j=0;j<3;++j) g_muzzleWant[j]=muzzle[j];
                                g_muzzleWantValid=true;
                            }
                        }
                    }
                    break;
                }
            }
        }
        // Identity and destination only. The origin itself is read from the
        // game inside its own laser update, where it and the weapon node root
        // still describe one pose; nothing about where the weapon is gets
        // sampled here any more.
        if(fpsApplied && g_vrEnabled && g_handAiming && g_weaponFollowsHand && g_laserFollowsMuzzle
           && g_holdCommand.tracked && g_holdCommand.weapon && g_holdCommand.weaponNodes && !g_faulted) {
            edf6vr::LaserSightFrame laser{};
            laser.weapon=g_holdCommand.weapon;
            laser.soldier=g_holdCommand.soldier;
            laser.nodes=g_holdCommand.weaponNodes;
            laser.count=g_holdCommand.count;
            laser.objectId=g_holdCommand.objectId;
            laser.hand=g_holdCommand.hand;
            for(int j=0;j<3;++j) laser.root[j]=g_holdCommand.rootWorld[j];
            laser.time=g_holdCommand.refreshed;
            edf6vr::PublishLaserFrame(laser,1);
        }
        else edf6vr::ClearLaserMuzzle();
        if(!fpsApplied) {
            PublishFencerPad({}); g_fencerTurn.store(0);
            ClearClassPresentation();
            g_nativeZoom=g_renderZoom=1;
            edf6vr::g_openxr.SetBinocularZoom(1);
        }
        if(fpsApplied && g_vrEnabled && g_vrRotation && !g_faulted
           && GetTickCount64()-g_headForwardAt<250)
            edf6vr::PublishRadarHeading(g_vrSoldier,g_lastHeadYaw+g_yawOffset,g_headForwardAt);
        else edf6vr::ClearRadarHeading();
        if(!fpsApplied) {
            if(vehicleApplied) {
                const auto aim=edf6vr::QuatRotate(g_vehicleHeadReference,{0,0,-1});
                const float direction[3]={aim.x,aim.y,aim.z};edf6vr::g_openxr.SetAimDirection(direction);
            } else edf6vr::g_openxr.SetAimDirection(nullptr);
            g_weaponTransform=nullptr; g_armsNode=nullptr;
            g_weaponModel=nullptr; g_weaponBones=nullptr; g_weaponBoneCount=0;
            g_weaponBasisValid=false;
        }
        if(!fpsApplied && !(vehicleApplied && pose.headFound) && g_bodyTarget.camera==camera) g_bodyTarget={};
        PublishBoosterOwner(fpsApplied && g_hideBody && edf6vr::HasType(g_image,g_vrSoldier,".?AVPaleWing@@")?g_vrSoldier:nullptr,
                            pose.objectId,GetTickCount64());
        // Menus with the wait removed are unusable, so the moment this stops
        // looking like a live mission the stock limiter goes back. Best effort:
        // if the hook stops being called at all, F5 in a mission is the way back.
        g_nativeStereoProbeActive.store(g_nativeStereoProbe && g_vrEnabled && supportedSoldier && valid,std::memory_order_relaxed);
        const bool nativeLive=edf6vr::NativeWorldEnabled() && g_vrEnabled && !g_faulted &&
            (fpsApplied || vehicleApplied) && g_stereoMode>=4 && !g_swapEyes &&
            edf6vr::g_openxr.Mode()==edf6vr::XrMode::HeadsetDisplay;
        edf6vr::RefreshNativeWorld(nativeLive,g_lastIpd*g_ipdScale,g_lastIpd);
        PublishRangerDualHands();
        // The Ranger's left hand, and only after the call above has built it.
        //
        // AfterUpdate clears both hold commands on entry and fills the left one
        // here, at the end, so a left laser frame published beside the right one
        // is always the empty command this tick wiped -- which is why 1.1.1-dev12
        // changed nothing and every left attachment still fell to the owner
        // guard. Its weapon has a laser attachment of its own; without a frame
        // that names it, it is left wherever the original model has it.
        if(fpsApplied && g_vrEnabled && g_handAiming && g_weaponFollowsHand && g_laserFollowsMuzzle
           && !g_faulted && g_dualActive.load() && g_leftHoldCommand.tracked
           && g_leftHoldCommand.weapon && g_leftHoldCommand.weaponNodes
           && g_leftHoldCommand.weapon!=g_holdCommand.weapon) {
            edf6vr::LaserSightFrame left{};
            left.weapon=g_leftHoldCommand.weapon;
            left.soldier=g_leftHoldCommand.soldier;
            left.nodes=g_leftHoldCommand.weaponNodes;
            left.count=g_leftHoldCommand.count;
            left.objectId=g_leftHoldCommand.objectId;
            left.hand=g_leftHoldCommand.hand;
            for(int j=0;j<3;++j) left.root[j]=g_leftHoldCommand.rootWorld[j];
            left.time=g_leftHoldCommand.refreshed;
            edf6vr::PublishLaserFrame(left,0);
        } else edf6vr::ClearLaserFrame(0);
        // The lock-on markers come from the game's own HUD data and are drawn in
        // world space. Vehicle seats aim with the head, so they need them too,
        // and the lock-on sight's frame, which goes with the cut-away HUD panel.
        edf6vr::SetAimHudSource(camera,nativeLive && ((fpsApplied && g_handAiming) || vehicleApplied),g_reticleScale,vehicleApplied);
        edf6vr::SetNativeWorldQueueEnabled(nativeLive);
        edf6vr::SetLegacyPreUiCapture(!(edf6vr::NativeWorldEnabled() && g_uiRedirect && g_uiLayer));
        UpdateMissionFpsLimit((supportedSoldier && valid) || vehicleApplied,GetTickCount64());
        if(!supportedSoldier || !valid) ++g_rejected;
        const auto now=GetTickCount64();
        TraceFiringPresentation(camera,g_vrSoldier,fpsApplied,nativeLive,before,after);
        if(g_calls==1 || now-g_reportTime>=5000) {
            g_reportTime=now;
            Log("ACTIONWEAPON hideCalls=%llu modelsKept=%llu",g_actionHideCalls.load(),g_actionModelsKept.load());
            Log("TRACKEDBOUNDS candidates=%llu carried=%llu",g_boundsCandidates.load(),g_boundsCarried.load());
            ReportGuideProbe();
            Log("AUDIOHEALTH voiceUnderrunRecoveries=%llu (zero does not exclude output-device glitches)",g_audioUnderruns.load());
            ReportAudioOutput();
            if(edf6vr::NativeWorldEnabled()) {
                const auto q=edf6vr::NativeWorldQueueStatistics();
                Log("NATIVEQUEUE pairs=%llu loops=%llu begin=%llu end=%llu invalid=%llu compositorAvailable=%d",
                    q.pairs,q.loops,q.begins,q.ends,q.invalidMarkers,edf6vr::NativeWorldCompositeAvailable());
            }
            if(g_fencerStereoBatches.load() || g_fencerStereoDepths.load())
                Log("FENCERSTEREO batches=%llu isolatedDepths=%llu nativePose=1 nativeShotOrigin=1",
                    g_fencerStereoBatches.load(),g_fencerStereoDepths.load());
            const auto radar=edf6vr::ReadRadarHeadingStats();
            const auto laser=edf6vr::ReadLaserSightStats();
            // lever is how far the game puts its own origin from the weapon's
            // node root. It is a rigid property of the weapon, so a band that
            // stays narrow says the two were read from one pose; a band that
            // spreads says they were not, and the carried origin would wander.
            Log("LASERSIGHT calls=%llu applied=%llu aimed=%llu carried=%llu lever=[%.3f %.3f]m "
                "native=(%.3f,%.3f,%.3f) muzzle=(%.3f,%.3f,%.3f)",
                laser.calls,laser.applied,laser.aimed,laser.carried,
                laser.leverMax?laser.leverMin:0,laser.leverMax,
                laser.native[0],laser.native[1],laser.native[2],
                laser.muzzle[0],laser.muzzle[1],laser.muzzle[2]);
            // Which guard turned a call away. These and applied sum to calls, so
            // a laser that stops following names its own reason.
            Log("LASERREFUSE stale=%llu owner=%llu identity=%llu geometry=%llu carry=%llu reach=%llu ownSoldierUnframed=%llu lastUnframedOwner=%p frames=%p/%p",
                laser.stale,laser.owner,laser.identity,laser.geometry,laser.refused,laser.reach,
                laser.ownSoldierUnframed,laser.lastUnframedOwner,laser.frameWeapons[0],laser.frameWeapons[1]);
            {
                const auto world=edf6vr::ReadWorldUiStats();
                unsigned long long composites=0,compositeFailures=0;
                edf6vr::WorldUiCompositeCounts(composites,compositeFailures);
                Log("NAMEPLATE on=%d draws status/follower/rescue=%llu/%llu/%llu scopes=%llu offThread=%llu binds=%llu frames=%llu composites=%llu compositeFailures=%llu",
                    g_worldNameplates,g_nameplateCalls[0].load(),g_nameplateCalls[1].load(),g_nameplateCalls[2].load(),
                    world.scopes,world.scopesOffThread,world.binds,world.frames,composites,compositeFailures);
                char notes[512]{};
                edf6vr::ReadWorldUiNotes(notes,sizeof(notes));
                Log("NAMEPLATEWORLD context same/other=%llu/%llu callers=EDF.dll+%X/%X/%X lost status/follower/rescue=%llu/%llu/%llu debug=%d scale=%.2f,%.2f offsetY=%.3f size=[%s] %s",
                    g_nameplateSameContext.load(),g_nameplateOtherContext.load(),g_nameplateCaller[0].load(),
                    g_nameplateCaller[1].load(),g_nameplateCaller[2].load(),g_nameplateLost[0].load(),g_nameplateLost[1].load(),
                    g_nameplateLost[2].load(),g_worldNameplateDebug,g_worldNameplateScaleX,g_worldNameplateScaleY,
                    g_worldNameplateOffsetY,g_nameplateSizeNote,notes);
                unsigned long long clusterComposites=0,clusterFailures=0;
                edf6vr::UiClusterCounts(clusterComposites,clusterFailures);
                Log("UICLUSTER on=%d place=%d class=%d composites=%llu failures=%llu",g_uiCluster,g_uiClusterPlace,
                    g_uiClusterClass,clusterComposites,clusterFailures);
            }
            const auto crosshair=edf6vr::ReadNativeCrosshairStats();
            Log("CROSSHAIR ready=%d calls=%llu hidden=%llu",edf6vr::NativeCrosshairReady(),
                crosshair.calls,crosshair.hidden);
            const auto hands=edf6vr::ReadHandDrawStats();
            Log("HANDMODEL on=%d rig=%d(%s) bodyDraws=%llu layerFrames=%llu refused=%llu passesSeen=0x%X mask=0x%X meshes=%u tris L=%u R=%u partial=%u reweighted=%u collapsed=%u tapered=%u handDraws=%llu viaLayer=%llu layerRejected=%llu dropped=%llu pending=%llu %s",
                g_handModels,g_handRigState.ready,g_handRigState.source,g_handBodyDraws.load(),g_handLayerDraws.load(),g_handRefused.load(),
                g_handPassSeen.load(),g_handPassMask,hands.meshes,hands.trianglesLeft,hands.trianglesRight,hands.trianglesPartial,hands.verticesReweighted,
                hands.verticesCollapsed,hands.verticesTapered,hands.handDraws,hands.layerDraws,hands.layerRejected,hands.dropped,hands.pending,hands.note);
            Log("ZOOM native=%.3f render=%.3f binocular=%d",g_nativeZoom,g_renderZoom,g_binocularZoom);
            Log("MUZZLEFLASH draws=%llu owned=%llu scaled=%llu scale=%.2f leftPrepared=%llu dualPassthrough=%llu efsUpdates=%llu efsLeftUpdates=%llu efsLeftCreates=%llu why=notOurs%llu/staleFrame%llu/class%llu/carried%llu/skeleton%llu/range%llu/carry%llu gapL=%.3f/%.3fm up%+.3f n%llu gapR=%.3f/%.3fm up%+.3f n%llu carryL kept=%llu lost=%llu by%.2fm carryR kept=%llu lost=%llu by%.2fm toPalm=%.3f/%.3f toRoot=%.3f/%.3f",g_flashDraws.load(),g_flashOwned.load(),g_flashScaled.load(),g_muzzleFlashScale.load(),g_flashLeftPrepared.load(),g_flashLeftRejected.load(),g_efsFlashUpdates.load(),g_efsFlashLeftUpdates.load(),g_efsFlashLeftCreates.load(),g_flashLeftWhy[0].load(),g_flashLeftWhy[1].load(),g_flashLeftWhy[2].load(),g_flashLeftWhy[3].load(),g_flashLeftWhy[4].load(),g_flashLeftWhy[5].load(),g_flashLeftWhy[6].load(),g_flashGapLast[0].load(),g_flashGapMax[0].load(),g_flashGapUp[0].load(),g_flashGapSamples[0].load(),g_flashGapLast[1].load(),g_flashGapMax[1].load(),g_flashGapUp[1].load(),g_flashGapSamples[1].load(),g_flashCarryKept[0].load(),g_flashCarryLost[0].load(),g_flashCarryLostBy[0].load(),g_flashCarryKept[1].load(),g_flashCarryLost[1].load(),g_flashCarryLostBy[1].load(),g_flashToPalm[0].load(),g_flashToPalm[1].load(),g_flashToRoot[0].load(),g_flashToRoot[1].load());
            Log("CASING calls=%llu candidates=%llu applied=%llu passthrough=%llu stage=%u",
                g_casingCalls.load(),g_casingCandidates.load(),g_casingApplied.load(),g_casingRejected.load(),g_casingStage.load());
            Log("CLASSEFFECT booster=%llu hidden=%llu marker=%llu moved=%llu muzzleRebased=%llu expired=%llu tactical=%d tacticalPeak=%.1fm above=%.1fm",
                g_boosterDraws.load(),g_boosterHidden.load(),g_markerCalls.load(),g_markerApplied.load(),
                g_muzzleRebased.load(),g_muzzleExpired.load(),g_nativeTactical.load(),g_tacticalPeak,g_tacticalPeakHeight);
            g_tacticalPeak=g_tacticalPeakHeight=0;   // per window, so one strike does not colour the rest of the mission
            Log("RADAR calls=%llu applied=%llu nativeYaw=%.4f headYaw=%.4f",
                radar.calls,radar.applied,radar.nativeYaw,radar.headYaw);
            Log("camera=%p callbacks=%llu applied=%llu rejected=%llu supportedSoldier=%d valid=%d enabled=%d fault=%d yaw=%.2f pitch=%.2f forwardBefore=(%.5f,%.5f,%.5f) forwardAfter=(%.5f,%.5f,%.5f)",
                camera,g_calls,g_applied,g_rejected,supportedSoldier,valid,g_enabled,g_faulted,g_yaw,g_pitch,
                before.m[2][0],before.m[2][1],before.m[2][2],after.m[2][0],after.m[2][1],after.m[2][2]);
            Log("FPS enabled=%d appliedNow=%d applied=%llu head=%d headIndex=%d body=%p hide=%d drawCalls=%llu bodySkipped=%llu eye=(%.4f,%.4f,%.4f) cameraBefore=(%.4f,%.4f,%.4f) cameraAfter=(%.4f,%.4f,%.4f)",
                g_fpsEnabled,fpsApplied,g_fpsApplied,pose.headFound,pose.headIndex,pose.body,g_hideBody,
                g_drawCalls.load(),g_bodySkipped.load(),pose.eye[0],pose.eye[1],pose.eye[2],
                before.m[3][0],before.m[3][1],before.m[3][2],after.m[3][0],after.m[3][1],after.m[3][2]);
            edf6vr::HmdSample sample{};
            const bool haveSample=edf6vr::g_openxr.Sample(sample);
            Log("VR ready=%d on=%d xrRunning=%d xrFrames=%llu stage=%d sample=%d posValid=%d "
                "headYaw=%.4f headPitch=%.4f yawOffset=%.4f pitchSign=%+d locked=%d wroteAim=%llu wroteMove=%llu inputCalls=%llu",
                g_vrReady,g_vrEnabled,edf6vr::g_openxr.Running(),edf6vr::g_openxr.Frames(),
                edf6vr::g_openxr.StageSpace(),haveSample,sample.positionValid,
                g_lastHeadYaw,g_lastHeadPitch,g_yawOffset,g_pitchSign,g_pitchLocked,
                g_vrAimWrites,g_vrMoveWrites,g_vrInputCalls);
            Log("AIMFIELDS soldierYaw=%.4f soldierPitch=%.4f cameraYaw=%.4f cameraPitchUp=%.4f "
                "yawDelta=%.4f wroteYaw=%.4f wrotePitch=%.4f move=(%.3f,%.3f) look=(%.4f,%.4f) limits=(%.3f,%.3f) pos=(%.2f,%.2f,%.2f)",
                g_lastAim.yaw,g_lastAim.pitch,g_lastCameraYaw,g_lastCameraPitchUp,
                std::atan2(std::sin(g_lastAim.yaw-g_lastCameraYaw),std::cos(g_lastAim.yaw-g_lastCameraYaw)),
                g_lastWrittenYaw,g_lastWrittenPitch,
                g_lastAim.moveInput[0],g_lastAim.moveInput[1],g_lastAim.lookInput[0],g_lastAim.lookInput[1],
                g_lastAim.pitchLimit,g_lastAim.yawLimit,
                g_lastAim.position[0],g_lastAim.position[1],g_lastAim.position[2]);
            edf6vr::FrameCapture frame{};
            const bool haveFrame=edf6vr::ReadFrameCapture(frame);
            Log("RENDER installed=%d seen=%d presents=%llu present1=%llu swapChain=%p device=%p context=%p "
                "hwnd=%p %ux%u format=%u samples=%u/%u buffers=%u windowed=%d flags=%08X syncInterval=%u presentFlags=%08X status=%s",
                edf6vr::PresentCaptureInstalled(),haveFrame,frame.presents,frame.present1Calls,
                frame.swapChain,frame.device,frame.context,frame.window,
                frame.width,frame.height,frame.format,frame.sampleCount,frame.sampleQuality,
                frame.bufferCount,frame.windowed,frame.flags,frame.syncInterval,frame.presentFlags,edf6vr::PresentCaptureStatus());
            int sleepNop=0, branchNop=0;
            g_limiterState=ReadLimiterState(sleepNop,branchNop);
            Log("FPSLIMIT requested=%d internal=%s requestedSync=%u effectiveSync=%u",
                g_removeFpsLimit,LimiterName(g_limiterState),edf6vr::RequestedSyncInterval(),frame.syncInterval);
            const DWORD drawThread=g_drawThread.load();
            Log("FPSDIAG changes=%llu loopThread=%lu presentThread=%lu drawThread=%lu "
                "inlineRenderer=%d inlineDraw=%d toggleAllowed=%d",
                g_limiterChanges,g_frameLoopThread,edf6vr::PresentThread(),drawThread,
                edf6vr::PresentThread()==g_frameLoopThread,
                drawThread && drawThread==g_frameLoopThread,g_allowLimiterToggle);
            Log("FPSDIAG internalWait=%s sleepNop=%d branchNop=%d highFpsBehavior=UNVERIFIED",
                LimiterName(g_limiterState),sleepNop,branchNop);
            {
                float wantFov=0, ipd=0;
                const bool advice=edf6vr::g_openxr.StereoAdvice(wantFov,ipd);
                const float degrees=wantFov*57.29578f;
                Log("STEREO mode=%d advice=%d wantVFovDeg=%.2f fovScale=%.2f ipd=%.4f eye=%d offset=%+.4f "
                    "srcHeight=%u pxPerDeg=%.2f fovWrites=%llu projectionLayers=%llu",
                    g_stereoMode,advice,degrees,g_fovScale,ipd,g_frameEye,g_lastEyeOffset,
                    frame.height,degrees>0?static_cast<float>(frame.height)/degrees:0.0f,
                    g_fovWrites,edf6vr::g_openxr.ProjectionLayers());
                ReportHeadsetFit(frame.width,frame.height);
                edf6vr::g_openxr.SetWarpSteps(g_warpSteps);
                edf6vr::g_openxr.SetWarpNearest(g_warpNearest);
                edf6vr::g_openxr.SetWarpEase(g_nearKnee,g_nearScale);
                edf6vr::g_openxr.SetWarpHudless(g_warpHudless);
                ReloadTunables();
                Log("%s",edf6vr::AimHudStatus());
                WritePatchListBesideLog();
                edf6vr::g_openxr.SetDumpDirectory(g_modDirectory);
                // The panels show the game's own 2D content, which it lays out into
                // whatever rectangle it is given. Once that rectangle is narrowed to
                // the headset's horizontal field the content is squeezed, and a panel
                // sized from the texture would show the squeeze rather than undo it.
                edf6vr::g_openxr.SetContentAspect(g_configuredHeight
                    ?static_cast<float>(g_configuredWidth)/static_cast<float>(g_configuredHeight):0.0f);
                edf6vr::g_openxr.SetFovScale(g_fovScale);
                edf6vr::g_openxr.SetWarpEyeScale(g_ipdScale);
                edf6vr::g_openxr.SetWarpDebug(g_warpDebug);
                edf6vr::g_openxr.SetUiLayer(g_uiLayer,g_uiWidth,g_uiDistance);
                edf6vr::SetUiRedirect(g_vrEnabled && g_uiLayer && g_uiRedirect
                    && edf6vr::g_openxr.UiCaptureAvailable());
                edf6vr::g_openxr.SetReticle(g_reticleFollows,g_reticlePixels,g_reticleDistance,
                                            g_reticleScale);
                edf6vr::g_openxr.SetEyeWarp(g_stereoMode>=4,g_cameraNear,g_cameraFar);
                if(g_stereoMode>=4)
                    Log("WARP eyes=%llu uiPanels=%llu near=%.3f far=%.1f steps=%.0f nearest=%.2f keepUi=%d "
                        "alternate=%d debug=%.0f panel=%d %.2fm@%.2fm redirect=%s preUi=%u/%s gate=%s status=%s mask=%s",
                        edf6vr::g_openxr.WarpedEyes(),edf6vr::g_openxr.UiLayers(),
                        g_cameraNear,g_cameraFar,g_warpSteps,
                        g_warpNearest,g_warpHudless,g_warpAlternate,g_warpDebug,
                        g_uiLayer?1:0,g_uiWidth,g_uiDistance,edf6vr::UiRedirectNote(),
                        edf6vr::PreUiBinds(),edf6vr::PreUiNote(),edf6vr::UiGateNote(),
                        edf6vr::g_openxr.WarpStatusText(),edf6vr::g_openxr.MaskStatusText());
                if(g_depthProbe) {
                    edf6vr::ReportDepthProbe(&LogText);
                    // Read only, and only the words around the field of view.
                    auto lens=g_lastCamera;
                    if(lens && edf6vr::Readable(lens,0x60)) {
                        char text[400]{}; int at=0;
                        for(std::size_t offset=0x10;offset<0x60 && at>=0 && at<static_cast<int>(sizeof(text));offset+=4) {
                            const float value=*reinterpret_cast<const float*>(
                                static_cast<const unsigned char*>(lens)+offset);
                            at+=std::snprintf(text+at,sizeof(text)-static_cast<std::size_t>(at),
                                              " %02X=%.4f",static_cast<unsigned>(offset),value);
                        }
                        Log("CLIP camera=%p%s",lens,text);
                    }
                }
                Log("STEREO reshownFrames=%llu eyeRepeats=%llu eyeSource=%d "
                    "(reshown: frames with no new image; repeats: two images in a row for one eye)",
                    edf6vr::g_openxr.ReshownFrames(),edf6vr::g_openxr.EyeRepeats(),g_eyeSource);
            }
            {
                const auto stateNow=GetTickCount64();
                const auto age=[&](ULONGLONG stamp) { return stamp?static_cast<long long>(stateNow-stamp):-1LL; };
                ReportBodyTumble();
                Log("STATE sinceInputMs=%lld sinceCameraWriteMs=%lld sinceCameraUpdateMs=%lld "
                    "sinceBodyDrawMs=%lld sceneAgeMs=%llu moveBasis=%d force=%d "
                    "camera=%s soldier=%s display=%s headSteady=%.2f wobble=%.1fcm peak=%.1fcm keptByCamera=%llu",
                    age(g_soldierInput),age(g_cameraWrite),age(g_cameraUpdate),age(g_bodyDraw),
                    edf6vr::g_openxr.SceneAgeMs(),g_moveBasis.candidate,g_displayForce,
                    g_cameraType,g_soldierType,
                    edf6vr::g_openxr.SceneAgeMs()<750?"stereo":"board",
                    g_headDamping,g_headWobble*100.0f,g_headWobblePeak*100.0f,g_sceneKeptByCamera);
                g_headWobblePeak=0;
                Log("VIEWANCHOR enabled=%d primed=%d idleRootSamples=%u idleRootStepMax=%.6fm (no move input; excludes camera/bone animation)",
                    g_headRootLock,g_headAnchor.primed,g_idleRootSamples,g_idleRootStepMax);
                g_idleRootSamples=0; g_idleRootStepMax=0;
            }
            {
                int sessionState=0, endFrame=0, layerCount=-1, layerKind=0, fovDeg=0, images=0;
                unsigned long long endFrameFailures=0, notRendering=0;
                edf6vr::g_openxr.ReadSubmission(sessionState,endFrame,endFrameFailures,notRendering,
                                                layerCount,layerKind,fovDeg,images);
                int alphaLow=-1, alphaHigh=-1;
                edf6vr::g_openxr.ReadAlpha(alphaLow,alphaHigh);
                Log("SUBMIT sessionState=%d lastEndFrame=%d endFrameFailures=%llu notRendering=%llu "
                    "lastLayerCount=%d kind=%s fovDeg=%d images=%s%s alpha=%d..%d",
                    sessionState,endFrame,endFrameFailures,notRendering,layerCount,
                    layerKind==0?"none":(layerKind==1?"board":"world"),fovDeg,
                    (images&1)?"L":"-",(images&2)?"R":"-",alphaLow,alphaHigh);
            }
            Log("VR display mode=%s layers=%llu",
                edf6vr::g_openxr.Mode()==edf6vr::XrMode::HeadsetDisplay?"headset":"tracking only",
                edf6vr::g_openxr.SubmittedLayers());
            ReportPerf(now);
            {
                // Nothing uses the controllers yet. This says whether they arrive
                // at all through an instance negotiated straight against the
                // runtime with no loader in between, which is the one thing that
                // has to be true before the weapon can be put on one.
                float leftPos[3]{},leftRot[4]{},rightPos[3]{},rightRot[4]{};
                const bool left=edf6vr::g_openxr.HandPose(0,leftPos,leftRot);
                const bool right=edf6vr::g_openxr.HandPose(1,rightPos,rightRot);
                // The game may have resolved XInput before this plugin loaded,
                // in which case the swapped import is never asked and the sweep
                // is the way in. Kept trying, because the module it lives in may
                // not be loaded yet either.
                edf6vr::SweepResolvedXInput(g_image,&LogText);
                Log("REACH bands <8cm=%u <11=%u <14=%u <17=%u <20=%u <25=%u <32=%u more=%u",
                    g_handBands[0],g_handBands[1],g_handBands[2],g_handBands[3],
                    g_handBands[4],g_handBands[5],g_handBands[6],g_handBands[7]);
                // The same, counted only while the right trigger is held, which
                // is how the player marks out the zone they want.
                Log("REACH while firing <8cm=%u <11=%u <14=%u <17=%u <20=%u <25=%u <32=%u more=%u "
                    "nearest=%.3f furthest=%.3f",
                    g_zoneBands[0],g_zoneBands[1],g_zoneBands[2],g_zoneBands[3],
                    g_zoneBands[4],g_zoneBands[5],g_zoneBands[6],g_zoneBands[7],
                    g_zoneNearest>8?0.0f:g_zoneNearest,g_zoneFurthest);
                // Where the hand actually goes, along the head's own axes.
                // "ahead" is the one that matters: the gesture is level with the
                // head, ordinary play is out in front of it.
                Log("SHAPE all ahead=[%.2f %.2f] right=[%.2f %.2f] up=[%.2f %.2f] "
                    "| firing ahead=[%.2f %.2f] right=[%.2f %.2f] up=[%.2f %.2f]",
                    g_shapeAll.low[0],g_shapeAll.high[0],g_shapeAll.low[1],g_shapeAll.high[1],
                    g_shapeAll.low[2],g_shapeAll.high[2],
                    g_shapeFiring.low[0],g_shapeFiring.high[0],
                    g_shapeFiring.low[1],g_shapeFiring.high[1],
                    g_shapeFiring.low[2],g_shapeFiring.high[2]);
                Log("PAD %s xinput=%s frames=%llu",g_padSeen,
                    edf6vr::XInputBridgeStatus(),g_padFrames);
                g_padSeenButtons=0; g_padSeenLeft=0; g_padSeenRight=0;
                g_padSeenStick[0]=g_padSeenStick[1]=g_padSeenStick[2]=0;
                g_padSeenGesture=false; g_padSeenClosest=9;
                for(auto& band:g_handBands) band=0;
                for(auto& band:g_zoneBands) band=0;
                g_zoneFurthest=0; g_zoneNearest=9;
                g_shapeAll=Shape{}; g_shapeFiring=Shape{};
                Log("HANDS input=%s aiming=%d band=%.1f viewTurn=(%.1f,%.1f) "
                    "left=%d (%.3f,%.3f,%.3f) right=%d (%.3f,%.3f,%.3f) "
                    "rightForward=(%.3f,%.3f,%.3f)",
                    edf6vr::g_openxr.InputStatus(),g_handAiming?1:0,g_aimBandDegrees,
                    g_viewTurn[0],g_viewTurn[1],
                    left?1:0,leftPos[0],leftPos[1],leftPos[2],
                    right?1:0,rightPos[0],rightPos[1],rightPos[2],
                    // The aim pose points along its own negative Z, as every
                    // OpenXR pose does.
                    -2.0f*(rightRot[0]*rightRot[2]+rightRot[3]*rightRot[1]),
                    -2.0f*(rightRot[1]*rightRot[2]-rightRot[3]*rightRot[0]),
                    -(1.0f-2.0f*(rightRot[0]*rightRot[0]+rightRot[1]*rightRot[1])));
            }
            Log("VR room=(%.3f,%.3f,%.3f) heightOffset=%.3f roll=%.4f moveBasis=%d scale=%.3f samples=%d roomInput=(%.3f,%.3f) dead=%.2f away=%.2f lean=(%.2f,%.2f) status=%s",
                sample.position.x,sample.position.y,sample.position.z,g_hmdHeightOffset,g_lastRoll,
                g_moveBasis.candidate,g_moveBasis.scale,g_moveBasis.samples,
                g_lastRoomInput[0],g_lastRoomInput[1],g_roomDead,g_roomAway,
                g_roomLean[0],g_roomLean[1],edf6vr::g_openxr.Status());
            const auto diagnosticSlots=fpsApplied?edf6vr::WeaponSlotCount(static_cast<unsigned char*>(pose.body)-edf6vr::kBodyModelOffset):0;
            for(unsigned int slot=0;slot<diagnosticSlots;++slot) {
                auto soldier=static_cast<unsigned char*>(pose.body)-edf6vr::kBodyModelOffset;
                edf6vr::WeaponPose held{};
                const bool found=edf6vr::ReadWeaponPose(soldier,slot,held);
                float dot=0; for(int j=0;j<3;++j) dot+=held.forward[j]*after.m[2][j];
                Log("AIM diagnosticOnly=1 slot=%u weapon=%p valid=%d worldDirection=(%.5f,%.5f,%.5f) cameraZCos=%.6f",
                    slot,held.weapon,found,held.forward[0],held.forward[1],held.forward[2],dot);
                if(!found) continue;
                // Everything the hand-held weapon work needs to know, in one
                // line: where the weapon actually is, where the hand actually
                // is, and whether the direction it fires along is a function of
                // the aim angles alone. If it is, then writing the aim angles is
                // enough to steer the shot and the transform is only a picture;
                // if it is not, the transform has to carry the shot too.
                const float pitch=static_cast<float>(g_pitchSign)*g_lastAim.pitch;
                const float cp=std::cos(pitch);
                const float fromAngles[3]={std::sin(g_lastAim.yaw)*cp,std::sin(pitch),
                                           std::cos(g_lastAim.yaw)*cp};
                float agree=0; for(int j=0;j<3;++j) agree+=held.forward[j]*fromAngles[j];
                float rightPos[3]{},rightRot[4]{};
                const bool haveHand=edf6vr::g_openxr.HandPose(1,rightPos,rightRot);
                const edf6vr::Vec3 handGame=edf6vr::XrToGame(
                    edf6vr::Vec3{rightPos[0],rightPos[1],rightPos[2]});
                // Where inside the weapon's own model its place in the world is
                // kept. A targeted sweep now that the model itself is known: the
                // drawn mesh sits on the arms bone, so the value that matters
                // will be within arm's reach of it.
                if(held.model && pose.armsFound) {
                    char places[200]="none";
                    int written=0;
                    __try {
                        if(edf6vr::Readable(held.model,0x1000)) {
                            auto modelBytes=static_cast<const unsigned char*>(held.model);
                            for(std::size_t off=0;off+12<=0x1000 && written<4;off+=4) {
                                auto v=reinterpret_cast<const float*>(modelBytes+off);
                                float away=0;
                                bool sane=true;
                                for(int j=0;j<3;++j) {
                                    if(!std::isfinite(v[j])) { sane=false; break; }
                                    const float d=v[j]-pose.armsWorld.m[3][j];
                                    away+=d*d;
                                }
                                if(!sane || away>1.0f) continue;
                                const std::size_t already=written?std::strlen(places):0;
                                std::snprintf(places+already,sizeof(places)-already,
                                              written?" +%zX(%.2f)":"+%zX(%.2f)",
                                              off,std::sqrt(away));
                                ++written;
                            }
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    // The soldier keeps his model at +0x860 and his skeleton at
                    // +0x900, so a model's own skeleton sits 0xA0 past it. If the
                    // weapon has one, the mesh is placed by its own nodes and not
                    // by anything looked at so far.
                    void* ownResource=nullptr; unsigned long long ownCount=0;
                    __try {
                        auto registry=static_cast<unsigned char*>(held.model)+0xA0;
                        if(edf6vr::Readable(registry,0x28)) {
                            ownResource=*reinterpret_cast<void* const*>(registry);
                            ownCount=*reinterpret_cast<const unsigned long long*>(registry+0x20);
                            if(ownCount>4096) ownCount=0;
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    Log("WEAPONMODEL model=%p (weapon+%zX) slot=%u nearArmsAt=%s ownSkeleton=%p nodes=%llu hidden=%llu boneWrites=%llu doubles=%llu muzzleWrites=%llu",
                        held.model,edf6vr::kWeaponModelOffset,g_heldSlot,places,ownResource,ownCount,
                        g_weaponHidden,g_weaponBoneWrites.load(),g_weaponDoubles,g_muzzleWrites);
                }
                // Where the support hand goes while the trigger is held. The
                // player marks the zone out by holding the weapon as they mean
                // to and firing, exactly as the head gesture zone was measured.
                Log("GRIP seen=%u ahead=[%.3f %.3f] right=[%.3f %.3f] up=[%.3f %.3f] "
                    "firing=%u mean=(%.3f,%.3f,%.3f) twoHand=%d",
                    g_gripSamples,
                    g_gripLow[0]>8?0.0f:g_gripLow[0],g_gripHigh[0]<-8?0.0f:g_gripHigh[0],
                    g_gripLow[1]>8?0.0f:g_gripLow[1],g_gripHigh[1]<-8?0.0f:g_gripHigh[1],
                    g_gripLow[2]>8?0.0f:g_gripLow[2],g_gripHigh[2]<-8?0.0f:g_gripHigh[2],
                    g_gripSamples,
                    g_gripSamples?g_gripSum[0]/g_gripSamples:0.0f,
                    g_gripSamples?g_gripSum[1]/g_gripSamples:0.0f,
                    g_gripSamples?g_gripSum[2]/g_gripSamples:0.0f,
                    g_twoHandOn?1:0);
                // Keep usable calibration evidence even while the player holds
                // the stock steady without firing (the GRIP sample window is
                // trigger-gated). This is diagnostic only, never auto-calibrates.
                if(g_twoHandOn) {
                    float leftPalm[3]{},rightPalm[3]{},leftQ[4]{},rightQ[4]{};
                    if(edf6vr::g_openxr.GripPose(0,leftPalm,leftQ)
                       && edf6vr::g_openxr.GripPose(1,rightPalm,rightQ)) {
                        Log("TWOHAND alignment palmDeltaXR=(%.4f,%.4f,%.4f) lateralOnly=1 aimPitch=%.2fdeg",
                            leftPalm[0]-rightPalm[0],leftPalm[1]-rightPalm[1],leftPalm[2]-rightPalm[2],
                            g_lastWrittenPitch*static_cast<float>(g_pitchSign)*57.2957795f);
                    }
                }
                for(int j=0;j<3;++j) { g_gripLow[j]=9; g_gripHigh[j]=-9; g_gripSum[j]=0; }
                g_gripSamples=0;
                // How far the weapon's own root is from the hand holding it.
                //
                // If the weapon is rigidly held this cannot change, whatever the
                // player does; if it does change, the placement is not rigid and
                // the arithmetic is wrong somewhere. Reported as a range over the
                // last few seconds, which is the only way to see it move.
                HoldDrawStats hold{};
                AcquireSRWLockExclusive(&g_holdStatsLock);
                hold=g_holdStats; g_holdStats={};
                ReleaseSRWLockExclusive(&g_holdStatsLock);
                Log("HOLD lever=[%.3f %.3f]m over %u draws source=[%.3f %.3f] errorMax=%.6f rejected=%u nested=%u changed=%u restoreFaults=%u updateTid=%lu drawTid=%lu",
                    hold.applied?hold.targetMin:0,hold.targetMax,hold.applied,
                    hold.applied?hold.sourceMin:0,hold.sourceMax,hold.errorMax,
                    hold.rejected,hold.nested,hold.changed,hold.restoreFaults,hold.updateThread,hold.drawThread);
                Log("HOLD renderPalette=%p passMask=0x%X displacementMax=%.3fm standing=[%.3f %.3f]m walking=[%.3f %.3f]m step=%.3fm "
                    "paletteVsNode=%.4fm/%.2fdeg",
                    hold.palette,hold.passMask,hold.displacementMax,
                    hold.standing.high?hold.standing.low:0,hold.standing.high,
                    hold.walking.high?hold.walking.low:0,hold.walking.high,hold.stepMax,
                    hold.rootGapMax,hold.rootTurnMax);
                {
                    const unsigned draws=g_yawRetimeDraws.exchange(0,std::memory_order_relaxed);
                    const unsigned turned=g_yawRetimeTurned.exchange(0,std::memory_order_relaxed);
                    const unsigned unknown=g_yawRetimeUnknown.exchange(0,std::memory_order_relaxed);
                    const unsigned refused=g_yawRetimeRefused.exchange(0,std::memory_order_relaxed);
                    const float most=g_yawRetimeMaxDeg.exchange(0,std::memory_order_relaxed);
                    if(draws) Log("HOLDYAW draws=%u turnedOntoDrawnCamera=%u maxTurn=%.2fdeg noMatch=%u refused=%u",
                                  draws,turned,most,unknown,refused);
                }
                if(const unsigned support=g_supportKicks.exchange(0,std::memory_order_relaxed))
                    Log("RECOILHAND the steadying hand kicked with the weapon in %u hand draws",support);
                if(g_radioHeld.load(std::memory_order_relaxed))
                    Log("RADIOPROBE held=%llu chosen=%llu model=%p draws=%llu passes=0x%X fastPath=%llu carried=%llu captured=%llu replayed=%llu "
                        "heldDraws by pass=%llu/%llu/%llu/%llu knownSiteDraws by pass=%llu/%llu/%llu/%llu",
                        g_radioHeld.load(),g_radioChosen.load(),g_radioModel.load(),g_radioDraws.load(),g_radioPasses.load(),
                        g_radioFast.load(),g_radioPrepared.load(),g_radioCaptured.load(),g_radioReplayed.load(),
                        g_radioPassCalls[0].load(),g_radioPassCalls[1].load(),g_radioPassCalls[2].load(),g_radioPassCalls[3].load(),
                        edf6vr::WeaponSiteProbeCount(0),edf6vr::WeaponSiteProbeCount(1),edf6vr::WeaponSiteProbeCount(2),edf6vr::WeaponSiteProbeCount(3));
                Log("ROLL wrist=%.1fdeg trim=%.1fdeg enabled=%d steady=%d settled=%d",
                    g_weaponRollNow*180.0f/3.14159265f,
                    g_weaponRollTrim*180.0f/3.14159265f,
                    g_weaponRoll?1:0,g_steadyValid?1:0,g_settled);
                Log("ARMS found=%d index=%d world=(%.3f,%.3f,%.3f) fromMuzzle=%.3fm",
                    pose.armsFound?1:0,pose.armsIndex,
                    pose.armsWorld.m[3][0],pose.armsWorld.m[3][1],pose.armsWorld.m[3][2],
                    std::sqrt((pose.armsWorld.m[3][0]-held.world.m[3][0])*(pose.armsWorld.m[3][0]-held.world.m[3][0])
                             +(pose.armsWorld.m[3][1]-held.world.m[3][1])*(pose.armsWorld.m[3][1]-held.world.m[3][1])
                             +(pose.armsWorld.m[3][2]-held.world.m[3][2])*(pose.armsWorld.m[3][2]-held.world.m[3][2])));
                Log("WEAPON follow=%d writes=%llu modelWrites=%llu faults=%llu put=(%.3f,%.3f,%.3f) "
                    "gameHadItAt=(%.3f,%.3f,%.3f) muzzleAt=(%.3f,%.3f,%.3f) originWrites=%llu armBone=%llu armDrift=[%.3f %.3f]m probeDrift=[%.3f %.3f]m",
                    g_weaponFollowsHand?1:0,g_weaponWrites,g_weaponModelWrites,g_weaponFaults,
                    g_weaponPut[0],g_weaponPut[1],g_weaponPut[2],
                    g_weaponWas[0],g_weaponWas[1],g_weaponWas[2],
                    g_muzzleWant[0],g_muzzleWant[1],g_muzzleWant[2],g_originWrites,g_armBoneWrites,
                    g_armDriftMax?g_armDriftMin:0,g_armDriftMax,
                    g_probeDriftMax?g_probeDriftMin:0,g_probeDriftMax);
                g_armDriftMin=1e30f; g_armDriftMax=0;
                // Neither of the weapon's own two candidates was the shot
                // origin, so the soldier is asked as well. A shot that leaves
                // from the body most likely leaves from a field of his.
                ReportBones(g_soldierSeen);
                ReportPositions("weapon",g_weaponObject,0x360,g_lastAim.position,3.0f);
                ReportPositions("soldier",g_soldierSeen,0x1980,g_lastAim.position,2.0f);
                Log("WEAPON slot=%u transform=%p world=(%.3f,%.3f,%.3f) soldier=(%.3f,%.3f,%.3f) "
                    "eye=(%.3f,%.3f,%.3f) handRoom=%d (%.3f,%.3f,%.3f) "
                    "aimAngleCos=%.5f right=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f)",
                    slot,held.transform,
                    held.world.m[3][0],held.world.m[3][1],held.world.m[3][2],
                    g_lastAim.position[0],g_lastAim.position[1],g_lastAim.position[2],
                    pose.eye[0],pose.eye[1],pose.eye[2],
                    haveHand?1:0,handGame.x,handGame.y,handGame.z,agree,
                    held.world.m[0][0],held.world.m[0][1],held.world.m[0][2],
                    held.world.m[1][0],held.world.m[1][1],held.world.m[1][2]);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_faulted=true; Log("FAULT: validation/write exception; test disabled until restart"); }
    g_fastDrawWeapon.store(g_holdCommand.model,std::memory_order_release);
    g_fastDrawLeft.store(g_leftHoldCommand.model,std::memory_order_release);
    g_fastDrawBody.store(g_bodyTarget.pose.body,std::memory_order_release);
    PublishActionHands(g_vrEnabled && g_fpsEnabled && !g_faulted && g_weaponFollowsHand
        && !g_vehicleMounted && !g_nativeTactical.load());
}
void __fastcall HookUpdate(void* camera,const void* context) {
    AcquireSRWLockExclusive(&g_lock);
    RestoreBeforeUpdate(camera);
    ReleaseSRWLockExclusive(&g_lock);
    // Preserve the game's update and its exception behavior. No worker-thread memory writes.
    g_original(camera,context);
    AcquireSRWLockExclusive(&g_lock);
    AfterUpdate(camera);
    ReleaseSRWLockExclusive(&g_lock);
    if(g_vrStopRequested.exchange(false)) {
        StopVr("hotkey");
        edf6vr::g_openxr.Stop();
        // Asked for by hand, so leave it off rather than turning it straight
        // back on underneath them.
        g_vrUserStopped=true;
    }
    if(g_vrStartRequested.exchange(false)) { g_vrUserStopped=false; StartVr(); }
    ApplyNameplateSizeForVr();
    if(g_vrAutoStart && !g_vrEnabled && !g_vrUserStopped && !g_faulted && g_vrReady
       && g_calls>10 && GetTickCount64()-g_loaded>5000 && g_autoStartAttempts<40
       && !VrStartHopeless()) {
        const auto now=GetTickCount64();
        // SteamVR may not be up yet, so keep trying for a couple of minutes
        // rather than giving up on the first refusal.
        if(now-g_autoStartTried>=3000) {
            g_autoStartTried=now;
            ++g_autoStartAttempts;
            StartVr();
            if(!g_vrEnabled && g_autoStartAttempts==1)
                Log("VR: automatic start did not take; retrying every 3s. F11 still works, "
                    "and AutoStart=0 in the INI turns this off");
            else if(g_vrEnabled)
                Log("VR: started automatically after %u attempt(s)",g_autoStartAttempts);
                // The game spends the best part of a minute on logos before it
                // draws anything, and the controllers are dead for all of it.
                // Nothing on screen says when that ends, so it is said in the
                // hands instead: two short buzzes mean the controllers are live.
                edf6vr::g_openxr.Buzz(0,0.08f,0.6f);
                edf6vr::g_openxr.Buzz(1,0.08f,0.6f);
        }
    }
    // Measuring VR OFF needs the present hook before F11 is ever pressed. It
    // goes in on the first in-game frame, never during load: installing at load
    // is what stopped the game from starting in 0.4.0, while installing during
    // play has been stable since 0.4.1.
    if(g_captureRequested.exchange(false) && !edf6vr::PresentCaptureInstalled()) {
        if(!edf6vr::InstallPresentCapture(&LogRender,g_describeSwapChain))
            Log("RENDER: present capture unavailable: %s",edf6vr::PresentCaptureStatus());
    }
}
bool SkipBodyDraw(void* model,int pass) noexcept {
    __try {
        const auto& target=g_bodyTarget;
        if(!g_fpsEnabled || !g_fpsReady || !g_hideBody || g_faulted || model!=target.pose.body
           || !target.refreshed || GetTickCount64()-target.refreshed>250) return false;
        auto soldier=static_cast<unsigned char*>(model)-edf6vr::kBodyModelOffset;
        // Derive from the live model argument; do not follow cached player pointers.
        if(!edf6vr::Readable(soldier,0x928) || !edf6vr::IsSupportedSoldier(g_image,soldier)
           || !edf6vr::HasType(g_image,model,".?AVAnimationModel@@")
           || *reinterpret_cast<std::uint32_t*>(soldier+0x314)!=target.pose.objectId
           || *reinterpret_cast<void**>(soldier+0x900)!=target.pose.skeletonResource
           || *reinterpret_cast<void**>(soldier+0x910)!=target.pose.nodeArray) return false;
        return edf6vr::MarkBodyActive(model,pass);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool __fastcall HookUiGate(void* camera) {
    const bool drawing=g_uiGateOriginal?g_uiGateOriginal(camera):false;
    edf6vr::SetUiRedirect(g_vrEnabled && !g_faulted && g_uiLayer && g_uiRedirect
        && edf6vr::g_openxr.UiCaptureAvailable());
    edf6vr::NoteUiGate(drawing);
    return drawing;
}

// Put the weapon where the hand is, at the last moment before it is drawn.
//
// Only the translation row, and only three floats, so this is cheap enough to
// do on every draw call: whichever order the weapon is submitted in relative to
// the rest of the soldier, the value is fresh when it is read.
void PlaceWeaponForDraw() noexcept {
    // Retired: arms_r/model/muzzle writes were not borrowed and contaminated
    // both the next placement source and nearest-weapon selection.
}

// Which drawn model is the weapon.
//
// The weapon object holds no model, and skipping the body does not take the
// weapon with it, so the weapon has a draw call of its own. It is recognised
// from the other side instead: the game's own weapon transform says where the
// weapon is in the world, so the model that carries that position somewhere in
// its own fields is the weapon. Finding the offset as well as the model is the
// point -- that offset is how the weapon would later be moved, or drawn again
// somewhere else.
struct DrawnModel {
    void* model=nullptr;
    unsigned long long draws=0;
    unsigned vtableRva=0;
    int matchOffset=-1;
    bool inWeapon=false;
};
DrawnModel g_drawn[32]{};
int g_drawnCount=0;
bool g_drawnChanged=false;

void NoteDrawnModel(void* model) noexcept {
    if(!g_weaponSeen || !model) return;
    // The interesting models are decided before anything is recorded.
    //
    // Recording every model first and testing later filled all the slots with
    // scenery -- there is a great deal of scenery and it is drawn early -- so
    // the body never got a slot and nothing was ever reported, which read as
    // "no match" when it meant "never looked".
    const auto address=static_cast<unsigned char*>(model);
    auto within=[&](void* owner,long long span) -> long long {
        if(!owner) return -1;
        const long long delta=address-static_cast<unsigned char*>(owner);
        return (delta>=0 && delta<span)?delta:-1;
    };
    const long long inWeapon=within(g_weaponObject,0x8000);
    const long long inSoldier=within(g_soldierSeen,0x8000);
    if(inWeapon<0 && inSoldier<0) return;

    for(int i=0;i<g_drawnCount;++i) {
        if(g_drawn[i].model==model) { ++g_drawn[i].draws; return; }
    }
    if(g_drawnCount>=32) return;
    const int at=g_drawnCount++;
    g_drawn[at].model=model;
    g_drawn[at].draws=1;
    g_drawn[at].inWeapon=(inWeapon>=0);
    g_drawn[at].matchOffset=static_cast<int>(inWeapon>=0?inWeapon:inSoldier);
    g_drawnChanged=true;
    __try {
        if(edf6vr::Readable(model,8)) {
            auto vtable=*reinterpret_cast<unsigned char* const*>(model);
            if(g_image.base && vtable>g_image.base)
                g_drawn[at].vtableRva=static_cast<unsigned>(vtable-g_image.base);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void ReportDrawnModels() noexcept {
    if(!g_drawnChanged) return;
    g_drawnChanged=false;
    for(int i=0;i<g_drawnCount;++i) {
        if(g_drawn[i].matchOffset<0) continue;   // scenery, and there is a lot of it
        Log("DRAWN model=%p%s vtable=EDF.dll+%X draws=%llu inside=%s+%X",
            g_drawn[i].model,g_drawn[i].model==g_bodyTarget.pose.body?" (body)":"",
            g_drawn[i].vtableRva,g_drawn[i].draws,
            g_drawn[i].inWeapon?"weapon":"soldier",g_drawn[i].matchOffset);
    }
}

// Move every bone of the weapon sideways, in the world.
void ShiftWeaponBones(float metres) noexcept {
    unsigned char* bones=nullptr;
    unsigned long long count=0;
    float sideways[3]{};
    AcquireSRWLockShared(&g_lock);
    bones=g_weaponBones;
    count=g_weaponBoneCount;
    for(int j=0;j<3;++j) sideways[j]=g_weaponSideways[j];
    ReleaseSRWLockShared(&g_lock);
    if(!bones || !std::isfinite(metres)) return;
    __try {
        for(unsigned long long b=0;b<count;++b) {
            auto row=reinterpret_cast<float*>(bones+b*0x110+0xB0+0x30);
            for(int j=0;j<3;++j) row[j]+=sideways[j]*metres;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ++g_weaponFaults; }
}

bool ValidateHoldCommand(const WeaponHoldCommand& command,void* model) noexcept {
    __try {
        if(!model || model!=command.model || !command.refreshed
           || GetTickCount64()-command.refreshed>250 || !command.count || command.count>kMaxWeaponBones) return false;
        auto soldier=static_cast<unsigned char*>(command.soldier);
        auto registry=static_cast<unsigned char*>(model)+0xA0;
        if(!edf6vr::Readable(soldier,0x928) || !edf6vr::Readable(registry,0x28)
           || !edf6vr::IsSupportedSoldier(g_image,soldier)
           || (FencerOwnsWeaponPose(soldier) && !command.fencer)
           || *reinterpret_cast<std::uint32_t*>(soldier+0x314)!=command.objectId
           || *reinterpret_cast<void**>(soldier+0x910)!=command.bodyNodes
           || *reinterpret_cast<void**>(registry+0x10)!=command.weaponNodes
           || *reinterpret_cast<std::uint64_t*>(registry+0x20)!=command.count) return false;
        const auto bodyCount=*reinterpret_cast<std::uint64_t*>(soldier+0x920);
        const auto first=reinterpret_cast<std::uintptr_t>(command.bodyNodes);
        const auto arm=reinterpret_cast<std::uintptr_t>(command.armsNode);
        if(!first || bodyCount>1024 || arm<first || (arm-first)%0x110
           || (arm-first)/0x110>=bodyCount) return false;
        return edf6vr::Readable(static_cast<unsigned char*>(command.armsNode)+0xB0,64)
            && edf6vr::Readable(command.weaponNodes,static_cast<std::size_t>(command.count)*0x110,true);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Match 6BF9F0's LOD choice and 6C2C10's special pass-2 branch. The renderer
// uploads contiguous world matrices from registry+28 -> +8 (11009A0), NOT
// the animation nodes' +B0 matrices. The latter are already copied by then.
bool ResolveHoldPalette(void* model,int pass,edf6vr::Matrix*& palette,std::size_t& count) noexcept {
    __try {
        auto bytes=static_cast<unsigned char*>(model);
        if(pass<0 || pass>5 || pass==4 || !edf6vr::Readable(bytes,0x483)) return false;
        auto registry=bytes+0xA0;
        if(pass!=2 || !bytes[0x482]) {
            auto descriptor=bytes+0x160;
            const auto lodCount=*reinterpret_cast<std::uint64_t*>(bytes+0x260);
            if(lodCount>64) return false;
            auto lods=*reinterpret_cast<unsigned char**>(bytes+0x250);
            if(lodCount) {
                const auto index=bytes[0x45];
                if(index>7 || !edf6vr::Readable(lods,static_cast<std::size_t>(lodCount)*0xF0)) return false;
                const float distance=*reinterpret_cast<float*>(bytes+0x24+index*4);
                if(!std::isfinite(distance)) return false;
                for(std::size_t i=0;i<lodCount;++i) {
                    const float threshold=*reinterpret_cast<float*>(lods+i*0xF0+0xE8);
                    if(!std::isfinite(threshold)) return false;
                    if(threshold>distance*distance) break;
                    descriptor=lods+i*0xF0;
                }
            }
            registry=*reinterpret_cast<unsigned char**>(descriptor+0x58);
        }
        if(!edf6vr::Readable(registry,0x38)) return false;
        const auto n=*reinterpret_cast<std::int32_t*>(registry+0x20);
        if(n<=0 || n>static_cast<int>(kMaxWeaponBones)) return false;
        palette=*reinterpret_cast<edf6vr::Matrix**>(registry+0x30);
        count=static_cast<std::size_t>(n);
        return edf6vr::Readable(palette,count*sizeof(*palette),true);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The stick turn the weapon is placed with, made the one the picture is drawn
// with.
//
// The destination is worked out on the update thread with that update's yaw
// offset and drawn on the render thread against the camera of the frame being
// drawn -- the same update's, or the one before when the update thread has
// already moved on. The weapon's camera takes its origin from the command
// (SetWeaponStereoOrigin), which is why walking no longer shows, but keeps the
// native rotation. While the stick turns, the yaw offset moves a few degrees
// an update, and on the frames where the two came from different updates the
// weapon was turned about the eye by that much: a shake while turning on the
// stick, never while turning the body or the hand. The drawn camera is already
// matched back to its update (for the pose the frame is declared with,
// render_pose.cpp); the weapon is turned onto that update's yaw offset here.
// Without a turn the two are the same number and nothing moves.
void RetimeHoldYaw(WeaponHoldCommand& command) noexcept {
    g_yawRetimeDraws.fetch_add(1,std::memory_order_relaxed);
    float rendered=0;
    if(!edf6vr::NativeRenderYaw(rendered)) { g_yawRetimeUnknown.fetch_add(1,std::memory_order_relaxed); return; }
    const float delta=std::remainder(rendered-command.yawOffset,6.28318530718f);
    if(!std::isfinite(delta) || std::fabs(delta)>0.8f) { g_yawRetimeRefused.fetch_add(1,std::memory_order_relaxed); return; }
    if(delta==0) return;
    edf6vr::TurnAboutVertical(command.hand.axes,command.hand.palm,command.eyeWorld,delta);
    edf6vr::TurnAboutVertical(command.handAxes,command.handPos,command.eyeWorld,delta);
    command.yawOffset=rendered;
    g_yawRetimeTurned.fetch_add(1,std::memory_order_relaxed);
    const float degrees=std::fabs(delta)*57.29578f;
    float seen=g_yawRetimeMaxDeg.load(std::memory_order_relaxed);
    while(degrees>seen && !g_yawRetimeMaxDeg.compare_exchange_weak(seen,degrees,std::memory_order_relaxed)) {}
}

// Move the destination onto the hand as it is now, not as it was.
//
// The weapon is rigidly held, so the destination is a fixed relation to the
// tracked hand: write that relation down in the hand's own frame and it can be
// rebuilt against any later sample of the hand. Only the hand is re-read. The
// head sample stays the one the camera was placed with, because the camera has
// not moved since either -- and re-reading it here would introduce the very
// mismatch this is removing, in the one case the player reports as steady.
void LateLatchHand(WeaponHoldCommand& command) noexcept {
    if(!command.tracked || !command.latch) return;
    float now[3]{},turn[4]{};
    if(!edf6vr::g_openxr.GripPose(command.handIndex,now,turn)) return;
    const edf6vr::Quat q{turn[0],turn[1],turn[2],turn[3]};
    auto intoSoldier=[&](const edf6vr::Vec3& v) {
        return edf6vr::RotateY(edf6vr::XrToGame(edf6vr::QuatRotate(q,v)),command.yawOffset);
    };
    const edf6vr::Vec3 right=intoSoldier(edf6vr::Vec3{1,0,0});
    const edf6vr::Vec3 up=intoSoldier(edf6vr::Vec3{0,1,0});
    const edf6vr::Vec3 ahead=intoSoldier(edf6vr::Vec3{0,0,-1});
    const edf6vr::Vec3 room=edf6vr::XrToGame(edf6vr::Vec3{now[0],now[1],now[2]});
    const edf6vr::Vec3 moved=edf6vr::RotateY(
        edf6vr::Vec3{room.x-command.headRoom[0],room.y-command.headRoom[1],
                     room.z-command.headRoom[2]},command.yawOffset);
    const float palm[3]={command.eyeWorld[0]+moved.x,
                         command.eyeWorld[1]+moved.y,
                         command.eyeWorld[2]+moved.z};
    const float axes[3][3]={{right.x,right.y,right.z},{up.x,up.y,up.z},{ahead.x,ahead.y,ahead.z}};
    for(int k=0;k<3;++k) {
        float length=0;
        for(int i=0;i<3;++i) length+=axes[k][i]*axes[k][i];
        if(!(length>0.9f && length<1.1f)) return;   // not a rotation; leave it alone
    }
    // The destination, written in the frame of the hand it was measured against.
    float localAxes[3][3]{},localPalm[3]{};
    for(int k=0;k<3;++k)
        for(int j=0;j<3;++j) {
            float d=0;
            for(int i=0;i<3;++i) d+=command.hand.axes[k][i]*command.handAxes[j][i];
            localAxes[k][j]=d;
        }
    for(int j=0;j<3;++j) {
        float d=0;
        for(int i=0;i<3;++i) d+=(command.hand.palm[i]-command.handPos[i])*command.handAxes[j][i];
        localPalm[j]=d;
    }
    // The same relation, against the hand as it is now.
    for(int k=0;k<3;++k)
        for(int i=0;i<3;++i) {
            float v=0;
            for(int j=0;j<3;++j) v+=localAxes[k][j]*axes[j][i];
            command.hand.axes[k][i]=v;
        }
    for(int i=0;i<3;++i) {
        float v=palm[i];
        for(int j=0;j<3;++j) v+=localPalm[j]*axes[j][i];
        command.hand.palm[i]=v;
    }
}

// Move the anchor to where the soldier is now, not where he was.
//
// Only his root is re-read, and only the difference is used, so an unmoving
// soldier changes nothing and a mistaken read cannot displace the weapon by
// more than a plausible step. WeaponCatchUpBody=0 in the INI puts it back the
// way 0.37.1 had it without a rebuild.
// Every place inside the weapon object that looks like a position.
//
// The bullet leaves from the body, not from the weapon, and the flash proved
// the object's transform is not what the firing code reads. Rather than guess
// at another offset and spend a test on it, this reads the object once every
// few seconds and reports each float triple sitting within a few metres of the
// soldier. A field that tracks him is a candidate for the shot origin; a field
// that does not is not. Read only -- it writes nothing and decides nothing.
// Where the game is stuck, read rather than guessed.
//
// Changing anti-aliasing or resolution hangs the game: its own five-second
// block stops, then its input reads stop dead and never move again, while this
// watchdog thread carries on writing. Four guesses at the cause have all been
// wrong, and each cost a run on real hardware. A hang is not something to guess
// at -- the thread is still there, holding whatever it is stuck in, and it can
// simply be asked.
//
// The thread is suspended only long enough to copy its instruction pointer and
// a page of its stack. Nothing is resolved while it is stopped: a thread caught
// inside D3D may hold the loader lock, and asking about modules would take that
// same lock and hang this thread too. So the numbers come out first, the thread
// goes back, and only then are they turned into module names.
struct HangShot {
    DWORD thread=0;
    unsigned long long rip=0;
    unsigned long long words[48]{};
    int count=0;
};

bool CaptureThread(DWORD thread,HangShot& shot) noexcept {
    if(!thread) return false;
    HANDLE handle=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,thread);
    if(!handle) return false;
    bool taken=false;
    if(SuspendThread(handle)!=static_cast<DWORD>(-1)) {
        CONTEXT context{};
        context.ContextFlags=CONTEXT_CONTROL;
        if(GetThreadContext(handle,&context)) {
            shot.thread=thread;
            shot.rip=context.Rip;
            auto stack=reinterpret_cast<const unsigned long long*>(context.Rsp);
            if(edf6vr::Readable(stack,sizeof(shot.words))) {
                std::memcpy(shot.words,stack,sizeof(shot.words));
                shot.count=static_cast<int>(sizeof(shot.words)/sizeof(shot.words[0]));
            }
            taken=true;
        }
        ResumeThread(handle);
    }
    CloseHandle(handle);
    return taken;
}

// Turned into names only once everything is running again.
void ReportShot(const char* what,const HangShot& shot) noexcept {
    auto name=[&](unsigned long long address,char* out,std::size_t size) {
        out[0]=0;
        if(!address) return false;
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info))) return false;
        if(info.State!=MEM_COMMIT || !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ
                                                    |PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))
            return false;
        wchar_t path[MAX_PATH]{};
        auto module=static_cast<HMODULE>(info.AllocationBase);
        if(!module || !GetModuleFileNameW(module,path,MAX_PATH)) return false;
        const wchar_t* leaf=std::wcsrchr(path,L'\\');
        leaf=leaf?leaf+1:path;
        char narrow[64]{};
        WideCharToMultiByte(CP_UTF8,0,leaf,-1,narrow,sizeof(narrow)-1,nullptr,nullptr);
        std::snprintf(out,size,"%s+%llX",narrow,
                      address-reinterpret_cast<unsigned long long>(module));
        return true;
    };
    char at[96]{};
    name(shot.rip,at,sizeof(at));
    char line[512];
    int used=std::snprintf(line,sizeof(line),"HANG %s tid=%lu at=%s stack:",
                           what,shot.thread,at[0]?at:"?");
    int found=0;
    for(int i=0;i<shot.count && found<8;++i) {
        char frame[96]{};
        if(!name(shot.words[i],frame,sizeof(frame))) continue;
        used+=std::snprintf(line+used,sizeof(line)-used," %s",frame);
        ++found;
    }
    if(!found) std::snprintf(line+used,sizeof(line)-used," none readable");
    Log("%s",line);
}

// Once the game has clearly stopped, and only a few times, so a slow load does
// not fill the log with false alarms.
void WatchForHang() noexcept {
    static ULONGLONG lastCalls=0;
    static int still=0;
    static int taken=0;
    if(!g_frameLoopThread) return;
    // Loading a mission stops the update thread for longer than six seconds,
    // quite legitimately, and the three shots were being spent on that before
    // the freeze worth looking at ever arrived. The budget is per stall now: it
    // comes back as soon as the game does.
    if(g_calls!=lastCalls) { lastCalls=g_calls; still=0; taken=0; return; }
    if(taken>=3) return;
    if(g_calls==0) return;
    if(++still<3) return;   // three of this thread's own turns, about six seconds
    still=0;
    ++taken;
    HangShot update{},draw{};
    const bool haveUpdate=CaptureThread(g_frameLoopThread,update);
    DWORD drawThread=0;
    AcquireSRWLockShared(&g_holdStatsLock);
    drawThread=g_holdStats.drawThread;
    ReleaseSRWLockShared(&g_holdStatsLock);
    const bool haveDraw=drawThread && drawThread!=g_frameLoopThread
                        && CaptureThread(drawThread,draw);
    Log("HANG the game has not updated for six seconds; looking at where it is");
    if(haveUpdate) ReportShot("update",update);
    if(haveDraw) ReportShot("draw",draw);
    if(!haveUpdate && !haveDraw) Log("HANG could not read either thread");
}

// Which bone the bullet is coming out of.
//
// The shot leaves from exactly where the model holds the gun: the shoulder with
// a launcher, the waist with a rifle. That is a bone of the soldier, and the
// only thing missing is its name. arms_r is ruled out -- written every tick,
// read back unchanged a tick later, and the bullet never moved. So the skeleton
// is asked by name, with the same lookup that finds arms_r, and the heights are
// printed next to the soldier's own feet. Whichever one sits where the bullet
// appears is the one to move next. Read only; nothing here writes.
void ReportBones(void* soldier) noexcept {
    if(!soldier || !g_nodeLookup) return;
    // The soldier's own skeleton, from the game's data rather than from English
    // guesses: sgott's AIARMYSOLDIER_AF names every bone, in romanised Japanese.
    // Two lines, because one ran out of room before reaching the three names
    // that are not anatomy -- which are the ones worth seeing.
    static const wchar_t* const anatomy[]={
        L"koshi",L"hara0",L"hara1",L"hara2",L"mune",L"kubi",L"head",
        L"kata_r",L"jowan2_r",L"kawan2_r",L"te_r",L"arms_r",
    };
    static const wchar_t* const rest[]={
        L"aim_center",L"aim_target",L"weapon",L"kata_l",L"te_l",L"arms_l",
    };
    auto report=[&](const char* what,const wchar_t* const* names,std::size_t count) {
        char line[512];
        int used=std::snprintf(line,sizeof(line),"BONES %s feet=%.2f eye=%.2f |",
                               what,g_lastAim.position[1],g_eyeWorld[1]);
        int found=0;
        for(std::size_t i=0;i<count;++i) {
            float world[3]{};
            if(!edf6vr::ReadNamedBone(soldier,g_nodeLookup,names[i],world)) continue;
            char narrow[24]{};
            WideCharToMultiByte(CP_UTF8,0,names[i],-1,narrow,sizeof(narrow)-1,nullptr,nullptr);
            used+=std::snprintf(line+used,sizeof(line)-used," %s=%.2f(%.1f,%.1f)",
                                narrow,world[1]-g_lastAim.position[1],world[0],world[2]);
            ++found;
            if(used>static_cast<int>(sizeof(line))-56) break;
        }
        if(!found) std::snprintf(line+used,sizeof(line)-used," none resolved");
        Log("%s",line);
    };
    report("body",anatomy,sizeof(anatomy)/sizeof(anatomy[0]));
    report("aim ",rest,sizeof(rest)/sizeof(rest[0]));
}

void ReportPositions(const char* what,void* object,std::size_t span,
                     const float about[3],float within) noexcept {
    if(!object) return;
    __try {
        const std::size_t kSpan=span;
        if(!edf6vr::Readable(object,kSpan)) return;
        auto words=reinterpret_cast<const float*>(object);
        char line[512];
        int used=std::snprintf(line,sizeof(line),"FIREPOINT %s=%p:",what,object);
        int found=0;
        for(std::size_t i=0;i+2<kSpan/sizeof(float) && found<8;++i) {
            float away=0;
            bool sane=true;
            for(int j=0;j<3;++j) {
                const float v=words[i+j];
                if(!std::isfinite(v) || v==0) { sane=false; break; }
                const float d=v-about[j];
                away+=d*d;
            }
            if(!sane || !(away<within*within)) continue;
            used+=std::snprintf(line+used,sizeof(line)-used," +%llX=(%.2f,%.2f,%.2f)",
                                static_cast<unsigned long long>(i*sizeof(float)),
                                words[i],words[i+1],words[i+2]);
            ++found;
            i+=2;
        }
        if(!found) std::snprintf(line+used,sizeof(line)-used," none");
        Log("%s",line);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Whether the palette the bones are carried from and the animation node root
// the update thread can reach are the same frame.
//
// Read-only, and the reason it is worth the few flops: the laser origin is
// carried from the node root on the update thread, while the model is carried
// from this palette. If the two disagree, the origin is carried out of a frame
// the weapon is not in, and the log says so without another test run.
void MeasureRootAgreement(const WeaponHoldCommand& command,const edf6vr::Matrix& arm) noexcept {
    __try {
        if(!command.weaponNodes) return;
        const auto node=*reinterpret_cast<const edf6vr::Matrix*>(
            static_cast<const unsigned char*>(command.weaponNodes)+0xB0);
        float gap=0,turn=0;
        for(int j=0;j<3;++j) {
            const float d=arm.m[3][j]-node.m[3][j];
            gap+=d*d;
        }
        gap=std::sqrt(gap);
        for(int k=0;k<3;++k) {
            float dot=0,left=0,right=0;
            for(int j=0;j<3;++j) {
                dot+=arm.m[k][j]*node.m[k][j];
                left+=arm.m[k][j]*arm.m[k][j];
                right+=node.m[k][j]*node.m[k][j];
            }
            if(!(left>1e-8f) || !(right>1e-8f)) return;
            float cosine=dot/std::sqrt(left*right);
            cosine=cosine>1?1:(cosine<-1?-1:cosine);
            const float angle=std::acos(cosine)*180.0f/3.14159265f;
            if(angle>turn) turn=angle;
        }
        if(!std::isfinite(gap) || !std::isfinite(turn)) return;
        AcquireSRWLockExclusive(&g_holdStatsLock);
        if(gap>g_holdStats.rootGapMax) g_holdStats.rootGapMax=gap;
        if(turn>g_holdStats.rootTurnMax) g_holdStats.rootTurnMax=turn;
        ReleaseSRWLockExclusive(&g_holdStatsLock);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// How far the weapon sits from the eye, at the moment it is drawn.
void MeasureReach(const WeaponHoldCommand& command) noexcept {
    if(!command.tracked) return;
    float moved=0;
    for(int j=0;j<3;++j) moved+=command.step[j]*command.step[j];
    moved=std::sqrt(moved);
    float reach=0;
    for(int j=0;j<3;++j) {
        const float d=command.hand.palm[j]-command.eyeWorld[j];
        reach+=d*d;
    }
    reach=std::sqrt(reach);
    if(!std::isfinite(reach)) return;
    // Split by whether he is walking, so the two can be compared without the
    // player having to hold still on command. Both ranges include whatever the
    // hand was doing, so neither is meaningful alone -- but the hand does not
    // know whether the soldier is walking, so a range that is much wider while
    // walking is the fault and not the hand.
    AcquireSRWLockExclusive(&g_holdStatsLock);
    auto& band=(moved>0.02f)?g_holdStats.walking:g_holdStats.standing;
    band.low=std::min(band.low,reach);
    band.high=std::max(band.high,reach);
    if(std::isfinite(moved)) g_holdStats.stepMax=std::max(g_holdStats.stepMax,moved);
    ReleaseSRWLockExclusive(&g_holdStatsLock);
}

thread_local bool g_insideFencerStereo=false;
bool DrawFencerStereo(void* model,void* renderContext,int pass,void* view) {
    if(model && (model==g_fencerLeftModel.load(std::memory_order_relaxed) || model==g_fencerRightModel.load(std::memory_order_relaxed)))
        return false; // the hands pose them (fencer_dual.h)
    if(!model || (model!=g_fencerStereoModels[0].load(std::memory_order_acquire)
        && model!=g_fencerStereoModels[1].load(std::memory_order_acquire))) return false;
    if(!g_modelOriginal || !renderContext || (pass!=0 && pass!=1)) return false;
    AcquireSRWLockShared(&g_lock);
    const bool eligible=g_weaponStereoLive && g_vrEnabled && g_fpsEnabled && !g_faulted
        && g_stereoMode>=4 && !g_swapEyes && g_lastIpd>.03f && g_lastIpd<.09f
        && g_fencerStereo.soldier==g_vrSoldier && ValidateFencerStereo(g_fencerStereo,model);
    const float half=g_lastIpd*g_ipdScale*.5f;
    const float sourceOffset=edf6vr::NativeWorldEnabled()?-g_lastEyeOffset:g_lastEyeOffset;
    ReleaseSRWLockShared(&g_lock);
    if(!eligible || !edf6vr::WeaponStereoLive() || !edf6vr::Readable(renderContext,16)) return false;
    if(edf6vr::NativeWorldRenderEye()==1 && edf6vr::WeaponStereoPairReady()) return true;
    if(g_insideFencerStereo) { g_modelOriginal(model,renderContext,pass,view); return true; }
    auto ctx=*reinterpret_cast<ID3D11DeviceContext**>(static_cast<unsigned char*>(renderContext)+8);
    if(!ctx) return false;
    edf6vr::FrameCapture frame{};
    if(!edf6vr::ReadFrameCapture(frame)) return false;
    bool isolated=false,captured=false;
    g_insideFencerStereo=true;
    __try {
        // Actual draw camera + relative IPD. Do not re-anchor to the camera
        // update thread: these native vertices belong to this render frame.
        edf6vr::SetWeaponStereoNativeCamera(-half-sourceOffset,half-sourceOffset);
        if(pass==0) {
            isolated=edf6vr::BeginWeaponDepthIsolation(ctx);
            if(isolated) ++g_fencerStereoDepths;
        } else {
            captured=edf6vr::BeginWeaponStereoCapture(ctx,pass,frame.width,frame.height,true);
            if(captured) ++g_fencerStereoBatches;
        }
        g_modelOriginal(model,renderContext,pass,view);
    } __finally {
        if(captured) edf6vr::EndWeaponStereoCapture();
        if(isolated) edf6vr::EndWeaponDepthIsolation();
        g_insideFencerStereo=false;
    }
    return true;
}
bool DrawHeldWeapon(void* model,void* renderContext,int pass,void* view) {
    WeaponHoldCommand command{};
    AcquireSRWLockShared(&g_lock);
    command=model==g_leftHoldCommand.model?g_leftHoldCommand:g_holdCommand;
    const bool liveCamera=g_weaponStereoLive && g_vrEnabled && g_stereoMode>=4 && !g_swapEyes
        && g_lastIpd>.03f && g_lastIpd<.09f;
    const float eyeHalf=g_lastIpd*g_ipdScale*.5f,sourceOffset=g_lastEyeOffset;
    ReleaseSRWLockShared(&g_lock);
    if(model!=command.model) return false;
    if(command.handIndex==0 && !g_dualActive.load() && !g_fencerActive.load()) return false;
    if(g_firingPresentationTrace) {
        g_heldDrawStamp.store(GetTickCount64(),std::memory_order_relaxed);
        g_heldDrawTraceCount.fetch_add(1,std::memory_order_relaxed);
        if(pass>=0 && pass<32) g_heldPassTraceMask.fetch_or(1u<<pass,std::memory_order_relaxed);
    }
    // Both private weapon eyes were completed during the first world pass.
    if((pass==0 || pass==1) && liveCamera && edf6vr::NativeWorldRenderEye()==1
       && edf6vr::WeaponStereoPairReady()) return true;
    // Reentrant draws consume the existing borrowed pose; never transform it
    // twice and never recursively acquire the non-recursive SRW lock.
    if(g_insideHoldBorrow) {
        AcquireSRWLockExclusive(&g_holdStatsLock); ++g_holdStats.nested; ReleaseSRWLockExclusive(&g_holdStatsLock);
        g_modelOriginal(model,renderContext,pass,view);
        return true;
    }
    AcquireSRWLockExclusive(&g_holdBorrowLock);
    g_insideHoldBorrow=true;
    edf6vr::Matrix kept[kMaxWeaponBones]{},wanted[kMaxWeaponBones]{};
    unsigned touched=0;
    bool prepared=false,layerCapture=false,stereoCapture=false,depthIsolation=false;
    float sourceLever=0,wantedLever=0;
    edf6vr::Matrix* palette=nullptr;
    std::size_t paletteCount=0;
    __try {
        __try {
            if(ValidateHoldCommand(command,model) && ResolveHoldPalette(model,pass,palette,paletteCount)) {
                RetimeHoldYaw(command);
                LateLatchHand(command);
                ApplyRecoilToHand(command.handIndex,command.hand.axes,command.hand.palm);
                MeasureReach(command);
                for(std::size_t b=0;b<paletteCount;++b) kept[b]=palette[b];
                // The frame the weapon is in, taken from the very matrices about
                // to be drawn rather than from the animation node beside them.
                //
                // arms_r and this palette are two copies of the same skeleton
                // sampled at different moments; the measured distance between
                // the arm and the weapon, which cannot change on a weapon held
                // in an arm, ranged over 40cm. That gap is widest while walking,
                // which is when the blur was reported. A source taken from the
                // palette has nothing left to be out of step with.
                const edf6vr::Matrix arm=kept[0];
                MeasureRootAgreement(command,arm);
                // The muzzle rides along, so the flash keeps the place on the
                // weapon the game gave it.
                float muzzle[3]{};
                // A back mount's barrel straightened on its root first
                // (FencerStraightenJoint): the jump and the dash swing it
                // through the Fencer's own bone, which the root does not see.
                edf6vr::Matrix straight[kMaxWeaponBones]{};
                const edf6vr::Matrix* source=kept;
                if(command.fencer && FencerStraightenJoint(command,kept,paletteCount,straight)) source=straight;
                prepared=edf6vr::CarryWeaponBones(arm,command.hand,source,paletteCount,
                                                wanted,sourceLever,wantedLever);
                if(prepared) {
                    const bool dual=RangerDualWeapon(command.weapon);
                    if(dual)++g_dualDraws[command.handIndex];
                    if(command.fencer) {
                        const float* a=kept[0].m[2]; const float* b=wanted[0].m[2];
                        const float la=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]), lb=std::sqrt(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);
                        if(la>1e-4f && lb>1e-4f) {
                            const float c=std::clamp((a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/(la*lb),-1.0f,1.0f);
                            g_fencerDrawTurnDeg.store(std::acos(c)*57.29578f,std::memory_order_relaxed);
                        }
                    }
                    if(!dual && command.muzzleLocalValid && edf6vr::PlaceWeaponLocalPoint(command.hand,command.muzzleLocal,muzzle))
                        PublishMuzzleFrame(command.weapon,command.soldier,command.objectId,muzzle,command.rootWorld,command.refreshed,
                                           command.fencer?command.handIndex:1u);
                    PublishCasingFrame(command,&arm,&wanted[0]);
                }
                if(prepared) for(std::size_t b=0;b<paletteCount;++b) {
                    touched=static_cast<unsigned>(b)+1; // partial writes are restored too
                    palette[b]=wanted[b];
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { prepared=false; }
        // On a partial write failure, restore before forwarding the stock draw.
        if(!prepared) for(unsigned b=0;b<touched;++b) {
            __try { palette[b]=kept[b]; }
            __except(EXCEPTION_EXECUTE_HANDLER) {
                AcquireSRWLockExclusive(&g_holdStatsLock); ++g_holdStats.restoreFaults; ReleaseSRWLockExclusive(&g_holdStatsLock);
            }
        }
        if(!prepared) touched=0;
        AcquireSRWLockExclusive(&g_holdStatsLock);
        if(prepared) {
            if(g_firingPresentationTrace) g_heldPreparedStamp.store(GetTickCount64(),std::memory_order_relaxed);
            ++g_weaponBoneWrites;
            ++g_holdStats.applied;
            g_holdStats.palette=palette;
            g_holdStats.passMask|=1u<<pass;
            float displacement=0;
            for(int j=0;j<3;++j) {
                const float d=wanted[0].m[3][j]-kept[0].m[3][j]; displacement+=d*d;
            }
            g_holdStats.displacementMax=std::max(g_holdStats.displacementMax,std::sqrt(displacement));
            g_holdStats.sourceMin=std::min(g_holdStats.sourceMin,sourceLever);
            g_holdStats.sourceMax=std::max(g_holdStats.sourceMax,sourceLever);
            g_holdStats.targetMin=std::min(g_holdStats.targetMin,wantedLever);
            g_holdStats.targetMax=std::max(g_holdStats.targetMax,wantedLever);
            g_holdStats.errorMax=std::max(g_holdStats.errorMax,std::fabs(sourceLever-wantedLever));
        } else {++g_holdStats.rejected;if(g_firingPresentationTrace) ++g_heldRejectTraceCount;}
        g_holdStats.updateThread=command.updateThread;
        g_holdStats.drawThread=GetCurrentThreadId();
        ReleaseSRWLockExclusive(&g_holdStatsLock);
        if(prepared && renderContext && (!g_weaponStereoLive || liveCamera)
           && (edf6vr::WeaponLayerCaptureWanted() || edf6vr::WeaponStereoCaptureWanted(true))
           && edf6vr::Readable(renderContext,16)) {
            const auto drawContext=*reinterpret_cast<ID3D11DeviceContext**>(static_cast<unsigned char*>(renderContext)+8);
            edf6vr::FrameCapture frame{};
            if(edf6vr::ReadFrameCapture(frame)) {
                if(liveCamera) {
                    edf6vr::SetWeaponStereoEyes(-eyeHalf,eyeHalf);
                    edf6vr::SetWeaponStereoOrigin(command.eyeWorld,sourceOffset);
                    if(pass==0) depthIsolation=edf6vr::BeginWeaponDepthIsolation(drawContext);
                }
                layerCapture=edf6vr::BeginWeaponLayerCapture(drawContext,pass,frame.width,frame.height);
                // Both tracked weapons join the SAME pair before native lighting.
                // The second model must also pass the AwaitLighting gate above.
                if(!layerCapture) stereoCapture=edf6vr::BeginWeaponStereoCapture(drawContext,pass,frame.width,frame.height,true);
            }
        }
        const bool radio=model==g_radioModel.load(std::memory_order_relaxed);
        const auto replaysBefore=radio?edf6vr::WeaponStereoReplays():0;
        if(radio && pass>=0 && pass<4) {
            g_radioPassCalls[pass].fetch_add(1,std::memory_order_relaxed);
            edf6vr::SetWeaponSiteProbe(pass);
        }
        g_modelOriginal(model,renderContext,pass,view);
        if(radio) edf6vr::SetWeaponSiteProbe(-1);
        if(radio) {
            if(prepared) g_radioPrepared.fetch_add(1,std::memory_order_relaxed);
            if(layerCapture || stereoCapture) g_radioCaptured.fetch_add(1,std::memory_order_relaxed);
            g_radioReplayed.fetch_add(edf6vr::WeaponStereoReplays()-replaysBefore,std::memory_order_relaxed);
        }
    } __finally {
        if(layerCapture) edf6vr::EndWeaponLayerCapture();
        if(stereoCapture) edf6vr::EndWeaponStereoCapture();
        if(depthIsolation) edf6vr::EndWeaponDepthIsolation();
        edf6vr::SetWeaponSiteProbe(-1);
        // Includes exceptions raised by the original draw; do not swallow the
        // game's exception, but never leave our pose in the source skeleton.
        for(unsigned b=0;b<touched;++b) {
            __try {
                auto row=palette+b;
                if(std::memcmp(row,&wanted[b],sizeof(*row))) {
                    AcquireSRWLockExclusive(&g_holdStatsLock); ++g_holdStats.changed; ReleaseSRWLockExclusive(&g_holdStatsLock);
                }
                *row=kept[b];
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                AcquireSRWLockExclusive(&g_holdStatsLock); ++g_holdStats.restoreFaults; ReleaseSRWLockExclusive(&g_holdStatsLock);
            }
        }
        g_insideHoldBorrow=false;
        ReleaseSRWLockExclusive(&g_holdBorrowLock);
    }
    return true;
}

// Read-only unwind, at most two samples per pass bucket (18 lines per process).
// Identify the native renderer's parents before attempting world stereo reentry.
// No repeated rendering, frame waits, or writes to the model/context/view.
#include "native_renderer_probe.h"
#include "world_distance.h"
bool AllowNativeWorldPair() noexcept {return edf6vr::NativeWorldLive() && edf6vr::NativeWorldCompositeAvailable();}
void BeginNativeWorldProducer(std::uint64_t,unsigned eye) noexcept {
    if(!eye) g_nativeProducerIpd=edf6vr::NativeWorldSeparation();
}
void EndNativeWorldProducer(std::uint64_t frame,unsigned,void* scene) noexcept {
    if(edf6vr::NativeWorldFrameValid(frame) && !edf6vr::EnqueueNativeWorldComposite(scene))
        edf6vr::SetNativeWorldQueueEnabled(false); // invalidate, never capture an uncomposited backbuffer
}
void BeginNativeWorldQueue(std::uint64_t frame,unsigned eye,void*) noexcept {
    edf6vr::BeginNativeWorldEye(frame,eye);
    if(!edf6vr::NativeWorldFrameValid(frame)) edf6vr::CancelNativeWorldEye(frame);
}
void EndNativeWorldQueue(std::uint64_t frame,unsigned eye,void* renderContext) noexcept {
    if(!edf6vr::NativeWorldFrameValid(frame)) {edf6vr::CancelNativeWorldEye(frame);return;}
    // Producer end queued the game's 705B10 world-to-screen conversion BEFORE
    // this marker. The backbuffer now contains this eye, including native color
    // conversion. HUD is queued separately after both world submissions.
    ID3D11DeviceContext* context=nullptr;
    if(renderContext) ReadUmbraProbeData(static_cast<unsigned char*>(renderContext)+8,&context,sizeof(context));
    edf6vr::FrameCapture captured{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer;
    if(edf6vr::ReadFrameCapture(captured) && captured.swapChain)
        static_cast<IDXGISwapChain*>(captured.swapChain)->GetBuffer(0,IID_PPV_ARGS(&buffer));
    edf6vr::EndNativeWorldEye(frame,eye,context,buffer.Get());
}
void ProbeNativeRenderStack(void* model,void* renderContext,int pass,void* view) noexcept {
    if(!g_nativeStereoProbeActive.load(std::memory_order_relaxed)) return;
    static std::atomic<unsigned> samples[9]{};
    const unsigned bucket=pass>=0 && pass<8?static_cast<unsigned>(pass):8u;
    auto count=samples[bucket].load(std::memory_order_relaxed);
    do { if(count>=2) return; }
    while(!samples[bucket].compare_exchange_weak(count,count+1,std::memory_order_relaxed));
    void* frames[32]{};
    const auto total=CaptureStackBackTrace(0,32,frames,nullptr);
    char chain[768]{}; std::size_t used=0;
    const auto base=reinterpret_cast<std::uintptr_t>(g_image.base);
    for(unsigned i=0;i<total && used+32<sizeof(chain);++i) {
        const auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
        if(address>=base && address-base<edf6vr::kImageSize) {
            const int n=std::snprintf(chain+used,sizeof(chain)-used," %u:%llX",i,
                static_cast<unsigned long long>(address-base));
            if(n>0) used+=static_cast<std::size_t>(n);
        }
    }
    Log("NATIVESTACK thread=%lu pass=%d sample=%u model=%p context=%p view=%p frames=%u EDF-return-RVAs:%s",
        GetCurrentThreadId(),pass,count+1,model,renderContext,view,total,chain);
}
bool DrawCockpitVehicle(void* model,void* renderContext,int pass,void* view) {
    // The native pair supplies its matched immutable cockpit, not the newest
    // update-thread pose. Identity is checked from this live model argument.
    if(const auto* cockpit=edf6vr::NativeWorldCockpit()) {
        if(edf6vr::ValidateCockpitModel(g_image,model,cockpit->rig)) {
            edf6vr::CockpitLimbScope scope(cockpit->rig);
            g_modelOriginal(model,renderContext,pass,view);return true;
        }
    }
    return false;
}
// The mission truck being ridden: drawn as the game draws it, less its
// windows' glass (TruckGlassScope). Apart: HookModelDraw has a __try.
bool DrawTruckWithoutGlass(void* model,void* renderContext,int pass,void* view) {
    if(!model||model!=g_truckGlassModel.load(std::memory_order_acquire)
       ||GetTickCount64()-g_truckGlassAt.load(std::memory_order_relaxed)>=500) return false;
    edf6vr::TruckGlassScope scope;
    g_modelOriginal(model,renderContext,pass,view);return true;
}
void __fastcall HookModelDraw(void* model,void* renderContext,int pass,void* view) {
    if(VehicleRecoilDraw(model,renderContext,pass,view))return;
    if(DrawCockpitVehicle(model,renderContext,pass,view))return;
    ObserveFencerDraw(model);
    ++g_drawCalls;
    ProbeNativeRenderStack(model,renderContext,pass,view);
    if(DrawFencerStereo(model,renderContext,pass,view)) return;
    if(g_findWeaponModel) NoteDrawnModel(model);
    // Leave the weapon out of the world, on request.
    //
    // Moving it has failed from every angle -- the muzzle transform, the bone it
    // hangs on, and three places inside its own model -- which says the matrix
    // the mesh is drawn with is settled before this call. Whether it can be left
    // out is a different question with a clear answer, and it is the one the
    // layer plan actually needs: a weapon that can be withheld here is a weapon
    // that can be drawn again elsewhere, per eye, in a pass of our own.
    if(g_weaponHide && model && model==g_weaponModelSeen) { ++g_weaponHidden; return; }
    if(model && model==g_radioModel.load(std::memory_order_relaxed)) {
        g_radioDraws.fetch_add(1,std::memory_order_relaxed);
        if(pass>=0 && pass<32) g_radioPasses.fetch_or(1u<<pass,std::memory_order_relaxed);
        if(model==g_fastDrawWeapon.load(std::memory_order_relaxed)) g_radioFast.fetch_add(1,std::memory_order_relaxed);
    }
    if(g_weaponFollowsHand && model && g_modelOriginal
       && (model==g_fastDrawWeapon.load(std::memory_order_acquire) || model==g_fastDrawLeft.load(std::memory_order_acquire))
       && DrawHeldWeapon(model,renderContext,pass,view)) return;
    if(g_weaponDouble && model && model==g_weaponModelSeen && g_modelOriginal) {
        PlaceWeaponForDraw();
        g_modelOriginal(model,renderContext,pass,view);
        // The second call was made -- the counter climbed into the thousands --
        // and drew nothing. The model keeps a mark at +0x4D0 that the draw sets
        // when it has submitted; MarkBodyActive already writes it, to keep the
        // skeleton alive while the body mesh is withheld. So the mark is put
        // back to what it was before the draw, and the draw asked again.
        unsigned char mark=0; float marked=0;
        bool restore=false;
        __try {
            if(edf6vr::Readable(static_cast<unsigned char*>(model)+0x4D0,8,true)) {
                auto flag=static_cast<unsigned char*>(model)+0x4D0;
                mark=*flag;
                marked=*reinterpret_cast<float*>(flag+4);
                *flag=static_cast<unsigned char>(g_weaponDoubleMark);
                *reinterpret_cast<float*>(flag+4)=0;
                restore=true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { restore=false; }
        ShiftWeaponBones(g_weaponDoubleShift);
        g_modelOriginal(model,renderContext,pass,view);
        ShiftWeaponBones(-g_weaponDoubleShift);
        if(restore) __try {
            auto flag=static_cast<unsigned char*>(model)+0x4D0;
            *flag=mark;
            *reinterpret_cast<float*>(flag+4)=marked;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        ++g_weaponDoubles;
        return;
    }
    g_drawThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    edf6vr::NoteModelDraw();
    const bool bodyCandidate=model && model==g_fastDrawBody.load(std::memory_order_acquire);
    if(bodyCandidate) {
        g_bodyDraw=GetTickCount64();
        // Gating this on the movement basis was worse: it needs thirty samples of
        // the soldier answering movement input and a short sortie never solved
        // it, so the mission stayed on the board. Back to the body being drawn,
        // which at least gets the pause menu and the walk back right.
        edf6vr::g_openxr.MarkSceneLive();
    }
    bool skip=false,hands=true;
    if(bodyCandidate) {
        AcquireSRWLockShared(&g_lock);
        skip=SkipBodyDraw(model,pass);hands=!g_bodyTarget.handsOff;
        ReleaseSRWLockShared(&g_lock);
    }
    if(skip) {
        // The body stays out of the picture, but its hands go in, at the
        // controllers. False means nothing was drawn, as before.
        if(hands&&DrawBodyHands(model,renderContext,pass,view)) return;
        ++g_bodySkipped; return;
    }
    if(DrawTruckWithoutGlass(model,renderContext,pass,view)) return;
    g_modelOriginal(model,renderContext,pass,view);
}
}

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    if(!info) return false;
    QueryPerformanceFrequency(&g_frequency);
    Settings();
    g_cockpitRequested=GetPrivateProfileIntW(L"VR",L"VehicleCockpit",1,g_iniPath)!=0;
    ReadHandSettings();
    g_vehicleLevelView=GetPrivateProfileIntW(L"VR",L"VehicleLevelView",1,g_iniPath)!=0;
    g_devKeys=GetPrivateProfileIntW(L"Diagnostics",L"DevKeys",0,g_iniPath)!=0;
    // Before the first line of this run is written.
    TrimLog(GetPrivateProfileIntW(L"Diagnostics",L"LogSessions",10,g_iniPath));
    Log("%s",kSessionMark);
    if(g_iniMergeRan)
        Log("INI %s", g_iniMerge.failed?"defaults could not be applied (read-only or missing resource); code defaults used for missing keys"
            :(g_iniMerge.created?"created from the shipped defaults":(g_iniMerge.added?"updated: new settings added with their defaults, your values kept":"up to date")));
    if(g_iniMerge.added) Log("INI added %u new key(s)",g_iniMerge.added);
    if(g_iniMerge.sections) Log("INI added %u new section(s) whole, where the shipped file has them",g_iniMerge.sections);
    if(g_iniReset.reset)
        Log("INI replaced with the defaults of settings revision %d (the file was revision %d); the old one is "
            "EDF6VR.ini.v%d.bak%s",kSettingsRevision,g_iniReset.from,g_iniReset.from<1?1:g_iniReset.from,
            g_iniReset.keptResolution?"; ForceWidth/ForceHeight carried over":"");
    else if(g_iniReset.failed)
        Log("INI could not be replaced with the new defaults (no backup possible?); the old file is kept and merged");
    Log("EDF6VR 2.1.6 cockpit loading, with EDF6MultiSlot 1.5.32. Fencer weapons aim the barrel itself; no dead band on the aim.");
    wchar_t host[MAX_PATH]{}; GetModuleFileNameW(nullptr,host,MAX_PATH);
    const auto slash=wcsrchr(host,L'\\');
    if(_wcsicmp(slash?slash+1:host,L"EDF6.exe")) { Log("REFUSED: process is not EDF6.exe"); return false; }
    // The HD texture pack is loose files, and only the loader's redirector reads
    // them. The package no longer carries a ModLoader.ini, and the builder turns
    // Redirect on when it runs, but another mod's installer can still put a
    // Redirect=False back. Read the way the loader reads it: default True, and
    // only "True" is on.
    if(slash) {
        const std::wstring folder(host,slash+1);
        const auto loaderIni=folder+L"ModLoader.ini";
        wchar_t redirect[6]{};
        GetPrivateProfileStringW(L"ModLoader",L"Redirect",L"True",redirect,6,loaderIni.c_str());
        if(_wcsicmp(redirect,L"True")
           && GetFileAttributesW((folder+L"Mods\\HDTextureWork\\written.txt").c_str())!=INVALID_FILE_ATTRIBUTES)
            Log("WARNING: HD textures are built but ModLoader.ini has Redirect=%ls, so the game "
                "never loads them. Set Redirect=True, or run HD_Texture_2x.bat and answer y",redirect);
    }
    // Before anything else this plugin might load, and before the game has had
    // long to run: whatever xinput library is here now belongs to the game or to
    // Steam, not to us.
    edf6vr::ReportXInputModules("at plugin load",&LogText);
    // Before the game has asked for a single resource: what EDFModLoader offers
    // to the Mods folder. Read only, the loader's own answer is handed back
    // untouched, and it is what decides whether a texture pack can be loose DDS
    // files or has to be rebuilt archives.
    if(GetPrivateProfileIntW(L"Diagnostics",L"ResourcePathLog",0,g_iniPath)!=0)
        Log("PATHLOG installed=%d file=%lsEDF6VR.paths.txt (EDFModLoader Mods candidates)",
            edf6vr::InstallResourcePathLog(g_modDirectory)?1:0,g_modDirectory);
    char reason[256]{};
    if(!edf6vr::CheckImage(GetModuleHandleW(L"EDF.dll"),g_image,reason,sizeof(reason))) { Log("REFUSED: %s",reason); return false; }
    // Earliest the swap can go in, since it needs the image and nothing else.
    edf6vr::InstallXInputBridge(g_image,&LogText);
    Log("%s",reason);
    g_original=reinterpret_cast<Update>(g_image.original);
    info->infoVersion=PluginInfo::MaxInfoVer;
    info->name="EDF6VR Ranger / Wing Diver / Air Raider OpenXR";
    info->version=PLUG_VER(1,8,0,0);
    bool changed=false;
    const bool ok=edf6vr::ReplacePointer(g_image.updateSlot,g_image.original,reinterpret_cast<void*>(&HookUpdate),changed);
    if(!changed) { Log("REFUSED: vtable replacement failed or another plugin changed the slot"); return false; }
    // Never return false after publishing a callback: loader would unload executable code.
    if(!ok) { g_faulted=true; Log("FAULT: page protection restore failed; retaining DLL, test disabled"); }
    Log("HOOK INSTALLED EDF.dll+%X -> %p original=EDF.dll+%X. StartEnabled=%d. F6 toggle, F7 zero, F8 +15 yaw; NumLock+NumPad4/6/8/2.",
        edf6vr::kUpdateSlotRva,reinterpret_cast<void*>(&HookUpdate),edf6vr::kUpdateRva,g_enabled);
    if(!g_faulted && edf6vr::CheckFirstPersonProfile(g_image)) {
        g_modelOriginal=reinterpret_cast<ModelDraw>(g_image.base+edf6vr::kModelDrawRva);
        g_nodeLookup=reinterpret_cast<edf6vr::NodeLookup>(g_image.base+edf6vr::kNodeLookupRva);
        changed=false;
        const bool modelOK=edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+edf6vr::kModelDrawSlotRva),
            reinterpret_cast<void*>(g_modelOriginal),reinterpret_cast<void*>(&HookModelDraw),changed);
        g_fpsReady=modelOK;
        if(changed && !modelOK) g_faulted=true;
    }
    // The gate goes on separately from everything else: it is a slot on the same
    // vtable, verified the same way, and it only listens.
    if(!g_faulted && edf6vr::CheckUiGate(g_image)) {
        g_uiGateOriginal=reinterpret_cast<UiGate>(g_image.base+edf6vr::kUiGateRva);
        changed=false;
        const bool gateOK=edf6vr::ReplacePointer(
            reinterpret_cast<void**>(g_image.base+edf6vr::kUiGateSlotRva),
            reinterpret_cast<void*>(g_uiGateOriginal),reinterpret_cast<void*>(&HookUiGate),changed);
        g_uiGateReady=gateOK;
        if(changed && !gateOK) g_faulted=true;
    }
    Log("UI GATE ready=%d slot=EDF.dll+%X original=EDF.dll+%X (from EDFModLoader DisableUI pattern)",
        g_uiGateReady,edf6vr::kUiGateSlotRva,edf6vr::kUiGateRva);
    // The pad. Failing here costs the controllers and nothing else, so it is
    // never a reason to refuse the rest.
    if(!g_faulted) edf6vr::InstallXInputBridge(g_image,&LogText);
    edf6vr::AllowSweep(GetPrivateProfileIntW(L"VR",L"XInputSweep",0,g_iniPath)!=0);
    edf6vr::AllowCapabilities(GetPrivateProfileIntW(L"VR",L"XInputCapabilities",1,g_iniPath)!=0);
    edf6vr::UsePrivateSpare(GetPrivateProfileIntW(L"VR",L"XInputPrivateSpare",0,g_iniPath)!=0);
    edf6vr::WantRealPad(GetPrivateProfileIntW(L"VR",L"XInputRealPad",1,g_iniPath)!=0);
    g_startAtFirstFrame=GetPrivateProfileIntW(L"VR",L"StartAtFirstFrame",1,g_iniPath)!=0;
    g_weaponFollowsHand=GetPrivateProfileIntW(L"VR",L"WeaponFollowsHand",0,g_iniPath)!=0;
    wchar_t captureDirectory[MAX_PATH]{};
    GetModuleFileNameW(nullptr,captureDirectory,MAX_PATH);
    if(auto captureSlash=wcsrchr(captureDirectory,L'\\')) {
        captureSlash[1]=0;
        wcscat_s(captureDirectory,L"_VRDEV\\EDF6VR\\research\\runtime\\weapon_layer_0.114.5");
        const bool capture=GetPrivateProfileIntW(L"Render",L"WeaponLayerCapture",0,g_iniPath)!=0;
        g_weaponStereoLive=GetPrivateProfileIntW(L"Render",L"WeaponStereoLive",0,g_iniPath)!=0;
        const bool stereoRequested=g_weaponStereoLive || GetPrivateProfileIntW(L"Render",L"WeaponStereoCapture",0,g_iniPath)!=0;
        const bool stereo=(stereoRequested||g_cockpitRequested) && edf6vr::InstallWeaponStereoHooks(g_image);
        g_cockpitHooksReady=stereo;edf6vr::ConfigureCockpit(g_cockpitRequested&&stereo);
        Log("COCKPIT requested=%d hooks=%d Nix chest + Depth Crawler; other vehicles use existing camera",g_cockpitRequested,stereo);
        if(stereoRequested) Log("WEAPONSTEREO DrawIndexed call sites installed=%d",stereo);
        const bool lighting=(capture || stereo) && edf6vr::InstallWeaponLightingCaptureHooks(g_image);
        if(capture || stereo) Log("WEAPONLAYER lighting call sites installed=%d (material-only fallback if refused)",lighting);
        edf6vr::ConfigureWeaponLayerCapture(capture && !stereoRequested,captureDirectory,&LogText,8000,lighting);
        edf6vr::ConfigureWeaponStereoCapture(stereo,captureDirectory,&LogText,8000,lighting);
        edf6vr::ConfigureWeaponStereoLive(g_weaponStereoLive && stereo && lighting);
    }
    g_weaponMuzzleFollows=GetPrivateProfileIntW(L"VR",L"WeaponMuzzleFollows",1,g_iniPath)!=0;
    g_laserFollowsMuzzle=GetPrivateProfileIntW(L"VR",L"LaserSightFollowsMuzzle",1,g_iniPath)!=0;
    g_binocularZoom=GetPrivateProfileIntW(L"Render",L"BinocularZoom",1,g_iniPath)!=0;
    g_weaponArmBone=GetPrivateProfileIntW(L"VR",L"WeaponArmBone",0,g_iniPath)!=0;
    // Lost in 0.56.0, when the regex that deleted the logo pulse took the line
    // below it as well. The flag stayed false all session while the INI said 1,
    // so the probe never armed, the left A button went to the game as B, and a
    // whole test measured nothing. Deleting code by pattern is how that happens.
    g_shotProbe=GetPrivateProfileIntW(L"VR",L"ShotOriginProbe",0,g_iniPath)!=0;
    g_autoDeploy=GetPrivateProfileIntW(L"Test",L"AutoDeploy",0,g_iniPath)!=0;
    g_fireOriginLift=ReadFloat(g_iniPath,L"FireOriginLift",0.0f,-5.0f,5.0f,L"VR");
    g_watchBullets=GetPrivateProfileIntW(L"VR",L"WatchBullets",1,g_iniPath)!=0;
    g_shotFromMuzzle=GetPrivateProfileIntW(L"VR",L"ShotFromMuzzle",1,g_iniPath)!=0;
    g_autoFireAfter=std::clamp(
        static_cast<int>(GetPrivateProfileIntW(L"Test",L"AutoFireAfterSeconds",6,g_iniPath)),1,120);
    GetPrivateProfileStringW(L"VR",L"WatchBone",L"aim_center",g_watchBone,16,g_iniPath);
    {
        wchar_t sites[128]{};
        GetPrivateProfileStringW(L"VR",L"FireSites",L"22E9C0,231CC0,22EA4A",sites,128,g_iniPath);
        g_fireSiteCount=0;
        const wchar_t* at=sites;
        while(*at && g_fireSiteCount<kMaxFireSites) {
            wchar_t* end=nullptr;
            const unsigned long value=std::wcstoul(at,&end,16);
            if(end==at) break;
            if(value) g_fireSite[g_fireSiteCount++]=static_cast<unsigned>(value);
            at=end;
            while(*at==L',' || *at==L' ') ++at;
        }
    }
    // Poses from a file, for asking a question without a headset on.
    wchar_t script[MAX_PATH]{};
    GetPrivateProfileStringW(L"Test",L"PoseScript",L"",script,MAX_PATH,g_iniPath);
    edf6vr::ArmScriptedPoses(script);
    g_configuredWidth=GetPrivateProfileIntW(L"Render",L"ForceWidth",3840,g_iniPath);
    g_configuredHeight=GetPrivateProfileIntW(L"Render",L"ForceHeight",2160,g_iniPath);
    g_matchHeadsetFov=GetPrivateProfileIntW(L"Render",L"MatchHeadsetFov",1,g_iniPath)!=0;
    g_headsetFovMargin=ReadFloat(g_iniPath,L"HeadsetFovMarginDegrees",0.5f,0.0f,20.0f,L"Render");
    edf6vr::g_openxr.SetFovMargin(g_headsetFovMargin);
    ResolveFitPath();
    unsigned renderWidth=g_configuredWidth,renderHeight=g_configuredHeight;
    const char* fit="not fitted";
    char fitted[160]{};
    if(g_matchHeadsetFov && g_fitPath[0] && renderWidth && renderHeight) {
        // Degrees times a hundred, so the file stays a plain integer profile.
        const int storedHorizontal=GetPrivateProfileIntW(L"HeadsetFit",L"HalfHorizontalCentiDeg",0,g_fitPath);
        const int storedVertical=GetPrivateProfileIntW(L"HeadsetFit",L"HalfVerticalCentiDeg",0,g_fitPath);
        wchar_t name[128]{};
        GetPrivateProfileStringW(L"HeadsetFit",L"System",L"",name,128,g_fitPath);
        if(storedHorizontal>0 && storedVertical>0) {
            const float halfH=storedHorizontal/(100.0f*kDegrees),halfV=storedVertical/(100.0f*kDegrees);
            unsigned wantWidth=0,wantHeight=0;
            if(FittedRect(halfH,halfV,g_configuredWidth,g_configuredHeight,g_headsetFovMargin,
                          wantWidth,wantHeight)) {
                renderWidth=wantWidth; renderHeight=wantHeight;
                std::snprintf(fitted,sizeof(fitted),
                    "fitted to %.2f x %.2f deg half angles + %.2f margin, same density, from %S",
                    storedHorizontal/100.0f,storedVertical/100.0f,g_headsetFovMargin,
                    name[0]?name:L"a measured headset");
                fit=fitted;
            } else fit="measurement present but out of range; configured rectangle kept";
        } else fit="no measurement yet; configured rectangle kept, this session measures it";
    } else if(!g_matchHeadsetFov) fit="MatchHeadsetFov=0";
    g_lockedWidth=renderWidth;
    char resolutionReason[128]{};
    const bool resolutionLocked=edf6vr::InstallResolutionLock(g_image,renderWidth,renderHeight,
                                                             resolutionReason,sizeof(resolutionReason));
    Log("RESOLUTION requested=%ux%u configured=%ux%u installed=%d getter=EDF.dll+1183310 status=%s (%s)",
        renderWidth,renderHeight,g_configuredWidth,g_configuredHeight,resolutionLocked?1:0,resolutionReason,fit);
    if(resolutionLocked && renderWidth && renderHeight) {
        // The chat bubble's text is the one HUD element that scales its own
        // y by height/(width*9/16); with the fitted rectangle that put it six
        // lines under the bubble.
        char chatReason[128]{};
        const bool chatFixed=edf6vr::InstallChatTextScaleFix(g_image,chatReason,sizeof(chatReason));
        Log("RESOLUTION chat text: installed=%d site=EDF.dll+%X (%s)",chatFixed?1:0,edf6vr::kChatTextScaleRva,chatReason);
    }
    // From here, not from the in-game report: the menus are drawn long before
    // that report first runs, and they were left square until a mission had been
    // played once.
    edf6vr::g_openxr.SetContentAspect(g_configuredHeight
        ?static_cast<float>(g_configuredWidth)/static_cast<float>(g_configuredHeight):0.0f);
    g_weaponHide=GetPrivateProfileIntW(L"VR",L"WeaponHide",0,g_iniPath)!=0;
    g_weaponDouble=GetPrivateProfileIntW(L"VR",L"WeaponDouble",0,g_iniPath)!=0;
    g_weaponDoubleShift=ReadFloat(g_iniPath,L"WeaponDoubleShiftMetres",0.30f,-2.0f,2.0f,L"VR");
    g_weaponDoubleMark=static_cast<int>(GetPrivateProfileIntW(L"VR",L"WeaponDoubleMark",0,g_iniPath));
    g_weaponAhead=ReadFloat(g_iniPath,L"WeaponAheadMetres",0.0f,-1.5f,1.5f,L"VR");
    g_weaponRight=ReadFloat(g_iniPath,L"WeaponRightMetres",0.0f,-1.0f,1.0f,L"VR");
    g_weaponUp=ReadFloat(g_iniPath,L"WeaponUpMetres",0.0f,-1.0f,1.0f,L"VR");
    g_weaponRoll=GetPrivateProfileIntW(L"VR",L"WeaponRoll",1,g_iniPath)!=0;
    g_weaponRollSign=GetPrivateProfileIntW(L"VR",L"WeaponRollInvert",0,g_iniPath)!=0?-1.0f:1.0f;
    constexpr float kToRadians=3.14159265f/180.0f;
    g_weaponYaw=ReadFloat(g_iniPath,L"WeaponYawDegrees",kWeaponYawDefault,-90.0f,90.0f,L"VR")*kToRadians;
    g_weaponPitch=ReadFloat(g_iniPath,L"WeaponPitchDegrees",kWeaponPitchDefault,-90.0f,90.0f,L"VR")*kToRadians;
    g_weaponRollTrim=ReadFloat(g_iniPath,L"WeaponRollDegrees",0.0f,-180.0f,180.0f,L"VR")*kToRadians;
    g_adjustEnabled=GetPrivateProfileIntW(L"VR",L"WeaponAdjustGesture",1,g_iniPath)!=0;
    g_adjustMovePerSecond=ReadFloat(g_iniPath,L"WeaponAdjustMetresPerSecond",0.25f,0.02f,2.0f,L"VR");
    g_adjustTurnPerSecond=ReadFloat(g_iniPath,L"WeaponAdjustDegreesPerSecond",30.0f,2.0f,180.0f,L"VR")*kToRadians;
    g_twoHandAim=GetPrivateProfileIntW(L"VR",L"TwoHandAim",1,g_iniPath)!=0;
    g_twoHandGripAlign=GetPrivateProfileIntW(L"VR",L"TwoHandGripAlign",0,g_iniPath)!=0;
    g_twoHandLeftRight=ReadFloat(g_iniPath,L"TwoHandLeftRightMetres",0.0f,-0.3f,0.3f,L"VR");
    g_twoHandLeftUp=ReadFloat(g_iniPath,L"TwoHandLeftUpMetres",0.0f,-0.3f,0.3f,L"VR");
    g_twoHandLeftAhead=ReadFloat(g_iniPath,L"TwoHandLeftAheadMetres",0.0f,-0.3f,0.3f,L"VR");
    g_twoHandAhead=ReadFloat(g_iniPath,L"TwoHandAheadMetres",0.22f,-1.0f,1.5f,L"VR");
    g_twoHandRight=ReadFloat(g_iniPath,L"TwoHandRightMetres",0.05f,-1.0f,1.0f,L"VR");
    g_twoHandUp=ReadFloat(g_iniPath,L"TwoHandUpMetres",-0.05f,-1.0f,1.0f,L"VR");
    g_twoHandRadius=ReadFloat(g_iniPath,L"TwoHandRadiusMetres",0.13f,0.05f,0.8f,L"VR");
    g_twoHandLeaveRadius=ReadFloat(g_iniPath,L"TwoHandLeaveRadiusMetres",0.23f,0.05f,1.0f,L"VR");
    // The two hands took hold too far out: 7 cm closer to take hold, 3 cm
    // sooner to let go. An INI still holding the old defaults, 0.20 and 0.26,
    // has them retired once and written back, as the aim band's was -- a merge
    // only adds keys, and an editor left open writes the old ones back.
    if(g_twoHandRadius>0.1999f && g_twoHandRadius<0.2001f) {
        g_twoHandRadius=0.13f; RememberIn(L"VR",L"TwoHandRadiusMetres",0.13f);
        Log("TWOHAND retired the old 0.20 take-hold radius; now 0.13");
    }
    if(g_twoHandLeaveRadius>0.2599f && g_twoHandLeaveRadius<0.2601f) {
        g_twoHandLeaveRadius=0.23f; RememberIn(L"VR",L"TwoHandLeaveRadiusMetres",0.23f);
        Log("TWOHAND retired the old 0.26 let-go radius; now 0.23");
    }
    g_twoHandApart=ReadFloat(g_iniPath,L"TwoHandMinApartMetres",0.12f,0.02f,0.5f,L"VR");
    g_twoHandSoft=ReadFloat(g_iniPath,L"TwoHandSoftDegrees",55.0f,5.0f,120.0f,L"VR")*3.14159265f/180.0f;
    g_twoHandMax=ReadFloat(g_iniPath,L"TwoHandMaxDegrees",90.0f,10.0f,170.0f,L"VR")*3.14159265f/180.0f;
    g_twoHandSmoothing=ReadFloat(g_iniPath,L"TwoHandSmoothing",0.35f,0.0f,1.0f,L"VR");
    edf6vr::SetPadProvider(&RefreshPad);
    edf6vr::OfferPad(g_padMode!=0);
    // The game resolved XInput before this plugin existed, so the import swap
    // will never be consulted. Patching the library entry points catches it
    // whatever it holds.
    if(GetPrivateProfileIntW(L"VR",L"XInputPatch",1,g_iniPath)!=0)
        edf6vr::PatchXInputExports(&LogText);
    Log("XINPUT %s slot=EDF.dll+%X",edf6vr::XInputBridgeStatus(),edf6vr::kGetProcAddressSlotRva);
    g_requestPointReady=edf6vr::CheckRequestPointProfile(g_image);
    Log("REQUESTPOINTS profile=%d budget=%d (INI Diagnostics/MissionRequestPoints)",g_requestPointReady,g_testRequestPoints);
    g_vehicleReady=edf6vr::CheckVehicleProfile(g_image);
    Log("VEHICLE profile=%d (56D709/56DB5B/576E60)",g_vehicleReady);
    if(!g_fpsReady) g_fpsEnabled=false;
    Log("FPS HOOK ready=%d drawSlot=EDF.dll+%X original=EDF.dll+%X bodyOffset=%zX headLookup=EDF.dll+%X. F9 FPS toggle; F10 body hide diagnostic; F6 exits FPS.",
        g_fpsReady,edf6vr::kModelDrawSlotRva,edf6vr::kModelDrawRva,edf6vr::kBodyModelOffset,edf6vr::kNodeLookupRva);
    if(g_fpsReady && !g_faulted && edf6vr::CheckAimProfile(g_image)) {
        g_dualReady=InstallRangerDual();
        Log("RANGERDUAL hooks ready=%d; left shoulder grip / independent fire / reload paused / zoom disabled",g_dualReady);
        g_inputOriginal=reinterpret_cast<InputRead>(g_image.base+edf6vr::kInputReadRva);
        changed=false;
        const bool inputOK=edf6vr::RedirectCall(g_image.base+edf6vr::kInputCallSiteRva,
            reinterpret_cast<void*>(g_inputOriginal),reinterpret_cast<void*>(&HookInputRead),changed);
        g_vrReady=inputOK;
        if(changed && !inputOK) g_faulted=true;
    }
    if(!g_vrReady) g_vrEnabled=false;
    if(g_vrReady && !g_faulted) {
        changed=false;
        const bool radarOK=edf6vr::InstallRadarHeading(g_image,changed);
        Log("RADAR hook ready=%d changed=%d call=EDF.dll+%X",radarOK,changed,edf6vr::kRadarAngleCall);
        if(changed && !radarOK) g_faulted=true;
        changed=false;
        const bool aimHudOK=edf6vr::InstallAimHud(g_image,changed);
        Log("AIMHUD hook ready=%d changed=%d prepare=F8210 slot=1768C28",aimHudOK,changed);
        changed=false;
        const bool laserOK=edf6vr::InstallLaserSight(g_image,changed);
        Log("LASERSIGHT hook ready=%d changed=%d call=EDF.dll+%X",laserOK,changed,edf6vr::kLaserPrepareCall);
        if(changed && !laserOK) g_faulted=true;
        changed=false;
        {
            bool nameplateChanged=false;
            const bool nameplatesOK=InstallNameplateHooks(nameplateChanged);
            Log("NAMEPLATE hooks ready=%d classes=0x%X changed=%d (MultiPlayStatus 8077B0, FollowerDurability 8040E0, RescueMessage 808410)",
                nameplatesOK,g_worldNameplateMask,nameplateChanged);
        }
        const bool crosshairOK=edf6vr::InstallNativeCrosshair(g_image,changed);
        Log("CROSSHAIR hook ready=%d changed=%d state=EDF.dll+%X; the panel keeps its middle while the VR reticle is drawn",
            crosshairOK,changed,edf6vr::kCrosshairStateRva);
        changed=false;
        const bool boundsOK=InstallTrackedBounds(changed);
        Log("TRACKEDBOUNDS ready=%d changed=%d call=6C06F2; unused action visibility trial disabled",boundsOK,changed);
        WritePatchListBesideLog();
        if(changed && !boundsOK)g_faulted=true;
        changed=false;
        const bool audioOK=InstallAudioHealth(changed);
        Log("AUDIOHEALTH ready=%d changed=%d read-only CRI underrun warning=106C0B6",audioOK,changed);
        if(changed && !audioOK)g_faulted=true;
        changed=false;
        const bool outputOK=InstallAudioOutputHealth(changed);
        Log("AUDIOOUTPUT ready=%d changed=%d read-only writable frames call=100B07B",outputOK,changed);
        if(changed && !outputOK)g_faulted=true;
        Log("CLASSEFFECT hooks ready=%d booster=2CBDF0 marker=6A3D0E/6A3DFB/684271",InstallClassEffects());
        Log("FENCERGUIDE hooks ready=%d guide=17E2498/688C30 laser=17E2458/6890A0",InstallFencerAttachmentHooks());
        changed=false;
        const bool probeOK=InstallGuideProbe(changed);
        Log("GUIDEPROBE hooks ready=%d changed=%d prepare guide=17E2498+18/688680 laser=17E2458+18/688970 (read-only)",probeOK,changed);
        if(changed && !probeOK) g_faulted=true;
        changed=false;
        const bool carryOK=InstallGuideCarry(changed);
        Log("GUIDECARRY hooks ready=%d changed=%d arc=6888D9->6899F0 rays=688FFD->11BD380 (caller stack only; no weapon matrix is written)",carryOK,changed);
        if(changed && !carryOK) g_faulted=true;
        g_casingFollowsWeapon=GetPrivateProfileIntW(L"VR",L"ShellCaseFollowsWeapon",1,g_iniPath)!=0;
        changed=false;
        const bool casingOK=!g_casingFollowsWeapon || InstallCasingOrigin(changed);
        Log("CASING hook ready=%d enabled=%d changed=%d call=690B3F manager=1194280",casingOK,g_casingFollowsWeapon,changed);
        if(changed && !casingOK) g_faulted=true;
        g_muzzleFlashScale=ReadFloat(g_iniPath,L"MuzzleFlashScale",0.5f,0.1f,1.0f,L"Render");
        changed=false;
        const bool flashOK=InstallMuzzleFlashScale(changed);
        Log("MUZZLEFLASH hook ready=%d changed=%d scale=%.2f calls=2BFC02/2C0D32 bounds=2BFC89/2C0DB9 efs=2BA9C0/2BAF60/2BAD96",flashOK,changed,g_muzzleFlashScale.load());
        if(changed && !flashOK) g_faulted=true;
#ifdef EDF6VR_AUDIO_RESEARCH
        g_soundPositionFix=GetPrivateProfileIntW(L"Diagnostics",L"SoundPositionFix",0,g_iniPath)!=0;
        g_soundNearFieldHold=ReadFloat(g_iniPath,L"SoundNearFieldHoldMetres",0.0f,0.0f,5.0f,L"Diagnostics");
        g_soundNearFieldFade=ReadFloat(g_iniPath,L"SoundNearFieldFadeMetres",0.0f,0.0f,20.0f,L"Diagnostics");
        g_soundPanFlip=ReadFloat(g_iniPath,L"SoundPanFlipDegrees",45.0f,0.0f,180.0f,L"Diagnostics");
        g_soundPanFlipCount=GetPrivateProfileIntW(L"Diagnostics",L"SoundPanFlipCount",4,g_iniPath);
        if(!g_soundPanFlipCount || g_soundPanFlipCount>64) g_soundPanFlipCount=4;
        g_soundPlaceVoices=GetPrivateProfileIntW(L"Diagnostics",L"SoundPlaceVoices",1,g_iniPath)!=0;
        g_soundFireWindowMs=GetPrivateProfileIntW(L"Diagnostics",L"SoundFireWindowMs",120,g_iniPath);
        if(g_soundFireWindowMs>1000) g_soundFireWindowMs=120;
        g_soundCarriedMetres=ReadFloat(g_iniPath,L"SoundCarriedMetres",2.0f,0.1f,20.0f,L"Diagnostics");
        g_soundCarriedAfterMs=GetPrivateProfileIntW(L"Diagnostics",L"SoundCarriedAfterMs",150,g_iniPath);
        if(g_soundCarriedAfterMs>5000) g_soundCarriedAfterMs=150;
        g_soundCarriedMoveMetres=ReadFloat(g_iniPath,L"SoundCarriedMoveMetres",0.20f,0.01f,10.0f,L"Diagnostics");
        g_soundUnderfootMetres=ReadFloat(g_iniPath,L"SoundUnderfootMetres",0.6f,0.0f,5.0f,L"Diagnostics");
        g_soundFootDropMetres=ReadFloat(g_iniPath,L"SoundFootDropMetres",1.7f,0.0f,5.0f,L"Diagnostics");
        g_soundBackBehindMetres=ReadFloat(g_iniPath,L"SoundBackBehindMetres",0.35f,0.0f,3.0f,L"Diagnostics");
        g_soundBackDownMetres=ReadFloat(g_iniPath,L"SoundBackDownMetres",0.20f,0.0f,3.0f,L"Diagnostics");
        g_soundPanStep=ReadFloat(g_iniPath,L"SoundPanStepDegrees",25.0f,0.0f,180.0f,L"Diagnostics");
        const bool soundTrace=GetPrivateProfileIntW(L"Diagnostics",L"SoundPositionTrace",0,g_iniPath)!=0;
        if(g_soundPositionFix || soundTrace) {
            const bool soundReady=InstallSoundPositionProbe();
            // Observation only, and no part of the fix: it stays behind the trace.
            if(soundTrace) {
                const bool lifecycleReady=InstallSoundLifecycleProbe();
                Log("SOUNDLIFE installed=%d; native playback status and manager cadence observation only",lifecycleReady);
            }
            Log("SOUNDPOS installed=%d fix=%d placeVoices=%d fireWindow=%ums carried=%.2fm after=%ums move=%.2fm "
                "panStep=%.1fdeg flip=%.1fdegx%u; VR first person, all classes; volume/pitch/lifetime unchanged",
                soundReady,soundReady && g_soundPositionFix,g_soundPlaceVoices,g_soundFireWindowMs,
                g_soundCarriedMetres,g_soundCarriedAfterMs,g_soundCarriedMoveMetres,
                g_soundPanStep,g_soundPanFlip,g_soundPanFlipCount);
        }
#endif
        if(g_nativeStereoProbe) {
            const bool imports=InstallUmbraProbe();
            const bool commands=imports && InstallUmbraCommandProbe();
            const bool matchRequested=GetPrivateProfileIntW(L"Render",L"MatchRenderedPose",1,g_iniPath)!=0;
            const bool traceRequested=GetPrivateProfileIntW(L"Diagnostics",L"MotionTrace",0,g_iniPath)!=0;
            const bool getters=(matchRequested || traceRequested || g_nativeWorldRequested) && imports && InstallUmbraTrace();
            const bool matchEnabled=(matchRequested || g_nativeWorldRequested) && getters;
            edf6vr::ConfigureRenderedPose(matchEnabled);
            Log("RENDERPOSE enabled=%d native camera history; required for native world",matchEnabled);
            if(traceRequested) {
                wchar_t traceDirectory[MAX_PATH]{};GetModuleFileNameW(nullptr,traceDirectory,MAX_PATH);
                if(auto traceSlash=wcsrchr(traceDirectory,L'\\')) {traceSlash[1]=0;wcscat_s(traceDirectory,L"_VRDEV\\EDF6VR\\research\\runtime");}
                const bool trace=commands && getters;
                edf6vr::ConfigureMotionTrace(trace,traceDirectory,&LogText);
                Log("MOTIONTRACE enabled=%d read-only view/projection imports, one 30-second mission capture",trace);
            }
            const edf6vr::NativeWorldQueueCallbacks callbacks{&AllowNativeWorldPair,&BeginNativeWorldQueue,&EndNativeWorldQueue,&BeginNativeWorldProducer,&EndNativeWorldProducer};
            const bool compositeReady=g_nativeWorldRequested && edf6vr::InstallNativeWorldComposite(g_image,&Log);
            const bool nativeReady=g_nativeWorldRequested && imports && getters && compositeReady &&
                edf6vr::InstallNativeWorldQueue(g_image,callbacks,&Log);
            edf6vr::ConfigureNativeWorld(nativeReady,&LogText);
            const bool distanceReady=nativeReady && InstallWorldDistance();
            Log("WORLDDIST installed=%d Main=1197FBD Far=1197F31 preserve reference-FOV cutoff",distanceReady);
            Log("NATIVEWORLD enabled=%d requested=%d paired submission 1197A55/119889B; 1 simulation / 2 native world views",nativeReady,g_nativeWorldRequested);
            Log("UMBRA integration ready=%d commands=%d cameraLimit=16 nativeWorld=%d",imports,commands,nativeReady);
        }
    }
    Log("FPSDIAG toggle F5: blanks EDF.dll+11A2F65 (6) and +11A2F74 (2) while a mission runs. allowed=%d",
        g_allowLimiterToggle);
    Log("VR HOOK ready=%d callSite=EDF.dll+%X inputRead=EDF.dll+%X aimAngles=+%zX moveInput=+%zX lookInput=+%zX. F11 VR toggle; F12 recenter.",
        g_vrReady,edf6vr::kInputCallSiteRva,edf6vr::kInputReadRva,
        edf6vr::kAimAngleOffset,edf6vr::kMoveInputOffset,edf6vr::kLookInputOffset);
    if(g_logGamepad) g_padThread=std::thread(&GamepadWatch);
    if(g_captureDevice && !edf6vr::InstallDeviceCapture(g_image,&LogRender))
        Log("RENDER: device capture unavailable: %s",edf6vr::PresentCaptureStatus());
    if(g_captureFrames>=2) {
        // Load-time installation is what stopped 0.4.0 from starting at all.
        // It stays available only for a deliberate experiment.
        if(!edf6vr::InstallPresentCapture(&LogRender,g_describeSwapChain))
            Log("RENDER: present capture unavailable: %s",edf6vr::PresentCaptureStatus());
    } else Log("RENDER: present capture mode %d; nothing installed during load",g_captureFrames);
    if(g_vrEnabled) { g_vrEnabled=false; StartVr(); }
    return true;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) g_module=instance;
    // No hooks, IO, threads or waits under the loader lock. Resident until process exit.
    return TRUE;
}
