// Included by plugin.cpp. Only the animation visibility CALL is intercepted;
// inventory switching and weapon gameplay visibility still run unchanged.
using ActionVisibility=void(__fastcall*)(void*,bool);
ActionVisibility g_actionVisibility=nullptr,g_actionModelVisibility=nullptr;
SRWLOCK g_actionVisibilityLock=SRWLOCK_INIT;
WeaponHoldCommand g_actionHands[2]{};
std::atomic<unsigned long long> g_actionHideCalls{0},g_actionModelsKept{0};
bool ValidateHoldCommand(const WeaponHoldCommand&,void*) noexcept;

void PublishActionHands(bool enabled) noexcept {
    AcquireSRWLockExclusive(&g_actionVisibilityLock);
    g_actionHands[0]=enabled?g_leftHoldCommand:WeaponHoldCommand{};
    g_actionHands[1]=enabled?g_holdCommand:WeaponHoldCommand{};
    ReleaseSRWLockExclusive(&g_actionVisibilityLock);
}
bool KeepActionWeaponModel(void* weapon) noexcept {
    WeaponHoldCommand command{};
    AcquireSRWLockShared(&g_actionVisibilityLock);
    for(const auto& hand:g_actionHands) if(weapon && weapon==hand.weapon) {command=hand;break;}
    ReleaseSRWLockShared(&g_actionVisibilityLock);
    __try {
        if(!weapon || command.weapon!=weapon || !ValidateHoldCommand(command,command.model)
           || command.model!=static_cast<unsigned char*>(weapon)+edf6vr::kWeaponModelOffset
           || !edf6vr::Readable(weapon,0x128)) return false;
        auto soldier=static_cast<unsigned char*>(command.soldier);
        const auto health=*reinterpret_cast<const float*>(soldier+0x2F8);
        if(!std::isfinite(health) || health<=0 || *reinterpret_cast<const unsigned*>(soldier+0x39C)==3
           || *reinterpret_cast<void**>(static_cast<unsigned char*>(weapon)+0x120)!=soldier) return false;
        // This CALL is for the equipped slot, never for the stowed weapon.
        // Recheck that slot even if the 250ms snapshot predates a weapon switch.
        const unsigned count=edf6vr::WeaponSlotCount(soldier);
        auto slots=*reinterpret_cast<unsigned char**>(soldier+0x1970);
        for(unsigned i=0;i<count;++i) {
            auto wrapper=*reinterpret_cast<void***>(slots+i*0x150+0x40);
            if(edf6vr::Readable(wrapper,8) && *wrapper==weapon) return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
void __fastcall HookActionWeaponVisibility(void* weapon,bool visible) {
    const bool keep=!visible && KeepActionWeaponModel(weapon);
    if(!visible)g_actionHideCalls.fetch_add(1,std::memory_order_relaxed);
    // Preserve +78C, native subclass callbacks, ammo-dependent presentation and
    // all animation/gameplay flags. Restore only the model's rendering gate.
    g_actionVisibility(weapon,visible);
    if(keep) {
        g_actionModelVisibility(static_cast<unsigned char*>(weapon)+edf6vr::kWeaponModelOffset,true);
        g_actionModelsKept.fetch_add(1,std::memory_order_relaxed);
    }
}
bool InstallActionVisibility(bool& changed) noexcept {
    changed=false;
    constexpr unsigned char site[]={0x48,0x8B,0x09,0x40,0x0F,0xB6,0xD6,0xE8,0x65,0xB6,0x0F,0x00,0xFF,0xC7};
    constexpr unsigned char modelCall[]={0x40,0x0F,0xB6,0xD7,0xE8,0x2B,0xA3,0x02,0x00};
    if(!g_image.base || std::memcmp(g_image.base+0x59AADF,site,sizeof site)
       || std::memcmp(g_image.base+0x69617C,modelCall,sizeof modelCall)) return false;
    g_actionVisibility=reinterpret_cast<ActionVisibility>(g_image.base+0x696150);
    g_actionModelVisibility=reinterpret_cast<ActionVisibility>(g_image.base+0x6C04B0);
    return edf6vr::RedirectCall(g_image.base+0x59AAE6,reinterpret_cast<void*>(g_actionVisibility),
        reinterpret_cast<void*>(&HookActionWeaponVisibility),changed);
}
