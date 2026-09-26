#include "image_profile.h"
#include "weapon_layer.h"
#include <cstring>
#include <limits>
namespace edf6vr {
bool InstallWeaponLightingCaptureHooks(const ImageProfile& image) noexcept {
    // Load-time only, after CheckImage; no runtime D3D vtable mutation.
    // Native shader selection: renderer+AE0 (shadow) / +AC0 (no shadow).
    constexpr unsigned sites[]={0x1131198,0x1131233};
    constexpr unsigned char expected[]={0x41,0xFF,0x92,0x48,0x01,0x00,0x00};
    constexpr unsigned char before[]={0x41,0xC1,0xE8,0x04,0xC1,0xEA,0x04};
    if(!image.base) return false;
    for(auto rva:sites) {
        if(!Readable(image.base+rva-7,14) || std::memcmp(image.base+rva,expected,7)
           || std::memcmp(image.base+rva-7,before,7)) return false;
    }
    auto* thunk=AllocateNearThunk(image.base+sites[0],reinterpret_cast<void*>(&DispatchWeaponLighting));
    if(!thunk) return false;
    unsigned char patch[2][7]{};
    for(unsigned i=0;i<2;++i) {
        const auto distance=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(image.base+sites[i]+5);
        if(distance<std::numeric_limits<std::int32_t>::min() || distance>std::numeric_limits<std::int32_t>::max()) {
            VirtualFree(thunk,0,MEM_RELEASE); return false;
        }
        const auto rel=static_cast<std::int32_t>(distance);
        patch[i][0]=0xE8; std::memcpy(patch[i]+1,&rel,4); patch[i][5]=patch[i][6]=0x90;
    }
    auto* first=image.base+sites[0]; const auto length=sites[1]-sites[0]+7;
    DWORD old=0;
    if(!VirtualProtect(first,length,PAGE_EXECUTE_READWRITE,&old)) { VirtualFree(thunk,0,MEM_RELEASE); return false; }
    for(unsigned i=0;i<2;++i) { std::memcpy(image.base+sites[i],patch[i],7); RecordPatch(image.base+sites[i],7,"lighting dispatch"); }
    FlushInstructionCache(GetCurrentProcess(),first,length);
    DWORD ignored=0; VirtualProtect(first,length,old,&ignored);
    // Executable thunk stays resident for the patched sites' process lifetime.
    return true;
}
}
