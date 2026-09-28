#include "hostmode.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstring>
#include <cwchar>

#include "armor.h"
#include "updatecheck.h"
#include "identity.h"
#include "log.h"
#include "mission.h"
#include "patches.h"

namespace multislot {
namespace {

// Offsets inside the game this module relies on (EDF.dll 678CCB46); checked by the tests.
constexpr std::uint32_t kSessionHolder = 0x20B2AC0;    // session holder; object at holder-0x98, session at +0xC0
constexpr std::uint32_t kOnlineManager = 0x20B2AC8;
constexpr std::uint32_t kIsRoomHost = 0x787370;         // IsRoomHost(): (online manager) -> bool
constexpr std::uint32_t kMainFrameOnUpdate = 0x8C12B0;  // HUiMainFrame::OnUpdate(this, context)
constexpr std::uint32_t kLobbyOnUpdate = 0x8ECBB0;      // HUiLobby::OnUpdate(this, context), the room list screen
constexpr std::uint32_t kLobbyRefresh = 0x8EDBC0;       // HUiLobby: shows "Lobby_Refreshing" and starts a new room search
constexpr std::size_t kLobbySearch = 0x7E0;             // HUiLobby: its room search (8EDD28, started by 73A6B0)
constexpr std::size_t kSearchActive = 0x43;             // search: set when one starts (73A852)
constexpr std::size_t kSearchResultsIn = 0x40;          // search: cleared then (73A85F), set when the results are in (73AEB7)
constexpr std::size_t kLobbyDialogCallback = 0x118 + 0x38;  // HUiLobby: the std::function its dialogs run on closing (8F0087)
constexpr std::uint32_t kComponentIndex = 0x839600;     // HUiLayout: (this, const std::wstring& name) -> index or -1
constexpr std::uint32_t kTextComponent = 0x8859C0;      // HUiLayout: (this, shared_ptr<HUiTextField>* out, index)
constexpr std::uint32_t kSetText = 0x863690;            // HUiTextField: (this, const wchar_t*)
constexpr int kRefreshUpdates = 120;                    // re-apply the label every ~2 s (frames can be recreated)

unsigned char* game = nullptr;
wchar_t iniFile[MAX_PATH]{};
std::atomic<bool> eightPlayers{false};
std::atomic<bool> roomEightPlayers{false};
std::atomic<bool> steamLobbyCaptured{false};  // the Steam lobby step took the setting for the room being created
std::atomic<bool> labelShownLogged{false};

// Everything below the hooks runs on the game's UI thread (HUiMainFrame::OnUpdate).
bool keyWasDown = false;

struct FrameState {
    void* frame = nullptr;
    bool written = false;
    wchar_t label[kLabelChars]{};
    int updates = 0;
};
FrameState frames[4];
int nextFrame = 0;

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
struct SharedPtr {
    void* object;
    ControlBlock* control;
};
// MSVC std::wstring holding at most 7 characters in place.
struct ShortWideString {
    wchar_t text[8];
    std::uint64_t size;
    std::uint64_t capacity;
};

void Release(SharedPtr& pointer) {
    ControlBlock* control = pointer.control;
    pointer = {};
    if (control && InterlockedDecrement(&control->uses) == 0) {
        control->vtable->destroy(control);
        if (InterlockedDecrement(&control->weaks) == 0) control->vtable->deleteThis(control);
    }
}

bool InRoomSession() {
    __try {
        std::uint64_t holder = 0;
        std::memcpy(&holder, game + kSessionHolder, sizeof(holder));
        if (!holder) return false;
        return *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(holder - 0x98 + 0xC0)) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool RoomHost() {
    std::uint64_t manager = 0;
    std::memcpy(&manager, game + kOnlineManager, sizeof(manager));
    using IsRoomHostFn = bool(__fastcall*)(void*);
    return reinterpret_cast<IsRoomHostFn>(game + kIsRoomHost)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(manager)));
}

// Test harness key (mission.h): steps the ghost player count while the menu frame is up, so a mission
// started alone can be tried with 5, 6, 7 and 8 players without restarting the game.
constexpr int kGhostKey = 0x77;  // VK_F8
bool ghostKeyWasDown = false;
bool copyArmorKeyWasDown = false;
int hostModeKey = 0;
std::uint32_t hostModePad = 0;
wchar_t hostModeHint[24]{};

bool KeyDown(int key) {
    const HWND window = GetForegroundWindow();
    if (!window) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId() && (GetAsyncKeyState(key) & 0x8000) != 0;
}

// Writes the label into the frame's MSLabel field; false when this layout has none.
bool ShowLabel(void* frame, const wchar_t* text) {
    ShortWideString name{L"MSLabel", 7, 7};  // on the stack like the game's own lookups (8C0A6A)
    using IndexFn = int(__fastcall*)(void*, const ShortWideString*);
    using TextComponentFn = SharedPtr*(__fastcall*)(void*, SharedPtr*, int);
    using SetTextFn = void(__fastcall*)(void*, const wchar_t*);
    const int index = reinterpret_cast<IndexFn>(game + kComponentIndex)(frame, &name);
    if (index < 0) return false;
    SharedPtr field{};
    reinterpret_cast<TextComponentFn>(game + kTextComponent)(frame, &field, index);
    const bool shown = field.object != nullptr;
    if (shown) reinterpret_cast<SetTextFn>(game + kSetText)(field.object, text);
    Release(field);
    return shown;
}

FrameState& StateFor(void* frame) {
    for (auto& state : frames)
        if (state.frame == frame) return state;
    FrameState& state = frames[nextFrame];
    nextFrame = (nextFrame + 1) % 4;
    state = FrameState{};
    state.frame = frame;
    return state;
}

void Save(bool on) {
    if (iniFile[0]) WritePrivateProfileStringW(L"MultiSlot", L"EightPlayerRooms", on ? L"1" : L"0", iniFile);
}

// Room creation first makes the Steam lobby that joiners enter before the EOS lobby (7435F7 `mov r8d, 4`,
// cMaxMembers of ISteamMatchmaking::CreateLobby): the room takes the setting here, and the EOS lobby created
// next uses the same value even if F2 is pressed in between.
void SteamCreateCapacityHandler(CpuContext* context) {
    const bool on = eightPlayers.load();
    roomEightPlayers.store(on);
    steamLobbyCaptured.store(true);
    context->r8 = on ? kModRoomCapacity : kVanillaPlayers;
}

// Lobby create options (742A9D `mov qword [rbp-0x60], 4`, MaxLobbyMembers). Without a Steam lobby step before
// it (not seen on Steam, but kept safe), the room takes the setting now.
void CreateCapacityHandler(CpuContext* context) {
    const bool on = steamLobbyCaptured.exchange(false) ? roomEightPlayers.load() : eightPlayers.load();
    roomEightPlayers.store(on);
    const std::uint64_t capacity = on ? kModRoomCapacity : kVanillaPlayers;
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(context->rbp - 0x60)), &capacity, sizeof(capacity));
    if (on)
        Log("HOST creating a MultiSlot room for %d players (%dPlayer MOD ON; Steam and EOS lobbies for %d)",
            kModRoomCapacity, kModRoomCapacity, kModRoomCapacity);
    else
        Log("HOST creating a normal %d-player room (%dPlayer MOD OFF)", kVanillaPlayers, kModRoomCapacity);
}

// Room update (749C91 `mov edx, 4` before SetMaxMembers).
void UpdateCapacityHandler(CpuContext* context) {
    context->rdx = roomEightPlayers.load() ? kModRoomCapacity : kVanillaPlayers;
}

// Room update: the SEARCH_TYPE published for the room kind (`mov ebx, 0x9x`), mirrored in MultiSlot rooms.
template <std::uint32_t Vanilla>
void SearchTypeHandler(CpuContext* context) {
    context->rbx = roomEightPlayers.load() ? 2 * kSearchTypeCenter - Vanilla : Vanilla;
}

// Room search (74AC50 `movabs rax, (high << 32) | 0x91`): the SEARCH_TYPE range asked for per room kind, from
// the setting as it is now. OFF lists normal and MultiSlot rooms of the kind, [mirror(high), high]; ON lists
// MultiSlot rooms only, [mirror(high), mirror(0x91)]: someone set to play with eight is shown only rooms that
// take eight (the user's request, 2026-09-25). Invitations and joins by id do not search, so they still
// reach any room the join check accepts.
std::atomic<int> searchLogged{-1};

// F2 on the room list (the user's request, 2026-09-25): the list shown was searched with the old setting, so it
// searches again. Only when the list was on screen at the press (its update ran within kLobbyVisibleMs), and only
// once it may: the game's search start (73A6B0) has no guard of its own and replaces a search still running.
constexpr std::uint64_t kLobbyVisibleMs = 250, kSearchAgainMs = 10000;
std::atomic<std::uint64_t> lobbySeenAt{0};    // the room list's last update (GetTickCount64)
std::atomic<std::uint64_t> searchAgainAt{0};  // when F2 asked for a new search there, 0 when nothing is asked

// 1: the list may search now, 0: not yet (a search is running or a dialog is open), -1: not readable.
int LobbyMaySearch(const unsigned char* lobby) {
    __try {
        const unsigned char* search = lobby + kLobbySearch;
        void* dialog = nullptr;
        std::memcpy(&dialog, lobby + kLobbyDialogCallback, sizeof(dialog));
        return LobbyMaySearchAgain(search[kSearchActive], search[kSearchResultsIn], dialog != nullptr) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
template <std::uint32_t High>
void SearchRangeHandler(CpuContext* context) {
    const bool on = eightPlayers.load();
    const std::uint64_t high = on ? 2 * kSearchTypeCenter - 0x91 : High;
    context->rax = (high << 32) | (2 * kSearchTypeCenter - High);
    const int mode = on ? 1 : 0;
    if (searchLogged.exchange(mode) != mode)
        Log(on ? "SEARCH %dPlayer MOD ON: the room list shows MultiSlot rooms only"
               : "SEARCH %dPlayer MOD OFF: the room list shows normal and MultiSlot rooms",
            kModRoomCapacity);
}

}  // namespace

void InitHostMode(unsigned char* gameBase, const wchar_t* iniPath, bool on, int key, std::uint32_t padButton,
                  const wchar_t* hint) {
    hostModeKey = key;
    hostModePad = padButton;
    hostModeHint[0] = 0;
    if (hint) {
        const std::size_t length = wcsnlen(hint, 24);
        if (length < 24) wmemcpy(hostModeHint, hint, length + 1);
    }
    game = gameBase;
    iniFile[0] = 0;
    if (iniPath && wcslen(iniPath) < MAX_PATH) wmemcpy(iniFile, iniPath, wcslen(iniPath) + 1);
    eightPlayers.store(on);
    roomEightPlayers.store(false);
    steamLobbyCaptured.store(false);
    searchLogged.store(-1);
    lobbySeenAt.store(0);
    searchAgainAt.store(0);
    keyWasDown = false;
    for (auto& state : frames) state = FrameState{};
}

bool EightPlayerRooms() { return eightPlayers.load(); }

bool LobbyMaySearchAgain(std::uint8_t searchActive, std::uint8_t resultsIn, bool dialogOpen) {
    return !dialogOpen && (!searchActive || resultsIn);
}

std::uint64_t LobbyOnUpdateHook(void* lobby, void* context) {
    using OnUpdateFn = std::uint64_t(__fastcall*)(void*, void*);
    const std::uint64_t result = reinterpret_cast<OnUpdateFn>(game + kLobbyOnUpdate)(lobby, context);
    const std::uint64_t now = GetTickCount64();
    lobbySeenAt.store(now);
    const std::uint64_t wanted = searchAgainAt.load();
    if (!wanted || !lobby) return result;
    if (now - wanted > kSearchAgainMs) {
        searchAgainAt.store(0);
        Log("SEARCH the room list stayed busy for %llu s after F2; not searched again", kSearchAgainMs / 1000);
        return result;
    }
    const int may = LobbyMaySearch(static_cast<const unsigned char*>(lobby));
    if (may < 0) {
        searchAgainAt.store(0);
        Log("SEARCH the room list could not be read; not searched again");
        return result;
    }
    if (!may) return result;
    searchAgainAt.store(0);
    using RefreshFn = void(__fastcall*)(void*);
    reinterpret_cast<RefreshFn>(game + kLobbyRefresh)(lobby);
    Log("SEARCH the room list searched again after F2 (%dPlayer MOD %s)", kModRoomCapacity, eightPlayers.load() ? "ON" : "OFF");
    return result;
}
bool RoomCreatedWithEightPlayers() { return roomEightPlayers.load(); }

std::size_t ComposeLabel(const MenuContext& context, bool on, bool roomOn, wchar_t* out, std::size_t outChars) {
    if (!out || !outChars) return 0;
    out[0] = 0;
    // _TRUNCATE: a text that does not fit is cut off (and still terminated).
    if (!context.inRoom) {
        _snwprintf_s(out, outChars, _TRUNCATE, L"%ls%s%dPlayer MOD :%s",
                     context.hostModeHint ? context.hostModeHint : L"",
                     context.hostModeHint && context.hostModeHint[0] ? L" " : L"", kModRoomCapacity,
                     on ? L"ON" : L"OFF");
    } else {
        // The page guide: while the room screen shows more than four members, and always in a MultiSlot room
        // this player hosts (there it names the second page before anyone fills it).
        wchar_t pages[40]{};
        const RoomPageView& view = context.pages;
        const bool guide = view.pages > 1 || (context.roomHost && roomOn);
        if (view.active && guide && context.pageHint && context.pageHint[0]) {
            std::size_t first = kPanelsPerPage + 1, last = kPanelsPerPage * 2;
            if (view.pages > 1) {
                const int next = (view.page + 1) % view.pages;
                first = static_cast<std::size_t>(next) * kPanelsPerPage + 1;
                last = first + kPanelsPerPage - 1 < view.shown ? first + kPanelsPerPage - 1 : view.shown;
            }
            if (last > first)
                _snwprintf_s(pages, _TRUNCATE, L"%s: Members %zu-%zu", context.pageHint, first, last);
            else
                _snwprintf_s(pages, _TRUNCATE, L"%s: Member %zu", context.pageHint, first);
        }
        // The host sees the room's own setting, which nothing here can change but is worth knowing. A guest
        // sees only what the inputs do here: the pages, and copy armor after them.
        if (context.roomHost)
            _snwprintf_s(out, outChars, _TRUNCATE, L"%dPlayer MOD :%s%s%s", kModRoomCapacity, roomOn ? L"ON" : L"OFF",
                         pages[0] ? L"   " : L"", pages);
        else
            _snwprintf_s(out, outChars, _TRUNCATE, L"%s", pages);
    }
    // "copy armor" (armor.h): only in a room, where there is something to copy from and where its inputs work.
    if (context.inRoom && context.copyArmorHint && context.copyArmorHint[0]) {
        // The room panel keeps this player's real armor, so the number they are being given goes here.
        wchar_t copy[48]{};
        if (context.copyArmorTo > 0)
            _snwprintf_s(copy, _TRUNCATE, L"%s%ls copy armor :%d%s", out[0] ? L"   " : L"", context.copyArmorHint,
                         context.copyArmorTo, context.copyArmorAtMax ? L"(Max)" : L"");
        else
            _snwprintf_s(copy, _TRUNCATE, L"%s%ls copy armor :%s", out[0] ? L"   " : L"", context.copyArmorHint,
                         CopyArmor() ? L"ON" : L"OFF");
        wcsncat_s(out, outChars, copy, _TRUNCATE);
    }
    // Last on purpose. The field holds 96 characters and the guides above tell the player what the
    // buttons do right now; if something has to be cut it should be this, so the notice is appended with
    // _TRUNCATE after everything else rather than competing with it.
    if (const wchar_t* update = UpdateNotice(); update && update[0]) {
        wchar_t line[kLabelChars]{};
        _snwprintf_s(line, _TRUNCATE, L"%s%ls", out[0] ? L"   " : L"", update);
        wcsncat_s(out, outChars, line, _TRUNCATE);
    }
    // Test harness: the players a mission started alone would have (hidden while it is off).
    if (context.ghosts > 0) {
        wchar_t ghosts[24]{};
        _snwprintf_s(ghosts, _TRUNCATE, L"%sF8 Ghosts :%d", out[0] ? L"   " : L"", context.ghosts);
        wcsncat_s(out, outChars, ghosts, _TRUNCATE);
    }
    // An empty text field keeps no text object, so a space stands for nothing.
    if (!out[0]) _snwprintf_s(out, outChars, _TRUNCATE, L" ");
    return wcslen(out);
}

void UpdateMenuFrame(void* frame, bool keyDown, const MenuContext& context) {
    const bool pressed = keyDown && !keyWasDown;
    keyWasDown = keyDown;
    if (pressed && !context.inRoom) {
        const bool on = !eightPlayers.load();
        eightPlayers.store(on);
        Save(on);
        Log("MENU F2: 8Player MOD %s (%s)", on ? "ON - rooms you create are MultiSlot rooms for 8 players"
                                               : "OFF - rooms you create are normal 4-player rooms",
            iniFile[0] ? "saved" : "not saved");
        const std::uint64_t now = GetTickCount64();
        if (now - lobbySeenAt.load() < kLobbyVisibleMs) {
            searchAgainAt.store(now);
            Log("MENU F2 on the room list: it searches again for %s", on ? "MultiSlot rooms only" : "normal and MultiSlot rooms");
        }
    }
    if (!frame) return;
    wchar_t label[kLabelChars]{};
    ComposeLabel(context, eightPlayers.load(), roomEightPlayers.load(), label, kLabelChars);
    FrameState& state = StateFor(frame);
    if (state.written && std::wcscmp(state.label, label) == 0 && ++state.updates < kRefreshUpdates) return;
    state.updates = 0;
    state.written = true;
    std::wmemcpy(state.label, label, kLabelChars);
    // The title (lyt_SlotFrame) and result (lyt_ResultFrame) screens are HUiMainFrames too and have no MSLabel,
    // so a frame without one is normal; the log only records that the label reached a menu frame.
    if (ShowLabel(frame, label) && !labelShownLogged.exchange(true)) Log("MENU label shown: \"%ls\"", label);
}

MidHandler HostModeHookHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x7435F7: return &SteamCreateCapacityHandler;
        case 0x742A9D: return &CreateCapacityHandler;
        case 0x749C91: return &UpdateCapacityHandler;
        case 0x749CBF: return &SearchTypeHandler<0x93>;
        case 0x749CC6: return &SearchTypeHandler<0x92>;
        case 0x749CCD: return &SearchTypeHandler<0x94>;
        case 0x749CD4: return &SearchTypeHandler<0x91>;
        case 0x74AC69: return &SearchRangeHandler<0x93>;
        case 0x74AC74: return &SearchRangeHandler<0x92>;
        case 0x74AC7F: return &SearchRangeHandler<0x94>;
        case 0x74AC8A: return &SearchRangeHandler<0x91>;
        default: return nullptr;
    }
}

std::uint64_t MainFrameOnUpdateHook(void* frame, void* context) {
    using OnUpdateFn = std::uint64_t(__fastcall*)(void*, void*);
    const std::uint64_t result = reinterpret_cast<OnUpdateFn>(game + kMainFrameOnUpdate)(frame, context);
    PollIdentity();  // the plugin loads before the game signs in, so this is asked from here until it answers
    if (CopyArmorKey() || CopyArmorPad()) {
        const bool down = (KeyDown(CopyArmorKey()) || PadButtonDown(CopyArmorPad())) && InRoomSession();
        if (down && !copyArmorKeyWasDown) SetCopyArmor(!CopyArmor());
        copyArmorKeyWasDown = down;
    }
    if (GhostHarness()) {
        const bool down = KeyDown(kGhostKey);
        if (down && !ghostKeyWasDown) {
            const int next = NextGhostCount(GhostPlayers());
            SetGhostPlayers(next);
            Log("MENU F8: ghost players %d (a mission started alone in your own room gets %d player(s))", next, next + 1);
        }
        ghostKeyWasDown = down;
    }
    MenuContext menu{};
    menu.inRoom = InRoomSession();
    // Out of a room there is nobody to copy from, and an offline mission must not keep the last room's
    // armor: the room screen is the only thing that recomputes it, and it stops running when the room goes.
    if (!menu.inRoom) ForgetRoom();
    menu.roomHost = menu.inRoom && RoomHost();
    menu.pages = CurrentRoomPage();
    menu.pageHint = PageHint();
    menu.ghosts = GhostHarness() ? GhostPlayers() : 0;
    // Only what can be done here: the setting is switched outside a room, copy armor inside one. ComposeLabel
    // leaves each out where it does not apply.
    menu.copyArmorHint = CopyArmorHint();
    menu.copyArmorTo = CopyArmorTo();
    menu.copyArmorAtMax = CopyArmorAtMax();
    menu.hostModeHint = hostModeHint;
    UpdateMenuFrame(frame, KeyDown(hostModeKey) || PadButtonDown(hostModePad), menu);
    return result;
}

}  // namespace multislot
