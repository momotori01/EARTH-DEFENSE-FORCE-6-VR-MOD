#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include "roomview.h"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

#include "armor.h"
#include "identity.h"
#include "joinlog.h"
#include "log.h"
#include "mission.h"
#include "patches.h"

namespace multislot {
namespace {

// ui::HUiRoom fields (EDF.dll 678CCB46).
constexpr std::size_t kMembers = 0x900;          // {shared_ptr<MemberInfo>* data; capacity; size}
constexpr std::size_t kRebuild = 0x940;          // byte: OnUpdate rebuilds members and panels
constexpr std::size_t kChildA = 0xAF0;           // control blocks of the child screen / dialog
constexpr std::size_t kChildB = 0xB00;           //   handles OnUpdate drops when it rebuilds
constexpr std::uint32_t kPanelBuilder = 0x8F8020;
constexpr std::uint32_t kVoiceIcons = 0x903160;
constexpr std::uint32_t kRoomOnUpdate = 0x8FDFE0;
// Pad input: two slots of 0x4A0 bytes holding pointers to per-button channels; the channel's
// byte +0x4C is the button's current state (EDF+1182A00 writes it every frame).
constexpr std::uint32_t kPadSlots = 0x2136530;
// Armor probe (1.4.0, diagnostic only: it reads and logs, it changes nothing). HUiRoom::MemberInfo keeps
// one record per room member; 901100 fills it from the member list with the getters named here. GameStatus
// keeps the local player's own choice per split-screen slot, 0x3E60 apart.
constexpr std::size_t kMemberNameOk = 0x10;    // byte  the name below has been filled in
// std::wstring of the name the member plays under, SSO capacity 7 (constructor 8F1E80 builds it in place).
constexpr std::size_t kMemberName = 0x18;
constexpr std::size_t kMemberClassOk = 0x08;   // byte  (747340 read the source field)
constexpr std::size_t kMemberClass = 0x0C;     // int   soldier type
constexpr std::size_t kMemberArmorOk = 0x38;   // byte  (745D50)
constexpr std::size_t kMemberArmor = 0x3C;     // int   the number the room panel prints as _pl_Armor
constexpr std::size_t kMemberRatioOk = 0x40;   // byte  (745ED0)
constexpr std::size_t kMemberRatio = 0x44;     // float clear ratio
constexpr std::size_t kMemberPlaceOk = 0x48;   // byte  (746050)
constexpr std::size_t kMemberPlace = 0x4C;     // int   index into a name table
constexpr std::uint32_t kLocalPlayers = 0x6E90;    // GameStatus: class, then the armor field at +4
constexpr std::size_t kLocalPlayerStride = 0x3E60;
constexpr std::uint32_t kEffectiveArmor = 0x0D8700;  // (GameStatus, slot) -> the armor limit, or its default
constexpr std::size_t kPadSlotSize = 0x4A0;
constexpr std::size_t kChannelPressed = 0x4C;

struct Entry {
    void* object;
    void* control;
};
struct MemberVector {
    Entry* data;
    std::size_t capacity;
    std::size_t size;
};

struct ControlBlock {
    void** vtable;
    std::int32_t uses;
    std::int32_t weaks;
};

const unsigned char* game = nullptr;
RoomViewSettings config{};
RoomFunction originalBuilder = nullptr;
RoomFunction originalVoiceIcons = nullptr;
using OnUpdateFunction = std::uint64_t (*)(void*, void*);
OnUpdateFunction originalOnUpdate = nullptr;

// Everything below runs on the game's UI thread only (HUiRoom::OnUpdate and what it calls).
void* currentRoom = nullptr;
int currentPage = 0;
std::size_t dummyCount = 0;
bool padWasDown = false;
bool pageKeyWasDown[kMaxPageKeys]{};
bool addKeyWasDown = false;
bool removeKeyWasDown = false;
// For the menu label: when the room screen last updated and how many members it showed.
std::uint64_t lastUpdate = 0;
std::size_t lastShown = 0;
constexpr std::uint64_t kRoomScreenTimeout = 500;  // ms

MemberVector* Members(void* room) { return static_cast<MemberVector*>(static_cast<void*>(static_cast<char*>(room) + kMembers)); }

bool Alive(void* control) {
    return control && static_cast<ControlBlock*>(control)->uses > 0;
}

std::uint64_t CallWithMembers(RoomFunction original, void* room, Entry* data, std::size_t count) {
    MemberVector* members = Members(room);
    const MemberVector saved = *members;
    members->data = data;
    members->capacity = count;
    members->size = count;
    std::uint64_t result = 0;
    __try {
        result = original(room);
    } __finally {
        *members = saved;
    }
    return result;
}

void RequestRebuild(void* room) { static_cast<std::uint8_t*>(room)[kRebuild] = 1; }

// A child screen or dialog of the room is alive; rebuilding now would drop it. An unreadable
// field counts as open, so nothing is rebuilt.
bool ChildOpen(void* room) {
    __try {
        const auto* bytes = static_cast<const char*>(room);
        return Alive(*reinterpret_cast<void* const*>(bytes + kChildA)) || Alive(*reinterpret_cast<void* const*>(bytes + kChildB));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

bool PadDown(std::uint32_t channel) {
    if (!channel) return false;
    __try {
        for (std::size_t slot = 0; slot < 2; ++slot) {
            const auto* button = *reinterpret_cast<const std::uint8_t* const*>(game + kPadSlots + slot * kPadSlotSize + channel);
            if (button && button[kChannelPressed]) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

bool KeyDown(int key) {
    if (!key) return false;
    const HWND window = GetForegroundWindow();
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId() && (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool Edge(bool down, bool& was) {
    const bool pressed = down && !was;
    was = down;
    return pressed;
}

int lastLocalClass = -2, lastLocalArmor = -2;

const std::uint8_t* GameStatus() {
    if (!game) return nullptr;
    std::uint64_t status = 0;
    std::memcpy(&status, game + kGameStatusPointer, sizeof(status));
    return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(status));
}

int Field32(const std::uint8_t* at, std::size_t offset) {
    std::int32_t value = 0;
    std::memcpy(&value, at + offset, sizeof(value));
    return value;
}

// Diagnostic: what the game holds for this machine's own player. Logged once and again whenever it
// changes, so picking a different armor or class on the equipment screen shows up here.
void ProbeLocalArmor() {
    __try {
        const std::uint8_t* status = GameStatus();
        if (!status) return;
        const int soldier = Field32(status, kLocalPlayers);
        const int armor = Field32(status, kLocalPlayers + 4);
        if (soldier == lastLocalClass && armor == lastLocalArmor) return;
        lastLocalClass = soldier;
        lastLocalArmor = armor;
        const auto effective = reinterpret_cast<int(__fastcall*)(const void*, int)>(game + kEffectiveArmor);
        int ownClass = 0, pickups = 0, own = 0;
        const bool read = LocalArmor(ownClass, pickups, own);
        Log("ARMOR you: class %d, armor %d from %d pickups%s (armor limit setting %d, effective %d)", soldier,
            read ? own : -1, read ? pickups : -1, read ? "" : " - could not be read", armor, effective(status, 0));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

const char* SoldierName(int soldier) {
    switch (soldier) {
        case 0: return "Ranger";
        case 1: return "Wing Diver";
        case 2: return "Air Raider";
        case 3: return "Fencer";
        default: return "?";
    }
}

// The roster, written once whenever it changes. A member's name is the one thing in a log that says who
// the person is; the EOS ProductUserId on the user-slot lines says which line in another log is theirs.
char lastRoster[768]{};

void NoteRoster(const MemberVector* members, std::size_t count, const RoomMemberArmor* seen) {
    NoteUserSlots(count);  // Also runs when a pending user's arrival leaves the displayed roster unchanged.
    char roster[768]{};
    int used = 0;
    for (std::size_t i = 0; i < count && used >= 0; ++i) {
        const auto* member = static_cast<const std::uint8_t*>(members->data[i].object);
        char name[128]{};
        if (member && member[kMemberNameOk] != 0) NameText(member + kMemberName, name, sizeof(name));
        const int written =
            _snprintf_s(roster + used, sizeof(roster) - used, _TRUNCATE, "%s%zu %s %s %d", used ? ", " : "", i,
                        name[0] ? name : "(no name)", seen[i].valid ? SoldierName(seen[i].soldier) : "not chosen",
                        seen[i].valid ? seen[i].armor : 0);
        used = written < 0 ? -1 : used + written;
    }
    if (std::strcmp(roster, lastRoster) == 0) return;
    _snprintf_s(lastRoster, sizeof(lastRoster), _TRUNCATE, "%s", roster);
    Log("ROOM members (%zu): %s", count, roster);
}

// What "copy armor" needs from the room screen: the class and armor of every member, in panel order.
void NoteMembers(void* room) {
    __try {
        const MemberVector* members = Members(room);
        const std::size_t count = members->size;
        if (!members->data || count > static_cast<std::size_t>(kMaxPlayers)) {
            ForgetRoom();
            return;
        }
        RoomMemberArmor seen[kMaxPlayers]{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto* member = static_cast<const std::uint8_t*>(members->data[i].object);
            if (!member) continue;
            seen[i].valid = member[kMemberClassOk] != 0 && member[kMemberArmorOk] != 0;
            seen[i].soldier = Field32(member, kMemberClass);
            seen[i].armor = Field32(member, kMemberArmor);
        }
        NoteRoster(members, count, seen);
        NoteRoomMembers(seen, count);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ForgetRoom();
    }
}

void Poll(void* room) {
    if (room != currentRoom) {
        currentRoom = room;
        currentPage = 0;
        lastRoster[0] = 0;  // a different room writes its roster again
        ForgetUserSlots();
    }
    ProbeLocalArmor();
    NoteMembers(room);
    bool page = Edge(PadDown(config.padButton), padWasDown);
    for (std::size_t i = 0; i < kMaxPageKeys; ++i) page |= Edge(KeyDown(config.pageKeys[i]), pageKeyWasDown[i]);
    const bool add = config.dummies && Edge(KeyDown(config.dummyAddKey), addKeyWasDown);
    const bool remove = config.dummies && Edge(KeyDown(config.dummyRemoveKey), removeKeyWasDown);
    if (!page && !add && !remove) return;
    if (ChildOpen(room)) return;
    const std::size_t shown = Members(room)->size;
    if (add || remove) {
        const std::size_t previous = dummyCount;
        std::size_t next = dummyCount;
        if (add && shown < static_cast<std::size_t>(kMaxPlayers)) ++next;
        if (remove && next) --next;
        if (next != dummyCount) {
            dummyCount = next;
            RequestRebuild(room);
            // `shown` still holds the previous frame's list: say what the next rebuild will show.
            Log("ROOMVIEW fake members %zu (the room screen will show %zu)", dummyCount,
                shown >= previous ? shown - previous + dummyCount : dummyCount);
        }
    }
    const std::size_t total = shown;
    if (page && PageCount(total) > 1) {
        currentPage = NextPage(total, currentPage);
        RequestRebuild(room);
        const PageSlice slice = SliceForPage(total, currentPage);
        Log("ROOMVIEW page %d/%d shows members %zu-%zu of %zu", slice.page + 1, PageCount(total), slice.first + 1,
            slice.first + slice.count, total);
    }
}

std::uint64_t PagedCall(void* room, RoomFunction original) {
    const MemberVector* members = Members(room);
    if (members->size <= kPanelsPerPage) return original(room);  // the vanilla case
    if (members->size > 64 || !members->data) return original(room);  // not a member vector
    const PageSlice slice = SliceForPage(members->size, room == currentRoom ? currentPage : 0);
    if (room == currentRoom) currentPage = slice.page;
    return CallWithMembers(original, room, members->data + slice.first, slice.count);
}

}  // namespace

PageSlice SliceForPage(std::size_t total, int page) {
    const int pages = PageCount(total);
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    const std::size_t first = static_cast<std::size_t>(page) * kPanelsPerPage;
    const std::size_t left = total > first ? total - first : 0;
    return {first, left < kPanelsPerPage ? left : kPanelsPerPage, page};
}

int PageCount(std::size_t total) {
    return total <= kPanelsPerPage ? 1 : static_cast<int>((total + kPanelsPerPage - 1) / kPanelsPerPage);
}

int NextPage(std::size_t total, int page) {
    const int pages = PageCount(total);
    return pages <= 1 ? 0 : (page + 1) % pages;
}

int PadButtonOffset(const wchar_t* name) {
    struct Named {
        const wchar_t* name;
        int offset;
    };
    constexpr Named buttons[] = {
        {L"None", 0}, {L"RightStick", 0xB8}, {L"LeftStick", 0xB0}, {L"Back", 0x90}, {L"Start", 0x98},
        {L"X", 0x80},  {L"Y", 0x88},          {L"LB", 0xA0},        {L"RB", 0xA8},   {L"LT", 0x40},
        {L"RT", 0x48},
    };
    for (const auto& button : buttons)
        if (_wcsicmp(name, button.name) == 0) return button.offset;
    return -1;
}

const wchar_t* PadButtonShortName(std::uint32_t offset) {
    switch (offset) {
        case 0xB8: return L"RS";
        case 0xB0: return L"LS";
        case 0x90: return L"Back";
        case 0x98: return L"Start";
        case 0x80: return L"X";
        case 0x88: return L"Y";
        case 0xA0: return L"LB";
        case 0xA8: return L"RB";
        case 0x40: return L"LT";
        case 0x48: return L"RT";
        default: return L"";
    }
}

std::size_t KeyName(int key, wchar_t* out, std::size_t outChars) {
    if (!out || !outChars) return 0;
    int written = 0;
    if (key == VK_TAB) written = _snwprintf_s(out, outChars, _TRUNCATE, L"Tab");
    else if (key >= VK_F1 && key <= VK_F24) written = _snwprintf_s(out, outChars, _TRUNCATE, L"F%d", key - VK_F1 + 1);
    else written = _snwprintf_s(out, outChars, _TRUNCATE, L"%d", key);
    if (written < 0) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(written);
}

std::size_t ParseKeyList(const wchar_t* text, int* keys, std::size_t maxKeys, wchar_t* skipped, std::size_t skippedChars) {
    if (skipped && skippedChars) skipped[0] = 0;
    std::size_t count = 0, skippedUsed = 0;
    if (!text || !keys) return 0;
    const auto separator = [](wchar_t c) { return c == L',' || c == L' ' || c == L'\t' || c == L';'; };
    for (const wchar_t* at = text; *at;) {
        while (*at && separator(*at)) ++at;
        const wchar_t* start = at;
        while (*at && !separator(*at)) ++at;
        const std::size_t length = static_cast<std::size_t>(at - start);
        if (!length) continue;
        wchar_t name[16]{};
        int key = -1;
        if (length < 16) {
            wmemcpy(name, start, length);
            key = VirtualKey(name);
        }
        if (key == 0) continue;  // None
        if (key < 0 || key == kEightPlayerModKey) {
            if (skipped && skippedUsed + length + 2 < skippedChars) {
                if (skippedUsed) skipped[skippedUsed++] = L',';
                wmemcpy(skipped + skippedUsed, start, length);
                skippedUsed += length;
                skipped[skippedUsed] = 0;
            }
            continue;
        }
        bool seen = false;
        for (std::size_t i = 0; i < count; ++i) seen = seen || keys[i] == key;
        if (!seen && count < maxKeys) keys[count++] = key;
    }
    return count;
}

bool ResolveDummyKeys(RoomViewSettings& settings) {
    const auto taken = [&](int key, int other) {
        if (!key) return false;
        if (key == kEightPlayerModKey || key == other) return true;
        for (int page : settings.pageKeys)
            if (page && page == key) return true;
        return false;
    };
    bool changed = false;
    if (taken(settings.dummyAddKey, 0)) {
        settings.dummyAddKey = taken(VK_F6, 0) ? 0 : VK_F6;
        changed = true;
    }
    if (taken(settings.dummyRemoveKey, settings.dummyAddKey)) {
        settings.dummyRemoveKey = taken(VK_F7, settings.dummyAddKey) ? 0 : VK_F7;
        changed = true;
    }
    return changed;
}

void BuildPageHint(RoomViewSettings& settings) {
    wchar_t hint[32]{};
    std::size_t used = 0;
    const auto add = [&](const wchar_t* part) {
        const std::size_t length = wcslen(part);
        if (!length || used + length + 2 > 32) return;
        if (used) hint[used++] = L'/';
        wmemcpy(hint + used, part, length);
        used += length;
        hint[used] = 0;
    };
    for (int key : settings.pageKeys) {
        if (!key) continue;
        wchar_t name[8]{};
        KeyName(key, name, 8);
        add(name);
    }
    add(PadButtonShortName(settings.padButton));
    wmemcpy(settings.pageHint, hint, 32);
}

int VirtualKey(const wchar_t* name) {
    if (_wcsicmp(name, L"None") == 0) return 0;
    if (_wcsicmp(name, L"Tab") == 0) return VK_TAB;
    if ((name[0] == L'F' || name[0] == L'f') && name[1] >= L'1' && name[1] <= L'9') {
        wchar_t* end = nullptr;
        const long number = std::wcstol(name + 1, &end, 10);
        if (end && !*end && number >= 1 && number <= 24) return VK_F1 + static_cast<int>(number) - 1;
        return -1;
    }
    wchar_t* end = nullptr;
    const long code = std::wcstol(name, &end, 10);
    return (end && end != name && !*end && code > 0 && code < 256) ? static_cast<int>(code) : -1;
}

void InitRoomView(const unsigned char* gameBase, const RoomViewSettings& settings) {
    game = gameBase;
    config = settings;
    originalBuilder = reinterpret_cast<RoomFunction>(gameBase + kPanelBuilder);
    originalVoiceIcons = reinterpret_cast<RoomFunction>(gameBase + kVoiceIcons);
    originalOnUpdate = reinterpret_cast<OnUpdateFunction>(gameBase + kRoomOnUpdate);
}

std::uint64_t BuildPanelsHook(void* room) { return PagedCall(room, originalBuilder); }

std::uint64_t UpdateVoiceIconsHook(void* room) { return PagedCall(room, originalVoiceIcons); }

std::uint64_t RoomOnUpdateHook(void* room, void* context) {
    Poll(room);
    NoteRoomUpdate(room, GetTickCount64());
    return originalOnUpdate(room, context);
}

std::uint64_t CallWithPageView(void* room, RoomFunction original) { return PagedCall(room, original); }

void SetDummyCount(std::size_t count) { dummyCount = count; }

std::size_t DummyCount() { return dummyCount; }

void SetPage(void* room, int page) {
    currentRoom = room;
    currentPage = page;
}

void NoteRoomUpdate(void* room, std::uint64_t now) {
    const std::size_t limit = static_cast<std::size_t>(kMaxPlayers);
    std::size_t shown = Members(room)->size;  // fake members are part of the game's own list (fakemembers.h)
    if (shown > limit) shown = shown > 64 ? 0 : limit;  // not a member vector: show no page hint
    lastShown = shown;
    lastUpdate = now ? now : 1;
}

RoomPageView RoomPageAt(std::uint64_t now) {
    RoomPageView view{};
    view.active = lastUpdate && now >= lastUpdate && now - lastUpdate <= kRoomScreenTimeout;
    view.shown = lastShown;
    view.pages = PageCount(lastShown);
    view.page = currentPage < 0 ? 0 : (currentPage >= view.pages ? view.pages - 1 : currentPage);
    return view;
}

RoomPageView CurrentRoomPage() { return RoomPageAt(GetTickCount64()); }

const wchar_t* PageHint() { return config.pageHint; }

bool PadButtonDown(std::uint32_t channel) { return PadDown(channel); }

}  // namespace multislot
