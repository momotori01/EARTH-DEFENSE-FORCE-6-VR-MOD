#pragma once
#include "cockpit.h"
#include <d3d11.h>
namespace edf6vr {
bool PrepareCockpitDraw(ID3D11Device*) noexcept;
// subtitleMode: the subtitle screen (source 3) shows `subtitles` (the radio
// subtitles' own capture, ui_capture.h; nothing when null) instead of a crop of
// the HUD.
bool DrawCockpit(ID3D11DeviceContext*,ID3D11Texture2D*,const Matrix& view,const Matrix& projection,const CockpitPose&,ID3D11Texture2D* hud=nullptr,std::uint64_t frame=0,
                 ID3D11Texture2D* subtitles=nullptr,bool subtitleMode=false) noexcept;
// Joint closures are world geometry: use the current eye's native depth before
// that depth is reused for the other eye. No depth copy or CPU readback.
bool DrawCockpitJointCaps(ID3D11DeviceContext*,ID3D11Texture2D*,const Matrix& view,const Matrix& projection,const CockpitPose&,ID3D11DepthStencilView*,bool reverseDepth=false) noexcept;
bool DrawNativeBargaCaps(ID3D11DeviceContext*,ID3D11Texture2D*,const Matrix& view,const Matrix& projection,const CockpitPose&,std::uint64_t frame,unsigned eye) noexcept;
// A stale/missing eye pair drops borrowed frame resources, not expensive
// device-owned shaders, atlases or in-flight limb readbacks. Render thread only.
void DiscardCockpitFrame() noexcept;
// Full release: device replacement or render-thread XR shutdown after GPU drain.
void ReleaseCockpitDraw() noexcept;
// Where tools/edf6/crew_figures.py wrote the crew figures (Mods/Plugins/EDF6VRCrew).
void ConfigureCrewFigures(const wchar_t* folder) noexcept;
struct CrewDrawStats { unsigned builds=0,failures=0; unsigned long long draws=0; char note[128]{}; };
CrewDrawStats ReadCrewDrawStats() noexcept;   // render-thread counters, read for the log
// Only inside the selected local vehicle's native AnimationModel draw. The
// game's shader, material, animation and render camera remain in use.
struct CockpitLimbScope {
    explicit CockpitLimbScope(const CockpitRig&) noexcept;
    ~CockpitLimbScope() noexcept;
    CockpitLimbScope(const CockpitLimbScope&)=delete;
    CockpitLimbScope& operator=(const CockpitLimbScope&)=delete;
private:
    const CockpitRig* previous;
};
bool CockpitLimbIntercept(ID3D11DeviceContext*,UINT count,UINT start,INT base) noexcept;
// A mission truck ridden from inside (MissionTruckRider): its windows' glass,
// the model's see-through mesh (no depth writes; 204 or 408 indices), is left
// out of the draw: its noise normal map bends the world behind it differently
// in each eye. Only inside that truck's own model draw.
struct TruckGlassScope {
    TruckGlassScope() noexcept;
    ~TruckGlassScope() noexcept;
    TruckGlassScope(const TruckGlassScope&)=delete;
    TruckGlassScope& operator=(const TruckGlassScope&)=delete;
private:
    bool previous;
};
bool TruckGlassIntercept(ID3D11DeviceContext*,UINT count) noexcept;
// Research (2026-10-01, the other crew member of the Proteus's tandem cabin
// drawn from the game's own model): inside a rider's body-model draw (slot 0-3,
// the vehicle's seats), the DrawIndexed calls the game issues are counted, never
// changed -- how many, their index counts, whether they write depth -- which a
// replay of the rider inside the cockpit would have to repeat.
struct CrewDrawScope {
    explicit CrewDrawScope(unsigned slot) noexcept;
    ~CrewDrawScope() noexcept;
    CrewDrawScope(const CrewDrawScope&)=delete;
    CrewDrawScope& operator=(const CrewDrawScope&)=delete;
private:
    int previous;
};
void CrewDrawNote(ID3D11DeviceContext*,UINT count) noexcept;
struct CrewDrawCounts { std::uint64_t draws=0,indices=0,depthWrites=0; unsigned sizes[8]{}; };
CrewDrawCounts ReadCrewDrawCounts(unsigned slot) noexcept;
void ResetCrewDrawCounts(unsigned slot) noexcept;
// Since load: draws inside the scope, and those left out.
void TruckGlassCounts(std::uint64_t& seen,std::uint64_t& skipped) noexcept;
struct CockpitDrawStats {
    std::uint64_t interiors=0,filtered=0,pending=0,unsupported=0;
    std::uint64_t initializations=0,prepareMs=0;
    unsigned meshes=0,keptTriangles=0,removedTriangles=0;
    std::uint64_t instrumentFrames=0;
    std::uint64_t ownDepthCaps=0;   // cap passes drawn with their own depth (tanks)
    unsigned lidTriangles=0;        // combat vehicles' lid, built from the hull
    std::uint64_t lidDraws=0;
    bool textured=false;
    bool occlusion=false,nativeLights=false,environment=false;
};
CockpitDrawStats ReadCockpitDrawStats() noexcept;
}
