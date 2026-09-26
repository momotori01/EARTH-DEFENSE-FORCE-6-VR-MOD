#pragma once
#include <cstddef>
#include <cstdint>

#include "midhook.h"

namespace multislot {

// Offsets inside the game this module relies on (EDF.dll 678CCB46); checked by the tests.
constexpr std::uint32_t kGameStatusPointer = 0x20B2890;  // GameStatus* global
constexpr std::uint32_t kLoadoutRecords = 0x14C78;       // GameStatus: 4 x 0xD4 online loadout records
constexpr std::size_t kLoadoutRecordSize = 0xD4;
constexpr std::uint32_t kMissionContextConstructor = 0x1D6200;
constexpr std::uint32_t kPlayerCountScale = 0xE18B0;      // (GameDataMgr*, difficulty, players) -> float
constexpr std::size_t kVanillaPlayerArray = 0x100;        // MissionContext::m_player_info_array (4 x 16)
constexpr std::size_t kMovedPlayerArray = 0x300;          // after the 0x2F8-byte object: kMaxPlayers x 16
constexpr std::size_t kPlayerEntrySize = 0x10;

constexpr std::uint32_t kCreateOnlinePlayer = 0x591130;    // (player index, transform, weapon mode) -> object
constexpr std::uint32_t kAreaFactorInitialize = 0x207870;  // EventFactor_ObjectAreaIn::Initialize
constexpr std::uint32_t kResultItems = 0x14FD4;            // GameStatus: 4 x NetGameStatus::Item (two ints)

// ghostPlayers > 0: solo test harness, see GhostHooks() in patches.h.
void InitMission(unsigned char* gameBase, int ghostPlayers = 0);
// The game's own "is this an online session" test (7748F0).
bool OnlineSession();
// Players of the current mission as the mission sync stored them (GameStatus+0x14FF8; 1 + ghosts alone).
int MissionPlayers();

// Value that replaces `index * 0xD4` where the game addresses GameStatus+0x14C78 + index*0xD4:
// players 1-4 keep the game's records, 5 and up get one sidecar each, anything else a scratch record.
std::uint64_t LoadoutRecordOffset(std::int64_t index);
const std::uint8_t* LoadoutSidecar(int index);  // nullptr outside 4..kMaxPlayers-1

// Mid-function hook handlers and call redirections for the tables in patches.h, by site RVA.
MidHandler MissionHookHandler(std::uint32_t rva);
void* MissionCallHandler(std::uint32_t rva);
void* MissionSlotHandler(std::uint32_t rva);
std::uint64_t SidecarItem(int index);  // item of result position 4..kMaxPlayers-1, 0 otherwise
MidHandler GhostHookHandler(std::uint32_t rva);
void* GhostCallHandler(std::uint32_t rva);
int ActiveGhosts();
// Solo test harness: how many ghost players the next mission started alone gets, and the next count in the
// cycle the ghost key steps through (0, then four up, so a mission has 1 or 5..kMaxPlayers players). Only
// meaningful while the harness is installed (GhostHarness()).
int GhostPlayers();
void SetGhostPlayers(int count);
int NextGhostCount(int count);
bool GhostHarness();
// The player index this machine controls, from the remote flags CreatePlayers hands its create loop, or -1
// before a mission (and for a machine that controls none). Online every player, this one included, is built
// from the synced loadout records, so this is the only thing that says which record is ours (armor.h).
int LocalPlayerIndex();

}  // namespace multislot
