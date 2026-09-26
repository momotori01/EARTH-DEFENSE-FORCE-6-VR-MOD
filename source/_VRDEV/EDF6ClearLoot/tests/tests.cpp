#include "../src/plugin.cpp"
#include <limits>
using namespace clearloot;
static unsigned failures=0,pickups=0,exits=0,sounds=0,uis=0,bgms=0;
static float radiusSeen=0,healSeen=0;
static int exitSeen=0;
static void *managerExpected=nullptr,*soldierExpected=nullptr,*callbackExpected=nullptr;
static const float* positionExpected=nullptr;
static bool uiSuccess=true;
using Probe=void(__fastcall*)(void*,void*,const float*);
static Probe probe=nullptr;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
template<class T>T& Field(void* p,std::size_t offset){return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);}
bool ProbeSafe(const float* position) noexcept {
    __try {probe(nullptr,nullptr,position);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void __fastcall PickupStub(void* manager,void* soldier,const float* position,float radius,float heal,void* callback){
    ++pickups;radiusSeen=radius;healSeen=heal;
    CHECK(manager==managerExpected && soldier==soldierExpected);
    CHECK(position==positionExpected && callback==callbackExpected);
    // Execute the same legacy SSE memory instruction contract as the crash site.
    CHECK(ProbeSafe(position));
}
void __fastcall ExitStub(void*,int result){++exits;exitSeen=result;}
void __fastcall SoundStub(void* manager,const wchar_t* name,void* options){
    CHECK(manager==*reinterpret_cast<void**>(image+soundManagerGlobal));
    CHECK(name==reinterpret_cast<const wchar_t*>(image+soundName) && options==nullptr);++sounds;
}
void* __fastcall UiStub(void* manager,void* output,const wchar_t* path,void* options){
    CHECK(manager && path && options==manager);++uis;
    *static_cast<void**>(output)=uiSuccess?manager:nullptr;return output;
}
void __fastcall BgmStub(void* manager,const char* name){CHECK(manager && name);++bgms;}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    CHECK(mapped && CheckProfile(mapped));if(!image)return 2;
    const unsigned char code[]={0x0F,0x57,0xC0,0x41,0x0F,0x5C,0x00,0xC3};
    auto executable=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    CHECK(executable);if(!executable)return 2;
    std::memcpy(executable,code,sizeof(code));DWORD old=0;
    CHECK(VirtualProtect(executable,4096,PAGE_EXECUTE_READ,&old));
    FlushInstructionCache(GetCurrentProcess(),executable,sizeof(code));probe=reinterpret_cast<Probe>(executable);
    alignas(16) unsigned char soldier[0x400]{},manager[0xE00]{},oldSnapshot[64]{};
    // Old snapshot's position at +20 was unaligned and reproduces C0000005.
    CHECK(!ProbeSafe(reinterpret_cast<const float*>(oldSnapshot+20)));
    Field<void*>(image,managerGlobal)=manager;Field<void*>(image,soundManagerGlobal)=manager;
    Field<void*>(soldier,0)=image+0x17CDF28;Field<unsigned>(soldier,0x314)=42;
    Field<void*>(soldier,0x340)=manager;
    alignas(16) float position[4]{1,2,3,1};
    void* callback[2]{image+callbackTable,soldier};
    managerExpected=manager;soldierExpected=soldier;callbackExpected=callback;positionExpected=position;
    originalPickup=&PickupStub;originalExit=&ExitStub;originalCreateUi=&UiStub;originalBgm=&BgmStub;
    auto update=[&]{const auto n=pickups;HookPickup(manager,soldier,position,2.5f,.8f,callback);CHECK(pickups==n+1 && healSeen==.8f);};
    void* uiOut[2]{};
    auto ui=[&](const wchar_t* name){CHECK(HookCreateUi(manager,uiOut,name,manager)==uiOut);};
    auto clear=[&]{ui(L"app:/ui/lyt_HUiMissionCleared.sgo");};
    // Resource preloads are not hooked. Other UI/music and no soldier never arm.
    clear();CHECK(!request.until);
    update();CHECK(radiusSeen==2.5f);
    ui(L"app:/ui/lyt_HUiMissionFailed.sgo");ui(L"lyt_HUiMissionCleared.sgo.backup");
    HookBgm(manager,"Jingle_MissionFailed");CHECK(!request.until);
    CHECK(ClearUiName(L"APP:\\UI\\LYT_HUIMISSIONCLEARED.SGO"));
    uiSuccess=false;clear();CHECK(!request.until);uiSuccess=true;
    // Clear banner arms, but does not invoke native pickup inside the UI call.
    auto n=pickups;clear();CHECK(request.until && pickups==n && !request.collected);
    update();CHECK(radiusSeen==mapRadius && request.collected);
    update();CHECK(radiusSeen==2.5f);clear();update();CHECK(radiusSeen==2.5f);
    HookBgm(manager,"Jingle_MissionCleared");update();CHECK(radiusSeen==2.5f); // duplicate trigger
    n=pickups;HookExit(nullptr,1);CHECK(pickups==n && exits==1 && exitSeen==1 && !request.until && !snapshot.soldier);
    update();CHECK(radiusSeen==2.5f); // next mission gets normal range
    // All four classes; final clear jingle, and native busy actor postponement.
    for(unsigned vt:{0x17CDF28u,0x17D0FF8u,0x17CF100u,0x17CF5B8u}) {
        HookExit(nullptr,2);Field<void*>(soldier,0)=image+vt;update();
        HookBgm(manager,"Jingle_MissionClearedFinal");CHECK(request.until);
        Field<void*>(soldier,0x340)=nullptr;update();CHECK(radiusSeen==2.5f && !request.collected);
        Field<void*>(soldier,0x340)=manager;update();CHECK(radiusSeen==mapRadius && request.collected);
    }
    for(int result:{0,1,2,3,4,5}) {
        HookExit(nullptr,result);update();clear();n=pickups;
        HookExit(nullptr,result);CHECK(pickups==n && exitSeen==result && !request.until);
        update();CHECK(radiusSeen==2.5f);
    }
    clear();request.until=GetTickCount64()-1;update();CHECK(radiusSeen==2.5f && !request.until);
    clear();++Field<unsigned>(soldier,0x314);update();CHECK(radiusSeen==2.5f && !request.until);
    clear();request.recipient.manager=nullptr;update();CHECK(radiusSeen==2.5f && !request.until);
    snapshot.at=GetTickCount64()-3000;clear();CHECK(!request.until);update();
    enabled=false;clear();CHECK(!request.until);enabled=true;
    clear();enabled=false;update();CHECK(radiusSeen==2.5f && !request.collected);enabled=true;
    // Persistence and exactly one sound on OFF -> ON; no reward/pickup invocation.
    wchar_t temp[MAX_PATH]{},ini[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH,temp)>0 && GetTempFileNameW(temp,L"ecl",0,ini)!=0);
    wcscpy_s(iniPath,ini);originalPlaySound=&SoundStub;
    keyDown=false;enabled=true;n=pickups;
    ToggleKey(true,true);CHECK(!enabled && sounds==0 && GetPrivateProfileIntW(L"ClearLoot",L"Enabled",9,ini)==0);
    ToggleKey(true,true);CHECK(!enabled && sounds==0);
    ToggleKey(false,true);ToggleKey(true,true);
    CHECK(enabled && sounds==1 && GetPrivateProfileIntW(L"ClearLoot",L"Enabled",9,ini)==1);
    ToggleKey(true,true);CHECK(sounds==1 && pickups==n);
    ToggleKey(false,false);ToggleKey(true,false);ToggleKey(true,true);CHECK(enabled && sounds==1);
    Field<void*>(image,soundManagerGlobal)=nullptr;CHECK(!PlayConfirmation() && sounds==1);
    Field<void*>(image,soundManagerGlobal)=manager;originalPlaySound=nullptr;CHECK(!PlayConfirmation() && sounds==1);
    CHECK(DeleteFileW(ini));iniPath[0]=0;
    // Every hook is checked against the actual game profile, conflicts refused.
    bool published=false;CHECK(Install(published) && published);CHECK(!CheckProfile(mapped));
    CHECK(uis && bgms);VirtualFree(executable,0,MEM_RELEASE);
    printf("ClearLoot tests: %u failures (SSE crash reproduction, original pointer forwarding, clear banner/jingle, no exit pickup, 4 classes, stale/duplicate/abort, F1 persistence/sound, real patch profile).\n",failures);
    return failures?1:0;
}
