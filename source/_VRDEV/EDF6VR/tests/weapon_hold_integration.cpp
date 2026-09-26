// Actual production draw hook, synthetic game objects. No HMD or game code.
#include "../src/plugin.cpp"
#include <thread>
#include <limits>

static int failures=0,draws=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
alignas(16) static unsigned char soldier[0x2100]{},model[0x500]{},bodyBones[2*0x110]{},weaponBones[4*0x110]{};
static edf6vr::Matrix originals[4]{},arm{};
static edf6vr::Matrix renderPalette[4]{};
static bool nested=false,raiseDraw=false,expectPlaced=true;
template<class T> static T& Field(void* base,std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(base)+offset);
}
static void __fastcall FakeDraw(void* object,void* context,int pass,void* view) {
    ++draws;
    if(object!=model) return;
    // A callback can take the ordinary update lock while the pose is borrowed.
    AcquireSRWLockShared(&g_lock);
    ReleaseSRWLockShared(&g_lock);
    for(unsigned b=0;b<4;++b) {
        // 11009A0 consumes the separate render palette, not node+B0.
        const auto& current=renderPalette[b];
        CHECK(!std::memcmp(&Field<edf6vr::Matrix>(weaponBones+b*0x110,0xB0),&originals[b],64));
        for(unsigned row=0;row<4;++row) for(unsigned j=0;j<4;++j) {
            // Carried from the palette's own root, not from the arm node: the
            // two are copies of the same skeleton taken at different moments,
            // and the source has to come from the snapshot being drawn.
            const float expected=expectPlaced && row==3 && j<3
                ? originals[b].m[row][j]-originals[0].m[row][j]+static_cast<float>(2+j)
                : originals[b].m[row][j];
            CHECK(std::fabs(current.m[row][j]-expected)<0.0001f);
        }
    }
    CHECK(!std::memcmp(&Field<edf6vr::Matrix>(bodyBones,0xB0),&arm,64));
    if(nested) { nested=false; HookModelDraw(object,context,pass,view); }
    if(raiseDraw) RaiseException(0xE0423612,0,0,nullptr);
}
static void CheckRestored() {
    for(unsigned b=0;b<4;++b) {
        CHECK(!std::memcmp(&Field<edf6vr::Matrix>(weaponBones+b*0x110,0xB0),&originals[b],64));
        CHECK(!std::memcmp(&renderPalette[b],&originals[b],64));
    }
    CHECK(!g_insideHoldBorrow);
}
static void ExceptionCheck() {
    raiseDraw=true;
    bool caught=false;
    __try { HookModelDraw(model,nullptr,2,nullptr); }
    __except(GetExceptionCode()==0xE0423612?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { caught=true; }
    raiseDraw=false;
    CHECK(caught);
    CheckRestored();
    HookModelDraw(model,nullptr,2,nullptr); // finally released the lock
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    // A rotated source arm and a differently rotated destination must preserve
    // the local attachment, including during a fresh animation pose next frame.
    const edf6vr::Matrix rotatedArm={{{0,0,-2,0},{0,3,0,0},{4,0,0,0},{400,20,-500,1}}};
    edf6vr::WeaponHoldFrame rotatedHand{{{1,0,0},{0,0,1},{0,-1,0}},{5,6,7}};
    edf6vr::Matrix rotatedBone=rotatedArm,carried{};
    rotatedBone.m[3][0]+=0.4f; rotatedBone.m[3][1]+=0.1f; rotatedBone.m[3][2]-=0.2f;
    float before=0,after=0;
    CHECK(edf6vr::CarryWeaponBones(rotatedArm,rotatedHand,&rotatedBone,1,&carried,before,after));
    CHECK(std::fabs(carried.m[3][0]-5.2f)<0.0001f);
    CHECK(std::fabs(carried.m[3][1]-5.6f)<0.0001f);
    CHECK(std::fabs(carried.m[3][2]-7.1f)<0.0001f);
    CHECK(carried.m[0][0]==2 && carried.m[1][2]==3 && carried.m[2][1]==-4);
    CHECK(std::fabs(before-after)<0.0001f && carried.m[3][3]==1);
    // Sample native muzzle/root together, then place that local point using a
    // later hand. A differently animated render palette must never be mixed in.
    for(int roll=0;roll<360;++roll) {
        const float a=roll*3.14159265f/180,c=std::cos(a),s=std::sin(a);
        const edf6vr::Matrix animated={{{c,s,0,0},{-s,c,0,0},{0,0,1,0},{100,-2,300,1}}};
        const float muzzle[3]={100+.2f*c-.1f*s,-2+.2f*s+.1f*c,300.3f};
        float local[3]{},placed[3]{};
        CHECK(edf6vr::WeaponLocalPoint(animated,muzzle,local));
        CHECK(edf6vr::PlaceWeaponLocalPoint(rotatedHand,local,placed));
        CHECK(std::fabs(placed[0]-5.2f)<.0001f && std::fabs(placed[1]-5.7f)<.0001f && std::fabs(placed[2]-7.1f)<.0001f);
    }
    float invalidPoint[3]={NAN,0,0},refusedPoint[3]{};
    CHECK(!edf6vr::WeaponLocalPoint(rotatedArm,invalidPoint,refusedPoint));
    CHECK(!edf6vr::PlaceWeaponLocalPoint(rotatedHand,invalidPoint,refusedPoint));
    // A destination of the opposite handedness is orthonormal, so nothing above
    // catches it, and it turns the move into a reflection: the weapon reaches
    // the renderer inside out and culling removes the half facing the player.
    // That was the missing left half of the rifle and the launcher.
    edf6vr::WeaponHoldFrame mirrored=rotatedHand;
    for(int j=0;j<3;++j) mirrored.axes[0][j]=-mirrored.axes[0][j];
    edf6vr::Matrix refused{};
    CHECK(!edf6vr::CarryWeaponBones(rotatedArm,mirrored,&rotatedBone,1,&refused,before,after));
    auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[256]{};
    if(!mapped || !edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason))) return 2;
    Field<void*>(soldier,0)=g_image.base+0x17CDF28;
    Field<unsigned>(soldier,0x314)=17;
    Field<void*>(soldier,0x910)=bodyBones;
    Field<std::uint64_t>(soldier,0x920)=2;
    Field<void*>(model,0xB0)=weaponBones;
    Field<std::uint64_t>(model,0xC0)=4;
    Field<void*>(model,0xD0)=renderPalette;
    Field<void*>(model,0x1B8)=model+0xA0;
    arm={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{100,20,-250,1}}};
    Field<edf6vr::Matrix>(bodyBones,0xB0)=arm;
    for(unsigned b=0;b<4;++b) {
        originals[b]=arm;
        originals[b].m[0][0]=1+static_cast<float>(b); // preserve bone scale
        originals[b].m[3][0]+=0.2f;
        originals[b].m[3][1]+=0.1f;
        originals[b].m[3][2]+=0.4f+static_cast<float>(b)*0.1f;
        Field<edf6vr::Matrix>(weaponBones+b*0x110,0xB0)=originals[b];
        renderPalette[b]=originals[b];
    }
    g_modelOriginal=&FakeDraw; g_weaponFollowsHand=true; g_findWeaponModel=false;
    g_holdCommand.model=model; g_holdCommand.soldier=soldier; g_holdCommand.bodyNodes=bodyBones;
    g_fastDrawWeapon.store(model);
    g_holdCommand.weapon=model;g_holdCommand.muzzleLocalValid=true;
    g_holdCommand.muzzleLocal[0]=.2f;g_holdCommand.muzzleLocal[1]=.1f;g_holdCommand.muzzleLocal[2]=.3f;
    g_holdCommand.weaponWas[0]=10000; // unrelated old world point must be ignored
    g_holdCommand.armsNode=bodyBones; g_holdCommand.weaponNodes=weaponBones;
    g_holdCommand.count=4; g_holdCommand.objectId=17; g_holdCommand.refreshed=GetTickCount64();
    g_holdCommand.updateThread=GetCurrentThreadId();
    for(unsigned j=0;j<3;++j) { g_holdCommand.hand.axes[j][j]=1; g_holdCommand.hand.palm[j]=static_cast<float>(2+j); }
    for(int i=0;i<1000;++i) { HookModelDraw(model,nullptr,2,nullptr); CheckRestored(); }
    CHECK(g_holdStats.applied==1000 && g_holdStats.rejected==0);
    CHECK(std::fabs(g_muzzleFrame.point[0]-2.2f)<.0001f && std::fabs(g_muzzleFrame.point[1]-3.1f)<.0001f
        && std::fabs(g_muzzleFrame.point[2]-4.3f)<.0001f);
    CHECK(g_holdStats.errorMax<0.0001f && g_holdStats.targetMax-g_holdStats.targetMin<0.0001f);
    nested=true; HookModelDraw(model,nullptr,2,nullptr); CheckRestored();
    CHECK(g_holdStats.nested==1);
    ExceptionCheck();
    // Two submitters of the same mesh cannot read each other's temporary pose.
    g_holdCommand.refreshed=GetTickCount64();
    std::thread a([] { for(int i=0;i<500;++i) HookModelDraw(model,nullptr,2,nullptr); });
    std::thread b([] { for(int i=0;i<500;++i) HookModelDraw(model,nullptr,2,nullptr); });
    a.join(); b.join(); CheckRestored();
    CHECK(g_holdStats.changed==0 && g_holdStats.restoreFaults==0);
    // Native LOD thresholds select descriptor+58; special pass 2 instead
    // takes model+A0. Both paths must resolve the palette actually uploaded.
    alignas(16) unsigned char lods[2*0xF0]{},lodRegistry[0x40]{};
    edf6vr::Matrix alternate[4]{};
    Field<void*>(model,0x250)=lods; Field<std::uint64_t>(model,0x260)=2;
    Field<float>(model,0x24)=3;
    Field<float>(lods,0xE8)=4; Field<float>(lods+0xF0,0xE8)=16;
    Field<void*>(lods,0x58)=lodRegistry;
    Field<std::int32_t>(lodRegistry,0x20)=4; Field<void*>(lodRegistry,0x30)=alternate;
    edf6vr::Matrix* resolved=nullptr; std::size_t resolvedCount=0;
    CHECK(ResolveHoldPalette(model,1,resolved,resolvedCount) && resolved==alternate && resolvedCount==4);
    Field<unsigned char>(model,0x482)=1;
    CHECK(ResolveHoldPalette(model,2,resolved,resolvedCount) && resolved==renderPalette);
    Field<unsigned char>(model,0x482)=0;
    Field<std::uint64_t>(model,0x260)=0;
    CHECK(ResolveHoldPalette(model,2,resolved,resolvedCount) && resolved==renderPalette);
    CHECK(!ResolveHoldPalette(model,4,resolved,resolvedCount));
    expectPlaced=false;
    Field<unsigned>(soldier,0x314)=18; HookModelDraw(model,nullptr,2,nullptr); CheckRestored();
    Field<unsigned>(soldier,0x314)=17;
    g_holdCommand.refreshed=0; HookModelDraw(model,nullptr,2,nullptr); CheckRestored();
    g_holdCommand.refreshed=GetTickCount64();
    Field<std::uint64_t>(model,0xC0)=3; HookModelDraw(model,nullptr,2,nullptr); CheckRestored();
    Field<std::uint64_t>(model,0xC0)=4;
    g_holdCommand.hand.palm[0]=std::numeric_limits<float>::infinity();
    HookModelDraw(model,nullptr,2,nullptr); CheckRestored();
    CHECK(g_holdStats.rejected==4);
    g_holdCommand.hand.palm[0]=2;
    Field<void*>(model,0xD0)=nullptr;
    HookModelDraw(model,nullptr,2,nullptr); CheckRestored();
    CHECK(g_holdStats.rejected==5);
    Field<void*>(model,0xD0)=renderPalette;
    auto untouched=Field<edf6vr::Matrix>(bodyBones,0xB0);
    HookModelDraw(bodyBones,nullptr,2,nullptr);
    CHECK(!std::memcmp(&untouched,&Field<edf6vr::Matrix>(bodyBones,0xB0),64));
    expectPlaced=true;
    for(const auto table:{0x17D0FF8u,0x17CF100u}) {
        Field<void*>(soldier,0)=g_image.base+table;
        g_holdCommand.refreshed=GetTickCount64();
        const auto applied=g_holdStats.applied;
        for(int frame=0;frame<30;++frame) { HookModelDraw(model,nullptr,2,nullptr); CheckRestored(); }
        CHECK(g_holdStats.applied==applied+30);
    }
    Field<void*>(soldier,0)=g_image.base+0x17CF5B8;
    expectPlaced=false;
    const auto applied=g_holdStats.applied;
    HookModelDraw(model,nullptr,2,nullptr); CheckRestored();
    CHECK(g_holdStats.applied==applied);
    // Unrelated world/NPC draws must complete even while camera/hand updates
    // own g_lock. Use a bounded wait so a regression fails instead of hanging.
    const HANDLE completed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    CHECK(completed!=nullptr);
    if(completed) {
        AcquireSRWLockExclusive(&g_lock);
        std::thread unrelated([&] {HookModelDraw(bodyBones,nullptr,2,nullptr);SetEvent(completed);});
        CHECK(WaitForSingleObject(completed,1000)==WAIT_OBJECT_0);
        ReleaseSRWLockExclusive(&g_lock);
        unrelated.join();CloseHandle(completed);
    }
    FreeLibrary(mapped);
    printf("Weapon draw borrow: %d failures, %d forwarded draws; reentry, parallel draws, exception restore, owner guards\n",failures,draws);
    return failures?1:0;
}
