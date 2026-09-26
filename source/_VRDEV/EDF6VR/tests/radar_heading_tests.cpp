#include "radar_heading.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
namespace {
float __cdecl TestAtan2(float y,float x) { return std::atan2(y,x); }
float __cdecl TestSqrt(float x) { return std::sqrt(x); }
bool BindMathImport(unsigned char* stub,void* function) {
    if(stub[0]!=0xFF || stub[1]!=0x25) return false;
    std::int32_t displacement=0; std::memcpy(&displacement,stub+2,4);
    auto* slot=stub+6+displacement; DWORD old=0,ignored=0;
    if(!VirtualProtect(slot,8,PAGE_READWRITE,&old)) return false;
    std::memcpy(slot,&function,8);
    return VirtualProtect(slot,8,old,&ignored)!=0;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    int failures=0;
    auto check=[&](bool ok,const char* label) { if(!ok) { ++failures; std::printf("FAIL %s\n",label); } };
    int local=0,other=0;
    float angles[]={-1.57f,0.2f,0,1};
    edf6vr::ClearRadarHeading();
    check(!edf6vr::ApplyRadarHeading(angles,&local,1000),"OFF passes through");
    edf6vr::PublishRadarHeading(&local,1.2f,1000);
    check(!edf6vr::ApplyRadarHeading(angles,&other,1001) && angles[1]==0.2f,"other soldier unchanged");
    check(edf6vr::ApplyRadarHeading(angles,&local,1001) && angles[0]==0 && angles[1]==1.2f && angles[3]==1,
          "head yaw overrides body and avoids pole fallback");
    check(!edf6vr::ApplyRadarHeading(angles,&local,1250),"stale tracking excluded");
    check(!edf6vr::ApplyRadarHeading(angles,&local,999),"future timestamp excluded");
    edf6vr::PublishRadarHeading(&local,9.0f,1300);
    check(edf6vr::ApplyRadarHeading(angles,&local,1301) && std::abs(angles[1]-2.7168147f)<0.00001f,"wrapped stick plus head yaw");
    edf6vr::PublishRadarHeading(&local,std::numeric_limits<float>::quiet_NaN(),1400);
    check(!edf6vr::ApplyRadarHeading(angles,&local,1401),"invalid pose excluded");
    edf6vr::PublishRadarHeading(&local,-0.7f,1500); edf6vr::ClearRadarHeading();
    check(!edf6vr::ApplyRadarHeading(angles,&local,1501),"stop clears immediately");
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    edf6vr::ImageProfile image{}; char reason[256]{};
    if(!module || !edf6vr::CheckImage(module,image,reason,sizeof(reason))) return 2;
    unsigned char fallback[5]{}; std::memcpy(fallback,image.base+0x82B743,5);
    bool changed=false;
    check(edf6vr::InstallRadarHeading(image,changed) && changed,"actual image signature and redirect");
    check(!std::memcmp(fallback,image.base+0x82B743,5),"unrelated fallback call unchanged");
    check(!edf6vr::InstallRadarHeading(image,changed) && !changed,"refuse conflicting call");
    // Inspect and execute the actual bridge. Bind only the native leaf math
    // helper's two imports; never run EDF.dll entry point or the radar renderer.
    std::int32_t delta=0; std::memcpy(&delta,image.base+edf6vr::kRadarAngleCall+1,4);
    auto* nearThunk=image.base+edf6vr::kRadarAngleCall+5+delta;
    unsigned char* bridge=nullptr; std::memcpy(&bridge,nearThunk+2,8);
    const unsigned char prefix[]={0x49,0x89,0xF0,0x48,0xB8};
    check(nearThunk[0]==0x48 && nearThunk[1]==0xB8 && nearThunk[10]==0xFF && nearThunk[11]==0xE0,"near absolute tail jump");
    check(bridge && !std::memcmp(bridge,prefix,5) && bridge[13]==0xFF && bridge[14]==0xE0,"owner bridge ABI");
    const bool mathBound=BindMathImport(image.base+0x12DA90C,reinterpret_cast<void*>(&TestAtan2))
        && BindMathImport(image.base+0x12DA8BE,reinterpret_cast<void*>(&TestSqrt));
    check(mathBound,"native leaf math imports");
    // Test caller: preserve RSI, reserve shadow space, set RSI from argument 3,
    // and call the installed near thunk exactly as the native radar call does.
    unsigned char caller[]={0x56,0x48,0x83,0xEC,0x20,0x4C,0x89,0xC6,0x48,0xB8,
        0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x20,0x5E,0xC3};
    std::memcpy(caller+10,&nearThunk,8);
    void* executable=VirtualAlloc(nullptr,sizeof(caller),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(executable!=nullptr,"test caller allocation");
    if(executable && mathBound) {
        std::memcpy(executable,caller,sizeof(caller)); DWORD old=0;
        const bool ready=VirtualProtect(executable,sizeof(caller),PAGE_EXECUTE_READ,&old)!=0;
        check(ready,"test caller RX");
        FlushInstructionCache(GetCurrentProcess(),executable,sizeof(caller));
        if(ready) {
            using Call=float* (__fastcall*)(float*,const float*,const void*);
            const auto call=reinterpret_cast<Call>(executable);
            const float direction[]={0.5f,0.9f,1,0};
            edf6vr::ClearRadarHeading();
            check(call(angles,direction,&local)==angles && std::abs(angles[1]-std::atan2(0.5f,1.0f))<0.00001f,
                  "actual thunk OFF returns native angles and pointer");
            edf6vr::PublishRadarHeading(&local,-1.2f,GetTickCount64());
            check(call(angles,direction,&local)==angles && angles[0]==0 && angles[1]==-1.2f,
                  "actual RSI owner and yaw replacement");
            check(call(angles,direction,&other)==angles && angles[0]!=0 && std::abs(angles[1]-std::atan2(0.5f,1.0f))<0.00001f,
                  "actual thunk preserves other owner");
        }
    }
    if(executable) VirtualFree(executable,0,MEM_RELEASE);
    FreeLibrary(module);
    std::printf("Radar heading: %d failures\n",failures);
    return failures?1:0;
}
