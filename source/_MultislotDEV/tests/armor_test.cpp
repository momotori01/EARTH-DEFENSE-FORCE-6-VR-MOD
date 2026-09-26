// "copy armor" (armor.h) without a game: the armor a pickup count gives, the count an armor needs, who
// gets copied, and which member the machine is. The three sites that read the count are checked against
// EDF.dll by tests.cpp; this is the part that decides what they hand back.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <cwchar>
#include <vector>

#include "../src/armor.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// Ranger-like numbers from the real game: 2014 armor is 1871 base plus pickups (the probe of 2026-09-19).
constexpr ArmorRules kRanger{150.0f, 1.0f};
constexpr ArmorRules kFencer{300.0f, 2.0f};

RoomMemberArmor Member(int soldier, int armor, bool valid = true) { return RoomMemberArmor{soldier, armor, valid}; }

// Ranger, Wing Diver, Air Raider, Fencer, as the INI ships them.
constexpr int caps[kSoldierTypeCount] = {4000, 2500, 3000, 4000};

}  // namespace

int main() {
    // The number every screen shows is base + step * pickups, truncated as the game publishes it.
    Check(ArmorFor(kRanger, 0) == 150 && ArmorFor(kRanger, 50) == 200, "armor is base plus a step per pickup");
    Check(ArmorFor(kFencer, 100) == 500, "a class with a bigger step");
    Check(ArmorFor(kRanger, -5) == 150, "a negative count counts as none");
    Check(ArmorFor({-10.0f, 1.0f}, 0) == 0, "armor never goes below zero");

    // The count to reach an armor, never below the count already held.
    Check(CountFor(kRanger, 0, 200) == 50 && ArmorFor(kRanger, 50) >= 200, "the count that first reaches the armor");
    Check(CountFor(kFencer, 0, 501) == 101, "a step that overshoots still reaches the armor");
    Check(CountFor(kFencer, 0, 500) == 100, "an armor that lands exactly on a step needs no extra");
    Check(CountFor(kRanger, 80, 200) == 80, "an armor below your own leaves the count alone");
    Check(CountFor({150.0f, 0.0f}, 7, 900) == 7, "a class that gains nothing per pickup is left alone");

    // Who to copy: the lowest of your own class, otherwise the lowest of the room, never yourself.
    const std::vector<RoomMemberArmor> room{Member(0, 1146), Member(3, 2014), Member(3, 4312), Member(0, 6486)};
    Check(TargetArmor(room.data(), room.size(), 1, 3, 2014, 0) == 4312, "the lowest armor of your own class");
    Check(TargetArmor(room.data(), room.size(), 1, 2, 2014, 0) == 1146, "no one of your class: the lowest in the room");
    Check(TargetArmor(room.data(), room.size(), room.size(), 3, 9999, 0) == 2014, "with nobody left out, you are counted too");
    Check(TargetArmor(room.data(), 0, 0, 0, 0, 0) == 0 && TargetArmor(nullptr, 4, 0, 0, 0, 0) == 0,
          "an empty room has nobody to copy");
    const std::vector<RoomMemberArmor> alone{Member(3, 2014)};
    Check(TargetArmor(alone.data(), alone.size(), 0, 3, 2014, 0) == 0, "alone in the room there is nobody to copy");
    const std::vector<RoomMemberArmor> unreported{Member(3, 2014), Member(0, 500, false), Member(0, 0)};
    Check(TargetArmor(unreported.data(), unreported.size(), 0, 3, 2014, 0) == 0,
          "a member who has not reported a class and armor is not copied");

    // A member barely different from you is no help, so they are passed over for the next one up. With
    // A 3000, B 2000, C 300 and D 200, both C and D reach past each other to B.
    const std::vector<RoomMemberArmor> beginners{Member(0, 3000), Member(0, 2000), Member(0, 300), Member(0, 200)};
    Check(TargetArmor(beginners.data(), beginners.size(), 2, 0, 300, 300) == 2000, "C passes over D and copies B");
    Check(TargetArmor(beginners.data(), beginners.size(), 3, 0, 200, 300) == 2000, "D passes over C and copies B");
    Check(TargetArmor(beginners.data(), beginners.size(), 2, 0, 300, 0) == 200,
          "without the gap, the lowest is taken as before");
    Check(TargetArmor(beginners.data(), beginners.size(), 0, 0, 3000, 300) == 200,
          "someone well ahead still sees everyone (and will not be lowered to them)");
    const std::vector<RoomMemberArmor> allClose{Member(0, 1000), Member(0, 1100), Member(0, 1200)};
    Check(TargetArmor(allClose.data(), allClose.size(), 0, 0, 1000, 300) == 0,
          "a room where everyone is close has nobody worth copying");

    // Which member this machine is: the one publishing this class and this armor.
    Check(FindSelf(room.data(), room.size(), 3, 2014) == 1, "you are the member with your class and armor");
    Check(FindSelf(room.data(), room.size(), 3, 9999) == room.size(), "no match leaves nobody out");
    Check(FindSelf(room.data(), room.size(), 1, 1146) == room.size(), "the armor alone is not enough");
    Check(FindSelf(nullptr, 4, 0, 0) == 4, "no member list leaves nobody out");
    // Two members with the same class and armor: the first is left out, and the other still gets copied.
    const std::vector<RoomMemberArmor> twins{Member(0, 300), Member(0, 300), Member(0, 900)};
    const std::size_t self = FindSelf(twins.data(), twins.size(), 0, 300);
    Check(self == 0 && TargetArmor(twins.data(), twins.size(), self, 0, 300, 0) == 900,
          "the first of two with your numbers is left out, and the second is passed over as too close");

    // More than one machine copying at once. The room always shows real armor - a raise never reaches it -
    // so every machine decides from the same fixed numbers and one round is the whole story.
    struct Machine {
        int real;      // the armor the save gives, and what the room shows
        bool copying;  // the feature is on here
        int mission;   // the armor this machine takes into the mission
    };
    const auto settle = [&](std::vector<Machine>& room, const ArmorRules& rules, int soldier, int gap) {
        for (int round = 0; round < 5; ++round)
            for (std::size_t me = 0; me < room.size(); ++me) {
                std::vector<RoomMemberArmor> seen;
                for (const Machine& machine : room) seen.push_back(Member(soldier, machine.real));
                if (!room[me].copying) {
                    room[me].mission = room[me].real;
                    continue;
                }
                const std::size_t self = FindSelf(seen.data(), seen.size(), soldier, room[me].real);
                const int target = TargetArmor(seen.data(), seen.size(), self, soldier, room[me].real, gap);
                const int mine = CountFor(rules, 0, room[me].real);
                room[me].mission = target > room[me].real ? ArmorFor(rules, CountFor(rules, mine, target))
                                                          : room[me].real;
            }
    };
    // The room the rule was written for: two beginners far below two veterans. Each beginner passes over the
    // other and reaches B, the first player who is really ahead of them.
    std::vector<Machine> beginnersRoom{{3000, false, 0}, {2000, false, 0}, {300, true, 0}, {200, true, 0}};
    settle(beginnersRoom, kRanger, 0, 300);
    Check(beginnersRoom[2].mission == 2000 && beginnersRoom[3].mission == 2000,
          "two beginners both reach the first player who is really ahead");
    // And B stays where B is. B can see the beginners as they really are, so the lowest B could copy is
    // below B, and nothing happens - whether or not B has it switched on.
    Check(beginnersRoom[0].mission == 3000 && beginnersRoom[1].mission == 2000, "the veterans are left alone");
    std::vector<Machine> everyone{{3000, true, 0}, {2000, true, 0}, {300, true, 0}, {200, true, 0}};
    settle(everyone, kRanger, 0, 300);
    Check(everyone[0].mission == 3000 && everyone[1].mission == 2000 && everyone[2].mission == 2000 &&
              everyone[3].mission == 2000,
          "with everyone copying, the veterans still do not move");
    // Two beginners on the same armor pass each other over instead of leaving each other alone.
    std::vector<Machine> tied{{2000, false, 0}, {300, true, 0}, {300, true, 0}};
    settle(tied, kRanger, 0, 300);
    Check(tied[1].mission == 2000 && tied[2].mission == 2000, "the same armor at the bottom is passed over, not copied");
    // A step that does not divide evenly lands on the first count that reaches the armor, once.
    std::vector<Machine> stepped{{3000, false, 0}, {2000, false, 0}, {300, true, 0}};
    settle(stepped, ArmorRules{150.0f, 3.0f}, 0, 300);
    Check(stepped[2].mission >= 2000 && stepped[2].mission <= 2003, "rounding up reaches the armor and stops there");

    // The ceiling: a rescue up to what the class is allowed, never past it.
    Check(CappedArmor(2000, 4000) == 2000 && CappedArmor(6000, 4000) == 4000, "the ceiling holds a big copy down");
    Check(CappedArmor(6000, 0) == 6000, "no ceiling lets everything through");
    Check(CappedArmor(2500, 2500) == 2500, "a copy that lands on the ceiling is kept");
    const std::vector<RoomMemberArmor> rich{Member(1, 9000), Member(1, 6000), Member(1, 400)};
    const int found = TargetArmor(rich.data(), rich.size(), 2, 1, 400, 300);
    Check(found == 6000 && CappedArmor(found, 2500) == 2500,
          "a Wing Diver among veterans is lifted to 2500, not to theirs");
    const std::vector<RoomMemberArmor> onlyVeterans{Member(1, 9000), Member(1, 6000)};
    Check(CappedArmor(TargetArmor(onlyVeterans.data(), onlyVeterans.size(), 1, 1, 6000, 300), 2500) == 2500,
          "someone already past the ceiling is offered nothing above it, so nothing happens");

    // The feature is off until it is switched on, and it has no key until one is installed.
    Check(!CopyArmor() && CopyArmorKey() == 0 && CopyArmorPad() == 0, "off with no input before it is installed");
    InitArmor(nullptr, 0x73, 0xB0, L"F4/LS", 300, caps);
    Check(CopyArmorKey() == 0x73 && CopyArmorPad() == 0xB0 && !CopyArmor(), "installed with a key and a pad button, still off");
    Check(std::wcscmp(CopyArmorHint(), L"F4/LS") == 0, "the label guide names both inputs");
    InitArmor(nullptr, 0, 0, L"F4/LS", 300, caps);
    Check(CopyArmorHint()[0] == 0, "with no input there is no guide to show");
    InitArmor(nullptr, 0x73, 0xB0, L"F4/LS", 300, caps);
    SetCopyArmor(true);
    Check(CopyArmor(), "the key switches it on");
    SetCopyArmor(false);
    Check(!CopyArmor(), "and off again");
    // Without a game there is nothing to read; nothing may be raised and nothing may crash.
    NoteRoomMembers(room.data(), room.size());
    ForgetRoom();
    Check(ArmorHookHandler(0x78EC3D) && ArmorHookHandler(0x595CD9) && ArmorHookHandler(0x595A12),
          "the raised sites have handlers: the sync, and this machine's player offline and online");
    Check(ArmorHookHandler(0x0D7BFA) == nullptr, "the value the room is shown is not one of them");
    Check(ArmorHookHandler(0x123456) == nullptr, "an unknown site has none");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("copy armor verified\n");
    return 0;
}
