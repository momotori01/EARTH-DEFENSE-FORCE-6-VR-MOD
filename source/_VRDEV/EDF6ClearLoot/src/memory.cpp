#include "memory.h"
#include <cstring>
namespace clearloot {
bool Readable(const void* ptr,std::size_t size,bool writable) noexcept {
    auto at=reinterpret_cast<std::uintptr_t>(ptr);
    if (!at || size>UINTPTR_MAX-at) return false;
    const auto end=at+size;
    while(at<end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(at),&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT
            || (mbi.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        const DWORD p=mbi.Protect&0xff;
        if (writable ? !(p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY)
                     : !(p==PAGE_READONLY||p==PAGE_READWRITE||p==PAGE_WRITECOPY||p==PAGE_EXECUTE_READ||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY)) return false;
        const auto next=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        if(next<=at) return false;
        at=next;
    }
    return true;
}
void* AllocateNearThunk(const void* anchor,void* target) noexcept {
    if(!anchor || !target) return nullptr;
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto granularity=info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
    const auto base=reinterpret_cast<std::uintptr_t>(anchor) & ~static_cast<std::uintptr_t>(granularity-1);
    // A rel32 reaches +/-2GB; stay well inside that on both sides of the anchor.
    constexpr std::uintptr_t kReach=0x60000000;
    unsigned char* page=nullptr;
    for(std::uintptr_t step=granularity; step<kReach && !page; step+=granularity) {
        if(base>step)
            page=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(base-step),0x1000,
                MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(!page)
            page=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(base+step),0x1000,
                MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    }
    if(!page) return nullptr;
    const unsigned char thunk[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    std::memcpy(page,thunk,sizeof(thunk));
    const auto address=reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(page+2,&address,sizeof(address));
    DWORD previous=0;
    if(!VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&previous)) {
        VirtualFree(page,0,MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(),page,sizeof(thunk));
    return page;
}

bool RedirectCall(unsigned char* callSite,void* expectedTarget,void* replacement,bool& changed) noexcept {
    changed=false;
    __try {
        if(!callSite || !expectedTarget || !replacement) return false;
        if(!Readable(callSite,5) || callSite[0]!=0xE8) return false;
        std::int32_t relative=0;
        std::memcpy(&relative,callSite+1,sizeof(relative));
        if(callSite+5+relative!=static_cast<unsigned char*>(expectedTarget)) return false;
        auto thunk=AllocateNearThunk(callSite,replacement);
        if(!thunk) return false;
        const auto delta=static_cast<unsigned char*>(thunk)-(callSite+5);
        if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0) { VirtualFree(thunk,0,MEM_RELEASE); return false; }
        const auto next=static_cast<std::int32_t>(delta);
        DWORD previous=0;
        if(!VirtualProtect(callSite,5,PAGE_EXECUTE_READWRITE,&previous)) { VirtualFree(thunk,0,MEM_RELEASE); return false; }
        std::memcpy(callSite+1,&next,sizeof(next));
        changed=true;
        DWORD restored=0;
        const bool ok=VirtualProtect(callSite,5,previous,&restored)!=0;
        FlushInstructionCache(GetCurrentProcess(),callSite,5);
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}
