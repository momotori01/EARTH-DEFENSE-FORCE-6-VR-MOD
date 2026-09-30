#include "traffic.h"

#include <atomic>
#include <cstdio>

#include "log.h"

namespace multislot {
namespace {

constexpr std::uint64_t kWindowMs = 60000;
// EDF6's stated ceiling for routine state sync. Printed beside the measurement so a reader can tell at a
// glance whether the game was anywhere near the point where it starts leaving updates out.
constexpr double kRoutineBudgetKbps = 320.0;
constexpr std::size_t kChannels = 256;  // Channel is a uint8_t
constexpr std::size_t kChannelsShown = 8;
constexpr std::size_t kPeers = 12;  // one full room, with room to spare

// Every counter lives at namespace scope, so it is zero before any code runs: std::atomic's default
// constructor does not initialise in C++17, but static storage is zero-initialised first.
// One peer's share of a window. The pointer is the key; a row whose peer sent nothing all window is
// given up at report time, so changing rooms does not leave stale peers behind.
struct PeerCounts {
    std::atomic<const void*> key;
    std::atomic<std::uint64_t> bytes;
    std::atomic<std::uint64_t> packets;
    std::atomic<std::uint64_t> lastMs;
    std::atomic<std::uint64_t> longestSilenceMs;
};

struct Direction {
    PeerCounts peers[kPeers];
    std::atomic<std::uint64_t> bytes;
    std::atomic<std::uint64_t> packets;
    std::atomic<std::uint32_t> largest;
    std::atomic<std::uint64_t> channelBytes[kChannels];
    std::atomic<std::uint64_t> channelPackets[kChannels];
    std::atomic<std::uint64_t> second;             // nowMs / 1000 of the second currently being filled
    std::atomic<std::uint64_t> secondBytes;
    std::atomic<std::uint64_t> busiestSecondBytes;
};

Direction sent;
Direction received;
std::atomic<bool> meterOn;
PeerNameFn peerName = nullptr;
std::atomic<std::uint64_t> windowStart;
std::atomic<bool> windowStarted;

double Kbps(std::uint64_t bytes, double seconds) {
    if (seconds <= 0.0) return 0.0;
    return static_cast<double>(bytes) * 8.0 / 1000.0 / seconds;
}

// Finds or claims this peer's row. Null when every row belongs to someone else, which takes more than a
// full room; the direction totals still count the packet.
PeerCounts* FindPeer(Direction& d, const void* peer, std::uint64_t nowMs) {
    for (auto& row : d.peers)
        if (row.key.load(std::memory_order_relaxed) == peer) return &row;
    for (auto& row : d.peers) {
        const void* empty = nullptr;
        if (row.key.compare_exchange_strong(empty, peer, std::memory_order_relaxed)) {
            row.bytes.store(0, std::memory_order_relaxed);
            row.packets.store(0, std::memory_order_relaxed);
            row.lastMs.store(nowMs, std::memory_order_relaxed);
            row.longestSilenceMs.store(0, std::memory_order_relaxed);
            return &row;
        }
        if (row.key.load(std::memory_order_relaxed) == peer) return &row;  // lost the race to this peer
    }
    return nullptr;
}

void CountPeer(Direction& d, const void* peer, std::uint32_t bytes, std::uint64_t nowMs) {
    PeerCounts* row = FindPeer(d, peer, nowMs);
    if (!row) return;
    row->bytes.fetch_add(bytes, std::memory_order_relaxed);
    row->packets.fetch_add(1, std::memory_order_relaxed);
    const std::uint64_t last = row->lastMs.exchange(nowMs, std::memory_order_relaxed);
    if (nowMs > last) {
        const std::uint64_t silence = nowMs - last;
        std::uint64_t longest = row->longestSilenceMs.load(std::memory_order_relaxed);
        while (silence > longest &&
               !row->longestSilenceMs.compare_exchange_weak(longest, silence, std::memory_order_relaxed)) {
        }
    }
}

void Count(Direction& d, std::uint8_t channel, std::uint32_t bytes, std::uint64_t nowMs) {
    d.bytes.fetch_add(bytes, std::memory_order_relaxed);
    d.packets.fetch_add(1, std::memory_order_relaxed);
    d.channelBytes[channel].fetch_add(bytes, std::memory_order_relaxed);
    d.channelPackets[channel].fetch_add(1, std::memory_order_relaxed);
    std::uint32_t largest = d.largest.load(std::memory_order_relaxed);
    while (bytes > largest && !d.largest.compare_exchange_weak(largest, bytes, std::memory_order_relaxed)) {
    }
    // The busiest second in the window. Several sender threads can race here; the loser's bytes land in
    // the next second instead of this one, which is close enough for a measurement and costs no lock on
    // a path the game walks hundreds of times a second.
    const std::uint64_t second = nowMs / 1000;
    std::uint64_t filling = d.second.load(std::memory_order_relaxed);
    if (second != filling && d.second.compare_exchange_strong(filling, second, std::memory_order_relaxed)) {
        const std::uint64_t finished = d.secondBytes.exchange(0, std::memory_order_relaxed);
        std::uint64_t busiest = d.busiestSecondBytes.load(std::memory_order_relaxed);
        while (finished > busiest &&
               !d.busiestSecondBytes.compare_exchange_weak(busiest, finished, std::memory_order_relaxed)) {
        }
    }
    d.secondBytes.fetch_add(bytes, std::memory_order_relaxed);
}

// One line per peer that exchanged anything, written before the direction's own total.
void ReportPeers(Direction& d, const char* what, double seconds) {
    for (auto& row : d.peers) {
        const void* key = row.key.load(std::memory_order_relaxed);
        if (!key) continue;
        const std::uint64_t packets = row.packets.exchange(0, std::memory_order_relaxed);
        const std::uint64_t bytes = row.bytes.exchange(0, std::memory_order_relaxed);
        const std::uint64_t silence = row.longestSilenceMs.exchange(0, std::memory_order_relaxed);
        if (!packets) {
            row.key.store(nullptr, std::memory_order_relaxed);  // gone: let a later peer have the row
            continue;
        }
        char name[48]{};
        if (peerName) peerName(key, name, sizeof(name));
        if (!name[0])
            _snprintf_s(name, sizeof(name), _TRUNCATE, "peer-%08llX",
                        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(key) & 0xFFFFFFFFull));
        Log("TRAFFIC %s %s: %.1f packets/s, %.0f kbps, %.0f B avg, longest silence %llu ms", what, name,
            static_cast<double>(packets) / seconds, Kbps(bytes, seconds),
            static_cast<double>(bytes) / static_cast<double>(packets),
            static_cast<unsigned long long>(silence));
    }
}

// Drains the window and writes one line. Nothing is written for a direction that carried no packet, so a
// session spent in the menus adds nothing to the log.
void Report(Direction& d, const char* what, double seconds) {
    const std::uint64_t packets = d.packets.exchange(0, std::memory_order_relaxed);
    const std::uint64_t bytes = d.bytes.exchange(0, std::memory_order_relaxed);
    const std::uint32_t largest = d.largest.exchange(0, std::memory_order_relaxed);
    std::uint64_t busiest = d.busiestSecondBytes.exchange(0, std::memory_order_relaxed);
    const std::uint64_t filling = d.secondBytes.exchange(0, std::memory_order_relaxed);
    if (filling > busiest) busiest = filling;  // the window may end mid-second
    char channels[256];
    std::size_t at = 0;
    std::size_t shown = 0;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        const std::uint64_t channelPackets = d.channelPackets[channel].exchange(0, std::memory_order_relaxed);
        const std::uint64_t channelBytes = d.channelBytes[channel].exchange(0, std::memory_order_relaxed);
        if (!channelPackets || shown >= kChannelsShown) continue;
        const int written = _snprintf_s(channels + at, sizeof(channels) - at, _TRUNCATE,
                                        " | ch%zu %.0f kbps %llu pkt %.0f B avg", channel,
                                        Kbps(channelBytes, seconds), static_cast<unsigned long long>(channelPackets),
                                        static_cast<double>(channelBytes) / static_cast<double>(channelPackets));
        if (written < 0) break;
        at += static_cast<std::size_t>(written);
        ++shown;
    }
    if (!packets) return;
    ReportPeers(d, what, seconds);
    Log("TRAFFIC %s over %.0fs: %.0f kbps avg, busiest second %.0f kbps (EDF6 holds routine sync under "
        "about %.0f kbps and skips the less important updates near that), %llu packets, %.0f B avg, "
        "largest %u%s",
        what, seconds, Kbps(bytes, seconds), Kbps(busiest, 1.0), kRoutineBudgetKbps,
        static_cast<unsigned long long>(packets), static_cast<double>(bytes) / static_cast<double>(packets),
        largest, channels);
}

void ReportIfDue(std::uint64_t nowMs) {
    std::uint64_t start = windowStart.load(std::memory_order_relaxed);
    if (!windowStarted.load(std::memory_order_relaxed)) {
        windowStart.store(nowMs, std::memory_order_relaxed);
        windowStarted.store(true, std::memory_order_relaxed);
        return;
    }
    if (nowMs - start < kWindowMs) return;
    // One thread takes the window; the others go straight back to sending.
    if (!windowStart.compare_exchange_strong(start, nowMs, std::memory_order_relaxed)) return;
    const double seconds = static_cast<double>(nowMs - start) / 1000.0;
    Report(sent, "sent", seconds);
    Report(received, "received", seconds);
}

}  // namespace

void SetTrafficMeter(bool on) { meterOn.store(on, std::memory_order_relaxed); }

bool TrafficMeterOn() { return meterOn.load(std::memory_order_relaxed); }

void SetPeerNameResolver(PeerNameFn resolver) { peerName = resolver; }

void RecordSent(const void* peer, std::uint8_t channel, std::uint32_t bytes, std::uint64_t nowMs) {
    if (!TrafficMeterOn()) return;
    Count(sent, channel, bytes, nowMs);
    if (peer) CountPeer(sent, peer, bytes, nowMs);
    ReportIfDue(nowMs);
}

void RecordReceived(const void* peer, std::uint8_t channel, std::uint32_t bytes, std::uint64_t nowMs) {
    if (!TrafficMeterOn()) return;
    Count(received, channel, bytes, nowMs);
    if (peer) CountPeer(received, peer, bytes, nowMs);
    ReportIfDue(nowMs);
}

void FlushTraffic(std::uint64_t nowMs) {
    const std::uint64_t start = windowStart.exchange(nowMs, std::memory_order_relaxed);
    const double seconds = windowStarted.load(std::memory_order_relaxed) && nowMs > start
                               ? static_cast<double>(nowMs - start) / 1000.0
                               : 1.0;
    Report(sent, "sent", seconds);
    Report(received, "received", seconds);
}

}  // namespace multislot
