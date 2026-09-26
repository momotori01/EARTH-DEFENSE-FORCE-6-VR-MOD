#include "mission.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstring>

#include "log.h"
#include "patches.h"

namespace multislot {
namespace {

unsigned char* game = nullptr;
std::atomic<int> ghostPlayers{0};
bool ghostHarness = false;  // the ghost hooks are installed: the count may change between missions
std::atomic<int> activeGhosts{0};

constexpr int kExtraPlayers = kMaxPlayers - kVanillaPlayers;
std::uint8_t sidecars[kExtraPlayers][kLoadoutRecordSize];
std::uint8_t scratchRecord[kLoadoutRecordSize];
std::uint64_t sidecarItems[kExtraPlayers];
constexpr std::uint32_t kMissionPlayers = 0x14FF8;  // GameStatus: player count written by the mission sync

// CreatePlayers keeps one byte per player at rsp+0x28 saying whether that player is someone else's: the
// user loop (1D98B6) writes them and the create loop (1D9A3B..1D9B9F) walks them. The slot is eight bytes
// - the ninth would be the low byte of the user vector's begin pointer at rsp+0x30, which the user loop
// re-reads every pass - so from the ninth player on the array lives here instead.
std::uint8_t remoteFlags[kMaxPlayers];
std::size_t flagsWritten = 0;
std::atomic<int> localPlayer{-1};

std::uint8_t* At(std::uint64_t address) { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(address)); }

// One line per kind of event, so a mission with ten players does not flood the log.
// Bits 0-15 are one event each; a loadout record gets one per player index above that.
constexpr unsigned kLoadoutLogBit = 16;
static_assert(kLoadoutLogBit + kMaxPlayers <= 32, "one log bit per player index must fit");
std::atomic<unsigned> logged{0};
void LogOnce(unsigned bit, const char* text, long long value) {
    const unsigned mask = 1u << bit;
    if (logged.fetch_or(mask) & mask) return;
    Log(text, value);
}

// OnlineSession() without C++ objects so it can use SEH.
bool OnlineMode() {
    __try {
        std::uint64_t status = 0;
        std::memcpy(&status, game + kGameStatusPointer, sizeof(status));
        const auto* s = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(status));
        const std::int32_t mode = *reinterpret_cast<const std::int32_t*>(s + 0x38);
        if (mode == -1) return false;
        const auto* modes = *reinterpret_cast<const std::uint8_t* const*>(s + 0x20);
        const auto* entry = *reinterpret_cast<const std::uint8_t* const*>(modes + static_cast<std::uint32_t>(mode) * 8);
        const auto* info = *reinterpret_cast<const std::uint8_t* const*>(entry + 0x10);
        const std::int32_t offset = *reinterpret_cast<const std::int32_t*>(info + 8);
        return *reinterpret_cast<const std::int32_t*>(info + offset + 0x68) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <std::uint64_t CpuContext::*Target, std::uint64_t CpuContext::*Index>
void RecordOffsetHandler(CpuContext* context) {
    context->*Target = LoadoutRecordOffset(static_cast<std::int64_t>(context->*Index));
}

// CreatePlayers normalises a four-entry spawn offset table on its stack for every player; entry
// five would overwrite the security cookie. `mov esi, r12d` (loop count) is replaced by min(r12d, 4).
void SpawnTableCountHandler(CpuContext* context) {
    const auto players = static_cast<std::uint32_t>(context->r12);
    context->rsi = players < 4 ? players : 4;
    // Once per launch whatever the count: offline and four-player missions leave no other trace, so this is
    // the only line that says the mission phase ran at all.
    LogOnce(0, "MISSION CreatePlayers for %lld player(s)", players);
}

// Replaces `movups xmm0, [rdi+r13-0x108]`: the finished offset of player r15 from the table at
// rbp+0x40. Players 5-8 stand twice (8: three times) as far out along players 2, 3, 4's directions.
void SpawnOffsetHandler(CpuContext* context) {
    const auto index = static_cast<std::uint32_t>(context->r15);
    const auto* table = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(context->rbp + 0x40));
    float value[4];
    if (index < 4) {
        std::memcpy(value, table + index * 16, sizeof(value));
    } else {
        const std::uint32_t extra = index - 4;
        std::memcpy(value, table + (1 + extra % 3) * 16, sizeof(value));
        const float scale = 2.0f + static_cast<float>(extra / 3);
        value[0] *= scale;
        value[2] *= scale;
    }
    std::memcpy(context->xmm[0], value, sizeof(value));
}

struct ControlBlock;
struct ControlBlockVtable {
    void(__fastcall* destroy)(ControlBlock*);
    void(__fastcall* deleteThis)(ControlBlock*);
};
struct ControlBlock {
    ControlBlockVtable* vtable;
    long uses;
    long weaks;
};

void ResetWeak(std::uint64_t* entry) {
    entry[0] = 0;
    auto* control = reinterpret_cast<ControlBlock*>(static_cast<std::uintptr_t>(entry[1]));
    entry[1] = 0;
    if (control && InterlockedDecrement(&control->weaks) == 0) control->vtable->deleteThis(control);
}

// Replaces `mov [rsp+r15+0x28], al`: the flag of the user r15, ascending from 0.
void RemoteFlagHandler(CpuContext* context) {
    const auto index = static_cast<std::size_t>(context->r15);
    if (index >= static_cast<std::size_t>(kMaxPlayers)) return;
    if (!index) flagsWritten = 0;
    remoteFlags[index] = static_cast<std::uint8_t>(context->rax);
    flagsWritten = index + 1;
}

// Replaces `lea r12, [rsp+0x28]`, where the create loop picks the array up. Anything the user loop did not
// write stays 0, which is what the game's own `mov dword [rsp+0x28], 0` left there. It has to: the create
// loop turns a set flag into a local player slot of -1 (1D9A71), and -1 indexes the armor table out of
// range (595CD9). Offline there are no users at all, so every flag is one of these - defaulting them to
// "someone else's" crashed the first offline sortie (2026-09-19 10:29).
void RemoteFlagArrayHandler(CpuContext* context) {
    for (std::size_t i = flagsWritten; i < static_cast<std::size_t>(kMaxPlayers); ++i) remoteFlags[i] = 0;
    flagsWritten = 0;
    int mine = -1;
    for (int i = 0; i < kMaxPlayers && mine < 0; ++i)
        if (!remoteFlags[i]) mine = i;
    localPlayer.store(mine);
    context->r12 = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(remoteFlags));
}

// Replaces `add rbx, 0x100` before CreatePlayers clears the player array: the loop now clears
// entries 1-4 of the moved array, this resets entries 5-8 the same way (weak_ptr = {}).
void ClearPlayersHandler(CpuContext* context) {
    const std::uint64_t self = context->rbx;
    // This runs between the user loop and the create loop, so the flags are in `remoteFlags` already and
    // the player count is still at rsp+0x24. Ghosts have no user: flag them remote so they get no camera
    // and no controller.
    const int ghosts = activeGhosts.load();
    if (ghosts && OnlineSession()) {
        std::uint32_t count = 0;
        std::memcpy(&count, At(SiteRsp(context) + 0x24), sizeof(count));
        if (count == static_cast<std::uint32_t>(1 + ghosts)) {
            for (int i = 1; i <= ghosts && i < kMaxPlayers; ++i) remoteFlags[i] = 1;
            if (flagsWritten < static_cast<std::size_t>(ghosts) + 1) flagsWritten = static_cast<std::size_t>(ghosts) + 1;
            LogOnce(3, "MISSION ghost harness: creating %lld ghost players", ghosts);
        }
    }
    for (int i = kVanillaPlayers; i < kMaxPlayers; ++i)
        ResetWeak(reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(self + kMovedPlayerArray + i * kPlayerEntrySize)));
    context->rbx = self + kMovedPlayerArray;
}

// Replaces `lea end, [begin+0x40]` in loops over the moved player array: all eight entries.
template <std::uint64_t CpuContext::*End, std::uint64_t CpuContext::*Begin>
void LoopEndHandler(CpuContext* context) {
    context->*End = context->*Begin + kMaxPlayers * kPlayerEntrySize;
}

using AreaInitializeFn = bool(__fastcall*)(void*, void*, std::uint64_t*, std::uint32_t, std::int32_t, std::int32_t*);
// Replaces `call EventFactor_ObjectAreaIn::Initialize(factor, area, players, 4, flag, &mode)` in
// Factor_PlayerAreaIn/_Meeting. rbp is the script factor whose +0x20 is the MissionContext; r8 is the
// game's four-entry copy (it releases those references itself after the call). Initialize copies each
// weak_ptr with its own reference and keeps empty entries empty, as for players who are not there.
void AreaFactorCallHandler(CpuContext* context) {
    std::int32_t flag = 0;
    std::uint64_t mode = 0, mission = 0;
    std::memcpy(&flag, At(SiteRsp(context) + 0x20), sizeof(flag));
    std::memcpy(&mode, At(SiteRsp(context) + 0x28), sizeof(mode));
    std::memcpy(&mission, At(context->rbp + 0x20), sizeof(mission));
    std::uint64_t players[kMaxPlayers * 2]{};
    std::memcpy(players, At(context->r8), kVanillaPlayers * kPlayerEntrySize);
    for (int i = kVanillaPlayers; i < kMaxPlayers; ++i) {
        std::memcpy(&players[i * 2], At(mission + kMovedPlayerArray + i * kPlayerEntrySize), kPlayerEntrySize);
        auto* control = reinterpret_cast<ControlBlock*>(static_cast<std::uintptr_t>(players[i * 2 + 1]));
        if (control) InterlockedIncrement(&control->weaks);
    }
    const auto initialize = reinterpret_cast<AreaInitializeFn>(game + kAreaFactorInitialize);
    const bool result = initialize(At(context->rcx), At(context->rdx), players, kMaxPlayers, flag,
                                   reinterpret_cast<std::int32_t*>(static_cast<std::uintptr_t>(mode)));
    for (int i = kVanillaPlayers; i < kMaxPlayers; ++i) ResetWeak(&players[i * 2]);
    context->rax = (context->rax & ~0xFFull) | (result ? 1u : 0u);
}

// The online HUD keeps one entry per player in tables of four - the lamp colour and the class letter of
// HudPlayer_MultiPlayStatus (806FE0), the chat balloon of HudPlayer_Chat (802970), the radar marker of
// HUiHudRader - and 7FFBD0 hands out the player's index to look them up with. Players 5-8 read past the
// end: the status HUD found a null texture there and the mission crashed on its first frame.
// Replaces `movsxd rcx, [rsi+0x48]; mov [r14], ecx`: rcx keeps the real index for the loadout record the
// next instructions read (7FFDA3 -> sidecar), the tables get it wrapped, so player 5 shares player 1's
// colour. A player who is not in the mission keeps the negative index, which every reader of it checks for.
void PlayerTagIndexHandler(CpuContext* context) {
    std::int32_t index = 0;
    std::memcpy(&index, At(context->rsi + 0x48), sizeof(index));
    context->rcx = static_cast<std::uint64_t>(static_cast<std::int64_t>(index));
    const std::int32_t wrapped = index > 0 ? index % kVanillaPlayers : index;
    std::memcpy(At(context->r14), &wrapped, sizeof(wrapped));
    if (index >= kVanillaPlayers)
        LogOnce(14, "MISSION HUD colour of player index %lld uses one of the four the HUD has", index);
}

// ResultSync_Begin clears the game's four item counts; the sidecars are cleared with them.
void ClearItemsHandler(CpuContext*) { std::memset(sidecarItems, 0, sizeof(sidecarItems)); }

std::uint64_t SidecarItemSums() {
    std::uint32_t a = 0, b = 0;
    for (const std::uint64_t item : sidecarItems) {
        a += static_cast<std::uint32_t>(item);
        b += static_cast<std::uint32_t>(item >> 32);
    }
    return (static_cast<std::uint64_t>(b) << 32) | a;
}

// 2C85C0 before `mov [r11+0xE0C], ecx`: totals so far in [r11+0xE08] (first ints) and ecx (second ints).
void ItemTotalsHandler(CpuContext* context) {
    const std::uint64_t sums = SidecarItemSums();
    std::uint32_t first = 0;
    std::memcpy(&first, At(context->r11 + 0xE08), sizeof(first));
    first += static_cast<std::uint32_t>(sums);
    std::memcpy(At(context->r11 + 0xE08), &first, sizeof(first));
    context->rcx = static_cast<std::uint32_t>(static_cast<std::uint32_t>(context->rcx) + static_cast<std::uint32_t>(sums >> 32));
}

// 2C92E0 before `add edx, [rsi+0xE10]`: edx/ecx and [rsi+0xE08]/[rsi+0xE0C] hold the totals.
void ItemRecountHandler(CpuContext* context) {
    const std::uint64_t sums = SidecarItemSums();
    const auto a = static_cast<std::uint32_t>(sums), b = static_cast<std::uint32_t>(sums >> 32);
    std::uint32_t first = 0, second = 0;
    std::memcpy(&first, At(context->rsi + 0xE08), sizeof(first));
    std::memcpy(&second, At(context->rsi + 0xE0C), sizeof(second));
    first += a;
    second += b;
    std::memcpy(At(context->rsi + 0xE08), &first, sizeof(first));
    std::memcpy(At(context->rsi + 0xE0C), &second, sizeof(second));
    context->rdx = static_cast<std::uint32_t>(static_cast<std::uint32_t>(context->rdx) + a);
    context->rcx = static_cast<std::uint32_t>(static_cast<std::uint32_t>(context->rcx) + b);
}

// Replaces the three item store callbacks: positions 0-3 as the game, 4-7 into sidecars, others dropped.
void __fastcall ItemStoreHook(void*, const std::int32_t* position, const std::uint64_t* item) {
    const std::int32_t index = *position;
    if (index >= 0 && index < kVanillaPlayers) {
        std::uint64_t status = 0;
        std::memcpy(&status, game + kGameStatusPointer, sizeof(status));
        std::memcpy(At(status + kResultItems + static_cast<std::uint64_t>(index) * 8), item, sizeof(*item));
    } else if (index >= kVanillaPlayers && index < kMaxPlayers) {
        sidecarItems[index - kVanillaPlayers] = *item;
        if (*item) LogOnce(12, "MISSION item counts of result position %lld kept in a sidecar", index);
    }
}

using ConstructorFn = void*(__fastcall*)(void*);
void* __fastcall MissionContextConstructorHook(void* self) {
    const auto original = reinterpret_cast<ConstructorFn>(game + kMissionContextConstructor);
    void* result = original(self);
    // make_shared memory is not zeroed; an all-zero weak_ptr is an empty one.
    std::memset(static_cast<std::uint8_t*>(self) + kMovedPlayerArray, 0, kMaxPlayers * kPlayerEntrySize);
    return result;
}

using ScaleFn = float(__fastcall*)(void*, int, int);
// CONFIG.SGO ModeList[mode][7][difficulty][1] holds the factors for 1-4 players (online e.g. 1.2, 0.8, 1.0,
// 1.2); the lookup has no bound, so five or more players must never reach it.
// Enemy durability (0D7720 = diff[3][0] * value * factor): five or more players get the 4-player factor, so
// only the enemy counts grow with the team (1.1.0; 0.5.0-1.0.0 added the 3->4 step once for the fifth player).
float __fastcall DurabilityScaleHook(void* data, int difficulty, int players) {
    const auto original = reinterpret_cast<ScaleFn>(game + kPlayerCountScale);
    if (players <= kVanillaPlayers) return original(data, difficulty, players);
    LogOnce(1, "MISSION enemy durability for %lld players uses the 4-player factor", players);
    return original(data, difficulty, kVanillaPlayers);
}

// Damage from enemies (54F050 = diff[3][1] or [2] * factor): five or more players take the 4-player damage.
float __fastcall DamageScaleHook(void* data, int difficulty, int players) {
    const auto original = reinterpret_cast<ScaleFn>(game + kPlayerCountScale);
    if (players <= kVanillaPlayers) return original(data, difficulty, players);
    LogOnce(13, "MISSION enemy damage for %lld players uses the 4-player factor", players);
    return original(data, difficulty, kVanillaPlayers);
}

// Online player count as the mission sync stores it: alone (1) becomes 1 + GhostPlayers.
template <std::uint64_t CpuContext::*Count>
void GhostCountHandler(CpuContext* context) {
    const auto count = static_cast<std::uint32_t>(context->*Count);
    const int ghosts = ghostPlayers.load();
    if (count == 1 && ghosts > 0) {
        activeGhosts.store(ghosts);
        context->*Count = static_cast<std::uint32_t>(1 + ghosts);
        Log("MISSION ghost harness: online player count 1 -> %d", 1 + ghosts);
    } else {
        activeGhosts.store(0);
    }
}

using CreateOnlinePlayerFn = void*(__fastcall*)(std::uint32_t, void*, std::uint32_t);
// CreatePlayer's online path finds the user whose index matches; ghosts use player 1's user and
// loadout, created at their own spawn point.
void* __fastcall GhostCreateOnlinePlayerHook(std::uint32_t index, void* transform, std::uint32_t weaponMode) {
    const int ghosts = activeGhosts.load();
    const std::uint32_t source = ghosts && index >= 1 && index <= static_cast<std::uint32_t>(ghosts) ? 0 : index;
    return reinterpret_cast<CreateOnlinePlayerFn>(game + kCreateOnlinePlayer)(source, transform, weaponMode);
}

}  // namespace

bool OnlineSession() { return game && OnlineMode(); }

int MissionPlayers() {
    if (!game) return 0;
    std::uint64_t status = 0;
    std::memcpy(&status, game + kGameStatusPointer, sizeof(status));
    std::int32_t players = 0;
    if (status) std::memcpy(&players, At(status + kMissionPlayers), sizeof(players));
    return players;
}

void InitMission(unsigned char* gameBase, int ghosts) {
    game = gameBase;
    SetGhostPlayers(ghosts);
    ghostHarness = ghosts > 0;
    activeGhosts.store(0);
}

int ActiveGhosts() { return activeGhosts.load(); }

int GhostPlayers() { return ghostPlayers.load(); }

void SetGhostPlayers(int count) {
    ghostPlayers.store(count < 0 ? 0 : (count > kMaxPlayers - 1 ? kMaxPlayers - 1 : count));
}

int NextGhostCount(int count) {
    if (count < kVanillaPlayers) return kVanillaPlayers;         // off -> five players
    return count + 1 > kMaxPlayers - 1 ? 0 : count + 1;          // five .. eight, then off
}

bool GhostHarness() { return ghostHarness; }

int LocalPlayerIndex() { return localPlayer.load(); }

MidHandler GhostHookHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x790BA6: return &GhostCountHandler<&CpuContext::rax>;
        case 0x78D765: return &GhostCountHandler<&CpuContext::r12>;
        default: return nullptr;
    }
}

void* GhostCallHandler(std::uint32_t rva) {
    return rva == 0x1DC525 ? reinterpret_cast<void*>(&GhostCreateOnlinePlayerHook) : nullptr;
}

std::uint64_t LoadoutRecordOffset(std::int64_t index) {
    if (index >= 0 && index < kVanillaPlayers) return static_cast<std::uint64_t>(index) * kLoadoutRecordSize;
    std::uint64_t status = 0;
    std::memcpy(&status, game + kGameStatusPointer, sizeof(status));
    const std::uint8_t* record = scratchRecord;
    if (index >= kVanillaPlayers && index < kMaxPlayers) {
        record = sidecars[index - kVanillaPlayers];
        LogOnce(static_cast<unsigned>(kLoadoutLogBit + index), "MISSION loadout record of player index %lld kept in a sidecar", index);
    } else {
        LogOnce(2, "MISSION loadout record index %lld out of range; using a scratch record", index);
    }
    // 64-bit wrap-around: the game's `index*0xD4 + GameStatus + 0x14C78` then lands on `record`.
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(record)) - (status + kLoadoutRecords);
}

void* MissionSlotHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x17EEF70:
        case 0x1791DF0:
        case 0x179DE90: return reinterpret_cast<void*>(&ItemStoreHook);
        default: return nullptr;
    }
}

std::uint64_t SidecarItem(int index) {
    return index >= kVanillaPlayers && index < kMaxPlayers ? sidecarItems[index - kVanillaPlayers] : 0;
}

const std::uint8_t* LoadoutSidecar(int index) {
    return index >= kVanillaPlayers && index < kMaxPlayers ? sidecars[index - kVanillaPlayers] : nullptr;
}

MidHandler MissionHookHandler(std::uint32_t rva) {
    using C = CpuContext;
    switch (rva) {
        case 0x595A03: return &RecordOffsetHandler<&C::rbx, &C::rdi>;
        case 0x790887: return &RecordOffsetHandler<&C::r8, &C::rcx>;
        case 0x790927: return &RecordOffsetHandler<&C::rax, &C::r9>;
        case 0x59DCFA: return &RecordOffsetHandler<&C::rsi, &C::rax>;
        case 0x5A4366: return &RecordOffsetHandler<&C::rax, &C::rax>;
        case 0x7FFDA3: return &RecordOffsetHandler<&C::rcx, &C::rcx>;
        case 0x7FFD95: return &PlayerTagIndexHandler;
        case 0x0DFD27: return &RecordOffsetHandler<&C::rbx, &C::rax>;
        case 0x591914: return &RecordOffsetHandler<&C::rax, &C::r15>;
        case 0x1D968E: return &SpawnTableCountHandler;
        case 0x1D98B6: return &RemoteFlagHandler;
        case 0x1D9A3B: return &RemoteFlagArrayHandler;
        case 0x1D99F2: return &ClearPlayersHandler;
        case 0x1D9A98: return &SpawnOffsetHandler;
        case 0x1A19F3: return &LoopEndHandler<&C::rsi, &C::rdi>;
        case 0x1A352B: return &LoopEndHandler<&C::rsi, &C::r13>;
        case 0x1A3ACC: return &LoopEndHandler<&C::r13, &C::r12>;
        case 0x1BAC0A: return &LoopEndHandler<&C::r14, &C::rdi>;
        case 0x1C243F: return &LoopEndHandler<&C::rdi, &C::r13>;
        case 0x1DBA07: return &LoopEndHandler<&C::rbp, &C::rdi>;
        case 0x1A3281:
        case 0x1A3451: return &AreaFactorCallHandler;
        case 0x78E693: return &ClearItemsHandler;
        case 0x2C865F: return &ItemTotalsHandler;
        case 0x2C9402: return &ItemRecountHandler;
        default: return nullptr;
    }
}

void* MissionCallHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x1D6CD5:
        case 0x1DD618: return reinterpret_cast<void*>(&MissionContextConstructorHook);
        case 0x0D7770: return reinterpret_cast<void*>(&DurabilityScaleHook);
        case 0x54F0DF:
        case 0x54F1B1: return reinterpret_cast<void*>(&DamageScaleHook);
        default: return nullptr;
    }
}

}  // namespace multislot
