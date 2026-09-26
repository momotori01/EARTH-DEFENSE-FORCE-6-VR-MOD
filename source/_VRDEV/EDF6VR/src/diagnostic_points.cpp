#include "diagnostic_points.h"
#include "first_person.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace edf6vr {
namespace {
template<class T> const T& At(const void* p,std::size_t o) noexcept {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(p)+o);
}
}
bool CheckRequestPointProfile(const ImageProfile& image) noexcept {
    __try {
        const unsigned char read[]={0x48,0x8B,0x15,0xAD,0xEA,0xA1,0x01,0x48,0x6B,0xC9,0x38,
            0x48,0x8B,0x42,0x38,0xF3,0x0F,0x10,0x74,0x01,0x30};
        const unsigned char consume[]={0x29,0x86,0x68,0x0E,0,0,0xF3,0x0F,0x11,0xB6,0x80,0x0E,0,0};
        const unsigned char owned[]={0x48,0x8B,0xBB,0x50,0x19,0,0,0x48,0x8B,0x83,0x60,0x19,0,0,0x48,0x8D,0x34,0xC7};
        const unsigned char complete[]={0x83,0xBE,0x68,0x0E,0,0,0,0x7F,0x2F,0x84,0xC9,0x75,0x2B,
            0x38,0x8E,0x40,0x01,0,0,0x75,0x23,0x48,0x8B,0xCE,0xE8,0xC2,0x1D,0,0};
        const unsigned char ammo[]={0x89,0x87,0xE8,0x0B,0,0};
        return image.base && !std::memcmp(image.base+0x59B428,owned,sizeof owned)
            && !std::memcmp(image.base+0x693F41,complete,sizeof complete)
            && !std::memcmp(image.base+0x695D80,ammo,sizeof ammo) && !std::memcmp(image.base+0x693EC4,read,sizeof read)
            && !std::memcmp(image.base+0x693F18,consume,sizeof consume);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned OwnedWeaponCount(void* soldier) noexcept {
    __try {
        if(!Readable(soldier,0x1968)) return 0;
        const auto count=At<std::uint64_t>(soldier,0x1960);
        auto array=At<void*>(soldier,0x1950);
        return count && count<=32 && Readable(array,count*8)?static_cast<unsigned>(count):0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void* OwnedWeaponAt(void* soldier,unsigned index) noexcept {
    __try {
        if(index>=OwnedWeaponCount(soldier)) return nullptr;
        return At<void*>(At<void*>(soldier,0x1950),index*8);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
int GrantInitialRequestPoints(const ImageProfile& image,void* soldier,int initial,
    RequestPointBudget& state,bool& allocated) noexcept {
    allocated=false;
    __try {
        if(initial<=0 || initial>1000000 || !IsSupportedSoldier(image,soldier)
            || !Readable(soldier,0x1968) || !At<void*>(soldier,0x340)) return 0;
        const auto id=At<unsigned>(soldier,0x314); if(id>=7) return 0;
        auto manager=At<void*>(image.base,0x20B2978);
        if(!Readable(manager,0x40)) return 0;
        auto entries=At<unsigned char*>(manager,0x38);
        if(!entries) return 0;
        auto ledger=reinterpret_cast<const float*>(entries+id*0x38+0x30);
        if(!Readable(ledger,4)) return 0;
        const float total=*ledger;
        if(!std::isfinite(total) || total<0 || total>10000000) return 0;
        const auto count=OwnedWeaponCount(soldier);
        if(!count) return 0; // Wait until the initial loadout exists.
        auto array=At<void*>(soldier,0x1950);
        if(state.owner!=soldier || state.ownerId!=id || state.ledger!=ledger
            || total+1<state.lastTotal) {
            state={};state.owner=soldier;state.ownerId=id;
            state.ledger=const_cast<float*>(ledger);state.weapons=array;
            state.remaining=initial;
        }
        state.lastTotal=total;
        if(state.applied) return 0;
        // Do not freeze a partly constructed loadout. This is the owning game's
        // update thread, before native weapon updates consume reload state.
        for(unsigned i=0;i<count;++i) {
            auto w=OwnedWeaponAt(soldier,i);
            if(!Readable(w,0xE84) || !TypeName(image,w)) return 0;
        }
        state.applied=true;state.ownedCount=count;allocated=true;
        int granted=0;
        for(unsigned i=0;i<count;++i) {
            auto weapon=OwnedWeaponAt(soldier,i);auto type=TypeName(image,weapon);
            if(!type || std::strncmp(type,".?AVWeapon_",11)!=0
                || At<void*>(weapon,0x120)!=soldier || At<int>(weapon,0x208)!=2) continue;
            ++state.requests;
            if(At<int>(weapon,0xBE8)>0) { ++state.alreadyReady;continue; }
            const int debt=At<int>(weapon,0xE68),full=At<int>(weapon,0x20C);
            if(At<int>(weapon,0xBE8)!=0 || At<int>(weapon,0x248)<=0
                || full<=0 || full>1000000 || debt<=0 || debt>full) continue;
            auto pending=reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(weapon)+0xE68);
            if(!Readable(const_cast<const LONG*>(pending),4,true)) continue;
            const int gift=std::min(state.remaining,debt);
            if(gift<=0 || InterlockedCompareExchange(pending,debt-gift,debt)!=debt) continue;
            // 693F41..693F59 performs completion with the game's own flags and
            // 695D20 restores ammo. No virtual call, shared credit, or ammo write.
            state.remaining-=gift;granted+=gift;++state.credited;
        }
        return granted;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}
