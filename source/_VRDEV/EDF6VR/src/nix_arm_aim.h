// Included by plugin.cpp inside its private namespace, after vehicle_recoil.h.
//
// The Nix's arms and shoulder weapons aimed where the right controller points
// (the user, 2026-10-03: "右コントローラを向けている方向にエイム先を向ける…腕の
// 可動範囲は、上半身の向いている正面の上下左右２０度位"), on top of the hand aim
// that turns the body (HandAimStick). Pointed at something off to the right,
// the arms reach it first and stay on it while the body comes round after them.
//
// What turns: in the weapon's tick (HookDualWeaponTick: Weapon_VehicleShoot
// 6B3850), before the game fires from them, the weapon's transforms -- the
// matrix at weapon+0x1D0 -> +0x50, whose barrel (rows through the local fire
// direction at +0x350) and muzzle (+0x80) the shot and the aim line leave
// from (6B3660), and the one at +0x90 -- about the part's pivot; in the draw,
// the arm's bones and the weapon's own model by the same turn about the same
// pivot (vehicle_recoil.h: NixArmApply). The pivot is the part's first bone:
// the shoulder (kata_l/r) for an arm, the mount for a shoulder weapon. The
// game's skeleton is never changed.
//
// Where they point: along the controller's pointing, put in the world as the
// view is (VehicleReferenceToWorld), each barrel parallel to it (the arms do
// not converge); each barrel's turn is kept within NixArmAimDegrees of the
// game's own (AimCone), up and down and to either side.
//
// The game writes the transforms afresh before a tick (as for the soldier's
// weapons); should it not, what we wrote is told by an exact match and the
// turn is redone from the game's last one, never on top of our own.
#pragma once

// g_nixArmAimOn ([VR] NixArmAim) is in vehicle_recoil.h, whose draw asks it.
float g_nixArmAimDegrees=20.f;      // [VR] NixArmAimDegrees (the user's, hardware 2026-10-03)
// Robot-like (the user: "ロボットっぽく、少し遅延と追従速度が早すぎないように"):
// the ray followed with a lag, and each arm turned no faster than a set speed,
// out to the aim and back to the game's own when the aim goes.
// Heavier after the first hardware run ("もっと重鈍でいい"): a longer lag, a lower
// top speed, and a turn that gathers speed (and sheds it before it arrives)
// rather than starting and stopping at once.
// The user's own tuning, right for the C2 and the Destroy Cannon (hardware
// 2026-10-03: "今の数値が、C2やデストロイキャノンには丁度いい").
float g_nixArmAimSeconds=0.3f;      // [VR] NixArmAimLagSeconds
float g_nixArmAimSpeed=50.f;        // [VR] NixArmAimSpeed, degrees a second
float g_nixArmAimAccel=50.f;        // [VR] NixArmAimAcceleration, degrees a second a second
// Only the controller's pointing is read, never its roll (the user: "ロールとかは
// 拾わなくていい"): its forward, and the arms turn the shortest way onto it.

// The update thread's aim point, from the camera update (NixArmAimTarget).
// The arms keep parallel, along the controller's pointing, as the game's own
// do (hardware 2026-10-03: aimed at one point 100 m out they crossed, "クロス
// すると攻撃に使えない…従来通り両手とも前を向くように").
struct NixArmTarget { float dir[3]{}; bool valid=false; ULONGLONG at=0; };
NixArmTarget g_nixArmTarget{};   // under g_nixArmLock
struct NixArmSmooth { float dir[3]{}; bool have=false; double at=0; };
NixArmSmooth g_nixArmSmooth{};   // update thread only
std::atomic<float> g_nixArmLastAngle[kVehicleRecoilParts]{};

// MEASURING, for arms set per machine. A machine whose body turns faster than
// the arms leaves them behind (the Red ones and the Revolver Custom: "腰の回転の
// 方が早く、手の追従が置いていかれて結果エイムしづらい"), and the arms are to be set
// per machine from the start, never sped up on the way ("腕の速度が不意に加速
// したりは困る。機体毎に最初から設定したい"). Two things are logged for that:
//  - how fast each machine's body really turns: the heading of each barrel as
//    the game makes it (before our turn), tick to tick -- the fastest seen, and
//    the fastest held for a fifth of a second (NIXBODY);
//  - where the machine's call-in settings sit in the vehicle (NIXSETUP): the
//    vehicle and what it points to are searched for its vehicle_setup entries
//    (kNixVariants, from the call-in weapons' data), the fourth of which --
//    [[30,.05,.1]] on the C2, [[45,.01,.02]] on the Destroy Cannon, [[70,.1,.1]]
//    on the Revolver Custom, [[60..90,.1,.1]] on the Red ones -- looks like the
//    body's turn.
// Nothing here changes how the arms move.
struct NixArmBodyWatch { float heading=0; double at=0; bool have=false; };
NixArmBodyWatch g_nixArmBodyWatch[kVehicleRecoilParts]{};   // weapon thread, under g_nixArmMemoLock
void* g_nixArmBodyVehicle=nullptr;                           // likewise
float g_nixArmBodySustained=0; double g_nixArmBodySustainedAt=0;   // likewise
std::atomic<unsigned> g_nixArmBodyRateMax{0},g_nixArmBodySustainedMax{0};        // the report's window, deg/s
std::atomic<unsigned> g_nixArmBodySessionMax{0},g_nixArmBodySessionSustained{0}; // since boarding this machine
std::atomic<unsigned long long> g_nixArmBodySamples{0};
void NixArmBodyReport(const char* why,void* vehicle) noexcept {
    Log("NIXBODY %s vehicle=%p body turn since boarding: fastest %u deg/s, fastest held 0.2 s %u deg/s (samples %llu)",
        why,vehicle,g_nixArmBodySessionMax.load(),g_nixArmBodySessionSustained.load(),g_nixArmBodySamples.load());
}
// A barrel as the game made it, at this tick: its heading's turn since the
// part's last new barrel.
void NixArmNoteBody(void* vehicle,unsigned part,const edf6vr::Vec3& barrel,double clock) noexcept {
    if(vehicle!=g_nixArmBodyVehicle) {
        if(g_nixArmBodyVehicle) NixArmBodyReport("left",g_nixArmBodyVehicle);
        for(auto& w:g_nixArmBodyWatch) w=NixArmBodyWatch{};
        g_nixArmBodyVehicle=vehicle; g_nixArmBodySustained=0; g_nixArmBodySustainedAt=clock;
        g_nixArmBodySessionMax.store(0); g_nixArmBodySessionSustained.store(0); g_nixArmBodySamples.store(0);
    }
    if(part>=kVehicleRecoilParts || !(std::fabs(barrel.x)+std::fabs(barrel.z)>1e-3f)) return;
    const float heading=std::atan2(barrel.x,barrel.z);
    auto& w=g_nixArmBodyWatch[part];
    if(w.have && w.heading==heading) return;   // not written afresh: no sample
    // The next new one is measured over the whole gap; over 0.25 s apart, or
    // past 360 deg/s (a respawn, a knock-down), it only starts afresh.
    if(w.have && clock-w.at>1e-4 && clock-w.at<0.25) {
        float turn=heading-w.heading;
        while(turn>3.14159265f) turn-=6.28318531f;
        while(turn<-3.14159265f) turn+=6.28318531f;
        const float seen=std::fabs(turn)*57.29578f/static_cast<float>(clock-w.at);
        if(seen<360.0f) {
            const float k=1.0f-std::exp(-static_cast<float>(clock-g_nixArmBodySustainedAt)/0.2f);
            g_nixArmBodySustained+=(seen-g_nixArmBodySustained)*k; g_nixArmBodySustainedAt=clock;
            NixArmNoteMax(g_nixArmBodyRateMax,static_cast<unsigned>(seen));
            NixArmNoteMax(g_nixArmBodySustainedMax,static_cast<unsigned>(g_nixArmBodySustained));
            NixArmNoteMax(g_nixArmBodySessionMax,static_cast<unsigned>(seen));
            NixArmNoteMax(g_nixArmBodySessionSustained,static_cast<unsigned>(g_nixArmBodySustained));
            g_nixArmBodySamples.fetch_add(1,std::memory_order_relaxed);
        }
    }
    w.heading=heading; w.at=clock; w.have=true;
}

// The call-ins of the machines the arm aim serves -- the Nix (Vehicle504_begaruta)
// and the Eiren (Vehicle612_nix) -- from their weapons' data (sgott data/6/weapon,
// EWEAPON356..378, the DLC and MPACK ones; Ammo_CustomParameter[4]): the
// machine's durability against its own ([3][0][0], times game_object_durability,
// 1200 in every V504 and V612 paint), its body's turn ([3][3][0][0]) and how many
// guns it mounts. Names are the English ones from the same data.
// The turn is the body's turn speed: on hardware 2026-10-03 (NIXARM, the fastest
// held 0.2 s at a full push) the C2 (30) turned about 21 deg/s and the Revolver
// Custom (70) about 45 -- in step with the numbers (x0.65) -- and the C2's arms,
// as tuned, "C2の横旋回速度と釣り合ってるかな位".
struct NixVariant { const char* name; bool eiren; float durability; float turn; unsigned guns; };
const NixVariant kNixVariants[]={
    {"Powered Exoskeleton Nix Gold Coat",false,1.f,20.f,4},   // DLC_VEHICLE_BEGARUTA01 v504_begaruta_gold.sgo
    {"Powered Exoskeleton Nix Fantasia Shield",false,1.f,20.f,2},   // DLC_VEHICLE_BEGARUTA02 v504_begaruta_pink.sgo
    {"Powered Exoskeleton Nix Metal Coat",false,1.f,20.f,4},   // DLC_VEHICLE_V504_BEGARUTA_GOLD v504_begaruta_gold.sgo
    {"Powered Exoskeleton Nix A",false,0.800000012f,20.f,2},   // EWEAPON356 v504_begaruta_blue.sgo
    {"Nix Grenadier",false,1.14999998f,20.f,2},   // EWEAPON357 v504_begaruta_blue.sgo
    {"Powered Exoskeleton Nix A2",false,1.20000005f,20.f,2},   // EWEAPON358 v504_begaruta_blue.sgo
    {"Nix Red Coat",false,1.29999995f,60.f,4},   // EWEAPON359 v504_begaruta_red.sgo
    {"Powered Exoskeleton Nix B",false,1.54999995f,20.f,4},   // EWEAPON360 v504_begaruta_blue.sgo
    {"Powered Exoskeleton Eiren",true,1.70000005f,15.f,2},   // EWEAPON361 v612_nix.sgo
    {"Nix Battle Cannon",false,1.85000002f,10.f,4},   // EWEAPON362 v504_begaruta.sgo
    {"Powered Exoskeleton Nix C1",false,3.29999995f,30.f,3},   // EWEAPON363 v504_begaruta_blue.sgo
    {"Nix Missile Gun",false,6.19999981f,45.f,4},   // EWEAPON364 v504_begaruta.sgo
    {"Powered Exoskeleton Nix C2",false,8.60000038f,30.f,3},   // EWEAPON365 v504_begaruta_blue.sgo
    {"Nix Grenadier M2",false,10.3000002f,30.f,4},   // EWEAPON366 v504_begaruta_blue.sgo
    {"Powered Exoskeleton Eiren II",true,12.f,20.f,3},   // EWEAPON367 v612_nix.sgo
    {"Nix Red Shadow",false,11.f,70.f,4},   // EWEAPON368 v504_begaruta_red.sgo
    {"Nix Buster Cannon",false,16.7999992f,25.f,4},   // EWEAPON369 v504_begaruta.sgo
    {"Powered Exoskeleton Nix C3",false,23.f,40.f,4},   // EWEAPON370 v504_begaruta_blue.sgo
    {"Nix Revolver Custom",false,30.2999992f,70.f,4},   // EWEAPON371 v504_begaruta.sgo
    {"Nix Red Armor",false,32.f,70.f,4},   // EWEAPON372 v504_begaruta_red.sgo
    {"Powered Exoskeleton Eiren III",true,30.f,25.f,3},   // EWEAPON373 v612_nix.sgo
    {"Nix Destroy Cannon",false,63.f,45.f,4},   // EWEAPON374 v504_begaruta.sgo
    {"Nix Red Guard",false,64.f,90.f,4},   // EWEAPON375 v504_begaruta_red.sgo
    {"Nix Grenadier EZ",false,83.f,60.f,4},   // EWEAPON376 v504_begaruta_blue.sgo
    {"Powered Exoskeleton Nix ZC",false,87.f,60.f,4},   // EWEAPON377 v504_begaruta_blue.sgo
    {"Powered Exoskeleton Eiren IV",true,60.f,45.f,4},   // EWEAPON378 v612_nix.sgo
    {"Saber Eiren",true,65.f,240.f,4},   // MPACK_A_WEAPON076 v612_nix_black.sgo
    {"Eiren Assault",true,75.f,90.f,4},   // MPACK_B_WEAPON109 v612_nix_red.sgo
};
constexpr unsigned kNixVariantCount=sizeof(kNixVariants)/sizeof(kNixVariants[0]);
// The arms are set per machine when it is boarded, and kept so (the user: "腕の
// 速度が不意に加速したりは困る。機体毎に最初から設定したい"): the C2's tuning,
// scaled in time by the machine's body turn against the C2's (turn / 30, never
// below 1, so slower machines keep it): the lag shorter by that, the top speed
// higher by it, the acceleration by its square -- the same motion relative to
// its own body as the C2's arms have to theirs. Hardware 2026-10-03: "いい感じ".
constexpr float kNixArmReferenceTurn=30.0f;   // the C2's
std::atomic<float> g_nixArmMachineScale{1.0f};
std::atomic<int> g_nixArmMachine{-1};          // kNixVariants index, -1 not known

// Which machine was boarded, by its durability: the vehicle keeps
// game_object_durability at +0x2EC (1200) and its full durability at +0x2F4
// (1200 x the call-in's: 36360 for the Revolver Custom, 76800 for the Red Guard,
// 27600 for the C3, found by searching on hardware 2026-10-03). The ratio names
// the call-in among those of the vehicle's class; the gun count only breaks a
// tie. Not found (another ratio -- online, if it scales them -- or another
// machine): the arms keep the settings as they are. Once per machine boarded
// (again only if the gun count changes while it is boarded), update thread.
void NixMachineIdentify(void* vehicle,unsigned guns) noexcept {
    static void* seen=nullptr; static unsigned seenGuns=0;
    if(!vehicle || (vehicle==seen && guns==seenGuns)) return;
    seen=vehicle; seenGuns=guns;
    g_nixArmMachine.store(-1); g_nixArmMachineScale.store(1.0f);
    float field[6]{};   // +0x2E8 .. +0x2FC
    bool read=false;
    __try {
        auto* v=static_cast<unsigned char*>(vehicle);
        if(edf6vr::Readable(v+0x2E8,sizeof(field))) { std::memcpy(field,v+0x2E8,sizeof(field)); read=true; }
    } __except(EXCEPTION_EXECUTE_HANDLER) { read=false; }
    const char* type=edf6vr::TypeName(g_image,vehicle);
    const bool eiren=type && !std::strcmp(type,".?AVVehicle612_nix@@");
    const bool nix=type && !std::strcmp(type,".?AVVehicle504_begaruta@@");
    const float base=field[1],full=field[3];
    const float ratio=read && std::isfinite(base) && std::isfinite(full) && base>1.0f?full/base:0.0f;
    int pick=-1; unsigned picks=0; bool alike=true;
    for(int pass=0;pass<2 && (nix || eiren);++pass) {   // the gun count's own first, then any
        for(unsigned k=0;k<kNixVariantCount;++k) {
            const NixVariant& n=kNixVariants[k];
            if(n.eiren!=eiren || !(std::fabs(ratio-n.durability)<=n.durability*0.005f)) continue;
            if(pass==0 && n.guns!=guns) continue;
            if(pick>=0 && kNixVariants[pick].turn!=n.turn) alike=false;
            if(pick<0) pick=static_cast<int>(k);
            ++picks;
        }
        if(picks) break;
    }
    float scale=1.0f;
    const bool known=pick>=0 && alike;
    if(known) {
        scale=std::clamp(kNixVariants[pick].turn/kNixArmReferenceTurn,1.0f,10.0f);
        g_nixArmMachine.store(pick); g_nixArmMachineScale.store(scale);
    }
    Log("NIXMACHINE vehicle=%p %s guns=%u durability +0x2E8..+0x2FC=%.0f/%.0f/%.0f/%.0f/%.0f/%.0f (x%.3f) -> %s%s (body turn %.0f, arms x%.2f: lag %.2f s, top %.0f deg/s, accel %.0f deg/s/s)%s",
        vehicle,type?type:"?",guns,field[0],field[1],field[2],field[3],field[4],field[5],ratio,known?kNixVariants[pick].name:"not known",
        pick>=0 && !alike?" (call-ins that turn differently share it)":"",known?kNixVariants[pick].turn:0.0f,scale,g_nixArmAimSeconds/scale,
        g_nixArmAimSpeed*scale,g_nixArmAimAccel*scale*scale,known?"":" -- the arms keep the settings as they are");
}
std::atomic<unsigned long long> g_nixArmTicks{0},g_nixArmTurned{0},g_nixArmClamped{0},g_nixArmRedone{0},g_nixArmReleased{0};

// Every camera update in a vehicle: the controller's ray, when the seat is a
// Nix's cabin and the hand is tracked; nothing otherwise.
void NixArmAimTarget(bool nixCabin,const edf6vr::Matrix& seatCamera,const edf6vr::Quat& reference,
                     const edf6vr::Vec3& positionReference,bool positionValid) noexcept {
    NixArmTarget next{};
    float position[3]{},rotation[4]{};
    if(g_nixArmAimOn && nixCabin && edf6vr::g_openxr.HandPose(1,position,rotation)) {
        const edf6vr::Quat q{rotation[0],rotation[1],rotation[2],rotation[3]};
        const edf6vr::Vec3 delta=positionValid?edf6vr::Vec3{position[0]-positionReference.x,position[1]-positionReference.y,
                                                             position[2]-positionReference.z}:edf6vr::Vec3{};
        edf6vr::Vec3 point{},direction{};
        if(edf6vr::NormalizedQuat(q) && edf6vr::VehicleReferenceToWorld(seatCamera,reference,delta,
               edf6vr::QuatRotate(q,edf6vr::Vec3{0,0,-1}),point,direction)) {
            auto& s=g_nixArmSmooth;
            const double now=Now();
            const float d[3]={direction.x,direction.y,direction.z};
            if(!s.have || now-s.at>0.25) { for(int j=0;j<3;++j) s.dir[j]=d[j]; }
            else {
                const float lag=g_nixArmAimSeconds/g_nixArmMachineScale.load(std::memory_order_relaxed);
                const float k=lag>0.001f?1.0f-std::exp(-static_cast<float>(now-s.at)/lag):1.0f;
                float n=0;
                for(int j=0;j<3;++j) { s.dir[j]+=(d[j]-s.dir[j])*k; n+=s.dir[j]*s.dir[j]; }
                n=std::sqrt(n);
                if(n>1e-4f) for(float& v:s.dir) v/=n; else for(int j=0;j<3;++j) s.dir[j]=d[j];
            }
            s.have=true; s.at=now;
            for(int j=0;j<3;++j) next.dir[j]=s.dir[j];
            next.valid=true; next.at=GetTickCount64();
        }
    }
    if(!next.valid) g_nixArmSmooth.have=false;
    AcquireSRWLockExclusive(&g_nixArmLock); g_nixArmTarget=next; ReleaseSRWLockExclusive(&g_nixArmLock);
}

// From VehicleRecoilUpdate: which of the vehicle's weapons ride which part.
void NixArmAimShare(const VehicleRecoilWork& w) noexcept {
    NixArmShared next{};
    if(g_nixArmAimOn && w.machine && !std::strcmp(w.machine->label,"nix") && w.weaponCount) {
        const auto& out=w.published;
        next.vehicle=w.vehicle; next.nodes=w.nodes; next.nodeCount=w.nodeCount;
        for(unsigned p=0;p<out.partCount && p<kVehicleRecoilParts;++p) {
            next.pivot[p]=out.parts[p].moves[0];
            // The shoulder launchers (the Nix's parts 2 and 3) pair up: one
            // weapon fires from them, and the other follows its turn.
            for(unsigned q=0;q<out.partCount && q<kVehicleRecoilParts;++q)
                if(q!=p && out.parts[p].machinePart>=2 && out.parts[q].machinePart>=2) next.pair[p]=static_cast<signed char>(q);
        }
        for(unsigned i=0;i<w.weaponCount && next.weaponCount<kVehicleRecoilWeapons;++i) {
            // Only the machine's own guns: the soldier riding it keeps his
            // weapons too, and the same scan finds them (hardware 2026-10-03:
            // a grenade and a rifle, put on the left arm, turned it their way).
            void* owner=nullptr;
            __try { owner=*reinterpret_cast<void**>(static_cast<unsigned char*>(w.weapons[i])+0x120); }
            __except(EXCEPTION_EXECUTE_HANDLER) { owner=nullptr; }
            const char* type=edf6vr::TypeName(g_image,w.weapons[i]);
            if(owner!=w.vehicle || !type || std::strncmp(type,".?AVWeapon_Vehicle",18)) continue;
            // The part by where the weapon is mounted (its own matrix at +0x150,
            // translation +0x180), not its muzzle: the C2's long cannon reaches
            // past the shoulder, and was drawn with one part's turn and fired
            // with another's (its line stood off the barrel, hardware 2026-10-03).
            float at[3]{};
            bool mounted=false;
            __try {
                const auto* m=reinterpret_cast<const float*>(static_cast<unsigned char*>(w.weapons[i])+0x180);
                for(int j=0;j<3;++j) at[j]=m[j];
                mounted=std::isfinite(at[0]) && std::isfinite(at[1]) && std::isfinite(at[2]);
            } __except(EXCEPTION_EXECUTE_HANDLER) { mounted=false; }
            if(!mounted) { float d[3]{}; if(!VehicleRecoilMuzzle(w.weapons[i],at,d)) continue; }
            const unsigned part=VehicleRecoilNearestPart(out,w.nodes,at);
            if(part>=out.partCount) continue;
            next.models[next.weaponCount]=static_cast<unsigned char*>(w.weapons[i])+edf6vr::kWeaponModelOffset;
            next.weapons[next.weaponCount]=w.weapons[i];
            next.part[next.weaponCount++]=static_cast<unsigned char>(part);
            next.armed[part]=true;
        }
        if(next.weaponCount) NixMachineIdentify(w.vehicle,next.weaponCount);
    }
    next.at=GetTickCount64();
    AcquireSRWLockExclusive(&g_nixArmLock); g_nixArmShared=next; ReleaseSRWLockExclusive(&g_nixArmLock);
}

// Where each part's barrel points now (world), moved toward the wanted way at
// no more than g_nixArmAimSpeed. Weapon thread, under g_nixArmMemoLock.
struct NixArmFollow { float dir[3]{}; float speed=0; double at=0; bool have=false; };
NixArmFollow g_nixArmFollow[kVehicleRecoilParts]{};
// The weapon thread's record of what it wrote into each weapon's transforms.
struct NixArmMemo {
    void* weapon=nullptr; unsigned char* transform=nullptr; edf6vr::Matrix native[2]{},written[2]{}; bool have=false;
    float turn[3][3]{}; ULONGLONG turnAt=0; bool turnValid=false;   // the tick's last, for the aim line
};
NixArmMemo g_nixArmMemo[kVehicleRecoilWeapons]{};
SRWLOCK g_nixArmMemoLock=SRWLOCK_INIT;
unsigned g_nixArmMemoNext=0;
NixArmMemo& NixArmMemoFor(void* weapon,unsigned char* transform) noexcept {
    for(auto& m:g_nixArmMemo) if(m.weapon==weapon && m.transform==transform) return m;
    auto& m=g_nixArmMemo[g_nixArmMemoNext++%kVehicleRecoilWeapons];
    m=NixArmMemo{}; m.weapon=weapon; m.transform=transform;
    return m;
}
void NixArmPublishTurn(const NixArmShared& s,unsigned part,const float M[3][3],bool valid) noexcept {
    const ULONGLONG now=GetTickCount64();
    AcquireSRWLockExclusive(&g_nixArmLock);
    auto put=[&](unsigned p) {
        if(p>=kVehicleRecoilParts) return;
        auto& t=g_nixArmTurns[p];
        std::memcpy(t.m,M,sizeof(t.m)); t.valid=valid; t.at=now;
    };
    put(part);
    if(part<kVehicleRecoilParts && s.pair[part]>=0 && !s.armed[s.pair[part]]) put(static_cast<unsigned>(s.pair[part]));
    ReleaseSRWLockExclusive(&g_nixArmLock);
}
// Before one of the Nix's weapons ticks: its transforms turned toward the
// aim point. False: not ours, the caller runs the tick as it was.
bool NixArmWeaponTick(void* weapon,void* context,DualTick original) noexcept {
    if(!g_nixArmAimOn || !weapon) return false;
    NixArmShared s{}; NixArmTarget target{};
    AcquireSRWLockShared(&g_nixArmLock); s=g_nixArmShared; target=g_nixArmTarget; ReleaseSRWLockShared(&g_nixArmLock);
    const ULONGLONG now=GetTickCount64();
    int index=-1;
    for(unsigned i=0;i<s.weaponCount;++i) if(s.weapons[i]==weapon) index=static_cast<int>(i);
    if(index<0 || now-s.at>250) return false;
    g_nixArmTicks.fetch_add(1,std::memory_order_relaxed);
    const unsigned part=s.part[index];
    static const float kIdentity[3][3]={{1,0,0},{0,1,0},{0,0,1}};
    auto* w=static_cast<unsigned char*>(weapon);
    unsigned char* t=nullptr;
    edf6vr::Matrix live[2]{};
    float local[3]{};
    bool read=false;
    __try {
        if(edf6vr::Readable(w,0x360)) t=*reinterpret_cast<unsigned char**>(w+0x1D0);
        if(t && edf6vr::Readable(t,0xD0,true)) {
            live[0]=*reinterpret_cast<const edf6vr::Matrix*>(t+0x50);
            live[1]=*reinterpret_cast<const edf6vr::Matrix*>(t+0x90);
            std::memcpy(local,w+0x350,sizeof(local));
            read=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { read=false; }
    if(read && part<kVehicleRecoilParts) {
        // Ours from the last tick (the game did not write it afresh): turn
        // from the game's last instead, never on top of our own.
        edf6vr::Matrix native[2]{live[0],live[1]};
        AcquireSRWLockExclusive(&g_nixArmMemoLock);
        NixArmMemo& memo=NixArmMemoFor(weapon,t);
        const bool ours=memo.have && !std::memcmp(&live[0],&memo.written[0],sizeof(live[0]));
        if(ours) { native[0]=memo.native[0]; native[1]=memo.native[1]; }
        ReleaseSRWLockExclusive(&g_nixArmMemoLock);
        if(ours) g_nixArmRedone.fetch_add(1,std::memory_order_relaxed);
        float pivot[3]{};
        const bool pivotOk=s.nodes && s.pivot[part]<s.nodeCount && VehicleRecoilNodeAt(s.nodes,s.pivot[part],pivot);
        bool turned=false;
        float M[3][3]{};
        edf6vr::Matrix out[2]{native[0],native[1]};
        if(pivotOk) {
            // The barrel as the game made it: the local fire direction through
            // the rows (as VehicleRecoilMuzzle), the muzzle at row 3.
            const auto& r=native[0].m;
            float d[3]{},size=0;
            for(int j=0;j<3;++j) { d[j]=local[0]*r[0][j]+local[1]*r[1][j]+local[2]*r[2][j]; size+=d[j]*d[j]; }
            size=std::sqrt(size);
            if(!(size>1e-4f) || !std::isfinite(size)) { for(int j=0;j<3;++j) d[j]=r[2][j]; size=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); }
            const float maxTurn=g_nixArmAimDegrees*0.01745329252f;
            const bool aim=target.valid && now-target.at<250;
            edf6vr::Vec3 barrel{},wanted{}; bool cut=false,ok=size>1e-4f && std::isfinite(size);
            if(ok) {
                barrel={d[0]/size,d[1]/size,d[2]/size};
                wanted=barrel;   // with no aim, back to the game's own
                if(aim) ok=edf6vr::AimCone(barrel,edf6vr::Vec3{target.dir[0],target.dir[1],target.dir[2]},maxTurn,wanted,&cut);
            }
            edf6vr::Vec3 now3=barrel;
            if(ok) {
                // Toward the wanted way at no more than the set speed, from the
                // game's own when it starts (or has been away).
                const double clock=Now();
                AcquireSRWLockExclusive(&g_nixArmMemoLock);
                NixArmNoteBody(s.vehicle,part,barrel,clock);
                auto& f=g_nixArmFollow[part];
                if(!f.have || clock-f.at>0.25) { f.dir[0]=barrel.x; f.dir[1]=barrel.y; f.dir[2]=barrel.z; f.at=clock; f.speed=0; }
                const float dt=static_cast<float>(std::clamp(clock-f.at,0.0,0.1));
                const float c=std::clamp(f.dir[0]*wanted.x+f.dir[1]*wanted.y+f.dir[2]*wanted.z,-1.0f,1.0f);
                const float apart=std::acos(c);
                // Gathers speed at the set acceleration up to the top speed, and
                // no faster than it can still stop in what is left (v^2 = 2 a d).
                const float scale=g_nixArmMachineScale.load(std::memory_order_relaxed);
                const float accel=g_nixArmAimAccel*scale*scale*0.01745329252f,top=g_nixArmAimSpeed*scale*0.01745329252f;
                f.speed=std::min({f.speed+accel*dt,top,std::sqrt(2.0f*accel*apart)});
                const float step=f.speed*dt;
                if(apart<=step || apart<1e-5f) { f.dir[0]=wanted.x; f.dir[1]=wanted.y; f.dir[2]=wanted.z; if(apart<1e-5f) f.speed=0; }
                else {
                    // Turn by `step` about the axis square to both.
                    float k[3]={f.dir[1]*wanted.z-f.dir[2]*wanted.y,f.dir[2]*wanted.x-f.dir[0]*wanted.z,f.dir[0]*wanted.y-f.dir[1]*wanted.x};
                    const float kl=std::sqrt(k[0]*k[0]+k[1]*k[1]+k[2]*k[2]);
                    if(kl>1e-6f) {
                        for(float& v:k) v/=kl;
                        const float kx[3]={k[1]*f.dir[2]-k[2]*f.dir[1],k[2]*f.dir[0]-k[0]*f.dir[2],k[0]*f.dir[1]-k[1]*f.dir[0]};
                        const float cs=std::cos(step),sn=std::sin(step);
                        for(int j=0;j<3;++j) f.dir[j]=f.dir[j]*cs+kx[j]*sn;
                    } else { f.dir[0]=wanted.x; f.dir[1]=wanted.y; f.dir[2]=wanted.z; }
                }
                f.at=clock; f.have=true;
                now3={f.dir[0],f.dir[1],f.dir[2]};
                ReleaseSRWLockExclusive(&g_nixArmMemoLock);
                // Still within reach of the body's aim as it is now.
                edf6vr::Vec3 held{};
                if(edf6vr::AimCone(barrel,now3,maxTurn,held)) now3=held;
            }
            const float c=std::clamp(barrel.x*now3.x+barrel.y*now3.y+barrel.z*now3.z,-1.0f,1.0f);
            const float angle=std::acos(c);
            // Home with no aim: let go.
            if(ok && !aim && angle<0.001f) {
                AcquireSRWLockExclusive(&g_nixArmMemoLock); g_nixArmFollow[part].have=false; ReleaseSRWLockExclusive(&g_nixArmMemoLock);
            } else if(ok && edf6vr::TurnBetween(barrel,now3,M)) {
                for(auto& m:out) NixArmTurnMatrix(m,M,pivot);
                g_nixArmLastAngle[part].store(angle*57.29578f,std::memory_order_relaxed);
                if(cut) g_nixArmClamped.fetch_add(1,std::memory_order_relaxed);
                turned=true;
            }
        }
        bool wrote=false;
        if(turned || ours) {
            // Turned, or (no aim now) the game's own put back over ours.
            __try {
                std::memcpy(t+0x50,&out[0],sizeof(out[0]));
                std::memcpy(t+0x90,&out[1],sizeof(out[1]));
                wrote=true;
            } __except(EXCEPTION_EXECUTE_HANDLER) { wrote=false; }
        }
        AcquireSRWLockExclusive(&g_nixArmMemoLock);
        NixArmMemo& after=NixArmMemoFor(weapon,t);
        if(turned && wrote) {
            after.native[0]=native[0]; after.native[1]=native[1]; after.written[0]=out[0]; after.written[1]=out[1]; after.have=true;
            std::memcpy(after.turn,M,sizeof(after.turn)); after.turnAt=now; after.turnValid=true;
        } else { after.have=false; after.turnValid=false; }
        ReleaseSRWLockExclusive(&g_nixArmMemoLock);
        if(turned && wrote) { g_nixArmTurned.fetch_add(1,std::memory_order_relaxed); NixArmPublishTurn(s,part,M,true); }
        else {
            if(ours && wrote) g_nixArmReleased.fetch_add(1,std::memory_order_relaxed);
            NixArmPublishTurn(s,part,kIdentity,false);
        }
    }
    original(weapon,context);
    return true;
}

// The aim line (Weapon_VehicleShoot/SwingShoot slot 3, 6B3660) is built from
// the same transform, but before the tick turns it (hardware 2026-10-03: the
// red line stayed on the game's own barrel), so the tick's last turn is put on
// the transform first. The tick then finds its own matrix there and turns from
// the game's, as it does when the game has not written it afresh.
std::atomic<unsigned long long> g_nixArmLineTurns{0};
using NixAimLineUpdate=void(__fastcall*)(void*,void*);
NixAimLineUpdate g_nixAimLineOriginal=nullptr;
void NixArmBeforeAimLine(void* weapon) noexcept {
    if(!g_nixArmAimOn || !weapon) return;
    NixArmShared s{};
    AcquireSRWLockShared(&g_nixArmLock); s=g_nixArmShared; ReleaseSRWLockShared(&g_nixArmLock);
    const ULONGLONG now=GetTickCount64();
    int index=-1;
    for(unsigned i=0;i<s.weaponCount;++i) if(s.weapons[i]==weapon) index=static_cast<int>(i);
    if(index<0 || now-s.at>250) return;
    const unsigned part=s.part[index];
    float pivot[3]{};
    if(part>=kVehicleRecoilParts || !s.nodes || s.pivot[part]>=s.nodeCount || !VehicleRecoilNodeAt(s.nodes,s.pivot[part],pivot)) return;
    auto* w=static_cast<unsigned char*>(weapon);
    unsigned char* t=nullptr; edf6vr::Matrix live[2]{};
    __try {
        if(edf6vr::Readable(w,0x1D8)) t=*reinterpret_cast<unsigned char**>(w+0x1D0);
        if(!t || !edf6vr::Readable(t,0xD0,true)) return;
        live[0]=*reinterpret_cast<const edf6vr::Matrix*>(t+0x50);
        live[1]=*reinterpret_cast<const edf6vr::Matrix*>(t+0x90);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    float M[3][3]{};
    AcquireSRWLockExclusive(&g_nixArmMemoLock);
    NixArmMemo& memo=NixArmMemoFor(weapon,t);
    const bool ours=memo.have && !std::memcmp(&live[0],&memo.written[0],sizeof(live[0]));
    const bool fresh=memo.turnValid && now-memo.turnAt<250;
    if(fresh) std::memcpy(M,memo.turn,sizeof(M));
    ReleaseSRWLockExclusive(&g_nixArmMemoLock);
    if(ours || !fresh) return;
    edf6vr::Matrix out[2]{live[0],live[1]};
    for(auto& m:out) NixArmTurnMatrix(m,M,pivot);
    __try {
        std::memcpy(t+0x50,&out[0],sizeof(out[0]));
        std::memcpy(t+0x90,&out[1],sizeof(out[1]));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    AcquireSRWLockExclusive(&g_nixArmMemoLock);
    NixArmMemo& after=NixArmMemoFor(weapon,t);
    after.native[0]=live[0]; after.native[1]=live[1]; after.written[0]=out[0]; after.written[1]=out[1]; after.have=true;
    ReleaseSRWLockExclusive(&g_nixArmMemoLock);
    g_nixArmLineTurns.fetch_add(1,std::memory_order_relaxed);
}
void __fastcall HookNixAimLine(void* weapon,void* context) {
    NixArmBeforeAimLine(weapon);
    g_nixAimLineOriginal(weapon,context);
}
bool InstallNixArmAimLine() noexcept {
    if(!g_image.base) return false;
    g_nixAimLineOriginal=reinterpret_cast<NixAimLineUpdate>(g_image.base+0x6B3660);
    bool all=true;
    for(const unsigned table:{0x17E6120u,0x17E6320u}) {   // Weapon_VehicleShoot, Weapon_VehicleSwingShoot
        bool changed=false;
        if(!edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+table+0x18),g_image.base+0x6B3660,
                                   reinterpret_cast<void*>(&HookNixAimLine),changed)) all=false;
    }
    return all;
}
// The shot. The game rebuilds the firing muzzle's matrices from the weapon's own
// (6904D2/694873: g_dualPose) at the moment it fires, after the tick turned
// them (hardware 2026-10-03: the shots left the game's own barrel, its own
// way), and a weapon has a muzzle entry per barrel (weapon+0x1D0, 0xF0 apart,
// count +0x1E0; the shot uses index % count). So the tick's last turn is put on
// the firing entry right there, and the entry put back after the shot.
std::atomic<unsigned long long> g_nixArmShotTurns{0};
bool NixArmFire(void* weapon,unsigned index,void* alternate,void* counter,bool consume) noexcept {
    if(!g_nixArmAimOn || !weapon) return false;
    NixArmShared s{};
    AcquireSRWLockShared(&g_nixArmLock); s=g_nixArmShared; ReleaseSRWLockShared(&g_nixArmLock);
    const ULONGLONG now=GetTickCount64();
    int at=-1;
    for(unsigned i=0;i<s.weaponCount;++i) if(s.weapons[i]==weapon) at=static_cast<int>(i);
    if(at<0 || now-s.at>250) return false;
    const unsigned part=s.part[at];
    float pivot[3]{};
    if(part>=kVehicleRecoilParts || !s.nodes || s.pivot[part]>=s.nodeCount || !VehicleRecoilNodeAt(s.nodes,s.pivot[part],pivot)) return false;
    auto* w=static_cast<unsigned char*>(weapon);
    unsigned char* entry=nullptr;
    __try {
        if(!edf6vr::Readable(w,0x1E8)) return false;
        auto* entries=*reinterpret_cast<unsigned char**>(w+0x1D0);
        const auto count=*reinterpret_cast<const std::uint64_t*>(w+0x1E0);
        if(!entries || !count || count>64 || !edf6vr::Readable(entries,static_cast<std::size_t>(count)*0xF0,true)) return false;
        entry=entries+(index%count)*0xF0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    float M[3][3]{};
    AcquireSRWLockExclusive(&g_nixArmMemoLock);
    bool fresh=false;
    for(const auto& m:g_nixArmMemo)
        if(m.weapon==weapon && m.turnValid && now-m.turnAt<250) { std::memcpy(M,m.turn,sizeof(M)); fresh=true; break; }
    ReleaseSRWLockExclusive(&g_nixArmMemoLock);
    if(!fresh) return false;
    edf6vr::Matrix kept[2]{};
    __try {
        kept[0]=*reinterpret_cast<const edf6vr::Matrix*>(entry+0x50);
        kept[1]=*reinterpret_cast<const edf6vr::Matrix*>(entry+0x90);
        edf6vr::Matrix out[2]{kept[0],kept[1]};
        for(auto& m:out) NixArmTurnMatrix(m,M,pivot);
        std::memcpy(entry+0x50,&out[0],sizeof(out[0]));
        std::memcpy(entry+0x90,&out[1],sizeof(out[1]));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    g_nixArmShotTurns.fetch_add(1,std::memory_order_relaxed);
    __try { g_dualFire(weapon,index,alternate,counter,consume); }
    __finally {
        std::memcpy(entry+0x50,&kept[0],sizeof(kept[0]));
        std::memcpy(entry+0x90,&kept[1],sizeof(kept[1]));
    }
    return true;
}
// For the hand aim's hum (plugin.cpp): with the arms on the aim, the body's
// turn needs no hum ("この仕様であればもう不要").
bool NixArmAimActive() noexcept { return g_nixArmAimOn; }
// Once every five seconds from the camera update, while in a vehicle.
void NixArmAimReport() noexcept {
    static ULONGLONG at=0;
    const ULONGLONG now=GetTickCount64();
    if(now-at<5000) return;
    at=now;
    NixArmShared s{}; NixArmTarget target{};
    AcquireSRWLockShared(&g_nixArmLock); s=g_nixArmShared; target=g_nixArmTarget; ReleaseSRWLockShared(&g_nixArmLock);
    if(!s.weaponCount && !target.valid) return;
    char parts[64]; parts[0]=0;
    for(unsigned i=0,used=0;i<s.weaponCount && used<sizeof(parts)-8;++i) {
        const int n=std::snprintf(parts+used,sizeof(parts)-used,"%s%u",i?"/":"",static_cast<unsigned>(s.part[i]));
        if(n<0) break;
        used+=static_cast<unsigned>(n);
    }
    const unsigned long long shiftSum=g_nixArmPivotShiftSum.exchange(0),shiftCount=g_nixArmPivotShiftCount.exchange(0);
    const unsigned shiftMax=g_nixArmPivotShiftMax.exchange(0),turnMax=g_nixArmPivotTurnMax.exchange(0);
    const unsigned bodyMax=g_nixArmBodyRateMax.exchange(0),bodyHeld=g_nixArmBodySustainedMax.exchange(0);
    Log("NIXARM on=%d weapons=%u (parts %s) target=%d ticks=%llu turned=%llu clamped=%llu redone=%llu released=%llu lineTurns=%llu shotTurns=%llu drawn=%llu last turn by part=%.1f/%.1f/%.1f/%.1f deg (max %.0f, parallel) body turn max %u deg/s, held 0.2 s %u deg/s (since boarding %u/%u) machine %s arms x%.2f pivots palette/node=%llu/%llu palette off the node array avg/max=%.1f/%.1f cm turn max=%.2f deg",
        g_nixArmAimOn?1:0,s.weaponCount,parts,target.valid?1:0,g_nixArmTicks.load(),g_nixArmTurned.load(),g_nixArmClamped.load(),g_nixArmRedone.load(),
        g_nixArmReleased.load(),g_nixArmLineTurns.load(),g_nixArmShotTurns.load(),g_nixArmDrawn.load(),g_nixArmLastAngle[0].load(),g_nixArmLastAngle[1].load(),
        g_nixArmLastAngle[2].load(),g_nixArmLastAngle[3].load(),g_nixArmAimDegrees,bodyMax,bodyHeld,g_nixArmBodySessionMax.load(),g_nixArmBodySessionSustained.load(),
        g_nixArmMachine.load()>=0?kNixVariants[g_nixArmMachine.load()].name:"not known",g_nixArmMachineScale.load(),g_nixArmPivotPalette.load(),g_nixArmPivotLive.load(),
        shiftCount?shiftSum/10.0/static_cast<double>(shiftCount):0.0,shiftMax/10.0,turnMax/100.0);
}
