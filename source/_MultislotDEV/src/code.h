#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace multislot {

bool WriteCode(unsigned char* at, const unsigned char* bytes, std::size_t size);

// Stubs and mid-function thunks in one block within rel32 reach of `anchor`.
class ThunkPage {
public:
    bool Allocate(const unsigned char* anchor);
    // `mov rax, target; jmp rax`. Returns the stub address, or nullptr when full or not allocated.
    unsigned char* Add(void* target);
    // Copies arbitrary code; returns its address, or nullptr when full or not allocated.
    unsigned char* Emit(const unsigned char* code, std::size_t size);
    bool Seal();  // makes the page executable and read-only
    void Release();
    bool Contains(const unsigned char* address) const;
    const unsigned char* Base() const { return page_; }
    std::size_t Used() const { return used_; }

private:
    unsigned char* page_ = nullptr;
    std::size_t used_ = 0;
};

// `call rel32` bytes at `site` aimed at `destination`; empty when out of reach.
std::vector<std::uint8_t> CallBytes(const unsigned char* site, const unsigned char* destination);
// `lea rax, [rip+disp32]` bytes at `site` loading `destination`; empty when out of reach.
// `jmp rel32` at `site` aimed at `destination`, NOP-padded to `length` (>= 5); empty when out of reach.
std::vector<std::uint8_t> JumpBytes(const unsigned char* site, const unsigned char* destination, std::size_t length);

}  // namespace multislot
