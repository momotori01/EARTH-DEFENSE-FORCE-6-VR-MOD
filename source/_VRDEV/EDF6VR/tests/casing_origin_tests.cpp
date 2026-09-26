#include "../src/plugin.cpp"
#include <limits>
#include <thread>
static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
template<class T> static T& Field(void* p,std::size_t offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset); }
static bool Near(float a,float b) { return std::fabs(a-b)<0.0001f; }
static unsigned calls=0;
static void* receivedParam=nullptr;
static const edf6vr::Matrix* receivedMatrix=nullptr;
static edf6vr::Matrix seen{};
static unsigned char seenParam[kCasingParamSize]{};
static void* __fastcall SpawnStub(void* manager,const edf6vr::Matrix* matrix,void* factory,void* param) {
    ++calls; receivedParam=param; receivedMatrix=matrix; seen=*matrix;
    CHECK(manager==reinterpret_cast<void*>(0x1234) && factory==reinterpret_cast<void*>(0x5678));
    CHECK((reinterpret_cast<std::uintptr_t>(matrix)&15)==0);
    std::memcpy(seenParam,param,sizeof(seenParam));
    // Actual native manager writes transient pointers before synchronous creation.
    Field<const edf6vr::Matrix*>(param,8)=matrix;
    Field<void*>(param,0x20)=reinterpret_cast<void*>(0xABCD);
    return reinterpret_cast<void*>(0xBEEF);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[256]{};
    if(!mapped || !edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason))) return 2;
    CHECK(CheckCasingProfile());
    alignas(16) unsigned char camera[0xC00]{},soldier[0x2100]{},entries[0x150]{},weapon[0x1600]{},transform[0x100]{},nodes[0x110]{};
    void* wrapper=weapon;
    Field<void*>(soldier,0)=g_image.base+0x17CDF28;
    Field<unsigned>(soldier,0x314)=123;
    Field<void*>(camera,edf6vr::kSourceOffset)=soldier;
    Field<void*>(camera,edf6vr::kSoldierOffset)=soldier;
    Field<void*>(soldier,0x1970)=entries; Field<unsigned>(soldier,0x1980)=1;
    Field<void*>(entries,0x40)=&wrapper;
    Field<void*>(weapon,0x120)=soldier;
    Field<void*>(weapon,0x1D0)=transform;
    Field<void*>(weapon,0x4E0)=reinterpret_cast<void*>(0x5678);
    Field<void*>(weapon,0x4F0)=g_image.base+0x17E2598;
    Field<void*>(weapon,edf6vr::kWeaponModelOffset+0xB0)=nodes;
    Field<std::uint64_t>(weapon,edf6vr::kWeaponModelOffset+0xC0)=1;
    Field<float>(weapon,0x358)=1;
    const edf6vr::Matrix root={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{10,20,30,1}}};
    Field<edf6vr::Matrix>(soldier,0x60)=root;
    Field<edf6vr::Matrix>(nodes,0xB0)=root;
    // Muzzle is already moved elsewhere. It must NOT be used as a native root.
    Field<edf6vr::Matrix>(transform,0x50)=root;
    Field<float>(transform,0x80)=100;
    g_lastCamera=camera;
    g_originalCasingSpawn=&SpawnStub;
    WeaponHoldCommand command{};
    command.weapon=weapon; command.soldier=soldier; command.objectId=123; command.weaponNodes=nodes;
    command.hand={{{0,1,0},{-1,0,0},{0,0,1}},{11,21,30}}; // roll +90deg
    for(int j=0;j<3;++j) command.rootWorld[j]=root.m[3][j];
    command.refreshed=GetTickCount64();
    std::thread publisher([&] { PublishCasingFrame(command); }); publisher.join();
    alignas(16) edf6vr::Matrix source=root;
    source.m[3][0]+=.2f; source.m[3][1]+=.1f; source.m[3][2]+=.3f;
    // Relative eject velocity (2,0,0) plus inherited world velocity (5,6,7).
    Field<float>(weapon,0x190)=5; Field<float>(weapon,0x194)=6; Field<float>(weapon,0x198)=7;
    Field<float>(weapon,0x550)=7; Field<float>(weapon,0x554)=6; Field<float>(weapon,0x558)=7;
    Field<float>(weapon,0x55C)=1; Field<float>(weapon,0x560)=3; Field<float>(weapon,0x56C)=1;
    Field<unsigned>(weapon,0x5F0)=60;
    unsigned char native[kCasingParamSize]{}; std::memcpy(native,weapon+0x4F0,sizeof(native));
    auto spawn=[&] {
        const unsigned before=calls;
        auto ret=HookCasingSpawn(reinterpret_cast<void*>(0x1234),&source,reinterpret_cast<void*>(0x5678),weapon+0x4F0);
        CHECK(ret==reinterpret_cast<void*>(0xBEEF) && calls==before+1);
    };
    // This is the real callback nesting: game HookUpdate already owns g_lock.
    AcquireSRWLockExclusive(&g_lock); spawn(); ReleaseSRWLockExclusive(&g_lock);
    CHECK(receivedParam!=weapon+0x4F0 && receivedMatrix!=&source);
    CHECK(Near(seen.m[3][0],10.9f) && Near(seen.m[3][1],21.2f) && Near(seen.m[3][2],30.3f));
    CHECK(Near(seen.m[0][1],1) && Near(seen.m[1][0],-1) && seen.m[3][3]==1);
    CHECK(Near(Field<float>(seenParam,0x60),5) && Near(Field<float>(seenParam,0x64),8) && Near(Field<float>(seenParam,0x68),7));
    CHECK(Near(Field<float>(seenParam,0x70),0) && Near(Field<float>(seenParam,0x74),3));
    CHECK(Field<float>(seenParam,0x6C)==1 && Field<float>(seenParam,0x7C)==1);
    CHECK(!std::memcmp(native,weapon+0x4F0,sizeof(native)));
    CHECK(!std::memcmp(nodes+0xB0,&root,64)); // no borrowed/native node writes
    CHECK(!std::memcmp(seenParam,native,0x60) && !std::memcmp(seenParam+0x80,native+0x80,kCasingParamSize-0x80));
    // High-speed travel rebases destination exactly once, preserves original port.
    Field<float>(soldier,0x94)+=4; Field<float>(nodes,0xE4)+=4; source.m[3][1]+=4;
    spawn(); CHECK(Near(seen.m[3][1],25.2f));
    // Repeated spawn cannot feed transformed points back into the source.
    for(int i=0;i<500;++i) { spawn(); CHECK(Near(seen.m[3][1],25.2f)); }
    Field<float>(soldier,0x94)-=4; Field<float>(nodes,0xE4)-=4; source.m[3][1]-=4;

    for(int trial=0;trial<10;++trial) {
        command.refreshed=GetTickCount64(); PublishCasingFrame(command);
        g_casingFollowsWeapon=true; g_nativeTactical=false; g_weaponArmBone=false;
        Field<void*>(soldier,0)=g_image.base+0x17CDF28;
        Field<unsigned>(soldier,0x314)=123; Field<void*>(weapon,0x120)=soldier;
        Field<void*>(weapon,edf6vr::kWeaponModelOffset+0xB0)=nodes;
        Field<edf6vr::Matrix>(nodes,0xB0)=root;
        switch(trial) {
        case 0: g_casingFollowsWeapon=false; break;
        case 1: ClearClassPresentation(); break;
        case 2: command.weapon=nullptr; PublishCasingFrame(command); command.weapon=weapon; break;
        case 3: Field<unsigned>(soldier,0x314)=124; break;
        case 4: Field<void*>(weapon,0x120)=nullptr; break;
        case 5: Field<void*>(soldier,0)=g_image.base+0x17CF5B8; break; // Fencer native
        case 6: g_nativeTactical=true; break;
        case 7: g_weaponArmBone=true; break;
        case 8: Field<void*>(weapon,edf6vr::kWeaponModelOffset+0xB0)=nullptr; break;
        case 9: Field<float>(nodes,0xB0)=std::numeric_limits<float>::quiet_NaN(); break;
        }
        spawn(); CHECK(receivedParam==weapon+0x4F0 && receivedMatrix==&source);
        CHECK(!std::memcmp(&seen,&source,64));
        std::memcpy(weapon+0x4F0,native,sizeof(native)); // stub's native writes
    }
    g_casingFollowsWeapon=true;g_nativeTactical=false;g_weaponArmBone=false;
    Field<edf6vr::Matrix>(nodes,0xB0)=root;
    command.refreshed=1000;PublishCasingFrame(command);
    alignas(16) edf6vr::Matrix moved{};
    alignas(16) unsigned char copied[kCasingParamSize]{};
    CHECK(!PrepareCasing(reinterpret_cast<void*>(0x5678),weapon+0x4F0,&source,moved,copied,1251));
    CHECK(!PrepareCasing(reinterpret_cast<void*>(0x5678),weapon+0x4F0,&source,moved,copied,999));
    CHECK(PrepareCasing(reinterpret_cast<void*>(0x5678),weapon+0x4F0,&source,moved,copied,1100));
    // Profile rejection must not alter the CALL (all game code remains unrun).
    DWORD protection=0; VirtualProtect(g_image.base+0x68A856,1,PAGE_EXECUTE_READWRITE,&protection);
    const auto byte=g_image.base[0x68A856];g_image.base[0x68A856]=0xCC;
    bool changed=false; CHECK(!InstallCasingOrigin(changed) && !changed);
    g_image.base[0x68A856]=byte;DWORD ignored=0;VirtualProtect(g_image.base+0x68A856,1,protection,&ignored);
    CHECK(CheckCasingProfile());
    CHECK(InstallCasingOrigin(changed) && changed);
    CHECK(!CheckCasingProfile()); // cannot patch a changed call twice
    ClearClassPresentation(); FreeLibrary(mapped);
    printf("Casing spawn: %d failures; current-image ABI/profile, rigid port, travel, velocity, ownership, no feedback\n",failures);
    return failures?1:0;
}
