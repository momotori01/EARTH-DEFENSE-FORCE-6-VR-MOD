#pragma once
#include "image_profile.h"
#include "weapon_hold.h"
#include <cstdint>
namespace edf6vr {
inline constexpr unsigned kLaserPrepareCall=0x6891A5;
inline constexpr unsigned kLaserQueryCtor=0x11A6B00;
struct LaserSightStats {
    std::uint64_t calls=0,applied=0,aimed=0,carried=0;
    // Why a call did not apply, one bucket each, in the order they are tested.
    // applied+stale+owner+identity+geometry+refused+reach == calls, so a laser
    // that stops following names the guard that stopped it instead of leaving
    // the next session to guess between seven of them.
    std::uint64_t stale=0,owner=0,identity=0,geometry=0,refused=0,reach=0;
    // Owner refusals where the attachment's weapon belongs to the same soldier
    // as a live frame but is neither frame's weapon: our own weapon the frames
    // do not name (a Ranger's left slug shot lit its laser from the right hand).
    std::uint64_t ownSoldierUnframed=0;
    void* lastUnframedOwner=nullptr;
    void* frameWeapons[2]{};
    float native[3]{},muzzle[3]{};
    // How far the game's own origin sits from the weapon's own node root. A
    // rigid property of the weapon: if it wanders, the two were not read from
    // the same pose and the carried origin cannot be trusted.
    float leverMin=1e30f,leverMax=0;
    // The walk the last applied origin was carried by (soldier+0x90 less the
    // frame's root), when, and for which weapon: the scope's camera takes the
    // same carry, or the laser starts a step off it while moving (scope.h).
    float walked[3]{};
    ULONGLONG walkedAt=0;
    void* walkedWeapon=nullptr;
};
// Identity and destination only. No weapon or bone coordinate is sampled on the
// update thread: the origin itself is taken from the game at the instant it has
// just derived it, together with the node root it belongs to.
struct LaserSightFrame {
    void* weapon=nullptr;
    void* soldier=nullptr;
    void* nodes=nullptr;      // the weapon's own node array, for re-validation
    std::uint64_t count=0;
    std::uint32_t objectId=0;
    WeaponHoldFrame hand{};   // where the drawn weapon's root is put
    float root[3]{};          // the soldier's root when that was measured
    ULONGLONG time=0;
};
// One frame per hand: 1 is the weapon the game itself has equipped, 0 the one
// the Ranger's left hand is holding. Its laser attachment belongs to that
// weapon and is refused by the owner guard of the other, so without its own
// frame the left laser stays wherever the original model has it -- which is
// what rolled and slid about while the right one was correct.
void PublishLaserFrame(const LaserSightFrame&,unsigned hand=1) noexcept;
void ClearLaserFrame(unsigned hand) noexcept;
void ClearLaserMuzzle() noexcept;
bool ApplyLaserMuzzle(void* attachment,ULONGLONG now) noexcept;
LaserSightStats ReadLaserSightStats() noexcept;
bool InstallLaserSight(const ImageProfile&,bool& changed) noexcept;
}
