// Host mode against a fake game: the room create/update handlers with both settings, the menu label
// choice, and the menu frame update with stand-ins for the three HUiLayout/HUiTextField functions it calls
// (component index by name, text component by index, set text) reached through jumps at their RVAs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "../src/hostmode.h"
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

std::uint64_t Address(const void* p) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(p)); }

constexpr std::uint32_t kComponentIndex = 0x839600;
constexpr std::uint32_t kTextComponent = 0x8859C0;
constexpr std::uint32_t kSetText = 0x863690;

struct FakeControl;
struct FakeControlVtable {
    void(__fastcall* destroy)(FakeControl*);
    void(__fastcall* deleteThis)(FakeControl*);
};
struct FakeControl {
    FakeControlVtable* vtable;
    long uses;
    long weaks;
};
int destroyed = 0;
void __fastcall FakeDestroy(FakeControl*) { ++destroyed; }
void __fastcall FakeDelete(FakeControl*) {}
FakeControlVtable controlVtable{&FakeDestroy, &FakeDelete};
FakeControl fieldControl{&controlVtable, 1, 1};
int fieldObject = 0;

struct ShortName {
    wchar_t text[8];
    std::uint64_t size;
    std::uint64_t capacity;
};
struct FakeShared {
    void* object;
    FakeControl* control;
};

int indexResult = 3;
int indexCalls = 0;
std::wstring lookedUp;
void* lookupFrame = nullptr;
int __fastcall FakeIndex(void* frame, const ShortName* name) {
    ++indexCalls;
    lookupFrame = frame;
    lookedUp.assign(name->text, static_cast<std::size_t>(name->size));
    return indexResult;
}

int requestedIndex = -1;
FakeShared* __fastcall FakeTextComponent(void*, FakeShared* out, int index) {
    requestedIndex = index;
    InterlockedIncrement(&fieldControl.uses);
    out->object = &fieldObject;
    out->control = &fieldControl;
    return out;
}

int setTextCalls = 0;
std::wstring shown;
void __fastcall FakeSetText(void* field, const wchar_t* text) {
    if (field == &fieldObject) {
        ++setTextCalls;
        shown = text;
    }
}

void JumpTo(unsigned char* at, void* target) {
    const unsigned char stub[] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};  // mov rax, imm64; jmp rax
    std::memcpy(at, stub, sizeof(stub));
    const std::uint64_t address = Address(target);
    std::memcpy(at + 2, &address, 8);
}

std::uint64_t Stack64(const std::uint8_t* at) {
    std::uint64_t value = 0;
    std::memcpy(&value, at, 8);
    return value;
}

MenuContext Menu(bool inRoom, bool host, std::size_t members = 0, int page = 0, bool active = true) {
    MenuContext context{};
    context.inRoom = inRoom;
    context.roomHost = host;
    context.pages.active = active && members > 0;
    context.pages.shown = members;
    context.pages.pages = PageCount(members);
    context.pages.page = page;
    context.pageHint = L"F3/Tab/RS";
    context.hostModeHint = L"F2/LS";
    return context;
}

// The label names the room size, so every expected text follows kModRoomCapacity.
std::wstring Label(const wchar_t* tail) { return std::to_wstring(kModRoomCapacity) + L"Player MOD" + tail; }

std::wstring Compose(const MenuContext& context, bool eightPlayers, bool roomEightPlayers) {
    wchar_t text[kLabelChars]{};
    ComposeLabel(context, eightPlayers, roomEightPlayers, text, kLabelChars);
    return text;
}

}  // namespace

int lobbyUpdates = 0, lobbyRefreshes = 0;
void* refreshedLobby = nullptr;
std::uint64_t __fastcall FakeLobbyUpdate(void*, void*) { ++lobbyUpdates; return 0x1234; }
void __fastcall FakeLobbyRefresh(void* lobby) { ++lobbyRefreshes; refreshedLobby = lobby; }

int main() {
    const std::size_t imageSize = 0x900000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    Check(image != nullptr, "fake image");
    if (!image) return 1;
    JumpTo(image + kComponentIndex, reinterpret_cast<void*>(&FakeIndex));
    JumpTo(image + kTextComponent, reinterpret_cast<void*>(&FakeTextComponent));
    JumpTo(image + kSetText, reinterpret_cast<void*>(&FakeSetText));
    JumpTo(image + 0x8ECBB0, reinterpret_cast<void*>(&FakeLobbyUpdate));
    JumpTo(image + 0x8EDBC0, reinterpret_cast<void*>(&FakeLobbyRefresh));

    for (const auto& hook : HostModeHooks()) Check(HostModeHookHandler(hook.rva) != nullptr, hook.name);
    Check(HostModeHookHandler(0x1234) == nullptr, "unknown site has no handler");

    // OFF: normal rooms, byte for byte what the game stores (Steam lobby first, then the EOS lobby).
    InitHostMode(image, nullptr, false, VK_F2, 0xB0, L"F2/LS");
    std::uint8_t frameMemory[0x100]{};
    CpuContext steam{};
    steam.r8 = 0xDEAD;
    HostModeHookHandler(0x7435F7)(&steam);
    Check(steam.r8 == 4, "OFF: Steam lobby created for 4");
    CpuContext create{};
    create.rbp = Address(frameMemory + 0x80);
    std::uint64_t four = 4;
    std::memcpy(frameMemory + 0x20, &four, 8);
    HostModeHookHandler(0x742A9D)(&create);
    Check(Stack64(frameMemory + 0x20) == 4 && !RoomCreatedWithEightPlayers(), "OFF: lobby created for 4");
    CpuContext update{};
    update.rdx = 0xDEAD;
    HostModeHookHandler(0x749C91)(&update);
    Check(update.rdx == 4, "OFF: room update keeps 4 slots");
    const std::pair<std::uint32_t, std::uint64_t> kinds[] = {{0x749CBF, 0x93}, {0x749CC6, 0x92}, {0x749CCD, 0x94}, {0x749CD4, 0x91}};
    for (const auto& kind : kinds) {
        CpuContext publish{};
        HostModeHookHandler(kind.first)(&publish);
        Check(publish.rbx == kind.second, "OFF: vanilla SEARCH_TYPE is published");
    }
    const std::pair<std::uint32_t, std::uint64_t> ranges[] = {{0x74AC69, 0x93}, {0x74AC74, 0x92}, {0x74AC7F, 0x94}, {0x74AC8A, 0x91}};
    for (const auto& range : ranges) {
        CpuContext search{};
        HostModeHookHandler(range.first)(&search);
        Check(search.rax == ((range.second << 32) | (2 * kSearchTypeCenter - range.second)),
              "OFF: the room search lists normal and MultiSlot rooms of the kind");
    }

    // ON: every slot in both lobbies and the mirrored family; the room keeps its setting after F2.
    InitHostMode(image, nullptr, true, VK_F2, 0xB0, L"F2/LS");
    for (const auto& range : ranges) {
        CpuContext search{};
        HostModeHookHandler(range.first)(&search);
        const std::uint64_t low = search.rax & 0xFFFFFFFFull, high = search.rax >> 32;
        Check(low == 2 * kSearchTypeCenter - range.second && high == 2 * kSearchTypeCenter - 0x91 && high < 0x91 && low <= high,
              "ON: the room search lists MultiSlot rooms of the kind only");
    }
    steam.r8 = 0xDEAD;
    HostModeHookHandler(0x7435F7)(&steam);
    Check(steam.r8 == kModRoomCapacity && RoomCreatedWithEightPlayers(), "ON: Steam lobby created for the mod capacity");
    HostModeHookHandler(0x742A9D)(&create);
    Check(Stack64(frameMemory + 0x20) == kModRoomCapacity && RoomCreatedWithEightPlayers(), "ON: EOS lobby created for the mod capacity");
    for (const auto& kind : kinds) {
        CpuContext publish{};
        HostModeHookHandler(kind.first)(&publish);
        Check(publish.rbx == 2 * kSearchTypeCenter - kind.second && publish.rbx >= 2 * kSearchTypeCenter - 0x94 &&
                  publish.rbx <= 2 * kSearchTypeCenter - 0x91,
              "ON: mirrored SEARCH_TYPE is published");
    }
    const MenuContext outside = Menu(false, false);
    UpdateMenuFrame(nullptr, false, outside);
    UpdateMenuFrame(nullptr, true, outside);  // F2 outside a room: OFF
    Check(!EightPlayerRooms() && RoomCreatedWithEightPlayers(), "F2 changes the setting, not the room that exists");
    CpuContext searchAfterF2{};
    HostModeHookHandler(0x74AC8A)(&searchAfterF2);
    Check(searchAfterF2.rax == ((0x91ull << 32) | (2 * kSearchTypeCenter - 0x91)), "the room search follows F2 at once");
    HostModeHookHandler(0x749C91)(&update);
    Check(update.rdx == kModRoomCapacity, "an existing MultiSlot room keeps its slots after F2");
    UpdateMenuFrame(nullptr, false, outside);
    HostModeHookHandler(0x742A9D)(&create);
    Check(Stack64(frameMemory + 0x20) == 4 && !RoomCreatedWithEightPlayers(), "the next room follows the new setting");

    // One room creation: the Steam lobby step decides, and the EOS lobby made right after it agrees even if F2
    // was pressed in between; an EOS creation with no Steam step before it reads the setting itself.
    UpdateMenuFrame(nullptr, true, outside);  // ON
    UpdateMenuFrame(nullptr, false, outside);
    HostModeHookHandler(0x7435F7)(&steam);
    UpdateMenuFrame(nullptr, true, outside);  // OFF between the two lobbies
    UpdateMenuFrame(nullptr, false, outside);
    HostModeHookHandler(0x742A9D)(&create);
    Check(steam.r8 == kModRoomCapacity && Stack64(frameMemory + 0x20) == kModRoomCapacity && RoomCreatedWithEightPlayers(),
          "Steam and EOS lobbies of one room get the same capacity");
    HostModeHookHandler(0x742A9D)(&create);
    Check(Stack64(frameMemory + 0x20) == 4 && !RoomCreatedWithEightPlayers(), "a lobby without a Steam step uses the current setting");

    // F2 on the room list searches again: only while the list is on screen, only when no search is running and no
    // dialog is open, and once.
    Check(LobbyMaySearchAgain(0, 0, false) && LobbyMaySearchAgain(1, 1, false) && !LobbyMaySearchAgain(1, 0, false) &&
              !LobbyMaySearchAgain(0, 0, true),
          "the list searches again only when idle and without a dialog");
    {
        InitHostMode(image, nullptr, false, VK_F2, 0xB0, L"F2/LS");
        std::vector<std::uint8_t> lobby(0x1000, 0);
        lobbyUpdates = lobbyRefreshes = 0;
        UpdateMenuFrame(nullptr, false, outside);
        UpdateMenuFrame(nullptr, true, outside);  // F2 with no room list on screen
        UpdateMenuFrame(nullptr, false, outside);
        Check(LobbyOnUpdateHook(lobby.data(), nullptr) == 0x1234 && lobbyUpdates == 1 && lobbyRefreshes == 0,
              "F2 away from the room list asks for no search; the list's own update still runs");
        UpdateMenuFrame(nullptr, true, outside);  // F2 while the list is shown
        UpdateMenuFrame(nullptr, false, outside);
        lobby[0x7E0 + 0x43] = 1;                  // a search is running
        LobbyOnUpdateHook(lobby.data(), nullptr);
        Check(lobbyRefreshes == 0, "no second search while one is running");
        lobby[0x7E0 + 0x40] = 1;                  // its results are in
        void* pending = &lobby;                   // but a dialog is open
        std::memcpy(lobby.data() + 0x118 + 0x38, &pending, sizeof(pending));
        LobbyOnUpdateHook(lobby.data(), nullptr);
        Check(lobbyRefreshes == 0, "no search while a dialog is open over the list");
        std::memset(lobby.data() + 0x118 + 0x38, 0, sizeof(pending));
        LobbyOnUpdateHook(lobby.data(), nullptr);
        Check(lobbyRefreshes == 1 && refreshedLobby == lobby.data(), "F2 on the list searches again once it may");
        LobbyOnUpdateHook(lobby.data(), nullptr);
        Check(lobbyRefreshes == 1, "and only once");
    }
    InitHostMode(image, nullptr, false, VK_F2, 0xB0, L"F2/LS");

    // Label texts.
    Check(Compose(outside, false, true) == L"F2/LS " + Label(L" :OFF") && Compose(outside, true, false) == L"F2/LS " + Label(L" :ON"),
          "outside a room: the F2 setting");
    Check(Compose(Menu(true, true, 3), false, true) == Label(L" :ON   F3/Tab/RS: Members 5-8"),
          "hosting a MultiSlot room: its setting and the page guide, before anyone is on page 2");
    Check(Compose(Menu(true, true, 3), true, false) == Label(L" :OFF"), "hosting a normal room: no page guide");
    Check(Compose(Menu(true, true, 3, 0, false), false, true) == Label(L" :ON"), "no page guide off the room screen");
    Check(Compose(Menu(true, false, 3), true, true) == L" ",
          "a guest with nothing to do here sees nothing");
    Check(Compose(Menu(true, true, 6, 0), false, true) == Label(L" :ON   F3/Tab/RS: Members 5-6"), "host, page 1 of 6 members");
    Check(Compose(Menu(true, true, 8, 1), false, true) == Label(L" :ON   F3/Tab/RS: Members 1-4"), "host, page 2 of eight members");
    Check(Compose(Menu(true, false, 5, 0), false, true) == L"F3/Tab/RS: Member 5", "guest, page 1 of 5 members");
    Check(Compose(Menu(true, false, 8, 0, false), false, true) == L" ",
          "no page hint while the room screen is not updating");
    MenuContext noInputs = Menu(true, false, 8, 0);
    noInputs.pageHint = L"";
    Check(Compose(noInputs, false, true) == L" ", "no page hint without page inputs");
    // The copy armor guide follows whatever else the label carries, and is absent while it is not installed.
    MenuContext copyingGuest = Menu(true, false, 5, 0);
    copyingGuest.copyArmorHint = L"F4/LS";
    Check(Compose(copyingGuest, false, true) == L"F3/Tab/RS: Member 5   F4/LS copy armor :OFF",
          "in a room, the pages and copy armor");
    // The room panel keeps the real armor, so the number being given is shown here, with (Max) when it is
    // this class's ceiling rather than what was found in the room.
    copyingGuest.copyArmorTo = 2000;
    Check(Compose(copyingGuest, false, true) == L"F3/Tab/RS: Member 5   F4/LS copy armor :2000",
          "the armor being given is shown");
    copyingGuest.copyArmorAtMax = true;
    Check(Compose(copyingGuest, false, true) == L"F3/Tab/RS: Member 5   F4/LS copy armor :2000(Max)",
          "and says so when it is the ceiling");
    copyingGuest.copyArmorTo = 0;
    copyingGuest.copyArmorAtMax = false;
    copyingGuest.copyArmorHint = L"LS";
    Check(Compose(copyingGuest, false, true) == L"F3/Tab/RS: Member 5   LS copy armor :OFF",
          "a pad button alone still names itself");
    copyingGuest.copyArmorHint = L"";
    Check(Compose(copyingGuest, false, true) == L"F3/Tab/RS: Member 5", "nothing installed, nothing shown");
    MenuContext copyingHost = Menu(true, true, 3);
    copyingHost.copyArmorHint = L"F4/LS";
    Check(Compose(copyingHost, false, true) == Label(L" :ON   F3/Tab/RS: Members 5-8") + L"   F4/LS copy armor :OFF",
          "the host also sees the room's own setting");
    MenuContext copyingOutside = Menu(false, false);
    copyingOutside.copyArmorHint = L"F4/LS";
    Check(Compose(copyingOutside, true, false) == L"F2/LS " + Label(L" :ON"),
          "outside a room there is nothing to copy from, so the guide stays away");
    wchar_t tiny[8]{};
    Check(ComposeLabel(Menu(true, true, 8, 0), false, true, tiny, 8) == 7 && tiny[7] == 0, "a short buffer is truncated, not overrun");

    // Menu frame updates write the label through the game's functions, once per change.
    wchar_t iniPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, iniPath);
    wcscat_s(iniPath, L"multislot_hostmode_test.ini");
    DeleteFileW(iniPath);
    InitHostMode(image, iniPath, false, VK_F2, 0xB0, L"F2/LS");
    int frameA = 0, frameB = 0;
    UpdateMenuFrame(&frameA, false, outside);
    Check(indexCalls == 1 && lookedUp == L"MSLabel" && lookupFrame == &frameA && requestedIndex == 3, "label field looked up by name");
    Check(setTextCalls == 1 && shown == L"F2/LS " + Label(L" :OFF"), "OFF label shown");
    Check(fieldControl.uses == 1 && destroyed == 0, "the component reference is returned");
    UpdateMenuFrame(&frameA, false, outside);
    Check(setTextCalls == 1, "unchanged label is not rewritten every frame");
    UpdateMenuFrame(&frameA, true, outside);
    Check(EightPlayerRooms() && setTextCalls == 2 && shown == L"F2/LS " + Label(L" :ON"), "F2 turns it on and the label follows");
    Check(GetPrivateProfileIntW(L"MultiSlot", L"EightPlayerRooms", -1, iniPath) == 1, "the setting is saved");
    UpdateMenuFrame(&frameA, true, outside);
    Check(EightPlayerRooms(), "holding F2 switches once");
    UpdateMenuFrame(&frameA, false, Menu(true, true, 2));
    UpdateMenuFrame(&frameA, true, Menu(true, true, 2));
    Check(EightPlayerRooms() && shown == Label(L" :OFF"), "in a room F2 does nothing; the host sees the room's setting");
    UpdateMenuFrame(&frameA, false, Menu(true, true, 5, 0));
    Check(shown == Label(L" :OFF   F3/Tab/RS: Member 5"), "the page hint appears with the fifth member");
    UpdateMenuFrame(&frameA, false, Menu(true, true, 5, 1));
    Check(shown == Label(L" :OFF   F3/Tab/RS: Members 1-4"), "and follows the page");
    UpdateMenuFrame(&frameA, false, Menu(true, false, 2));
    Check(shown == L" ", "a guest with four or fewer members has nothing to do here");
    UpdateMenuFrame(&frameB, false, outside);
    Check(lookupFrame == &frameB && shown == L"F2/LS " + Label(L" :ON"), "another frame gets its own label");
    const int before = setTextCalls;
    for (int i = 0; i < 130; ++i) UpdateMenuFrame(&frameB, false, outside);
    Check(setTextCalls == before + 1, "the label is re-applied every 120 updates (frames can be rebuilt at the same address)");
    UpdateMenuFrame(&frameB, true, outside);
    Check(!EightPlayerRooms() && GetPrivateProfileIntW(L"MultiSlot", L"EightPlayerRooms", -1, iniPath) == 0, "F2 again: OFF, saved");
    indexResult = -1;
    const int calls = setTextCalls;
    int frameC = 0;
    UpdateMenuFrame(&frameC, false, outside);
    Check(setTextCalls == calls, "a frame without MSLabel is left alone");
    Check(fieldControl.uses == 1 && destroyed == 0, "no reference leaked or dropped");
    DeleteFileW(iniPath);

    VirtualFree(image, 0, MEM_RELEASE);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("host mode and menu label verified\n");
    return 0;
}
