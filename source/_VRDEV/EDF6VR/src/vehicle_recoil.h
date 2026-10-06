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
    const wchar_t* moves[10];  // moves[0] is the pivot; all of them move together (names a model lacks are skipped)
    float backMetres;          // pushed back this far per unit of kick
};
struct VehicleRecoilMachine {
    const char* classes[2];    // the vehicle's own RTTI class, when that is what tells it apart
    const wchar_t* marker;     // else a bone only this model has
    const char* label;
    VehicleRecoilPart parts[6];
    unsigned count;
    // The cabin's own bone (PlaceVehicleCockpit builds the cabin on it) and
    // the head bones hung on it, which follow the smoothed cabin (SteadyCabin).
    const wchar_t* cabinBone=nullptr;
    const wchar_t* head[2]{};
};
// Bones from research/vehicle/model_anchor_catalog.json. The infantry weapon
// goes back 5 cm per unit; these are 1.5 to 2.5 times that for the size.
//
// The upper-arm shields hang on a bone each paint names its own way:
// weapon_upperArm_l1/r1 on the plain V504, v_504_u_shield_l1/r1 on the blue,
// gold, grey, high-speed, red and yellow ones (the C2's "E.D.F" plate, left
// behind by the arm aim on hardware 2026-10-03), weapon_upperArm_l2 on the
// pink one. Every paint's arm is the shoulder's whole subtree, seven bones.
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
        {{L"weapon_shoulder_l"},0.08f},{{L"weapon_shoulder_r"},0.08f}},4,L"mune",{L"head",L"eye"}},
    {{".?AVVehicle504_begaruta@@",".?AVVehicle612_nix@@"},nullptr,"nix",{
        {{L"kata_l",L"jowan_l",L"kawan_l",L"weapon_hand_l",L"weapon_lowerArm_l",L"weapon_upperArm_l",L"weapon_upperArm_l1",
          L"v_504_u_shield_l1",L"weapon_upperArm_l2"},0.10f},
        {{L"kata_r",L"jowan_r",L"kawan_r",L"weapon_hand_r",L"weapon_lowerArm_r",L"weapon_upperArm_r",L"weapon_upperArm_r1",
          L"v_504_u_shield_r1",L"weapon_upperArm_r2"},0.10f},
        {{L"weapon_shoulder_l"},0.08f},{{L"weapon_shoulder_r"},0.08f}},4,L"body",{L"head"}},
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
    unsigned cabinBone=~0u, head[2]{}, headCount=0;   // node indices (the machine's cabinBone/head)
    struct Part {
        unsigned moves[8]{}; unsigned moveCount=0;
        unsigned machinePart=0;    // its place in the machine's own list
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

// The Nix's arm aim (nix_arm_aim.h) and the draw below share these: which
// weapon rides which part and about which bone (from the update), and the
// turn each part's weapon was given in its tick (for the draw). A part with no
// weapon of its own (the second shoulder launcher) takes its pair's turn.
bool g_nixArmAimOn=true;   // [VR] NixArmAim
struct NixArmShared {
    void* vehicle=nullptr; unsigned char* nodes=nullptr; unsigned nodeCount=0;
    void* weapons[kVehicleRecoilWeapons]{}; unsigned char part[kVehicleRecoilWeapons]{}; unsigned weaponCount=0;
    void* models[kVehicleRecoilWeapons]{};   // each weapon's own model (weapon+0xF30), drawn with its part's turn
    unsigned pivot[kVehicleRecoilParts]{}; bool armed[kVehicleRecoilParts]{};
    signed char pair[kVehicleRecoilParts]{-1,-1,-1,-1,-1,-1};
    ULONGLONG at=0;
};
struct NixArmTurn { float m[3][3]{}; bool valid=false; ULONGLONG at=0; };
SRWLOCK g_nixArmLock=SRWLOCK_INIT;
// The head's share of the walking-bob smoothing (SteadyCabin, plugin.cpp): the
// turn and shift the smoothing gave the cabin, put in the cabin bone's own
// frame (K = bone raw^-1 smoothed bone^-1, row vectors), so the draw can move
// the head bones the same way about the palette's own bone -- whatever moment
// the palette is from. Without it the head, hung on the raw bone, rose and fell
// through the smoothed view in a jump (hardware 2026-10-03: "跳躍時に頭がかなり
// 上下する都合で、頭の中のモデルが見えて…頭部モデルだけ揺れないように").
struct CabinHeadFix { edf6vr::Matrix k{}; bool valid=false; ULONGLONG at=0; };
SRWLOCK g_cabinHeadLock=SRWLOCK_INIT;
CabinHeadFix g_cabinHeadFix{};                      // under g_cabinHeadLock
std::atomic<unsigned long long> g_cabinHeadDrawn{0};
// Row-vector affine 4x4: a then b, and the inverse.
edf6vr::Matrix CabinMul(const edf6vr::Matrix& a,const edf6vr::Matrix& b) noexcept {
    edf6vr::Matrix r{};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) { float s=0; for(int k=0;k<4;++k) s+=a.m[i][k]*b.m[k][j]; r.m[i][j]=s; }
    return r;
}
bool CabinInverse(const edf6vr::Matrix& m,edf6vr::Matrix& out) noexcept {
    const auto& a=m.m;
    const float c00=a[1][1]*a[2][2]-a[1][2]*a[2][1],c01=a[1][2]*a[2][0]-a[1][0]*a[2][2],c02=a[1][0]*a[2][1]-a[1][1]*a[2][0];
    const float det=a[0][0]*c00+a[0][1]*c01+a[0][2]*c02;
    if(!(std::fabs(det)>1e-9f) || !std::isfinite(det)) return false;
    const float d=1.0f/det;
    out=edf6vr::Matrix{};
    out.m[0][0]=c00*d; out.m[0][1]=(a[0][2]*a[2][1]-a[0][1]*a[2][2])*d; out.m[0][2]=(a[0][1]*a[1][2]-a[0][2]*a[1][1])*d;
    out.m[1][0]=c01*d; out.m[1][1]=(a[0][0]*a[2][2]-a[0][2]*a[2][0])*d; out.m[1][2]=(a[0][2]*a[1][0]-a[0][0]*a[1][2])*d;
    out.m[2][0]=c02*d; out.m[2][1]=(a[0][1]*a[2][0]-a[0][0]*a[2][1])*d; out.m[2][2]=(a[0][0]*a[1][1]-a[0][1]*a[1][0])*d;
    for(int j=0;j<3;++j) out.m[3][j]=-(a[3][0]*out.m[0][j]+a[3][1]*out.m[1][j]+a[3][2]*out.m[2][j]);
    out.m[3][3]=1.0f;
    return true;
}
// From the camera update, once the cabin is smoothed: raw and smoothed cabin,
// and the cabin bone as the node array has it now.
void PublishCabinHeadFix(void* vehicle,const edf6vr::Matrix& raw,const edf6vr::Matrix& smoothed) noexcept {
    CabinHeadFix next{};
    VehicleRecoilPublished state{};
    AcquireSRWLockShared(&g_vehicleRecoilLock); state=g_vehicleRecoilDraw; ReleaseSRWLockShared(&g_vehicleRecoilLock);
    edf6vr::Matrix bone{},boneInverse{},rawInverse{};
    bool read=false;
    if(vehicle && state.model==static_cast<unsigned char*>(vehicle)+0xE40 && state.headCount && state.cabinBone<state.nodeCount && state.nodes) {
        __try {
            const auto* m=reinterpret_cast<const float*>(state.nodes+static_cast<std::size_t>(state.cabinBone)*0x110+0xB0);
            for(int i=0;i<4;++i) for(int j=0;j<4;++j) bone.m[i][j]=m[i*4+j];
            read=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { read=false; }
    }
    if(read && CabinInverse(bone,boneInverse) && CabinInverse(raw,rawInverse)) {
        next.k=CabinMul(CabinMul(CabinMul(bone,rawInverse),smoothed),boneInverse);
        next.valid=std::isfinite(next.k.m[3][0]) && std::isfinite(next.k.m[3][1]) && std::isfinite(next.k.m[3][2]);
        next.at=GetTickCount64();
    }
    AcquireSRWLockExclusive(&g_cabinHeadLock); g_cabinHeadFix=next; ReleaseSRWLockExclusive(&g_cabinHeadLock);
}
void ClearCabinHeadFix() noexcept {
    AcquireSRWLockExclusive(&g_cabinHeadLock); g_cabinHeadFix=CabinHeadFix{}; ReleaseSRWLockExclusive(&g_cabinHeadLock);
}
NixArmShared g_nixArmShared{};                      // under g_nixArmLock
NixArmTurn g_nixArmTurns[kVehicleRecoilParts]{};    // under g_nixArmLock
std::atomic<unsigned long long> g_nixArmDrawn{0};
// The draw's pivots (VehicleRecoilDraw): taken from the render palette, or the
// node array's when the palette could not be read; and how far the palette's
// pivot stood from the node array's (millimetres, and its turn in hundredths of
// a degree) -- the shift a turn about the node array's pivot gave the drawn arm.
std::atomic<unsigned long long> g_nixArmPivotPalette{0},g_nixArmPivotLive{0},g_nixArmPivotShiftSum{0},g_nixArmPivotShiftCount{0};
std::atomic<unsigned> g_nixArmPivotShiftMax{0},g_nixArmPivotTurnMax{0};
void NixArmNoteMax(std::atomic<unsigned>& at,unsigned value) noexcept {
    unsigned seen=at.load(std::memory_order_relaxed);
    while(value>seen && !at.compare_exchange_weak(seen,value,std::memory_order_relaxed)) {}
}
void NixArmAimShare(const VehicleRecoilWork& w) noexcept;   // nix_arm_aim.h
// A matrix turned by M (row vectors, v M) about a pivot: its rows turn, and its
// point turns about the pivot. For the arm aim's bones and transforms alike.
void NixArmTurnMatrix(edf6vr::Matrix& m,const float M[3][3],const float pivot[3]) noexcept {
    for(int r=0;r<3;++r) {
        float v[3];
        for(int j=0;j<3;++j) v[j]=m.m[r][0]*M[0][j]+m.m[r][1]*M[1][j]+m.m[r][2]*M[2][j];
        for(int j=0;j<3;++j) m.m[r][j]=v[j];
    }
    const float a[3]={m.m[3][0]-pivot[0],m.m[3][1]-pivot[1],m.m[3][2]-pivot[2]};
    for(int j=0;j<3;++j) m.m[3][j]=pivot[j]+a[0]*M[0][j]+a[1]*M[1][j]+a[2]*M[2][j];
}
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
        part.machinePart=p;
        if(part.moveCount) ++out.partCount;
    }
    unsigned index=0;
    if(w.machine->cabinBone && CabinNodeIndex(registry,nodes,count,w.machine->cabinBone,&index)) {
        out.cabinBone=index;
        for(const auto* name:w.machine->head)
            if(name && CabinNodeIndex(registry,nodes,count,name,&index) && out.headCount<2) out.head[out.headCount++]=index;
    }
    char bones[48]; bones[0]=0;
    for(unsigned p=0,used=0;p<out.partCount && used<sizeof(bones)-8;++p) {
        const int n=std::snprintf(bones+used,sizeof(bones)-used,"%s%u",p?"/":"",out.parts[p].moveCount);
        if(n<0) break;
        used+=static_cast<unsigned>(n);
    }
    Log("VEHICLERECOIL %s: %u of %u parts found on %u bones (bones moved by part %s)",w.machine->label,out.partCount,w.machine->count,count,bones);
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
    if((!g_vehicleRecoilOn && !g_nixArmAimOn) || !vehicle || !g_nodeLookup) return;
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
        if(g_vehicleRecoilOn) out.parts[best].kick.Shot(now,g_recoilDecaySeconds);
        for(int j=0;j<3;++j) out.parts[best].direction[j]=d[j];
        ++w.shots[best];
    }
    for(unsigned k=0;k<out.weaponModelCount;++k)
        out.weaponPart[k]=tick<w.drainUntil[w.modelWeapon[k]]?static_cast<unsigned char>(kVehicleRecoilParts):w.modelPart[k];
    out.seen=tick; out.frameTime=now; out.nodes=nodes;
    AcquireSRWLockExclusive(&g_vehicleRecoilLock); g_vehicleRecoilDraw=out; ReleaseSRWLockExclusive(&g_vehicleRecoilLock);
    NixArmAimShare(w);
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
// The arm aim's turn on one part in a palette, kept for putting back as the
// kick's is (the kick then goes on top, along the turned barrel).
bool NixArmApply(edf6vr::Matrix* palette,std::size_t count,const unsigned* which,unsigned whichCount,
                 const float M[3][3],const float pivot[3],VehicleRecoilKept* kept,unsigned capacity,unsigned& keptCount) noexcept {
    __try {
        const unsigned n=which?whichCount:static_cast<unsigned>(count);
        for(unsigned b=0;b<n;++b) {
            const unsigned index=which?which[b]:b;
            if(index>=count || keptCount>=capacity) continue;
            kept[keptCount].matrix=palette[index]; kept[keptCount++].index=index;
            NixArmTurnMatrix(palette[index],M,pivot);
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
    NixArmTurn turns[kVehicleRecoilParts]{};   // the arm aim's, taken with the state
    NixArmShared nix{};                         // and which weapon model rides which part for it
    CabinHeadFix head{};                        // the head's share of the bob smoothing
    float pivot[kVehicleRecoilParts][3]{}; bool pivotOk[kVehicleRecoilParts]{};
    bool valid=false; int lastPass=-1;
    ULONGLONG takenAt=0;
};
VehicleRecoilFrame g_vehicleRecoilFrame{};   // the draw thread's only
// A part's pivot as a render palette has it (its first bone's point), and how
// far that bone is turned from the node array's (the larger axis angle, deg).
bool VehicleRecoilPalettePivot(const edf6vr::Matrix* palette,std::size_t count,const unsigned char* nodes,unsigned index,
                               float at[3],float& turnDegrees) noexcept {
    __try {
        if(!palette || index>=count) return false;
        for(int j=0;j<3;++j) at[j]=palette[index].m[3][j];
        if(!std::isfinite(at[0]) || !std::isfinite(at[1]) || !std::isfinite(at[2])) return false;
        turnDegrees=0;
        const auto* live=reinterpret_cast<const float*>(nodes+static_cast<std::size_t>(index)*0x110+0xB0);
        for(int r=0;r<3;++r) {
            const float* a=palette[index].m[r]; const float* b=live+4*r;
            const float la=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]),lb=std::sqrt(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);
            if(!(la>1e-6f) || !(lb>1e-6f)) continue;
            const float c=std::clamp((a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/(la*lb),-1.0f,1.0f);
            turnDegrees=(std::max)(turnDegrees,std::acos(c)*57.2957795f);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool VehicleRecoilDraw(void* model,void* renderContext,int pass,void* view) noexcept {
    static thread_local bool inside=false;
    if(inside || (!g_vehicleRecoilOn && !g_nixArmAimOn) || !model) return false;
    auto& frame=g_vehicleRecoilFrame;
    // A new frame's depth pass: take the latest state. Not on the second eye's,
    // so both eyes draw the same kick.
    // And whatever the pass once the record is 200 ms old, so a scene drawn
    // without a depth pass still gets its kick.
    const ULONGLONG nowTick=GetTickCount64();
    if((pass==0 && (!frame.valid || frame.lastPass!=0) && edf6vr::NativeWorldRenderEye()<1) || nowTick-frame.takenAt>200) {
        AcquireSRWLockShared(&g_vehicleRecoilLock); frame.state=g_vehicleRecoilDraw; ReleaseSRWLockShared(&g_vehicleRecoilLock);
        AcquireSRWLockShared(&g_nixArmLock); std::memcpy(frame.turns,g_nixArmTurns,sizeof(frame.turns)); frame.nix=g_nixArmShared; ReleaseSRWLockShared(&g_nixArmLock);
        AcquireSRWLockShared(&g_cabinHeadLock); frame.head=g_cabinHeadFix; ReleaseSRWLockShared(&g_cabinHeadLock);
        // Only a state the draw below would use (250 ms): an old one names the
        // node array of a vehicle that may be gone. Read anyway, at the next
        // mission's start, it faulted once inside the __try (2026-10-05 23:05:28,
        // EDF6VR.dll+5936A), which the MultiSlot exception log records.
        const bool fresh=frame.state.model && frame.state.partCount && nowTick-frame.state.seen<=250;
        for(unsigned p=0;p<frame.state.partCount && p<kVehicleRecoilParts;++p)
            frame.pivotOk[p]=fresh && VehicleRecoilNodeAt(frame.state.nodes,frame.state.parts[p].moves[0],frame.pivot[p]);
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
    unsigned weaponPart=kVehicleRecoilParts,aimPart=kVehicleRecoilParts;
    for(unsigned i=0;i<state.weaponModelCount && !vehicle;++i) if(state.weaponModels[i]==model) weaponPart=state.weaponPart[i];
    // The arm aim's own weapon -> part (by where the weapon is mounted), so a
    // gun is drawn with the turn its shots and aim line are given.
    for(unsigned i=0;i<frame.nix.weaponCount && !vehicle;++i) if(frame.nix.models[i]==model) aimPart=frame.nix.part[i];
    if(!vehicle && weaponPart>=state.partCount && aimPart>=state.partCount) return false;
    float level[kVehicleRecoilParts]{}; bool aimed[kVehicleRecoilParts]{}; bool any=false;
    for(unsigned p=0;p<state.partCount;++p) {
        if(!vehicle && p!=weaponPart && p!=aimPart) continue;
        level[p]=g_vehicleRecoilOn && (vehicle || p==weaponPart)?state.parts[p].kick.Level(state.frameTime,g_recoilDecaySeconds):0.0f;
        aimed[p]=g_nixArmAimOn && p<kVehicleRecoilParts && (vehicle || p==aimPart) && frame.turns[p].valid && nowTick-frame.turns[p].at<250;
        any=any || level[p]>0.001f || aimed[p];
    }
    const bool headFix=vehicle && frame.head.valid && nowTick-frame.head.at<250 && state.headCount && state.cabinBone<state.nodeCount;
    any=any || headFix;
    if(!any) return false;
    edf6vr::Matrix* palette=nullptr; std::size_t count=0;
    if(!VehicleRecoilPalette(model,pass,palette,count)) return false;
    if(vehicle) {
        g_vehicleRecoilPalette.store(static_cast<unsigned>(count),std::memory_order_relaxed);
        // The palette is the node array as drawn only if it has the same bones.
        if(count!=state.nodeCount) { g_vehicleRecoilMismatch.fetch_add(1,std::memory_order_relaxed); return false; }
    }
    // The pivots as this draw has them: each part's first bone in the
    // vehicle's render palette for this pass (the vehicle's own palette, or,
    // for one of its weapons, the vehicle's for the same pass). The node array
    // the record read runs ahead of the palette while the machine moves (the
    // soldier's HOLD paletteVsNode measured up to a metre), and a turn about a
    // point the drawn arm has left shifts the arm by (pivot error)(I-M): the
    // Nix's arms shook while it moved (hardware, 2026-10-03). The node array's
    // pivot only when the palette cannot be read or stands over 3 m off. All
    // read before any part is turned.
    edf6vr::Matrix* drawnPalette=vehicle?palette:nullptr; std::size_t drawnCount=vehicle?count:0;
    if(!vehicle && (!VehicleRecoilPalette(state.model,pass,drawnPalette,drawnCount) || drawnCount!=state.nodeCount)) drawnPalette=nullptr;
    float pivots[kVehicleRecoilParts][3]{}; bool pivotOk[kVehicleRecoilParts]{};
    for(unsigned p=0;p<state.partCount && p<kVehicleRecoilParts;++p) {
        if((!(level[p]>0.001f) && !aimed[p]) || !frame.pivotOk[p]) continue;
        float at[3]; float turnDegrees=0;
        const float* node=frame.pivot[p];
        if(drawnPalette && VehicleRecoilPalettePivot(drawnPalette,drawnCount,state.nodes,state.parts[p].moves[0],at,turnDegrees)) {
            const float dx=at[0]-node[0],dy=at[1]-node[1],dz=at[2]-node[2];
            const float shift=std::sqrt(dx*dx+dy*dy+dz*dz);
            if(shift<3.0f) {
                for(int j=0;j<3;++j) pivots[p][j]=at[j];
                pivotOk[p]=true;
                g_nixArmPivotPalette.fetch_add(1,std::memory_order_relaxed);
                const unsigned mm=static_cast<unsigned>(shift*1000.0f);
                g_nixArmPivotShiftSum.fetch_add(mm,std::memory_order_relaxed); g_nixArmPivotShiftCount.fetch_add(1,std::memory_order_relaxed);
                NixArmNoteMax(g_nixArmPivotShiftMax,mm);
                NixArmNoteMax(g_nixArmPivotTurnMax,static_cast<unsigned>(turnDegrees*100.0f));
                continue;
            }
        }
        for(int j=0;j<3;++j) pivots[p][j]=node[j];
        pivotOk[p]=true;
        g_nixArmPivotLive.fetch_add(1,std::memory_order_relaxed);
    }
    constexpr unsigned kKept=256;
    VehicleRecoilKept kept[kKept]; unsigned keptCount=0;
    bool turned=true;
    for(unsigned p=0;p<state.partCount && turned;++p) {
        if(!(level[p]>0.001f) && !aimed[p]) continue;
        const auto& part=state.parts[p];
        // The same pivot whichever of the arm and the weapon on it is drawn.
        if(p>=kVehicleRecoilParts || !pivotOk[p]) continue;
        const float* pivot=pivots[p];
        // The arm aim's turn first, the kick on top of it.
        if(aimed[p]) {
            turned=NixArmApply(palette,count,vehicle?part.moves:nullptr,part.moveCount,frame.turns[p].m,pivot,kept,kKept,keptCount);
            if(turned) g_nixArmDrawn.fetch_add(1,std::memory_order_relaxed);
        }
        if(!turned || !(level[p]>0.001f)) continue;
        const float angle=g_recoilShape.pitchRadians*level[p]*g_vehicleRecoilScale;
        const float back=part.backMetres*level[p]*g_vehicleRecoilScale;
        turned=VehicleRecoilApply(palette,count,vehicle?part.moves:nullptr,part.moveCount,pivot,part.direction,
                                  angle,back,kept,kKept,keptCount);
    }
    // The head bones by the cabin's smoothing, about the palette's own cabin
    // bone: head' = head bone^-1 K bone.
    if(turned && headFix) {
        __try {
            edf6vr::Matrix bone=palette[state.cabinBone],boneInverse{};
            if(CabinInverse(bone,boneInverse)) {
                const edf6vr::Matrix move=CabinMul(CabinMul(boneInverse,frame.head.k),bone);
                for(unsigned h=0;h<state.headCount && keptCount<kKept;++h) {
                    const unsigned index=state.head[h];
                    if(index>=count) continue;
                    kept[keptCount].matrix=palette[index]; kept[keptCount++].index=index;
                    palette[index]=CabinMul(palette[index],move);
                }
                g_cabinHeadDrawn.fetch_add(1,std::memory_order_relaxed);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { turned=false; }
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
