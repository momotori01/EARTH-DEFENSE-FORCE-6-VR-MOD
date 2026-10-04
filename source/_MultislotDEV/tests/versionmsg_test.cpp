// The version message (versionmsg.h): which rooms count as older or newer, the real join check (EDF.dll with
// the plugin's join check patch written into a private mapping) feeding the hook, and the dialog text it swaps.
//   VersionMessageTests EDF.dll
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "../src/patches.h"
#include "../src/versionmsg.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::uint64_t fakeNow = 1000;
std::uint64_t FakeNow() { return fakeNow; }

// 7A3500 stand-ins: the game's own texts for the keys that matter, in either language.
const wchar_t* __fastcall JapaneseTexts(void*, const wchar_t* key) {
    if (std::wcscmp(key, L"OnlineError_RoomError") == 0) return L"ルームに参加できませんでした。";
    if (std::wcscmp(key, L"OnlineError_RoomFull") == 0) return L"定員オーバーのため、\nルームに参加できませんでした。";
    return L"その他";
}
const wchar_t* __fastcall EnglishTexts(void*, const wchar_t* key) {
    if (std::wcscmp(key, L"OnlineError_RoomError") == 0) return L"You were unable to join the room.";
    return L"other";
}

// 957EC0 stand-ins (the global table, key only).
const wchar_t* __fastcall JapaneseKeyText(const wchar_t* key) {
    if (std::wcscmp(key, L"Lobby_Join_Impossible") == 0) return L"難易度HARDEST、INFERNOをプレイするためには、";
    return JapaneseTexts(nullptr, key);
}
const wchar_t* __fastcall EnglishKeyText(const wchar_t* key) {
    if (std::wcscmp(key, L"Lobby_Join_Impossible") == 0) return L"In order to play the HARDEST or INFERNO difficulties,";
    return EnglishTexts(nullptr, key);
}

bool Contains(const wchar_t* text, const wchar_t* part) { return text && std::wcsstr(text, part) != nullptr; }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: VersionMessageTests EDF.dll\n");
        return 2;
    }
    // Which rooms are which: vanilla 0x90..0x9F, this family 0x3C..0x3F (centre 0x68), older families above
    // it (1.6.0-1.6.5 0x44..0x47, 1.2.6-1.5.38 0x54..0x57, ... 0.2-0.4.1 0x8C..0x8F), newer ones below.
    Check(kSearchTypeCenter == 0x68, "this build is the 0x68 family");
    for (std::uint64_t v = 0x91; v <= 0x94; ++v) Check(ClassifyRoom(v) == RoomVersion::Vanilla, "vanilla rooms");
    for (std::uint64_t v = 0x3C; v <= 0x3F; ++v) Check(ClassifyRoom(v) == RoomVersion::Same, "this family");
    for (const std::uint64_t v : {0x44ull, 0x47ull, 0x54ull, 0x57ull, 0x5Cull, 0x64ull, 0x6Cull, 0x74ull, 0x7Cull, 0x8Cull,
                                  0x8Full, 0x40ull})
        Check(ClassifyRoom(v) == RoomVersion::Older, "older families sit above this one");
    for (const std::uint64_t v : {0x3Bull, 0x38ull, 0x34ull, 0x20ull})
        Check(ClassifyRoom(v) == RoomVersion::Newer, "newer families sit below it");
    Check(ClassifyRoom(0xA0) == RoomVersion::Other && ClassifyRoom(0x1234) == RoomVersion::Other, "anything else is not ours");
    Check(VersionMessage(RoomVersion::Same, true) == nullptr && VersionMessage(RoomVersion::Vanilla, false) == nullptr &&
              VersionMessage(RoomVersion::Other, true) == nullptr,
          "no message unless older or newer");
    Check(IsJapaneseText(L"ルームに参加できませんでした。") && !IsJapaneseText(L"You were unable to join the room.") &&
              !IsJapaneseText(nullptr),
          "the game's language is read off its own text");
    // The game's own OnlineError_RoomError in the Chinese and Korean tables: English for them, not Japanese.
    Check(!IsJapaneseText(L"無法參加房間。") && !IsJapaneseText(L"无法参加房间。") && !IsJapaneseText(L"방에 참가하지 못했습니다.") &&
              IsJapaneseText(L"難易度HARDEST、INFERNOをプレイするためには、"),
          "kanji alone is not Japanese (CN, SC), nor Hangul (KR)");

    // The real join check: EDF.dll mapped, the plugin's two join check patches written in.
    const HMODULE game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!game) {
        std::printf("FAIL: cannot map %ls\n", argv[1]);
        return 1;
    }
    auto* base = reinterpret_cast<unsigned char*>(game);
    int written = 0;
    for (const auto& patch : GuestPatches()) {
        if (patch.rva != 0x749AB1 && patch.rva != 0x749AC0) continue;
        Check(Matches(base + patch.rva, patch), "the join check is the game's before the patch");
        DWORD old = 0;
        VirtualProtect(base + patch.rva, patch.replacement.size(), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(base + patch.rva, patch.replacement.data(), patch.replacement.size());
        VirtualProtect(base + patch.rva, patch.replacement.size(), old, &old);
        ++written;
    }
    Check(written == 2, "both join check patches written");
    FlushInstructionCache(GetCurrentProcess(), base + 0x749AB1, 0x20);
    InitVersionMessage(base);
    SetVersionMessageOriginals(reinterpret_cast<JoinCheckFn>(base + 0x749AC0), &JapaneseTexts, &JapaneseKeyText);
    SetVersionMessageClock(&FakeNow);

    for (std::uint64_t v = 0x90; v <= 0x9F; ++v) Check(JoinCheckHook(v), "the real check takes vanilla rooms");
    for (std::uint64_t v = 0x3C; v <= 0x3F; ++v) Check(JoinCheckHook(v), "and this family");
    Check(PendingRoomVersion() == RoomVersion::Vanilla, "nothing waits after an accepted room");
    for (std::uint64_t v = 0x44; v <= 0x47; ++v) Check(!JoinCheckHook(v), "and refuses 1.6.0-1.6.5 rooms");
    for (std::uint64_t v = 0x54; v <= 0x57; ++v) Check(!JoinCheckHook(v), "and 1.2.6-1.5.38 rooms");
    Check(PendingRoomVersion() == RoomVersion::Older, "a refused older room is noted");

    // The dialog: only OnlineError_RoomError is swapped, once, in the game's language.
    Check(std::wcscmp(DialogTextHook(nullptr, L"OnlineError_RoomFull"), L"定員オーバーのため、\nルームに参加できませんでした。") == 0,
          "another dialog keeps its text");
    const wchar_t* older = DialogTextHook(nullptr, L"OnlineError_RoomError");
    Check(Contains(older, L"MultiSlotのバージョンが違うため") && Contains(older, L"古いバージョン") &&
              Contains(older, L"アップデートしてください"),
          "the refused older room's dialog says so, in Japanese");
    Check(std::wcscmp(DialogTextHook(nullptr, L"OnlineError_RoomError"), L"ルームに参加できませんでした。") == 0,
          "and only once");

    SetVersionMessageOriginals(reinterpret_cast<JoinCheckFn>(base + 0x749AC0), &EnglishTexts, &EnglishKeyText);
    Check(!JoinCheckHook(0x38) && PendingRoomVersion() == RoomVersion::Newer, "a newer family is refused and noted");
    const wchar_t* newer = DialogTextHook(nullptr, L"OnlineError_RoomError");
    Check(Contains(newer, L"newer version") && Contains(newer, L"Please update EDF6VR"), "in English when the game is");

    Check(!JoinCheckHook(0x55), "refused again");
    Check(JoinCheckHook(0x91), "then a room that is joined");
    Check(std::wcscmp(DialogTextHook(nullptr, L"OnlineError_RoomError"), L"You were unable to join the room.") == 0,
          "a later join clears the note: its failure is not called a version");

    Check(!JoinCheckHook(0x55), "refused once more");
    fakeNow += 60001;
    Check(std::wcscmp(DialogTextHook(nullptr, L"OnlineError_RoomError"), L"You were unable to join the room.") == 0,
          "a note more than a minute old is not used");
    Check(!JoinCheckHook(0xA0) && PendingRoomVersion() == RoomVersion::Vanilla, "a refused value that is not ours notes nothing");

    // The room list: entries are noted as 73B230 decodes them, the join button refuses another family through
    // the game's own HARDEST/INFERNO refusal, and that dialog gets the text.
    fakeNow += 1000;
    SetVersionMessageOriginals(reinterpret_cast<JoinCheckFn>(base + 0x749AC0), &JapaneseTexts, &JapaneseKeyText);
    const MidHandler entry = VersionMessageHookHandler(0x73C5AC), gate = VersionMessageHookHandler(0x8EC476);
    Check(entry && gate && !VersionMessageHookHandler(0x123) && VersionMessageCallHandler(0x709A30) &&
              VersionMessageCallHandler(0x951BC1) && VersionMessageCallHandler(0x8EC4F5) && !VersionMessageCallHandler(0x123),
          "a handler per site");
    alignas(16) static unsigned char status[0x400]{}, rooms[4][0x200]{};
    status[0x300] = 1;  // final mission cleared
    for (auto& room : rooms) room[0x18] = 1;  // NORMAL
    const std::uint64_t searchTypes[4] = {0x91, 0x3D, 0x45, 0x39};  // vanilla, this family, 1.6.5, a newer one
    for (int i = 0; i < 4; ++i) {
        CpuContext built{};
        built.rbx = searchTypes[i];
        built.r14 = reinterpret_cast<std::uintptr_t>(rooms[i]);
        entry(&built);
    }
    Check(RoomFamilyOf(reinterpret_cast<std::uintptr_t>(rooms[0])) == RoomVersion::Vanilla &&
              RoomFamilyOf(reinterpret_cast<std::uintptr_t>(rooms[1])) == RoomVersion::Same &&
              RoomFamilyOf(reinterpret_cast<std::uintptr_t>(rooms[2])) == RoomVersion::Older &&
              RoomFamilyOf(reinterpret_cast<std::uintptr_t>(rooms[3])) == RoomVersion::Newer && RoomFamilyOf(0x1234) == RoomVersion::Vanilla,
          "each entry's family is noted by its RoomInfo");
    const auto press = [&](int i) {
        CpuContext pressed{};
        pressed.rcx = reinterpret_cast<std::uintptr_t>(status);
        pressed.r14 = reinterpret_cast<std::uintptr_t>(rooms[i]);
        gate(&pressed);
        return pressed;
    };
    for (int i : {0, 1}) {
        const CpuContext pressed = press(i);
        Check(pressed.rcx == reinterpret_cast<std::uintptr_t>(status) && pressed.r14 == reinterpret_cast<std::uintptr_t>(rooms[i]),
              "a vanilla room and a room of this family join as before");
    }
    Check(std::wcscmp(ListRefusalTextHook(L"Lobby_Join_Impossible"), L"難易度HARDEST、INFERNOをプレイするためには、") == 0,
          "the HARDEST/INFERNO refusal keeps its text when it is the game's own");
    for (int i : {2, 3}) {
        const CpuContext pressed = press(i);
        const auto* gameStatus = reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(pressed.rcx));
        const auto* room = reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(pressed.r14));
        std::int32_t difficulty = 0;
        std::memcpy(&difficulty, room + 0x18, sizeof(difficulty));
        Check(gameStatus != status && gameStatus[0x300] == 0 && difficulty > 2,
              "another family: the gate reads 'not cleared' and 'difficulty above HARD', so it takes the refusal path");
        Check(status[0x300] == 1 && rooms[i][0x18] == 1, "the game's own GameStatus and RoomInfo are not written to");
        const wchar_t* text = ListRefusalTextHook(L"Lobby_Join_Impossible");
        Check(Contains(text, L"MultiSlotのバージョンが違うため") &&
                  Contains(text, i == 2 ? L"古いバージョン" : L"新しいバージョン"),
              "and its dialog says older or newer");
        Check(std::wcscmp(ListRefusalTextHook(L"Lobby_Join_Impossible"), L"難易度HARDEST、INFERNOをプレイするためには、") == 0,
              "once");
    }
    // A RoomInfo address reused by a room of this family is that room now.
    CpuContext rebuilt{};
    rebuilt.rbx = 0x3C;
    rebuilt.r14 = reinterpret_cast<std::uintptr_t>(rooms[2]);
    entry(&rebuilt);
    const CpuContext again = press(2);
    Check(again.r14 == reinterpret_cast<std::uintptr_t>(rooms[2]), "a reused entry follows its new room");
    Check(std::wcscmp(ListRefusalTextHook(L"OnlineError_RoomFull"), L"定員オーバーのため、\nルームに参加できませんでした。") == 0,
          "other keys through 957EC0 are the game's");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("version message verified against %ls\n", argv[1]);
    return 0;
}
