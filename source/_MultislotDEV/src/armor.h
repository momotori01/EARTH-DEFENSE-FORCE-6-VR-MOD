#pragma once
#include <cstddef>
#include <cstdint>

#include "midhook.h"

namespace multislot {

// "copy armor" (F4 on a menu screen): while it is on, this machine's armor is raised to the lowest armor
// among the other members of the room - of your own class if anyone else plays it, otherwise of the whole
// room. It never lowers anything and it never writes the save.
//
// What the game keeps is not an armor number but the count of armor pickups, one per class, at
// GameStatus+0x6F88 + (slot * 0xF98 + class) * 4; the number every screen shows is
// base(class) + step(class) * count (D7BD0, with base E3470 and step E34A0 of the game data at
// GameStatus+0x130). That count is read in exactly three places that matter, all of them "publish" or
// "create" (ArmorHooks() in patches.h):
//   78EC3D the mission sync blob, which becomes every machine's copy of your loadout record
//   595CD9 this machine's own player object, offline
//   595A12 this machine's own player object, online: there every player comes from a loadout record, so the
//          record is read instead of the save and only the one CreatePlayers says we control is raised
//          (1.5.1; 1.5.0 raised only the offline read, so online everyone else saw the copied armor and
//          this machine kept its real one)
// The value published to the room (D7BFA) is deliberately left alone, so the room always shows real armor.
// Raising it there too would feed back: everyone reads the room to decide what to copy, so a raised number
// would become someone else's source, and a room with several beginners in it would climb.
// The four places that write the count back (AB311 save load, 87AF8F / 87B028 / 87B09F the armor screen)
// all take their new value from the screen's own state, never from a read of the count, so a raised count
// cannot reach the save. Your own armor screen keeps showing the real number for the same reason.
struct RoomMemberArmor {
    int soldier;  // class id: 0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer
    int armor;    // the number the room panel shows, or 0 when the member has not reported one
    bool valid;   // both of those are set
};

// `hint` is what the menu label calls the inputs ("F4/LS"); it is copied. `ignoreWithin` is how close
// another member's armor has to be to this machine's for them to be passed over (300 by default): copying
// someone who is barely different would not help, so the next one up is taken instead. `caps` is the most
// armor this may give, one per class in class order (0 = no ceiling); it is a rescue, not a shortcut to
// someone else's armor.
void InitArmor(unsigned char* gameBase, int key, std::uint32_t padButton, const wchar_t* hint, int ignoreWithin,
               const int* caps);
constexpr int kSoldierTypeCount = 4;  // 0 Ranger, 1 Wing Diver, 2 Air Raider, 3 Fencer
// F4 (0 = the feature is not installed).
int CopyArmorKey();
// The pad button that does the same, as a channel offset (0 = none). The page button switches member
// pages with one stick, so this defaults to the other one: a VR controller can reach both.
std::uint32_t CopyArmorPad();
// The inputs as the label names them, "" when the feature is not installed.
const wchar_t* CopyArmorHint();
// The armor this machine is being given, 0 when it is not being raised. The room screen keeps showing the
// real armor, so this is what the menu label reports instead.
int CopyArmorTo();
// True when the armor found was above this class's ceiling and was held down to it (the label says "(Max)").
bool CopyArmorAtMax();
bool CopyArmor();
void SetCopyArmor(bool on);
// One room screen update: recomputes what this machine's armor should be.
void NoteRoomMembers(const RoomMemberArmor* members, std::size_t count);
// Nothing to copy from any more (the room screen is gone).
void ForgetRoom();
// This machine's own class, pickup count and the armor they give, as every screen would show it. False
// when the game is not there to read. Used for the log, so a wrong reading is visible before it matters.
bool LocalArmor(int& soldier, int& count, int& armor);

// Mid-function hooks for ArmorHooks() in patches.h, by site RVA.
MidHandler ArmorHookHandler(std::uint32_t rva);

// Test seams. `count` is the pickup count the game holds for `soldier`; the result is the count to use.
struct ArmorRules {
    float base;   // armor with no pickups
    float step;   // armor per pickup
};
int ArmorFor(const ArmorRules& rules, int count);
// The pickup count that first reaches `armor`, never below `count`.
int CountFor(const ArmorRules& rules, int count, int armor);
// The armor to copy: the lowest among the other members of `soldier`, else the lowest among all the others.
// Members within `ignoreWithin` of `mine` are passed over, so a room full of beginners reaches up to the
// first player who is really ahead. Returns 0 when there is nobody to copy from. `self` is the index to
// leave out, or `count` for none.
int TargetArmor(const RoomMemberArmor* members, std::size_t count, std::size_t self, int soldier, int mine,
                int ignoreWithin);
// Which member this machine is, by the class and armor it publishes; `count` when none matches.
std::size_t FindSelf(const RoomMemberArmor* members, std::size_t count, int soldier, int armor);
// `target` held down to `cap` (0 = no ceiling).
int CappedArmor(int target, int cap);

}  // namespace multislot
