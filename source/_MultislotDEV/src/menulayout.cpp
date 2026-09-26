#include "menulayout.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <vector>

#include "menu_layout.h"

namespace multislot {
namespace {

constexpr std::size_t kMaxLayoutBytes = 1 << 20;
constexpr const wchar_t kTempSuffix[] = L".multislot-tmp";

struct KnownLayout {
    std::size_t size;
    std::uint64_t fnv1a;
};
// Every layout a release has written, so a later version can update it and a disabled plugin can remove
// it. When assets/LYT_MAINFRAME.SGO changes, add its entry here and keep the old ones (a test checks).
constexpr KnownLayout kKnownLayouts[] = {
    {7098, 0xDA755A27A8E88468ull},  // 0.6.0, 1.0.0, 1.1.0, 1.1.1, 1.2.0-1.5.3
};

std::uint64_t Fnv1a(const unsigned char* data, std::size_t size) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

// No *_s string functions here: on overflow they end the process through the invalid parameter handler.
bool CopyText(wchar_t* out, std::size_t chars, const wchar_t* text) {
    const std::size_t length = wcslen(text);
    if (length >= chars) return false;
    wmemcpy(out, text, length + 1);
    return true;
}

bool AppendText(wchar_t* out, std::size_t chars, const wchar_t* text) {
    const std::size_t used = wcsnlen(out, chars);
    return used < chars && CopyText(out + used, chars - used, text);
}

enum class Existing { Absent, Current, Ours, Foreign, Unreadable };

Existing Inspect(const wchar_t* path) {
    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Existing::Absent : Existing::Unreadable;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) != 0;
    const bool small = ok && size.QuadPart >= 0 && static_cast<unsigned long long>(size.QuadPart) <= kMaxLayoutBytes;
    std::vector<unsigned char> data;
    if (small && size.QuadPart > 0) {
        data.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size();
    }
    CloseHandle(file);
    if (!ok) return Existing::Unreadable;
    if (!small) return Existing::Foreign;
    if (data.size() == sizeof(kMenuLayoutBytes) && std::memcmp(data.data(), kMenuLayoutBytes, data.size()) == 0)
        return Existing::Current;
    return OurMenuLayout(data.data(), data.size()) ? Existing::Ours : Existing::Foreign;
}

bool TempPath(wchar_t* out, const wchar_t* path) { return CopyText(out, MAX_PATH, path) && AppendText(out, MAX_PATH, kTempSuffix); }

bool Folder(wchar_t* out, const wchar_t* path) {
    if (!CopyText(out, MAX_PATH, path)) return false;
    wchar_t* slash = wcsrchr(out, L'\\');
    if (!slash) return false;
    *slash = 0;
    return true;
}

// Written next to the target and moved over it, so the game never reads half a layout.
bool WriteLayout(const wchar_t* path) {
    wchar_t temp[MAX_PATH]{};
    if (!TempPath(temp, path)) return false;
    const HANDLE file = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, kMenuLayoutBytes, static_cast<DWORD>(sizeof(kMenuLayoutBytes)), &written, nullptr) &&
                    written == sizeof(kMenuLayoutBytes) && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok && MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    const DWORD error = GetLastError();
    DeleteFileW(temp);
    SetLastError(error);
    return false;
}

}  // namespace

const unsigned char* MenuLayout(std::size_t* size) {
    if (size) *size = sizeof(kMenuLayoutBytes);
    return kMenuLayoutBytes;
}

bool OurMenuLayout(const unsigned char* data, std::size_t size) {
    if (!data) return false;
    const std::uint64_t hash = Fnv1a(data, size);
    for (const auto& known : kKnownLayouts)
        if (known.size == size && known.fnv1a == hash) return true;
    return false;
}

bool MenuLayoutPath(const wchar_t* pluginPath, wchar_t* out, std::size_t outChars) {
    wchar_t buffer[MAX_PATH]{};
    if (!pluginPath || !out || !CopyText(buffer, MAX_PATH, pluginPath)) return false;
    wchar_t* file = wcsrchr(buffer, L'\\');
    if (!file) return false;
    *file = 0;
    wchar_t* plugins = wcsrchr(buffer, L'\\');
    if (!plugins || _wcsicmp(plugins + 1, L"Plugins") != 0) return false;
    *plugins = 0;
    const wchar_t* mods = wcsrchr(buffer, L'\\');
    if (!mods || _wcsicmp(mods + 1, L"Mods") != 0) return false;
    return AppendText(buffer, MAX_PATH, L"\\UI\\LYT_MAINFRAME.SGO") && CopyText(out, outChars, buffer);
}

LayoutInstall InstallMenuLayout(const wchar_t* path) {
    const Existing existing = Inspect(path);
    if (existing == Existing::Current) return LayoutInstall::Current;
    if (existing == Existing::Foreign) return LayoutInstall::Foreign;
    if (existing == Existing::Unreadable) return LayoutInstall::Failed;
    wchar_t folder[MAX_PATH]{};
    if (!Folder(folder, path)) return LayoutInstall::Failed;
    if (!CreateDirectoryW(folder, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return LayoutInstall::Failed;
    if (!WriteLayout(path)) return LayoutInstall::Failed;
    return existing == Existing::Ours ? LayoutInstall::Updated : LayoutInstall::Written;
}

LayoutRemoval RemoveMenuLayout(const wchar_t* path) {
    wchar_t temp[MAX_PATH]{};
    if (TempPath(temp, path)) DeleteFileW(temp);  // left behind only if a write was interrupted
    const Existing existing = Inspect(path);
    if (existing == Existing::Absent) return LayoutRemoval::Absent;
    if (existing == Existing::Foreign) return LayoutRemoval::Foreign;
    if (existing == Existing::Unreadable || !DeleteFileW(path)) return LayoutRemoval::Failed;
    // The UI folder goes too when the layout was all it held (RemoveDirectory refuses a folder with files).
    wchar_t folder[MAX_PATH]{};
    if (Folder(folder, path)) {
        const DWORD attributes = GetFileAttributesW(folder);
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) RemoveDirectoryW(folder);
    }
    return LayoutRemoval::Removed;
}

}  // namespace multislot
