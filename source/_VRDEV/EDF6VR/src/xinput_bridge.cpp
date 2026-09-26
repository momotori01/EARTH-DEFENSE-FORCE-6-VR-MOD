#include "image_profile.h"
#include "xinput_bridge.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <tlhelp32.h>
#include <intrin.h>

namespace edf6vr {
namespace {
// XInput's own headers are not needed for three functions and two structs, and
// not needing them keeps the plugin free of the SDK.
struct XinputGamepad {
    WORD buttons;
    BYTE leftTrigger, rightTrigger;
    SHORT thumbLX, thumbLY, thumbRX, thumbRY;
};
struct XinputState {
    DWORD packet;
    XinputGamepad gamepad;
};
struct XinputVibration { WORD left, right; };
// Games ask this before they will listen to a slot at all: it is the question
// "is there a controller here", and until now nothing answered it for the
// headset. XInputGetState alone is not enough, because a game that believes the
// slot is empty need never read it.
struct XinputCapabilities {
    BYTE type, subType;
    WORD flags;
    XinputGamepad gamepad;
    XinputVibration vibration;
};

using GetStateFn=DWORD(WINAPI*)(DWORD,XinputState*);
using SetStateFn=DWORD(WINAPI*)(DWORD,XinputVibration*);
using GetCapsFn=DWORD(WINAPI*)(DWORD,DWORD,XinputCapabilities*);
using GetProcFn=FARPROC(WINAPI*)(HMODULE,LPCSTR);

GetProcFn g_realGetProc=nullptr;
GetStateFn g_realGetState=nullptr;
SetStateFn g_realSetState=nullptr;
GetCapsFn g_realGetCaps=nullptr;
void** g_slot=nullptr;
bool g_installed=false;

SRWLOCK g_lock=SRWLOCK_INIT;
PadState g_pad{};
float g_rumbleLeft=0, g_rumbleRight=0;
bool g_rumbleWaiting=false;

unsigned long long g_getStateExCalls=0;
int g_sweptTotal=0;
unsigned long long g_getStateCalls=0;
unsigned long long g_setStateCalls=0;
unsigned long long g_capsCalls=0;
unsigned long long g_capsAnswered=0;
bool g_capsAllowed=true;
bool g_privateSpare=false;
bool g_realPadWanted=false;
bool g_padOffered=false;
bool g_askedForGetState=false;
char g_status[160]="not installed";
CaptureLogger g_log=nullptr;

PadProvider g_provider=nullptr;

// Who is actually reading the pad. Everything so far has assumed it is the
// game, and the assumption has never been checked: a Steam overlay polls XInput
// too, and buttons handed to an overlay reach nothing. Each distinct caller is
// recorded once, by the module it returns into, and reported in the log.
struct Caller { void* address; unsigned long long calls; };
Caller g_callers[8]{};
int g_callerCount=0;
bool g_callersChanged=false;

// The libraries a game calls XInput through, by name, and nothing else.
//
// Matching on "xinput" anywhere in the name was too generous, and it is what
// stopped the game starting three times over. XInput's client libraries forward
// to a shared implementation -- xinputuap.dll -- which also has an XInputSetState
// and an XInputGetCapabilities of its own, inside itself, so even refusing
// forwarders does not save us. Patching that redirects every caller in the
// process, not only the game, and the process does not survive it. It only
// appeared once no spare library was loaded, because before that the game's
// XInput never reached for it.
//
// A game asks for one of these names. Nothing else is ours to touch.
bool AGameFacingXInput(const wchar_t* lowered) noexcept {
    static const wchar_t* known[]={
        L"xinput1_4.dll",L"xinput1_3.dll",L"xinput1_2.dll",
        L"xinput1_1.dll",L"xinput9_1_0.dll",L"xinput.dll"};
    for(const wchar_t* name:known) if(!wcscmp(lowered,name)) return true;
    return false;
}

void NoteCaller(void* address) noexcept {
    for(int i=0;i<g_callerCount;++i) {
        if(g_callers[i].address==address) { ++g_callers[i].calls; return; }
    }
    if(g_callerCount>=8) return;
    g_callers[g_callerCount].address=address;
    g_callers[g_callerCount].calls=1;
    ++g_callerCount;
    g_callersChanged=true;
}

unsigned long long g_injected=0;   // merges that actually carried a button

DWORD MergePad(DWORD user,XinputState* state,DWORD result) noexcept {
    // Only the first pad carries the headset. A second real controller stays
    // exactly as it was.
    if(user!=0 || !state) return result;
    PadState mine{};
    AcquireSRWLockShared(&g_lock);
    mine=g_pad;
    ReleaseSRWLockShared(&g_lock);
    if(!mine.valid) return result;

    if(result!=ERROR_SUCCESS) {
        // No real pad on this slot: present ours as the whole device, and keep
        // the packet moving so the game does not treat it as stale.
        std::memset(state,0,sizeof(*state));
        static DWORD packet=0;
        state->packet=++packet;
        result=ERROR_SUCCESS;
    }
    auto& pad=state->gamepad;
    // Merged rather than replaced, so a real pad still works beside the headset.
    pad.buttons|=mine.buttons;
    if(mine.leftTrigger>pad.leftTrigger) pad.leftTrigger=mine.leftTrigger;
    if(mine.rightTrigger>pad.rightTrigger) pad.rightTrigger=mine.rightTrigger;
    // Whichever is being pushed harder wins, so neither source has to be centred
    // for the other to work.
    auto pick=[](SHORT real,SHORT ours) -> SHORT {
        const int a=real<0?-real:real, b=ours<0?-ours:ours;
        return b>a?ours:real;
    };
    pad.thumbLX=pick(pad.thumbLX,mine.leftX);
    pad.thumbLY=pick(pad.thumbLY,mine.leftY);
    pad.thumbRX=pick(pad.thumbRX,mine.rightX);
    pad.thumbRY=pick(pad.thumbRY,mine.rightY);
    if(mine.buttons||mine.leftTrigger>30||mine.rightTrigger>30) ++g_injected;
    return result;
}

DWORD WINAPI HookGetState(DWORD user,XinputState* state) noexcept {
    ++g_getStateCalls;
    NoteCaller(_ReturnAddress());
    // Built here, when the game asks, rather than on one of the game's own hooks.
    // Those stop running when the game is paused, and a pad built on one of them
    // freezes with them: the menu was reading whatever the last frame of play had
    // left behind. This runs whenever the game looks at the pad, which it does in
    // menus too.
    if(user==0 && g_provider) g_provider();
    DWORD result=ERROR_DEVICE_NOT_CONNECTED;
    if(g_realGetState) result=g_realGetState(user,state);
    return MergePad(user,state,result);
}

// Ordinal 100 of xinput1_3 and xinput1_4 is XInputGetStateEx, undocumented and
// identical in shape to XInputGetState except that it also reports the guide
// button. Games reach for it precisely because of that button, and they resolve
// it by ordinal, which has no name to match on: the GetProcAddress hook skipped
// it as an ordinal and the sweep never knew to look for its address. That is why
// the writes arrived and the reads never did.
GetStateFn g_realGetStateEx=nullptr;

DWORD WINAPI HookGetStateEx(DWORD user,XinputState* state) noexcept {
    ++g_getStateExCalls;
    NoteCaller(_ReturnAddress());
    if(user==0 && g_provider) g_provider();
    DWORD result=ERROR_DEVICE_NOT_CONNECTED;
    if(g_realGetStateEx) result=g_realGetStateEx(user,state);
    return MergePad(user,state,result);
}

// The first few answers, written out in full.
//
// The game asks whether a slot has a controller and then, on the strength of
// that answer, decides whether to read it. It has stopped reading. Nothing in
// this file changed, so either the answer is not what it looks like from here or
// the question is different from the one being answered -- and the only way to
// tell those apart is to write down both.
char g_capsTrace[6][128];
int g_capsTraced=0;
bool g_capsTraceChanged=false;

DWORD WINAPI HookGetCapabilities(DWORD user,DWORD flags,XinputCapabilities* caps) noexcept {
    ++g_capsCalls;
    NoteCaller(_ReturnAddress());
    // The provider is deliberately not run here. Answering "is there a
    // controller" does not need the controllers read, and this is called twice
    // a frame on the game's own thread -- which, now that a display session
    // exists from the title screen onward, means reaching into OpenXR twice a
    // frame from a thread that has no business being there.
    DWORD result=ERROR_DEVICE_NOT_CONNECTED;
    if(g_realGetCaps) result=g_realGetCaps(user,flags,caps);
    if(user!=0 || !caps || result==ERROR_SUCCESS) return result;
    // Answered from the offer, not from the pad state.
    //
    // The pad state is only refreshed when the game reads the pad, and the game
    // only reads a slot it believes holds a controller. Answering "yes" out of
    // g_pad.valid therefore closed a loop with no way in: no read, so no
    // refresh, so not valid, so the answer was no, so no read. With a real
    // controller plugged in the real capabilities answered instead and the loop
    // never showed; with only the headset, nothing worked at all.
    if(!g_padOffered || !g_capsAllowed) return result;
    // An ordinary wired gamepad, with everything present. The masks say which
    // controls exist rather than what they are doing, which is why they are all
    // set.
    std::memset(caps,0,sizeof(*caps));
    caps->type=1;                     // XINPUT_DEVTYPE_GAMEPAD
    caps->subType=1;                  // XINPUT_DEVSUBTYPE_GAMEPAD
    caps->gamepad.buttons=0xF3FF;
    caps->gamepad.leftTrigger=0xFF;
    caps->gamepad.rightTrigger=0xFF;
    caps->gamepad.thumbLX=caps->gamepad.thumbLY=static_cast<SHORT>(0xFFC0);
    caps->gamepad.thumbRX=caps->gamepad.thumbRY=static_cast<SHORT>(0xFFC0);
    caps->vibration.left=caps->vibration.right=0xFFFF;
    ++g_capsAnswered;
    if(g_capsTraced<6) {
        std::snprintf(g_capsTrace[g_capsTraced],sizeof(g_capsTrace[0]),
                      "XINPUT caps answered: user=%lu flags=%lu real=%lu -> SUCCESS "
                      "type=%u sub=%u size=%zu",
                      user,flags,result,caps->type,caps->subType,sizeof(*caps));
        ++g_capsTraced;
        g_capsTraceChanged=true;
    }
    return ERROR_SUCCESS;
}

DWORD WINAPI HookSetState(DWORD user,XinputVibration* vibration) noexcept {
    ++g_setStateCalls;
    if(user==0 && vibration) {
        AcquireSRWLockExclusive(&g_lock);
        g_rumbleLeft=static_cast<float>(vibration->left)/65535.0f;
        g_rumbleRight=static_cast<float>(vibration->right)/65535.0f;
        g_rumbleWaiting=true;
        ReleaseSRWLockExclusive(&g_lock);
    }
    return g_realSetState?g_realSetState(user,vibration):ERROR_SUCCESS;
}

FARPROC WINAPI HookGetProcAddress(HMODULE module,LPCSTR name) noexcept {
    FARPROC real=g_realGetProc?g_realGetProc(module,name):nullptr;
    // Ordinal lookups arrive with a tiny integer in the pointer and must not be
    // dereferenced as a string.
    if(!name) return real;
    if(reinterpret_cast<ULONG_PTR>(name)>>16==0) {
        // An ordinal, not a string. One hundred is XInputGetStateEx.
        if(reinterpret_cast<ULONG_PTR>(name)==100 && real) {
            g_realGetStateEx=reinterpret_cast<GetStateFn>(real);
            return reinterpret_cast<FARPROC>(&HookGetStateEx);
        }
        return real;
    }
    __try {
        if(!std::strcmp(name,"XInputGetState")) {
            g_askedForGetState=true;
            g_realGetState=reinterpret_cast<GetStateFn>(real);
            if(g_log) g_log("XINPUT the game asked for XInputGetState; handing it ours");
            return reinterpret_cast<FARPROC>(&HookGetState);
        }
        if(!std::strcmp(name,"XInputSetState")) {
            g_realSetState=reinterpret_cast<SetStateFn>(real);
            return reinterpret_cast<FARPROC>(&HookSetState);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return real;
}
}

bool InstallXInputBridge(const ImageProfile& image,CaptureLogger log) noexcept {
    if(g_installed) return true;
    g_log=log;
    if(!image.base) { std::snprintf(g_status,sizeof(g_status),"no image"); return false; }
    auto slot=reinterpret_cast<void**>(image.base+kGetProcAddressSlotRva);
    if(!Readable(slot,sizeof(void*))) {
        std::snprintf(g_status,sizeof(g_status),"IAT slot not readable");
        return false;
    }
    // Refuse unless the slot still holds the real GetProcAddress. Anything else
    // means another plugin is there, or the profile is wrong, and a blind write
    // would break the game rather than this feature.
    auto kernel=GetModuleHandleW(L"kernel32.dll");
    auto expected=kernel?reinterpret_cast<void*>(GetProcAddress(kernel,"GetProcAddress")):nullptr;
    if(!expected || *slot!=expected) {
        std::snprintf(g_status,sizeof(g_status),
                      "IAT slot holds %p, expected GetProcAddress at %p",*slot,expected);
        return false;
    }
    g_realGetProc=reinterpret_cast<GetProcFn>(expected);
    bool changed=false;
    const bool ok=ReplacePointer(slot,expected,reinterpret_cast<void*>(&HookGetProcAddress),changed);
    if(!changed) {
        std::snprintf(g_status,sizeof(g_status),"IAT slot would not take the swap");
        return false;
    }
    g_slot=slot;
    g_installed=true;
    std::snprintf(g_status,sizeof(g_status),"installed%s",ok?"":", page protection not restored");
    return true;
}

// If the game resolved XInput before this plugin loaded, the IAT swap never
// sees the request: asked stays at zero and nothing reaches the game. The
// resolved pointers are still somewhere in EDF6 writable data, so they are
// looked for and swapped there instead.
//
// Searching for the value rather than a known offset means no new profile
// constant and nothing to go stale: the address being looked for is whatever
// the loader actually gave the game this run.
int SweepXInputPointers(unsigned char* base,CaptureLogger log) noexcept {
    // Every loaded module whose name carries xinput, not the first one that
    // answers: the game may have loaded a different version from the one this
    // plugin loaded for its own gamepad watch, and then the address being
    // searched for would be the wrong one.
    void* wanted[16]{};
    void* replacement[16]{};
    int count=0;
    char found[256]{};
    int used=0;
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snapshot!=INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{};
        entry.dwSize=sizeof(entry);
        for(BOOL more=Module32FirstW(snapshot,&entry);more;more=Module32NextW(snapshot,&entry)) {
            wchar_t lowered[MAX_PATH]{};
            wcsncpy_s(lowered,entry.szModule,_TRUNCATE);
            _wcslwr_s(lowered);
            if(!AGameFacingXInput(lowered)) continue;
            auto get=reinterpret_cast<void*>(GetProcAddress(entry.hModule,"XInputGetState"));
            auto set=reinterpret_cast<void*>(GetProcAddress(entry.hModule,"XInputSetState"));
            auto ex=reinterpret_cast<void*>(GetProcAddress(entry.hModule,MAKEINTRESOURCEA(100)));
            if(ex && ex!=get && count<16) {
                wanted[count]=ex;
                replacement[count++]=reinterpret_cast<void*>(&HookGetStateEx);
                if(!g_realGetStateEx) g_realGetStateEx=reinterpret_cast<GetStateFn>(ex);
            }
            if(get && count<16) {
                wanted[count]=get;
                replacement[count++]=reinterpret_cast<void*>(&HookGetState);
                if(!g_realGetState) g_realGetState=reinterpret_cast<GetStateFn>(get);
            }
            if(set && count<16) {
                wanted[count]=set;
                replacement[count++]=reinterpret_cast<void*>(&HookSetState);
                if(!g_realSetState) g_realSetState=reinterpret_cast<SetStateFn>(set);
            }
            const int written=_snprintf_s(found+used,sizeof(found)-used,_TRUNCATE,
                                          "%s%ls",used?",":"",entry.szModule);
            if(written>0) used+=written;
        }
        CloseHandle(snapshot);
    }
    if(!count) {
        if(log) log("XINPUT sweep: no xinput module is loaded");
        return 0;
    }

    int inImage=0, inMemory=0, regions=0;
    unsigned long long bytes=0;
    auto swap=[&](void** at,int& tally) {
        for(int i=0;i<count;++i) {
            if(*at!=wanted[i]) continue;
            bool changed=false;
            ReplacePointer(at,*at,replacement[i],changed);
            if(changed) ++tally;
            return;
        }
    };
    // The module's own writable data first, then everywhere else the process has
    // committed. The pointer is as likely to sit in a heap object as in a static,
    // and a static is the only place a section scan can find it.
    __try {
        if(base) {
            auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if(dos->e_magic==IMAGE_DOS_SIGNATURE) {
                auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
                if(nt->Signature==IMAGE_NT_SIGNATURE) {
                    auto section=IMAGE_FIRST_SECTION(nt);
                    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++section) {
                        if(!(section->Characteristics&IMAGE_SCN_MEM_WRITE)) continue;
                        auto at=reinterpret_cast<void**>(base+section->VirtualAddress);
                        const std::size_t slots=section->Misc.VirtualSize/sizeof(void*);
                        if(!Readable(at,slots*sizeof(void*))) continue;
                        for(std::size_t j=0;j<slots;++j) swap(&at[j],inImage);
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    __try {
        MEMORY_BASIC_INFORMATION info{};
        auto address=reinterpret_cast<unsigned char*>(0x10000);
        while(VirtualQuery(address,&info,sizeof(info))==sizeof(info)) {
            auto next=static_cast<unsigned char*>(info.BaseAddress)+info.RegionSize;
            if(next<=address) break;
            const DWORD writable=PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE
                                |PAGE_EXECUTE_WRITECOPY;
            const bool worth=info.State==MEM_COMMIT && (info.Protect&writable)
                          && !(info.Protect&PAGE_GUARD) && info.RegionSize<=(256u<<20);
            if(worth) {
                ++regions;
                bytes+=info.RegionSize;
                auto at=reinterpret_cast<void**>(info.BaseAddress);
                const std::size_t slots=info.RegionSize/sizeof(void*);
                for(std::size_t j=0;j<slots;++j) swap(&at[j],inMemory);
            }
            address=next;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if(log) {
        char note[320]{};
        _snprintf_s(note,sizeof(note),_TRUNCATE,
                    "XINPUT sweep modules=[%s] targets=%d swapped image=%d memory=%d "
                    "over %d regions, %llu MB",
                    found,count,inImage,inMemory,regions,bytes>>20);
        log(note);
    }
    return inImage+inMemory;
}


bool XInputBridgeInstalled() noexcept { return g_installed; }

// Overwrite the first bytes of an exported function with a jump to ours.
//
// This is the only way left that does not depend on when the game resolved the
// address: the import table was never used, and the pointer it holds is not
// anywhere a scan can reliably find. Patching the function itself catches every
// caller however it got there.
//
// No trampoline, because none is needed. The replacement never calls the
// function it replaced: the real device is read through a different xinput
// library, loaded separately and left alone, so the overwritten instructions are
// never wanted again. That removes the one genuinely difficult part of an inline
// hook, which is relocating whatever instructions the jump lands on top of.
// Whether an address belongs to the module it was asked of.
//
// An export can be a forwarder: GetProcAddress hands back an address inside
// some other library entirely, and writing a jump over that corrupts an
// implementation shared with the rest of the process. Nothing here has any
// business patching a function that is not where it says it is.
bool InsideModule(HMODULE module,void* address) noexcept {
    __try {
        auto base=reinterpret_cast<unsigned char*>(module);
        auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(!base || !address || dos->e_magic!=IMAGE_DOS_SIGNATURE) return false;
        auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE) return false;
        const auto size=nt->OptionalHeader.SizeOfImage;
        auto at=static_cast<unsigned char*>(address);
        return at>=base && at<base+size;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PatchExport(HMODULE module,const char* name,void* replacement,CaptureLogger log) noexcept {
    if(!module || !replacement) return false;
    auto target=reinterpret_cast<unsigned char*>(GetProcAddress(module,name));
    if(!InsideModule(module,target)) return false;   // a forwarder; not ours to write
    if(!target) return false;
    // mov rax, imm64 ; jmp rax
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto address=reinterpret_cast<std::uintptr_t>(replacement);
    std::memcpy(jump+2,&address,sizeof(address));
    if(!std::memcmp(target,jump,sizeof(jump))) return true;   // already ours
    DWORD previous=0;
    if(!VirtualProtect(target,sizeof(jump),PAGE_EXECUTE_READWRITE,&previous)) return false;
    std::memcpy(target,jump,sizeof(jump));
    RecordPatch(target,sizeof(jump),name);
    DWORD restored=0;
    VirtualProtect(target,sizeof(jump),previous,&restored);
    FlushInstructionCache(GetCurrentProcess(),target,sizeof(jump));
    if(log) {
        char note[160]{};
        _snprintf_s(note,sizeof(note),_TRUNCATE,"XINPUT patched %s at %p",name,target);
        log(note);
    }
    return true;
}

bool g_patched=false;

void ReportXInputModules(const char* when,CaptureLogger log) noexcept {
    // Which xinput libraries the process has, and who is holding them. Asked
    // before this plugin loads one of its own, the answer says whether the game
    // uses XInput at all - which is the branch everything else depends on.
    if(!log) return;
    char found[256]{};
    int used=0;
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snapshot!=INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{};
        entry.dwSize=sizeof(entry);
        for(BOOL more=Module32FirstW(snapshot,&entry);more;more=Module32NextW(snapshot,&entry)) {
            wchar_t lowered[MAX_PATH]{};
            wcsncpy_s(lowered,entry.szModule,_TRUNCATE);
            _wcslwr_s(lowered);
            if(!wcsstr(lowered,L"xinput")) continue;
            const int written=_snprintf_s(found+used,sizeof(found)-used,_TRUNCATE,
                                          "%s%ls",used?",":"",entry.szModule);
            if(written>0) used+=written;
        }
        CloseHandle(snapshot);
    }
    char note[320]{};
    _snprintf_s(note,sizeof(note),_TRUNCATE,"XINPUT modules %s: [%s]",when,used?found:"none");
    log(note);
}

bool g_sweepAllowed=false;

void AllowSweep(bool on) noexcept { g_sweepAllowed=on; }

// Off by default, and it should stay off.
//
// It swapped twelve to seventeen fresh pointers on every pass, one hundred and
// ninety two over fourteen sweeps, and the reads never arrived. Pointers that
// keep coming back are not the pointers a program is holding: they are copies
// passing through, on thread stacks and in temporaries, and writing to those is
// writing into somebody's live call frame. It also walks two and a half
// gigabytes each time, which is the periodic stutter the user reported.
//
// Kept because it proved something worth knowing - the game writes rumble
// through a pointer it does hold, so a swap can reach it - but it is not a way
// to find the read path.
int SweepResolvedXInput(const ImageProfile& image,CaptureLogger log) noexcept {
    if(!g_sweepAllowed) return 0;
    // Stop only when a read has actually arrived. Stopping at "we swapped
    // something" was wrong: the first sweep ran seven seconds in, over 126 MB,
    // caught the pointer the game writes rumble through and declared victory,
    // while the pointer it reads with had not been stored yet. The game keeps
    // allocating; the sweep has to keep looking until it is being read.
    if(g_getStateCalls || g_getStateExCalls) return 0;
    // The scan walks every committed writable page, which is gigabytes. Rarely,
    // and not forever: the game loads what it is going to load in the first
    // minute, and a scan that has failed twenty times is not going to succeed on
    // the twenty first while costing the frame rate to find out.
    static ULONGLONG last=0;
    static int attempts=0;
    const ULONGLONG now=GetTickCount64();
    if(attempts>=30) return 0;
    if(last && now-last<10000) return 0;
    last=now;
    ++attempts;
    const int swapped=SweepXInputPointers(image.base,log);
    if(attempts==30 && log)
        log("XINPUT sweep: giving up; the game is not reading through any pointer "
            "this can reach");
    if(swapped) {
        g_askedForGetState=true;
        g_sweptTotal+=swapped;
        std::snprintf(g_status,sizeof(g_status),"installed, %d pointer(s) swept over %d sweeps",
                      g_sweptTotal,attempts);
    }
    return swapped;
}

const char* XInputBridgeStatus() noexcept {
    static char note[256];
    std::snprintf(note,sizeof(note),
                  "%s asked=%d reads=%llu readsEx=%llu writes=%llu caps=%llu/%llu injected=%llu",
                  g_status,g_askedForGetState?1:0,g_getStateCalls,g_getStateExCalls,
                  g_setStateCalls,g_capsAnswered,g_capsCalls,g_injected);
    return note;
}

// Which libraries have already been done. The game loads xinput1_4.dll about
// five seconds after this plugin, well after the one pass this used to make, and
// an unpatched library is an entry point the game can read the pad through
// without ever meeting the headset. So the pass runs again whenever it is asked
// to, and skips what it has already done.
HMODULE g_done[8]{};
HMODULE g_reserved=nullptr;
int g_doneCount=0;
int g_patchedTotal=0;

bool AlreadyDone(HMODULE module) noexcept {
    for(int i=0;i<g_doneCount;++i) if(g_done[i]==module) return true;
    return false;
}

// A private copy of an XInput library, under a name nothing else will ask for.
//
// Copied next to this plugin and loaded from there. Windows keeps it as its own
// module with its own exports, so reading the real device through it costs
// nothing, and the game has no way to resolve into it: it does not know the
// name. That is the whole point -- the library the game does find is the one we
// patched, every time, rather than about half the time.
HMODULE LoadPrivateXInput(CaptureLogger log) noexcept {
    // Two things this must not do, both learnt the hard way.
    //
    // It must not sit beside the plugin: Mods/Plugins is where the loader looks
    // for plugins, so a library dropped there is loaded as one, and the game
    // stopped starting. And its name must not contain "xinput", because the
    // sweep that patches XInput libraries recognises them by name -- it patched
    // the very copy kept for reading the real device, through a forwarder, into
    // a module shared with the rest of the process.
    //
    // So: the temp directory, and a name with neither problem.
    wchar_t here[MAX_PATH]{};
    const DWORD length=GetTempPathW(MAX_PATH,here);
    if(!length || length>=MAX_PATH) return nullptr;
    wchar_t copy[MAX_PATH]{};
    if(wcslen(here)+32>=MAX_PATH) return nullptr;
    wcscpy_s(copy,here);
    wcscat_s(copy,L"EDF6VRSparePad.dll");

    wchar_t system[MAX_PATH]{};
    const UINT room=GetSystemDirectoryW(system,MAX_PATH);
    if(!room || room>=MAX_PATH) return nullptr;
    const wchar_t* candidates[]={L"\\xinput1_4.dll",L"\\xinput9_1_0.dll"};
    for(const wchar_t* leaf:candidates) {
        wchar_t source[MAX_PATH]{};
        wcscpy_s(source,system);
        if(wcslen(source)+wcslen(leaf)+1>=MAX_PATH) continue;
        wcscat_s(source,leaf);
        // Overwritten each run: a stale copy from an older Windows would be a
        // silent mismatch, and copying a megabyte once per launch costs nothing.
        if(!CopyFileW(source,copy,FALSE) && GetLastError()!=ERROR_SHARING_VIOLATION) continue;
        if(HMODULE loaded=LoadLibraryW(copy)) {
            if(log) {
                char note[MAX_PATH];
                std::snprintf(note,sizeof(note),
                              "XINPUT reading the real device through a private copy, so the game "
                              "cannot resolve into it");
                log(note);
            }
            return loaded;
        }
    }
    return nullptr;
}

bool PatchXInputExports(CaptureLogger log) noexcept {
    // One library is kept unpatched and used to read the real device, so a
    // physical pad still works. It has to be one the game is not using, or the
    // patch would swallow its own reads.
    // One library is left unpatched so the real device can still be read, and
    // which one it is decided whether the title screen worked.
    //
    // It used to be a system xinput, loaded by name. Loading it is what put it in
    // the process, and from then on the game could resolve XInput through either
    // that or the one we patched -- a race the title screen lost about half the
    // time, with capabilities answered and XInputGetState never called once.
    // Naming a different system library instead is no answer either: xinput1_3's
    // exports are forwarders, and patching those wrote over an implementation
    // shared with the rest of the process and stopped the game starting.
    //
    // So the spare is a private copy under a name nothing else will ever ask
    // for. The game cannot resolve into it, there is nothing to race over, and
    // a physical pad still works.
    // With no real pad wanted, no library is spared at all.
    //
    // The spare exists only so a physical controller keeps working, and it is
    // the whole of the title-screen trouble: whichever library is left unpatched
    // is one the game might resolve XInput through, and then the headset's
    // buttons never reach it. A private copy under a name the game could not
    // know was meant to settle that, and the game would not start with one --
    // neither beside the plugin nor in the temp directory. So the choice is put
    // plainly instead: a real pad, or a title screen that always works.
    static HMODULE mine=nullptr;
    if(!g_realPadWanted) {
        g_realGetState=nullptr;
        g_realGetStateEx=nullptr;
        g_realSetState=nullptr;
        g_realGetCaps=nullptr;
    }
    if(!mine && g_realPadWanted && g_privateSpare) mine=LoadPrivateXInput(log);
    if(!mine && g_realPadWanted) {
        // No copy: fall back to the old arrangement rather than going without.
        const wchar_t* spare[]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"};
        for(const wchar_t* name:spare) {
            if(GetModuleHandleW(name)) continue;
            mine=LoadLibraryW(name);
            if(mine) break;
        }
    }
    if(!mine && g_realPadWanted) mine=GetModuleHandleW(L"xinput1_4.dll");
    if(mine && !g_reserved) {
        g_reserved=mine;
        wchar_t wide[MAX_PATH]{};
        if(log && GetModuleFileNameW(mine,wide,MAX_PATH)) {
            char name[MAX_PATH]{};
            const wchar_t* leaf=wcsrchr(wide,static_cast<wchar_t>(92));
            WideCharToMultiByte(CP_UTF8,0,leaf?leaf+1:wide,-1,name,sizeof(name),nullptr,nullptr);
            char note[MAX_PATH+64];
            std::snprintf(note,sizeof(note),
                          "XINPUT keeping %s unpatched, to read the real device through",name);
            log(note);
        }
    }
    if(mine && !g_realGetState) {
        g_realGetState=reinterpret_cast<GetStateFn>(
            reinterpret_cast<void*>(GetProcAddress(mine,"XInputGetState")));
        g_realSetState=reinterpret_cast<SetStateFn>(
            reinterpret_cast<void*>(GetProcAddress(mine,"XInputSetState")));
        g_realGetStateEx=reinterpret_cast<GetStateFn>(
            reinterpret_cast<void*>(GetProcAddress(mine,MAKEINTRESOURCEA(100))));
        g_realGetCaps=reinterpret_cast<GetCapsFn>(
            reinterpret_cast<void*>(GetProcAddress(mine,"XInputGetCapabilities")));
    }

    int patched=0;
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snapshot!=INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{};
        entry.dwSize=sizeof(entry);
        for(BOOL more=Module32FirstW(snapshot,&entry);more;more=Module32NextW(snapshot,&entry)) {
            wchar_t lowered[MAX_PATH]{};
            wcsncpy_s(lowered,entry.szModule,_TRUNCATE);
            _wcslwr_s(lowered);
            if(!AGameFacingXInput(lowered)) continue;
            if(entry.hModule==mine) continue;     // the one kept for real reads
            if(AlreadyDone(entry.hModule)) continue;
            if(log) {
                wchar_t path[MAX_PATH]{};
                char file[MAX_PATH]{"?"};
                if(GetModuleFileNameW(entry.hModule,path,MAX_PATH))
                    WideCharToMultiByte(CP_UTF8,0,path,-1,file,sizeof(file),nullptr,nullptr);
                char note[MAX_PATH+96];
                std::snprintf(note,sizeof(note),"XINPUT patching base %p from %s",
                              static_cast<void*>(entry.hModule),file);
                log(note);
            }
            if(PatchExport(entry.hModule,"XInputGetState",
                           reinterpret_cast<void*>(&HookGetState),log)) ++patched;
            if(PatchExport(entry.hModule,"XInputSetState",
                           reinterpret_cast<void*>(&HookSetState),log)) ++patched;
            if(PatchExport(entry.hModule,"XInputGetCapabilities",
                           reinterpret_cast<void*>(&HookGetCapabilities),log)) ++patched;
            if(GetProcAddress(entry.hModule,MAKEINTRESOURCEA(100))
               && PatchExport(entry.hModule,MAKEINTRESOURCEA(100),
                              reinterpret_cast<void*>(&HookGetStateEx),log)) ++patched;
            if(g_doneCount<8) g_done[g_doneCount++]=entry.hModule;
        }
        CloseHandle(snapshot);
    }
    if(patched) {
        g_patched=true;
        g_patchedTotal+=patched;
        std::snprintf(g_status,sizeof(g_status),"installed, %d export(s) patched in %d library(s)",
                      g_patchedTotal,g_doneCount);
    }
    return g_patched;
}

void ReportXInputCallers(CaptureLogger log) noexcept {
    if(!log) return;
    if(g_capsTraceChanged) {
        g_capsTraceChanged=false;
        for(int i=0;i<g_capsTraced;++i) log(g_capsTrace[i]);
    }
    if(!g_callersChanged) return;
    g_callersChanged=false;
    for(int i=0;i<g_callerCount;++i) {
        HMODULE owner=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                          |GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(g_callers[i].address),&owner);
        wchar_t wide[MAX_PATH]{};
        char name[MAX_PATH]{"?"};
        if(owner && GetModuleFileNameW(owner,wide,MAX_PATH)) {
            const wchar_t* leaf=wcsrchr(wide,static_cast<wchar_t>(92));
            WideCharToMultiByte(CP_UTF8,0,leaf?leaf+1:wide,-1,name,sizeof(name),nullptr,nullptr);
        }
        char note[256];
        std::snprintf(note,sizeof(note),"XINPUT read from %s+%llX after %llu call(s)",name,
                      owner?static_cast<unsigned long long>(
                          reinterpret_cast<unsigned char*>(g_callers[i].address)
                          -reinterpret_cast<unsigned char*>(owner)):0ull,
                      g_callers[i].calls);
        log(note);
    }
}

void AllowCapabilities(bool on) noexcept { g_capsAllowed=on; }

void OfferPad(bool on) noexcept { g_padOffered=on; }

void UsePrivateSpare(bool on) noexcept { g_privateSpare=on; }

void WantRealPad(bool on) noexcept { g_realPadWanted=on; }

void SetPadProvider(PadProvider provider) noexcept { g_provider=provider; }

void SetPadState(const PadState& pad) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_pad=pad;
    ReleaseSRWLockExclusive(&g_lock);
}

bool TakeRumble(float& left,float& right) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool waiting=g_rumbleWaiting;
    left=g_rumbleLeft; right=g_rumbleRight;
    g_rumbleWaiting=false;
    ReleaseSRWLockExclusive(&g_lock);
    return waiting;
}
}
