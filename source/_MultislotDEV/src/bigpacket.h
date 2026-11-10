#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace multislot {

// A game packet over EOS's 1170 bytes, sent in parts on MultiSlot's channel and put back together on arrival.
//
// EDF6 never splits a message, so a message over 1162 bytes makes a packet EOS refuses, and 12C8BC0 drops it
// without a word (packetsize.h). Eight players' loadouts in the mission start message come to 1161-1163 bytes:
// on 2026-11-10 four eight-player starts passed at 1161, and on 2026-11-11 one at 1163 was refused to all seven
// members, who were dropped 27 s later (the game's give-up) while the host started alone.
//
// The receiving side has room for it: 12C8CC0 asks EOS for up to 0x1000 bytes into a 0x1000-byte buffer and
// hands each packet on whole (12C71F0), so the joined packet reaches the game exactly as the sender's game made
// it. Only the game's data sends (12C8BC0, the call at 12C8C54) are split; nothing smaller changes.
//
// Wire: 00 00 'M' 'S', version 1, type 4, id (2 bytes, per sender), part, parts, channel, 0, total (2 bytes),
// then the part. A player without the mod drops it unread like every MultiSlot packet (handaim.h) and an older
// MultiSlot passes the type by: for both the packet is lost, as it was before.
constexpr std::uint8_t kBigPacketType = 4;
constexpr std::size_t kBigPacketHeader = 14;
constexpr std::uint32_t kBigPacketEosLimit = 1170;  // EOS_P2P_MAX_PACKET_SIZE
constexpr std::uint32_t kBigPacketMaxPart = kBigPacketEosLimit - static_cast<std::uint32_t>(kBigPacketHeader);
constexpr std::uint32_t kBigPacketMaxTotal = 0x1000;  // what 12C8CC0 receives into
constexpr std::uint64_t kBigPacketLifeMs = 10000;     // parts of a packet that never completes are dropped after this
constexpr std::size_t kBigPacketPending = 16;         // packets being put back together at once

// The parts of a packet of `size` bytes (over kBigPacketEosLimit, at most kBigPacketMaxTotal); empty otherwise.
std::vector<std::vector<std::uint8_t>> SplitBigPacket(const std::uint8_t* packet, std::uint32_t size,
                                                      std::uint8_t channel, std::uint16_t id);
// A fresh id for the next split packet from this machine.
std::uint16_t NextBigPacketId();
// Whether a received packet is a part this build reads.
bool IsBigPacketPart(const void* data, std::uint32_t size);
// Keeps a part from `peer` (a ProductUserId as text). True when it completes a packet: the packet is then in
// `out` and the channel the sender's game used in `channel`. False for a malformed part too.
bool JoinBigPacketPart(const char* peer, const void* data, std::uint32_t size, std::uint64_t nowMs,
                       std::vector<std::uint8_t>& out, std::uint8_t& channel);

void ResetBigPacketsForTest();

}  // namespace multislot
