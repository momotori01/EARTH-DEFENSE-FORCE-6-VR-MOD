#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "midhook.h"
#include "patches.h"

namespace multislot {

// Who sits where in a vehicle, in the log (1.6.7, diagnostics only - nothing is changed).
//
// The 2026-10-04 six-player session: the sixth player's game died twice while others were taking a
// Caliban's or a Grape's back seats, a player was put into seat 3 and out again seven times in a row,
// and a Grape called at the start was seen by no one but the Air Raider who called it. The logs held
// only each player's own seat (EDF6VR's VEHICLE lines), so these four places are written down, each change
// once:
//   - our soldier tells the room which seat it now has (5763E0, at 5764B0: rdi soldier, rbp vehicle, eax the
//     seat index from 62D810, -1 when it has none);
//   - another player's seat arrives (5774C0's ride case, at 577652 just before 5765E0 applies it: rsi-0x120
//     their soldier, r12d the seat, the vehicle object at [rsp+0x70]);
//   - a vehicle is asked to seat a soldier (6325B0's case 4, at 632699: rsi the soldier, r14d the seat, r15d
//     the soldier's change counter, rbx the vehicle+0x120, which then refuses a seat outside 0..count-1 and
//     a counter older than the soldier's [+0x1824]);
//   - a vehicle is here (6314A0, its seat bookkeeping, which every vehicle runs every frame: rcx the vehicle),
//     written when one is first seen or seen again after kVehicleGoneMs without it - so each machine's log
//     says when a called vehicle reached it (the Grape no one else saw).
// Each line names the vehicle class, the seat against the vehicle's seat count (+0x618), and the player by
// their EOS id. Installed with [MultiSlot] NetLog=1 (the default); at most kRideLogLines lines per launch.

constexpr std::uint32_t kRideSend = 0x5764B0;
constexpr std::uint32_t kRideReceive = 0x577652;
constexpr std::uint32_t kRideRequest = 0x632699;
constexpr std::uint32_t kVehicleUpdate = 0x6314A0;
constexpr std::uint64_t kVehicleGoneMs = 3000;
constexpr std::size_t kVehicleSeats = 0x608;      // the seat array (0x340 bytes each)
constexpr std::size_t kVehicleSeatCount = 0x618;
constexpr std::size_t kSoldierRideCounter = 0x1824;
constexpr int kRideLogLines = 400;

std::vector<MidSite> RideLogHooks();
MidHandler RideLogHookHandler(std::uint32_t rva);
void InitRideLog(unsigned char* gameBase);

// For the tests: the line one event would write (empty when it repeats the last one from that place).
enum class RideEvent { Ours, Theirs, Request };
std::size_t FormatRideLine(RideEvent event, const void* soldier, const void* vehicle, int seat,
                           std::uint32_t counter, char* out, std::size_t size);
// The line for a vehicle's update at `now` (empty while it keeps being seen).
std::size_t NoteVehicle(const void* vehicle, std::uint64_t now, char* out, std::size_t size);
void ResetRideLogForTest();

}  // namespace multislot
