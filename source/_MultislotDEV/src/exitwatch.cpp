#include "exitwatch.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define PSAPI_VERSION 2
#include <Psapi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <intrin.h>

#include "log.h"

namespace multislot {
namespace {

using TerminateFn = BOOL(WINAPI*)(HANDLE, UINT);
using ExitFn = void(WINAPI*)(UINT);

constexpr int kMaxModules = 128;
constexpr const char* kImportDlls[] = {"KERNEL32.dll", "api-ms-win-core-processthreads-l1-1-0.dll",
                                       "api-ms-win-core-processthreads-l1-1-1.dll",
                                       "api-ms-win-core-processthreads-l1-1-2.dll"};

// Append-only: an entry is complete before `count` moves past it, so the hooks read it without a lock (a lock
// could be held by a thread the ending process has already stopped).
struct Watched {
    std::uintptr_t begin = 0, end = 0;
    TerminateFn terminate = nullptr;  // what the module's slot held before; null when not wrapped
    ExitFn exit = nullptr;
};
Watched watched[kMaxModules];
std::atomic<int> watchedCount{0};
TerminateFn realTerminate = nullptr;
ExitFn realExit = nullptr;
HMODULE selfModule = nullptr;

const wchar_t* BaseName(const wchar_t* path) {
    const wchar_t* slash = wcsrchr(path, L'\\');
    return slash ? slash + 1 : path;
}

// The module an address belongs to, by name, and its base. No heap: this runs in a process that is ending.
void ModuleAt(const void* address, char* name, std::size_t size, std::uintptr_t& base) {
    name[0] = 0;
    base = 0;
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        !module || !GetModuleFileNameW(module, path, MAX_PATH)) {
        _snprintf_s(name, size, _TRUNCATE, "?");
        return;
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, BaseName(path), -1, name, static_cast<int>(size), nullptr, nullptr))
        _snprintf_s(name, size, _TRUNCATE, "?");
    name[size - 1] = 0;
    base = reinterpret_cast<std::uintptr_t>(module);
}

// The callers, nearest first: frame 0 is in this file, frame 1 the wrapper, so the list starts at the caller of
// TerminateProcess / ExitProcess.
__declspec(noinline) void Callers(char* out, std::size_t size) {
    void* frames[10]{};
    const USHORT n = RtlCaptureStackBackTrace(2, 8, frames, nullptr);
    std::size_t used = 0;
    out[0] = 0;
    for (USHORT i = 0; i < n && used + 1 < size; ++i) {
        char name[64];
        std::uintptr_t base = 0;
        ModuleAt(frames[i], name, sizeof(name), base);
        const int written = _snprintf_s(out + used, size - used, _TRUNCATE, "%s%s+%llX", i ? " < " : "", name,
                                        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[i]) - base));
        if (written <= 0) break;
        used += static_cast<std::size_t>(written);
    }
}

__declspec(noinline) void NoteExit(const char* function, UINT code) {
    char chain[420];
    Callers(chain, sizeof(chain));
    char why[560];
    DescribeExit(why, sizeof(why), function, code, chain);
    LogShutdown(why);
}

const Watched* Owner(const void* caller) {
    const auto at = reinterpret_cast<std::uintptr_t>(caller);
    const int n = watchedCount.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        if (at >= watched[i].begin && at < watched[i].end) return &watched[i];
    return nullptr;
}

BOOL WINAPI WatchedTerminate(HANDLE process, UINT code);
void WINAPI WatchedExit(UINT code);

BOOL WINAPI WatchedTerminate(HANDLE process, UINT code) {
    const void* caller = _ReturnAddress();
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId()) NoteExit("TerminateProcess", code);
    const Watched* owner = Owner(caller);
    TerminateFn next = owner && owner->terminate ? owner->terminate : realTerminate;
    if (!next || next == &WatchedTerminate) next = &::TerminateProcess;
    return next(process, code);
}

void WINAPI WatchedExit(UINT code) {
    const void* caller = _ReturnAddress();
    NoteExit("ExitProcess", code);
    const Watched* owner = Owner(caller);
    ExitFn next = owner && owner->exit ? owner->exit : realExit;
    if (!next || next == &WatchedExit) next = &::ExitProcess;
    next(code);
}

// One import of `module` pointed at `replacement`; the slot's previous value in `previous`.
bool Redirect(HMODULE module, const char* function, void* replacement, void** previous) {
    const auto base = reinterpret_cast<unsigned char*>(module);
    __try {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return false;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!directory.VirtualAddress) return false;
        for (auto entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); entry->Name;
             ++entry) {
            bool wanted = false;
            for (const char* dll : kImportDlls) wanted = wanted || _stricmp(reinterpret_cast<const char*>(base + entry->Name), dll) == 0;
            if (!wanted) continue;
            auto names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(
                base + (entry->OriginalFirstThunk ? entry->OriginalFirstThunk : entry->FirstThunk));
            auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + entry->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots) {
                if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
                const auto byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0) continue;
                if (reinterpret_cast<void*>(slots->u1.Function) == replacement) return false;
                DWORD protect = 0;
                if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &protect)) return false;
                *previous = reinterpret_cast<void*>(slots->u1.Function);
                slots->u1.Function = reinterpret_cast<ULONGLONG>(replacement);
                DWORD ignored = 0;
                VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), protect, &ignored);
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

void Scan() {
    HMODULE modules[512];
    DWORD bytes = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) return;
    wchar_t windows[MAX_PATH]{};
    if (!GetWindowsDirectoryW(windows, MAX_PATH)) return;
    const std::size_t listed = bytes / sizeof(HMODULE);
    const std::size_t total = listed < 512 ? listed : 512;
    for (std::size_t i = 0; i < total; ++i) {
        const HMODULE module = modules[i];
        if (!module || module == selfModule) continue;
        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const int n = watchedCount.load(std::memory_order_acquire);
        bool seen = false;
        for (int k = 0; k < n && !seen; ++k) seen = watched[k].begin == base;
        if (seen) continue;
        if (n >= kMaxModules) return;
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(module, path, MAX_PATH)) continue;
        MODULEINFO info{};
        if (!K32GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) continue;
        Watched& w = watched[n];
        w = Watched{};
        w.begin = base;
        w.end = base + info.SizeOfImage;
        bool any = false;
        if (ExitWatchCovers(path, windows)) {
            void* previous = nullptr;
            if (Redirect(module, "TerminateProcess", reinterpret_cast<void*>(&WatchedTerminate), &previous))
                w.terminate = reinterpret_cast<TerminateFn>(previous);
            previous = nullptr;
            if (Redirect(module, "ExitProcess", reinterpret_cast<void*>(&WatchedExit), &previous))
                w.exit = reinterpret_cast<ExitFn>(previous);
            any = w.terminate || w.exit;
        }
        watchedCount.store(n + 1, std::memory_order_release);  // every module once, watched or not
        if (any) {
            char name[96]{};
            WideCharToMultiByte(CP_UTF8, 0, BaseName(path), -1, name, sizeof(name) - 1, nullptr, nullptr);
            Log("EXIT WATCH %s: %s%s%s now writes SHUTDOWN with its caller", name, w.terminate ? "TerminateProcess" : "",
                w.terminate && w.exit ? " and " : "", w.exit ? "ExitProcess" : "");
        }
    }
}

DWORD WINAPI WatchLoop(void*) {
    for (;;) {
        Sleep(30000);
        Scan();
    }
}

}  // namespace

bool ExitWatchCovers(const wchar_t* path, const wchar_t* windowsDir) {
    if (!path || !path[0]) return false;
    if (windowsDir && windowsDir[0]) {
        const std::size_t length = wcslen(windowsDir);
        if (_wcsnicmp(path, windowsDir, length) == 0 && (path[length] == L'\\' || path[length] == 0)) return false;
    }
    const wchar_t* name = BaseName(path);
    for (const wchar_t* skip : {L"EDF.dll", L"EDF6MultiSlot.dll", L"EDF6VR.dll"})
        if (_wcsicmp(name, skip) == 0) return false;
    return true;
}

std::size_t DescribeExit(char* out, std::size_t size, const char* function, unsigned code, const char* chain) {
    if (!out || !size) return 0;
    const int written = _snprintf_s(out, size, _TRUNCATE, "the game was ended by %s(0x%X) from %s", function, code,
                                    chain && chain[0] ? chain : "?");
    return written < 0 ? std::strlen(out) : static_cast<std::size_t>(written);
}

void StartExitWatch() {
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&StartExitWatch), &selfModule);
    if (const HMODULE kernel = GetModuleHandleW(L"kernel32.dll")) {
        realTerminate = reinterpret_cast<TerminateFn>(reinterpret_cast<void*>(GetProcAddress(kernel, "TerminateProcess")));
        realExit = reinterpret_cast<ExitFn>(reinterpret_cast<void*>(GetProcAddress(kernel, "ExitProcess")));
    }
    Scan();
    if (const HANDLE thread = CreateThread(nullptr, 0, &WatchLoop, nullptr, 0, nullptr)) CloseHandle(thread);
}

}  // namespace multislot
