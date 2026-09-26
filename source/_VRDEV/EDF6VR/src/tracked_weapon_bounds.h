// AnimationModel's normal OBB submission, before Umbra resolves visibility.
// Match the OBB to the tracked render palette, without disabling world culling.
using TrackedBounds=void(__fastcall*)(void*,const edf6vr::Matrix*);
TrackedBounds g_trackedBounds=nullptr;
std::atomic<unsigned long long> g_boundsCandidates{0},g_boundsCarried{0};
bool PrepareTrackedBounds(void* model,const edf6vr::Matrix* source,edf6vr::Matrix& wanted) noexcept {
    WeaponHoldCommand command{};
    AcquireSRWLockShared(&g_actionVisibilityLock);
    for(const auto& hand:g_actionHands)if(model && model==hand.model){command=hand;break;}
    ReleaseSRWLockShared(&g_actionVisibilityLock);
    __try {
        if(!command.weapon || !ValidateHoldCommand(command,model)
           || !edf6vr::Readable(source,64) || !edf6vr::Readable(command.weapon,0x128)
           || *reinterpret_cast<void**>(static_cast<unsigned char*>(command.weapon)+0x120)!=command.soldier)return false;
        const bool left=command.handIndex==0;
        // A Fencer's left weapon is a slot weapon like the right: without this
        // its bounds stayed on the game's mount, and a step that swung the
        // native left arm out of view had Umbra cull the carried weapon (log:
        // carried was exactly half of candidates, the left never).
        const bool allowed=left ? (RangerDualWeapon(command.weapon) || (command.fencer && KeepActionWeaponModel(command.weapon)))
                                : KeepActionWeaponModel(command.weapon);
        if(!allowed)return false;
        auto soldier=static_cast<unsigned char*>(command.soldier);
        const auto root=*reinterpret_cast<const edf6vr::Matrix*>(static_cast<unsigned char*>(command.weaponNodes)+0xB0);
        float travel=0,reach=0;
        for(int j=0;j<3;++j) {
            const float current=*reinterpret_cast<const float*>(soldier+0x90+j*4);
            const float step=current-command.rootWorld[j],local=command.hand.palm[j]-command.rootWorld[j];
            travel+=step*step;reach+=local*local;command.hand.palm[j]=current+local;
        }
        if(!std::isfinite(travel) || !std::isfinite(reach) || travel>=400 || reach>=25)return false;
        float before=0,after=0;
        return edf6vr::CarryWeaponBones(root,command.hand,source,1,&wanted,before,after);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void __fastcall HookTrackedBounds(void* model,const edf6vr::Matrix* source) {
    // Almost every invocation is an unrelated NPC/world model.
    if(model && (model==g_fastDrawWeapon.load(std::memory_order_acquire)
       || model==g_fastDrawLeft.load(std::memory_order_acquire))) {
        ++g_boundsCandidates;
        edf6vr::Matrix wanted{};
        if(PrepareTrackedBounds(model,source,wanted)) {
            ++g_boundsCarried;g_trackedBounds(model,&wanted);return;
        }
    }
    g_trackedBounds(model,source);
}
bool InstallTrackedBounds(bool& changed) noexcept {
    changed=false;
    constexpr unsigned char site[]={0x48,0x8D,0x96,0x10,0x01,0,0,0x48,0x8B,0xCE,
        0xE8,0xC9,0x29,0xAF,0x00,0x0F,0x10,0x86,0x40,0x01,0,0};
    if(!g_image.base || std::memcmp(g_image.base+0x6C06E8,site,sizeof site))return false;
    g_trackedBounds=reinterpret_cast<TrackedBounds>(g_image.base+0x11B30C0);
    return edf6vr::RedirectCall(g_image.base+0x6C06F2,reinterpret_cast<void*>(g_trackedBounds),
        reinterpret_cast<void*>(&HookTrackedBounds),changed);
}
