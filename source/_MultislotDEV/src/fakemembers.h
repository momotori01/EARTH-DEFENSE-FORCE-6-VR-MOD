#pragma once
#include <cstddef>
#include <cstdint>

namespace multislot {

// One entry of the room member list: shared_ptr<eos::RoomInfo::PlayerInfo> {object, control block}.
struct RoomMember {
    void* object;
    void* control;
};

// The container the room member list builder (EDF+7468C0) fills for its callers. Its first field belongs
// to the game and is never touched here.
struct RoomMemberList {
    void* reserved;
    RoomMember* data;
    std::uint64_t capacity;
    std::uint64_t size;
};

// The game's own helpers for that container: EDF+748E70 reallocates the buffer to a capacity, EDF+749040
// grows the list with copies of one entry, counting each reference.
using ReserveFn = std::uint8_t(__fastcall*)(RoomMemberList*, std::uint64_t);
using ResizeFn = void(__fastcall*)(RoomMemberList*, std::uint64_t, const RoomMember*);

// Test mode ([RoomScreen] DummyMembers=1): appends `fakes` copies of the first member, up to `limit` members
// in total, so everything that asks the game for the room's members - the room screen panels, the voice chat
// HUD, anything added later - runs with 5-8 members on one machine. Returns how many were added.
std::size_t AppendFakeMembers(RoomMemberList* list, std::size_t fakes, std::size_t limit, ReserveFn reserve, ResizeFn resize);

void InitFakeMembers(unsigned char* gameBase);
// Redirects the calls to the member list builder while dummy members are enabled (patches.h).
void* FakeMemberCallHandler(std::uint32_t rva);

}  // namespace multislot
