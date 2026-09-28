#include "patches.h"

#include <cstring>

#include "mission.h"  // kMovedPlayerArray, kPlayerEntrySize

namespace multislot {
namespace {

using Bytes = std::vector<std::uint8_t>;

// The same instruction with its little-endian immediate operand at `offset` replaced.
Patch Immediate(const char* name, std::uint32_t rva, Bytes original, std::size_t offset, std::size_t size, std::uint64_t value) {
    Bytes replacement = original;
    std::memcpy(replacement.data() + offset, &value, size);
    return {name, rva, std::move(original), std::move(replacement)};
}

// make_shared allocates the control block plus the object; the moved player array ends the object.
constexpr std::uint64_t kContextAllocation = kMovedPlayerArray + kMaxPlayers * kPlayerEntrySize + 0x10;

constexpr std::uint64_t Mirror(std::uint64_t vanilla) { return 2 * kSearchTypeCenter - vanilla; }

// Operands of the rewritten SEARCH_TYPE functions, all derived from the centre.
constexpr std::uint8_t kLowMirror = static_cast<std::uint8_t>(Mirror(0x94));               // lowest MultiSlot value
constexpr std::uint8_t kLowMirrorDisp = static_cast<std::uint8_t>(0x100 - kLowMirror);     // disp8 of -kLowMirror
constexpr std::uint8_t kToVanillaBase = static_cast<std::uint8_t>(0x90 - kLowMirror);      // (v - low) - this = v - 0x90
constexpr std::uint8_t kCentreDisp = static_cast<std::uint8_t>(0x100 - kSearchTypeCenter); // low byte of disp32 -centre
constexpr std::uint8_t kNearestKind = static_cast<std::uint8_t>(0x91 - kSearchTypeCenter); // |v - centre| of kind 0
static_assert(kSearchTypeCenter <= 0x80 && Mirror(0x91) < 0x90, "the mirror must stay below the vanilla values");

}  // namespace

std::vector<Patch> GuestPatches() {
    return {
        // Join check 749AC0 `(v & ~0xF) == 0x90` -> v in 0x90..0x9F or the four MultiSlot values. It needs
        // 18 bytes: the entry jumps back into the int3 padding after the previous function (749AB1..749ABF).
        {"join check accepts MultiSlot rooms (padding part)", 0x749AB1,
         {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC},
         {
             0x8D, 0x41, kLowMirrorDisp,  // 749AB1 lea  eax, [rcx-low]
             0x83, 0xF8, 0x04,            // 749AB4 cmp  eax, 4
             0x73, 0x09,                  // 749AB7 jae  749AC2
             0xB0, 0x01,                  // 749AB9 mov  al, 1        ; low..low+3
             0xC3,                        // 749ABB ret
         }},
        {"join check accepts MultiSlot rooms", 0x749AC0,
         {0x83, 0xE1, 0xF0, 0x81, 0xF9, 0x90, 0x00, 0x00, 0x00, 0x0F, 0x94, 0xC0, 0xC3},
         {
             0xEB, 0xEF,                  // 749AC0 jmp  749AB1
             0x83, 0xE8, kToVanillaBase,  // 749AC2 sub  eax, 0x90-low ; v - 0x90
             0x83, 0xF8, 0x10,            // 749AC5 cmp  eax, 0x10
             0x0F, 0x92, 0xC0,            // 749AC8 setb al           ; 0x90..0x9F, as vanilla
             0xC3,                        // 749ACB ret
             0xCC,
         }},

        // SEARCH_TYPE -> room kind (0x91:0 0x92:2 0x93:3 0x94:1, else 0), rewritten to look up
        // |v - centre| - (0x91 - centre) so the MultiSlot values decode to the same kinds. Same size.
        {"search type decode handles mirror", 0x74AC20,
         {0x81, 0xE9, 0x91, 0x00, 0x00, 0x00, 0x74, 0x1E, 0x83, 0xE9, 0x01, 0x74, 0x13, 0x83, 0xE9, 0x01,
          0x74, 0x08, 0x83, 0xF9, 0x01, 0x75, 0x0F, 0x8B, 0xC1, 0xC3, 0xB8, 0x03, 0x00, 0x00, 0x00, 0xC3,
          0xB8, 0x02, 0x00, 0x00, 0x00, 0xC3, 0x33, 0xC0, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC},
         {
             0x8D, 0x81, kCentreDisp, 0xFF, 0xFF, 0xFF,  // lea   eax, [rcx-centre]
             0x99,                                      // cdq
             0x31, 0xD0,                                // xor   eax, edx
             0x29, 0xD0,                                // sub   eax, edx          ; eax = |v-centre|
             0x83, 0xE8, kNearestKind,                  // sub   eax, 0x91-centre  ; 0x91/low+3 -> 0 .. 0x94/low -> 3
             0x83, 0xF8, 0x03,                          // cmp   eax, 3
             0x77, 0x0C,                                // ja    other
             0x48, 0x8D, 0x15, 0x08, 0x00, 0x00, 0x00,  // lea   rdx, [kinds]
             0x0F, 0xB6, 0x04, 0x02,                    // movzx eax, byte [rdx+rax]
             0xC3,                                      // ret
             0x31, 0xC0,                                // other: xor eax, eax
             0xC3,                                      // ret
             0x00, 0x02, 0x03, 0x01,                    // kinds for the four distances
             0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
         }},

        // The searched ranges (74AC50) follow the 8Player MOD setting: HostModeHooks.

        // Room info: `mov [r14+0x7C], 4` -> `shr rax, 32; mov [r14+0x7C], eax`. The call at 73B5CC
        // now returns (capacity << 32) | members; 73B5D1 already stores the low half as the count.
        {"room info capacity from EOS", 0x73B5D5,
         {0x41, 0xC7, 0x46, 0x7C, 0x04, 0x00, 0x00, 0x00},
         {0x48, 0xC1, 0xE8, 0x20, 0x41, 0x89, 0x46, 0x7C}},
    };
}

std::vector<CallSite> GuestCalls() {
    return {
        // Search result / room info parse (73B230): member count, then the capacity rewrite above.
        {"room info member count", 0x73B5CC, 0x12AEB30},
        // Host room update (78BB60): `count >= 4` becomes the HIDDEN attribute passed to 749AD0,
        // which would hide a MultiSlot room from searches as soon as its 4th player joined.
        {"room HIDDEN-when-full check", 0x78BDE2, 0x12AEB30},
    };
}

std::vector<Patch> SessionPatches() {
    return {
        // eos::Users (12B77E0, one per room, made by the room constructor 12BD460) inlines `slots.resize(4)` on its
        // empty vector of shared_ptr<User>: Users::Add (12B7F50) puts a joining member in the first empty slot and
        // drops them when there is none, so the host never accepts their P2P connection. The slot index is the
        // user's UsrIndexKey. The shrink branch (`lea rbp, [rdx+0x40]`) cannot run on an empty vector.
        Immediate("room user slots size check", 0x12B78EA, {0x48, 0x83, 0xF9, 0x04}, 3, 1, kMaxPlayers),
        Immediate("room user slots capacity check", 0x12B7954, {0x48, 0x83, 0xF8, 0x04}, 3, 1, kMaxPlayers),
        Immediate("room user slots grow", 0x12B795F, {0xBA, 0x04, 0x00, 0x00, 0x00}, 1, 4, kMaxPlayers),
        Immediate("room user slots fill", 0x12B796E, {0xB8, 0x04, 0x00, 0x00, 0x00}, 1, 4, kMaxPlayers),
        // eos::packet::Controller (12CB5F0, built with the room's Users): `mov ebx, 4` is the reserve and resize count
        // of its session vector, which 12CDA40 / 12CDD50 / 12CFA90 index by the user slot without a bound.
        Immediate("room packet sessions", 0x12CB96E, {0xBB, 0x04, 0x00, 0x00, 0x00}, 1, 4, kMaxPlayers),
        // UiVoiceChat_Notify (the "who is talking" HUD, constructor 9605E0): its record vector (+0x140 data, +0x148
        // capacity, +0x150 size, 0x50-byte records) is reserved and resized to four inline. Its update (961140, vtable
        // slot 1811998) writes one record per room member without a bound, so a fifth member overran the vector and
        // every machine in the room crashed in a string copy (2026-09-18 00:07, all five logs).
        Immediate("voice chat HUD records reserve check", 0x96069E, {0x48, 0x83, 0x7F, 0x10, 0x04}, 4, 1, kMaxPlayers),
        Immediate("voice chat HUD records allocation", 0x9606A9, {0xB9, 0x40, 0x01, 0x00, 0x00}, 1, 4, 0x50 * kMaxPlayers),
        Immediate("voice chat HUD records move limit", 0x9606C3, {0x48, 0x83, 0xFB, 0x04}, 3, 1, kMaxPlayers),
        Immediate("voice chat HUD records move count", 0x9606D4, {0xBB, 0x04, 0x00, 0x00, 0x00}, 1, 4, kMaxPlayers),
        Immediate("voice chat HUD records capacity", 0x960774, {0x48, 0xC7, 0x47, 0x10, 0x04, 0x00, 0x00, 0x00}, 4, 4, kMaxPlayers),
        Immediate("voice chat HUD records count", 0x960781, {0xBA, 0x04, 0x00, 0x00, 0x00}, 1, 4, kMaxPlayers),
    };
}

std::vector<CallSite> RoomViewCalls() {
    return {
        // Panel builder 8F8020 reads its template name from a four-entry table for every member.
        {"room panels rebuild", 0x8FBABE, 0x8F8020},
        {"room panels refresh", 0x8FFE45, 0x8F8020},
        // Voice chat icons pair panel i with member i every frame.
        {"room voice icons", 0x8FFBED, 0x903160},
    };
}

PointerSlot RoomViewSlot() {
    return {"HUiRoom OnUpdate", 0x180AC48, 0x8FDFE0};
}

std::vector<CallSite> FakeMemberCalls() {
    return {
        // eos::RoomInfo::PlayerInfo list builder: the room screen's member vector and the voice chat HUD's
        // records are both built from what it returns, and so is anything else that asks for the members.
        {"room member list (room screen)", 0x901170, 0x7468C0},
        {"room member list (voice chat HUD)", 0x9611F6, 0x7468C0},
        {"room member list (voice chat HUD refresh)", 0x961373, 0x7468C0},
    };
}

std::vector<MidSite> HostModeHooks() {
    return {
        // Room creation starts with a Steam lobby that joiners enter before the EOS lobby: `mov r8d, 4` is its
        // cMaxMembers for 7812B0 (ISteamMatchmaking::CreateLobby). A full Steam lobby refuses the fifth player.
        {"steam lobby create capacity", 0x7435F7, {0x41, 0xB8, 0x04, 0x00, 0x00, 0x00}, 0, 0},
        // Lobby create options: `mov qword [rbp-0x60], 4` (MaxLobbyMembers); the handler stores the capacity.
        {"lobby create capacity", 0x742A9D, {0x48, 0xC7, 0x45, 0xA0, 0x04, 0x00, 0x00, 0x00}, 0, 0},
        // Room update: `mov edx, 4` before SetMaxMembers.
        {"lobby update capacity", 0x749C91, {0xBA, 0x04, 0x00, 0x00, 0x00}, 0, 0},
        // Room update: `mov ebx, 0x9x`, the SEARCH_TYPE published for the room kind in [r14+0x90].
        {"publish search type kind 3", 0x749CBF, {0xBB, 0x93, 0x00, 0x00, 0x00}, 0, 0},
        {"publish search type kind 2", 0x749CC6, {0xBB, 0x92, 0x00, 0x00, 0x00}, 0, 0},
        {"publish search type kind 1", 0x749CCD, {0xBB, 0x94, 0x00, 0x00, 0x00}, 0, 0},
        {"publish search type kind 0", 0x749CD4, {0xBB, 0x91, 0x00, 0x00, 0x00}, 0, 0},
        // Room search (74AC50): `movabs rax, (high << 32) | 0x91`, the SEARCH_TYPE range asked for per room kind.
        // OFF lists normal and MultiSlot rooms of the kind, ON MultiSlot rooms only (hostmode.cpp).
        {"search range kind 3", 0x74AC69, {0x48, 0xB8, 0x91, 0x00, 0x00, 0x00, 0x93, 0x00, 0x00, 0x00}, 0, 0},
        {"search range kind 2", 0x74AC74, {0x48, 0xB8, 0x91, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00, 0x00}, 0, 0},
        {"search range kind 1", 0x74AC7F, {0x48, 0xB8, 0x91, 0x00, 0x00, 0x00, 0x94, 0x00, 0x00, 0x00}, 0, 0},
        {"search range kind 0", 0x74AC8A, {0x48, 0xB8, 0x91, 0x00, 0x00, 0x00, 0x91, 0x00, 0x00, 0x00}, 0, 0},
    };
}

PointerSlot MainFrameSlot() {
    return {"HUiMainFrame OnUpdate", 0x1806D38, 0x8C12B0};
}

PointerSlot LobbySlot() {
    return {"HUiLobby OnUpdate", 0x1809B90, 0x8ECBB0};
}

std::vector<Patch> MissionPatches() {
    // `add reg, 0x100` / `lea reg, [reg+0x100]` imm32 0x100 -> 0x300: MissionContext's player array moved.
    const auto moved = [](const char* name, std::uint32_t rva, Bytes original) {
        Bytes replacement = original;
        replacement[replacement.size() - 3] = 0x03;
        return Patch{name, rva, std::move(original), std::move(replacement)};
    };
    return {
        // MissionSync_Res (78D0E0, host): replies from players with index >= 4 were dropped.
        Immediate("mission sync host keeps players 5+", 0x78D24C, {0x83, 0xF9, 0x04}, 2, 1, kMaxPlayers),
        // MissionSync_Update (790600): the 0xA4-byte part of a record was only copied for index < 4.
        Immediate("mission sync stores records 5+", 0x7908A0, {0x83, 0xF9, 0x04}, 2, 1, kMaxPlayers),
        // CreateOnlinePlayerObject (595960) refused index >= 4; 59DC90 and 0DFCF0 skipped it.
        Immediate("online player object accepts index 4+", 0x5959E1, {0x83, 0xFF, 0x04}, 2, 1, kMaxPlayers),
        Immediate("player record lookup accepts index 4+", 0x59DCEA, {0x83, 0xF8, 0x04}, 2, 1, kMaxPlayers),
        Immediate("player color lookup accepts index 4+", 0x0DFD19, {0x41, 0x83, 0xF8, 0x04}, 3, 1, kMaxPlayers),

        // MissionContext grows past its 0x2F8 bytes (+0x10 control block) to hold kMaxPlayers entries at +0x300.
        Immediate("MissionContext allocation", 0x1D6C9C, {0xB9, 0x08, 0x03, 0x00, 0x00}, 1, 4, kContextAllocation),
        Immediate("MissionContext snapshot allocation", 0x1DD5DC, {0xB9, 0x08, 0x03, 0x00, 0x00}, 1, 4, kContextAllocation),
        moved("MissionContext destructor player array", 0x1D6ECE, {0x48, 0x8D, 0x8E, 0x00, 0x01, 0x00, 0x00}),
        // `lea r8d, [rdx-0xC]` with edx = the 0x10-byte entry size: the count of entries to destroy.
        Immediate("MissionContext destructor player count", 0x1D6EE1, {0x44, 0x8D, 0x42, 0xF4}, 3, 1,
                  static_cast<std::uint8_t>(kMaxPlayers - 0x10)),
        {"CreatePlayers stores into the moved array", 0x1D9A45, {0x48, 0x8D, 0xB9, 0x08, 0x01, 0x00, 0x00},
         {0x48, 0x8D, 0xB9, 0x08, 0x03, 0x00, 0x00}},
        moved("FindPlayerIndex array", 0x1DA361, {0x48, 0x8D, 0xB9, 0x00, 0x01, 0x00, 0x00}),
        Immediate("FindPlayerIndex count", 0x1DA3F8, {0x48, 0x83, 0xFD, 0x04}, 3, 1, kMaxPlayers),
        // lea eax, [rbp-(count+1)]: the loop leaves rbp = count, so this is the -1 of "not found".
        Immediate("FindPlayerIndex not found", 0x1DA402, {0x8D, 0x45, 0xFB}, 2, 1,
                  static_cast<std::uint8_t>(-(kMaxPlayers + 1))),
        moved("snapshot player array", 0x1DB0E1, {0x49, 0x8D, 0xBE, 0x00, 0x01, 0x00, 0x00}),
        // GetPlayerObject(i): (i + 0x10) << 4 -> (i + 0x30) << 4.
        {"GetPlayerObject array", 0x1B4F08, {0x48, 0x83, 0xC3, 0x10}, {0x48, 0x83, 0xC3, 0x30}},
        Immediate("PlayerIgnoreDamageEventMode players", 0x1B8139, {0x83, 0xFD, 0x04}, 2, 1, kMaxPlayers),
        Immediate("PlayerStealthMode players", 0x1B82B9, {0x83, 0xFD, 0x04}, 2, 1, kMaxPlayers),
        // Loops over the first four entries follow the array (players 5+ are not part of these checks yet).
        moved("player loop 1A1990", 0x1A19C4, {0x48, 0x81, 0xC7, 0x00, 0x01, 0x00, 0x00}),
        moved("Factor_PlayerAreaIn players", 0x1A31B4, {0x49, 0x81, 0xC6, 0x00, 0x01, 0x00, 0x00}),
        moved("Factor_PlayerAreaIn_Meeting players", 0x1A3384, {0x49, 0x81, 0xC6, 0x00, 0x01, 0x00, 0x00}),
        moved("Factor_PlayerObjectDistance players", 0x1A3516, {0x49, 0x81, 0xC5, 0x00, 0x01, 0x00, 0x00}),
        moved("Factor_PlayerPointDistance players", 0x1A3AB3, {0x49, 0x81, 0xC4, 0x00, 0x01, 0x00, 0x00}),
        moved("player loop 1BABA0", 0x1BAC03, {0x48, 0x81, 0xC7, 0x00, 0x01, 0x00, 0x00}),
        moved("player loop 1C2370", 0x1C2438, {0x49, 0x81, 0xC5, 0x00, 0x01, 0x00, 0x00}),
        moved("player loop 1DB9F0", 0x1DBA00, {0x48, 0x81, 0xC7, 0x00, 0x01, 0x00, 0x00}),

        // ResultSync_Update (78FD70) resets and stores the item counts of the first four players only.
        Immediate("result items reset players 5+", 0x78FFAC, {0x83, 0xFB, 0x04}, 2, 1, kMaxPlayers),
        Immediate("result items store players 5+", 0x78FFDC, {0x41, 0x83, 0xFD, 0x04}, 3, 1, kMaxPlayers),
    };
}

std::vector<MidSite> MissionHooks() {
    return {
        // `imul reg, index, 0xD4` wherever GameStatus+0x14C78 + index*0xD4 is addressed.
        {"loadout record CreateOnlinePlayerObject", 0x595A03, {0x48, 0x69, 0xDF, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        {"loadout record MissionSync header", 0x790887, {0x4C, 0x69, 0xC1, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        {"loadout record MissionSync weapons", 0x790927, {0x49, 0x69, 0xC1, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        {"loadout record 59DC90", 0x59DCFA, {0x48, 0x69, 0xF0, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        {"loadout record player object", 0x5A4366, {0x48, 0x69, 0xC0, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        {"loadout record soldier type UI", 0x7FFDA3, {0x48, 0x69, 0xC9, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        {"loadout record color", 0x0DFD27, {0x48, 0x69, 0xD8, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        {"loadout record color index", 0x591914, {0x49, 0x69, 0xC7, 0xD4, 0x00, 0x00, 0x00}, 0, 0},
        // The player index 7FFBD0 reports for the online HUD's four-entry colour tables, wrapped.
        {"HUD colour index of players 5+", 0x7FFD95, {0x48, 0x63, 0x4E, 0x48, 0x41, 0x89, 0x0E}, 0, 0},
        // CreatePlayers (1D9520).
        {"CreatePlayers spawn table count", 0x1D968E, {0x41, 0x8B, 0xF4, 0x44, 0x0F, 0x28, 0x4D, 0x90}, 3, 5},
        // The remote flag of each player: `mov [rsp+r15+0x28], al` fills eight bytes on the stack and
        // `lea r12, [rsp+0x28]` reads them back. Both move to a buffer of kMaxPlayers in mission.cpp.
        {"CreatePlayers remote flag store", 0x1D98B6, {0x42, 0x88, 0x44, 0x3C, 0x28}, 0, 0},
        {"CreatePlayers remote flag array", 0x1D9A3B, {0x4C, 0x8D, 0x64, 0x24, 0x28}, 0, 0},
        {"CreatePlayers clears the moved array", 0x1D99F2, {0x48, 0x81, 0xC3, 0x00, 0x01, 0x00, 0x00}, 0, 0},
        {"CreatePlayers spawn offset", 0x1D9A98, {0x42, 0x0F, 0x10, 0x84, 0x2F, 0xF8, 0xFE, 0xFF, 0xFF}, 0, 0},

        // `lea end, [begin+0x40]` of the loops over the moved array: end = begin + 0x80 (all eight players).
        {"Factor_GroupObjectDistance players 5+", 0x1A19F3, {0x48, 0x8D, 0x77, 0x40, 0x48, 0x3B, 0xFE}, 4, 3},
        {"Factor_PlayerObjectDistance players 5+", 0x1A352B, {0x49, 0x8D, 0x75, 0x40, 0x48, 0x89, 0x75, 0x7F}, 4, 4},
        {"Factor_PlayerPointDistance players 5+", 0x1A3ACC, {0x4D, 0x8D, 0x6C, 0x24, 0x40}, 0, 0},
        {"TalkBegin players 5+", 0x1BAC0A, {0x4C, 0x8D, 0x77, 0x40, 0xBD, 0xFF, 0xFF, 0xFF, 0xFF}, 4, 5},
        {"SetAiRouteNavigate players 5+", 0x1C243F, {0x49, 0x8D, 0x7D, 0x40, 0x48, 0x89, 0x7D, 0xD7}, 4, 4},
        {"user observer players 5+", 0x1DBA07, {0x48, 0x8D, 0x6F, 0x40, 0x48, 0x3B, 0xFD}, 4, 3},
        // Factor_PlayerAreaIn / _Meeting hand a four-entry copy of the array to
        // EventFactor_ObjectAreaIn::Initialize (207870); the call is made by the handler with all eight.
        {"Factor_PlayerAreaIn players 5+", 0x1A3281, {0xE8, 0xEA, 0x45, 0x06, 0x00}, 0, 0},
        {"Factor_PlayerAreaIn_Meeting players 5+", 0x1A3451, {0xE8, 0x1A, 0x44, 0x06, 0x00}, 0, 0},
        // Item counts of players 5+ (GameStatus+0x14FD4 holds four): cleared with the game's, added to its totals.
        {"result items cleared for players 5+", 0x78E693, {0x48, 0x89, 0x81, 0xD4, 0x4F, 0x01, 0x00}, 0, 7},
        {"result item totals include players 5+", 0x2C865F, {0x41, 0x89, 0x8B, 0x0C, 0x0E, 0x00, 0x00}, 0, 7},
        {"result item totals include players 5+ (recount)", 0x2C9402, {0x03, 0x96, 0x10, 0x0E, 0x00, 0x00}, 0, 6},
    };
}

std::vector<CallSite> MissionCalls() {
    return {
        {"MissionContext constructor", 0x1D6CD5, 0x1D6200},
        {"MissionContext snapshot constructor", 0x1DD618, 0x1D6200},
        {"difficulty scale 0D7720", 0x0D7770, 0x0E18B0},
        {"difficulty scale 54F050", 0x54F0DF, 0x0E18B0},
        {"difficulty scale 54F050 (second)", 0x54F1B1, 0x0E18B0},
    };
}

std::vector<MidSite> ArmorHooks() {
    return {
        // `mov reg, [GameStatus + index*4 + 0x6F88]`, index = slot * 0xF98 + class. The room keeps showing the
        // real armor (D7BFA, the value published to it, is deliberately not here): a raised number published
        // to the room comes back as what everyone else copies from, and a room of beginners would climb.
        {"armor pickups in the mission sync", 0x78EC3D, {0x42, 0x8B, 0x84, 0x8B, 0x88, 0x6F, 0x00, 0x00}, 0, 0},
        {"armor pickups of your own player (offline)", 0x595CD9, {0x43, 0x8B, 0x84, 0x82, 0x88, 0x6F, 0x00, 0x00}, 0, 0},
        // Online every player is built from a loadout record, this machine's own included, so the record's
        // pickup count is read here instead of the save: `mov esi, [rbx+rdx+0x118]` with rdi the player index.
        {"armor pickups of your own player (online)", 0x595A12, {0x8B, 0xB4, 0x13, 0x18, 0x01, 0x00, 0x00}, 0, 0},
    };
}

std::vector<MidSite> SpawnHooks() {
    return {
        // Right after the scene object factory call in the loops that create groups, before
        // `mov r8, rax; test rax, rax`: the first created object's class decides the loop count.
        {"enemy group count (CreateObjectGroup)", 0x1D8E5F, {0x4C, 0x8B, 0xC0, 0x48, 0x85, 0xC0}, 0, 6},
        {"enemy group count (CreateAreaObject)", 0x1D7A4B, {0x4C, 0x8B, 0xC0, 0x48, 0x85, 0xC0}, 0, 6},
        // CreateFlyingEnemyGroup_OnRoute after its first CreateObject, before `cmp qword [rsp+0x60], 0`.
        {"flying enemy group on route count", 0x1AF968, {0x48, 0x83, 0x7C, 0x24, 0x60, 0x00}, 0, 6},
        // Generators keep a per-wave count: `mov reg32, r9d; mov reg, r8` copies it at entry.
        {"Object.SetEnemyGenerator count", 0x1C2F97, {0x45, 0x8B, 0xF1, 0x49, 0x8B, 0xD8}, 0, 6},
        {"Object.SetInstantEnemyGenerator count", 0x1C3235, {0x45, 0x8B, 0xF9, 0x49, 0x8B, 0xD8}, 0, 6},
        // EDF6_TimeShip_CreatePodCrue with its pod found (`mov rax, [rsp+0x40]`), before the crew loop uses r12d.
        {"EDF6_TimeShip_CreatePodCrue count", 0x1CAF52, {0x48, 0x8B, 0x44, 0x24, 0x40}, 0, 5},
        // `mov eax, [rbp+x]` reads the count stack argument of the instant area generators.
        {"InstantGenerateEnemy_Area count", 0x1B5434, {0x8B, 0x85, 0xE0, 0x00, 0x00, 0x00}, 0, 6},
        {"InstantGenerateEnemy_AreaEX count", 0x1B59C0, {0x8B, 0x85, 0x10, 0x01, 0x00, 0x00}, 0, 6},
    };
}

std::vector<MidSite> DiagnosticHooks() {
    return {
        {"lobby join button gate", 0x8EC476, {0x80, 0xB9, 0x00, 0x03, 0x00, 0x00, 0x00}, 0, 7},
        {"lobby join completion", 0x8EFEC0, {0x48, 0x89, 0x5C, 0x24, 0x10}, 0, 5},
        {"lobby room list next page completion", 0x8F01D0, {0x48, 0x89, 0x5C, 0x24, 0x10}, 0, 5},
        {"lobby room list refresh completion", 0x8F02E0, {0x48, 0x89, 0x5C, 0x24, 0x08}, 0, 5},
        // Steam LobbyEnter_t callback (743490), before `cmp dword [rdx+0x10], 1; mov rbx, rcx`.
        {"Steam lobby enter result", 0x743496, {0x83, 0x7A, 0x10, 0x01, 0x48, 0x8B, 0xD9}, 0, 7},
        // Users::Add (12B7F50) after the empty-slot search: a slot was found (`lea r9, [rbx+8]; mov r8, rbx`), or
        // none was left (`mov [r15], r14; mov [r15+8], r14` returns an empty user).
        {"room user slot taken", 0x12B80CB, {0x4C, 0x8D, 0x4B, 0x08, 0x4C, 0x8B, 0xC3}, 0, 7},
        // The EOS lobby member-status callback (registered at 12B378A). rdx is the callback info:
        // +0x10 the member's ProductUserId, +0x18 the status. The jump table at 12BF4E0 sends LEFT(1),
        // DISCONNECTED(2) and KICKED(3) down the same path, which removes the user from eos::Users and
        // closes their P2P connection, so a transient disconnect tears a member down exactly like a
        // deliberate leave. Recording the code is how the next occurrence proves or refutes that.
        {"lobby member status", 0x12BEF00, {0x48, 0x89, 0x5C, 0x24, 0x10}, 0, 5},
        {"room user slots full", 0x12B8076, {0x4D, 0x89, 0x37, 0x4D, 0x89, 0x77, 0x08}, 0, 7},
        {"handshake initial timeout", 0x12D5C90, {0xC6, 0x87, 0xA8, 0x00, 0x00, 0x00, 0x01}, 0, 7},
    };
}

std::vector<CallSite> DiagnosticCalls() {
    return {
        {"handshake validation succeeded", 0x12D56CE, 0x12C7C40},
        {"handshake validation failed", 0x12D56A4, 0x12C8820},
        {"lobby join content check", 0x8EC659, 0x0D92B0},
        {"lobby join start", 0x8EC837, 0x8E7060},
        {"invite/session SEARCH_TYPE check", 0x709A30, 0x749AC0},
    };
}

std::vector<CallSite> RecoveryCalls() {
    return {{"handshake final hello recovery", 0x12D5B9B, 0x12C8F50}};
}

std::vector<PointerSlot> MissionSlots() {
    return {
        // (callback, int* position, NetGameStatus::Item*) -> GameStatus+0x14FD4 + position*8, no bound.
        {"ResultSync item store", 0x17EEF70, 0x793670},
        {"Online_GameOverWait item store", 0x1791DF0, 0x1BC410},
        {"Online_GameOverWait item store (BVM)", 0x179DE90, 0x22BCF0},
    };
}

std::vector<MidSite> GhostHooks() {
    return {
        // The instruction that stores the count runs after the handler, with the handler's value.
        {"ghost count MissionSync_Update", 0x790BA6, {0x89, 0x81, 0xF8, 0x4F, 0x01, 0x00}, 0, 6},
        {"ghost count MissionSync_Res", 0x78D765, {0x44, 0x89, 0xA0, 0xF8, 0x4F, 0x01, 0x00}, 0, 7},
    };
}

std::vector<CallSite> GhostCalls() {
    return {{"ghost player object", 0x1DC525, 0x591130}};
}

bool Matches(const std::uint8_t* at, const Patch& patch) {
    return at && patch.original.size() == patch.replacement.size() &&
           std::memcmp(at, patch.original.data(), patch.original.size()) == 0;
}

bool SlotTargets(const std::uint8_t* at, std::uint64_t imageBase, std::uint32_t targetRva) {
    if (!at) return false;
    std::uint64_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value == imageBase + targetRva;
}

bool CallTargets(const std::uint8_t* at, std::uint32_t siteRva, std::uint32_t targetRva) {
    if (!at || at[0] != 0xE8) return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, at + 1, sizeof(relative));
    return static_cast<std::int64_t>(siteRva) + 5 + relative == static_cast<std::int64_t>(targetRva);
}

}  // namespace multislot
