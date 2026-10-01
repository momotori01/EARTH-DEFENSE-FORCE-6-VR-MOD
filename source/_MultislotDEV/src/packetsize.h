#pragma once

#include <cstdint>
#include <vector>

#include "midhook.h"
#include "patches.h"

namespace multislot {

// How big the messages are that EDF6 queues for a peer, against the one packet EOS will carry.
//
// EOS P2P refuses any packet over 1170 bytes. EDF6 packs its messages into packets of up to 1100 (12CFFD0:
// when the queued bytes plus the new message pass 0x44C it sends what it has first, then starts a packet
// with its own 8-byte header) - but it never splits one message. A message over 1162 bytes becomes a packet
// over 1170, EOS refuses it, and 12C8BC0 only reacts to EOS_NoConnection, so it is dropped without a word.
//
// The suspect is the mission start: MissionSync_Res (78D0E0) re-serialises every member's loadout record
// (773840, about 110-215 bytes each depending on the values) and sends them as one message. Seven players
// have started missions; eight have not been seen to. This measures it instead of guessing:
//   - every message from the start/result sync functions (78D0E0-7938CE on the stack), whatever its size,
//     so a room of five to seven gives the size per player and eight can be worked out;
//   - every message of 1000 bytes and more, from anywhere;
//   - every send EOS refuses (in netlog.cpp's SendPacket wrapper).
// Read-only. Installed with [MultiSlot] NetLog=1, which is the default.

constexpr std::uint32_t kEosMaxPacket = 1170;       // EOS_P2P_MAX_PACKET_SIZE
constexpr std::uint32_t kGamePacketLimit = 0x44C;   // 1100: where 12CFFD0 closes a packet
constexpr std::uint32_t kGamePacketHeader = 8;      // written at the start of every packet it opens

enum class MessageFit {
    Shares,     // fits beside other messages in one of the game's packets
    OwnPacket,  // goes out in a packet of its own, still under EOS's limit
    Dropped,    // makes a packet EOS refuses: lost
};
MessageFit ClassifyMessage(std::uint32_t bytes);

// Whether `address` is a return address: the bytes just before it are a call (E8 rel32, FF 15 rip, or an
// FF /2 through a register or memory). `before` points at `address - 7` and must have 7 readable bytes.
bool FollowsCall(const std::uint8_t* before);

// The start and result sync functions: UserSync, MissionSync, ResultSync and their callbacks.
bool InSyncFunctions(std::uint32_t rva);

std::vector<MidSite> PacketSizeHooks();
MidHandler PacketSizeHookHandler(std::uint32_t rva);
void InitPacketSize(unsigned char* gameBase);

// From the SendPacket wrapper: EOS refused a packet, or was handed one over its limit.
void NoteSendRefused(std::uint32_t bytes, std::uint8_t channel, std::int32_t reliability, int result,
                     std::uintptr_t callerRva);

}  // namespace multislot
