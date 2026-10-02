// Runs the packet size hook's handler the way the thunk does - a saved register block with the game's stack
// right above it - against the real EDF.dll mapped without running it, and reads back what it logged.
//   PacketSizeTests EDF.dll work-folder
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "../src/log.h"
#include "../src/midhook.h"
#include "../src/packetsize.h"

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

bool Contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

std::size_t Count(const std::string& text, const char* needle) {
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
    return count;
}

// What the thunk leaves behind: the registers, then the game's stack at the hooked instruction. Longer than
// the handler's walk, so it never reads this test's own locals (which hold the very addresses under test).
struct Frame {
    CpuContext context{};
    std::uint64_t stack[512]{};
};
static_assert(offsetof(Frame, stack) == sizeof(CpuContext), "the game's stack starts right after the saved registers");

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: PacketSizeTests EDF.dll work-folder\n");
        return 2;
    }
    const HMODULE game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!game) {
        std::printf("FAIL: cannot map %ls\n", argv[1]);
        return 1;
    }
    const auto base = reinterpret_cast<std::uint64_t>(game);
    CreateDirectoryW(argv[2], nullptr);
    const std::wstring logPath = std::wstring(argv[2]) + L"\\packetsize.log";
    DeleteFileW(logPath.c_str());
    LogOpen(logPath.c_str());
    InitPacketSize(reinterpret_cast<unsigned char*>(game));
    const MidHandler handler = PacketSizeHookHandler(PacketSizeHooks()[0].rva);
    Check(handler != nullptr, "the hook has a handler");
    if (!handler) return 1;

    std::uint8_t payload[1300]{};
    for (std::size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<std::uint8_t>(i);
    const auto run = [&](std::uint32_t bytes, std::initializer_list<std::uint64_t> stack) {
        Frame frame{};
        frame.context.r8 = bytes;
        frame.context.rdx = reinterpret_cast<std::uint64_t>(payload);
        std::size_t i = 0;
        for (const std::uint64_t value : stack) frame.stack[i++] = value;
        handler(&frame.context);
        return ReadText(logPath);
    };
    const std::uint64_t syncReturn = base + 0x78D6FF;     // after MissionSync_Res's call to the record writer
    const std::uint64_t routineReturn = base + 0x78830B;  // after the per-frame object loop's call
    const std::uint64_t notAReturn = base + 0x12CFFDB;    // inside the image, but not after a call
    const std::uint64_t queueReturn = base + 0x12CF001 + 5;  // after 12CEE60's call to 12CFFD0

    std::string log = run(100, {queueReturn, syncReturn});
    Check(!Contains(log, "PACKET"), "a small message is not looked at");
    log = run(600, {queueReturn, 0x1234, routineReturn});
    Check(!Contains(log, "PACKET"), "a routine 600-byte message is not logged");
    log = run(600, {queueReturn, notAReturn, 42, syncReturn});
    Check(Count(log, "PACKET sync message 600 B, packet 608 of EOS's 1170: fits") == 1,
          "a sync message is logged whatever its size, with the packet it makes");
    Check(Contains(log, "from EDF+12CF006 < EDF+78D6FF head 00 01 02 03"), "with the calls it came through and its first bytes");
    Check(!Contains(log, "EDF+12CFFDB"), "a stack value after no call is left out");
    log = run(1250, {queueReturn, routineReturn});
    Check(Contains(log, "PACKET big message 1250 B, packet 1258 of EOS's 1170: OVER EOS's limit"),
          "a message over the limit is logged from anywhere and called what it is");
    Check(Contains(log, "[largest this run 1250 B]"), "and the largest so far goes with every line");
    log = run(1100, {queueReturn, routineReturn});
    Check(Contains(log, "PACKET big message 1100 B, packet 1108 of EOS's 1170: needs a packet of its own"),
          "a message too big to share a packet but under the limit says so");
    NoteSendRefused(1258, 1, 0, 2, 0x12C8C5A);
    log = ReadText(logPath);
    Check(Contains(log, "PACKET EOS refused a send: 1258 B on channel 1, reliability 0, result 2 (over EOS's 1170-byte limit), from EDF+12C8C5A"),
          "a refused send is logged");
    // The start sync's record writer, through the wrapper the own-loadout call (78EE4F) is redirected to:
    // the game's own 773840 runs against a packet writer laid out as the game's (bytes from +0x10, position
    // at +0x5F0), so the size logged is the real wire size of that record.
    using RecordFn = std::uint64_t (*)(void*, void*, void*, std::uint64_t);
    const auto ownLoadout = reinterpret_cast<RecordFn>(SortieRecordCallHandler(0x78EE4F));
    Check(ownLoadout != nullptr, "the own-loadout call has a wrapper");
    const auto recordSize = [&](std::int32_t small, float colour) {
        std::uint8_t record[0xD4]{};
        record[0] = 1;  // class
        for (std::size_t at = 4; at < 0x28; at += 4) std::memcpy(record + at, &small, 4);
        for (std::size_t at = 0x28; at < 0xA8; at += 4) std::memcpy(record + at, &colour, 4);
        for (std::size_t i = 0; i < 0x30; ++i) record[0xA8 + i] = static_cast<std::uint8_t>(i);
        static std::uint8_t writer[0x600];
        std::memset(writer, 0, sizeof(writer));
        std::int32_t index = 2;
        const std::uint64_t result = ownLoadout(&index, record, writer, ~0ull);
        std::uint64_t written = 0;
        std::memcpy(&written, writer + 0x5F0, sizeof(written));
        Check((result & 0xFF) == 1, "the game's record writer ran and returned true");
        return written;
    };
    const std::uint64_t lightest = recordSize(1, 0.5f);         // small numbers, colours in 0..1: halves
    const std::uint64_t heaviest = recordSize(0x12345678, 1234.567f);  // every value at its widest encoding
    log = ReadText(logPath);
    char expect[160];
    std::snprintf(expect, sizeof(expect), "SORTIE own loadout written for the host: %llu B, the message %llu B so far",
                  static_cast<unsigned long long>(lightest), static_cast<unsigned long long>(lightest));
    Check(Contains(log, expect), "the record's size is logged as the writer's position moved");
    Check(lightest >= 100 && lightest < heaviest && heaviest <= 240, "a record is 100-240 bytes on the wire");
    std::printf("loadout record on the wire: %llu B with small values, %llu B with every value at its widest\n",
                static_cast<unsigned long long>(lightest), static_cast<unsigned long long>(heaviest));

    for (int i = 0; i < 400; ++i) run(1200, {queueReturn});
    log = ReadText(logPath);
    Check(Count(log, "line budget spent") >= 1, "the lines stop at the budget, saying so once");
    Check(Count(log, "PACKET big message") < 310, "and no more are written");

    if (failures) {
        std::printf("%d check(s) failed\n----- log -----\n%s\n", failures, ReadText(logPath).c_str());
        return 1;
    }
    std::printf("packet size handler verified against %ls\n", argv[1]);
    return 0;
}
