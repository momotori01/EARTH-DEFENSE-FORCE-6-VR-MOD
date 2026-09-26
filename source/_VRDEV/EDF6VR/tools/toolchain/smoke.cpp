#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>

extern "C" int edf6vr_asm_smoke();

int main() {
    static_assert(sizeof(void*) == 8, "EDF6VR needs an x64 toolchain");
    const int result = edf6vr_asm_smoke();
    std::printf("MSVC=%d; pointer=%zu; MASM=%d; process=%lu\n",
                _MSC_VER, sizeof(void*), result, GetCurrentProcessId());
    return result == 42 ? 0 : 1;
}
