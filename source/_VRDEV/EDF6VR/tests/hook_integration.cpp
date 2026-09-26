// Exercise the production callback using synthetic live objects and a no-op game update.
// EDF.dll is mapped ONLY as reference data. No EDF instruction is ever executed here.
#include "../src/plugin.cpp"
#include <vector>

static unsigned int originalCalls=0;
static void __fastcall FakeGameUpdate(void*,const void*) { ++originalCalls; }
static int failures=0;
#define VERIFY(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    // User edits the installed INI while in a mission. Exercise the actual
    // reload entry using a private temporary INI, including partial/invalid text.
    wchar_t tempDir[MAX_PATH]{},tempIni[MAX_PATH]{};
    VERIFY(GetTempPathW(MAX_PATH,tempDir)!=0 && GetTempFileNameW(tempDir,L"eva",0,tempIni)!=0);
    wcscpy_s(g_iniPath,tempIni);g_reticleScale=.35f;
    VERIFY(WritePrivateProfileStringW(L"Render",L"ReticleScale",L"0.65",tempIni)!=0);
    ReloadTunables();VERIFY(std::fabs(g_reticleScale-.65f)<.00001f);
    VERIFY(WritePrivateProfileStringW(L"Render",L"ReticleScale",L"nan",tempIni)!=0);
    ReloadTunables();VERIFY(std::fabs(g_reticleScale-.65f)<.00001f);
    VERIFY(WritePrivateProfileStringW(L"Render",L"ReticleScale",L"100",tempIni)!=0);
    ReloadTunables();VERIFY(g_reticleScale==4.f);
    g_iniPath[0]=0;g_reticleScale=1.f;DeleteFileW(tempIni);
    HMODULE mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!mapped) return 2;
    char reason[256]{};
    if(!edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason))) { puts(reason); return 2; }
    // Firing diagnostics must not run continuously in menus/idle, or mistake
    // a trigger held during a long lock acquisition for the actual spawn.
    PresentationTraceWindow trace{};
    VERIFY(!trace.Tick(100,false,false,0));
    VERIFY(!trace.Tick(101,true,false,0));
    VERIFY(trace.Tick(102,true,true,0));
    VERIFY(!trace.Tick(103,true,true,0));
    VERIFY(trace.Tick(202,true,true,0));
    VERIFY(!trace.Tick(6102,true,true,0));
    VERIFY(trace.Tick(8000,true,true,1)); // projectile after long lock
    VERIFY(!trace.Tick(8001,false,false,1));
    VERIFY(!trace.Tick(8200,true,false,1)); // do not rearm on VR toggle
    trace.lines=600;VERIFY(!trace.Tick(8300,true,true,2));
    VERIFY(ReadPresentationModel(nullptr).flags==-1);
    VERIFY(ReadPresentationModel(reinterpret_cast<void*>(1)).flags==-1);
    std::vector<unsigned char> traceModel(0x500);
    *reinterpret_cast<void**>(traceModel.data())=g_image.base+edf6vr::kModelVtableRva;
    traceModel[0x31C]=3;traceModel[0x480]=1;traceModel[0x4D1]=1;
    const auto unchangedTraceModel=traceModel;
    const auto traceState=ReadPresentationModel(traceModel.data());
    VERIFY(traceState.flags==3 && traceState.enabled==1 && traceState.activity==1);
    VERIFY(traceModel==unchangedTraceModel); // observation never forces visibility
    // Find the primary AssultSoldier vtable by the same RTTI reader used in production.
    void* soldierVtable=nullptr;
    for(std::uint32_t r=0x1755000;r<0x1f54000;r+=8) {
        void* candidate=g_image.base+r;
        if(edf6vr::HasType(g_image,&candidate,".?AVAssultSoldier@@")) { soldierVtable=candidate; break; }
    }
    VERIFY(soldierVtable!=nullptr);
    if(!soldierVtable) return 1;
    alignas(16) unsigned char camera[0xC00]{};
    *reinterpret_cast<void**>(camera)=g_image.base+edf6vr::kVtableRva;
    *reinterpret_cast<void**>(camera+edf6vr::kSourceOffset)=&soldierVtable;
    *reinterpret_cast<void**>(camera+edf6vr::kSoldierOffset)=&soldierVtable;
    const edf6vr::Matrix identity{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{10,20,30,1}}};
    auto matrix=reinterpret_cast<edf6vr::Matrix*>(camera+edf6vr::kCameraMatrixOffset);
    *matrix=identity;
    g_original=FakeGameUpdate; g_enabled=true; g_yaw=15; g_pitch=10;
    edf6vr::Matrix expected{}; edf6vr::RotateCamera(identity,15,10,expected);
    for(int i=0;i<1000;++i) HookUpdate(camera,nullptr);
    VERIFY(originalCalls==1000); VERIFY(g_applied==1000); VERIFY(!g_faulted);
    VERIFY(!std::memcmp(matrix,&expected,sizeof(expected)));
    VERIFY(!std::memcmp(matrix->m[3],identity.m[3],16));
    g_enabled=false; HookUpdate(camera,nullptr);
    VERIFY(!std::memcmp(matrix,&identity,sizeof(identity)));
    g_enabled=true;
    *reinterpret_cast<void**>(camera+edf6vr::kSourceOffset)=nullptr;
    HookUpdate(camera,nullptr); VERIFY(g_applied==1000); // Vehicle/missing source rejected.
    *reinterpret_cast<void**>(camera+edf6vr::kSourceOffset)=&soldierVtable;
    auto savedSoldierVtable=soldierVtable;
    soldierVtable=g_image.base+edf6vr::kVtableRva; // Wrong class.
    HookUpdate(camera,nullptr); VERIFY(g_applied==1000);
    soldierVtable=savedSoldierVtable;
    matrix->m[0][0]=2; HookUpdate(camera,nullptr); VERIFY(g_applied==1000); // Invalid basis.
    *matrix=identity;
    HookUpdate(camera,nullptr); VERIFY(g_applied==1001);
    g_enabled=false; HookUpdate(camera,nullptr);
    VERIFY(!std::memcmp(matrix,&identity,sizeof(identity)));
    // Unsupported build must fail before any slot write.
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(g_image.base+reinterpret_cast<IMAGE_DOS_HEADER*>(g_image.base)->e_lfanew);
    DWORD old=0; VERIFY(VirtualProtect(&nt->FileHeader.TimeDateStamp,4,PAGE_READWRITE,&old)!=FALSE);
    const DWORD savedTimestamp=nt->FileHeader.TimeDateStamp; nt->FileHeader.TimeDateStamp=0;
    edf6vr::ImageProfile rejected{};
    VERIFY(!edf6vr::CheckImage(mapped,rejected,reason,sizeof(reason)));
    nt->FileHeader.TimeDateStamp=savedTimestamp; DWORD unused=0;
    VERIFY(VirtualProtect(&nt->FileHeader.TimeDateStamp,4,old,&unused)!=FALSE);
    VERIFY(*g_image.updateSlot==g_image.original);
    // Real installed-image bytes, mapped as DATA only. The existing Moist-style
    // patch must remain idempotent, reversible and confined to the loop thread.
    int sleepNop=0,branchNop=0;
    g_frameLoopThread=GetCurrentThreadId();g_nextLimiterAttempt=0;g_removeFpsLimit=true;
    if(ReadLimiterState(sleepNop,branchNop)==2) VERIFY(SetLimiterRemoved(false));
    VERIFY(ReadLimiterState(sleepNop,branchNop)==1);
    const auto tick=GetTickCount64();
    const auto changes=g_limiterChanges;
    UpdateMissionFpsLimit(true,tick);VERIFY(ReadLimiterState(sleepNop,branchNop)==2);
    UpdateMissionFpsLimit(true,tick+1);VERIFY(g_limiterChanges==changes+1);
    bool wrongThreadAllowed=true;
    std::thread wrong([&] {wrongThreadAllowed=SetLimiterRemoved(false);});wrong.join();
    VERIFY(!wrongThreadAllowed && ReadLimiterState(sleepNop,branchNop)==2);
    UpdateMissionFpsLimit(false,tick+2102);VERIFY(ReadLimiterState(sleepNop,branchNop)==1);
    UpdateMissionFpsLimit(true,tick+3000);VERIFY(ReadLimiterState(sleepNop,branchNop)==2);
    VERIFY(SetLimiterRemoved(false));g_removeFpsLimit=false; // F5/manual opt-out persists in a live mission
    UpdateMissionFpsLimit(true,tick+3100);VERIFY(ReadLimiterState(sleepNop,branchNop)==1);
    auto* instruction=g_image.base+0x11A2F65;
    DWORD protection=0;VERIFY(VirtualProtect(instruction,1,PAGE_EXECUTE_READWRITE,&protection)!=FALSE);
    const unsigned char saved=*instruction;*instruction=0xCC;
    g_removeFpsLimit=true;UpdateMissionFpsLimit(true,tick+4000);
    VERIFY(ReadLimiterState(sleepNop,branchNop)==0 && *instruction==0xCC); // unknown code is never overwritten
    *instruction=saved;DWORD restoredProtection=0;VirtualProtect(instruction,1,protection,&restoredProtection);
    edf6vr::SetPresentUncapped(false);
    FreeLibrary(mapped);
    printf("Production hook / synthetic objects: %d failures, original called %u times. No game code executed.\n",failures,originalCalls);
    return failures?1:0;
}
