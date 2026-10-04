#include "handaim.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstring>

#include "identity.h"
#include "joinlog.h"
#include "log.h"

namespace multislot {
namespace {

// 00 00 'M' 'S', version, type, then the type's body. The two zero bytes are what packet::Controller skips
// on (12D25A2) and 'M' 'S' what makes the first dword nonzero for the join handshake (12D5606).
constexpr std::uint8_t kMagic[4] = {0x00, 0x00, 'M', 'S'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kTypeHandAim = 1;
constexpr std::size_t kHeader = 9;  // magic 4, version 1, type 1, sequence 2, count 1
constexpr std::size_t kEntry = 7;   // slot 1, x y z as int16 of the unit vector
constexpr std::size_t kPeers = 16;
constexpr std::size_t kPeerChars = 33;

std::atomic<bool> enabled{true};
std::uintptr_t game = 0;
std::atomic<PeerNamer> namer{&ProductUserIdText};

struct Slot {
    std::int16_t v[3]{};
    std::uint64_t at = 0;  // 0 = never
};

SRWLOCK localLock = SRWLOCK_INIT;
Slot local[kHandAimSlots];
std::uint16_t sequence = 0;
bool loggedLocal = false;

struct Remote {
    char peer[kPeerChars]{};
    std::uint16_t sequence = 0;
    std::uint64_t lastPacket = 0;
    Slot slots[kHandAimSlots];
    bool loggedReceive = false;
    bool loggedApply = false;
};
SRWLOCK remoteLock = SRWLOCK_INIT;
Remote remotes[kPeers];

struct Due {
    const void* peer = nullptr;
    std::uint64_t at = 0;
    bool logged = false;
};
SRWLOCK dueLock = SRWLOCK_INIT;
Due due[kPeers];

// soldier+0x1ED0's User -> that member's ProductUserId text. A room's users stay put for a mission, so
// this is filled once per member and asked per shot.
struct Owner {
    std::uintptr_t user = 0;
    char peer[kPeerChars]{};
};
SRWLOCK ownerLock = SRWLOCK_INIT;
Owner owners[kPeers];

// The muzzle one catch-up shot was turned on, to be put back right after it (same thread, same weapon).
struct CatchUp {
    void* weapon = nullptr;
    unsigned char* entry = nullptr;
    unsigned char kept[kMuzzleEntrySize]{};
    bool active = false;
};
thread_local CatchUp catchUp;

// Diagnostics (2026-10-04: the first real test received every direction and turned no shot): how far each shot
// got, per path, the first case of each reason in full, and a summary every 10 s while anything moves.
enum Step { kSeen, kNotCall, kNoOwner, kNotListed, kNoMember, kNoDirection, kNoMuzzle, kNoTurn, kTurned, kCarried, kSteps };
struct PathStats {
    const char* name;
    std::atomic<unsigned long long> steps[kSteps]{};
    std::atomic<unsigned> explained{0};  // one bit per step: its first case was logged
    std::atomic<std::uint32_t> flags{0}, mode{0};
    std::atomic<bool> loggedTurn{false};
};
PathStats catchUpStats{"catch-up"}, perShotStats{"per-shot"};
std::atomic<std::uint64_t> summaryAt{0};
std::atomic<unsigned long long> summarised{0};

bool FirstOf(PathStats& stats, Step step) {
    const unsigned bit = 1u << step;
    return !(stats.explained.fetch_or(bit) & bit);
}

void Count(PathStats& stats, Step step) { stats.steps[step].fetch_add(1, std::memory_order_relaxed); }

void Summarise(std::uint64_t now) {
    const std::uint64_t last = summaryAt.load();
    if (last && now - last < 10000) return;
    unsigned long long total = 0;
    for (const PathStats* stats : {&catchUpStats, &perShotStats})
        for (const auto& step : stats->steps) total += step.load();
    if (total == summarised.exchange(total)) return;
    summaryAt.store(now);
    const auto& c = catchUpStats.steps;
    const auto& q = perShotStats.steps;
    Log("HANDAIM receive: catch-up shots %llu (flags 0x%X): turned %llu; left alone: not a call %llu, no owner %llu, "
        "not in the owner's list %llu, owner not a room member %llu, no fresh direction %llu, no muzzle %llu, turn failed %llu"
        " | per-shot shots %llu (flags 0x%X, mode %d): turned %llu, direction already in the message %llu; left alone: "
        "not a call %llu, no owner %llu, not in the owner's list %llu, owner not a room member %llu, no fresh direction "
        "%llu, no muzzle %llu, turn failed %llu",
        c[kSeen].load(), catchUpStats.flags.load(), c[kTurned].load(), c[kNotCall].load(), c[kNoOwner].load(),
        c[kNotListed].load(), c[kNoMember].load(), c[kNoDirection].load(), c[kNoMuzzle].load(), c[kNoTurn].load(),
        q[kSeen].load(), perShotStats.flags.load(), static_cast<int>(perShotStats.mode.load()), q[kTurned].load(),
        q[kCarried].load(), q[kNotCall].load(), q[kNoOwner].load(), q[kNotListed].load(), q[kNoMember].load(),
        q[kNoDirection].load(), q[kNoMuzzle].load(), q[kNoTurn].load());
}

std::uint32_t ReadU32(const void* object, std::size_t offset) {
    std::uint32_t value = 0;
    __try {
        std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return value;
}

std::uint64_t ReadU64(const void* object, std::size_t offset) {
    std::uint64_t value = 0;
    __try {
        std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return value;
}

std::int16_t Quantize(float value) {
    const float scaled = value * 32767.0f;
    if (!(scaled > -32767.0f)) return -32767;  // also NaN
    if (scaled > 32767.0f) return 32767;
    return static_cast<std::int16_t>(std::lround(scaled));
}

bool Unit(const float in[3], float out[3]) {
    const float length = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    if (!(length > 1e-6f) || !std::isfinite(length)) return false;
    for (int i = 0; i < 3; ++i) out[i] = in[i] / length;
    return true;
}

Remote* FindRemote(const char* peer, bool create, std::uint64_t nowMs) {
    Remote* oldest = &remotes[0];
    for (auto& remote : remotes) {
        if (remote.peer[0] && std::strncmp(remote.peer, peer, kPeerChars) == 0) return &remote;
        if (remote.lastPacket < oldest->lastPacket) oldest = &remote;
    }
    if (!create) return nullptr;
    *oldest = Remote{};
    strncpy_s(oldest->peer, peer, _TRUNCATE);
    (void)nowMs;
    return oldest;
}

bool ReadPointer(std::uintptr_t address, std::uintptr_t& value) {
    __try {
        value = *reinterpret_cast<const std::uintptr_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void SetHandAimEnabled(bool on) { enabled.store(on, std::memory_order_relaxed); }
bool HandAimEnabled() { return enabled.load(std::memory_order_relaxed); }

bool IsMultiSlotPacket(const void* data, std::uint32_t size) {
    if (!data || size < sizeof(kMagic)) return false;
    __try {
        return std::memcmp(data, kMagic, sizeof(kMagic)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::size_t BuildHandAimPacket(std::uint8_t* out, std::size_t size, std::uint64_t nowMs) {
    if (!out || size < kHandAimMaxPacket || !HandAimEnabled()) return 0;
    AcquireSRWLockExclusive(&localLock);
    std::size_t used = kHeader;
    std::uint8_t count = 0;
    for (int slot = 0; slot < kHandAimSlots; ++slot) {
        const Slot& s = local[slot];
        if (!s.at || nowMs - s.at > kHandAimLocalLifeMs) continue;
        out[used] = static_cast<std::uint8_t>(slot);
        std::memcpy(out + used + 1, s.v, sizeof(s.v));
        used += kEntry;
        ++count;
    }
    if (count) {
        ++sequence;
        std::memcpy(out, kMagic, sizeof(kMagic));
        out[4] = kVersion;
        out[5] = kTypeHandAim;
        std::memcpy(out + 6, &sequence, sizeof(sequence));
        out[8] = count;
    }
    ReleaseSRWLockExclusive(&localLock);
    return count ? used : 0;
}

bool ReceiveHandAimPacket(const char* peer, const void* data, std::uint32_t size, std::uint64_t nowMs) {
    if (!peer || !peer[0] || !IsMultiSlotPacket(data, size) || size < kHeader) return false;
    std::uint8_t packet[kHandAimMaxPacket]{};
    const std::size_t length = size < sizeof(packet) ? size : sizeof(packet);
    __try {
        std::memcpy(packet, data, length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    // A later version or another type is left for the build that knows it.
    if (packet[4] != kVersion || packet[5] != kTypeHandAim) return false;
    const std::uint8_t count = packet[8];
    if (count > kHandAimSlots || kHeader + count * kEntry > length) return false;
    std::uint16_t seq = 0;
    std::memcpy(&seq, packet + 6, sizeof(seq));
    bool first = false;
    AcquireSRWLockExclusive(&remoteLock);
    Remote* remote = FindRemote(peer, true, nowMs);
    // Unreliable and unordered: an older packet than the last one kept is dropped, unless the sender went
    // quiet long enough to have restarted its count.
    const bool fresh = !remote->lastPacket || nowMs - remote->lastPacket > 2000 ||
                       static_cast<std::int16_t>(static_cast<std::uint16_t>(seq - remote->sequence)) > 0;
    if (fresh) {
        remote->sequence = seq;
        remote->lastPacket = nowMs;
        for (std::uint8_t i = 0; i < count; ++i) {
            const std::uint8_t* entry = packet + kHeader + i * kEntry;
            if (entry[0] >= kHandAimSlots) continue;
            Slot& s = remote->slots[entry[0]];
            std::memcpy(s.v, entry + 1, sizeof(s.v));
            s.at = nowMs;
        }
        first = !remote->loggedReceive;
        remote->loggedReceive = true;
    }
    ReleaseSRWLockExclusive(&remoteLock);
    if (first) Log("HANDAIM first hand directions from EOS %s (%u weapon slots)", peer, count);
    return true;
}

bool HandAimDue(const void* peer, std::uint64_t nowMs) {
    if (!peer) return false;
    bool due_ = false, first = false;
    AcquireSRWLockExclusive(&dueLock);
    Due* slot = nullptr;
    Due* oldest = &due[0];
    for (auto& d : due) {
        if (d.peer == peer) {
            slot = &d;
            break;
        }
        if (d.at < oldest->at) oldest = &d;
    }
    if (!slot) {
        *oldest = Due{peer, 0, false};
        slot = oldest;
    }
    if (!slot->at || nowMs - slot->at >= kHandAimSendGapMs) {
        slot->at = nowMs;
        due_ = true;
        first = !slot->logged;
        slot->logged = true;
    }
    ReleaseSRWLockExclusive(&dueLock);
    if (first && DetailLog()) {
        char text[40]{};
        HandAimPeerText(peer, text, sizeof(text));
        Log("HANDAIM sending hand directions to EOS %s", text[0] ? text : "(unknown)");
    }
    return due_;
}

void SetLocalHandAim(int slot, const float dir[3], std::uint64_t nowMs) {
    if (!HandAimEnabled() || slot < 0 || slot >= kHandAimSlots || !dir) return;
    float in[3]{}, unit[3]{};
    __try {
        std::memcpy(in, dir, sizeof(in));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!Unit(in, unit)) return;
    bool first = false;
    AcquireSRWLockExclusive(&localLock);
    Slot& s = local[slot];
    for (int i = 0; i < 3; ++i) s.v[i] = Quantize(unit[i]);
    s.at = nowMs ? nowMs : 1;
    first = !loggedLocal;
    loggedLocal = true;
    ReleaseSRWLockExclusive(&localLock);
    if (first) Log("HANDAIM EDF6VR is giving this machine's hand directions (first: weapon slot %d)", slot);
}

bool RemoteHandAim(const char* peer, int slot, float dir[3], std::uint64_t nowMs) {
    if (!HandAimEnabled() || !peer || !peer[0] || slot < 0 || slot >= kHandAimSlots || !dir) return false;
    std::int16_t v[3]{};
    bool found = false, first = false;
    AcquireSRWLockExclusive(&remoteLock);
    if (Remote* remote = FindRemote(peer, false, nowMs)) {
        const Slot& s = remote->slots[slot];
        if (s.at && nowMs - s.at <= kHandAimRemoteLifeMs) {
            std::memcpy(v, s.v, sizeof(v));
            found = true;
            first = !remote->loggedApply;
            remote->loggedApply = true;
        }
    }
    ReleaseSRWLockExclusive(&remoteLock);
    if (!found) return false;
    const float raw[3] = {v[0] / 32767.0f, v[1] / 32767.0f, v[2] / 32767.0f};
    if (!Unit(raw, dir)) return false;
    if (first) Log("HANDAIM a shot of EOS %s's weapon slot %d fired along their hand", peer, slot);
    return true;
}

void DescribeRemote(const char* peer, std::uint64_t nowMs, char* out, std::size_t size) {
    if (!out || !size) return;
    out[0] = 0;
    AcquireSRWLockShared(&remoteLock);
    const Remote* found = nullptr;
    for (const auto& remote : remotes)
        if (remote.peer[0] && peer && std::strncmp(remote.peer, peer, kPeerChars) == 0) found = &remote;
    if (!found) {
        _snprintf_s(out, size, _TRUNCATE, "nothing ever received from them");
    } else {
        int used = _snprintf_s(out, size, _TRUNCATE, "last packet %llu ms ago, slots:",
                               static_cast<unsigned long long>(nowMs - found->lastPacket));
        for (int slot = 0; slot < kHandAimSlots && used >= 0; ++slot) {
            if (!found->slots[slot].at) continue;
            const int more = _snprintf_s(out + used, size - static_cast<std::size_t>(used), _TRUNCATE, " %d (%llu ms)", slot,
                                         static_cast<unsigned long long>(nowMs - found->slots[slot].at));
            used = more < 0 ? -1 : used + more;
        }
    }
    ReleaseSRWLockShared(&remoteLock);
}

bool SoldierPeer(std::uintptr_t gameBase, const void* soldier, char* peer, std::size_t size) {
    if (!peer || size < kPeerChars) return false;
    peer[0] = 0;
    std::uintptr_t user = 0;
    if (!soldier || !ReadPointer(reinterpret_cast<std::uintptr_t>(soldier) + 0x1ED0, user) || !user) return false;
    AcquireSRWLockShared(&ownerLock);
    for (const auto& owner : owners)
        if (owner.user == user && owner.peer[0]) {
            strncpy_s(peer, size, owner.peer, _TRUNCATE);
            break;
        }
    ReleaseSRWLockShared(&ownerLock);
    if (peer[0]) return true;
    // Only a user that is in the room right now is named, and only through the id the room holds, so the
    // EOS SDK is never handed a pointer that merely sat at +0x1ED0 of something that is not a player.
    std::uintptr_t users = 0;
    const char* failure = nullptr;
    UserSlotsSnapshot snapshot{};
    if (!gameBase || !ReadCurrentRoomUsers(gameBase, users, failure) ||
        !ReadUserSlots(reinterpret_cast<const void*>(users), snapshot))
        return false;
    for (const auto& slot : snapshot.slots) {
        if (slot.object != user || !slot.productId) continue;
        HandAimPeerText(reinterpret_cast<const void*>(slot.productId), peer, size);
        break;
    }
    if (!peer[0]) return false;
    AcquireSRWLockExclusive(&ownerLock);
    Owner* free = &owners[0];
    for (auto& owner : owners)
        if (!owner.user) {
            free = &owner;
            break;
        }
    free->user = user;
    strncpy_s(free->peer, peer, _TRUNCATE);
    ReleaseSRWLockExclusive(&ownerLock);
    return true;
}

void InitHandAim(unsigned char* gameBase) { game = reinterpret_cast<std::uintptr_t>(gameBase); }

void SetHandAimPeerNamer(PeerNamer replacement) { namer.store(replacement ? replacement : &ProductUserIdText); }

const char* HandAimPeerText(const void* id, char* out, std::size_t size) { return namer.load()(id, out, size); }

void ResetHandAimForTest() {
    AcquireSRWLockExclusive(&localLock);
    for (auto& s : local) s = Slot{};
    sequence = 0;
    loggedLocal = false;
    ReleaseSRWLockExclusive(&localLock);
    AcquireSRWLockExclusive(&remoteLock);
    for (auto& r : remotes) r = Remote{};
    ReleaseSRWLockExclusive(&remoteLock);
    AcquireSRWLockExclusive(&dueLock);
    for (auto& d : due) d = Due{};
    ReleaseSRWLockExclusive(&dueLock);
    AcquireSRWLockExclusive(&ownerLock);
    for (auto& o : owners) o = Owner{};
    ReleaseSRWLockExclusive(&ownerLock);
    enabled.store(true, std::memory_order_relaxed);
    game = 0;
    catchUp = CatchUp{};
    for (PathStats* stats : {&catchUpStats, &perShotStats}) {
        for (auto& step : stats->steps) step.store(0);
        stats->explained.store(0);
        stats->flags.store(0);
        stats->mode.store(0);
        stats->loggedTurn.store(false);
    }
    summaryAt.store(0);
    summarised.store(0);
}

int OwnedWeaponSlot(const void* soldier, const void* weapon) {
    if (!soldier || !weapon) return -1;
    __try {
        const auto* bytes = static_cast<const unsigned char*>(soldier);
        const auto count = *reinterpret_cast<const std::uint64_t*>(bytes + kOwnedWeaponCount);
        const auto* list = *reinterpret_cast<void* const* const*>(bytes + kOwnedWeapons);
        if (!list || !count || count > 32) return -1;
        for (std::uint64_t i = 0; i < count && i < static_cast<std::uint64_t>(kHandAimSlots); ++i)
            if (list[i] == weapon) return static_cast<int>(i);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return -1;
}

bool MuzzleFireDirection(const void* weapon, const float matrix[4][4], float out[3]) {
    __try {
        const auto* fire = reinterpret_cast<const float*>(static_cast<const unsigned char*>(weapon) + kWeaponFireVector);
        float length = 0.0f;
        for (int j = 0; j < 3; ++j) {
            out[j] = fire[0] * matrix[0][j] + fire[1] * matrix[1][j] + fire[2] * matrix[2][j];
            length += out[j] * out[j];
        }
        length = std::sqrt(length);
        if (!std::isfinite(length) || length < 1e-4f) return false;
        for (int j = 0; j < 3; ++j) out[j] /= length;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TurnRows(float rows[4][4], const float from[3], const float to[3]) {
    const float cosine = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
    if (!std::isfinite(cosine) || cosine < -0.999f) return false;  // no shortest arc from half a turn away
    float axis[3] = {from[1] * to[2] - from[2] * to[1], from[2] * to[0] - from[0] * to[2], from[0] * to[1] - from[1] * to[0]};
    const float span = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (!std::isfinite(span)) return false;
    if (span < 1e-6f) return true;  // already along it
    for (float& a : axis) a /= span;
    // Rodrigues about a unit axis, as EDF6VR's FencerTurnRows: no term that divides by (1 + cos).
    const float angle = std::atan2(span, cosine), c = std::cos(angle), s = std::sin(angle);
    float turned[3][3]{};
    for (int r = 0; r < 3; ++r) {
        const float v[3] = {rows[r][0], rows[r][1], rows[r][2]};
        const float k[3] = {axis[1] * v[2] - axis[2] * v[1], axis[2] * v[0] - axis[0] * v[2], axis[0] * v[1] - axis[1] * v[0]};
        const float along = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2];
        for (int j = 0; j < 3; ++j) {
            turned[r][j] = v[j] * c + k[j] * s + axis[j] * along * (1 - c);
            if (!std::isfinite(turned[r][j])) return false;
        }
    }
    for (int r = 0; r < 3; ++r)
        for (int j = 0; j < 3; ++j) rows[r][j] = turned[r][j];
    return true;
}

namespace {

// 6904F7 / 690603 is still a call, so the shot is fired there and comes back to the "after" hook. Where it goes
// does not matter: the game's 696FD0, or EDF6VR's HookDualFire, which hands another player's weapon straight
// on to 696FD0 (1.5.38: the turn is the plugin's alone, VR or not, by agreement with the EDF6VR session).
bool CatchUpIsCall(std::uint32_t call) {
    if (!game) return false;
    __try {
        return *reinterpret_cast<const unsigned char*>(game + call) == 0xE8;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

unsigned char* MuzzleEntry(void* weapon, unsigned index) {
    __try {
        auto* entries = *reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(weapon) + 0x1D0);
        const auto count = *reinterpret_cast<const std::uint64_t*>(static_cast<unsigned char*>(weapon) + 0x1E0);
        if (!entries || !count || count > 64) return nullptr;
        auto* entry = entries + (index % count) * kMuzzleEntrySize;
        volatile unsigned char probe = entry[0];
        probe = entry[kMuzzleEntrySize - 1];
        (void)probe;
        return entry;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Both muzzle matrices (+0x50 and +0x90) turned so the shot leaves along `dir`; their points stay.
bool TurnMuzzle(void* weapon, unsigned char* entry, const float dir[3]) {
    __try {
        float first[4][4], second[4][4];
        std::memcpy(first, entry + 0x50, sizeof(first));
        std::memcpy(second, entry + 0x90, sizeof(second));
        float barrel[3]{};
        if (!MuzzleFireDirection(weapon, first, barrel) || !TurnRows(first, barrel, dir) || !TurnRows(second, barrel, dir))
            return false;
        std::memcpy(entry + 0x50, first, sizeof(first));
        std::memcpy(entry + 0x90, second, sizeof(second));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Everything between "another player's weapon is about to fire muzzle N" and its muzzle turned onto their hand;
// the step it stopped at, explained in full the first time for each path.
Step TurnBefore(PathStats& stats, void* weapon, unsigned muzzle, std::uint32_t call, std::uint64_t now) {
    const char* path = stats.name;
    if (!CatchUpIsCall(call)) {
        if (FirstOf(stats, kNotCall)) Log("HANDAIM %s: EDF+%X is no longer a call; left alone", path, call);
        return kNotCall;
    }
    void* owner = nullptr;
    __try {
        owner = *reinterpret_cast<void**>(static_cast<unsigned char*>(weapon) + kWeaponOwner);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        owner = nullptr;
    }
    if (!owner) {
        if (FirstOf(stats, kNoOwner)) Log("HANDAIM %s: weapon %p has no owner at +0x120", path, weapon);
        return kNoOwner;
    }
    const int slot = OwnedWeaponSlot(owner, weapon);
    if (slot < 0) {
        if (FirstOf(stats, kNotListed))
            Log("HANDAIM %s: weapon %p is not in its owner %p's list (+0x1950, %llu weapons)", path, weapon, owner,
                static_cast<unsigned long long>(ReadU64(owner, kOwnedWeaponCount)));
        return kNotListed;
    }
    char peer[40]{};
    float dir[3]{};
    if (!SoldierPeer(game, owner, peer, sizeof(peer))) {
        if (FirstOf(stats, kNoMember))
            Log("HANDAIM %s: the owner %p of weapon slot %d is not a room member (user %p)", path, owner, slot,
                reinterpret_cast<void*>(static_cast<std::uintptr_t>(ReadU64(owner, 0x1ED0))));
        return kNoMember;
    }
    if (!RemoteHandAim(peer, slot, dir, now)) {
        if (FirstOf(stats, kNoDirection)) {
            char held[160]{};
            DescribeRemote(peer, now, held, sizeof(held));
            Log("HANDAIM %s: no fresh direction from EOS %s for weapon slot %d (%s)", path, peer, slot, held);
        }
        return kNoDirection;
    }
    unsigned char* entry = MuzzleEntry(weapon, muzzle);
    if (!entry) {
        if (FirstOf(stats, kNoMuzzle)) Log("HANDAIM %s: weapon %p has no readable muzzle entry %u", path, weapon, muzzle);
        return kNoMuzzle;
    }
    __try {
        std::memcpy(catchUp.kept, entry, kMuzzleEntrySize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return kNoMuzzle;
    }
    if (!TurnMuzzle(weapon, entry, dir)) {
        __try {
            std::memcpy(entry, catchUp.kept, kMuzzleEntrySize);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (FirstOf(stats, kNoTurn)) {
            float matrix[4][4]{}, barrel[3]{};
            __try {
                std::memcpy(matrix, entry + 0x50, sizeof(matrix));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            const bool readable = MuzzleFireDirection(weapon, matrix, barrel);
            Log("HANDAIM %s: the muzzle of weapon %p could not be turned (barrel %s %.3f %.3f %.3f, onto %.3f %.3f %.3f)",
                path, weapon, readable ? "" : "unreadable", barrel[0], barrel[1], barrel[2], dir[0], dir[1], dir[2]);
        }
        return kNoTurn;
    }
    catchUp.weapon = weapon;
    catchUp.entry = entry;
    catchUp.active = true;
    if (!stats.loggedTurn.exchange(true))
        Log("HANDAIM turned another player's %s shot onto their hand here (weapon slot %d of EOS %s)", path, slot, peer);
    return kTurned;
}

void CatchUpBefore(CpuContext* context, std::uint32_t call) {
    catchUp = CatchUp{};  // a shot whose "after" never came (an exception in 696FD0) is not waited for
    if (!HandAimEnabled()) return;
    const std::uint64_t now = GetTickCount64();
    auto* weapon = reinterpret_cast<void*>(static_cast<std::uintptr_t>(context->rbx));
    Count(catchUpStats, kSeen);
    catchUpStats.flags.store(ReadU32(weapon, 0x153C) & 0xFF);
    Count(catchUpStats, TurnBefore(catchUpStats, weapon, 0, call, now));  // both loops and the replay fire muzzle 0
    Summarise(now);
}

// 692A12, before the per-shot receive's `call 696FD0` (692540: rsi the weapon, rdi the muzzle). A weapon in mode 0
// or 1 had its muzzle in the message, which 7606A0 has just written (EDF6VR puts the hand there at 690D3B); any
// other mode sends none, and the shot leaves along this machine's copy of the owner's single aim (the third real
// test, 2026-10-04 12:02: a Fencer's hand cannon, Weapon_HeavyShoot, flags 0 and mode -1) - so that one is turned.
void PerShotBefore(CpuContext* context) {
    catchUp = CatchUp{};
    if (!HandAimEnabled()) return;
    const std::uint64_t now = GetTickCount64();
    auto* weapon = reinterpret_cast<void*>(static_cast<std::uintptr_t>(context->rsi));
    const std::uint32_t mode = ReadU32(weapon, 0x1540);
    Count(perShotStats, kSeen);
    perShotStats.flags.store(ReadU32(weapon, 0x153C) & 0xFF);
    perShotStats.mode.store(mode);
    if (mode == 0 || mode == 1)
        Count(perShotStats, kCarried);
    else
        Count(perShotStats, TurnBefore(perShotStats, weapon, static_cast<std::uint32_t>(context->rdi), kPerShotCall, now));
    Summarise(now);
}

// After the shot: the muzzle goes back whole, as the game built it.
void PutBack(std::uint64_t weaponRegister) {
    if (!catchUp.active) return;
    if (catchUp.weapon == reinterpret_cast<void*>(static_cast<std::uintptr_t>(weaponRegister))) {
        __try {
            std::memcpy(catchUp.entry, catchUp.kept, kMuzzleEntrySize);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    catchUp = CatchUp{};
}
void CatchUpAfter(CpuContext* context) { PutBack(context->rbx); }
void PerShotAfter(CpuContext* context) { PutBack(context->rsi); }

void BeforeFirst(CpuContext* context) { CatchUpBefore(context, kCatchUpCalls[0]); }
void BeforeSecond(CpuContext* context) { CatchUpBefore(context, kCatchUpCalls[1]); }
void BeforeThird(CpuContext* context) { CatchUpBefore(context, kCatchUpCalls[2]); }

}  // namespace

std::vector<MidSite> HandAimCatchUpHooks() {
    // `lea r9, [rbx+0xBD0]` (the shot counter argument) just before each `call 696FD0`, and
    // `cmp dword [rbx+0x368], 0` just after it: seven position-independent bytes each, and neither touches
    // the call itself, which is what EDF6VR redirects (and checks) when it is loaded.
    const std::vector<std::uint8_t> before = {0x4C, 0x8D, 0x8B, 0xD0, 0x0B, 0x00, 0x00};
    const std::vector<std::uint8_t> after = {0x83, 0xBB, 0x68, 0x03, 0x00, 0x00, 0x00};
    return {{"rapid-fire catch-up shot: turn (first loop)", kCatchUpBefore[0], before, 0, 7},
            {"rapid-fire catch-up shot: put back (first loop)", kCatchUpAfter[0], after, 0, 7},
            {"rapid-fire catch-up shot: turn (second loop)", kCatchUpBefore[1], before, 0, 7},
            {"rapid-fire catch-up shot: put back (second loop)", kCatchUpAfter[1], after, 0, 7},
            // 6947E0's paced replay: `mov byte [rsp+0x20], 1` before its call (694894; mov r9, rdi is before it, then
            // xor r8d, r8d; xor edx, edx; mov rcx, rbx), and the same `cmp dword [rbx+0x368], 0` after it.
            {"rapid-fire paced replay shot: turn", kCatchUpBefore[2], {0xC6, 0x44, 0x24, 0x20, 0x01}, 0, 5},
            {"rapid-fire paced replay shot: put back", kCatchUpAfter[2], after, 0, 7},
            // The per-shot receive: `mov byte [rsp+0x20], 1` before its call at 692A24 (EDF6VR redirects that call),
            // and `mov eax, [rbp+0x7F]; cmp [rsi+0xBD0], eax` after it (nine bytes; the jge that follows reads the cmp).
            {"per-shot receive: turn", kPerShotReceive, {0xC6, 0x44, 0x24, 0x20, 0x01}, 0, 5},
            {"per-shot receive: put back", kPerShotAfter, {0x8B, 0x45, 0x7F, 0x39, 0x86, 0xD0, 0x0B, 0x00, 0x00}, 0, 9}};
}

MidHandler HandAimCatchUpHandler(std::uint32_t rva) {
    if (rva == kCatchUpBefore[0]) return &BeforeFirst;
    if (rva == kCatchUpBefore[1]) return &BeforeSecond;
    if (rva == kCatchUpBefore[2]) return &BeforeThird;
    if (rva == kCatchUpAfter[0] || rva == kCatchUpAfter[1] || rva == kCatchUpAfter[2]) return &CatchUpAfter;
    if (rva == kPerShotReceive) return &PerShotBefore;
    if (rva == kPerShotAfter) return &PerShotAfter;
    return nullptr;
}

}  // namespace multislot

extern "C" {

int MultiSlot_HandAimVersion() { return multislot::kHandAimApiVersion; }

void MultiSlot_SetHandAim(int slot, const float* dir) { multislot::SetLocalHandAim(slot, dir, GetTickCount64()); }

int MultiSlot_GetHandAim(const void* soldier, int slot, float* dir) {
    using namespace multislot;
    if (!HandAimEnabled() || !soldier || !dir) return 0;
    char peer[40]{};
    if (!SoldierPeer(game, soldier, peer, sizeof(peer))) return 0;
    return RemoteHandAim(peer, slot, dir, GetTickCount64()) ? 1 : 0;
}

}
