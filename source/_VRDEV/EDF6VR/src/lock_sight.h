// Included in plugin.cpp's private namespace, after ranger_dual.h and fencer_dual.h.
//
// A lock-on weapon in a VR hand locks on where that hand points, and shows its
// sight: each of a Fencer's two weapons, the shoulder ones as well, and both of
// a Ranger's guns when he has the pair out.
//
// The game (EDF.dll, read offline). A weapon's lock-on cone is weapon+0xC00:
// three rows, right / up / forward, then its place (+0xC30), the half angles
// across and up (+0xC40, +0xC44) and the range (+0xC50). 696710 copies the rows
// from the weapon's fire transform (weapon+0x150) inside the weapon's own tick
// -- from the lock update (6963A0, at 696457) and once more at 694139 -- and
// the search for a target (22E5A0) and the test that keeps one (22DF30) read
// that copy and nothing else: a target is in when it lies ahead of row 2,
// inside the range, and within the half angles of row 2 along rows 0 and 1.
//
// What the fire transform is depends on the weapon. In a Fencer's hand it is
// the game's aim frame carried onto the hand through the model's root, so it
// goes wherever the arm's animation puts the root; on his shoulder it is
// built from the mount's joint, which is not along the aim at all; and a
// Ranger's left gun, which the game has on his back, keeps the back mount's.
// The cone followed all of that. So the copy is corrected instead: right after
// 696710, in a tick that belongs to a VR hand, the rows are that hand's own
// aim frame (WeaponHoldCommand::lockRows). weapon+0x150 itself is not written
// -- writing it for a Ranger's gun wrecked the discharge (HANDOFF, the left
// gun's sight).
//
// The sight. The camera has two sight slots (camera+0x720, 0x1F0 each;
// slot+0x90 is the weapon whose cone and marked targets FC910 collects every
// update and FA060 draws). The game binds them to the soldier's weapon slots,
// one each (FB990: FAE90(slot i, 5A45B0(soldier, i)) for i < soldier+0x1980).
// The Fencer has two weapon slots, so both of his are bound. The Ranger has
// one: his second sight slot is never touched, and with the pair out his left
// gun's sight had nowhere to be. It is bound there, by the game's own setter,
// for as long as the pair is out.
using LockFrame=void(__fastcall*)(void*);
LockFrame g_lockFrameOriginal=nullptr;
// Cones laid on the hand's aim, left / right.
std::atomic<unsigned long long> g_lockFramesLaid[2]{};
void __fastcall HookLockFrame(void* weapon) {
    g_lockFrameOriginal(weapon);
    const auto& tick=g_dualTick;
    if(!tick.active || tick.weapon!=weapon || !tick.command.lockFrame) return;
    const auto& want=tick.command.lockRows;
    for(int i=0;i<3;++i) {
        const float size=want[i][0]*want[i][0]+want[i][1]*want[i][1]+want[i][2]*want[i][2];
        if(!std::isfinite(size) || size<0.81f || size>1.21f) return;
    }
    __try {
        auto* rows=reinterpret_cast<float*>(static_cast<unsigned char*>(weapon)+0xC00);
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) rows[i*4+j]=want[i][j];
        g_lockFramesLaid[tick.command.handIndex<2?tick.command.handIndex:1].fetch_add(1,std::memory_order_relaxed);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// The Ranger's left gun in the camera's second sight slot. Called on the
// camera's update thread before the game's update, which is where FC910 reads
// the slot's weapon; nothing else does (FAE90 apart). The weapon stays there
// only while everything that vouches for it holds this very update: the pair
// is out, the camera's soldier is that Ranger and its own reference to him is
// alive, his input was read a moment ago, and the pair is still his.
using SightBind=void(__fastcall*)(void* slot,void* weapon);
SightBind g_sightBind=nullptr;
void* g_leftSightCamera=nullptr;
void* g_leftSightWeapon=nullptr;
std::atomic<unsigned long long> g_leftSightBinds{0},g_leftSightReleases{0};
std::atomic<bool> g_leftSightBound{false};
constexpr std::size_t kSightSlots=0x720,kSightSlotSize=0x1F0,kSightSlotWeapon=0x90;
void BindLeftSight(void* camera) noexcept {
    if(!g_sightBind || !camera) return;
    __try {
        if(!edf6vr::Readable(camera,kSightSlots+2*kSightSlotSize,true)) return;
        void* want=nullptr;
        if(g_dualActive.load()) {
            const auto state=ReadRangerDual();
            const auto now=GetTickCount64(),input=g_dualInputAt.load();
            auto* const alive=DualAt<unsigned char*>(camera,0x358);
            if(state.weapons[0] && state.soldier && input && now-input<250
               && DualAt<void*>(camera,0x360)==state.soldier
               && edf6vr::Readable(alive,0x10) && DualAt<int>(alive,8)>0
               && CurrentRangerPair(state,state.soldier)) want=state.weapons[0];
        }
        auto* const slot=static_cast<unsigned char*>(camera)+kSightSlots+kSightSlotSize;
        void* const bound=DualAt<void*>(slot,kSightSlotWeapon);
        if(want) {
            // Only into an empty slot, or over our own.
            if(bound!=want && (!bound || (camera==g_leftSightCamera && bound==g_leftSightWeapon))) {
                g_sightBind(slot,want);
                g_leftSightBinds.fetch_add(1,std::memory_order_relaxed);
            }
            if(DualAt<void*>(slot,kSightSlotWeapon)==want) { g_leftSightCamera=camera; g_leftSightWeapon=want; g_leftSightBound=true; }
        } else if(camera==g_leftSightCamera) {
            // Ours still, and not a slot the game fills itself (a soldier with two weapon slots).
            if(g_leftSightWeapon && bound==g_leftSightWeapon && edf6vr::WeaponSlotCount(DualAt<void*>(camera,0x360))<2) {
                g_sightBind(slot,nullptr);
                g_leftSightReleases.fetch_add(1,std::memory_order_relaxed);
            }
            g_leftSightCamera=nullptr; g_leftSightWeapon=nullptr; g_leftSightBound=false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// 0: nothing installed. Bit 0: the cone's rows; bit 1: the Ranger's left sight.
unsigned InstallLockSight() noexcept {
    unsigned done=0;
    __try {
        auto* base=g_image.base;
        // 696710 as read: movaps xmm0,[rcx+150]; mov rdx,rcx; movaps [rcx+C00],xmm0.
        static const unsigned char copy[]={0x0F,0x28,0x81,0x50,0x01,0x00,0x00,0x48,0x8B,0xD1,0x0F,0x29,0x81,0x00,0x0C,0x00,0x00};
        if(base && edf6vr::Readable(base+0x696710,sizeof(copy)) && !std::memcmp(base+0x696710,copy,sizeof(copy))) {
            g_lockFrameOriginal=reinterpret_cast<LockFrame>(base+0x696710);
            bool changed=false;
            if(edf6vr::RedirectCall(base+0x694139,base+0x696710,reinterpret_cast<void*>(&HookLockFrame),changed)
               && edf6vr::RedirectCall(base+0x696457,base+0x696710,reinterpret_cast<void*>(&HookLockFrame),changed)) done|=1;
        }
        // FAE90 as read: mov [rsp+18],rbp; push rsi; sub rsp,20; mov rbp,rdx; mov rsi,rcx; cmp [rcx+90],rdx.
        // FB9A4, the game's own use of it: imul rcx,rax,1F0; add rcx,720; add rcx,rbx; call FAE90.
        // FC947, the one reader: mov rdx,[rcx+90]; test rdx,rdx; je; cmp byte [rdx+BF0],0.
        static const unsigned char bind[]={0x48,0x89,0x6C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xEA,0x48,0x8B,0xF1,0x48,0x39,0x91,0x90,0x00,0x00,0x00};
        static const unsigned char slots[]={0x48,0x69,0xC8,0xF0,0x01,0x00,0x00,0x48,0x81,0xC1,0x20,0x07,0x00,0x00,0x48,0x03,0xCB,0xE8};
        static const unsigned char reader[]={0x48,0x8B,0x91,0x90,0x00,0x00,0x00,0x48,0x85,0xD2};
        if(base && edf6vr::Readable(base+0xFAE90,sizeof(bind)) && !std::memcmp(base+0xFAE90,bind,sizeof(bind))
           && !std::memcmp(base+0xFB9A4,slots,sizeof(slots))
           && base+0xFB9BA+*reinterpret_cast<const std::int32_t*>(base+0xFB9B6)==base+0xFAE90
           && !std::memcmp(base+0xFC947,reader,sizeof(reader))) {
            g_sightBind=reinterpret_cast<SightBind>(base+0xFAE90);
            done|=2;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return done;
}
void LogLockSight() noexcept {
    const auto left=g_lockFramesLaid[0].load(),right=g_lockFramesLaid[1].load(),binds=g_leftSightBinds.load();
    if(!left && !right && !binds) return;
    Log("LOCKSIGHT lock-on cones laid on the hand's aim L/R=%llu/%llu ticks; the Ranger's left sight in the second slot: %s (bound %llu, released %llu)",
        left,right,g_leftSightBound.load()?"now":"not now",binds,g_leftSightReleases.load());
}
