#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <cstdarg>
#include <atomic>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "memory.h"
namespace clearloot {
unsigned char* image=nullptr;
HMODULE module=nullptr;
wchar_t logPath[MAX_PATH]{};
wchar_t iniPath[MAX_PATH]{};
SRWLOCK lock=SRWLOCK_INIT;
std::atomic<bool> enabled{true};
struct Snapshot {
    void* manager=nullptr; void* soldier=nullptr;
    unsigned id=0;
    ULONGLONG at=0;
};
Snapshot snapshot{};
struct ClearRequest { Snapshot recipient{}; ULONGLONG until=0; bool collected=false; };
ClearRequest request{};
using CreateUi=void*(__fastcall*)(void*,void*,const wchar_t*,void*);
using Bgm=void(__fastcall*)(void*,const char*);
CreateUi originalCreateUi=nullptr;
Bgm originalBgm=nullptr;
constexpr unsigned uiCall=0x1B2876,uiTarget=0x119C600;
constexpr unsigned bgmCall=0x1ACD77,bgmTarget=0x7B27B0;
// The story message window (CreateStoryMessageWindow, native 1B2210, registered
// at 1EB868 for "::StoryMessageWindow@ CreateStoryMessageWindow()"): its object
// is allocated by the call at 1B221E. In the shipped missions the only scripts
// that open one are the white-out / black-out endings (AsCommon.h WhiteOutMessage
// and BlackOutMessage: M011, M039, M055, M066, M092, M093_2, M099, M116, RM081,
// DLC M099; M000B's are commented out), and each goes on to ResetScene() two
// seconds later and only then to the mission-clear banner. ResetScene takes the
// soldier and the items away, so at the banner there is no pickup update left to
// collect in (the log: CLEAR ... recentRecipient=0). The window is the clear
// signal there (the user, 2026-10-04: arm the collection when the text
// presentation starts).
using StoryAlloc=void*(__fastcall*)(std::size_t);
StoryAlloc originalStoryAlloc=nullptr;
constexpr unsigned storyCall=0x1B221E,storyTarget=0x12D85B0,storyFunction=0x1B2210,storyRegister=0x1EB868,storyDeclaration=0x1796DF8;
using Pickup=void(__fastcall*)(void*,void*,const float*,float,float,void*);
using Exit=void(__fastcall*)(void*,int);
Pickup originalPickup=nullptr;
Exit originalExit=nullptr;
using PlaySound=void(__fastcall*)(void*,const wchar_t*,void*);
PlaySound originalPlaySound=nullptr;
constexpr unsigned soundManagerGlobal=0x20B2950,soundName=0x17A6CF8,soundTarget=0x7B2860;
bool keyDown=false;
constexpr unsigned pickupCall=0x59B938,pickupTarget=0x2C8AC0;
constexpr unsigned exitLea=0x1E649E,exitTarget=0x1BCFA0;
constexpr unsigned managerGlobal=0x20B2988,callbackTable=0x17D2FE8;
constexpr float mapRadius=1000000.0f;
void Log(const char* format,...) noexcept {
    if(!logPath[0]) return;
    char text[1000]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    FILE* f=nullptr;if(_wfopen_s(&f,logPath,L"ab") || !f)return;
    SYSTEMTIME t{};GetLocalTime(&t);
    fprintf(f,"[%02u:%02u:%02u] %s\r\n",t.wHour,t.wMinute,t.wSecond,text);fclose(f);
}
bool Soldier(void* ptr) noexcept {
    if(!Readable(ptr,0x348))return false;
    auto v=*static_cast<void**>(ptr);
    return v==image+0x17CDF28 || v==image+0x17D0FF8 || v==image+0x17CF100 || v==image+0x17CF5B8;
}
bool Capture(void* manager,void* soldier,const float* position,float heal,void* callback,Snapshot& out,ULONGLONG now) noexcept {
    __try {
        if(!Soldier(soldier) || !Readable(position,16) || !Readable(callback,16)
           || manager!=*reinterpret_cast<void**>(image+managerGlobal)
           || *static_cast<void**>(callback)!=image+callbackTable
           || static_cast<void**>(callback)[1]!=soldier || !std::isfinite(heal))return false;
        for(int j=0;j<4;++j)if(!std::isfinite(position[j]))return false;
        out.manager=manager;out.soldier=soldier;out.id=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(soldier)+0x314);
        out.at=now;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool PlayConfirmation() {
    auto manager=*reinterpret_cast<void**>(image+soundManagerGlobal);
    if(!originalPlaySound || !Readable(manager,sizeof(void*)))return false;
    // Exact sound-only call from native pickup at 2C756E..2C7584.
    // Do not invoke 590F10 (ESB visual effect), or reward/healing routines.
    originalPlaySound(manager,reinterpret_cast<const wchar_t*>(image+soundName),nullptr);
    return true;
}
void ToggleKey(bool down,bool focused) {
    const bool pressed=focused && down && !keyDown;keyDown=down;
    if(!pressed)return;
    const bool next=!enabled;
    if(!WritePrivateProfileStringW(L"ClearLoot",L"Enabled",next?L"1":L"0",iniPath)) {
        Log("F1: could not save setting, state unchanged (error=%lu)",GetLastError());return;
    }
    enabled=next;
    Log("F1: clear loot %s (saved)",enabled.load()?"ON":"OFF");
    if(enabled) {
        Log("ON confirmation: sound-only item pickup requested=%d; no item or healing awarded",PlayConfirmation());
    }
}
bool SameRecipient(const Snapshot& a,const Snapshot& b) noexcept {
    return a.manager==b.manager && a.soldier==b.soldier && a.id==b.id;
}
bool Recent(const Snapshot& s,ULONGLONG now) noexcept {
    return s.manager && s.soldier && now>=s.at && now-s.at<=2000;
}
void ArmClear(const char* source,ULONGLONG now) {
    AcquireSRWLockExclusive(&lock);
    const bool fresh=Recent(snapshot,now);
    const bool duplicate=request.until && now<=request.until && SameRecipient(request.recipient,snapshot);
    const bool armed=fresh && !duplicate && enabled.load();
    if(armed) request={snapshot,now+15000,false};
    ReleaseSRWLockExclusive(&lock);
    Log("CLEAR signal=%s enabled=%d recentRecipient=%d armed=%d duplicate=%d",source,enabled.load(),fresh,armed,duplicate);
}
template<class C,std::size_t N> bool ReadName(const C* src,C (&dst)[N]) noexcept {
    __try {
        if(!src)return false;
        for(std::size_t i=0;i<N;++i) {
            if(!Readable(src+i,sizeof(C)))return false;
            dst[i]=src[i];if(!dst[i])return true;
        }
        dst[0]=0;return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){dst[0]=0;return false;}
}
bool ClearUiName(const wchar_t* name) noexcept {
    const wchar_t* base=name;
    for(auto p=name;*p;++p)if(*p==L'/' || *p==L'\\')base=p+1;
    return _wcsicmp(base,L"lyt_HUiMissionCleared.sgo")==0;
}
void* __fastcall HookCreateUi(void* manager,void* output,const wchar_t* path,void* options) {
    wchar_t name[256]{};const bool named=ReadName(path,name);
    void* result=originalCreateUi(manager,output,path,options);
    // This is UI creation, not resource preloading. Forward its output/return unchanged.
    static unsigned seen=0;
    if(named && seen<32){++seen;Log("UI created: %ls",name);}
    if(named && ClearUiName(name) && Readable(output,sizeof(void*)) && *static_cast<void**>(output))
        ArmClear("mission-clear-ui",GetTickCount64());
    return result;
}
void __fastcall HookBgm(void* manager,const char* track) {
    char name[128]{};const bool named=ReadName(track,name);
    originalBgm(manager,track);
    static unsigned seen=0;
    if(named && seen<32){++seen;Log("BGM: %s",name);}
    if(named && (!std::strcmp(name,"Jingle_MissionCleared") || !std::strcmp(name,"Jingle_MissionClearedFinal")))
        ArmClear("mission-clear-jingle",GetTickCount64());
}
void* __fastcall HookStoryAlloc(std::size_t size) {
    void* window=originalStoryAlloc(size);
    static unsigned seen=0;
    if(seen<32){++seen;Log("STORY message window opened (%zu bytes)",size);}
    // Armed like the banner: collected on the next pickup update, before ResetScene.
    if(window)ArmClear("story-message-window",GetTickCount64());
    return window;
}
// No copied positions or reconstructed callbacks are passed into native pickup.
// Its SSE position operand at 2C8B48 requires the game's original alignment.
int Remaining(void* manager) noexcept {
    __try {
        if(!Readable(manager,0xDE8))return -1;
        auto head=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(manager)+0xDE0);
        if(!Readable(head,0x20))return -1;
        auto node=*reinterpret_cast<unsigned char**>(head);int count=0;
        for(unsigned visited=0;node!=head;++visited) {
            if(visited>=20000 || !Readable(node,0x20))return -1;
            auto item=*reinterpret_cast<unsigned char**>(node+0x18);
            if(!Readable(item,0xC5))return -1;
            if(!item[0xC4])++count;
            node=*reinterpret_cast<unsigned char**>(node);
        }
        return count;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
void __fastcall HookPickup(void* manager,void* soldier,const float* position,float radius,float heal,void* callback) {
    Snapshot next{};const auto now=GetTickCount64();bool collect=false;
    if(Capture(manager,soldier,position,heal,callback,next,now)) {
        DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        ToggleKey((GetAsyncKeyState(VK_F1)&0x8000)!=0,pid==GetCurrentProcessId());
        AcquireSRWLockExclusive(&lock);
        snapshot=next;
        if(request.until && (now>request.until || !SameRecipient(request.recipient,next)))request={};
        // Native pickup returns immediately without soldier+340; defer until ready.
        collect=enabled.load() && request.until && !request.collected
            && *reinterpret_cast<void**>(static_cast<unsigned char*>(soldier)+0x340);
        if(collect)request.collected=true;
        ReleaseSRWLockExclusive(&lock);
    }
    const int before=collect?Remaining(manager):-1;
    if(collect)Log("COLLECT begin native-update radius=%.0f before=%d positionAligned=%d",mapRadius,before,(reinterpret_cast<std::uintptr_t>(position)&15)==0);
    // Exactly one original invocation, in its original thread/update context.
    originalPickup(manager,soldier,position,collect?mapRadius:radius,heal,callback);
    if(collect)Log("COLLECT complete native-update before=%d after=%d",before,Remaining(manager));
}
void __fastcall HookExit(void* functions,int result) {
    AcquireSRWLockExclusive(&lock);
    const bool signalled=request.until!=0,collected=request.collected;
    request={};snapshot={};
    ReleaseSRWLockExclusive(&lock);
    Log("EXIT result=%d clear=%d enabled=%d clearSignal=%d collected=%d; no pickup call at exit",result,result==1,enabled.load(),signalled,collected);
    originalExit(functions,result);
}
bool CheckProfile(HMODULE handle) noexcept {
    __try {
        auto base=reinterpret_cast<unsigned char*>(handle);
        if(!Readable(base,0x1000))return false;
        auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || dos->e_lfanew>0x800)return false;
        auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(pe->Signature!=IMAGE_NT_SIGNATURE || pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64
           || pe->FileHeader.TimeDateStamp!=0x678CCB46 || pe->OptionalHeader.SizeOfImage!=0x22CE000)return false;
        const unsigned char distance[]={0x0F,0x2F,0xC1,0x0F,0x86,0x7E,0,0,0};
        const unsigned char exitLoad[]={0x48,0x8D,0x0D,0xFB,0x6A,0xFD,0xFF};
        const unsigned char pickup[]={0xE8,0x83,0xD1,0xD2,0xFF};
        const unsigned char enumClear[]={0x41,0xB9,1,0,0,0};
        const unsigned char ui[]={0xE8,0x85,0x9D,0xFE,0};
        const unsigned char bgm[]={0xE8,0x34,0x5A,0x60,0};
        // 1B2210: push rbx; sub rsp,20h; mov rbx,rcx; mov ecx,0B0h; call 12D85B0 (the window's allocation).
        const unsigned char story[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xB9,0xB0,0,0,0,0xE8,0x8D,0x63,0x12,1};
        // 1EB868: lea rcx,[1B2210], the function pointer registered with the declaration at 1EB889.
        const unsigned char storyLea[]={0x48,0x8D,0x0D,0xA1,0x69,0xFC,0xFF};
        const unsigned char storyDecl[]={0x48,0x8D,0x15,0x68,0xB5,0x5A,0x01};
        const unsigned char soundCall[]={0x48,0x8B,0x0D,0xDB,0xB3,0xDE,1,0x48,0x8D,0x15,0x7C,0xF7,0x4D,1,0x45,0x33,0xC0,0xE8,0xDC,0xB2,0x4E,0};
        if(std::memcmp(base+0x2C8B97,distance,sizeof(distance)) || std::memcmp(base+exitLea,exitLoad,sizeof(exitLoad))
           || std::memcmp(base+pickupCall,pickup,sizeof(pickup)) || std::memcmp(base+0x1E3624,enumClear,sizeof(enumClear))
           || std::strcmp(reinterpret_cast<const char*>(base+0x1793540),"MISSION_RESULT_CLEAR")
           || std::strcmp(reinterpret_cast<const char*>(base+0x1794D18),"void internal_Exit(::MissionResult )")
           || *reinterpret_cast<void**>(base+callbackTable+8)!=base+0x5A45D0
           || *reinterpret_cast<void**>(base+callbackTable+0x28)!=base+0x5A4610
           || std::memcmp(base+uiCall,ui,sizeof(ui)) || std::memcmp(base+bgmCall,bgm,sizeof(bgm))
           || std::memcmp(base+storyFunction,story,sizeof(story)) || base+storyCall+5+*reinterpret_cast<const std::int32_t*>(base+storyCall+1)!=base+storyTarget
           || std::memcmp(base+storyRegister,storyLea,sizeof(storyLea)) || base+storyRegister+7+*reinterpret_cast<const std::int32_t*>(base+storyRegister+3)!=base+storyFunction
           || std::memcmp(base+0x1EB889,storyDecl,sizeof(storyDecl)) || base+0x1EB889+7+*reinterpret_cast<const std::int32_t*>(base+0x1EB889+3)!=base+storyDeclaration
           || std::strcmp(reinterpret_cast<const char*>(base+storyDeclaration),"::StoryMessageWindow@ CreateStoryMessageWindow()")
           || std::strcmp(reinterpret_cast<const char*>(base+0x1796DD0),"::UI CreateUI_File(const string & in)")
           || std::strcmp(reinterpret_cast<const char*>(base+0x1796E80),"void Bgm(const string & in)")
           || std::memcmp(base+0x2C756E,soundCall,sizeof(soundCall))
           || std::wcscmp(reinterpret_cast<const wchar_t*>(base+soundName),L"\u30a2\u30a4\u30c6\u30e0\u53d6\u5f97"))return false;
        image=base;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Install(bool& published) noexcept {
    published=false;
    originalPickup=reinterpret_cast<Pickup>(image+pickupTarget);
    originalExit=reinterpret_cast<Exit>(image+exitTarget);
    originalPlaySound=reinterpret_cast<PlaySound>(image+soundTarget);
    originalCreateUi=reinterpret_cast<CreateUi>(image+uiTarget);
    originalBgm=reinterpret_cast<Bgm>(image+bgmTarget);
    originalStoryAlloc=reinterpret_cast<StoryAlloc>(image+storyTarget);
    // Patch the native script's registered function address, not script files
    // or a shared VM dispatcher. The pointer uses the same member-call ABI.
    void* thunk=AllocateNearThunk(image+exitLea,reinterpret_cast<void*>(&HookExit));
    if(!thunk)return false;
    bool changed=false;
    if(!RedirectCall(image+pickupCall,reinterpret_cast<void*>(originalPickup),reinterpret_cast<void*>(&HookPickup),changed)) {
        published=changed;VirtualFree(thunk,0,MEM_RELEASE);return false;
    }
    published=true;
    if(!RedirectCall(image+uiCall,reinterpret_cast<void*>(originalCreateUi),reinterpret_cast<void*>(&HookCreateUi),changed)
       || !RedirectCall(image+bgmCall,reinterpret_cast<void*>(originalBgm),reinterpret_cast<void*>(&HookBgm),changed)
       || !RedirectCall(image+storyCall,reinterpret_cast<void*>(originalStoryAlloc),reinterpret_cast<void*>(&HookStoryAlloc),changed)) {
        VirtualFree(thunk,0,MEM_RELEASE);return false;
    }
    const auto delta=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(image+exitLea+7);
    if(delta<INT32_MIN || delta>INT32_MAX){VirtualFree(thunk,0,MEM_RELEASE);return false;}
    DWORD old=0;
    if(!VirtualProtect(image+exitLea,7,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(thunk,0,MEM_RELEASE);return false;}
    const auto disp=static_cast<std::int32_t>(delta);std::memcpy(image+exitLea+3,&disp,4);
    DWORD ignored=0;const bool ok=VirtualProtect(image+exitLea,7,old,&ignored)!=0;
    FlushInstructionCache(GetCurrentProcess(),image+exitLea,7);return ok;
}
}
extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    using namespace clearloot;
    if(!info)return false;
    auto& ini=iniPath;GetModuleFileNameW(module,ini,MAX_PATH);
    auto dot=wcsrchr(ini,L'.');if(!dot)return false;wcscpy_s(dot,MAX_PATH-(dot-ini),L".ini");
    wcscpy_s(logPath,ini);wcscpy_s(wcsrchr(logPath,L'.'),MAX_PATH-(wcsrchr(logPath,L'.')-logPath),L".log");
    enabled=GetPrivateProfileIntW(L"ClearLoot",L"Enabled",1,ini)!=0;
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Clear Loot";info->version=PLUG_VER(0,1,3,0);
    Log("EDF6ClearLoot 0.1.3 loading; enabled=%d; independent of VR",enabled.load());
    // Install even when saved OFF so F1 can turn it back on next session.
    if(!CheckProfile(GetModuleHandleW(L"EDF.dll"))){Log("REFUSED: unsupported game or conflicting patch");return false;}
    bool published=false;const bool ok=Install(published);
    if(!ok)enabled=false;
    Log("HOOK ready=%d published=%d pickup=59B938 clearUi=1B2876 clearBgm=1ACD77 storyWindow=1B221E exitReset=1E649E",ok,published);
    return ok || published; // Never unload code referenced by a published callback.
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){clearloot::module=instance;DisableThreadLibraryCalls(instance);}return TRUE;
}
