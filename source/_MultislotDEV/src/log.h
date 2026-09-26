#pragma once
#include <cstddef>

namespace multislot {

// Append-only text log. Safe to call from an exception handler: no heap, no CRT file locks.
void LogOpen(const wchar_t* path);
void LogWrite(const char* text, std::size_t length);
void Log(const char* format, ...);

// The log is one file that never grows much past kLogCapBytes: when it is opened or a write takes it over the
// cap, the oldest lines are dropped so that about the newest kLogKeepBytes remain, starting at a line boundary
// after a note that lines were dropped. Measured 2026-09-18: a start is about 3 KB, 30-40 minutes in a
// five-player room with NetLog=1 about 30-80 KB, a crash report 1-2 KB. 2 MB keeps the last few evenings (the
// crashed start survives many restarts), stays small enough to attach in a chat (Discord's free limit is
// 10 MB), and a trim during play copies at most 1.5 MB.
constexpr long long kLogCapBytes = 2LL * 1024 * 1024;
constexpr long long kLogKeepBytes = 1536LL * 1024;
// Trims the file at `path` that way when it is larger than `cap` (the log's own writes wait meanwhile);
// true when it did.
bool TrimLogFile(const wchar_t* path, long long cap, long long keep);

// How the run before this one ended, worked out in LogOpen by looking at what the file already holds:
// whichever of the startup banner and the SHUTDOWN line comes last in it. Two machines died mid-mission on
// 2026-09-19 with no exception and no shutdown, and nothing in the file could say whether they had crashed
// or simply been closed - this is what answers that next time.
enum class LastRun {
    Unknown,   // no log yet, or it was trimmed past both marks: say nothing
    Ended,     // a SHUTDOWN line after the last banner: the game was closed normally
    Cut,       // a banner with no SHUTDOWN after it: killed, hung, or died without reaching the handler
};
LastRun PreviousRun();
// Written from DLL_PROCESS_DETACH. Raw Win32, no heap and no CRT, so it is safe that late.
void LogShutdown(const char* why);

// Research lines that repeat during play (lobby and P2P events, room capacity checks, enemy count scaling) are
// written only with [MultiSlot] NetLog=1 (the default since 1.2.2); startup, menu and crash lines always are.
void SetDetailLog(bool on);
bool DetailLog();

}  // namespace multislot
