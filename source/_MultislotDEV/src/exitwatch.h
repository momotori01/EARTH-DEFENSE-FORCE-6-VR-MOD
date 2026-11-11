#pragma once
#include <cstddef>

namespace multislot {

// Who ends the game, when it is not EDF.dll.
//
// EDF.dll's own TerminateProcess writes SHUTDOWN (netlog.cpp InstallExitMarker), DLL_PROCESS_DETACH writes it
// for ExitProcess, and a crash leaves its own line here and a Windows error report. On 2026-11-10 two friends'
// games vanished on the way back to the lobby with none of these: their Windows logged no error either (error
// reporting on). Such an end is TerminateProcess or ExitProcess called by another module, or a kill from outside.
//
// This wraps both in the import table of every other module loaded from outside Windows' own folders (EOS, Steam,
// Umbra, XAudio, the mod loader, overlays...), now and every 30 s for modules loaded later. A call that ends this
// process writes the SHUTDOWN line with the function, the exit code, the module and the callers, then goes on to
// the function it replaced. Left out: EDF.dll (already marked), this plugin, and EDF6VR.dll (this plugin keeps
// out of the VR mod). If the game still vanishes with no SHUTDOWN line, it was ended from outside the process.
void StartExitWatch();

// For the tests: whether a module at `path` is watched. `windowsDir` is Windows' folder (GetWindowsDirectoryW).
bool ExitWatchCovers(const wchar_t* path, const wchar_t* windowsDir);
// For the tests: the SHUTDOWN text for an exit call. Returns its length.
std::size_t DescribeExit(char* out, std::size_t size, const char* function, unsigned code, const char* chain);

}  // namespace multislot
