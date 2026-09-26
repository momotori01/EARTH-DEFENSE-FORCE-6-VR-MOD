#include "code.h"

#include <cstring>

namespace multislot {
namespace {
constexpr std::size_t kPage = 0x4000;
constexpr std::size_t kStub = 12;
// A rel32 reaches +/-2 GB; stay well inside that on both sides of the anchor.
constexpr std::uintptr_t kReach = 0x60000000;
}  // namespace

bool WriteCode(unsigned char* at, const unsigned char* bytes, std::size_t size) {
    DWORD previous = 0;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(at, bytes, size);
    DWORD ignored = 0;
    VirtualProtect(at, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    return true;
}

bool ThunkPage::Allocate(const unsigned char* anchor) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::uintptr_t granularity = info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(anchor) & ~(granularity - 1);
    for (std::uintptr_t step = granularity; step < kReach && !page_; step += granularity) {
        if (base > step)
            page_ = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(base - step), kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!page_)
            page_ = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(base + step), kPage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    }
    used_ = 0;
    return page_ != nullptr;
}

unsigned char* ThunkPage::Add(void* target) {
    unsigned char code[kStub] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    const auto address = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(code + 2, &address, sizeof(address));
    return Emit(code, kStub);
}

unsigned char* ThunkPage::Emit(const unsigned char* code, std::size_t size) {
    if (!page_ || size > kPage - used_) return nullptr;
    unsigned char* at = page_ + used_;
    std::memcpy(at, code, size);
    used_ += size;
    return at;
}

bool ThunkPage::Seal() {
    DWORD previous = 0;
    if (!page_ || !VirtualProtect(page_, kPage, PAGE_EXECUTE_READ, &previous)) return false;
    FlushInstructionCache(GetCurrentProcess(), page_, kPage);
    return true;
}

void ThunkPage::Release() {
    if (page_) VirtualFree(page_, 0, MEM_RELEASE);
    page_ = nullptr;
    used_ = 0;
}

bool ThunkPage::Contains(const unsigned char* address) const {
    return page_ && address >= page_ && address < page_ + kPage;
}

std::vector<std::uint8_t> JumpBytes(const unsigned char* site, const unsigned char* destination, std::size_t length) {
    const auto delta = static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(destination) - reinterpret_cast<std::intptr_t>(site + 5));
    if (length < 5 || delta < INT32_MIN || delta > INT32_MAX) return {};
    const auto relative = static_cast<std::int32_t>(delta);
    std::vector<std::uint8_t> bytes(length, 0x90);
    bytes[0] = 0xE9;
    std::memcpy(bytes.data() + 1, &relative, sizeof(relative));
    return bytes;
}

std::vector<std::uint8_t> CallBytes(const unsigned char* site, const unsigned char* destination) {
    const auto delta = static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(destination) - reinterpret_cast<std::intptr_t>(site + 5));
    if (delta < INT32_MIN || delta > INT32_MAX) return {};
    const auto relative = static_cast<std::int32_t>(delta);
    std::vector<std::uint8_t> bytes(5);
    bytes[0] = 0xE8;
    std::memcpy(bytes.data() + 1, &relative, sizeof(relative));
    return bytes;
}

}  // namespace multislot
