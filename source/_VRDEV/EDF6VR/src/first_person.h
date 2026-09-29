#pragma once
#include "camera_math.h"
#include "image_profile.h"

namespace edf6vr {
inline constexpr std::size_t kBodyModelOffset=0x860;
// The weapon carries its drawn model the same way the soldier carries his: as a
// sub-object inside itself, not as a pointer to one. Found by watching which
// drawn models sit a short way past the weapon object, with the body landing on
// soldier+0x860 in the same sweep, which is what says the method is sound.
inline constexpr std::size_t kWeaponModelOffset=0xF30;
inline constexpr std::size_t kSkeletonOffset=0x900;
inline constexpr std::uint32_t kModelVtableRva=0x17C4030;
inline constexpr std::uint32_t kModelDrawRva=0x6BF9F0;
inline constexpr std::uint32_t kModelDrawSlotRva=kModelVtableRva+8;
inline constexpr std::uint32_t kNodeLookupRva=0x11001F0;
using NodeLookup=void* (__fastcall*)(void*,const void*);
bool CheckFirstPersonProfile(const ImageProfile&) noexcept;
// Exact supported types only. HeavyArmor uses native pad aim and native weapon poses in the initial trial.
bool IsSupportedSoldier(const ImageProfile&,const void* soldier) noexcept;
unsigned WeaponSlotCount(void* soldier) noexcept;
struct EyeSettings { float headUp=0.15f; float fallbackHeight=1.70f; };
struct PlayerPose {
    void* body=nullptr;
    void* skeletonResource=nullptr;
    void* nodeArray=nullptr;
    std::uint32_t objectId=0;
    float eye[3]{};
    // The soldier's own transform. The head bone hangs off this, so the part of
    // the head's movement that is animation rather than travel is exactly the
    // part that moves in this frame while this frame stays still.
    Matrix root{};
    bool rootValid=false;
    bool headFound=false;
    int headIndex=-1;
    // The bone the weapon hangs on.
    //
    // Every Ranger weapon in the game's own data names it: the weapon objects
    // carry ModelConstraint = "arms_r" (one carries "arms"), which is a node in
    // the soldier's skeleton. So the weapon mesh is not a model to be found and
    // moved, it is a model tied to a bone -- and the bone can be looked up by
    // name exactly as the head already is.
    void* armsNode=nullptr;
    Matrix armsWorld{};
    bool armsFound=false;
    int armsIndex=-1;
};
// Reads this camera's supported local soldier and its embedded body model.
bool ReadPlayerPose(const ImageProfile&,void* soldier,NodeLookup,const EyeSettings&,PlayerPose&,bool allowMounted=false) noexcept;
// Where a named bone of the soldier's skeleton is, in the world.
//
// The shot leaves from where the model holds the gun -- the shoulder for a
// launcher, the waist for a rifle -- which is a bone, and naming it is the
// whole question. arms_r is not it: written every tick and read back unchanged
// a tick later, it moved nothing. So the rest of the skeleton is asked, by the
// same name lookup that finds arms_r, and whichever bone sits where the bullet
// appears is the one to move.
bool ReadNamedBone(void* soldier,NodeLookup,const wchar_t* name,float world[3]) noexcept;
// The bone's world axes (rows 0-2 of its matrix) and position.
bool ReadNamedBoneFrame(void* soldier,NodeLookup,const wchar_t* name,float rows[3][3],float world[3]) noexcept;
// The node itself, so it can be written as well as read.
//
// Names come from the game's own soldier definition rather than from guesswork:
// sgott's AIARMYSOLDIER_AF lists the whole skeleton, and it is romanised
// Japanese -- koshi for the waist, kata_r for the shoulder, mune for the chest,
// te_r for the hand -- which is why twenty-two English guesses resolved three.
// It also names aim_center, aim_target and weapon, none of which are anatomy.
void* FindNamedBone(void* soldier,NodeLookup,const wchar_t* name) noexcept;
bool WriteBonePosition(void* node,const float world[3]) noexcept;

// --- Shared SoldierBase aim/input state, confirmed against EDF.dll ---
// 573BA8..573BCF adds the look input to the aim angles; 5705B8 stores
// atan2(forward.x, forward.z) into the yaw field, which fixes the convention.
inline constexpr std::size_t kMoveInputOffset=0xD50;    // x, y(unused), z
inline constexpr std::size_t kLookInputOffset=0xD60;    // pitch delta, yaw delta
inline constexpr std::size_t kAimAngleOffset=0x1230;    // pitch, yaw, third, w
inline constexpr std::size_t kAimLimitOffset=0x1250;    // symmetric limits, negative disables
inline constexpr std::size_t kSoldierWorldOffset=0x60;  // row0..row3, row3 is the position
inline constexpr std::uint32_t kInputReadRva=0x570650;
inline constexpr std::uint32_t kInputCallSiteRva=0x572FFB;

struct SoldierAim {
    float pitch=0, yaw=0, third=0;
    float position[3]{};
    float moveInput[2]{};   // x, z
    float lookInput[2]{};   // pitch delta, yaw delta
    float pitchLimit=-1, yawLimit=-1;
};
// Verifies the byte patterns for every aim/input site before trusting the offsets.
bool CheckAimProfile(const ImageProfile&) noexcept;
bool ReadSoldierAim(void* soldier,SoldierAim&) noexcept;
// Writes the absolute aim angles and the move/look input for one frame.
// Only valid from inside the input-read hook, before the game consumes them.
bool WriteSoldierAim(void* soldier,float pitch,float yaw) noexcept;
bool WriteSoldierInput(void* soldier,float moveX,float moveZ,float lookPitch,float lookYaw) noexcept;
bool PlaceAtEye(const Matrix& original,const float eye[3],Matrix& output) noexcept;
// Preserve the render-activity bookkeeping used by the game's animation update.
bool MarkBodyActive(void* model,int pass) noexcept;
// Read-only direction used by CharacterGhostCamera's ballistic preview (F8346..F838E).
// It is a diagnostic, not proof that every weapon/projectile uses that vector.
bool ReadWeaponDirection(void* soldier,unsigned int slot,void*& weapon,float direction[3]) noexcept;
// The weapon's own world transform, read only.
//
// F8352 loads it as a pointer from weapon+0x1D0 and multiplies the weapon's
// local direction (weapon+0x350) by the rows at +0x50, +0x60 and +0x70; +0x80
// is the translation row. So the weapon carries a full basis of its own, which
// is what a hand-held weapon would have to be written into.
struct WeaponPose {
    Matrix world{};      // rows from transform+0x50, translation from +0x80
    float forward[3]{};  // the ballistic direction, already normalised
    void* weapon=nullptr;
    void* transform=nullptr;
    void* model=nullptr;     // the AnimationModel that is actually drawn
    // The weapon's own skeleton. Laid out exactly like the soldier's -- the
    // registry sits 0xA0 past the model, the node array at +0x10 of that, the
    // count at +0x20, each node 0x110 with its world matrix at +0xB0 -- and it
    // has four nodes. A skinned mesh is placed by the bones handed to the draw,
    // so these are where the drawn weapon actually is.
    void* nodes=nullptr;
    unsigned long long nodeCount=0;
    bool valid=false;
};
// Where the shot leaves from, as far as anything here knows.
//
// Writing the weapon's transform moved the muzzle flash and left the bullet
// coming out of the body, so the firing code reads elsewhere. A read-only sweep
// of the weapon object reported exactly two float triples that sit near the
// soldier: this one, which drifts with the weapon between chest and eye height,
// and one at +0x180 that never moves at all. The moving one is the candidate.
// Three floats, anywhere, for hunting the shot origin.
//
// Neither position the weapon object holds was it. The sweep of the soldier
// found one at chest height -- 0.83m above his feet, well below the eye --
// repeated at +0x8B0, +0x9A0 and +0xD00, which is where a bullet that comes out
// of the body would come from. Which object and which offset are both settings,
// so the next candidate costs an INI line rather than a build.
bool WritePoint(void* object,std::size_t offset,const float world[3]) noexcept;
bool ReadWeaponPose(void* soldier,unsigned int slot,WeaponPose&) noexcept;
// Same validated geometry for an owned weapon not in an active native slot.
bool ReadOwnedWeaponPose(void* soldier,void* weapon,WeaponPose&) noexcept;
// Move the drawn weapon, and only the drawn weapon.
//
// The translation row is written; the basis is left exactly as the game built
// it. That is deliberate. The ballistic direction is the weapon's local
// direction turned by that basis, and it was measured to agree with the aim
// angles to five decimal places, so leaving the basis alone keeps the muzzle
// pointing where the shot goes -- for free, and permanently. Writing the basis
// would make "where the gun points" and "where the bullet goes" two things that
// have to be kept in agreement by hand.
bool WriteWeaponPosition(const WeaponPose&,const float world[3]) noexcept;
}
