#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "patches.h"

namespace multislot {

// Downed insects and robots kept in sync (1.6.11, 2026-10-27; not run in a room before release).
//
// The user: an enemy blown off its feet by an explosion "warps" when it gets up again, and a fast slide
// instead of the jump is no better: "ダウン中も同期が止まらないようにしたい". The warping ones are the insects
// (ants, spiders and their queens, Dango) and the Berserker robots.
//
// Every one of them carries the game's walking sync object (net::WalkCharacterSyncControl, vtable 17ABA10),
// switched on or off once a frame by its class: 77C600 on, 77C590 off (both do nothing when already so),
// then 77C6D0 on the host (records the position, flags a send on a >45 degree turn) or 77C8E0 on the guest
// (plays the queued snapshots back through the movement controller; 77D610 jumps straight to the newest one
// when it is 40 m or more away, when five are queued, or when the playback budget runs out). The class's own
// send carries the position only while the object is on: an ant every 90 frames, a spider 60, a robot 40,
// or at once when the object's "send now" flags (+0x180 & 5) are set (77D5C0).
//
// The host switches it off while the enemy is in the air (ant / spider / Dango +0xE70, robot +0x11D0), and
// the ant's guest also while the ant plays its blow reaction (state 40E440). So nothing is sent while it
// flies, each machine flies it by its own copy of the blow, and the position sent when it lands is where the
// guest's copy slides or jumps to.
//
// 1.6.11 kept the object on for anything in the air. The first room that had it (2026-10-27, five players)
// showed that "in the air" is mostly the enemies' own movement - a spider's every jump (1000-1500 a minute),
// a Berserker's boost flights - so the host forced ~7000 sends a minute and its traffic doubled (600 kbps). The
// user: "ダウン同期を吹き飛びだけにして". So 1.6.12 asks the family's own reaction states whether a hit has the
// enemy blown away or knocked down (BlownAway); only then, and only while it is off the ground, the host's
// enemy reads as on the ground for the length of its own sync update (Observe), so the class goes down its own
// "on" branch, and the host forces a send every `sendFrames` frames (77D5C0), at most `sendCap` a second in
// all. The host side writes no code. The ant's guest switches its sync off for its whole blow reaction, which
// only a code change gets past: one byte (DownSyncGates), written while the host says it keeps them in sync
// and back otherwise. Bees use a separate flying sync and are not included.
//
// Who needs it: everyone in the room (see "Who in the room has it" below). With anyone missing it, or with
// its switch off, every machine leaves its enemies as the game has them - the user, 2026-10-27: "ホストがバニラ
// なりオフなりで情報が無い場合、バニラと同じ挙動になるようにして". The only thing it adds to the network is that
// one-second word between the machines that have it.
//
// Measurement (always, while installed): the class updates (vtable slot 57 of the insects, 68 of the
// robots), the sync object's send and receive (17ABA10 slots 0 and 1) and the three jumps to the newest
// snapshot (calls to 77D610) are watched. For every flight / blow it logs how long it lasted, how many
// positions went out (host) or came in (guest), and on the guest how far the enemy was from the host's copy
// while it flew and at the first position after it landed - the size of the warp, with this on or off.

enum class DownKind : std::uint8_t { Ant, Spider, Dango, Robot };

struct DownSyncClass {
    const char* name;
    std::uint32_t vtable;  // RVA
    DownKind kind;
};
// The concrete classes whose objects are watched (the queens use their family's class).
const std::vector<DownSyncClass>& DownSyncClasses();

// One byte of a verified run of code that DownSync flips.
struct DownSyncGate {
    const char* name;
    std::uint32_t rva;                   // start of the verified run
    std::vector<std::uint8_t> original;  // the run as the game has it
    std::uint32_t flip;                  // offset of the byte that changes
    std::uint8_t patched;                // its value while DownSync is on
    bool guestSide;                      // a guest's gate (follows the host's word), else a host's
};
std::vector<DownSyncGate> DownSyncGates();

// The class update slots replaced by observers (all verified against their target), in DownSyncClasses order.
std::vector<PointerSlot> DownSyncSlots();
void* DownSyncSlotHandler(std::uint32_t rva);
// The sync object's send (slot 0) and receive (slot 1), shared by every WalkCharacterSyncControl.
std::vector<PointerSlot> DownSyncSyncSlots();
void* DownSyncSyncSlotHandler(std::uint32_t rva);
// The three calls to 77D610 in 77C8E0: five queued, 40 m away, playback budget spent.
std::vector<CallSite> DownSyncSnapCalls();
void* DownSyncSnapHandler(std::uint32_t rva);

struct DownSyncSettings {
    bool on = true;           // this machine's switch at start (the room still has to agree)
    int sendFrames = 6;       // forced send interval per enemy, frames
    int sendCap = 60;         // forced sends per second, all enemies
    int key = 0x78;           // VK_F9; 0 = no key
};
// Before the slots and calls are written (they call into the game through `gameBase`).
void InitDownSync(unsigned char* gameBase, const DownSyncSettings& settings);
// After the slots and calls are in: verifies the gates (all left as the game has them until the room agrees).
// False when a gate is not the expected code; the measurement then still runs and nothing is kept in sync.
bool StartDownSync(unsigned char* gameBase, const DownSyncSettings& settings);
// This machine's switch.
bool DownSyncOn();
// Sets the switch (the key, the INI); logs a marker line and re-checks the gates at once.
void SetDownSync(bool on, const char* why);

// ---- Who in the room has it (a word between the players who run this build) ----
//
// Nothing changes for anyone unless everyone agrees: every machine with this build (and its switch on) tells
// every other member so once a second, in a MultiSlot packet (00 00 'M' 'S', version 1, type 3) that the game
// of a player without it drops unread and an older MultiSlot passes by. Type 2 was 1.6.11's word, when anything
// in the air counted; 1.6.12 neither sends nor reads it, so a room mixing the two leaves everything as the game
// has it rather than letting a 1.6.11 host send every spider's jump. The machine that owns the enemies (the
// host) keeps its downed ones in sync only while every member it plays with has said so in the last few
// seconds, and then says that too; a guest follows them only while the host says it. A member without this
// build, or with the switch off, or a host that never says it: everyone's enemies stay exactly as the game has
// them. Alone (no members), nothing changes either.
constexpr std::uint8_t kDownSyncPacketType = 3;  // 1.6.11 used 2
constexpr std::size_t kDownSyncPacketSize = 8;      // magic 4, version 1, type 1, flags 1, reserved 1
constexpr std::uint8_t kDownSyncSwitchOn = 1;       // flags: this machine's switch is on (it accepts them)
constexpr std::uint8_t kDownSyncKeeping = 2;        // flags: this machine owns enemies and keeps them in sync
constexpr std::uint64_t kDownSyncWordGapMs = 1000;  // one word a second to each member
constexpr std::uint64_t kDownSyncWordLifeMs = 3500; // a word older than this no longer counts
constexpr std::uint64_t kDownSyncMemberLifeMs = 3000;  // a member the game has not sent to for this long has left

struct DownPeer {
    std::uint64_t sentAt = 0;     // the game last sent this member its own data
    std::uint64_t switchAt = 0;   // this member last said its switch is on
    std::uint64_t keepingAt = 0;  // this member last said it keeps its downed enemies in sync
};
// The host side: the switch on, at least one member, every member the game is sending to has said it accepts.
bool HostSideActive(bool switchOn, const std::vector<DownPeer>& peers, std::uint64_t nowMs);
// The guest side: the switch on and some member (the host) saying it keeps its downed enemies in sync.
bool GuestSideActive(bool switchOn, const std::vector<DownPeer>& peers, std::uint64_t nowMs);
std::size_t BuildDownSyncPacket(std::uint8_t* out, std::size_t size, std::uint8_t flags);
// The flags of a type-3 packet, or -1 when it is not one this build reads (1.6.11 type 2 included).
int ReadDownSyncPacket(const void* data, std::uint32_t size);

// For the packet hooks (netlog.cpp): the game just sent `peer` (ProductUserId text) its data; the word for
// that member, when one is due (0 = nothing to send); and a received MultiSlot packet (false: not this build's word).
bool DownSyncInstalled();
void NoteDownSyncPeer(const char* peer, std::uint64_t nowMs);
std::size_t DownSyncWordFor(const char* peer, std::uint8_t* out, std::size_t size, std::uint64_t nowMs);
bool ReceiveDownSyncPacket(const char* peer, const void* data, std::uint32_t size, std::uint64_t nowMs);

// ---- Pure parts (tests/downsync_test.cpp) ----

// Whether an enemy is in a window this frame: alive and blown away or knocked down by a hit.
bool DownWindow(bool dead, bool blown);

// The host's forced send for one enemy this frame.
struct ForceInput {
    bool on;          // DownSync on
    bool host;        // this machine owns the enemy (not a replica)
    bool alive;
    bool active;      // the sync object is running
    bool window;      // DownWindow
    int framesSince;  // frames since this enemy's last forced send (or since the window began)
    int sendFrames;
    std::uint8_t flags;  // the object's +0x180
};
bool ShouldForceSend(const ForceInput& in);

// At most `cap` takes per second (a token bucket refilled by elapsed milliseconds, burst = cap / 4).
class SendBudget {
public:
    void Reset(int cap, std::uint64_t nowMs);
    bool Take(std::uint64_t nowMs);
    std::uint64_t refused() const { return refused_; }

private:
    double tokens_ = 0;
    double cap_ = 0;
    std::uint64_t last_ = 0;
    std::uint64_t refused_ = 0;
};

// A distribution kept as its samples (bounded): count, mean, median, 90th percentile and max.
class Spread {
public:
    void Add(float value);
    std::size_t count() const { return values_.size() + dropped_; }
    float Mean() const;
    float Percentile(float p) const;  // 0..1, of the kept samples
    float Max() const { return max_; }
    void Clear();

private:
    std::vector<float> values_;
    std::size_t dropped_ = 0;
    double sum_ = 0;
    float max_ = 0;
};

// The index of the newest queued snapshot, given the object's write index (+0x20); the queue holds five.
inline std::size_t NewestEntry(std::uint64_t writeIndex) { return static_cast<std::size_t>((writeIndex + 4) % 5); }

}  // namespace multislot
