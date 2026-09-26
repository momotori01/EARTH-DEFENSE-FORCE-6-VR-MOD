// Mission handlers against a fake game image: a zero-filled region the size of EDF.dll with a
// GameStatus pointer at 20B2890 and two tiny stand-in functions (MissionContext constructor and the
// per-player-count scale lookup). The handlers see CpuContext values like the thunks would give them.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/mission.h"
#include "../src/patches.h"

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
const std::uint8_t* Pointer(std::uint64_t value) { return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(value)); }

struct FakeControl {
    void** vtable;
    long uses;
    long weaks;
};
int deleted = 0;
void __fastcall FakeDestroy(FakeControl*) {}
void __fastcall FakeDelete(FakeControl*) { ++deleted; }
void* fakeVtable[2] = {reinterpret_cast<void*>(&FakeDestroy), reinterpret_cast<void*>(&FakeDelete)};

// EventFactor_ObjectAreaIn::Initialize stand-in: records what the area factor handler passes.
struct AreaCall {
    void* factor = nullptr;
    void* area = nullptr;
    std::uint32_t count = 0;
    std::int32_t flag = 0;
    std::int32_t* mode = nullptr;
    std::uint64_t entries[16]{};
    long weaks[8]{};
    int calls = 0;
} areaCall;

bool __fastcall FakeAreaInitialize(void* factor, void* area, std::uint64_t* players, std::uint32_t count, std::int32_t flag,
                                   std::int32_t* mode) {
    areaCall.factor = factor;
    areaCall.area = area;
    areaCall.count = count;
    areaCall.flag = flag;
    areaCall.mode = mode;
    std::memcpy(areaCall.entries, players, 16 * 8);
    for (int i = 0; i < 8; ++i) {
        const auto* control = reinterpret_cast<FakeControl*>(static_cast<std::uintptr_t>(players[i * 2 + 1]));
        areaCall.weaks[i] = control ? control->weaks : -1;
    }
    ++areaCall.calls;
    return true;
}

int scaleOverflow = 0;
float __fastcall FakeScale(void*, int, int players) {
    static const float table[] = {1.2f, 0.8f, 1.0f, 1.2f};
    if (players >= 1 && players <= 4) return table[players - 1];
    ++scaleOverflow;
    return 100.0f;
}

}  // namespace

int main() {
    const std::size_t imageSize = kGameStatusPointer + 0x1000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(image != nullptr, "fake image");
    if (!image) return 1;
    std::vector<std::uint8_t> status(0x15100, 0);
    const std::uint64_t statusAddress = Address(status.data());
    std::memcpy(image + kGameStatusPointer, &statusAddress, sizeof(statusAddress));
    const unsigned char constructor[] = {0x48, 0x89, 0xC8, 0xC3};             // mov rax, rcx; ret
    const unsigned char scale[] = {0xF3, 0x41, 0x0F, 0x2A, 0xC0, 0xC3};         // cvtsi2ss xmm0, r8d; ret
    std::memcpy(image + kMissionContextConstructor, constructor, sizeof(constructor));
    std::memcpy(image + kPlayerCountScale, scale, sizeof(scale));
    InitMission(image);

    // Every table entry has a handler.
    for (const auto& hook : MissionHooks()) Check(MissionHookHandler(hook.rva) != nullptr, hook.name);
    for (const auto& call : MissionCalls()) Check(MissionCallHandler(call.rva) != nullptr, call.name);

    // Record offsets: players 1-4 unchanged, 5 and up in distinct sidecars outside GameStatus, others scratch.
    const std::uint64_t records = statusAddress + kLoadoutRecords;
    bool vanilla = true;
    for (int i = 0; i < kVanillaPlayers; ++i)
        vanilla = vanilla && LoadoutRecordOffset(i) == static_cast<std::uint64_t>(i) * kLoadoutRecordSize;
    Check(vanilla, "records 1-4 keep index*0xD4");
    bool sidecars = true;
    for (int i = kVanillaPlayers; i < kMaxPlayers; ++i) {
        const std::uint64_t record = records + LoadoutRecordOffset(i);
        sidecars = sidecars && Pointer(record) == LoadoutSidecar(i) &&
                   (record + kLoadoutRecordSize <= statusAddress || record >= statusAddress + status.size());
    }
    Check(sidecars, "the records above four land in their own sidecars, outside GameStatus");
    Check(LoadoutSidecar(5) - LoadoutSidecar(4) >= static_cast<std::ptrdiff_t>(kLoadoutRecordSize), "sidecars do not overlap");
    const std::uint64_t scratch = records + LoadoutRecordOffset(kMaxPlayers);
    Check(records + LoadoutRecordOffset(-1) == scratch && records + LoadoutRecordOffset(1000) == scratch &&
              Pointer(scratch) != LoadoutSidecar(4),
          "invalid indices share a scratch record");

    // 595A03 `imul rbx, rdi, 0xD4`, then [rbx+rdx+0xF8] with rdx = GameStatus+0x14B80 and [rbx+rcx+0x14C7C] with rcx = GameStatus.
    CpuContext context{};
    context.rdi = static_cast<std::uint64_t>(std::int64_t{6});
    MissionHookHandler(0x595A03)(&context);
    Check(Pointer(context.rbx + statusAddress + 0x14B80 + 0xF8) == LoadoutSidecar(6), "CreateOnlinePlayerObject reads player 7's sidecar");
    Check(Pointer(context.rbx + statusAddress + 0x14C7C) == LoadoutSidecar(6) + 4, "absolute-displacement read hits the same sidecar");
    context.rdi = 2;
    MissionHookHandler(0x595A03)(&context);
    Check(context.rbx == 2 * kLoadoutRecordSize, "player 3 unchanged");

    // MissionSync_Update writes a whole record for index 4..7 through its three address forms; the
    // fields after the four records (+0x14FC8.., counts at +0x14FF4/+0x14FF8) must stay untouched.
    for (int index = 4; index < 8; ++index) {
        CpuContext parse{};
        parse.rcx = static_cast<std::uint64_t>(std::int64_t{index});
        parse.r9 = parse.rcx;
        MissionHookHandler(0x790887)(&parse);  // r8 = offset
        MissionHookHandler(0x790927)(&parse);  // rax = offset
        const std::uint64_t base = statusAddress + 0x14B80;
        std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(statusAddress + 0x14C78 + parse.r8)), 0x40 + index, 0xA4);
        std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(parse.r8 + base + 0xF8)), 0x40 + index, 4);
        std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(parse.rax + base + 0x100)), 0x40 + index, 0x90);
        std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(parse.r8 + base + 0x19C)), 0x40 + index, 0x30);
    }
    bool statusClean = true;
    for (std::size_t i = 0; i < status.size(); ++i) statusClean = statusClean && status[i] == 0;
    Check(statusClean, "records 5-8 never touch GameStatus");
    Check(LoadoutSidecar(7)[0] == 0x47 && LoadoutSidecar(7)[0xD3] == 0x47 && LoadoutSidecar(4)[0x50] == 0x44,
          "each sidecar holds its own record");

    // 5A4366 `imul rax, rax, 0xD4; add rax, [GameStatus]; mov rcx, [rax+rbx*8+0x14D1C]`.
    CpuContext object{};
    object.rax = 5;
    MissionHookHandler(0x5A4366)(&object);
    Check(Pointer(object.rax + statusAddress + 0x14D1C) == LoadoutSidecar(5) + 0xA4, "player object weapon read");
    // 0DFD27 `imul rbx, rax, 0xD4; add rbx, rcx` (rcx = GameStatus+0x14B80), then [rbx+0xFC].
    CpuContext color{};
    color.rax = 7;
    MissionHookHandler(0x0DFD27)(&color);
    Check(Pointer(color.rbx + statusAddress + 0x14B80 + 0xFC) == LoadoutSidecar(7) + 4, "color read");
    // 591914 `imul rax, r15, 0xD4`; 59DCFA `imul rsi, rax`; 7FFDA3 `imul rcx, rcx`.
    CpuContext other{};
    other.rax = 5;
    MissionHookHandler(0x59DCFA)(&other);  // rsi from rax, before rax is replaced below
    other.r15 = 4;
    MissionHookHandler(0x591914)(&other);
    Check(Pointer(other.rax + statusAddress + 0x14B80 + 0xFC) == LoadoutSidecar(4) + 4 &&
              Pointer(other.rsi + statusAddress + 0x14C78) == LoadoutSidecar(5),
          "color index and record lookup reads");
    other.rcx = 3;
    MissionHookHandler(0x7FFDA3)(&other);
    Check(other.rcx == 3 * kLoadoutRecordSize, "soldier type UI for player 4 unchanged");

    // CreatePlayers spawn table count: min(r12d, 4) into rsi.
    CpuContext spawn{};
    spawn.r12 = 7;
    spawn.rsi = 0xDEAD;
    MissionHookHandler(0x1D968E)(&spawn);
    Check(spawn.rsi == 4, "spawn table loop stops at four entries");
    spawn.r12 = 0xFFFFFFFF00000003ull;
    MissionHookHandler(0x1D968E)(&spawn);
    Check(spawn.rsi == 3, "fewer players keep their count (zero-extended like mov esi, r12d)");

    // CreatePlayers spawn offset from the table at rbp+0x40.
    std::uint8_t frame[0x100]{};
    const float table[16] = {0, 0, 0, 1, -0.5f, 0, -0.5f, 1, 0.5f, 0, -0.5f, 1, 0, 0, -1, 1};
    std::memcpy(frame + 0x40, table, sizeof(table));
    CpuContext offset{};
    offset.rbp = Address(frame);
    float got[4];
    offset.r15 = 2;
    MissionHookHandler(0x1D9A98)(&offset);
    std::memcpy(got, offset.xmm[0], sizeof(got));
    Check(got[0] == 0.5f && got[1] == 0 && got[2] == -0.5f && got[3] == 1, "player 3 takes table entry 3");
    offset.r15 = 4;
    MissionHookHandler(0x1D9A98)(&offset);
    std::memcpy(got, offset.xmm[0], sizeof(got));
    Check(got[0] == -1.0f && got[2] == -1.0f && got[3] == 1, "player 5 stands twice as far as player 2");
    offset.r15 = 7;
    MissionHookHandler(0x1D9A98)(&offset);
    std::memcpy(got, offset.xmm[0], sizeof(got));
    Check(got[0] == -1.5f && got[2] == -1.5f, "player 8 stands three times as far as player 2");

    // CreatePlayers clear: the moved entries reset like weak_ptr = {}; rbx -> array.
    constexpr std::size_t kMovedBytes = kMaxPlayers * kPlayerEntrySize;
    constexpr std::size_t kContextBytes = kMovedPlayerArray + kMovedBytes;
    std::vector<std::uint64_t> missionContext(kContextBytes / 8, 0);
    FakeControl last{fakeVtable, 0, 1}, shared{fakeVtable, 1, 2};
    const auto entry = [&](int i) { return &missionContext[(kMovedPlayerArray + i * kPlayerEntrySize) / 8]; };
    entry(4)[0] = 0x1234;
    entry(4)[1] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&last));
    entry(kMaxPlayers - 1)[0] = 0x5678;
    entry(kMaxPlayers - 1)[1] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&shared));
    entry(0)[0] = 0x9999;  // entry 1 belongs to the game's own loop
    CpuContext clear{};
    const auto self = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(missionContext.data()));
    clear.rbx = self;
    MissionHookHandler(0x1D99F2)(&clear);
    Check(clear.rbx == self + kMovedPlayerArray, "clear loop starts at the moved array");
    Check(entry(4)[0] == 0 && entry(4)[1] == 0 && entry(kMaxPlayers - 1)[0] == 0 && entry(kMaxPlayers - 1)[1] == 0,
          "the moved entries are emptied");
    Check(deleted == 1 && last.weaks == 0 && shared.weaks == 1, "last weak reference deletes its control block, others only decrement");
    Check(entry(0)[0] == 0x9999, "entry 1 is left to the game");

    // Constructor redirect: calls the game's constructor, then empties the moved array.
    std::vector<std::uint8_t> object0(kContextBytes + 0x10, 0xAA);
    const auto construct = reinterpret_cast<void*(__fastcall*)(void*)>(MissionCallHandler(0x1D6CD5));
    Check(construct(object0.data()) == object0.data(), "constructor result is passed through");
    bool movedZero = true;
    for (std::size_t i = kMovedPlayerArray; i < kContextBytes; ++i) movedZero = movedZero && object0[i] == 0;
    Check(movedZero && object0[kMovedPlayerArray - 1] == 0xAA && object0[kContextBytes] == 0xAA,
          "exactly the moved entries are zeroed");

    // Per-player-count factor lookups. The stand-in is an online table (1.2, 0.8, 1.0, 1.2 for 1-4 players)
    // that records any count above four, which the real lookup would read past its array.
    const unsigned char jumpToScale[] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};  // mov rax, imm64; jmp rax
    std::memcpy(image + kPlayerCountScale, jumpToScale, sizeof(jumpToScale));
    const auto fakeScale = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&FakeScale));
    std::memcpy(image + kPlayerCountScale + 2, &fakeScale, 8);
    const auto durability = reinterpret_cast<float(__fastcall*)(void*, int, int)>(MissionCallHandler(0x0D7770));
    const auto damage = reinterpret_cast<float(__fastcall*)(void*, int, int)>(MissionCallHandler(0x54F0DF));
    Check(MissionCallHandler(0x54F1B1) == static_cast<void*>(damage) && durability != damage, "one durability and one damage hook");
    Check(durability(nullptr, 2, 1) == 1.2f && durability(nullptr, 2, 2) == 0.8f && durability(nullptr, 2, 4) == 1.2f &&
              damage(nullptr, 2, 3) == 1.0f && damage(nullptr, 2, 4) == 1.2f,
          "1-4 players read the game's factors");
    Check(durability(nullptr, 2, 5) == 1.2f && durability(nullptr, 2, 6) == 1.2f && durability(nullptr, 2, 8) == 1.2f &&
              durability(nullptr, 2, 12) == 1.2f,
          "5 or more players: 4-player durability, nothing added");
    Check(damage(nullptr, 2, 5) == 1.2f && damage(nullptr, 2, 8) == 1.2f, "5 or more players: 4-player damage");
    Check(scaleOverflow == 0, "the lookup never sees more than four players");

    // Loops over the moved array end after every entry.
    CpuContext loop{};
    loop.rdi = 0x1000;
    loop.r13 = 0x2000;
    loop.r12 = 0x3000;
    MissionHookHandler(0x1A19F3)(&loop);
    MissionHookHandler(0x1A3ACC)(&loop);
    const std::uint64_t span = static_cast<std::uint64_t>(kMaxPlayers) * kPlayerEntrySize;
    Check(loop.rsi == 0x1000 + span && loop.r13 == 0x3000 + span, "loop end = begin + every entry");
    MissionHookHandler(0x1A352B)(&loop);
    MissionHookHandler(0x1BAC0A)(&loop);
    MissionHookHandler(0x1DBA07)(&loop);
    Check(loop.rsi == 0x3000 + 2 * span && loop.r14 == 0x1000 + span && loop.rbp == 0x1000 + span,
          "loop end registers per site");
    loop.r13 = 0x5000;
    MissionHookHandler(0x1C243F)(&loop);
    Check(loop.rdi == 0x5000 + span, "SetAiRouteNavigate loop end");

    // Factor_PlayerAreaIn: the handler calls Initialize itself with every player.
    const unsigned char jumpToFake[] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};  // mov rax, imm64; jmp rax
    std::memcpy(image + kAreaFactorInitialize, jumpToFake, sizeof(jumpToFake));
    const auto fakeInitialize = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&FakeAreaInitialize));
    std::memcpy(image + kAreaFactorInitialize + 2, &fakeInitialize, 8);
    std::vector<std::uint64_t> context2(kContextBytes / 8, 0);
    FakeControl p2{fakeVtable, 1, 3}, p5{fakeVtable, 1, 1}, p8{fakeVtable, 1, 1};
    const auto moved = [&](int i) { return &context2[(kMovedPlayerArray + i * kPlayerEntrySize) / 8]; };
    moved(4)[0] = 0x5555;
    moved(4)[1] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&p5));
    moved(7)[0] = 0x8888;
    moved(7)[1] = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&p8));
    std::uint64_t gameCopy[8] = {0x1111, 0, 0x2222, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&p2)), 0, 0, 0, 0};
    std::vector<std::uint8_t> nativeFactor(0x40, 0), areaStack(sizeof(CpuContext) + 0x100, 0);
    const std::uint64_t contextAddress = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(context2.data()));
    std::memcpy(nativeFactor.data() + 0x20, &contextAddress, 8);
    auto* area = reinterpret_cast<CpuContext*>(areaStack.data());
    const auto* areaRsp = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(SiteRsp(area)));
    std::int32_t meeting = 1;
    const std::uint64_t modeAddress = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&meeting));
    const std::int32_t zero = 0;
    std::memcpy(const_cast<std::uint8_t*>(areaRsp) + 0x20, &zero, 4);
    std::memcpy(const_cast<std::uint8_t*>(areaRsp) + 0x28, &modeAddress, 8);
    area->rbp = Address(nativeFactor.data());
    area->rcx = 0xF1;
    area->rdx = 0xA2;
    area->r8 = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(gameCopy));
    area->rax = 0xFFFFFFFFFFFFFF00ull;
    MissionHookHandler(0x1A3451)(area);
    Check(areaCall.calls == 1 && areaCall.count == kMaxPlayers && areaCall.flag == 0 && areaCall.mode == &meeting &&
              areaCall.factor == reinterpret_cast<void*>(0xF1) && areaCall.area == reinterpret_cast<void*>(0xA2),
          "Initialize gets the game's arguments with every player");
    Check(areaCall.entries[2] == 0x2222 && areaCall.entries[8] == 0x5555 && areaCall.entries[14] == 0x8888 && areaCall.entries[10] == 0,
          "players 1-4 from the game's copy, 5-8 from the moved array");
    Check(areaCall.weaks[1] == 3 && areaCall.weaks[4] == 2 && areaCall.weaks[7] == 2 && areaCall.weaks[5] == -1,
          "players 5-8 hold an extra weak reference during the call only");
    Check(p5.weaks == 1 && p8.weaks == 1 && p2.weaks == 3 && deleted == 1, "references are returned afterwards");
    Check(area->rax == 0xFFFFFFFFFFFFFF01ull, "Initialize's bool result lands in al");

    // Item counts: positions 0-3 into GameStatus+0x14FD4, the rest into sidecars, others dropped; totals include them.
    const auto store = reinterpret_cast<void(__fastcall*)(void*, const std::int32_t*, const std::uint64_t*)>(MissionSlotHandler(0x17EEF70));
    Check(store && MissionSlotHandler(0x1791DF0) == static_cast<void*>(store) && MissionSlotHandler(0x179DE90) == static_cast<void*>(store),
          "one item store for all three callbacks");
    const auto itemAt = [&](int position) {
        std::uint64_t value = 0;
        std::memcpy(&value, status.data() + kResultItems + position * 8, 8);
        return value;
    };
    for (std::int32_t position = 0; position <= kMaxPlayers; ++position) {
        const std::uint64_t item = (static_cast<std::uint64_t>(position + 10) << 32) | static_cast<std::uint64_t>(position + 1);
        store(nullptr, &position, &item);
    }
    Check(itemAt(0) == ((10ull << 32) | 1) && itemAt(3) == ((13ull << 32) | 4), "positions 1-4 go where the game keeps them");
    std::uint32_t lowSum = 0, highSum = 0;
    for (int i = kVanillaPlayers; i < kMaxPlayers; ++i) {
        lowSum += static_cast<std::uint32_t>(i) + 1;
        highSum += static_cast<std::uint32_t>(i) + 10;
    }
    Check(SidecarItem(4) == ((14ull << 32) | 5) &&
              SidecarItem(kMaxPlayers - 1) == ((static_cast<std::uint64_t>(kMaxPlayers + 9) << 32) | kMaxPlayers),
          "the positions above four go to sidecars");
    std::uint32_t localCount = 0;
    std::memcpy(&localCount, status.data() + 0x14FF4, 4);
    Check(localCount == 0, "a position past the last player never reaches the counts after the item table");
    std::vector<std::uint8_t> result(0x1000, 0);
    const std::uint32_t firstSoFar = 100, secondSoFar = 200;
    std::memcpy(result.data() + 0xE08, &firstSoFar, 4);
    std::memcpy(result.data() + 0xE0C, &secondSoFar, 4);
    CpuContext totals{};
    totals.r11 = Address(result.data());
    totals.rcx = 200;
    MissionHookHandler(0x2C865F)(&totals);
    std::uint32_t first = 0;
    std::memcpy(&first, result.data() + 0xE08, 4);
    Check(first == 100 + lowSum && totals.rcx == 200 + highSum, "totals add the players above four");
    CpuContext recount{};
    recount.rsi = Address(result.data());
    recount.rdx = 1000;
    recount.rcx = 2000;
    MissionHookHandler(0x2C9402)(&recount);
    std::uint32_t second = 0;
    std::memcpy(&first, result.data() + 0xE08, 4);
    std::memcpy(&second, result.data() + 0xE0C, 4);
    Check(recount.rdx == 1000 + lowSum && recount.rcx == 2000 + highSum && first == 100 + 2 * lowSum &&
              second == 200 + highSum,
          "recount adds the players above four");
    MissionHookHandler(0x78E693)(nullptr);
    Check(SidecarItem(4) == 0 && SidecarItem(kMaxPlayers - 1) == 0, "ResultSync_Begin clears the sidecar items");

    // Ghost harness. A fake online mode for OnlineMode(): GameStatus+0x38 = 0, +0x20 -> {entry},
    // entry+0x10 -> info, info+8 = 0, info+0x68 = 1.
    const unsigned char create[] = {0x48, 0x89, 0xC8, 0xC3};  // CreateOnlinePlayer stand-in returns its index
    std::memcpy(image + kCreateOnlinePlayer, create, sizeof(create));
    std::vector<std::uint8_t> info(0x100, 0), entryObject(0x40, 0);
    std::uint64_t modes[1] = {Address(entryObject.data())};
    const std::uint64_t infoAddress = Address(info.data()), modesAddress = Address(reinterpret_cast<std::uint8_t*>(modes));
    std::memcpy(entryObject.data() + 0x10, &infoAddress, 8);
    info[0x68] = 1;
    std::memcpy(status.data() + 0x20, &modesAddress, 8);
    InitMission(image, 3);
    Check(MissionHookHandler(0x595A03) != nullptr && GhostHookHandler(0x790BA6) && GhostHookHandler(0x78D765) && GhostCallHandler(0x1DC525),
          "ghost handlers exist");
    for (const auto& hook : GhostHooks()) Check(GhostHookHandler(hook.rva) != nullptr, hook.name);
    for (const auto& call : GhostCalls()) Check(GhostCallHandler(call.rva) != nullptr, call.name);
    CpuContext count{};
    count.rax = 2;
    GhostHookHandler(0x790BA6)(&count);
    Check(count.rax == 2 && ActiveGhosts() == 0, "a real second player turns ghosts off");
    count.r12 = 1;
    GhostHookHandler(0x78D765)(&count);
    Check(count.r12 == 4 && ActiveGhosts() == 3, "alone: count 1 becomes 1 + GhostPlayers");
    const auto createFor = reinterpret_cast<void*(__fastcall*)(std::uint32_t, void*, std::uint32_t)>(GhostCallHandler(0x1DC525));
    Check(createFor(0, nullptr, 0) == nullptr && reinterpret_cast<std::uintptr_t>(createFor(2, nullptr, 0)) == 0 &&
              reinterpret_cast<std::uintptr_t>(createFor(4, nullptr, 0)) == 4,
          "ghosts 2-4 are created from player 1, other indices unchanged");
    // CreatePlayers' stack: count at rsp+0x24, remote flags at rsp+0x28, rsp just above the context.
    std::vector<std::uint8_t> frameStack(sizeof(CpuContext) + 0x100, 0);
    auto* stacked = reinterpret_cast<CpuContext*>(frameStack.data());
    stacked->rbx = self;
    const auto* rsp = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(SiteRsp(stacked)));
    const std::uint32_t four = 4;
    std::memcpy(const_cast<std::uint8_t*>(rsp) + 0x24, &four, 4);
    // The user loop writes one flag per member into the plugin's buffer, the create loop picks it up.
    CpuContext flagStore{};
    flagStore.r15 = 0;
    flagStore.rax = 0;  // player 1 is this machine's
    MissionHookHandler(0x1D98B6)(&flagStore);
    MissionHookHandler(0x1D99F2)(stacked);
    CpuContext flagArray{};
    MissionHookHandler(0x1D9A3B)(&flagArray);
    const auto* flags = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(flagArray.r12));
    Check(flags[0] == 0 && flags[1] == 1 && flags[2] == 1 && flags[3] == 1,
          "ghosts are flagged remote, player 1 stays local");
    Check(flags != rsp + 0x28 && flags[kMaxPlayers - 1] == 0,
          "the flags left the stack and a player the user loop skipped stays this machine's, as the game left it");
    flagStore.r15 = 0;
    flagStore.rax = 1;
    MissionHookHandler(0x1D98B6)(&flagStore);
    flagStore.r15 = kMaxPlayers - 1;
    flagStore.rax = 0;
    MissionHookHandler(0x1D98B6)(&flagStore);
    flagStore.r15 = kMaxPlayers + 5;  // past the last player: dropped, not written past the buffer
    flagStore.rax = 1;
    MissionHookHandler(0x1D98B6)(&flagStore);
    MissionHookHandler(0x1D9A3B)(&flagArray);
    Check(flags[0] == 1 && flags[kMaxPlayers - 1] == 0, "every user from the first to the last gets his own flag");
    // Offline the room has no users at all, so the loop that writes the flags never runs and the create loop
    // has to see the zeroes the game's own `mov dword [rsp+0x28], 0` left: a set flag becomes a local player
    // slot of -1, which indexes the armor table out of range (595CD9, the first offline sortie crashed).
    MissionHookHandler(0x1D9A3B)(&flagArray);  // handed over again with nothing written in between
    bool offline = true;
    for (int i = 0; i < kMaxPlayers; ++i) offline = offline && flags[i] == 0;
    Check(offline, "a mission with no users at all leaves every player to this machine");
    info[0x68] = 0;
    std::memset(const_cast<std::uint8_t*>(rsp) + 0x28, 0, 8);
    stacked->rbx = self;
    MissionHookHandler(0x1D99F2)(stacked);
    Check(rsp[0x29] == 0, "offline missions never get ghost flags");
    count.rax = 1;
    InitMission(image, 0);
    GhostHookHandler(0x790BA6)(&count);
    Check(count.rax == 1 && ActiveGhosts() == 0, "GhostPlayers=0 leaves the count alone");

    VirtualFree(image, 0, MEM_RELEASE);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("mission slots verified\n");
    return 0;
}
