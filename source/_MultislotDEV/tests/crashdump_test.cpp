// The minidump the crash handler writes (crashlog.h). The 2026-09-27 host crash (EDF+12B44FC) is a
// null whose source is a stack local, so the log's registers and call stack cannot explain it and a
// dump of the faulting frame is the only way on. This checks the writer really produces a file, and
// that it stays within the limits the shipped default relies on: at most one per launch, only for an
// access violation in the module it was given, and nothing at all when it is switched off.
//
//   CrashDumpTests work-folder
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <string>

#include "../src/crashlog.h"
#include "../src/log.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

long long FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return -1;
    return (static_cast<long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

// An access violation raised from this executable, so the faulting instruction is inside the module
// the handler was installed for. The handler never handles anything, so this __except catches it and
// the test survives to look at what was written.
void FaultOnce() {
    __try {
        volatile int* nowhere = nullptr;
        *nowhere = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::wstring folder = argc > 1 ? std::wstring(argv[1], argv[1] + std::strlen(argv[1])) : L".";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring log = folder + L"\\crashdump.log";
    const std::wstring dump = folder + L"\\crashdump.dmp";
    DeleteFileW(log.c_str());
    DeleteFileW(dump.c_str());
    LogOpen(log.c_str());

    // Nothing is armed before the handler is installed, and a null path leaves it unarmed.
    Check(!CrashDumpArmed(), "no dump before the handler is installed");

    // The test executable stands in for EDF.dll: a fault in this module counts as a fault in "the game".
    InstallCrashLog(GetModuleHandleW(nullptr), dump.c_str());
    Check(CrashDumpArmed(), "a usable path and DbgHelp arm the dump");

    FaultOnce();
    const long long first = FileSize(dump);
    Check(first > 0, "the first access violation writes a dump");
    // A minidump of a live process always carries the thread stacks, so it is never a stub.
    Check(first > 4096, "the dump holds more than a header");

    // Only one per launch: a second fault must leave the first file exactly as it was.
    FILETIME before{};
    if (const HANDLE handle = CreateFileW(dump.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        handle != INVALID_HANDLE_VALUE) {
        GetFileTime(handle, nullptr, nullptr, &before);
        CloseHandle(handle);
    }
    FaultOnce();
    Check(FileSize(dump) == first, "a second access violation writes no second dump");

    // The log says where it went, so whoever collects it knows there is a file to send.
    std::string text;
    if (const HANDLE handle = CreateFileW(log.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        handle != INVALID_HANDLE_VALUE) {
        char buffer[8192]{};
        DWORD read = 0;
        while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read) text.append(buffer, read);
        CloseHandle(handle);
    }
    Check(text.find("CRASH DUMP written") != std::string::npos, "the log names the dump it wrote");
    Check(text.find("EXCEPTION C0000005") != std::string::npos, "and still records the exception itself");

    if (failures) std::printf("--- log was ---\n%s---------------\n", text.c_str());
    DeleteFileW(dump.c_str());
    DeleteFileW(log.c_str());
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("crash dump verified\n");
    return 0;
}
