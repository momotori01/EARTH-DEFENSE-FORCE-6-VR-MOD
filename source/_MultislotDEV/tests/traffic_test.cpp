// The traffic meter (traffic.h): counting only when asked, one summary line per direction per minute,
// per-channel breakdown of the channels that carried something, the busiest second, and the window
// closing by itself once a minute has passed.
//   TrafficTests work-folder
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/log.h"
#include "../src/traffic.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::string ReadText(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

// Two stand-ins for EOS ProductUserId pointers; the meter only ever compares them.
const void* const kAlice = reinterpret_cast<const void*>(0x1000);
const void* const kBob = reinterpret_cast<const void*>(0x2000);

// A minute of routine sync: 100 packets of 100 bytes a second on channel 1 (80 kbps), with one busy
// second where channel 2 adds 200 packets of 1000 bytes.
void SendOneMinute(std::uint64_t start) {
    for (int second = 0; second < 60; ++second) {
        const std::uint64_t at = start + static_cast<std::uint64_t>(second) * 1000;
        for (int i = 0; i < 100; ++i) RecordSent(kAlice, 1, 100, at + static_cast<std::uint64_t>(i) * 10);
        if (second == 30)
            for (int i = 0; i < 200; ++i) RecordSent(kBob, 2, 1000, at + 500);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: TrafficTests <work-folder>\n");
        return 2;
    }
    const std::wstring folder = argv[1];
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring logPath = folder + L"/traffic.log";
    DeleteFileW(logPath.c_str());
    LogOpen(logPath.c_str());

    constexpr std::uint64_t t0 = 1000000;

    // Off: nothing is counted and nothing is written, however much goes past.
    SendOneMinute(t0);
    FlushTraffic(t0 + 60000);
    Check(!Contains(ReadText(logPath), "TRAFFIC"), "a meter that was never turned on writes nothing");

    SetTrafficMeter(true);
    Check(TrafficMeterOn(), "the meter reports itself on");
    SendOneMinute(t0);
    FlushTraffic(t0 + 60000);
    const std::string first = ReadText(logPath);

    // 60 s of 100x100 B on channel 1 plus 200x1000 B on channel 2 = 800000 B = 107 kbps.
    Check(Contains(first, "TRAFFIC sent over 60s: 107 kbps avg"), "the average over the window is right");
    // The busy second carried 100x100 + 200x1000 = 210000 B = 1680 kbps.
    Check(Contains(first, "busiest second 1680 kbps"), "the busiest second is the burst, not the average");
    Check(Contains(first, "6200 packets, 129 B avg, largest 1000"), "packets, mean size and the largest one");
    Check(Contains(first, "about 320 kbps"), "the line says what the game's own ceiling is");
    Check(Contains(first, "ch1 80 kbps 6000 pkt 100 B avg"), "the routine channel is broken out");
    // Per peer, the rate is what matters: 6000 packets over the minute is 100 a second.
    // Per peer the rate is what matters: 6000 packets over the minute is 100 a second, and the gap
    // between them was never worse than the 10 ms they were sent at.
    Check(Contains(first, "TRAFFIC sent peer-00001000: 100.0 packets/s, 80 kbps, 100 B avg, longest silence 10 ms"),
          "each peer gets its own rate and worst gap");
    Check(Contains(first, "TRAFFIC sent peer-00002000: 3.3 packets/s, 27 kbps, 1000 B avg"),
          "including a peer that got far fewer");
    Check(Contains(first, "ch2 27 kbps 200 pkt 1000 B avg"), "so is the bursty one");
    Check(!Contains(first, "ch0"), "a channel that carried nothing is not listed");
    Check(!Contains(first, "TRAFFIC received"), "and nothing is claimed for a direction that was silent");

    // Receiving is counted and reported separately.
    RecordReceived(kAlice, 1, 400, t0 + 60000);
    RecordReceived(kAlice, 1, 600, t0 + 61000);
    FlushTraffic(t0 + 62000);
    const std::string second = ReadText(logPath);
    // A second between the two arrivals is exactly the stall this column exists to catch.
    Check(Contains(second, "TRAFFIC received peer-00001000: 1.0 packets/s, 4 kbps, 500 B avg, longest silence 1000 ms"),
          "received is counted per peer, and a one-second stall shows as one");
    Check(Contains(second, "TRAFFIC received over 2s: 4 kbps avg"), "received traffic gets its own line");
    Check(Contains(second, "2 packets, 500 B avg, largest 600"), "with its own counts");

    // The window closes by itself: no Flush, just a packet after the minute is up.
    const std::size_t before = second.size();
    RecordSent(nullptr, 3, 250, t0 + 62000);
    RecordSent(nullptr, 3, 250, t0 + 122000);
    const std::string third = ReadText(logPath);
    Check(third.size() > before && Contains(third.substr(before), "TRAFFIC sent over 60s"),
          "a minute passing writes the summary without being asked");
    Check(Contains(third.substr(before), "ch3 0 kbps 2 pkt 250 B avg"), "and carries that window's channels");
    // Packets with no peer still count towards the totals, and invent no peer row.
    Check(!Contains(third.substr(before), "TRAFFIC sent peer-"), "a packet with no peer adds no peer line");
    // Alice sent nothing this window, so her row was given up rather than reported as idle.
    Check(!Contains(third.substr(before), "peer-00001000"), "a peer that went quiet drops out");

    // Off again: the counters stop, and the next window is empty rather than stale.
    SetTrafficMeter(false);
    Check(!TrafficMeterOn(), "the meter reports itself off");
    const std::size_t afterThird = third.size();
    SendOneMinute(t0 + 200000);
    FlushTraffic(t0 + 260000);
    Check(!Contains(ReadText(logPath).substr(afterThird), "TRAFFIC"), "turning it off stops both counting and writing");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("traffic meter: all checks passed\n");
    return 0;
}
