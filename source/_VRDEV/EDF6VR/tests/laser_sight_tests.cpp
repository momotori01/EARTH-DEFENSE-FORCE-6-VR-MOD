// Mapped EDF is data only. Execute our bridge with a fake query constructor.
#include "../src/laser_sight.cpp"
#include <cstdio>
#include <limits>
#include <vector>
namespace { int calls=0; void* __fastcall FakeCtor(void* p) { ++calls; return p; } }

namespace {
// A weapon laid out the way the hook walks it: model at +kWeaponModelOffset,
// its registry at +A0 of that, the node array at +10 with the count at +20,
// and each node's world matrix at +B0.
struct FakeWeapon {
    std::vector<unsigned char> object,nodes,soldier;
    FakeWeapon():object(edf6vr::kWeaponModelOffset+0x200,0),nodes(4*0x110,0),soldier(0x400,0) {
        auto registry=object.data()+edf6vr::kWeaponModelOffset+0xA0;
        *reinterpret_cast<void**>(registry+0x10)=nodes.data();
        *reinterpret_cast<std::uint64_t*>(registry+0x20)=4;
    }
    void* weapon() { return object.data(); }
    edf6vr::Matrix& root() { return *reinterpret_cast<edf6vr::Matrix*>(nodes.data()+0xB0); }
    float* soldierRoot() { return reinterpret_cast<float*>(soldier.data()+0x90); }
    std::uint32_t& objectId() { return *reinterpret_cast<std::uint32_t*>(soldier.data()+0x314); }
};
// The same carry the drawn bones get, worked out independently of the code
// under test: the origin in the root's own frame, put back on the hand.
void Expected(const edf6vr::Matrix& root,const edf6vr::WeaponHoldFrame& hand,
              const float* origin,float* out) {
    float axis[3][3]{};
    for(int k=0;k<3;++k) {
        float norm=0;
        for(int j=0;j<3;++j) norm+=root.m[k][j]*root.m[k][j];
        norm=std::sqrt(norm);
        for(int j=0;j<3;++j) axis[k][j]=root.m[k][j]/norm;
    }
    float local[3]{};
    for(int k=0;k<3;++k) for(int j=0;j<3;++j) local[k]+=(origin[j]-root.m[3][j])*axis[k][j];
    for(int j=0;j<3;++j) {
        out[j]=hand.palm[j];
        for(int k=0;k<3;++k) out[j]+=local[k]*hand.axes[k][j];
    }
}
bool Near(const float* a,const float* b,float tolerance) {
    for(int j=0;j<3;++j) if(std::fabs(a[j]-b[j])>tolerance) return false;
    return true;
}
}

int wmain(int argc,wchar_t** argv) {
    using namespace edf6vr;
    if(argc!=2) return 2;
    int failures=0;
    auto check=[&](bool ok,const char* why) { if(!ok) { ++failures; std::printf("FAIL %s\n",why); } };

    FakeWeapon gun; int other=0;
    alignas(16) unsigned char attachment[0xE0]{},before[0xE0]{};
    *reinterpret_cast<void**>(attachment+8)=gun.weapon();
    auto origin=reinterpret_cast<float*>(attachment+0x90);
    auto direction=reinterpret_cast<float*>(attachment+0xA0);

    // The weapon sits away from the origin of the world, turned, and the laser
    // leaves it three quarters of a metre down the barrel -- the lever the real
    // game was measured to have.
    gun.root()={{{0,0,1,0},{0,1,0,0},{-1,0,0,0},{5,1,-7,1}}};
    const float barrel[3]={5.0f,1.10f,-7.78f};
    auto placeNative=[&]{ for(int j=0;j<3;++j) origin[j]=barrel[j]; origin[3]=1; };
    placeNative();
    gun.soldierRoot()[0]=5; gun.soldierRoot()[1]=0; gun.soldierRoot()[2]=-7;
    gun.objectId()=0x4242;

    LaserSightFrame frame{};
    frame.weapon=gun.weapon(); frame.soldier=gun.soldier.data();
    frame.nodes=gun.nodes.data(); frame.count=4; frame.objectId=0x4242;
    frame.hand={{{1,0,0},{0,1,0},{0,0,1}},{2,1.5f,3}};
    for(int j=0;j<3;++j) frame.root[j]=gun.soldierRoot()[j];
    frame.time=1000;

    std::memcpy(before,attachment,sizeof(before));
    ClearLaserMuzzle(); check(!ApplyLaserMuzzle(attachment,1001),"off");
    { auto wrong=frame; wrong.weapon=&other; PublishLaserFrame(wrong); }
    check(!ApplyLaserMuzzle(attachment,1001),"other weapon");
    PublishLaserFrame(frame);
    check(!ApplyLaserMuzzle(attachment,1250) && !ApplyLaserMuzzle(attachment,999),"stale/future");

    float want[3]{};
    Expected(gun.root(),frame.hand,barrel,want);
    for(int i=0;i<1000;++i) {
        placeNative();   // native recomputation every tick
        check(ApplyLaserMuzzle(attachment,1001),"owner applies");
        check(Near(origin,want,1e-4f),"carried origin, no accumulation");
        check(origin[3]==1,"W preserved");
    }
    check(!std::memcmp(before,attachment,0x90) && !std::memcmp(before+0x9C,attachment+0x9C,sizeof(before)-0x9C),
          "direction/range/visibility and all other bytes unchanged");

    // The whole reason for reading the origin and the root together. Turn and
    // move the original model as a rigid body and the carried origin must not
    // move at all: that is the drag the player reported.
    for(int turn=0;turn<360;++turn) {
        const float angle=turn*3.14159265f/180;
        const float c=std::cos(angle),s=std::sin(angle);
        edf6vr::Matrix spun={{{c,0,s,0},{0,1,0,0},{-s,0,c,0},{5+0.3f*s,1+0.2f*c,-7+0.4f*s,1}}};
        gun.root()=spun;
        // the game derives its origin from the same pose, so it turns with it
        const float local[3]={0,0.10f,-0.78f};
        for(int j=0;j<3;++j) {
            origin[j]=spun.m[3][j];
            for(int k=0;k<3;++k) origin[j]+=local[k]*spun.m[k][j];
        }
        origin[3]=1;
        check(ApplyLaserMuzzle(attachment,1001),"rigid model applies");
        float steady[3]{};
        for(int j=0;j<3;++j) {
            steady[j]=frame.hand.palm[j];
            for(int k=0;k<3;++k) steady[j]+=local[k]*frame.hand.axes[k][j];
        }
        check(Near(origin,steady,1e-4f),"origin does not follow the original model");
    }

    // The soldier walking on carries the destination with him, exactly once.
    gun.root()={{{0,0,1,0},{0,1,0,0},{-1,0,0,0},{5,1,-7,1}}};
    placeNative();
    gun.soldierRoot()[0]=5.5f; gun.soldierRoot()[2]=-7.25f;
    check(ApplyLaserMuzzle(attachment,1001),"walked applies");
    float walked[3]={want[0]+0.5f,want[1],want[2]-0.25f};
    check(Near(origin,walked,1e-4f),"destination carried by the soldier step");
    gun.soldierRoot()[0]=5; gun.soldierRoot()[2]=-7;

    // Guards.
    placeNative(); origin[0]=100;
    check(!ApplyLaserMuzzle(attachment,1001),"native origin too far from its own root");
    placeNative(); origin[1]=std::numeric_limits<float>::quiet_NaN();
    check(!ApplyLaserMuzzle(attachment,1001),"invalid native origin");
    placeNative(); gun.objectId()=0x4243;
    check(!ApplyLaserMuzzle(attachment,1001),"soldier identity changed");
    gun.objectId()=0x4242;
    *reinterpret_cast<void**>(gun.object.data()+kWeaponModelOffset+0xA0+0x10)=nullptr;
    check(!ApplyLaserMuzzle(attachment,1001),"weapon skeleton replaced");
    *reinterpret_cast<void**>(gun.object.data()+kWeaponModelOffset+0xA0+0x10)=gun.nodes.data();
    gun.soldierRoot()[0]=100;
    check(!ApplyLaserMuzzle(attachment,1001),"soldier teleported");
    gun.soldierRoot()[0]=5;
    check(ApplyLaserMuzzle(attachment,1001),"recovers once the guards pass");

    // Aim: the native ray keeps its length, W and range; only the direction is
    // replaced with the tracked forward the weapon is drawn along.
    {
        auto aimed=frame; aimed.hand.axes[2][0]=0; aimed.hand.axes[2][1]=0; aimed.hand.axes[2][2]=1;
        PublishLaserFrame(aimed);
    }
    for(int roll=0;roll<360;++roll) {
        placeNative();
        const float angle=roll*3.14159265f/180;
        direction[0]=3*std::sin(angle);direction[1]=3*std::cos(angle);direction[2]=0;direction[3]=17;
        check(ApplyLaserMuzzle(attachment,1001),"rolling laser applies");
        check(std::fabs(direction[0])<1e-5f && std::fabs(direction[1])<1e-5f
            && std::fabs(direction[2]-3)<1e-4f && direction[3]==17,"tracked forward, original length and W");
    }
    check(!std::memcmp(before,attachment,0x90) && !std::memcmp(before+0xB0,attachment+0xB0,0x30),
          "laser only changes the derived ray xyz");
    {
        auto broken=frame;
        broken.hand.axes[2][1]=std::numeric_limits<float>::quiet_NaN();
        PublishLaserFrame(broken);
        placeNative(); direction[0]=1;direction[1]=direction[2]=0;
        check(!ApplyLaserMuzzle(attachment,1001) && direction[0]==1,"invalid destination publishes nothing");
    }
    PublishLaserFrame(frame);
    placeNative(); direction[0]=direction[1]=direction[2]=0;
    check(ApplyLaserMuzzle(attachment,1001) && direction[0]==0 && direction[2]==0,"zero-length native ray preserved");
    ClearLaserMuzzle(); check(!ApplyLaserMuzzle(attachment,1001),"clear on VR off");

    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    ImageProfile image{}; char why[256]{};
    if(!module || !CheckImage(module,image,why,sizeof(why))) return 2;
    bool changed=false; check(InstallLaserSight(image,changed) && changed,"actual signature and redirect");
    check(!InstallLaserSight(image,changed) && !changed,"conflicting hook refused");
    std::int32_t disp=0; std::memcpy(&disp,image.base+kLaserPrepareCall+1,4);
    auto nearJump=image.base+kLaserPrepareCall+5+disp;
    void* bridge=nullptr; std::memcpy(&bridge,nearJump+2,8);
    original=&FakeCtor;
    unsigned char code[]={0x53,0x48,0x89,0xD3,0x48,0x83,0xEC,0x20,0x48,0xB8,
        0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x83,0xC4,0x20,0x5B,0xC3};
    std::memcpy(code+10,&bridge,8);
    auto page=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    check(page!=nullptr,"harness allocation");
    if(page) {
        std::memcpy(page,code,sizeof(code)); DWORD old=0;
        const bool executable=VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old)!=0;
        check(executable,"harness executable"); FlushInstructionCache(GetCurrentProcess(),page,sizeof(code));
        if(executable) {
            std::memcpy(attachment,before,sizeof(before));
            *reinterpret_cast<void**>(attachment+8)=gun.weapon();
            placeNative();
            auto live=frame; live.time=GetTickCount64();
            PublishLaserFrame(live);
            int query=0;
            const auto invoke=reinterpret_cast<void*(__fastcall*)(void*,void*)>(page);
            check(invoke(&query,attachment)==&query && calls==1,"original constructor return and ABI");
            check(Near(origin,want,1e-4f),"bridge carries attachment RBX");
        }
        VirtualFree(page,0,MEM_RELEASE);
    }
    FreeLibrary(module);
    const auto summary=ReadLaserSightStats();
    check(summary.carried!=0 && summary.leverMax>0.7f && summary.leverMax<0.9f
          && summary.leverMin>0.7f && summary.leverMin<0.9f,"lever band reports the rigid barrel length");
    std::printf("Laser origin carry, ownership and native-call bridge: %d failures\n",failures);
    return failures?1:0;
}
