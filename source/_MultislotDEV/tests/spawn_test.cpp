// Enemy spawn counts against a fake game: a GameStatus with the online-mode chain OnlineSession() follows
// and the mission player count, fake scene objects that carry MSVC RTTI, and CpuContext values laid out
// the way the thunks hand them to the handlers at each site.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/mission.h"
#include "../src/patches.h"
#include "../src/spawn.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::uint64_t Address(const std::uint8_t* p) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p)); }

void Put32(std::uint8_t* at, std::int32_t value) { std::memcpy(at, &value, sizeof(value)); }
void Put64(std::uint8_t* at, std::uint64_t value) { std::memcpy(at, &value, sizeof(value)); }
std::int32_t Get32(const std::uint8_t* at) {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// One polymorphic object as MSVC lays it out: type descriptor at +0x100 (name at +0x110), complete object
// locator at +0x200 (RVAs relative to the block, which plays the image base), vtable at +0x308 with the
// locator in the slot before it, the object at +0x400 pointing at the vtable.
struct FakeObject {
    std::uint8_t block[0x500]{};
    std::uint64_t Make(const char* decorated, std::uint32_t signature = 1) {
        const std::uint64_t base = Address(block);
        std::memcpy(block + 0x110, decorated, std::strlen(decorated) + 1);
        const std::uint32_t locator[6] = {signature, 0, 0, 0x100, 0, 0x200};
        std::memcpy(block + 0x200, locator, sizeof(locator));
        Put64(block + 0x300, base + 0x200);
        Put64(block + 0x400, base + 0x308);
        return base + 0x400;
    }
};

FakeObject ant, hill, anchor, nested, badLocator;

struct Frame {
    std::vector<std::uint8_t> stack = std::vector<std::uint8_t>(sizeof(CpuContext) + 0x200, 0);
    CpuContext* context() { return reinterpret_cast<CpuContext*>(stack.data()); }
    std::uint8_t* rsp() { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(SiteRsp(context()))); }
};

}  // namespace

int main() {
    // Rounding: count * (1 + 0.2 per player above four), half up; 1-4 players and non-positive counts unchanged.
    struct Row {
        int count, players, expected;
    };
    const Row rows[] = {
        {1, 4, 1},   {1, 5, 1},   {1, 6, 1},     {1, 7, 2},   {1, 8, 2},     {2, 5, 2},    {3, 5, 4},
        {5, 5, 6},   {10, 5, 12}, {10, 6, 14},   {10, 7, 16}, {10, 8, 18},   {7, 6, 10},   {4, 6, 6},
        {12, 6, 17}, {2, 7, 3},   {2, 8, 4},     {5, 8, 9},   {150, 8, 270},               {20, 1, 20},
        {20, 0, 20}, {0, 8, 0},   {-1, 8, -1},   {9000, 8, 10000},           {20000, 8, 20000},
    };
    for (const auto& row : rows) {
        const int got = ScaledEnemyCount(row.count, row.players);
        if (got != row.expected) {
            ++failures;
            std::printf("FAIL: ScaledEnemyCount(%d, %d) = %d, expected %d\n", row.count, row.players, got, row.expected);
        }
    }
    // Every count the mod supports, against the factors the README and the INI promise (tenths, half up).
    const int factorTenths[] = {10, 10, 10, 10, 10, 12, 14, 16, 18, 20, 22, 24, 26};
    bool factors = kMaxPlayers < static_cast<int>(sizeof(factorTenths) / sizeof(*factorTenths));
    for (int players = 1; players <= kMaxPlayers; ++players)
        for (const int count : {1, 2, 3, 5, 10, 150}) {
            const int expected = (count * factorTenths[players] + 5) / 10;
            factors = factors && ScaledEnemyCount(count, players) == expected;
        }
    Check(factors, "every supported player count follows the documented factor");
    Check(ScaledEnemyCount(3, kMaxPlayers + 2) == ScaledEnemyCount(3, kMaxPlayers) &&
              ScaledEnemyCount(10, kMaxPlayers + 50) == ScaledEnemyCount(10, kMaxPlayers),
          "more players than the mod supports scale as a full room");
    Check(IsMultipliedEnemyClass("GiantAnt") && IsMultipliedEnemyClass("EDF6_Berserker_Large") && IsMultipliedEnemyClass("Deiroi"),
          "mobile enemies are multiplied");
    for (const char* fixed : {"AntHill", "BigNest", "GeneratorPoll", "EDF6_TimeShip_Anchor", "EDF6_TimeShip", "EDF6_TimeShip_Capsule",
                              "UfoCarrier508", "DropBoat512", "EDF6_SpiderEggSac", "SpiderNet", "GiantAntEgg", "AlienTrailer",
                              "UfoMother511", "MovingFortress", "EDF6_GrandMother", "EDF6_Jormungand", "Humanoid_BigGreyBoss",
                              "giantant", "GiantAnt2", ""})
        Check(!IsMultipliedEnemyClass(fixed), fixed);
    Check(!IsMultipliedEnemyClass(nullptr), "no class name");
    for (const auto& hook : SpawnHooks()) Check(SpawnHookHandler(hook.rva) != nullptr, hook.name);
    Check(SpawnHookHandler(0x123456) == nullptr, "unknown site has no handler");

    // Fake game: GameStatus pointer at 20B2890, online mode chain, player count at +0x14FF8.
    const std::size_t imageSize = kGameStatusPointer + 0x1000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "fake image");
    if (!image) return 1;
    std::vector<std::uint8_t> status(0x15100, 0), info(0x100, 0), entry(0x40, 0);
    Put64(image + kGameStatusPointer, Address(status.data()));
    std::uint64_t modes[1] = {Address(entry.data())};
    Put64(status.data() + 0x20, Address(reinterpret_cast<std::uint8_t*>(modes)));
    Put64(entry.data() + 0x10, Address(info.data()));
    const auto setOnline = [&](bool online) { info[0x68] = online ? 1 : 0; };
    const auto setPlayers = [&](std::int32_t players) { Put32(status.data() + 0x14FF8, players); };
    InitMission(image);
    setOnline(true);
    setPlayers(5);
    Check(OnlineSession() && MissionPlayers() == 5, "fake online session with five players");

    const std::uint64_t antObject = ant.Make(".?AVGiantAnt@@");
    const std::uint64_t hillObject = hill.Make(".?AVAntHill@@");
    const std::uint64_t anchorObject = anchor.Make(".?AVEDF6_TimeShip_Anchor@@");
    const std::uint64_t nestedObject = nested.Make(".?AVGiantAnt@game@@");
    const std::uint64_t badObject = badLocator.Make(".?AVGiantAnt@@", 0);

    // CreateObjectGroup (1D8E5F): r14 = params (+0x68 count, +0x70 team), rax = entry (+0x28 object),
    // [rsp+0x78] = objects created before this one.
    const auto pointGroup = [&](std::uint64_t object, std::int32_t team, std::int32_t count, std::uint64_t created = 0,
                                bool entry = true) {
        Frame frame;
        std::uint8_t params[0x100]{}, holder[0x40]{};
        Put32(params + 0x68, count);
        Put32(params + 0x70, team);
        Put64(holder + 0x28, object);
        Put64(frame.rsp() + 0x78, created);
        frame.context()->r14 = Address(params);
        frame.context()->rax = entry ? Address(holder) : 0;
        SpawnHookHandler(0x1D8E5F)(frame.context());
        return Get32(params + 0x68);
    };
    Check(pointGroup(antObject, 1, 10) == 12, "5 players: an ant group of 10 becomes 12");
    setPlayers(8);
    Check(pointGroup(antObject, 1, 10) == 18 && pointGroup(antObject, 1, 1) == 2, "8 players: x1.8 rounded");
    setPlayers(6);
    Check(pointGroup(antObject, 1, 1) == 1 && pointGroup(antObject, 1, 3) == 4, "6 players: 1 stays 1, 3 becomes 4");
    Check(pointGroup(hillObject, 1, 3) == 3 && pointGroup(anchorObject, 1, 2) == 2, "nests and anchors keep their count");
    Check(pointGroup(antObject, 0, 10) == 10 && pointGroup(antObject, 2, 10) == 10, "other teams keep their count");
    Check(pointGroup(antObject, 1, 10, 1) == 10, "only the first created object decides");
    Check(pointGroup(antObject, 1, 10, 0, false) == 10, "a failed creation decides nothing");
    Check(pointGroup(nestedObject, 1, 10) == 10 && pointGroup(badObject, 1, 10) == 10, "unexpected RTTI shapes are left alone");
    Check(pointGroup(0x10, 1, 10) == 10 && pointGroup(0, 1, 10) == 10, "unreadable objects are left alone");
    Check(pointGroup(antObject, 1, 0) == 0 && pointGroup(antObject, 1, -3) == -3, "empty and negative counts stay");
    setPlayers(4);
    Check(pointGroup(antObject, 1, 10) == 10, "4 players: unchanged");
    setPlayers(1);
    Check(pointGroup(antObject, 1, 10) == 10, "alone: unchanged");
    setPlayers(7);
    setOnline(false);
    Check(pointGroup(antObject, 1, 10) == 10, "offline missions are never changed (the count field may be stale)");
    setOnline(true);

    // CreateAreaObject (1D7A4B): r13 = params (+0x50 count, +0x58 team).
    {
        Frame frame;
        std::uint8_t params[0x100]{}, holder[0x40]{};
        Put32(params + 0x50, 20);
        Put32(params + 0x58, 1);
        Put64(holder + 0x28, antObject);
        frame.context()->r13 = Address(params);
        frame.context()->rax = Address(holder);
        setPlayers(6);
        SpawnHookHandler(0x1D7A4B)(frame.context());
        Check(Get32(params + 0x50) == 28, "area group of 20 with 6 players becomes 28");
        Put32(params + 0x50, 20);
        Put64(holder + 0x28, hillObject);
        SpawnHookHandler(0x1D7A4B)(frame.context());
        Check(Get32(params + 0x50) == 20, "area group of nests keeps its count");
    }

    // CreateFlyingEnemyGroup_OnRoute (1AF968): esi = count, edi = pass, [rsp+0x60] = object, team at rbp+0x98.
    {
        Frame frame;
        std::uint8_t locals[0x200]{};
        Put32(locals + 0x98, 1);
        Put64(frame.rsp() + 0x60, antObject);
        frame.context()->rbp = Address(locals);
        frame.context()->rsi = 20;
        frame.context()->rdi = 0;
        setPlayers(8);
        SpawnHookHandler(0x1AF968)(frame.context());
        Check(frame.context()->rsi == 36, "flying route group of 20 with 8 players becomes 36");
        frame.context()->rsi = 20;
        frame.context()->rdi = 1;
        SpawnHookHandler(0x1AF968)(frame.context());
        Check(frame.context()->rsi == 20, "later passes decide nothing");
        frame.context()->rdi = 0;
        Put32(locals + 0x98, 0);
        SpawnHookHandler(0x1AF968)(frame.context());
        Check(frame.context()->rsi == 20, "friendly flyers keep their count");
    }

    // Pod crews (1CAF52): count in r12d, pod at [rsp+0x40] with its seat count at +0x1098 and seats taken at +0x107C.
    {
        std::vector<std::uint8_t> pod(0x1100, 0);
        const auto crew = [&](std::uint32_t count, std::uint64_t seats, std::uint32_t taken) {
            Frame frame;
            Put64(frame.rsp() + 0x40, Address(pod.data()));
            Put64(pod.data() + 0x1098, seats);
            std::memcpy(pod.data() + 0x107C, &taken, 4);
            frame.context()->r12 = count;
            SpawnHookHandler(0x1CAF52)(frame.context());
            return frame.context()->r12;
        };
        setOnline(true);
        setPlayers(5);
        Check(crew(3, 3, 0) == 3 && crew(2, 3, 0) == 2, "5 players: a full capsule stays full, 2 stays 2");
        setPlayers(7);
        Check(crew(1, 3, 0) == 2 && crew(2, 3, 0) == 3, "7 players: 1 -> 2, 2 -> 3.2 -> 3");
        setPlayers(8);
        Check(crew(2, 3, 0) == 3 && crew(2, 3, 1) == 2 && crew(1, 3, 3) == 1 && crew(3, 5, 0) == 5,
              "crew only grows into free seats and never shrinks");
        setPlayers(4);
        Check(crew(2, 3, 0) == 2, "4 players: pod crew unchanged");
    }

    // Generators: count in r9d at entry.
    for (std::uint32_t rva : {0x1C2F97u, 0x1C3235u}) {
        CpuContext context{};
        setPlayers(6);
        context.r9 = 12;
        SpawnHookHandler(rva)(&context);
        Check(context.r9 == 17, "generator wave of 12 with 6 players becomes 17");
        context.r9 = 0xFFFFFFFFull;
        SpawnHookHandler(rva)(&context);
        Check(context.r9 == 0xFFFFFFFFull, "negative generator count is left to the game");
        context.r9 = 0xABCD000000000002ull;
        setPlayers(7);
        SpawnHookHandler(rva)(&context);
        Check(context.r9 == 3, "r9d is read as 32 bits and written zero-extended");
        setPlayers(4);
        context.r9 = 12;
        SpawnHookHandler(rva)(&context);
        Check(context.r9 == 12, "generator with 4 players unchanged");
    }

    // Instant area generators: count stack argument at rbp+0xE0 / rbp+0x110.
    {
        std::uint8_t locals[0x200]{};
        CpuContext context{};
        context.rbp = Address(locals);
        Put32(locals + 0xE0, 5);
        Put32(locals + 0x110, 10);
        setPlayers(8);
        SpawnHookHandler(0x1B5434)(&context);
        setPlayers(5);
        SpawnHookHandler(0x1B59C0)(&context);
        Check(Get32(locals + 0xE0) == 9 && Get32(locals + 0x110) == 12, "instant generators: 5 -> 9 (8 players), 10 -> 12 (5 players)");
        setOnline(false);
        Put32(locals + 0xE0, 5);
        setPlayers(8);
        SpawnHookHandler(0x1B5434)(&context);
        Check(Get32(locals + 0xE0) == 5, "offline instant generator unchanged");
    }

    VirtualFree(image, 0, MEM_RELEASE);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("enemy spawn counts verified\n");
    return 0;
}
