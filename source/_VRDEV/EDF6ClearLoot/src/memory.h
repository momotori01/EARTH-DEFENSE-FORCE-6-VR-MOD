#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstddef>
namespace clearloot {
bool Readable(const void*,std::size_t,bool writable=false) noexcept;
void* AllocateNearThunk(const void*,void*) noexcept;
bool RedirectCall(unsigned char*,void*,void*,bool&) noexcept;
}
