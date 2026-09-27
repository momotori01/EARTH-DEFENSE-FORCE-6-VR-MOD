#include "crashlog.h"

// For MINIDUMP_EXCEPTION_INFORMATION and the MINIDUMP_TYPE flags only; MiniDumpWriteDump itself is
// resolved with GetProcAddress, so no import library is needed. The structures here are declared under
// #pragma pack(4): a hand-written copy with natural alignment puts ExceptionPointers 4 bytes too far
// along and MiniDumpWriteDump fails with ERROR_NOACCESS.
#include <DbgHelp.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "log.h"

namespace multislot {
namespace {

constexpr DWORD kCppException = 0xE06D7363;
constexpr std::size_t kTextSize = 8192;

std::uintptr_t gameBase = 0;
std::uintptr_t gameEnd = 0;
// First-chance exceptions can be normal elsewhere, so each source gets its own small budget
// and a noisy module cannot use up the entries a real crash in EDF.dll needs.
std::atomic<int> gameBudget{16};
std::atomic<int> cppBudget{6};
std::atomic<int> otherBudget{4};

// Minidump of the first access violation inside EDF.dll. DbgHelp is resolved at install time, never
// from inside the handler: loading a DLL while unwinding a fault is its own way to crash.
using WriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                  const MINIDUMP_EXCEPTION_INFORMATION*, void*, void*);
// Thread stacks are in every dump, and the object behind the 2026-09-27 null is a stack local, so
// this stays small (a few MB) while still carrying the memory the stack and registers point at.
constexpr MINIDUMP_TYPE kDumpType = static_cast<MINIDUMP_TYPE>(
    MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules | MiniDumpWithThreadInfo);
WriteDumpFn writeDump = nullptr;
wchar_t dumpPathW[MAX_PATH]{};
std::atomic<int> dumpBudget{1};

struct Text {
    char data[kTextSize];
    std::size_t length = 0;
    void Add(const char* format, ...) {
        if (length >= kTextSize - 1) return;
        va_list args;
        va_start(args, format);
        const int n = _vsnprintf_s(data + length, kTextSize - length, _TRUNCATE, format, args);
        va_end(args);
        length = n < 0 ? kTextSize - 1 : length + static_cast<std::size_t>(n);
    }
};

bool CrashClass(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_IN_PAGE_ERROR:
    case kCppException:
        return true;
    default:
        return false;
    }
}

void Where(Text& text, std::uintptr_t address) {
    if (address >= gameBase && address < gameEnd) {
        text.Add("EDF+%llX", static_cast<unsigned long long>(address - gameBase));
        return;
    }
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) &&
        GetModuleFileNameW(module, path, MAX_PATH)) {
        const wchar_t* name = wcsrchr(path, L'\\');
        text.Add("%ls+%llX", name ? name + 1 : path,
                 static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
        return;
    }
    text.Add("%016llX", static_cast<unsigned long long>(address));
}

// MSVC C++ exception: ExceptionInformation = {magic, object, ThrowInfo, image base}.
void CppTypeName(Text& text, const EXCEPTION_RECORD* record) {
    __try {
        if (record->NumberParameters < 4) return;
        const auto base = static_cast<std::uintptr_t>(record->ExceptionInformation[3]);
        const auto throwInfo = static_cast<std::uintptr_t>(record->ExceptionInformation[2]);
        if (!base || !throwInfo) return;
        const auto types = base + static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t*>(throwInfo + 12));
        if (*reinterpret_cast<const std::int32_t*>(types) < 1) return;
        const auto first = base + static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t*>(types + 4));
        const auto descriptor = base + static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t*>(first + 4));
        text.Add(" type=%.120s", reinterpret_cast<const char*>(descriptor + 16));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        text.Add(" type=?");
    }
}

void Stack(Text& text, CONTEXT context) {
    __try {
        for (int frame = 0; frame < 24 && context.Rip; ++frame) {
            text.Add("  #%02d ", frame);
            Where(text, context.Rip);
            text.Add("\r\n");
            DWORD64 imageBase = 0;
            auto function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (function) {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData,
                                 &establisher, nullptr);
            } else {
                // Leaf code without unwind data: the return address is on top of the stack.
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        text.Add("  (stack unreadable beyond this point)\r\n");
    }
}

// Other modules throw and catch C++ exceptions as a matter of course (the OpenXR runtime does while it
// starts), so only those raised by the game, or by a runtime helper it called, are reported: EDF.dll has
// to be among the first few frames of the throw.
bool ThrownForGame(CONTEXT context) {
    __try {
        for (int frame = 0; frame < 6 && context.Rip; ++frame) {
            if (context.Rip >= gameBase && context.Rip < gameEnd) return true;
            DWORD64 imageBase = 0;
            const auto function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (!function) return false;
            PVOID handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher,
                             nullptr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

std::atomic<DWORD> writerThread{0};

// Called with the report lock held, from the handler, at most once per launch.
void WriteMiniDump(EXCEPTION_POINTERS* info) {
    HANDLE file = CreateFileW(dumpPathW, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Log("CRASH DUMP could not be created (error %lu)", GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION param{GetCurrentThreadId(), info, FALSE};
    SetLastError(0);
    BOOL written = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, kDumpType, &param, nullptr, nullptr);
    if (!written) {
        // A Windows build that will not take these flags still gives a usable dump without them.
        const DWORD rich = GetLastError();
        SetFilePointer(file, 0, nullptr, FILE_BEGIN);
        SetEndOfFile(file);
        SetLastError(0);
        written = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &param, nullptr, nullptr);
        if (written) Log("CRASH DUMP full type refused (error %lu); wrote a plain dump instead", rich);
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) size.QuadPart = 0;
    CloseHandle(file);
    if (written)
        Log("CRASH DUMP written: %ls (%lld MB). Send this with the log", dumpPathW,
            static_cast<long long>(size.QuadPart / (1024 * 1024)));
    else
        Log("CRASH DUMP failed (error %lu); the log above is all there is", GetLastError());
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const DWORD code = record->ExceptionCode;
    if (!CrashClass(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (code == kCppException && !ThrownForGame(*info->ContextRecord)) return EXCEPTION_CONTINUE_SEARCH;
    // A fault while this thread is already writing a report (caught by the __try blocks below)
    // re-enters here first; the SRW lock is not recursive, so leave it to those blocks.
    const DWORD thread = GetCurrentThreadId();
    if (writerThread.load() == thread) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t rip = info->ContextRecord->Rip;
    const bool inGame = rip >= gameBase && rip < gameEnd;
    auto& budget = code == kCppException ? cppBudget : (inGame ? gameBudget : otherBudget);
    if (budget.fetch_sub(1) <= 0) return EXCEPTION_CONTINUE_SEARCH;

    static Text text;  // exception paths may run deep in a thread's stack; keep 8 KB off it
    static SRWLOCK lock = SRWLOCK_INIT;
    AcquireSRWLockExclusive(&lock);
    writerThread.store(thread);
    text.length = 0;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    text.Add("[%04u-%02u-%02u %02u:%02u:%02u.%03u] EXCEPTION %08lX thread %lu at ", now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, code, thread);
    Where(text, rip);
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && record->NumberParameters >= 2) {
        const auto kind = record->ExceptionInformation[0];
        text.Add(" %s %016llX", kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
                 static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    if (code == kCppException) CppTypeName(text, record);
    const CONTEXT& c = *info->ContextRecord;
    text.Add("\r\n  rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX\r\n", c.Rax, c.Rbx, c.Rcx, c.Rdx);
    text.Add("  rsi=%016llX rdi=%016llX rbp=%016llX rsp=%016llX\r\n", c.Rsi, c.Rdi, c.Rbp, c.Rsp);
    text.Add("  r8 =%016llX r9 =%016llX r10=%016llX r11=%016llX\r\n", c.R8, c.R9, c.R10, c.R11);
    text.Add("  r12=%016llX r13=%016llX r14=%016llX r15=%016llX\r\n", c.R12, c.R13, c.R14, c.R15);
    Stack(text, c);
    LogWrite(text.data, text.length);
    // Only an access violation in the game's own code, and only once per launch: everything else the
    // handler sees is either another module's business or a fault the game goes on to handle.
    if (inGame && code == EXCEPTION_ACCESS_VIOLATION && writeDump && dumpPathW[0] &&
        dumpBudget.fetch_sub(1) > 0)
        WriteMiniDump(info);
    writerThread.store(0);
    ReleaseSRWLockExclusive(&lock);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashLog(HMODULE game, const wchar_t* dumpPath) {
    const auto base = reinterpret_cast<std::uintptr_t>(game);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    gameBase = base;
    gameEnd = base + nt->OptionalHeader.SizeOfImage;
    if (dumpPath) {
        const std::size_t length = wcslen(dumpPath);
        if (length < MAX_PATH) {
            if (const HMODULE dbghelp = LoadLibraryW(L"DbgHelp.dll")) {
                writeDump = reinterpret_cast<WriteDumpFn>(
                    reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
                if (writeDump) wmemcpy(dumpPathW, dumpPath, length + 1);
            }
        }
    }
    AddVectoredExceptionHandler(1, &OnException);
}

bool CrashDumpArmed() { return writeDump != nullptr && dumpPathW[0] != 0; }

}  // namespace multislot
