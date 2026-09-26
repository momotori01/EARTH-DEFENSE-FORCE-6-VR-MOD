// Mods\UI\LYT_MAINFRAME.SGO handling in a scratch folder: where the file goes, writing, updating, leaving
// another mod's file alone, and removing only our own.
//   MenuLayoutTests work-folder
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../src/menulayout.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::vector<unsigned char> ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(bytes.data()),
                                                                  static_cast<std::streamsize>(bytes.size()));
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::vector<unsigned char> Embedded() {
    std::size_t size = 0;
    const unsigned char* data = MenuLayout(&size);
    return std::vector<unsigned char>(data, data + size);
}

bool IsEmbedded(const std::wstring& path) { return Exists(path) && ReadBytes(path) == Embedded(); }

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: MenuLayoutTests work-folder\n");
        return 2;
    }
    const std::wstring root = argv[1];
    const std::wstring mods = root + L"\\Mods";
    const std::wstring ui = mods + L"\\UI";
    const std::wstring layout = ui + L"\\LYT_MAINFRAME.SGO";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(mods.c_str(), nullptr);
    DeleteFileW(layout.c_str());
    DeleteFileW((layout + L".multislot-tmp").c_str());
    RemoveDirectoryW(ui.c_str());

    // The embedded layout: an SGO with the label field, recognised as ours.
    const auto embedded = Embedded();
    const wchar_t label[] = L"MSLabel";
    const auto* labelBytes = reinterpret_cast<const unsigned char*>(label);
    Check(embedded.size() > 16 && std::memcmp(embedded.data(), "SGO\0", 4) == 0, "embedded layout is an SGO file");
    Check(std::search(embedded.begin(), embedded.end(), labelBytes, labelBytes + 7 * sizeof(wchar_t)) != embedded.end(),
          "embedded layout names the MSLabel field");
    Check(OurMenuLayout(embedded.data(), embedded.size()), "the current layout is in the known list (add it when the asset changes)");
    auto changed = embedded;
    changed[changed.size() / 2] ^= 0x5A;
    Check(!OurMenuLayout(changed.data(), changed.size()), "a changed layout is not ours");

    // Path: only for a plugin in Mods\Plugins.
    wchar_t path[MAX_PATH]{};
    Check(MenuLayoutPath(L"C:\\Games\\EDF6\\Mods\\Plugins\\EDF6MultiSlot.dll", path, MAX_PATH) &&
              std::wcscmp(path, L"C:\\Games\\EDF6\\Mods\\UI\\LYT_MAINFRAME.SGO") == 0,
          "Mods\\Plugins\\x.dll -> Mods\\UI\\LYT_MAINFRAME.SGO");
    Check(MenuLayoutPath(L"C:\\EDF6\\mods\\plugins\\EDF6MultiSlot.dll", path, MAX_PATH), "folder names in any case");
    Check(!MenuLayoutPath(L"C:\\Games\\EDF6\\EDF6MultiSlot.dll", path, MAX_PATH), "a plugin outside Mods\\Plugins gets no layout");
    Check(!MenuLayoutPath(L"C:\\Games\\EDF6\\Other\\Plugins\\EDF6MultiSlot.dll", path, MAX_PATH), "Plugins must be inside Mods");
    Check(!MenuLayoutPath(L"EDF6MultiSlot.dll", path, MAX_PATH), "a bare file name gets no layout");
    wchar_t tiny[8]{};
    Check(!MenuLayoutPath(L"C:\\Games\\EDF6\\Mods\\Plugins\\EDF6MultiSlot.dll", tiny, 8), "a short buffer is refused, not overrun");
    std::wstring longPath = L"C:\\";
    while (longPath.size() < MAX_PATH + 20) longPath += L"folder\\";
    longPath += L"Mods\\Plugins\\EDF6MultiSlot.dll";
    Check(!MenuLayoutPath(longPath.c_str(), path, MAX_PATH), "an over-long plugin path is refused without ending the process");

    // Removing when nothing is there.
    Check(RemoveMenuLayout(layout.c_str()) == LayoutRemoval::Absent, "remove: nothing to remove");

    // Install: creates UI and writes the file; a second run finds it current.
    Check(InstallMenuLayout(layout.c_str()) == LayoutInstall::Written && IsEmbedded(layout), "install writes the layout");
    Check(!Exists(layout + L".multislot-tmp"), "no temporary file is left behind");
    Check(InstallMenuLayout(layout.c_str()) == LayoutInstall::Current && IsEmbedded(layout), "install again: already current");

    // Remove: deletes our file and the folder it created.
    Check(RemoveMenuLayout(layout.c_str()) == LayoutRemoval::Removed && !Exists(layout) && !Exists(ui), "remove deletes our layout and the empty UI folder");

    // Another mod's layout is never replaced or deleted.
    CreateDirectoryW(ui.c_str(), nullptr);
    WriteBytes(layout, changed);
    Check(InstallMenuLayout(layout.c_str()) == LayoutInstall::Foreign && ReadBytes(layout) == changed, "install leaves another mod's layout alone");
    Check(RemoveMenuLayout(layout.c_str()) == LayoutRemoval::Foreign && ReadBytes(layout) == changed, "remove leaves another mod's layout alone");
    DeleteFileW(layout.c_str());

    // A UI folder that holds other files stays.
    const std::wstring other = ui + L"\\OTHER_MOD.SGO";
    WriteBytes(other, {1, 2, 3});
    Check(InstallMenuLayout(layout.c_str()) == LayoutInstall::Written, "install next to other files");
    Check(RemoveMenuLayout(layout.c_str()) == LayoutRemoval::Removed && Exists(other) && Exists(ui), "remove keeps the folder and the other files");
    DeleteFileW(other.c_str());

    // A temporary file left by an interrupted write is cleaned up.
    WriteBytes(layout + L".multislot-tmp", {9});
    Check(InstallMenuLayout(layout.c_str()) == LayoutInstall::Written && IsEmbedded(layout), "install over a stale temporary file");
    WriteBytes(layout + L".multislot-tmp", {9});
    Check(RemoveMenuLayout(layout.c_str()) == LayoutRemoval::Removed && !Exists(layout + L".multislot-tmp") && !Exists(ui),
          "remove cleans a stale temporary file");

    // A folder where the file should be cannot be read as a layout: nothing is touched.
    CreateDirectoryW(ui.c_str(), nullptr);
    CreateDirectoryW(layout.c_str(), nullptr);
    Check(InstallMenuLayout(layout.c_str()) == LayoutInstall::Failed, "install refuses when the name is a folder");
    Check(RemoveMenuLayout(layout.c_str()) == LayoutRemoval::Failed && Exists(layout), "remove refuses when the name is a folder");
    RemoveDirectoryW(layout.c_str());
    RemoveDirectoryW(ui.c_str());

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("menu layout file handling verified\n");
    return 0;
}
