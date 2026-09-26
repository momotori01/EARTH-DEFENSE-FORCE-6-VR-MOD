// The fake member harness (fakemembers.h): how many copies of the first member it appends to the room's
// member list, and that it leaves the list alone when it cannot.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../src/fakemembers.h"
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

// Stand-ins for the game's own list helpers (EDF+748E70 and EDF+749040), with the same contract.
std::vector<std::vector<RoomMember>> buffers;
int reserveCalls = 0;
bool reserveFails = false;

std::uint8_t __fastcall FakeReserve(RoomMemberList* list, std::uint64_t capacity) {
    ++reserveCalls;
    if (reserveFails) return 0;
    buffers.emplace_back(static_cast<std::size_t>(capacity), RoomMember{nullptr, nullptr});
    std::vector<RoomMember>& buffer = buffers.back();
    const std::size_t moved = list->size < capacity ? static_cast<std::size_t>(list->size) : static_cast<std::size_t>(capacity);
    for (std::size_t i = 0; i < moved; ++i) buffer[i] = list->data[i];
    list->data = buffer.data();
    list->capacity = capacity;
    return 1;
}

void __fastcall FakeResize(RoomMemberList* list, std::uint64_t size, const RoomMember* value) {
    for (std::size_t i = static_cast<std::size_t>(list->size); i < static_cast<std::size_t>(size); ++i) {
        list->data[i] = *value;
        if (value->control) ++*static_cast<std::int32_t*>(value->control);  // the game counts the reference
    }
    list->size = size;
}

struct Room {
    std::vector<RoomMember> entries;
    std::vector<std::int32_t> uses;
    RoomMemberList list{};

    explicit Room(std::size_t members, std::size_t capacity = 0) : entries(capacity ? capacity : members), uses(members, 1) {
        for (std::size_t i = 0; i < members; ++i) entries[i] = {&uses[i], &uses[i]};
        list = {reinterpret_cast<void*>(0x1234), entries.data(), entries.size(), members};
    }
};

}  // namespace

int main() {
    const std::size_t limit = static_cast<std::size_t>(kMaxPlayers);

    // One member, four fakes: five entries, all copies of the first, its reference counted each time.
    Room room(1);
    Check(AppendFakeMembers(&room.list, 4, limit, &FakeReserve, &FakeResize) == 4, "four fake members are added");
    Check(room.list.size == 5 && room.list.capacity >= 5, "the list holds five members");
    bool copies = true;
    for (std::size_t i = 1; i < 5; ++i) copies = copies && room.list.data[i].object == &room.uses[0];
    Check(copies && room.list.data[0].object == &room.uses[0], "every fake is a copy of the first member");
    Check(room.uses[0] == 5, "each copy counts a reference");
    Check(room.list.reserved == reinterpret_cast<void*>(0x1234), "the game's own field is left alone");

    // Never past a full room, and nothing to do once it is full.
    Room nearlyFull(kMaxPlayers - 2);
    Check(AppendFakeMembers(&nearlyFull.list, 5, limit, &FakeReserve, &FakeResize) == 2 &&
              nearlyFull.list.size == kMaxPlayers,
          "a room two short of full takes two fakes and stops");
    Room fullRoom(kMaxPlayers);
    Check(AppendFakeMembers(&fullRoom.list, 1, limit, &FakeReserve, &FakeResize) == 0 &&
              fullRoom.list.size == kMaxPlayers,
          "a full room gets no fake members");

    // Spare capacity is used without reallocating.
    Room spare(2, 6);
    reserveCalls = 0;
    Check(AppendFakeMembers(&spare.list, 3, limit, &FakeReserve, &FakeResize) == 3 && reserveCalls == 0,
          "capacity that is already there is used as it is");

    // Nothing to copy, nothing asked for, or a helper that cannot allocate: the list is untouched.
    Room one(1);
    const RoomMemberList before = one.list;
    Check(AppendFakeMembers(&one.list, 0, limit, &FakeReserve, &FakeResize) == 0, "no fakes asked for");
    Room empty(0);
    Check(AppendFakeMembers(&empty.list, 4, limit, &FakeReserve, &FakeResize) == 0 && empty.list.size == 0,
          "an empty member list has nothing to copy");
    Check(AppendFakeMembers(nullptr, 4, limit, &FakeReserve, &FakeResize) == 0, "no list");
    Check(AppendFakeMembers(&one.list, 4, limit, nullptr, &FakeResize) == 0 &&
              AppendFakeMembers(&one.list, 4, limit, &FakeReserve, nullptr) == 0,
          "no helpers");
    reserveFails = true;
    Check(AppendFakeMembers(&one.list, 4, limit, &FakeReserve, &FakeResize) == 0, "a failed reserve adds nothing");
    reserveFails = false;
    Check(one.list.data == before.data && one.list.size == before.size && one.uses[0] == 1, "the list is as it was");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("fake member harness verified\n");
    return 0;
}
