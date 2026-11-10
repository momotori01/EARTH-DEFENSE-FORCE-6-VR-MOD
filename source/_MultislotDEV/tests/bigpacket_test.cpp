// A game packet over EOS's limit in parts (bigpacket.h): sizes at the edges, the round trip in any order,
// what is refused, peers and ids kept apart, and parts that never complete let go.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/bigpacket.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<std::uint8_t> Packet(std::uint32_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> p(size);
    for (std::uint32_t i = 0; i < size; ++i) p[i] = static_cast<std::uint8_t>(seed + i * 7 + (i >> 8));
    return p;
}

bool Join(const char* peer, const std::vector<std::uint8_t>& part, std::uint64_t now, std::vector<std::uint8_t>& out,
          std::uint8_t& channel) {
    return JoinBigPacketPart(peer, part.data(), static_cast<std::uint32_t>(part.size()), now, out, channel);
}

}  // namespace

int main() {
    ResetBigPacketsForTest();
    // Sizes: nothing at or under EOS's limit or over the game's buffer; the 1163-byte start message of
    // 2026-11-11 (a 1171-byte packet) in two parts, each one EOS carries.
    Check(SplitBigPacket(Packet(1170, 1).data(), 1170, 0, 1).empty(), "1170 B goes as it is");
    Check(SplitBigPacket(Packet(4097, 1).data(), 4097, 0, 1).empty(), "over the game's 0x1000 is not split");
    Check(SplitBigPacket(nullptr, 2000, 0, 1).empty(), "no data, no parts");
    const auto start = Packet(1171, 3);
    const auto parts = SplitBigPacket(start.data(), 1171, 0, 7);
    Check(parts.size() == 2 && parts[0].size() == kBigPacketEosLimit && parts[1].size() == kBigPacketHeader + 15,
          "1171 B: one full part and 15 B");
    for (const auto& part : parts) {
        Check(part.size() <= kBigPacketEosLimit, "every part within EOS's limit");
        Check(part[0] == 0 && part[1] == 0 && part[2] == 'M' && part[3] == 'S', "a MultiSlot packet (the game skips it)");
        Check(IsBigPacketPart(part.data(), static_cast<std::uint32_t>(part.size())), "read as a part");
    }
    const auto most = SplitBigPacket(Packet(4096, 5).data(), 4096, 2, 9);
    Check(most.size() == 4, "the game's whole buffer in four parts");

    // In order, out of order, and the channel the sender's game used.
    std::vector<std::uint8_t> out;
    std::uint8_t channel = 99;
    Check(!Join("peer-a", parts[0], 1000, out, channel), "one part of two is not a packet yet");
    Check(Join("peer-a", parts[1], 1001, out, channel) && out == start && channel == 0, "two parts: the packet, exactly");
    out.clear();
    Check(!Join("peer-a", parts[1], 2000, out, channel) && Join("peer-a", parts[0], 2001, out, channel) && out == start,
          "the last part first");
    const auto big = Packet(4096, 5);
    out.clear();
    bool done = false;
    for (int i : {2, 0, 3}) done = done || Join("peer-b", most[static_cast<std::size_t>(i)], 3000, out, channel);
    Check(!done && Join("peer-b", most[1], 3001, out, channel) && out == big && channel == 2, "four parts in any order");

    // Two senders with the same id, and one sender's two packets, stay apart.
    const auto other = Packet(1300, 11);
    const auto otherParts = SplitBigPacket(other.data(), 1300, 0, 7);
    out.clear();
    Check(!Join("peer-a", parts[0], 4000, out, channel) && !Join("peer-c", otherParts[0], 4000, out, channel),
          "first halves from two peers under id 7");
    Check(Join("peer-c", otherParts[1], 4001, out, channel) && out == other, "peer c gets its own packet");
    Check(Join("peer-a", parts[1], 4002, out, channel) && out == start, "and peer a its own");

    // Refused: not a part, a later version, a cut part, a total under the limit, a part number past the end.
    auto wrong = parts[1];
    wrong[5] = 1;
    Check(!IsBigPacketPart(wrong.data(), static_cast<std::uint32_t>(wrong.size())), "type 1 is hand aim, not a part");
    wrong = parts[1];
    wrong[4] = 2;
    Check(!Join("peer-d", wrong, 5000, out, channel), "a later version is left alone");
    wrong = parts[1];
    wrong.pop_back();
    Check(!Join("peer-d", wrong, 5000, out, channel), "a part one byte short");
    wrong = parts[1];
    wrong[12] = 0x80;
    wrong[13] = 0x04;  // 1152: under EOS's limit, never split
    Check(!Join("peer-d", wrong, 5000, out, channel), "a total that was never split");
    wrong = parts[1];
    wrong[8] = 2;
    Check(!Join("peer-d", wrong, 5000, out, channel), "part 2 of 2");
    Check(!Join("", parts[0], 5000, out, channel), "no peer");

    // Parts of a packet that never completes go after kBigPacketLifeMs; a reused id with another size starts over.
    ResetBigPacketsForTest();
    Check(!Join("peer-e", parts[0], 10000, out, channel), "first half kept");
    Check(!Join("peer-e", parts[1], 10000 + kBigPacketLifeMs + 1, out, channel), "too late: the first half is gone");
    Check(Join("peer-e", parts[0], 10000 + kBigPacketLifeMs + 2, out, channel) && out == start,
          "and the late half waited for a new first half");
    Check(!Join("peer-f", otherParts[0], 30000, out, channel), "1300 B under id 7");
    Check(!Join("peer-f", parts[0], 30001, out, channel), "id 7 again with 1171 B starts over");
    Check(Join("peer-f", parts[1], 30002, out, channel) && out == start, "the new one completes");

    // More unfinished packets than slots: the oldest gives way, nothing is mixed.
    ResetBigPacketsForTest();
    for (std::uint16_t id = 0; id < kBigPacketPending + 1; ++id) {
        const auto p = SplitBigPacket(start.data(), 1171, 0, id);
        Join("peer-g", p[0], 40000 + id, out, channel);
    }
    const auto first = SplitBigPacket(start.data(), 1171, 0, 0);
    const auto last = SplitBigPacket(start.data(), 1171, 0, static_cast<std::uint16_t>(kBigPacketPending));
    Check(!Join("peer-g", first[1], 41000, out, channel), "the oldest unfinished one was let go");
    Check(Join("peer-g", last[1], 41001, out, channel) && out == start, "the newest still completes");

    Check(NextBigPacketId() != NextBigPacketId(), "ids move on");
    std::printf("big packet parts: %d failures\n", failures);
    return failures ? 1 : 0;
}
