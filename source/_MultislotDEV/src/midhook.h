#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "code.h"

namespace multislot {

// Registers as a mid-function thunk saved them, lowest address first. A handler may change any of
// them (including xmm0-xmm5); the thunk loads the changed values back before it runs the displaced
// instructions and returns to the game.
struct CpuContext {
    std::uint8_t xmm[6][16];  // xmm0..xmm5
    std::uint64_t rflags;
    std::uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    std::uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
};

using MidHandler = void (*)(CpuContext* context);

// The game's rsp at the hooked instruction (its locals are addressed from here or from rbp).
inline std::uint64_t SiteRsp(const CpuContext* context) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&context->rax)) + sizeof(context->rax);
}

// Thunk: save every general register, flags and xmm0-5, call `handler` on a 16-byte aligned stack,
// restore, run `displaced` (position-independent instructions copied from the site) and jump to
// `resume`.
std::vector<std::uint8_t> MidThunkCode(MidHandler handler, const std::uint8_t* displaced, std::size_t displacedSize,
                                       std::uint64_t resume);

// Emits the thunk into `page`; nullptr when the page is full.
unsigned char* EmitMidThunk(ThunkPage& page, MidHandler handler, const std::uint8_t* displaced,
                            std::size_t displacedSize, std::uint64_t resume);

}  // namespace multislot
