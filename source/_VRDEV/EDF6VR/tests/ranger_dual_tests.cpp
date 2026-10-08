// Production hooks against synthetic owners. Native holster code is also run
// against these fixtures, with only its external effect cleanup stubbed out.
#include "pad_capture.h"
#define SetPadState CaptureTestPad
#include "../src/plugin.cpp"
#undef SetPadState
#include <limits>
#include <array>
static int failures=0,ticks=0,poses=0,zoomCalls=0,holsterCalls=0,draws=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
alignas(16) static unsigned char soldier[0x2200]{},otherSoldier[0x2200]{},weapons[3][0x2000]{},slots[0x150]{},body[0x220]{},bones[2][0x220]{},attachments[2][0xF0]{};
static void* owned[3]{};
static bool expectDual=false,raiseTick=false;
static bool burstMode=false;
static unsigned switchSounds=0;
static void __fastcall FakeSwitchSound(void* manager,void* source,const wchar_t* key,void* extra) {
    CHECK(manager==otherSoldier && source==soldier+0x90 && extra==nullptr);
    CHECK(!std::wcscmp(key,L"\u6B66\u5668\u5207\u308A\u66FF\u3048"));
    const bool unlocked=TryAcquireSRWLockExclusive(&g_lock)!=0;
    CHECK(unlocked);if(unlocked)ReleaseSRWLockExclusive(&g_lock);
    ++switchSounds;
}
static void CheckSwitchSound() {
    auto managerSlot=reinterpret_cast<void**>(g_image.base+0x20B2950);
    auto oldManager=*managerSlot;bool changed=false;
    CHECK(edf6vr::ReplacePointer(managerSlot,oldManager,otherSoldier,changed));
    g_dualSwitchSound=FakeSwitchSound;
    unsigned char savedWeapons[sizeof weapons],savedSlots[sizeof slots];
    std::memcpy(savedWeapons,weapons,sizeof weapons);std::memcpy(savedSlots,slots,sizeof slots);
    PlayRangerDualSwitchSound(soldier);CHECK(switchSounds==1);
    CHECK(!std::memcmp(savedWeapons,weapons,sizeof weapons) && !std::memcmp(savedSlots,slots,sizeof slots));
    CHECK(edf6vr::ReplacePointer(managerSlot,otherSoldier,nullptr,changed));
    PlayRangerDualSwitchSound(soldier);CHECK(switchSounds==1);
    CHECK(edf6vr::ReplacePointer(managerSlot,nullptr,oldManager,changed));
    g_dualSwitchSound=nullptr;
}
static int burstShots[2]{};
static DualZoom nativeTestHolster=nullptr;
static void __fastcall FakeEffectCleanup(void*) {}
static int visibleChanges=0,fireCalls=0,spreadCalls=0;
static bool raiseFire=false,expectTrackedShot=true;
static float coneSeen=0;
static void __fastcall FakeVisibility(void* weapon,bool visible) {
    ++visibleChanges;CHECK(weapon==weapons[0]);
    DualAt<unsigned char>(weapon,0x78C)=visible?1:0;
    DualAt<unsigned char>(weapon,0xF30+0x480)=visible?1:0;
}
static void* __fastcall FakeSpread(void* output,const float*,float minimum,float maximum,std::uint64_t*) {
    ++spreadCalls;CHECK(minimum==0);coneSeen=maximum;return output;
}
static float __fastcall TestSin(float x) {return std::sin(x);}
static float __fastcall TestCos(float x) {return std::cos(x);}
static float __fastcall TestSqrt(float x) {return std::sqrt(x);}
static float __fastcall TestAtan2(float y,float x) {return std::atan2(y,x);}
static int nativeVisibilityChanges=0;
static void __fastcall TestChildVisibility(void*,unsigned index,bool) {CHECK(index==0);++nativeVisibilityChanges;}
static void BindMath(unsigned rva,void* target) {
    auto slot=reinterpret_cast<void**>(g_image.base+rva);bool changed=false;
    CHECK(edf6vr::ReplacePointer(slot,*slot,target,changed));
}
static void CheckNativeAccuracyAndVisibility() {
    // Resolve only four math imports used by the original native accuracy
    // routine. No game process, scripts, renderer or audio engine is started.
    BindMath(0x1756400,reinterpret_cast<void*>(&TestCos));
    BindMath(0x1756408,reinterpret_cast<void*>(&TestSin));
    BindMath(0x1756438,reinterpret_cast<void*>(&TestSqrt));
    BindMath(0x1756450,reinterpret_cast<void*>(&TestAtan2));
    g_dualSpread=reinterpret_cast<DualSpread>(g_image.base+0x4E820);
    const float dir[4]={0,0,1,0};float native[4]{},wrapped[4]{};
    std::uint64_t seed=17,other=17;
    g_dualShot={};g_dualSpread(native,dir,0,.01f,&seed);
    CHECK(HookDualSpread(wrapped,dir,0,.01f,&other)==wrapped);
    CHECK(!std::memcmp(native,wrapped,sizeof native) && seed==other); // single wield exact
    float largest=0;
    g_dualShot={true,false,0,1};
    for(int i=0;i<128;++i) {
        HookDualSpread(wrapped,dir,0,0,&other);
        const float norm=std::sqrt(wrapped[0]*wrapped[0]+wrapped[1]*wrapped[1]+wrapped[2]*wrapped[2]);
        CHECK(std::fabs(norm-1)<.0001f);
        const float angle=std::acos(std::clamp(wrapped[2]/norm,-1.0f,1.0f));
        CHECK(angle<=.34908f);largest=std::max(largest,angle);
    }
    CHECK(largest>.30f);g_dualShot={}; // reaches beyond the former 12-degree cap
    // Exercise actual 696150 -> 6C04B0; stub only scene-node notification.
    bool changed=false;
    auto notify=reinterpret_cast<void**>(g_image.base+0x17E36E0+0x60);
    CHECK(edf6vr::ReplacePointer(notify,*notify,reinterpret_cast<void*>(&FakeEffectCleanup),changed));
    CHECK(edf6vr::RedirectCall(g_image.base+0x6C04CF,g_image.base+0x11B3480,
        reinterpret_cast<void*>(&TestChildVisibility),changed));
    DualAt<unsigned char>(weapons[0],0x78C)=1;
    DualAt<unsigned char>(weapons[0],0xF30+0x31C)=1;
    DualAt<unsigned char>(weapons[0],0xF30+0x480)=1;
    g_dualVisibility(weapons[0],false);
    CHECK(!DualAt<unsigned char>(weapons[0],0x78C) && !DualAt<unsigned char>(weapons[0],0xF30+0x480));
    g_dualVisibility(weapons[0],true);
    CHECK(DualAt<unsigned char>(weapons[0],0x78C)==1 && DualAt<unsigned char>(weapons[0],0xF30+0x480)==1);
    CHECK(nativeVisibilityChanges==2);
}
static unsigned audioWarnings=0;
static unsigned writableResult=0,writableCalls=0,obbCalls=0;
static edf6vr::Matrix observedObb{};
static unsigned __fastcall TestWritable(void*) {++writableCalls;return writableResult;}
static void __fastcall TestObb(void* object,const edf6vr::Matrix* matrix) {
    CHECK(object==otherSoldier);++obbCalls;observedObb=*matrix;
}
static void CheckTrackedBoundsAndOutput() {
    bool changed=false;
    CHECK(InstallTrackedBounds(changed) && changed);CHECK(!InstallTrackedBounds(changed) && !changed);
    auto slot=reinterpret_cast<void**>(g_image.base+0x17567D0);
    CHECK(edf6vr::ReplacePointer(slot,*slot,reinterpret_cast<void*>(&TestObb),changed));
    auto model=weapons[1]+edf6vr::kWeaponModelOffset;
    DualAt<void*>(model,0x10)=otherSoldier;
    g_fastDrawWeapon=model;g_holdCommand.hand={{{1,0,0},{0,1,0},{0,0,1}},{1,2,3}};
    for(int j=0;j<3;++j){g_holdCommand.rootWorld[j]=0;DualAt<float>(soldier,0x90+j*4)=0;}
    for(int roll=0;roll<360;roll+=15) {
        const float a=roll*3.14159265f/180,c=std::cos(a),s=std::sin(a);
        const edf6vr::Matrix root={{{c,s,0,0},{-s,c,0,0},{0,0,1,0},{0,-1,0,1}}};
        DualAt<edf6vr::Matrix>(bones[1],0xB0)=root;
        edf6vr::Matrix native=root;for(int j=0;j<3;++j){native.m[0][j]*=2;native.m[1][j]*=3;native.m[2][j]*=4;}
        native.m[3][0]=.2f*c-.1f*s;native.m[3][1]=-1+.2f*s+.1f*c;native.m[3][2]=.3f;
        const auto saved=native;
        g_holdCommand.refreshed=GetTickCount64();PublishActionHands(true);
        // Real 11B30C0 forwards OBB to stub Umbra and updates model center/radius.
        HookTrackedBounds(model,&native);
        CHECK(std::fabs(observedObb.m[3][0]-1.2f)<.0001f && std::fabs(observedObb.m[3][1]-2.1f)<.0001f
            && std::fabs(observedObb.m[3][2]-3.3f)<.0001f);
        CHECK(std::fabs(DualAt<float>(model,0x54)-2.1f)<.0001f && std::fabs(DualAt<float>(model,0x64)-std::sqrt(29.0f))<.0001f);
        CHECK(!std::memcmp(&native,&saved,64) && !std::memcmp(bones[1]+0xB0,&root,64));
    }
    CHECK(obbCalls==24 && g_boundsCarried.load()==24);
    edf6vr::Matrix output{};const auto root=DualAt<edf6vr::Matrix>(bones[1],0xB0);
    PublishActionHands(false);CHECK(!PrepareTrackedBounds(model,&root,output));
    CHECK(InstallAudioOutputHealth(changed) && changed);CHECK(!InstallAudioOutputHealth(changed) && !changed);
    g_audioWritable=TestWritable;
    alignas(16) unsigned char backend[0x60]{};
    DualAt<void*>(backend,0x40)=otherSoldier;DualAt<unsigned>(backend,0x58)=3200;DualAt<unsigned>(backend,0x1C)=48000;
    writableResult=800;CHECK(HookAudioWritable(backend)==800 && g_outputEmpty.load()==0);
    writableResult=3200;CHECK(HookAudioWritable(backend)==3200 && g_outputEmpty.load()==1);
    writableResult=~0u;CHECK(HookAudioWritable(backend)==~0u && g_outputInvalid.load()==1);
    DualAt<unsigned char>(backend,5)=1;writableResult=0;CHECK(HookAudioWritable(backend)==0 && g_outputInvalid.load()==2);
    CHECK(writableCalls==4 && g_outputCalls.load()==4);
}
static void __fastcall TestAudioWarning(unsigned level,const char* text) {
    CHECK(level==1 && text==reinterpret_cast<const char*>(g_image.base+0x18F67A0));
    CHECK(TryAcquireSRWLockExclusive(&g_lock)!=0);ReleaseSRWLockExclusive(&g_lock);
    ++audioWarnings;
}
static void CheckActionVisibilityAndAudioHealth() {
    bool changed=false;
    CHECK(InstallActionVisibility(changed) && changed);
    CHECK(!InstallActionVisibility(changed) && !changed);
    // Use the real native visibility functions with the existing notification
    // stub. Gameplay visibility remains false while only model visibility is on.
    g_holdCommand={};g_holdCommand.weapon=weapons[1];g_holdCommand.model=weapons[1]+edf6vr::kWeaponModelOffset;
    g_holdCommand.soldier=soldier;g_holdCommand.objectId=17;g_holdCommand.count=2;
    g_holdCommand.weaponNodes=bones[1];g_holdCommand.bodyNodes=body;g_holdCommand.armsNode=body;
    g_leftHoldCommand={};
    DualAt<float>(soldier,0x2F8)=100;
    DualAt<unsigned char>(weapons[1],0xF30+0x31C)=1;
    auto model=weapons[1]+edf6vr::kWeaponModelOffset;
    const auto ammo=DualAt<int>(weapons[1],0xBE8),reload=DualAt<int>(weapons[1],0xE68);
    for(unsigned table:{0x17CDF28u,0x17D0FF8u,0x17CF100u}) {
        DualAt<void*>(soldier,0)=g_image.base+table;
        g_holdCommand.refreshed=GetTickCount64();PublishActionHands(true);
        CHECK(KeepActionWeaponModel(weapons[1]));
        // Resolve and invoke the installed CALL's actual branch island.
        const auto site=g_image.base+0x59AAE6;
        auto target=site+5+*reinterpret_cast<const std::int32_t*>(site+1);
        reinterpret_cast<ActionVisibility>(target)(weapons[1],false);
        CHECK(!DualAt<unsigned char>(weapons[1],0x78C) && DualAt<unsigned char>(model,0x480));
        CHECK(DualAt<int>(weapons[1],0xBE8)==ammo && DualAt<int>(weapons[1],0xE68)==reload);
    }
    CHECK(g_actionModelsKept.load()==3);
    CHECK(!KeepActionWeaponModel(weapons[0])); // stowed/NPC pointer
    DualAt<void*>(slots,0x40)=owned;CHECK(!KeepActionWeaponModel(weapons[1]));DualAt<void*>(slots,0x40)=owned+1;
    DualAt<unsigned>(soldier,0x314)=18;CHECK(!KeepActionWeaponModel(weapons[1]));DualAt<unsigned>(soldier,0x314)=17;
    DualAt<void*>(weapons[1],0x120)=otherSoldier;CHECK(!KeepActionWeaponModel(weapons[1]));DualAt<void*>(weapons[1],0x120)=soldier;
    DualAt<float>(soldier,0x2F8)=0;CHECK(!KeepActionWeaponModel(weapons[1]));DualAt<float>(soldier,0x2F8)=100;
    DualAt<unsigned>(soldier,0x39C)=3;CHECK(!KeepActionWeaponModel(weapons[1]));DualAt<unsigned>(soldier,0x39C)=0;
    DualAt<void*>(soldier,0)=g_image.base+0x17CF5B8;CHECK(!KeepActionWeaponModel(weapons[1]));
    DualAt<void*>(soldier,0)=g_image.base+0x17CDF28;
    g_holdCommand.refreshed=1;PublishActionHands(true);CHECK(!KeepActionWeaponModel(weapons[1]));
    g_holdCommand.refreshed=GetTickCount64();PublishActionHands(true);CHECK(KeepActionWeaponModel(weapons[1]));
    PublishActionHands(false);HookActionWeaponVisibility(weapons[1],false);
    CHECK(!DualAt<unsigned char>(model,0x480) && g_actionModelsKept.load()==3);
    HookActionWeaponVisibility(weapons[1],true);CHECK(DualAt<unsigned char>(model,0x480));
    CHECK(InstallAudioHealth(changed) && changed);CHECK(!InstallAudioHealth(changed) && !changed);
    g_audioWarning=TestAudioWarning;
    const auto site=g_image.base+0x106C0B6;
    auto target=site+5+*reinterpret_cast<const std::int32_t*>(site+1);
    reinterpret_cast<AudioWarning>(target)(1,reinterpret_cast<const char*>(g_image.base+0x18F67A0));
    CHECK(audioWarnings==1 && g_audioUnderruns.load()==1);
}
static void __fastcall FakeFire(void* weapon,unsigned index,void* alternate,void* counter,bool consume) {
    ++fireCalls;CHECK(index==0 && !alternate && counter==soldier && consume);
    const unsigned h=weapon==weapons[0]?0:1;
    if(expectTrackedShot) {
        const auto& matrix=DualAt<edf6vr::Matrix>(attachments[h],0x50);
        const auto& hand=g_dualState.hands[h].hand;
        for(unsigned i=0;i<3;++i) {
            CHECK(std::fabs(matrix.m[3][i]-hand.palm[i])<.0001f);
            for(unsigned j=0;j<3;++j)CHECK(std::fabs(matrix.m[i][j]-hand.axes[i][j])<.0001f);
        }
    }
    const float forward[4]={0,0,1,0};std::uint64_t rng=17;float out[16]{};
    // Many pellets in a single discharge must contribute only one recoil event.
    for(int i=0;i<10;++i)CHECK(HookDualSpread(out,forward,0,.01f,&rng)==out);
    if(raiseFire)RaiseException(0xE0423624,0,0,nullptr);
}
static edf6vr::Matrix palette[2][2]{};
static void __fastcall FakeHolster(void* weapon) {
    ++holsterCalls;CHECK(weapon==weapons[0]);CHECK(DualAt<unsigned char>(weapon,0x13E)==1);
    if(nativeTestHolster)nativeTestHolster(weapon);
    else {DualAt<int>(weapon,0xE18)=0;DualAt<unsigned char>(weapon,0x13E)=0;}
}
static void __fastcall FakeDraw(void* model,void*,int,void*) {
    ++draws;const unsigned h=model==weapons[0]+edf6vr::kWeaponModelOffset?0:1;
    const auto& expected=g_dualState.hands[h].hand;
    for(unsigned j=0;j<3;++j) CHECK(std::fabs(palette[h][0].m[3][j]-expected.palm[j])<.0001f);
}
static int __fastcall FakeIndex(void* slot,int index) {CHECK(slot==slots);return index;}
static void __fastcall FakeZoom(void* weapon) {++zoomCalls;DualAt<unsigned char>(weapon,0x780)=0;}
static void __fastcall FakePose(void* attachment,void* matrix) {
    ++poses;
    auto weapon=static_cast<unsigned char*>(matrix)-0x150;
    const unsigned h=weapon==weapons[0]?0:1;
    const auto source=DualAt<edf6vr::Matrix>(bones[h],0xB0);
    DualAt<edf6vr::Matrix>(attachment,0x50)=source;DualAt<edf6vr::Matrix>(attachment,0x90)=source;
}
static void __fastcall FakeTick(void* weapon,void* context) {
    ++ticks;CHECK(context==soldier);
    // Native callbacks may take these locks: neither may be held across forward.
    CHECK(TryAcquireSRWLockExclusive(&g_dualLock)!=0);ReleaseSRWLockExclusive(&g_dualLock);
    CHECK(TryAcquireSRWLockExclusive(&g_lock)!=0);ReleaseSRWLockExclusive(&g_lock);
    const unsigned h=weapon==weapons[0]?0:1;
    if(expectDual) {
        CHECK(DualAt<unsigned char>(weapon,0xE6E)==1);
        CHECK(DualAt<unsigned char>(weapon,0x13E)==1);
        CHECK(DualAt<unsigned char>(weapon,0x13B)==0);
        // Some subclasses overwrite E6E before their base. The pose call is
        // inside that base, immediately before the proven reload branch.
        DualAt<unsigned char>(weapon,0xE6E)=0;
        HookDualPose(attachments[h],static_cast<unsigned char*>(weapon)+0x150);
        CHECK(DualAt<unsigned char>(weapon,0xE6E)==1);
        const auto& m=DualAt<edf6vr::Matrix>(attachments[h],0x50);
        CHECK(!std::memcmp(&m,bones[h]+0xB0,64)); // animation/model updates retain native pose
    }
    if(burstMode) {
        // Independent scheduled-fire fixture: trigger release must not stop an
        // already scheduled burst. The wrapper must never restore its counters.
        auto& pending=DualAt<int>(weapon,0xE18);
        auto& ammo=DualAt<int>(weapon,0xBE8);
        const auto trigger=DualAt<unsigned char>(weapon,0x139);
        if(trigger && !DualAt<unsigned char>(weapon,0x13A) && pending==0 && ammo>0)pending=3;
        DualAt<unsigned char>(weapon,0x13A)=trigger;
        if(pending>0 && ammo>0) {--pending;--ammo;++burstShots[h];}
        if(!DualAt<unsigned char>(weapon,0xE6E) && DualAt<unsigned char>(weapon,0x13E))--DualAt<int>(weapon,0xE68);
        DualAt<unsigned char>(weapon,0x139)=0;
        return;
    }
    // Model native ammo/update progress independently: pausing the whole update or
    // restoring BE8/E18 afterward would fail these assertions.
    if(DualAt<unsigned char>(weapon,0x139) && DualAt<int>(weapon,0xBE8)>0) --DualAt<int>(weapon,0xBE8);
    --DualAt<int>(weapon,0xE18);
    if(!DualAt<unsigned char>(weapon,0xE6E) && DualAt<unsigned char>(weapon,0x13E)) --DualAt<int>(weapon,0xE68);
    DualAt<unsigned char>(weapon,0x139)=0;
    if(raiseTick) RaiseException(0xE0423623,0,0,nullptr);
}
static void CheckTickException() {
    bool caught=false;raiseTick=true;
    __try {HookDualWeaponTick(weapons[0],soldier);}
    __except(GetExceptionCode()==0xE0423623?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    raiseTick=false;CHECK(caught);CHECK(!g_dualTick.weapon && !g_dualTick.active);
    CHECK(DualAt<unsigned char>(weapons[0],0xE6E)==0);CHECK(DualAt<unsigned char>(weapons[0],0x13E)==0);
}
static WeaponHoldCommand Command(unsigned h) {
    WeaponHoldCommand c{};c.handIndex=h;c.soldier=soldier;c.objectId=17;
    c.weapon=weapons[h];c.model=weapons[h]+edf6vr::kWeaponModelOffset;c.weaponNodes=bones[h];c.count=2;
    c.armsNode=body;c.bodyNodes=body;c.refreshed=GetTickCount64();
    c.hand.axes[0][h]=1;c.hand.axes[1][2]=1;c.hand.axes[2][1-h]=h==0?-1.0f:1.0f;
    c.hand.palm[0]=h==0?-1.0f:1.0f;c.hand.palm[1]=1.6f;c.hand.palm[2]=3;
    return c;
}
static void CheckFireBoundary(RangerDualState state) {
    g_dualState=state;g_dualActive=true;g_dualFire=FakeFire;g_dualSpread=FakeSpread;
    for(unsigned h=0;h<2;++h) {
        g_dualRecoil[h]={};g_dualFires[h]=0;
        g_dualTick={state.hands[h],weapons[h],true};
        // Reproduce native muzzle rebuild AFTER HookDualPose. Old code fired
        // using this original matrix and never reached the left destination.
        FakePose(attachments[h],weapons[h]+0x150);
        const auto native=DualAt<edf6vr::Matrix>(attachments[h],0x50);
        HookDualFire(weapons[h],0,nullptr,soldier,true);
        CHECK(coneSeen==.01f && g_dualFires[h].load()==1);
        CHECK(!std::memcmp(&native,attachments[h]+0x50,64) && !g_dualShot.active);
        HookDualFire(weapons[h],0,nullptr,soldier,true);
        CHECK(coneSeen>.01f && g_dualFires[h].load()==2);
        CHECK(!std::memcmp(&native,attachments[h]+0x50,64));
    }
    CHECK(fireCalls==4 && spreadCalls==40);
    bool caught=false;raiseFire=true;
    const auto native=DualAt<edf6vr::Matrix>(attachments[1],0x50);
    __try {HookDualFire(weapons[1],0,nullptr,soldier,true);}
    __except(GetExceptionCode()==0xE0423624?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    raiseFire=false;CHECK(caught && !g_dualShot.active);
    CHECK(!std::memcmp(&native,attachments[1]+0x50,64));
    g_dualTick={};g_dualActive=false;expectTrackedShot=false;
    HookDualFire(weapons[0],0,nullptr,soldier,true);CHECK(coneSeen==.01f);
    expectTrackedShot=true;
    for(unsigned h=0;h<2;++h)DualAt<unsigned char>(weapons[h],0xE6E)=0;
}
// hand_aim_sync.h (the sending half; EDF6MultiSlot turns others' shots): the
// slot, the turn of a muzzle's rows, and the shot-by-shot send hook on the real
// call.
static edf6vr::Matrix HandAimTestIdentity() {edf6vr::Matrix m{};for(int i=0;i<4;++i)m.m[i][i]=1;return m;}
static void CheckHandAimSync() {
    alignas(16) static unsigned char weapon[0x1000]{},owner[0x2000]{};
    static void* list[3]{};
    const float local[3]={0,0,1};std::memcpy(weapon+0x350,local,sizeof local);
    list[0]=weapon+0x800;list[1]=weapon;list[2]=weapon+0x900;   // the weapon is second in its owner's list
    DualAt<void**>(owner,0x1950)=list;DualAt<std::uint64_t>(owner,0x1960)=3;
    CHECK(HandAimSlot(owner,weapon)==1 && HandAimSlot(owner,weapon+0x10)==-1);
    // The turn PrepareLeftFireTurn's direction is put on with: rows onto it, the point kept.
    edf6vr::Matrix m=HandAimTestIdentity();m.m[3][0]=1.5f;
    const float from[3]={0,0,1},to[3]={.6f,0,.8f};
    CHECK(HandAimTurnRows(m,from,to));
    float got[3]{};CHECK(HandAimFireDirection(weapon,m,got));
    CHECK(std::fabs(got[0]-.6f)<1e-4f && std::fabs(got[1])<1e-4f && std::fabs(got[2]-.8f)<1e-4f);
    CHECK(m.m[3][0]==1.5f);
    // The shot-by-shot send hook goes on the real call at 690D3B (E8 -> 694910), once.
    CHECK(g_image.base[0x690D3B]==0xE8);
    CHECK(InstallHandAimSend());
    CHECK(g_shotSendOriginal==reinterpret_cast<ShotSendFn>(g_image.base+0x694910));
    CHECK(!InstallHandAimSend());   // the call no longer reaches 694910 directly
    // The catch-up calls stay as HookDualFire left them: EDF6MultiSlot's mid-hooks sit around them.
    CHECK(g_image.base[0x6904F7]==0xE8 && g_image.base[0x690603]==0xE8);
    // A send away from the tick: both matrices' rows turned onto the hand's
    // direction by the first one's arc, both points kept.
    alignas(16) static unsigned char entry[0xF0]{};
    edf6vr::Matrix first=HandAimTestIdentity(),second=HandAimTestIdentity();
    first.m[3][0]=2;first.m[3][1]=1;second.m[3][2]=-3;
    std::memcpy(entry+0x50,&first,sizeof first);std::memcpy(entry+0x90,&second,sizeof second);
    CHECK(HandAimTurnEntry(weapon,entry,to));
    edf6vr::Matrix turnedFirst{},turnedSecond{};
    std::memcpy(&turnedFirst,entry+0x50,sizeof turnedFirst);std::memcpy(&turnedSecond,entry+0x90,sizeof turnedSecond);
    CHECK(HandAimFireDirection(weapon,turnedFirst,got));
    CHECK(std::fabs(got[0]-.6f)<1e-4f && std::fabs(got[2]-.8f)<1e-4f);
    CHECK(std::fabs(turnedSecond.m[2][0]-.6f)<1e-4f && std::fabs(turnedSecond.m[2][2]-.8f)<1e-4f);
    CHECK(turnedFirst.m[3][0]==2 && turnedFirst.m[3][1]==1 && turnedSecond.m[3][2]==-3);
    // Which Fencer weapons give no direction (they strike only on the animation
    // event, and their hits stay the game's) and which are never turned away
    // from the tick (shot_origin.h's sweeps), by the real vtables.
    static void* katana[2]={g_image.base+0x17e5950};
    static void* spear[2]={g_image.base+0x17e5950};
    static void* hammer[2]={g_image.base+0x17e42a0};
    static void* shield[2]={g_image.base+0x17e51a0};
    static void* cannon[2]={g_image.base+0x17e3f18};
    static void* pile[2]={g_image.base+0x17e4a80};
    g_fencerSpearAnswers.Put(spear,1);
    CHECK(HandAimGameSwing(katana) && HandAimGameSwing(hammer));
    CHECK(!HandAimGameSwing(spear) && !HandAimGameSwing(shield) && !HandAimGameSwing(cannon) && !HandAimGameSwing(pile));
    CHECK(HandAimFencerSweeps(katana) && HandAimFencerSweeps(hammer) && HandAimFencerSweeps(shield));
    CHECK(!HandAimFencerSweeps(spear) && !HandAimFencerSweeps(cannon) && !HandAimFencerSweeps(pile));
    // No Fencer in play: nothing is turned away from the tick.
    WeaponHoldCommand command{};
    CHECK(!HandAimAwayCommand(cannon,command));
}
// menu_board.h: each menu's OnUpdate (slot 1) is replaced once on the real
// image, and a menu update puts the display on the board until it goes quiet.
static void CheckMenuBoard() {
    for(const auto& c:kMenuClasses) CHECK(*reinterpret_cast<void**>(g_image.base+c.table+8)==g_image.base+c.update);
    bool changed=false;
    CHECK(InstallMenuBoard(changed) && changed);
    for(unsigned i=0;i<kMenuClassCount;++i)
        CHECK(*reinterpret_cast<void**>(g_image.base+kMenuClasses[i].table+8)==reinterpret_cast<void*>(kMenuHooks[i])
              && g_menuUpdateOriginal[i]==reinterpret_cast<MenuUpdate>(g_image.base+kMenuClasses[i].update));
    CHECK(!InstallMenuBoard(changed));   // the slots no longer hold the game's functions
    CHECK(!edf6vr::g_openxr.MenuOpen());
    edf6vr::g_openxr.MarkMenuOpen();
    CHECK(edf6vr::g_openxr.MenuOpen());
    Sleep(350);
    CHECK(!edf6vr::g_openxr.MenuOpen());
}
// ranger_dual.h NoteSpreadInput / SpreadInputFor and the turn shot_origin.h
// makes with them: a bullet built inside its cone around the noted direction is
// turned from that direction onto the barrel, so its angle off the barrel is
// its cone deviation (the game's accuracy kept) and nothing of an aim the
// direction was off (the hand cannon's 10 degrees gone).
static void CheckBarrelFromCone() {
    auto unit=[](float x,float y,float z){float l=std::sqrt(x*x+y*y+z*z);return std::array<float,3>{x/l,y/l,z/l};};
    auto angle=[](const float* a,const float* b){float c=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];return std::acos(std::clamp(c,-1.0f,1.0f));};
    const float noted[3]={0,0,2};
    NoteSpreadInput(noted,0.05f);
    const auto dir=unit(0,0,1);
    const auto inside=unit(std::sin(0.03f),0,std::cos(0.03f));   // 0.03 rad off: inside a 0.05 cone
    const auto outside=unit(std::sin(0.07f),0,std::cos(0.07f));  // past 0.05 + half a degree
    const ULONGLONG now=GetTickCount64();
    const float* got=SpreadInputFor(inside.data(),now);
    CHECK(got && std::fabs(got[2]-1)<1e-6f);
    CHECK(!SpreadInputFor(outside.data(),now));
    CHECK(!SpreadInputFor(inside.data(),now+300));                 // stale
    // The turn: the game aimed 10 degrees under the barrel; the bullet left 0.03 rad
    // to the side of that aim.
    const float down=10*0.01745329f;
    const auto aim=unit(0,-std::sin(down),std::cos(down));
    float built[3][4]{};
    {   // the bullet's rows: forward = the aim turned 0.03 rad to the side
        float rows[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
        CHECK(FencerTurnRows(rows,dir.data(),inside.data()));
        CHECK(FencerTurnRows(rows,dir.data(),aim.data()));
        std::memcpy(built,rows,sizeof built);
    }
    float forward[3]={built[2][0],built[2][1],built[2][2]};
    NoteSpreadInput(aim.data(),0.05f);
    const float* before=SpreadInputFor(forward,GetTickCount64());
    CHECK(before && angle(before,aim.data())<1e-5f);
    const auto barrel=unit(0.2f,0.1f,1);
    float rows[3][4];std::memcpy(rows,built,sizeof rows);
    CHECK(FencerTurnRows(rows,before,barrel.data()));
    const float now2[3]={rows[2][0],rows[2][1],rows[2][2]};
    CHECK(std::fabs(angle(now2,barrel.data())-0.03f)<2e-3f);       // the cone survives, the 10 degrees do not
    // The old turn, from the soldier's aim (level, 10 degrees above the built aim), kept them.
    std::memcpy(rows,built,sizeof rows);
    CHECK(FencerTurnRows(rows,dir.data(),barrel.data()));
    const float old[3]={rows[2][0],rows[2][1],rows[2][2]};
    CHECK(angle(old,barrel.data())>9*0.01745329f);
    float bad[3]={NAN,0,1};NoteSpreadInput(bad,0.05f);CHECK(!SpreadInputFor(forward,GetTickCount64()));
}
// The Ranger's left gun has a second reticle: published after the left hold
// command is built (PublishRangerSecondAim), not where the right one is
// (PublishSecondAim), where AfterUpdate has just wiped that command.
static void CheckRangerSecondAim() {
    const bool vr=g_vrEnabled,hand=g_handAiming,dual=g_dualActive.load(),fencer=g_fencerActive.load();
    const float yaw=g_yawOffset;
    const auto saved=g_leftHoldCommand;
    g_vrEnabled=true;g_handAiming=true;g_dualActive=true;g_fencerActive=false;g_yawOffset=0.4f;
    float got[3]{};
    // Mid-update, the command wiped: the second reticle is not touched.
    const float before[3]={0,0,-1};
    edf6vr::g_openxr.SetAimDirection2(before);
    g_leftHoldCommand={};
    PublishSecondAim();
    CHECK(edf6vr::g_openxr.AimDirection2(got) && got[2]==-1.0f);
    // A wiped command after the build (the left hand lost): no reticle.
    PublishRangerSecondAim(true);
    CHECK(!edf6vr::g_openxr.AimDirection2(got));
    // Built: along the hand's forward, the way the main reticle maps the aim.
    g_leftHoldCommand.tracked=true;
    const float pitch=0.3f,turn=-1.1f;
    g_leftHoldCommand.hand.axes[2][0]=std::sin(turn)*std::cos(pitch);
    g_leftHoldCommand.hand.axes[2][1]=std::sin(pitch);
    g_leftHoldCommand.hand.axes[2][2]=std::cos(turn)*std::cos(pitch);
    PublishRangerSecondAim(true);
    const auto want=edf6vr::AimToReference(turn,pitch,g_yawOffset);
    CHECK(edf6vr::g_openxr.AimDirection2(got));
    CHECK(std::fabs(got[0]-want.x)<1e-4f && std::fabs(got[1]-want.y)<1e-4f && std::fabs(got[2]-want.z)<1e-4f);
    // ...and it is the hand's own direction, not the right gun's.
    CHECK(std::fabs(got[1]-std::sin(pitch))<1e-4f);
    // A hand weapon's command is not always unit length: still the same way.
    for(auto& v:g_leftHoldCommand.hand.axes[2]) v*=2;
    PublishRangerSecondAim(true);
    CHECK(edf6vr::g_openxr.AimDirection2(got) && std::fabs(got[0]-want.x)<1e-4f && std::fabs(got[1]-want.y)<1e-4f);
    // The mid-update call leaves the Ranger's alone.
    PublishSecondAim();
    CHECK(edf6vr::g_openxr.AimDirection2(got) && std::fabs(got[0]-want.x)<1e-4f);
    // Out of the soldier's view (a vehicle), out of VR, or without hand aim: none.
    PublishRangerSecondAim(false);CHECK(!edf6vr::g_openxr.AimDirection2(got));
    g_vrEnabled=false;PublishRangerSecondAim(true);CHECK(!edf6vr::g_openxr.AimDirection2(got));g_vrEnabled=true;
    g_handAiming=false;PublishRangerSecondAim(true);CHECK(!edf6vr::g_openxr.AimDirection2(got));g_handAiming=true;
    // The left gun put away: none, from either call.
    PublishRangerSecondAim(true);CHECK(edf6vr::g_openxr.AimDirection2(got));
    g_dualActive=false;
    PublishRangerSecondAim(true);CHECK(!edf6vr::g_openxr.AimDirection2(got));
    edf6vr::g_openxr.SetAimDirection2(before);
    PublishSecondAim();CHECK(!edf6vr::g_openxr.AimDirection2(got));
    // The Fencer's is PublishSecondAim's: the Ranger's call leaves it.
    g_fencerActive=true;
    edf6vr::g_openxr.SetAimDirection2(before);
    PublishRangerSecondAim(true);CHECK(edf6vr::g_openxr.AimDirection2(got) && got[2]==-1.0f);
    edf6vr::g_openxr.SetAimDirection2(nullptr);
    g_leftHoldCommand=saved;g_vrEnabled=vr;g_handAiming=hand;g_dualActive=dual;g_fencerActive=fencer;g_yawOffset=yaw;
}
// Vehicle hand aim, on at first in the Nix only: an older INI's seats are
// turned off once, and a seat ticked again afterwards stays on.
static void CheckVehicleHandAimDefaults() {
    wchar_t dir[MAX_PATH]{},path[MAX_PATH]{};GetTempPathW(MAX_PATH,dir);
    swprintf_s(path,L"%sedf6vr_handaim_%lu.ini",dir,GetCurrentProcessId());
    const char older[]="[VR]\r\nVehicleHandAimNix=1\r\nVehicleHandAimDepth=0\r\nVehicleHandAimBarga=0\r\nVehicleHandAimTank=1\r\n"
                       "VehicleHandAimCombat=1\r\nVehicleHandAimHeli=1\r\nVehicleHandAimGunner=1\r\nVehicleHandAimBruteGunner=1\r\n";
    FILE* file=nullptr;_wfopen_s(&file,path,L"wb");CHECK(file!=nullptr);if(!file)return;
    fwrite(older,1,sizeof(older)-1,file);fclose(file);
    bool saved[kHandAimClasses];std::memcpy(saved,g_vehicleHandAimOn,sizeof saved);
    CHECK(RetireVehicleHandAimDefaults(path));
    ReadVehicleHandAim(path);
    for(int i=0;i<kHandAimClasses;++i)CHECK(g_vehicleHandAimOn[i]==(i==0));
    CHECK(GetPrivateProfileIntW(L"VR",L"VehicleHandAimDefaults",0,path)==2);
    // Ticked again (the settings program writes 1): kept from now on.
    WritePrivateProfileStringW(L"VR",L"VehicleHandAimTank",L"1",path);
    CHECK(!RetireVehicleHandAimDefaults(path));
    ReadVehicleHandAim(path);
    CHECK(g_vehicleHandAimOn[static_cast<int>(edf6vr::HandAimClass::Tank)]);
    CHECK(g_vehicleHandAimOn[static_cast<int>(edf6vr::HandAimClass::Nix)]);
    // The Nix switched off by the player is not turned back on.
    DeleteFileW(path);_wfopen_s(&file,path,L"wb");CHECK(file!=nullptr);if(!file)return;
    const char nixOff[]="[VR]\r\nVehicleHandAimNix=0\r\n";fwrite(nixOff,1,sizeof(nixOff)-1,file);fclose(file);
    CHECK(RetireVehicleHandAimDefaults(path));ReadVehicleHandAim(path);
    for(int i=0;i<kHandAimClasses;++i)CHECK(!g_vehicleHandAimOn[i]);
    // No keys at all: the code's own defaults, the Nix only.
    DeleteFileW(path);
    ReadVehicleHandAim(path);
    for(int i=0;i<kHandAimClasses;++i)CHECK(g_vehicleHandAimOn[i]==(i==0));
    DeleteFileW(path);
    std::memcpy(g_vehicleHandAimOn,saved,sizeof saved);
}
// The vehicle recoil draw keeps its hands off an old state's node array: the
// vehicle it names may be gone (the next mission's start, 2026-10-05).
static void CheckVehicleRecoilStaleState() {
    void* reserved=VirtualAlloc(nullptr,0x10000,MEM_RESERVE,PAGE_NOACCESS);CHECK(reserved!=nullptr);if(!reserved)return;
    static unsigned char model[0x40]{},other[0x40]{};
    VehicleRecoilPublished state{};
    state.model=model;state.partCount=1;state.parts[0].moves[0]=3;state.parts[0].moveCount=1;
    state.nodes=static_cast<unsigned char*>(reserved);state.nodeCount=8;
    state.seen=GetTickCount64()-60000;
    AcquireSRWLockExclusive(&g_vehicleRecoilLock);const auto savedState=g_vehicleRecoilDraw;g_vehicleRecoilDraw=state;ReleaseSRWLockExclusive(&g_vehicleRecoilLock);
    const auto savedFrame=g_vehicleRecoilFrame;g_vehicleRecoilFrame={};
    const bool recoil=g_vehicleRecoilOn;g_vehicleRecoilOn=true;
    static std::atomic<int> faults{0};faults=0;
    void* handler=AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* e)->LONG{
        if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) faults.fetch_add(1);
        return EXCEPTION_CONTINUE_SEARCH;});
    CHECK(!VehicleRecoilDraw(other,nullptr,0,nullptr));
    CHECK(faults==0);
    CHECK(g_vehicleRecoilFrame.valid && !g_vehicleRecoilFrame.pivotOk[0]);
    // A fresh state still has its pivot read (here from a readable array).
    alignas(16) static unsigned char nodes[8*0x110]{};
    reinterpret_cast<float*>(nodes+3*0x110+0xB0)[12]=1.5f;
    state.nodes=nodes;state.seen=GetTickCount64();
    AcquireSRWLockExclusive(&g_vehicleRecoilLock);g_vehicleRecoilDraw=state;ReleaseSRWLockExclusive(&g_vehicleRecoilLock);
    g_vehicleRecoilFrame={};
    VehicleRecoilDraw(other,nullptr,0,nullptr);
    CHECK(faults==0 && g_vehicleRecoilFrame.pivotOk[0] && g_vehicleRecoilFrame.pivot[0][0]==1.5f);
    RemoveVectoredExceptionHandler(handler);
    g_vehicleRecoilOn=recoil;g_vehicleRecoilFrame=savedFrame;
    AcquireSRWLockExclusive(&g_vehicleRecoilLock);g_vehicleRecoilDraw=savedState;ReleaseSRWLockExclusive(&g_vehicleRecoilLock);
    VirtualFree(reserved,0,MEM_RELEASE);
}
// Fencer shoulder weapons that fire straight ahead (the mortars): which ones,
// by the real vtables and the launch vector at weapon+0x350; the shell turned
// from its cone's input onto the hand's aim; the guide line's throw turned the
// same way with the game's start and owner motion kept, in the drawn arc and
// in the landing rays. The dip is the one measured on 2026-10-09: 12 degrees.
static float* g_seenVelocity=nullptr;static float g_seenVelocityCopy[3]{};
static void* __fastcall FakeGuideArc(void*,float*,float* velocity,float*) {
    for(int j=0;j<3;++j) g_seenVelocityCopy[j]=velocity[j];g_seenVelocity=velocity;return nullptr;
}
static float g_seenRay[2][3]{};
static void* __fastcall FakeGuideRay(void*,void*,unsigned char* items,int,int) {
    std::memcpy(g_seenRay[0],items,12);std::memcpy(g_seenRay[1],items+0x10,12);return nullptr;
}
static void CheckShoulderStraight() {
    auto angle=[](const float* a,const float* b){float c=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];return std::acos(std::clamp(c,-1.0f,1.0f));};
    alignas(16) static unsigned char weapon[0x1000]{},entries[0x100]{};
    const bool shoulder0=g_fencerShoulder[0],shoulder1=g_fencerShoulder[1],active=g_fencerActive.load();
    void* const was0=g_fencerWeapons[0].load();void* const was1=g_fencerWeapons[1].load();
    FencerShadow savedShadow{};AcquireSRWLockShared(&g_fencerShadowLock);savedShadow=g_fencerShadow;ReleaseSRWLockShared(&g_fencerShadowLock);
    DualAt<void*>(weapon,0)=g_image.base+0x17E3F18;            // Weapon_HeavyShoot
    const float straight[4]={0,0,1,1},javelin[4]={0,1,1,1};
    std::memcpy(weapon+0x350,straight,16);
    g_fencerShoulder[0]=true;g_fencerShoulder[1]=false;
    CHECK(FencerStraightShoulder(0,weapon));
    CHECK(!FencerStraightShoulder(1,weapon));                  // held in a hand, not on a shoulder
    std::memcpy(weapon+0x350,javelin,16);
    CHECK(!FencerStraightShoulder(0,weapon));                  // the javelins' designed lob stays
    std::memcpy(weapon+0x350,straight,16);
    DualAt<void*>(weapon,0)=g_image.base+0x17E40A0;            // Weapon_HomingShoot
    CHECK(!FencerStraightShoulder(0,weapon));
    DualAt<void*>(weapon,0)=g_image.base+0x17E3F18;
    // The left hand's aim: the shadow's smoothed aim, level and straight ahead.
    AcquireSRWLockExclusive(&g_fencerShadowLock);g_fencerShadow={};g_fencerShadow.valid=true;ReleaseSRWLockExclusive(&g_fencerShadowLock);
    float aim[3]{};
    CHECK(FencerHandAimForward(0,nullptr,aim) && std::fabs(aim[2]-1)<1e-6f);
    // The shell: the game's aim before its cone is 12 degrees under the hand's aim;
    // the shell left 0.006 rad to the side of that. Turned from the noted input it
    // is 0.006 rad off the aim; the old turn (by the aims' own arc) kept the 12.
    const float dip=12*0.01745329f;
    const float dipped[3]={0,-std::sin(dip),std::cos(dip)};
    NoteSpreadInput(dipped,0.01f);
    float rows[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    const float side[3]={std::sin(0.006f),0,std::cos(0.006f)},ahead[3]={0,0,1};
    CHECK(FencerTurnRows(rows,ahead,side) && FencerTurnRows(rows,ahead,dipped));
    const float built[3]={rows[2][0],rows[2][1],rows[2][2]};
    const float* input=SpreadInputFor(built,GetTickCount64());
    CHECK(input && angle(input,dipped)<1e-5f);
    if(input) {
        CHECK(FencerTurnRows(rows,input,aim));
        const float shot[3]={rows[2][0],rows[2][1],rows[2][2]};
        CHECK(std::fabs(angle(shot,aim)-0.006f)<1e-3f);
    }
    CHECK(angle(built,aim)>11*0.01745329f);
    // The guide: the matrices give the dipped direction (row 2 of the entry rows
    // times the +Z launch vector), the throw is 2 units a step.
    DualAt<unsigned char*>(weapon,0x1D0)=entries;
    const float r0[4]={1,0,0,0},r1[4]={0,std::cos(dip),std::sin(dip),0},r2[4]={0,-std::sin(dip),std::cos(dip),0},at[4]={5,1,-3,1};
    std::memcpy(entries+0x50,r0,16);std::memcpy(entries+0x60,r1,16);std::memcpy(entries+0x70,r2,16);std::memcpy(entries+0x80,at,16);
    DualAt<float>(weapon,0x894)=2.0f;
    g_fencerActive=true;g_fencerWeapons[0].store(weapon);g_fencerWeapons[1].store(nullptr);
    GuideDeltas d{};
    CHECK(GuideComputeDeltas(weapon,d) && d.valid && d.shiftOnly && d.ownStart && d.hand==0);
    for(int j=0;j<3;++j) CHECK(std::fabs(d.shift[j]-2*(aim[j]-dipped[j]))<1e-5f && d.delta[j]==0 && d.start[j]==at[j]);
    // The drawn arc: the game's step (throw plus 0.3 of owner motion) comes out
    // as the throw along the aim plus the same owner motion; the start is not moved.
    auto arcOriginal=g_guideArcOriginal;auto rayOriginal=g_guideRayOriginal;
    g_guideArcOriginal=FakeGuideArc;g_guideRayOriginal=FakeGuideRay;
    float start[3]={5,1,-3},velocity[3]={0.3f+2*dipped[0],2*dipped[1],2*dipped[2]},gravity[3]{0,-0.01f,0};
    g_guideLine=d;HookGuideArc(nullptr,start,velocity,gravity);g_guideLine={};
    CHECK(start[0]==5 && start[1]==1 && start[2]==-3);
    CHECK(std::fabs(g_seenVelocityCopy[0]-0.3f)<1e-5f && std::fabs(g_seenVelocityCopy[1])<1e-5f && std::fabs(g_seenVelocityCopy[2]-2)<1e-5f);
    // The first landing ray spans one step from the start: its end moves by the shift.
    alignas(16) unsigned char item[0xD0]{};
    const float first[4]={5,1,-3,1},second[4]={5+0.3f+2*dipped[0],1+2*dipped[1],-3+2*dipped[2],1};
    std::memcpy(item,first,16);std::memcpy(item+0x10,second,16);
    g_guideRay=d;g_guideRayIndex=0;g_guideRayHaveLast=false;
    HookGuideRay(nullptr,nullptr,item,1,0);
    CHECK(g_seenRay[0][0]==5 && g_seenRay[0][1]==1 && g_seenRay[0][2]==-3);
    CHECK(std::fabs(g_seenRay[1][0]-5.3f)<1e-5f && std::fabs(g_seenRay[1][1]-1)<1e-5f && std::fabs(g_seenRay[1][2]+1)<1e-5f);
    GuideRayEnd();
    // A javelin, or matrices 31 degrees off the aim (a weapon change): nothing.
    std::memcpy(weapon+0x350,javelin,16);
    CHECK(!GuideComputeDeltas(weapon,d) && !d.valid);
    std::memcpy(weapon+0x350,straight,16);
    const float wide=31*0.01745329f;
    const float f1[4]={0,std::cos(wide),std::sin(wide),0},f2[4]={0,-std::sin(wide),std::cos(wide),0};
    std::memcpy(entries+0x60,f1,16);std::memcpy(entries+0x70,f2,16);
    CHECK(!GuideComputeDeltas(weapon,d));
    g_guideArcOriginal=arcOriginal;g_guideRayOriginal=rayOriginal;
    g_fencerShoulder[0]=shoulder0;g_fencerShoulder[1]=shoulder1;g_fencerActive=active;
    g_fencerWeapons[0].store(was0);g_fencerWeapons[1].store(was1);
    AcquireSRWLockExclusive(&g_fencerShadowLock);g_fencerShadow=savedShadow;ReleaseSRWLockExclusive(&g_fencerShadowLock);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[200]{};if(!module || !edf6vr::CheckImage(module,g_image,reason,sizeof reason))return 2;
    CHECK(CheckRangerDualProfile());
    // Steady cadence is measured per hand immediately BEFORE the next shot,
    // as HookDualFire does. Every cadence uses the full spread scale; slower
    // fire remains milder only because its actual shot history accumulates less.
    for(int rate=2;rate<=60;++rate) {
        edf6vr::RangerRecoil history{};
        for(int shot=0;shot<rate*2;++shot)history.Shot(double(shot)/rate);
        const float level=history.Level(2);
        const float oldSpread=12*level,newSpread=edf6vr::RangerRecoilSpreadDegrees(level);
        CHECK(newSpread>oldSpread);
        CHECK(std::fabs(newSpread-(rate-1)/3.0f)<.0001f);
        if(rate==60)CHECK(newSpread>19.5f && newSpread<=20);
        // Recovery remains continuous across the former protection threshold.
        float last=newSpread;
        for(int ms=1;ms<=1000;++ms) {
            const float spread=edf6vr::RangerRecoilSpreadDegrees(history.Level(2+ms/1000.0));
            CHECK(spread<=last && last-spread<.10f);last=spread;
        }
        CHECK(last==0);
    }
    float priorSpread=0;
    for(int step=0;step<=10000;++step) {
        const float spread=edf6vr::RangerRecoilSpreadDegrees(step/10000.0f);
        CHECK(spread>=priorSpread && spread-priorSpread<.004f);priorSpread=spread;
    }
    CHECK(edf6vr::RangerRecoilSpreadDegrees(1)==20);
    edf6vr::RangerRecoil fast{},slow{},idle{};
    for(int i=0;i<60;++i)fast.Shot(i/60.0);
    for(int i=0;i<10;++i)slow.Shot(i/10.0);
    CHECK(fast.Level(59/60.0)>.99f && fast.Level(1)>.98f);
    CHECK(slow.Level(1)>0 && slow.Level(1)<fast.Level(1)*.25f);
    CHECK(fast.Level(1.5)<fast.Level(1) && fast.Level(2)==0 && slow.Level(2)==0 && idle.Level(1)==0);
    // Accumulation and decay run simultaneously, with no expiry-time jump.
    CHECK(fast.Level(1.5)>.2f && fast.Level(1.5)<.3f);
    float previousLevel=fast.Level(1);
    for(int ms=1;ms<=1000;++ms) {
        const float level=fast.Level(1+ms/1000.0);
        CHECK(level<=previousLevel && previousLevel-level<.0021f);
        previousLevel=level;
    }
    edf6vr::RangerRecoil overlap{};overlap.Shot(0);
    CHECK(overlap.Level(.5)>0 && overlap.Level(.5)<overlap.Level(0));
    const auto remaining=overlap.Level(.5);overlap.Shot(.5);
    CHECK(std::fabs(overlap.Level(.5)-(remaining+1.0f/30))<.00001f);
    for(int i=0;i<1000;++i)idle.Shot(3);
    CHECK(idle.Level(3)==1 && idle.Level(4)==0); // cap and full recovery under a burst
    // Strict left shoulder, yaw-relative, with boundary hysteresis and NaN guard.
    const float head[3]={10,2,20},left[3]={10.18f,2,20},right[3]={9.82f,2,20};
    CHECK(edf6vr::InLeftShoulder(head,left,0,false));CHECK(!edf6vr::InLeftShoulder(head,right,0,false));
    const float turned[3]={10,2,19.82f};CHECK(edf6vr::InLeftShoulder(head,turned,1.57079633f,false));
    const float oldBorder[3]={10.38f,2,20};CHECK(edf6vr::InLeftShoulder(head,oldBorder,0,false));
    CHECK(!edf6vr::NearTemple(head,oldBorder,0,.18f,0,0,.15f,.05f));
    const float border[3]={10.43f,2,20};CHECK(!edf6vr::InLeftShoulder(head,border,0,false));CHECK(edf6vr::InLeftShoulder(head,border,0,true));
    const float lowShoulder[3]={10.22f,1.78f,19.96f};CHECK(edf6vr::InLeftShoulder(head,lowShoulder,0,false));
    const float frontInside[3]={10.18f,2,20.12f},frontOutside[3]={10.18f,2,20.14f};
    CHECK(edf6vr::InLeftShoulder(head,frontInside,0,false));CHECK(!edf6vr::InLeftShoulder(head,frontOutside,0,false));
    CHECK(edf6vr::InLeftShoulder(head,frontOutside,0,true));
    CHECK(edf6vr::NearTemple(head,left,0,.18f,0,0,.15f,.05f));
    CHECK(!edf6vr::InLeftShoulder(nullptr,left,0,false));CHECK(!edf6vr::InLeftShoulder(head,left,std::numeric_limits<float>::quiet_NaN(),false));
    {
        // yaw 0: side=-dx, ahead=dz. The old hTemple sphere still counts, and so
        // does anywhere behind the hHead down to the shoulder blades.
        const float hHead[3]={0,1.6f,0};
        const float hTemple[3]={.18f,1.6f,0},hOver[3]={.15f,1.4f,-.25f},hMidBack[3]={-.05f,1.25f,-.35f};
        const float hFront[3]={.15f,1.4f,.2f},hFarRight[3]={-.4f,1.4f,-.25f},hWaist[3]={.1f,.9f,-.25f},hFarBack[3]={.1f,1.4f,-.7f};
        const float hFarLeft[3]={.35f,1.4f,-.25f};   // swung out wide behind the left shoulder: not the holster
        CHECK(!edf6vr::InLeftShoulder(hHead,hFarLeft,0,false));
        CHECK(edf6vr::InLeftShoulder(hHead,hTemple,0,false));
        CHECK(edf6vr::InLeftShoulder(hHead,hOver,0,false));
        CHECK(edf6vr::InLeftShoulder(hHead,hMidBack,0,false));
        CHECK(!edf6vr::InLeftShoulder(hHead,hFront,0,false));
        CHECK(!edf6vr::InLeftShoulder(hHead,hFarRight,0,false));
        CHECK(!edf6vr::InLeftShoulder(hHead,hWaist,0,false));
        CHECK(!edf6vr::InLeftShoulder(hHead,hFarBack,0,false));
        // Facing the other way (yaw pi) the same reach, hTurned with the body, still counts.
        const float hTurned[3]={-.15f,1.4f,.25f};
        CHECK(edf6vr::InLeftShoulder(hHead,hTurned,3.14159265f,false));
        CHECK(!edf6vr::InLeftShoulder(hHead,hOver,3.14159265f,false));
        // Hysteresis: just outside on entry, still inside on the way out.
        const float hEdge[3]={.1f,1.4f,-.57f};
        CHECK(!edf6vr::InLeftShoulder(hHead,hEdge,0,false) && edf6vr::InLeftShoulder(hHead,hEdge,0,true));
    }
    edf6vr::RangerHolster holster{};
    auto step=holster.Step(true,true,false);CHECK(step.entered && !step.toggle && step.consume);
    for(int i=0;i<20;++i){step=holster.Step(true,true,false);CHECK(!step.entered && !step.toggle);}
    step=holster.Step(true,true,true);CHECK(step.toggle && !step.entered);
    step=holster.Step(true,true,true);CHECK(!step.toggle);
    step=holster.Step(true,false,true);CHECK(step.consume && !step.toggle);
    step=holster.Step(true,false,false);CHECK(!step.consume);
    step=holster.Step(true,false,true);CHECK(!step.toggle);
    step=holster.Step(true,true,true);CHECK(step.entered && !step.toggle && step.consume);
    step=holster.Step(false,false,true);CHECK(step.consume && !step.toggle);
    holster.Step(false,false,false);step=holster.Step(true,true,false);CHECK(step.entered);
    DualAt<void*>(soldier,0)=g_image.base+0x17CDF28;DualAt<unsigned>(soldier,0x314)=17;
    DualAt<void*>(soldier,0x1970)=slots;DualAt<unsigned>(soldier,0x1980)=1;
    DualAt<void*>(soldier,0x1950)=owned;DualAt<unsigned>(soldier,0x1960)=3;
    DualAt<void*>(soldier,0x910)=body;DualAt<std::uint64_t>(soldier,0x920)=2;
    DualAt<unsigned short>(slots,0x18)=1;DualAt<int>(slots,0x38)=1;DualAt<void*>(slots,0x40)=owned+1;
    edf6vr::Matrix identity={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    for(unsigned h=0;h<3;++h) {
        owned[h]=weapons[h];DualAt<void*>(weapons[h],0)=g_image.base+0x17E36E0;
        DualAt<void*>(weapons[h],0x120)=soldier;DualAt<int>(weapons[h],0xBE8)=5;DualAt<int>(weapons[h],0xE68)=120;DualAt<int>(weapons[h],0xE18)=20;
        if(h==2)continue;
        DualAt<void*>(weapons[h],0x1D0)=attachments[h];DualAt<std::uint64_t>(weapons[h],0x1E0)=1;
        DualAt<float>(weapons[h],0x358)=1;
        DualAt<edf6vr::Matrix>(attachments[h],0x50)=identity;DualAt<edf6vr::Matrix>(bones[h],0xB0)=identity;
        auto model=weapons[h]+edf6vr::kWeaponModelOffset;
        DualAt<void*>(model,0)=g_image.base+edf6vr::kModelVtableRva;
        DualAt<void*>(model,0xB0)=bones[h];DualAt<std::uint64_t>(model,0xC0)=2;
    }
    g_dualIndex=FakeIndex;g_dualZoom=FakeZoom;g_dualPose=FakePose;g_dualHolster=FakeHolster;
    for(auto& f:g_dualOriginals)f=FakeTick;
    RangerDualState state{};CHECK(SelectRangerPair(soldier,state));CHECK(state.weapons[0]==weapons[0] && state.weapons[1]==weapons[1]);
    CHECK(state.weapons[0]!=weapons[2]); // backpack must never become left gun
    state.hands[0]=Command(0);state.hands[1]=Command(1);
    CHECK(ValidateHoldCommand(state.hands[0],state.hands[0].model));
    g_dualState=state;g_dualActive=true;g_dualInputAt=GetTickCount64();g_dualLeftFire=true;
    for(unsigned h=0;h<2;++h)g_dualWeapons[h]=state.weapons[h];
    DualAt<unsigned char>(weapons[1],0x13E)=1;
    expectDual=true;
    for(int i=0;i<6;++i) {
        DualAt<unsigned char>(weapons[1],0x139)=1;
        HookDualWeaponTick(weapons[0],soldier);HookDualWeaponTick(weapons[1],soldier);
    }
    CHECK(ticks==12 && poses==12);CHECK(DualAt<int>(weapons[0],0xBE8)==0 && DualAt<int>(weapons[1],0xBE8)==0);
    CHECK(DualAt<int>(weapons[0],0xE68)==120 && DualAt<int>(weapons[1],0xE68)==120);
    CHECK(DualAt<int>(weapons[0],0xE18)==14 && DualAt<int>(weapons[1],0xE18)==14);
    CHECK(DualAt<unsigned char>(weapons[0],0x13E)==0 && DualAt<unsigned char>(weapons[1],0x13E)==1);
    float muzzle[3]{};CHECK(!ResolveTrackedMuzzle(weapons[0],muzzle,GetTickCount64()));
    CHECK(!ResolveTrackedMuzzle(weapons[1],muzzle,GetTickCount64())); // only discharges publish tracked muzzle
    g_holdCommand=state.hands[1];g_leftHoldCommand=state.hands[0];g_modelOriginal=FakeDraw;
    for(unsigned h=0;h<2;++h) {
        auto model=weapons[h]+edf6vr::kWeaponModelOffset;
        DualAt<void*>(model,0x1B8)=model+0xA0;DualAt<void*>(model,0xD0)=palette[h];
        palette[h][0]=palette[h][1]=identity;
        CHECK(DrawHeldWeapon(model,nullptr,2,nullptr));
        CHECK(!std::memcmp(palette[h],&identity,64));
    }
    CHECK(draws==2 && !g_insideHoldBorrow);
    CheckTickException();
    g_vrSoldier=soldier;g_vrEnabled=g_fpsEnabled=g_dualReady=true;g_dualEligible=true;g_dualEligibleAt=GetTickCount64();g_adjust=Adjusting::Off;
    DualAt<unsigned char>(soldier,0xD72)=1;DualAt<unsigned char>(soldier,0xD73)=1;DualAt<unsigned char>(soldier,0xD75)=1;
    DualAt<unsigned char>(weapons[1],0x780)=1;g_dualCancel=false;
    CHECK(!UpdateRangerDual(soldier));CHECK(zoomCalls==0);HookDualWeaponTick(weapons[1],soldier);CHECK(zoomCalls==1);
    CHECK(!DualAt<unsigned char>(soldier,0xD72) && !DualAt<unsigned char>(soldier,0xD73) && !DualAt<unsigned char>(soldier,0xD75));
    DualAt<unsigned>(soldier,0x5D0)=4;CHECK(!RangerMayFire(soldier));DualAt<unsigned>(soldier,0x5D0)=0;
    DualAt<int>(soldier,0x198C)=1;CHECK(!RangerMayFire(soldier));DualAt<int>(soldier,0x198C)=0;CHECK(RangerMayFire(soldier));
    edf6vr::ControllerState controls{};controls.present[0]=controls.present[1]=true;
    g_padMode=1;g_adjustEnabled=false;g_rangerHolster={};g_gripHeld[0]=false;
    controls.trigger[0]=1;controls.squeeze[0]=1;g_padSeenButtons=0;g_padSeenLeft=0;
    const unsigned serial=g_dualToggleSerial.load();
    g_adjustEnabled=true;g_adjust=Adjusting::Counting;
    controls.stick[0][1]=-1;g_dualCancel=false;
    BuildPad(controls,true,head,right,left,0,true);
    CHECK(g_dualToggleSerial.load()==serial+1);CHECK(!(g_padSeenButtons&kPadLeftShoulder) && g_padSeenLeft==0);
    CHECK(edf6vr::capturedTestPad.leftY<-30000 && g_adjust!=Adjusting::Counting && !g_dualCancel.load());
    controls.stick[0][1]=1;
    BuildPad(controls,true,head,right,left,0,true);CHECK(g_dualToggleSerial.load()==serial+1);
    CHECK(edf6vr::capturedTestPad.leftY>30000 && !g_heightHold.active && !g_dualCancel.load());
    BuildPad(controls,true,head,right,right,0,true);CHECK(!(g_padSeenButtons&kPadLeftShoulder));
    // Do not consume the test's queued toggle until the normal exit test below.
    g_dualToggleSerial.store(serial);g_dualCancel=false;
    edf6vr::HeadBasis aim{};g_twoHandOn=g_twoHandDirValid=true;TwoHanded(aim,nullptr,nullptr);CHECK(!g_twoHandOn && !g_twoHandDirValid);
    // Original right HUD direction remains a single source; no OpenXR layer was added.
    g_dualToggleSerial.fetch_add(1);CHECK(UpdateRangerDual(soldier));CHECK(!g_dualActive.load());
    CHECK(!UpdateRangerDual(soldier)); // held input cannot repeat the sound
    expectDual=false;HookDualWeaponTick(weapons[1],soldier);HookDualWeaponTick(weapons[0],soldier);
    CHECK(holsterCalls==1);HookDualWeaponTick(weapons[0],soldier);CHECK(holsterCalls==1);
    CHECK(DualAt<int>(weapons[1],0xE68)==119 && DualAt<int>(weapons[0],0xE68)==120);
    CHECK(!ResolveTrackedMuzzle(weapons[0],muzzle,GetTickCount64()));
    g_dualInputAt=GetTickCount64();g_dualEligibleAt=GetTickCount64();
    g_dualToggleSerial.fetch_add(1);CHECK(UpdateRangerDual(soldier));CHECK(g_dualActive.load());
    CHECK(!UpdateRangerDual(soldier));
    CheckSwitchSound();
    g_dualCancel=true;CHECK(!UpdateRangerDual(soldier));CHECK(!g_dualActive.load());
    CheckFireBoundary(state);
    // One press schedules three shots independently in BOTH hands. Release
    // after the first; scheduled shots must finish while reload stays frozen.
    burstMode=true;expectDual=true;g_dualState=state;g_dualActive=true;g_dualInputAt=GetTickCount64();
    g_dualVisibility=FakeVisibility;
    DualAt<unsigned char>(weapons[0],0x78C)=0;DualAt<unsigned char>(weapons[0],0xF30+0x480)=0;
    for(unsigned h=0;h<2;++h) {
        g_dualWeapons[h]=state.weapons[h];
        DualAt<int>(weapons[h],0xBE8)=9;DualAt<int>(weapons[h],0xE18)=0;DualAt<int>(weapons[h],0xE68)=120;
        DualAt<unsigned char>(weapons[h],0x13A)=0;
    }
    for(unsigned i=0;i<5;++i) {
        g_dualLeftFire=i==0;DualAt<unsigned char>(weapons[1],0x139)=i==0?1:0;
        HookDualWeaponTick(weapons[0],soldier);HookDualWeaponTick(weapons[1],soldier);
    }
    for(unsigned h=0;h<2;++h) {
        CHECK(burstShots[h]==3 && DualAt<int>(weapons[h],0xBE8)==6);
        CHECK(DualAt<int>(weapons[h],0xE18)==0 && DualAt<int>(weapons[h],0xE68)==120);
    }
    CHECK(visibleChanges==1 && DualAt<unsigned char>(weapons[0],0x78C)==1 && DualAt<unsigned char>(weapons[0],0xF30+0x480)==1);
    // Invoke actual EDF native holstering on an unfinished left burst. It
    // owns the burst reset, trigger latches, cooldown and ammo-on-abort policy.
    bool cleanupPatched=false;
    CHECK(edf6vr::RedirectCall(g_image.base+0x6900BF,g_image.base+0x68FF60,
        reinterpret_cast<void*>(&FakeEffectCleanup),cleanupPatched));
    nativeTestHolster=reinterpret_cast<DualZoom>(g_image.base+0x690230);
    g_dualLeftFire=true;HookDualWeaponTick(weapons[0],soldier);
    CHECK(DualAt<int>(weapons[0],0xE18)==2 && burstShots[0]==4);
    DualAt<int>(weapons[0],0x36C)=17;
    ResetRangerDual();expectDual=false;HookDualWeaponTick(weapons[0],soldier);
    CHECK(visibleChanges==2 && DualAt<unsigned char>(weapons[0],0x78C)==state.leftVisible);
    CHECK(DualAt<int>(weapons[0],0xE18)==0 && burstShots[0]==4);
    CHECK(DualAt<int>(weapons[0],0xBE8)==5 && DualAt<float>(weapons[0],0xE0C)==17);
    CHECK(!DualAt<unsigned char>(weapons[0],0x13A) && !DualAt<unsigned char>(weapons[0],0x13D));
    HookDualWeaponTick(weapons[0],soldier);CHECK(burstShots[0]==4);
    // EDF's consume-rest-on-interruption flag must remain native as well.
    DualAt<unsigned char>(weapons[0],0x13E)=1;DualAt<unsigned char>(weapons[0],0xE1C)=1;
    DualAt<int>(weapons[0],0xE18)=2;nativeTestHolster(weapons[0]);
    CHECK(DualAt<int>(weapons[0],0xBE8)==0 && DualAt<int>(weapons[0],0xE18)==0);
    DualAt<unsigned char>(weapons[0],0xE1C)=0;burstMode=false;
    // Recycled owners, changed weapon slot, and non-Ranger must all refuse.
    DualAt<unsigned>(soldier,0x314)=18;CHECK(!CurrentRangerPair(state,soldier));DualAt<unsigned>(soldier,0x314)=17;
    DualAt<void*>(slots,0x40)=owned;CHECK(!CurrentRangerPair(state,soldier));DualAt<void*>(slots,0x40)=owned+1;
    DualAt<void*>(soldier,0)=g_image.base+0x17CF5B8;CHECK(!SelectRangerPair(soldier,state));
    // Apply the real installation to the mapped copy; no native game routine is
    // executed. This verifies all 27 primary vtables and the pose call's ABI path.
    CHECK(InstallRangerDual());
    for(const auto& p:kDualWeapons)CHECK(DualAt<void*>(g_image.base+p.table,0x28)==reinterpret_cast<void*>(&HookDualWeaponTick));
    for(auto& f:g_dualOriginals)f=FakeTick;
    const int before=ticks;
    reinterpret_cast<DualTick>(DualAt<void*>(g_image.base+0x17E36E0,0x28))(weapons[2],soldier);
    CHECK(ticks==before+1);
    CheckNativeAccuracyAndVisibility();
    CheckActionVisibilityAndAudioHealth();
    CheckTrackedBoundsAndOutput();
    CheckHandAimSync();
    CheckMenuBoard();
    CheckBarrelFromCone();
    CheckRangerSecondAim();
    CheckVehicleHandAimDefaults();
    CheckVehicleRecoilStaleState();
    CheckShoulderStraight();
    printf("Ranger dual production checks: %d failures\n",failures);return failures?1:0;
}
