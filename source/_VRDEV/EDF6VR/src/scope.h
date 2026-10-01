#pragma once
#include "camera_math.h"
#include <cstdint>

namespace edf6vr {
// Weapon scopes: a live, magnified picture in the eyepiece while the game's zoom
// is on, the headset's own view left at its normal size.
//
// Built after FO4VR True Scopes (rollingrock, GPL-3.0; the design, no code) and
// GoldenEye VR (MrSco, MIT): a third, mono view of the world from a camera on the
// firing line, drawn by the engine's own renderer (here: the native world loop
// run once more, native_world_queue.cpp), put on a disc at the eyepiece in both
// eyes. Its field: tan(half) = (lens radius / eye distance) / magnification, so
// the lens magnifies by exactly the game's zoom. DeusExHRVR's lesson: one optical
// origin on the firing line, or the magnified reticle drifts off the shot.
//
// ScopeMode 2 is the "digital zoom" variant for comparison: no extra view, the
// disc shows a magnified crop of each eye's own picture.

// An eyepiece or a screen on a weapon model, measured from the shipped meshes:
// the scope's own part (a connected piece of the mesh), its rear face
// (scratchpad parts.py / rear_faces.py, 2026-10-01). `node` is a node name the
// model has, to recognise it (its root's, except s_sniper_rsnr whose root is
// "mdl"). The face is given in the frame of node `frame` (nullptr: the model
// root; "screen": an Air Raider monitor's hinge, posed by the zoom animation),
// with its normal (toward the shooter) and its up in that frame.
struct ScopeLensSpec {
    const wchar_t* node;
    const wchar_t* frame;
    int shape;               // 0 round, 1 rectangular (box sights, monitors)
    float centre[3];
    float halfWidth,halfHeight;
    float normal[3];
    float up[3];
    bool holo;               // a holographic monitor over the sights, not the scope's own glass
};
int ScopeLensCount() noexcept;
const ScopeLensSpec* ScopeLensAt(int index) noexcept;
// A zoom weapon with no scope of its own (Ranger, Wing Diver, Air Raider): a
// round holographic monitor popped up over the sights, in the weapon root's frame.
const ScopeLensSpec& ScopeHoloSpec() noexcept;
// What shows the picture (ScopeView::kind), and how it is framed (style).
enum ScopeKind : int { ScopeInLens=0, ScopeHoloOnWeapon=1, ScopeHoloPanel=2 };
enum ScopeStyle : int { ScopeStyleLens=0, ScopeStyleHoloBlue=1, ScopeStyleHoloGreen=2 };
// The Fencer's panel: a rectangular monitor this far in front of the eyes, this
// wide (32 x 18 cm; 24 x 13.5 was asked to be larger, hardware 2026-10-01).
constexpr float kScopePanelDistance=.5f,kScopePanelHalfWidth=.16f,kScopePanelHalfHeight=.09f;

// The scope's camera, published by the update thread for the producer's third
// loop and for the digital variant. Game world coordinates.
struct ScopeView {
    bool active=false;
    int mode=0;              // 1 rendered view, 2 digital zoom
    float aspect=1;          // the lens's width / height, for the frustum's width
    int kind=ScopeInLens;
    int style=ScopeStyleLens;
    float origin[3]{};       // on the firing line (the shot's start)
    float forward[3]{};      // along the aim, normalised
    float tanHalf=0;         // the lens's half field, vertical and horizontal
    float zoom=1;
    int lens=-1;             // ScopeLensAt index
    std::uint64_t at=0;      // GetTickCount64
};
void PublishScopeView(const ScopeView&) noexcept;
ScopeView ReadScopeView() noexcept;

// Where the eyepiece is this frame, published by the weapon's draw (the pose
// the weapon layer was drawn with). `normal` faces back toward the shooter.
struct ScopeLensFrame {
    bool valid=false;
    float centre[3]{},normal[3]{},up[3]{};
    float radius=0;          // the half height (the round lens's radius)
    float halfWidth=0;
    int shape=0;
    float lensTan=0;         // radius / distance from the eye the weapon was placed for
    // The weapon layer's camera (weapon_stereo.cpp, anchored): the native one
    // moved to `eye`, each eye `eyeHalf` along screen right.
    bool anchored=false;
    float eye[3]{};
    float eyeHalf=0;
    std::uint64_t at=0;
};
void PublishScopeLens(const ScopeLensFrame&) noexcept;
ScopeLensFrame ReadScopeLens() noexcept;

// (lens radius / eye distance) / magnification, clamped to what a frustum takes.
float ScopeTanHalf(float lensTan,float zoom) noexcept;
// The scope camera: `head` turned by the shortest arc that takes its forward
// (row 2) onto `forward`, moved to `origin`. Rows as the engine's camera-to-world
// (row0 = row1 x row2; screen right is -row0).
bool ScopeCamera(const Matrix& head,const float origin[3],const float forward[3],Matrix& out) noexcept;
// Umbra's frustum (left, right, top, bottom, near, far: near-plane units) made
// symmetric with the vertical half field `tanHalf` and the horizontal
// `tanHalf * max(aspect, 1)`; near, far and the tag kept.
bool ScopeFrustum(const float in[6],float tanHalf,float aspect,float out[6]) noexcept;
// The eyepiece placed by its frame node's world rows (node -> world), seen from `eye`.
bool ScopeLensFrameFrom(const Matrix& frame,const ScopeLensSpec& spec,const float eye[3],float scale,ScopeLensFrame& out) noexcept;
// A world point through an eye camera: Umbra cell-to-camera `view`, EDF's
// Ry(pi), then `projection` (row vectors), as aim_hud.cpp does.
bool ScopeClip(const Matrix& view,const Matrix& projection,const float world[3],float clip[4]) noexcept;
// The same as one matrix: world (x, y, z, 1) row vector times it gives clip.
Matrix ScopeWorldToClip(const Matrix& view,const Matrix& projection) noexcept;
// A rigid view's camera-to-world rows (the inverse).
Matrix ScopeCameraOf(const Matrix& view) noexcept;
}
