// Included by plugin.cpp inside its private namespace, after the shot sites.
//
// A kick for a vehicle's own weapons, the infantry one scaled up. The machines
// that already recoil (the tanks and trucks) are left alone; these are the ones
// whose guns sit still however hard they fire: the Nix and the Gravis (the arm
// from its root at the shoulder outward, and the shoulder launchers) and the
// Depth Crawler (the gatling and the two side mounts). The robots only: the
// helicopters' guns (409's chin gun and pods, 410's side cannons) kicked as
// well until the user took them off again (2026-09-26).
//
// A shot is a weapon's ammunition going down, read once an update from the
// weapons the vehicle holds, while the player is holding a fire control (a
// trigger or a grip, g_playerFireAt). Not everything held as a weapon is a
// gun: the helicopters keep their fuel as one, and it goes down every update
// whether anyone fires or not (the Brute's side guns shook without a shot
// fired). A count that drops twenty times within a second with the controls
// untouched is left out for as long as it keeps dropping. Which part a shot
// came from is the part nearest the weapon's muzzle. The kick is the infantry
// one -- the same angle per shot and
// the same decay -- turned about the part's own pivot, so a longer arm swings
// further, and pushed back along the shot by a distance set for the part's
// size. It is applied to the bones as they are handed to the draw and taken
// off again afterwards, so the game's skeleton never sees it.
#pragma once

struct VehicleRecoilPart {
    const wchar_t* moves[8];   // moves[0] is the pivot; all of them move together
    float backMetres;          // pushed back this far per unit of kick
};
struct VehicleRecoilMachine {
    const char* classes[2];    // the vehicle's own RTTI class, when that is what tells it apart
    const wchar_t* marker;     // else a bone only this model has
    const char* label;
    VehicleRecoilPart parts[6];
    unsigned count;
};
// Bones from research/vehicle/model_anchor_catalog.json. The infantry weapon
// goes back 5 cm per unit; these are 1.5 to 2.5 times that for the size.
//
// The Nix the player calls in ("Combat Frame Nix C1" and the rest) is
// Vehicle504_begaruta, and each paint of it names its root bone differently
// (v504_begaruta_blue, _red, _gray1...), so it is told by its class. Its arm
// bones are the V612's and more (the upper-arm mount); a bone a model lacks is
// simply not moved. The arm names are no marker: the Balam and the Barga
// cannon have them too, and they were not asked for.
constexpr VehicleRecoilMachine kVehicleRecoilMachines[]={
    // The Gravis (v608_oldrobot) is a Vehicle504_begaruta as well, so it is
    // looked for first, by its own bone, or it would be taken for a Nix and
    // none of its arm bones found.
    {{},L"v608_oldrobot","gravis",{
        {{L"arm_l",L"arm_slide_l",L"weapon_hand_l",L"weapon_lowerArm_l",L"weapon_upperArm_l"},0.12f},
        {{L"arm_r",L"arm_slide_r",L"weapon_hand_r",L"weapon_lowerArm_r",L"weapon_upperArm_r"},0.12f},
        {{L"weapon_shoulder_l"},0.08f},{{L"weapon_shoulder_r"},0.08f}},4},
    {{".?AVVehicle504_begaruta@@",".?AVVehicle612_nix@@"},nullptr,"nix",{
        {{L"kata_l",L"jowan_l",L"kawan_l",L"weapon_hand_l",L"weapon_lowerArm_l",L"weapon_upperArm_l",L"weapon_upperArm_l1"},0.10f},
        {{L"kata_r",L"jowan_r",L"kawan_r",L"weapon_hand_r",L"weapon_lowerArm_r",L"weapon_upperArm_r",L"weapon_upperArm_r1"},0.10f},
        {{L"weapon_shoulder_l"},0.08f},{{L"weapon_shoulder_r"},0.08f}},4},
    {{},L"groundrobo","crawler",{
        {{L"gatling_joint"},0.10f},
        {{L"weapon_node_l",L"weapon_joint_l"},0.08f},{{L"weapon_node_r",L"weapon_joint_r"},0.08f}},3},
};

bool g_vehicleRecoilOn=true;       // [VR] VehicleRecoil
float g_vehicleRecoilScale=1.0f;   // [VR] VehicleRecoilScale, on angle and distance both

constexpr unsigned kVehicleRecoilParts=6, kVehicleRecoilWeapons=8;
struct VehicleRecoilPublished {
    void* model=nullptr;
    unsigned char* nodes=nullptr;   // the vehicle's node array: the pivots are read from it
    unsigned nodeCount=0;
    ULONGLONG seen=0;
    // The moment the kick is measured at, taken once in the camera update and
    // used by every draw of that frame. Read afresh in each draw, the depth
    // pass and the colour pass saw the part a few milliseconds of decay apart;
    // the colour pass keeps only what matches the depth exactly, so the part
    // went grey for the length of every burst.
    double frameTime=0;
    // The weapons are models of their own (weapon+0xF30), placed on the mount
    // bone after the vehicle is posed, so turning the arm left them where they
    // were. Each follows the part it hangs on, whole.
    void* weaponModels[8]{}; unsigned char weaponPart[8]{}; unsigned weaponModelCount=0;
    unsigned partCount=0;
    struct Part {
        unsigned moves[8]{}; unsigned moveCount=0;
        float backMetres=0;
        edf6vr::RecoilKick kick{};
        float direction[3]{0,0,1};
    } parts[kVehicleRecoilParts]{};
};
SRWLOCK g_vehicleRecoilLock=SRWLOCK_INIT;
VehicleRecoilPublished g_vehicleRecoilDraw{};   // under g_vehicleRecoilLock
std::atomic<unsigned long long> g_vehicleRecoilFrames{0};   // frames the draw took the state for (VehicleRecoilFrame)

// The update thread's own record. Only ApplyVehicleCamera's thread touches it.
struct VehicleRecoilWork {
    void* vehicle=nullptr;
    unsigned char* nodes=nullptr;
    unsigned nodeCount=0;
    const VehicleRecoilMachine* machine=nullptr;
    VehicleRecoilPublished published{};
    void* weapons[kVehicleRecoilWeapons]{};
    int ammo[kVehicleRecoilWeapons]{};
    unsigned weaponCount=0,scans=0;
    ULONGLONG scannedAt=0,reportedAt=0;
    unsigned long long shots[kVehicleRecoilParts]{};
    unsigned long long unplaced=0,notMine=0,drained=0;
    // Per weapon: every drop seen, the drops with nobody at the controls
    // counted over a second, and until when it is taken to be draining.
    unsigned long long drops[kVehicleRecoilWeapons]{};
    ULONGLONG idleFrom[kVehicleRecoilWeapons]{},drainUntil[kVehicleRecoilWeapons]{};
    unsigned idleCount[kVehicleRecoilWeapons]{};
    // Which weapon each published model belongs to, and the part it rides.
    unsigned char modelWeapon[8]{},modelPart[8]{};
};
VehicleRecoilWork g_vehicleRecoilWork{};
std::atomic<unsigned long long> g_vehicleRecoilApplied{0},g_vehicleRecoilMismatch{0};
std::atomic<unsigned> g_vehicleRecoilPalette{0};

void __fastcall HookModelDraw(void* model,void* renderContext,int pass,void* view);

bool VehicleRecoilWeapon(const void* p) noexcept {
    const char* name=edf6vr::TypeName(g_image,p);
    return name && !std::strncmp(name,".?AVWeapon_",11);
}
// The weapons a vehicle holds, wherever it keeps them: a pointer in the object
// itself, in a block it points at, or one step further (the soldier keeps his
// behind a wrapper in an array of entries). Once on boarding, and again every
// two seconds for a while if none turned up (they may be made after).
unsigned VehicleRecoilFindWeapons(unsigned char* v,void* out[kVehicleRecoilWeapons]) noexcept {
    unsigned n=0;
    auto add=[&](void* w) {
        if(!w || n>=kVehicleRecoilWeapons) return;
        for(unsigned i=0;i<n;++i) if(out[i]==w) return;
        out[n++]=w;
    };
    __try {
        for(std::size_t off=0;off<0x1800 && n<kVehicleRecoilWeapons;off+=8) {
            if(!edf6vr::Readable(v+off,8)) break;
            auto* p=*reinterpret_cast<unsigned char**>(v+off);
            if(!p || !edf6vr::Readable(p,0x80)) continue;
            if(VehicleRecoilWeapon(p)) { add(p); continue; }
            for(std::size_t k=0;k<16;++k) {
                auto* q=*reinterpret_cast<unsigned char**>(p+k*8);
                if(!q || !edf6vr::Readable(q,0x10)) continue;
                if(VehicleRecoilWeapon(q)) { add(q); continue; }
                for(std::size_t m=0;m<2;++m) {
                    auto* r=*reinterpret_cast<unsigned char**>(q+m*8);
                    if(r && VehicleRecoilWeapon(r)) add(r);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}
// Where a weapon's shot leaves and which way: its transform, as a soldier's
// weapon keeps it (weapon+0x1D0; rows at +0x50, translation at +0x80), turned
// by the local fire direction at +0x350.
bool VehicleRecoilMuzzle(void* weapon,float at[3],float direction[3]) noexcept {
    __try {
        auto* w=static_cast<unsigned char*>(weapon);
        if(!edf6vr::Readable(w,0x360)) return false;
        auto* t=*reinterpret_cast<unsigned char**>(w+0x1D0);
        if(!edf6vr::Readable(t,0x90)) return false;
        const auto* rows=reinterpret_cast<const float*>(t+0x50);
        const auto* local=reinterpret_cast<const float*>(w+0x350);
        float d[3]{},size=0;
        for(int j=0;j<3;++j) {
            at[j]=rows[12+j];
            d[j]=local[0]*rows[j]+local[1]*rows[4+j]+local[2]*rows[8+j];
            size+=d[j]*d[j];
        }
        size=std::sqrt(size);
        if(!(size>1e-4f)) { for(int j=0;j<3;++j) d[j]=rows[8+j]; size=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); }
        if(!(size>1e-4f)) return false;
        for(int j=0;j<3;++j) direction[j]=d[j]/size;
        return std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool VehicleRecoilNodeAt(const unsigned char* nodes,unsigned index,float at[3]) noexcept {
    __try {
        const auto* m=reinterpret_cast<const float*>(nodes+static_cast<std::size_t>(index)*0x110+0xB0);
        for(int j=0;j<3;++j) at[j]=m[12+j];
        return std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// A bone by name, as its index in the vehicle's node array (0x110 apart): the
// game's own lookup, asked the way vehicle_camera.cpp's CabinNode asks it.
struct VehicleRecoilName { wchar_t text[8]{}; std::uint64_t size=0,capacity=7; };
static_assert(sizeof(VehicleRecoilName)==32);
bool CabinNodeIndex(void* registry,unsigned char* nodes,unsigned count,const wchar_t* key,unsigned* index=nullptr) noexcept {
    if(!g_nodeLookup || !registry || !nodes || !key) return false;
    VehicleRecoilName name{};
    name.size=wcslen(key);
    if(name.size<=7) wmemcpy(name.text,key,name.size);
    else { *reinterpret_cast<const wchar_t**>(name.text)=key; name.capacity=name.size; }
    std::uintptr_t node=0;
    __try { node=reinterpret_cast<std::uintptr_t>(g_nodeLookup(registry,&name)); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    const auto begin=reinterpret_cast<std::uintptr_t>(nodes);
    if(!node || node<begin || node-begin>=static_cast<std::uintptr_t>(count)*0x110 || (node-begin)%0x110) return false;
    if(index) *index=static_cast<unsigned>((node-begin)/0x110);
    return true;
}
void VehicleRecoilRebuild(VehicleRecoilWork& w,unsigned char* v,unsigned char* nodes,unsigned count) noexcept {
    w=VehicleRecoilWork{};
    w.vehicle=v; w.nodes=nodes; w.nodeCount=count;
    auto* registry=v+0xEE0;
    for(const auto& machine:kVehicleRecoilMachines) {
        bool match=false;
        for(const char* type:machine.classes) if(type && edf6vr::HasType(g_image,v,type)) match=true;
        if(!match && machine.marker) match=CabinNodeIndex(registry,nodes,count,machine.marker);
        if(!match) continue;
        w.machine=&machine; break;
    }
    if(!w.machine) {
        Log("VEHICLERECOIL none for %s: not one of the machines that kick",edf6vr::TypeName(g_image,v));
        return;
    }
    auto& out=w.published;
    out.model=v+0xE40; out.nodeCount=count;
    for(unsigned p=0;p<w.machine->count && out.partCount<kVehicleRecoilParts;++p) {
        auto& part=out.parts[out.partCount];
        part=VehicleRecoilPublished::Part{};
        part.backMetres=w.machine->parts[p].backMetres;
        for(const auto* name:w.machine->parts[p].moves) {
            if(!name) break;
            unsigned index=0;
            if(CabinNodeIndex(registry,nodes,count,name,&index) && part.moveCount<8) part.moves[part.moveCount++]=index;
        }
        if(part.moveCount) ++out.partCount;
    }
    Log("VEHICLERECOIL %s: %u of %u parts found on %u bones",w.machine->label,out.partCount,w.machine->count,count);
}
// The part with a bone nearest a point, or partCount if none is within 6 m.
unsigned VehicleRecoilNearestPart(const VehicleRecoilPublished& out,const unsigned char* nodes,const float at[3]) noexcept {
    unsigned best=out.partCount; float nearest=1e9f;
    for(unsigned p=0;p<out.partCount;++p) for(unsigned b=0;b<out.parts[p].moveCount;++b) {
        float bone[3]{};
        if(!VehicleRecoilNodeAt(nodes,out.parts[p].moves[b],bone)) continue;
        const float dx=bone[0]-at[0],dy=bone[1]-at[1],dz=bone[2]-at[2];
        const float r=dx*dx+dy*dy+dz*dz;
        if(r<nearest) { nearest=r; best=p; }
    }
    return nearest<=6.0f*6.0f?best:out.partCount;
}
// Every camera update while the player is in a vehicle.
void VehicleRecoilUpdate(void* vehicle) noexcept {
    if(!g_vehicleRecoilOn || !vehicle || !g_nodeLookup) return;
    auto& w=g_vehicleRecoilWork;
    auto* v=static_cast<unsigned char*>(vehicle);
    unsigned char* nodes=nullptr; std::uint64_t count=0;
    __try {
        if(!edf6vr::Readable(v,0xF08)) return;
        nodes=*reinterpret_cast<unsigned char**>(v+0xEF0);
        count=*reinterpret_cast<std::uint64_t*>(v+0xF00);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if(!nodes || !count || count>256 || !edf6vr::Readable(nodes,static_cast<std::size_t>(count)*0x110)) return;
    if(w.vehicle!=v || w.nodes!=nodes || w.nodeCount!=count) VehicleRecoilRebuild(w,v,nodes,static_cast<unsigned>(count));
    if(!w.machine) return;
    const ULONGLONG tick=GetTickCount64();
    if(!w.weaponCount && w.scans<10 && tick-w.scannedAt>2000) {
        w.scannedAt=tick; ++w.scans;
        w.weaponCount=VehicleRecoilFindWeapons(v,w.weapons);
        for(unsigned i=0;i<w.weaponCount;++i) {
            __try { w.ammo[i]=*reinterpret_cast<const int*>(static_cast<unsigned char*>(w.weapons[i])+0xBE8); }
            __except(EXCEPTION_EXECUTE_HANDLER) { w.ammo[i]=0; }
            float at[3]{},d[3]{};
            const bool muzzle=VehicleRecoilMuzzle(w.weapons[i],at,d);
            void* owner=nullptr;
            __try { owner=*reinterpret_cast<void**>(static_cast<unsigned char*>(w.weapons[i])+0x120); }
            __except(EXCEPTION_EXECUTE_HANDLER) { owner=nullptr; }
            Log("VEHICLERECOIL weapon %u %s ammo=%d owner=%p (vehicle %p) muzzle=%d (%.2f,%.2f,%.2f)",i,
                edf6vr::TypeName(g_image,w.weapons[i]),w.ammo[i],owner,vehicle,muzzle?1:0,at[0],at[1],at[2]);
        }
        if(!w.weaponCount) Log("VEHICLERECOIL %s: no weapons found yet (scan %u)",w.machine->label,w.scans);
        auto& found=w.published;
        found.weaponModelCount=0;
        for(unsigned i=0;i<w.weaponCount && found.weaponModelCount<8;++i) {
            auto* model=static_cast<unsigned char*>(w.weapons[i])+edf6vr::kWeaponModelOffset;
            if(!edf6vr::HasType(g_image,model,".?AVAnimationModel@@")) continue;
            float at[3]{},d[3]{};
            if(!VehicleRecoilMuzzle(w.weapons[i],at,d)) continue;
            const unsigned part=VehicleRecoilNearestPart(found,nodes,at);
            if(part>=found.partCount) continue;
            w.modelWeapon[found.weaponModelCount]=static_cast<unsigned char>(i);
            w.modelPart[found.weaponModelCount]=static_cast<unsigned char>(part);
            found.weaponModels[found.weaponModelCount]=model;
            found.weaponPart[found.weaponModelCount++]=static_cast<unsigned char>(part);
            Log("VEHICLERECOIL weapon %u model rides part %u",i,part);
        }
    }
    const double now=Now();
    auto& out=w.published;
    for(unsigned i=0;i<w.weaponCount;++i) {
        int ammo=0;
        __try { ammo=*reinterpret_cast<const int*>(static_cast<unsigned char*>(w.weapons[i])+0xBE8); }
        __except(EXCEPTION_EXECUTE_HANDLER) { continue; }
        const int before=w.ammo[i]; w.ammo[i]=ammo;
        if(!(ammo<before) || before-ammo>1000) continue;   // a reload, or nonsense
        ++w.drops[i];
        // Still draining: left out for as long as it keeps going down.
        if(tick<w.drainUntil[i]) { w.drainUntil[i]=tick+2000; ++w.drained; continue; }
        // Not while the player's hands are off the fire controls: a teammate's
        // shot from another seat, or a count going down on its own.
        if(tick-g_playerFireAt.load(std::memory_order_relaxed)>300) {
            ++w.notMine;
            if(tick-w.idleFrom[i]>1000) { w.idleFrom[i]=tick; w.idleCount[i]=0; }
            if(++w.idleCount[i]>=20) {
                w.drainUntil[i]=tick+2000; w.idleCount[i]=0;
                Log("VEHICLERECOIL weapon %u goes down on its own (now %d): not a gun, left out while it does",i,ammo);
            }
            continue;
        }
        float at[3]{},d[3]{};
        if(!VehicleRecoilMuzzle(w.weapons[i],at,d)) { ++w.unplaced; continue; }
        // The part with a bone nearest the muzzle.
        const unsigned best=VehicleRecoilNearestPart(out,nodes,at);
        if(best>=out.partCount) { ++w.unplaced; continue; }
        out.parts[best].kick.Shot(now,g_recoilDecaySeconds);
        for(int j=0;j<3;++j) out.parts[best].direction[j]=d[j];
        ++w.shots[best];
    }
    for(unsigned k=0;k<out.weaponModelCount;++k)
        out.weaponPart[k]=tick<w.drainUntil[w.modelWeapon[k]]?static_cast<unsigned char>(kVehicleRecoilParts):w.modelPart[k];
    out.seen=tick; out.frameTime=now; out.nodes=nodes;
    AcquireSRWLockExclusive(&g_vehicleRecoilLock); g_vehicleRecoilDraw=out; ReleaseSRWLockExclusive(&g_vehicleRecoilLock);
    if(tick-w.reportedAt>5000) {
        w.reportedAt=tick;
        char drops[160]; drops[0]=0;
        for(unsigned i=0,used=0;i<w.weaponCount && used<sizeof(drops)-32;++i) {
            const int n=std::snprintf(drops+used,sizeof(drops)-used,"%s%llu%s",i?"/":"",w.drops[i],
                                      tick<w.drainUntil[i]?"(drain)":"");
            if(n<0) break;
            used+=static_cast<unsigned>(n);
        }
        Log("VEHICLERECOIL %s weapons=%u models=%u drops by weapon=%s shots by part=%llu/%llu/%llu/%llu/%llu/%llu unplaced=%llu notMine=%llu drained=%llu drawn=%llu paletteMismatch=%llu(palette %u, nodes %u) framesTaken=%llu",
            w.machine->label,w.weaponCount,out.weaponModelCount,drops,w.shots[0],w.shots[1],w.shots[2],w.shots[3],w.shots[4],w.shots[5],w.unplaced,w.notMine,w.drained,
            g_vehicleRecoilApplied.load(),g_vehicleRecoilMismatch.load(),g_vehicleRecoilPalette.load(),w.nodeCount,g_vehicleRecoilFrames.load());
    }
}

// The render palette of a model for one pass: the same walk as
// ResolveHoldPalette, allowing a vehicle's bone count.
bool VehicleRecoilPalette(void* model,int pass,edf6vr::Matrix*& palette,std::size_t& count) noexcept {
    __try {
        auto bytes=static_cast<unsigned char*>(model);
        if(pass<0 || pass>5 || pass==4 || !edf6vr::Readable(bytes,0x483)) return false;
        auto registry=bytes+0xA0;
        if(pass!=2 || !bytes[0x482]) {
            auto descriptor=bytes+0x160;
            const auto lodCount=*reinterpret_cast<std::uint64_t*>(bytes+0x260);
            if(lodCount>64) return false;
            auto lods=*reinterpret_cast<unsigned char**>(bytes+0x250);
            if(lodCount) {
                const auto index=bytes[0x45];
                if(index>7 || !edf6vr::Readable(lods,static_cast<std::size_t>(lodCount)*0xF0)) return false;
                const float distance=*reinterpret_cast<float*>(bytes+0x24+index*4);
                if(!std::isfinite(distance)) return false;
                for(std::size_t i=0;i<lodCount;++i) {
                    const float threshold=*reinterpret_cast<float*>(lods+i*0xF0+0xE8);
                    if(!std::isfinite(threshold)) return false;
                    if(threshold>distance*distance) break;
                    descriptor=lods+i*0xF0;
                }
            }
            registry=*reinterpret_cast<unsigned char**>(descriptor+0x58);
        }
        if(!edf6vr::Readable(registry,0x38)) return false;
        const auto n=*reinterpret_cast<std::int32_t*>(registry+0x20);
        if(n<=0 || n>256) return false;
        palette=*reinterpret_cast<edf6vr::Matrix**>(registry+0x30);
        count=static_cast<std::size_t>(n);
        return edf6vr::Readable(palette,count*sizeof(*palette),true);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Turns a bone's world matrix about the pivot by the kick: the muzzle end up,
// about the axis square to the shot and the vertical, and the whole back along
// the shot. Rows 0..2 are directions and turn; row 3 is a point.
void VehicleRecoilTurn(edf6vr::Matrix& m,const float pivot[3],const float direction[3],float angle,float back) noexcept {
    float up[3]={0,1,0};
    const float along=direction[1];
    for(int j=0;j<3;++j) up[j]-=direction[j]*along;
    const float size=std::sqrt(up[0]*up[0]+up[1]*up[1]+up[2]*up[2]);
    if(!(size>1e-3f)) return;   // shooting straight up or down: no "up" to tip towards
    for(float& u:up) u/=size;
    const float k[3]={direction[1]*up[2]-direction[2]*up[1],direction[2]*up[0]-direction[0]*up[2],direction[0]*up[1]-direction[1]*up[0]};
    const float c=std::cos(angle),s=std::sin(angle);
    auto turn=[&](float v[3]) {
        const float dot=k[0]*v[0]+k[1]*v[1]+k[2]*v[2];
        const float cross[3]={k[1]*v[2]-k[2]*v[1],k[2]*v[0]-k[0]*v[2],k[0]*v[1]-k[1]*v[0]};
        for(int j=0;j<3;++j) v[j]=v[j]*c+cross[j]*s+k[j]*dot*(1-c);
    };
    for(int r=0;r<3;++r) turn(m.m[r]);
    float at[3]={m.m[3][0]-pivot[0],m.m[3][1]-pivot[1],m.m[3][2]-pivot[2]};
    turn(at);
    for(int j=0;j<3;++j) m.m[3][j]=pivot[j]+at[j]-direction[j]*back;
}

// Turns the kicked bones of one part in a palette: every index in `which`,
// or all of the palette when `which` is null (a weapon's own model, whole).
// What was there is kept for putting back.
struct VehicleRecoilKept { edf6vr::Matrix matrix; unsigned index; };
bool VehicleRecoilApply(edf6vr::Matrix* palette,std::size_t count,const unsigned* which,unsigned whichCount,
                        const float pivot[3],const float direction[3],float angle,float back,
                        VehicleRecoilKept* kept,unsigned capacity,unsigned& keptCount) noexcept {
    __try {
        const unsigned n=which?whichCount:static_cast<unsigned>(count);
        for(unsigned b=0;b<n;++b) {
            const unsigned index=which?which[b]:b;
            if(index>=count || keptCount>=capacity) continue;
            kept[keptCount].matrix=palette[index]; kept[keptCount++].index=index;
            VehicleRecoilTurn(palette[index],pivot,direction,angle,back);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void VehicleRecoilRestore(edf6vr::Matrix* palette,const VehicleRecoilKept* kept,unsigned keptCount) noexcept {
    for(unsigned i=keptCount;i-- > 0;) {
        __try { palette[kept[i].index]=kept[i].matrix; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
}

// First thing in HookModelDraw. The player's vehicle, or one of its weapons'
// own models, while a part is kicking: the bones are turned in the palette
// about to be drawn, the draw runs as it would have (through HookModelDraw
// again, so the cockpit and everything else still see it), and the bones are
// put back.
// What every pass of one rendered frame draws the kick with: the published
// state and the pivots, taken once, on the draw thread, when the frame's depth
// pass (pass 0) begins, and held through its colour pass and the second eye.
//
// Measuring at the update's frameTime (99E29958) was not enough: the update
// runs beside the draw, and a new publish -- every shot of a burst is one --
// could land between a frame's depth pass and its colour pass, which then drew
// the part somewhere the depth did not have it, and grey. The pivots were
// read live from the node array too, which the update keeps moving under a
// driven machine. Both now come from this record. (hardware, 2026-09-26: the
// flashing was still there.)
struct VehicleRecoilFrame {
    VehicleRecoilPublished state{};
    float pivot[kVehicleRecoilParts][3]{}; bool pivotOk[kVehicleRecoilParts]{};
    bool valid=false; int lastPass=-1;
    ULONGLONG takenAt=0;
};
VehicleRecoilFrame g_vehicleRecoilFrame{};   // the draw thread's only
bool VehicleRecoilDraw(void* model,void* renderContext,int pass,void* view) noexcept {
    static thread_local bool inside=false;
    if(inside || !g_vehicleRecoilOn || !model) return false;
    auto& frame=g_vehicleRecoilFrame;
    // A new frame's depth pass: take the latest state. Not on the second eye's,
    // so both eyes draw the same kick.
    // And whatever the pass once the record is 200 ms old, so a scene drawn
    // without a depth pass still gets its kick.
    const ULONGLONG nowTick=GetTickCount64();
    if((pass==0 && (!frame.valid || frame.lastPass!=0) && edf6vr::NativeWorldRenderEye()<1) || nowTick-frame.takenAt>200) {
        AcquireSRWLockShared(&g_vehicleRecoilLock); frame.state=g_vehicleRecoilDraw; ReleaseSRWLockShared(&g_vehicleRecoilLock);
        for(unsigned p=0;p<frame.state.partCount && p<kVehicleRecoilParts;++p)
            frame.pivotOk[p]=VehicleRecoilNodeAt(frame.state.nodes,frame.state.parts[p].moves[0],frame.pivot[p]);
        frame.valid=true; frame.takenAt=nowTick;
        g_vehicleRecoilFrames.fetch_add(1,std::memory_order_relaxed);
    }
    // The pass of every draw, whatever the model: it is the switch from a later
    // pass back to 0 that says a new frame's depth pass has begun. Kept only for
    // our own models, it stuck at 0 after the first snapshot -- taken before the
    // Nix was out -- and the kick never came back (hardware, 2026-09-26).
    frame.lastPass=pass;
    if(!frame.valid) return false;
    const VehicleRecoilPublished& state=frame.state;
    if(!state.model || !state.partCount || GetTickCount64()-state.seen>250) return false;
    const bool vehicle=model==state.model;
    unsigned weaponPart=kVehicleRecoilParts;
    for(unsigned i=0;i<state.weaponModelCount && !vehicle;++i) if(state.weaponModels[i]==model) weaponPart=state.weaponPart[i];
    if(!vehicle && weaponPart>=state.partCount) return false;
    float level[kVehicleRecoilParts]{}; bool any=false;
    for(unsigned p=0;p<state.partCount;++p) {
        if(!vehicle && p!=weaponPart) continue;
        level[p]=state.parts[p].kick.Level(state.frameTime,g_recoilDecaySeconds);
        any=any || level[p]>0.001f;
    }
    if(!any) return false;
    edf6vr::Matrix* palette=nullptr; std::size_t count=0;
    if(!VehicleRecoilPalette(model,pass,palette,count)) return false;
    if(vehicle) {
        g_vehicleRecoilPalette.store(static_cast<unsigned>(count),std::memory_order_relaxed);
        // The palette is the node array as drawn only if it has the same bones.
        if(count!=state.nodeCount) { g_vehicleRecoilMismatch.fetch_add(1,std::memory_order_relaxed); return false; }
    }
    constexpr unsigned kKept=256;
    VehicleRecoilKept kept[kKept]; unsigned keptCount=0;
    bool turned=true;
    for(unsigned p=0;p<state.partCount && turned;++p) {
        if(!(level[p]>0.001f)) continue;
        const auto& part=state.parts[p];
        // The pivot from the node, so the arm and the weapon on it turn about
        // the same point whichever of them is drawn -- as it was when this
        // frame's depth pass began (VehicleRecoilFrame).
        if(p>=kVehicleRecoilParts || !frame.pivotOk[p]) continue;
        const float* pivot=frame.pivot[p];
        const float angle=g_recoilShape.pitchRadians*level[p]*g_vehicleRecoilScale;
        const float back=part.backMetres*level[p]*g_vehicleRecoilScale;
        turned=VehicleRecoilApply(palette,count,vehicle?part.moves:nullptr,part.moveCount,pivot,part.direction,
                                  angle,back,kept,kKept,keptCount);
    }
    if(!turned) { VehicleRecoilRestore(palette,kept,keptCount); return false; }
    // The game's own exceptions from the draw are not ours to swallow; the
    // bones go back either way.
    inside=true;
    __try { HookModelDraw(model,renderContext,pass,view); }
    __finally {
        inside=false;
        VehicleRecoilRestore(palette,kept,keptCount);
    }
    g_vehicleRecoilApplied.fetch_add(1,std::memory_order_relaxed);
    return true;
}
