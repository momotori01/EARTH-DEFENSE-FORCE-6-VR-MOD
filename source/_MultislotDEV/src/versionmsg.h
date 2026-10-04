#pragma once
#include <cstdint>

#include "midhook.h"

namespace multislot {

// "This room is another MultiSlot version, update" instead of the game's bare "could not join the room" - and,
// from the room list, no join at all.
//
// A MultiSlot room publishes its family's SEARCH_TYPE (patches.h kSearchTypeCenter), and a family changes
// whenever something every machine simulates changes (1.6.0: enemy strength with 5-8 players). Families move
// down, and a search lists [lowest of its own family, vanilla]: an older build never lists a newer room, but a
// newer build lists every older one, and the room list's join button checks no SEARCH_TYPE at all (only
// invitations do, 749AC0 at 709A30): before 1.6.1 a newer build could join an older room from the list and
// play with enemies of a different strength. So the plugin
//  - notes each room list entry's SEARCH_TYPE as 73B230 decodes it into the RoomInfo (73C5AC),
//  - at the join button (8EC476) turns a room of another family into the game's own refusal path - the one
//    that refuses HARDEST/INFERNO before the final mission - by pointing that gate at a stand-in GameStatus
//    (not cleared) and RoomInfo (difficulty 3), and gives that dialog (Lobby_Join_Impossible, 8EC4F5) this text,
//  - for invitations watches the join check (709A30) and gives the OnlineError_RoomError dialog (951AB0's
//    lookup at 951BC1) the same text, once, within a minute.
// The text says whether the room is older or newer, in the game's language. Older builds cannot do any of
// this: someone on 1.5.38 or earlier still sees the old text when an invitation takes them to a newer room.

enum class RoomVersion { Vanilla, Same, Older, Newer, Other };
RoomVersion ClassifyRoom(std::uint64_t searchType);
// The text for a refused room of that kind, Japanese or English; nullptr for Vanilla, Same and Other.
const wchar_t* VersionMessage(RoomVersion version, bool japanese);
// Whether the game's own text is Japanese (kana or kanji in it).
bool IsJapaneseText(const wchar_t* text);

void InitVersionMessage(unsigned char* gameBase);
// Redirected calls (patches.h VersionMessageCalls) and hooks (VersionMessageHooks), by site.
void* VersionMessageCallHandler(std::uint32_t rva);
MidHandler VersionMessageHookHandler(std::uint32_t rva);
bool __fastcall JoinCheckHook(std::uint64_t searchType);
const wchar_t* __fastcall DialogTextHook(void* table, const wchar_t* key);
const wchar_t* __fastcall ListRefusalTextHook(const wchar_t* key);

// For the tests: stand-ins for 749AC0, 7A3500 and 957EC0, the clock, and what is noted.
using JoinCheckFn = bool(__fastcall*)(std::uint64_t);
using TextLookupFn = const wchar_t*(__fastcall*)(void*, const wchar_t*);
using KeyTextFn = const wchar_t*(__fastcall*)(const wchar_t*);
void SetVersionMessageOriginals(JoinCheckFn joinCheck, TextLookupFn textLookup, KeyTextFn keyText);
void SetVersionMessageClock(std::uint64_t (*now)());
RoomVersion PendingRoomVersion();
RoomVersion RoomFamilyOf(std::uint64_t roomInfo);

}  // namespace multislot
