#pragma once

#include <cstddef>
#include <cstdint>

namespace multislot {

// EDF6's own description of its netplay says it keeps routine state sync under about 320 kbps and skips
// the less important updates as it nears that, so a busy room can lose updates with no packet loss at
// all. This build makes that pressure worse by design: an 8-player room carries up to 28 P2P links where
// 4 players carry 6. Nothing had ever measured it, so every packet EDF6 hands to or takes from EOS is
// counted on its way past and one line per direction per minute says how close the game runs to its own
// budget, broken down by channel. Read-only: no packet is changed, delayed or dropped here.
//
// Why by channel: if the game sends absolute positions on one channel and one-off events (a boost, a
// jump, a shot) on another, then only the second kind is worth delivering reliably - a resent position
// arrives already stale and can drag a player backwards. The breakdown is what tells the two apart.
// `peer` identifies the other side (EOS hands out a stable ProductUserId pointer per peer within a
// session) and may be null, in which case only the totals move. Per peer it is the packets per second
// that matter: EDF6 sends its state sync once per update, so the rate arriving from someone is the rate
// their machine is producing - which is also, near enough, how fast their game is running. The longest
// silence says whether that stream ever stalls; the clock is the 15 ms system tick, so only a silence
// well above that means anything.
void RecordSent(const void* peer, std::uint8_t channel, std::uint32_t bytes, std::uint64_t nowMs);
void RecordReceived(const void* peer, std::uint8_t channel, std::uint32_t bytes, std::uint64_t nowMs);

// How a peer pointer is turned into something readable. Called once per peer per window, never per
// packet. Without one, peers are reported by a short form of their pointer.
using PeerNameFn = void (*)(const void* peer, char* out, std::size_t size);
void SetPeerNameResolver(PeerNameFn resolver);

// [Sync] TrafficMeter. Off (the default for a build with nothing to measure) makes both Record calls
// return immediately.
void SetTrafficMeter(bool on);
bool TrafficMeterOn();

// Summarises the window that is still filling, instead of waiting for the minute to end.
void FlushTraffic(std::uint64_t nowMs);

}  // namespace multislot
