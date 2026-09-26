// Execute the actual on-disk export thunks, with benign test targets instead of
// Windows functions. Never run DllMain or load the game. A breakpoint at the
// common dispatch deterministically inserts another call after PA was written.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include "../src/loaderproxy.h"

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
}
using Fn = std::uintptr_t(*)();
using ArgsFn = std::uint64_t(*)(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                               std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
using FloatFn = double(*)(double, double, double, double, double, double);

std::uint64_t Args(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
                   std::uint64_t e, std::uint64_t f, std::uint64_t g, std::uint64_t h) {
    return a + 3*b + 5*c + 7*d + 11*e + 13*f + 17*g + 19*h;
}
double Floats(double a, double b, double c, double d, double e, double f) {
    return a + 3*b + 5*c + 7*d + 11*e + 13*f;
}

struct Image {
    std::uint8_t* base = nullptr;
    std::vector<DWORD> exports;
    std::vector<DWORD> slots;
    ~Image() { if (base) VirtualFree(base, 0, MEM_RELEASE); }
    bool Load(const wchar_t* path) {
        std::ifstream input(path, std::ios::binary);
        std::vector<char> file{std::istreambuf_iterator<char>(input), {}};
        if (file.size() < sizeof(IMAGE_DOS_HEADER)) return false;
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > file.size()) return false;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;
        base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage + 0x10000,
                                                      MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        if (!base) return false;
        std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
        auto section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (section->PointerToRawData + section->SizeOfRawData > file.size()) return false;
            std::memcpy(base + section->VirtualAddress, file.data() + section->PointerToRawData, section->SizeOfRawData);
        }
        const auto dir = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + nt->OptionalHeader.DataDirectory[0].VirtualAddress);
        if (dir->NumberOfFunctions != 180 || dir->NumberOfNames != 180) return false;
        const auto functions = reinterpret_cast<const DWORD*>(base + dir->AddressOfFunctions);
        for (DWORD i = 0; i < dir->NumberOfFunctions; ++i) {
            const DWORD rva = functions[i];
            if (std::memcmp(base + rva, "\x48\x8b\x05", 3)) return false;
            std::int32_t displacement = 0;
            std::memcpy(&displacement, base + rva + 3, 4);
            const auto slot = static_cast<DWORD>(static_cast<std::int64_t>(rva) + 7 + displacement);
            exports.push_back(rva); slots.push_back(slot);
            // Each target returns its own identity, never calls any system API.
            auto target = base + nt->OptionalHeader.SizeOfImage + i * 16;
            target[0] = 0xB8;
            const DWORD identity = 1000 + i;
            std::memcpy(target + 1, &identity, 4); target[5] = 0xC3;
            std::memcpy(base + slot, &target, 8);
        }
        FlushInstructionCache(GetCurrentProcess(), base, nt->OptionalHeader.SizeOfImage + 0x10000);
        return true;
    }
    Fn Function(std::size_t i) const { return reinterpret_cast<Fn>(base + exports[i]); }
};

Image* interrupted = nullptr;
bool inject = false;
int traps = 0;
LONG CALLBACK Interleave(EXCEPTION_POINTERS* info) {
    if (!interrupted || info->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT ||
        info->ExceptionRecord->ExceptionAddress != interrupted->base + 0xE7A0) return EXCEPTION_CONTINUE_SEARCH;
    ++traps;
    if (inject) {
        inject = false;
        // Model a second thread running between the PA store and dispatch.
        // The nested benign export overwrites PA exactly as that thread would.
        Check(interrupted->Function(1)() == 1001, "intervening export reaches its own target");
    }
    std::memcpy(&info->ContextRecord->Rip, interrupted->base + 0x1DF98, 8);
    return EXCEPTION_CONTINUE_EXECUTION;
}

void ForcedInterleave(Image& image, bool fixed) {
    interrupted = &image; inject = true; traps = 0;
    const auto saved = image.base[0xE7A0]; image.base[0xE7A0] = 0xCC;
    FlushInstructionCache(GetCurrentProcess(), image.base + 0xE7A0, 1);
    const auto result = image.Function(0)();
    Check(result == (fixed ? 1000u : 1001u), fixed ? "fixed call keeps its own target" : "original misdispatch is reproduced");
    Check(traps == (fixed ? 0 : 2), "only original exports use the shared gateway");
    image.base[0xE7A0] = saved;
    FlushInstructionCache(GetCurrentProcess(), image.base + 0xE7A0, 1);
    interrupted = nullptr;
    std::printf("%s deterministic interleave: export 0 returned %llu; gateway traps %d\n",
                fixed ? "fixed" : "original", static_cast<unsigned long long>(result), traps);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    Image original, fixed;
    if (!original.Load(argv[1]) || !fixed.Load(argv[2])) { std::puts("Unable to map loader fixtures"); return 2; }
    Check(original.exports == fixed.exports && original.slots == fixed.slots, "export addresses and target slots preserved");
    for (std::size_t i = 0; i < fixed.exports.size(); ++i) {
        Check(!std::strcmp(multislot::LoaderProxyStyle(original.base + original.exports[i]), "shared-dispatch (legacy)"),
              "startup diagnostic identifies legacy exports");
        Check(!std::strcmp(multislot::LoaderProxyStyle(fixed.base + fixed.exports[i]), "direct-register"),
              "startup diagnostic identifies fixed exports");
    }
    Check(!std::strcmp(multislot::LoaderProxyStyle(nullptr), "unavailable"), "missing loader handled");
    const auto handler = AddVectoredExceptionHandler(1, Interleave);
    if (!handler) return 2;
    ForcedInterleave(original, false); ForcedInterleave(fixed, true);
    RemoveVectoredExceptionHandler(handler);
    for (std::size_t i = 0; i < fixed.exports.size(); ++i)
        Check(fixed.Function(i)() == 1000 + i, "all 180 fixed exports reach the matching target");
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int thread = 0; thread < 8; ++thread) threads.emplace_back([&, thread] {
        for (std::size_t i = 0; i < 100000; ++i) {
            const auto index = (i + static_cast<std::size_t>(thread)) % fixed.exports.size();
            if (fixed.Function(index)() != 1000 + index) ++errors;
        }
    });
    for (auto& thread : threads) thread.join();
    Check(errors.load() == 0, "800000 concurrent calls across 8 threads keep their targets");
    for (std::size_t i = 0; i < fixed.exports.size(); ++i) {
        const auto args = &Args;
        std::memcpy(fixed.base + fixed.slots[i], &args, 8);
        Check(reinterpret_cast<ArgsFn>(fixed.Function(i))(0x1234567887654321,2,3,4,5,6,7,0xDEADBEEF00000008) ==
              Args(0x1234567887654321,2,3,4,5,6,7,0xDEADBEEF00000008), "register and stack integer args / 64-bit return preserved");
        const auto floats = &Floats;
        std::memcpy(fixed.base + fixed.slots[i], &floats, 8);
        Check(reinterpret_cast<FloatFn>(fixed.Function(i))(.5,1.5,2.5,3.5,4.5,5.5) == Floats(.5,1.5,2.5,3.5,4.5,5.5),
              "XMM and stack floating args / return preserved");
    }
    std::printf("180 exports, integer/floating ABI, 800000 concurrent calls: %d failures\n", failures);
    return failures ? 1 : 0;
}
