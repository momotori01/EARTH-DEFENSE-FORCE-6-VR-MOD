#include "image_profile.h"
#include "native_world_queue.h"
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <limits>

namespace edf6vr {
namespace {
using Setup=void(__fastcall*)(void*,void*,const void*,bool);
using Flush=void(__fastcall*)(void*);
using Reserve=void*(__fastcall*)(void*,int);
using Advance=void(__fastcall*)(void*,int);
struct NativeCalls { Setup setup=nullptr;Flush flush=nullptr;Reserve reserve=nullptr,peek=nullptr;Advance commit=nullptr,pop=nullptr; } calls;
NativeWorldQueueCallbacks callbacks{};
NativeWorldQueueLog logger=nullptr;
std::atomic<bool> enabled{false};
std::atomic<std::uint32_t> generation{1},nextSequence{0};
std::atomic<std::uint64_t> pairs{0},loops{0},begins{0},ends{0},invalidMarkers{0};
void* expectedSetupReturn=nullptr;
unsigned char* installedBase=nullptr;
unsigned char* codePage=nullptr;
RUNTIME_FUNCTION bridgeFunction{};
bool registeredFunction=false;
enum class Phase { Idle,Left,AwaitRight,Right };
struct ProducerState {
    Phase phase=Phase::Idle;
    std::uintptr_t stack=0;
    void* owner=nullptr;
    std::uint64_t frame=0;
};
thread_local ProducerState producer;
constexpr unsigned kSetupSite=0x1197A55,kEndSite=0x119889B,kRestart=0x1197A3A,kContinue=0x11988A0;
constexpr unsigned kProducerQueue=0x28A8;
constexpr std::uint32_t kMarkerMagic=0x3151574E; // NWQ1
struct Marker {
    std::uint64_t frame;
    std::uint32_t eye,kind,magic,reserved;
    std::uint64_t padding;
};
static_assert(sizeof(Marker)==32);

void __fastcall ConsumeMarker(void* renderContext,void* queue) {
    // The renderer loop has already consumed the one-unit handler record.
    // Consume precisely our two units before calling any external code.
    const auto* source=static_cast<const Marker*>(calls.peek(queue,2));
    Marker marker{};
    std::memcpy(&marker,source,sizeof(marker));
    calls.pop(queue,2);
    if(marker.magic!=kMarkerMagic || marker.eye>1 || marker.kind>1 || !marker.frame) {
        invalidMarkers.fetch_add(1,std::memory_order_relaxed);return;
    }
    if(marker.kind==0) {
        begins.fetch_add(1,std::memory_order_relaxed);
        if(callbacks.begin) callbacks.begin(marker.frame,marker.eye,renderContext);
    } else {
        ends.fetch_add(1,std::memory_order_relaxed);
        if(callbacks.end) callbacks.end(marker.frame,marker.eye,renderContext);
    }
}
void QueueMarker(void* owner,std::uint64_t frame,unsigned eye,unsigned kind) {
    auto* queue=static_cast<unsigned char*>(owner)+kProducerQueue;
    // Native reserve/commit may publish in batches and wrap after a record.
    // Follow the native handler+payload convention; do not reserve three units
    // as one record because the native consumer advances the handler separately.
    auto* handler=static_cast<std::uintptr_t*>(calls.reserve(queue,1));
    handler[0]=reinterpret_cast<std::uintptr_t>(&ConsumeMarker);handler[1]=0;
    calls.commit(queue,1);
    auto* payload=static_cast<Marker*>(calls.reserve(queue,2));
    const Marker marker{frame,eye,kind,kMarkerMagic,0,0};
    std::memcpy(payload,&marker,sizeof(marker));calls.commit(queue,2);
}
bool AllowPair() noexcept {
    return enabled.load(std::memory_order_acquire) && (!callbacks.allowPair || callbacks.allowPair());
}
void StartSetup(void* context,void* owner,const void* flags,bool set,std::uintptr_t stack,bool exactCaller) {
    if(exactCaller && producer.phase==Phase::AwaitRight && producer.stack==stack && producer.owner==owner) {
        producer.phase=Phase::Right;
        if(callbacks.producerBegin)callbacks.producerBegin(producer.frame,1);
        QueueMarker(owner,producer.frame,1,0);
    } else if(exactCaller && producer.phase==Phase::Idle && owner && AllowPair()) {
        auto sequence=nextSequence.fetch_add(1,std::memory_order_relaxed)+1;
        if(!sequence) sequence=nextSequence.fetch_add(1,std::memory_order_relaxed)+1;
        const auto epoch=generation.load(std::memory_order_acquire);
        producer={Phase::Left,stack,owner,(static_cast<std::uint64_t>(epoch)<<32)|sequence};
        pairs.fetch_add(1,std::memory_order_relaxed);
        if(callbacks.producerBegin)callbacks.producerBegin(producer.frame,0);
        QueueMarker(owner,producer.frame,0,0);
    }
    // Nested/unrecognized/disabled scene submissions forward once, without
    // consuming or replacing the active outer pair's state.
    calls.setup(context,owner,flags,set);
}
__declspec(noinline) void __fastcall HookSetup(void* context,void* owner,const void* flags,bool set) {
    const auto stack=reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress())+sizeof(void*);
    const bool exact=!expectedSetupReturn || _ReturnAddress()==expectedSetupReturn;
    StartSetup(context,owner,flags,set,stack,exact);
}
bool __fastcall FinishLoop(void* owner,std::uintptr_t stack,void* scene=nullptr) {
    bool repeat=false;
    if(producer.stack==stack && producer.owner==owner) {
        if(producer.phase==Phase::Left) {
            if(callbacks.producerEnd)callbacks.producerEnd(producer.frame,0,scene);
            QueueMarker(owner,producer.frame,0,1);
            loops.fetch_add(1,std::memory_order_relaxed);
            producer.phase=Phase::AwaitRight;repeat=true;
        } else if(producer.phase==Phase::Right) {
            if(callbacks.producerEnd)callbacks.producerEnd(producer.frame,1,scene);
            QueueMarker(owner,producer.frame,1,1);
            loops.fetch_add(1,std::memory_order_relaxed);
            producer={};
        }
    }
    // This publishes pending native commands; it is not a renderer wait.
    // Even cancellation must complete both producer loops once a pair starts.
    calls.flush(owner);
    return repeat;
}

struct Emitter {
    unsigned char* memory;std::size_t pos=0;
    void Byte(unsigned char value) {memory[pos++]=value;}
    void Bytes(const unsigned char* p,std::size_t n) {std::memcpy(memory+pos,p,n);pos+=n;}
    template<std::size_t N>void Bytes(const unsigned char (&p)[N]) {Bytes(p,N);}
    void U32(std::uint32_t value) {std::memcpy(memory+pos,&value,4);pos+=4;}
    void U64(std::uint64_t value) {std::memcpy(memory+pos,&value,8);pos+=8;}
    void Jump(void* target) {const unsigned char op[]={0xFF,0x25,0,0,0,0};Bytes(op);U64(reinterpret_cast<std::uint64_t>(target));}
};
struct BridgeLayout {std::size_t codeSize=0,unwindOffset=0;unsigned char prologueLength=0;};
BridgeLayout EmitBridge(unsigned char* memory,void* finish,void* restart,void* continuation,const RUNTIME_FUNCTION& parent) {
    Emitter e{memory};
    // The patched site is JMP, so RSP is the original 16-byte-aligned native
    // call-site stack. No synthetic return address is added or edited (CET).
    // The original CALL already permits volatile GPR/RFLAGS/XMM0..5 changes.
    // Use its existing32-byte shadow space, preserving all nonvolatiles through
    // the C++ ABI. Adding scratch below RSP breaks chained SAVE_NONVOL offsets.
    // RDX matches HookSetup's return-slot+8 token without frame-pointer guessing.
    const unsigned char stackArg[]={0x48,0x8B,0xD4};e.Bytes(stackArg);
    // Native RDI remains the scene pointer through all four viewport iterations.
    const unsigned char sceneArg[]={0x4C,0x8B,0xC7};e.Bytes(sceneArg);
    e.Byte(0x48);e.Byte(0xB8);e.U64(reinterpret_cast<std::uint64_t>(finish));e.Byte(0xFF);e.Byte(0xD0);
    e.Byte(0x84);e.Byte(0xC0); // test AL,AL
    e.Byte(0x74);const auto normalDisplacement=e.pos;e.Byte(0);
    e.Jump(restart);
    memory[normalDisplacement]=static_cast<unsigned char>(e.pos-normalDisplacement-1);
    e.Jump(continuation);
    const auto codeSize=e.pos;
    while(e.pos%4)e.Byte(0xCC);
    const auto unwindOffset=e.pos;
    // Secondary unwind record chains to the *original native scene function*.
    // A JMP entry has no new caller return address. Treating this as a regular
    // independent function would unwind to garbage at the native local stack.
    e.Byte(0x21);e.Byte(0);e.Byte(0);e.Byte(0); // v1,CHAININFO,no new stack frame
    e.U32(parent.BeginAddress);e.U32(parent.EndAddress);e.U32(parent.UnwindData);
    return {codeSize,unwindOffset,0};
}
bool BytesAt(const ImageProfile& image,unsigned rva,const unsigned char* bytes,std::size_t count) noexcept {
    return Readable(image.base+rva,count) && !std::memcmp(image.base+rva,bytes,count);
}
template<std::size_t N>bool BytesAt(const ImageProfile& image,unsigned rva,const unsigned char (&bytes)[N]) noexcept {return BytesAt(image,rva,bytes,N);}
bool ValidateImage(const ImageProfile& image,RUNTIME_FUNCTION& parent) noexcept {
    __try {
        if(!image.base || !Readable(image.base,4096))return false;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>2048)return false;
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=kTimestamp || nt->OptionalHeader.SizeOfImage!=kImageSize)return false;
        const unsigned char setup[]={0xE8,0x66,0x3A,0xF9,0xFF};
        const unsigned char sceneRegister[]={0x48,0x8B,0xF9}; //1197911: RDI=scene RCX
        const unsigned char end[]={0xE8,0x70,0x2B,0xF8,0xFF};
        const unsigned char restart[]={0xC6,0x44,0x24,0x30,0x03,0x41,0xB1,0x01,0x4C,0x8D,0x44,0x24,0x30};
        const unsigned char induction[]={0x45,0x33,0xED,0x45,0x8B,0xE5,0x44,0x89,0x6C,0x24,0x50,0x41,0x8B,0xDD,0x48,0x89,0x5C,0x24,0x70};
        const unsigned char loopEnd[]={0x41,0xFF,0xC4,0x44,0x89,0x64,0x24,0x50,0x48,0x8B,0x5C,0x24,0x70,0x48,0xFF,0xC3,0x48,0x89,0x5C,0x24,0x70,0x41,0x83,0xFC,0x04,0x0F,0x85,0xDC,0xF1,0xFF,0xFF};
        const unsigned char flush[]={0x48,0x81,0xC1,0xA8,0x28,0,0,0xE9,0x94,0x69,0xF3,0xFE};
        const unsigned char continuation[]={0x48,0x8B,0x8D,0x70,0x03,0,0,0x48,0x33,0xCC};
        const unsigned char reserve[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x41,0x48};
        const unsigned char commit[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x01,0x51,0x50};
        const unsigned char peek[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x41,0x08};
        const unsigned char pop[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x01,0x51,0x10};
        if(!BytesAt(image,kSetupSite,setup)||!BytesAt(image,kEndSite,end)||!BytesAt(image,kRestart,restart)||!BytesAt(image,0x1197911,sceneRegister)
           ||!BytesAt(image,0x1197A5A,induction)||!BytesAt(image,0x1198875,loopEnd)||!BytesAt(image,0x111B410,flush)
           ||!BytesAt(image,kContinue,continuation)||!BytesAt(image,0x51D00,reserve)||!BytesAt(image,0x51D60,commit)
           ||!BytesAt(image,0x51E90,peek)||!BytesAt(image,0x51F00,pop))return false;
        DWORD64 base=0;const auto* entry=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(image.base+kEndSite),&base,nullptr);
        if(!entry || base!=reinterpret_cast<DWORD64>(image.base) || entry->BeginAddress!=0x11978D0 || entry->EndAddress!=0x11988D0)return false;
        parent=*entry;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
unsigned char* AllocateBridge(const ImageProfile& image) noexcept {
    SYSTEM_INFO system{};GetSystemInfo(&system);
    const auto granularity=static_cast<std::uintptr_t>(system.dwAllocationGranularity);
    const auto base=reinterpret_cast<std::uintptr_t>(image.base);
    const auto start=(base+kImageSize+granularity-1)&~(granularity-1);
    // Positive image-relative offsets let CHAININFO use the native image base,
    // including its C++ EH records. Also safely inside CALL/JMP rel32 reach.
    for(auto address=start;address<base+0x60000000;address+=granularity) {
        auto* page=static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(address),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        if(page)return page;
    }
    return nullptr;
}
void FreeBridge() noexcept {
    if(registeredFunction){RtlDeleteFunctionTable(&bridgeFunction);registeredFunction=false;}
    if(codePage){VirtualFree(codePage,0,MEM_RELEASE);codePage=nullptr;}
}
}

bool InstallNativeWorldQueue(const ImageProfile& image,const NativeWorldQueueCallbacks& handlers,NativeWorldQueueLog log) noexcept {
    if(installedBase)return false;
    RUNTIME_FUNCTION parent{};
    if(!handlers.begin || !handlers.end || !ValidateImage(image,parent))return false;
    codePage=AllocateBridge(image);if(!codePage)return false;
    const auto layout=EmitBridge(codePage,reinterpret_cast<void*>(&FinishLoop),image.base+kRestart,image.base+kContinue,parent);
    constexpr unsigned setupThunkOffset=512;
    Emitter setup{codePage+setupThunkOffset};setup.Byte(0x48);setup.Byte(0xB8);setup.U64(reinterpret_cast<std::uint64_t>(&HookSetup));setup.Byte(0xFF);setup.Byte(0xE0);
    const auto imageBase=reinterpret_cast<std::uintptr_t>(image.base);
    const auto codeRva=reinterpret_cast<std::uintptr_t>(codePage)-imageBase;
    bridgeFunction.BeginAddress=static_cast<DWORD>(codeRva);
    bridgeFunction.EndAddress=static_cast<DWORD>(codeRva+layout.codeSize);
    bridgeFunction.UnwindData=static_cast<DWORD>(codeRva+layout.unwindOffset);
    DWORD old=0;
    if(!VirtualProtect(codePage,4096,PAGE_EXECUTE_READ,&old)){FreeBridge();return false;}
    FlushInstructionCache(GetCurrentProcess(),codePage,4096);
    if(!RtlAddFunctionTable(&bridgeFunction,1,imageBase)){FreeBridge();return false;}
    registeredFunction=true;
    unsigned char patches[2][5]{};
    const unsigned sites[]={kSetupSite,kEndSite};
    const unsigned destinations[]={setupThunkOffset,0};
    for(unsigned i=0;i<2;++i) {
        const auto difference=reinterpret_cast<std::intptr_t>(codePage+destinations[i])-reinterpret_cast<std::intptr_t>(image.base+sites[i]+5);
        if(difference<std::numeric_limits<std::int32_t>::min() || difference>std::numeric_limits<std::int32_t>::max()){FreeBridge();return false;}
        const auto relative=static_cast<std::int32_t>(difference);patches[i][0]=i?0xE9:0xE8;std::memcpy(patches[i]+1,&relative,4);
    }
    // Install at plugin load only. No running-scene patch toggling. Preflight
    // all sites again, keep originals, and roll back our two writes on failure.
    unsigned char originals[2][5];for(unsigned i=0;i<2;++i)std::memcpy(originals[i],image.base+sites[i],5);
    auto* range=image.base+kSetupSite;const std::size_t length=kEndSite-kSetupSite+5;
    if(!VirtualProtect(range,length,PAGE_EXECUTE_READWRITE,&old)){FreeBridge();return false;}
    RUNTIME_FUNCTION verified{};
    if(!ValidateImage(image,verified)) {DWORD ignored=0;VirtualProtect(range,length,old,&ignored);FreeBridge();return false;}
    calls={reinterpret_cast<Setup>(image.base+0x112B4C0),reinterpret_cast<Flush>(image.base+0x111B410),
           reinterpret_cast<Reserve>(image.base+0x51D00),reinterpret_cast<Reserve>(image.base+0x51E90),
           reinterpret_cast<Advance>(image.base+0x51D60),reinterpret_cast<Advance>(image.base+0x51F00)};
    callbacks=handlers;logger=log;expectedSetupReturn=image.base+kSetupSite+5;
    for(unsigned i=0;i<2;++i){std::memcpy(image.base+sites[i],patches[i],5);RecordPatch(image.base+sites[i],5,"native world submit");}
    FlushInstructionCache(GetCurrentProcess(),range,length);
    DWORD ignored=0;
    if(!VirtualProtect(range,length,old,&ignored)) {
        for(unsigned i=0;i<2;++i)std::memcpy(image.base+sites[i],originals[i],5);
        FlushInstructionCache(GetCurrentProcess(),range,length);VirtualProtect(range,length,old,&ignored);
        callbacks={};calls={};expectedSetupReturn=nullptr;FreeBridge();return false;
    }
    installedBase=image.base;
    if(logger)logger("NATIVEWORLD queue hooks ready begin=1197A55 end=119889B restart=1197A3A prep=once loop=two publication=not-wait unwind=chained");
    return true;
}
void SetNativeWorldQueueEnabled(bool value) noexcept {
    const auto previous=enabled.exchange(value,std::memory_order_acq_rel);
    if(previous && !value)generation.fetch_add(1,std::memory_order_acq_rel);
}
int NativeWorldProducerEye() noexcept {
    return producer.phase==Phase::Left?0:producer.phase==Phase::Right?1:-1;
}
std::uint64_t NativeWorldProducerFrame() noexcept {return producer.frame;}
bool NativeWorldFrameValid(std::uint64_t frame) noexcept {
    return frame && enabled.load(std::memory_order_acquire) && static_cast<std::uint32_t>(frame>>32)==generation.load(std::memory_order_acquire);
}
NativeWorldQueueStats NativeWorldQueueStatistics() noexcept {
    return {pairs.load(std::memory_order_relaxed),loops.load(std::memory_order_relaxed),begins.load(std::memory_order_relaxed),ends.load(std::memory_order_relaxed),invalidMarkers.load(std::memory_order_relaxed)};
}
}
