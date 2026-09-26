#pragma once
#include <cstddef>
#include <cstdint>

namespace multislot {

// The room screen (ui::HUiRoom) has four member panels: PlayersGroup holds one host panel and
// three member panels, and the panel builder reads its template name from a four-entry table,
// so a fifth room member makes it read past that table. Room view keeps at most four panels and
// shows the members of the current page in them; a button switches pages.
//
// Optional test mode: local dummy members, drawn only on this screen, so paging can be tried
// without five real players. Nothing about them is sent anywhere.

constexpr std::size_t kMaxPageKeys = 4;
constexpr int kEightPlayerModKey = 0x71;  // VK_F2 belongs to 8Player MOD (hostmode.h), never to the room screen

struct RoomViewSettings {
    std::uint32_t padButton = 0xB8;                    // input channel offset (see PadButtonOffset); 0 = none
    int pageKeys[kMaxPageKeys] = {0x72, 0x09, 0, 0};   // virtual keys VK_F3, VK_TAB; 0 = unused
    bool dummies = false;
    int dummyAddKey = 0x75;                            // VK_F6
    int dummyRemoveKey = 0x76;                         // VK_F7
    wchar_t pageHint[32] = L"F3/Tab/RS";              // the page inputs as the menu label names them
};

constexpr std::size_t kPanelsPerPage = 4;

struct PageSlice {
    std::size_t first;
    std::size_t count;
    int page;
};

// Which members page `page` shows; the page is clamped to the pages `total` members fill.
PageSlice SliceForPage(std::size_t total, int page);
int PageCount(std::size_t total);
int NextPage(std::size_t total, int page);

// A pad button held down now, by channel offset; false for 0. Readable from any thread the game's UI runs on.
bool PadButtonDown(std::uint32_t channel);
// "RightStick", "LeftStick", "Back", "Start", "X", "Y", "LB", "RB", "LT", "RT", "None".
// Returns the channel offset inside a pad input slot, 0 for "None", or -1 if unknown.
int PadButtonOffset(const wchar_t* name);
// Short name for the label: "RS", "LS", "Back", ... ; "" for None or an unknown offset.
const wchar_t* PadButtonShortName(std::uint32_t offset);
// "F1".."F24", "Tab", "None", or a decimal virtual-key code. Returns -1 if unknown.
int VirtualKey(const wchar_t* name);
// "F3", "Tab" or the decimal code, for the label.
std::size_t KeyName(int key, wchar_t* out, std::size_t outChars);
// "F3,Tab" (commas or spaces) -> at most maxKeys distinct keys; None is ignored. Unknown names and F2 (the
// 8Player MOD key) are left out and listed, comma separated, in `skipped`.
std::size_t ParseKeyList(const wchar_t* text, int* keys, std::size_t maxKeys, wchar_t* skipped, std::size_t skippedChars);
// A dummy key may be neither F2, a page key nor the other dummy key: it falls back to its default (F6 / F7),
// or to none when that is taken too. True when a key was changed.
bool ResolveDummyKeys(RoomViewSettings& settings);
// settings.pageHint = the page keys and pad button as the label shows them ("F3/Tab/RS").
void BuildPageHint(RoomViewSettings& settings);

// What the room screen shows, for the menu label. active: HUiRoom::OnUpdate ran within the last half second.
struct RoomPageView {
    bool active;
    std::size_t shown;  // members, including fake ones in test mode, at most kMaxPlayers
    int page;
    int pages;
};
RoomPageView CurrentRoomPage();
const wchar_t* PageHint();

void InitRoomView(const unsigned char* gameBase, const RoomViewSettings& settings);

// Installed as: the two calls to the panel builder EDF+8F8020, the call to the voice-chat icon
// updater EDF+903160, and HUiRoom's OnUpdate vtable slot (EDF+180AC48).
std::uint64_t BuildPanelsHook(void* room);
std::uint64_t UpdateVoiceIconsHook(void* room);
std::uint64_t RoomOnUpdateHook(void* room, void* context);

// Test seams: the same logic with the original functions supplied by the caller.
using RoomFunction = std::uint64_t (*)(void* room);
using CallbackFunction = std::uint64_t (*)(void* room, void* result);
std::uint64_t CallWithPageView(void* room, RoomFunction original);
// How many fake members the harness adds to the game's own member list (fakemembers.h), F6/F7 on the room screen.
void SetDummyCount(std::size_t count);
std::size_t DummyCount();
void SetPage(void* room, int page);
// What RoomOnUpdateHook records each frame, and the view as of `now` (GetTickCount64 milliseconds).
void NoteRoomUpdate(void* room, std::uint64_t now);
RoomPageView RoomPageAt(std::uint64_t now);

}  // namespace multislot
