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
