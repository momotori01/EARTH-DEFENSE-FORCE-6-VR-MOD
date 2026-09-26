#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace multislot {

// Records the first few crash-class exceptions (code, faulting address, registers and the
// unwound call stack as module+offset) in the plugin log. It never handles an exception, so
// the game behaves exactly as without it; the log is what a 5-player test run leaves behind.
void InstallCrashLog(HMODULE game);

}  // namespace multislot
