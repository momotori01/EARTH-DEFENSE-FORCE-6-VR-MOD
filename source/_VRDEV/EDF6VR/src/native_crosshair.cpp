#include "native_crosshair.h"
#include <atomic>
#include <cstring>

namespace edf6vr {
namespace {
using CrosshairState=void*(*)(void* weapon,void* out);
CrosshairState original=nullptr;
std::atomic<bool> ready{false};
std::atomic<unsigned long long> hideUntil{0};
std::atomic<std::uint64_t> calls{0},hidden{0};

void* CrosshairHook(void* weapon,void* out) {
    const auto result=original(weapon,out);
    calls.fetch_add(1,std::memory_order_relaxed);
    if(out && NativeCrosshairHidden(GetTickCount64())) {
        static_cast<unsigned char*>(out)[kCrosshairVisibleOffset]=0;
        hidden.fetch_add(1,std::memory_order_relaxed);
    }
    return result;
}
}

bool InstallNativeCrosshair(const ImageProfile& image,bool& changed) noexcept {
    changed=false;
    if(!image.base) return false;
    const auto target=image.base+kCrosshairStateRva;
    // The function opens the way the shipped build does, and every slot and call
    // still reaches it. All of it, or nothing is touched.
    constexpr unsigned char prologue[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
        0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x8B,0xA9,0xE8,0x0B};
    if(!Readable(target,sizeof(prologue)) || std::memcmp(target,prologue,sizeof(prologue))) return false;
    for(const auto rva:kCrosshairSlotRvas) {
        const auto slot=reinterpret_cast<void**>(image.base+rva);
        if(!Readable(slot,sizeof(void*)) || *slot!=target) return false;
    }
    for(const auto rva:kCrosshairCallRvas) {
        const auto site=image.base+rva;
        if(!Readable(site,5) || site[0]!=0xE8) return false;
        std::int32_t relative=0; std::memcpy(&relative,site+1,sizeof(relative));
        if(site+5+relative!=target) return false;
    }
    original=reinterpret_cast<CrosshairState>(target);
    const auto hook=reinterpret_cast<void*>(&CrosshairHook);
    bool all=true;
    for(const auto rva:kCrosshairSlotRvas) {
        bool one=false;
        all=ReplacePointer(reinterpret_cast<void**>(image.base+rva),target,hook,one) && all;
        changed=changed||one;
    }
    for(const auto rva:kCrosshairCallRvas) {
        bool one=false;
        all=RedirectCall(image.base+rva,target,hook,one) && all;
        changed=changed||one;
    }
    // A partial install still only ever hides the crosshair while asked, so it
    // is harmless; but the panel keeps its cut unless every path is covered.
    ready.store(all);
    return all;
}
bool NativeCrosshairReady() noexcept { return ready.load(); }
void HideNativeCrosshairFor(unsigned long long now) noexcept { hideUntil.store(now+1000,std::memory_order_relaxed); }
bool NativeCrosshairHidden(unsigned long long now) noexcept { return now<hideUntil.load(std::memory_order_relaxed); }
NativeCrosshairStats ReadNativeCrosshairStats() noexcept {
    return {calls.load(std::memory_order_relaxed),hidden.load(std::memory_order_relaxed)};
}
}
