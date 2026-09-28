#include "smoothing.h"

#include <cmath>
#include <cstring>

namespace multislot {
namespace {

// mulps xmm6, xmmword ptr [rip + 0x120F6E3]  -> EDF+17A5A70, the shared 0.05.
constexpr std::uint8_t kOriginal[] = {0x0F, 0x59, 0x35, 0xE3, 0xF6, 0x20, 0x01};
constexpr std::size_t kDispOffset = 3;  // where the rip-relative displacement starts
constexpr std::size_t kVectorBytes = 16;

// The packet Controller sends on this interval (EDF+12CB5F0 writes 90.0 to its +0x58), so a
// correction is stepped once per 90 ms.
constexpr int kSendIntervalMs = 90;

}  // namespace

int SmoothingSettleMs(float factor) {
    if (!(factor > 0.0f) || factor > 1.0f) return 0;
    if (factor >= 1.0f) return kSendIntervalMs;
    // steps until the remaining error is a tenth: (1 - factor)^n = 0.1
    const double steps = std::log(0.1) / std::log(1.0 - static_cast<double>(factor));
    return static_cast<int>(steps * kSendIntervalMs + 0.5);
}

bool SmoothingPatch(unsigned char* base, ThunkPage& thunks, float factor, Patch& out) {
    if (!base || !(factor > 0.0f) || factor > 1.0f) return false;
    if (factor == kVanillaSmoothing) return false;  // nothing to do

    unsigned char* const site = base + kSmoothingSite;
    if (std::memcmp(site, kOriginal, sizeof(kOriginal)) != 0) return false;

    // mulps needs its memory operand 16-byte aligned. The page is allocated on the 64 KB granularity,
    // so aligning the offset inside it is enough.
    const unsigned char* page = thunks.Base();
    if (!page) return false;
    const std::size_t used = thunks.Used();
    const auto address = reinterpret_cast<std::uintptr_t>(page) + used;
    const std::size_t padding = static_cast<std::size_t>((16 - (address % 16)) % 16);
    if (padding) {
        const unsigned char zeros[16]{};
        if (!thunks.Emit(zeros, padding)) return false;
    }
    float splat[4] = {factor, factor, factor, factor};
    unsigned char bytes[kVectorBytes];
    std::memcpy(bytes, splat, sizeof(bytes));
    const unsigned char* constant = thunks.Emit(bytes, sizeof(bytes));
    if (!constant || reinterpret_cast<std::uintptr_t>(constant) % 16 != 0) return false;

    // rip is the address of the next instruction.
    const auto next = reinterpret_cast<std::intptr_t>(site) + static_cast<std::intptr_t>(sizeof(kOriginal));
    const std::intptr_t delta = reinterpret_cast<std::intptr_t>(constant) - next;
    if (delta < INT32_MIN || delta > INT32_MAX) return false;
    const auto disp = static_cast<std::int32_t>(delta);

    std::vector<std::uint8_t> original(kOriginal, kOriginal + sizeof(kOriginal));
    std::vector<std::uint8_t> replacement = original;
    std::memcpy(replacement.data() + kDispOffset, &disp, sizeof(disp));
    out = Patch{"remote player position correction factor", kSmoothingSite, std::move(original),
                std::move(replacement)};
    return true;
}

}  // namespace multislot
