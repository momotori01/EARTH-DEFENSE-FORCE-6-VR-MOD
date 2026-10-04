// Runs the game's own join handshake update (Link::OnInitial, EDF+12D5AA0, event 1) against the real EDF.dll
// mapped without running it, on a fake clock, and records when it sends a hello, restarts the link (Close then
// Accept), calls the peer validated and gives up - with the game's own count and with JoinRetrySeconds.
// Only the clock import and the four calls it makes into the network layer are replaced; the routine's own
// timers, counter, deadline and branches are the game's.
//   JoinRetryTests EDF.dll
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/patches.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

constexpr long long kTicksPerSecond = 10000000;  // _Xtime_get_ticks: 100 ns (the routine divides by 1e7)
constexpr long long kFrame = kTicksPerSecond / 100;  // 10 ms a frame: 500 ms is exactly 50 of them

long long now = 0;
struct Event {
    char kind;  // h hello, c close, a accept, v validated
    double at;
};
std::vector<Event> events;

double Seconds() { return static_cast<double>(now) / kTicksPerSecond; }
long long FakeTicks() { return now; }
void FakeSend(void*, void*, void*) { events.push_back({'h', Seconds()}); }
void FakeClose(void*, void*) { events.push_back({'c', Seconds()}); }
void FakeAccept(void*, void*) { events.push_back({'a', Seconds()}); }
void FakeNextState(void*, void*, void*) { events.push_back({'v', Seconds()}); }

bool Write(unsigned char* at, const void* bytes, std::size_t size) {
    DWORD old = 0;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(at, bytes, size);
    VirtualProtect(at, size, old, &old);
    return FlushInstructionCache(GetCurrentProcess(), at, size) != 0;
}

// mov rax, imm64; jmp rax at a function's entry, so the routine's call lands in a stand-in.
bool JumpTo(unsigned char* function, const void* to) {
    unsigned char bytes[12] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    const auto address = reinterpret_cast<std::uint64_t>(to);
    std::memcpy(bytes + 2, &address, sizeof(address));
    return Write(function, bytes, sizeof(bytes));
}

struct Run {
    std::vector<double> hellos, closes, accepts;
    double validated = -1, timedOut = -1;
};

// One link from the state's entry to 25 s later. `host` gives the user the host bit (the 10 s deadline);
// `readyAt` sets the user's validated bit then (negative: never).
Run Simulate(unsigned char* base, bool host, double readyAt) {
    using OnInitial = void (*)(void* link, int event);
    const auto update = reinterpret_cast<OnInitial>(base + 0x12D5AA0);
    alignas(16) static unsigned char link[0x200], user[0x40], control[0x20], manager[0x40];
    std::memset(link, 0, sizeof(link));
    std::memset(user, 0, sizeof(user));
    std::memset(control, 0, sizeof(control));
    const auto put = [](unsigned char* object, std::size_t offset, auto value) {
        std::memcpy(object + offset, &value, sizeof(value));
    };
    // The user: +0x10 flags (bit 0 validated, bit 1 remote, bit 2 host), +0x18 its EOS id. Held through a
    // weak_ptr at Link+0x60/+0x68 whose control block keeps one use, so the routine's lock/unlock pair
    // never reaches the destructor.
    put(user, 0x10, static_cast<std::uint32_t>(host ? 0x6 : 0x2));
    put(user, 0x18, static_cast<std::uint64_t>(0x1234));
    put(control, 8, static_cast<std::uint32_t>(1));
    put(control, 0xC, static_cast<std::uint32_t>(1));
    put(link, 0x10, static_cast<void*>(manager));
    put(link, 0x60, static_cast<void*>(user));
    put(link, 0x68, static_cast<void*>(control));
    put(link, 0x88, static_cast<std::uint64_t>(15));  // the profile std::string at +0x70: short, inline
    // What the state's entry (12D5D99) leaves: count 0, first hello at once, deadline 10 s or 20 s from now.
    now = 0;
    put(link, 0x90, 0);
    put(link, 0x58, 0.0f);
    put(link, 0xA0, host ? 10000.0f : 20000.0f);
    put(link, 0x98, now);
    put(link, 0x50, now);
    events.clear();
    Run run;
    for (; now <= 25 * kTicksPerSecond; now += kFrame) {
        if (readyAt >= 0 && Seconds() >= readyAt - 1e-9) user[0x10] |= 1;
        update(link, 1);
        if (run.timedOut < 0 && link[0xA8]) run.timedOut = Seconds();
        if (!events.empty() && events.back().kind == 'v') break;
    }
    for (const Event& e : events) {
        if (e.kind == 'h') run.hellos.push_back(e.at);
        if (e.kind == 'c') run.closes.push_back(e.at);
        if (e.kind == 'a') run.accepts.push_back(e.at);
        if (e.kind == 'v' && run.validated < 0) run.validated = e.at;
    }
    return run;
}

bool Same(const std::vector<double>& got, std::initializer_list<double> want) {
    if (got.size() != want.size()) return false;
    std::size_t i = 0;
    for (const double w : want)
        if (std::fabs(got[i++] - w) > 0.001) return false;
    return true;
}

bool Near(double got, double want) { return std::fabs(got - want) < 0.001; }

void Print(const char* what, const Run& run) {
    std::printf("%-34s %2zu hellos, restarts at", what, run.hellos.size());
    for (const double t : run.closes) std::printf(" %.1f", t);
    if (run.closes.empty()) std::printf(" (none)");
    if (run.validated >= 0) std::printf(", validated at %.1f", run.validated);
    if (run.timedOut >= 0) std::printf(", gives up at %.1f", run.timedOut);
    std::printf(" s\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: JoinRetryTests EDF.dll\n");
        return 2;
    }
    const HMODULE game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!game) {
        std::printf("FAIL: cannot map %ls\n", argv[1]);
        return 1;
    }
    const auto base = reinterpret_cast<unsigned char*>(game);
    // 12D840B is `jmp [1755C28]`, MSVCP140's _Xtime_get_ticks; the import is unresolved in this mapping.
    Check(std::memcmp(base + 0x12D840B, "\xFF\x25", 2) == 0, "12D840B is an import thunk");
    {
        const void* ticks = reinterpret_cast<void*>(&FakeTicks);
        Check(Write(base + 0x1755C28, &ticks, sizeof(ticks)), "the clock import is replaced");
    }
    Check(JumpTo(base + 0x12C8F50, reinterpret_cast<void*>(&FakeSend)) && JumpTo(base + 0x12C7BE0, reinterpret_cast<void*>(&FakeClose)) &&
              JumpTo(base + 0x12C7180, reinterpret_cast<void*>(&FakeAccept)) &&
              JumpTo(base + 0x12D5F70, reinterpret_cast<void*>(&FakeNextState)),
          "hello send, Close, Accept and the next-state call are replaced");
    if (failures) return 1;

    const auto setRetry = [&](int seconds) {
        const auto vanilla = JoinRetryPatches(10);  // the original bytes, whatever the setting
        const auto patches = JoinRetryPatches(seconds);
        const auto& bytes = patches.empty() ? vanilla[0].original : patches[0].replacement;
        Check(Write(base + kJoinRetrySite, bytes.data(), bytes.size()), "the retry site is written");
    };
    // 40 hellos, one every 500 ms from the first update to 19.5 s (none at 20 s: the deadline is checked first).
    const auto everyHalfSecond = [](const Run& run) {
        bool even = run.hellos.size() == 40;
        for (std::size_t i = 0; even && i < run.hellos.size(); ++i) even = Near(run.hellos[i], 0.5 * static_cast<double>(i));
        return even;
    };

    // The game's own count: what the logs show (restart 5.0 s in, then every 5.5 s; gives up at 20 s).
    setRetry(5);
    Run member = Simulate(base, false, -1);
    Print("game's own, member never answers:", member);
    Check(everyHalfSecond(member), "vanilla: a hello every 500 ms, 40 of them");
    Check(Same(member.closes, {5.0, 10.5, 16.0}) && Same(member.accepts, {5.0, 10.5, 16.0}),
          "vanilla: restarts at 5.0, 10.5 and 16.0 s, each a Close and an Accept");
    Check(Near(member.timedOut, 20.0), "vanilla: a member link gives up at 20 s");
    Run host = Simulate(base, true, -1);
    Print("game's own, host never answers:", host);
    Check(Same(host.closes, {5.0}) && Near(host.timedOut, 10.0), "vanilla: the host link restarts at 5.0 s, gives up at 10 s");
    Run late = Simulate(base, false, 7.0);
    Print("game's own, member answers at 7 s:", late);
    Check(Same(late.closes, {5.0}) && Near(late.validated, 7.0), "vanilla: a peer validated at 7 s was restarted at 5 s first");

    // JoinRetrySeconds=10: the same hellos and the same deadlines; one restart at 10 s for a member, none
    // inside the host's 10 s.
    setRetry(10);
    member = Simulate(base, false, -1);
    Print("10 s, member never answers:", member);
    Check(everyHalfSecond(member), "10 s: the same 40 hellos, one every 500 ms");
    Check(Same(member.closes, {10.0}) && Same(member.accepts, {10.0}), "10 s: a member link restarts once, at 10.0 s");
    Check(Near(member.timedOut, 20.0), "10 s: and still gives up at 20 s");
    host = Simulate(base, true, -1);
    Print("10 s, host never answers:", host);
    Check(host.closes.empty() && Near(host.timedOut, 10.0), "10 s: the host link is never restarted and still gives up at 10 s");
    late = Simulate(base, false, 7.0);
    Print("10 s, member answers at 7 s:", late);
    Check(late.closes.empty() && Near(late.validated, 7.0), "10 s: a peer validated at 7 s is not restarted on the way");
    const Run early = Simulate(base, false, 2.0);
    Print("10 s, member answers at 2 s:", early);
    Check(early.closes.empty() && Near(early.validated, 2.0) && early.hellos.size() == 4,
          "10 s: a quick peer is validated as before (hellos at 0, 0.5, 1, 1.5)");

    setRetry(60);
    member = Simulate(base, false, -1);
    Print("60 s, member never answers:", member);
    Check(member.closes.empty() && Near(member.timedOut, 20.0), "60 s: no restart before the 20 s deadline");

    setRetry(5);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("join handshake timing verified against %ls\n", argv[1]);
    return 0;
}
