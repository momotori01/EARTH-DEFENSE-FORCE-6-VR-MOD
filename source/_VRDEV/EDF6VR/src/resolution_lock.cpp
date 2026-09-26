#include "resolution_lock.h"
#include <cstring>
#include <cstdio>
namespace edf6vr {
bool InstallResolutionLock(const ImageProfile& image,unsigned width,unsigned height,
                           char* reason,std::size_t capacity) noexcept {
    auto report=[&](const char* message) { if(reason && capacity) std::snprintf(reason,capacity,"%s",message); };
    if(!width && !height) { report("disabled"); return true; }
    if(width<640 || width>7680 || height<480 || height>4320) { report("invalid dimensions"); return false; }
    // Buffer size alone is insufficient: the game has separate scene/letterbox
    // getters. 0.72.0 allocated a 4K buffer around a 1080p scene/depth texture.
    // Override the complete render rectangle before allocation. Native window
    // sizing remains independent; no ResizeBuffers call is injected.
    struct Patch { unsigned rva, size; bool dimensions; unsigned char expected[43]; };
    constexpr Patch patches[]={
        {0x1183250,16,false,{0x33,0xC0,0x38,0x05,0xB7,0xBB,0xE6,0x00,0x0F,0x45,0x05,0x71,0x2E,0xFB,0x00,0xC3}},
        {0x1183260,43,true,{0x80,0x3D,0xA8,0xBB,0xE6,0x00,0x00,0x74,0x11,0x8B,0x05,0x65,0x2E,0xFB,0x00,0x89,0x01,
            0x8B,0x05,0x61,0x2E,0xFB,0x00,0x89,0x02,0xC3,0x8B,0x05,0x44,0x2E,0xFB,0x00,0x89,0x01,
            0x8B,0x05,0x40,0x2E,0xFB,0x00,0x89,0x02,0xC3}},
        {0x1183290,16,false,{0x33,0xC0,0x38,0x05,0x77,0xBB,0xE6,0x00,0x0F,0x45,0x05,0x2D,0x2E,0xFB,0x00,0xC3}},
        {0x11832A0,17,true,{0x8B,0x05,0x2E,0x2E,0xFB,0x00,0x89,0x01,0x8B,0x05,0x2A,0x2E,0xFB,0x00,0x89,0x02,0xC3}},
        {kResolutionGetterRva,17,true,{0x8B,0x05,0xAE,0x2D,0xFB,0x00,0x89,0x01,0x8B,0x05,0xAA,0x2D,0xFB,0x00,0x89,0x02,0xC3}}
    };
    unsigned char replacements[5][43]{};
    const unsigned char dimensions[]={0xC7,0x01,0,0,0,0,0xB8,0,0,0,0,0x89,0x02,0xC3};
    const unsigned char zero[]={0xB8,0,0,0,0,0xC3};
    if(!image.base) { report("no image"); return false; }
    // Validate every site before modifying any. All sites share one code page.
    for(unsigned i=0;i<5;++i) {
        const auto& p=patches[i]; auto* bytes=replacements[i];
        std::memset(bytes,0x90,p.size);
        if(p.dimensions) {
            std::memcpy(bytes,dimensions,sizeof(dimensions));
            std::memcpy(bytes+2,&width,4); std::memcpy(bytes+7,&height,4);
        } else std::memcpy(bytes,zero,sizeof(zero));
        const auto* site=image.base+p.rva;
        if(!Readable(site,p.size)) { report("render getter unreadable"); return false; }
        if(std::memcmp(site,bytes,p.size) && std::memcmp(site,p.expected,p.size)) {
            report("render getter signature differs; refusing all patches"); return false;
        }
    }
    auto* start=image.base+patches[0].rva;
    const auto length=kResolutionGetterRva+17-patches[0].rva;
    DWORD old=0;
    if(!VirtualProtect(start,length,PAGE_EXECUTE_READWRITE,&old)) { report("protect failed"); return false; }
    for(unsigned i=0;i<5;++i) { std::memcpy(image.base+patches[i].rva,replacements[i],patches[i].size); RecordPatch(image.base+patches[i].rva,patches[i].size,"render size getter"); }
    FlushInstructionCache(GetCurrentProcess(),start,length);
    DWORD ignored=0;
    const bool restored=VirtualProtect(start,length,old,&ignored)!=0;
    report(restored?"buffer/scene/viewport locked; render letterbox disabled":"installed; protection restore failed");
    return restored;
}

bool InstallChatTextScaleFix(const ImageProfile& image,char* reason,std::size_t capacity) noexcept {
    auto report=[&](const char* message) { if(reason && capacity) std::snprintf(reason,capacity,"%s",message); };
    // xmm1 = float(h); xmm0 = float(w) * 9/16; xmm1 /= xmm0; xmm1 *= [rsp+0x54]
    // becomes xmm1 = [rsp+0x54]: the text's y as the bubble already has it.
    constexpr unsigned char expected[42]={
        0x8B,0x44,0x24,0x64, 0x0F,0x57,0xC9, 0xF3,0x48,0x0F,0x2A,0xC8,
        0x8B,0x44,0x24,0x68, 0x0F,0x57,0xC0, 0xF3,0x48,0x0F,0x2A,0xC0,
        0xF3,0x0F,0x59,0x05,0x22,0x4B,0xFF,0x00, 0xF3,0x0F,0x5E,0xC8, 0xF3,0x0F,0x59,0x4C,0x24,0x54};
    unsigned char replacement[42]; std::memset(replacement,0x90,sizeof(replacement));
    const unsigned char load[]={0xF3,0x0F,0x10,0x4C,0x24,0x54};   // movss xmm1, dword ptr [rsp+0x54]
    std::memcpy(replacement,load,sizeof(load));
    if(!image.base) { report("no image"); return false; }
    auto* site=image.base+kChatTextScaleRva;
    if(!Readable(site,sizeof(expected))) { report("chat text site unreadable"); return false; }
    if(!std::memcmp(site,replacement,sizeof(replacement))) { report("chat text scaling already neutral"); return true; }
    if(std::memcmp(site,expected,sizeof(expected))) { report("chat text signature differs; left alone"); return false; }
    DWORD old=0;
    if(!VirtualProtect(site,sizeof(replacement),PAGE_EXECUTE_READWRITE,&old)) { report("protect failed"); return false; }
    std::memcpy(site,replacement,sizeof(replacement));
    RecordPatch(site,sizeof(replacement),"chat text y scale");
    FlushInstructionCache(GetCurrentProcess(),site,sizeof(replacement));
    DWORD ignored=0; VirtualProtect(site,sizeof(replacement),old,&ignored);
    report("chat text y no longer scaled by height/(width*9/16)");
    return true;
}
}
