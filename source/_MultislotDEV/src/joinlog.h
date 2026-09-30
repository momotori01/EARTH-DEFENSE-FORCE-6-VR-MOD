#pragma once
#include <cstddef>
#include <cstdint>

#include "midhook.h"
#include "patches.h"  // kMaxPlayers: the slot snapshot is as wide as the build makes the room

namespace multislot {

// Lobby join diagnostics (NetLog=1): what the room list's join button decided, why a join ended and which
// user slot each room member got.
void InitJoinLog(unsigned char* gameBase);

// Read-only snapshot of the actual slot vector, including holes and users whose handshake is pending.
// bit 0 of User+0x10 is the predicate used by Room::GetUsers; EOS Connected is a different state.
struct UserSlotState {
    std::uintptr_t object = 0;
    std::uintptr_t productId = 0;
    std::uint32_t flags = 0;
    std::int32_t index = -1;
};
struct UserSlotsSnapshot {
    std::size_t capacity = 0, occupied = 0, ready = 0;
    // As many as the build widens the room to, not a literal 8: at 10 or 12 the read used to refuse the
    // vector as too long, which made EligibleFinalHello fail and switched HandshakeRecovery off without
    // saying so. Found in hajisensai/edf-coop-stable 1.5.13, which fixed the same thing.
    UserSlotState slots[kMaxPlayers]{};
};
bool ReadUserSlots(const void* users, UserSlotsSnapshot& out);
// Active RoomImpl, not its eos::lobby::Room base (whose vtable is installed during teardown).
inline constexpr std::uintptr_t kActiveRoomVtableRva = 0x17ECA00;
bool ReadCurrentRoomUsers(std::uintptr_t gameBase, std::uintptr_t& users, const char*& failure);
void NoteUserSlots(std::size_t rosterCount);
void ForgetUserSlots();
MidHandler JoinLogHookHandler(std::uint32_t rva);
void* JoinLogCallHandler(std::uint32_t rva);

}  // namespace multislot
