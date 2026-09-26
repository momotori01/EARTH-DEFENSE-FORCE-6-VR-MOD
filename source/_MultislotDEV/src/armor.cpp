#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "armor.h"

#include <atomic>
#include <cstring>
#include <cwchar>

#include "log.h"
#include "mission.h"
#include "patches.h"

namespace multislot {
namespace {

constexpr std::uint32_t kArmorCounts = 0x6F88;   // GameStatus: pickups, [slot * 0xF98 + class]
constexpr std::uint32_t kLocalPlayers = 0x6E90;  // GameStatus: this machine's class, 0x3E60 per slot
constexpr std::uint32_t kGameData = 0x130;       // GameStatus: the game data the rules come from
constexpr std::uint32_t kArmorBase = 0x0E3470;   // (game data, class) -> armor with no pickups
constexpr std::uint32_t kArmorStep = 0x0E34A0;   // (game data, class) -> armor per pickup
constexpr int kSoldierTypes = kSoldierTypeCount;

unsigned char* game = nullptr;
int copyKey = 0;
std::uint32_t copyPad = 0;
int ignoreArmorWithin = 0;
int armorCaps[kSoldierTypeCount]{};
wchar_t copyHint[24]{};
std::atomic<bool> copyArmor{false};
// Written on the UI thread from the room screen, read on whatever thread creates players. A count of -1
// is "leave the game's own value alone"; `forClass` says which class it was worked out for.
std::atomic<int> raisedCount{-1};
std::atomic<int> raisedFor{-1};
std::atomic<int> raisedTo{0};
std::atomic<int> ownArmor{0};
std::atomic<bool> raisedAtMax{false};
// The raise each of the three sites last reported, so switching off and on says it again.
int lastRaised[3]{};
unsigned reported = 0;

const std::uint8_t* Status() {
    if (!game) return nullptr;
    std::uint64_t status = 0;
    std::memcpy(&status, game + kGameStatusPointer, sizeof(status));
    return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(status));
}

int Field32(const std::uint8_t* at, std::size_t offset) {
    std::int32_t value = 0;
    std::memcpy(&value, at + offset, sizeof(value));
    return value;
}

using ClassRuleFn = float(__fastcall*)(const void*, int);

bool RulesFor(const std::uint8_t* status, int soldier, ArmorRules& rules) {
    if (!status || soldier < 0 || soldier >= kSoldierTypes) return false;
    __try {
        const void* data = status + kGameData;
        rules.base = reinterpret_cast<ClassRuleFn>(game + kArmorBase)(data, soldier);
        rules.step = reinterpret_cast<ClassRuleFn>(game + kArmorStep)(data, soldier);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return rules.step > 0.0f;
}

void Clear(const char* why) {
    if (raisedCount.exchange(-1) != -1) Log("ARMOR copy armor: no longer raising your armor (%s)", why);
    raisedFor.store(-1);
    raisedTo.store(0);
    ownArmor.store(0);
    raisedAtMax.store(false);
    lastRaised[0] = lastRaised[1] = lastRaised[2] = 0;
}

}  // namespace

int ArmorFor(const ArmorRules& rules, int count) {
    if (count < 0) count = 0;
    // The game truncates the float when it publishes the number (cvttss2si at 7480B1).
    const float armor = rules.base + rules.step * static_cast<float>(count);
    return armor <= 0.0f ? 0 : static_cast<int>(armor);
}

int CountFor(const ArmorRules& rules, int count, int armor) {
    if (count < 0) count = 0;
    if (rules.step <= 0.0f) return count;
    while (ArmorFor(rules, count) < armor) {
        const int next = count + 1;
        if (next <= count) break;  // a count that cannot grow any further
        count = next;
    }
    return count;
}

int TargetArmor(const RoomMemberArmor* members, std::size_t count, std::size_t self, int soldier, int mine,
                int ignoreWithin) {
    if (!members) return 0;
    int sameClass = 0, anyClass = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i == self || !members[i].valid || members[i].armor <= 0) continue;
        const int apart = members[i].armor - mine;
        if ((apart < 0 ? -apart : apart) <= ignoreWithin) continue;  // too close to be worth copying
        if (!anyClass || members[i].armor < anyClass) anyClass = members[i].armor;
        if (members[i].soldier != soldier) continue;
        if (!sameClass || members[i].armor < sameClass) sameClass = members[i].armor;
    }
    return sameClass ? sameClass : anyClass;
}

int CappedArmor(int target, int cap) { return cap > 0 && target > cap ? cap : target; }

std::size_t FindSelf(const RoomMemberArmor* members, std::size_t count, int soldier, int armor) {
    if (!members) return count;
    for (std::size_t i = 0; i < count; ++i)
        if (members[i].valid && members[i].soldier == soldier && members[i].armor == armor) return i;
    return count;
}

void InitArmor(unsigned char* gameBase, int key, std::uint32_t padButton, const wchar_t* hint, int ignoreWithin,
               const int* caps) {
    game = gameBase;
    copyKey = key;
    copyPad = padButton;
    ignoreArmorWithin = ignoreWithin < 0 ? 0 : ignoreWithin;
    for (int i = 0; i < kSoldierTypes; ++i) armorCaps[i] = caps && caps[i] > 0 ? caps[i] : 0;
    copyHint[0] = 0;
    if (hint && (key || padButton)) {
        const std::size_t length = wcsnlen(hint, 24);
        if (length < 24) wmemcpy(copyHint, hint, length + 1);
    }
    copyArmor.store(false);
    Clear("start");
}

int CopyArmorKey() { return copyKey; }
std::uint32_t CopyArmorPad() { return copyPad; }
const wchar_t* CopyArmorHint() { return copyHint; }
int CopyArmorTo() { return copyArmor.load() ? raisedTo.load() : 0; }
bool CopyArmorAtMax() { return copyArmor.load() && raisedAtMax.load(); }
bool CopyArmor() { return copyArmor.load(); }

void SetCopyArmor(bool on) {
    if (copyArmor.exchange(on) == on) return;
    Log("ARMOR copy armor %s", on ? "ON - your armor follows the lowest in the room (it is never lowered)"
                                  : "OFF - your own armor again");
    if (!on) Clear("switched off");
}

bool LocalArmor(int& soldier, int& count, int& armor) {
    const std::uint8_t* status = Status();
    if (!status) return false;
    ArmorRules rules{};
    __try {
        soldier = Field32(status, kLocalPlayers);
        if (!RulesFor(status, soldier, rules)) return false;
        count = Field32(status, kArmorCounts + static_cast<std::size_t>(soldier) * 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    armor = ArmorFor(rules, count);
    return true;
}

void ForgetRoom() {
    if (raisedCount.load() != -1) Clear("the room screen is gone");
}

void NoteRoomMembers(const RoomMemberArmor* members, std::size_t count) {
    if (!copyArmor.load()) return;
    const std::uint8_t* status = Status();
    if (!status || !members || !count) return;
    int soldier = 0, mine = 0;
    ArmorRules rules{};
    __try {
        soldier = Field32(status, kLocalPlayers);
        if (soldier < 0 || soldier >= kSoldierTypes) return;
        if (!RulesFor(status, soldier, rules)) return;
        mine = Field32(status, kArmorCounts + static_cast<std::size_t>(soldier) * 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (mine < 0) return;
    const int own = ArmorFor(rules, mine);
    // Leave out the member this machine is. The room always has this machine's real armor, raised or not,
    // which is what makes everyone's decision here stable: nobody ever copies a copied number.
    const std::size_t self = FindSelf(members, count, soldier, own);

    const int found = TargetArmor(members, count, self, soldier, own, ignoreArmorWithin);
    const int target = CappedArmor(found, armorCaps[soldier]);
    if (target <= own) {
        Clear(!found                  ? "there is nobody to copy from"
              : found > target        ? "you are already at the most this can give your class"
                                      : "nobody in the room is far enough ahead of you");
        return;
    }
    const int wanted = CountFor(rules, mine, target);
    const int reached = ArmorFor(rules, wanted);
    if (wanted <= mine) {
        Clear("the armor to copy is not above your own");
        return;
    }
    // Both, always: `a.exchange(x) != x || b.exchange(y) != y` would skip storing the class whenever the
    // count changed, and the hooks only raise the entry of the class this was worked out for.
    const bool countChanged = raisedCount.exchange(wanted) != wanted;
    const bool classChanged = raisedFor.exchange(soldier) != soldier;
    raisedTo.store(reached);
    ownArmor.store(own);
    raisedAtMax.store(found > target);
    if (!countChanged && !classChanged) return;
    Log("ARMOR copy armor: class %d, your armor %d -> %d (copying %d%s, %d pickups instead of %d; anyone within "
        "%d of you is passed over)", soldier, own, reached, found,
        found > target ? " held down to this class's most" : "", wanted, mine, ignoreArmorWithin);
    for (std::size_t i = 0; i < count; ++i)
        Log("ARMOR   member %zu: class %d, armor %d%s%s", i + 1, members[i].soldier, members[i].armor,
            members[i].valid ? "" : " (not reported)", i == self ? " <- you" : "");
}

namespace {

// What each of the three sites does with the count, said once per value so the log shows which parts of the
// game actually took the raised armor - the room panel, everyone else's copy of you, and your own HP.
constexpr const char* kSiteNames[] = {"sent to everyone for the mission", "given to your own player (offline)",
                                      "given to your own player"};

// The three reads of the pickup count that publish it or create a player with it. Only this machine's own
// entry is raised: the index is slot * 0xF98 + class, and only slot 0 plays online.
template <int Site, std::uint64_t CpuContext::*Base, std::uint64_t CpuContext::*Index, std::uint64_t CpuContext::*Out>
void ArmorCountHandler(CpuContext* context) {
    const auto* status = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(context->*Base));
    const auto index = static_cast<std::uint32_t>(context->*Index);
    std::int32_t count = 0;
    __try {
        std::memcpy(&count, status + kArmorCounts + static_cast<std::size_t>(index) * 4, sizeof(count));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (!(reported & 1u)) {
            reported |= 1u;
            Log("ARMOR pickup count %u is out of range; using 0", index);
        }
        context->*Out = 0;
        return;
    }
    const int raised = raisedCount.load();
    if (raised > count && copyArmor.load() && static_cast<int>(index) == raisedFor.load()) {
        if (lastRaised[Site] != raised) {
            lastRaised[Site] = raised;
            Log("ARMOR armor %d instead of %d, %s", raisedTo.load(), ownArmor.load(), kSiteNames[Site]);
        }
        count = raised;
    }
    context->*Out = static_cast<std::uint32_t>(count);
}

// Replaces `mov esi, [rbx+rdx+0x118]`, the pickup count of the loadout record CreateOnlinePlayerObject is
// building a player from. Online this is where every player's armor comes from, including this machine's,
// so this is the online half of "given to your own player" - and it must raise only our own record, which
// is the one for the player CreatePlayers said we control.
void RecordArmorHandler(CpuContext* context) {
    std::int32_t count = 0;
    const auto at = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(context->rbx + context->rdx + 0x118));
    __try {
        std::memcpy(&count, at, sizeof(count));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        context->rsi = 0;
        return;
    }
    const int raised = raisedCount.load();
    const int index = LocalPlayerIndex();
    if (raised > count && copyArmor.load() && index >= 0 && static_cast<int>(context->rdi) == index) {
        if (lastRaised[2] != raised) {
            lastRaised[2] = raised;
            Log("ARMOR armor %d instead of %d, %s", raisedTo.load(), ownArmor.load(), kSiteNames[2]);
        }
        count = raised;
    }
    context->rsi = static_cast<std::uint32_t>(count);
}

}  // namespace

MidHandler ArmorHookHandler(std::uint32_t rva) {
    using C = CpuContext;
    switch (rva) {
        case 0x78EC3D: return &ArmorCountHandler<0, &C::rbx, &C::r9, &C::rax>;
        case 0x595CD9: return &ArmorCountHandler<1, &C::r10, &C::r8, &C::rax>;
        case 0x595A12: return &RecordArmorHandler;
        default: return nullptr;
    }
}

}  // namespace multislot
