// The log file cap (log.h): trimming keeps the newest whole lines after a note, opening and writing past
// 2 MB trim, and threads writing while a trim runs lose no line and break none.
//   LogTests work-folder
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "../src/log.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

constexpr char kNote[] = "[... older lines were dropped to keep this log under 2 MB ...]\r\n";
constexpr std::size_t kFillerLine = 64;

std::string ReadText(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// `count` 64-byte lines "line 00000000 xxx...\r\n" numbered from `first`.
std::string Filler(std::size_t first, std::size_t count) {
    std::string text;
    text.reserve(count * kFillerLine);
    char line[kFillerLine + 1];
    for (std::size_t i = 0; i < count; ++i) {
        std::snprintf(line, sizeof(line), "line %08zu ", first + i);
        std::size_t used = std::strlen(line);
        std::memset(line + used, 'x', kFillerLine - 2 - used);
        line[kFillerLine - 2] = '\r';
        line[kFillerLine - 1] = '\n';
        text.append(line, kFillerLine);
    }
    return text;
}

void WriteText(const std::wstring& path, const std::string& text) {
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool StartsWithNote(const std::string& text) { return text.compare(0, sizeof(kNote) - 1, kNote) == 0; }

std::vector<std::string> Lines(const std::string& text, std::size_t from) {
    std::vector<std::string> lines;
    while (from < text.size()) {
        const std::size_t end = text.find("\r\n", from);
        if (end == std::string::npos) {
            lines.push_back(text.substr(from));
            break;
        }
        lines.push_back(text.substr(from, end - from));
        from = end + 2;
    }
    return lines;
}

// Filler lines after the note must be whole and consecutive, ending with number `last`.
bool FillerIntact(const std::string& text, std::size_t last) {
    const auto lines = Lines(text, sizeof(kNote) - 1);
    if (lines.empty()) return false;
    std::size_t expected = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::size_t number = 0;
        if (lines[i].size() != kFillerLine - 2 || sscanf_s(lines[i].c_str(), "line %zu", &number) != 1) return false;
        if (i && number != expected) return false;
        expected = number + 1;
    }
    return expected == last + 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: LogTests work-folder\n");
        return 2;
    }
    const std::wstring folder = argv[1];
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring small = folder + L"\\small.log";
    const std::wstring big = folder + L"\\big.log";

    // Trim with small limits: the newest whole lines stay, behind the note.
    const std::string text = Filler(0, 5000);  // 320 KB
    WriteText(small, text);
    Check(!TrimLogFile(small.c_str(), 400 * 1024, 100 * 1024) && ReadText(small) == text, "a log under the cap is left alone");
    Check(TrimLogFile(small.c_str(), 200 * 1024, 100 * 1024), "a log over the cap is trimmed");
    std::string trimmed = ReadText(small);
    Check(StartsWithNote(trimmed), "a trimmed log starts with the note");
    Check(trimmed.size() == sizeof(kNote) - 1 + 100 * 1024, "a cut on a line boundary keeps exactly the newest part");
    WriteText(small, text);
    Check(TrimLogFile(small.c_str(), 200 * 1024, 100 * 1024 + 10), "a log over the cap is trimmed (cut inside a line)");
    trimmed = ReadText(small);
    Check(trimmed.size() == sizeof(kNote) - 1 + 100 * 1024, "a cut inside a line drops the rest of that line");
    Check(FillerIntact(trimmed, 4999), "the kept lines are whole, consecutive and end with the newest");
    Check(!TrimLogFile(small.c_str(), 200 * 1024, 100 * 1024), "a trimmed log is under the cap");

    Check(kLogCapBytes == 2LL * 1024 * 1024 && kLogKeepBytes == 1536LL * 1024, "the log keeps 1.5 MB once it passes 2 MB");

    // Opening a log over the cap trims it to the newest part, and new lines follow.
    std::size_t lines = static_cast<std::size_t>(kLogCapBytes / kFillerLine) + 16000;  // about 1 MB over
    WriteText(big, Filler(0, lines));
    LogOpen(big.c_str());
    trimmed = ReadText(big);
    Check(StartsWithNote(trimmed) && trimmed.size() <= sizeof(kNote) - 1 + static_cast<std::size_t>(kLogKeepBytes),
          "opening a log over 2 MB keeps the newest 1.5 MB");
    Check(FillerIntact(trimmed, lines - 1), "opening keeps whole lines up to the last one");
    Log("after open %d", 1);
    trimmed = ReadText(big);
    Check(trimmed.size() > 20 && trimmed.compare(trimmed.size() - 14, 14, "after open 1\r\n") == 0, "new lines are appended after the trim");

    // A write that takes the log over the cap trims it.
    lines = static_cast<std::size_t>(kLogCapBytes / kFillerLine) - 2;  // 128 bytes under the cap
    WriteText(big, Filler(0, lines));
    LogOpen(big.c_str());
    Check(ReadText(big).size() == lines * kFillerLine, "opening a log under 2 MB leaves it alone");
    const std::string longLine(300, 'y');
    Log("%s", longLine.c_str());
    trimmed = ReadText(big);
    Check(StartsWithNote(trimmed) && trimmed.size() <= sizeof(kNote) - 1 + static_cast<std::size_t>(kLogKeepBytes) + 400,
          "the write that crosses 2 MB trims the log");
    Check(trimmed.compare(trimmed.size() - 302, 302, longLine + "\r\n") == 0, "the crossing line is kept at the end");

    // A line that repeats is written once and counted, so one noisy source cannot fill the log.
    WriteText(big, std::string());
    LogOpen(big.c_str());
    Log("first");
    for (int i = 0; i < 500; ++i) Log("same line %d", 7);
    Log("different");
    trimmed = ReadText(big);
    const auto repeated = Lines(trimmed, 0);
    Check(repeated.size() == 4, "a repeated line is written once, with a count and the next line");
    Check(repeated[1].find("same line 7") != std::string::npos, "the first of the repeats is written");
    Check(repeated[2].find("(repeated 499 more times: same line 7)") != std::string::npos, "the rest are counted, quoting the line");
    Check(repeated[3].find("different") != std::string::npos, "the next line follows the count");
    Log("same line %d", 7);
    Log("same line %d", 8);
    Check(Lines(ReadText(big), 0).size() == 6, "a line that differs again is written as it is");

    // Threads keep writing while a trim runs: every line arrives once and whole.
    WriteText(big, Filler(0, static_cast<std::size_t>(kLogCapBytes / kFillerLine) - 3200));  // 200 KB under the cap
    LogOpen(big.c_str());
    static constexpr int kThreads = 4, kPerThread = 3000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([t] {
            for (int i = 0; i < kPerThread; ++i) Log("T%d L%05d", t, i);
        });
    for (auto& thread : threads) thread.join();
    trimmed = ReadText(big);
    Check(StartsWithNote(trimmed), "the log was trimmed while the threads wrote");
    Check(trimmed.size() <= static_cast<std::size_t>(kLogCapBytes), "the log stays under 2 MB");
    std::set<std::pair<int, int>> seen;
    bool whole = true;
    int duplicates = 0;
    for (const auto& line : Lines(trimmed, sizeof(kNote) - 1)) {
        if (line.rfind("line ", 0) == 0) {
            whole = whole && line.size() == kFillerLine - 2;
            continue;
        }
        int t = -1, i = -1;
        const std::size_t body = line.find("] T");
        if (line.size() != 26 + 9 || body != 24 || sscanf_s(line.c_str() + body + 2, "T%d L%d", &t, &i) != 2) {
            whole = false;
            continue;
        }
        if (!seen.insert({t, i}).second) ++duplicates;
    }
    Check(whole, "no line is broken or mixed with another");
    Check(duplicates == 0 && seen.size() == static_cast<std::size_t>(kThreads * kPerThread), "every thread line arrives exactly once");

    DeleteFileW(small.c_str());
    DeleteFileW(big.c_str());

    // How the previous run ended (log.h). LogOpen decides it from what the file already holds: whichever of
    // the startup banner and the SHUTDOWN line is last in it. A game that is killed writes neither, so the
    // next start finds its own banner last and says the run was cut.
    const std::wstring mark = L"lastrun.log";
    // The real log is CRLF; these fixtures are built the same way without an escape in sight.
    const auto line = [](const char* body) { return std::string(body) + char(13) + char(10); };
    const auto write = [&](const std::string& body, bool create = true) {
        DeleteFileW(mark.c_str());
        if (create) {
            std::ofstream out(mark, std::ios::binary);
            out << body;
        }
        LogOpen(mark.c_str());
        return PreviousRun();
    };
    const std::string banner = line("[2026-09-20 00:00:00.000] ==== EDF6MultiSlot 1.5.2 ====");
    const std::string ended = line("[2026-09-20 00:09:00.000] SHUTDOWN the game exited");
    const std::string busy = line("[2026-09-20 00:00:01.000] LOBBY room list refresh: ok 1, EOS result 0");
    Check(write("", false) == LastRun::Unknown, "no log at all says nothing about a previous run");
    Check(write(line("[2026-09-20 00:00:00.000] nothing to go on")) == LastRun::Unknown,
          "a log trimmed past both marks says nothing");
    Check(write(banner + busy) == LastRun::Cut, "a banner with no shutdown after it is a run that was cut");
    Check(write(banner + ended) == LastRun::Ended, "a shutdown line after the banner is a game that was closed");
    // Several runs in one file: only the last pair counts.
    Check(write(banner + ended + banner) == LastRun::Cut, "an earlier clean run does not cover for the last one");
    Check(write(banner + banner + ended) == LastRun::Ended, "and an earlier cut run does not spoil the last one");
    // The marks have to match the lines the plugin really writes.
    write("", false);
    Log("==== EDF6MultiSlot %s ====", "1.5.2");
    LogShutdown("the game exited");
    LogShutdown("twice");  // only the first one is written
    const std::string written = ReadText(mark);
    Check(written.find("==== EDF6MultiSlot 1.5.2 ====") != std::string::npos &&
              written.find("] SHUTDOWN the game exited") != std::string::npos &&
              written.find("twice") == std::string::npos,
          "the real banner and shutdown lines are the ones the marks look for, and shutdown is written once");
    LogOpen(mark.c_str());
    Check(PreviousRun() == LastRun::Ended, "and reading those two back says the run ended");
    DeleteFileW(mark.c_str());
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("log cap verified\n");
    return 0;
}
