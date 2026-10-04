// Included by plugin.cpp inside its private namespace, after nix_arm_aim.h.
//
// Two-hand shots seen as two directions by the other players (the user,
// 2026-10-04: "レンジャーやフェンサーが両手で別方向に撃った時、他の人からは同じ方向に
// 撃ってるように見えてる。MOD導入済み同士だと別方向に見えるようにしたい。ポージング不要、
// 2方向に攻撃が出ていれば良い"), worked out with the EDF6MultiSlot session (1.5.36,
// its src/handaim.h).
//
// EDF6 has one aim per player, and this mod turns a two-hand weapon's shots onto
// its hand only at the discharge (HookDualFire, at the 696FD0 calls), after the
// game has told the others about the shot:
//  - a weapon synced shot by shot (net::WeaponBulletSyncTable) sends its muzzle
//    from 694910, called at 690D3B just before the 696FD0 at 690D54; the
//    receiver writes that matrix to the muzzle and fires (7606A0, 692A24);
//  - a rapid-fire weapon (weapon+0x153C flag 0x10) sends only a shot count, and
//    the receiver catches up along its own copy of the player's single aim
//    (690420: 6969A0 then 696FD0 at 6904F7 / 690603).
// So:
//  1. HookShotSend (the call at 690D3B): the muzzle 694910 sends is put on the
//     hand for the call -- the same pose the shot is then fired with. That needs
//     nothing on the other end: a player without either mod sees it too.
//  2. Every tick of a two-hand weapon (HandAimNoteTick): the direction its shots
//     leave along goes to EDF6MultiSlot (MultiSlot_SetHandAim), which sends it to
//     the room after the game's own packets.
// Turning another player's rapid-fire catch-up shots onto what their
// EDF6MultiSlot sent is EDF6MultiSlot's own job (1.5.37 and later: mid-hooks
// around the 696FD0 calls at 6904F7 / 690603, whatever those calls reach). Only
// a VR player fires two ways, so the sending half lives here and the receiving
// half there, which every viewer with EDF6MultiSlot has, VR or not (the user,
// 2026-10-04: "一本化がベスト？ベストならやって"). This side used to turn them too
// (HandAimRemoteFire, from HookDualFire); that is gone.
// A weapon is named by its place in its owner's weapon list (soldier+0x1950,
// edf6vr::OwnedWeaponAt): both ends build it from the same synced loadout. The
// exports are looked up at run time; without EDF6MultiSlot (or an older one) 2
// does nothing. [VR] HandAimSync=0 turns both off.
//
// Which shots leave inside the weapon's tick and which do not was worked out
// over every Ranger main and Fencer weapon in the game data before the next
// online test (the user, 2026-10-04: "人呼んでテストってコストデカいから、全武器の
// タイプ検証してからテスト回して"; research: the sync choice 767150 over every
// star level, custom_parameter, the fire paths below). Two gaps came out, now
// closed here, and one shot the others saw that this player never fired:
//  3. A shot that leaves on the soldier's animation event "発射" (590880 -> the
//     weapon's slot 0xB8 -> 690BB0 -> 690CC0, which sends and fires) is outside
//     the weapon's tick, so 1 never saw it: the Fencer's cannons and missiles
//     that fire with their arm animation (custom_parameter[2] == 2: HeavyShoot
//     +0x1630 at 69E640, HomingShoot at 69E7DC; the hand cannons, canister and
//     Gallia cannons, cannon shots, missiles), the spears, the pile banker.
//     Here (hardware 2026-10-04) the hand cannons' sends never reached the hook
//     and their bullets were built 10 degrees off the carried muzzle; this
//     player sees them on the barrel only because shot_origin.h turns each
//     bullet at its birth, after the send. Such a send, of a weapon shot_origin.h
//     turns (a hand weapon, not a sweep, not on a shoulder), has its muzzle's
//     rows turned onto the hand's direction; its point stays where the shot is
//     born. Count-only and matrix-less shots need nothing: EDF6MultiSlot turns
//     those on the other end (1.6.5) whatever this end sent.
//  4. The katana and blades and the hammers strike only on that event (690BB0
//     is reached from their slot 0xB8 alone: 6AFA40, 69FF43), and their hits
//     are left as the game builds them (shot_origin.h), so the others see the
//     game's swing too: no direction goes for them, or EDF6MultiSlot would turn
//     their matrix-less hits onto the hand. A shield strikes both in its tick
//     (its state 6AB1C0) and on the event, and keeps its direction.
//  5. The Ranger's left gun out of its tick is held back by HookDualFire; its
//     send is dropped with it, or the others would see a shot this player never
//     fired, from the gun hanging on the back (an animation-fired weapon there:
//     the planet sniper cannons, the Prominence missiles).
#pragma once

bool g_handAimSync=true;   // [VR] HandAimSync
using HandAimVersionFn=int(*)();
using HandAimSetFn=void(*)(int,const float*);
std::atomic<HandAimSetFn> g_handAimSet{nullptr};
std::atomic<ULONGLONG> g_handAimLookedAt{0};
std::atomic<int> g_handAimApi{0};
std::atomic<unsigned long long> g_handAimNotes{0},g_handAimSendTurns{0},g_handAimFailures{0};
// 3-5 above: sends turned away from the weapon's tick, left-gun sends dropped
// with their held-back shots, ticks of a swing that gave no direction.
std::atomic<unsigned long long> g_handAimAwayTurns{0},g_handAimHeldSends{0},g_handAimSwingTicks{0};
// How each two-hand weapon the send hook saw is synced (694910 reads
// weapon+0x153C: flag 0x10 = count only, nothing sent here; else +0x1540: mode
// 0 rotation, 1 rotation and point): which of the two ways a shot reaches the
// others (hardware 2026-10-04: a Fencer's two Weapon_Gatling looked one way to
// the other player, so which way they travel is the question).
std::atomic<unsigned long long> g_handAimCountOnly{0},g_handAimMode0{0},g_handAimMode1{0},g_handAimModeOther{0};
std::atomic<unsigned> g_handAimLastFlags{0},g_handAimLastMode{0};

// EDF6MultiSlot's exports (version 1), once found; looked for at most every 2 s.
bool HandAimBind() noexcept {
    if(g_handAimSet.load(std::memory_order_acquire)) return true;
    const ULONGLONG now=GetTickCount64(),at=g_handAimLookedAt.load(std::memory_order_relaxed);
    if(at && now-at<2000) return false;
    g_handAimLookedAt.store(now,std::memory_order_relaxed);
    HMODULE module=GetModuleHandleW(L"EDF6MultiSlot.dll");
    if(!module) return false;
    const auto version=reinterpret_cast<HandAimVersionFn>(reinterpret_cast<void*>(GetProcAddress(module,"MultiSlot_HandAimVersion")));
    const auto set=reinterpret_cast<HandAimSetFn>(reinterpret_cast<void*>(GetProcAddress(module,"MultiSlot_SetHandAim")));
    if(!version || !set) return false;
    int api=0;
    __try { api=version(); } __except(EXCEPTION_EXECUTE_HANDLER) { api=0; }
    if(api<1) return false;
    g_handAimApi.store(api,std::memory_order_relaxed);
    g_handAimSet.store(set,std::memory_order_release);
    Log("HANDAIM bound to EDF6MultiSlot (hand aim version %d): players with it see each hand's shots go that hand's way",api);
    return true;
}
// The weapon's place in its owner's weapon list, or -1.
int HandAimSlot(void* soldier,void* weapon) noexcept {
    if(!soldier || !weapon) return -1;
    const unsigned count=edf6vr::OwnedWeaponCount(soldier);
    for(unsigned i=0;i<count && i<8;++i) if(edf6vr::OwnedWeaponAt(soldier,i)==weapon) return static_cast<int>(i);
    return -1;
}
// The direction a muzzle matrix fires along: its rows by the weapon's local
// fire vector (weapon+0x350), as DualFireDirection reads it in place.
bool HandAimFireDirection(void* weapon,const edf6vr::Matrix& m,float out[3]) noexcept {
    __try {
        const auto* local=reinterpret_cast<const float*>(static_cast<unsigned char*>(weapon)+0x350);
        float length=0;
        for(int j=0;j<3;++j) { out[j]=local[0]*m.m[0][j]+local[1]*m.m[1][j]+local[2]*m.m[2][j]; length+=out[j]*out[j]; }
        length=std::sqrt(length);
        if(!std::isfinite(length) || length<1e-4f) return false;
        for(int j=0;j<3;++j) out[j]/=length;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// A matrix's rows turned by the shortest arc from one direction to another
// (FencerTurnRows); its point stays.
bool HandAimTurnRows(edf6vr::Matrix& m,const float from[3],const float to[3]) noexcept {
    float rows[3][4]{};
    for(int r=0;r<3;++r) for(int j=0;j<3;++j) rows[r][j]=m.m[r][j];
    if(!FencerTurnRows(rows,from,to)) return false;
    for(int r=0;r<3;++r) for(int j=0;j<3;++j) { if(!std::isfinite(rows[r][j])) return false; m.m[r][j]=rows[r][j]; }
    return true;
}
// The muzzle a two-hand discharge leaves with, as HookDualFire makes it, worked
// out without firing or publishing anything: `source` is the muzzle as the game
// rebuilt it (6969A0); it is carried from the weapon's node root onto the hand
// (CarryWeaponBones, as CarryDualShotPose does), and the Ranger's left gun is
// then turned onto the direction PrepareLeftFireTurn sends its bullet (the
// game's own direction, turned from the game's aim onto the hand). Inside the
// weapon's tick only (g_dualTick is this weapon's).
bool HandAimFinalPose(void* weapon,const edf6vr::Matrix source[2],edf6vr::Matrix out[2],float dir[3]) noexcept {
    const auto& command=g_dualTick.command;
    if(!g_dualTick.active || g_dualTick.weapon!=weapon || command.weapon!=weapon || !command.weaponNodes) return false;
    const bool rangerLeft=!FencerWeaponActive(weapon) && command.handIndex==0;
    __try {
        float game[3]{};
        const bool haveGame=rangerLeft && HandAimFireDirection(weapon,source[0],game);
        const auto native=*reinterpret_cast<const edf6vr::Matrix*>(static_cast<unsigned char*>(command.weaponNodes)+0xB0);
        float before=0,after=0;
        if(!edf6vr::CarryWeaponBones(native,command.hand,source,2,out,before,after)) return false;
        float carried[3]{};
        if(!HandAimFireDirection(weapon,out[0],carried)) return false;
        for(int j=0;j<3;++j) dir[j]=carried[j];
        float aim[3]{};
        if(haveGame && GuideAimForward(command.soldier,aim)) {
            float ahead[3]{},length=0;
            for(int j=0;j<3;++j) { ahead[j]=command.hand.axes[2][j]; length+=ahead[j]*ahead[j]; }
            length=std::sqrt(length);
            if(std::isfinite(length) && length>=0.5f) {
                for(int j=0;j<3;++j) ahead[j]/=length;
                float rows[3][4]{{game[0],game[1],game[2],0}};
                if(FencerTurnRows(rows,aim,ahead) && std::isfinite(rows[0][0]) && std::isfinite(rows[0][1]) && std::isfinite(rows[0][2])) {
                    const float to[3]={rows[0][0],rows[0][1],rows[0][2]};
                    edf6vr::Matrix turned[2]{out[0],out[1]};
                    if(HandAimTurnRows(turned[0],carried,to) && HandAimTurnRows(turned[1],carried,to)) {
                        out[0]=turned[0]; out[1]=turned[1];
                        for(int j=0;j<3;++j) dir[j]=to[j];
                    }
                }
            }
        }
        return std::isfinite(dir[0]) && std::isfinite(dir[1]) && std::isfinite(dir[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The muzzle's two matrices turned by the arc that takes the first one's fire
// direction onto `dir`; both points stay.
bool HandAimTurnEntry(void* weapon,unsigned char* entry,const float dir[3]) noexcept {
    __try {
        edf6vr::Matrix rows[2]={*reinterpret_cast<const edf6vr::Matrix*>(entry+0x50),*reinterpret_cast<const edf6vr::Matrix*>(entry+0x90)};
        float from[3]{};
        if(!HandAimFireDirection(weapon,rows[0],from) || !HandAimTurnRows(rows[0],from,dir) || !HandAimTurnRows(rows[1],from,dir)) return false;
        *reinterpret_cast<edf6vr::Matrix*>(entry+0x50)=rows[0];
        *reinterpret_cast<edf6vr::Matrix*>(entry+0x90)=rows[1];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The Fencer melee whose hit shot_origin.h leaves as the game builds it: the
// katana and blades (a Weapon_Swing that is not a spear), the hammers, the
// shields.
bool HandAimFencerSweeps(void* weapon) noexcept {
    const bool swing=edf6vr::HasType(g_image,weapon,".?AVWeapon_Swing@@");
    return (swing && !FencerKnownSpear(weapon)) || edf6vr::HasType(g_image,weapon,".?AVWeapon_ImpactHammer@@")
        || edf6vr::HasType(g_image,weapon,".?AVWeapon_Shield@@");
}
// Of those, the ones that strike only on the animation event (4 above).
bool HandAimGameSwing(void* weapon) noexcept {
    const bool swing=edf6vr::HasType(g_image,weapon,".?AVWeapon_Swing@@");
    return (swing && !FencerKnownSpear(weapon)) || edf6vr::HasType(g_image,weapon,".?AVWeapon_ImpactHammer@@");
}
// The hand a Fencer weapon's shot away from its tick is turned with (3 above):
// only where shot_origin.h turns the bullet onto the barrel (FencerHandWeapons,
// not on a shoulder, not a sweep), the command taken as FencerWeaponTick takes it.
bool HandAimAwayCommand(void* weapon,WeaponHoldCommand& command) noexcept {
    if(!g_fencerHandWeapons || !FencerWeaponActive(weapon)) return false;
    const unsigned hand=weapon==g_fencerWeapons[0].load(std::memory_order_relaxed)?0u:1u;
    if(g_fencerShoulder[hand] || HandAimFencerSweeps(weapon)) return false;
    AcquireSRWLockShared(&g_fencerHandLock); command=hand==0?g_fencerLeft:g_fencerRight; ReleaseSRWLockShared(&g_fencerHandLock);
    if(command.weapon!=weapon || !command.tracked || !ValidateHoldCommand(command,command.model)) return false;
    FencerFollowSoldier(command);
    return true;
}
// One muzzle entry of a weapon (weapon+0x1D0, 0xF0 apart, count +0x1E0).
unsigned char* HandAimEntry(void* weapon,unsigned index) noexcept {
    __try {
        auto* entries=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(weapon)+0x1D0);
        const auto count=*reinterpret_cast<const std::uint64_t*>(static_cast<unsigned char*>(weapon)+0x1E0);
        if(!entries || !count || count>64) return nullptr;
        auto* entry=entries+(index%count)*0xF0;
        return edf6vr::Readable(entry,0xF0,true)?entry:nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// 2. From the two-hand weapon ticks (HookDualWeaponTick for the Ranger's pair,
// FencerWeaponTick), once g_dualTick is this weapon's: which way its shots go
// now, to EDF6MultiSlot. The first muzzle is rebuilt and carried as a shot
// would be, read, and put back whole.
void HandAimNoteTick(void* weapon) noexcept {
    if(!g_handAimSync || !g_dualTick.active || g_dualTick.weapon!=weapon || !g_dualPose || !HandAimBind()) return;
    if(FencerWeaponActive(weapon) && HandAimGameSwing(weapon)) { g_handAimSwingTicks.fetch_add(1,std::memory_order_relaxed); return; }
    const auto set=g_handAimSet.load(std::memory_order_acquire);
    if(!set) return;
    const int slot=HandAimSlot(g_dualTick.command.soldier,weapon);
    if(slot<0) return;
    auto* entry=HandAimEntry(weapon,0);
    if(!entry) return;
    unsigned char kept[0xF0];
    std::memcpy(kept,entry,sizeof(kept));
    edf6vr::Matrix out[2]{};
    float dir[3]{};
    bool ok=false;
    __try {
        __try {
            g_dualPose(entry,static_cast<unsigned char*>(weapon)+0x150);
            const edf6vr::Matrix source[2]={*reinterpret_cast<const edf6vr::Matrix*>(entry+0x50),*reinterpret_cast<const edf6vr::Matrix*>(entry+0x90)};
            ok=HandAimFinalPose(weapon,source,out,dir);
        } __finally { std::memcpy(entry,kept,sizeof(kept)); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok=false; }
    if(!ok) { g_handAimFailures.fetch_add(1,std::memory_order_relaxed); return; }
    __try { set(slot,dir); } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    g_handAimNotes.fetch_add(1,std::memory_order_relaxed);
}

// 1. The shot-by-shot send (694910 at 690D3B: weapon, muzzle index, two values
// of the caller's; it reads the muzzle's +0x50 rows and its point +0x80 and
// nothing else of it, and sends nothing for a count-only weapon). For a
// two-hand weapon the muzzle goes on the hand for the call -- HookDualFire's
// condition, HookDualFire's pose -- and back after. Away from the tick, 3 and 5
// above.
using ShotSendFn=void(__fastcall*)(void*,int,unsigned,unsigned);
ShotSendFn g_shotSendOriginal=nullptr;
void __fastcall HookShotSend(void* weapon,int index,unsigned a,unsigned b) {
    const bool fencer=FencerWeaponActive(weapon);
    const bool inTick=g_dualTick.active && weapon==g_dualTick.weapon && (g_dualActive.load() || fencer);
    // 5: HookDualFire's hold-back, the same test.
    if(g_handAimSync && !inTick && g_dualActive.load() && weapon && weapon==ReadRangerDual().weapons[0]) {
        g_handAimHeldSends.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    WeaponHoldCommand away{};
    const bool awayShot=!inTick && g_handAimSync && g_dualPose && HandAimAwayCommand(weapon,away);
    unsigned char* entry=nullptr;
    if(g_handAimSync && g_dualPose && (inTick || awayShot))
        entry=HandAimEntry(weapon,static_cast<unsigned>(index));
    if(!entry) { g_shotSendOriginal(weapon,index,a,b); return; }
    __try {
        const unsigned flags=*(static_cast<unsigned char*>(weapon)+0x153C);
        const unsigned mode=*reinterpret_cast<const unsigned*>(static_cast<unsigned char*>(weapon)+0x1540);
        g_handAimLastFlags.store(flags,std::memory_order_relaxed); g_handAimLastMode.store(mode,std::memory_order_relaxed);
        if(flags&0x10) g_handAimCountOnly.fetch_add(1,std::memory_order_relaxed);
        else if(mode==0) g_handAimMode0.fetch_add(1,std::memory_order_relaxed);
        else if(mode==1) g_handAimMode1.fetch_add(1,std::memory_order_relaxed);
        else g_handAimModeOther.fetch_add(1,std::memory_order_relaxed);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    unsigned char kept[0xF0];
    std::memcpy(kept,entry,sizeof(kept));
    const auto previous=g_dualTick;
    bool turned=false;
    __try {
        // Away from the tick the hand's command is borrowed for the pose, as
        // the tick would have it.
        if(awayShot) g_dualTick={away,weapon,true};
        g_dualPose(entry,static_cast<unsigned char*>(weapon)+0x150);
        const edf6vr::Matrix source[2]={*reinterpret_cast<const edf6vr::Matrix*>(entry+0x50),*reinterpret_cast<const edf6vr::Matrix*>(entry+0x90)};
        edf6vr::Matrix out[2]{};
        float dir[3]{};
        const bool posed=HandAimFinalPose(weapon,source,out,dir);
        if(awayShot) {
            // The muzzle as the game has it, only turned: the point is where
            // this shot is born here (shot_origin.h moves no point).
            std::memcpy(entry,kept,sizeof(kept));
            turned=posed && HandAimTurnEntry(weapon,entry,dir);
        } else if(posed) {
            *reinterpret_cast<edf6vr::Matrix*>(entry+0x50)=out[0];
            *reinterpret_cast<edf6vr::Matrix*>(entry+0x90)=out[1];
            turned=true;
        }
        g_shotSendOriginal(weapon,index,a,b);
    } __finally { std::memcpy(entry,kept,sizeof(kept)); g_dualTick=previous; }
    if(turned) {
        g_handAimSendTurns.fetch_add(1,std::memory_order_relaxed);
        if(awayShot) g_handAimAwayTurns.fetch_add(1,std::memory_order_relaxed);
    }
}
bool InstallHandAimSend() noexcept {
    if(!g_image.base) return false;
    __try {
        if(g_image.base[0x690D3B]!=0xE8
           || g_image.base+0x690D40+*reinterpret_cast<const std::int32_t*>(g_image.base+0x690D3C)!=g_image.base+0x694910) return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    g_shotSendOriginal=reinterpret_cast<ShotSendFn>(g_image.base+0x694910);
    bool changed=false;
    return edf6vr::RedirectCall(g_image.base+0x690D3B,g_image.base+0x694910,reinterpret_cast<void*>(&HookShotSend),changed);
}

void HandAimReport() noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(now-at<5000) return;
    at=now;
    const unsigned long long notes=g_handAimNotes.load(),sends=g_handAimSendTurns.load();
    static unsigned long long last=~0ull;
    const unsigned long long sum=notes+sends+g_handAimFailures.load()+g_handAimHeldSends.load()+g_handAimSwingTicks.load();
    if(sum==last) return;
    last=sum;
    Log("HANDAIM sync=%d multislot=%d (api %d) directions given=%llu shot sends on the hand=%llu (away from the tick %llu; count only %llu, per shot mode0 %llu mode1 %llu other %llu; last flags %02X mode %u) left-gun sends held back=%llu swing ticks without a direction=%llu failures=%llu (others' shots: EDF6MultiSlot turns them)",
        g_handAimSync?1:0,g_handAimSet.load()?1:0,g_handAimApi.load(),notes,sends,g_handAimAwayTurns.load(),g_handAimCountOnly.load(),g_handAimMode0.load(),
        g_handAimMode1.load(),g_handAimModeOther.load(),g_handAimLastFlags.load(),g_handAimLastMode.load(),g_handAimHeldSends.load(),
        g_handAimSwingTicks.load(),g_handAimFailures.load());
}
