#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace multislot {

// EDF.dll build this table was taken from (Steam, 2025-01 update).
constexpr std::uint32_t kImageTimeDateStamp = 0x678CCB46;
constexpr std::uint32_t kImageSize = 0x22CE000;

constexpr int kVanillaPlayers = 4;
constexpr int kMaxPlayers = 8;

// Lobby SEARCH_TYPE. Vanilla rooms publish 0x90+k (k = 1..4) and a search asks for the range
// [0x91, 0x90+m]; joining checks (v & ~0xF) == 0x90. MultiSlot rooms publish the mirror of the vanilla
// value around kSearchTypeCenter: 0xD0 - v = 0x40-k (0x3C..0x3F, MultiSlot 1.6.6+). A modded search asks
// for [0x40-m, 0x90+m], which holds vanilla and MultiSlot rooms of the same kinds, or with 8Player MOD ON
// for [0x40-m, 0x3F], MultiSlot rooms only (1.5.6). Vanilla searches never
// reach below 0x91 and vanilla's join check refuses 0x3C..0x3F. Earlier MultiSlot versions cannot share a
// room of five or more with this one (0.2-0.4.1: mirror 0x8C..0x8F, enemy counts and strength not adjusted;
// 0.4.2-0.4.3: 0x7C..0x7F; 0.5.0-1.0.0: 0x74..0x77, the fifth player also raised enemy durability;
// 1.1.0-1.1.1: 0x6C..0x6F, four user slots and packet sessions, so a fifth member never got a P2P link;
// 1.2.0: 0x64..0x67, a four-record voice chat HUD that crashed everyone when the fifth member joined;
// 1.2.1-1.2.5: 0x5C..0x5F, four-entry HUD colour tables that crashed every machine in a mission of five or
// more on its first frame; 1.2.6-1.5.38: 0x54..0x57, enemies at the 4-player durability and damage with 5-8
// players, where 1.6.0 adds a step per player (mission.h kScaleSteps); 1.6.0-1.6.5: 0x44..0x47, centre 0x6C, the
// step on durability only on HARDEST and INFERNO, whose damage 1.6.6 raises too; 0x4C..0x4F, centre 0x70, was the
// offline ten-player experiment and is skipped), so they cannot join these rooms, and this version lists their rooms but refuses
// to join them: everyone in a room has to link to everyone and simulate missions the same way. Everything
// derived from the centre is computed in patches.cpp, so a new family is this constant plus new tests.
// Raising kMaxPlayers means a new family too: a room of nine needs nine user slots on every machine in it.
// Ten was built and tested offline on 2026-09-19 (centre 0x70, see research/NOTES.md); the distributed
// build stays at the eight that five machines have played.
constexpr std::uint32_t kSearchTypeCenter = 0x68;

// Bytes replaced at a fixed RVA. `original` is verified before anything is written.
struct Patch {
    const char* name;
    std::uint32_t rva;
    std::vector<std::uint8_t> original;
    std::vector<std::uint8_t> replacement;
};

// A `call rel32` whose target is verified, then pointed at a plugin function.
struct CallSite {
    const char* name;
    std::uint32_t rva;
    std::uint32_t target;
};

// An absolute function pointer (a vtable slot) whose value is verified, then replaced.
struct PointerSlot {
    const char* name;
    std::uint32_t rva;
    std::uint32_t target;
};

// Always applied: find and join both vanilla and MultiSlot rooms.
std::vector<Patch> GuestPatches();
// Always applied: every member of a room keeps a user slot and a packet session for each member, and the
// voice chat HUD keeps a record for each. eos::Users (constructor 12B77E0), eos::packet::Controller
// (constructor 12CB5F0) and UiVoiceChat_Notify (constructor 9605E0) size theirs to four, the local user
// included: a user without a slot gets no P2P link, and the HUD writes a fifth record past its vector. In
// rooms of four or fewer the extra entries stay empty, as the unused ones of smaller rooms already do.
std::vector<Patch> SessionPatches();
// Redirected to RoomCountAndCapacity and RoomFullCount (rooms.h), in this order.
std::vector<CallSite> GuestCalls();
// Room screen member pages (roomview.h): two calls to BuildPanelsHook, then UpdateVoiceIconsHook.
std::vector<CallSite> RoomViewCalls();
// HUiRoom::OnUpdate, replaced by RoomOnUpdateHook.
PointerSlot RoomViewSlot();
// Test mode ([RoomScreen] DummyMembers=1): every call to the room member list builder 7468C0, redirected to
// MemberListHook (fakemembers.h) so fake members reach every consumer of the list, not just the room screen.
std::vector<CallSite> FakeMemberCalls();

// Instructions at `rva` (`original`, at least 5 bytes, verified) replaced by a jump to a thunk that
// calls a handler (mission.h) and then runs original[displacedOffset, +displacedSize) before
// returning to rva + original.size(). Displaced bytes must be position-independent.
struct MidSite {
    const char* name;
    std::uint32_t rva;
    std::vector<std::uint8_t> original;
    std::size_t displacedOffset;
    std::size_t displacedSize;
};

// Rooms you host (hostmode.h): lobby capacity and published SEARCH_TYPE follow the 8Player MOD setting
// the room was created with (OFF: 4 and the vanilla value, ON: 8 and the mirrored value). Rooms you search
// for follow the current setting (OFF: normal and MultiSlot rooms, ON: MultiSlot rooms only).
std::vector<MidSite> HostModeHooks();
// HUiMainFrame::OnUpdate (the menu frame), replaced by MainFrameOnUpdateHook.
PointerSlot MainFrameSlot();
// HUiLobby::OnUpdate (the room list screen), replaced by LobbyOnUpdateHook: F2 there searches again.
PointerSlot LobbySlot();

// Mission phase for players 5-8 (applied with [Mission] Extend=1). With four or fewer players every
// changed instruction computes what the game computed: records and array entries 1-4 stay where the
// game reads them, only indices 4..7 reach the new storage.
std::vector<Patch> MissionPatches();
std::vector<MidSite> MissionHooks();
std::vector<CallSite> MissionCalls();
// Callback vtable slots replaced by mission handlers (mission.h, MissionSlotHandler).
std::vector<PointerSlot> MissionSlots();

// Enemy spawn counts for 5-8 players (spawn.h), applied with [Mission] Extend=1 and ExtraEnemies=1.
// Only online missions with more than four players get different counts.
std::vector<MidSite> SpawnHooks();

// "copy armor" (armor.h): the three reads of this machine's armor pickup count that publish it or create a
// player with it. With the feature off every one of them hands back the count the game holds.
std::vector<MidSite> ArmorHooks();

// Lobby join and room member diagnostics (joinlog.h), installed with NetLog=1. They only log.
std::vector<MidSite> DiagnosticHooks();
std::vector<CallSite> DiagnosticCalls();
// Experimental final-hello recovery, independently switchable with HandshakeRecovery=0.
std::vector<CallSite> RecoveryCalls();
// Always applied: a room of another MultiSlot family says so instead of "could not join" (versionmsg.h). The
// join check for invitations (709A30 -> 749AC0), the dialog text lookups (951BC1 -> 7A3500, 8EC4F5 -> 957EC0),
// the room list entry builder and the room list's join button.
std::vector<CallSite> VersionMessageCalls();
std::vector<MidSite> VersionMessageHooks();

// Solo test harness ([Test] GhostPlayers=N, needs Extend=1): when the online player count written by
// the mission sync is 1 (host alone), it becomes 1+N; players 2..N+1 are created as remote copies of
// the host (CreateOnlinePlayerObject looks up player 1) that nobody controls.
std::vector<MidSite> GhostHooks();
std::vector<CallSite> GhostCalls();

// [Sync] PositionEveryPacket: the packets this machine sends for its own player all carry the position,
// where the game puts it in a sixth of the frames and a 90 ms timer sends only the latest frame's packet.
// Sender side only; receivers need nothing new.
std::vector<Patch> PositionPatches();
// [Sync] FacingEveryPacket: likewise for bit 1, the angles that say which way the player faces.
std::vector<Patch> FacingPatches();

// [MultiSlot] JoinRetrySeconds: how long the game's join handshake (Link::OnInitial, 12D5AA0) waits for a
// member before it closes the connection and starts it over. The game sends a hello every 500 ms and
// restarts after ten of them (5 s). Takes the seconds; returns nothing for the game's own 5.
constexpr std::uint32_t kJoinRetrySite = 0x12D5CF2;
constexpr int kVanillaJoinRetrySeconds = 5;
constexpr int kMaxJoinRetrySeconds = 60;
std::vector<Patch> JoinRetryPatches(int seconds);

bool Matches(const std::uint8_t* at, const Patch& patch);
bool CallTargets(const std::uint8_t* at, std::uint32_t siteRva, std::uint32_t targetRva);
bool SlotTargets(const std::uint8_t* at, std::uint64_t imageBase, std::uint32_t targetRva);

}  // namespace multislot
