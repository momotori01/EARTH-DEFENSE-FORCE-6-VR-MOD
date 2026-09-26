// Included inside plugin.cpp's private namespace after shot_origin.h.
// Only the WeaponBase shell spawn CALL is redirected, never the shared manager.
constexpr unsigned kCasingSpawnCall=0x690B3F, kCasingSpawnManager=0x1194280;
constexpr std::size_t kCasingParamOffset=0x4F0, kCasingParamSize=0x110;
using CasingSpawn=void*(__fastcall*)(void*,const edf6vr::Matrix*,void*,void*);
CasingSpawn g_originalCasingSpawn=nullptr;
bool g_casingFollowsWeapon=true;
std::atomic<unsigned long long> g_casingCalls{0},g_casingApplied{0},g_casingRejected{0};
std::atomic<unsigned long long> g_casingCandidates{0};
std::atomic<unsigned> g_casingStage{0};

// Native nodes (+B0) remain native: DrawWeaponBorrow changes the distinct,
// contiguous renderer palette (registry+28 -> +8), and restores it afterwards.
// Read source and shell matrix in this same synchronous game-thread spawn;
// only the tracked destination crosses threads. No g_lock re-entry.
bool PrepareCasing(void* factory,void* parameter,const edf6vr::Matrix* source,
                   edf6vr::Matrix& wanted,unsigned char* paramCopy,ULONGLONG now) noexcept {
    if(!g_casingFollowsWeapon || g_weaponArmBone || g_nativeTactical) return false;
    CasingFrame frame{};
    AcquireSRWLockShared(&g_classLock);
    frame=parameter && g_leftCasingFrame.weapon && reinterpret_cast<std::uintptr_t>(parameter)==reinterpret_cast<std::uintptr_t>(g_leftCasingFrame.weapon)+kCasingParamOffset
        ?g_leftCasingFrame:g_casingFrame;
    ReleaseSRWLockShared(&g_classLock);
    if(!frame.weapon || !frame.soldier || !parameter
       || reinterpret_cast<std::uintptr_t>(parameter)!=reinterpret_cast<std::uintptr_t>(frame.weapon)+kCasingParamOffset
       || now<frame.time || now-frame.time>250) return false;
    ++g_casingCandidates;
    g_casingStage=1; // owner/parameter validation
    __try {
        auto weapon=static_cast<unsigned char*>(frame.weapon);
        auto soldier=static_cast<unsigned char*>(frame.soldier);
        if(!edf6vr::Readable(parameter,kCasingParamSize) || !edf6vr::Readable(source,64)
           || !edf6vr::Readable(weapon,edf6vr::kWeaponModelOffset+0xC8)
           || !edf6vr::Readable(soldier,0x318)
           || !edf6vr::IsSupportedSoldier(g_image,soldier) || FencerOwnsWeaponPose(soldier)
           || *reinterpret_cast<std::uint32_t*>(soldier+0x314)!=frame.objectId
           || *reinterpret_cast<void**>(weapon+0x120)!=soldier
           || *reinterpret_cast<void**>(weapon+0x4E0)!=factory
           || *reinterpret_cast<void**>(parameter)!=g_image.base+0x17E2598
           || !OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon),false)) return false;
        g_casingStage=2; // current native node array
        const auto registry=weapon+edf6vr::kWeaponModelOffset+0xA0;
        auto nodes=*reinterpret_cast<unsigned char**>(registry+0x10);
        const auto count=*reinterpret_cast<std::uint64_t*>(registry+0x20);
        if(!nodes || nodes!=frame.nodes || !count || count>64 || !edf6vr::Readable(nodes,0xF0)) return false;
        g_casingStage=3; // native/target geometry and teleport guards
        const edf6vr::Matrix nativeRoot=*reinterpret_cast<const edf6vr::Matrix*>(nodes+0xB0);
        float reach=0,travel=0,sourceReach=0;
        for(int j=0;j<3;++j) {
            const float root=*reinterpret_cast<float*>(soldier+0x90+j*4);
            const float delta=root-frame.root[j],local=frame.hand.palm[j]-frame.root[j];
            const float lever=source->m[3][j]-nativeRoot.m[3][j];
            if(!std::isfinite(delta) || !std::isfinite(local) || !std::isfinite(lever)) return false;
            travel+=delta*delta; reach+=local*local; sourceReach+=lever*lever;
            frame.hand.palm[j]=root+local;
        }
        if(travel>=400 || reach>=25 || sourceReach>=25) return false;
        float oldLever=0,newLever=0;
        if(!edf6vr::CarryWeaponBones(nativeRoot,frame.hand,source,1,&wanted,oldLever,newLever)) return false;
        g_casingStage=4; // velocity validation

        // Transform velocities with no translation or scale. Preserve inherited
        // world travel (6909C6), random native speeds, W lanes and all settings.
        auto param=static_cast<const unsigned char*>(parameter);
        edf6vr::Matrix velocity{};
        float inherited[3]{};
        for(int j=0;j<3;++j) {
            inherited[j]=*reinterpret_cast<const float*>(weapon+0x190+j*4);
            velocity.m[0][j]=*reinterpret_cast<const float*>(param+0x60+j*4)-inherited[j];
            velocity.m[1][j]=*reinterpret_cast<const float*>(param+0x70+j*4);
        }
        edf6vr::Matrix turned{};
        if(!edf6vr::CarryWeaponBones(nativeRoot,frame.hand,&velocity,1,&turned,oldLever,newLever)) return false;
        std::memcpy(paramCopy,parameter,kCasingParamSize);
        for(int j=0;j<3;++j) {
            const float linear=turned.m[0][j]+inherited[j];
            if(!std::isfinite(linear)) return false;
            std::memcpy(paramCopy+0x60+j*4,&linear,4);
            std::memcpy(paramCopy+0x70+j*4,&turned.m[1][j],4);
        }
        g_casingStage=5; // successfully prepared
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void* __fastcall HookCasingSpawn(void* manager,const edf6vr::Matrix* source,void* factory,void* parameter) {
    ++g_casingCalls;
    alignas(16) edf6vr::Matrix wanted{};
    alignas(16) unsigned char copy[kCasingParamSize]{};
    if(PrepareCasing(factory,parameter,source,wanted,copy,GetTickCount64())) {
        ++g_casingApplied;
        // 1194280 writes transient source/output pointers into InitParam then
        // synchronously constructs the shell. Neither stack copy escapes here;
        // the original call also supplies a stack matrix. No persistent writes.
        return g_originalCasingSpawn(manager,&wanted,factory,copy);
    }
    ++g_casingRejected;
    return g_originalCasingSpawn(manager,source,factory,parameter);
}

bool CheckCasingProfile() noexcept {
    constexpr unsigned char site[]={0x0F,0x11,0x8B,0x50,0x05,0,0,0x0F,0x11,0x83,0x60,0x05,0,0,
        0x4C,0x8B,0x83,0xE0,0x04,0,0,0x48,0x8D,0x54,0x24,0x40,
        0x48,0x8B,0x0D,0x19,0x1E,0xA2,0x01,0xE8,0x3C,0x37,0xB0,0};
    constexpr unsigned char param[]={0x48,0x8D,0x05,0x3B,0x7D,0x15,0x01,0x48,0x89,0x03};
    constexpr unsigned char manager[]={0x49,0x89,0x51,0x08,0x33,0xFF,0x49,0x89,0x79,0x10,
        0x49,0x89,0x79,0x18,0x48,0x8D,0x44,0x24,0x20,0x49,0x89,0x41,0x20};
    __try {
        return !std::memcmp(g_image.base+0x690B1E,site,sizeof(site))
            && !std::memcmp(g_image.base+0x68A856,param,sizeof(param))
            && !std::memcmp(g_image.base+0x119429A,manager,sizeof(manager));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool InstallCasingOrigin(bool& changed) noexcept {
    changed=false;
    if(!CheckCasingProfile()) return false;
    g_originalCasingSpawn=reinterpret_cast<CasingSpawn>(g_image.base+kCasingSpawnManager);
    return edf6vr::RedirectCall(g_image.base+kCasingSpawnCall,reinterpret_cast<void*>(g_originalCasingSpawn),
                              reinterpret_cast<void*>(&HookCasingSpawn),changed);
}
