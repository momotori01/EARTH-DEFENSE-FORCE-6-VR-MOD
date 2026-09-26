#pragma once
#include <cstdint>

namespace multislot {

// Resolves EDF.dll's LobbyDetails member-count wrapper and the EOS exports used below.
void InitRooms(const unsigned char* gameBase);

// Both replace calls to EDF+12AEB30 (rcx = LobbyDetails handle holder, returns the member count),
// so a room's real EOS capacity is used instead of the hard-coded 4.
// Room info parse: returns (capacity << 32) | members.
std::uint64_t RoomCountAndCapacity(void* holder);
// Host room update: returns 4 when the room is full and 0 otherwise; the game compares the
// result with 4 and publishes it as HIDDEN.
std::uint32_t RoomFullCount(void* holder);

// Pure decision used by both, kept separate for tests. Returns 0 when EOS data is inconsistent.
std::uint32_t CapacityFromInfo(std::uint32_t members, std::uint32_t availableSlots, std::uint32_t maxMembers);

}  // namespace multislot
