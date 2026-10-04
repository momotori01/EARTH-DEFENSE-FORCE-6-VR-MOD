#include "versionmsg.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cwchar>

#include "joinlog.h"
#include "log.h"
#include "patches.h"

namespace multislot {
namespace {

constexpr std::uint64_t kLowestOurs = 2 * kSearchTypeCenter - 0x94;  // this family: kLowestOurs..+3
constexpr std::uint64_t kHighestOurs = 2 * kSearchTypeCenter - 0x91;
constexpr std::uint64_t kMessageLifeMs = 60000;  // a refusal explains the dialog that follows it, not a later one

JoinCheckFn joinCheck = nullptr;
TextLookupFn textLookup = nullptr;
KeyTextFn keyText = nullptr;
std::uint64_t (*nowMs)() = &GetTickCount64;

// What the next dialog of each kind is to say: an invitation's OnlineError_RoomError, or the room list's
// Lobby_Join_Impossible.
struct Pending {
    std::atomic<int> version{static_cast<int>(RoomVersion::Vanilla)};
    std::atomic<std::uint64_t> at{0};
    void Note(RoomVersion v) {
        const bool explained = v == RoomVersion::Older || v == RoomVersion::Newer;
        version.store(static_cast<int>(explained ? v : RoomVersion::Vanilla));
        at.store(explained ? nowMs() : 0);
    }
    // The noted version if it is still fresh, once.
    RoomVersion Take() {
        const auto v = static_cast<RoomVersion>(version.exchange(static_cast<int>(RoomVersion::Vanilla)));
        const std::uint64_t when = at.load();
        if ((v != RoomVersion::Older && v != RoomVersion::Newer) || !when || nowMs() - when > kMessageLifeMs)
            return RoomVersion::Vanilla;
        return v;
    }
};
Pending invite, list;

// The room list entries' SEARCH_TYPE family, by RoomInfo address: rebuilt with the list, so a reused address is
// overwritten when its new room is decoded.
struct Entry {
    std::uint64_t room = 0;
    RoomVersion version = RoomVersion::Vanilla;
};
constexpr int kEntries = 128;
SRWLOCK entriesLock = SRWLOCK_INIT;
Entry entries[kEntries];
int nextEntry = 0;

// The join button's gate reads these instead of GameStatus and the RoomInfo for a room of another family:
// "final mission not cleared" (+0x300 = 0) and "difficulty HARDEST" (+0x18 = 3), so it takes its refusal path.
alignas(16) unsigned char refusingStatus[0x308]{};
struct RefusingRoom {
    unsigned char bytes[0x20]{};
    RefusingRoom() { bytes[0x18] = 3; }
};
RefusingRoom refusingRoom;

// Same tone and line breaks as the game's own OnlineError_* texts (TEXTTABLE_STEAM.*.TXT_SGO).
constexpr const wchar_t* kOlderJa =
    L"MultiSlotのバージョンが違うため、\nルームに参加できませんでした。\n\n"
    L"このルームは古いバージョンで作られています。\nルームの全員でEDF6VRを最新版にアップデートしてください。";
constexpr const wchar_t* kNewerJa =
    L"MultiSlotのバージョンが違うため、\nルームに参加できませんでした。\n\n"
    L"このルームは新しいバージョンで作られています。\nEDF6VRを最新版にアップデートしてください。";
constexpr const wchar_t* kOlderEn =
    L"You were unable to join the room:\nit uses a different MultiSlot version.\n\n"
    L"This room was made with an older version.\nEveryone in the room needs to update EDF6VR to the latest version.";
constexpr const wchar_t* kNewerEn =
    L"You were unable to join the room:\nit uses a different MultiSlot version.\n\n"
    L"This room was made with a newer version.\nPlease update EDF6VR to the latest version.";

bool KeyIs(const wchar_t* key, const wchar_t* expected) {
    __try {
        return key && std::wcscmp(key, expected) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const char* Which(RoomVersion version) { return version == RoomVersion::Older ? "older" : "newer"; }

// 73C5AC: rbx = the room's SEARCH_TYPE (73C5A5 `mov ecx, ebx`), r14 = the RoomInfo being filled.
void RoomEntryHandler(CpuContext* context) {
    const auto version = ClassifyRoom(static_cast<std::uint32_t>(context->rbx));
    AcquireSRWLockExclusive(&entriesLock);
    Entry* slot = nullptr;
    for (auto& entry : entries)
        if (entry.room == context->r14) {
            slot = &entry;
            break;
        }
    if (!slot) {
        slot = &entries[nextEntry];
        nextEntry = (nextEntry + 1) % kEntries;
    }
    *slot = Entry{context->r14, version};
    ReleaseSRWLockExclusive(&entriesLock);
}

// 8EC476 `cmp byte [rcx+0x300], 0`: rcx = GameStatus, r14 = the RoomInfo whose join was pressed.
void JoinGateHandler(CpuContext* context) {
    if (DetailLog()) NoteJoinPressed(context);
    const auto version = RoomFamilyOf(context->r14);
    if (version != RoomVersion::Older && version != RoomVersion::Newer) return;
    list.Note(version);
    context->rcx = reinterpret_cast<std::uintptr_t>(refusingStatus);
    context->r14 = reinterpret_cast<std::uintptr_t>(refusingRoom.bytes);
    Log("LOBBY join refused from the room list: the room is a MultiSlot room of an %s version (SEARCH_TYPE family "
        "other than 0x%llX..0x%llX); the game's refusal dialog says so",
        Which(version), static_cast<unsigned long long>(kLowestOurs), static_cast<unsigned long long>(kHighestOurs));
}

}  // namespace

RoomVersion ClassifyRoom(std::uint64_t searchType) {
    if ((searchType & ~0xFull) == 0x90) return RoomVersion::Vanilla;
    if (searchType >= kLowestOurs && searchType <= kHighestOurs) return RoomVersion::Same;
    // Every MultiSlot family mirrors 0x91..0x94 below 0x90, and each new one sits below the last.
    if (searchType > kHighestOurs && searchType < 0x90) return RoomVersion::Older;
    if (searchType < kLowestOurs) return RoomVersion::Newer;
    return RoomVersion::Other;
}

const wchar_t* VersionMessage(RoomVersion version, bool japanese) {
    if (version == RoomVersion::Older) return japanese ? kOlderJa : kOlderEn;
    if (version == RoomVersion::Newer) return japanese ? kNewerJa : kNewerEn;
    return nullptr;
}

// Kana only: the Chinese tables (CN, SC) are all kanji, and every Japanese text these dialogs use has kana in it
// (checked against TEXTTABLE_STEAM.{JA,EN,CN,SC,KR}.TXT_SGO, 2026-10-04). Any other language gets the English text.
bool IsJapaneseText(const wchar_t* text) {
    if (!text) return false;
    __try {
        for (int i = 0; i < 512 && text[i]; ++i)
            if (text[i] >= 0x3040 && text[i] <= 0x30FF) return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

void InitVersionMessage(unsigned char* gameBase) {
    joinCheck = reinterpret_cast<JoinCheckFn>(gameBase + 0x749AC0);
    textLookup = reinterpret_cast<TextLookupFn>(gameBase + 0x7A3500);
    keyText = reinterpret_cast<KeyTextFn>(gameBase + 0x957EC0);
}

bool __fastcall JoinCheckHook(std::uint64_t searchType) {
    const bool accepted = joinCheck(searchType);
    if (DetailLog()) NoteSearchTypeCheck(searchType, accepted);
    const RoomVersion version = accepted ? RoomVersion::Vanilla : ClassifyRoom(searchType);
    invite.Note(version);
    if (version == RoomVersion::Older || version == RoomVersion::Newer)
        Log("LOBBY join refused: the room is MultiSlot family SEARCH_TYPE 0x%llX, %s than this one (0x%llX..0x%llX); "
            "the dialog will say so",
            static_cast<unsigned long long>(searchType), Which(version), static_cast<unsigned long long>(kLowestOurs),
            static_cast<unsigned long long>(kHighestOurs));
    return accepted;
}

const wchar_t* __fastcall DialogTextHook(void* table, const wchar_t* key) {
    const wchar_t* text = textLookup(table, key);
    if (!KeyIs(key, L"OnlineError_RoomError")) return text;
    const auto version = invite.Take();
    return version == RoomVersion::Vanilla ? text : VersionMessage(version, IsJapaneseText(text));
}

const wchar_t* __fastcall ListRefusalTextHook(const wchar_t* key) {
    const wchar_t* text = keyText(key);
    if (!KeyIs(key, L"Lobby_Join_Impossible")) return text;
    const auto version = list.Take();
    return version == RoomVersion::Vanilla ? text : VersionMessage(version, IsJapaneseText(text));
}

void* VersionMessageCallHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x709A30: return reinterpret_cast<void*>(&JoinCheckHook);
        case 0x951BC1: return reinterpret_cast<void*>(&DialogTextHook);
        case 0x8EC4F5: return reinterpret_cast<void*>(&ListRefusalTextHook);
        default: return nullptr;
    }
}

MidHandler VersionMessageHookHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x73C5AC: return &RoomEntryHandler;
        case 0x8EC476: return &JoinGateHandler;
        default: return nullptr;
    }
}

RoomVersion RoomFamilyOf(std::uint64_t roomInfo) {
    RoomVersion version = RoomVersion::Vanilla;
    AcquireSRWLockShared(&entriesLock);
    for (const auto& entry : entries)
        if (entry.room == roomInfo && roomInfo) {
            version = entry.version;
            break;
        }
    ReleaseSRWLockShared(&entriesLock);
    return version;
}

void SetVersionMessageOriginals(JoinCheckFn check, TextLookupFn lookup, KeyTextFn text) {
    joinCheck = check;
    textLookup = lookup;
    keyText = text;
}

void SetVersionMessageClock(std::uint64_t (*now)()) { nowMs = now ? now : &GetTickCount64; }

RoomVersion PendingRoomVersion() { return static_cast<RoomVersion>(invite.version.load()); }

}  // namespace multislot
