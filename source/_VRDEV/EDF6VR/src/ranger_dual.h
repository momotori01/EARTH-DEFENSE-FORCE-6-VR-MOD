// Included in plugin.cpp's private namespace. The snapshot lock is never held
// across native calls, OpenXR calls, or the draw borrow. No stored game pointer
// is written during cancellation: tick-local overrides are restored in finally.
struct RangerDualState {
    void* soldier=nullptr; std::uint32_t id=0;
    void* weapons[2]{}; // 0 left, 1 right; NEVER native HUD slot numbers
    WeaponHoldCommand hands[2]{};
    unsigned serial=0;
    unsigned char leftVisible=0;
};
SRWLOCK g_dualLock=SRWLOCK_INIT;
RangerDualState g_dualState{};
RangerDualState g_dualCleanup{}; // consumed by a live weapon tick, never dereferenced by reset
std::atomic<void*> g_dualWeapons[2]{},g_dualCleanupWeapon{nullptr};
unsigned g_dualHandledSerial=0;
using DualIndex=int(__fastcall*)(void*,int);
using DualTick=void(__fastcall*)(void*,void*);
using DualPose=void(__fastcall*)(void*,void*);
using DualZoom=void(__fastcall*)(void*);
DualIndex g_dualIndex=nullptr;
DualPose g_dualPose=nullptr;
DualZoom g_dualZoom=nullptr;
DualZoom g_dualHolster=nullptr;
using DualVisibility=void(__fastcall*)(void*,bool);
using DualFire=void(__fastcall*)(void*,unsigned,void*,void*,bool);
using DualSpread=void*(__fastcall*)(void*,const float*,float,float,std::uint64_t*);
DualVisibility g_dualVisibility=nullptr;
DualFire g_dualFire=nullptr;
DualSpread g_dualSpread=nullptr;
using DualSwitchSound=void(__fastcall*)(void*,void*,const wchar_t*,void*);
DualSwitchSound g_dualSwitchSound=nullptr;
constexpr unsigned kDualFireCalls[]={0x6904F7,0x690603,0x690D54,0x692A24,0x694894};
struct RangerRecoilOwner {void* weapon=nullptr;unsigned serial=0;edf6vr::RangerRecoil recoil{};};
// Only the native local weapon thread mutates recoil; camera logs read atomics.
RangerRecoilOwner g_dualRecoil[2]{};
std::atomic<unsigned long long> g_dualFires[2]{};
std::atomic<float> g_dualRecoilLevel[2]{};
std::atomic<unsigned long long> g_dualInitializers[2]{};
std::atomic<float> g_dualInitPosition[2][3]{},g_dualFireDirection[2][3]{};
// Degrees between a shot and the hand that fired it, last and worst.
std::atomic<float> g_dualAimOff[2]{},g_dualAimOffMax[2]{};
// Left-gun bullets turned back onto the hand they left, and by how much.
std::atomic<unsigned long long> g_dualAimTurns{0},g_dualAimMismatch{0};
std::atomic<float> g_dualAimTurnDeg{0},g_dualAimSelfDeg{0};
// What one left-gun discharge needs, worked out while the tick still has the
// weapon in both states. `nominal` is the direction the shot was aimed along
// before the accuracy cone was added to it; `fireFrom` is that direction as
// the carry left it and `fireTo` where it belongs, and shot_origin.h turns the
// bullet from one to the other.
struct DualShotContext {bool active=false,counted=false;unsigned hand=0;float recoil=0;bool fencer=false;
                        float nominal[3]{};bool haveNominal=false;
                        float fireFrom[3]{},fireTo[3]{};bool haveFireTurn=false;};
// fencer_dual.h, which shares these hooks.
bool FencerWeaponActive(void* weapon) noexcept;
void FencerWeaponTick(void* weapon,void* context,DualTick original) noexcept;
void FencerNoteShot(unsigned hand) noexcept;
void FencerAimForward(float pitchGame,float yaw,float out[3]) noexcept;
bool FencerTurnRows(float rows[3][4],const float from[3],const float to[3]) noexcept;
// guide_probe.h: the soldier's own aim as a world direction.
bool GuideAimForward(void* soldier,float* out) noexcept;
thread_local DualShotContext g_dualShot{};
void LateLatchHand(WeaponHoldCommand&) noexcept;
bool ValidateHoldCommand(const WeaponHoldCommand&,void*) noexcept;
struct DualWeaponProfile { unsigned table,tick; const char* name; };
constexpr DualWeaponProfile kDualWeapons[]={
#include "ranger_weapon_profiles.inc"
};
DualTick g_dualOriginals[sizeof(kDualWeapons)/sizeof(kDualWeapons[0])]{};
std::atomic<unsigned long long> g_dualTicks[2]{},g_dualPoses[2]{},g_dualDraws[2]{},g_dualFailures{0};
// Left-gun discharges held back because no tracked pose was ready for them
// (the native one is the gun hanging on the back, pointing up), and hand
// commands that described the other gun.
std::atomic<unsigned long long> g_dualStaleCommand{0},g_dualHeldBack{0};
struct DualTickContext { WeaponHoldCommand command{}; void* weapon=nullptr; bool active=false; };
// The Ranger's reload pause (E6E) belongs to the Ranger's dual wield only; a
// Fencer weapon in this context reloads as the game has it.
thread_local DualTickContext g_dualTick{};

template<class T> T& DualAt(void* p,std::size_t offset) noexcept {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset);
}
bool CurrentRangerPair(const RangerDualState& state,void* soldier) noexcept {
    __try {
        if(!soldier || soldier!=state.soldier || !edf6vr::Readable(soldier,0x1990)
           || !edf6vr::HasType(g_image,soldier,".?AVAssultSoldier@@")
           || DualAt<unsigned>(soldier,0x314)!=state.id || edf6vr::HasMountReference(soldier)
           || edf6vr::WeaponSlotCount(soldier)!=1) return false;
        auto slot=DualAt<unsigned char*>(soldier,0x1970);
        auto wrapper=DualAt<void*>(slot,0x40);
        if(!edf6vr::Readable(wrapper,8) || *static_cast<void**>(wrapper)!=state.weapons[1]) return false;
        bool found[2]{};
        for(unsigned i=0;i<edf6vr::OwnedWeaponCount(soldier);++i)
            for(unsigned h=0;h<2;++h) if(edf6vr::OwnedWeaponAt(soldier,i)==state.weapons[h]) found[h]=true;
        for(unsigned h=0;h<2;++h) {
            if(!found[h] || !edf6vr::Readable(state.weapons[h],0x1530,true)
               || DualAt<void*>(state.weapons[h],0x120)!=soldier) return false;
        }
        return state.weapons[0]!=state.weapons[1];
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
RangerDualState ReadRangerDual() noexcept {
    RangerDualState state{};
    AcquireSRWLockShared(&g_dualLock); state=g_dualState; ReleaseSRWLockShared(&g_dualLock);
    return state;
}
void ResetRangerDual() noexcept {
    const bool was=g_dualActive.exchange(false);
    AcquireSRWLockExclusive(&g_dualLock);
    if(was) {g_dualCleanup=g_dualState;g_dualCleanupWeapon.store(g_dualState.weapons[0]);}
    g_dualState={}; ReleaseSRWLockExclusive(&g_dualLock);
    g_dualWeapons[0]=nullptr;g_dualWeapons[1]=nullptr;
    g_dualRecoilLevel[0]=0;g_dualRecoilLevel[1]=0;
    g_dualAimOff[0]=0;g_dualAimOff[1]=0;g_dualAimOffMax[0]=0;g_dualAimOffMax[1]=0;
    g_dualAimTurnDeg=0;
    // Only when the pair was out: this runs every update for any soldier that
    // is not dual-eligible (the Fencer), and the left frames then belong to
    // fencer_dual.h's left hand.
    if(was) {
        AcquireSRWLockExclusive(&g_classLock);g_leftMuzzleFrame={};g_leftCasingFrame={};ReleaseSRWLockExclusive(&g_classLock);
        Log("RANGERDUAL off; native equipped weapon reload resumes");
    }
}
bool RangerDualWeapon(void* weapon) noexcept {
    if(!g_dualActive.load()) return false;
    const auto state=ReadRangerDual();
    return (weapon==state.weapons[0] || weapon==state.weapons[1]) && CurrentRangerPair(state,state.soldier);
}
bool RangerMayFire(void* soldier) noexcept {
    // Same primary-fire gates as 59ADD6..59ADE7. Stun/death/weapon-switch
    // animation must not be bypassed by the independent left trigger.
    return !(DualAt<unsigned>(soldier,0x5D0)&0x84) && DualAt<int>(soldier,0x198C)<=0;
}
bool SelectRangerPair(void* soldier,RangerDualState& state) noexcept {
    __try {
        if(!g_dualIndex || !edf6vr::HasType(g_image,soldier,".?AVAssultSoldier@@")
           || edf6vr::WeaponSlotCount(soldier)!=1) return false;
        auto slot=DualAt<void*>(soldier,0x1970);
        const int selected=DualAt<int>(slot,0x38);
        if(selected<0 || selected>1 || DualAt<unsigned short>(slot,0x18)==0xffff) return false;
        // 590C99 -> 5A2220 resolves this slot's loadout index into owned weapons.
        // This excludes Ranger's backpack/support slot, even if it has a model.
        const int right=g_dualIndex(slot,selected),left=g_dualIndex(slot,1-selected);
        const auto count=edf6vr::OwnedWeaponCount(soldier);
        if(right<0 || left<0 || unsigned(right)>=count || unsigned(left)>=count || right==left) return false;
        state={};state.soldier=soldier;state.id=DualAt<unsigned>(soldier,0x314);
        state.weapons[0]=edf6vr::OwnedWeaponAt(soldier,unsigned(left));
        state.weapons[1]=edf6vr::OwnedWeaponAt(soldier,unsigned(right));
        if(!CurrentRangerPair(state,soldier)) return false;
        state.leftVisible=DualAt<unsigned char>(state.weapons[0],0x78C);
        for(auto weapon:state.weapons) {
            bool known=false;
            for(const auto& p:kDualWeapons) if(*static_cast<void**>(weapon)==g_image.base+p.table) known=true;
            edf6vr::WeaponPose pose{};
            if(!known || !edf6vr::ReadOwnedWeaponPose(soldier,weapon,pose) || !pose.nodes || !pose.nodeCount) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Same sound-only call as native weapon cycling at 59DF98..59DFB0.
// Invoke on the native input thread AFTER releasing g_lock. Never enter the
// native slot/equip function: it would alter burst, reload and animation state.
void PlayRangerDualSwitchSound(void* soldier) noexcept {
    if(!g_dualSwitchSound || !g_image.base || !soldier) return;
    __try {
        if(!edf6vr::HasType(g_image,soldier,".?AVAssultSoldier@@")
           || !edf6vr::Readable(soldier,0xC0)) return;
        auto manager=DualAt<void*>(g_image.base,0x20B2950);
        if(!manager || !edf6vr::Readable(manager,8)) return;
        g_dualSwitchSound(manager,static_cast<unsigned char*>(soldier)+0x90,
            reinterpret_cast<const wchar_t*>(g_image.base+0x17D2B50),nullptr);
        Log("RANGERDUAL switch sound submitted: native weapon change");
    } __except(EXCEPTION_EXECUTE_HANDLER) {Log("RANGERDUAL switch sound unavailable");}
}
// Called only after the native local soldier's input reader, under g_lock.
// True only for an accepted user toggle, never automatic cancellation.
bool UpdateRangerDual(void* soldier) noexcept {
    if(soldier!=g_vrSoldier) return false;
    const unsigned request=g_dualToggleSerial.load();
    const bool toggle=((request-g_dualHandledSerial)&1)!=0;
    g_dualHandledSerial=request;
    const auto now=GetTickCount64(),input=g_dualInputAt.load();
    const bool cancelled=g_dualCancel.exchange(false);
    const bool eligible=g_dualReady && g_vrEnabled && g_fpsEnabled && !g_faulted
        && g_dualEligible.load() && now-g_dualEligibleAt.load()<250 && input && now-input<250
        && g_adjust!=Adjusting::Active;
    if(!eligible || (cancelled && !toggle)) { ResetRangerDual(); return false; }
    if(g_dualActive.load() && !CurrentRangerPair(ReadRangerDual(),soldier)) ResetRangerDual();
    bool switched=false;
    if(toggle) {
        if(g_dualActive.load()) {ResetRangerDual();switched=true;}
        else {
            RangerDualState state{};
            if(SelectRangerPair(soldier,state)) {
                state.serial=request;
                AcquireSRWLockExclusive(&g_dualLock);g_dualState=state;ReleaseSRWLockExclusive(&g_dualLock);
                for(unsigned h=0;h<2;++h)g_dualWeapons[h]=state.weapons[h];
                g_dualActive.store(true);
                switched=true;
                g_twoHandOn=false;g_twoHandDirValid=false;g_twoHandWhereValid=false;
                Log("RANGERDUAL on owner=%p id=%u right=%p left=%p; reload paused",soldier,state.id,state.weapons[1],state.weapons[0]);
            } else Log("RANGERDUAL refused: current Ranger loadout/weapon profile not valid");
        }
    }
    if(g_dualActive.load()) {
        // Resolved actions are cleared too, independent of keyboard/pad remaps.
        // 59AE42 reads D72/73 for weapon secondary; 59AD9D reads D75 for reload.
        DualAt<unsigned char>(soldier,0xD72)=0;DualAt<unsigned char>(soldier,0xD73)=0;
        DualAt<unsigned char>(soldier,0xD75)=0;
        // Native zoom cancellation runs at the weapon tick, outside g_lock.
        g_nativeZoom=g_renderZoom=1;edf6vr::g_openxr.SetBinocularZoom(1);
    }
    return switched;
}

bool MakeLeftHandCommand(const WeaponHoldCommand& right,void* weapon,WeaponHoldCommand& left) noexcept {
    edf6vr::WeaponPose pose{};
    if(!edf6vr::ReadOwnedWeaponPose(right.soldier,weapon,pose) || !pose.nodes || !pose.nodeCount
       || pose.nodeCount>kMaxWeaponBones) return false;
    float at[3]{},q[4]{},aimAt[3]{},aimQ[4]{};
    if(!edf6vr::g_openxr.GripPose(0,at,q) || !edf6vr::g_openxr.HandPose(0,aimAt,aimQ)) return false;
    edf6vr::HeadBasis aim{};
    // The trim is a turn of the controller in its own axes, and the barrel is
    // that direction itself: rebuilt from a yaw and a pitch it lost which way
    // the hand faced near vertical, and the weapon rolled there.
    if(!edf6vr::HeadBasisFromXr(edf6vr::TrimAimRotation({aimQ[0],aimQ[1],aimQ[2],aimQ[3]},g_weaponYaw,g_weaponPitch),aim)
       || !edf6vr::NormalizedQuat({q[0],q[1],q[2],q[3]})) return false;
    const auto forward=edf6vr::RotateY(aim.forward,right.yawOffset);
    const edf6vr::Quat rot{q[0],q[1],q[2],q[3]};
    edf6vr::Vec3 axes[3];const edf6vr::Vec3 basis[3]={{1,0,0},{0,1,0},{0,0,-1}};
    for(unsigned i=0;i<3;++i) axes[i]=edf6vr::RotateY(edf6vr::XrToGame(edf6vr::QuatRotate(rot,basis[i])),right.yawOffset);
    left=right;left.handIndex=0;left.weapon=weapon;left.model=pose.model;
    left.weaponNodes=pose.nodes;left.count=pose.nodeCount;
    for(unsigned i=0;i<3;++i) {left.handAxes[i][0]=axes[i].x;left.handAxes[i][1]=axes[i].y;left.handAxes[i][2]=axes[i].z;}
    const auto from=axes[2];
    const edf6vr::Vec3 cross{from.y*forward.z-from.z*forward.y,from.z*forward.x-from.x*forward.z,from.x*forward.y-from.y*forward.x};
    const float cosine=from.x*forward.x+from.y*forward.y+from.z*forward.z;
    if(cosine<-.999f) return false;
    // Shortest arc without Euler roll reconstruction, including vertical aim.
    for(auto& v:axes) {
        const edf6vr::Vec3 a{cross.y*v.z-cross.z*v.y,cross.z*v.x-cross.x*v.z,cross.x*v.y-cross.y*v.x};
        const edf6vr::Vec3 b{cross.y*a.z-cross.z*a.y,cross.z*a.x-cross.x*a.z,cross.x*a.y-cross.y*a.x};
        v={v.x+a.x+b.x/(1+cosine),v.y+a.y+b.y/(1+cosine),v.z+a.z+b.z/(1+cosine)};
    }
    const float c=std::cos(g_weaponRollTrim),s=std::sin(g_weaponRollTrim);
    const auto r=axes[0],u=axes[1];
    axes[0]={r.x*c+u.x*s,r.y*c+u.y*s,r.z*c+u.z*s};
    axes[1]={u.x*c-r.x*s,u.y*c-r.y*s,u.z*c-r.z*s};
    const auto room=edf6vr::XrToGame({at[0],at[1],at[2]});
    const auto offset=edf6vr::RotateY({room.x-right.headRoom[0],room.y-right.headRoom[1],room.z-right.headRoom[2]},right.yawOffset);
    const float palm[3]={right.eyeWorld[0]+offset.x,right.eyeWorld[1]+offset.y,right.eyeWorld[2]+offset.z};
    const float rows[3][3]={{axes[0].x,axes[0].y,axes[0].z},{axes[1].x,axes[1].y,axes[1].z},{axes[2].x,axes[2].y,axes[2].z}};
    for(unsigned j=0;j<3;++j) {
        left.handPos[j]=palm[j];
        left.hand.palm[j]=palm[j]+rows[2][j]*g_weaponAhead-rows[0][j]*g_weaponRight+rows[1][j]*g_weaponUp;
        for(unsigned i=0;i<3;++i)left.hand.axes[i][j]=(i==0?-1.0f:1.0f)*rows[i][j];
        left.weaponWas[j]=pose.world.m[3][j];
    }
    // The muzzle offset came in as the right weapon's, because this command
    // starts as a copy of that one. Measure the left weapon's own, from the
    // one snapshot this pose was read in, so that placing it on the left hand
    // gives a point that does not move with an animation.
    __try {
        left.muzzleLocalValid=edf6vr::WeaponLocalPoint(
            *reinterpret_cast<const edf6vr::Matrix*>(static_cast<const unsigned char*>(pose.nodes)+0xB0),
            pose.world.m[3],left.muzzleLocal);
    } __except(EXCEPTION_EXECUTE_HANDLER) { left.muzzleLocalValid=false; }
    return true;
}
void PublishRangerDualHands() noexcept {
    if(!g_dualActive.load()) return;
    auto state=ReadRangerDual();
    if(!CurrentRangerPair(state,g_vrSoldier) || g_holdCommand.weapon!=state.weapons[1]
       || !g_holdCommand.tracked || !MakeLeftHandCommand(g_holdCommand,state.weapons[0],g_leftHoldCommand)) {
        g_leftHoldCommand={};ResetRangerDual();return;
    }
    state.hands[0]=g_leftHoldCommand;state.hands[1]=g_holdCommand;
    AcquireSRWLockExclusive(&g_dualLock);g_dualState=state;ReleaseSRWLockExclusive(&g_dualLock);
    PublishCasingFrame(g_leftHoldCommand);
    static ULONGLONG report=0;
    if(GetTickCount64()-report>5000) {
        report=GetTickCount64();
        Log("RANGERDUAL heldBack=%llu staleCommand=%llu",g_dualHeldBack.load(),g_dualStaleCommand.load());
        Log("RANGERDUAL ticks L/R=%llu/%llu poses=%llu/%llu draws=%llu/%llu failures=%llu ammo=%d/%d reload=%d/%d burst=%d/%d fire=%llu/%llu recoil=%.3f/%.3f visible=%u/%u",
            g_dualTicks[0].load(),g_dualTicks[1].load(),g_dualPoses[0].load(),g_dualPoses[1].load(),
            g_dualDraws[0].load(),g_dualDraws[1].load(),g_dualFailures.load(),
            DualAt<int>(state.weapons[0],0xBE8),DualAt<int>(state.weapons[1],0xBE8),
            DualAt<int>(state.weapons[0],0xE68),DualAt<int>(state.weapons[1],0xE68),
            DualAt<int>(state.weapons[0],0xE18),DualAt<int>(state.weapons[1],0xE18),
            g_dualFires[0].load(),g_dualFires[1].load(),g_dualRecoilLevel[0].load(),g_dualRecoilLevel[1].load(),
            unsigned(DualAt<unsigned char>(state.weapons[0],0x78C)),unsigned(DualAt<unsigned char>(state.weapons[1],0x78C)));
        Log("RANGERDUAL bulletInit=%llu/%llu originL=(%.3f,%.3f,%.3f) R=(%.3f,%.3f,%.3f) directionL=(%.3f,%.3f,%.3f) R=(%.3f,%.3f,%.3f)",
            g_dualInitializers[0].load(),g_dualInitializers[1].load(),
            g_dualInitPosition[0][0].load(),g_dualInitPosition[0][1].load(),g_dualInitPosition[0][2].load(),
            g_dualInitPosition[1][0].load(),g_dualInitPosition[1][1].load(),g_dualInitPosition[1][2].load(),
            g_dualFireDirection[0][0].load(),g_dualFireDirection[0][1].load(),g_dualFireDirection[0][2].load(),
            g_dualFireDirection[1][0].load(),g_dualFireDirection[1][1].load(),g_dualFireDirection[1][2].load());
        Log("RANGERDUAL offHand L/R=%.1f/%.1fdeg worst L/R=%.1f/%.1fdeg turnedBack=%llu last=%.1fdeg readCheck=%.1fdeg mismatch=%llu (each shot against the hand it left)",
            g_dualAimOff[0].load(),g_dualAimOff[1].load(),
            g_dualAimOffMax[0].load(),g_dualAimOffMax[1].load(),
            g_dualAimTurns.load(),g_dualAimTurnDeg.load(),
            g_dualAimSelfDeg.load(),g_dualAimMismatch.load());
    }
}

void __fastcall HookDualPose(void* attachment,void* ownerMatrix) {
    g_dualPose(attachment,ownerMatrix);
    if(!g_dualTick.weapon || g_dualTick.command.fencer) return;
    auto weapon=g_dualTick.weapon;
    // This native call is before reload and firing. Derived classes can update
    // E6E before calling their base, so set the gate at this exact point.
    DualAt<unsigned char>(weapon,0xE6E)=1;
    // Leave base-update matrices native: model attachment/animation may consume
    // them later. Tracked transforms are borrowed only around actual discharge.
}
bool CarryDualShotPose(void* attachment,void* ownerMatrix) noexcept {
    if(!g_dualTick.active) return false;
    auto weapon=g_dualTick.weapon;
    __try {
        if(ownerMatrix!=static_cast<unsigned char*>(weapon)+0x150) return false;
        auto entries=DualAt<unsigned char*>(weapon,0x1D0);
        const auto count=DualAt<std::uint64_t>(weapon,0x1E0);
        const auto offset=static_cast<unsigned char*>(attachment)-entries;
        if(count==0 || count>64 || offset<0 || offset%0xF0 || std::uint64_t(offset/0xF0)>=count
           || !edf6vr::Readable(attachment,0xD0,true)) return false;
        const auto& command=g_dualTick.command;
        if(command.weapon!=weapon) { ++g_dualStaleCommand; return false; }
        // weapon+150 is not this frame: measured in play it sits about 1.5 m
        // from the node root with the same heading, so it cannot stand in for
        // it. The node root is the frame the muzzle is carried from.
        const auto native=DualAt<edf6vr::Matrix>(command.weaponNodes,0xB0);
        edf6vr::Matrix output[2]{};
        const edf6vr::Matrix source[2]={DualAt<edf6vr::Matrix>(attachment,0x50),DualAt<edf6vr::Matrix>(attachment,0x90)};
        float before=0,after=0;
        if(!edf6vr::CarryWeaponBones(native,command.hand,source,2,output,before,after)) {++g_dualFailures;return false;}
        DualAt<edf6vr::Matrix>(attachment,0x50)=output[0];
        DualAt<edf6vr::Matrix>(attachment,0x90)=output[1];
        if(offset==0) {
            float root[3]{};for(unsigned j=0;j<3;++j)root[j]=DualAt<float>(command.soldier,0x90+j*4);
            PublishMuzzleFrame(weapon,command.soldier,command.objectId,output[0].m[3],root,GetTickCount64(),command.handIndex);
        }
        ++g_dualPoses[command.handIndex];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {++g_dualFailures;}
    return false;
}
// The direction a discharge would leave along: the attachment's own rows by
// the weapon's local fire vector, the same reading 688C30 and 688680 make of
// it. Called once with the game's matrices and once with the carried ones.
bool DualFireDirection(void* weapon,void* attachment,float* out) noexcept {
    __try {
        auto* bytes=static_cast<unsigned char*>(weapon);
        if(!edf6vr::Readable(bytes,0x360) || !edf6vr::Readable(attachment,0x90)) return false;
        const auto* local=reinterpret_cast<const float*>(bytes+0x350);
        const auto* rows=static_cast<const unsigned char*>(attachment)+0x50;
        float length=0;
        for(int j=0;j<3;++j) {
            float value=0;
            for(int k=0;k<3;++k) value+=local[k]*reinterpret_cast<const float*>(rows+k*16)[j];
            out[j]=value; length+=value*value;
        }
        length=std::sqrt(length);
        if(!std::isfinite(length) || length<1e-4f) return false;
        for(int j=0;j<3;++j) out[j]/=length;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Where this left-gun shot should go, and where the carry has actually put it.
//
// CarryDualShotPose maps the weapon's node root onto the hand, and that root
// is written by the animation: while the right gun reloads, the whole upper
// body swings and the left gun's shots left up to 61 degrees off the hand
// (RANGERDUAL offHand/worst), always upward. The weapon transform it is
// divided by is not animated -- the game rebuilds it from the mount and the
// aim -- so the two disagree by exactly the animation, and that is what ends
// up in the bullet.
//
// So the direction is rebuilt from things no animation touches: the game's own
// fire direction, turned by the shortest arc that takes the soldier's aim onto
// the hand's own forward. The weapon's tilt off the aim rides along inside the
// game's direction, so a lobbed weapon keeps its lob; pinning the bullet on
// the hand, as the Fencer's weapons are pinned, would have flattened it.
void PrepareLeftFireTurn(void* weapon,void* attachment,const float* gameDirection,
                         const WeaponHoldCommand& command) noexcept {
    g_dualShot.haveFireTurn=false;
    float carried[3]{},aim[3]{};
    if(!DualFireDirection(weapon,attachment,carried)) return;
    if(!GuideAimForward(command.soldier,aim)) return;
    float ahead[3]{},length=0;
    for(int j=0;j<3;++j) { ahead[j]=command.hand.axes[2][j]; length+=ahead[j]*ahead[j]; }
    length=std::sqrt(length);
    if(!std::isfinite(length) || length<0.5f) return;
    for(int j=0;j<3;++j) ahead[j]/=length;
    float rows[3][4]{{gameDirection[0],gameDirection[1],gameDirection[2],0},{0,0,0,0},{0,0,0,0}};
    if(!FencerTurnRows(rows,aim,ahead)) return;
    for(int j=0;j<3;++j) {
        if(!std::isfinite(rows[0][j]) || !std::isfinite(carried[j])) return;
        g_dualShot.fireFrom[j]=carried[j];
        g_dualShot.fireTo[j]=rows[0][j];
    }
    g_dualShot.haveFireTurn=true;
}
void* __fastcall HookDualSpread(void* output,const float* direction,float minimum,float maximum,std::uint64_t* rng) {
    if(g_dualShot.active) {
        const unsigned hand=g_dualShot.hand;
        for(unsigned j=0;j<3;++j)g_dualFireDirection[hand][j]=direction[j];
        for(unsigned j=0;j<3;++j)g_dualShot.nominal[j]=direction[j];
        g_dualShot.haveNominal=true;
        // How far this shot left from where that hand is pointing. The
        // direction comes out of the weapon transform carried onto the hand,
        // and that carry divides by the model's node root -- the animation --
        // so a recoil or a roll can lean it away from the controller. The
        // guide had exactly that fault and it is measured here before
        // anything is done about the bullet.
        {
            const auto& axis=g_dualTick.command.hand.axes[2];
            float along=0,length=0,aim=0;
            for(unsigned j=0;j<3;++j) {
                along+=direction[j]*axis[j]; length+=axis[j]*axis[j]; aim+=direction[j]*direction[j];
            }
            length=std::sqrt(length)*std::sqrt(aim);
            if(std::isfinite(length) && length>1e-4f) {
                const float degrees=std::acos(std::clamp(along/length,-1.0f,1.0f))*57.29578f;
                g_dualAimOff[hand].store(degrees,std::memory_order_relaxed);
                if(degrees>g_dualAimOffMax[hand].load(std::memory_order_relaxed))
                    g_dualAimOffMax[hand].store(degrees,std::memory_order_relaxed);
            }
        }
        // The turn was worked out from our own reading of the weapon's fire
        // direction. This is the game handing us the real one, so the two are
        // compared before anything is done with it: if they disagree the
        // reading is wrong and the shot is left exactly as the game made it.
        if(g_dualShot.haveFireTurn) {
            float along=0,mine=0,theirs=0;
            for(unsigned j=0;j<3;++j) {
                along+=direction[j]*g_dualShot.fireFrom[j];
                mine+=g_dualShot.fireFrom[j]*g_dualShot.fireFrom[j];
                theirs+=direction[j]*direction[j];
            }
            const float length=std::sqrt(mine)*std::sqrt(theirs);
            const float cosine=std::isfinite(length) && length>1e-4f
                ? std::clamp(along/length,-1.0f,1.0f) : -1.0f;
            g_dualAimSelfDeg.store(std::acos(cosine)*57.29578f,std::memory_order_relaxed);
            if(cosine<0.996f) {   // five degrees
                g_dualShot.haveFireTurn=false;
                g_dualAimMismatch.fetch_add(1,std::memory_order_relaxed);
            }
        }
        if(!g_dualShot.counted) {
            g_dualShot.counted=true;
            if(g_dualShot.fencer) FencerNoteShot(hand);   // the kick goes on the hand's aim, not the cone
            else { g_dualRecoil[hand].recoil.Shot(GetTickCount64()/1000.0); ++g_dualFires[hand]; }
        }
        // This is the native accuracy cone (weapon+378 times +E14), after
        // spread-pattern/charge modifiers. Preserve native RNG and pellet pattern.
        if(!g_dualShot.fencer && std::isfinite(maximum) && maximum>=0)
            maximum+=edf6vr::RangerRecoilSpreadDegrees(g_dualShot.recoil)*0.01745329252f;
    }
    return g_dualSpread(output,direction,minimum,maximum,rng);
}
void __fastcall HookDualFire(void* weapon,unsigned index,void* alternate,void* counter,bool consume) {
    const bool fencer=FencerWeaponActive(weapon);
    if(!g_dualTick.active || weapon!=g_dualTick.weapon || !(g_dualActive.load() || fencer)) {
        // The left gun has no native aim: fired without a tracked pose it
        // leaves from where it hangs on the back, straight up. Hold the shot.
        if(g_dualActive.load() && weapon && weapon==ReadRangerDual().weapons[0]) { ++g_dualHeldBack; return; }
        g_dualFire(weapon,index,alternate,counter,consume);return;
    }
    auto entries=DualAt<unsigned char*>(weapon,0x1D0);
    const auto count=DualAt<std::uint64_t>(weapon,0x1E0);
    if(!count || count>64 || !edf6vr::Readable(entries,static_cast<std::size_t>(count)*0xF0,true)) {
        ++g_dualFailures;return;
    }
    auto attachment=entries+(index%count)*0xF0;
    const edf6vr::Matrix kept[2]={DualAt<edf6vr::Matrix>(attachment,0x50),DualAt<edf6vr::Matrix>(attachment,0x90)};
    const auto previous=g_dualShot;
    const auto hand=g_dualTick.command.handIndex<2?g_dualTick.command.handIndex:1u;
    if(fencer) g_dualShot={true,false,hand,0,true};
    else {
        const auto state=ReadRangerDual();
        auto& recoil=g_dualRecoil[hand];
        if(recoil.weapon!=weapon || recoil.serial!=state.serial)recoil={weapon,state.serial,{}};
        g_dualShot={true,false,hand,recoil.recoil.Level(GetTickCount64()/1000.0),false};
        g_dualRecoilLevel[hand]=g_dualShot.recoil;
    }
    __try {
        // 6904D2/694873 rebuild native muzzle AFTER the base tick hook. Rebuild
        // and carry at the actual discharge boundary, including burst shots.
        g_dualPose(attachment,static_cast<unsigned char*>(weapon)+0x150);
        // The game's own fire direction, before the carry touches it: this is
        // the one moment it can be read, and it carries the weapon's own tilt
        // off the aim, which a lobbed weapon in the left hand must keep.
        float gameDirection[3]{};
        const bool haveGame=hand==0 && !fencer
            && DualFireDirection(weapon,attachment,gameDirection);
        if(CarryDualShotPose(attachment,static_cast<unsigned char*>(weapon)+0x150)) {
            if(haveGame) PrepareLeftFireTurn(weapon,attachment,gameDirection,g_dualTick.command);
            g_dualFire(weapon,index,alternate,counter,consume);
        }
    } __finally {
        DualAt<edf6vr::Matrix>(attachment,0x50)=kept[0];
        DualAt<edf6vr::Matrix>(attachment,0x90)=kept[1];
        g_dualShot=previous;
    }
}
void __fastcall HookDualWeaponTick(void* weapon,void* context) {
    DualTick original=nullptr;
    const auto table=*static_cast<void**>(weapon);
    for(unsigned i=0;i<sizeof(kDualWeapons)/sizeof(kDualWeapons[0]);++i)
        if(table==g_image.base+kDualWeapons[i].table) {original=g_dualOriginals[i];break;}
    if(!original) return; // only checked vtable slots enter this hook
    if(FencerWeaponActive(weapon)) { FencerWeaponTick(weapon,context,original); return; }
    // The hook is shared by weapon classes. NPCs, other players and the other
    // soldier classes take no snapshot lock, tracking read, or extra game call.
    if(weapon!=g_dualWeapons[0].load() && weapon!=g_dualWeapons[1].load()
       && weapon!=g_dualCleanupWeapon.load()) {original(weapon,context);return;}
    RangerDualState cleanup{};
    AcquireSRWLockExclusive(&g_dualLock);
    if(g_dualCleanup.weapons[0]==weapon) {cleanup=g_dualCleanup;g_dualCleanup={};g_dualCleanupWeapon=nullptr;}
    ReleaseSRWLockExclusive(&g_dualLock);
    if(cleanup.soldier && g_dualHolster) {
        // Native holstering applies the weapon's burst/charge/effect policy. Do not
        // run it on a weapon that native switching just equipped in the right.
        __try {
            const auto owner=DualAt<void*>(weapon,0x120);
            bool ownedNow=false;
            if(owner==cleanup.soldier && edf6vr::Readable(owner,0x1988)
               && edf6vr::HasType(g_image,owner,".?AVAssultSoldier@@") && DualAt<unsigned>(owner,0x314)==cleanup.id)
                for(unsigned i=0;i<edf6vr::OwnedWeaponCount(owner);++i)
                    if(edf6vr::OwnedWeaponAt(owner,i)==weapon)ownedNow=true;
            if(ownedNow && !DualAt<unsigned char>(weapon,0x13E) && !RangerDualWeapon(weapon)) {
                DualAt<unsigned char>(weapon,0x139)=0;
                DualAt<unsigned char>(weapon,0x13E)=1;
                __try {
                    g_dualHolster(weapon);
                    if(g_dualVisibility)g_dualVisibility(weapon,cleanup.leftVisible!=0);
                }
                __finally {DualAt<unsigned char>(weapon,0x13E)=0;}
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {++g_dualFailures;}
    }
    auto state=ReadRangerDual();
    const unsigned hand=weapon==state.weapons[0]?0:1;
    const auto now=GetTickCount64(),input=g_dualInputAt.load();
    if(!g_dualActive.load() || !input || now-input>250 || weapon!=state.weapons[hand]
       || !CurrentRangerPair(state,state.soldier)) {
        original(weapon,context);return;
    }
    const auto previous=g_dualTick;
    const unsigned char reload=DualAt<unsigned char>(weapon,0xE6E),equipped=DualAt<unsigned char>(weapon,0x13E),ready=DualAt<unsigned char>(weapon,0xE6C);
    // The command must be this weapon's. Right after a swap the left command can
    // still describe the other gun for a tick or two; carrying this weapon's
    // muzzle through that gun's node frame sent the shot off at right angles --
    // straight up, with this one still hanging on the back.
    const bool commandOwned=state.hands[hand].weapon==weapon;
    if(!commandOwned) ++g_dualStaleCommand;
    const bool poseValid=commandOwned && ValidateHoldCommand(state.hands[hand],state.hands[hand].model);
    g_dualTick={state.hands[hand],weapon,poseValid};
    if(poseValid)LateLatchHand(g_dualTick.command);
    // Match current owner locomotion, while keeping the camera/head frame coherent.
    float travel=0;
    if(poseValid) for(unsigned j=0;j<3;++j) {
        const float delta=DualAt<float>(state.soldier,0x90+j*4)-state.hands[hand].rootWorld[j];
        travel+=delta*delta;g_dualTick.command.hand.palm[j]+=delta;
    }
    if(!std::isfinite(travel) || travel>=400)g_dualTick.active=false;
    ++g_dualTicks[hand];
    const auto& recoil=g_dualRecoil[hand];
    g_dualRecoilLevel[hand]=(recoil.weapon==weapon && recoil.serial==state.serial)?recoil.recoil.Level(now/1000.0):0;
    __try {
        if(DualAt<unsigned char>(weapon,0x780) && g_dualZoom)g_dualZoom(weapon);
        DualAt<unsigned char>(weapon,0xE6E)=1;
        if(g_dualTick.active && hand==0) {DualAt<unsigned char>(weapon,0x13E)=1;DualAt<unsigned char>(weapon,0xE6C)=1;}
        DualAt<unsigned char>(weapon,0x13B)=0;
        if(hand==0) DualAt<unsigned char>(weapon,0x139)=g_dualTick.active && g_dualLeftFire.load() && RangerMayFire(state.soldier)?1:0;
        original(weapon,context); // exactly once: native ammo, cooldown, charge, projectiles
        if(hand==0 && g_dualTick.active && g_dualVisibility
           && (!DualAt<unsigned char>(weapon,0x78C) || !DualAt<unsigned char>(weapon,0xF30+0x480)))
            g_dualVisibility(weapon,true); // 1 = visible; native model/scene nodes, no duplicate draw
    } __finally {
        DualAt<unsigned char>(weapon,0xE6E)=reload;
        DualAt<unsigned char>(weapon,0x13E)=equipped;DualAt<unsigned char>(weapon,0xE6C)=ready;
        g_dualTick=previous;
    }
}

bool CheckRangerDualProfile() noexcept {
    __try {
        if(!g_image.base || g_image.base[0x69366A]!=0xE8
           || g_image.base+0x69366F+DualAt<std::int32_t>(g_image.base,0x69366B)!=g_image.base+0x6969A0) return false;
        const unsigned char reload[]={0x80,0xBE,0x6E,0x0E,0,0,0,0x0F,0x85,0x30,4,0,0};
        if(std::memcmp(g_image.base+0x693B3C,reload,sizeof reload)) return false;
        const unsigned char index[]={0x89,0x77,0x38,0xE8,0x82,0x15,1,0};
        if(std::memcmp(g_image.base+0x590C96,index,sizeof index)) return false;
        const unsigned char zoom[]={0x80,0xB9,0x80,7,0,0,0,0x0F,0x85,0xF3,3,0,0,0xC3};
        if(std::memcmp(g_image.base+0x695F10,zoom,sizeof zoom)) return false;
        const unsigned char holster[]={0x80,0xB9,0x3E,1,0,0,0,0x48,0x8B,0xD9,0x74,0x34};
        if(std::memcmp(g_image.base+0x690236,holster,sizeof holster))return false;
        const unsigned char visibility[]={0x38,0x91,0x8C,7,0,0,0x74,6};
        if(std::memcmp(g_image.base+0x696160,visibility,sizeof visibility))return false;
        const unsigned char sound[]={0x48,0x8D,0x91,0x90,0,0,0,0x45,0x33,0xC9,
            0x4C,0x8D,0x05,0xA7,0x4B,0x23,1,0x48,0x8B,0x0D,0xA0,0x49,0xB1,1,
            0xE8,0xCB,0x4A,0x21,0};
        if(std::memcmp(g_image.base+0x59DF98,sound,sizeof sound)
           || std::memcmp(g_image.base+0x17D2B50,L"\u6B66\u5668\u5207\u308A\u66FF\u3048",14))return false;
        for(const auto call:kDualFireCalls)
            if(g_image.base[call]!=0xE8 || g_image.base+call+5+DualAt<std::int32_t>(g_image.base,call+1)!=g_image.base+0x696FD0)return false;
        if(g_image.base[0x691B10]!=0xE8 || g_image.base+0x691B15+DualAt<std::int32_t>(g_image.base,0x691B11)!=g_image.base+0x4E820)return false;
        for(const auto& p:kDualWeapons) {
            void* table=g_image.base+p.table;
            if(!edf6vr::HasType(g_image,&table,p.name) || DualAt<void*>(table,0x28)!=g_image.base+p.tick) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool InstallRangerDual() noexcept {
    if(!CheckRangerDualProfile())return false;
    g_dualIndex=reinterpret_cast<DualIndex>(g_image.base+0x5A2220);
    g_dualPose=reinterpret_cast<DualPose>(g_image.base+0x6969A0);
    g_dualZoom=reinterpret_cast<DualZoom>(g_image.base+0x695F10);
    g_dualHolster=reinterpret_cast<DualZoom>(g_image.base+0x690230);
    g_dualVisibility=reinterpret_cast<DualVisibility>(g_image.base+0x696150);
    g_dualFire=reinterpret_cast<DualFire>(g_image.base+0x696FD0);
    g_dualSpread=reinterpret_cast<DualSpread>(g_image.base+0x4E820);
    g_dualSwitchSound=reinterpret_cast<DualSwitchSound>(g_image.base+0x7B2A80);
    bool changed=false;
    if(!edf6vr::RedirectCall(g_image.base+0x69366A,reinterpret_cast<void*>(g_dualPose),reinterpret_cast<void*>(&HookDualPose),changed))return false;
    for(const auto call:kDualFireCalls)
        if(!edf6vr::RedirectCall(g_image.base+call,reinterpret_cast<void*>(g_dualFire),reinterpret_cast<void*>(&HookDualFire),changed))return false;
    if(!edf6vr::RedirectCall(g_image.base+0x691B10,reinterpret_cast<void*>(g_dualSpread),reinterpret_cast<void*>(&HookDualSpread),changed))return false;
    unsigned index=0;
    for(const auto& p:kDualWeapons) {
        changed=false;
        g_dualOriginals[index++]=reinterpret_cast<DualTick>(g_image.base+p.tick);
        if(!edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+p.table+0x28),g_image.base+p.tick,
            reinterpret_cast<void*>(&HookDualWeaponTick),changed))return false;
    }
    return true;
}
