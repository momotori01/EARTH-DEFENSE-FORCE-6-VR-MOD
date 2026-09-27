#include "vehicle_camera.h"
#include "cockpit_combat_shells.h"
#include "cockpit_heli_shells.h"
#include <cmath>
#include <cstring>
#include <cwchar>
namespace edf6vr {
namespace {
template<class T> const T& At(const void* p,std::size_t o) noexcept {
    return *reinterpret_cast<const T*>(static_cast<const unsigned char*>(p)+o);
}
bool Basis(const Quat& q,Matrix& m) noexcept {
    HeadBasis h{}; if(!HeadBasisFromXr(q,h)) return false;
    m={}; m.m[3][3]=1;
    const Vec3 r{h.up.y*h.forward.z-h.up.z*h.forward.y,
        h.up.z*h.forward.x-h.up.x*h.forward.z,h.up.x*h.forward.y-h.up.y*h.forward.x};
    m.m[0][0]=r.x;m.m[0][1]=r.y;m.m[0][2]=r.z;
    m.m[1][0]=h.up.x;m.m[1][1]=h.up.y;m.m[1][2]=h.up.z;
    m.m[2][0]=h.forward.x;m.m[2][1]=h.forward.y;m.m[2][2]=h.forward.z;
    return ValidCamera(m);
}
}
bool CheckVehicleProfile(const ImageProfile& image) noexcept {
    __try {
        if(!image.base) return false;
        const unsigned char weak[]={0x48,0x8B,0x81,0x50,0x15,0,0}; // 56D709
        const unsigned char seat[]={0x4C,0x89,0xA6,0x40,0x15,0,0}; // 576E60
        const unsigned char object[]={0x48,0x8B,0xBB,0x48,0x15,0,0}; // 56DB5B
        const unsigned char array[]={0x48,0x8B,0x8F,0x08,0x06,0,0,0x48,0x03,0xCE};
        const unsigned char stride[]={0x48,0x81,0xC6,0x40,0x03,0,0,0x48,0x3B,0x9F,0x18,0x06,0,0};
        const unsigned char registry[]={0x49,0x8D,0x8F,0x40,0x0E,0,0};
        const unsigned char modelCtor[]={0x49,0x8D,0x8E,0x40,0x0E,0,0,0xE8,0xB1,0xF1,0x08,0};
        return !std::memcmp(image.base+0x629583,modelCtor,sizeof modelCtor)
            && !std::memcmp(image.base+0x603D70,array,sizeof array)
            && !std::memcmp(image.base+0x603D85,stride,sizeof stride)
            && !std::memcmp(image.base+0x62B530,registry,sizeof registry)
            && !std::memcmp(image.base+0x56D709,weak,sizeof weak)
            && !std::memcmp(image.base+0x576E60,seat,sizeof seat)
            && !std::memcmp(image.base+0x56DB5B,object,sizeof object);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool HasMountReference(const void* soldier) noexcept {
    __try {
        if(!Readable(soldier,0x1558)) return true;
        auto control=At<void*>(soldier,0x1550);
        return control && (!Readable(control,12) || At<unsigned>(control,8)!=0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}
bool ReadVehicleSeat(const ImageProfile& image,void* soldier,VehicleSeat& out) noexcept {
    out={};
    __try {
        if(!IsSupportedSoldier(image,soldier) || !Readable(soldier,0x1558)
            || !At<void*>(soldier,0x340)) return false;
        auto control=At<void*>(soldier,0x1550);
        if(!Readable(control,12) || !At<unsigned>(control,8)) return false;
        auto vehicle=At<void*>(soldier,0x1548), seat=At<void*>(soldier,0x1540);
        auto type=TypeName(image,vehicle);
        if(!type || std::strncmp(type,".?AVVehicle",11)!=0 || !Readable(vehicle,0x318)
            || !HasType(image,seat,".?AVRideInfo@VehicleBase@@") || !Readable(seat,0x270)) return false;
        // 58F33F/58F346 compare the current camera owner with RideInfo+8.
        out.vehicle=vehicle;out.seat=seat;out.cameraOwner=At<void*>(seat,8);
        out.vehicleId=At<std::uint32_t>(vehicle,0x314);
        out.riderHead=HasType(image,vehicle,".?AVVehicle503_Bike@@")
            || HasType(image,vehicle,".?AVVehicle511_Bike@@");
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out={};return false; }
}
namespace {
struct CabinProfile { const wchar_t* modelBone; const char* label; Vec3 eye; const wchar_t* positionBone=nullptr; int seatIndex=0; };
// Local model coordinates. Geometry evidence and remaining visual tolerances:
// research/vehicle/cabin_mesh_bounds.json. Cabin presets are initial estimates;
// no mesh, bone, vehicle transform, material, or collision is written.
constexpr CabinProfile cabins[]={
    // 403/404 SGO: seat1=A (left), seat2=B (right). Keep the view above
    // each actual turret base rather than the native distant/ground camera.
    // Roll bones carry turret yaw without borrowing the pitching barrel pose.
    {L"Vehicle403_tank","403-left-turret",{0,.90f,-.40f},L"MachineGun_A_roll",1},
    {L"Vehicle403_tank","403-right-turret",{0,.90f,-.40f},L"MachineGun_B_roll",2},
    {L"Vehicle404_bigtank","404-left-turret",{0,1.80f,-.80f},L"subCannon_A_roll",1},
    {L"Vehicle404_bigtank","404-right-turret",{0,1.80f,-.80f},L"subCannon_B_roll",2},
    // Vehicle410 settings explicitly map seat1/2 to left/right gunners.
    // Shipped MDB provides dedicated gun camera anchors above each gun.
    {L"Vehicle410_heli","brute-left-guncam",{0,0,0},L"gunCam_l",1},
    {L"Vehicle410_heli","brute-right-guncam",{0,0,0},L"gunCam_r",2},
    // Initial roof/deck estimates; keep all vehicle geometry visible.
    {L"v510_maser","emc-front-deck",{-1.15f,1.45f,7.80f},L"body"},
    {L"v603_flak","kevlar-hatch",{0.55f,3.15f,1.25f}},
    // SGO vehicle_riding_position: 1=L, 2=R, 3=missile (V407 and V614).
    // V407 lower sensor is skinned to the barrel; V614 lower front is on the
    // mount. Follow the actual mesh bone so moving a barrel cannot drag a
    // camera attached to a different part. Geometry remains fully visible.
    {L"Vehicle407_bigbegaruta","proteus-left-sensor",{0.02672f,-0.84964f,1.11752f},L"gun_mount_barrel_l",1},
    {L"Vehicle407_bigbegaruta","proteus-right-sensor",{-0.02670f,-0.84964f,1.11752f},L"gun_mount_barrel_r",2},
    {L"Vehicle407_bigbegaruta","proteus-missile-top",{0,2.10f,0},L"rocket_launcher",3},
    {L"v614_proteus_mk2","proteus614-left-sensor",{0.17906f,-1.22999f,4.28718f},L"gun_mount_l",1},
    {L"v614_proteus_mk2","proteus614-right-sensor",{-0.17906f,-1.23000f,4.28718f},L"gun_mount_r",2},
    {L"v614_proteus_mk2","proteus614-missile-top",{0,1.88f,0},L"missile_launcher",3},
    // Animated anchors use bone-local offsets, converted from each actual MDB
    // bind matrix (axes differ between V504/V612 and V515/V605).
    {L"v504_begaruta","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    // Stand inside the upper railed deck's forward extension: Z=7 has floor
    // Y=45.4375; add 1.65 m eye height. Z=8 selects a lower chest surface.
    // See research/vehicle/barga_deck_surfaces.txt. Keep the full vehicle visible.
    {L"v515_retrobalam","barga-front-deck",{0.00000f,10.58618f,7.00000f},L"mune"},
    {L"v605_barga_cannon","barga605-front-deck",{10.58618f,-7.00000f,0.00000f},L"mune"},
    {L"Vehicle407_bigbegaruta","proteus-driver",{0.00000f,1.11978f,8.24150f},L"neck"},
    {L"v504_begaruta_blue","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v608_oldrobot","oldrobot-eye",{0.30737f,0.00000f,0.00000f},L"eye"},
    {L"v612_nix","nix612-head",{0.22000f,-0.92995f,0.00000f},L"head"},
    {L"v614_proteus_mk2","proteus614-driver",{2.73437f,3.64395f,0.00000f},L"neck"},
    {L"groundrobo","crawler-visor",{0.00000f,1.13292f,1.45000f},L"body"},
    {L"v504_begaruta_coating","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v504_begaruta_gray1","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v504_begaruta_gray2","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v504_begaruta_haispeed","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v504_begaruta_pink","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v504_begaruta_red","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v504_begaruta_yellow","nix-head",{0.00000f,0.22000f,0.92995f},L"head"},
    {L"v515_retrobalam_green","barga-front-deck",{0.00000f,10.58618f,7.00000f},L"mune"},
    {L"Vehicle401_STRIKER","striker-driver",{-0.60f,2.35f,1.70f}},
    {L"Vehicle402_Rocket","rocket-driver",{-0.65f,2.55f,1.80f}},
    {L"v507_rescuetank","rescue-driver",{-0.70f,3.10f,3.80f}},
    {L"Vehicle409_heli","nereid-canopy",{0,2.40f,3.40f}},
    {L"Vehicle410_heli","brute-canopy",{0,3.10f,3.50f}},
    {L"v506_heli","euros-canopy",{0,2.05f,2.55f}},
    {L"v602_heli","602-canopy",{0,1.85f,2.65f}},
    {L"v505_tank","505-hatch",{0.50f,2.85f,-0.35f}},
    {L"v601_tank","601-hatch",{0.50f,2.95f,-0.35f}},
    {L"Vehicle403_tank","403-hatch",{0.65f,3.25f,-0.40f}},
    {L"Vehicle404_bigtank","404-hatch",{1.00f,9.55f,-1.00f}},
    // The mission trucks' drivers (right-hand drive), measured on each cab's
    // model on 2026-09-25 (research/cockpit/trucks: seat cushion, wheel hub,
    // roof), after the first ride: the kei truck's (and its BGP paint's) 72
    // cm over the cushion (.90), 54 behind and 40 above the hub, 28 under the
    // roof; the trailer cab's 74 over its cushion (1.68), 69 behind and 48
    // above its hub. The 0.98 estimates were 12-23 cm further forward and the
    // trailer's 18 higher.
    {L"v512_keiTruck","kei-driver",{-0.44f,1.62f,1.06f}},
    {L"v512_keiTruck_bgp","kei-driver",{-0.44f,1.62f,1.06f}},
    {L"v513_trailerTruck01Cab","trailer-driver",{-0.67f,2.42f,1.82f}},
    {L"v607_robotruck","robotruck-driver",{-0.65f,2.75f,2.65f}},
    {L"v610_ptruck_gun","pickup-driver",{-0.45f,1.75f,1.15f}},
    {L"v611_ptruck_rocket","pickup-driver",{-0.45f,1.75f,1.15f}},
};
struct LookupString { wchar_t text[8]{}; std::uint64_t size=0,capacity=7; };
static_assert(sizeof(LookupString)==32);
unsigned char* CabinNode(NodeLookup lookup,void* registry,unsigned char* nodes,
    std::uint64_t count,const wchar_t* key) {
    LookupString name{};name.size=wcslen(key);
    if(name.size<=7) wmemcpy(name.text,key,name.size);
    else { *reinterpret_cast<const wchar_t**>(name.text)=key;name.capacity=name.size; }
    auto node=static_cast<unsigned char*>(lookup(registry,&name));
    const auto n=reinterpret_cast<std::uintptr_t>(node),begin=reinterpret_cast<std::uintptr_t>(nodes);
    return node && n>=begin && n-begin<count*0x110 && (n-begin)%0x110==0?node:nullptr;
}
}
bool PlaceVehicleAnchor(const ImageProfile& image,const VehicleSeat& seat,NodeLookup lookup,
    Matrix& camera,const char*& label,int& seatIndex) noexcept {
    label="native-seat-fallback";seatIndex=-1;
    __try {
        auto v=static_cast<unsigned char*>(seat.vehicle);
        if(!Readable(v,0xF08) || !Readable(seat.seat,0x340) || !lookup || !ValidCamera(camera)) return false;
        auto seats=At<unsigned char*>(v,0x608);const auto count=At<std::uint64_t>(v,0x618);
        if(!count || count>16 || !Readable(seats,count*0x340)) return false;
        const auto address=reinterpret_cast<std::uintptr_t>(seat.seat),first=reinterpret_cast<std::uintptr_t>(seats);
        if(address<first || address-first>=count*0x340 || (address-first)%0x340) return false;
        seatIndex=static_cast<int>((address-first)/0x340);
        const bool mappedGunnerSeats=(count==4 && HasType(image,v,".?AVVehicleBigBegaruta@@"))
            || (count==3 && (HasType(image,v,".?AVVehicleHelicopter410@@")
                || HasType(image,v,".?AVVehicle403_Tank@@")
                || HasType(image,v,".?AVVehicle404_Tank@@")));
        if(seat.cameraOwner!=v || (seatIndex!=0 && !mappedGunnerSeats)) {
            label="native-passenger-seat";return false;
        }
        if(seatIndex!=0) label="native-passenger-seat";
        // VehicleBase ctor 629583/62958A constructs AnimationModel at E40.
        // Its registry is +A0, at EE0; 61AE4D..58 copies vehicle+60 there.
        if(!HasType(image,v+0xE40,".?AVAnimationModel@@")) return false;
        auto resource=At<void*>(v,0xEE0);
        auto nodes=At<unsigned char*>(v,0xEF0);
        const auto bones=At<std::uint64_t>(v,0xF00);
        if(!Readable(resource,8) || !bones || bones>2048 || !Readable(nodes,bones*0x110)) return false;
        const auto root=At<Matrix>(v,0x60);
        if(!ValidCamera(root)) return false;
        for(const auto& profile:cabins) {
            if(profile.seatIndex!=seatIndex) continue;
            if(!CabinNode(lookup,v+0xEE0,nodes,bones,profile.modelBone)) continue;
            Matrix anchor=root;
            if(profile.positionBone) {
                auto node=CabinNode(lookup,v+0xEE0,nodes,bones,profile.positionBone);
                // This is the read-only world pose also used for soldier heads.
                // A missing/stale bone must not reinterpret a bone-local offset
                // as a root-space point. Keep the native camera on failure.
                if(!node) return false;
                anchor=At<Matrix>(node,0xB0);
                if(!ValidCamera(anchor)) return false;
            }
            Matrix placed=camera;
            for(int j=0;j<3;++j) placed.m[3][j]=anchor.m[3][j]+profile.eye.x*anchor.m[0][j]
                +profile.eye.y*anchor.m[1][j]+profile.eye.z*anchor.m[2][j];
            if(!ValidCamera(placed)) return false;
            camera=placed;label=profile.label;return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool MissionTruckRider(const ImageProfile& image,const VehicleSeat& seat,NodeLookup lookup,bool* driver) noexcept {
    if(driver) *driver=false;
    __try {
        auto v=static_cast<unsigned char*>(seat.vehicle);
        if(!lookup || !Readable(v,0xF08) || !HasType(image,v+0xE40,".?AVAnimationModel@@")) return false;
        auto nodes=At<unsigned char*>(v,0xEF0);const auto bones=At<std::uint64_t>(v,0xF00);
        if(!bones || bones>2048 || !Readable(nodes,bones*0x110)) return false;
        bool truck=false;
        for(const auto name:{L"v512_keiTruck",L"v512_keiTruck_bgp",L"v513_trailerTruck01Cab"})
            truck=truck||CabinNode(lookup,v+0xEE0,nodes,bones,name)!=nullptr;
        if(!truck) return false;
        // The driver: the first seat (SGO 操縦席), whose camera is the vehicle's.
        auto seats=At<unsigned char*>(v,0x608);const auto count=At<std::uint64_t>(v,0x618);
        if(driver && count && count<=16 && Readable(seats,count*0x340))
            *driver=seat.seat==seats && seat.cameraOwner==v;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool VehicleLocalPoint(const VehicleSeat& seat,const float world[3],float local[3]) noexcept {
    __try {
        if(!Readable(seat.vehicle,0xA0)) return false;
        const auto root=At<Matrix>(seat.vehicle,0x60);
        if(!ValidCamera(root)) return false;
        for(int i=0;i<3;++i) {
            float n=0,d=0;
            for(int k=0;k<3;++k){n+=root.m[i][k]*root.m[i][k];d+=(world[k]-root.m[3][k])*root.m[i][k];}
            if(n<1e-8f) return false;
            local[i]=d/n;   // a scaled root's axes: back to model units
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
namespace {
bool UnitHull(const VehicleSeat& seat,Matrix& hull) noexcept {
    if(!Readable(seat.vehicle,0xA0)) return false;
    hull=At<Matrix>(seat.vehicle,0x60);
    if(!ValidCamera(hull)) return false;
    for(int i=0;i<3;++i){float n=0;for(int k=0;k<3;++k)n+=hull.m[i][k]*hull.m[i][k];n=std::sqrt(n);
        if(!(n>1e-4f))return false;for(int k=0;k<3;++k)hull.m[i][k]/=n;}
    return true;
}
}
bool VehicleLocalDirection(const VehicleSeat& seat,const float world[3],float local[3]) noexcept {
    __try {
        Matrix hull{};if(!UnitHull(seat,hull))return false;
        for(int i=0;i<3;++i){local[i]=0;for(int k=0;k<3;++k)local[i]+=world[k]*hull.m[i][k];}
        return std::isfinite(local[0])&&std::isfinite(local[1])&&std::isfinite(local[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool VehicleBoneRows(const VehicleSeat& seat,NodeLookup lookup,const wchar_t* bone,Matrix& rows) noexcept {
    __try {
        auto v=static_cast<unsigned char*>(seat.vehicle);
        if(!lookup || !Readable(v,0xF08)) return false;
        auto nodes=At<unsigned char*>(v,0xEF0);const auto count=At<std::uint64_t>(v,0xF00);
        if(!count || count>2048 || !Readable(nodes,count*0x110)) return false;
        auto node=CabinNode(lookup,v+0xEE0,nodes,count,bone);
        if(!node) return false;
        rows=At<Matrix>(node,0xB0);
        return ValidCamera(rows);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool FaceVehicleForward(const VehicleSeat& seat,Matrix& camera) noexcept {
    __try {
        if(!Readable(seat.vehicle,0xA0) || !ValidCamera(camera)) return false;
        const auto chassis=At<Matrix>(seat.vehicle,0x60);
        if(!ValidCamera(chassis)) return false;
        Matrix placed=camera;
        for(int i=0;i<3;++i) for(int j=0;j<4;++j) placed.m[i][j]=chassis.m[i][j];
        if(!ValidCamera(placed)) return false;
        camera=placed;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool PlaceVehicleCockpit(const ImageProfile& image,const VehicleSeat& seat,NodeLookup lookup,Matrix& cabin,CockpitRig& rig) noexcept {
    __try {
        auto* v=static_cast<unsigned char*>(seat.vehicle);
        if(!lookup||!Readable(v,0xF08)||seat.cameraOwner!=seat.vehicle||
            !HasType(image,v+0xE40,".?AVAnimationModel@@"))return false;
        // Which station is this? Only the driver's seat had a cabin until the
        // Proteus, whose two gun pods each carry a real gunner compartment.
        auto* seats=At<unsigned char*>(v,0x608);const auto seatCount=At<std::uint64_t>(v,0x618);
        if(!seatCount||seatCount>16||!Readable(seats,seatCount*0x340))return false;
        const auto address=reinterpret_cast<std::uintptr_t>(seat.seat),firstSeat=reinterpret_cast<std::uintptr_t>(seats);
        if(address<firstSeat||address-firstSeat>=seatCount*0x340||(address-firstSeat)%0x340)return false;
        const auto seatIndex=static_cast<unsigned>((address-firstSeat)/0x340);
        auto* nodes=At<unsigned char*>(v,0xEF0);const auto count=At<std::uint64_t>(v,0xF00);auto* resource=At<void*>(v,0xEE0);
        // The Grape has 13 bones; every cabin below finds its own bones by name.
        if(!resource||!Readable(resource,8)||count<8||count>256||!Readable(nodes,count*0x110))return false;
        // Proteus MK2: SGO seat 1 is the left gunner, seat 2 the right. Both sit
        // inside their own arm pod, so the cabin follows that pod's bone, not
        // the hull. The pod's own shell is the only part hidden for the rider.
        const bool proteus=seatCount==4&&HasType(image,v,".?AVVehicleBigBegaruta@@")
            &&CabinNode(lookup,v+0xEE0,nodes,count,L"v614_proteus_mk2")!=nullptr;
        if(proteus&&(seatIndex==1||seatIndex==2)) {
            auto* pod=CabinNode(lookup,v+0xEE0,nodes,count,seatIndex==1?L"gun_mount_l":L"gun_mount_r");
            if(!pod)return false;
            const Matrix posed=At<Matrix>(pod,0xB0);if(!ValidCamera(posed))return false;
            // Measured compartment centre in that bone's frame: the pod's inner
            // skins are +-.7285 about x=+-.179, the seated eye datum is 1.10
            // below the bone and the station sits 2.34 out along the arm. That
            // puts the eye 1.29m above the compartment floor and leaves the legs
            // the length of the pod's own climbing floor ahead of them.
            const float origin[3]={seatIndex==1?.179f:-.179f,-1.10f,2.34f};
            Matrix next=posed;
            for(int j=0;j<3;++j)
                next.m[3][j]+=origin[0]*posed.m[0][j]+origin[1]*posed.m[1][j]+origin[2]*posed.m[2][j];
            if(!ValidCamera(next))return false;
            const auto podBone=static_cast<unsigned>((pod-nodes)/0x110);
            if(rig.kind!=CockpitKind::ProteusGunner||rig.model!=v+0xE40||rig.nodes!=nodes||
               rig.resource!=resource||rig.nodeCount!=count||rig.bodyBone!=podBone) {
                CockpitRig selected{};selected.kind=CockpitKind::ProteusGunner;selected.model=v+0xE40;
                selected.resource=resource;selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);
                selected.bodyBone=podBone;
                // Keep the whole machine, including the gun overhead and the
                // far pod: only this pod's own shell would block the station.
                for(unsigned i=0;i<count;++i)selected.limb[i]=1;
                selected.limb[podBone]=0;
                rig=selected;
            }
            cabin=next;return true;
        }
        // Driver and missile operator share one tandem cabin inside the upper
        // body. The neck bone owns that body and its own axes are x=up,
        // y=forward, z=right (research/cockpit/survey_proteus_body.py), so the
        // cabin's basis is a permutation of the bone's rather than a copy. The
        // rear build already carries its own eye at the origin, so both
        // stations place the cabin on the same point of the machine.
        if(proteus&&(seatIndex==0||seatIndex==3)) {
            auto* neck=CabinNode(lookup,v+0xEE0,nodes,count,L"neck");
            if(!neck)return false;
            const Matrix posed=At<Matrix>(neck,0xB0);if(!ValidCamera(posed))return false;
            const float origin[3]={1.10f,4.60f,0};   // up, forward, lateral
            Matrix next=posed;
            for(int j=0;j<3;++j) {
                next.m[3][j]+=origin[0]*posed.m[0][j]+origin[1]*posed.m[1][j]+origin[2]*posed.m[2][j];
                next.m[0][j]=posed.m[2][j];next.m[1][j]=posed.m[0][j];next.m[2][j]=posed.m[1][j];
            }
            if(!ValidCamera(next))return false;
            const auto neckBone=static_cast<unsigned>((neck-nodes)/0x110);
            const auto tandem=seatIndex?CockpitKind::ProteusMissile:CockpitKind::ProteusDriver;
            if(rig.kind!=tandem||rig.model!=v+0xE40||rig.nodes!=nodes||
               rig.resource!=resource||rig.nodeCount!=count||rig.bodyBone!=neckBone) {
                CockpitRig selected{};selected.kind=tandem;selected.model=v+0xE40;
                selected.resource=resource;selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);
                selected.bodyBone=neckBone;
                // Keep the whole machine except the body shell wrapped around
                // this cabin: arms, waist, legs and missiles stay in view, which
                // is what the crew are meant to watch.
                for(unsigned i=0;i<count;++i)selected.limb[i]=1;
                selected.limb[neckBone]=0;
                rig=selected;
            }
            cabin=next;return true;
        }
        // Tanks. The driver's cabin is hull-fixed on the body bone, the pilot's
        // shoulders up in the turret ring (research/cockpit/survey_tanks.py).
        // From inside, the turret is all screen, so its shell is hidden; each
        // barrel is kept from a cut forward and a cap fitted to its section
        // closes the cut (research/cockpit/tank_capfit.py), small enough that
        // the barrel and where it points stay in view. The Blacker and the
        // Varias lose the mantlet block and keep only the recoiling barrel,
        // capped on its own rear end. Where the hull's own turret well is
        // open a deck is laid over it. The Titan's and the Railgun's two
        // gunners sit in the Proteus pod under their own gun, with the hull
        // and turret gone and every barrel shown.
        {
            // cut==whole: the bone is kept entire and only carries the cap.
            constexpr float whole=-1e9f;
            struct TankBarrel {const wchar_t* bone;float cut;unsigned cap;};
            struct TankProfile {const wchar_t* model;float eye[3];TankBarrel barrels[3];const wchar_t* hide[8];unsigned deck;unsigned char pos60,pos68;};
            constexpr unsigned none=~0u;
            // Eyes in body-bone coordinates (+X model left): left of the turret
            // axis, 30 cm over the sill, the sill just over the hull deck. Left
            // far enough that the gun is seen from beside it, not end-on: .30 m
            // for the Blacker's slim barrel, 1.00 m for the Titan's 1.2 m gun.
            static constexpr TankProfile tanks[]={
                {L"v505_tank",{.30f,1.70f,0},{{L"cannon_slide",whole,kCapBlacker},{},{}},
                    {L"cannon_main",L"cannon",L"antena0_l",L"antena1_l",L"antena2_l",L"antena0_r",L"antena1_r",L"antena2_r"},none,0,0},
                {L"v505_tank_edf6benefits",{.30f,1.70f,0},{{L"cannon_slide",whole,kCapBlacker},{},{}},
                    {L"cannon_main",L"cannon",L"antena0_l",L"antena1_l",L"antena2_l",L"antena0_r",L"antena1_r",L"antena2_r"},none,0,0},
                {L"v601_tank",{.30f,1.80f,-.173f},{{L"cannon_slide",whole,kCapVarias},{},{}},
                    {L"cannon_main",L"cannon"},none,24,32},
                {L"v510_maser",{.30f,4.28f-2.4194f,0},{{L"cannon",2.50f,kCapEmc},{},{}},
                    {L"cannon_main"},none,0,0},
                {L"v510_maser_red",{.30f,4.28f-2.4194f,0},{{L"cannon",2.50f,kCapEmc},{},{}},
                    {L"cannon_main"},none,0,0},
                // KG6 Kebler (V603): twin guns on the turret's flanks, so the seat is
                // on the turret axis between them. Both gun housings go with the
                // turret; each recoiling barrel is cut where it clears the cabin
                // (it stays outside it at any traverse) and capped there.
                {L"v603_flak",{0,1.78f,.643f},{{L"cannon_slide_l",1.60f,kCapKebler},{L"cannon_slide_r",1.60f,kCapKebler},{}},
                    {L"cannon_main",L"cannon_l",L"cannon_r",L"doppler_radar",L"tracking_radar"},none,24,32},
                {L"Vehicle403_tank",{.30f,1.78f,0},{{L"cannon",2.30f,kCapRailgun},{},{}},
                    {L"cannon_main",L"MachineGun_A_roll",L"MachineGun_A_aim",L"MachineGun_B_roll",L"MachineGun_B_aim",L"cannon_sub_cylinder"},kCapRailgunDeck,0,0},
                {L"Vehicle404_bigtank",{1.00f,4.36f,0},{{L"cannon_aim",6.75f,kCapTitanMain},{L"subCannon_A_aim",-.16f,kCapTitanSub},{L"subCannon_B_aim",-.16f,kCapTitanSub}},
                    {L"cannon_main",L"subCannon_A_roll",L"subCannon_B_roll"},kCapTitanDeck,0,0},
            };
            // Gunners: the pod rides this seat's own gun mount (its yaw), the
            // gun's pivot 1.20 m over the cabin origin and 5 cm ahead, over
            // the head behind the lengthened roof cover. Seat 1 is the A
            // (vehicle-left) gun, seat 2 the B gun, as in the SGO seat table.
            struct GunnerProfile {const wchar_t* model;const wchar_t* roll[2];const wchar_t* own[2];float origin[3];TankBarrel main;float otherCut;unsigned otherCap;};
            static constexpr GunnerProfile gunners[]={
                {L"Vehicle404_bigtank",{L"subCannon_A_roll",L"subCannon_B_roll"},{L"subCannon_A_aim",L"subCannon_B_aim"},
                    {0,-.2485f,1.6415f},{L"cannon_aim",6.75f,kCapTitanMain},-.16f,kCapTitanSub},
                {L"Vehicle403_tank",{L"MachineGun_A_roll",L"MachineGun_B_roll"},{L"MachineGun_A_aim",L"MachineGun_B_aim"},
                    {0,-.8293f,-.015f},{L"cannon",2.30f,kCapRailgun},.90f,kCapRailgunMg},
            };
            const TankProfile* tank=nullptr;
            for(const auto& t:tanks)if(CabinNode(lookup,v+0xEE0,nodes,count,t.model)){tank=&t;break;}
            const GunnerProfile* gunner=nullptr;
            if(tank)for(const auto& g:gunners)if(std::wcscmp(g.model,tank->model)==0)gunner=&g;
            auto boneOf=[&](const wchar_t* name)->int{
                auto* node=name?CabinNode(lookup,v+0xEE0,nodes,count,name):nullptr;
                return node?static_cast<int>((node-nodes)/0x110):-1;};
            auto addBarrel=[&](CockpitRig& target,const TankBarrel& b,bool dish)->bool{
                const int bone=boneOf(b.bone);if(bone<0)return false;
                if(b.cut>whole&&target.cutCount<target.cutBones.size()){target.cutBones[target.cutCount]=static_cast<unsigned char>(bone);target.cutZ[target.cutCount]=b.cut;++target.cutCount;}
                if(dish&&b.cap!=none&&target.capCount<target.capBones.size()){target.capBones[target.capCount]=unsigned(bone);target.capMesh[target.capCount]=static_cast<unsigned char>(b.cap);++target.capCount;}
                target.limb[bone]=1;return true;};
            // The Railgun's root carries a 0.9 scale, so its posed bones may be
            // uniformly scaled. Offsets are taken along the scaled rows (they are
            // model units); the cabin itself gets unit rows, the caps keep the
            // bone's scale like the model they cover.
            auto unitRows=[](Matrix m)->Matrix{
                for(int i=0;i<3;++i){float n=0;for(int k=0;k<3;++k)n+=m.m[i][k]*m.m[i][k];n=std::sqrt(n);
                    if(n>1e-6f)for(int k=0;k<3;++k)m.m[i][k]/=n;}
                return m;};
            auto scaledFrame=[&](const Matrix& m)->bool{
                for(int i=0;i<3;++i){float n=0;for(int k=0;k<3;++k)n+=m.m[i][k]*m.m[i][k];if(!(n>.25f&&n<4.f))return false;}
                return ValidCamera(unitRows(m));};
            auto snapshot=[&](CockpitRig& target)->bool{
                for(unsigned i=0;i<target.capCount;++i) {
                    target.capFrames[i]=At<Matrix>(nodes+target.capBones[i]*0x110,0xB0);
                    if(!scaledFrame(target.capFrames[i]))return false;
                }
                return true;};
            if(tank&&seatIndex==0) {
                const int body=boneOf(L"body");if(body<0)return false;
                const Matrix posed=At<Matrix>(nodes+body*0x110,0xB0);if(!scaledFrame(posed))return false;
                // Cabin origin is the eye less the seated-eye offset (cabin -X is model +X).
                const float origin[3]={tank->eye[0],tank->eye[1]-kCockpitSeatedEye[1],tank->eye[2]-kCockpitSeatedEye[2]};
                Matrix next=unitRows(posed);
                for(int j=0;j<3;++j)next.m[3][j]+=origin[0]*posed.m[0][j]+origin[1]*posed.m[1][j]+origin[2]*posed.m[2][j];
                if(!ValidCamera(next))return false;
                if(rig.kind!=CockpitKind::Tank||rig.model!=v+0xE40||rig.nodes!=nodes||rig.resource!=resource||
                   rig.nodeCount!=count||rig.bodyBone!=unsigned(body)) {
                    CockpitRig selected{};selected.kind=CockpitKind::Tank;selected.model=v+0xE40;selected.resource=resource;
                    selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);selected.bodyBone=unsigned(body);
                    selected.cutPosition60=tank->pos60;selected.cutPosition68=tank->pos68;
                    for(unsigned i=0;i<count;++i)selected.limb[i]=1;
                    for(const auto* name:tank->hide){if(!name)continue;const int bone=boneOf(name);if(bone<0)return false;selected.limb[bone]=0;}
                    for(const auto& b:tank->barrels)if(b.bone&&!addBarrel(selected,b,true))return false;
                    if(tank->deck!=none){selected.capBones[selected.capCount]=unsigned(body);selected.capMesh[selected.capCount]=static_cast<unsigned char>(tank->deck);++selected.capCount;}
                    rig=selected;
                }
                auto live=rig;if(!snapshot(live))return false;rig=live;
                cabin=next;return true;
            }
            if(gunner&&seatCount==3&&(seatIndex==1||seatIndex==2)) {
                const unsigned side=seatIndex==1?0u:1u;
                const int roll=boneOf(gunner->roll[side]);if(roll<0)return false;
                const Matrix posed=At<Matrix>(nodes+roll*0x110,0xB0);if(!scaledFrame(posed))return false;
                const float* origin=gunner->origin;
                Matrix next=unitRows(posed);
                for(int j=0;j<3;++j)next.m[3][j]+=origin[0]*posed.m[0][j]+origin[1]*posed.m[1][j]+origin[2]*posed.m[2][j];
                if(!ValidCamera(next))return false;
                if(rig.kind!=CockpitKind::TitanGunner||rig.model!=v+0xE40||rig.nodes!=nodes||rig.resource!=resource||
                   rig.nodeCount!=count||rig.bodyBone!=unsigned(roll)) {
                    CockpitRig selected{};selected.kind=CockpitKind::TitanGunner;selected.model=v+0xE40;selected.resource=resource;
                    selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);selected.bodyBone=unsigned(roll);
                    selected.cutPosition60=tank->pos60;selected.cutPosition68=tank->pos68;
                    // Nothing of the machine but its guns. The main gun and the
                    // other seat's gun are cut and capped. This seat's own gun
                    // stays whole: its closed body lies over the roof cover,
                    // which hides its back; a cut would leave it open-ended
                    // just ahead of the cover.
                    const int own=boneOf(gunner->own[side]);if(own<0)return false;
                    selected.limb[own]=1;
                    const TankBarrel other{gunner->own[1-side],gunner->otherCut,gunner->otherCap};
                    if(!addBarrel(selected,gunner->main,true)||!addBarrel(selected,other,true))return false;
                    rig=selected;
                }
                auto live=rig;if(!snapshot(live))return false;rig=live;
                cabin=next;return true;
            }
        }
        // Combat vehicles (Grape, Negling, Caliban): one seat set into the
        // driver's compartment of the real vehicle, left of the centreline,
        // the whole cockpit inside the hull (cockpit_combat_shells.h). Above
        // the sill, from behind the seat forward, the hull is cut away and the
        // cut covered, so the rider sees ahead, both sides and up over the
        // vehicle's own body, and the turret or launcher behind.
        for(const auto& shell:kCombatShells) {
            if(seatIndex!=0||!CabinNode(lookup,v+0xEE0,nodes,count,shell.model))continue;
            auto* hull=CabinNode(lookup,v+0xEE0,nodes,count,shell.body);if(!hull)return false;
            const auto body=static_cast<unsigned>((hull-nodes)/0x110);
            const Matrix posed=At<Matrix>(hull,0xB0);
            // Unit rows for the cabin; the offset along the posed rows (model units).
            Matrix next=posed;
            for(int i=0;i<3;++i){float n=0;for(int k=0;k<3;++k)n+=posed.m[i][k]*posed.m[i][k];n=std::sqrt(n);
                if(!(n>.5f&&n<2.f))return false;for(int k=0;k<3;++k)next.m[i][k]=posed.m[i][k]/n;}
            if(!ValidCamera(next))return false;
            // Cabin origin: the eye less the seated-eye offset, in the hull bone's frame.
            const float origin[3]={shell.eye[0]-shell.bodyOrigin[0],shell.eye[1]-kCockpitSeatedEye[1]-shell.bodyOrigin[1],
                shell.eye[2]-kCockpitSeatedEye[2]-shell.bodyOrigin[2]};
            for(int j=0;j<3;++j)next.m[3][j]+=origin[0]*posed.m[0][j]+origin[1]*posed.m[1][j]+origin[2]*posed.m[2][j];
            if(!ValidCamera(next))return false;
            if(rig.kind!=shell.kind||rig.model!=v+0xE40||rig.nodes!=nodes||rig.resource!=resource||
               rig.nodeCount!=count||rig.bodyBone!=body) {
                CockpitRig selected{};selected.kind=shell.kind;selected.model=v+0xE40;selected.resource=resource;
                selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);selected.bodyBone=body;
                // The whole machine is drawn; the hull loses only what meets
                // the cut (SelectCockpitLimbs). The lid (cockpit_combat_shells.h)
                // rides the hull bone as a cap with no mesh of its own: it is
                // built from this hull.
                for(unsigned i=0;i<count;++i)selected.limb[i]=1;
                selected.capCount=1;selected.capBones[0]=body;selected.capMesh[0]=0xFF;
                for(unsigned p=0;p<std::size(kCombatPaints);++p)
                    if(kCombatPaints[p].kind==shell.kind&&CabinNode(lookup,v+0xEE0,nodes,count,kCombatPaints[p].model))
                        {selected.paint=static_cast<unsigned char>(p);break;}
                rig=selected;
            }
            rig.capFrames[0]=posed;
            cabin=next;return true;
        }
        // The Brute's door gunners (SGO seat 1 left, 2 right): in the open cabin
        // door on their gun's side, facing out (cockpit_brute_gunner.inc). The
        // hull bone carries the cabin, turned a quarter to that side: out of
        // the door is +Z, the gunner's right is along the fuselage.
        if((seatIndex==1||seatIndex==2)&&CabinNode(lookup,v+0xEE0,nodes,count,L"Vehicle410_heli")) {
            auto* hull=CabinNode(lookup,v+0xEE0,nodes,count,L"body");if(!hull)return false;
            const auto body=static_cast<unsigned>((hull-nodes)/0x110);
            const Matrix posed=At<Matrix>(hull,0xB0);
            Matrix unit=posed;
            for(int i=0;i<3;++i){float n=0;for(int k=0;k<3;++k)n+=posed.m[i][k]*posed.m[i][k];n=std::sqrt(n);
                if(!(n>.5f&&n<2.f))return false;for(int k=0;k<3;++k)unit.m[i][k]=posed.m[i][k]/n;}
            const float s=seatIndex==1?1.f:-1.f;
            Matrix next=unit;
            for(int k=0;k<3;++k){next.m[0][k]=-s*unit.m[2][k];next.m[1][k]=unit.m[1][k];next.m[2][k]=s*unit.m[0][k];}
            // Cabin origin, model (s*1.20, 2.32, 1.26), the middle of his door
            // panel: the body bone binds at y 1.9473.
            const float origin[3]={s*1.20f,2.32f-1.9473f,1.26f};
            for(int j=0;j<3;++j)next.m[3][j]=posed.m[3][j]+origin[0]*posed.m[0][j]+origin[1]*posed.m[1][j]+origin[2]*posed.m[2][j];
            if(!ValidCamera(next))return false;
            const unsigned variant=seatIndex==1?0u:1u;
            if(rig.kind!=CockpitKind::HeliBruteGunner||rig.model!=v+0xE40||rig.nodes!=nodes||rig.resource!=resource||
               rig.nodeCount!=count||rig.bodyBone!=body||rig.capVariant!=variant) {
                CockpitRig selected{};selected.kind=CockpitKind::HeliBruteGunner;selected.model=v+0xE40;selected.resource=resource;
                selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);selected.bodyBone=body;selected.capVariant=variant;
                for(unsigned i=0;i<count;++i)selected.limb[i]=1;
                rig=selected;
            }
            cabin=next;return true;
        }
        // Helicopters (Nereid): the pilot under the real canopy
        // (cockpit_heli_shells.h). The hull bone carries the cabin, and the
        // canopy's lining rides it as a cap built from that hull; the game's
        // own canopy glass is left out of its draw so the windows are clear.
        for(const auto& shell:kHeliShells) {
            if(seatIndex!=0||!CabinNode(lookup,v+0xEE0,nodes,count,shell.model))continue;
            auto* hull=CabinNode(lookup,v+0xEE0,nodes,count,shell.body);if(!hull)return false;
            const auto body=static_cast<unsigned>((hull-nodes)/0x110);
            const Matrix posed=At<Matrix>(hull,0xB0);
            Matrix next=posed;
            for(int i=0;i<3;++i){float n=0;for(int k=0;k<3;++k)n+=posed.m[i][k]*posed.m[i][k];n=std::sqrt(n);
                if(!(n>.5f&&n<2.f))return false;for(int k=0;k<3;++k)next.m[i][k]=posed.m[i][k]/n;}
            if(!ValidCamera(next))return false;
            const float origin[3]={shell.eye[0]-shell.bodyOrigin[0],shell.eye[1]-kCockpitSeatedEye[1]-shell.bodyOrigin[1],
                shell.eye[2]-kCockpitSeatedEye[2]-shell.bodyOrigin[2]};
            for(int j=0;j<3;++j)next.m[3][j]+=origin[0]*posed.m[0][j]+origin[1]*posed.m[1][j]+origin[2]*posed.m[2][j];
            if(!ValidCamera(next))return false;
            if(rig.kind!=shell.kind||rig.model!=v+0xE40||rig.nodes!=nodes||rig.resource!=resource||
               rig.nodeCount!=count||rig.bodyBone!=body) {
                CockpitRig selected{};selected.kind=shell.kind;selected.model=v+0xE40;selected.resource=resource;
                selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);selected.bodyBone=body;
                for(unsigned i=0;i<count;++i)selected.limb[i]=1;
                selected.capCount=1;selected.capBones[0]=body;selected.capMesh[0]=0xFF;
                rig=selected;
            }
            rig.capFrames[0]=posed;
            cabin=next;return true;
        }
        if(seatIndex)return false;
        const bool crawler=CabinNode(lookup,v+0xEE0,nodes,count,L"groundrobo")!=nullptr;
        const bool cannon=CabinNode(lookup,v+0xEE0,nodes,count,L"v605_barga_cannon")!=nullptr;
        bool barga=cannon;
        for(const auto name:{L"v515_retrobalam",L"v515_retrobalam_green",L"v515_retrobalam_ace",L"v515_retrobalam_ult"})
            barga=barga||CabinNode(lookup,v+0xEE0,nodes,count,name)!=nullptr;
        // Combat Wagon: a truck carrying the Nix chest (robo_*: the same chest
        // mesh 2.62 m lower and 1.79 m aft, its bone permuted like the V612's).
        const bool wagon=!crawler&&!barga&&CabinNode(lookup,v+0xEE0,nodes,count,L"v607_robotruck")!=nullptr;
        // Combat Frame Gravis: the same Nix cabin in its larger chest (mune,
        // bound like the V612's), fitted by research/cockpit/robot_fit.py --voxel.
        const bool gravis=!crawler&&!barga&&!wagon&&CabinNode(lookup,v+0xEE0,nodes,count,L"v608_oldrobot")!=nullptr;
        // The Wagon's chest sits on the truck's mount, so its cabin keeps the
        // lower half in the chest (NixChest); the Nix's hangs into its hidden
        // waist, the Gravis's through its hidden belly and spine.
        const auto kind=barga?CockpitKind::Barga:crawler?CockpitKind::Crawler:wagon?CockpitKind::NixChest:CockpitKind::Nix;
        const bool alternate=cannon||wagon||gravis||CabinNode(lookup,v+0xEE0,nodes,count,L"v612_nix")!=nullptr;
        if(!alternate&&!crawler&&!barga) {
            bool nix=false;
            for(const auto& p:cabins) if(!std::strcmp(p.label,"nix-head")&&CabinNode(lookup,v+0xEE0,nodes,count,p.modelBone)) {nix=true;break;}
            if(!nix)return false;
        }
        auto* body=CabinNode(lookup,v+0xEE0,nodes,count,barga?L"koshi":wagon?L"robo_body":gravis?L"mune":L"body");if(!body)return false;
        const Matrix posed=At<Matrix>(body,0xB0);if(!ValidCamera(posed))return false;
        Matrix next=posed;
        if(alternate) for(int j=0;j<3;++j) {next.m[0][j]=-posed.m[2][j];next.m[1][j]=posed.m[0][j];next.m[2][j]=-posed.m[1][j];}
        // MDB bind survey: chest body origin Y=6.313; hull Y=6.363..8.453.
        // Crawler survey: bind body Y=1.28708; seated cabin origin Y=2.45,
        // Z=.05. Keep its full animated basis while climbing walls/ceilings.
        // Nix fixed cabin origin Y=7.18, Z~0.15: the torso's bottom plate is
        // Y=6.45..6.57 under the seat and its front top Y=7.57
        // (research/cockpit/robot_fit.py). The roof keeps under the chest top;
        // the lower half (deck -1.115) hangs into the hidden waist, where it
        // was before the cabin was fitted to the chest (the user: comfort
        // before a strict fit). It was at 7.38 (all in the chest, the roof
        // 13 cm over the eyes) and before that 7.093. CockpitSeatedCamera
        // supplies the eye.
        // The former head eye was Y=7.95/Z=1.30.
        // Barga pelvis bind Y=28.256399. Eye Y=41.001319 (+1.5m from
        // the first cabin) follows the lower body, never the punching chest.
        // Gravis chest bind Y=7.2408: its cabin origin Y=7.58, Z=.10, the
        // ceiling under its head's flat underside (Y 8.133), the deck through
        // the hidden belly and spine, 9 cm over the waist. At 7.65 the head's
        // underside hung 3 cm below the ceiling.
        for(int j=0;j<3;++j) next.m[3][j]+=(barga?12.64492f:crawler?1.16292f:gravis?.3392f:.867f)*next.m[1][j]+(barga?.10f:crawler?.05f:gravis?.10f:.15f)*next.m[2][j];
        if(!ValidCamera(next))return false;
        if(rig.kind!=kind||rig.model!=v+0xE40||rig.nodes!=nodes||rig.resource!=resource||rig.nodeCount!=count) {
            CockpitRig selected{};selected.kind=kind;selected.model=v+0xE40;selected.resource=resource;selected.nodes=nodes;selected.nodeCount=static_cast<unsigned>(count);
            if(crawler)selected.bodyBone=static_cast<unsigned>((body-nodes)/0x110);
            constexpr const wchar_t* names[]={L"jowan_l",L"kawan_l",L"jowan_r",L"kawan_r",L"momo_l",L"sune0_l",L"sune1_l",L"ashi_l",L"heel_l",L"toe_l",L"footPanel_l",
                L"momo_r",L"sune0_r",L"sune1_r",L"ashi_r",L"heel_r",L"toe_r",L"footPanel_r",L"weapon_hand_l",L"weapon_lowerArm_l",L"weapon_upperArm_l",L"weapon_upperArm_l1",
                L"weapon_hand_r",L"weapon_lowerArm_r",L"weapon_upperArm_r",L"weapon_upperArm_r1",L"weapon_shoulder_l",L"weapon_shoulder_r",L"head"};
            // Groundrobo's four complete leg chains and weapon attachment
            // nodes stay native/animated. The body's surveyed undercarriage
            // is also retained by SelectCockpitLimbs; only its upper shell goes.
            constexpr const wchar_t* crawlerNames[]={L"leg_b0_l",L"leg_b1_l",L"leg_b2_l",L"leg_b3_l",L"heel_b_l",
                L"leg_b0_r",L"leg_b1_r",L"leg_b2_r",L"leg_b3_r",L"heel_b_r",
                L"leg_f0_l",L"leg_f1_l",L"leg_f2_l",L"leg_f3_l",L"heel_f_l",
                L"leg_f0_r",L"leg_f1_r",L"leg_f2_r",L"leg_f3_r",L"heel_f_r",
                L"weapon_node_l",L"weapon_joint_l",L"weapon_node_r",L"weapon_joint_r",L"gatling_joint"};
            // The wagon keeps its truck, wheels, head, left arm and the right
            // shoulder weapon on its strut; only the chest and its joints go.
            constexpr const wchar_t* wagonNames[]={L"body",L"tireB0_l",L"tireB0_r",L"tireB1_l",L"tireB1_r",L"tireF_l",L"tireF_r",
                L"robo_head",L"robo_jowan_l",L"robo_kawan_l",L"robo_kata_r",L"robo_weaponShoulder_r"};
            // The Gravis keeps its waist, legs, arms, back boosters and head; the
            // chest, the belly and the spine the cabin's deck hangs through go.
            constexpr const wchar_t* gravisNames[]={L"koshi",L"momo_l",L"sune_l",L"foot_l",L"heel_l",L"toe_l",L"foot_roll_l",
                L"momo_r",L"sune_r",L"foot_r",L"heel_r",L"toe_r",L"foot_roll_r",L"arm_l",L"arm_slide_l",L"arm_r",L"arm_slide_r",
                L"booster_l",L"booster_r",L"head",L"eye"};
            unsigned found=0;
            const auto chosen=crawler?crawlerNames:wagon?wagonNames:gravis?gravisNames:names;
            const unsigned total=crawler?unsigned(std::size(crawlerNames)):wagon?unsigned(std::size(wagonNames)):
                gravis?unsigned(std::size(gravisNames)):unsigned(std::size(names));
            for(unsigned i=0;i<total;++i)if(auto* node=CabinNode(lookup,v+0xEE0,nodes,count,chosen[i])) {selected.limb[(node-nodes)/0x110]=1;++found;}
            if(barga) {
                selected.limb={};found=0;selected.capVariant=cannon?1u:0u;
                constexpr const wchar_t* bargaNames[]={L"koshi",L"booster_l",L"booster_r",
                    L"jowan_l",L"kawan_l",L"hand_l",L"jowan_r",L"kawan_r",L"hand_r",
                    L"fingIn0_l",L"fingIn1_l",L"fingOutA0_l",L"fingOutA1_l",L"fingOutB0_l",L"fingOutB1_l",L"fingOutC0_l",L"fingOutC1_l",
                    L"fingIn0_r",L"fingIn1_r",L"fingOutA0_r",L"fingOutA1_r",L"fingOutB0_r",L"fingOutB1_r",L"fingOutC0_r",L"fingOutC1_r",
                    L"shoulderArmor_l",L"shoulderArmorOut_l",L"shoulderPole_l",L"shoulderCenter_l",
                    L"shoulderArmor_r",L"shoulderArmorOut_r",L"shoulderPole_r",L"shoulderCenter_r",
                    L"legRoll_l",L"legRoll_r",L"momo_l",L"sune_l",L"footPanel_l",L"footSlide_l",L"foot_l",L"toeA_l",L"toeB_l",L"toeC_l",L"toeD_l",
                    L"momo_r",L"sune_r",L"footPanel_r",L"footSlide_r",L"foot_r",L"toeA_r",L"toeB_r",L"toeC_r",L"toeD_r",
                    L"cannon0_l",L"cannon1_l",L"cannon2_l",L"cannonSlideB_l",L"cannonSlideF_l",L"cannon3_l",
                    L"cannon0_r",L"cannon1_r",L"cannon2_r",L"cannonSlideB_r",L"cannonSlideF_r",L"cannon3_r"};
                for(const auto name:bargaNames)if(auto* node=CabinNode(lookup,v+0xEE0,nodes,count,name)) {selected.limb[(node-nodes)/0x110]=1;++found;}
                if(found<(cannon?65u:53u))return false;
                for(unsigned i=0;i<std::size(kBargaCapBones);++i) {
                    auto* node=CabinNode(lookup,v+0xEE0,nodes,count,kBargaCapBones[i]);if(!node)return false;
                    selected.capBones[i]=static_cast<unsigned>((node-nodes)/0x110);
                }
                selected.capCount=unsigned(std::size(kBargaCapBones));
            } else if(found<(crawler?25u:wagon?12u:gravis?21u:14u))return false;
            rig=selected;
        }
        if(barga) {
            auto snapshot=rig;
            for(unsigned i=0;i<snapshot.capCount;++i) {
                snapshot.capFrames[i]=At<Matrix>(nodes+snapshot.capBones[i]*0x110,0xB0);
                if(!ValidCamera(snapshot.capFrames[i]))return false;
            }
            rig=snapshot;
        }
        cabin=next;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ValidateCockpitModel(const ImageProfile& image,void* model,const CockpitRig& rig) noexcept {
    __try {
        if(!model||model!=rig.model||!Readable(model,0xC8)||!HasType(image,model,".?AVAnimationModel@@"))return false;
        return At<void*>(model,0xA0)==rig.resource&&At<void*>(model,0xB0)==rig.nodes&&At<std::uint64_t>(model,0xC0)==rig.nodeCount;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool VehicleHeadingReference(const Quat& head,Quat& reference) noexcept {
    HeadBasis basis{};if(!HeadBasisFromXr(head,basis))return false;
    reference={0,std::sin(basis.yaw*.5f),0,std::cos(basis.yaw*.5f)};return true;
}
bool LevelVehicleAtEntry(VehicleEntryLevel& reference,const Matrix& input,Matrix& output) noexcept {
    if(!ValidCamera(input))return false;
    if(!reference.valid){Matrix level{};float pitch=0;if(!LevelCameraPitch(input,level,pitch))return false;
        reference.pitch=pitch;reference.valid=true;}
    if(!std::isfinite(reference.pitch))return false;
    const float c=std::cos(reference.pitch),s=std::sin(reference.pitch);output=input;
    for(int j=0;j<3;++j){output.m[1][j]=c*input.m[1][j]-s*input.m[2][j];output.m[2][j]=s*input.m[1][j]+c*input.m[2][j];}
    return ValidCamera(output);
}
bool ComposeVehicleCamera(const Matrix& nativeCamera,const Quat& reference,
    const Quat& head,const Vec3& delta,Matrix& output,float& localYaw) noexcept {
    Matrix ref{},current{};
    if(!ValidCamera(nativeCamera) || !Basis(reference,ref) || !Basis(head,current)
        || !std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z)
        || delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>100) return false;
    Matrix relative{};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j)
        for(int k=0;k<3;++k) relative.m[i][j]+=current.m[i][k]*ref.m[j][k];
    output=nativeCamera;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
        output.m[i][j]=0;
        for(int k=0;k<3;++k) output.m[i][j]+=relative.m[i][k]*nativeCamera.m[k][j];
    }
    const auto game=XrToGame(delta);
    const float d[3]={game.x,game.y,game.z};
    float local[3]{};
    for(int i=0;i<3;++i) for(int k=0;k<3;++k) local[i]+=d[k]*ref.m[i][k];
    for(int j=0;j<3;++j) for(int k=0;k<3;++k) output.m[3][j]+=local[k]*nativeCamera.m[k][j];
    localYaw=std::atan2(relative.m[2][0],relative.m[2][2]);
    return ValidCamera(output);
}
void RebaseVehicleStick(float yaw,float& x,float& y) noexcept {
    if(!std::isfinite(yaw) || !std::isfinite(x) || !std::isfinite(y)) { x=y=0;return; }
    // XInput +X is right, while EDF positive yaw turns left.
    const float c=std::cos(yaw),s=std::sin(yaw),oldX=x;
    x=c*x-s*y;y=s*oldX+c*y;
    const float peak=std::fmax(1.0f,std::fmax(std::fabs(x),std::fabs(y)));
    x/=peak;y/=peak;
}
bool VehicleStickForward(const ImageProfile& image,const VehicleSeat& seat,NodeLookup lookup,const Matrix& hull,Vec3& forward,WalkerProbe* probe) noexcept {
    __try {
        auto* v=static_cast<unsigned char*>(seat.vehicle);
        if(!ValidCamera(hull)||!Readable(v,0xF08)||!Readable(seat.seat,0x340))return false;
        auto* seats=At<unsigned char*>(v,0x608);const auto seatCount=At<std::uint64_t>(v,0x618);
        if(!seatCount||seatCount>16||!Readable(seats,seatCount*0x340)||seat.seat!=seats)return false;   // the driver only
        forward={hull.m[2][0],hull.m[2][1],hull.m[2][2]};
        // Walkers read the stick on their lower body in the game (the Nix,
        // hardware 2026-09-25; the Proteus, Barga and Gravis alike), so theirs
        // goes as read: false. The probe still gets their lower body (koshi,
        // which carries the legs and the crotch plate) for the log; its axes
        // are the model's in V504/V515/V407, and permuted like the cabin's own
        // bone in V612/V605/V608 (forward -Y) and V614 (forward +Y), as the bind
        // matrices show (research/cockpit: koshi binds as body/neck).
        if(!lookup||!HasType(image,v+0xE40,".?AVAnimationModel@@"))return true;
        auto* nodes=At<unsigned char*>(v,0xEF0);const auto count=At<std::uint64_t>(v,0xF00);auto* resource=At<void*>(v,0xEE0);
        if(!resource||!Readable(resource,8)||count<30||count>256||!Readable(nodes,count*0x110))return true;
        auto* koshi=CabinNode(lookup,v+0xEE0,nodes,count,L"koshi");if(!koshi)return true;
        bool walker=false;int axis=2;float sign=1;
        for(const auto name:{L"v504_begaruta",L"v504_begaruta_blue",L"v504_begaruta_coating",L"v504_begaruta_gray1",L"v504_begaruta_gray2",
            L"v504_begaruta_haispeed",L"v504_begaruta_pink",L"v504_begaruta_red",L"v504_begaruta_yellow",L"Vehicle407_bigbegaruta",
            L"v515_retrobalam",L"v515_retrobalam_green",L"v515_retrobalam_ace",L"v515_retrobalam_ult"})
            if(CabinNode(lookup,v+0xEE0,nodes,count,name)){walker=true;break;}
        if(!walker&&(CabinNode(lookup,v+0xEE0,nodes,count,L"v612_nix")||CabinNode(lookup,v+0xEE0,nodes,count,L"v605_barga_cannon")||
            CabinNode(lookup,v+0xEE0,nodes,count,L"v608_oldrobot"))){walker=true;axis=1;sign=-1;}
        if(!walker&&CabinNode(lookup,v+0xEE0,nodes,count,L"v614_proteus_mk2")){walker=true;axis=1;}
        if(!walker)return true;
        const Matrix legs=At<Matrix>(koshi,0xB0);if(!ValidCamera(legs))return false;
        forward={sign*legs.m[axis][0],sign*legs.m[axis][1],sign*legs.m[axis][2]};
        if(probe) {
            probe->valid=true;probe->koshi=forward;probe->position={legs.m[3][0],legs.m[3][1],legs.m[3][2]};
            if(auto* body=CabinNode(lookup,v+0xEE0,nodes,count,L"body")) {
                const Matrix chest=At<Matrix>(body,0xB0);
                if(ValidCamera(chest))probe->body={sign*chest.m[axis][0],sign*chest.m[axis][1],sign*chest.m[axis][2]};
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool VehicleStickOnHull(const ImageProfile& image,const VehicleSeat& seat) noexcept {
    // The Negling and the Combat Wagon are trucks carrying a turning upper
    // body; the game drives them on the truck, so the stick is left as read.
    // So does the Nix on its lower body (hardware, 2026-09-25: turned by the
    // lower body's angle off the camera, the walk followed the upper body's
    // twist, as the Negling's had followed its launcher). And the tanks
    // (hardware, 2026-09-27: the Kebler drove along its barrel -- the game
    // steers a tank on its hull, the camera rebase added the turret's angle):
    // the Railgun, the Titan, the Blacker, the EMC, the Varias, the Kebler.
    __try {
        if(!seat.vehicle) return false;
        for(const auto type:{".?AVVehicle402_Rocket@@",".?AVVehicle607_RoboTruck@@",".?AVVehicle504_begaruta@@",".?AVVehicle612_nix@@",
            ".?AVVehicle403_Tank@@",".?AVVehicle404_Tank@@",".?AVVehicle505_Tank@@",".?AVVehicle510_Maser@@",".?AVVehicle601_Tank@@",".?AVVehicle603_Flak@@"})
            if(HasType(image,seat.vehicle,type)) return true;
        return false;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool VehicleStickYaw(const Matrix& nativeCamera,const Vec3& forward,float& yaw) noexcept {
    // On the ground plane: the native camera looks down on the machine, and a
    // stick means a heading, not a climb.
    const float lx=nativeCamera.m[0][0],lz=nativeCamera.m[0][2],fx=nativeCamera.m[2][0],fz=nativeCamera.m[2][2];
    const float left=std::sqrt(lx*lx+lz*lz),ahead=std::sqrt(fx*fx+fz*fz),flat=std::sqrt(forward.x*forward.x+forward.z*forward.z);
    if(!(left>.3f&&ahead>.3f&&flat>.3f)||!std::isfinite(forward.x)||!std::isfinite(forward.z))return false;
    yaw=std::atan2((forward.x*lx+forward.z*lz)/left,(forward.x*fx+forward.z*fz)/ahead);
    return std::isfinite(yaw);
}
}
