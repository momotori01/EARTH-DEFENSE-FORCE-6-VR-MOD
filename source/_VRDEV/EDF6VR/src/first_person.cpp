#include "first_person.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace edf6vr {
namespace {
template<typename T> T& At(void* p,std::size_t off) noexcept {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+off);
}
// MSVC x64 basic_string<wchar_t> input ABI as consumed at EDF.dll+1108060.
// Inline storage avoids passing allocator ownership across module boundaries.
struct GameShortString { wchar_t text[8]; std::uint64_t size; std::uint64_t capacity; };
static_assert(sizeof(GameShortString)==32);
constexpr GameShortString headName{{L'h',L'e',L'a',L'd',0,0,0,0},4,7};
constexpr GameShortString armsRightName{{L'a',L'r',L'm',L's',L'_',L'r',0,0},6,7};
constexpr GameShortString armsName{{L'a',L'r',L'm',L's',0,0,0,0},4,7};
struct SoldierClassProfile { const char* name; std::uint32_t table,update; };
constexpr SoldierClassProfile soldierClasses[]={
    {".?AVAssultSoldier@@",0x17CDF28,0x550A30},
    {".?AVPaleWing@@",0x17D0FF8,0x580520},
    {".?AVEngineer@@",0x17CF100,0x563DA0},
    {".?AVHeavyArmor@@",0x17CF5B8,0x572DF0},
};
bool BranchTo(const ImageProfile& image,std::uint32_t at,unsigned char opcode,std::uint32_t target) noexcept {
    return image.base[at]==opcode && image.base+at+5+*reinterpret_cast<const std::int32_t*>(image.base+at+1)==image.base+target;
}
bool CheckSoldierClasses(const ImageProfile& image) noexcept {
    for(const auto& entry:soldierClasses) {
        void* table=image.base+entry.table;
        if(!HasType(image,&table,entry.name)
           || *reinterpret_cast<void**>(image.base+entry.table+0x20)!=image.base+entry.update
           || (entry.update!=0x572DF0 && !BranchTo(image,entry.update,0xE9,0x572DF0))) return false;
    }
    // Derived constructors call SoldierBase, which calls HumanBase, without a
    // this adjustment. Keep the shared body/input/weapon layout version-guarded.
    return *reinterpret_cast<void**>(image.base+0x17CF5B8+0x28)==image.base+0x567780
        && BranchTo(image,0x567795,0xE8,0x59ABB0)
        && BranchTo(image,0x59B2F6,0xE8,0x578680)
        && BranchTo(image,0x57CB33,0xE8,0x58D020)
        && BranchTo(image,0x563603,0xE8,0x58D020)
        && BranchTo(image,0x58D07D,0xE8,0x56A5C0);
}
}
bool IsSupportedSoldier(const ImageProfile& image,const void* soldier) noexcept {
    for(const auto& entry:soldierClasses) if(HasType(image,soldier,entry.name)) return true;
    return false;
}
unsigned WeaponSlotCount(void* soldier) noexcept {
    __try {
        if(!Readable(soldier,0x1984)) return 0;
        const auto count=At<unsigned>(soldier,0x1980);
        const auto entries=At<void*>(soldier,0x1970);
        return count && count<=8 && Readable(entries,count*0x150)?count:0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool CheckFirstPersonProfile(const ImageProfile& image) noexcept {
    __try {
        if(!image.base) return false;
        void* table=image.base+kModelVtableRva;
        const unsigned char draw[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30};
        const unsigned char node[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x09};
        const unsigned char bodyCtor[]={0x48,0x8d,0x8e,0x60,0x08,0,0,0xe8,0x4e,0xe0,0x14,0};
        return CheckSoldierClasses(image) && HasType(image,&table,".?AVAnimationModel@@")
            && *reinterpret_cast<void**>(image.base+kModelDrawSlotRva)==image.base+kModelDrawRva
            && !std::memcmp(image.base+kModelDrawRva,draw,sizeof(draw))
            && !std::memcmp(image.base+kNodeLookupRva,node,sizeof(node))
            && !std::memcmp(image.base+0x56A6E6,bodyCtor,sizeof(bodyCtor));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* FindNamedBone(void* soldier,NodeLookup lookup,const wchar_t* name) noexcept {
    __try {
        if(!lookup || !soldier || !name || !Readable(soldier,kSkeletonOffset+0x28)) return nullptr;
        // aim_center and aim_target are ten characters, and the game's string
        // keeps seven inline before it moves to the heap -- so asking for them
        // the short way asks for "aim_cen", which is nothing. Past seven the
        // same struct holds a pointer where the letters were, and a capacity
        // that says so.
        int length=0;
        while(name[length]) ++length;
        GameShortString wanted{};
        if(length<=7) {
            for(int j=0;j<length;++j) wanted.text[j]=name[j];
            wanted.text[length]=0;
            wanted.capacity=7;
        } else {
            *reinterpret_cast<const wchar_t**>(&wanted.text)=name;
            wanted.capacity=15;
        }
        wanted.size=static_cast<std::uint64_t>(length);
        auto registry=static_cast<unsigned char*>(soldier)+kSkeletonOffset;
        auto node=static_cast<unsigned char*>(lookup(registry,&wanted));
        if(!node || !Readable(node,0xB0+64)) return nullptr;
        return node;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool ReadNamedBone(void* soldier,NodeLookup lookup,const wchar_t* name,float world[3]) noexcept {
    __try {
        auto node=static_cast<unsigned char*>(FindNamedBone(soldier,lookup,name));
        if(!node) return false;
        auto row=reinterpret_cast<const float*>(node+0xB0+0x30);
        for(int j=0;j<3;++j) {
            if(!std::isfinite(row[j])) return false;
            world[j]=row[j];
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadNamedBoneFrame(void* soldier,NodeLookup lookup,const wchar_t* name,float rows[3][3],float world[3]) noexcept {
    __try {
        auto node=static_cast<unsigned char*>(FindNamedBone(soldier,lookup,name));
        if(!node) return false;
        auto m=reinterpret_cast<const float*>(node+0xB0);
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
            if(!std::isfinite(m[i*4+j])) return false;
            rows[i][j]=m[i*4+j];
        }
        for(int j=0;j<3;++j) { if(!std::isfinite(m[12+j])) return false; world[j]=m[12+j]; }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool WriteBonePosition(void* node,const float world[3]) noexcept {
    __try {
        if(!node || !Readable(node,0xB0+64,true)) return false;
        for(int j=0;j<3;++j) if(!std::isfinite(world[j])) return false;
        auto row=reinterpret_cast<float*>(static_cast<unsigned char*>(node)+0xB0+0x30);
        for(int j=0;j<3;++j) row[j]=world[j];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadPlayerPose(const ImageProfile& image,void* soldier,NodeLookup lookup,
                    const EyeSettings& settings,PlayerPose& out,bool allowMounted) noexcept {
    out={};
    __try {
        if(!lookup || !Readable(soldier,0x1560)
           || !IsSupportedSoldier(image,soldier)) return false;
        // The camera/source identity gate is in plugin.cpp. Require player control
        // and reject an active vehicle weak reference, as does F7500/F7210.
        if(!At<void*>(soldier,0x340)) return false;
        auto vehicle=At<void*>(soldier,0x1550);
        if(!allowMounted && vehicle && (!Readable(vehicle,12) || At<std::uint32_t>(vehicle,8)!=0)) return false;
        auto body=static_cast<unsigned char*>(soldier)+kBodyModelOffset;
        if(!HasType(image,body,".?AVAnimationModel@@")) return false;
        auto registry=static_cast<unsigned char*>(soldier)+kSkeletonOffset;
        auto resource=At<void*>(registry,0);
        auto nodes=At<unsigned char*>(registry,0x10);
        const auto count=At<std::uint64_t>(registry,0x20);
        if(!resource || !Readable(resource,0x100) || !nodes || count==0 || count>1024
           || !Readable(nodes,static_cast<std::size_t>(count)*0x110)) return false;
        const auto& root=At<Matrix>(soldier,0x5E0);
        if(!ValidCamera(root) || !std::isfinite(settings.headUp) || std::fabs(settings.headUp)>0.5f
           || !std::isfinite(settings.fallbackHeight) || settings.fallbackHeight<1 || settings.fallbackHeight>2.2f) return false;
        out.root=root; out.rootValid=true;
        out.body=body; out.skeletonResource=resource; out.nodeArray=nodes;
        out.objectId=At<std::uint32_t>(soldier,0x314);
        for(int j=0;j<3;++j) out.eye[j]=root.m[3][j]+root.m[1][j]*settings.fallbackHeight;
        {
            auto arms=static_cast<unsigned char*>(lookup(registry,&armsRightName));
            if(!arms) arms=static_cast<unsigned char*>(lookup(registry,&armsName));
            const auto at=reinterpret_cast<std::uintptr_t>(arms);
            const auto from=reinterpret_cast<std::uintptr_t>(nodes);
            if(arms && at>=from && at-from<count*0x110 && (at-from)%0x110==0) {
                const auto& world=At<Matrix>(arms,0xB0);
                if(ValidCamera(world)) {
                    out.armsNode=arms;
                    out.armsWorld=world;
                    out.armsFound=true;
                    out.armsIndex=static_cast<int>((at-from)/0x110);
                }
            }
        }
        const auto head=static_cast<unsigned char*>(lookup(registry,&headName));
        const auto address=reinterpret_cast<std::uintptr_t>(head),base=reinterpret_cast<std::uintptr_t>(nodes);
        if(head && address>=base && address-base<count*0x110 && (address-base)%0x110==0) {
            const auto& world=At<Matrix>(head,0xB0);
            // Bones may include scale; only their position is needed, not rotation.
            float distance2=0;
            for(int j=0;j<3;++j) {
                if(!std::isfinite(world.m[3][j])) return false;
                const float d=world.m[3][j]-root.m[3][j]; distance2+=d*d;
            }
            if(distance2>0.04f && distance2<9 && std::fabs(world.m[3][3]-1)<0.01f) {
                for(int j=0;j<3;++j) out.eye[j]=world.m[3][j]+root.m[1][j]*settings.headUp;
                out.headFound=true; out.headIndex=static_cast<int>((address-base)/0x110);
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out={}; return false; }
}

bool CheckAimProfile(const ImageProfile& image) noexcept {
    __try {
        if(!image.base) return false;
        // 573BA8: aim[0x1230..] += look[0xD60..], preserving the w lane.
        const unsigned char accumulate[]={0xF3,0x0F,0x10,0x86,0x3C,0x12,0x00,0x00,
                                          0x0F,0x10,0x8E,0x30,0x12,0x00,0x00,
                                          0x0F,0x58,0x8E,0x60,0x0D,0x00,0x00};
        // 5705AC: yaw = atan2(worldForward.x, worldForward.z) stored at +0x1234.
        const unsigned char yawFromForward[]={0x48,0x8D,0x8B,0x80,0x00,0x00,0x00,
                                              0xE8,0x38,0xDC,0xAD,0xFF,
                                              0xF3,0x0F,0x11,0x83,0x34,0x12,0x00,0x00};
        // 573C37: pitch clamped to +/- pi/2.
        const unsigned char pitchClamp[]={0xC7,0x03,0xDB,0x0F,0xC9,0xBF};
        const unsigned char inputRead[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,
                                         0x48,0x89,0x74,0x24,0x18,0x57};
        const unsigned char callSite[]={0xE8,0x50,0xD6,0xFF,0xFF};
        return !std::memcmp(image.base+0x573BA8,accumulate,sizeof(accumulate))
            && !std::memcmp(image.base+0x5705AC,yawFromForward,sizeof(yawFromForward))
            && !std::memcmp(image.base+0x573C48,pitchClamp,sizeof(pitchClamp))
            && !std::memcmp(image.base+kInputReadRva,inputRead,sizeof(inputRead))
            && !std::memcmp(image.base+kInputCallSiteRva,callSite,sizeof(callSite));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadSoldierAim(void* soldier,SoldierAim& out) noexcept {
    out=SoldierAim{};
    __try {
        if(!Readable(soldier,0x1260)) return false;
        const auto angles=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+kAimAngleOffset);
        const auto limits=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+kAimLimitOffset);
        const auto move=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+kMoveInputOffset);
        const auto look=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+kLookInputOffset);
        const auto world=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+kSoldierWorldOffset);
        for(int j=0;j<3;++j) if(!std::isfinite(world[12+j])) return false;
        if(!std::isfinite(angles[0]) || !std::isfinite(angles[1])) return false;
        out.pitch=angles[0]; out.yaw=angles[1]; out.third=angles[2];
        out.pitchLimit=limits[0]; out.yawLimit=limits[1];
        out.moveInput[0]=move[0]; out.moveInput[1]=move[2];
        out.lookInput[0]=look[0]; out.lookInput[1]=look[1];
        for(int j=0;j<3;++j) out.position[j]=world[12+j];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out=SoldierAim{}; return false; }
}

bool WriteSoldierAim(void* soldier,float pitch,float yaw) noexcept {
    __try {
        if(!Readable(soldier,0x1260,true)) return false;
        if(!std::isfinite(pitch) || !std::isfinite(yaw)) return false;
        constexpr float kHalfPi=1.5707963f;
        // Same clamps the game applies at 573BE4/573C03/573C37, applied up front so
        // the value we write is one the game would also accept.
        const auto limits=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+kAimLimitOffset);
        if(std::isfinite(limits[0]) && limits[0]>=0) pitch=std::fmax(-limits[0],std::fmin(limits[0],pitch));
        if(std::isfinite(limits[1]) && limits[1]>=0) yaw=std::fmax(-limits[1],std::fmin(limits[1],yaw));
        pitch=std::fmax(-kHalfPi,std::fmin(kHalfPi,pitch));
        auto angles=reinterpret_cast<float*>(static_cast<unsigned char*>(soldier)+kAimAngleOffset);
        angles[0]=pitch; angles[1]=yaw;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool WriteSoldierInput(void* soldier,float moveX,float moveZ,float lookPitch,float lookYaw) noexcept {
    __try {
        if(!Readable(soldier,0xD70,true)) return false;
        if(!std::isfinite(moveX) || !std::isfinite(moveZ)
           || !std::isfinite(lookPitch) || !std::isfinite(lookYaw)) return false;
        const float length=std::sqrt(moveX*moveX+moveZ*moveZ);
        // The game treats the move input as a unit-length stick vector.
        if(length>1) { moveX/=length; moveZ/=length; }
        auto move=reinterpret_cast<float*>(static_cast<unsigned char*>(soldier)+kMoveInputOffset);
        auto look=reinterpret_cast<float*>(static_cast<unsigned char*>(soldier)+kLookInputOffset);
        move[0]=moveX; move[2]=moveZ;
        look[0]=lookPitch; look[1]=lookYaw;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PlaceAtEye(const Matrix& input,const float eye[3],Matrix& output) noexcept {
    if(!ValidCamera(input)) return false;
    for(int j=0;j<3;++j) if(!std::isfinite(eye[j])) return false;
    output=input;
    for(int j=0;j<3;++j) output.m[3][j]=eye[j];
    return true;
}
bool MarkBodyActive(void* model,int pass) noexcept {
    __try {
        if(!Readable(static_cast<unsigned char*>(model)+0x4D0,8,true)) return false;
        // Equivalent to the post-draw block at 6BFB16 for color passes.
        // Keep skeleton/attachments active despite omitting body mesh submission.
        if(pass==1 || pass==2 || pass==5) {
            At<unsigned char>(model,0x4D0)=1;
            At<float>(model,0x4D4)=0;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteWeaponPosition(const WeaponPose& held,const float world[3]) noexcept {
    __try {
        if(!held.valid || !held.transform) return false;
        for(int j=0;j<3;++j) if(!std::isfinite(world[j])) return false;
        if(!Readable(held.transform,0x90,true)) return false;
        auto row=reinterpret_cast<float*>(static_cast<unsigned char*>(held.transform)+0x80);
        for(int j=0;j<3;++j) row[j]=world[j];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool WritePoint(void* object,std::size_t offset,const float world[3]) noexcept {
    __try {
        if(!object || !Readable(object,offset+16,true)) return false;
        for(int j=0;j<3;++j) if(!std::isfinite(world[j])) return false;
        auto point=reinterpret_cast<float*>(static_cast<unsigned char*>(object)+offset);
        for(int j=0;j<3;++j) point[j]=world[j];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadOwnedWeaponPose(void* soldier,void* weapon,WeaponPose& out) noexcept {
    out=WeaponPose{};
    __try {
        if(!Readable(weapon,0x360) || At<void*>(weapon,0x120)!=soldier) return false;
        out.weapon=weapon;
        out.transform=At<unsigned char*>(out.weapon,0x1D0);
        if(!Readable(out.transform,0x90)) return false;
        auto rows=static_cast<const unsigned char*>(out.transform);
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) {
            const float v=*reinterpret_cast<const float*>(rows+0x50+i*16+j*4);
            if(!std::isfinite(v)) return false;
            out.world.m[i][j]=v;
        }
        const auto local=reinterpret_cast<const float*>(static_cast<unsigned char*>(weapon)+0x350);
        float norm=0;
        for(int j=0;j<3;++j) {
            for(int i=0;i<3;++i) out.forward[j]+=local[i]*out.world.m[i][j];
            norm+=out.forward[j]*out.forward[j];
        }
        if(!std::isfinite(norm) || norm<.000001f) return false;
        for(auto& v:out.forward) v/=std::sqrt(norm);
        auto model=static_cast<unsigned char*>(out.weapon)+kWeaponModelOffset;
        if(Readable(model,0x10)) out.model=model;
        if(out.model) {
            auto registry=static_cast<unsigned char*>(out.model)+0xA0;
            if(Readable(registry,0x28)) {
                auto nodes=At<unsigned char*>(registry,0x10);
                const auto count=At<std::uint64_t>(registry,0x20);
                if(nodes && count && count<=64
                   && Readable(nodes,static_cast<std::size_t>(count)*0x110)) {
                    out.nodes=nodes;
                    out.nodeCount=count;
                }
            }
        }
        out.valid=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out=WeaponPose{}; return false; }
}

bool ReadWeaponPose(void* soldier,unsigned int slot,WeaponPose& out) noexcept {
    void* weapon=nullptr; float direction[3]{};
    if(!ReadWeaponDirection(soldier,slot,weapon,direction)) { out={};return false; }
    // Existing slot readers also serve synthetic/native objects without an owner
    // field; ownership is checked explicitly by every mutating hook.
    __try {
        return ReadOwnedWeaponPose(*reinterpret_cast<void**>(static_cast<unsigned char*>(weapon)+0x120),weapon,out);
    } __except(EXCEPTION_EXECUTE_HANDLER) { out={};return false; }
}

bool ReadWeaponDirection(void* soldier,unsigned int slot,void*& weapon,float direction[3]) noexcept {
    weapon=nullptr;
    __try {
        const auto count=WeaponSlotCount(soldier);
        if(!count || slot>=count) return false;
        auto entries=At<unsigned char*>(soldier,0x1970);
        auto wrapper=At<void*>(entries+slot*0x150,0x40);
        if(!Readable(wrapper,8)) return false;
        weapon=At<void*>(wrapper,0);
        if(!Readable(weapon,0x360)) return false;
        const auto transform=At<unsigned char*>(weapon,0x1D0);
        if(!Readable(transform,0x90)) return false;
        const auto local=reinterpret_cast<const float*>(static_cast<unsigned char*>(weapon)+0x350);
        float norm=0;
        for(int j=0;j<3;++j) {
            direction[j]=0;
            for(int i=0;i<3;++i) direction[j]+=local[i]*At<float>(transform,0x50+i*16+j*4);
            if(!std::isfinite(direction[j])) return false;
            norm+=direction[j]*direction[j];
        }
        if(!std::isfinite(norm) || norm<0.000001f) return false;
        norm=std::sqrt(norm);
        for(int j=0;j<3;++j) direction[j]/=norm;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { weapon=nullptr; return false; }
}
}
