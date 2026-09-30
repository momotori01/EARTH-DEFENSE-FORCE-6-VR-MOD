#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace multislot {

// Wrappers on EDF.dll's EOS imports. Except for the explicitly scoped recovery send,
// calls and results pass through unchanged. Also routes the EOS SDK's Lobby/P2P/RTC log lines (which the
// game discards: its callback is an empty function) into the plugin log.
// Returns the number of import slots redirected.
int InstallNetLog(HMODULE game, bool diagnostics, bool recovery);

// [Sync] ReliableGameTraffic: EDF6 hands EOS every game packet as UnreliableUnordered, so a dropped one
// is simply gone and the two machines diverge. This sends them ReliableUnordered instead, which only the
// sender needs - EOS acknowledges on the far side by itself. Off unless asked for: a resent packet arrives
// after the packet that superseded it, and the game applies what it receives in arrival order, so this can
// also drag a player backwards. Measure with [Sync] TrafficMeter before believing either way.
void SetReliableGameTraffic(bool on);

// Call site EDF+12D5B9B only: one final hello when leaving Link::OnInitial.
void FinalHelloHook(void* manager, const void* peer, const char* token);

// EDF.dll ends the game by calling TerminateProcess on itself, which skips every DLL_PROCESS_DETACH,
// so the SHUTDOWN line added in 1.5.2 was never written once and every start reported the previous run
// as cut. Wrapping that import writes the line just before the process goes, and leaves a crash or a
// kill (which never reach it) correctly unmarked. True when the import was found and redirected.
bool InstallExitMarker(HMODULE game);

}  // namespace multislot
