#pragma once
#include <cstddef>

// The package ships no EDF6VR.ini, so extracting an update over an install
// never replaces the player's settings. The shipped defaults live inside the
// DLL instead, and on every start:
//  - no INI yet: the defaults are written out whole, comments and all;
//  - an INI already there: only the keys it lacks are added, with their
//    default values. Nothing the player has set is ever changed.
namespace edf6vr {
struct IniMergeResult { bool created=false; unsigned added=0; bool failed=false; };
IniMergeResult MergeIniDefaults(const char* defaults,std::size_t length,const wchar_t* iniPath) noexcept;

// Once per generation, the other way round: a file whose [Settings] Revision is
// below `revision` (or missing) is replaced whole by the defaults, after being
// copied to "<ini>.v<old>.bak" (v1 for a file with no Revision). Nothing is
// replaced if the copy cannot be made. [Render] ForceWidth/ForceHeight are
// carried over: they are the resolution the player chose for the headset with
// Set_Resolution.bat, not a setting whose meaning changed. MergeIniDefaults
// runs afterwards as always.
struct IniResetResult { bool reset=false; int from=0; bool keptResolution=false; bool failed=false; };
IniResetResult ResetOlderIni(const char* defaults,std::size_t length,const wchar_t* iniPath,int revision) noexcept;
}
