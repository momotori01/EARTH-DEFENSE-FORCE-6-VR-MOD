#pragma once
#include <cstddef>

namespace multislot {

// Mods\UI\LYT_MAINFRAME.SGO: the game's menu frame layout (Root.cpk UI/LYT_MAINFRAME.SGO) plus one text
// field, MSLabel, that shows the 8Player MOD label (built by tools/make_menu_label.py, embedded in the
// plugin). EDFModLoader loads it in place of the archived layout. The plugin keeps the file only while
// it is active: it writes it after patching, and removes it again (when it is one of ours) if it is
// disabled or the game build is not supported, so the game's own layout comes back.
const unsigned char* MenuLayout(std::size_t* size);
// True for a layout this or an earlier version of the plugin wrote.
bool OurMenuLayout(const unsigned char* data, std::size_t size);

// <...>\Mods\UI\LYT_MAINFRAME.SGO for a plugin at <...>\Mods\Plugins\<name>.dll; false elsewhere.
bool MenuLayoutPath(const wchar_t* pluginPath, wchar_t* out, std::size_t outChars);

enum class LayoutInstall { Written, Current, Updated, Foreign, Failed };
// Writes the layout unless the same file is there already; a file from another mod is left alone.
LayoutInstall InstallMenuLayout(const wchar_t* path);

enum class LayoutRemoval { Removed, Absent, Foreign, Failed };
// Deletes the file only when it is one of ours (and the UI folder, if that leaves it empty).
LayoutRemoval RemoveMenuLayout(const wchar_t* path);

}  // namespace multislot
