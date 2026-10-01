#include "image_profile.h"
#include <cstdio>
#include <cstring>

namespace edf6vr {
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
static bool InImage(const ImageProfile& image,const void* p,std::size_t size) noexcept {
    const auto a=reinterpret_cast<std::uintptr_t>(p),b=reinterpret_cast<std::uintptr_t>(image.base);
    return a>=b && a-b<=kImageSize && size<=kImageSize-(a-b);
}
static bool VtableType(const ImageProfile& image,const void* table,const char* name) noexcept {
    if(!InImage(image,table,8)) return false;
    auto vt=static_cast<const unsigned char*>(table);
    if(!InImage(image,vt-8,8)) return false;
    const auto col=*reinterpret_cast<const unsigned char* const*>(vt-8);
    if(!InImage(image,col,24)) return false;
    const auto fields=reinterpret_cast<const std::uint32_t*>(col);
    if(fields[0]!=1 || fields[3]>=kImageSize-16 || fields[5]>=kImageSize
       || image.base+fields[5]!=col) return false;
    const auto actual=image.base+fields[3]+16;
    const auto length=std::strlen(name)+1;
    return InImage(image,actual,length) && std::memcmp(actual,name,length)==0;
}
const char* TypeName(const ImageProfile& image,const void* object) noexcept {
    // The same walk as VtableType, reporting what is there instead of asking
    // whether it is one particular thing. Three guesses at telling a mission
    // from a menu have failed because both have a soldier, a first person
    // camera and a body being drawn; what the game calls those objects is
    // something it can be asked directly.
    __try {
        if(!Readable(object,8)) return nullptr;
        const auto table=*static_cast<const void* const*>(object);
        if(!InImage(image,table,8)) return nullptr;
        auto vt=static_cast<const unsigned char*>(table);
        if(!InImage(image,vt-8,8)) return nullptr;
        const auto col=*reinterpret_cast<const unsigned char* const*>(vt-8);
        if(!InImage(image,col,24)) return nullptr;
        const auto fields=reinterpret_cast<const std::uint32_t*>(col);
        if(fields[0]!=1 || fields[3]>=kImageSize-16 || fields[5]>=kImageSize
           || image.base+fields[5]!=col) return nullptr;
        const auto name=reinterpret_cast<const char*>(image.base+fields[3]+16);
        if(!InImage(image,name,2)) return nullptr;
        return name;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool HasType(const ImageProfile& image,const void* object,const char* name) noexcept {
    __try {
        return Readable(object,8) && VtableType(image,*static_cast<const void* const*>(object),name);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool CheckImageImpl(HMODULE module,ImageProfile& out,char* reason,std::size_t length) noexcept {
    auto fail=[&](const char* msg) { if(reason&&length) sprintf_s(reason,length,"%s",msg); return false; };
    if(!module) return fail("EDF.dll is not loaded");
    auto base=reinterpret_cast<unsigned char*>(module);
    if(!Readable(base,4096)) return fail("Unreadable PE headers");
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x800) return fail("Invalid DOS header");
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
       || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return fail("Not an x64 PE image");
    if(nt->FileHeader.TimeDateStamp!=kTimestamp || nt->OptionalHeader.SizeOfImage!=kImageSize)
        return fail("Unsupported EDF.dll build; refusing all writes");
    ImageProfile candidate{base,reinterpret_cast<void**>(base+kUpdateSlotRva),base+kUpdateRva};
    if(!Readable(candidate.updateSlot,8) || *candidate.updateSlot!=candidate.original) return fail("Update vtable slot differs or is already hooked");
    if(!VtableType(candidate,base+kVtableRva,".?AVCharacterGhostCamera@@")) return fail("CharacterGhostCamera RTTI mismatch");
    const unsigned char entry[]={0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x90,0,0,0};
    if(!Readable(base+kUpdateRva,sizeof(entry)) || std::memcmp(base+kUpdateRva,entry,sizeof(entry))) return fail("Camera update prologue mismatch");
    // FOV reference from EDFModLoader's EDF6 ChangeFOV patch. Only executable sections.
    const unsigned char aob[]={0xf3,0x0f,0x10,0x05,0,0,0,0,0xf3,0x0f,0x5e,0x87,0x10,0x04,0,0,0xf3,0x0f,0x11,0x47,0x24};
    const auto sections=IMAGE_FIRST_SECTION(nt);
    if(nt->FileHeader.NumberOfSections>32) return fail("Invalid section count");
    std::size_t count=0; const unsigned char* match=nullptr;
    for(unsigned int s=0;s<nt->FileHeader.NumberOfSections;++s) {
        const auto& sec=sections[s];
        if(!(sec.Characteristics&IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto size=sec.Misc.VirtualSize;
        if(sec.VirtualAddress>=kImageSize || size>kImageSize-sec.VirtualAddress || size<sizeof(aob)
           || !Readable(base+sec.VirtualAddress,size)) return fail("Invalid executable section");
        for(std::size_t i=0;i<=size-sizeof(aob);++i) {
            const auto p=base+sec.VirtualAddress+i;
            if(p[0]==aob[0] && !std::memcmp(p,aob,4) && !std::memcmp(p+8,aob+8,sizeof(aob)-8)) { ++count; match=p; }
        }
    }
    if(count!=1 || match!=base+0xF8906) return fail("FOV/camera AOB is absent, ambiguous or moved");
    out=candidate;
    if(reason&&length) sprintf_s(reason,length,"Supported EDF.dll; camera RTTI, slot, entry and unique AOB verified");
    return true;
}
bool CheckImage(HMODULE module,ImageProfile& out,char* reason,std::size_t length) noexcept {
    __try { return CheckImageImpl(module,out,reason,length); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        if(reason&&length) sprintf_s(reason,length,"Exception while validating EDF.dll; refusing writes");
        return false;
    }
}
bool CheckUiGate(const ImageProfile& image) noexcept {
    __try {
        if(!image.base) return false;
        auto slot=reinterpret_cast<void**>(image.base+kUiGateSlotRva);
        if(!Readable(slot,8)) return false;
        const auto expected=static_cast<void*>(image.base+kUiGateRva);
        if(*slot!=expected) return false;
        const unsigned char opening[]={0x80,0xB9,0xF0,0x03,0x00,0x00,0x00,0x75,0x3C};
        return Readable(image.base+kUiGateRva,sizeof(opening))
               && !std::memcmp(image.base+kUiGateRva,opening,sizeof(opening));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

namespace {
struct PatchRecord { const void* address; std::size_t size; char purpose[56]; };
PatchRecord g_patches[256]{};
unsigned g_patchCount=0;
SRWLOCK g_patchLock=SRWLOCK_INIT;
}
void RecordPatch(const void* address,std::size_t size,const char* purpose) noexcept {
    if(!address) return;
    AcquireSRWLockExclusive(&g_patchLock);
    unsigned i=0;
    for(;i<g_patchCount;++i) if(g_patches[i].address==address) break;
    if(i==g_patchCount && g_patchCount<256) ++g_patchCount;
    if(i<256) {
        g_patches[i].address=address; g_patches[i].size=size;
        std::snprintf(g_patches[i].purpose,sizeof(g_patches[i].purpose),"%s",purpose?purpose:"");
    }
    ReleaseSRWLockExclusive(&g_patchLock);
}
unsigned PatchCount() noexcept { AcquireSRWLockShared(&g_patchLock); const auto n=g_patchCount; ReleaseSRWLockShared(&g_patchLock); return n; }
bool WritePatchList(const wchar_t* path) noexcept {
    if(!path || !path[0]) return false;
    FILE* file=nullptr;
    if(_wfopen_s(&file,path,L"w") || !file) return false;
    std::fprintf(file,"# Bytes EDF6VR changes in memory: <module>+0x<rva> <bytes> <purpose>\n");
    std::fprintf(file,"# Pointer slots are 8 bytes (a vtable or import entry); everything else is code.\n");
    AcquireSRWLockShared(&g_patchLock);
    for(unsigned i=0;i<g_patchCount;++i) {
        const auto& p=g_patches[i];
        HMODULE module=nullptr; char name[MAX_PATH]="(heap)"; std::uintptr_t rva=reinterpret_cast<std::uintptr_t>(p.address);
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(p.address),&module) && module) {
            char full[MAX_PATH]{}; GetModuleFileNameA(module,full,MAX_PATH);
            const char* slash=std::strrchr(full,'\\'); std::snprintf(name,sizeof(name),"%s",slash?slash+1:full);
            rva-=reinterpret_cast<std::uintptr_t>(module);
        }
        std::fprintf(file,"%s+0x%llX %llu %s\n",name,static_cast<unsigned long long>(rva),static_cast<unsigned long long>(p.size),p.purpose);
    }
    ReleaseSRWLockShared(&g_patchLock);
    std::fclose(file);
    return true;
}

bool ReplacePointer(void** slot,void* expected,void* replacement,bool& changed) noexcept {
    changed=false;
    if(reinterpret_cast<std::uintptr_t>(slot)%alignof(void*) || !Readable(slot,8)) return false;
    DWORD oldProtect=0;
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&oldProtect)) return false;
    auto previous=InterlockedCompareExchangePointer(slot,replacement,expected);
    changed=(previous==expected);
    if(changed) RecordPatch(slot,8,"pointer slot");
    DWORD unused=0;
    const bool restored=VirtualProtect(slot,8,oldProtect,&unused)!=FALSE;
    // Caller must keep the DLL resident whenever changed is true, including protection failure.
    return changed&&restored;
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

namespace {
// A rel32 call (E8) or jump (E9) moved onto a near thunk to `replacement`.
bool RedirectBranch(unsigned char* callSite,unsigned char opcode,void* expectedTarget,void* replacement,
                    const char* purpose,bool& changed) noexcept {
    changed=false;
    __try {
        if(!callSite || !expectedTarget || !replacement) return false;
        if(!Readable(callSite,5) || callSite[0]!=opcode) return false;
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
        RecordPatch(callSite,5,purpose);
        DWORD restored=0;
        const bool ok=VirtualProtect(callSite,5,previous,&restored)!=0;
        FlushInstructionCache(GetCurrentProcess(),callSite,5);
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

bool RedirectJump(unsigned char* jumpSite,void* expectedTarget,void* replacement,bool& changed) noexcept {
    return RedirectBranch(jumpSite,0xE9,expectedTarget,replacement,"jump redirect",changed);
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
        RecordPatch(callSite,5,"call redirect");
        DWORD restored=0;
        const bool ok=VirtualProtect(callSite,5,previous,&restored)!=0;
        FlushInstructionCache(GetCurrentProcess(),callSite,5);
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

}
