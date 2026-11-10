#include "bigpacket.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstring>

namespace multislot {
namespace {

constexpr std::uint8_t kMagic[4] = {0x00, 0x00, 'M', 'S'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kPeerChars = 40;
constexpr std::uint32_t kMaxParts = (kBigPacketMaxTotal + kBigPacketMaxPart - 1) / kBigPacketMaxPart;

struct Pending {
    char peer[kPeerChars]{};
    std::uint16_t id = 0;
    std::uint8_t parts = 0;
    std::uint8_t channel = 0;
    std::uint32_t total = 0;
    std::uint32_t have = 0;  // one bit a part
    std::uint64_t startedMs = 0;
    std::vector<std::uint8_t> bytes;
    bool used = false;
};

SRWLOCK lock = SRWLOCK_INIT;
Pending pending[kBigPacketPending];
std::atomic<std::uint16_t> nextId{0};

std::uint32_t PartsFor(std::uint32_t total) { return (total + kBigPacketMaxPart - 1) / kBigPacketMaxPart; }
std::uint32_t PartSize(std::uint32_t total, std::uint32_t part) {
    const std::uint32_t start = part * kBigPacketMaxPart;
    return total - start < kBigPacketMaxPart ? total - start : kBigPacketMaxPart;
}

// Copies a received packet out of memory the game owns.
bool CopyIn(void* to, const void* from, std::size_t size) {
    __try {
        std::memcpy(to, from, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct Header {
    std::uint16_t id;
    std::uint8_t part, parts, channel;
    std::uint32_t total;
};

bool ReadHeader(const std::uint8_t* p, std::uint32_t size, Header& h) {
    if (size < kBigPacketHeader || std::memcmp(p, kMagic, sizeof(kMagic)) != 0 || p[4] != kVersion ||
        p[5] != kBigPacketType)
        return false;
    std::memcpy(&h.id, p + 6, 2);
    h.part = p[8];
    h.parts = p[9];
    h.channel = p[10];
    std::uint16_t total = 0;
    std::memcpy(&total, p + 12, 2);
    h.total = total;
    // Only what SplitBigPacket makes: over EOS's limit, within the game's buffer, every part its exact size.
    if (p[11] != 0 || h.total <= kBigPacketEosLimit || h.total > kBigPacketMaxTotal || h.parts != PartsFor(h.total) ||
        h.part >= h.parts || size != kBigPacketHeader + PartSize(h.total, h.part))
        return false;
    return true;
}

}  // namespace

std::vector<std::vector<std::uint8_t>> SplitBigPacket(const std::uint8_t* packet, std::uint32_t size,
                                                      std::uint8_t channel, std::uint16_t id) {
    std::vector<std::vector<std::uint8_t>> parts;
    if (!packet || size <= kBigPacketEosLimit || size > kBigPacketMaxTotal) return parts;
    const std::uint32_t count = PartsFor(size);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t length = PartSize(size, i);
        std::vector<std::uint8_t> part(kBigPacketHeader + length);
        std::memcpy(part.data(), kMagic, sizeof(kMagic));
        part[4] = kVersion;
        part[5] = kBigPacketType;
        std::memcpy(part.data() + 6, &id, 2);
        part[8] = static_cast<std::uint8_t>(i);
        part[9] = static_cast<std::uint8_t>(count);
        part[10] = channel;
        part[11] = 0;
        const auto total = static_cast<std::uint16_t>(size);
        std::memcpy(part.data() + 12, &total, 2);
        std::memcpy(part.data() + kBigPacketHeader, packet + i * kBigPacketMaxPart, length);
        parts.push_back(std::move(part));
    }
    return parts;
}

std::uint16_t NextBigPacketId() { return nextId.fetch_add(1, std::memory_order_relaxed); }

bool IsBigPacketPart(const void* data, std::uint32_t size) {
    std::uint8_t head[kBigPacketHeader]{};
    if (!data || size < kBigPacketHeader || !CopyIn(head, data, sizeof(head))) return false;
    return std::memcmp(head, kMagic, sizeof(kMagic)) == 0 && head[4] == kVersion && head[5] == kBigPacketType;
}

bool JoinBigPacketPart(const char* peer, const void* data, std::uint32_t size, std::uint64_t nowMs,
                       std::vector<std::uint8_t>& out, std::uint8_t& channel) {
    if (!peer || !peer[0] || !data || size > kBigPacketEosLimit) return false;
    std::uint8_t part[kBigPacketEosLimit];
    if (!CopyIn(part, data, size)) return false;
    Header h{};
    if (!ReadHeader(part, size, h)) return false;
    static_assert(kMaxParts <= 32, "one bit a part");
    bool complete = false;
    AcquireSRWLockExclusive(&lock);
    Pending* slot = nullptr;
    Pending* empty = nullptr;
    Pending* oldest = &pending[0];
    for (auto& p : pending) {
        if (p.used && nowMs - p.startedMs > kBigPacketLifeMs) p = Pending{};
        if (!p.used) {
            if (!empty) empty = &p;
            continue;
        }
        if (p.id == h.id && std::strncmp(p.peer, peer, kPeerChars) == 0) slot = &p;
        if (p.startedMs < oldest->startedMs) oldest = &p;
    }
    // Another packet under the same id (the 16-bit id wrapped) starts over.
    if (slot && (slot->total != h.total || slot->parts != h.parts || slot->channel != h.channel)) *slot = Pending{};
    if (!slot || !slot->used) {
        if (!slot) slot = empty ? empty : oldest;
        *slot = Pending{};
        slot->used = true;
        strncpy_s(slot->peer, peer, _TRUNCATE);
        slot->id = h.id;
        slot->parts = h.parts;
        slot->channel = h.channel;
        slot->total = h.total;
        slot->startedMs = nowMs;
        slot->bytes.assign(h.total, 0);
    }
    std::memcpy(slot->bytes.data() + h.part * kBigPacketMaxPart, part + kBigPacketHeader, PartSize(h.total, h.part));
    slot->have |= 1u << h.part;
    if (slot->have == (1u << h.parts) - 1) {
        out = std::move(slot->bytes);
        channel = slot->channel;
        *slot = Pending{};
        complete = true;
    }
    ReleaseSRWLockExclusive(&lock);
    return complete;
}

void ResetBigPacketsForTest() {
    AcquireSRWLockExclusive(&lock);
    for (auto& p : pending) p = Pending{};
    ReleaseSRWLockExclusive(&lock);
    nextId.store(0);
}

}  // namespace multislot
