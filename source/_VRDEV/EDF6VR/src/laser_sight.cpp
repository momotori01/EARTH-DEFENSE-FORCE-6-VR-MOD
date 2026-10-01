#include "laser_sight.h"
#include "first_person.h"
#include <cmath>
#include <cstring>
namespace edf6vr {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
LaserSightFrame published[2]{};   // 0 the left hand's weapon, 1 the equipped one
LaserSightStats stats{};
using QueryCtor=void* (__fastcall*)(void*);
QueryCtor original=nullptr;
void* __fastcall PrepareQuery(void* query,void* attachment) {
    auto result=original(query);
    // Native update has computed +90/+A0 but has not read them into the ray.
    // Modify its derived output, never the shared weapon transform/bones.
    ApplyLaserMuzzle(attachment,GetTickCount64());
    return result;
}
}
void PublishLaserFrame(const LaserSightFrame& next,unsigned hand) noexcept {
    if(hand>1) return;
    LaserSightFrame checked{};
    if(next.weapon && next.soldier && next.nodes && next.count && next.count<=64) {
        bool ok=true;
        for(int k=0;k<3;++k) for(int j=0;j<3;++j) if(!std::isfinite(next.hand.axes[k][j])) ok=false;
        for(int j=0;j<3;++j) if(!std::isfinite(next.hand.palm[j]) || !std::isfinite(next.root[j])) ok=false;
        if(ok) checked=next;
    }
    AcquireSRWLockExclusive(&lock);
    published[hand]=checked;
    ReleaseSRWLockExclusive(&lock);
}
void ClearLaserFrame(unsigned hand) noexcept { PublishLaserFrame(LaserSightFrame{},hand); }
void ClearLaserMuzzle() noexcept { ClearLaserFrame(0); ClearLaserFrame(1); }
// The origin the game itself has just derived, carried onto the tracked hand by
// the weapon's own node root read in that same instant.
//
// Taking the origin from the weapon object transform instead, read a pass
// earlier beside the node root, is what 1.1.1-dev5 did. Two fields read
// together are not two fields written together: the object transform and this
// skeleton are filled by different passes, so the lever between them carried
// whatever the original model had done in between. That is what dragged the
// origin about, and it is nearly a metre long, so the leftover swung with the
// wrist as well. 6890E6..689181 builds this origin from the weapon's second
// matrix a few instructions before the call we are inside, so here, and only
// here, the two describe one pose. It is the carry the muzzle flash already
// uses, and the flash lands where the game put it.
//
// `refused` walks forward through the guards, so whichever one turned the call
// away is the one counted.
bool ApplyLaserMuzzle(void* attachment,ULONGLONG now) noexcept {
    AcquireSRWLockExclusive(&lock);
    bool applied=false; ++stats.calls;
    std::uint64_t* refused=&stats.stale;
    __try {
        // Whichever hand holds the weapon this attachment belongs to. A laser on
        // the weapon neither hand is holding is left exactly as the game made it.
        auto bytes=static_cast<unsigned char*>(attachment);
        const bool readable=Readable(attachment,0xB0,true);
        const void* owner=readable?*reinterpret_cast<const void**>(bytes+8):nullptr;
        LaserSightFrame frame{};
        bool live=false;
        for(unsigned h=0;h<2;++h) {
            const LaserSightFrame& candidate=published[h];
            if(!candidate.weapon || !candidate.soldier || !candidate.nodes
               || now<candidate.time || now-candidate.time>=250) continue;
            live=true;
            if(candidate.weapon==owner) frame=candidate;
        }
        live=live && readable;
        if(live && !frame.weapon && owner && Readable(owner,0x128)) {
            const void* ownerSoldier=*reinterpret_cast<void* const*>(static_cast<const unsigned char*>(owner)+0x120);
            for(unsigned h=0;h<2;++h) {
                stats.frameWeapons[h]=published[h].weapon;
                if(published[h].weapon && published[h].soldier==ownerSoldier) {
                    ++stats.ownSoldierUnframed; stats.lastUnframedOwner=const_cast<void*>(owner); break;
                }
            }
        }
        auto registry=static_cast<unsigned char*>(frame.weapon)+kWeaponModelOffset+0xA0;
        auto soldier=static_cast<unsigned char*>(frame.soldier);
        const bool owned=live && frame.weapon && frame.weapon==owner;
        if(live) refused=&stats.owner;
        const bool known=owned && Readable(registry,0x28) && Readable(soldier,0x318)
            && *reinterpret_cast<void**>(registry+0x10)==frame.nodes
            && *reinterpret_cast<std::uint64_t*>(registry+0x20)==frame.count
            && *reinterpret_cast<const std::uint32_t*>(soldier+0x314)==frame.objectId
            && Readable(frame.nodes,0xF0);
        if(owned) refused=&stats.identity;
        if(known) {
            const auto root=*reinterpret_cast<const Matrix*>(
                static_cast<const unsigned char*>(frame.nodes)+0xB0);
            auto origin=reinterpret_cast<float*>(bytes+0x90);
            WeaponHoldFrame hand=frame.hand;
            // The soldier has walked on since the destination was measured.
            // Carry the hand with him rather than leave the origin behind.
            const auto walked=reinterpret_cast<const float*>(soldier+0x90);
            float travel=0,lever=0,carry[3]{};
            for(int j=0;j<3;++j) {
                const float moved=walked[j]-frame.root[j];
                carry[j]=moved;
                const float arm=origin[j]-root.m[3][j];
                travel+=moved*moved; lever+=arm*arm;
                hand.palm[j]+=moved;
            }
            refused=&stats.geometry;
            edf6vr::Matrix unused{}; float before=0,after=0,carried[3]{};
            const bool sane=std::isfinite(travel) && travel<400 && std::isfinite(lever) && lever<25;
            if(sane) refused=&stats.refused;
            if(sane && CarryWeaponBones(root,hand,&root,1,&unused,before,after,origin,carried)) {
                refused=&stats.reach;
                float away=0;
                for(int j=0;j<3;++j) {
                    if(!std::isfinite(carried[j])) { away=1e30f; break; }
                    const float d=carried[j]-hand.palm[j];
                    away+=d*d;
                }
                if(std::isfinite(away) && away<25) {
                    std::memcpy(stats.native,origin,sizeof(stats.native));
                    std::memcpy(origin,carried,sizeof(carried)); // W lane untouched
                    const float reach=std::sqrt(lever);
                    if(reach<stats.leverMin) stats.leverMin=reach;
                    if(reach>stats.leverMax) stats.leverMax=reach;
                    ++stats.carried;
                    // Native +A0 comes from the rolling attachment forward row.
                    // Keep its length/W and the range the game fires the ray to,
                    // but aim it with the same tracked frame the visible weapon
                    // is drawn in.
                    auto direction=reinterpret_cast<float*>(bytes+0xA0);
                    float directionSquare=0,aimSquare=0;
                    for(int j=0;j<3;++j) {
                        directionSquare+=direction[j]*direction[j];
                        aimSquare+=hand.axes[2][j]*hand.axes[2][j];
                    }
                    if(std::isfinite(directionSquare) && directionSquare>1e-8f
                       && std::isfinite(aimSquare) && aimSquare>1e-8f) {
                        const float scale=std::sqrt(directionSquare/aimSquare);
                        for(int j=0;j<3;++j) direction[j]=hand.axes[2][j]*scale;
                        ++stats.aimed;
                    }
                    std::memcpy(stats.muzzle,carried,sizeof(stats.muzzle));
                    std::memcpy(stats.walked,carry,sizeof(stats.walked));
                    stats.walkedAt=now; stats.walkedWeapon=frame.weapon;
                    ++stats.applied; applied=true;
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(!applied) ++*refused;
    ReleaseSRWLockExclusive(&lock);
    return applied;
}
LaserSightStats ReadLaserSightStats() noexcept {
    AcquireSRWLockShared(&lock); auto result=stats; ReleaseSRWLockShared(&lock); return result;
}
bool InstallLaserSight(const ImageProfile& image,bool& changed) noexcept {
    changed=false;
    // OnUpdate: RBX = WeaponAttachment_LaserSight, RCX = stack query object.
    constexpr unsigned char expected[]={0x48,0x8D,0x4D,0x40,0xE8,0x56,0xD9,0xB1,0x00,
        0x48,0x8D,0x05,0x8F,0xFD,0x0D,0x01};
    if(!image.base || !Readable(image.base+kLaserPrepareCall-4,sizeof(expected)) ||
       std::memcmp(image.base+kLaserPrepareCall-4,expected,sizeof(expected)) ||
       !Readable(image.base+0x17E2468,8) ||
       *reinterpret_cast<void**>(image.base+0x17E2468)!=image.base+0x6890A0) return false;
    // Inject RBX as second argument; preserve the caller's nonvolatile registers,
    // stack and original query ctor return value. No lock crosses native code.
    unsigned char code[]={0x48,0x89,0xDA,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    auto target=reinterpret_cast<void*>(&PrepareQuery); std::memcpy(code+5,&target,8);
    auto* bridge=VirtualAlloc(nullptr,sizeof(code),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!bridge) return false;
    std::memcpy(bridge,code,sizeof(code)); DWORD old=0;
    if(!VirtualProtect(bridge,sizeof(code),PAGE_EXECUTE_READ,&old)) { VirtualFree(bridge,0,MEM_RELEASE); return false; }
    FlushInstructionCache(GetCurrentProcess(),bridge,sizeof(code));
    original=reinterpret_cast<QueryCtor>(image.base+kLaserQueryCtor);
    const bool ok=RedirectCall(image.base+kLaserPrepareCall,reinterpret_cast<void*>(original),bridge,changed);
    if(!changed) VirtualFree(bridge,0,MEM_RELEASE);
    return ok;
}
}
