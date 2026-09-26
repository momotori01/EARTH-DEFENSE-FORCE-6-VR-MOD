// Included inside plugin.cpp's private namespace. Independent snapshot lock;
// no callback takes g_lock or holds this lock while entering native game code.
struct MuzzleFrame {
    void* weapon=nullptr;
    void* soldier=nullptr;
    std::uint32_t objectId=0;
    float point[3]{},root[3]{};
    ULONGLONG time=0;
};
SRWLOCK g_classLock=SRWLOCK_INIT;
MuzzleFrame g_muzzleFrame{};
MuzzleFrame g_leftMuzzleFrame{};
// Destination only. Never publish a borrowed palette as a native source.
struct CasingFrame {
    void* weapon=nullptr;
    void* soldier=nullptr;
    void* nodes=nullptr;
    std::uint32_t objectId=0;
    edf6vr::WeaponHoldFrame hand{};
    float root[3]{};
    // The weapon's root as the DRAW used it -- palette matrix 0, not the
    // animation node beside it. The two are the same skeleton sampled at
    // different moments and drift apart by up to 40 cm, so carrying the
    // flash from the node while the weapon was carried from the palette put
    // the flash off the muzzle by an amount that grew with how far the
    // weapon had been turned away from its native pose.
    edf6vr::Matrix weaponRoot{};
    // And where that root ended up once the draw had carried it. The two
    // together are the exact rigid motion the weapon model was given, which
    // is the motion anything stuck to the weapon has to be given as well.
    edf6vr::Matrix weaponDrawn{};
    bool weaponRootValid=false,weaponDrawnValid=false;
    ULONGLONG time=0;
};
CasingFrame g_casingFrame{};
CasingFrame g_leftCasingFrame{};
void PublishCasingFrame(const WeaponHoldCommand& command,
                        const edf6vr::Matrix* weaponRoot=nullptr,
                        const edf6vr::Matrix* weaponDrawn=nullptr) noexcept {
    CasingFrame frame{};
    frame.weapon=command.weapon; frame.soldier=command.soldier;
    frame.nodes=command.weaponNodes; frame.objectId=command.objectId;
    frame.hand=command.hand; frame.time=command.refreshed;
    if(weaponRoot) { frame.weaponRoot=*weaponRoot; frame.weaponRootValid=true; }
    if(weaponDrawn) { frame.weaponDrawn=*weaponDrawn; frame.weaponDrawnValid=true; }
    for(int j=0;j<3;++j) frame.root[j]=command.rootWorld[j];
    AcquireSRWLockExclusive(&g_classLock);
    (command.handIndex==0?g_leftCasingFrame:g_casingFrame)=frame;
    ReleaseSRWLockExclusive(&g_classLock);
}
void* g_boosterOwner=nullptr;
std::uint32_t g_boosterOwnerId=0;
ULONGLONG g_boosterOwnerAt=0;
std::atomic<bool> g_nativeTactical{false};
// The furthest the Air Raider's camera got from him since the last report,
// and how far above him it was at that moment. The TACTICAL line only speaks
// when the answer CHANGES, so without this a targeting view that is wrongly
// refused says nothing at all and a test of it comes back empty-handed.
float g_tacticalPeak=0,g_tacticalPeakHeight=0;
std::atomic<unsigned long long> g_boosterDraws{0},g_boosterHidden{0},g_markerCalls{0},g_markerApplied{0};
std::atomic<unsigned long long> g_muzzleRebased{0},g_muzzleExpired{0};
using BoosterDraw=void(__fastcall*)(void*);
BoosterDraw g_originalBooster=nullptr;
using MarkerUpdate=void(__fastcall*)(void*,void*,const float*,const float*,bool,int);
MarkerUpdate g_originalMarker=nullptr;

void PublishMuzzleFrame(void* weapon,void* soldier,std::uint32_t id,const float* point,const float* root,ULONGLONG time,unsigned hand=1) noexcept {
    MuzzleFrame next{};
    if(weapon && soldier && point && root) {
        next.weapon=weapon; next.soldier=soldier; next.objectId=id; next.time=time;
        for(int j=0;j<3;++j) { next.point[j]=point[j]; next.root[j]=root[j]; }
    }
    AcquireSRWLockExclusive(&g_classLock); (hand==0?g_leftMuzzleFrame:g_muzzleFrame)=next; ReleaseSRWLockExclusive(&g_classLock);
}
void ClearClassPresentation() noexcept {
    g_nativeStereoProbeActive.store(false,std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_classLock);
    g_muzzleFrame={}; g_casingFrame={}; g_leftMuzzleFrame={};g_leftCasingFrame={}; g_boosterOwner=nullptr; g_boosterOwnerAt=0;
    ReleaseSRWLockExclusive(&g_classLock);
}
bool ResolveTrackedMuzzle(void* weapon,float* point,ULONGLONG now) noexcept {
    MuzzleFrame frame{};
    AcquireSRWLockShared(&g_classLock); frame=weapon==g_leftMuzzleFrame.weapon?g_leftMuzzleFrame:g_muzzleFrame; ReleaseSRWLockShared(&g_classLock);
    __try {
        if(!weapon || weapon!=frame.weapon || !frame.soldier || now<frame.time || now-frame.time>250) {
            ++g_muzzleExpired; return false;
        }
        auto bytes=static_cast<unsigned char*>(frame.soldier);
        if(!edf6vr::Readable(bytes,0x318) || !edf6vr::IsSupportedSoldier(g_image,bytes)
           || *reinterpret_cast<const std::uint32_t*>(bytes+0x314)!=frame.objectId) return false;
        const auto root=reinterpret_cast<const float*>(bytes+0x90);
        float lever=0,travel=0;
        for(int j=0;j<3;++j) {
            const float local=frame.point[j]-frame.root[j],delta=root[j]-frame.root[j];
            if(!std::isfinite(local) || !std::isfinite(delta)) return false;
            lever+=local*local; travel+=delta*delta;
            point[j]=root[j]+local;
        }
        // Owner-local reach guards persist at high speed. Reject stale teleports.
        if(lever>=25 || travel>=400) return false;
        ++g_muzzleRebased; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void PublishBoosterOwner(void* soldier,std::uint32_t id,ULONGLONG now) noexcept {
    AcquireSRWLockExclusive(&g_classLock);
    g_boosterOwner=soldier; g_boosterOwnerId=id; g_boosterOwnerAt=now;
    ReleaseSRWLockExclusive(&g_classLock);
}
bool HideOwnBooster(void* effect,ULONGLONG now) noexcept {
    void* owner=nullptr; std::uint32_t id=0; ULONGLONG time=0;
    AcquireSRWLockShared(&g_classLock);
    owner=g_boosterOwner; id=g_boosterOwnerId; time=g_boosterOwnerAt;
    ReleaseSRWLockShared(&g_classLock);
    __try {
        return owner && now>=time && now-time<250 && edf6vr::Readable(effect,0x3D8)
            && edf6vr::HasType(g_image,effect,".?AVBooster@@")
            && *reinterpret_cast<void**>(static_cast<unsigned char*>(effect)+0x3D0)==owner
            && edf6vr::Readable(owner,0x318) && edf6vr::HasType(g_image,owner,".?AVPaleWing@@")
            && *reinterpret_cast<const std::uint32_t*>(static_cast<unsigned char*>(owner)+0x314)==id;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall HookBoosterDraw(void* effect) {
    ++g_boosterDraws;
    if(HideOwnBooster(effect,GetTickCount64())) { ++g_boosterHidden; return; }
    g_originalBooster(effect);
}
// start/end are caller-local values, not weapon or skeleton storage.
bool RebaseMarkerRay(void* common,const float* start,const float* end,float* movedStart,float* movedEnd) noexcept {
    __try {
        if(g_nativeTactical || !edf6vr::HasType(g_image,common,".?AVLaserMarkerCommon@@")
           || !edf6vr::Readable(start,16) || !edf6vr::Readable(end,16)) return false;
        void* weapon=nullptr;
        AcquireSRWLockShared(&g_classLock); weapon=g_muzzleFrame.weapon; ReleaseSRWLockShared(&g_classLock);
        if(!weapon) return false;
        auto bytes=static_cast<unsigned char*>(weapon);
        // Ordinary markers embed Common at +1570; Drone_LaserMarker owns a
        // separate Common through a pointer there (68424B: MOV, not LEA).
        // Start from our published weapon, never guess an owner from heap proximity.
        if(reinterpret_cast<std::uintptr_t>(common)!=reinterpret_cast<std::uintptr_t>(weapon)+0x1570) {
            if(!edf6vr::HasType(g_image,weapon,".?AVWeapon_Drone_LaserMarker@@")
               || !edf6vr::Readable(bytes,0x1578)
               || *reinterpret_cast<void**>(bytes+0x1570)!=common) return false;
        }
        float point[3]{};
        if(!ResolveTrackedMuzzle(weapon,point,GetTickCount64())) return false;
        float distance=0;
        for(int j=0;j<3;++j) {
            if(!std::isfinite(start[j]) || !std::isfinite(end[j])) return false;
            const float delta=point[j]-start[j]; distance+=delta*delta;
            movedStart[j]=point[j]; movedEnd[j]=end[j]+delta;
        }
        movedStart[3]=start[3]; movedEnd[3]=end[3];
        return std::isfinite(distance) && distance<25;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall HookMarkerUpdate(void* common,void* context,const float* start,const float* end,bool active,int mode) {
    ++g_markerCalls;
    alignas(16) float a[4]{},b[4]{};
    if(RebaseMarkerRay(common,start,end,a,b)) {
        ++g_markerApplied; g_originalMarker(common,context,a,b,active,mode);
    } else g_originalMarker(common,context,start,end,active,mode);
}
bool InstallClassEffects() noexcept {
    // PaleWing ctor57D8B2 -> factory351480 -> Booster ctor2CB810.
    // Param+30 is the soldier, copied to effect+3D0. This draw submits both
    // the nozzle and its child glow (+3F8). Leave update slot+28 untouched.
    constexpr unsigned char booster[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,
        0x48,0x8D,0x51,0x60,0x48,0x81,0xC1,0x20,0x01,0,0,0xE8,0x87,0x7B,0x02,0};
    __try {
        if(std::memcmp(g_image.base+0x2CBDF0,booster,sizeof(booster))) return false;
        constexpr unsigned char droneCommon[]={0x48,0x8B,0x8B,0x70,0x15,0,0};
        if(std::memcmp(g_image.base+0x68424B,droneCommon,sizeof(droneCommon))) return false;
        void* table=g_image.base+0x17A6D58;
        if(!edf6vr::HasType(g_image,&table,".?AVBooster@@")
           || *reinterpret_cast<void**>(g_image.base+0x17A6D70)!=g_image.base+0x2CBDF0) return false;
        for(const auto site:{0x6A3D0Eu,0x6A3DFBu,0x684271u}) {
            if(g_image.base[site]!=0xE8 || g_image.base+site+5+*reinterpret_cast<const std::int32_t*>(g_image.base+site+1)!=g_image.base+0x6A64A0) return false;
        }
        g_originalBooster=reinterpret_cast<BoosterDraw>(g_image.base+0x2CBDF0);
        g_originalMarker=reinterpret_cast<MarkerUpdate>(g_image.base+0x6A64A0);
        bool changed=false;
        if(!edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+0x17A6D70),reinterpret_cast<void*>(g_originalBooster),reinterpret_cast<void*>(&HookBoosterDraw),changed)) return false;
        for(const auto site:{0x6A3D0Eu,0x6A3DFBu,0x684271u})
            if(!edf6vr::RedirectCall(g_image.base+site,reinterpret_cast<void*>(g_originalMarker),reinterpret_cast<void*>(&HookMarkerUpdate),changed)) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Native camera already moves tens of metres away for area targeting (measured
// in 0.82). This is a geometric fallback, not a decoded weapon/state enum.
// Hysteresis prevents toggling during its transition. Other classes are untouched.
//
// Distance alone is not enough. An ant that bites the Air Raider carries him
// off and throws him, and the camera trails far enough behind to read as the
// targeting view -- it flipped to the panel and back five times in one grab on
// hardware. Two things separate the real thing from being eaten: the player's
// body model is there to be read, and aerial targeting looks DOWN at the
// ground from above, while a camera trailing a thrown soldier sits level with
// him or below. distanceOut/heightOut are for the log, so the next run says how
// far each of these was from the line rather than only which side it fell.
bool IsNativeTacticalView(void* soldier,const edf6vr::Matrix& camera,bool previous,
                          float* distanceOut=nullptr,float* heightOut=nullptr) noexcept {
    if(distanceOut) *distanceOut=0;
    if(heightOut) *heightOut=0;
    __try {
        if(!edf6vr::HasType(g_image,soldier,".?AVEngineer@@") || !edf6vr::Readable(soldier,0xA0)
           || !edf6vr::ValidCamera(camera)) return false;
        const auto root=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+0x90);
        float square=0;
        for(int j=0;j<3;++j) { const float d=camera.m[3][j]-root[j]; square+=d*d; }
        if(!std::isfinite(square)) return false;
        const float height=camera.m[3][1]-root[1];
        if(distanceOut) *distanceOut=std::sqrt(square);
        if(heightOut) *heightOut=height;
        // Held by an ant the body model goes away and the pose cannot be read.
        // Keep whatever the view was; do not decide anything from a soldier
        // that is not there to measure against.
        auto body=static_cast<unsigned char*>(soldier)+edf6vr::kBodyModelOffset;
        if(!edf6vr::HasType(g_image,body,".?AVAnimationModel@@")) return previous;
        if(!std::isfinite(height)) return false;
        // Leaving stays on distance alone, so a genuine targeting view that
        // drops low on its way out still ends.
        if(previous) return square>36.0f;
        return square>100.0f && height>2.0f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
