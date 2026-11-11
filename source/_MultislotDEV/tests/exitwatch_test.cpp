// Who ends the game (exitwatch.h): which modules are watched, and the SHUTDOWN text, cut to its buffer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <cstring>

#include "../src/exitwatch.h"

using namespace multislot;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}
}  // namespace

int main() {
    const wchar_t* windows = L"C:\\Windows";
    Check(!ExitWatchCovers(L"C:\\Windows\\System32\\KERNELBASE.dll", windows), "Windows' own modules are left alone");
    Check(!ExitWatchCovers(L"c:\\windows\\System32\\DriverStore\\FileRepository\\nv\\nvwgf2umx.dll", windows),
          "drivers too, whatever the case");
    Check(ExitWatchCovers(L"C:\\WindowsApps\\overlay.dll", windows), "a folder that only starts like Windows' is not it");
    Check(ExitWatchCovers(L"S:\\SteamLibrary\\steamapps\\common\\EARTH DEFENSE FORCE 6\\EOSSDK-Win64-Shipping.dll", windows),
          "EOS is watched");
    Check(ExitWatchCovers(L"S:\\Game\\steam_api64.dll", windows) && ExitWatchCovers(L"S:\\Game\\umbra_sandlot.dll", windows) &&
              ExitWatchCovers(L"S:\\Game\\winmm.dll", windows) && ExitWatchCovers(L"S:\\Game\\Mods\\Plugins\\EDF6ClearLoot.dll", windows),
          "Steam, Umbra, the loader and other plugins are watched");
    Check(!ExitWatchCovers(L"S:\\Game\\EDF.dll", windows), "EDF.dll keeps its own marker");
    Check(!ExitWatchCovers(L"S:\\Game\\Mods\\Plugins\\EDF6MultiSlot.dll", windows), "not this plugin");
    Check(!ExitWatchCovers(L"S:\\Game\\Mods\\Plugins\\edf6vr.dll", windows), "never the VR mod");
    Check(!ExitWatchCovers(L"", windows) && !ExitWatchCovers(nullptr, windows), "no path, no watch");

    char text[160];
    const auto length = DescribeExit(text, sizeof(text), "TerminateProcess", 0xC0000005u,
                                     "EOSSDK-Win64-Shipping.dll+1A2B3C < EDF.dll+12C8D8A");
    Check(length == std::strlen(text) &&
              std::strcmp(text, "the game was ended by TerminateProcess(0xC0000005) from EOSSDK-Win64-Shipping.dll+1A2B3C < "
                                "EDF.dll+12C8D8A") == 0,
          "function, code and callers");
    char small[24];
    const auto cut = DescribeExit(small, sizeof(small), "ExitProcess", 3, "steam_api64.dll+10");
    Check(cut == std::strlen(small) && cut == sizeof(small) - 1, "cut to the buffer, never past it");
    Check(DescribeExit(text, sizeof(text), "ExitProcess", 0, "") > 0 && std::strstr(text, "from ?") != nullptr,
          "no callers read");
    std::printf("exit watch: %d failures\n", failures);
    return failures ? 1 : 0;
}
