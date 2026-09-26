#include "spawn.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstring>

#include "log.h"
#include "mission.h"
#include "patches.h"

namespace multislot {
namespace {

constexpr int kEnemyTeam = 1;              // team the Create*Enemy* natives give their objects
constexpr int kLargestScaledCount = 10000;  // far above any mission script; guards the arithmetic
constexpr int kSpawnLogLimit = 300;
std::atomic<int> spawnLines{0};

// Found in the mission scripts of the base game and both DLC packs (EDF.dll RTTI has the same names).
// Left out on purpose: AntHill, BigNest, GeneratorPoll, EDF6_TimeShip_Anchor, EDF6_TimeShip,
// EDF6_TimeShip_Capsule, UfoCarrier508, DropBoat512, EDF6_SpiderEggSac, SpiderNet, GiantAntEgg,
// AlienTrailer, ShootingTarget, DeiroiFallEffect, UfoMother511, MovingFortress, EDF6_GrandMother,
// EDF6_Jormungand, Humanoid_BigGreyBoss and anything not listed.
constexpr const char* kMultipliedClasses[] = {
    "GiantAnt", "GiantAntEX", "GiantSpider", "GiantSpiderEX", "GiantBee", "GiantBeeEX", "GiantDango",
    "EDF6_GiantDangoLarge", "Humanoid_Basic", "EDF6_Martian", "EDF6_Merman", "EDF6_ShellFish", "EDF6_SpinnerUfo",
    "EDF6_Venus", "EDF6_SquidSmall", "EDF6_Berserker_TypeA", "EDF6_Berserker_Middle", "EDF6_Berserker_TypeBommer",
    "EDF6_Berserker_Large", "EDF6_Berserker_TypeGrapple", "EDF6_Berserker_TypeShooter", "UfoSmall507", "UfoSmall515",
    "DragonSmall", "Nephila", "Monster501", "Monster504", "EDF6_FlyingDyno", "Deiroi",
};

const std::uint8_t* At(std::uint64_t address) { return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(address)); }

std::int32_t Int32At(std::uint64_t address) {
    std::int32_t value = 0;
    std::memcpy(&value, At(address), sizeof(value));
    return value;
}

void SetInt32At(std::uint64_t address, std::int32_t value) {
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), &value, sizeof(value));
}

// MSVC x64 RTTI of a polymorphic object: vtable[-1] is the complete object locator {signature 1, offset,
// constructor displacement offset, type descriptor RVA, hierarchy RVA, locator RVA}; the type descriptor
// holds two pointers and then the decorated name of the most derived class (".?AVGiantAnt@@").
bool ReadClassName(std::uint64_t object, char* out, std::size_t size) {
    __try {
        const std::uint64_t vtable = *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(object));
        const std::uint64_t locator = *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(vtable - 8));
        const auto* col = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(locator));
        if (col[0] != 1) return false;
        const std::uint64_t imageBase = locator - col[5];
        const auto* name = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(imageBase + col[3] + 16));
        std::size_t i = 0;
        for (; i + 1 < size && name[i]; ++i) out[i] = name[i];
        out[i] = 0;
        return i > 0 && name[i] == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadPointer(std::uint64_t address, std::uint64_t& value) {
    __try {
        value = *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(address));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ".?AVGiantAnt@@" -> "GiantAnt" (in place); nullptr for any other shape (namespaces, templates).
const char* PlainClassName(char* decorated) {
    if (std::strncmp(decorated, ".?AV", 4) != 0) return nullptr;
    char* name = decorated + 4;
    const std::size_t length = std::strlen(name);
    if (length < 3 || std::strcmp(name + length - 2, "@@") != 0) return nullptr;
    name[length - 2] = 0;
    return std::strchr(name, '@') ? nullptr : name;
}

bool ShouldScale(int& players) {
    players = MissionPlayers();
    return players > kVanillaPlayers && OnlineSession();
}

// Count for a script spawn whose first created object is `object`.
int CountForObject(const char* site, std::uint64_t object, std::int32_t team, std::int32_t count) {
    int players = 0;
    if (team != kEnemyTeam || count <= 0 || !object || !ShouldScale(players)) return count;
    char decorated[128];
    if (!ReadClassName(object, decorated, sizeof(decorated))) return count;
    const char* name = PlainClassName(decorated);
    const bool multiplied = name && IsMultipliedEnemyClass(name);
    const int scaled = multiplied ? ScaledEnemyCount(count, players) : count;
    if (DetailLog() && spawnLines.fetch_add(1) < kSpawnLogLimit)
        Log("SPAWN %s: %s x%d -> x%d (%d players)%s", site, name ? name : decorated, count, scaled, players,
            multiplied ? "" : ", not a multiplied enemy class");
    return scaled;
}

// Count for a generator: the objects it creates later are always enemies of the script's choice.
int CountForGenerator(const char* site, std::int32_t count) {
    int players = 0;
    if (count <= 0 || !ShouldScale(players)) return count;
    const int scaled = ScaledEnemyCount(count, players);
    if (DetailLog() && spawnLines.fetch_add(1) < kSpawnLogLimit) Log("SPAWN %s: x%d -> x%d (%d players)", site, count, scaled, players);
    return scaled;
}

// MissionContext::CreateObjectGroup (1D8B40, CreateEnemyGroup*, squads' followers, follower groups) and
// CreateAreaObject (1D7490, the *_Area natives), right after the factory call of each loop iteration:
// rax = the created entry (+0x28 the object), [rsp+0x78] = objects already in the result. The first
// object decides, and a larger count just lets the loop (`cmp index, [params+count]`) run on.
void GroupCreatedHandler(CpuContext* context, const char* site, std::uint64_t params, std::uint32_t countOffset,
                         std::uint32_t teamOffset) {
    std::uint64_t created = 0, object = 0;
    std::memcpy(&created, At(SiteRsp(context) + 0x78), sizeof(created));
    if (!context->rax || created || !ReadPointer(context->rax + 0x28, object)) return;
    const std::int32_t count = Int32At(params + countOffset);
    const int scaled = CountForObject(site, object, Int32At(params + teamOffset), count);
    if (scaled != count) SetInt32At(params + countOffset, scaled);
}

void PointGroupHandler(CpuContext* context) { GroupCreatedHandler(context, "enemy group", context->r14, 0x68, 0x70); }
void AreaGroupHandler(CpuContext* context) { GroupCreatedHandler(context, "enemy group in area", context->r13, 0x50, 0x58); }

// CreateFlyingEnemyGroup_OnRoute (1AF6F0) creates one object per loop pass with CreateObject:
// [rsp+0x60] = object, edi = pass, esi = count, CreateObject's parameters at rbp+0x10 (team +0x88).
void FlyingRouteHandler(CpuContext* context) {
    if (static_cast<std::uint32_t>(context->rdi) != 0) return;
    std::uint64_t object = 0;
    std::memcpy(&object, At(SiteRsp(context) + 0x60), sizeof(object));
    const auto count = static_cast<std::int32_t>(static_cast<std::uint32_t>(context->rsi));
    const int scaled = CountForObject("flying enemy group on route", object, Int32At(context->rbp + 0x98), count);
    if (scaled != count) context->rsi = static_cast<std::uint32_t>(scaled);
}

// Natives whose count arrives in r9d (before `mov reg32, r9d`).
void RegisterCount(CpuContext* context, const char* site) {
    const auto count = static_cast<std::int32_t>(static_cast<std::uint32_t>(context->r9));
    const int scaled = CountForGenerator(site, count);
    if (scaled != count) context->r9 = static_cast<std::uint32_t>(scaled);
}

void EnemyGeneratorHandler(CpuContext* context) { RegisterCount(context, "Object.SetEnemyGenerator"); }
void InstantGeneratorHandler(CpuContext* context) { RegisterCount(context, "Object.SetInstantEnemyGenerator"); }

bool ReadUInt32(std::uint64_t address, std::uint32_t& value) {
    __try {
        value = *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(address));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// EDF6_TimeShip_CreatePodCrue (1CABB0) once the pod is known ([rsp+0x40]) and before the crew loop runs
// r12d times. A pod seats what its SGO lists (e613_capsule: three `sheets`; seat count at +0x1098, seats
// taken at +0x107C) and crew beyond that stays outside the pod, so the count only grows into free seats.
void PodCrewHandler(CpuContext* context) {
    const auto count = static_cast<std::int32_t>(static_cast<std::uint32_t>(context->r12));
    int players = 0;
    if (count <= 0 || !ShouldScale(players)) return;
    std::uint64_t pod = 0, seats = 0;
    std::uint32_t taken = 0;
    std::memcpy(&pod, At(SiteRsp(context) + 0x40), sizeof(pod));
    if (!pod || !ReadPointer(pod + 0x1098, seats) || !ReadUInt32(pod + 0x107C, taken)) return;
    const std::uint64_t free = seats > taken ? seats - taken : 0;
    int scaled = ScaledEnemyCount(count, players);
    if (static_cast<std::uint64_t>(scaled) > free) scaled = free > static_cast<std::uint64_t>(count) ? static_cast<int>(free) : count;
    if (DetailLog() && spawnLines.fetch_add(1) < kSpawnLogLimit)
        Log("SPAWN EDF6_TimeShip_CreatePodCrue: x%d -> x%d (%d players, %llu of %llu seats free)", count, scaled, players,
            static_cast<unsigned long long>(free), static_cast<unsigned long long>(seats));
    if (scaled != count) context->r12 = static_cast<std::uint32_t>(scaled);
}

// Natives whose count is a stack argument, read by `mov eax, [rbp+offset]` right after this handler.
void StackCount(CpuContext* context, std::uint32_t offset, const char* site) {
    const std::int32_t count = Int32At(context->rbp + offset);
    const int scaled = CountForGenerator(site, count);
    if (scaled != count) SetInt32At(context->rbp + offset, scaled);
}

void InstantAreaHandler(CpuContext* context) { StackCount(context, 0xE0, "InstantGenerateEnemy_Area"); }
void InstantAreaExHandler(CpuContext* context) { StackCount(context, 0x110, "InstantGenerateEnemy_AreaEX"); }

}  // namespace

int ScaledEnemyCount(int count, int players) {
    if (count <= 0 || players <= kVanillaPlayers) return count;
    const long long p = players < kMaxPlayers ? players : kMaxPlayers;
    // count * (p + 1) / 5, rounded half up, in integers (no float drift at x.5).
    const long long scaled = (2LL * count * (p + 1) + 5) / 10;
    return scaled > kLargestScaledCount ? (count > kLargestScaledCount ? count : kLargestScaledCount) : static_cast<int>(scaled);
}

bool IsMultipliedEnemyClass(const char* className) {
    if (!className) return false;
    for (const char* name : kMultipliedClasses)
        if (std::strcmp(name, className) == 0) return true;
    return false;
}

MidHandler SpawnHookHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x1D8E5F: return &PointGroupHandler;
        case 0x1D7A4B: return &AreaGroupHandler;
        case 0x1AF968: return &FlyingRouteHandler;
        case 0x1C2F97: return &EnemyGeneratorHandler;
        case 0x1C3235: return &InstantGeneratorHandler;
        case 0x1CAF52: return &PodCrewHandler;
        case 0x1B5434: return &InstantAreaHandler;
        case 0x1B59C0: return &InstantAreaExHandler;
        default: return nullptr;
    }
}

}  // namespace multislot
