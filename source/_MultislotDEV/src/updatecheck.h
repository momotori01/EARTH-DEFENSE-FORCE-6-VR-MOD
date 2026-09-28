#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>

namespace multislot {

// "There is a newer EDF6VR package" in the menu's text field.
//
// Two friends could not start a mission together because their builds did not match, and nothing in the
// game told them. The VR package now ships Update_EDF6VR.bat, which compares against GitHub and updates
// after a Y/N - but someone has to know to run it. This says so, and nothing more: it downloads nothing
// and writes nothing. Downloading from inside the game was rejected on purpose (the DLLs are locked
// while it runs, antivirus dislikes it, and a bad build would reach everyone at once).
//
// It lives in MultiSlot's label rather than the VR mod's own because the VR mod would have to add a
// field to the same menu layout (Mods\UI\LYT_MAINFRAME.SGO) that MultiSlot already edits, and because
// someone playing flat has EDF6VR.dll renamed to .disabled and never loaded - MultiSlot still runs, so
// they are told too.
//
// This is the only thing in the mod that talks to anything outside the game, so it is one request, on
// its own thread, at startup, read-only, and silent about every failure. [Update] CheckEDF6VR=0 in the
// INI stops it being made at all.
struct Version {
    int major = -1, minor = -1, patch = -1;
    bool valid() const { return major >= 0; }
    bool NewerThan(const Version& other) const;
};

// "2.1.0" or "EDF6VR-2.1.0" or "EDF6VR 2.1.0 cockpit loading" -> {2,1,0}. Invalid when no x.y.z is found.
Version ParseVersion(const char* text);

// Starts the one request on its own thread. `gameFolder` is where EDF6.exe lives (EDF6VR\ and
// Mods\Plugins\ are read from there). Does nothing when `enabled` is false.
void StartUpdateCheck(const wchar_t* gameFolder, bool enabled);

// The line for the menu label, or an empty string while there is nothing to say - which is the case
// until the reply arrives, and for ever if it does not, or if what is installed is already current.
const wchar_t* UpdateNotice();

// Test seam: what the label should append, without a request. An empty string clears it.
void SetUpdateNoticeForTest(const wchar_t* text);

// What is installed, read the way Update-EDF6VR.ps1 reads it: EDF6VR\PACKAGE_MANIFEST.json first, then
// the version string inside EDF6VR.dll (or EDF6VR.dll.disabled). Invalid when neither is there.
Version InstalledVrVersion(const wchar_t* gameFolder);

}  // namespace multislot
