#pragma once
#include <Windows.h>
#include <cstdint>
#include "vr_math.h"
#include "ui_cluster.h"

namespace edf6vr {
void SetWarpTrialEnabled(bool) noexcept;
int WarpTrialPhase() noexcept;

// What the controllers are doing, both hands, read once a frame. Index 0 is the
// left hand and 1 the right. Named for where the control is rather than what it
// does in the game, because what it does is a mapping decided elsewhere.
struct ControllerState {
    bool present[2]{};
    float trigger[2]{};      // 0 to 1
    float squeeze[2]{};      // 0 to 1
    // True when squeeze came from a force sensor rather than from the analogue
    // grip. The two need different thresholds: the analogue one rises from a
    // hand resting on the controller, the force one does not.
    bool squeezeIsForce[2]{};
    float stick[2][2]{};     // x then y, -1 to 1
    bool stickClick[2]{};
    // The thumb resting on the stick (capacitive), where the profile has it;
    // otherwise a deflected or pressed stick stands in.
    bool stickTouch[2]{};
    bool lower[2]{};         // A on Index and on Touch right, X on Touch left
    bool upper[2]{};         // B on Index and on Touch right, Y on Touch left
    bool menu[2]{};
};
// Left-handed mode (plugin.cpp, [VR] LeftHanded): every per-hand field trades
// sides, so what the mod calls the right hand -- the gun's -- is the physical left.
inline void SwapControllerHands(ControllerState& s) noexcept {
    auto swapBoth=[](auto& pair) { auto keep=pair[0]; pair[0]=pair[1]; pair[1]=keep; };
    swapBoth(s.present); swapBoth(s.trigger); swapBoth(s.squeeze); swapBoth(s.squeezeIsForce);
    for(int a=0;a<2;++a) { const float keep=s.stick[0][a]; s.stick[0][a]=s.stick[1][a]; s.stick[1][a]=keep; }
    swapBoth(s.stickClick); swapBoth(s.stickTouch); swapBoth(s.lower); swapBoth(s.upper); swapBoth(s.menu);
}
// Just the sticks back to their own hands (movement stays on the left stick).
inline void UnswapControllerSticks(ControllerState& s) noexcept {
    for(int a=0;a<2;++a) { const float keep=s.stick[0][a]; s.stick[0][a]=s.stick[1][a]; s.stick[1][a]=keep; }
    const bool click=s.stickClick[0]; s.stickClick[0]=s.stickClick[1]; s.stickClick[1]=click;
    const bool touch=s.stickTouch[0]; s.stickTouch[0]=s.stickTouch[1]; s.stickTouch[1]=touch;
}

struct HmdSample {
    Quat orientation{};
    Vec3 position{};        // OpenXR reference-space metres
    bool orientationValid=false;
    bool positionValid=false;
    std::uint64_t frame=0;
    std::int64_t displayTime=0;
};

using VrLogger=void (*)(const char* text);

// What is put in front of the eyes. Quad is the head-locked screen that has been
// working since 0.5.0 and stays as the fallback; the projection modes place the
// game image in the world with the frusta the runtime asks for.
enum class XrDisplayMode {
    Quad,               // one head-locked screen, both eyes, no parallax
    ProjectionMono,     // world-scale projection, the same image in both eyes
    ProjectionStereo,   // as above, but each eye gets its own alternating frame
};

// How the session is driven. Only one of these can own the frame loop, because
// OpenXR allows exactly one xrWaitFrame/xrEndFrame cycle per session.
enum class XrMode {
    TrackingOnly,   // our own D3D11 device, our own thread, no layers submitted
    HeadsetDisplay, // the game's device, driven from its render thread, one quad layer
};

// Minimal OpenXR client: it negotiates directly with the active runtime DLL and
// publishes the latest head pose. In TrackingOnly it keeps a thread of its own
// and draws nothing. In HeadsetDisplay the game's present drives every frame and
// the finished game image is shown on a head-locked quad.
class OpenXrRuntime {
public:
    // gameDevice/gameContext are required for HeadsetDisplay and ignored
    // otherwise; the mode falls back to TrackingOnly when they are missing.
    bool Start(VrLogger log,XrMode mode,void* gameDevice,void* gameContext) noexcept;
    // HeadsetDisplay: queues shutdown; keep calling RenderFrame until Mode is
    // TrackingOnly. TrackingOnly: joins the private tracking thread as before.
    void Stop() noexcept;
    bool Running() const noexcept;
    bool Sample(HmdSample& out) const noexcept;
    const char* Status() const noexcept;      // last error or state text
    std::uint64_t Frames() const noexcept;
    bool StageSpace() const noexcept;
    XrMode Mode() const noexcept;

    // Render thread only, and only in HeadsetDisplay: runs one OpenXR frame and
    // copies the finished game image onto the quad. Never throws or blocks the
    // game when the session is not ready.
    // Optional render-thread frame start before EDF main visibility/drawing.
    // Present consumes it once; menus and missing hooks keep the Present fallback.
    void PrepareSceneFrame() noexcept;
    void RenderFrame(void* backBuffer,unsigned width,unsigned height,unsigned sampleCount) noexcept;

    // The width-over-height the game's 2D content was laid out for, so the
    // flat panels can undo a render rectangle narrower than that. Zero uses the
    // rendered texture's own aspect, which is right when the two agree.
    // Degrees added to every side of the frustum the runtime reports, so the
    // rendered image reaches past the display on all four edges rather than
    // meeting it exactly. Paid for in pixels per degree, not in field of view.
    void SetFovMargin(float degrees) noexcept;
    void SetContentAspect(float widthOverHeight) noexcept;
    // Fraction of the runtime's field of view the game is asked to render.
    // Below 1 the image no longer fills the view, but every remaining degree
    // gets more pixels and the game has less world to draw.
    void SetFovScale(float) noexcept;
    void SetDisplayMode(XrDisplayMode) noexcept;
    XrDisplayMode DisplayMode() const noexcept;
    bool UiCaptureAvailable() const noexcept;
    // Game thread, once per frame before the game renders: which eye this frame
    // is for. -1 means one image for both eyes.
    void SetFrameEye(int eye) noexcept;
    // Build the eye the game did not draw, from the one it did plus the scene
    // depth, instead of leaving it on its previous image. Both eyes then come
    // from the same instant and update every frame, at no cost to the game.
    void SetEyeWarp(bool on,float nearPlane,float farPlane) noexcept;
    // Samples along the search for the source pixel. More is slower and finds
    // thin things the coarse walk steps over.
    void SetWarpSteps(float steps) noexcept;
    // Nothing is treated as nearer than this, which stops a weapon drawn in its
    // own depth range from throwing the shift off the end of the search.
    // The controller aim poses in the reference space, hand 0 left and 1 right.
    // False when that hand is not being tracked. Nothing depends on these yet.
    bool HandPose(int hand,float position[3],float orientation[4]) const noexcept;
    // The palm rather than the muzzle end. A held object turns about the palm,
    // so this is what anything carried in the hand should be anchored on.
    bool GripPose(int hand,float position[3],float orientation[4]) const noexcept;
    // Everything else the controllers are doing. False when input is not up.
    bool Controls(ControllerState& out) const noexcept;
    // Queue a short buzz for the XR owner; finite lifetime includes queue delay.
    // Stop replaces pending feedback and is dispatched at the next XR input sync.
    void Buzz(int hand,float seconds,float amplitude) const noexcept;
    void StopBuzz(int hand) const noexcept;
    // Left-handed mode: while on, hand 1 ("right", the gun's) is the physical
    // left controller in HandPose, GripPose, Controls, Buzz and StopBuzz. The
    // Physical forms never swap (the drawn hands stay on their own controllers).
    void SetHandSwap(bool on) noexcept;
    bool HandSwap() const noexcept;
    bool HandPosePhysical(int hand,float position[3],float orientation[4]) const noexcept;
    bool GripPosePhysical(int hand,float position[3],float orientation[4]) const noexcept;
    bool ControlsPhysical(ControllerState& out) const noexcept;
    const char* InputStatus() const noexcept;
    // The controller family the runtime chose for the right hand, as a short
    // INI-friendly word: "index", "touch" (Quest), "psvr2", "wmr", "vive",
    // "simple", or "" until the runtime has said. Settings that depend on the
    // shape of the controller -- where the hand sits on it -- are kept per family.
    const char* ControllerKind() const noexcept;

    // Where the shot will go, as a direction in the reference space. The reticle
    // is hung along it. Pass null when it is not known.
    // handAim selects the independent VR sight in native stereo; vehicles keep theirs.
    void SetAimDirection(const float reference[3],bool handAim=false) noexcept;
    // A second reticle, for the other hand's weapon (Ranger dual wield, Fencer).
    void SetAimDirection2(const float reference[3]) noexcept;
    void SetBinocularZoom(float magnification) noexcept;
    void SetReticle(bool follows,int pixels,float distanceMetres,float scale) noexcept;

    void SetWarpNearest(float metres) noexcept;
    // The board that menus and the title are shown on: its width and distance.
    void SetBoard(float widthMetres,float distanceMetres) noexcept;
    // World-locked: the board is hung in the room in front of where the head
    // faced when it appeared, instead of following the head. Recenter moves it
    // in front of the head again.
    void SetBoardWorldLocked(bool on) noexcept;
    // The compact HUD (ui_cluster.h): what goes on it, where it hangs (0 the
    // panel's bottom-right corner, 1 the inside of the right wrist, 2 the left)
    // and, for the wrists, the pose in reference space the game thread works
    // out each tick.
    void SetUiCluster(bool on,const UiClusterLayout& layout) noexcept;
    void SetUiClusterPlace(int place,float wristWidthMetres,float marginRight,float marginBottom) noexcept;
    void SetUiClusterPose(const float position[3],const float orientation[4],bool valid) noexcept;
    // The radio subtitles, moved up toward the middle of the panel: the box's
    // pieces (fractions of the HUD picture, at most 3) and how far to move them
    // (negative is up). Off with on=false or no pieces.
    void SetSubtitles(bool on,const UiRect* rects,unsigned count,float dy) noexcept;
    // While a window the compact HUD would cut into is open (the quick chat),
    // the HUD is left whole on the panel and the cluster is not drawn. A
    // suspension older than a minute is ignored, in case a close was missed.
    void SuspendUiCluster(bool on) noexcept;
    void RecenterBoard() noexcept;
    // Where the near end of the parallax starts being compressed, and how much
    // of it is kept there. Beyond the first, nothing changes.
    void SetWarpEase(float kneeMetres,float scale) noexcept;
    // Leave the UI where it is instead of shifting it with the depth behind it.
    void SetWarpHudless(bool on) noexcept;
    // Paint what the warp reads instead of building the eye: 1 the distance,
    // 2 the sideways shift it would apply. Whether the weapon is in the depth
    // buffer at all is a thing to look at, not to reason about.
    // Must match the scale the game camera was actually moved by, or every
    // distance the warp builds comes out wrong by that same factor.
    // Where a frame dump is written, ending in a backslash.
    void SetDumpDirectory(const wchar_t* directory) noexcept;
    void SetWarpEyeScale(float scale) noexcept;
    void SetWarpDebug(float mode) noexcept;
    bool WarpDebugging() const noexcept;
    // Called when the game is given its camera, to keep the pose the coming
    // image will actually have been drawn from.
    void MarkCameraPose() noexcept;
    // Called while a soldier is being driven. Without one for a moment the
    // display drops to the board, which is what a menu wants.
    void MarkSceneLive() noexcept;
    // Called while a menu window is being updated (menu_board.h): the board
    // and no HUD capture until 0.3 s after the last one, online too.
    void MarkMenuOpen() noexcept;
    bool MenuOpen() const noexcept;
    // 0 decides for itself, 1 always the board, 2 always the world.
    // The lowest and highest alpha in the middle of the last image handed over,
    // or -1 before one has been read.
    // Put the HUD on a head-locked panel of its own, at a size and distance we
    // choose, rather than letting it ride the world across the whole field of
    // view, where it runs past the edges of sight.
    void SetUiLayer(bool on,float widthMetres,float distanceMetres) noexcept;
    void SetDesktopMirror(bool on) noexcept;
    // Crop each eye to its own field of view even where the runtime would take a
    // wider one (test of the path Meta's runtime needs; eye_crop.h).
    void SetEyeFovCrop(bool always) noexcept;
    void SetDesktopMirrorFov(float horizontalDegrees) noexcept;
    std::uint64_t UiLayers() const noexcept;
    void ReadAlpha(int& low,int& high) const noexcept;
    void ForceDisplay(int mode) noexcept;
    // Whether the runtime is taking what we submit. The session state, the last
    // xrEndFrame result, how many it refused, and how many frames it asked us
    // not to render at all.
    void ReadSubmission(int& sessionState,int& lastEndFrame,
                        unsigned long long& endFrameFailures,
                        unsigned long long& notRendering,
                        int& lastLayerCount,int& lastLayerKind,
                        int& fovDeg,int& haveImages) const noexcept;
    unsigned long long SceneAgeMs() const noexcept;
    std::uint64_t WarpedEyes() const noexcept;
    const char* WarpStatusText() const noexcept;
    const char* MaskStatusText() const noexcept;
    // What the game has to render with for the projection layer to be right:
    // a symmetric vertical field of view in radians, and the runtime's own eye
    // separation in metres. False until the runtime has reported its frusta.
    bool StereoAdvice(float& verticalFovRadians,float& ipdMetres) const noexcept;
    // The widest half-angle either eye actually displays, and the vertical the
    // advice above is built from, both in radians, plus the runtime's name for
    // the headset.
    //
    // The rendered horizontal field is not chosen anywhere: it falls out of the
    // render rectangle's aspect against the vertical. So this is the number
    // that aspect has to cover, and anything past it is world drawn into pixels
    // the headset never shows. False until the runtime has reported its frusta.
    bool HeadsetFrustum(float& halfHorizontalRadians,float& halfVerticalRadians,
                        const char*& systemName) const noexcept;
    std::uint64_t ProjectionLayers() const noexcept;
    // Frames that reshowed the previous images because no new one arrived.
    std::uint64_t ReshownFrames() const noexcept;
    // Frames whose image carried the same eye as the previous one, which
    // is how a mismatch between the camera rate and the present rate shows.
    std::uint64_t EyeRepeats() const noexcept;
    // True once, for the game thread, when a frame has actually taken the
    // image the current eye was drawn for. Holding the eye until then is
    // what keeps the two eyes alternating at whatever rate the game presents.
    bool TakeEyeConsumed() noexcept;
    std::uint64_t SubmittedLayers() const noexcept;

private:
    void ThreadMain() noexcept;
};

extern OpenXrRuntime g_openxr;
// How often the world-anchored HUD was laid over an eye, and refusals.
void WorldUiCompositeCounts(unsigned long long& composites,unsigned long long& failures) noexcept;
void UiClusterCounts(unsigned long long& composites,unsigned long long& failures) noexcept;
void SubtitleCounts(unsigned long long& moves,unsigned long long& failures) noexcept;
unsigned long long UiClusterSuspendedFrames() noexcept;
// The headset's own recenters seen so far (the runtime's event, or its LOCAL
// space stepping against STAGE); the game side recenters when it changes. The
// signs, for the log: events, steps, and whether LOCAL is watched at all.
unsigned RuntimeRecenterCount() noexcept;
void RuntimeRecenterSigns(unsigned long long& events,unsigned long long& jumps,bool& watchingLocal) noexcept;
// The last counted step of LOCAL against STAGE (size, and the frame gap before
// it), and the steps not counted (across a stall, or not held 0.3 s).
void RuntimeRecenterStep(float& centimetres,float& degrees,float& gapMs,unsigned long long& dropped) noexcept;
// Placement of that HUD over the eye: 1,1,0 is the game's own picture.
// debug paints that HUD magenta so it can be told from the panel's copy.
void SetWorldUiPlacement(float scaleX,float scaleY,float offsetY,bool debug) noexcept;
}
