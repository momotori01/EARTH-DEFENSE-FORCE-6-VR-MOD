#pragma once
#include <cstddef>
#include <cstdint>

#include "midhook.h"
#include "patches.h"
#include "roomview.h"

namespace multislot {

// "<n>Player MOD" (n = kModRoomCapacity): what kind of room you create as host.
//   OFF (default) - a normal 4-player room anyone can find and join, exactly as without the mod.
//   ON            - a MultiSlot room for kMaxPlayers that only players with this mod can find and join,
//                   and the room search lists MultiSlot rooms only (invitations still reach any room).
// F2 switches it on menu screens outside a room (saved to the INI). A room keeps the setting it was
// created with for as long as it exists. The menu frame (UI/LYT_MAINFRAME.SGO, which the plugin writes to
// Mods\UI with an extra text field MSLabel, menulayout.h) shows the label in its lower left corner. The
// main script plays that frame on the HQ, lobby and room screens but not in missions, so the label is gone
// once a mission starts. In a room it shows the room's setting to its host and, as a control guide, which
// keys switch the member page (see ComposeLabel).
constexpr int kModRoomCapacity = kMaxPlayers;

// iniPath may be null (nothing is saved). `key` and `padButton` switch the setting, which only happens
// outside a room; `hint` is what the label calls them ("F2/LS").
void InitHostMode(unsigned char* gameBase, const wchar_t* iniPath, bool eightPlayers, int key,
                  std::uint32_t padButton, const wchar_t* hint);
bool EightPlayerRooms();

// Mid-function hooks for HostModeHooks() in patches.h, by site RVA.
MidHandler HostModeHookHandler(std::uint32_t rva);
// HUiLobby::OnUpdate vtable slot (LobbySlot() in patches.h): the room list screen. F2 pressed while it is shown
// makes it search again with the new setting, as soon as no search is running and no dialog is open over it.
std::uint64_t LobbyOnUpdateHook(void* lobby, void* context);
// Test seam: whether the list may start a search now. searchActive/resultsIn are the search's bytes +0x43 (set when
// a search starts) and +0x40 (set when its results are in); dialogOpen is the lobby's pending dialog callback.
bool LobbyMaySearchAgain(std::uint8_t searchActive, std::uint8_t resultsIn, bool dialogOpen);
// HUiMainFrame::OnUpdate vtable slot (MainFrameSlot() in patches.h).
std::uint64_t MainFrameOnUpdateHook(void* frame, void* context);

// Test seams.
struct MenuContext {
    bool inRoom;               // an online room session exists
    bool roomHost;             // and this player hosts it
    RoomPageView pages;        // the room screen (roomview.h)
    const wchar_t* pageHint;   // "F3/Tab/RS"
    int ghosts;                // ghost players the solo test harness would add (0 = off or not installed)
    const wchar_t* copyArmorHint;  // "F3/RS" while copy armor is installed, nullptr or "" when it is not
    const wchar_t* hostModeHint;   // "F2/LS", what switches the setting outside a room
    int copyArmorTo;               // the armor copy armor is giving this player, 0 when it is giving none
    bool copyArmorAtMax;           // and that armor is this class's ceiling, not what was found in the room
};
// Long enough for the fullest line a room can show: the room's setting, the page guide and copy armor.
constexpr std::size_t kLabelChars = 96;
// Only what the player can do where they are. Outside a room: "F2/LS 8Player MOD :ON" / ":OFF", the setting for
// the rooms they create, which is the one thing there is to do there. In a room: the host also sees that room's
// own setting ("8Player MOD :ON"), which nothing can change now but nothing else reports; then the page guide
// "F3/Tab/RS: Members 5-8" (the page those inputs switch to) while more than four members are shown, and always
// in a MultiSlot room this player hosts; then "F4/LS copy armor :ON" / ":OFF". Nothing to show is a single space.
std::size_t ComposeLabel(const MenuContext& context, bool eightPlayers, bool roomEightPlayers, wchar_t* out,
                         std::size_t outChars);
// One menu frame update: the 8Player MOD input edge (down), then the label.
void UpdateMenuFrame(void* frame, bool down, const MenuContext& context);
bool RoomCreatedWithEightPlayers();

}  // namespace multislot
