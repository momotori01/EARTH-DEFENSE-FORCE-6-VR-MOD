#pragma once
#include "camera_math.h"
#include <array>
#include <cstdint>
#include <vector>

namespace edf6vr {
enum class CockpitKind : unsigned { Nix, Crawler, Barga, ProteusGunner, ProteusDriver, ProteusMissile, Tank, TitanGunner,
    CombatNegling, CombatGrape, CombatCaliban, HeliNereid, Heli602, Heli506, HeliBrute, HeliBruteGunner,
    // The Combat Wagon: the Nix cabin with its lower half kept in the chest,
    // which sits on the truck's mount (the Nix's hangs into its hidden waist).
    NixChest,
    // The armed pickups (V610, V611): a car's cab built into their empty one
    // on the helicopter kit (cockpit_truck_pickup.inc).
    TruckPickup };
inline constexpr unsigned kCockpitKinds=18;
// Metres, +X pilot's right, +Y up, +Z forwards. Fixed cabin modelling origin;
// the seated eye has its own offset so moving the camera never moves the hull.
// UV uses image coordinates (top-left origin); surfaces 16..19 select live HUD.
struct CockpitVertex { float position[3],normal[3],colour[3]; float uv[2]{}; float surface=0; float visibility[2]{1,1}; };
// Offline contact occlusion, both sides of each face. Reject a stale bake.
bool ApplyCockpitOcclusion(std::vector<CockpitVertex>&,CockpitKind kind=CockpitKind::Nix) noexcept;
std::vector<CockpitVertex> BuildNixCockpit(bool chest=false);
std::vector<CockpitVertex> BuildCrawlerCockpit();
std::vector<CockpitVertex> BuildBargaCockpit();
std::vector<CockpitVertex> BuildProteusGunnerCockpit();
// The tandem cabin in the upper body. One geometry, two stations: the rear
// build is the same cabin with the missile operator's eye at the origin, so
// either seat is placed and clamped exactly like any other cabin.
std::vector<CockpitVertex> BuildProteusCabinCockpit(bool rear);
inline constexpr std::array<float,3> kProteusCabinRearSeat{0,.56f,-1.18f};
// Tank driver: one cabin for every tank, hull-fixed, the turret above the
// shoulders all screen. Titan gunner: the Proteus pod, narrowed, under its own
// sub-cannon.
std::vector<CockpitVertex> BuildTankCockpit();
std::vector<CockpitVertex> BuildTitanGunnerCockpit();
std::vector<CockpitVertex> BuildCockpit(CockpitKind kind);
std::vector<CockpitVertex> BuildBargaJointCap(unsigned variant,unsigned part);
// Every mesh drawn in a bone's own frame with the native depth: Barga's ten
// joint caps (variant*5+part), then the tank barrel dishes and turret decks.
// Authored in that bone's local game coordinates (+X model left).
enum CockpitCapMesh : unsigned { kCapBlacker=10,kCapVarias,kCapRailgun,kCapEmc,kCapTitanMain,kCapTitanSub,
    kCapRailgunDeck,kCapTitanDeck,kCapRailgunMg,kCapKebler,kCockpitCapMeshes };
std::vector<CockpitVertex> BuildCockpitCap(unsigned index);
inline constexpr const wchar_t* kBargaCapBones[]={L"shoulderCenter_l",L"shoulderCenter_r",L"koshi",L"booster_l",L"booster_r"};
inline constexpr std::array<float,3> kBargaSphereCentre{0,-.25f,0};
inline constexpr float kBargaSphereRadius=1.15f;
std::vector<CockpitVertex> BuildCockpitDisplays(CockpitKind kind=CockpitKind::Nix);
inline constexpr std::array<float,3> kCockpitSeatedEye{0,.10f,-.10f};
Matrix CockpitSeatedCamera(const Matrix& cabin) noexcept;
// Translation-only swept head clearance, including space for both eyes. The
// Rendered solids and transparent panel faces supply collision surfaces;
// rotations stay 1:1. Input/output are absolute cabin-local positions, with a
// sweep from kCockpitSeatedEye. All axes reach the interior surfaces.
std::array<float,3> ClampCockpitHead(std::array<float,3> desired,float radius=.10f,CockpitKind kind=CockpitKind::Nix) noexcept;
bool ConstrainCockpitCamera(const Matrix& cabin,Matrix& camera,float radius=.10f,CockpitKind kind=CockpitKind::Nix,bool mirrored=false) noexcept;
// Build immutable collision trees before publishing the first VR sample.
void PrepareCockpitCollision() noexcept;
struct CockpitRig {
    CockpitKind kind=CockpitKind::Nix;
    void* model=nullptr;
    void* resource=nullptr;
    void* nodes=nullptr;
    unsigned nodeCount=0;
    unsigned bodyBone=256; // Crawler undercarriage shares the hidden hull's bone.
    std::array<unsigned char,256> limb{};
    unsigned capVariant=0,capCount=0;
    std::array<unsigned,5> capBones{};
    std::array<unsigned char,5> capMesh{}; // non-Barga caps: CockpitCapMesh per part
    // Barrel cuts: a vertex mostly skinned to cutBones[i] is dropped when its
    // bind-space Z is below cutZ[i]. POSITION is three halves at this byte
    // offset in a 60 or 68 byte vertex.
    unsigned cutCount=0;
    std::array<unsigned char,6> cutBones{};
    std::array<float,6> cutZ{};
    unsigned char cutPosition60=0,cutPosition68=0;
    // Snapshot on the simulation thread; rendering never dereferences nodes.
    std::array<Matrix,5> capFrames{};
    // Combat vehicles: the model's paint, an index into kCombatPaints
    // (cockpit_combat_shells.h); 0xFF when there is none.
    unsigned char paint=0xFF;
    // The Proteus's tandem cabin: who sits in the other seat (crew_figures.h):
    // class 1-4 (0 nobody), look 0-3, colour preset (0xFF: the model's own),
    // and that soldier's own two colours when read (CrewColoursOf), which win.
    unsigned char crewKind=0,crewModel=0,crewPreset=0xFF;
    bool crewColours=false;float crewColour[2][4]{};
};
struct CockpitPose {
    Matrix camera{},cabin{};
    CockpitRig rig{};
    std::uint64_t at=0;
};
class CockpitHistory {
public:
    void Record(const CockpitPose&) noexcept;
    bool Match(const Matrix& nativeView,std::uint64_t now,CockpitPose&) const noexcept;
    void Reset() noexcept;
private:
    std::array<CockpitPose,128> entries{};
    unsigned next=0,count=0;
};
// Dedicated lock: no engine callbacks or game-pointer dereferences under it.
void ConfigureCockpit(bool enabled) noexcept;
bool CockpitEnabled() noexcept;
void PublishCockpit(const CockpitPose&) noexcept;
void ClearCockpitPose() noexcept;
bool MatchCockpit(const Matrix& nativeView,CockpitPose&) noexcept;
// The camera's native +X is screen left; +Z is forward. Apply this conversion
// ONCE when placing the mesh, then the engine's normal view/Ry(pi)/projection.
// mirrored: the cabin is drawn the other way round in x (CockpitMirrored).
Matrix CockpitLocalToWorld(const Matrix& cabin,bool mirrored=false) noexcept;
// The Brute's right door gunner (seat 2) sits in the mirror image of the left
// one's booth (the machine is symmetric, the booth is not).
inline bool CockpitMirrored(const CockpitRig& rig) noexcept {return rig.kind==CockpitKind::HeliBruteGunner&&rig.capVariant==1;}
struct CockpitSelection { unsigned kept=0,removed=0; bool valid=false; };
CockpitSelection SelectCockpitLimbs(const unsigned char* vertices,std::size_t vertexBytes,
    unsigned stride,const void* indices,unsigned count,bool wide,int baseVertex,
    const CockpitRig&,std::vector<std::uint32_t>& output) noexcept;
// Combat vehicles: the hidden hull's own triangles, read from the game's mesh
// in memory (POSITION three halves at byte 0 of a 60 or 68 byte vertex, model
// space), nine floats each, appended to hull. Nothing is kept on disk.
void CollectCockpitHull(const unsigned char* vertices,std::size_t vertexBytes,
    unsigned stride,const void* indices,unsigned count,bool wide,int baseVertex,
    const CockpitRig&,std::vector<float>& hull) noexcept;
// The lid (cockpit_combat_shells.h): of that hull's triangles meeting the cut,
// the parts outside it, and the cut's deck and rear cover, in the hull bone's
// own frame, facing the seat; with its fittings (the deck's, the covers'), unless
// asked without them.
std::vector<CockpitVertex> BuildCombatLid(CockpitKind kind,const std::vector<float>& hull,bool fittings=true);
// The fittings alone, in model coordinates.
std::vector<CockpitVertex> BuildCombatDecor(CockpitKind kind,const std::vector<float>& hull);
// Helicopters (cockpit_heli_shells.h): from the same hull, the canopy's lining
// - the hull the pilot would see from behind, turned to the seat - in the hull
// bone's frame. BuildCombatLid gives it for a helicopter's kind.
std::vector<CockpitVertex> BuildHeliLining(CockpitKind kind,const std::vector<float>& hull);
}
