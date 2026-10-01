#include "motion_trace.h"
#include "render_pose.h"
#include "scene_aa.h"
#include "openxr_session.h"
#include "native_crosshair.h"
#include "aim_hud.h"
#include "native_world.h"
#include "cockpit_draw.h"
#include "cockpit_lighting.h"
#include "vr_math.h"
#include "weapon_stereo.h"
#include "scripted_pose.h"
#include "frame_profile.h"
#include "eye_warp.h"
#include "depth_probe.h"
#include "frame_dump.h"
#include "gpu_profile.h"
#include "warp_trial.h"
#include "desktop_mirror.h"
#include "render_capture.h"
#include "ui_capture.h"
#include "ui_world_composite.h"
#include "eye_check_sampler.h"
#include "eye_crop.h"
#include "scope_draw.h"

#include <unknwn.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <iterator>
#include <new>
#include <string>
#include <thread>

namespace edf6vr {
namespace {
std::atomic<bool> g_trialEnabled{false};
std::atomic<int> g_trialPhase{0};
WarpTrial g_trial;
}
void SetWarpTrialEnabled(bool value) noexcept { g_trialEnabled=value; }
int WarpTrialPhase() noexcept { return g_trialPhase.load(); }
namespace {
struct Api {
    PFN_xrGetInstanceProcAddr getProc=nullptr;
    PFN_xrCreateInstance createInstance=nullptr;
    PFN_xrDestroyInstance destroyInstance=nullptr;
    PFN_xrGetSystem getSystem=nullptr;
    PFN_xrCreateSession createSession=nullptr;
    PFN_xrDestroySession destroySession=nullptr;
    PFN_xrCreateReferenceSpace createSpace=nullptr;
    PFN_xrDestroySpace destroySpace=nullptr;
    PFN_xrEnumerateReferenceSpaces enumerateSpaces=nullptr;
    PFN_xrPollEvent pollEvent=nullptr;
    PFN_xrBeginSession beginSession=nullptr;
    PFN_xrEndSession endSession=nullptr;
    PFN_xrRequestExitSession requestExitSession=nullptr;
    PFN_xrWaitFrame waitFrame=nullptr;
    PFN_xrBeginFrame beginFrame=nullptr;
    PFN_xrEndFrame endFrame=nullptr;
    PFN_xrLocateSpace locateSpace=nullptr;
    PFN_xrEnumerateSwapchainFormats enumerateFormats=nullptr;
    PFN_xrCreateSwapchain createSwapchain=nullptr;
    PFN_xrDestroySwapchain destroySwapchain=nullptr;
    PFN_xrEnumerateSwapchainImages enumerateImages=nullptr;
    PFN_xrAcquireSwapchainImage acquireImage=nullptr;
    PFN_xrWaitSwapchainImage waitImage=nullptr;
    PFN_xrReleaseSwapchainImage releaseImage=nullptr;
    PFN_xrLocateViews locateViews=nullptr;
    // Input. Absent on a runtime that offers no controllers, so these are loaded
    // separately and their absence is not fatal.
    PFN_xrStringToPath stringToPath=nullptr;
    PFN_xrCreateActionSet createActionSet=nullptr;
    PFN_xrDestroyActionSet destroyActionSet=nullptr;
    PFN_xrCreateAction createAction=nullptr;
    PFN_xrSuggestInteractionProfileBindings suggestBindings=nullptr;
    PFN_xrAttachSessionActionSets attachActionSets=nullptr;
    PFN_xrCreateActionSpace createActionSpace=nullptr;
    PFN_xrSyncActions syncActions=nullptr;
    PFN_xrGetActionStatePose getActionStatePose=nullptr;
    PFN_xrGetActionStateFloat getActionStateFloat=nullptr;
    PFN_xrGetActionStateBoolean getActionStateBoolean=nullptr;
    PFN_xrGetActionStateVector2f getActionStateVector2f=nullptr;
    PFN_xrApplyHapticFeedback applyHaptic=nullptr;
    PFN_xrStopHapticFeedback stopHaptic=nullptr;
};

HMODULE g_runtimeModule=nullptr;
Api g_api{};
// The controllers. Nothing depends on these yet: they are read and reported so
// that whether poses arrive at all through an instance negotiated straight
// against the runtime, with no loader in between, is known before anything is
// built on top of them.
//
// Structure follows fear-vr (MIT, _VRDEV/References/fear-vr-main/src/host64/
// xr_input.cpp): one action set, a pose action with a subaction path per hand,
// and the same bindings suggested for each interaction profile in turn.
XrActionSet g_actionSet=XR_NULL_HANDLE;
XrAction g_aimAction=XR_NULL_HANDLE;
XrAction g_gripAction=XR_NULL_HANDLE;
// The rest of the controller. One action each, both hands on a subaction path,
// and per profile bindings below, because the paths are not the same on every
// controller and a profile refuses the whole suggestion if one path is wrong.
XrAction g_triggerAction=XR_NULL_HANDLE;
XrAction g_squeezeAction=XR_NULL_HANDLE;
// Index has both. squeeze/value follows the capacitive grip and rises from a
// hand simply resting on the controller, which is why a touch was counting as a
// press; squeeze/force is the force sensor and is what a squeeze actually is.
// Where it exists it is preferred.
XrAction g_squeezeForceAction=XR_NULL_HANDLE;
XrAction g_stickAction=XR_NULL_HANDLE;
XrAction g_stickClickAction=XR_NULL_HANDLE;
XrAction g_stickTouchAction=XR_NULL_HANDLE;
XrAction g_lowerAction=XR_NULL_HANDLE;   // A on Index and Touch right, X on Touch left
XrAction g_upperAction=XR_NULL_HANDLE;   // B on Index and Touch right, Y on Touch left
XrAction g_menuAction=XR_NULL_HANDLE;
XrAction g_hapticAction=XR_NULL_HANDLE;
XrPath g_handPath[2]{};
XrSpace g_aimSpace[2]{};
XrSpace g_gripSpace[2]{};
struct HandTracked {
    float position[3]{};
    float orientation[4]{0,0,0,1};
    bool tracked=false;
};
// What the controllers are doing, read once a frame and handed over whole.
ControllerState g_controls{};
SRWLOCK g_handLock=SRWLOCK_INIT;
HandTracked g_hand[2]{};      // 0 left, 1 right, aim pose in the reference space
// The same hands, taken at the grip pose instead.
//
// The aim pose sits out in front of the controller, where a laser would leave
// it; the grip pose sits in the palm. Anything held in the hand turns about the
// palm, so a weapon anchored on the aim pose swings away from the hand as soon
// as the wrist turns -- worst of all when pointing upwards, which is the longest
// lever. The grip space was already being created and never read.
HandTracked g_grip[2]{};
std::atomic<bool> g_inputReady{false};
char g_inputNote[128]="input not set up";
char g_controllerKind[32]="";
void NoteControllerKind(const char* path) noexcept {
    struct Known { const char* needle; const char* kind; };
    const Known known[]={{"index_controller","index"},{"touch_controller","touch"},{"touch_pro","touch"},{"touch_plus","touch"},
        {"psvr2","psvr2"},{"sony","psvr2"},{"microsoft","wmr"},{"vive_controller","vive"},{"simple_controller","simple"}};
    for(const auto& k:known) if(std::strstr(path,k.needle)) { strcpy_s(g_controllerKind,k.kind); return; }
    // Anything else: the last word of the path, letters and digits only.
    const char* last=std::strrchr(path,'/'); last=last?last+1:path;
    unsigned n=0;
    for(;last[n] && n<sizeof(g_controllerKind)-1;++n) g_controllerKind[n]=(std::isalnum(static_cast<unsigned char>(last[n]))||last[n]=='_')?last[n]:'_';
    g_controllerKind[n]=0;
}
XrInstance g_instance=XR_NULL_HANDLE;
XrSession g_session=XR_NULL_HANDLE;
XrSpace g_viewSpace=XR_NULL_HANDLE;
XrSpace g_refSpace=XR_NULL_HANDLE;
XrSystemId g_system=XR_NULL_SYSTEM_ID;
XrVersion g_runtimeApiVersion=0;
ID3D11Device* g_device=nullptr;          // ours in TrackingOnly, the game's in HeadsetDisplay
ID3D11DeviceContext* g_context=nullptr;
bool g_ownDevice=false;                  // release the device only when we made it
std::atomic<XrMode> g_mode{XrMode::TrackingOnly};
XrSwapchain g_swapchain=XR_NULL_HANDLE;
ID3D11Texture2D** g_images=nullptr;      // runtime-owned textures, borrowed
ID3D11Texture2D* g_resolved=nullptr;     // non-MSAA staging copy of the backbuffer
uint32_t g_imageCount=0;
unsigned g_swapchainWidth=0, g_swapchainHeight=0;
XrSpace g_quadSpace=XR_NULL_HANDLE;      // head-locked space the quad hangs in
// World-locked board: its pose in the reference space, placed when the board
// appears (no board frame for over a second) or on recenter.
std::atomic<bool> g_boardWorldLocked{true},g_boardRecenter{true};
XrPosef g_boardPose{{0,0,0,1},{0,0,0}};
ULONGLONG g_boardLastShown=0;
// The HUD on a panel of its own. EDF6 draws it for a 16:9 screen and we ask
// the game for a hundred and seven degrees, so carried along with the world
// it ends up past the edges of sight and the radar cannot be read. On its own
// panel its size and distance are ours to choose.
XrSwapchain g_swapchainUi=XR_NULL_HANDLE;
ID3D11Texture2D** g_imagesUi=nullptr;
uint32_t g_imageCountUi=0;
// The compact HUD (ui_cluster.h): a small swapchain of its own, remade when
// the canvas changes, and what the game thread set for it.
XrSwapchain g_swapchainCluster=XR_NULL_HANDLE;
ID3D11Texture2D** g_imagesCluster=nullptr;
uint32_t g_imageCountCluster=0;
uint32_t g_clusterMadeW=0,g_clusterMadeH=0;
// A square of its own for the reticle. Small, and remade when the size setting
// changes. It cannot share the panel image: the panel pass clears the middle of
// that, which is exactly the pixels the reticle is made of.
XrSwapchain g_swapchainReticle=XR_NULL_HANDLE;
ID3D11Texture2D** g_imagesReticle=nullptr;
uint32_t g_imageCountReticle=0;
int g_reticleMade=0;
UiClusterLayout g_clusterLayout;
std::atomic<bool> g_clusterOn{false};
std::atomic<int> g_clusterPlace{0};
std::atomic<float> g_clusterWristWidth{0.30f},g_clusterMarginRight{0.03f},g_clusterMarginBottom{0.05f};
XrPosef g_clusterPose{{0,0,0,1},{0,0,0}};
std::atomic<bool> g_clusterPoseValid{false};
SRWLOCK g_clusterLock=SRWLOCK_INIT;
std::atomic<unsigned long long> g_clusterComposites{0},g_clusterFailures{0};
std::atomic<bool> g_uiLayer{true};
std::atomic<bool> g_desktopMirror{true};
// Whether a projection view may declare a field of view other than the eye's
// own (XrViewConfigurationProperties::fovMutable; eye_crop.h). Assumed so until
// the runtime says otherwise. [Render] EyeFovCrop=1 crops on every runtime (test).
std::atomic<bool> g_fovMutable{true};
std::atomic<bool> g_eyeFovCropAlways{false};
std::atomic<float> g_desktopMirrorFov{90.f};
std::atomic<float> g_uiWidth{1.6f}, g_uiDistance{1.4f};
// The reticle. It is drawn at the middle of the HUD, which since the view was
// put on the head is the middle of where the player is looking, while the shot
// goes where the weapon points. So the middle of the HUD image is taken out of
// the panel and hung on a quad of its own, turned to the weapon.
//
// It uses a separate image: the panel copy is cleared in that rectangle.
// ReticlePixels is measured at 1080p; the HUD itself scales with resolution.
// Sight13 (Limpet sniper) is 2.08x Sight01 in shipped MDB geometry.
// 208 covers that larger footprint; crop and quad size grow together so the
// glyph scale stays unchanged. All three erase/extract paths use this size.
std::atomic<bool> g_reticleFollows{true};
std::atomic<int> g_reticlePixels{208};
std::atomic<float> g_reticleDistance{15.0f};
// How big to draw it, against the size it had on the panel. Separate from the
// square that is cut out, because those are different questions: the cut has to
// cover the whole reticle or the rest of it stays on the panel and only a speck
// follows the weapon, which is what a small cut looked like.
std::atomic<float> g_reticleScale{1.0f};
// Where the game will actually send the shot, in the reference space. Written
// from the game side, where the aim is known after the game has had its way
// with it.
std::atomic<float> g_aimX{0}, g_aimY{0}, g_aimZ{-1};
std::atomic<bool> g_aimKnown{false};
std::atomic<float> g_aim2X{0}, g_aim2Y{0}, g_aim2Z{-1};
std::atomic<bool> g_aim2Known{false};
std::atomic<unsigned long long> g_reticle2Layers{0};
std::atomic<bool> g_aimFromHand{false};
// Where the head is, kept from the same locate the tracking sample comes from.
// The reticle uses its position, but stays in reference space rather than VIEW:
// the compositor's newer head orientation must not rotate it with the HUD.
XrPosef g_lastViewPose{{0,0,0,1},{0,0,0}};
// Nameplates and other world-anchored HUD laid over the world eyes.
std::atomic<unsigned long long> g_worldUiComposites{0},g_worldUiFailures{0};
}
std::atomic<float> g_worldUiScaleX{1.0f},g_worldUiScaleY{1.0f},g_worldUiOffsetY{0.0f};
std::atomic<bool> g_worldUiDebug{false};
void SetWorldUiPlacement(float scaleX,float scaleY,float offsetY,bool debug) noexcept {
    g_worldUiScaleX.store(scaleX); g_worldUiScaleY.store(scaleY); g_worldUiOffsetY.store(offsetY); g_worldUiDebug.store(debug);
}
void WorldUiCompositeCounts(unsigned long long& composites,unsigned long long& failures) noexcept {
    composites=g_worldUiComposites.load(); failures=g_worldUiFailures.load();
}
namespace {
void CompositeWorldUi(ID3D11Texture2D* target) noexcept {
    auto* overlay=CapturedWorldUi();
    if(!overlay || !target) return;
    if(CompositeOverlayImage(g_device,g_context,overlay,target,g_worldUiScaleX.load(std::memory_order_relaxed),
                             g_worldUiScaleY.load(std::memory_order_relaxed),g_worldUiOffsetY.load(std::memory_order_relaxed),
                             g_worldUiDebug.load(std::memory_order_relaxed)))
        g_worldUiComposites.fetch_add(1,std::memory_order_relaxed);
    else g_worldUiFailures.fetch_add(1,std::memory_order_relaxed);
}
std::atomic<unsigned long long> g_uiLayers{0};
// The right eye keeps its own swapchain so each eye retains its last image when
// the game only renders one of them per frame.
XrSwapchain g_swapchainRight=XR_NULL_HANDLE;
ID3D11Texture2D** g_imagesRight=nullptr;
uint32_t g_imageCountRight=0;
int64_t g_swapchainFormat=0;
std::atomic<XrDisplayMode> g_displayMode{XrDisplayMode::Quad};
std::atomic<int> g_frameEye{-1};
// When no first person scene is being drawn there is nothing to place in the
// world: title, menus, loading, the results screen. Carrying on with a
// projection leaves one eye showing the menu and the other still holding the
// last frame of the mission, which is unreadable. The board is what that wants.
std::atomic<unsigned long long> g_frameEyeStamp{0};
std::atomic<int> g_displayForce{0};   // 0 automatic, 1 board, 2 the world
// The compositor showing its own background means it never took our frame,
// and the result of xrEndFrame was the one thing never being looked at.
std::atomic<int> g_lastEndFrame{0};
std::atomic<unsigned long long> g_endFrameFailures{0};
std::atomic<unsigned long long> g_notRendering{0};
std::atomic<int> g_sessionState{0};
// The layer counter moves in both branches, so it never said which one, nor
// whether a layer went out at all. These do.
std::atomic<int> g_lastLayerCount{-1};
std::atomic<int> g_lastLayerKind{0};      // 0 none, 1 board, 2 world
std::atomic<int> g_lastFovDeg{0};
std::atomic<int> g_haveImages{0};         // bit 0 left, bit 1 right
// A quad layer becomes an overlay in SteamVR, and an overlay with no alpha
// is invisible however correct everything else is. The board shows during a
// mission and not on a menu, same layer and same place, so what differs is
// what the game left in the image.
std::atomic<int> g_alphaLow{-1}, g_alphaHigh{-1};
std::atomic<float> g_adviceFov{0};       // vertical fov the game should render with
std::atomic<float> g_fovScale{1.0f};     // trades field of view for pixels per degree
std::atomic<float> g_binocularZoom{1.0f}; // narrow game frustum displayed over the normal XR frustum
std::atomic<float> g_adviceIpd{0};       // metres between the eye positions
std::atomic<bool> g_adviceReady{false};
// The runtime's own frusta, before any scale of ours. Displays are canted, so
// each eye's four angles are lopsided and the two eyes are mirrored; a single
// symmetric image has to reach the widest of them.
// Kept beyond every side of what the runtime reports, in radians.
//
// The vertical has always been exactly what the runtime asked for -- the
// advice is the largest of the four angles and nothing was added -- so the
// bottom of the second eye, which wants every bit of it, had no margin at all
// and a black edge there costs nothing to produce. Widening the rendered field
// puts the extra outside the display, where it is never seen; it is paid for
// in pixels per degree, not in what the player can look at.
std::atomic<float> g_fovMargin{0};
std::atomic<float> g_headsetHalfHorizontal{0},g_headsetHalfVertical{0};
char g_headsetName[128]{};
// The aspect the game's own two-dimensional content was laid out for.
//
// It lays that content into the render rectangle, so once the rectangle is
// narrowed to the headset's horizontal field the content comes out squeezed --
// and a panel sized from the texture's own aspect then shows the squeeze
// instead of undoing it. 2368x2160 made the 2.2m panel 2.007m tall, which is
// the square the player saw. Zero keeps the texture's own aspect.
std::atomic<float> g_contentAspect{0};
float PanelAspect(unsigned width,unsigned height) noexcept {
    const float configured=g_contentAspect.load(std::memory_order_relaxed);
    if(configured>0.2f && configured<8.0f) return configured;
    return height?static_cast<float>(width)/static_cast<float>(height):1.0f;
}
std::atomic<bool> g_warpEnabled{false};
std::atomic<float> g_warpNear{0.1f}, g_warpFar{1000.0f};
std::atomic<float> g_warpSteps{48};
std::atomic<float> g_warpNearest{0.35f};
std::atomic<float> g_nearKnee{1.5f}, g_nearScale{1.0f};
std::atomic<bool> g_warpHudless{true};
std::atomic<float> g_warpDebug{0};
std::atomic<float> g_warpEyeScale{1.0f};
wchar_t g_dumpDirectory[MAX_PATH]{};
std::atomic<unsigned long long> g_warpedEyes{0};
XrPosef g_eyePose[2]{};                  // last located eye poses, in the reference space
bool g_eyePoseValid=false;
// The pose each eye's image was actually drawn from. In stereo one eye is a
// frame behind, and telling the runtime it came from this frame's pose is what
// makes a stale eye reproject wrongly.
XrPosef g_writtenPose[2]{};
// The located pose as it stood when the game was given its camera. That is
// the pose the image about to be submitted was actually drawn from; the one
// current at submission is a whole frame newer, and declaring that makes the
// runtime reproject from somewhere the image never was. The rotation part of
// that error is uniform, but the translation part falls off with distance,
// which is why it shows on near things and not on far ones.
SRWLOCK g_poseLock=SRWLOCK_INIT;
XrPosef g_cameraPose[2]{};
bool g_cameraPoseValid=false;
bool g_writtenPoseValid[2]{};
// Whether each eye's swapchain holds anything worth showing. A frame that
// cannot produce a new image reshows these rather than submitting no layer at
// all, which is a black flash in the headset.
bool g_haveImage[2]{};
std::atomic<unsigned long long> g_reshownFrames{0};
std::atomic<unsigned long long> g_eyeRepeats{0};
std::atomic<bool> g_eyeConsumed{true};
int g_lastCopiedEye=-2;
std::atomic<unsigned long long> g_projectionLayers{0};
std::atomic<unsigned long long> g_layers{0};
std::atomic<bool> g_sessionRunning{false};
SRWLOCK g_frameLock=SRWLOCK_INIT;        // Start/Stop against the render thread
// Owned by the Present thread under g_frameLock. No lock is held while EDF draws.
XrFrameState g_preparedFrame{XR_TYPE_FRAME_STATE};
bool g_displayFramePrepared=false;
DWORD g_displayOwnerThread=0;
std::atomic<bool> g_displayStopPending{false};
ID3D11Query* g_stopFence=nullptr;
bool g_exitRequested=false, g_stopWarning=false;
ULONGLONG g_stopStarted=0;
float g_quadDistance=2.0f, g_quadWidth=2.6f;

std::atomic<bool> g_stop{false};
std::atomic<bool> g_running{false};
std::atomic<bool> g_stage{false};
std::atomic<unsigned long long> g_frames{0};
std::thread g_thread;
SRWLOCK g_sampleLock=SRWLOCK_INIT;
HmdSample g_sample{};
char g_status[256]="not started";
VrLogger g_log=nullptr;

void SetStatus(const char* text) noexcept {
    strncpy_s(g_status,text,_TRUNCATE);
    if(g_log) g_log(g_status);
}

void SetStatusf(const char* format,...) noexcept {
    char text[256]{};
    va_list args; va_start(args,format); vsnprintf(text,sizeof(text),format,args); va_end(args);
    SetStatus(text);
}

template<typename T> bool Load(T& slot,const char* name) noexcept {
    PFN_xrVoidFunction fn=nullptr;
    if(XR_FAILED(g_api.getProc(g_instance,name,&fn)) || !fn) return false;
    slot=reinterpret_cast<T>(fn);
    return true;
}

// The runtime manifest is a small JSON object; only library_path is needed here.
bool ReadLibraryPath(const std::wstring& manifest,std::wstring& library) noexcept {
    FILE* file=nullptr;
    if(_wfopen_s(&file,manifest.c_str(),L"rb") || !file) return false;
    std::string text;
    char buffer[1024];
    for(size_t n=fread(buffer,1,sizeof(buffer),file); n; n=fread(buffer,1,sizeof(buffer),file))
        text.append(buffer,n);
    fclose(file);
    const auto key=text.find("\"library_path\"");
    if(key==std::string::npos) return false;
    const auto colon=text.find(':',key);
    if(colon==std::string::npos) return false;
    const auto open=text.find('"',colon+1);
    if(open==std::string::npos) return false;
    std::string value;
    for(size_t i=open+1;i<text.size();++i) {
        if(text[i]=='\\' && i+1<text.size()) { value.push_back(text[++i]); continue; }
        if(text[i]!='"') { value.push_back(text[i]); continue; }
        if(value.empty()) return false;
        const int length=MultiByteToWideChar(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),nullptr,0);
        if(length<=0) return false;
        library.assign(static_cast<size_t>(length),L'\0');
        MultiByteToWideChar(CP_UTF8,0,value.c_str(),static_cast<int>(value.size()),&library[0],length);
        // A relative library_path resolves against the manifest directory.
        if(library.size()>1 && library[1]!=L':' && library[0]!=L'\\') {
            const auto slash=manifest.find_last_of(L"\\/");
            if(slash!=std::wstring::npos) library=manifest.substr(0,slash+1)+library;
        }
        return true;
    }
    return false;
}

bool ActiveRuntimeManifest(std::wstring& path) noexcept {
    wchar_t fromEnv[MAX_PATH]{};
    const DWORD length=GetEnvironmentVariableW(L"XR_RUNTIME_JSON",fromEnv,MAX_PATH);
    if(length>=MAX_PATH) return false;
    if(length) { path=fromEnv; return true; }
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Khronos\\OpenXR\\1",0,KEY_READ|KEY_WOW64_64KEY,&key)) return false;
    wchar_t value[MAX_PATH]{};
    DWORD size=sizeof(value), type=0;
    const LSTATUS status=RegQueryValueExW(key,L"ActiveRuntime",nullptr,&type,reinterpret_cast<LPBYTE>(value),&size);
    RegCloseKey(key);
    if(status || (type!=REG_SZ && type!=REG_EXPAND_SZ) || !value[0]) return false;
    if(size>sizeof(value) || size<sizeof(wchar_t) || value[size/sizeof(wchar_t)-1]!=0) return false;
    if(type==REG_EXPAND_SZ) {
        const DWORD expanded=ExpandEnvironmentStringsW(value,fromEnv,MAX_PATH);
        if(!expanded || expanded>MAX_PATH) return false;
        path=fromEnv;
    } else path=value;
    return true;
}

bool NegotiateRuntime() noexcept {
    std::wstring manifest, library;
    if(!ActiveRuntimeManifest(manifest)) { SetStatus("no OpenXR ActiveRuntime registered"); return false; }
    if(!ReadLibraryPath(manifest,library)) { SetStatus("OpenXR manifest has no library_path"); return false; }
    g_runtimeModule=LoadLibraryExW(library.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!g_runtimeModule) { SetStatusf("LoadLibrary failed for the OpenXR runtime (error %lu)",GetLastError()); return false; }
    auto negotiate=reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(
        reinterpret_cast<void*>(GetProcAddress(g_runtimeModule,"xrNegotiateLoaderRuntimeInterface")));
    if(!negotiate) { SetStatus("runtime exports no xrNegotiateLoaderRuntimeInterface"); return false; }
    XrNegotiateLoaderInfo info{};
    info.structType=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    info.structVersion=XR_LOADER_INFO_STRUCT_VERSION;
    info.structSize=sizeof(info);
    info.minInterfaceVersion=1;
    info.maxInterfaceVersion=XR_CURRENT_LOADER_RUNTIME_VERSION;
    info.minApiVersion=XR_MAKE_VERSION(1,0,0);
    info.maxApiVersion=XR_MAKE_VERSION(1,1,0xfff);
    XrNegotiateRuntimeRequest request{};
    request.structType=XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    request.structVersion=XR_RUNTIME_INFO_STRUCT_VERSION;
    request.structSize=sizeof(request);
    const XrResult result=negotiate(&info,&request);
    if(XR_FAILED(result) || !request.getInstanceProcAddr) {
        SetStatusf("runtime negotiation failed (%d)",static_cast<int>(result));
        return false;
    }
    g_api.getProc=request.getInstanceProcAddr;
    g_runtimeApiVersion=request.runtimeApiVersion;
    char narrow[MAX_PATH]{};
    WideCharToMultiByte(CP_UTF8,0,library.c_str(),-1,narrow,sizeof(narrow)-1,nullptr,nullptr);
    SetStatusf("runtime negotiated: %s (api %u.%u)",narrow,
        static_cast<unsigned>(XR_VERSION_MAJOR(g_runtimeApiVersion)),
        static_cast<unsigned>(XR_VERSION_MINOR(g_runtimeApiVersion)));
    return true;
}

bool HasD3D11Extension() noexcept {
    PFN_xrEnumerateInstanceExtensionProperties enumerate=nullptr;
    if(!Load(enumerate,"xrEnumerateInstanceExtensionProperties")) return true;   // let creation decide
    uint32_t count=0;
    if(XR_FAILED(enumerate(nullptr,0,&count,nullptr)) || !count || count>256) return true;
    auto properties=new(std::nothrow) XrExtensionProperties[count];
    if(!properties) return true;
    for(uint32_t i=0;i<count;++i) properties[i]=XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES};
    bool found=false;
    if(XR_SUCCEEDED(enumerate(nullptr,count,&count,properties))) {
        char list[512]{}; int at=0;
        for(uint32_t i=0;i<count;++i) {
            if(!strcmp(properties[i].extensionName,XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) found=true;
            // Submitting depth would let the runtime reproject positionally rather
            // than as though everything were at infinity, which is what near things
            // need; whether it is on offer decides if that is worth building.
            if(strstr(properties[i].extensionName,"depth") || strstr(properties[i].extensionName,"space_warp")) {
                if(at>=0 && at<static_cast<int>(sizeof(list)))
                    at+=std::snprintf(list+at,sizeof(list)-static_cast<std::size_t>(at)," %s",
                                      properties[i].extensionName);
            }
        }
        SetStatusf("runtime offers %u extensions, of interest:%s",count,at?list:" none");
    }
    delete[] properties;
    if(!found) SetStatus("the active OpenXR runtime does not offer XR_KHR_D3D11_enable");
    return found;
}

bool CreateInstance() noexcept {
    if(!Load(g_api.createInstance,"xrCreateInstance")) { SetStatus("xrCreateInstance unavailable"); return false; }
    if(!HasD3D11Extension()) return false;
    const char* extensions[]={XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    create.enabledExtensionCount=1;
    create.enabledExtensionNames=extensions;
    strcpy_s(create.applicationInfo.applicationName,"EDF6VR");
    strcpy_s(create.applicationInfo.engineName,"EDF6VR");
    create.applicationInfo.applicationVersion=1;
    create.applicationInfo.engineVersion=1;
    // Nothing here needs an OpenXR 1.1 feature, so ask for the widest-supported
    // version first and only fall back to whatever the runtime announced.
    create.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    XrResult result=g_api.createInstance(&create,&g_instance);
    const XrResult firstAttempt=result;
    if(XR_FAILED(result) && XR_VERSION_MAJOR(g_runtimeApiVersion)==1
       && g_runtimeApiVersion!=create.applicationInfo.apiVersion) {
        SetStatusf("xrCreateInstance failed (%d) at api 1.0; retrying at %u.%u.%u",
            static_cast<int>(result),
            static_cast<unsigned>(XR_VERSION_MAJOR(g_runtimeApiVersion)),
            static_cast<unsigned>(XR_VERSION_MINOR(g_runtimeApiVersion)),
            static_cast<unsigned>(XR_VERSION_PATCH(g_runtimeApiVersion)));
        create.applicationInfo.apiVersion=g_runtimeApiVersion;
        result=g_api.createInstance(&create,&g_instance);
    }
    if(XR_FAILED(result)) {
        // Report the plain 1.0 attempt: the fallback's own error is rarely the cause.
        SetStatusf("xrCreateInstance failed (%d); connect the headset to the selected OpenXR runtime first",
            static_cast<int>(firstAttempt));
        return false;
    }
    const bool loaded=
        Load(g_api.destroyInstance,"xrDestroyInstance") && Load(g_api.getSystem,"xrGetSystem")
        && Load(g_api.createSession,"xrCreateSession") && Load(g_api.destroySession,"xrDestroySession")
        && Load(g_api.createSpace,"xrCreateReferenceSpace") && Load(g_api.destroySpace,"xrDestroySpace")
        && Load(g_api.enumerateSpaces,"xrEnumerateReferenceSpaces") && Load(g_api.pollEvent,"xrPollEvent")
        && Load(g_api.beginSession,"xrBeginSession") && Load(g_api.endSession,"xrEndSession")
        && Load(g_api.requestExitSession,"xrRequestExitSession")
        && Load(g_api.waitFrame,"xrWaitFrame") && Load(g_api.beginFrame,"xrBeginFrame")
        && Load(g_api.endFrame,"xrEndFrame") && Load(g_api.locateSpace,"xrLocateSpace")
        && Load(g_api.enumerateFormats,"xrEnumerateSwapchainFormats")
        && Load(g_api.createSwapchain,"xrCreateSwapchain")
        && Load(g_api.destroySwapchain,"xrDestroySwapchain")
        && Load(g_api.enumerateImages,"xrEnumerateSwapchainImages")
        && Load(g_api.acquireImage,"xrAcquireSwapchainImage")
        && Load(g_api.waitImage,"xrWaitSwapchainImage")
        && Load(g_api.releaseImage,"xrReleaseSwapchainImage")
        && Load(g_api.locateViews,"xrLocateViews");
    if(!loaded) { SetStatus("runtime is missing a core OpenXR entry point"); return false; }
    PFN_xrGetInstanceProperties properties=nullptr;
    if(Load(properties,"xrGetInstanceProperties")) {
        XrInstanceProperties described{XR_TYPE_INSTANCE_PROPERTIES};
        if(XR_SUCCEEDED(properties(g_instance,&described)))
            SetStatusf("OpenXR instance created; runtime=%s %u.%u.%u",described.runtimeName,
                static_cast<unsigned>(XR_VERSION_MAJOR(described.runtimeVersion)),
                static_cast<unsigned>(XR_VERSION_MINOR(described.runtimeVersion)),
                static_cast<unsigned>(XR_VERSION_PATCH(described.runtimeVersion)));
    }
    return true;
}

// Which GPU and driver the picture is made on: a picture problem seen on one PC
// only (2026-09-29, double vision on Meta Link and Steam Link) starts here.
void LogAdapter(IDXGIAdapter* adapter) noexcept {
    if(!adapter || !g_log) return;
    DXGI_ADAPTER_DESC desc{};
    if(FAILED(adapter->GetDesc(&desc))) return;
    LARGE_INTEGER umd{};
    const bool driver=SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&umd));
    char name[128]{};
    WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof(name),nullptr,nullptr);
    const char* vendor=desc.VendorId==0x10DE?"NVIDIA":desc.VendorId==0x1002?"AMD":desc.VendorId==0x8086?"Intel":"other";
    char line[320]{};
    std::snprintf(line,sizeof(line),"GPU %s vendor=%04X (%s) device=%04X vram=%lluMB driver=%u.%u.%u.%u",
        name,desc.VendorId,vendor,desc.DeviceId,static_cast<unsigned long long>(desc.DedicatedVideoMemory>>20),
        driver?HIWORD(umd.HighPart):0u,driver?LOWORD(umd.HighPart):0u,driver?HIWORD(umd.LowPart):0u,driver?LOWORD(umd.LowPart):0u);
    g_log(line);
}

bool CreateDevice(const LUID& adapterLuid,D3D_FEATURE_LEVEL minimum) noexcept {
    IDXGIFactory1* factory=nullptr;
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory))) || !factory) {
        SetStatus("CreateDXGIFactory1 failed");
        return false;
    }
    IDXGIAdapter1* adapter=nullptr;
    for(UINT i=0;;++i) {
        IDXGIAdapter1* candidate=nullptr;
        if(factory->EnumAdapters1(i,&candidate)!=S_OK) break;
        DXGI_ADAPTER_DESC1 desc{};
        if(SUCCEEDED(candidate->GetDesc1(&desc))
           && desc.AdapterLuid.LowPart==adapterLuid.LowPart && desc.AdapterLuid.HighPart==adapterLuid.HighPart) {
            adapter=candidate;
            break;
        }
        candidate->Release();
    }
    factory->Release();
    if(!adapter) { SetStatus("the D3D11 adapter requested by the runtime was not found"); return false; }
    LogAdapter(adapter);
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL achieved{};
    const HRESULT hr=D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,levels,
        static_cast<UINT>(std::size(levels)),D3D11_SDK_VERSION,&g_device,&achieved,&g_context);
    adapter->Release();
    if(FAILED(hr) || !g_device) {
        SetStatusf("D3D11CreateDevice failed (0x%08lX)",static_cast<unsigned long>(hr));
        return false;
    }
    if(achieved<minimum) { SetStatus("D3D11 feature level below the runtime minimum"); return false; }
    return true;
}

// In HeadsetDisplay the session must sit on the device the game renders with,
// otherwise its textures cannot be copied without sharing. Refuse if that
// device is on a different adapter than the runtime asked for.
bool AdoptGameDevice(const LUID& adapterLuid) noexcept {
    if(!g_device) { SetStatus("no game device to adopt"); return false; }
    IDXGIDevice* dxgiDevice=nullptr;
    if(FAILED(g_device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(&dxgiDevice))) || !dxgiDevice) {
        SetStatus("the game device is not a DXGI device");
        return false;
    }
    IDXGIAdapter* adapter=nullptr;
    const HRESULT hr=dxgiDevice->GetAdapter(&adapter);
    dxgiDevice->Release();
    if(FAILED(hr) || !adapter) { SetStatus("the game device has no adapter"); return false; }
    DXGI_ADAPTER_DESC desc{};
    const bool described=SUCCEEDED(adapter->GetDesc(&desc));
    LogAdapter(adapter);
    adapter->Release();
    if(!described) { SetStatus("the game adapter could not be described"); return false; }
    if(desc.AdapterLuid.LowPart!=adapterLuid.LowPart || desc.AdapterLuid.HighPart!=adapterLuid.HighPart) {
        SetStatus("the game renders on a different adapter than the headset; headset display unavailable");
        return false;
    }
    return true;
}

bool LoadInputApi() noexcept {
    if(!g_runtimeModule || !g_instance) return false;
    auto get=[](const char* name,PFN_xrVoidFunction* out) {
        return XR_SUCCEEDED(g_api.getProc(g_instance,name,out)) && *out;
    };
    return get("xrStringToPath",reinterpret_cast<PFN_xrVoidFunction*>(&g_api.stringToPath))
        && get("xrCreateActionSet",reinterpret_cast<PFN_xrVoidFunction*>(&g_api.createActionSet))
        && get("xrDestroyActionSet",reinterpret_cast<PFN_xrVoidFunction*>(&g_api.destroyActionSet))
        && get("xrCreateAction",reinterpret_cast<PFN_xrVoidFunction*>(&g_api.createAction))
        && get("xrSuggestInteractionProfileBindings",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.suggestBindings))
        && get("xrAttachSessionActionSets",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.attachActionSets))
        && get("xrCreateActionSpace",reinterpret_cast<PFN_xrVoidFunction*>(&g_api.createActionSpace))
        && get("xrSyncActions",reinterpret_cast<PFN_xrVoidFunction*>(&g_api.syncActions))
        && get("xrGetActionStatePose",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.getActionStatePose))
        && get("xrGetActionStateFloat",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.getActionStateFloat))
        && get("xrGetActionStateBoolean",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.getActionStateBoolean))
        && get("xrGetActionStateVector2f",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.getActionStateVector2f))
        && get("xrApplyHapticFeedback",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.applyHaptic))
        && get("xrStopHapticFeedback",
               reinterpret_cast<PFN_xrVoidFunction*>(&g_api.stopHaptic));
}

// Everything here is best effort. A runtime with no controllers, or a headset
// with them switched off, must not stop the game being displayed.
void ReportInputProfiles() noexcept {
    // The runtime chooses the matching suggested profile, including controller
    // reconnection. Never infer controllers from the headset's marketing name.
    if(!g_api.getProc || !g_session) return;
    PFN_xrGetCurrentInteractionProfile current=nullptr;
    PFN_xrPathToString describe=nullptr;
    if(!Load(current,"xrGetCurrentInteractionProfile") || !Load(describe,"xrPathToString")) return;
    for(unsigned hand=0;hand<2;++hand) {
        XrInteractionProfileState state{XR_TYPE_INTERACTION_PROFILE_STATE};
        if(!g_handPath[hand] || XR_FAILED(current(g_session,g_handPath[hand],&state))) continue;
        char path[XR_MAX_PATH_LENGTH]="not active yet";uint32_t written=0;
        if(state.interactionProfile && XR_FAILED(describe(g_instance,state.interactionProfile,
            static_cast<uint32_t>(sizeof(path)),&written,path))) strcpy_s(path,"unknown profile");
        SetStatusf("OpenXR controller %s: %s (bindings selected by runtime)",hand?"right":"left",path);
        if(hand==1 && state.interactionProfile) NoteControllerKind(path);
    }
    if(g_log && g_hapticAction) {
        PFN_xrEnumerateBoundSourcesForAction sources=nullptr;
        if(Load(sources,"xrEnumerateBoundSourcesForAction")) {
            XrBoundSourcesForActionEnumerateInfo info{XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO};
            info.action=g_hapticAction;
            XrPath bound[16]{};uint32_t count=0;
            const auto result=sources(g_session,&info,16,&count,bound);
            char line[256]{};
            std::snprintf(line,sizeof(line),"HAPTIC bindings result=%d count=%u",static_cast<int>(result),count);g_log(line);
            if(result==XR_SUCCESS)for(uint32_t i=0;i<count && i<16;++i) {
                char path[XR_MAX_PATH_LENGTH]{};uint32_t written=0;
                if(describe(g_instance,bound[i],sizeof(path),&written,path)==XR_SUCCESS) {
                    std::snprintf(line,sizeof(line),"HAPTIC bound source=%s",path);g_log(line);
                }
            }
        }
    }
}
bool CreateInput() noexcept {
    if(g_inputReady) return true;
    if(!LoadInputApi()) { std::snprintf(g_inputNote,sizeof(g_inputNote),"no input entry points"); return false; }
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy_s(setInfo.actionSetName,"gameplay");
    strcpy_s(setInfo.localizedActionSetName,"Gameplay");
    if(XR_FAILED(g_api.createActionSet(g_instance,&setInfo,&g_actionSet))) {
        std::snprintf(g_inputNote,sizeof(g_inputNote),"action set failed");
        return false;
    }
    const char* hands[2]={"/user/hand/left","/user/hand/right"};
    for(int i=0;i<2;++i)
        if(XR_FAILED(g_api.stringToPath(g_instance,hands[i],&g_handPath[i]))) {
            std::snprintf(g_inputNote,sizeof(g_inputNote),"hand paths failed");
            return false;
        }
    auto makeAction=[&](XrActionType type,const char* name,const char* label,XrAction& action) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType=type;
        strcpy_s(info.actionName,name);
        strcpy_s(info.localizedActionName,label);
        info.countSubactionPaths=2;
        info.subactionPaths=g_handPath;
        return XR_SUCCEEDED(g_api.createAction(g_actionSet,&info,&action));
    };
    auto makePose=[&](const char* name,const char* label,XrAction& action) {
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
        info.actionType=XR_ACTION_TYPE_POSE_INPUT;
        strcpy_s(info.actionName,name);
        strcpy_s(info.localizedActionName,label);
        info.countSubactionPaths=2;
        info.subactionPaths=g_handPath;
        return XR_SUCCEEDED(g_api.createAction(g_actionSet,&info,&action));
    };
    if(!makePose("aim_pose","Aim Pose",g_aimAction) || !makePose("grip_pose","Grip Pose",g_gripAction)) {
        std::snprintf(g_inputNote,sizeof(g_inputNote),"pose actions failed");
        return false;
    }
    if(!makeAction(XR_ACTION_TYPE_FLOAT_INPUT,"trigger","Trigger",g_triggerAction)
       || !makeAction(XR_ACTION_TYPE_FLOAT_INPUT,"squeeze","Grip",g_squeezeAction)
       || !makeAction(XR_ACTION_TYPE_FLOAT_INPUT,"squeeze_force","Grip Force",g_squeezeForceAction)
       || !makeAction(XR_ACTION_TYPE_VECTOR2F_INPUT,"stick","Stick",g_stickAction)
       || !makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT,"stick_click","Stick Click",g_stickClickAction)
       || !makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT,"stick_touch","Stick Touch",g_stickTouchAction)
       || !makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT,"lower","Lower Button",g_lowerAction)
       || !makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT,"upper","Upper Button",g_upperAction)
       || !makeAction(XR_ACTION_TYPE_BOOLEAN_INPUT,"menu","Menu",g_menuAction)
       || !makeAction(XR_ACTION_TYPE_VIBRATION_OUTPUT,"haptic","Haptic",g_hapticAction)) {
        std::snprintf(g_inputNote,sizeof(g_inputNote),"actions failed");
        return false;
    }
    // The same two bindings for every profile the runtime might report. A
    // profile it does not know is refused on its own and the rest still stand.
    // One list per profile. A profile refuses the whole suggestion if any single
    // path is not one of its own, so they cannot share a list: Index has a and b
    // on both hands, Touch has x and y on the left, Vive has a trackpad and no
    // stick at all, and the simple controller has almost nothing.
    struct Binding { XrAction action; const char* path; };
    struct Profile { const char* name; const Binding* bindings; int count; };

    const Binding common[]={
        {g_aimAction,"/user/hand/left/input/aim/pose"},
        {g_aimAction,"/user/hand/right/input/aim/pose"},
        {g_gripAction,"/user/hand/left/input/grip/pose"},
        {g_gripAction,"/user/hand/right/input/grip/pose"},
        {g_hapticAction,"/user/hand/left/output/haptic"},
        {g_hapticAction,"/user/hand/right/output/haptic"},
    };
    auto build=[&](Binding* into,const Binding* extra,int extraCount) {
        int n=0;
        for(const auto& one:common) into[n++]=one;
        for(int i=0;i<extraCount;++i) into[n++]=extra[i];
        return n;
    };
    const Binding index[]={
        {g_stickTouchAction,"/user/hand/left/input/thumbstick/touch"},
        {g_stickTouchAction,"/user/hand/right/input/thumbstick/touch"},
        {g_triggerAction,"/user/hand/left/input/trigger/value"},
        {g_triggerAction,"/user/hand/right/input/trigger/value"},
        {g_squeezeAction,"/user/hand/left/input/squeeze/value"},
        {g_squeezeAction,"/user/hand/right/input/squeeze/value"},
        {g_stickAction,"/user/hand/left/input/thumbstick"},
        {g_stickAction,"/user/hand/right/input/thumbstick"},
        {g_stickClickAction,"/user/hand/left/input/thumbstick/click"},
        {g_stickClickAction,"/user/hand/right/input/thumbstick/click"},
        {g_lowerAction,"/user/hand/left/input/a/click"},
        {g_lowerAction,"/user/hand/right/input/a/click"},
        {g_upperAction,"/user/hand/left/input/b/click"},
        {g_upperAction,"/user/hand/right/input/b/click"},
        {g_squeezeForceAction,"/user/hand/left/input/squeeze/force"},
        {g_squeezeForceAction,"/user/hand/right/input/squeeze/force"},
    };
    const Binding touch[]={
        {g_stickTouchAction,"/user/hand/left/input/thumbstick/touch"},
        {g_stickTouchAction,"/user/hand/right/input/thumbstick/touch"},
        {g_triggerAction,"/user/hand/left/input/trigger/value"},
        {g_triggerAction,"/user/hand/right/input/trigger/value"},
        {g_squeezeAction,"/user/hand/left/input/squeeze/value"},
        {g_squeezeAction,"/user/hand/right/input/squeeze/value"},
        {g_stickAction,"/user/hand/left/input/thumbstick"},
        {g_stickAction,"/user/hand/right/input/thumbstick"},
        {g_stickClickAction,"/user/hand/left/input/thumbstick/click"},
        {g_stickClickAction,"/user/hand/right/input/thumbstick/click"},
        {g_lowerAction,"/user/hand/left/input/x/click"},
        {g_lowerAction,"/user/hand/right/input/a/click"},
        {g_upperAction,"/user/hand/left/input/y/click"},
        {g_upperAction,"/user/hand/right/input/b/click"},
        {g_menuAction,"/user/hand/left/input/menu/click"},
    };
    const Binding wmr[]={
        {g_triggerAction,"/user/hand/left/input/trigger/value"},
        {g_triggerAction,"/user/hand/right/input/trigger/value"},
        {g_squeezeAction,"/user/hand/left/input/squeeze/click"},
        {g_squeezeAction,"/user/hand/right/input/squeeze/click"},
        {g_stickAction,"/user/hand/left/input/thumbstick"},
        {g_stickAction,"/user/hand/right/input/thumbstick"},
        {g_stickClickAction,"/user/hand/left/input/thumbstick/click"},
        {g_stickClickAction,"/user/hand/right/input/thumbstick/click"},
        {g_menuAction,"/user/hand/left/input/menu/click"},
        {g_menuAction,"/user/hand/right/input/menu/click"},
    };
    const Binding vive[]={
        {g_triggerAction,"/user/hand/left/input/trigger/value"},
        {g_triggerAction,"/user/hand/right/input/trigger/value"},
        {g_squeezeAction,"/user/hand/left/input/squeeze/click"},
        {g_squeezeAction,"/user/hand/right/input/squeeze/click"},
        {g_stickAction,"/user/hand/left/input/trackpad"},
        {g_stickAction,"/user/hand/right/input/trackpad"},
        {g_stickClickAction,"/user/hand/left/input/trackpad/click"},
        {g_stickClickAction,"/user/hand/right/input/trackpad/click"},
        {g_menuAction,"/user/hand/left/input/menu/click"},
        {g_menuAction,"/user/hand/right/input/menu/click"},
    };
    const Binding simple[]={
        {g_triggerAction,"/user/hand/left/input/select/click"},
        {g_triggerAction,"/user/hand/right/input/select/click"},
        {g_menuAction,"/user/hand/left/input/menu/click"},
        {g_menuAction,"/user/hand/right/input/menu/click"},
    };
    const Profile profiles[]={
        {"/interaction_profiles/valve/index_controller",index,static_cast<int>(std::size(index))},
        {"/interaction_profiles/oculus/touch_controller",touch,static_cast<int>(std::size(touch))},
        {"/interaction_profiles/microsoft/motion_controller",wmr,static_cast<int>(std::size(wmr))},
        {"/interaction_profiles/htc/vive_controller",vive,static_cast<int>(std::size(vive))},
        {"/interaction_profiles/khr/simple_controller",simple,static_cast<int>(std::size(simple))},
    };
    int suggested=0;
    for(const auto& profile:profiles) {
        XrPath profilePath{};
        if(XR_FAILED(g_api.stringToPath(g_instance,profile.name,&profilePath))) continue;
        Binding all[32]{};
        const int count=build(all,profile.bindings,profile.count);
        XrActionSuggestedBinding bindings[32]{};
        bool ok=true;
        for(int i=0;i<count;++i) {
            bindings[i].action=all[i].action;
            if(XR_FAILED(g_api.stringToPath(g_instance,all[i].path,&bindings[i].binding))) ok=false;
        }
        if(!ok) continue;
        XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggest.interactionProfile=profilePath;
        suggest.countSuggestedBindings=static_cast<uint32_t>(count);
        suggest.suggestedBindings=bindings;
        if(XR_SUCCEEDED(g_api.suggestBindings(g_instance,&suggest))) ++suggested;
    }
    if(!suggested) { std::snprintf(g_inputNote,sizeof(g_inputNote),"no profile took the bindings"); return false; }
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets=1;
    attach.actionSets=&g_actionSet;
    const XrResult attached=g_api.attachActionSets(g_session,&attach);
    if(XR_FAILED(attached)) {
        std::snprintf(g_inputNote,sizeof(g_inputNote),"attach failed (%d)",static_cast<int>(attached));
        return false;
    }
    for(int i=0;i<2;++i) {
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action=g_aimAction;
        spaceInfo.subactionPath=g_handPath[i];
        spaceInfo.poseInActionSpace.orientation.w=1;
        if(XR_FAILED(g_api.createActionSpace(g_session,&spaceInfo,&g_aimSpace[i]))) g_aimSpace[i]=XR_NULL_HANDLE;
        spaceInfo.action=g_gripAction;
        if(XR_FAILED(g_api.createActionSpace(g_session,&spaceInfo,&g_gripSpace[i]))) g_gripSpace[i]=XR_NULL_HANDLE;
    }
    g_inputReady=true;
    std::snprintf(g_inputNote,sizeof(g_inputNote),"ready, %d profiles",suggested);
    ReportInputProfiles();
    return true;
}

// Producers never call XR or wait for the frame owner. This independent lock
// protects only two small mailboxes/counters: no XR, game locks or I/O under it.
struct HapticRequest { bool pending=false,stop=false; ULONGLONG until=0; float amplitude=0; };
struct HapticCounters {
    unsigned requested=0,cancelled=0,frameBusy=0,rejected=0,replaced=0,expired=0;
    unsigned applied=0,stopped=0,ok=0,notFocused=0,failed=0,other=0,unavailable=0;
    int applyResult=0,stopResult=0,syncResult=0;
    unsigned applyHand[2]{},stopHand[2]{};
    int resultHand[2]{};
    unsigned gate=0;
};
SRWLOCK g_hapticLock=SRWLOCK_INIT;
HapticRequest g_hapticPending[2]{};
HapticCounters g_hapticStats{};
ULONGLONG g_hapticNextLog=0;
void ClearHapticRequests() noexcept {
    AcquireSRWLockExclusive(&g_hapticLock);
    for(auto& request:g_hapticPending)request={};
    ReleaseSRWLockExclusive(&g_hapticLock);
}
void QueueHaptic(int hand,float seconds,float amplitude,bool stop) noexcept {
    // Measure the old drop condition, without sending on this thread. A busy
    // render lock is now diagnostic only; it cannot discard a cancellation.
    const bool free=TryAcquireSRWLockShared(&g_frameLock)!=0;
    if(free)ReleaseSRWLockShared(&g_frameLock);
    const auto now=GetTickCount64();
    AcquireSRWLockExclusive(&g_hapticLock);
    auto& stats=g_hapticStats;
    if(stop)++stats.cancelled;else ++stats.requested;
    if(!free)++stats.frameBusy;
    const unsigned gate=(!g_running.load()?1u:0u)|(g_stop.load()?2u:0u)|(!g_inputReady.load()?4u:0u);
    stats.gate|=gate;
    if(gate)++stats.rejected;
    else {
        auto& request=g_hapticPending[hand];
        if(request.pending)++stats.replaced;
        request={true,stop,now+static_cast<ULONGLONG>(std::ceil(std::min(seconds,5.f)*1000)),std::min(amplitude,1.f)};
    }
    ReleaseSRWLockExclusive(&g_hapticLock);
}
// Only the XR frame owner calls this (display Present or tracking worker).
// Tracking teardown joins that worker; display teardown owns g_frameLock.
void FlushHaptics(XrResult synced,ULONGLONG now) noexcept {
    HapticRequest pending[2]{};
    AcquireSRWLockExclusive(&g_hapticLock);
    for(int hand=0;hand<2;++hand){pending[hand]=g_hapticPending[hand];g_hapticPending[hand]={};}
    g_hapticStats.syncResult=static_cast<int>(synced);
    ReleaseSRWLockExclusive(&g_hapticLock);
    for(int hand=0;hand<2;++hand) {
        const auto& request=pending[hand];
        if(!request.pending)continue;
        const bool expired=!request.stop && now>=request.until;
        const unsigned gate=(!g_running.load()?1u:0u)|(g_stop.load()?2u:0u)|(!g_inputReady.load()?4u:0u)
            |(!g_session?8u:0u)|(!g_hapticAction?16u:0u)|(!g_handPath[hand]?32u:0u)
            |((request.stop?!g_api.stopHaptic:!g_api.applyHaptic)?64u:0u)|(!g_sessionRunning.load()?128u:0u);
        const bool available=gate==0;
        XrResult result=XR_SUCCESS;
        if(!expired && available) {
            XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
            info.action=g_hapticAction;info.subactionPath=g_handPath[hand];
            XrHapticVibration buzz{XR_TYPE_HAPTIC_VIBRATION};
            buzz.duration=request.stop?0:static_cast<XrDuration>(request.until-now)*1000000;
            buzz.frequency=XR_FREQUENCY_UNSPECIFIED;buzz.amplitude=request.amplitude;
            result=request.stop?g_api.stopHaptic(g_session,&info):
                g_api.applyHaptic(g_session,&info,reinterpret_cast<const XrHapticBaseHeader*>(&buzz));
        }
        AcquireSRWLockExclusive(&g_hapticLock);
        auto& stats=g_hapticStats;
        stats.gate|=gate;
        if(expired)++stats.expired;
        else if(!available)++stats.unavailable;
        else {
            if(request.stop){++stats.stopped;++stats.stopHand[hand];stats.stopResult=static_cast<int>(result);}
            else {++stats.applied;++stats.applyHand[hand];stats.applyResult=static_cast<int>(result);}
            stats.resultHand[hand]=static_cast<int>(result);
            // NOT_FOCUSED is positive: XR_SUCCEEDED alone is not delivery.
            if(result==XR_SUCCESS)++stats.ok;
            else if(result==XR_SESSION_NOT_FOCUSED)++stats.notFocused;
            else if(XR_FAILED(result))++stats.failed;
            else ++stats.other;
        }
        ReleaseSRWLockExclusive(&g_hapticLock);
    }
    if(!g_log || now<g_hapticNextLog)return;
    HapticCounters stats{};
    AcquireSRWLockExclusive(&g_hapticLock);
    stats=g_hapticStats;g_hapticStats={};
    ReleaseSRWLockExclusive(&g_hapticLock);
    if(!stats.requested && !stats.cancelled && !stats.applied && !stats.stopped
        && !stats.expired && !stats.unavailable)return;
    g_hapticNextLog=now+1000;
    char line[512]{};
    std::snprintf(line,sizeof(line),"HAPTIC requests=%u cancel=%u frameBusy=%u rejected=%u gate=%u replaced=%u expired=%u unavailable=%u apply=%u stop=%u ok=%u notFocused=%u failed=%u other=%u resultApply=%d resultStop=%d sync=%d state=%d ownerTid=%lu leftApplyStopResult=%u,%u,%d rightApplyStopResult=%u,%u,%d",
        stats.requested,stats.cancelled,stats.frameBusy,stats.rejected,stats.gate,stats.replaced,stats.expired,stats.unavailable,
        stats.applied,stats.stopped,stats.ok,stats.notFocused,stats.failed,stats.other,stats.applyResult,stats.stopResult,stats.syncResult,g_sessionState.load(),GetCurrentThreadId(),
        stats.applyHand[0],stats.stopHand[0],stats.resultHand[0],stats.applyHand[1],stats.stopHand[1],stats.resultHand[1]);
    g_log(line);
}

// Once a frame, while the session is running, after the frame's display time is
// known. Failure here is reported and otherwise ignored.
void ReadHands(XrTime when) noexcept {
    if(!g_inputReady || !when) return;
    XrActiveActionSet active{};
    active.actionSet=g_actionSet;
    active.subactionPath=XR_NULL_PATH;
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets=1;
    sync.activeActionSets=&active;
    const XrResult synced=g_api.syncActions(g_session,&sync);
    FlushHaptics(synced,GetTickCount64());
    if(XR_FAILED(synced)) {
        std::snprintf(g_inputNote,sizeof(g_inputNote),"sync failed (%d)",static_cast<int>(synced));
        return;
    }
    // The rest of the controller, alongside the poses. A control the runtime does
    // not have for this profile simply reports inactive and stays at zero.
    ControllerState controls{};
    for(int hand=0;hand<2;++hand) {
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.subactionPath=g_handPath[hand];
        auto readFloat=[&](XrAction action,float& into) {
            get.action=action;
            XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
            if(XR_SUCCEEDED(g_api.getActionStateFloat(g_session,&get,&state)) && state.isActive) {
                into=state.currentState;
                controls.present[hand]=true;
            }
        };
        auto readBool=[&](XrAction action,bool& into) {
            get.action=action;
            XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
            if(XR_SUCCEEDED(g_api.getActionStateBoolean(g_session,&get,&state)) && state.isActive) {
                into=state.currentState!=XR_FALSE;
                controls.present[hand]=true;
            }
        };
        readFloat(g_triggerAction,controls.trigger[hand]);
        readFloat(g_squeezeAction,controls.squeeze[hand]);
        // Where the controller has a force sensor it decides, because the value
        // beside it answers a different question: how much of the hand is on the
        // grip, not how hard it is being held.
        {
            get.action=g_squeezeForceAction;
            XrActionStateFloat force{XR_TYPE_ACTION_STATE_FLOAT};
            if(XR_SUCCEEDED(g_api.getActionStateFloat(g_session,&get,&force)) && force.isActive) {
                controls.squeeze[hand]=force.currentState;
                controls.squeezeIsForce[hand]=true;
            }
        }
        readBool(g_stickClickAction,controls.stickClick[hand]);
        readBool(g_stickTouchAction,controls.stickTouch[hand]);
        readBool(g_lowerAction,controls.lower[hand]);
        readBool(g_upperAction,controls.upper[hand]);
        readBool(g_menuAction,controls.menu[hand]);
        get.action=g_stickAction;
        XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};
        if(XR_SUCCEEDED(g_api.getActionStateVector2f(g_session,&get,&stick)) && stick.isActive) {
            controls.stick[hand][0]=stick.currentState.x;
            controls.stick[hand][1]=stick.currentState.y;
            controls.present[hand]=true;
        }
    }

    HandTracked palm[2]{};
    for(int i=0;i<2;++i) {
        if(g_gripSpace[i]==XR_NULL_HANDLE) continue;
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action=g_gripAction;
        get.subactionPath=g_handPath[i];
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        if(XR_FAILED(g_api.getActionStatePose(g_session,&get,&state)) || !state.isActive) continue;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if(XR_FAILED(g_api.locateSpace(g_gripSpace[i],g_refSpace,when,&location))) continue;
        const auto wanted=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if((location.locationFlags&wanted)!=wanted) continue;
        palm[i].position[0]=location.pose.position.x;
        palm[i].position[1]=location.pose.position.y;
        palm[i].position[2]=location.pose.position.z;
        palm[i].orientation[0]=location.pose.orientation.x;
        palm[i].orientation[1]=location.pose.orientation.y;
        palm[i].orientation[2]=location.pose.orientation.z;
        palm[i].orientation[3]=location.pose.orientation.w;
        palm[i].tracked=true;
    }

    HandTracked found[2]{};
    for(int i=0;i<2;++i) {
        if(g_aimSpace[i]==XR_NULL_HANDLE) continue;
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action=g_aimAction;
        get.subactionPath=g_handPath[i];
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        if(XR_FAILED(g_api.getActionStatePose(g_session,&get,&state)) || !state.isActive) continue;
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        if(XR_FAILED(g_api.locateSpace(g_aimSpace[i],g_refSpace,when,&location))) continue;
        const auto wanted=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if((location.locationFlags&wanted)!=wanted) continue;
        found[i].position[0]=location.pose.position.x;
        found[i].position[1]=location.pose.position.y;
        found[i].position[2]=location.pose.position.z;
        found[i].orientation[0]=location.pose.orientation.x;
        found[i].orientation[1]=location.pose.orientation.y;
        found[i].orientation[2]=location.pose.orientation.z;
        found[i].orientation[3]=location.pose.orientation.w;
        found[i].tracked=true;
    }
    // A script, when there is one, has the last word: it exists so a hand can
    // be put somewhere exact without anyone wearing a headset.
    for(int i=0;i<2;++i) {
        bool gavePosition=false,gaveTurn=false;
        float position[3]{},orientation[4]{0,0,0,1};
        if(!ScriptHand(i,position,orientation,gavePosition,gaveTurn)) continue;
        for(auto* into:{&found[i],&palm[i]}) {
            if(gavePosition) for(int j=0;j<3;++j) into->position[j]=position[j];
            if(gaveTurn) for(int j=0;j<4;++j) into->orientation[j]=orientation[j];
            into->tracked=true;
        }
    }
    ScriptControls(controls);
    AcquireSRWLockExclusive(&g_handLock);
    g_hand[0]=found[0]; g_hand[1]=found[1];
    g_grip[0]=palm[0]; g_grip[1]=palm[1];
    g_controls=controls;
    ReleaseSRWLockExclusive(&g_handLock);
}

void RotateByQuat(const float q[4],const float v[3],float out[3]) noexcept {
    const float x=q[0],y=q[1],z=q[2],w=q[3];
    const float tx=2*(y*v[2]-z*v[1]);
    const float ty=2*(z*v[0]-x*v[2]);
    const float tz=2*(x*v[1]-y*v[0]);
    out[0]=v[0]+w*tx+(y*tz-z*ty);
    out[1]=v[1]+w*ty+(z*tx-x*tz);
    out[2]=v[2]+w*tz+(x*ty-y*tx);
}

// The rotation taking a quad's own negative Z onto `direction`, which is what
// makes it face the way the weapon points.
void TurnOntoDirection(const float direction[3],XrQuaternionf& out) noexcept {
    const float from[3]={0,0,-1};
    const float dot=from[0]*direction[0]+from[1]*direction[1]+from[2]*direction[2];
    if(dot>0.9999f) { out={0,0,0,1}; return; }
    if(dot<-0.9999f) { out={0,1,0,0}; return; }   // straight behind: turn about up
    const float axis[3]={from[1]*direction[2]-from[2]*direction[1],
                         from[2]*direction[0]-from[0]*direction[2],
                         from[0]*direction[1]-from[1]*direction[0]};
    const float length=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
    if(!(length>1e-6f)) { out={0,0,0,1}; return; }
    const float angle=std::acos(dot<-1?-1:(dot>1?1:dot));
    const float sine=std::sin(angle*0.5f);
    out.x=axis[0]/length*sine;
    out.y=axis[1]/length*sine;
    out.z=axis[2]/length*sine;
    out.w=std::cos(angle*0.5f);
}

void PrepareSessionCockpit() noexcept {
    if(g_mode!=XrMode::HeadsetDisplay || !CockpitEnabled())return;
    // First boarding previously compiled shaders/uploaded both meshes at
    // Present (515ms in the field) and built a BVH on the camera thread. That
    // can expire the 250ms native pair/pose contract and flash a 2D board.
    // Do all device/CPU preparation before tracking and scene frames start.
    const auto at=GetTickCount64();PrepareCockpitCollision();
    const bool ready=PrepareCockpitDraw(g_device);
    if(g_log){char line[128]{};std::snprintf(line,sizeof(line),"COCKPIT session warmup ready=%d ms=%llu; before first camera sample",ready,GetTickCount64()-at);g_log(line);}
}
bool CreateSession() noexcept {
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrResult result=g_api.getSystem(g_instance,&systemInfo,&g_system);
    if(XR_FAILED(result)) {
        SetStatusf("xrGetSystem failed (%d); connect the headset in SteamVR, Quest Link or Virtual Desktop for the active runtime",static_cast<int>(result));
        return false;
    }
    PFN_xrGetSystemProperties describeSystem=nullptr;
    if(Load(describeSystem,"xrGetSystemProperties")) {
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        if(XR_SUCCEEDED(describeSystem(g_instance,g_system,&properties))) {
            strncpy_s(g_headsetName,properties.systemName,_TRUNCATE);
            SetStatusf("OpenXR headset=%s vendor=%u orientation=%u position=%u; native per-eye FOV/IPD from runtime",
                properties.systemName,properties.vendorId,properties.trackingProperties.orientationTracking,
                properties.trackingProperties.positionTracking);
        }
    }
    // What the runtime would like per eye, beside what the game is being made
    // to draw. Read-only: this changes no size, and is here because the render
    // size has been chosen without it since the beginning.
    PFN_xrEnumerateViewConfigurationViews describeViews=nullptr;
    if(Load(describeViews,"xrEnumerateViewConfigurationViews")) {
        uint32_t viewCount=0;
        if(XR_SUCCEEDED(describeViews(g_instance,g_system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                      0,&viewCount,nullptr)) && viewCount && viewCount<=4) {
            XrViewConfigurationView budget[4]{};
            for(uint32_t i=0;i<viewCount;++i) budget[i]={XR_TYPE_VIEW_CONFIGURATION_VIEW};
            if(XR_SUCCEEDED(describeViews(g_instance,g_system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                          viewCount,&viewCount,budget)))
                for(uint32_t i=0;i<viewCount && i<2;++i)
                    SetStatusf("view %u wants %ux%u (max %ux%u) samples %u (max %u)",i,
                        budget[i].recommendedImageRectWidth,budget[i].recommendedImageRectHeight,
                        budget[i].maxImageRectWidth,budget[i].maxImageRectHeight,
                        budget[i].recommendedSwapchainSampleCount,budget[i].maxSwapchainSampleCount);
        }
    }
    // Can a projection view declare a field of view of its own? Meta's PC runtime
    // says no, and then shows every picture as covering exactly the eye (eye_crop.h).
    g_fovMutable.store(true);
    PFN_xrGetViewConfigurationProperties viewProperties=nullptr;
    if(Load(viewProperties,"xrGetViewConfigurationProperties")) {
        XrViewConfigurationProperties properties{XR_TYPE_VIEW_CONFIGURATION_PROPERTIES};
        if(XR_SUCCEEDED(viewProperties(g_instance,g_system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,&properties))) {
            g_fovMutable.store(properties.fovMutable!=XR_FALSE);
            SetStatus(properties.fovMutable!=XR_FALSE
                ?"view field of view is mutable: the wider rendered frustum is declared as it is"
                :"view field of view is FIXED by this runtime: each eye gets its own frustum and the part of the picture that covers it");
        }
    }
    PFN_xrGetD3D11GraphicsRequirementsKHR requirementsFn=nullptr;
    if(!Load(requirementsFn,"xrGetD3D11GraphicsRequirementsKHR")) { SetStatus("XR_KHR_D3D11_enable unavailable"); return false; }
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    result=requirementsFn(g_instance,g_system,&requirements);
    if(XR_FAILED(result)) {
        SetStatusf("xrGetD3D11GraphicsRequirementsKHR failed (%d)",static_cast<int>(result));
        return false;
    }
    if(g_mode==XrMode::HeadsetDisplay) {
        if(!AdoptGameDevice(requirements.adapterLuid)) return false;
    } else if(!CreateDevice(requirements.adapterLuid,requirements.minFeatureLevel)) return false;
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device=g_device;
    XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};
    create.next=&binding;
    create.systemId=g_system;
    result=g_api.createSession(g_instance,&create,&g_session);
    if(XR_FAILED(result)) { SetStatusf("xrCreateSession failed (%d)",static_cast<int>(result)); return false; }
    uint32_t count=0;
    XrReferenceSpaceType spaces[8]{};
    bool stage=false;
    if(XR_SUCCEEDED(g_api.enumerateSpaces(g_session,static_cast<uint32_t>(std::size(spaces)),&count,spaces)))
        for(uint32_t i=0;i<count && i<std::size(spaces);++i)
            if(spaces[i]==XR_REFERENCE_SPACE_TYPE_STAGE) stage=true;
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace.orientation.w=1;
    spaceInfo.referenceSpaceType=stage?XR_REFERENCE_SPACE_TYPE_STAGE:XR_REFERENCE_SPACE_TYPE_LOCAL;
    result=g_api.createSpace(g_session,&spaceInfo,&g_refSpace);
    if(XR_FAILED(result)) { SetStatusf("reference space creation failed (%d)",static_cast<int>(result)); return false; }
    g_stage.store(stage);
    spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    result=g_api.createSpace(g_session,&spaceInfo,&g_viewSpace);
    if(XR_FAILED(result)) { SetStatusf("view space creation failed (%d)",static_cast<int>(result)); return false; }
    PrepareSessionCockpit();
    SetStatusf("OpenXR session created; reference space=%s",stage?"STAGE":"LOCAL");
    return true;
}

void Publish(const XrSpaceLocation& location,XrTime displayTime) noexcept {
    HmdSample sample{};
    sample.orientationValid=(location.locationFlags&XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)!=0;
    sample.positionValid=(location.locationFlags&XR_SPACE_LOCATION_POSITION_VALID_BIT)!=0;
    sample.orientation=Quat{location.pose.orientation.x,location.pose.orientation.y,
                            location.pose.orientation.z,location.pose.orientation.w};
    sample.position=Vec3{location.pose.position.x,location.pose.position.y,location.pose.position.z};
    sample.displayTime=displayTime;
    sample.frame=g_frames.fetch_add(1)+1;
    ScriptHead(sample);
    AcquireSRWLockExclusive(&g_sampleLock);
    g_sample=sample;
    ReleaseSRWLockExclusive(&g_sampleLock);
}

// One colour swapchain the size of the back buffer. Nothing is scaled: the quad
// shows the game image at its own resolution.
void DestroyDisplaySwapchain() noexcept {
    // WarpEye caches RTVs/SRVs referencing runtime-owned swapchain textures.
    // Release those before asking SteamVR to destroy the textures, including
    // on a size change rather than only on final session shutdown.
    ReleaseWarp();
    ReleaseDesktopMirror();
    ReleaseSceneAA();
    ResetEyeCheck();
    if(g_swapchain!=XR_NULL_HANDLE && g_api.destroySwapchain) g_api.destroySwapchain(g_swapchain);
    g_swapchain=XR_NULL_HANDLE;
    if(g_swapchainRight!=XR_NULL_HANDLE && g_api.destroySwapchain) g_api.destroySwapchain(g_swapchainRight);
    g_swapchainRight=XR_NULL_HANDLE;
    delete[] g_images;
    g_images=nullptr;
    g_imageCount=0;
    delete[] g_imagesRight;
    g_imagesRight=nullptr;
    g_imageCountRight=0;
    g_eyePoseValid=false;
    g_cameraPoseValid=false;
    g_writtenPoseValid[0]=g_writtenPoseValid[1]=false;
    g_haveImage[0]=g_haveImage[1]=false;
    g_lastCopiedEye=-2;
    g_swapchainWidth=0;
    g_swapchainHeight=0;
    if(g_resolved) { g_resolved->Release(); g_resolved=nullptr; }
    if(g_swapchainUi!=XR_NULL_HANDLE && g_api.destroySwapchain) g_api.destroySwapchain(g_swapchainUi);
    g_swapchainUi=XR_NULL_HANDLE;
    delete[] g_imagesUi; g_imagesUi=nullptr;
    g_imageCountUi=0;
    if(g_swapchainCluster!=XR_NULL_HANDLE) { g_api.destroySwapchain(g_swapchainCluster); g_swapchainCluster=XR_NULL_HANDLE; }
    delete[] g_imagesCluster; g_imagesCluster=nullptr;
    g_imageCountCluster=0; g_clusterMadeW=g_clusterMadeH=0;
    // The reticle's too. It was left out, so after F11 off and on its handle
    // still named a swapchain of the session that had ended: remade "already"
    // at this size, it failed every acquire and the reticle never came back.
    if(g_swapchainReticle!=XR_NULL_HANDLE && g_api.destroySwapchain) g_api.destroySwapchain(g_swapchainReticle);
    g_swapchainReticle=XR_NULL_HANDLE;
    delete[] g_imagesReticle; g_imagesReticle=nullptr;
    g_imageCountReticle=0; g_reticleMade=0;
}

// Long enough that no healthy frame ever reaches it, short enough that a sick
// one costs a dropped frame rather than the game.
//
// These waits used to have no end at all, and the stack taken off the hung game
// says what that costs: its update thread sitting in WaitForSingleObject inside
// EDF.dll waiting for its draw thread, and its draw thread sitting inside
// vrclient_x64.dll waiting for an image that was never going to come. A hook on
// someone else's render thread does not get to wait forever.
constexpr XrDuration kImageWait=100'000'000;   // 100ms in nanoseconds

bool CreateDisplaySwapchain(unsigned width,unsigned height) noexcept {
    if(!width || !height) return false;
    // The game can change resolution or go fullscreen underneath us.
    if(g_swapchain!=XR_NULL_HANDLE && (width!=g_swapchainWidth || height!=g_swapchainHeight))
        DestroyDisplaySwapchain();
    if(g_swapchain!=XR_NULL_HANDLE) return true;
    uint32_t formatCount=0;
    if(XR_FAILED(g_api.enumerateFormats(g_session,0,&formatCount,nullptr)) || !formatCount) {
        SetStatus("no swapchain formats offered");
        return false;
    }
    if(formatCount>64) formatCount=64;
    int64_t formats[64]{};
    if(XR_FAILED(g_api.enumerateFormats(g_session,formatCount,&formatCount,formats))) {
        SetStatus("swapchain format enumeration failed");
        return false;
    }
    // The back buffer holds sRGB-encoded bytes, so ask for the sRGB view of the
    // same layout. Both sit in the R8G8B8A8 typeless family, which is what lets
    // CopyResource move the bits across untouched.
    int64_t chosen=0;
    for(uint32_t i=0;i<formatCount && !chosen;++i)
        if(formats[i]==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) chosen=formats[i];
    for(uint32_t i=0;i<formatCount && !chosen;++i)
        if(formats[i]==DXGI_FORMAT_R8G8B8A8_UNORM) chosen=formats[i];
    if(!chosen) { SetStatus("the runtime offers no R8G8B8A8 swapchain format"); return false; }

    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    create.format=chosen;
    create.sampleCount=1;
    create.width=width;
    create.height=height;
    create.faceCount=1;
    create.arraySize=1;
    create.mipCount=1;
    XrResult result=g_api.createSwapchain(g_session,&create,&g_swapchain);
    if(XR_FAILED(result)) {
        g_swapchain=XR_NULL_HANDLE;
        SetStatusf("xrCreateSwapchain failed (%d)",static_cast<int>(result));
        return false;
    }
    uint32_t count=0;
    if(XR_FAILED(g_api.enumerateImages(g_swapchain,0,&count,nullptr)) || !count || count>16) {
        SetStatus("swapchain image enumeration failed");
        return false;
    }
    auto headers=new(std::nothrow) XrSwapchainImageD3D11KHR[count];
    if(!headers) { SetStatus("out of memory for swapchain images"); return false; }
    for(uint32_t i=0;i<count;++i) headers[i]=XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    result=g_api.enumerateImages(g_swapchain,count,&count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(headers));
    if(XR_FAILED(result)) {
        delete[] headers;
        SetStatus("swapchain images could not be read");
        return false;
    }
    g_images=new(std::nothrow) ID3D11Texture2D*[count];
    if(!g_images) { delete[] headers; SetStatus("out of memory for swapchain images"); return false; }
    for(uint32_t i=0;i<count;++i) g_images[i]=headers[i].texture;
    delete[] headers;
    g_imageCount=count;
    g_swapchainFormat=chosen;
    g_swapchainWidth=width;
    g_swapchainHeight=height;

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace.orientation.w=1;
    spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    if(XR_FAILED(g_api.createSpace(g_session,&spaceInfo,&g_quadSpace))) {
        g_quadSpace=XR_NULL_HANDLE;
        SetStatus("head-locked space for the quad could not be created");
        return false;
    }
    SetStatusf("headset swapchain ready: %ux%u format=%lld images=%u",
        width,height,static_cast<long long>(chosen),count);
    return true;
}

// The right eye, created only when both eyes are being fed separately. It
// reuses the format and size the first swapchain already agreed with the runtime.
bool CreateUiSwapchain() noexcept {
    if(g_swapchainUi!=XR_NULL_HANDLE) return true;
    if(g_swapchain==XR_NULL_HANDLE || !g_swapchainFormat) return false;
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    create.format=g_swapchainFormat;
    create.sampleCount=1;
    create.width=g_swapchainWidth;
    create.height=g_swapchainHeight;
    create.faceCount=1;
    create.arraySize=1;
    create.mipCount=1;
    if(XR_FAILED(g_api.createSwapchain(g_session,&create,&g_swapchainUi))) {
        g_swapchainUi=XR_NULL_HANDLE;
        SetStatus("UI swapchain could not be created; the HUD stays with the world");
        return false;
    }
    uint32_t count=0;
    if(XR_FAILED(g_api.enumerateImages(g_swapchainUi,0,&count,nullptr)) || !count || count>16) return false;
    auto headers=new(std::nothrow) XrSwapchainImageD3D11KHR[count];
    if(!headers) return false;
    for(uint32_t i=0;i<count;++i) headers[i]=XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    if(XR_FAILED(g_api.enumerateImages(g_swapchainUi,count,&count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(headers)))) {
        delete[] headers;
        return false;
    }
    g_imagesUi=new(std::nothrow) ID3D11Texture2D*[count];
    if(!g_imagesUi) { delete[] headers; return false; }
    for(uint32_t i=0;i<count;++i) g_imagesUi[i]=headers[i].texture;
    delete[] headers;
    g_imageCountUi=count;
    return true;
}


bool CreateClusterSwapchain(uint32_t width,uint32_t height) noexcept {
    if(g_swapchainCluster!=XR_NULL_HANDLE && g_clusterMadeW==width && g_clusterMadeH==height) return true;
    if(g_swapchain==XR_NULL_HANDLE || !g_swapchainFormat || width<16 || height<16 || width>4096 || height>4096) return false;
    if(g_swapchainCluster!=XR_NULL_HANDLE) {
        g_api.destroySwapchain(g_swapchainCluster);
        g_swapchainCluster=XR_NULL_HANDLE;
        delete[] g_imagesCluster; g_imagesCluster=nullptr; g_imageCountCluster=0;
    }
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    create.format=g_swapchainFormat;
    create.sampleCount=1;
    create.width=width;
    create.height=height;
    create.faceCount=1;
    create.arraySize=1;
    create.mipCount=1;
    if(XR_FAILED(g_api.createSwapchain(g_session,&create,&g_swapchainCluster))) { g_swapchainCluster=XR_NULL_HANDLE; return false; }
    uint32_t count=0;
    if(XR_FAILED(g_api.enumerateImages(g_swapchainCluster,0,&count,nullptr)) || !count || count>16) return false;
    auto headers=new(std::nothrow) XrSwapchainImageD3D11KHR[count];
    if(!headers) return false;
    for(uint32_t i=0;i<count;++i) headers[i]=XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    if(XR_FAILED(g_api.enumerateImages(g_swapchainCluster,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(headers)))) {
        delete[] headers; return false;
    }
    g_imagesCluster=new(std::nothrow) ID3D11Texture2D*[count];
    if(!g_imagesCluster) { delete[] headers; return false; }
    for(uint32_t i=0;i<count;++i) g_imagesCluster[i]=headers[i].texture;
    delete[] headers;
    g_imageCountCluster=count;
    g_clusterMadeW=width; g_clusterMadeH=height;
    return true;
}

bool CreateReticleSwapchain(int side) noexcept {
    if(g_swapchainReticle!=XR_NULL_HANDLE && g_reticleMade==side) return true;
    // The side grows with the render height (ReticlePixels at 1080p): 416 at
    // 2160, 520 at the 1.25x size (2700), 832 at the 7680x4320 maximum. A cap of
    // 512 refused the swapchain from 1.25x up and the reticle never appeared.
    if(g_swapchain==XR_NULL_HANDLE || !g_swapchainFormat || side<16 || side>1024) return false;
    if(g_swapchainReticle!=XR_NULL_HANDLE) {
        g_api.destroySwapchain(g_swapchainReticle);
        g_swapchainReticle=XR_NULL_HANDLE;
        delete[] g_imagesReticle; g_imagesReticle=nullptr; g_imageCountReticle=0;
    }
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    create.format=g_swapchainFormat;
    create.sampleCount=1;
    create.width=static_cast<uint32_t>(side);
    create.height=static_cast<uint32_t>(side);
    create.faceCount=1;
    create.arraySize=1;
    create.mipCount=1;
    if(XR_FAILED(g_api.createSwapchain(g_session,&create,&g_swapchainReticle))) {
        g_swapchainReticle=XR_NULL_HANDLE;
        return false;
    }
    uint32_t count=0;
    if(XR_FAILED(g_api.enumerateImages(g_swapchainReticle,0,&count,nullptr)) || !count || count>16)
        return false;
    auto headers=new(std::nothrow) XrSwapchainImageD3D11KHR[count];
    if(!headers) return false;
    for(uint32_t i=0;i<count;++i) headers[i]=XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    if(XR_FAILED(g_api.enumerateImages(g_swapchainReticle,count,&count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(headers)))) {
        delete[] headers;
        return false;
    }
    g_imagesReticle=new(std::nothrow) ID3D11Texture2D*[count];
    if(!g_imagesReticle) { delete[] headers; return false; }
    for(uint32_t i=0;i<count;++i) g_imagesReticle[i]=headers[i].texture;
    delete[] headers;
    g_imageCountReticle=count;
    g_reticleMade=side;
    return true;
}

bool CreateRightEyeSwapchain() noexcept {
    if(g_swapchainRight!=XR_NULL_HANDLE) return g_imagesRight && g_imageCountRight;
    if(g_swapchain==XR_NULL_HANDLE || !g_swapchainFormat) return false;
    g_haveImage[1]=false;
    g_writtenPoseValid[1]=false;
    XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    create.format=g_swapchainFormat;
    create.sampleCount=1;
    create.width=g_swapchainWidth;
    create.height=g_swapchainHeight;
    create.faceCount=1;
    create.arraySize=1;
    create.mipCount=1;
    if(XR_FAILED(g_api.createSwapchain(g_session,&create,&g_swapchainRight))) {
        g_swapchainRight=XR_NULL_HANDLE;
        SetStatus("second eye swapchain could not be created; staying monoscopic");
        return false;
    }
    uint32_t count=0;
    if(XR_FAILED(g_api.enumerateImages(g_swapchainRight,0,&count,nullptr)) || !count || count>16) return false;
    auto headers=new(std::nothrow) XrSwapchainImageD3D11KHR[count];
    if(!headers) return false;
    for(uint32_t i=0;i<count;++i) headers[i]=XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR};
    if(XR_FAILED(g_api.enumerateImages(g_swapchainRight,count,&count,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(headers)))) {
        delete[] headers;
        return false;
    }
    g_imagesRight=new(std::nothrow) ID3D11Texture2D*[count];
    if(!g_imagesRight) { delete[] headers; return false; }
    for(uint32_t i=0;i<count;++i) g_imagesRight[i]=headers[i].texture;
    delete[] headers;
    g_imageCountRight=count;
    return true;
}

// A single non-multisampled texture to resolve the multisampled back buffer
// into, because CopyResource cannot take multisampled content.
bool EnsureResolveTarget(unsigned width,unsigned height,DXGI_FORMAT format) noexcept {
    if(g_resolved) {
        // Changing anti-aliasing or resolution in the game menu gives us a new
        // back buffer. Reusing a target built for the old one is what froze the
        // game on 2026-09-09, so the shape is checked rather than assumed.
        D3D11_TEXTURE2D_DESC existing{};
        g_resolved->GetDesc(&existing);
        if(existing.Width==width && existing.Height==height && existing.Format==format) return true;
        g_resolved->Release();
        g_resolved=nullptr;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width;
    desc.Height=height;
    desc.MipLevels=1;
    desc.ArraySize=1;
    desc.Format=format;
    desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(g_device->CreateTexture2D(&desc,nullptr,&g_resolved)) && g_resolved;
}

// R8G8B8A8 in any of its spellings can be copied into the sRGB swapchain image,
// because CopyResource only needs the same typeless family. Anything else would
// have to be converted, so the frame is skipped instead.
bool CopyableFormat(DXGI_FORMAT format) noexcept {
    return format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
        || format==DXGI_FORMAT_R8G8B8A8_TYPELESS;
}

bool CopyIntoSwapchain(ID3D11Texture2D* backBuffer,unsigned sampleCount,uint32_t index,
                       ID3D11Texture2D** images,uint32_t imageCount,PerfBatch& perf) noexcept {
    if(index>=imageCount || !images || !images[index] || !g_context) return false;
    // The live back buffer decides, not what was cached several hundred frames
    // ago: the game can change anti-aliasing or resolution at any time, and
    // resolving a buffer that is no longer multisampled is undefined.
    D3D11_TEXTURE2D_DESC source{};
    backBuffer->GetDesc(&source);
    if(!CopyableFormat(source.Format)) return false;
    if(source.Width!=g_swapchainWidth || source.Height!=g_swapchainHeight) return false;
    sampleCount=source.SampleDesc.Count;
    D3D11_TEXTURE2D_DESC desc=source;
    desc.SampleDesc.Count=1;
    desc.SampleDesc.Quality=0;
    // Everything after this reads g_resolved as "this frame, flattened", and it
    // used to be made only when there was multisampling to flatten. Turn
    // anti-aliasing off and there is nothing to flatten, so it was never made,
    // so the second eye, the UI mask and the erase pass all quietly did nothing
    // -- and the projection layer went out carrying a swapchain that had never
    // been written into. The runtime refuses that: XR_ERROR_LAYER_INVALID, 1777
    // times in a row in the log, with the headset still showing the loading
    // screen while the desktop played the mission. So it is made either way, and
    // a single-sampled frame is copied into it instead of resolved.
    if(!MeasureCpu(perf,PerfStage::ResolveSetup,[&]() {
        return EnsureResolveTarget(desc.Width,desc.Height,desc.Format);
    })) return false;
    MeasureCpu(perf,PerfStage::ResolveSubmit,[&]() {
        GpuScope gpu(GpuStage::Resolve);
        if(sampleCount>1) g_context->ResolveSubresource(g_resolved,0,backBuffer,0,desc.Format);
        else g_context->CopyResource(g_resolved,backBuffer);
    });
    MeasureCpu(perf,PerfStage::CopySubmit,[&]() { GpuScope gpu(GpuStage::Copy); g_context->CopyResource(images[index],g_resolved); });
    return true;
}

// Drain a begun display frame before STOPPING/endSession or an F11 exit request.
// The tracking-only worker owns a separate frame loop and never sets this flag.
void FinishPreparedFrameEmpty() noexcept {
    if(!g_displayFramePrepared) return;
    g_displayFramePrepared=false;
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime=g_preparedFrame.predictedDisplayTime;
    end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    const auto result=g_api.endFrame(g_session,&end);
    g_lastEndFrame.store(static_cast<int>(result),std::memory_order_relaxed);
    if(XR_FAILED(result)) g_endFrameFailures.fetch_add(1,std::memory_order_relaxed);
}

void PollSessionEvents(bool& sessionRunning) noexcept {
    for(;;) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        if(g_api.pollEvent(g_instance,&event)!=XR_SUCCESS) break;
        if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            g_sessionState.store(static_cast<int>(
                reinterpret_cast<const XrEventDataSessionStateChanged*>(&event)->state),
                std::memory_order_relaxed);
            const auto* changed=reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            if(changed->state==XR_SESSION_STATE_READY && !sessionRunning && !g_stop.load()) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if(XR_SUCCEEDED(g_api.beginSession(g_session,&begin))) {
                    sessionRunning=true;
                    // Attaching action sets is legal only once per session and
                    // must come before any action is read.
                    CreateInput();
                }
            } else if(changed->state==XR_SESSION_STATE_STOPPING && sessionRunning) {
                FinishPreparedFrameEmpty();
                if(XR_SUCCEEDED(g_api.endSession(g_session))) sessionRunning=false;
            } else if(changed->state==XR_SESSION_STATE_EXITING
                   || changed->state==XR_SESSION_STATE_LOSS_PENDING) {
                // xrEndSession is legal only in STOPPING, not EXITING/LOSS_PENDING.
                g_displayFramePrepared=false;
                sessionRunning=false;
                g_stop.store(true);
            }
        } else if(event.type==XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
            ReportInputProfiles();
        } else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            g_stop.store(true);
        }
    }
}

void ResetInput() noexcept {
    g_controllerKind[0]=0;
    // fear-vr XrInput::ResetSession/Destroy: session spaces and actions must
    // not survive the instance that owns them. Called with frame ownership.
    g_inputReady.store(false);
    ClearHapticRequests();
    for(auto& s:g_aimSpace) { if(s && g_api.destroySpace) g_api.destroySpace(s); s=XR_NULL_HANDLE; }
    for(auto& s:g_gripSpace) { if(s && g_api.destroySpace) g_api.destroySpace(s); s=XR_NULL_HANDLE; }
    if(g_actionSet && g_api.destroyActionSet) g_api.destroyActionSet(g_actionSet);
    g_actionSet=XR_NULL_HANDLE;
    XrAction* actions[]={&g_aimAction,&g_gripAction,&g_triggerAction,&g_squeezeAction,
        &g_squeezeForceAction,&g_stickAction,&g_stickClickAction,&g_stickTouchAction,&g_lowerAction,
        &g_upperAction,&g_menuAction,&g_hapticAction};
    for(auto* action:actions) *action=XR_NULL_HANDLE;
    for(auto& path:g_handPath) path=XR_NULL_PATH;
    AcquireSRWLockExclusive(&g_handLock);
    for(auto& hand:g_hand) hand=HandTracked{};
    for(auto& grip:g_grip) grip=HandTracked{};
    g_controls=ControllerState{};
    ReleaseSRWLockExclusive(&g_handLock);
    strcpy_s(g_inputNote,"input not set up");
}
void Teardown() noexcept {
    g_displayFramePrepared=false;
    g_preparedFrame={XR_TYPE_FRAME_STATE};
    g_displayOwnerThread=0;
    ResetInput();
    ResetGpuProfile();
    DestroyDisplaySwapchain();         // the textures themselves belong to the runtime
    if(g_quadSpace && g_api.destroySpace) g_api.destroySpace(g_quadSpace);
    g_quadSpace=XR_NULL_HANDLE;
    if(g_api.destroySpace) {
        if(g_viewSpace) g_api.destroySpace(g_viewSpace);
        if(g_refSpace) g_api.destroySpace(g_refSpace);
    }
    g_viewSpace=XR_NULL_HANDLE;
    g_refSpace=XR_NULL_HANDLE;
    if(g_session && g_api.destroySession) g_api.destroySession(g_session);
    g_session=XR_NULL_HANDLE;
    if(g_instance && g_api.destroyInstance) g_api.destroyInstance(g_instance);
    g_instance=XR_NULL_HANDLE;
    // The game device is borrowed; only release one we created ourselves.
    if(g_ownDevice) {
        if(g_context) g_context->Release();
        if(g_device) g_device->Release();
    }
    g_context=nullptr;
    g_device=nullptr;
    g_ownDevice=false;
    g_sessionRunning.store(false);
    g_api=Api{};
    g_system=XR_NULL_SYSTEM_ID;
}

// Called only at a Present boundary, with g_frameLock held exclusively. F11
// immediately disables aiming, but resource destruction waits for both XR's
// STOPPING transition and completion of the game's submitted GPU work.
// Never spin/wait here: if unfinished, the game presents normally and retries.
void AdvanceDisplayStop() noexcept {
    FinishPreparedFrameEmpty();
    if(!g_stopStarted) {
        g_stopStarted=GetTickCount64();
        SetStatus("OpenXR stop: render-thread shutdown started");
    }
    bool sessionRunning=g_sessionRunning.load();
    PollSessionEvents(sessionRunning);
    g_sessionRunning.store(sessionRunning);
    if(!g_exitRequested) {
        if(g_sessionRunning.load()) {
            const auto result=g_api.requestExitSession(g_session);
            if(XR_FAILED(result)) {
                if(!g_stopWarning) SetStatusf("OpenXR stop: requestExit failed (%d); retaining resources",result);
                g_stopWarning=true;
                return;
            }
        }
        g_exitRequested=true;
    }
    if(sessionRunning) {
        if(!g_stopWarning && GetTickCount64()-g_stopStarted>5000) {
            SetStatus("OpenXR stop: awaiting STOPPING event; resources retained");
            g_stopWarning=true;
        }
        return;
    }
    // End the XR session before the fence, so any GPU work issued by endSession
    // on this context is covered as well as the final game Resolve/Copy.
    if(!g_stopFence) {
        D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0};
        const auto result=g_device->CreateQuery(&desc,&g_stopFence);
        if(FAILED(result) || !g_stopFence) {
            if(!g_stopWarning) SetStatus("OpenXR stop: GPU fence creation failed; retaining resources");
            g_stopWarning=true;
            return;
        }
        g_context->End(g_stopFence);
        g_context->Flush();  // shutdown only, not a per-frame profiling operation
    }
    BOOL gpuDone=FALSE;
    const auto ready=g_context->GetData(g_stopFence,&gpuDone,sizeof(gpuDone),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(ready!=S_OK || !gpuDone) {
        if(!g_stopWarning && GetTickCount64()-g_stopStarted>5000) {
            SetStatusf("OpenXR stop: still pending (sessionRunning=%d gpuResult=%ld gpuDone=%d); resources retained",
                       sessionRunning,ready,gpuDone);
            g_stopWarning=true;
        }
        return;
    }
    SetStatus("OpenXR stop: session ended and GPU complete; destroying XR resources");
    g_stopFence->Release();
    g_stopFence=nullptr;
    ClearNativeWorldImages();
    ReleaseCockpitDraw(); // full release only here/device change, never on a slow frame
    Teardown();
    g_mode.store(XrMode::TrackingOnly);
    g_displayStopPending.store(false);
    g_exitRequested=false;
    g_stopWarning=false;
    g_stopStarted=0;
    SetStatus("OpenXR stopped (render thread, GPU complete)");
}
}

OpenXrRuntime g_openxr;

void OpenXrRuntime::ThreadMain() noexcept {
    bool sessionRunning=false;
    while(!g_stop.load()) {
        PollSessionEvents(sessionRunning);
        g_sessionRunning.store(sessionRunning);
        if(!sessionRunning) { Sleep(20); continue; }
        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
        if(XR_FAILED(g_api.waitFrame(g_session,&waitInfo,&frameState))) { Sleep(5); continue; }
        XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
        if(XR_FAILED(g_api.beginFrame(g_session,&beginInfo))) { Sleep(5); continue; }
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
        ReadHands(frameState.predictedDisplayTime);
        if(XR_SUCCEEDED(g_api.locateSpace(g_viewSpace,g_refSpace,frameState.predictedDisplayTime,&location)))
            { g_lastViewPose=location.pose; Publish(location,frameState.predictedDisplayTime); }
        // No layers are submitted yet: this milestone only needs head tracking.
        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime=frameState.predictedDisplayTime;
        endInfo.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount=0;
        endInfo.layers=nullptr;
        g_api.endFrame(g_session,&endInfo);
    }
    AcquireSRWLockExclusive(&g_frameLock);
    if(sessionRunning) g_api.endSession(g_session);
    Teardown();
    g_running.store(false);
    ReleaseSRWLockExclusive(&g_frameLock);
}

// One OpenXR frame, run inside the game present. Everything here touches the
// game immediate context, so it must stay on the render thread.
// The game renders one wide image per pass, so its frustum has to be symmetric
// and at least as large as the runtime asks for. Vertical is the binding side on
// a 16:9 image, and the horizontal that follows from the aspect comfortably
// covers what the headset needs.
// Draws the eye the game did not render into the other swapchain, from the
// resolved colour and the scene depth. Everything it needs already exists: the
// resolve target carries a shader resource bind, the swapchains are colour
// attachments, and the depth survey has settled which texture the scene uses.
// The presented image from just before the HUD went on if the probe managed to
// pick one, and the scene target from the end of the scene pass otherwise. The
// first needs no correcting and leaves nothing behind; the second is a guess
// between two different stages of the pipeline and leaks thin bright things.
ID3D11Texture2D* HudlessFor(bool& exact) noexcept {
    if(auto* picture=PreUiColour()) { exact=true; return picture; }
    exact=false;
    return SceneColour();
}

int ReticleSideForHeight(uint32_t height) noexcept {
    const int base=g_reticlePixels.load(std::memory_order_relaxed);
    if(base<16 || base>512 || !height) return 0;
    const int side=static_cast<int>(std::ceil(static_cast<double>(base)*height/1080.0));
    return side>=16 && side<static_cast<int>(height)?side:0;
}

bool ZoomReticleDirection(const float aim[3],const XrQuaternionf& camera,
                          float zoom,float out[3]) noexcept {
    if(!std::isfinite(zoom) || zoom<1 || zoom>40) return false;
    if(zoom==1) { std::memcpy(out,aim,3*sizeof(float)); return true; }
    if(!NormalizedQuat(Quat{camera.x,camera.y,camera.z,camera.w})) return false;
    const float inverse[4]={-camera.x,-camera.y,-camera.z,camera.w};
    float local[3]{}; RotateByQuat(inverse,aim,local);
    if(!std::isfinite(local[0]) || !std::isfinite(local[1]) || !std::isfinite(local[2])
       || local[2]>=-0.0001f) return false; // no forward projection behind the camera
    // Scene focal length grows by zoom, while the XR presentation frustum stays
    // wide. Apply that same projective scaling to the marker: tan(angle), not
    // the Euler angle itself. Work in camera axes so pitch and roll also agree.
    local[0]*=zoom; local[1]*=zoom;
    const float length=std::sqrt(local[0]*local[0]+local[1]*local[1]+local[2]*local[2]);
    if(!std::isfinite(length) || length<0.001f) return false;
    for(int j=0;j<3;++j) local[j]/=length;
    const float rotation[4]={camera.x,camera.y,camera.z,camera.w};
    RotateByQuat(rotation,local,out);
    return true;
}

bool AimReticlePose(const float aim[3],const XrPosef& head,float distance,
                    XrPosef& pose) noexcept {
    const float length=std::sqrt(aim[0]*aim[0]+aim[1]*aim[1]+aim[2]*aim[2]);
    if(!std::isfinite(length) || length<0.001f || !std::isfinite(distance) || distance<=0)
        return false;
    const float direction[3]={aim[0]/length,aim[1]/length,aim[2]/length};
    pose.position={head.position.x+direction[0]*distance,
                   head.position.y+direction[1]*distance,
                   head.position.z+direction[2]*distance};
    if(!std::isfinite(pose.position.x) || !std::isfinite(pose.position.y)
       || !std::isfinite(pose.position.z)) return false;
    TurnOntoDirection(direction,pose.orientation);
    return true;
}

bool SetReticleErase(WarpParams& params,bool exact) noexcept {
    // Direct capture routes the glyph away from the world in the first place.
    // Pasting an earlier pre-UI image over that clean world removes late scene
    // draws and leaves a rectangular patch in both eyes.
    if(UiTexture() || !exact || !g_resolved || !g_uiLayer.load() || !g_reticleFollows.load()
       || !g_aimKnown.load()) return false;
    D3D11_TEXTURE2D_DESC desc{}; g_resolved->GetDesc(&desc);
    const int side=ReticleSideForHeight(desc.Height);
    if(side<16 || side>=static_cast<int>(desc.Height) || !desc.Width) return false;
    params.eraseReticle=true;
    params.reticleHalf[0]=0.5f*static_cast<float>(side)/static_cast<float>(desc.Width);
    params.reticleHalf[1]=0.5f*static_cast<float>(side)/static_cast<float>(desc.Height);
    return true;
}

void ConfigureWorldUi(WarpParams& params,bool exact) noexcept {
    const bool direct=UiTexture()!=nullptr;
    params.exact=exact;
    params.useHudless=!direct && g_warpHudless.load(std::memory_order_relaxed);
    // A late world draw can differ from PreUiColour. With direct HUD capture,
    // that difference is NOT UI: mode 1 would freeze it at the source eye's UV
    // while adjacent pixels warp, tearing the same object's silhouette.
    params.uiMode=direct?0.0f:(g_uiLayer.load(std::memory_order_relaxed)?2.0f:1.0f);
    params.eraseReticle=false;
    params.reticleHalf[0]=params.reticleHalf[1]=0;
    SetReticleErase(params,exact);
}

bool BuildOtherEye(int drawnEye,PerfBatch& perf,float steps,bool antiAlias) noexcept {
    const bool drawnRight=drawnEye==1;
    XrSwapchain other=drawnRight?g_swapchain:g_swapchainRight;
    ID3D11Texture2D** otherImages=drawnRight?g_images:g_imagesRight;
    const uint32_t otherCount=drawnRight?g_imageCount:g_imageCountRight;
    if(other==XR_NULL_HANDLE || !otherImages || !otherCount) return false;
    auto depth=SceneDepth();
    if(!depth) return false;
    const float fov=BinocularFov(g_adviceFov.load(std::memory_order_relaxed),g_binocularZoom.load(std::memory_order_relaxed));
    const float ipd=g_adviceIpd.load(std::memory_order_relaxed);
    if(fov<=0 || ipd<=0) return false;
    uint32_t index=0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(XR_FAILED(g_api.acquireImage(other,&acquire,&index)) || index>=otherCount) return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout=kImageWait;
    bool ok=false;
    if(XR_SUCCEEDED(g_api.waitImage(other,&wait))) {
        // Everything the warp reads, written out on request. Cheap to ask for
        // and it turns a question about the picture into something answerable
        // without going back into the headset.
        // Dump after UI extraction below, so native and legacy paths both
        // include the actual reticle source and acquired output with alpha.
        bool exact=false;
        auto* hudless=HudlessFor(exact);
        WarpParams params{};
        params.exact=exact;
        params.eyeSeparation=ipd*g_warpEyeScale.load(std::memory_order_relaxed);
        params.verticalFovRadians=fov;
        params.nearPlane=g_warpNear.load(std::memory_order_relaxed);
        params.farPlane=g_warpFar.load(std::memory_order_relaxed);
        // The game drew the right eye, so the one being built sits to its left.
        params.buildRightOfDrawn=!drawnRight;
        params.steps=steps;
        params.nearestMetres=g_warpNearest.load(std::memory_order_relaxed);
        params.nearKneeMetres=g_nearKnee.load(std::memory_order_relaxed);
        params.nearScale=g_nearScale.load(std::memory_order_relaxed);
        ConfigureWorldUi(params,exact);
        params.debug=g_warpDebug.load(std::memory_order_relaxed);
        ok=MeasureCpu(perf,PerfStage::WarpEye,[&]() {
            GpuScope gpu(GpuStage::Warp);
            return WarpEye(g_device,g_context,g_resolved,depth,otherImages[index],
                           hudless,params);
        });
        if(ok) {
            CompositeWeaponStereo(g_context,otherImages[index],drawnRight?0u:1u);
            ApplySceneAA(g_context,otherImages[index],antiAlias);
            CompositeWorldUi(otherImages[index]);
            if(g_desktopMirror.load() && !drawnRight)
                SnapshotMirror(g_context,otherImages[index],MirrorImage::Right,static_cast<DXGI_FORMAT>(g_swapchainFormat));
        }
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    ok=XR_SUCCEEDED(g_api.releaseImage(other,&release)) && ok;
    if(ok) {
        g_haveImage[drawnRight?0:1]=true;
        g_warpedEyes.fetch_add(1,std::memory_order_relaxed);
    }
    return ok;
}

// The weapon scope's picture (scope.h) over the weapon in this eye: in a
// scope's eyepiece or screen, on a holographic monitor over a weapon without
// one, or on the Fencer's panel in front of the eyes. The picture is the
// scope's own view (mode 1, eye 2 of the native loop) or a magnified crop of
// this eye's picture (mode 2, the digital zoom).
// One surface of it (a weapon may carry several: the Laser Guide Kit's three panels).
void CompositeScopeSurface(ID3D11Texture2D* target,unsigned eye,const NativeWorldImages& images,
                           const ScopeView& view,const ScopeLensFrame& lens) noexcept {
    ScopeDrawInput in{};
    in.lens=lens;in.view=images.view[eye];in.projection=images.projection[eye];in.mode=view.mode;in.style=view.style;
    in.tanHalf=view.tanHalf;in.aspect=lens.shape && lens.radius>0?lens.halfWidth/lens.radius:1.f;
    // A surface on the weapon goes through the weapon layer's camera: the
    // native one moved to the eye the weapon was placed from, plus this eye's
    // half IPD (weapon_stereo.cpp's transform, anchored). Through the native
    // camera it was left behind while walking, as the weapon once was.
    if(lens.anchored) {
        Matrix camera=ScopeCameraOf(images.view[eye]);
        const float dx=eye?lens.eyeHalf:-lens.eyeHalf;
        for(int j=0;j<3;++j) camera.m[3][j]=lens.eye[j]-camera.m[0][j]*dx;   // screen right is -row0
        in.view=ScopeCameraOf(camera);                                     // a rigid inverse undoes itself
    }
    // How the picture stands: a round lens by its camera's up, a screen by
    // its own (a monitor shows the picture square to its edges).
    float forward[3]={view.forward[0],view.forward[1],view.forward[2]},up[3]{};
    Matrix scopeCamera{};
    if(view.mode==1) {
        float l=0,r=0,t=0,b=0;
        if(!images.scopeReady || !FrustumFromProjection(images.scopeProjection.m,l,r,t,b)) {NoteScopeSkip(10);return;}
        // Only a view drawn with the narrow field; anything else is the head's own.
        if(std::fabs(std::tan(t)-view.tanHalf)>view.tanHalf*.25f) {NoteScopeSkip(11,std::tan(t),view.tanHalf);return;}
        scopeCamera=ScopeCameraOf(images.scopeView);   // row0 = up x forward: screen right is -row0
        for(int j=0;j<3;++j) {
            in.cameraRight[j]=-scopeCamera.m[0][j];in.cameraUp[j]=scopeCamera.m[1][j];in.cameraForward[j]=scopeCamera.m[2][j];
            forward[j]=scopeCamera.m[2][j];in.origin[j]=scopeCamera.m[3][j];
            up[j]=lens.shape?lens.up[j]:scopeCamera.m[1][j];
        }
        in.tanLeft=std::tan(l);in.tanRight=std::tan(r);in.tanTop=std::tan(t);in.tanBottom=std::tan(b);
        in.source=images.scope.Get();
    } else {
        const auto camera=ScopeCameraOf(images.view[eye]);
        for(int j=0;j<3;++j) {up[j]=lens.shape?lens.up[j]:camera.m[1][j];in.origin[j]=view.origin[j];}
        in.source=images.eye[eye].Get();
        in.sourceClip=ScopeWorldToClip(images.view[eye],images.projection[eye]);   // the picture's own camera
    }
    const float along=up[0]*forward[0]+up[1]*forward[1]+up[2]*forward[2];
    for(int j=0;j<3;++j) up[j]-=forward[j]*along;
    const float length=std::sqrt(up[0]*up[0]+up[1]*up[1]+up[2]*up[2]);
    if(!(length>1e-4f)) return;
    for(float& c:up) c/=length;
    const float right[3]={forward[1]*up[2]-forward[2]*up[1],forward[2]*up[0]-forward[0]*up[2],forward[0]*up[1]-forward[1]*up[0]};   // forward x up
    for(int j=0;j<3;++j) {in.right[j]=right[j];in.up[j]=up[j];in.forward[j]=forward[j];}
    DrawScopeLens(g_context,target,in);
}
void CompositeScopeLens(ID3D11Texture2D* target,unsigned eye,const NativeWorldImages& images) noexcept {
    const auto view=ReadScopeView();
    const auto now=GetTickCount64();
    if(!view.active || eye>1 || !images.ready || now-view.at>250) return;
    ScopeLensFrame lenses[kScopeMaxSurfaces]{};
    int count=0;
    if(view.kind==ScopeHoloPanel) {
        // Held in front of the eyes: between the two eye cameras, along their
        // forward, facing back, standing with the head.
        auto& lens=lenses[0];
        const auto left=ScopeCameraOf(images.view[0]),right=ScopeCameraOf(images.view[1]);
        for(int j=0;j<3;++j) {
            lens.centre[j]=(left.m[3][j]+right.m[3][j])*.5f+left.m[2][j]*kScopePanelDistance;
            lens.normal[j]=-left.m[2][j];lens.up[j]=left.m[1][j];
        }
        lens.radius=kScopePanelHalfHeight;lens.halfWidth=kScopePanelHalfWidth;lens.shape=1;lens.valid=true;
        count=1;
    } else count=ReadScopeLenses(lenses);
    for(int i=0;i<count;++i)
        if(lenses[i].valid && (view.kind==ScopeHoloPanel || now-lenses[i].at<=250))
            CompositeScopeSurface(target,eye,images,view,lenses[i]);
}
bool CopyNativeRightEye(ID3D11Texture2D* image,PerfBatch& perf,bool antiAlias,const NativeWorldImages* images=nullptr) noexcept {
    uint32_t index=0;
    XrSwapchainImageAcquireInfo take{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if(!image || XR_FAILED(g_api.acquireImage(g_swapchainRight,&take,&index))) return false;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};wait.timeout=kImageWait;
    bool ok=XR_SUCCEEDED(g_api.waitImage(g_swapchainRight,&wait)) &&
        CopyIntoSwapchain(image,1,index,g_imagesRight,g_imageCountRight,perf);
    if(ok) {
        CompositeWeaponStereo(g_context,g_imagesRight[index],1);
        // The right eye is a swapchain of its own: the lens goes on after its
        // weapon here too (it showed in the left eye alone, hardware 2026-10-01).
        if(images) CompositeScopeLens(g_imagesRight[index],1,*images);
        ApplySceneAA(g_context,g_imagesRight[index],antiAlias);
        CompositeWorldUi(g_imagesRight[index]);
        if(g_desktopMirror.load()) SnapshotMirror(g_context,g_imagesRight[index],MirrorImage::Right,
            static_cast<DXGI_FORMAT>(g_swapchainFormat));
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    ok=XR_SUCCEEDED(g_api.releaseImage(g_swapchainRight,&release)) && ok;
    if(ok) g_haveImage[1]=true;
    return ok;
}

// The shape the desktop buffer is actually shown at.
//
// Present maps the whole back buffer onto the whole client area, so once the
// render rectangle is fitted to the headset and is no longer 16:9 the two
// stop agreeing and everything drawn into the buffer arrives on the monitor
// stretched sideways. Zero when the window cannot be measured, which leaves
// the mirror on the buffer's own aspect as before.
float DesktopWindowAspect() noexcept {
    FrameCapture capture{};
    if(!ReadFrameCapture(capture) || !capture.window) return 0;
    RECT client{};
    if(!GetClientRect(static_cast<HWND>(capture.window),&client)) return 0;
    const LONG wide=client.right-client.left,tall=client.bottom-client.top;
    if(wide<=0 || tall<=0) return 0;
    return static_cast<float>(wide)/static_cast<float>(tall);
}
// Where each eye's picture is submitted, against where the runtime says the eyes
// are this frame: sideways (a swap shows as signs the wrong way round), up and
// ahead, the lag of the whole pair, the turn, which picture went to which eye,
// and the frusta. Five-second sample.
void ReportViews(const XrView* runtime,const XrCompositionLayerProjectionView* submitted,
                 bool leftGetsRight,bool rightGetsRight,const XrFovf& fov) noexcept {
    if(!g_log) return;
    const auto& q=runtime[0].pose.orientation;
    const float rotation[4]={q.x,q.y,q.z,q.w};
    const float x[3]={1,0,0},y[3]={0,1,0},z[3]={0,0,1};
    float right[3]{},up[3]{},back[3]{};
    RotateByQuat(rotation,x,right); RotateByQuat(rotation,y,up); RotateByQuat(rotation,z,back);
    auto middle=[](const XrPosef& a,const XrPosef& b) {
        return XrVector3f{(a.position.x+b.position.x)*.5f,(a.position.y+b.position.y)*.5f,(a.position.z+b.position.z)*.5f};
    };
    const XrVector3f rm=middle(runtime[0].pose,runtime[1].pose), sm=middle(submitted[0].pose,submitted[1].pose);
    auto along=[](const XrVector3f& p,const XrVector3f& m,const float* axis) {
        return (p.x-m.x)*axis[0]+(p.y-m.y)*axis[1]+(p.z-m.z)*axis[2];
    };
    auto turn=[](const XrQuaternionf& a,const XrQuaternionf& b) {
        const float d=std::fabs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
        return 2.0f*std::acos((std::min)(1.0f,d))*57.29578f;
    };
    auto deg=[](float radians) { return radians*57.29578f; };
    const auto& s0=submitted[0].pose.position; const auto& s1=submitted[1].pose.position;
    char line[1024]{};
    std::snprintf(line,sizeof(line),
        "XRVIEWS eyes across (m, + = right) runtime %+.4f/%+.4f submitted %+.4f/%+.4f, submitted up %+.4f/%+.4f ahead %+.4f/%+.4f; "
        "submitted pair off the runtime's by %+.3f/%+.3f/%+.3f m (right/up/ahead, lag); turned %.1f/%.1f deg; "
        "pictures L<-%s R<-%s; fov deg runtime L[%.1f %.1f %.1f %.1f] R[%.1f %.1f %.1f %.1f] submitted [%.1f %.1f %.1f %.1f]; "
        "picture part L=%d,%d %dx%d R=%d,%d %dx%d (%s)",
        along(runtime[0].pose.position,rm,right),along(runtime[1].pose.position,rm,right),
        along(s0,sm,right),along(s1,sm,right),along(s0,sm,up),along(s1,sm,up),-along(s0,sm,back),-along(s1,sm,back),
        along(sm,rm,right),along(sm,rm,up),-along(sm,rm,back),
        turn(submitted[0].pose.orientation,runtime[0].pose.orientation),turn(submitted[1].pose.orientation,runtime[1].pose.orientation),
        leftGetsRight?"right":"left",rightGetsRight?"right":"left",
        deg(runtime[0].fov.angleLeft),deg(runtime[0].fov.angleRight),deg(runtime[0].fov.angleUp),deg(runtime[0].fov.angleDown),
        deg(runtime[1].fov.angleLeft),deg(runtime[1].fov.angleRight),deg(runtime[1].fov.angleUp),deg(runtime[1].fov.angleDown),
        deg(fov.angleLeft),deg(fov.angleRight),deg(fov.angleUp),deg(fov.angleDown),
        submitted[0].subImage.imageRect.offset.x,submitted[0].subImage.imageRect.offset.y,
        submitted[0].subImage.imageRect.extent.width,submitted[0].subImage.imageRect.extent.height,
        submitted[1].subImage.imageRect.offset.x,submitted[1].subImage.imageRect.offset.y,
        submitted[1].subImage.imageRect.extent.width,submitted[1].subImage.imageRect.extent.height,
        !g_fovMutable.load()?"runtime keeps each eye's field: cropped":g_eyeFovCropAlways.load()?"cropped by EyeFovCrop=1":"whole picture, wider field declared");
    g_log(line);
}

void PublishStereoAdvice(const XrView* views,uint32_t count,unsigned width,unsigned height) noexcept {
    if(count<2 || !width || !height) return;
    float halfVertical=0,halfHorizontal=0;
    for(uint32_t i=0;i<count && i<2;++i) {
        halfVertical=std::fmax(halfVertical,std::fabs(views[i].fov.angleUp));
        halfVertical=std::fmax(halfVertical,std::fabs(views[i].fov.angleDown));
        halfHorizontal=std::fmax(halfHorizontal,std::fabs(views[i].fov.angleLeft));
        halfHorizontal=std::fmax(halfHorizontal,std::fabs(views[i].fov.angleRight));
    }
    if(!std::isfinite(halfVertical) || halfVertical<0.2f || halfVertical>1.4f) return;
    // The runtime's own frustum, in full, once.
    //
    // Everything downstream uses one symmetric half-angle taken from the
    // largest of the four, and the horizontal one is then invented from the
    // render aspect. If the real frustum is narrower or lopsided, the
    // difference is world drawn into pixels the headset never shows.
    static bool reported=false;
    if(!reported) {
        reported=true;
        for(uint32_t i=0;i<count && i<2;++i)
            SetStatusf("view %u frustum: left=%.2f right=%.2f up=%.2f down=%.2f deg "
                       "(horizontal %.2f, vertical %.2f)",i,
                views[i].fov.angleLeft*57.29578f,views[i].fov.angleRight*57.29578f,
                views[i].fov.angleUp*57.29578f,views[i].fov.angleDown*57.29578f,
                (views[i].fov.angleRight-views[i].fov.angleLeft)*57.29578f,
                (views[i].fov.angleUp-views[i].fov.angleDown)*57.29578f);
        const float aspect=static_cast<float>(width)/static_cast<float>(height);
        SetStatusf("rendered frustum: %ux%u at aspect %.4f gives horizontal %.2f deg "
                   "from vertical %.2f deg",width,height,aspect,
            2*std::atan(std::tan(halfVertical)*aspect)*57.29578f,2*halfVertical*57.29578f);
    }
    const float dx=views[1].pose.position.x-views[0].pose.position.x;
    const float dy=views[1].pose.position.y-views[0].pose.position.y;
    const float dz=views[1].pose.position.z-views[0].pose.position.z;
    const float ipd=std::sqrt(dx*dx+dy*dy+dz*dz);
    AcquireSRWLockExclusive(&g_poseLock);
    g_eyePose[0]=views[0].pose;
    g_eyePose[1]=views[1].pose;
    ReleaseSRWLockExclusive(&g_poseLock);
    if(!g_eyePoseValid) {
        g_writtenPose[0]=views[0].pose;
        g_writtenPose[1]=views[1].pose;
        g_writtenPoseValid[0]=g_writtenPoseValid[1]=true;
    }
    g_eyePoseValid=true;
    // Scaling this down renders less of the world into the same image, which is
    // both sharper per degree and cheaper, at the cost of black edges.
    const float scale=g_fovScale.load(std::memory_order_relaxed);
    const float margin=g_fovMargin.load(std::memory_order_relaxed);
    g_adviceFov.store(2*(halfVertical+margin)*scale,std::memory_order_relaxed);
    if(std::isfinite(halfHorizontal) && halfHorizontal>0.2f && halfHorizontal<1.5f) {
        g_headsetHalfHorizontal.store(halfHorizontal,std::memory_order_relaxed);
        g_headsetHalfVertical.store(halfVertical,std::memory_order_relaxed);
    }
    if(std::isfinite(ipd) && ipd>0.03f && ipd<0.09f) g_adviceIpd.store(ipd,std::memory_order_relaxed);
    g_adviceReady.store(true,std::memory_order_relaxed);
}

// Presentation frustum. Binocular zoom deliberately displays the narrow native
// scene across this normal frustum; narrowing both would cancel the zoom.
XrFovf RenderedFov(unsigned width,unsigned height) noexcept {
    const float halfVertical=g_adviceFov.load(std::memory_order_relaxed)*0.5f;
    const float aspect=static_cast<float>(width)/static_cast<float>(height);
    const float halfHorizontal=std::atan(std::tan(halfVertical)*aspect);
    XrFovf fov{};
    fov.angleLeft=-halfHorizontal;
    fov.angleRight=halfHorizontal;
    fov.angleUp=halfVertical;
    fov.angleDown=-halfVertical;
    return fov;
}

bool BeginDisplayFrame(PerfBatch& perf,bool early) noexcept {
    if(g_displayFramePrepared) return true;
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    if(XR_FAILED(MeasureCpu(perf,PerfStage::WaitFrame,[&]() {
        return g_api.waitFrame(g_session,&waitInfo,&frameState);
    }))) return false;
    // Observe the actual wait location; early calls are outside DisplayFrame timing.
    // A predicted period is the runtime's app cadence, not necessarily HMD Hz.
    static XrTime previousPrediction=0;
    static ULONGLONG nextPacingLog=0;
    const auto predictionStep=frameState.predictedDisplayTime-previousPrediction;
    previousPrediction=frameState.predictedDisplayTime;
    const auto pacingNow=GetTickCount64();
    if(g_log && pacingNow>=nextPacingLog) {
        nextPacingLog=pacingNow+5000;
        char report[256]{};
        std::snprintf(report,sizeof(report),
            "XRTIMING predictedPeriodMs=%.4f predictedStepMs=%.4f shouldRender=%u waitLocation=%s",
            static_cast<double>(frameState.predictedDisplayPeriod)/1000000.0,
            predictionStep>0 && predictionStep<1000000000?static_cast<double>(predictionStep)/1000000.0:0.0,
            frameState.shouldRender,early?"before-main-world":"Present-after-world");
        g_log(report);
    }
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if(XR_FAILED(MeasureCpu(perf,PerfStage::BeginFrame,[&]() {
        return g_api.beginFrame(g_session,&beginInfo);
    }))) return false;

    g_preparedFrame=frameState;
    g_displayFramePrepared=true;
    TraceFrameBegin(frameState.predictedDisplayTime,frameState.predictedDisplayPeriod);
    return true;
}

void RunDisplayFrame(ID3D11Texture2D* backBuffer,unsigned width,unsigned height,
                     unsigned sampleCount) noexcept {
    HmdSample renderedHead{};
    const bool haveRenderedHead=ConsumeNativeRenderPose(renderedHead);
    auto nativeImages=TakeNativeWorldImages(width,height);
    bool cockpitHud=false;
    if(nativeImages.ready && nativeImages.cockpitMatched) {
        auto* hud=g_uiLayer.load()?UiTexture():nullptr;
        const bool left=DrawCockpit(g_context,nativeImages.eye[0].Get(),nativeImages.view[0],nativeImages.projection[0],nativeImages.cockpit,hud,nativeImages.frame);
        const bool right=DrawCockpit(g_context,nativeImages.eye[1].Get(),nativeImages.view[1],nativeImages.projection[1],nativeImages.cockpit,hud,nativeImages.frame);
        nativeImages.ready=left&&right;cockpitHud=nativeImages.ready&&hud;
    }
    DiscardCockpitLighting(); // retain small private CBs, release native resources each display frame
    struct NativeWeaponFrame {
        bool attempted=false,delivered=false;
        ~NativeWeaponFrame() {if(attempted && !delivered) DiscardWeaponStereoFrame();}
    } nativeWeapon{nativeImages.attempted,false};
    DisplayPerf timing;
    auto& perf=timing.batch;
    InvalidateMirrorImage(MirrorImage::Ui);
    InvalidateMirrorImage(MirrorImage::Reticle);
    bool sessionRunning=g_sessionRunning.load();
    MeasureCpu(perf,PerfStage::PollEvents,[&]() { PollSessionEvents(sessionRunning); });
    g_sessionRunning.store(sessionRunning);
    if(!sessionRunning || g_stop.load()) return;

    if(!BeginDisplayFrame(perf,false)) return;
    const XrFrameState frameState=g_preparedFrame;

    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    MeasureCpu(perf,PerfStage::LocatePublish,[&]() {
        ReadHands(frameState.predictedDisplayTime);
        if(XR_SUCCEEDED(g_api.locateSpace(g_viewSpace,g_refSpace,frameState.predictedDisplayTime,&location)))
            { g_lastViewPose=location.pose; Publish(location,frameState.predictedDisplayTime); }
    });

    // Every mode needs the eye frusta: the projection layer submits them, and
    // the quad path still reports the advice so a mode change is instant.
    XrView views[2]{{XR_TYPE_VIEW},{XR_TYPE_VIEW}};
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime=frameState.predictedDisplayTime;
    locate.space=g_refSpace;
    uint32_t viewCount=0;
    if(XR_SUCCEEDED(g_api.locateViews(g_session,&locate,&viewState,2,&viewCount,views))
       && (viewState.viewStateFlags&XR_VIEW_STATE_ORIENTATION_VALID_BIT))
        PublishStereoAdvice(views,viewCount,width,height);

    const int force=g_displayForce.load(std::memory_order_relaxed);
    const bool sceneLive=force?force==2
        :GetTickCount64()-g_frameEyeStamp.load(std::memory_order_relaxed)<750;
    XrDisplayMode mode=sceneLive?g_displayMode.load(std::memory_order_relaxed)
                                      :XrDisplayMode::Quad;
    // Never silently send a depth-warped or stale pair as native stereo. A failed
    // native contract shows the board until a complete, pose-matched pair arrives.
    if(mode!=XrDisplayMode::Quad && NativeWorldEnabled() &&
       (!nativeImages.ready || !haveRenderedHead)) mode=XrDisplayMode::Quad;
    const int eye=sceneLive?g_frameEye.load(std::memory_order_relaxed):-1;
    if(nativeImages.ready && eye!=0) mode=XrDisplayMode::Quad;
    if(NativeWorldEnabled() && g_log) {
        static ULONGLONG nextNativeReport=0;
        // Report transient missing-pose failures too; a five-second periodic
        // sample otherwise hides the single-frame board flash during a jump.
        static ULONGLONG nextFailureReport=0;
        const auto now=GetTickCount64();
        const bool failed=sceneLive && mode==XrDisplayMode::Quad;
        if(now>=nextNativeReport || (failed && now>=nextFailureReport)) {
            if(failed) nextFailureReport=now+1000;
            nextNativeReport=GetTickCount64()+5000;char line[224]{};
            std::snprintf(line,sizeof(line),"NATIVEPRESENT frame=%llu images=%d matchedHead=%d cockpit=%d eye=%d mode=%s (native mode never depth-warps world)",
                nativeImages.frame,nativeImages.ready,haveRenderedHead,nativeImages.cockpitMatched,eye,mode==XrDisplayMode::Quad?"board":"projection");g_log(line);
        }
    }
    // Freeze the switch for both eyes. Only filter when native UI was separated.
    const bool antiAlias=SceneAAEnabled() && mode!=XrDisplayMode::Quad && g_uiLayer.load() && UiTexture();
    const bool trialScene=g_trial.started?sceneLive:(sceneLive && SceneDepth()!=nullptr);
    const float effectiveSteps=g_trial.Update(g_trialEnabled.load(),trialScene,GetTickCount64(),g_warpSteps.load());
    g_trialPhase=g_trial.phase;
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerProjectionView projectionViews[2]{
        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerQuad uiQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad reticleQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad reticleQuad2{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerQuad clusterQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    UiClusterLayout clusterLayout{}; bool clusterDrew=false;
    const XrCompositionLayerBaseHeader* layers[6]={};
    uint32_t layerCount=0;
    g_lastLayerKind.store(0,std::memory_order_relaxed);
    // In stereo the game gave us one eye this frame; the other keeps the image
    // already sitting in its swapchain.
    // Resizing destroys images and both eye handles. Choose them only after
    // setup, never retain a pointer into the deleted image arrays across it.
    const bool displayReady=frameState.shouldRender && backBuffer
        && MeasureCpu(perf,PerfStage::SwapchainSetup,[&]() { return CreateDisplaySwapchain(width,height); });
    const bool stereo=displayReady && mode==XrDisplayMode::ProjectionStereo && eye>=0 && CreateRightEyeSwapchain();
    // A native pair requires two chains even on the first frame/after resize.
    // An allocation failure must not turn it into legacy mono projection.
    if(NativeWorldEnabled() && mode!=XrDisplayMode::Quad && !stereo) mode=XrDisplayMode::Quad;
    const bool native=stereo && nativeImages.ready;
    // Do the two native pictures really differ, the right way round? (eye_check.h)
    if(native && displayReady) EyeCheckFrame(g_context,nativeImages.eye[0].Get(),nativeImages.eye[1].Get(),g_log);
    XrSwapchain target=(stereo && eye==1)?g_swapchainRight:g_swapchain;
    ID3D11Texture2D** targetImages=(stereo && eye==1)?g_imagesRight:g_images;
    const uint32_t targetCount=(stereo && eye==1)?g_imageCountRight:g_imageCount;
    if(!frameState.shouldRender) g_notRendering.fetch_add(1,std::memory_order_relaxed);
    if(displayReady) {
        BeginGpuFrame(g_device,g_context,sceneLive,width,height,static_cast<unsigned>(effectiveSteps),g_trial.phase);
        uint32_t index=0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(XR_SUCCEEDED(MeasureCpu(perf,PerfStage::AcquireImage,[&]() {
            return g_api.acquireImage(target,&acquire,&index);
        }))) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout=kImageWait;
            const bool ready=XR_SUCCEEDED(MeasureCpu(perf,PerfStage::WaitImage,[&]() {
                return g_api.waitImage(target,&wait);
            }));
            const bool copied=ready && CopyIntoSwapchain(native?nativeImages.eye[0].Get():backBuffer,native?1:sampleCount,index,
                                                         targetImages,targetCount,perf);
            bool warped=false;
            // Which pixels are HUD, worked out once for the whole frame. The
            // passes below read the answer instead of each deciding again.
            if(!native && copied && g_uiLayer.load(std::memory_order_relaxed) && g_resolved
               && !UiTexture() && !PreUiColour() && SceneColour()) {
                WarpParams maskParams{};
                BuildUiMask(g_device,g_context,g_resolved,SceneColour(),maskParams);
            }
            // The drawn eye is a straight copy, so the HUD is still in it. With
            // the panel on it has to come out of here as well, or it would be
            // lifted out of the built eye only and sit in one eye alone.
            bool eraseExact=false;
            auto* eraseHudless=HudlessFor(eraseExact);
            WarpParams erase{};
            SetReticleErase(erase,eraseExact);
            if(!native && copied && g_uiLayer.load(std::memory_order_relaxed) && g_resolved && eraseHudless
               && !UiTexture() && mode!=XrDisplayMode::Quad) {
                erase.eraseOnly=true;
                erase.useHudless=true;
                erase.uiMode=2.0f;
                erase.exact=eraseExact;
                WarpEye(g_device,g_context,g_resolved,nullptr,targetImages[index],eraseHudless,erase);
            }
            if(copied && stereo) {
                if(eye==g_lastCopiedEye) g_eyeRepeats.fetch_add(1,std::memory_order_relaxed);
                g_lastCopiedEye=eye;
                g_eyeConsumed.store(true,std::memory_order_release);
                if(native) warped=CopyNativeRightEye(nativeImages.eye[1].Get(),perf,antiAlias,&nativeImages);
                else if(g_warpEnabled.load(std::memory_order_relaxed) && g_resolved)
                    warped=BuildOtherEye(eye,perf,effectiveSteps,antiAlias);
                CompositeWeaponStereo(g_context,targetImages[index],static_cast<unsigned>(eye));
                if(native) CompositeScopeLens(targetImages[index],static_cast<unsigned>(eye),nativeImages);
            }
            if(copied) ApplySceneAA(g_context,targetImages[index],antiAlias);
            if(copied && mode!=XrDisplayMode::Quad) CompositeWorldUi(targetImages[index]);
            // Direct UI capture removes menus from the game back buffer too.
            // A pause/title board must receive that complete image, without HUD
            // cuts or cockpit placement, before its XR image is released.
            if(copied && mode==XrDisplayMode::Quad && UiTexture())
                CompositeOverlayImage(g_device,g_context,UiTexture(),targetImages[index]);
            static ULONGLONG aaReport=0;
            const auto aaNow=GetTickCount64();
            if(g_log && aaNow-aaReport>=5000) { aaReport=aaNow;g_log(SceneAAStatus()); }
            // Snapshot before release; the runtime owns the image afterwards.
            // Only keep left when needed as the same fallback submitted to right.
            if(copied && g_desktopMirror.load() && mode!=XrDisplayMode::Quad
                && (!stereo || eye==1 || !warped))
                SnapshotMirror(g_context,targetImages[index],stereo && eye==1?MirrorImage::Right:MirrorImage::Left,
                               static_cast<DXGI_FORMAT>(g_swapchainFormat));
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            const auto released=MeasureCpu(perf,PerfStage::ReleaseImage,[&]() { return g_api.releaseImage(target,&release); });
            if(copied && XR_SUCCEEDED(released)) g_haveImage[(stereo && eye==1)?1:0]=true;
            if(native && copied && warped && XR_SUCCEEDED(released)) nativeWeapon.delivered=true;
            if(native && !nativeWeapon.delivered) g_haveImage[0]=g_haveImage[1]=false;
            if(g_haveImage[0] && mode==XrDisplayMode::Quad) {
                // Head locked, so the screen stays in front of the eyes while the
                // head still aims the game camera through the input path.
                quad.layerFlags=0;
                quad.space=g_quadSpace;
                quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain=g_swapchain;
                quad.subImage.imageRect.offset={0,0};
                quad.subImage.imageRect.extent={static_cast<int32_t>(width),static_cast<int32_t>(height)};
                quad.subImage.imageArrayIndex=0;
                quad.pose.orientation.w=1;
                quad.pose.position.z=-g_quadDistance;
                if(g_boardWorldLocked.load(std::memory_order_relaxed) && g_refSpace!=XR_NULL_HANDLE) {
                    const auto now=GetTickCount64();
                    if(g_boardRecenter.exchange(false) || !g_boardLastShown || now-g_boardLastShown>1000) {
                        // In front of where the head faces, upright, at head
                        // height, turned to face it. Only the heading is used, so
                        // looking down at the moment it appears does not tip it.
                        const auto& q=g_lastViewPose.orientation;
                        const float fx=-2*(q.x*q.z+q.w*q.y), fz=-(1-2*(q.x*q.x+q.y*q.y));
                        const float yaw=std::atan2(-fx,-fz);
                        const float flat=std::sqrt(fx*fx+fz*fz);
                        const float dx=flat>1e-4f?fx/flat:0, dz=flat>1e-4f?fz/flat:-1;
                        g_boardPose.orientation={0,std::sin(yaw*0.5f),0,std::cos(yaw*0.5f)};
                        g_boardPose.position={g_lastViewPose.position.x+dx*g_quadDistance,
                                              g_lastViewPose.position.y,
                                              g_lastViewPose.position.z+dz*g_quadDistance};
                    }
                    g_boardLastShown=now;
                    quad.space=g_refSpace;
                    quad.pose=g_boardPose;
                }
                quad.size.width=g_quadWidth;
                quad.size.height=g_quadWidth/PanelAspect(width,height);
                layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
                layerCount=1;
                g_lastLayerKind.store(1,std::memory_order_relaxed);
                g_layers.fetch_add(1,std::memory_order_relaxed);
            } else if(mode!=XrDisplayMode::Quad && (!native || nativeWeapon.delivered) && g_eyePoseValid
                      && g_adviceReady.load(std::memory_order_relaxed)
                      && (g_haveImage[0] || (stereo && g_haveImage[1]))) {
                if(!copied) g_reshownFrames.fetch_add(1,std::memory_order_relaxed);
                // The game drew a symmetric wide frustum, so that is what gets
                // declared; the runtime warps it into each eye from there.
                const XrFovf fov=RenderedFov(width,height);
                // Only the eye that received this frame gets this frame's pose.
                XrPosef drawnFrom[2]={g_eyePose[0],g_eyePose[1]};
                AcquireSRWLockShared(&g_poseLock);
                if(g_cameraPoseValid) { drawnFrom[0]=g_cameraPose[0]; drawnFrom[1]=g_cameraPose[1]; }
                ReleaseSRWLockShared(&g_poseLock);
                // The game queues its logic cameras. The newest camera pose can
                // belong to a frame that has NOT rendered yet (measured 0.110).
                // Match the native view, never assume a fixed one-frame delay.
                const bool matched=haveRenderedHead && (native
                    ? ParallelRenderedEyePoses(renderedHead,nativeImages.physicalIpd,drawnFrom)
                    : RenderedEyePoses(renderedHead,location,views,viewState.viewStateFlags,viewCount,drawnFrom));
                if(RenderedPoseEnabled()) {
                    static unsigned long long matches=0,fallbacks=0;
                    static ULONGLONG nextReport=0;
                    matched?++matches:++fallbacks;
                    const auto now=GetTickCount64();
                    if(g_log && now>=nextReport) {
                        nextReport=now+5000;char line[192]{};
                        std::snprintf(line,sizeof(line),"RENDERPOSE matched=%llu fallback=%llu sample=%llu valid=%d",
                            matches,fallbacks,renderedHead.frame,matched?1:0);g_log(line);
                    }
                }
                if(stereo) {
                    if(copied) {
                        g_writtenPose[eye]=drawnFrom[eye];
                        g_writtenPoseValid[eye]=true;
                    }
                    // The built eye came out of this same frame, so it carries
                    // this frame's pose as well; that is the whole gain over
                    // leaving it on the image it had.
                    if(warped) {
                        const int other=eye==1?0:1;
                        g_writtenPose[other]=drawnFrom[other];
                        g_writtenPoseValid[other]=true;
                    }
                } else if(copied) {
                    g_writtenPose[0]=drawnFrom[0];
                    g_writtenPose[1]=drawnFrom[1];
                    g_writtenPoseValid[0]=g_writtenPoseValid[1]=true;
                }
                // No depth (e.g. during a size transition) means no built eye.
                // Use the released native eye for both views until both are
                // available, rather than submit a never-released XR swapchain.
                // A chain populated in a previous mission is not a current eye.
                // If this frame has no usable depth, show this frame's native
                // image in BOTH eyes instead of pairing it with stale terrain.
                // Preserve alternating native stereo when depth warping is off.
                const bool freshNativeFallback=stereo && copied && XR_SUCCEEDED(released)
                    && (native || g_warpEnabled.load(std::memory_order_relaxed)) && !warped;
                const bool pair=stereo && !freshNativeFallback && g_haveImage[0] && g_haveImage[1];
                const uint32_t fallbackEye=freshNativeFallback?static_cast<uint32_t>(eye):(g_haveImage[0]?0u:1u);
                for(uint32_t i=0;i<2;++i) {
                    const uint32_t sourceEye=pair?i:fallbackEye;
                    projectionViews[i].pose=
                        g_writtenPoseValid[sourceEye]?g_writtenPose[sourceEye]:g_eyePose[sourceEye];
                    projectionViews[i].fov=fov;
                    projectionViews[i].subImage.swapchain=
                        (stereo && sourceEye==1)?g_swapchainRight:g_swapchain;
                    projectionViews[i].subImage.imageRect.offset={0,0};
                    projectionViews[i].subImage.imageRect.extent={
                        static_cast<int32_t>(width),static_cast<int32_t>(height)};
                    projectionViews[i].subImage.imageArrayIndex=0;
                    // A runtime whose views keep their own field of view gets that
                    // field, and the part of the picture drawn over it (eye_crop.h).
                    if((!g_fovMutable.load(std::memory_order_relaxed) || g_eyeFovCropAlways.load(std::memory_order_relaxed))
                       && viewCount>=2) {
                        const XrFovf& eyeFov=views[i].fov;
                        // Cut from the frustum the game really drew this picture over
                        // (its projection matrix), not the one it was asked for.
                        XrFovf drawn=fov;
                        float l=0,r=0,u=0,d=0;
                        if(native && FrustumFromProjection(nativeImages.projection[sourceEye].m,l,r,u,d)) {
                            drawn.angleLeft=l; drawn.angleRight=r; drawn.angleUp=u; drawn.angleDown=d;
                        }
                        const EyeCrop crop=CropForEye(drawn.angleLeft,drawn.angleRight,drawn.angleUp,drawn.angleDown,
                            eyeFov.angleLeft,eyeFov.angleRight,eyeFov.angleUp,eyeFov.angleDown,
                            static_cast<int>(width),static_cast<int>(height));
                        if(crop.width<static_cast<int>(width) || crop.height<static_cast<int>(height)) {
                            projectionViews[i].fov=eyeFov;
                            projectionViews[i].subImage.imageRect.offset={crop.x,crop.y};
                            projectionViews[i].subImage.imageRect.extent={crop.width,crop.height};
                        }
                    }
                }
                projection.layerFlags=0;
                projection.space=g_refSpace;
                projection.viewCount=2;
                projection.views=projectionViews;
                static ULONGLONG nextViewsReport=0;
                if(g_log && viewCount>=2 && GetTickCount64()>=nextViewsReport) {
                    nextViewsReport=GetTickCount64()+5000;
                    ReportViews(views,projectionViews,projectionViews[0].subImage.swapchain==g_swapchainRight,
                                projectionViews[1].subImage.swapchain==g_swapchainRight,fov);
                    // What the game really drew, against the field it was asked for:
                    // the crop above is only as exact as the first.
                    float d[2][4]{}; bool have[2]{};
                    for(unsigned e=0;e<2 && native;++e)
                        have[e]=FrustumFromProjection(nativeImages.projection[e].m,d[e][0],d[e][1],d[e][2],d[e][3]);
                    if(have[0] && have[1] && g_log) {
                        char line[256]{};
                        const float k=57.29578f;
                        std::snprintf(line,sizeof(line),"FRUSTUM drawn by the game L[%.2f %.2f %.2f %.2f] R[%.2f %.2f %.2f %.2f] "
                            "assumed [%.2f %.2f %.2f %.2f] deg, picture %ux%u",
                            d[0][0]*k,d[0][1]*k,d[0][2]*k,d[0][3]*k,d[1][0]*k,d[1][1]*k,d[1][2]*k,d[1][3]*k,
                            fov.angleLeft*k,fov.angleRight*k,fov.angleUp*k,fov.angleDown*k,width,height);
                        g_log(line);
                    }
                }
                layers[0]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
                layerCount=1;
                g_lastLayerKind.store(2,std::memory_order_relaxed);
                g_lastFovDeg.store(static_cast<int>((fov.angleUp-fov.angleDown)*57.29578f),
                                   std::memory_order_relaxed);
                g_layers.fetch_add(1,std::memory_order_relaxed);
                g_projectionLayers.fetch_add(1,std::memory_order_relaxed);
            }
        }
    }
    // The HUD goes on top of whatever the world layer turned out to be, so it is
    // built last and only when there is a world layer to put it over. Nothing
    // below it may be filled in until it has had its turn to add itself.
    bool panelExact=false;
    auto* panelHudless=HudlessFor(panelExact);
    if(layerCount && mode!=XrDisplayMode::Quad && g_uiLayer.load(std::memory_order_relaxed)
       && (panelHudless || UiTexture()) && g_resolved && CreateUiSwapchain()) {
        GpuScope gpu(GpuStage::Ui);
        uint32_t uiIndex=0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if(XR_SUCCEEDED(g_api.acquireImage(g_swapchainUi,&acquire,&uiIndex)) && uiIndex<g_imageCountUi) {
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout=kImageWait;
            bool drew=false;
            auto* redirected=UiTexture();
            const int reticleSide=ReticleSideForHeight(height);
            const bool wantReticle=g_reticleFollows.load(std::memory_order_relaxed)
                                   && g_aimKnown.load() && g_refSpace && reticleSide>=16
                                   && reticleSide<static_cast<int>(height);
            // With the VR reticle drawn and the game's own switched off at the
            // source, the middle of the panel is the HUD like the rest of it:
            // MISSION CLEAR and online chat bubbles live there too, and cutting
            // the square out for the crosshair took them with it.
            const bool vrGlyph=wantReticle && NativeWorldEnabled()
                               && g_aimFromHand.load(std::memory_order_relaxed);
            const bool keepMiddle=vrGlyph && redirected && NativeCrosshairReady();
            if(keepMiddle) HideNativeCrosshairFor(GetTickCount64());
            if(XR_SUCCEEDED(g_api.waitImage(g_swapchainUi,&wait))) {
                WarpParams params{};
                params.uiOnly=true;
                params.useHudless=true;
                params.exact=panelExact;
                if(wantReticle && !keepMiddle) {
                    params.reticleHalf[0]=0.5f*static_cast<float>(reticleSide)
                                          /static_cast<float>(width);
                    params.reticleHalf[1]=0.5f*static_cast<float>(reticleSide)
                                          /static_cast<float>(height);
                }
                // Where the HUD was drawn into a texture of its own, that is the
                // panel: no comparing, no threshold, and the alpha is the one the
                // game drew with rather than one we decided.
                params.uiDirect=redirected!=nullptr;
                drew=WarpEye(g_device,g_context,redirected?redirected:g_resolved,nullptr,
                             g_imagesUi[uiIndex],redirected?nullptr:panelHudless,params);
                if(drew && cockpitHud) {
                    const UiRect all{0,0,1,1};CutUiRects(g_device,g_context,g_imagesUi[uiIndex],&all,1);
                }
                if(drew && g_desktopMirror.load()) SnapshotMirror(g_context,g_imagesUi[uiIndex],MirrorImage::Ui,static_cast<DXGI_FORMAT>(g_swapchainFormat));
                // The compact HUD: its pieces are cut out of the panel here and
                // drawn again, smaller and together, on a picture of their own.
                if(drew && !cockpitHud && redirected && g_clusterOn.load(std::memory_order_relaxed)) {
                    AcquireSRWLockShared(&g_clusterLock); clusterLayout=g_clusterLayout; ReleaseSRWLockShared(&g_clusterLock);
                    if(clusterLayout.count && clusterLayout.count<=8) {
                        UiRect cuts[8]; for(unsigned i=0;i<clusterLayout.count;++i) cuts[i]=clusterLayout.items[i].source;
                        CutUiRects(g_device,g_context,g_imagesUi[uiIndex],cuts,clusterLayout.count);
                        const float aspect=PanelAspect(width,height);
                        float cw=clusterLayout.canvasWidth*static_cast<float>(height)*aspect, ch=clusterLayout.canvasHeight*static_cast<float>(height);
                        cw=cw<64?64:(cw>2048?2048:cw); ch=ch<64?64:(ch>2048?2048:ch);
                        if(CreateClusterSwapchain(static_cast<uint32_t>(cw),static_cast<uint32_t>(ch))) {
                            uint32_t index=0; XrSwapchainImageAcquireInfo take{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                            if(XR_SUCCEEDED(g_api.acquireImage(g_swapchainCluster,&take,&index)) && index<g_imageCountCluster) {
                                XrSwapchainImageWaitInfo hold{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; hold.timeout=kImageWait;
                                if(XR_SUCCEEDED(g_api.waitImage(g_swapchainCluster,&hold)))
                                    clusterDrew=ComposeUiCluster(g_device,g_context,redirected,g_imagesCluster[index],clusterLayout);
                                XrSwapchainImageReleaseInfo let{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                                g_api.releaseImage(g_swapchainCluster,&let);
                            }
                        }
                        if(clusterDrew) g_clusterComposites.fetch_add(1,std::memory_order_relaxed);
                        else g_clusterFailures.fetch_add(1,std::memory_order_relaxed);
                    }
                }
            }
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            g_api.releaseImage(g_swapchainUi,&release);
            bool drewReticle=false;
            if(drew && wantReticle && CreateReticleSwapchain(reticleSide)) {
                uint32_t index=0;
                XrSwapchainImageAcquireInfo take{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if(XR_SUCCEEDED(g_api.acquireImage(g_swapchainReticle,&take,&index))
                   && index<g_imageCountReticle) {
                    XrSwapchainImageWaitInfo hold{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                    hold.timeout=kImageWait;
                    if(XR_SUCCEEDED(g_api.waitImage(g_swapchainReticle,&hold))) {
                        WarpParams only{};
                        only.uiOnly=true;
                        only.useHudless=true;
                        only.exact=panelExact;
                        only.uiDirect=redirected!=nullptr;
                        only.reticleOnly=true;
                        only.drawReticle=NativeWorldEnabled() && g_aimFromHand.load(std::memory_order_relaxed);
                        only.lockReticle=only.drawReticle && ReadAimHud().lockWeapon;
                        only.reticleHalf[0]=0.5f*static_cast<float>(reticleSide)
                                            /static_cast<float>(width);
                        only.reticleHalf[1]=0.5f*static_cast<float>(reticleSide)
                                            /static_cast<float>(height);
                        drewReticle=WarpEye(g_device,g_context,
                                            redirected?redirected:g_resolved,nullptr,
                                            g_imagesReticle[index],
                                            redirected?nullptr:panelHudless,only);
                        if(drewReticle && g_desktopMirror.load()) SnapshotMirror(g_context,g_imagesReticle[index],MirrorImage::Reticle,static_cast<DXGI_FORMAT>(g_swapchainFormat));
                        if(FrameDumpRequested()) {
                            if(g_log) {
                                char report[256]{};
                                std::snprintf(report,sizeof(report),"RETICLEDUMP direct=%d drawn=%d crop=%d aim=(%.4f,%.4f,%.4f) head=(%.4f,%.4f,%.4f,%.4f)",
                                    redirected!=nullptr,drewReticle,reticleSide,g_aimX.load(),g_aimY.load(),g_aimZ.load(),
                                    g_lastViewPose.orientation.x,g_lastViewPose.orientation.y,g_lastViewPose.orientation.z,g_lastViewPose.orientation.w);
                                g_log(report);
                            }
                            // Read XR output only while acquired, before release.
                            WriteFrameDump(g_device,g_context,g_resolved,SceneDepth(),SceneColour(),PreUiColour(),
                                g_dumpDirectory,g_log,redirected,drewReticle?g_imagesReticle[index]:nullptr);
                        }
                    }
                    XrSwapchainImageReleaseInfo let{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    g_api.releaseImage(g_swapchainReticle,&let);
                }
            }
            if(drew && g_quadSpace!=XR_NULL_HANDLE) {
                // Head locked and blended over the world, so the HUD sits at a size
                // and distance we choose instead of wherever the field of view puts it.
                uiQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                uiQuad.space=g_quadSpace;
                uiQuad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                uiQuad.subImage.swapchain=g_swapchainUi;
                uiQuad.subImage.imageRect.offset={0,0};
                uiQuad.subImage.imageRect.extent={static_cast<int32_t>(width),
                                                  static_cast<int32_t>(height)};
                uiQuad.subImage.imageArrayIndex=0;
                uiQuad.pose.orientation.w=1;
                uiQuad.pose.position.z=-g_uiDistance.load(std::memory_order_relaxed);
                const float uiWidth=g_uiWidth.load(std::memory_order_relaxed);
                uiQuad.size.width=uiWidth;
                uiQuad.size.height=uiWidth/PanelAspect(width,height);
                layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&uiQuad);
                g_uiLayers.fetch_add(1,std::memory_order_relaxed);
                if(clusterDrew && g_clusterMadeW && g_clusterMadeH) {
                    clusterQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                    clusterQuad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                    clusterQuad.subImage.swapchain=g_swapchainCluster;
                    clusterQuad.subImage.imageRect.offset={0,0};
                    clusterQuad.subImage.imageRect.extent={static_cast<int32_t>(g_clusterMadeW),static_cast<int32_t>(g_clusterMadeH)};
                    clusterQuad.subImage.imageArrayIndex=0;
                    const float aspect=PanelAspect(width,height);
                    const float uiHeight=uiWidth/aspect;
                    const int place=g_clusterPlace.load(std::memory_order_relaxed);
                    const bool onWrist=place!=0 && g_refSpace!=XR_NULL_HANDLE && g_clusterPoseValid.load(std::memory_order_relaxed);
                    if(onWrist) {
                        AcquireSRWLockShared(&g_clusterLock); clusterQuad.pose=g_clusterPose; ReleaseSRWLockShared(&g_clusterLock);
                        clusterQuad.space=g_refSpace;
                        const float w=g_clusterWristWidth.load(std::memory_order_relaxed);
                        clusterQuad.size.width=w;
                        clusterQuad.size.height=w*clusterLayout.canvasHeight/(clusterLayout.canvasWidth*aspect);
                    } else {
                        // The panel's bottom-right corner, in from the edges by the margins.
                        const float w=clusterLayout.canvasWidth*uiWidth, h=clusterLayout.canvasHeight*uiHeight;
                        clusterQuad.space=g_quadSpace;
                        clusterQuad.pose.orientation={0,0,0,1};
                        clusterQuad.pose.position={uiWidth*0.5f-w*0.5f-g_clusterMarginRight.load(std::memory_order_relaxed)*uiWidth,
                                                   -uiHeight*0.5f+h*0.5f+g_clusterMarginBottom.load(std::memory_order_relaxed)*uiHeight,
                                                   -g_uiDistance.load(std::memory_order_relaxed)};
                        clusterQuad.size.width=w; clusterQuad.size.height=h;
                    }
                    layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&clusterQuad);
                }

                // And the middle of it again, turned to the weapon. The panel
                // pass leaves that rectangle empty, so it appears once.
                //
                // Not while a scope shows its own picture: the middle of the HUD
                // then holds the game's zoom sight, which it places for a zoomed
                // screen the headset no longer shows, so it stood off the shot
                // (hardware 2026-10-01). The lens carries its own reticle, as True
                // Scopes hides the vanilla reticle quad while its lens is live.
                const auto scopeView=ReadScopeView();
                const bool scopeOwnsReticle=scopeView.active && GetTickCount64()-scopeView.at<250;
                if(drewReticle && !scopeOwnsReticle) {
                    float aim[3]={g_aimX.load(std::memory_order_relaxed),
                                  g_aimY.load(std::memory_order_relaxed),
                                  g_aimZ.load(std::memory_order_relaxed)};
                    const float distance=g_reticleDistance.load(std::memory_order_relaxed);
                    const float zoom=g_binocularZoom.load(std::memory_order_relaxed);
                    XrPosef reticleHead=g_lastViewPose;
                    if(zoom>1 && projection.viewCount==2) {
                        // Match the pose attached to the displayed world, not a
                        // newer tracking sample taken after that image was drawn.
                        reticleHead=projectionViews[0].pose;
                        reticleHead.position.x=(projectionViews[0].pose.position.x+projectionViews[1].pose.position.x)*0.5f;
                        reticleHead.position.y=(projectionViews[0].pose.position.y+projectionViews[1].pose.position.y)*0.5f;
                        reticleHead.position.z=(projectionViews[0].pose.position.z+projectionViews[1].pose.position.z)*0.5f;
                    }
                    float displayAim[3]{};
                    if(ZoomReticleDirection(aim,reticleHead.orientation,zoom,displayAim)
                       && AimReticlePose(displayAim,reticleHead,distance,reticleQuad.pose)) {
                        reticleQuad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                        reticleQuad.space=g_refSpace;
                        reticleQuad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
                        reticleQuad.subImage.swapchain=g_swapchainReticle;
                        reticleQuad.subImage.imageRect.offset={0,0};
                        reticleQuad.subImage.imageRect.extent={reticleSide,reticleSide};
                        reticleQuad.subImage.imageArrayIndex=0;
                        // The same angular size it had on the panel, so moving it
                        // further away does not make it grow or shrink.
                        const float onPanel=uiWidth*static_cast<float>(reticleSide)
                                            /static_cast<float>(width);
                        const float scaled=onPanel*distance
                                           /g_uiDistance.load(std::memory_order_relaxed)
                                           *g_reticleScale.load(std::memory_order_relaxed);
                        reticleQuad.size.width=scaled;
                        reticleQuad.size.height=scaled;
                        layers[layerCount++]=
                            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&reticleQuad);
                        // The other hand's reticle: the same glyph, hung along its aim.
                        if(g_aim2Known.load(std::memory_order_relaxed)) {
                            float aim2[3]={g_aim2X.load(std::memory_order_relaxed),g_aim2Y.load(std::memory_order_relaxed),g_aim2Z.load(std::memory_order_relaxed)};
                            float display2[3]{};
                            reticleQuad2=reticleQuad;
                            if(ZoomReticleDirection(aim2,reticleHead.orientation,zoom,display2)
                               && AimReticlePose(display2,reticleHead,distance,reticleQuad2.pose)) {
                                layers[layerCount++]=reinterpret_cast<const XrCompositionLayerBaseHeader*>(&reticleQuad2);
                                g_reticle2Layers.fetch_add(1,std::memory_order_relaxed);
                            }
                        }
                        static ULONGLONG lastReticleReport=0;
                        const auto now=GetTickCount64();
                        if(g_log && now-lastReticleReport>=5000) {
                            lastReticleReport=now;
                            char report[256]{};
                            std::snprintf(report,sizeof(report),
                                "RETICLE referenceSpace=1 crop=%d source=%ux%u aim=(%.3f,%.3f,%.3f) distance=%.1fm zoom=%.2f displayAim=(%.3f,%.3f,%.3f) glyph=%s aim2=%d layers2=%llu",
                                reticleSide,width,height,aim[0],aim[1],aim[2],distance,zoom,
                                displayAim[0],displayAim[1],displayAim[2],
                                NativeWorldEnabled() && g_aimFromHand.load()?"vr":"game",
                                g_aim2Known.load()?1:0,g_reticle2Layers.load());
                            g_log(report);
                        }
                    }
                }
            }
        }
    }
    // Also honor F4 when there was no aim layer, e.g. on a menu or transfer failure.
    if(FrameDumpRequested()) WriteFrameDump(g_device,g_context,g_resolved,SceneDepth(),SceneColour(),PreUiColour(),
        g_dumpDirectory,g_log,UiTexture());
    // Keep the desktop's menu/air-raid board native. Projection mode is mirrored
    // after all XR inputs have been copied; never feed desktop composition back
    // into g_resolved, depth estimation, or the headset swapchains.
    if(g_desktopMirror.load() && layerCount && projection.viewCount==2) {
        const auto source=projectionViews[1].subImage.swapchain==g_swapchainRight?MirrorImage::Right:MirrorImage::Left;
        const bool mirrored=MeasureCpu(perf,PerfStage::MirrorCompose,[&]() {
            GpuScope gpu(GpuStage::Mirror);
            return RenderDesktopMirror(g_context,backBuffer,source,projectionViews[1].pose,projectionViews[1].fov,
                g_lastViewPose,uiQuad.subImage.swapchain?&uiQuad:nullptr,reticleQuad.subImage.swapchain?&reticleQuad:nullptr,
                g_desktopMirrorFov.load(),DesktopWindowAspect());
        });
        static ULONGLONG nextMirrorLog=0;
        if(g_log && GetTickCount64()>=nextMirrorLog) {
            nextMirrorLog=GetTickCount64()+5000;
            char report[180]{};
            std::snprintf(report,sizeof(report),"MIRROR right submitted=%d source=%s ui=%d reticle=%d size=%ux%u samples=%u requestedHorizontalFov=%.1f",
                mirrored,source==MirrorImage::Right?"right":"native-fallback",uiQuad.subImage.swapchain?1:0,
                reticleQuad.subImage.swapchain?1:0,width,height,sampleCount,g_desktopMirrorFov.load());g_log(report);
        }
    }
    if(mode==XrDisplayMode::Quad && backBuffer && UiTexture())
        CompositeOverlayImage(g_device,g_context,UiTexture(),backBuffer);
    EndGpuFrame();
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime=frameState.predictedDisplayTime;
    endInfo.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount=layerCount;
    endInfo.layers=layerCount?layers:nullptr;
    g_lastLayerCount.store(static_cast<int>(layerCount),std::memory_order_relaxed);
    g_haveImages.store((g_haveImage[0]?1:0)|(g_haveImage[1]?2:0),std::memory_order_relaxed);
    g_displayFramePrepared=false;
    const auto ended=MeasureCpu(perf,PerfStage::EndFrame,
                                [&]() { return g_api.endFrame(g_session,&endInfo); });
    HmdSample traceSample{};g_openxr.Sample(traceSample);
    TraceFrameEnd(g_lastLayerKind.load()==2?projectionViews:nullptr,g_lastLayerKind.load()==2?2u:0u,traceSample,ended,viewCount==2?views:nullptr);
    g_lastEndFrame.store(static_cast<int>(ended),std::memory_order_relaxed);
    if(XR_FAILED(ended)) g_endFrameFailures.fetch_add(1,std::memory_order_relaxed);
}

void OpenXrRuntime::PrepareSceneFrame() noexcept {
    if(g_mode!=XrMode::HeadsetDisplay || !g_running.load() || g_stop.load()) return;
    if(!TryAcquireSRWLockExclusive(&g_frameLock)) return;
    __try {
        // Learn ownership from a real Present first. A worker/logic call is a no-op.
        if(g_mode==XrMode::HeadsetDisplay && g_running.load() && !g_stop.load()
            && !g_displayStopPending.load() && g_displayOwnerThread==GetCurrentThreadId()
            && !g_displayFramePrepared) {
            PerfBatch perf{};
            bool active=g_sessionRunning.load();
            MeasureCpu(perf,PerfStage::PollEvents,[&]() { PollSessionEvents(active); });
            g_sessionRunning.store(active);
            if(active && !g_stop.load()) BeginDisplayFrame(perf,true);
            SubmitPerf(perf);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        g_stop.store(true);
        g_running.store(false);
        g_displayStopPending.store(true);
    }
    ReleaseSRWLockExclusive(&g_frameLock);
}

void OpenXrRuntime::RenderFrame(void* backBuffer,unsigned width,unsigned height,
                                unsigned sampleCount) noexcept {
    if(g_mode!=XrMode::HeadsetDisplay) return;
    const auto start=PerfNow();
    if(!TryAcquireSRWLockExclusive(&g_frameLock)) return;
    __try {
        // Recheck after acquiring: a callback may have been captured before Stop.
        g_displayOwnerThread=GetCurrentThreadId();
        if(g_displayStopPending.load()) AdvanceDisplayStop();
        else if(g_running.load() && !g_stop.load())
            RunDisplayFrame(static_cast<ID3D11Texture2D*>(backBuffer),width,height,sampleCount);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        g_stop.store(true);
        g_running.store(false);
        g_displayStopPending.store(true);
    }
    ReleaseSRWLockExclusive(&g_frameLock);
    PerfBatch perf{};
    perf.Add(PerfStage::RenderFrame,PerfNow()-start);
    SubmitPerf(perf);
}

bool OpenXrRuntime::Start(VrLogger log,XrMode mode,void* gameDevice,void* gameContext) noexcept {
    if(g_displayStopPending.load()) {
        if(log) log("OpenXR start refused: previous display session is still stopping; wait for completion");
        return false;
    }
    if(g_running.load()) return true;
    ResetRenderedPoseHistory();
    g_log=log;
    g_stop.store(false);
    g_frames.store(0);
    g_layers.store(0);
    g_projectionLayers.store(0);
    g_reshownFrames.store(0);
    g_eyeRepeats.store(0);
    g_warpedEyes.store(0);
    g_uiLayers.store(0);
    g_eyeConsumed.store(true);
    g_adviceReady.store(false);
    g_sessionRunning.store(false);
    AcquireSRWLockExclusive(&g_sampleLock);
    g_sample=HmdSample{};
    ReleaseSRWLockExclusive(&g_sampleLock);
    // Displaying needs the device the game draws with; without it, track only.
    if(mode==XrMode::HeadsetDisplay && (!gameDevice || !gameContext)) {
        mode=XrMode::TrackingOnly;
        SetStatus("no game device yet; starting head tracking without the headset display");
    }
    AcquireSRWLockExclusive(&g_frameLock);
    // Checked again inside the lock. The check at the top of this function is
    // not enough: two threads can both pass it while neither has started yet,
    // and then the second one creates a second instance, is refused with
    // LIMIT_REACHED, and tears down the session the first one had just brought
    // up. That is a crash, and it is what the handover from an input-only
    // session to a displaying one ran into.
    if(g_running.load()) {
        ReleaseSRWLockExclusive(&g_frameLock);
        if(log) log("OpenXR start skipped: another thread got there first");
        return true;
    }
    g_mode=mode;
    g_ownDevice=(mode!=XrMode::HeadsetDisplay);
    g_device=g_ownDevice?nullptr:static_cast<ID3D11Device*>(gameDevice);
    g_context=g_ownDevice?nullptr:static_cast<ID3D11DeviceContext*>(gameContext);
    const bool ok=NegotiateRuntime() && CreateInstance() && CreateSession();
    if(!ok) Teardown();
    ReleaseSRWLockExclusive(&g_frameLock);
    if(!ok) return false;
    g_running.store(true);
    // In HeadsetDisplay the game present owns the frame loop; a thread of our
    // own would be a second xrWaitFrame on the same session, which is illegal.
    if(g_mode==XrMode::TrackingOnly) g_thread=std::thread([this]{ ThreadMain(); });
    else SetStatus("OpenXR running on the game device; the game present drives every frame");
    return true;
}

void OpenXrRuntime::Stop() noexcept {
    FinishMotionTrace();
    ResetRenderedPoseHistory();
    g_stop.store(true);
    g_running.store(false);
    ClearHapticRequests();
    if(g_mode==XrMode::HeadsetDisplay) {
        // Keep the frame callback connected so the next Present can drain GPU
        // work and complete destruction on the immediate-context owner thread.
        g_displayStopPending.store(true);
        return;
    }
    if(g_thread.joinable()) g_thread.join();
    // Wait out any frame the render thread is still inside before tearing down.
    AcquireSRWLockExclusive(&g_frameLock);
    g_mode=XrMode::TrackingOnly;
    ReleaseSRWLockExclusive(&g_frameLock);
    SetStatus("OpenXR stopped");
}

XrMode OpenXrRuntime::Mode() const noexcept { return g_mode.load(); }

void OpenXrRuntime::SetFovMargin(float degrees) noexcept {
    g_fovMargin.store(degrees>0 && degrees<20?degrees/57.29577951f:0.0f,std::memory_order_relaxed);
}
void OpenXrRuntime::SetContentAspect(float widthOverHeight) noexcept {
    g_contentAspect.store(widthOverHeight>0.2f && widthOverHeight<8.0f?widthOverHeight:0.0f,
                          std::memory_order_relaxed);
}
void OpenXrRuntime::SetFovScale(float scale) noexcept {
    if(scale>0.3f && scale<=1.5f) g_fovScale.store(scale,std::memory_order_relaxed);
}

void OpenXrRuntime::SetDisplayMode(XrDisplayMode mode) noexcept {
    g_displayMode.store(mode,std::memory_order_relaxed);
}
XrDisplayMode OpenXrRuntime::DisplayMode() const noexcept {
    return g_displayMode.load(std::memory_order_relaxed);
}
void OpenXrRuntime::SetFrameEye(int eye) noexcept {
    g_frameEye.store(eye,std::memory_order_relaxed);
}
bool OpenXrRuntime::StereoAdvice(float& verticalFov,float& ipd) const noexcept {
    verticalFov=BinocularFov(g_adviceFov.load(std::memory_order_relaxed),g_binocularZoom.load(std::memory_order_relaxed));
    ipd=g_adviceIpd.load(std::memory_order_relaxed);
    return g_adviceReady.load(std::memory_order_relaxed) && verticalFov>0;
}
bool OpenXrRuntime::HeadsetFrustum(float& halfHorizontal,float& halfVertical,
                                   const char*& systemName) const noexcept {
    halfHorizontal=g_headsetHalfHorizontal.load(std::memory_order_relaxed);
    halfVertical=g_headsetHalfVertical.load(std::memory_order_relaxed);
    systemName=g_headsetName;
    return halfHorizontal>0 && halfVertical>0;
}
unsigned long long OpenXrRuntime::ProjectionLayers() const noexcept {
    return g_projectionLayers.load(std::memory_order_relaxed);
}
unsigned long long OpenXrRuntime::ReshownFrames() const noexcept {
    return g_reshownFrames.load(std::memory_order_relaxed);
}

std::uint64_t OpenXrRuntime::EyeRepeats() const noexcept {
    return g_eyeRepeats.load(std::memory_order_relaxed);
}

void OpenXrRuntime::SetWarpSteps(float steps) noexcept {
    if(steps>=4 && steps<=256) g_warpSteps.store(steps,std::memory_order_relaxed);
}

std::atomic<bool> g_handSwap{false};
void OpenXrRuntime::SetHandSwap(bool on) noexcept { g_handSwap.store(on,std::memory_order_release); }
bool OpenXrRuntime::HandSwap() const noexcept { return g_handSwap.load(std::memory_order_acquire); }
int PhysicalHand(int hand) noexcept { return g_handSwap.load(std::memory_order_acquire)?1-hand:hand; }
bool OpenXrRuntime::GripPose(int hand,float position[3],float orientation[4]) const noexcept {
    if(hand<0 || hand>1) return false;
    return GripPosePhysical(PhysicalHand(hand),position,orientation);
}
bool OpenXrRuntime::GripPosePhysical(int hand,float position[3],float orientation[4]) const noexcept {
    if(hand<0 || hand>1) return false;
    AcquireSRWLockShared(&g_handLock);
    const HandTracked pose=g_grip[hand];
    ReleaseSRWLockShared(&g_handLock);
    if(!pose.tracked || !g_running.load() || !g_inputReady.load()) return false;
    for(int i=0;i<3;++i) position[i]=pose.position[i];
    for(int i=0;i<4;++i) orientation[i]=pose.orientation[i];
    return true;
}

bool OpenXrRuntime::HandPose(int hand,float position[3],float orientation[4]) const noexcept {
    if(hand<0 || hand>1) return false;
    return HandPosePhysical(PhysicalHand(hand),position,orientation);
}
bool OpenXrRuntime::HandPosePhysical(int hand,float position[3],float orientation[4]) const noexcept {
    if(hand<0 || hand>1) return false;
    AcquireSRWLockShared(&g_handLock);
    const HandTracked pose=g_hand[hand];
    ReleaseSRWLockShared(&g_handLock);
    if(!pose.tracked || !g_running.load() || !g_inputReady.load()) return false;
    for(int i=0;i<3;++i) position[i]=pose.position[i];
    for(int i=0;i<4;++i) orientation[i]=pose.orientation[i];
    return true;
}

bool OpenXrRuntime::Controls(ControllerState& out) const noexcept {
    const bool ready=ControlsPhysical(out);
    if(ready && g_handSwap.load(std::memory_order_acquire)) SwapControllerHands(out);
    return ready;
}
bool OpenXrRuntime::ControlsPhysical(ControllerState& out) const noexcept {
    AcquireSRWLockShared(&g_handLock);
    const bool ready=g_inputReady.load() && g_running.load();
    out=ready?g_controls:ControllerState{};
    ReleaseSRWLockShared(&g_handLock);
    return ready;
}

void OpenXrRuntime::Buzz(int hand,float seconds,float amplitude) const noexcept {
    if(hand<0 || hand>1 || !(amplitude>0) || !std::isfinite(amplitude) || !(seconds>0) || !std::isfinite(seconds)) return;
    QueueHaptic(PhysicalHand(hand),seconds,amplitude,false);
}

void OpenXrRuntime::StopBuzz(int hand) const noexcept {
    if(hand<0 || hand>1)return;
    QueueHaptic(PhysicalHand(hand),0,0,true);
}

const char* OpenXrRuntime::InputStatus() const noexcept { return g_inputNote; }
const char* OpenXrRuntime::ControllerKind() const noexcept { return g_controllerKind; }

void OpenXrRuntime::SetBoardWorldLocked(bool on) noexcept {
    if(g_boardWorldLocked.exchange(on)!=on) g_boardRecenter.store(true);
}
void OpenXrRuntime::RecenterBoard() noexcept { g_boardRecenter.store(true); }
void OpenXrRuntime::SetBoard(float widthMetres,float distanceMetres) noexcept {
    if(std::isfinite(widthMetres) && widthMetres>=0.3f && widthMetres<=10.0f) g_quadWidth=widthMetres;
    if(std::isfinite(distanceMetres) && distanceMetres>=0.3f && distanceMetres<=20.0f) g_quadDistance=distanceMetres;
}
void OpenXrRuntime::SetUiCluster(bool on,const UiClusterLayout& layout) noexcept {
    AcquireSRWLockExclusive(&g_clusterLock); g_clusterLayout=layout; ReleaseSRWLockExclusive(&g_clusterLock);
    g_clusterOn.store(on && layout.count>0 && layout.count<=8);
}
void OpenXrRuntime::SetUiClusterPlace(int place,float wristWidthMetres,float marginRight,float marginBottom) noexcept {
    g_clusterPlace.store(place>=0 && place<=2?place:0);
    if(wristWidthMetres>=0.05f && wristWidthMetres<=2.0f) g_clusterWristWidth.store(wristWidthMetres);
    if(marginRight>=-0.5f && marginRight<=0.5f) g_clusterMarginRight.store(marginRight);
    if(marginBottom>=-0.5f && marginBottom<=0.5f) g_clusterMarginBottom.store(marginBottom);
}
void OpenXrRuntime::SetUiClusterPose(const float position[3],const float orientation[4],bool valid) noexcept {
    if(valid && position && orientation) {
        AcquireSRWLockExclusive(&g_clusterLock);
        g_clusterPose.position={position[0],position[1],position[2]};
        g_clusterPose.orientation={orientation[0],orientation[1],orientation[2],orientation[3]};
        ReleaseSRWLockExclusive(&g_clusterLock);
    }
    g_clusterPoseValid.store(valid);
}
void UiClusterCounts(unsigned long long& composites,unsigned long long& failures) noexcept {
    composites=g_clusterComposites.load(); failures=g_clusterFailures.load();
}
void OpenXrRuntime::SetWarpNearest(float metres) noexcept {
    if(metres>=0.05f && metres<=5.0f) g_warpNearest.store(metres,std::memory_order_relaxed);
}

bool OpenXrRuntime::UiCaptureAvailable() const noexcept {
    if(!g_running.load() || !g_sessionRunning.load() || g_stop.load()
        || g_mode.load()!=XrMode::HeadsetDisplay || !g_uiLayer.load()
        || g_displayMode.load()==XrDisplayMode::Quad) return false;
    const int force=g_displayForce.load();
    return force!=1 && (force==2 || SceneAgeMs()<750);
}

void OpenXrRuntime::SetWarpEase(float kneeMetres,float scale) noexcept {
    if(kneeMetres>=0.1f && kneeMetres<=20.0f) g_nearKnee.store(kneeMetres,std::memory_order_relaxed);
    if(scale>=0.0f && scale<=1.0f) g_nearScale.store(scale,std::memory_order_relaxed);
}

void OpenXrRuntime::ReadSubmission(int& sessionState,int& lastEndFrame,
                                   unsigned long long& endFrameFailures,
                                   unsigned long long& notRendering,
                                   int& lastLayerCount,int& lastLayerKind,
                                   int& fovDeg,int& haveImages) const noexcept {
    sessionState=g_sessionState.load(std::memory_order_relaxed);
    lastEndFrame=g_lastEndFrame.load(std::memory_order_relaxed);
    endFrameFailures=g_endFrameFailures.load(std::memory_order_relaxed);
    notRendering=g_notRendering.load(std::memory_order_relaxed);
    lastLayerCount=g_lastLayerCount.load(std::memory_order_relaxed);
    lastLayerKind=g_lastLayerKind.load(std::memory_order_relaxed);
    fovDeg=g_lastFovDeg.load(std::memory_order_relaxed);
    haveImages=g_haveImages.load(std::memory_order_relaxed);
}

void OpenXrRuntime::SetAimDirection(const float reference[3],bool handAim) noexcept {
    g_aimFromHand.store(reference && handAim,std::memory_order_relaxed);
    if(!reference) { g_aimKnown.store(false,std::memory_order_relaxed); return; }
    const float length=std::sqrt(reference[0]*reference[0]+reference[1]*reference[1]
                                +reference[2]*reference[2]);
    if(!(length>0.001f) || !std::isfinite(length)) {
        g_aimKnown.store(false,std::memory_order_relaxed);
        return;
    }
    g_aimX.store(reference[0]/length,std::memory_order_relaxed);
    g_aimY.store(reference[1]/length,std::memory_order_relaxed);
    g_aimZ.store(reference[2]/length,std::memory_order_relaxed);
    g_aimKnown.store(true,std::memory_order_relaxed);
}

void OpenXrRuntime::SetAimDirection2(const float reference[3]) noexcept {
    if(!reference) { g_aim2Known.store(false,std::memory_order_relaxed); return; }
    const float length=std::sqrt(reference[0]*reference[0]+reference[1]*reference[1]+reference[2]*reference[2]);
    if(!(length>0.001f) || !std::isfinite(length)) { g_aim2Known.store(false,std::memory_order_relaxed); return; }
    g_aim2X.store(reference[0]/length,std::memory_order_relaxed);
    g_aim2Y.store(reference[1]/length,std::memory_order_relaxed);
    g_aim2Z.store(reference[2]/length,std::memory_order_relaxed);
    g_aim2Known.store(true,std::memory_order_relaxed);
}
void OpenXrRuntime::SetBinocularZoom(float magnification) noexcept {
    g_binocularZoom.store(std::isfinite(magnification) && magnification>=1 && magnification<=40
                         ?magnification:1,std::memory_order_relaxed);
}

void OpenXrRuntime::SetReticle(bool follows,int pixels,float distanceMetres,float scale) noexcept {
    g_reticleFollows.store(follows,std::memory_order_relaxed);
    if(pixels>=16 && pixels<=512) g_reticlePixels.store(pixels,std::memory_order_relaxed);
    if(distanceMetres>=1.0f && distanceMetres<=200.0f)
        g_reticleDistance.store(distanceMetres,std::memory_order_relaxed);
    if(scale>=0.05f && scale<=4.0f) g_reticleScale.store(scale,std::memory_order_relaxed);
}

void OpenXrRuntime::SetDesktopMirror(bool on) noexcept { g_desktopMirror.store(on); }
void OpenXrRuntime::SetEyeFovCrop(bool always) noexcept { g_eyeFovCropAlways.store(always); }
void OpenXrRuntime::SetDesktopMirrorFov(float degrees) noexcept {
    g_desktopMirrorFov.store(!std::isfinite(degrees)?90.f:(degrees<=0?0.f:std::fmin(140.f,std::fmax(45.f,degrees))));
}

void OpenXrRuntime::SetUiLayer(bool on,float widthMetres,float distanceMetres) noexcept {
    g_uiLayer.store(on,std::memory_order_relaxed);
    if(widthMetres>0.05f && widthMetres<10.0f) g_uiWidth.store(widthMetres,std::memory_order_relaxed);
    if(distanceMetres>0.2f && distanceMetres<20.0f)
        g_uiDistance.store(distanceMetres,std::memory_order_relaxed);
}

unsigned long long OpenXrRuntime::UiLayers() const noexcept {
    return g_uiLayers.load(std::memory_order_relaxed);
}

void OpenXrRuntime::ReadAlpha(int& low,int& high) const noexcept {
    low=g_alphaLow.load(std::memory_order_relaxed);
    high=g_alphaHigh.load(std::memory_order_relaxed);
}

void OpenXrRuntime::ForceDisplay(int mode) noexcept {
    g_displayForce.store(mode,std::memory_order_relaxed);
}

void OpenXrRuntime::MarkSceneLive() noexcept {
    // What tells a mission from a menu is the player's own body being drawn.
    // Three other candidates failed: the camera object keeps ticking on the
    // title screen, a first person camera gets written there too, and even the
    // soldier's input is read. Logging all four at once settled it in one run,
    // where the body was the only one that was absent at the title, present in
    // a mission, and stale in the pause menu.
    //
    // The body is only drawn when it is in view, though, so the first person
    // camera also calls this while the soldier is reading its input -- see
    // CameraKeepsSceneLive -- which is what keeps a player who is knocked down
    // and looking at the sky out of the board.
    g_frameEyeStamp.store(GetTickCount64(),std::memory_order_relaxed);
}

unsigned long long OpenXrRuntime::SceneAgeMs() const noexcept {
    const auto stamp=g_frameEyeStamp.load(std::memory_order_relaxed);
    return stamp?GetTickCount64()-stamp:~0ull;
}

void OpenXrRuntime::MarkCameraPose() noexcept {
    AcquireSRWLockExclusive(&g_poseLock);
    if(g_eyePoseValid) {
        g_cameraPose[0]=g_eyePose[0];
        g_cameraPose[1]=g_eyePose[1];
        g_cameraPoseValid=true;
    }
    ReleaseSRWLockExclusive(&g_poseLock);
}

void OpenXrRuntime::SetDumpDirectory(const wchar_t* directory) noexcept {
    if(directory) wcscpy_s(g_dumpDirectory,directory);
}

void OpenXrRuntime::SetWarpEyeScale(float scale) noexcept {
    if(scale>0.01f && scale<=3.0f) g_warpEyeScale.store(scale,std::memory_order_relaxed);
}

void OpenXrRuntime::SetWarpDebug(float mode) noexcept {
    g_warpDebug.store(mode,std::memory_order_relaxed);
}

bool OpenXrRuntime::WarpDebugging() const noexcept {
    return g_warpDebug.load(std::memory_order_relaxed)>0.5f;
}

void OpenXrRuntime::SetWarpHudless(bool on) noexcept {
    g_warpHudless.store(on,std::memory_order_relaxed);
}

void OpenXrRuntime::SetEyeWarp(bool on,float nearPlane,float farPlane) noexcept {
    if(nearPlane>0 && farPlane>nearPlane) {
        g_warpNear.store(nearPlane,std::memory_order_relaxed);
        g_warpFar.store(farPlane,std::memory_order_relaxed);
    }
    g_warpEnabled.store(on,std::memory_order_relaxed);
    if(!on) ReleaseWarp();
}

std::uint64_t OpenXrRuntime::WarpedEyes() const noexcept {
    return g_warpedEyes.load(std::memory_order_relaxed);
}

const char* OpenXrRuntime::WarpStatusText() const noexcept { return WarpStatus(); }

const char* OpenXrRuntime::MaskStatusText() const noexcept { return MaskStatus(); }

bool OpenXrRuntime::TakeEyeConsumed() noexcept {
    return g_eyeConsumed.exchange(false,std::memory_order_acq_rel);
}
unsigned long long OpenXrRuntime::SubmittedLayers() const noexcept { return g_layers.load(); }

bool OpenXrRuntime::Running() const noexcept { return g_running.load(); }
unsigned long long OpenXrRuntime::Frames() const noexcept { return g_frames.load(); }
bool OpenXrRuntime::StageSpace() const noexcept { return g_stage.load(); }
const char* OpenXrRuntime::Status() const noexcept { return g_status; }

bool OpenXrRuntime::Sample(HmdSample& out) const noexcept {
    AcquireSRWLockShared(&g_sampleLock);
    out=g_sample;
    ReleaseSRWLockShared(&g_sampleLock);
    return out.frame!=0 && out.orientationValid;
}
}
