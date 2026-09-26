#include "native_world_composite.h"
#include <cstring>

namespace edf6vr {
namespace {
using Composite=void(__fastcall*)(void*);
using Setup=void(__fastcall*)(void*,void*,const void*,bool);
struct CompositeNativeCalls {
    Composite composite=nullptr;
    Setup setup=nullptr;
    void* renderState=nullptr;
    void* queueOwner=nullptr;
    void** sceneSlot=nullptr;
    void* applicationVtable=nullptr;
} compositeCalls;
struct ApplicationSnapshot {void* application=nullptr;DWORD thread=0;ULONGLONG time=0;};
SRWLOCK applicationLock=SRWLOCK_INIT;
ApplicationSnapshot applicationSnapshot;
ULONGLONG (WINAPI* compositeClock)()=GetTickCount64;
DWORD (WINAPI* compositeThread)()=GetCurrentThreadId;
thread_local bool enqueueingComposite=false;
unsigned char* compositeImage=nullptr;
bool compositeInstalled=false;
constexpr unsigned kCompositeSite=0x70573B,kCompositeEntry=0x705B10;
constexpr unsigned kCompositeSetup=0x112B4C0,kRenderState=0x2139A90,kQueueOwner=0x2136FF0,kSceneSlot=0x20B2958;
constexpr unsigned kApplicationVtable=0x17E91A8;
constexpr ULONGLONG kApplicationFreshMs=250;

bool ValidApplication(void* application) noexcept {
    __try {
        // Application+78 is the original compositor's 24-byte constant wrapper.
        // The native producer copies its references; no invented color values.
        return application && compositeCalls.applicationVtable && Readable(application,0x90)
            && *static_cast<void**>(application)==compositeCalls.applicationVtable;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool SnapshotApplication(ApplicationSnapshot& result) noexcept {
    if(!compositeInstalled || !compositeCalls.composite || !compositeCalls.setup)return false;
    AcquireSRWLockShared(&applicationLock);result=applicationSnapshot;ReleaseSRWLockShared(&applicationLock);
    const auto now=compositeClock();
    return result.thread==compositeThread() && now>=result.time && now-result.time<=kApplicationFreshMs
        && ValidApplication(result.application);
}
void __fastcall HookWorldComposite(void* application) {
    ApplicationSnapshot next{};
    if(ValidApplication(application))next={application,compositeThread(),compositeClock()};
    AcquireSRWLockExclusive(&applicationLock);applicationSnapshot=next;ReleaseSRWLockExclusive(&applicationLock);
    // The existing desktop path always runs exactly once, including VR OFF.
    // No application/state lock is held while the native producer executes.
    compositeCalls.composite(application);
}
bool CurrentScene(void* scene) noexcept {
    __try {
        return scene && compositeCalls.sceneSlot && Readable(compositeCalls.sceneSlot,sizeof(void*))
            && *compositeCalls.sceneSlot==scene;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
template<std::size_t N>bool CompositeBytes(const ImageProfile& image,unsigned rva,const unsigned char (&bytes)[N]) noexcept {
    return Readable(image.base+rva,N) && std::memcmp(image.base+rva,bytes,N)==0;
}
bool ValidateCompositeImage(const ImageProfile& image) noexcept {
    __try {
        if(!image.base || !Readable(image.base,4096))return false;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image.base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>2048)return false;
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
            || nt->FileHeader.TimeDateStamp!=kTimestamp || nt->OptionalHeader.SizeOfImage!=kImageSize)return false;
        const unsigned char call[]={0xE8,0xD0,0x03,0,0};
        const unsigned char entry[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x56};
        const unsigned char clearFlags[]={0xC6,0x45,0x87,0x03,0x45,0x33,0xC9,0x4C,0x8D,0x45,0x87,
            0x48,0x8D,0x15,0xC4,0x18,0xA3,0x01,0x48,0x8D,0x0D,0x5D,0x43,0xA3,0x01,
            0xE8,0x88,0x5D,0xA2,0,0x48,0x8B,0xCE};
        const unsigned char sceneRead[]={0x48,0x8B,0x0D,0x58,0xCD,0x9A,0x01,0xE8,0x1B,0x02,0xA9,0};
        const unsigned char appConstant[]={0x49,0x8D,0x46,0x78,0x48,0x89,0x44,0x24,0x20};
        const unsigned char nativeTarget[]={0x4C,0x8B,0x05,0x24,0x15,0xA3,0x01}; //2137090 native swapchain target wrapper
        return CompositeBytes(image,kCompositeSite,call) && CompositeBytes(image,kCompositeEntry,entry)
            && CompositeBytes(image,0x70571A,clearFlags) && CompositeBytes(image,0x705BF9,sceneRead)
            && CompositeBytes(image,0x705D87,appConstant) && CompositeBytes(image,0x705B65,nativeTarget)
            && Readable(image.base+kApplicationVtable,16)
            && *reinterpret_cast<void**>(image.base+kApplicationVtable+8)==image.base+0x705530;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}

bool InstallNativeWorldComposite(const ImageProfile& image,NativeWorldCompositeLog log) noexcept {
    if(compositeImage || !ValidateCompositeImage(image))return false;
    compositeCalls={reinterpret_cast<Composite>(image.base+kCompositeEntry),reinterpret_cast<Setup>(image.base+kCompositeSetup),
        image.base+kRenderState,image.base+kQueueOwner,reinterpret_cast<void**>(image.base+kSceneSlot),image.base+kApplicationVtable};
    bool changed=false;
    if(!RedirectCall(image.base+kCompositeSite,image.base+kCompositeEntry,reinterpret_cast<void*>(&HookWorldComposite),changed)) {
        if(!changed)compositeCalls={};
        // If protection restoration failed after patching, retain the live
        // thunk/calls. The caller disables paired output rather than freeing a
        // still-reachable function. RedirectCall reports this via changed.
        else compositeImage=image.base;
        return false;
    }
    compositeImage=image.base;compositeInstalled=true;
    if(log)log("NATIVEWORLD composite hook ready call=70573B routine=705B10 source=scene+5C0 native-HDR-to-screen queue=2136FF0 HUD=once appFresh=250ms");
    return true;
}
bool NativeWorldCompositeAvailable() noexcept {
    ApplicationSnapshot snapshot{};return !enqueueingComposite && SnapshotApplication(snapshot);
}
bool EnqueueNativeWorldComposite(void* scene) noexcept {
    ApplicationSnapshot snapshot{};
    if(enqueueingComposite || !CurrentScene(scene) || !SnapshotApplication(snapshot))return false;
    enqueueingComposite=true;
    // Match70571A..705733 before705B10. It only queues native context bits3
    // false; the next eye's normal112B4C0 sets them true again.
    const unsigned char flags=3;
    compositeCalls.setup(compositeCalls.renderState,compositeCalls.queueOwner,&flags,false);
    compositeCalls.composite(snapshot.application);
    enqueueingComposite=false;
    return true;
}
}
