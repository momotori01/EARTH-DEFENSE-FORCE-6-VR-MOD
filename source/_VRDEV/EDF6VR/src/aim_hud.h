#pragma once
#include "image_profile.h"
#include "camera_math.h"
#include <d3d11.h>
#include <cstdint>
namespace edf6vr {
struct AimHudPoint { float world[3]{}; unsigned kind=0; }; // native cursor / after / acquisition candidate
// The lock-on sight: the game's own frame round its lock-on cone, four world
// corners a kilometre out along the aim (FA060 draws it from the same record:
// the sight's matrix, and the cone's horizontal and vertical half angles).
struct AimHudFrame { float corners[4][3]{}; float half[2]{}; };
struct AimHudSnapshot {
    static constexpr unsigned capacity=128;
    AimHudPoint points[capacity]{};
    unsigned count=0;
    AimHudFrame frames[2]{};
    unsigned frameCount=0;
    bool lockWeapon=false;
    ULONGLONG at=0;
};
// Reads the prepared native draw records, never target-selection/gameplay data.
bool ReadNativeAimHud(const void* camera,AimHudSnapshot&) noexcept;
bool InstallAimHud(const ImageProfile&,bool& changed) noexcept;
// The markers and the lock-on sight's frame, on foot and in vehicle seats: the
// HUD's own copy of the frame is in the middle of the panel, which the compact
// HUD does not show and a vehicle seat cuts away, and it would not stand round
// the cone in the world anyway.
void SetAimHudSource(void* camera,bool active,float scale) noexcept;
// The lock-on sight's frame: its lines' thickness as a share of the width it was
// first drawn with ([Render] LockSightWidth, 0.1 to 2).
void SetAimHudFrameWidth(float share) noexcept;
float AimHudFrameWidth() noexcept;
AimHudSnapshot ReadAimHud() noexcept;
float AimHudScale() noexcept;
// View is Umbra's inverse camera BEFORE EDF's Ry(pi) conversion.
bool ProjectAimHud(const Matrix& view,const Matrix& projection,const float world[3],float ndc[2]) noexcept;
// The same, anywhere in front of the eye (off the screen too): for lines.
bool ProjectAimHudAhead(const Matrix& view,const Matrix& projection,const float world[3],float ndc[2]) noexcept;
bool DrawAimHud(ID3D11DeviceContext*,ID3D11Texture2D*,const Matrix&,const Matrix&,
                const AimHudSnapshot&,float scale) noexcept;
bool InsideAimHudDraw() noexcept;
const char* AimHudStatus() noexcept;
}
