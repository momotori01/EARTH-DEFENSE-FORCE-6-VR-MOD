#pragma once
#include "first_person.h"
#include "vr_math.h"
#include "cockpit.h"
namespace edf6vr {
struct VehicleSeat {
    void* vehicle=nullptr;
    void* seat=nullptr;
    void* cameraOwner=nullptr;
    std::uint32_t vehicleId=0;
    bool riderHead=false;
};
bool CheckVehicleProfile(const ImageProfile&) noexcept;
// Unknown/unreadable mounted state also excludes all on-foot writes.
bool HasMountReference(const void* soldier) noexcept;
bool ReadVehicleSeat(const ImageProfile&,void* soldier,VehicleSeat&) noexcept;
// The mission trucks whose own cab is modelled inside (the kei truck V512 and
// its BGP paint, the trailer truck's cab V513), identified by their model's
// node names. The game does not seat the rider in the cab (the soldier's head
// sits at one height in all of them, 1.5 m, under the big truck's dashboard),
// so the driver (*driver) looks from the cab's own eye (PlaceVehicleAnchor's
// profiles) and the bed's four from their own heads; the body is left out,
// hands too, as in the other vehicles.
bool MissionTruckRider(const ImageProfile&,const VehicleSeat&,NodeLookup,bool* driver=nullptr) noexcept;
// A world point in the vehicle's own frame (its root: +X its left, +Y up, +Z
// forward), i.e. model coordinates, for the log.
bool VehicleLocalPoint(const VehicleSeat&,const float world[3],float local[3]) noexcept;
// Research (2026-10-01, a seated figure for the other crew member of the
// Proteus's tandem cabin): who sits in each seat of this vehicle, and where the
// game keeps them. Read only. A soldier is a seat's rider when its seat
// (+0x1540) is that RideInfo and its vehicle (+0x1548) this vehicle. Soldier
// pointers are looked for in every RideInfo (directly, or one pointer away at
// +0..+0x18) and in the vehicle's first 0xF00 bytes; `self` is checked too.
// That deep search costs 4-5 ms (measured online 2026-10-01); it found every
// rider, local and remote, held directly at RideInfo+0x260 (and +0x300), so a
// shallow scan reads only that.
inline constexpr unsigned kRideInfoRider=0x260;
struct CrewRef { unsigned seat=0,offset=0,inner=0; void* soldier=nullptr; };   // seat ~0u: the vehicle; inner ~0u: held directly
struct CrewSoldier {
    void* soldier=nullptr; unsigned kind=0,seat=~0u; void* rideInfo=nullptr; void* vehicle=nullptr;
    unsigned mount=0; std::uint32_t id=0; float world[3]{},local[3]{}; bool placed=false,self=false;
};
struct CrewScan {
    unsigned seatCount=0; void* rideInfo[8]{}; void* cameraOwner[8]{}; void* rider[8]{}; unsigned riderKind[8]{};
    unsigned refCount=0; CrewRef refs[24]{};
    unsigned soldierCount=0; CrewSoldier soldiers[8]{};
};
bool ScanVehicleCrew(const ImageProfile&,const VehicleSeat&,const void* self,CrewScan&,bool deep=true) noexcept;
// A world direction in the vehicle's own unit axes (+X its left, +Y up, +Z its nose).
bool VehicleLocalDirection(const VehicleSeat&,const float world[3],float local[3]) noexcept;
// A named bone's posed world matrix.
bool VehicleBoneRows(const VehicleSeat&,NodeLookup,const wchar_t* bone,Matrix& rows) noexcept;
// Moves only the camera origin to a model-specific cabin/hatch position.
// Seat zero plus explicitly identified Proteus/Brute gun seats are eligible.
// Other passengers/unknown models keep native placement.
bool PlaceVehicleAnchor(const ImageProfile&,const VehicleSeat&,NodeLookup,
    Matrix& camera,const char*& label,int& seatIndex) noexcept;
// Use the physical chassis basis every frame, never the native chase camera's
// inherited on-foot/HMD heading. Keeps the chosen seat origin; turret aim is
// deliberately independent. Also applies to bikes and unmapped/passenger seats.
bool FaceVehicleForward(const VehicleSeat&,Matrix& camera) noexcept;
// Nix and Depth Crawler, primary seat only. Eye is INSIDE the chest, attached to body,
// independently of the HMD. Rig is cached by the caller; all engine reads guarded.
bool PlaceVehicleCockpit(const ImageProfile&,const VehicleSeat&,NodeLookup,Matrix& cabin,CockpitRig&) noexcept;
// A solo test of the Proteus crew figure ([Diagnostics] CrewFigureTest): while
// the other tandem seat is empty, a figure of class `kind` (1 Ranger, 2 Wing
// Diver, 3 Air Raider, 4 Fencer; 0 off) and look `model` (0..3) sits there, in
// colour preset `preset` (0..11, 0xFF the model's own). A real rider always wins.
void SetCrewFigureTest(unsigned kind,unsigned model,unsigned preset) noexcept;
// In the test, model kCrewTestOwn and preset kCrewTestOwnColours take the
// player's own look and colours (from their own seat's soldier).
inline constexpr unsigned kCrewTestOwn=0xF,kCrewTestOwnColours=0xFE;
// Which of its class's four looks a soldier wears (0..3, -1 unknown): the file
// its body model was loaded from (body model +0x88 -> +0x20 -> a path such as
// "APP:\OBJECT\P605_RANGER.MRAB"; the CREWLOOK logs of 2026-10-01, which print
// that inner offset in decimal as "+0x32"), matched
// against the class's models (crew_figures.h CrewModelName). kind: 1..4.
int CrewLookOf(const void* soldier,unsigned kind) noexcept;
// A soldier's two colours (main, sub: RGB 0-1, as the lobby's palette and its
// custom colours give them). They live in a small block -- (0.5,0.5,0.5,0),
// colour 1, colour 2 with a 4th value of 0.5 -- found 2026-10-01 by scanning
// memory for the user's own and a friend's known colours. The routes to it
// differ by soldier (the player's own: body model +0x458 -> +0xF0 or +0x150; a
// friend online: body model +0xF0 -> +0x40 -> +0x30, the other table holding
// no block at all), so the block is recognised by that content: the known
// routes first, then a search of the soldier's pointers three deep. The block
// found is remembered a soldier (a failed search retried after 5 s). route, when
// given: 1-3 a known route, 4 the search, 0 none.
bool CrewColoursOf(const void* soldier,float main[4],float sub[4],int* route=nullptr) noexcept;
// The Barga cockpit's last forward shift from its chest's lean (metres; + forward).
float BargaChestLean() noexcept;
bool ValidateCockpitModel(const ImageProfile&,void* liveModel,const CockpitRig&) noexcept;
// Recenter heading only. Looking up/down while boarding must not become the
// seat's pitch/roll zero, or lowering the head afterwards tilts the whole cabin.
bool VehicleHeadingReference(const Quat& head,Quat& reference) noexcept;
struct VehicleEntryLevel {float pitch=0;bool valid=false;};
// Remove the initial native chase-camera pitch once, then inherit subsequent
// vehicle/camera pitch changes. Do not continuously flatten a moving vehicle.
bool LevelVehicleAtEntry(VehicleEntryLevel&,const Matrix&,Matrix&) noexcept;
// Pure presentation transform: no game pointers or movement/collision writes.
// Native seat rotation (including slope roll) and translation are preserved.
bool ComposeVehicleCamera(const Matrix& nativeCamera,const Quat& reference,
    const Quat& head,const Vec3& positionDelta,Matrix& output,float& localYaw) noexcept;
void RebaseVehicleStick(float yaw,float& x,float& y) noexcept;
// The gun hand pointed like a laser, read as the right stick in a vehicle (the
// user, 2026-10-01): its pointing direction's angle above `level` pushes up,
// its angle to the left of the cabin's front pushes left. Only the
// controller's direction counts, never where it is. level is the elevation the
// hand holds as level (measured, the user's natural hold points above the
// horizon). Each angle within `deadzone` (radians) reads nothing, so a hand
// held about straight ahead leaves the aim still; past it the push grows
// evenly to full at `full`. reference: the cabin's heading
// (VehicleHeadingReference); aim: the controller's aim pose. Both in the
// reference space. False, and no push, for a bad quaternion or range. angles
// gets the two raw angles (radians; up off the true horizon) when given.
struct HandAimAngles { float right=0,up=0; };
// Which switch a seat's hand aim answers to (the user, 2026-10-01: one on/off
// per kind in the settings exe). By the cabin built for the seat: the Gravis
// and the Combat Wagon go with the Nix, the Proteus's driver and missile seats
// with the gun seats; a seat with no cabin (bikes, trucks, the armed pickups)
// has none, and no hand aim.
enum class HandAimClass : int { None=-1, Nix, Depth, Barga, Tank, Combat, Heli, Gunner, BruteGunner, Count };
HandAimClass HandAimClassOf(CockpitKind kind) noexcept;
bool HandAimStick(const Quat& reference,const Quat& aim,float level,float deadzone,float full,float& x,float& y,HandAimAngles* angles=nullptr) noexcept;
// Where the left stick's forward points: the machine's own forward, the hull
// for vehicles (the cabin or chassis basis the view is built on). Walkers
// (Nix, Proteus, Barga, Gravis) read the stick on their lower body in the
// game: false for them. Driver's seat only; false leaves the stick as read.
// probe, when given, gets a walker's bones for the log (valid only then).
struct WalkerProbe { bool valid=false; Vec3 koshi{},body{},position{}; };
bool VehicleStickForward(const ImageProfile&,const VehicleSeat&,NodeLookup,const Matrix& hull,Vec3& forward,WalkerProbe* probe=nullptr) noexcept;
// The stick's rebase angle: the forward's yaw off the native camera, which is
// the frame EDF6 reads the stick in. Positive turns left, as RebaseVehicleStick.
bool VehicleStickYaw(const Matrix& nativeCamera,const Vec3& forward,float& yaw) noexcept;
// Machines whose stick the game already reads on their own hull, not the
// camera: the stick goes to them as it is. The Negling (hardware, 2026-09-25:
// hull at 12, launcher at 9, the camera rebase made the stick drive at 9),
// the Combat Wagon, the Nix on its lower body, and the six tanks (hardware,
// 2026-09-27: the Kebler drove along its barrel).
bool VehicleStickOnHull(const ImageProfile&,const VehicleSeat&) noexcept;
}
