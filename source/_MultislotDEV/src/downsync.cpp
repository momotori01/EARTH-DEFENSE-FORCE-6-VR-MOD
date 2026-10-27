#include "downsync.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include <map>
#include <string>
#include <unordered_map>

#include "code.h"
#include "log.h"

namespace multislot {

// ---- Pure parts ----

bool DownWindow(bool dead, bool grounded, bool blow) { return !dead && (!grounded || blow); }

bool ShouldForceSend(const ForceInput& in) {
    return in.on && in.host && in.alive && in.active && in.window && in.sendFrames > 0 &&
           in.framesSince >= in.sendFrames && (in.flags & 5) != 5;
}

void SendBudget::Reset(int cap, std::uint64_t nowMs) {
    cap_ = cap > 0 ? cap : 0;
    tokens_ = cap_ / 4;
    last_ = nowMs;
    refused_ = 0;
}

bool SendBudget::Take(std::uint64_t nowMs) {
    if (cap_ <= 0) return false;
    if (nowMs > last_) {
        tokens_ = std::min(cap_ / 4, tokens_ + static_cast<double>(nowMs - last_) * cap_ / 1000.0);
        last_ = nowMs;
    }
    if (tokens_ < 1.0) {
        ++refused_;
        return false;
    }
    tokens_ -= 1.0;
    return true;
}

constexpr std::size_t kSpreadKeep = 4096;

void Spread::Add(float value) {
    if (!std::isfinite(value)) return;
    sum_ += value;
    if (value > max_ || (values_.empty() && dropped_ == 0)) max_ = value;
    if (values_.size() < kSpreadKeep)
        values_.push_back(value);
    else
        ++dropped_;
}

float Spread::Mean() const {
    const std::size_t n = count();
    return n ? static_cast<float>(sum_ / static_cast<double>(n)) : 0.0f;
}

float Spread::Percentile(float p) const {
    if (values_.empty()) return 0.0f;
    std::vector<float> sorted(values_);
    std::sort(sorted.begin(), sorted.end());
    const float clamped = std::min(1.0f, std::max(0.0f, p));
    const auto index = static_cast<std::size_t>(clamped * static_cast<float>(sorted.size() - 1) + 0.5f);
    return sorted[std::min(index, sorted.size() - 1)];
}

void Spread::Clear() {
    values_.clear();
    dropped_ = 0;
    sum_ = 0;
    max_ = 0;
}

namespace {
constexpr std::uint8_t kPacketMagic[4] = {0x00, 0x00, 'M', 'S'};
constexpr std::uint8_t kPacketVersion = 1;
bool Recent(std::uint64_t at, std::uint64_t now, std::uint64_t life) { return at && now >= at && now - at <= life; }
}  // namespace

bool HostSideActive(bool switchOn, const std::vector<DownPeer>& peers, std::uint64_t nowMs) {
    if (!switchOn) return false;
    bool anyone = false;
    for (const auto& p : peers) {
        if (!Recent(p.sentAt, nowMs, kDownSyncMemberLifeMs)) continue;  // not (or no longer) in the room
        anyone = true;
        if (!Recent(p.switchAt, nowMs, kDownSyncWordLifeMs)) return false;
    }
    return anyone;
}

bool GuestSideActive(bool switchOn, const std::vector<DownPeer>& peers, std::uint64_t nowMs) {
    if (!switchOn) return false;
    for (const auto& p : peers)
        if (Recent(p.keepingAt, nowMs, kDownSyncWordLifeMs)) return true;
    return false;
}

std::size_t BuildDownSyncPacket(std::uint8_t* out, std::size_t size, std::uint8_t flags) {
    if (!out || size < kDownSyncPacketSize) return 0;
    std::memcpy(out, kPacketMagic, sizeof(kPacketMagic));
    out[4] = kPacketVersion;
    out[5] = kDownSyncPacketType;
    out[6] = flags;
    out[7] = 0;
    return kDownSyncPacketSize;
}

int ReadDownSyncPacket(const void* data, std::uint32_t size) {
    if (!data || size < kDownSyncPacketSize) return -1;
    std::uint8_t packet[kDownSyncPacketSize]{};
    __try {
        std::memcpy(packet, data, sizeof(packet));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    if (std::memcmp(packet, kPacketMagic, sizeof(kPacketMagic)) != 0 || packet[4] != kPacketVersion ||
        packet[5] != kDownSyncPacketType)
        return -1;
    return packet[6];
}

// ---- Tables ----

namespace {

constexpr std::uint32_t kSyncVtable = 0x17ABA10;  // net::WalkCharacterSyncControl
constexpr std::uint32_t kSyncSend = 0x77D500;     // its slot 0: serialise
constexpr std::uint32_t kSyncReceive = 0x77C2C0;  // its slot 1: receive
constexpr std::uint32_t kSnap = 0x77D610;         // jump to the newest snapshot
constexpr std::uint32_t kSendNow = 0x77D5C0;      // (object, 1): flags a send now
constexpr std::uint32_t kStateIs = 0x409370;      // (state machine, &{state, 0}): in that state
constexpr std::uint32_t kAntBlowState = 0x40E440; // the ant's blow reaction
constexpr std::uint32_t kAntUpdate = 0x409740, kSpiderUpdate = 0x437760, kDangoUpdate = 0x42BC90,
                        kRobotUpdate = 0x53DEC0;
constexpr std::uint32_t kInsectSlot = 0x1C8, kRobotSlot = 0x220;  // vtable slots 57 and 68

constexpr std::size_t kReplica = 0x128, kDead = 0x2E8, kPosition = 0x90, kAntStates = 0x1EA0;
constexpr std::size_t kActive = 0x1B0, kFlags = 0x180, kSequence = 0x1B1, kQueueWrite = 0x20, kQueueCount = 0x30,
                      kEntries = 0x40, kEntrySize = 0x30, kEntryPosition = 0x10;

std::size_t SyncOffset(DownKind kind) {
    switch (kind) {
    case DownKind::Ant: return 0x1F60;
    case DownKind::Spider: return 0x21E0;
    case DownKind::Dango: return 0x1EB0;
    case DownKind::Robot: return 0x1510;
    }
    return 0;
}
std::size_t GroundOffset(DownKind kind) { return kind == DownKind::Robot ? 0x11D0 : 0xE70; }
std::uint32_t UpdateTarget(DownKind kind) {
    switch (kind) {
    case DownKind::Ant: return kAntUpdate;
    case DownKind::Spider: return kSpiderUpdate;
    case DownKind::Dango: return kDangoUpdate;
    case DownKind::Robot: return kRobotUpdate;
    }
    return 0;
}

}  // namespace

const std::vector<DownSyncClass>& DownSyncClasses() {
    static const std::vector<DownSyncClass> classes{
        {"GiantAnt", 0x17BB798, DownKind::Ant},           // incl. E650 queen / gold queen
        {"GiantAntEX", 0x17BC648, DownKind::Ant},         // E662 red queen, E665
        {"GiantSpider", 0x17BE120, DownKind::Spider},     // incl. E651 queen
        {"GiantSpiderEX", 0x17BE7E0, DownKind::Spider},   // E667
        {"GiantDango", 0x17BDAC0, DownKind::Dango},
        {"Berserker_Large", 0x17ADA78, DownKind::Robot},
        {"Berserker_Middle", 0x17AE088, DownKind::Robot},
        {"Berserker_TypeA", 0x17AE968, DownKind::Robot},
        {"Berserker_TypeBommer", 0x17AED50, DownKind::Robot},
        {"Berserker_TypeGrapple", 0x17AF200, DownKind::Robot},
        {"Berserker_TypeShooter", 0x17AF9F8, DownKind::Robot},
    };
    return classes;
}

std::vector<DownSyncGate> DownSyncGates() {
    return {
        // ant host 409740: cmp byte [rbx+E70],0 ; je +68 (to 77C590 when in the air) -> je +0
        {"DownSync ant host: in the air", 0x409797, {0x80, 0xBB, 0x70, 0x0E, 0x00, 0x00, 0x00, 0x74, 0x68}, 8, 0x00, false},
        // ant guest 409C20: call 409370 (blow state 40E440) ; test al,al ; je +5 -> jmp +5 (skips "sync off")
        {"DownSync ant guest: blow reaction", 0x409CE0, {0xE8, 0x8B, 0xF6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x05}, 7, 0xEB, true},
        // spider host 437760: cmp byte [rbx+E70],0 ; je +34 -> je +0
        {"DownSync spider host: in the air", 0x4377AA, {0x80, 0xBB, 0x70, 0x0E, 0x00, 0x00, 0x00, 0x74, 0x34}, 8, 0x00, false},
        // Dango host 42BC90: cmp [rbx+E70],al (al = 0) ; je +1C -> je +0
        {"DownSync Dango host: in the air", 0x42BCF2, {0x38, 0x83, 0x70, 0x0E, 0x00, 0x00, 0x74, 0x1C}, 7, 0x00, false},
        // robot host 53DEC0: cmp byte [rbx+11D0],0 ; jne +13 (on the ground: on) -> jmp +13
        {"DownSync robot host: in the air", 0x53DF6C, {0x80, 0xBB, 0xD0, 0x11, 0x00, 0x00, 0x00, 0x75, 0x13}, 7, 0xEB, false},
    };
}

std::vector<PointerSlot> DownSyncSlots() {
    std::vector<PointerSlot> slots;
    for (const auto& c : DownSyncClasses()) {
        const std::uint32_t slot = c.kind == DownKind::Robot ? kRobotSlot : kInsectSlot;
        slots.push_back({c.name, c.vtable + slot, UpdateTarget(c.kind)});
    }
    return slots;
}

std::vector<PointerSlot> DownSyncSyncSlots() {
    return {{"DownSync sync send", kSyncVtable, kSyncSend}, {"DownSync sync receive", kSyncVtable + 8, kSyncReceive}};
}

std::vector<CallSite> DownSyncSnapCalls() {
    return {{"DownSync jump: five queued", 0x77C9B9, kSnap},
            {"DownSync jump: 40 m away", 0x77CA89, kSnap},
            {"DownSync jump: playback budget spent", 0x77CAA4, kSnap}};
}

// ---- Game side ----

namespace {

unsigned char* game = nullptr;
DownSyncSettings settings;
bool gatesReady = false;
std::atomic<bool> installed{false};
std::atomic<bool> downOn{false};       // this machine's switch (F9)
std::atomic<bool> hostActive{false};   // the host gates are written (everyone agrees)
std::atomic<bool> guestActive{false};  // the guest gate is written (the host says it keeps them)
std::atomic<std::uint64_t> ownedSeenMs{0};  // an enemy this machine owns was last updated
std::atomic<std::uint64_t> offSinceMs{0};   // the switch went off (the "off" word is sent for 5 s)
std::uint64_t lastRefreshMs = 0;
std::vector<DownSyncGate> gates;

// Members, by ProductUserId text (the packet hooks' thread and the update thread).
SRWLOCK peerLock = SRWLOCK_INIT;
std::map<std::string, DownPeer> peerMap;
std::map<std::string, std::uint64_t> wordAt;
std::map<std::string, bool> heardFrom;

using UpdateFn = void(__fastcall*)(void*);
using SyncFn = std::uint64_t(__fastcall*)(void*, void*, void*, void*);
using SnapFn = void(__fastcall*)(void*, void*);
using SendNowFn = void(__fastcall*)(void*, int);
using StateIsFn = bool(__fastcall*)(void*, const void*);

template <typename T>
T At(std::uint32_t rva) {
    return reinterpret_cast<T>(game + rva);
}

// SEH-only readers (no C++ objects in these).
struct Sample {
    std::uint64_t vtable;
    bool replica, dead, grounded, active;
    std::uint8_t flags;
    float position[3];
    unsigned char* sync;
};

bool ReadSample(void* self, DownKind kind, Sample& out) {
    __try {
        auto* p = static_cast<unsigned char*>(self);
        out.vtable = *reinterpret_cast<const std::uint64_t*>(p);
        out.replica = (p[kReplica] & 1) != 0;
        out.dead = p[kDead] != 0;
        out.grounded = p[GroundOffset(kind)] != 0;
        std::memcpy(out.position, p + kPosition, sizeof(out.position));
        out.sync = p + SyncOffset(kind);
        if (*reinterpret_cast<const std::uint64_t*>(out.sync) != reinterpret_cast<std::uint64_t>(game + kSyncVtable))
            return false;
        out.active = out.sync[kActive] != 0;
        out.flags = out.sync[kFlags];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool AntInBlow(void* self) {
    __try {
        alignas(16) std::uint64_t state[2] = {reinterpret_cast<std::uint64_t>(game + kAntBlowState), 0};
        return At<StateIsFn>(kStateIs)(static_cast<unsigned char*>(self) + kAntStates, state);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SendNow(unsigned char* sync) {
    __try {
        At<SendNowFn>(kSendNow)(sync, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct QueueState {
    std::uint64_t write, count;
    std::uint8_t sequence;
    float newest[3];
};

bool ReadQueue(const unsigned char* sync, QueueState& out) {
    __try {
        out.write = *reinterpret_cast<const std::uint64_t*>(sync + kQueueWrite);
        out.count = *reinterpret_cast<const std::uint64_t*>(sync + kQueueCount);
        out.sequence = sync[kSequence];
        const unsigned char* entry = sync + kEntries + NewestEntry(out.write) * kEntrySize + kEntryPosition;
        std::memcpy(out.newest, entry, sizeof(out.newest));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadPosition(const void* owner, float out[3]) {
    __try {
        std::memcpy(out, static_cast<const unsigned char*>(owner) + kPosition, sizeof(float) * 3);
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadActive(const void* sync) {
    __try {
        return static_cast<const unsigned char*>(sync)[kActive] != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SameBytes(const unsigned char* at, const std::uint8_t* bytes, std::size_t size) {
    __try {
        return std::memcmp(at, bytes, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool KeyDown(int key) {
    if (!key) return false;
    const HWND window = GetForegroundWindow();
    if (!window) return false;
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId() && (GetAsyncKeyState(key) & 0x8000) != 0;
}

float Distance(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---- Tracking ----

enum JumpReason { kQueued, kFar, kBudget, kReasons };
const char* const kReasonNames[kReasons] = {"five queued", "40 m", "budget"};

struct Enemy {
    std::uint64_t vtable = 0;
    DownKind kind = DownKind::Ant;
    const char* label = "?";
    unsigned char* sync = nullptr;
    bool replica = false;
    std::uint64_t seenMs = 0;
    // the current window
    bool window = false;
    bool mode = false;  // DownSync on when it began
    bool air = false, blow = false;
    std::uint64_t startMs = 0;
    int framesSince = 0;
    unsigned forced = 0, sends = 0, positionSends = 0, receipts = 0, jumps[kReasons]{};
    std::uint64_t maxQueue = 0;
    float awayMax = -1;
    // after it (guest): the first position that arrives, within 3 s
    bool post = false, postMode = false;
    std::uint64_t postMs = 0;
    unsigned postJumps[kReasons]{};
};

struct Bucket {
    unsigned windows = 0, air = 0, blow = 0;
    Spread seconds;
    unsigned sends = 0, positionSends = 0, forced = 0;
    Spread interval;
    unsigned receipts = 0, fed = 0;  // fed: flights during which at least one position came in (the host sends)
    std::uint64_t maxQueue = 0;
    Spread away;      // in flight: the farthest from the host's copy, one per window
    Spread landing;   // the first position after the window: how far it was (the would-be warp)
    unsigned noLanding = 0;
    unsigned jumps[kReasons]{}, postJumps[kReasons]{};
};

SRWLOCK lock = SRWLOCK_INIT;
std::unordered_map<void*, Enemy> enemies;        // by enemy
std::unordered_map<void*, void*> owners;         // sync object -> enemy
std::map<std::string, Bucket> buckets;           // label|role|mode
SendBudget budget;
std::uint64_t minuteStartMs = 0, lastPruneMs = 0, lastKeyMs = 0;
bool keyWasDown = false;
unsigned detailLines = 0;
constexpr unsigned kDetailLinesPerMinute = 40;
std::uint64_t forcedTotal = 0, jumpsOutside = 0;

Bucket& BucketFor(const Enemy& e, bool replica, bool mode) {
    return buckets[std::string(e.label) + (replica ? "|guest|" : "|host|") + (mode ? "on" : "off")];
}

void ResetWindow(Enemy& e) {
    e.air = e.blow = false;
    e.forced = e.sends = e.positionSends = e.receipts = 0;
    std::fill(std::begin(e.jumps), std::end(e.jumps), 0u);
    e.maxQueue = 0;
    e.awayMax = -1;
    e.framesSince = 0;
}

void FlushMinute(std::uint64_t now, const char* why) {
    // Caller holds the lock.
    for (auto& [key, b] : buckets) {
        if (!b.windows && !b.receipts && !b.sends) continue;
        const auto bar = key.find('|');
        const auto bar2 = key.find('|', bar + 1);
        const std::string label = key.substr(0, bar), role = key.substr(bar + 1, bar2 - bar - 1),
                          mode = key.substr(bar2 + 1);
        if (role == "host") {
            Log("DOWNSYNC %s %s host mode=%s: in the air or knocked down %u times (air %u, blow %u), %.2fs avg %.2fs max; "
                "positions sent %u of %u sends (forced %u), one every %.2fs median",
                why, label.c_str(), mode.c_str(), b.windows, b.air, b.blow, b.seconds.Mean(), b.seconds.Max(),
                b.positionSends, b.sends, b.forced, b.interval.Percentile(0.5f));
        } else {
            Log("DOWNSYNC %s %s guest mode=%s: in the air or knocked down %u times (air %u, blow %u), %.2fs avg %.2fs max; "
                "in it: %u positions in (during %u of them), queue max %llu, farthest from the host's copy %.1f m median %.1f p90 %.1f max, "
                "jumps %s/%s/%s=%u/%u/%u; first position after it: %zu arrived, %.1f m median %.1f p90 %.1f max away, "
                "%u none in 3 s, jumps after %u/%u/%u",
                why, label.c_str(), mode.c_str(), b.windows, b.air, b.blow, b.seconds.Mean(), b.seconds.Max(), b.receipts,
                b.fed, static_cast<unsigned long long>(b.maxQueue), b.away.Percentile(0.5f), b.away.Percentile(0.9f),
                b.away.Max(), kReasonNames[0], kReasonNames[1], kReasonNames[2], b.jumps[0], b.jumps[1], b.jumps[2],
                b.landing.count(), b.landing.Percentile(0.5f), b.landing.Percentile(0.9f), b.landing.Max(), b.noLanding,
                b.postJumps[0], b.postJumps[1], b.postJumps[2]);
        }
    }
    if (forcedTotal || budget.refused() || jumpsOutside)
        Log("DOWNSYNC %s totals: DownSync %s, forced sends %llu (refused by the %d/s cap %llu), jumps of watched enemies "
            "outside a flight %llu, enemies tracked %zu",
            why, hostActive.load() ? "on" : "off", static_cast<unsigned long long>(forcedTotal), settings.sendCap,
            static_cast<unsigned long long>(budget.refused()), static_cast<unsigned long long>(jumpsOutside),
            enemies.size());
    buckets.clear();
    forcedTotal = 0;
    jumpsOutside = 0;
    detailLines = 0;
    minuteStartMs = now;
}

void EndWindow(Enemy& e, std::uint64_t now) {
    // Caller holds the lock.
    Bucket& b = BucketFor(e, e.replica, e.mode);
    const float seconds = static_cast<float>(now - e.startMs) / 1000.0f;
    ++b.windows;
    b.air += e.air ? 1u : 0u;
    b.blow += e.blow ? 1u : 0u;
    b.seconds.Add(seconds);
    if (!e.replica) {
        b.sends += e.sends;
        b.positionSends += e.positionSends;
        b.forced += e.forced;
        if (e.positionSends) b.interval.Add(seconds / static_cast<float>(e.positionSends));
    } else {
        b.receipts += e.receipts;
        b.fed += e.receipts ? 1u : 0u;
        b.maxQueue = std::max(b.maxQueue, e.maxQueue);
        if (e.awayMax >= 0) b.away.Add(e.awayMax);
        for (int r = 0; r < kReasons; ++r) b.jumps[r] += e.jumps[r];
    }
    if (detailLines < kDetailLinesPerMinute) {
        ++detailLines;
        if (!e.replica)
            Log("DOWNSYNC flight %s host mode=%s: %.2fs (air %d, blow %d), positions sent %u (forced %u)", e.label,
                e.mode ? "on" : "off", seconds, e.air ? 1 : 0, e.blow ? 1 : 0, e.positionSends, e.forced);
        else
            Log("DOWNSYNC flight %s guest mode=%s: %.2fs (air %d, blow %d), positions in %u, queue max %llu, farthest "
                "from the host's copy %.1f m, jumps %u/%u/%u",
                e.label, e.mode ? "on" : "off", seconds, e.air ? 1 : 0, e.blow ? 1 : 0, e.receipts,
                static_cast<unsigned long long>(e.maxQueue), e.awayMax, e.jumps[0], e.jumps[1], e.jumps[2]);
    }
    e.window = false;
    if (e.replica) {
        e.post = true;
        e.postMode = e.mode;
        e.postMs = now;
        std::fill(std::begin(e.postJumps), std::end(e.postJumps), 0u);
    }
}

void EndPost(Enemy& e, float landing, std::uint64_t now) {
    Bucket& b = BucketFor(e, true, e.postMode);
    if (landing >= 0) {
        b.landing.Add(landing);
        if (detailLines < kDetailLinesPerMinute) {
            ++detailLines;
            Log("DOWNSYNC landed %s guest mode=%s: first position %.2fs after, %.1f m away", e.label,
                e.postMode ? "on" : "off", static_cast<float>(now - e.postMs) / 1000.0f, landing);
        }
    } else {
        ++b.noLanding;
    }
    for (int r = 0; r < kReasons; ++r) b.postJumps[r] += e.postJumps[r];
    e.post = false;
}

const DownSyncClass* ClassOf(std::uint64_t vtable) {
    for (const auto& c : DownSyncClasses())
        if (vtable == reinterpret_cast<std::uint64_t>(game + c.vtable)) return &c;
    return nullptr;
}

void ApplyGates(bool guestSide, bool on) {
    if (!gatesReady) return;
    for (const auto& g : gates) {
        if (g.guestSide != guestSide) continue;
        const std::uint8_t value = on ? g.patched : g.original[g.flip];
        WriteCode(game + g.rva + g.flip, &value, 1);
    }
}

// The gates follow what the room agrees (HostSideActive / GuestSideActive), checked before the game's own
// update runs, so a gate is never left over from another room for even one frame of code.
void RefreshGates(std::uint64_t now, bool force) {
    if (!gatesReady || (!force && now - lastRefreshMs < 50)) return;
    lastRefreshMs = now;
    std::vector<DownPeer> list;
    unsigned members = 0, agreeing = 0;
    AcquireSRWLockShared(&peerLock);
    for (const auto& entry : peerMap) {
        list.push_back(entry.second);
        const DownPeer& p = entry.second;
        if (p.sentAt && now >= p.sentAt && now - p.sentAt <= kDownSyncMemberLifeMs) {
            ++members;
            if (p.switchAt && now >= p.switchAt && now - p.switchAt <= kDownSyncWordLifeMs) ++agreeing;
        }
    }
    ReleaseSRWLockShared(&peerLock);
    const bool on = downOn.load();
    const bool host = HostSideActive(on, list, now);
    const bool guest = GuestSideActive(on, list, now);
    if (host != hostActive.load()) {
        ApplyGates(false, host);
        hostActive = host;
        if (host)
            Log("DOWNSYNC host side ON: all %u other members accept it; the insects and robots this machine owns are "
                "sent while they fly or lie in their blow", members);
        else
            Log("DOWNSYNC host side OFF (%s): enemies as the game has them", !on ? "this machine's switch is off"
                : !members ? "no other members" : "a member without this build or with its switch off");
    }
    if (guest != guestActive.load()) {
        ApplyGates(true, guest);
        guestActive = guest;
        if (guest)
            Log("DOWNSYNC guest side ON: the host keeps its downed enemies in sync; following them (%u of %u members "
                "accept it)", agreeing, members);
        else
            Log("DOWNSYNC guest side OFF (%s): enemies as the game has them",
                !on ? "this machine's switch is off" : "no word from the host that it keeps them in sync");
    }
}

void PollKey(std::uint64_t now) {
    if (!settings.key || now - lastKeyMs < 30) return;
    lastKeyMs = now;
    const bool down = KeyDown(settings.key);
    if (down && !keyWasDown) SetDownSync(!downOn.load(), "the key");
    keyWasDown = down;
}

void Observe(void* self, DownKind kind, UpdateFn original) {
    if (game) RefreshGates(GetTickCount64(), false);
    original(self);
    if (!game || !self) return;
    Sample s{};
    if (!ReadSample(self, kind, s)) return;
    const DownSyncClass* c = ClassOf(s.vtable);
    if (!c || c->kind != kind) return;
    const bool blow = kind == DownKind::Ant && !s.dead && s.grounded && AntInBlow(self);
    const bool window = DownWindow(s.dead, s.grounded, blow);
    const std::uint64_t now = GetTickCount64();
    PollKey(now);
    bool sendNow = false;
    AcquireSRWLockExclusive(&lock);
    if (!minuteStartMs) minuteStartMs = now;
    Enemy& e = enemies[self];
    if (e.vtable != s.vtable || e.sync != s.sync) {
        if (e.sync) owners.erase(e.sync);
        e = Enemy{};
        e.vtable = s.vtable;
        e.kind = kind;
        e.label = c->name;
        e.sync = s.sync;
    }
    owners[s.sync] = self;
    e.replica = s.replica;
    e.seenMs = now;
    if (!s.replica) ownedSeenMs = now;
    if (window && !e.window) {
        ResetWindow(e);
        e.window = true;
        e.mode = s.replica ? guestActive.load() : hostActive.load();
        e.startMs = now;
        if (e.post) EndPost(e, -1, now);
    }
    if (e.window) {
        e.air = e.air || !s.grounded;
        e.blow = e.blow || blow;
        ++e.framesSince;
        const ForceInput in{hostActive.load(), !s.replica, !s.dead, s.active, true, e.framesSince, settings.sendFrames,
                            s.flags};
        if (ShouldForceSend(in) && budget.Take(now)) {
            sendNow = true;
            e.framesSince = 0;
            ++e.forced;
            ++forcedTotal;
        }
        if (!window) EndWindow(e, now);
    }
    if (e.post && now - e.postMs > 3000) EndPost(e, -1, now);
    if (now - lastPruneMs > 5000) {
        lastPruneMs = now;
        for (auto it = enemies.begin(); it != enemies.end();) {
            if (now - it->second.seenMs > 5000) {
                owners.erase(it->second.sync);
                it = enemies.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (now - minuteStartMs >= 60000) FlushMinute(now, "minute");
    ReleaseSRWLockExclusive(&lock);
    if (sendNow) SendNow(s.sync);
}

void __fastcall AntUpdate(void* self) { Observe(self, DownKind::Ant, At<UpdateFn>(kAntUpdate)); }
void __fastcall SpiderUpdate(void* self) { Observe(self, DownKind::Spider, At<UpdateFn>(kSpiderUpdate)); }
void __fastcall DangoUpdate(void* self) { Observe(self, DownKind::Dango, At<UpdateFn>(kDangoUpdate)); }
void __fastcall RobotUpdate(void* self) { Observe(self, DownKind::Robot, At<UpdateFn>(kRobotUpdate)); }

std::uint64_t __fastcall SyncSend(void* sync, void* a, void* b, void* c) {
    const bool active = ReadActive(sync);
    const std::uint64_t result = At<SyncFn>(kSyncSend)(sync, a, b, c);
    AcquireSRWLockExclusive(&lock);
    const auto owner = owners.find(sync);
    if (owner != owners.end()) {
        const auto e = enemies.find(owner->second);
        if (e != enemies.end() && e->second.window) {
            ++e->second.sends;
            if (active) ++e->second.positionSends;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return result;
}

std::uint64_t __fastcall SyncReceive(void* sync, void* a, void* b, void* c) {
    QueueState before{};
    const bool readBefore = ReadQueue(static_cast<unsigned char*>(sync), before);
    const std::uint64_t result = At<SyncFn>(kSyncReceive)(sync, a, b, c);
    QueueState after{};
    if (!readBefore || !ReadQueue(static_cast<unsigned char*>(sync), after)) return result;
    // A full queue (five) takes a new snapshot over its newest entry: only the sequence moves then.
    const bool queued = after.write != before.write || after.count != before.count ||
                        (after.sequence != before.sequence && after.count >= 5);
    if (!queued) return result;
    const std::uint64_t now = GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    const auto owner = owners.find(sync);
    if (owner != owners.end()) {
        const auto found = enemies.find(owner->second);
        float here[3]{};
        if (found != enemies.end() && ReadPosition(owner->second, here)) {
            Enemy& e = found->second;
            const float away = Distance(here, after.newest);
            if (e.window) {
                ++e.receipts;
                e.maxQueue = std::max(e.maxQueue, after.count);
                e.awayMax = std::max(e.awayMax, away);
            } else if (e.post) {
                EndPost(e, away, now);
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return result;
}

void Snap(int reason, void* sync, void* outputs) {
    At<SnapFn>(kSnap)(sync, outputs);
    AcquireSRWLockExclusive(&lock);
    const auto owner = owners.find(sync);
    if (owner != owners.end()) {
        const auto found = enemies.find(owner->second);
        if (found != enemies.end()) {
            Enemy& e = found->second;
            if (e.window)
                ++e.jumps[reason];
            else if (e.post)
                ++e.postJumps[reason];
            else
                ++jumpsOutside;
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

void __fastcall SnapQueued(void* sync, void* outputs) { Snap(kQueued, sync, outputs); }
void __fastcall SnapFar(void* sync, void* outputs) { Snap(kFar, sync, outputs); }
void __fastcall SnapBudget(void* sync, void* outputs) { Snap(kBudget, sync, outputs); }

}  // namespace

void* DownSyncSlotHandler(std::uint32_t rva) {
    for (const auto& c : DownSyncClasses()) {
        const std::uint32_t slot = c.kind == DownKind::Robot ? kRobotSlot : kInsectSlot;
        if (c.vtable + slot != rva) continue;
        switch (c.kind) {
        case DownKind::Ant: return reinterpret_cast<void*>(&AntUpdate);
        case DownKind::Spider: return reinterpret_cast<void*>(&SpiderUpdate);
        case DownKind::Dango: return reinterpret_cast<void*>(&DangoUpdate);
        case DownKind::Robot: return reinterpret_cast<void*>(&RobotUpdate);
        }
    }
    return nullptr;
}

void* DownSyncSyncSlotHandler(std::uint32_t rva) {
    if (rva == kSyncVtable) return reinterpret_cast<void*>(&SyncSend);
    if (rva == kSyncVtable + 8) return reinterpret_cast<void*>(&SyncReceive);
    return nullptr;
}

void* DownSyncSnapHandler(std::uint32_t rva) {
    switch (rva) {
    case 0x77C9B9: return reinterpret_cast<void*>(&SnapQueued);
    case 0x77CA89: return reinterpret_cast<void*>(&SnapFar);
    case 0x77CAA4: return reinterpret_cast<void*>(&SnapBudget);
    }
    return nullptr;
}

void InitDownSync(unsigned char* gameBase, const DownSyncSettings& s) {
    game = gameBase;
    settings = s;
    budget.Reset(settings.sendCap, GetTickCount64());
}

bool StartDownSync(unsigned char* gameBase, const DownSyncSettings& s) {
    if (!game) InitDownSync(gameBase, s);
    gates = DownSyncGates();
    for (const auto& g : gates) {
        if (!SameBytes(game + g.rva, g.original.data(), g.original.size())) {
            Log("DOWNSYNC: %s (EDF+%X) is not the expected code; downed enemies are only measured, not kept in sync",
                g.name, g.rva);
            gates.clear();
            return false;
        }
    }
    gatesReady = true;
    installed = true;
    downOn = settings.on;
    if (!settings.on) offSinceMs = GetTickCount64();
    Log("DOWNSYNC ready: switch %s (%s toggles it). Downed insects and robots are kept in sync only while every "
        "member of the room has this build with its switch on, and the host says it does it; otherwise everyone's "
        "enemies stay exactly as the game has them. A forced send every %d frames, at most %d a second",
        settings.on ? "ON" : "OFF", settings.key ? "a key" : "no key", settings.sendFrames, settings.sendCap);
    return true;
}

bool DownSyncOn() { return downOn.load(); }

void SetDownSync(bool on, const char* why) {
    const std::uint64_t now = GetTickCount64();
    AcquireSRWLockExclusive(&lock);
    FlushMinute(now, on == downOn.load() ? "state" : "before the switch");
    ReleaseSRWLockExclusive(&lock);
    if (!gatesReady) on = false;
    if (!on && downOn.load()) offSinceMs = now;
    downOn = on;
    Log("DOWNSYNC switch %s by %s (the room still has to agree: every member with this build and its switch on)",
        on ? "ON" : "OFF", why);
    RefreshGates(now, true);
}

bool DownSyncInstalled() { return installed.load(); }

void NoteDownSyncPeer(const char* peer, std::uint64_t nowMs) {
    if (!installed.load() || !peer || !peer[0]) return;
    AcquireSRWLockExclusive(&peerLock);
    peerMap[peer].sentAt = nowMs;
    // Members gone for a minute are forgotten.
    for (auto it = peerMap.begin(); it != peerMap.end();) {
        const std::uint64_t last = std::max(it->second.sentAt, std::max(it->second.switchAt, it->second.keepingAt));
        if (nowMs > last && nowMs - last > 60000) {
            wordAt.erase(it->first);
            it = peerMap.erase(it);
        } else {
            ++it;
        }
    }
    ReleaseSRWLockExclusive(&peerLock);
}

std::size_t DownSyncWordFor(const char* peer, std::uint8_t* out, std::size_t size, std::uint64_t nowMs) {
    if (!installed.load() || !peer || !peer[0]) return 0;
    const bool on = downOn.load();
    // While the switch is on: "I accept it" (and "I keep mine in sync" when this machine owns enemies and the
    // room agrees). For 5 s after it goes off, the word without either, so the others stop at once; then nothing.
    const std::uint64_t off = offSinceMs.load();
    if (!on && !(off && nowMs >= off && nowMs - off < 5000)) return 0;
    bool due = false;
    AcquireSRWLockExclusive(&peerLock);
    std::uint64_t& last = wordAt[peer];
    if (!last || nowMs < last || nowMs - last >= kDownSyncWordGapMs) {
        last = nowMs;
        due = true;
    }
    ReleaseSRWLockExclusive(&peerLock);
    if (!due) return 0;
    const std::uint64_t owned = ownedSeenMs.load();
    const bool keeping = on && hostActive.load() && owned && nowMs >= owned && nowMs - owned <= 2000;
    const std::uint8_t flags = static_cast<std::uint8_t>((on ? kDownSyncSwitchOn : 0) | (keeping ? kDownSyncKeeping : 0));
    return BuildDownSyncPacket(out, size, flags);
}

bool ReceiveDownSyncPacket(const char* peer, const void* data, std::uint32_t size, std::uint64_t nowMs) {
    const int flags = ReadDownSyncPacket(data, size);
    if (flags < 0 || !peer || !peer[0]) return false;
    if (!installed.load()) return true;  // ours, but this machine takes no part
    bool first = false;
    AcquireSRWLockExclusive(&peerLock);
    DownPeer& p = peerMap[peer];
    p.switchAt = (flags & kDownSyncSwitchOn) ? nowMs : 0;
    p.keepingAt = (flags & kDownSyncKeeping) ? nowMs : 0;
    bool& heard = heardFrom[peer];
    first = !heard;
    heard = true;
    ReleaseSRWLockExclusive(&peerLock);
    if (first) Log("DOWNSYNC first word from EOS %s: switch %s%s", peer, (flags & kDownSyncSwitchOn) ? "on" : "off",
                   (flags & kDownSyncKeeping) ? ", keeps its downed enemies in sync" : "");
    return true;
}

}  // namespace multislot
