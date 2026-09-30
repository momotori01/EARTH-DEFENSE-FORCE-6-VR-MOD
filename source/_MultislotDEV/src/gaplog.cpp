#include "gaplog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>

#include "log.h"

namespace multislot {
namespace {

constexpr std::uint32_t kSiteRva = 0x59637C;
constexpr std::uint32_t kArrivalRva = 0x77AC60;
constexpr std::uint32_t kGiveUpRva = 0x77AB40;   // adopts the local position as the reference
constexpr std::uint32_t kStoreRva = 0x77AB00;    // reads a position out of the packet and stores it
constexpr std::uint32_t kDeserialRva = 0x592180; // SoldierBase::deserialize, whose flags decide that
constexpr std::uint32_t kReceived = 0x1840;      // checker +0x10: the position that last arrived
constexpr std::uint32_t kPending = 0x1851;       // checker +0x21: a position is waiting
constexpr std::uint32_t kFresh = 0x1852;         // checker +0x22: set with it by 77AC60
constexpr std::uint32_t kCounter = 0x1854;       // checker +0x24
constexpr std::uint32_t kTries = 0x1880;         // checker +0x50
// 1 / the smoothing fraction: the step the game takes is that fraction of the gap. Set from the INI.
double stepToGap = 20.0;
constexpr std::size_t kTracked = 32;  // room for a full mission's worth, so a late joiner always fits
constexpr std::uint32_t kFlagOffset = 0x1900;  // 1 when this call's target is the real one
// The gate inputs, all live at the hook. See the header for where each one is tested.
constexpr std::uint32_t kRunOffset = 0x128;        // bit 0 clear: NetworkUpdate takes its early exit
constexpr std::uint32_t kCheckerOffset = 0x1830;   // byte 0 zero: the sync checker does nothing
constexpr std::uint32_t kCheckerState = 0x1850;    // +0x20 of the checker, non-zero: it bails
constexpr std::uint32_t kCheckerThreshold = 0x1858;  // +0x28: how far out of sync it tolerates
constexpr std::uint32_t kCheckerPosition = 0x1840;   // +0x10: the position that arrived
constexpr std::uint32_t kPlayerPosition = 0x90;      // 77AB64 copies this into the above
constexpr std::uint32_t kCheckerTries = 0x1880;      // +0x50: counted against
constexpr std::uint32_t kCheckerTryLimit = 0x1884;   // +0x54
constexpr std::uint32_t kFlagsFromRbp = 0x10;      // [rbp-0x10]: the flags the checker wrote
constexpr std::uint64_t kReportMs = 1000;
constexpr std::uint64_t kForgetMs = 3000;  // an object that stopped being updated has left the mission
constexpr int kMaxLines = 4000;            // roughly an hour at a line a second, then it goes quiet
constexpr std::uint64_t kPosEveryMs = 100;  // ten a second: enough to align two machines by their motion
constexpr int kMaxPosLines = 40000;         // about twenty minutes for two players, then it goes quiet
constexpr std::uint32_t kFacing = 0x60;     // the transform; its third row is the way they are facing

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

double Length(const Vec3& v) {
    return std::sqrt(static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y +
                     static_cast<double>(v.z) * v.z);
}

Vec3 Minus(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

struct Tracked {
    const void* object = nullptr;
    unsigned label = 0;
    bool primed = false;      // a first sample only starts the differencing
    bool haveTarget = false;  // a second sample is needed before a speed can be had
    bool lastReal = false;    // the previous call used the real target, so its step is worth having
    std::uint32_t realCalls = 0;  // over the whole run: zero means this object is never interpolated
    std::uint64_t lastRealMs = 0;  // when the target last moved, which is the interval a speed needs
    std::uint32_t calls = 0;       // every call this window, real path or not
    std::uint32_t blocked = 0;     // calls that reached the tail with no correction
    bool announced = false;
    // The last blocked call's inputs, so the reason can be named.
    std::uint8_t run = 0, checker = 0, checkerState = 0, flags = 0, f1180 = 0, f2e8 = 0;
    std::uint32_t f39c = 0, f380 = 0, tries = 0, tryLimit = 0;
    float threshold = 0.0f;
    std::uint32_t cleared = 0;  // times the latch was put back to zero this window
    double rejected = 0.0;      // the error the checker refused to smooth, in game units
    std::uint32_t arrivals = 0;  // positions handed to the checker by 77AC60 this window
    std::uint32_t giveUps = 0;   // times 77AB40 adopted the local position instead
    std::uint32_t stores = 0;    // packets that actually carried a position (77AB00)
    std::uint32_t packets = 0;   // packets deserialised at all
    std::uint32_t withPos = 0;   // of those, how many had the position bit set
    std::uint32_t withAngles = 0;  // and how many bit 1: two of the angles at +0x1230, sent on the same 6-frame cycle
    std::uint8_t pending = 0, fresh = 0;
    std::uint32_t counter = 0;
    // Enough of the last state to notice a change worth a line of its own.
    std::uint8_t wasLatched = 0xFF;
    bool wasCorrecting = false;
    std::uint64_t sinceMs = 0;
    Vec3 where{};    // +0x90, this machine's idea of where they are
    Vec3 facing{};   // from the transform at +0x60
    std::uint64_t posMs = 0;
    std::uint32_t classRva = 0;
    Vec3 smoothed{};          // +0x1920 as the previous call left it
    Vec3 target{};            // where the network said they were, reconstructed from the step
    std::uint64_t lastMs = 0;
    // Collected since the last report.
    double offsetSum = 0.0, offsetMax = 0.0;  // the field's own length: the desync, in game units
    double lagSum = 0.0, lagMax = 0.0;        // that length divided by their speed: the desync as time
    std::uint32_t lagSamples = 0;
    double gapSum = 0.0, gapMax = 0.0, stepMax = 0.0, speedSum = 0.0, speedMax = 0.0;
    std::uint32_t samples = 0, speedSamples = 0;
};

Tracked tracked[kTracked];
unsigned char* gameBase = nullptr;
unsigned nextLabel = 1;
std::uint64_t reportedMs = 0;
int lines = 0;
int posLines = 0;
std::atomic<bool> meterOn{false};
SRWLOCK lock = SRWLOCK_INIT;

std::atomic<bool> clearLatch{false};
std::atomic<float> tolerance{0.0f};

template <typename T>
bool Write(std::uint64_t address, const T& value) {
    __try {
        *reinterpret_cast<T*>(static_cast<std::uintptr_t>(address)) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool Read(std::uint64_t address, T& value) {
    __try {
        value = *reinterpret_cast<const T*>(static_cast<std::uintptr_t>(address));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::size_t reportedIdle = static_cast<std::size_t>(-1);

// Called with the lock held. Writes one line per object the game is interpolating, then clears the
// window. Objects it is not interpolating have nothing to measure; they are counted, and the count is
// only written when it changes, so a mission does not repeat the same line every second.
void Report(std::uint64_t nowMs, double seconds) {
    std::size_t idle = 0;
    for (auto& t : tracked) {
        if (!t.object) continue;
        if (nowMs - t.lastMs > kForgetMs) {
            t = {};
            continue;
        }
        if (!t.samples) {
            // A player whose correction has worked before, but not this second: name the gate that is
            // shut. Anything that has never been corrected is a local or empty slot, and stays counted.
            if (t.realCalls && t.blocked && lines < kMaxLines) {
                ++lines;
                const char* why = !(t.run & 1)          ? "NetworkUpdate skips it entirely (+0x128 bit 0 clear)"
                                  : t.checker == 0      ? "the sync checker is off (+0x1830 zero)"
                                  : t.checkerState != 0 ? "the sync checker bailed (+0x1850 set)"
                                                        : "the checker ran and reported no correction";
                Log("DESYNC p%u NOT corrected for a whole second (%u calls): %s [run=%02X checker=%02X/%02X "
                    "flags=%02X 1180=%02X 39c=%u 2e8=%02X 380=%08X] really out by %.1f against a tolerance "
                    "of %.1f, tries %u/%u, latch cleared %u times",
                    t.label, t.blocked, why, t.run, t.checker, t.checkerState, t.flags, t.f1180, t.f39c,
                    t.f2e8, t.f380, t.rejected, static_cast<double>(t.threshold), t.tries, t.tryLimit,
                    t.cleared);
                Log("DESYNC p%u   that second: %u packets, %u carried a position, %u its angles, %u stored; %u "
                    "times the game adopted this machine's own position instead [pending=%02X fresh=%02X counter=%u]",
                    t.label, t.packets, t.withPos, t.withAngles, t.stores, t.giveUps, t.pending, t.fresh, t.counter);
            }
            ++idle;
            t.calls = 0;
            t.blocked = 0;
            t.cleared = 0;
            t.arrivals = 0;
            t.giveUps = 0;
            t.stores = 0;
            t.packets = 0;
            t.withPos = 0;
            t.withAngles = 0;
            continue;
        }
        t.blocked = 0;
        if (lines < kMaxLines) {
            ++lines;
            // Two rates, because they answer different things: the function runs once a frame, but only
            // some of those calls have a new target to aim at, and that second rate is how often this
            // object's position is actually being refreshed.
            Log("DESYNC p%u: drawn %.0f ms behind avg, %.0f ms worst; off by avg %.1f max %.1f units, new "
                "error avg %.1f max %.1f, target moved %.0f/s of %.0f calls/s, %.0f packets/s of which "
                "%.0f carried a position and %.0f its angles, gave up %.0f/s, their speed avg %.0f max %.0f",
                t.label, t.lagSamples ? t.lagSum / t.lagSamples : 0.0, t.lagMax, t.offsetSum / t.samples,
                t.offsetMax, t.gapSum / t.samples, t.gapMax, t.samples / seconds, t.calls / seconds,
                t.packets / seconds, t.withPos / seconds, t.withAngles / seconds, t.giveUps / seconds,
                t.speedSamples ? t.speedSum / t.speedSamples : 0.0, t.speedMax);
        } else if (lines == kMaxLines) {
            ++lines;
            Log("DESYNC line budget spent; no more per-second lines this run");
        }
        t.offsetSum = t.offsetMax = 0.0;
        t.lagSum = t.lagMax = 0.0;
        t.lagSamples = 0;
        t.arrivals = 0;
        t.giveUps = 0;
        t.stores = 0;
        t.packets = 0;
        t.withPos = 0;
        t.withAngles = 0;
        t.gapSum = t.gapMax = t.stepMax = t.speedSum = t.speedMax = 0.0;
        t.samples = t.speedSamples = 0;
        t.calls = 0;
    }
    if (idle != reportedIdle) {
        reportedIdle = idle;
        if (idle)
            Log("DESYNC %zu player object(s) are not being interpolated at all (596130 takes its early "
                "exit for them, so there is no gap to measure); they are not counted above", idle);
    }
}

Tracked* Find(const void* object, std::uint64_t nowMs) {
    Tracked* free = nullptr;
    for (auto& t : tracked) {
        if (t.object == object) return &t;
        if (!t.object && !free) free = &t;
    }
    // A full table gives up the least useful row rather than turning a newcomer away: an object that has
    // never once been interpolated has nothing to say, and a remote player arriving late must not be the
    // one that gets dropped.
    if (!free) {
        std::uint64_t oldest = ~0ull;
        for (auto& t : tracked)
            if (!t.realCalls && t.lastMs < oldest) {
                oldest = t.lastMs;
                free = &t;
            }
    }
    if (!free) return nullptr;
    *free = {};
    free->object = object;
    free->label = nextLabel++;
    free->lastMs = nowMs;
    return free;
}

// EDF+59637C, before `movups xmm0, [rdi+0x1920]`: rdi is the player, and +0x1920 still holds what the
// previous call stored there.
// Both take the checker in rcx, which is embedded in the player at +0x1830.
void CountFor(std::uint64_t checker, std::uint32_t Tracked::*counter) {
    if (!meterOn.load(std::memory_order_relaxed)) return;
    const auto object = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(checker - kCheckerOffset));
    AcquireSRWLockExclusive(&lock);
    for (auto& t : tracked)
        if (t.object == object) {
            ++(t.*counter);
            break;
        }
    ReleaseSRWLockExclusive(&lock);
}

// EDF+77AC60: only reached through SoldierBase's slot 110, which nothing appears to call.
void PositionArrivedHandler(CpuContext* context) { CountFor(context->rcx, &Tracked::arrivals); }

// EDF+77AB00: the real entry. rcx is the checker; the position it reads is stored at its +0x10.
void PositionStoredHandler(CpuContext* context) { CountFor(context->rcx, &Tracked::stores); }

// EDF+592180: one packet for this player. r8w's bit 0 says whether it carries a position at all; bit 1,
// counted too, is two of the angles at +0x1230, which the sender puts on the same six-frame cycle.
void DeserializeHandler(CpuContext* context) {
    if (!meterOn.load(std::memory_order_relaxed)) return;
    const auto object = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rcx));
    const bool carriesPosition = (context->r8 & 1) != 0;
    AcquireSRWLockExclusive(&lock);
    for (auto& t : tracked)
        if (t.object == object) {
            ++t.packets;
            if (carriesPosition) ++t.withPos;
            if (context->r8 & 2) ++t.withAngles;
            break;
        }
    ReleaseSRWLockExclusive(&lock);
}

// EDF+77AB40: the game gives up and takes this machine's own position as the reference instead.
void GaveUpHandler(CpuContext* context) { CountFor(context->rcx, &Tracked::giveUps); }

void SmoothedPositionHandler(CpuContext* context) {
    if (!meterOn.load(std::memory_order_relaxed)) return;
    const auto object = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rdi));
    // Only the three lanes the smoothing touches: the fourth, at +0x192C, is carried through untouched
    // by the shufps pair around the multiply and is not part of the position.
    Vec3 now{};
    if (!Read(context->rdi + 0x1920, now)) return;
    // Set a few instructions ago: 1 when xmm6 holds the position that came in, 0 when the call took the
    // early exit and xmm6 is still the (0, 0, 0, 1) constant.
    std::uint8_t real = 0;
    if (!Read(context->rdi + kFlagOffset, real)) return;
    const std::uint64_t nowMs = GetTickCount64();

    AcquireSRWLockExclusive(&lock);
    Tracked* t = Find(object, nowMs);
    if (t) {
        ++t->calls;
        Read(context->rdi + kPlayerPosition, t->where);
        // Ten a second, with everything needed to line this log up against another machine's.
        if (nowMs - t->posMs >= kPosEveryMs && posLines < kMaxPosLines) {
            t->posMs = nowMs;
            ++posLines;
            Vec3 facing{};
            Read(context->rdi + kFacing + 0x20, facing);  // third row of the transform
            // What the game believes is right, recovered the same way the gap is.
            const Vec3 step = Minus(now, t->smoothed);
            const Vec3 believed{static_cast<float>(t->where.x + step.x * stepToGap),
                                static_cast<float>(t->where.y + step.y * stepToGap),
                                static_cast<float>(t->where.z + step.z * stepToGap)};
            Vec3 received{};
            Read(context->rdi + kReceived, received);
            Log("POS p%u %s EDF+%X at (%.1f, %.1f, %.1f) facing (%.2f, %.2f, %.2f) believed (%.1f, %.1f, "
                "%.1f) received (%.1f, %.1f, %.1f) corrected=%d",
                t->label, t->realCalls ? "remote" : "own", t->classRva, static_cast<double>(t->where.x),
                static_cast<double>(t->where.y), static_cast<double>(t->where.z),
                static_cast<double>(facing.x), static_cast<double>(facing.y), static_cast<double>(facing.z),
                static_cast<double>(believed.x), static_cast<double>(believed.y),
                static_cast<double>(believed.z), static_cast<double>(received.x),
                static_cast<double>(received.y), static_cast<double>(received.z), real ? 1 : 0);
        }
        // A line whenever the picture changes, which is what a per-second average hides. Stalls last
        // tens of seconds, so these stay rare.
        std::uint8_t latched = 0;
        Read(context->rdi + kCheckerState, latched);
        if (t->wasLatched != latched || t->wasCorrecting != (real != 0)) {
            if (t->wasLatched != 0xFF && lines < kMaxLines) {
                ++lines;
                std::uint8_t pending = 0, fresh = 0;
                Read(context->rdi + kPending, pending);
                Read(context->rdi + kFresh, fresh);
                Log("DESYNC p%u %s after %llu ms: latch %u->%u, correcting %d->%d [pending=%02X fresh=%02X "
                    "arrivals=%u gaveup=%u]",
                    t->label, real ? "STARTS being corrected" : "STOPS being corrected",
                    static_cast<unsigned long long>(nowMs - t->sinceMs), t->wasLatched, latched,
                    t->wasCorrecting ? 1 : 0, real ? 1 : 0, pending, fresh, t->arrivals, t->giveUps);
            }
            t->wasLatched = latched;
            t->wasCorrecting = real != 0;
            t->sinceMs = nowMs;
        }
        // Which class this is, and where it sits, said once: labels running past p18 in a room that can
        // hold eight players means some of these are not players at all, and the size of the position
        // says whether the field holds world coordinates or something much smaller.
        if (!t->announced) {
            t->announced = true;
            const void* vtable = nullptr;
            if (Read(context->rdi, vtable) && lines < kMaxLines) {
                ++lines;
                const auto rva = gameBase && reinterpret_cast<const unsigned char*>(vtable) >= gameBase
                                     ? static_cast<unsigned long long>(
                                           reinterpret_cast<const unsigned char*>(vtable) - gameBase)
                                     : 0ull;
                // 17CDF28 AssultSoldier (Ranger), 17CF100 Engineer (Air Raider), 17CF5B8 HeavyArmor
                // (Fencer), 17D0FF8 PaleWing (Wing Diver), 17D24D8 SoldierBase. Those five are the only
                // classes whose slot 55 reaches 596130, so every object here is a player.
                t->classRva = static_cast<std::uint32_t>(rva);
                Log("DESYNC p%u is class EDF+%llX, field currently (%.2f, %.2f, %.2f)", t->label, rva,
                    static_cast<double>(now.x), static_cast<double>(now.y), static_cast<double>(now.z));
            }
        }
        if (real) {
            ++t->realCalls;
        } else if (t->realCalls) {
            // Only worth the reads once this object has been corrected at least once.
            ++t->blocked;
            Read(context->rdi + kRunOffset, t->run);
            Read(context->rdi + kCheckerOffset, t->checker);
            Read(context->rdi + kCheckerState, t->checkerState);
            Read(context->rbp - kFlagsFromRbp, t->flags);
            Read(context->rdi + 0x1180, t->f1180);
            Read(context->rdi + 0x39C, t->f39c);
            Read(context->rdi + 0x2E8, t->f2e8);
            Read(context->rdi + 0x380, t->f380);
            Read(context->rdi + kCheckerThreshold, t->threshold);
            Read(context->rdi + kPending, t->pending);
            Read(context->rdi + kFresh, t->fresh);
            Read(context->rdi + kCounter, t->counter);
            // What the checker is rejecting: where they really are, against where this machine has them.
            Vec3 arrived{}, here{};
            if (Read(context->rdi + kCheckerPosition, arrived) && Read(context->rdi + kPlayerPosition, here))
                t->rejected = Length(Minus(arrived, here));
            if (tolerance.load(std::memory_order_relaxed) > 0.0f)
                Write(context->rdi + kCheckerThreshold, tolerance.load(std::memory_order_relaxed));
            Read(context->rdi + kCheckerTries, t->tries);
            Read(context->rdi + kCheckerTryLimit, t->tryLimit);
            // The experiment: hand the checker back to itself. 77ADA1 bails on this byte, so clearing it
            // here lets the next call compute a correction instead of returning nothing.
            if (clearLatch.load(std::memory_order_relaxed) && t->checkerState != 0 && (t->run & 1) &&
                t->checker != 0) {
                std::uint8_t zero = 0;
                Write(context->rdi + kCheckerState, zero);
                ++t->cleared;
            }
        }
        // The step in the field was taken by the previous call, so it is that call's kind that decides
        // whether it means anything.
        if (t->primed && t->lastReal) {
            // The step the game just took is 5% of the gap it was closing, so the gap is the step times
            // twenty - and the value it was heading for is this one plus that gap.
            const Vec3 step = Minus(now, t->smoothed);
            const double gap = Length(step) * stepToGap;
            const Vec3 target{static_cast<float>(t->smoothed.x + step.x * stepToGap),
                              static_cast<float>(t->smoothed.y + step.y * stepToGap),
                              static_cast<float>(t->smoothed.z + step.z * stepToGap)};
            // Against when the target last moved, not against the previous frame: the frames in
            // between carried no new position, so using them would divide by far too short a time.
            if (t->haveTarget && nowMs > t->lastRealMs) {
                const double seconds = static_cast<double>(nowMs - t->lastRealMs) / 1000.0;
                const double speed = Length(Minus(target, t->target)) / seconds;
                t->speedSum += speed;
                if (speed > t->speedMax) t->speedMax = speed;
                ++t->speedSamples;
                // The offset expressed as time: at this speed, how long ago were they where they are
                // being drawn. Unitless, and directly the lead a shot would need.
                if (speed > 1.0) {
                    const double lagMs = Length(now) / speed * 1000.0;
                    t->lagSum += lagMs;
                    if (lagMs > t->lagMax) t->lagMax = lagMs;
                    ++t->lagSamples;
                }
            }
            t->target = target;
            t->haveTarget = true;
            t->lastRealMs = nowMs;
            t->gapSum += gap;
            if (gap > t->gapMax) t->gapMax = gap;
            // The offset the game is carrying right now, which is the distance on screen.
            const double offset = Length(now);
            t->offsetSum += offset;
            if (offset > t->offsetMax) t->offsetMax = offset;
            const double stepLength = Length(step);
            if (stepLength > t->stepMax) t->stepMax = stepLength;
            ++t->samples;
        }
        t->smoothed = now;
        t->primed = true;
        t->lastReal = real != 0;
        t->lastMs = nowMs;
    }
    if (!reportedMs) reportedMs = nowMs;
    else if (nowMs - reportedMs >= kReportMs) {
        const double seconds = static_cast<double>(nowMs - reportedMs) / 1000.0;
        reportedMs = nowMs;
        Report(nowMs, seconds);
    }
    ReleaseSRWLockExclusive(&lock);
}

}  // namespace

std::vector<MidSite> DesyncHooks() {
    return {
        // `movups xmm0, xmmword ptr [rdi + 0x1920]`: seven bytes, addressed off rdi, so they copy into
        // the thunk unchanged. The 5% multiply three bytes later is RIP-relative and must not be
        // displaced; it is also what [Smoothing] RemotePlayerPercent retargets, and this hook leaves it
        // alone.
        {"remote player smoothed position", kSiteRva, {0x0F, 0x10, 0x87, 0x20, 0x19, 0x00, 0x00}, 0, 7},
        // 77AC60's prologue: `push rbx; sub rsp, 0x20`, six position-independent bytes. It is the only
        // place a position from the network reaches the checker (it also sets +0x21/+0x22 and clears the
        // latch), and until now nothing counted whether it happens at all.
        {"sync position arrived", kArrivalRva, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20}, 0, 6},
        // 77AB40's prologue, `mov [rsp+8], rbx`: five position-independent bytes. This is the function
        // that takes the local position as the truth while the latch is set.
        {"sync gave up on the network position", kGiveUpRva, {0x48, 0x89, 0x5C, 0x24, 0x08}, 0, 5},
        // 77AB00's prologue, `push rbx; sub rsp, 0x30`: six position-independent bytes.
        {"position taken from a packet", kStoreRva, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30}, 0, 6},
        // SoldierBase::deserialize's prologue, `mov [rsp+8], rbx`. r8w's bit 0 decides whether the
        // packet carries a position at all - the gate at 5921A7.
        {"player state packet", kDeserialRva, {0x48, 0x89, 0x5C, 0x24, 0x08}, 0, 5},
    };
}

MidHandler DesyncHookHandler(std::uint32_t rva) {
    if (rva == kSiteRva) return &SmoothedPositionHandler;
    if (rva == kArrivalRva) return &PositionArrivedHandler;
    if (rva == kGiveUpRva) return &GaveUpHandler;
    if (rva == kStoreRva) return &PositionStoredHandler;
    if (rva == kDeserialRva) return &DeserializeHandler;
    return nullptr;
}

void InitDesyncMeter(unsigned char* base, float smoothingFraction) {
    gameBase = base;
    stepToGap = smoothingFraction > 0.0f ? 1.0 / static_cast<double>(smoothingFraction) : 20.0;
    if (DesyncMeterOn())
        Log("Desync meter: the game closes %.0f%% of the gap per call, so a step is multiplied by %.1f to "
            "recover it", static_cast<double>(smoothingFraction) * 100.0, stepToGap);
}

void SetClearSyncLatch(bool on) { clearLatch.store(on, std::memory_order_relaxed); }

void SetSyncTolerance(float units) { tolerance.store(units, std::memory_order_relaxed); }

void SetDesyncMeter(bool on) { meterOn.store(on, std::memory_order_relaxed); }

bool DesyncMeterOn() { return meterOn.load(std::memory_order_relaxed); }

void FlushDesync() {
    AcquireSRWLockExclusive(&lock);
    const std::uint64_t nowMs = GetTickCount64();
    const double seconds = reportedMs && nowMs > reportedMs ? static_cast<double>(nowMs - reportedMs) / 1000.0 : 1.0;
    reportedMs = nowMs;
    Report(nowMs, seconds);
    ReleaseSRWLockExclusive(&lock);
}

}  // namespace multislot
