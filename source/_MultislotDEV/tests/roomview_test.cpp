// Room screen paging without the game: a fake HUiRoom member vector and fake "original" functions
// record what the panel builder would see for each page and with dummy members.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "../src/patches.h"
#include "../src/roomview.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

struct Entry {
    void* object;
    void* control;
};
struct MemberVector {
    Entry* data;
    std::size_t capacity;
    std::size_t size;
};

struct Seen {
    std::vector<Entry> entries;
    Entry* data = nullptr;
    std::size_t size = 0;
    int calls = 0;
};
Seen seen;

MemberVector* Members(void* room) { return static_cast<MemberVector*>(static_cast<void*>(static_cast<char*>(room) + 0x900)); }

std::uint64_t FakeOriginal(void* room) {
    const MemberVector* members = Members(room);
    seen.data = members->data;
    seen.size = members->size;
    seen.entries.assign(members->data, members->data + members->size);
    ++seen.calls;
    return 0x1234;
}

int profileCalls = 0;
std::uint64_t FakeProfile(void*, void*) {
    ++profileCalls;
    return 7;
}

std::wstring NameOf(void* object) {
    const auto* body = static_cast<const std::uint8_t*>(object);
    std::uint64_t length = 0;
    std::memcpy(&length, body + 0x28, sizeof(length));
    if (length > 7) return L"(heap)";
    return std::wstring(reinterpret_cast<const wchar_t*>(body + 0x18), length);
}

}  // namespace

int main() {
    // Pure page arithmetic.
    Check(PageCount(0) == 1 && PageCount(4) == 1 && PageCount(5) == 2 && PageCount(8) == 2 && PageCount(9) == 3, "page count");
    PageSlice slice = SliceForPage(6, 1);
    Check(slice.first == 4 && slice.count == 2 && slice.page == 1, "page 2 of 6 members shows members 5-6");
    slice = SliceForPage(6, 7);
    Check(slice.page == 1 && slice.first == 4, "page past the end is clamped");
    slice = SliceForPage(3, 1);
    Check(slice.page == 0 && slice.first == 0 && slice.count == 3, "four or fewer members stay on page 1");
    Check(NextPage(8, 0) == 1 && NextPage(8, 1) == 0 && NextPage(4, 0) == 0, "next page wraps");
    Check(PadButtonOffset(L"RightStick") == 0xB8 && PadButtonOffset(L"rt") == 0x48 && PadButtonOffset(L"None") == 0 &&
              PadButtonOffset(L"Triangle") == -1,
          "pad button names");
    Check(VirtualKey(L"F2") == VK_F2 && VirtualKey(L"f24") == VK_F24 && VirtualKey(L"Tab") == VK_TAB &&
              VirtualKey(L"None") == 0 && VirtualKey(L"65") == 65 && VirtualKey(L"F25") == -1 && VirtualKey(L"Q") == -1,
          "key names");
    // A fake room with six members whose objects look like MemberInfo (0xA8 bytes).
    static std::uint8_t gameBase[16];
    InitRoomView(gameBase, RoomViewSettings{});
    std::vector<std::uint8_t> room(0x1000, 0);
    std::vector<std::vector<std::uint8_t>> objects(6, std::vector<std::uint8_t>(0xA8, 0));
    std::vector<std::int32_t> controls(12, 1);
    std::vector<Entry> real;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        objects[i][0x0C] = static_cast<std::uint8_t>(i + 1);  // soldier type
        std::uint64_t heapLength = 20, heapCapacity = 31;     // a long name living on the heap
        std::memcpy(objects[i].data() + 0x28, &heapLength, 8);
        std::memcpy(objects[i].data() + 0x30, &heapCapacity, 8);
        std::memset(objects[i].data() + 0x98, 0xEE, 16);      // user shared_ptr
        real.push_back({objects[i].data(), &controls[i]});
    }
    MemberVector* members = Members(room.data());
    *members = {real.data(), real.size(), real.size()};
    const MemberVector original = *members;

    SetDummyCount(0);
    SetPage(room.data(), 0);
    Check(CallWithPageView(room.data(), &FakeOriginal) == 0x1234, "original's return value is passed through");
    Check(seen.size == 4 && seen.entries[0].object == objects[0].data() && seen.entries[3].object == objects[3].data(),
          "page 1 of 6 shows members 1-4");
    Check(members->data == original.data && members->size == original.size && members->capacity == original.capacity,
          "member vector restored after the call");
    SetPage(room.data(), 1);
    CallWithPageView(room.data(), &FakeOriginal);
    Check(seen.size == 2 && seen.entries[0].object == objects[4].data() && seen.entries[1].object == objects[5].data(),
          "page 2 of 6 shows members 5-6");

    // Four or fewer members and no dummies: the game's call sees its own vector.
    *members = {real.data(), 3, 3};
    SetPage(room.data(), 1);
    CallWithPageView(room.data(), &FakeOriginal);
    Check(seen.data == real.data() && seen.size == 3, "vanilla case is untouched");

    // Fake members are part of the game's own member list (fakemembers.h), so the page view only slices
    // what the list holds. A full room is kMaxPlayers members, four to a page.
    constexpr std::size_t kFullPages = (kMaxPlayers + kPanelsPerPage - 1) / kPanelsPerPage;
    constexpr std::size_t kLastPageCount = kMaxPlayers - (kFullPages - 1) * kPanelsPerPage;
    static_assert(kMaxPlayers > kPanelsPerPage, "a full room has to be more than one page");
    std::vector<Entry> full;
    std::vector<std::vector<std::uint8_t>> bodies(kMaxPlayers, std::vector<std::uint8_t>(0xA8, 0));
    std::vector<std::int32_t> fullControls(2 * kMaxPlayers, 1);
    for (std::size_t i = 0; i < bodies.size(); ++i) full.push_back({bodies[i].data(), &fullControls[i]});
    *members = {full.data(), full.size(), full.size()};
    SetPage(room.data(), 1);
    CallWithPageView(room.data(), &FakeOriginal);
    Check(seen.size == kPanelsPerPage && seen.entries[0].object == bodies[kPanelsPerPage].data() &&
              seen.entries[kPanelsPerPage - 1].object == bodies[2 * kPanelsPerPage - 1].data(),
          "page 2 of a full room shows members 5-8");
    SetPage(room.data(), static_cast<int>(kFullPages) - 1);
    CallWithPageView(room.data(), &FakeOriginal);
    Check(seen.size == kLastPageCount && seen.entries[0].object == bodies[(kFullPages - 1) * kPanelsPerPage].data(),
          "the last page shows the members left over");
    Check(members->data == full.data() && members->size == kMaxPlayers, "member vector restored after paging a full room");
    Check(DummyCount() == 0, "no fake members are asked for by default");
    SetDummyCount(3);
    Check(DummyCount() == 3, "the fake member count is what the harness adds to the list");
    SetDummyCount(0);

    // What the menu label reads: members shown, the page, and whether the screen is live.
    SetPage(room.data(), 1);
    NoteRoomUpdate(room.data(), 10000);
    RoomPageView view = RoomPageAt(10100);
    Check(view.active && view.shown == kMaxPlayers && view.pages == static_cast<int>(kFullPages) && view.page == 1,
          "room page view: a full room, page 2");
    Check(!RoomPageAt(10600).active, "room page view goes stale half a second after the last room screen update");
    *members = {real.data(), 3, 3};
    NoteRoomUpdate(room.data(), 20000);
    view = RoomPageAt(20000);
    Check(view.active && view.shown == 3 && view.pages == 1 && view.page == 0, "three members: one page, page clamped");
    *members = {real.data(), 200, 200};
    NoteRoomUpdate(room.data(), 30000);
    Check(RoomPageAt(30000).shown == 0, "a vector that is not one shows nothing");

    // Keys: the page key list, the dummy keys kept off F2 and the page keys, and the label's names for them.
    int keys[kMaxPageKeys]{};
    wchar_t skipped[32]{};
    Check(ParseKeyList(L"F3,Tab", keys, kMaxPageKeys, skipped, 32) == 2 && keys[0] == VK_F3 && keys[1] == VK_TAB && !skipped[0],
          "PageKeys=F3,Tab");
    std::memset(keys, 0, sizeof(keys));
    Check(ParseKeyList(L" f2 ; Q, None,F3 F3,,tab,F9,F10,F11", keys, kMaxPageKeys, skipped, 32) == 4 && keys[0] == VK_F3 &&
              keys[1] == VK_TAB && keys[2] == VK_F9 && keys[3] == VK_F10 && std::wcscmp(skipped, L"f2,Q") == 0,
          "F2 and unknown names are left out and listed; duplicates and None ignored; at most four");
    std::memset(keys, 0, sizeof(keys));
    Check(ParseKeyList(L"", keys, kMaxPageKeys, skipped, 32) == 0 && keys[0] == 0, "an empty list: pad only");
    Check(ParseKeyList(L"VeryLongKeyNameThatIsNotAKey", keys, kMaxPageKeys, skipped, 4) == 0 && skipped[0] == 0,
          "a skipped name that does not fit is dropped from the report, not overrun");
    RoomViewSettings keySettings;
    keySettings.dummyAddKey = VK_F3;     // the old defaults F3/F4 from an existing INI
    keySettings.dummyRemoveKey = VK_F4;
    Check(ResolveDummyKeys(keySettings) && keySettings.dummyAddKey == VK_F6 && keySettings.dummyRemoveKey == VK_F4,
          "a dummy key on a page key moves to its default");
    keySettings.dummyAddKey = VK_F2;
    keySettings.dummyRemoveKey = VK_F2;
    Check(ResolveDummyKeys(keySettings) && keySettings.dummyAddKey == VK_F6 && keySettings.dummyRemoveKey == VK_F7,
          "F2 is never a dummy key");
    keySettings.dummyAddKey = VK_F8;
    keySettings.dummyRemoveKey = VK_F8;
    Check(ResolveDummyKeys(keySettings) && keySettings.dummyAddKey == VK_F8 && keySettings.dummyRemoveKey == VK_F7,
          "add and remove never share a key");
    keySettings.pageKeys[2] = VK_F6;
    keySettings.pageKeys[3] = VK_F7;
    keySettings.dummyAddKey = VK_F3;
    keySettings.dummyRemoveKey = VK_TAB;
    Check(ResolveDummyKeys(keySettings) && keySettings.dummyAddKey == 0 && keySettings.dummyRemoveKey == 0,
          "no dummy key when the defaults are page keys too");
    RoomViewSettings defaults;
    Check(!ResolveDummyKeys(defaults) && defaults.dummyAddKey == VK_F6 && defaults.dummyRemoveKey == VK_F7, "defaults do not collide");
    BuildPageHint(defaults);
    Check(std::wcscmp(defaults.pageHint, L"F3/Tab/RS") == 0, "default page hint");
    RoomViewSettings padOnly;
    std::memset(padOnly.pageKeys, 0, sizeof(padOnly.pageKeys));
    padOnly.padButton = static_cast<std::uint32_t>(PadButtonOffset(L"LB"));
    BuildPageHint(padOnly);
    Check(std::wcscmp(padOnly.pageHint, L"LB") == 0, "pad-only page hint");
    RoomViewSettings none;
    std::memset(none.pageKeys, 0, sizeof(none.pageKeys));
    none.padButton = 0;
    BuildPageHint(none);
    Check(none.pageHint[0] == 0, "no inputs, no hint");
    wchar_t keyName[8]{};
    Check(KeyName(VK_F12, keyName, 8) == 3 && std::wcscmp(keyName, L"F12") == 0 && KeyName(65, keyName, 8) == 2 &&
              std::wcscmp(keyName, L"65") == 0,
          "key names for the label");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("room view paging verified\n");
    return 0;
}
