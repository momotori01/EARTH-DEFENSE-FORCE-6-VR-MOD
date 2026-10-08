// Included in plugin.cpp's private namespace, after ranger_dual.h.
//
// guide_probe.h, which watches the same two attachment classes from the same
// two OnUpdate hooks and is included after this file. GuideRayBegin/End frame
// the update whose landing rays it corrects.
void GuideProbeUpdate(void* attachment,unsigned klass) noexcept;
bool GuideProbeArc(void* weapon,float* start,float* direction) noexcept;
void GuideRayBegin(void* attachment) noexcept;
void GuideRayEnd() noexcept;
//
// The Fencer's left weapon on the left controller, with the game's own
// heaviness. The game keeps one aim per soldier: a target at soldier+0x1230
// that the pad's look input (+0xD60) is added to each tick, a smoothed aim at
// +0x1240 that follows it by the loadout's coefficient at +0x1260
// (572DF0: 1240 += (1230-1240)*1260), and the weapon loop (59ABB0) that adds
// each weapon's recoil vector (weapon+0xF00, scaled by soldier+0x1AAC) to
// both. The right hand keeps all of that as it was: the pad pursuit turns the
// soldier's aim toward the right controller, and the right weapon, its shots
// and its recoil are the game's.
//
// The left hand gets a shadow of the same two aims, driven the same way: the
// pursuit's pad deflection toward the left controller, turned into look input
// with the gain the game is measured to apply to the right hand's deflection,
// integrated, clamped, and smoothed with the soldier's own coefficient. While
// the left weapon's tick runs, the shadow is swapped into the soldier in
// place of his aim, so whatever the weapon reads for its shot direction reads
// the left aim; afterwards the aim is put back and the left weapon's recoil
// vector is taken into the shadow (and zeroed, so the loop does not also add
// it to the right). The left weapon stays on its mount -- a powered suit
// carries it -- and is turned to the shadow's smoothed direction for the draw.
bool g_fencerSplitAim=true;    // FencerSplitAim: the whole thing; 0 = one aim for both weapons as the game has it
bool g_fencerHeavyAim=true;    // FencerHeavyAim: 0 = the left weapon follows the controller with no lag
bool g_fencerSwapHands=false;  // FencerSwapHands: slot 1 is the left hand (the left trigger's weapon) unless set
bool g_fencerHandWeapons=true; // FencerHandWeapons: the weapons ride the controllers (shots and flash from the carried muzzle); 0 = on their mounts
std::atomic<void*> g_fencerRightModel{nullptr};
float g_fencerHandOutward=0.05f; // FencerHandOutwardMetres: the carried weapon's distance outward from the controller, both hands
float g_fencerHandAhead=-0.027f;  // FencerHandAheadMetres / FencerHandUpMetres: the Fencer's own grip trims (the Ranger's
float g_fencerHandUp=0.022f;      // WeaponAhead/Right/UpMetres are not applied to the Fencer)
void* g_fencerGainPair[2]{};      // the weapons the gain table was learned for; a new pair starts over
bool g_fencerHandRoll=true;      // FencerHandRoll: the carried weapon rolls with the controller (heavy, like the aim); 0 = upright
// Shoulder weapons pivot on the player's shoulders: the eye (headset) plus
// these offsets in the body's yaw frame, the body yaw following the head's
// slowly (the torso we cannot track). FencerShoulderOnHead=0 leaves them on
// the game's mount.
bool g_fencerShoulderOnHead=true;
float g_fencerShoulderSide=0.40f,g_fencerShoulderDown=0.10f,g_fencerShoulderBack=0.25f;
std::atomic<unsigned long long> g_fencerMuzzleFar[2]{};   // transform writes refused for reach, per hand
std::atomic<unsigned long long> g_fencerTransformTurns{0},g_fencerBulletSkips{0}; // left transform turned per tick; bullets already on the shadow
float g_fencerBodyYaw=0; bool g_fencerBodyYawValid=false;
float g_fencerRoll[2]{};         // the smoothed roll of each carried weapon, radians from upright
// The roll target is integrated from the controller's twist about its own
// pointing axis frame to frame (continuous through vertical, where an
// absolute roll against world-up flips by 180 degrees), and re-anchored to the
// absolute roll while the axis is well away from vertical, so it cannot drift.
float g_fencerRollTarget[2]{};
edf6vr::Quat g_fencerRollPrev[2]{}; bool g_fencerRollPrevValid[2]{};
// Shoulder mounts: a weapon whose root sits near eye height (the hand-held
// ones hang ~0.5 m and more below the eye) stays on its mount and only turns
// to its aim; the user asked for exactly that. Hysteresis against the stance.
bool g_fencerShoulder[2]{}; float g_fencerMountDy[2]{}; unsigned g_fencerMountVotes[2]{}; void* g_fencerMountWeapon[2]{};
// Which model axis is which aim axis, per hand weapon, learned while the
// weapon is held plainly and then kept. The mount's rows carry the arm
// animation, and a dash swings the arm far enough that the nearest-axis
// mapping changed from one update to the next: the carried spear bent off
// sideways while dashing. The mapping is a property of the model, so once it
// has been seen the same 30 updates running with every axis within ~25
// degrees of its aim axis, it is frozen for that weapon.
struct FencerAxisMap { void* weapon=nullptr; int axisOf[3]{0,1,2}; float sign[3]{1,1,1}; int candidate[3]{-1,-1,-1}; float candidateSign[3]{}; unsigned agree=0; bool frozen=false;
    // What was actually used last frame. A rigid model cannot change which
    // of its axes is which, so a fresh answer that merely disagrees is not
    // evidence -- near a right angle two axes correlate almost equally and
    // the pick alternates, which turned the left weapon over and walked its
    // muzzle up and down into the ground. Hold the last answer while it is
    // still consistent with what the model is doing.
    int lastAxisOf[3]{}; float lastSign[3]{}; bool lastValid=false; };
FencerAxisMap g_fencerAxisMap[2]{};
std::atomic<unsigned long long> g_fencerMapFrozen{0},g_fencerMapOverrides{0};
// Frames where the frozen map had an axis pointing the wrong way and was
// set aside for the live measurement.
std::atomic<unsigned long long> g_fencerMapContradicted{0};
// Frames where a fresh disagreeing answer was passed over because the one in
// use still held up.
std::atomic<unsigned long long> g_fencerMapHeld{0};
// FencerDirectAim: the weapon's own axes laid straight on the hand's, the way
// the Ranger's are -- barrel down the model's +Z, its top along +Y. 0 falls
// back to the axis mapping, which is kept only until that machinery is
// deleted.
bool g_fencerDirectAim=true;
std::atomic<unsigned long long> g_fencerDirectAimed[2]{};
// A held shield's rows as the game posed them, in the frame of the aim they
// were posed along (row i, column k = native row i . aim-frame axis k, axes
// left/up/forward). Only for the log: it says which way the model's face
// points in the game's own hands.
float g_fencerShieldNative[2][3][3]{};
bool g_fencerShieldSeen[2]{};
// The shield, the blades, the hammers and the shoulder weapons keep the
// game's own pose, animation and all -- raising the shield to guard and
// lowering it to recover are what say at a glance whether it is up, and a
// blade's pose is its swing -- but not through a dash, which swings them off
// the aim. The pose is followed through a smoothing whose time
// constant is short on foot and long while the Fencer is moving faster than a
// walk (and a short tail after), so a dash barely moves it and it glides back
// afterwards. Holding the pre-dash pose outright and letting go at the end
// snapped it at every dash, which in a chain of dashes read as the angle
// jerking each time. Hand guns need none of this: they are laid on the hand.
constexpr float kFencerPoseFollowSec=0.08f, kFencerPoseDashSec=1.5f;
struct FencerPoseMemo {
    void* weapon=nullptr;
    float smooth[3][3]{}; bool valid=false; double at=0;
};
FencerPoseMemo g_fencerPoseMemo[2]{};
constexpr float kFencerDashSpeed=6.0f;            // m/s, horizontal; a walk is well under it
constexpr ULONGLONG kFencerDashTailMs=400;
// A dash or a jump starts on the Fencer's secondary button (the grip, LB/RB) of a
// hand whose weapon carries one: SecondaryFire_Type, which the weapon's setup
// (68A920) stores at weapon+0x690 (68D0A4) -- 4 jumpjets, 5 dash, 6 shield
// reflect (sgott meta.js). From the press the equipment holds the pose it had;
// the hold lasts at least kFencerActionMinMs, and while he moves fast across the
// ground or up and down (a jump until he lands), plus kFencerActionTailMs. The
// speed alone still starts one, as before, for a dash the press was not seen for.
constexpr ULONGLONG kFencerActionMinMs=350,kFencerActionTailMs=150;
constexpr float kFencerAirSpeed=1.0f;              // m/s, vertical
constexpr float kFencerJumpSpeed=2.5f;             // m/s, vertical: a jump or a fall without a press
// After a hold the pose comes back this fast (instead of the 1.5 s the dash
// smoothing took, which trailed every dash), then follows at kFencerPoseFollowSec.
constexpr float kFencerPoseReturnSec=0.15f; constexpr double kFencerPoseReturnFor=0.4;
std::atomic<unsigned long long> g_fencerActions[2]{};   // dashes, jumps started by a press
int g_fencerSecondaryType[2]={-1,-1};                    // last read, per hand, for the log
// The zoom (SecondaryFire_Type 1 at weapon+0x690, seen on hardware 2026-10-01:
// L=1 R=4 with a zoom cannon in the left): the hand whose grip last asked a zoom
// weapon for it, and the hand the scope panel last followed (for the log).
int g_fencerZoomHand=1,g_fencerScopeHand=1;
double g_fencerHoldEndedAt=0;
int FencerSecondaryType(void* weapon) noexcept {
    if(!weapon) return -1;
    __try {
        const int type=*reinterpret_cast<const int*>(static_cast<const unsigned char*>(weapon)+0x690);
        return type>=0 && type<=6?type:-1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
bool g_fencerDashHold=false;
ULONGLONG g_fencerDashUntil=0;
float g_fencerSpeed=0,g_fencerSpeedPeak=0;
float g_fencerPrevPos[3]{}; double g_fencerPrevAt=0;
std::atomic<unsigned long long> g_fencerPoseSlowed{0};    // updates smoothed with the dash time constant
// c: a weapon's rows in the frame of the aim it was posed along. Replaced by the
// smoothed relation, squared back to a rotation with each row its own length.
void FencerSmoothPose(unsigned hand,void* weapon,float c[3][3]) noexcept {
    if(hand>1) return;
    auto& m=g_fencerPoseMemo[hand];
    if(m.weapon!=weapon) { m=FencerPoseMemo{}; m.weapon=weapon; }
    LARGE_INTEGER ticks{},rate{};
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&rate);
    const double now=rate.QuadPart?static_cast<double>(ticks.QuadPart)/static_cast<double>(rate.QuadPart):0.0;
    const double dt=now-m.at; m.at=now;
    if(!m.valid || !(dt>0) || dt>0.5) { std::memcpy(m.smooth,c,sizeof(m.smooth)); m.valid=true; }
    else if(g_fencerDashHold) {
        // Held through the dash or the jump: the body's animation does not reach it.
        g_fencerPoseSlowed.fetch_add(1,std::memory_order_relaxed);
    } else {
        const float tau=now-g_fencerHoldEndedAt<kFencerPoseReturnFor?kFencerPoseReturnSec:kFencerPoseFollowSec;
        const float follow=1.0f-std::exp(-static_cast<float>(dt)/tau);
        for(int i=0;i<3;++i) for(int k=0;k<3;++k) m.smooth[i][k]+=(c[i][k]-m.smooth[i][k])*follow;
    }
    float length[3]{};
    for(int i=0;i<3;++i) length[i]=std::sqrt(c[i][0]*c[i][0]+c[i][1]*c[i][1]+c[i][2]*c[i][2]);
    float z[3]={m.smooth[2][0],m.smooth[2][1],m.smooth[2][2]};
    const float lz=std::sqrt(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);
    float y[3]={m.smooth[1][0],m.smooth[1][1],m.smooth[1][2]};
    if(!(lz>1e-4f)) return;
    for(float& v:z) v/=lz;
    const float d=y[0]*z[0]+y[1]*z[1]+y[2]*z[2];
    for(int k=0;k<3;++k) y[k]-=z[k]*d;
    const float ly=std::sqrt(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]);
    if(!(ly>1e-4f)) return;
    for(float& v:y) v/=ly;
    const float x[3]={y[1]*z[2]-y[2]*z[1],y[2]*z[0]-y[0]*z[2],y[0]*z[1]-y[1]*z[0]};
    for(int k=0;k<3;++k) { c[0][k]=x[k]*length[0]; c[1][k]=y[k]*length[1]; c[2][k]=z[k]*length[2]; }
}
// A shoulder weapon's pose as it is when he is on his feet, and only that.
//
// The mounts hang on the body's shoulder bones, so every body animation -- a
// jump pitched the barrel down, a dash span the guide line round, walking swayed
// it -- reached the weapon, and the dash hold could not see the dashes and jumps
// that support gear gives (2026-09-26). The user's idea was to keep the body in
// one pose and take only the weapon's own animation and the aim. The same thing,
// without touching the game's animation: the weapon's relation to the aim frame
// is learned per weapon while he is on the ground, not dashing and not firing
// (g_fencerSettled), and only that learned relation is used, whatever the body
// does. The weapon's own bones (a barrel sliding, a launcher opening) move under
// that root as before, and it points where the aim points.
//
// It is learned in ordinary play: first from a quarter second of steady rows
// after the weapon comes out (the change animation has settled by then), then
// followed slowly -- 0.3 s standing, 1 s walking, so a step's sway is mostly
// averaged away -- and frozen whenever he leaves the ground, dashes or fires.
// Asking the player to stand still for it was the first version, and nobody
// who downloads the mod would know to (user, 2026-09-26). Until the first
// quarter second it is smoothed as before.
//
// That learning is now only the fallback. A weapon whose model is one of the
// known back mounts (kFencerBackModels) takes kFencerShoulderRest from the
// start, and nothing the body does -- a jump, a dash, a step -- reaches it.
bool g_fencerSettled=false,g_fencerWalking=false;
// On the ground and not in a dash or a jump, with the triggers left out: the
// shield's guard IS its trigger, so g_fencerSettled (no fire for 300 ms) never
// let the raised pose be learned (hardware, EFABFBFF: guard learnt 0 of 3).
bool g_fencerOnFeet=false;
constexpr unsigned kFencerRestFirstUpdates=15;     // steady settled updates before the first sample
constexpr float kFencerRestLearnSec=0.3f;          // how fast the learned pose follows, standing
constexpr float kFencerRestWalkSec=1.0f;           // and walking
constexpr float kFencerRestSteady=0.02f;           // largest change of a row per update still taken as steady
// Down: the head bone near the floor. Lying downed and waiting for rescue passed
// every "settled" test (slow, no dash, no trigger), and the shield's rest pose
// followed the downed animation -- 144-179 degrees off at 23:50 on 2026-09-28,
// and rolled 62 degrees away over two minutes from 23:58 -- so after the revive
// it was held on the palm side. The head was 0.21 and 0.60 m above the feet in
// those two spells; on his feet it never went under 0.89 (a dash's crouch).
constexpr float kFencerDownHead=0.75f;
constexpr ULONGLONG kFencerDownTailMs=800;         // and the stand-up after it
bool g_fencerDown=false;
std::atomic<unsigned long long> g_fencerDowns{0};
// A backstop behind it: a learned pose only follows the game's while that stays
// within this of the FIRST pose learned. Aiming up and down moves the game's
// own pose by about 30 degrees; the downed poses were 144-179 away.
constexpr float kFencerRestAnchorDeg=60.0f;
std::atomic<unsigned long long> g_fencerRestOffAnchor{0};
// The rotation between two row sets, in degrees, each row taken as a direction.
float FencerRowsAngle(const float a[3][3],const float b[3][3]) noexcept {
    float trace=0;
    for(int i=0;i<3;++i) {
        const float la=std::sqrt(a[i][0]*a[i][0]+a[i][1]*a[i][1]+a[i][2]*a[i][2]);
        const float lb=std::sqrt(b[i][0]*b[i][0]+b[i][1]*b[i][1]+b[i][2]*b[i][2]);
        if(!(la>1e-4f) || !(lb>1e-4f)) return 180.0f;
        trace+=(a[i][0]*b[i][0]+a[i][1]*b[i][1]+a[i][2]*b[i][2])/(la*lb);
    }
    return std::acos(std::clamp((trace-1.0f)*0.5f,-1.0f,1.0f))*57.29578f;
}
// The back-mounted models: every weapon whose ModelConstraint is "backWeapon"
// (48 in sgott data/6/weapon) uses one of these ten. A model's root node bears
// its name, so the node lookup tells them apart (FencerBackModel). FENCERREST
// still reports the game's own pose, standing, for comparison.
//
// The shoulder mounts' pose in the aim frame (rows x, y, z; columns left, up,
// forward), recorded on hardware on 2026-09-26 (320 FENCERREST reports over
// eight of the ten models, both sides) and fitted to a level aim. Every model
// and both shoulders read the same within a few hundredths, so it is the
// body's backWeapon bone that sets it, and one table serves all of them. The
// game lets the mount lean only part of the way with the aim (row 2 drops by
// 0.4 per unit of aimUp); held fixed, it turns fully with the aim instead.
constexpr float kFencerShoulderRest[3][3]={{0.9958f,0.0125f,0.0910f},{-0.0400f,0.9507f,0.3076f},{-0.0827f,-0.3100f,0.9471f}};
constexpr const wchar_t* kFencerBackModels[]={L"h_attach_large02",L"h_cannon_shoulder01",L"h_attach_shoulder02",
    L"h_attach_needle01",L"h_attach_shoulder03",L"h_attach_vsl01",L"h_attach_gatling01",L"h_attach_blaster01",
    L"h_attach_heavylazer01",L"h_attach_616_shoulder04"};
constexpr int kFencerBackModelCount=static_cast<int>(sizeof(kFencerBackModels)/sizeof(kFencerBackModels[0]));
constexpr unsigned kFencerRestReportUpdates=60;    // steady updates standing before the pose is reported
struct FencerRestMemo { void* weapon=nullptr; float rest[3][3]{}; float anchor[3][3]{}; float last[3][3]{}; float reported[3][3]{};
    bool learned=false,haveLast=false,haveReported=false; unsigned steadyRun=0,standRun=0; double at=0; };
FencerRestMemo g_fencerRest[8]{};
unsigned g_fencerRestNext=0;
std::atomic<unsigned long long> g_fencerRestUsed{0},g_fencerRestLearnt{0},g_fencerRestReported{0},g_fencerRestFixed{0};
// c as in FencerSmoothPose. model: kFencerBackModels index or -1; aimUp: the
// native aim's vertical component, for the report (the rest pose may lean with the aim).
void FencerRestPose(unsigned hand,void* weapon,float c[3][3],int model=-1,float aimUp=0) noexcept {
    FencerRestMemo* m=nullptr;
    for(auto& e:g_fencerRest) if(e.weapon==weapon && weapon) { m=&e; break; }
    if(!m) { m=&g_fencerRest[g_fencerRestNext]; g_fencerRestNext=(g_fencerRestNext+1)%8; *m=FencerRestMemo{}; m->weapon=weapon; }
    LARGE_INTEGER ticks{},rate{};
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&rate);
    const double now=rate.QuadPart?static_cast<double>(ticks.QuadPart)/static_cast<double>(rate.QuadPart):0.0;
    const double dt=now-m->at; m->at=now;
    float change=0;
    if(m->haveLast) for(int i=0;i<3;++i) for(int k=0;k<3;++k) change=std::max(change,std::fabs(c[i][k]-m->last[i][k]));
    std::memcpy(m->last,c,sizeof(m->last)); m->haveLast=true;
    const bool steady=change<kFencerRestSteady;
    if(g_fencerSettled && steady) {
        if(m->steadyRun<kFencerRestFirstUpdates) ++m->steadyRun;
        if(!m->learned) {
            if(m->steadyRun>=kFencerRestFirstUpdates) { std::memcpy(m->rest,c,sizeof(m->rest)); std::memcpy(m->anchor,c,sizeof(m->anchor)); m->learned=true; g_fencerRestLearnt.fetch_add(1,std::memory_order_relaxed); }
        } else if(FencerRowsAngle(c,m->anchor)>kFencerRestAnchorDeg) {
            g_fencerRestOffAnchor.fetch_add(1,std::memory_order_relaxed);
        } else if(dt>0 && dt<0.5) {
            const float follow=1.0f-std::exp(-static_cast<float>(dt)/(g_fencerWalking?kFencerRestWalkSec:kFencerRestLearnSec));
            for(int i=0;i<3;++i) for(int k=0;k<3;++k) m->rest[i][k]+=(c[i][k]-m->rest[i][k])*follow;
        }
    } else if(!m->learned) m->steadyRun=0;
    // Report the game's pose once it has held a second standing still, and
    // again whenever it has moved since: the material for the fixed table.
    if(g_fencerSettled && !g_fencerWalking && steady) { if(m->standRun<kFencerRestReportUpdates) ++m->standRun; }
    else m->standRun=0;
    if(m->standRun>=kFencerRestReportUpdates) {
        float moved=m->haveReported?0.0f:1.0f;
        for(int i=0;i<3;++i) for(int k=0;k<3;++k) moved=std::max(moved,std::fabs(c[i][k]-m->reported[i][k]));
        if(moved>0.03f) {
            std::memcpy(m->reported,c,sizeof(m->reported)); m->haveReported=true;
            g_fencerRestReported.fetch_add(1,std::memory_order_relaxed);
            Log("FENCERREST model=%ls hand=%u aimUp=%.3f rows=%.4f,%.4f,%.4f;%.4f,%.4f,%.4f;%.4f,%.4f,%.4f",
                model>=0 && model<kFencerBackModelCount?kFencerBackModels[model]:L"unknown",hand,aimUp,
                c[0][0],c[0][1],c[0][2],c[1][0],c[1][1],c[1][2],c[2][0],c[2][1],c[2][2]);
        }
    }
    if(model>=0 && model<kFencerBackModelCount) {
        // A known back mount: the recorded pose, not what was learned.
        std::memcpy(m->rest,kFencerShoulderRest,sizeof(m->rest)); m->learned=true;
        g_fencerRestFixed.fetch_add(1,std::memory_order_relaxed);
    }
    if(!m->learned) { FencerSmoothPose(hand,weapon,c); return; }
    g_fencerRestUsed.fetch_add(1,std::memory_order_relaxed);
    float length[3]{};
    for(int i=0;i<3;++i) length[i]=std::sqrt(c[i][0]*c[i][0]+c[i][1]*c[i][1]+c[i][2]*c[i][2]);
    float z[3]={m->rest[2][0],m->rest[2][1],m->rest[2][2]},y[3]={m->rest[1][0],m->rest[1][1],m->rest[1][2]};
    const float lz=std::sqrt(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);
    if(!(lz>1e-4f)) return;
    for(float& v:z) v/=lz;
    const float d=y[0]*z[0]+y[1]*z[1]+y[2]*z[2];
    for(int k=0;k<3;++k) y[k]-=z[k]*d;
    const float ly=std::sqrt(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]);
    if(!(ly>1e-4f)) return;
    for(float& v:y) v/=ly;
    const float x[3]={y[1]*z[2]-y[2]*z[1],y[2]*z[0]-y[0]*z[2],y[0]*z[1]-y[1]*z[0]};
    for(int k=0;k<3;++k) { c[0][k]=x[k]*length[0]; c[1][k]=y[k]*length[1]; c[2][k]=z[k]*length[2]; }
}
// The block test (HookGuardTest): every call, the ones run on the left aim, and
// of those the hits it blocked; and how many of its four entries were redirected.
std::atomic<unsigned long long> g_fencerGuardTests{0},g_fencerGuardLeft{0},g_fencerGuardLeftBlocked{0};
unsigned g_fencerGuardSites=0;
// Rows squared back to a rotation, each its own length from `length`.
bool FencerSquareRows(const float in[3][3],const float length[3],float out[3][3]) noexcept {
    float z[3]={in[2][0],in[2][1],in[2][2]},y[3]={in[1][0],in[1][1],in[1][2]};
    const float lz=std::sqrt(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);
    if(!(lz>1e-4f)) return false;
    for(float& v:z) v/=lz;
    const float d=y[0]*z[0]+y[1]*z[1]+y[2]*z[2];
    for(int k=0;k<3;++k) y[k]-=z[k]*d;
    const float ly=std::sqrt(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]);
    if(!(ly>1e-4f)) return false;
    for(float& v:y) v/=ly;
    const float x[3]={y[1]*z[2]-y[2]*z[1],y[2]*z[0]-y[0]*z[2],y[0]*z[1]-y[1]*z[0]};
    for(int k=0;k<3;++k) { out[0][k]=x[k]*length[0]; out[1][k]=y[k]*length[1]; out[2][k]=z[k]*length[2]; }
    return true;
}
// The shield, in one of two poses of its own, both fixed on the controller:
// raised while its hand's trigger is held (the guard) and lowered otherwise.
//
// It kept the game's animated pose until 2026-09-26, then each pose was learned
// from the game's own while he stood still. The user asked for it laid on the
// controller in both (2026-10-02, "盾、下ろしている時もコントローラに直接固定に
// ... 構えて正面、下ろしてる時は今と同じ側面で"): nothing of the game's arm reaches
// it any more, so no dash, jump or swing of the other hand sways it. The
// lowered pose is the mean of the 58 lowered poses learned on hardware on
// 2026-10-01 (SHIELDREST, left hand; median 9 degrees from it), squared, and
// its mirror image in the right hand. The shield swings between the two in
// about a tenth of a second.
struct FencerShieldMemo {
    void* weapon=nullptr;
    float shown[3][3]{}; bool haveShown=false;
    bool guard=false; double at=0;
};
FencerShieldMemo g_fencerShield[4]{};
unsigned g_fencerShieldNext=0;
constexpr float kFencerShieldSwingSec=0.05f;      // the swing between lowered and raised
std::atomic<unsigned long long> g_fencerShieldShown[2]{};
void FencerShieldPose(unsigned hand,void* weapon,float c[3][3]) noexcept {
    if(hand>1) return;
    FencerShieldMemo* m=nullptr;
    for(auto& e:g_fencerShield) if(e.weapon==weapon && weapon) { m=&e; break; }
    if(!m) { m=&g_fencerShield[g_fencerShieldNext]; g_fencerShieldNext=(g_fencerShieldNext+1)%4; *m=FencerShieldMemo{}; m->weapon=weapon; }
    LARGE_INTEGER ticks{},rate{};
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&rate);
    const double now=rate.QuadPart?static_cast<double>(ticks.QuadPart)/static_cast<double>(rate.QuadPart):0.0;
    const double dt=now-m->at; m->at=now;
    const float trigger=g_handTrigger[hand].load(std::memory_order_relaxed);
    m->guard=m->guard?trigger>0.35f:trigger>0.55f;
    float length[3]{};
    for(int i=0;i<3;++i) length[i]=std::sqrt(c[i][0]*c[i][0]+c[i][1]*c[i][1]+c[i][2]*c[i][2]);
    // Rows in the aim frame (columns left, up, forward). Guarding, it stands
    // square in front of the controller, face forward: both shields seen
    // (tower, deflection) learnt these axes when raised -- model x back toward
    // the player, y to the right, z up. Lowered, it hangs at the side as the
    // game carries it.
    //
    // The right hand holds it as the mirror image, which for the same model is
    // the left's rows with the left column negated and its y row turned round
    // (still a rotation): the game's own poses, worked out from the Fencer's
    // clips on P607_FENCER (stand_base + shield_poseAim_armL/R_add at a level
    // aim, the shield on armsFlip_l/r), give the left within 3 degrees of the
    // learned pose and the right within 3 degrees of this. The left's lowered
    // pose on the right hand had the shield on the inside of the hand
    // (hardware, 2026-10-02: "右手のシールドの通常時が、右手の内側"). The guard
    // is its own mirror, and the game's raised poses (recoil_shield_armL/R_add)
    // keep the same relation, so one guard serves both hands.
    constexpr float kGuardFront[3][3]={{0,0,-1},{-1,0,0},{0,1,0}};
    constexpr float kLowered[3][3]={{-0.9429f,0.1130f,0.3135f},{-0.1768f,-0.9671f,-0.1829f},{0.2825f,-0.2279f,0.9318f}};
    constexpr float kLoweredRight[3][3]={{0.9429f,0.1130f,0.3135f},{-0.1768f,0.9671f,0.1829f},{-0.2825f,-0.2279f,0.9318f}};
    const auto& target=m->guard?kGuardFront:(hand==1?kLoweredRight:kLowered);
    g_fencerShieldShown[m->guard?1:0].fetch_add(1,std::memory_order_relaxed);
    if(!m->haveShown || !(dt>0) || dt>0.5) { std::memcpy(m->shown,target,sizeof(m->shown)); m->haveShown=true; }
    else {
        const float follow=1.0f-std::exp(-static_cast<float>(dt)/kFencerShieldSwingSec);
        for(int i=0;i<3;++i) for(int k=0;k<3;++k) m->shown[i][k]+=(target[i][k]-m->shown[i][k])*follow;
    }
    float out[3][3]{};
    if(FencerSquareRows(m->shown,length,out)) std::memcpy(c,out,sizeof(out));
}
// The soldier's movement since this command was made, added to where it puts
// the weapon. The command is built in the camera update and used by the next
// weapon tick, and the game has moved the soldier in between: the Ranger's
// second gun already does this. Without it a Fencer's shot left from where the
// weapon was an update ago -- a clear gap from the muzzle in a jump.
void FencerFollowSoldier(WeaponHoldCommand& command) noexcept {
    __try {
        if(!command.soldier || !edf6vr::Readable(command.soldier,0x9C)) return;
        const auto* at=reinterpret_cast<const float*>(static_cast<const unsigned char*>(command.soldier)+0x90);
        float moved[3]{},travel=0;
        for(int j=0;j<3;++j) { moved[j]=at[j]-command.rootWorld[j]; travel+=moved[j]*moved[j]; }
        if(!std::isfinite(travel) || travel>=25) return;
        for(int j=0;j<3;++j) command.hand.palm[j]+=moved[j];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// Row 2 of the command the draw last placed this weapon with, which is the
// barrel as the player sees it. The shot is turned onto it rather than onto
// an aim read again a tick later.
bool FencerDrawnBarrel(unsigned hand,const void* weapon,float out[3]) noexcept;
std::atomic<unsigned long long> g_fencerStaleBarrel[2]{}; // the muzzle read back was our own write: last barrel used
float g_fencerBarrelReach[2]{};                           // the barrel's length, for the log
// The last barrel the game gave for each weapon, and the last muzzle point
// written into that weapon's transform. Keyed by weapon, not by hand: a
// Fencer carries two sets, and the one put away keeps whatever was last
// written into it.
struct FencerBarrelMemo { void* weapon=nullptr; float barrel[3]{}; float wrote[3]{}; bool haveBarrel=false,haveWrote=false; };
FencerBarrelMemo g_fencerBarrelMemo[8]{};
unsigned g_fencerBarrelNext=0;
FencerBarrelMemo& FencerBarrelFor(void* weapon) noexcept {
    for(auto& m:g_fencerBarrelMemo) if(m.weapon==weapon) return m;
    auto& m=g_fencerBarrelMemo[g_fencerBarrelNext++%8];
    m=FencerBarrelMemo{}; m.weapon=weapon;
    return m;
}
float g_fencerMapConfidence[2]{};
std::atomic<unsigned long long> g_fencerCarrySkips{0};   // carries skipped: the matrix still held what was written
// The soldier's own smoothed aim while the left tick has the shadow swapped
// in (the bullet initialiser reads 1240 and would take the shadow for the
// game's aim: the left spear's bullets were "turned" by 0 degrees).
thread_local bool g_fencerSwapLive=false;
thread_local float g_fencerSwapNative[4]{};
std::atomic<unsigned long long> g_fencerBulletAligned[2]{},g_fencerCarryAdopted{0};
// Bullets turned from the direction the game aimed them along before their cone
// (shot_origin.h, NoteSpreadInput): the cone kept, the game's own aim not.
std::atomic<unsigned long long> g_fencerBulletFromCone[2]{};
// Shoulder weapons that fire straight ahead, whose shells and guide line are
// turned onto the hand's aim (shot_origin.h, guide_probe.h): shells turned from
// their cone's input, guide computations, and the arcs and the last angle taken
// out. After each shot the barrel joint's animation pitches the weapon's own
// matrices 5 to 17 degrees down for a moment; the left shells, both guide lines
// and the landing marker went with it, while the barrel as drawn stays up
// (FENCERSHOTAIM, FENCERGUIDESPIKE, 2026-10-09; the user: "射撃後砲身とズレて赤い
// ガイド線が下を向いていました"; a player: the light mortar is off target after one
// shot in VR, after three or four on a flat screen).
std::atomic<unsigned long long> g_fencerShoulderFromCone[2]{},g_fencerShoulderGuides[2]{},g_fencerShoulderArcs[2]{};
std::atomic<float> g_fencerShoulderGuideDeg[2]{};
std::atomic<float> g_fencerBulletAlignDeg[2]{};
// Shots that fell outside the arc the alignment is willing to correct, and
// the worst angle seen. A hand weapon that is past it shoots along its raw
// carried matrix instead of along its own aim.
std::atomic<unsigned long long> g_fencerBulletFarOff[2]{};
std::atomic<float> g_fencerBulletFarOffDeg[2]{};
// The weapon's own forward against the two frames it could be posed in: the
// game's native aim, and the frame this hand is carried onto. The axis
// assignment is made by comparing the model against the NATIVE one, so if the
// left weapon is really posed along its own aim instead, that comparison is
// against the wrong frame and picks a perpendicular axis once the two aims
// are opened apart -- which is what 86 degrees on the left looks like.
std::atomic<float> g_fencerVsNativeDeg[2]{},g_fencerVsWantedDeg[2]{};
// A weapon's mount is a fixed property: votes are counted over the first 90
// updates after a weapon is first seen (the left arm's bones pass within
// 0.25 m of the left shoulder pod's root at times, and the hysteresis flipped
// it to "hand" for a while), the majority rules meanwhile, then it is frozen.
unsigned g_fencerMountSeen[2]{},g_fencerMountShoulderVotes[2]{};
constexpr unsigned kFencerMountDecideUpdates=90;
// The mount, from the game's own data: a weapon's root sits ON its constraint
// bone (ModelConstraint names a node of the soldier's skeleton), so the nearest
// of these named bones to the root says where it hangs -- the hands carry,
// anything else is a mount. Heights failed twice: the roots of a hand gun and
// a shoulder launcher overlap by stance, and a hammer's or a raised shield's
// transform rises above the eye. Resolved once per soldier by the game's node
// lookup, read each update.
constexpr int kFencerMountBoneCount=8;
constexpr const wchar_t* kFencerMountNames[kFencerMountBoneCount]={L"arms_l",L"arms_r",L"te_l",L"te_r",L"arms",L"kata_l",L"kata_r",L"weapon"};
constexpr const char* kFencerMountLabels[kFencerMountBoneCount]={"arms_l","arms_r","te_l","te_r","arms","kata_l","kata_r","weapon"};
constexpr bool kFencerMountIsHand[kFencerMountBoneCount]={true,true,true,true,false,false,false,false};
// The four hand bones lead the table: they are what tells a carried weapon
// from a shoulder mount. Without them the root's height has to decide on its
// own, and across the middle of its range it cannot, so the weapon keeps
// whatever it was last called -- which is how a hand gun ends up posed as a
// shoulder mount. The lookup can come up empty on the first frames of a new
// soldier (EnsureHandRig sees the same), so an incomplete answer is asked
// again a little later instead of being cached for the life of the soldier.
constexpr int kFencerMountHandBones=4;
constexpr unsigned kFencerMountAttempts=10;
struct FencerMountBones { void* soldier=nullptr; std::uint32_t objectId=0; void* node[kFencerMountBoneCount]{}; bool resolved=false;
    unsigned attempts=0; ULONGLONG retryAt=0; };
FencerMountBones g_fencerMountBones{};
int g_fencerMountNearest[2]{-1,-1}; float g_fencerMountDist[2]{};
void FencerResolveMountBones(void* soldier,std::uint32_t objectId) noexcept {
    auto& mount=g_fencerMountBones;
    const bool same=mount.soldier==soldier && mount.objectId==objectId;
    if(same && mount.resolved) return;
    const auto now=GetTickCount64();
    if(same && (mount.attempts>=kFencerMountAttempts || now<mount.retryAt)) return;
    if(!same) mount={};
    mount.soldier=soldier; mount.objectId=objectId;
    ++mount.attempts; mount.retryAt=now+500;
    // Only what is still missing: a bone already answered for this soldier is
    // kept, so a later attempt can add to the set but never shrink it.
    for(int i=0;i<kFencerMountBoneCount;++i)
        if(!mount.node[i]) mount.node[i]=edf6vr::FindNamedBone(soldier,g_nodeLookup,kFencerMountNames[i]);
    int hands=0;
    for(int i=0;i<kFencerMountHandBones;++i) if(mount.node[i]) ++hands;
    mount.resolved=hands==kFencerMountHandBones;
    if(mount.resolved) { if(mount.attempts>1) Log("FENCERMOUNT bones resolved on attempt %u",mount.attempts); return; }
    if(mount.attempts<kFencerMountAttempts) return;
    char missing[160]{}; std::size_t used=0;
    for(int i=0;i<kFencerMountBoneCount;++i)
        if(!mount.node[i] && used<sizeof(missing)-16)
            used+=std::snprintf(missing+used,sizeof(missing)-used,"%s ",kFencerMountLabels[i]);
    Log("FENCERMOUNT gave up after %u attempts; missing: %s; mount decided by root height alone",
        mount.attempts,missing[0]?missing:"none");
}
int FencerNearestMountBone(const float point[3],float& distance) noexcept {
    int best=-1; float bestSquare=1e9f;
    for(int i=0;i<kFencerMountBoneCount;++i) {
        const auto node=static_cast<const unsigned char*>(g_fencerMountBones.node[i]);
        if(!node) continue;
        __try {
            const auto row=reinterpret_cast<const float*>(node+0xB0+0x30);
            float square=0;
            for(int j=0;j<3;++j) square+=(row[j]-point[j])*(row[j]-point[j]);
            if(std::isfinite(square) && square<bestSquare) { bestSquare=square; best=i; }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    distance=best>=0?std::sqrt(bestSquare):0;
    return best;
}
const char* FencerMountBoneName(unsigned hand) noexcept {
    const int i=hand<2?g_fencerMountNearest[hand]:-1;
    return i>=0 && i<kFencerMountBoneCount?kFencerMountLabels[i]:"none";
}
// Weapon pairs seen: a change resets the gain table; counted so a pair that
// flickers (slots reordering, a set switch) shows in the log.
std::atomic<unsigned long long> g_fencerPairChanges{0};
float g_fencerRollCoefficient=0.3f; // the soldier's +0x1260 for this update, read in BuildFencerHands
std::atomic<unsigned long long> g_fencerMuzzleWrites{0};
struct FencerShadow { float target[4]{}; float smooth[4]{}; bool valid=false; };
SRWLOCK g_fencerShadowLock=SRWLOCK_INIT;
FencerShadow g_fencerShadow{};            // the left aim; update thread and weapon thread, under the lock
SRWLOCK g_fencerHandLock=SRWLOCK_INIT;
WeaponHoldCommand g_fencerLeft{},g_fencerRight{};   // copies for the weapon ticks, which never take g_lock
std::atomic<unsigned long long> g_fencerTickWrites[2]{};   // entry +0x50 matrices carried inside the weapon ticks / attachment updates
std::atomic<unsigned long long> g_fencerOwnMatrixCarries[2]{}; // weapon+0x150 carries
std::atomic<unsigned long long> g_fencerGuideUpdates{0},g_fencerGuideCarries{0}; // attachment OnUpdate hooks: calls, carries
std::atomic<bool> g_fencerActive{false};
std::atomic<void*> g_fencerWeapons[2]{};  // 0 left (hooked), 1 right (native, recorded for the log)
std::atomic<void*> g_fencerLeftModel{nullptr};
// The game's look per tick for the right hand's pad, read off the log as a
// dead zone of about 0.3 and a straight line to about 0.032 rad/tick at full
// deflection (pad 0.55 -> 0.0125, 0.84 -> 0.024, 1.0 -> 0.032). One slope per
// axis (0 pitch, 1 yaw) is what is learned: the MEDIAN of recent samples, so
// the ticks where the game hands back a fraction (the pitch clamp, a blocked
// aim, a stale pad against a fresh look) cannot drag it -- the per-bucket EMA
// they fed collapsed to a tenth of the value twice in play, and the left hand
// went heavy at large deflections with it.
constexpr float kFencerDeadzone=0.28f;   // the pursuit's own compensation (fencer_input.h); the game answered 0.286 with a small look
constexpr unsigned kFencerSlopeRing=32;
float g_fencerSlope[2][kFencerSlopeRing]{};
unsigned g_fencerSlopeCount[2]{},g_fencerSlopeNext[2]{};
float g_fencerPrevPad[2]{};
float g_fencerDeflection[2]{};   // EMA of |pad| (right, 1) and |pursuit| (left, 0): how far each aim trails its controller
std::atomic<unsigned long long> g_fencerTicks{0},g_fencerSwapped{0},g_fencerKicks{0},g_fencerInvalid{0},g_fencerBuilt{0},g_fencerRefused{0};
unsigned g_fencerSerial=0;
ULONGLONG g_fencerReportAt=0;
float g_fencerLastKick[3]{};
// Bullets of the left weapon turned to the left aim at their initialiser
// (shot_origin.h), the angle the last one was turned by, how far the game's
// own bullet forward (row 2 of its matrix) sat from the right aim for the
// right weapon (a check that row 2 is the direction), and how far the draw
// turned the left model (a check that the pose reaches the picture).
std::atomic<unsigned long long> g_fencerBulletTurns{0},g_fencerBulletRightChecks{0};
std::atomic<float> g_fencerBulletTurnDeg{0},g_fencerRightRow2Deg{0},g_fencerDrawTurnDeg{0};
// The aim, as the game's world forward: yaw about up, pitch up positive.
void FencerAimForward(float pitchGame,float yaw,float out[3]) noexcept {
    const float up=static_cast<float>(g_pitchSign)*pitchGame, flat=std::cos(up);
    out[0]=std::sin(yaw)*flat; out[1]=std::sin(up); out[2]=std::cos(yaw)*flat;
}
// Turns three basis rows by the shortest arc that takes `from` onto `to`.
bool FencerTurnRows(float rows[3][4],const float from[3],const float to[3]) noexcept {
    const float cosine=from[0]*to[0]+from[1]*to[1]+from[2]*to[2];
    if(!std::isfinite(cosine) || cosine<-.999f) return false;
    float axis[3]={from[1]*to[2]-from[2]*to[1],from[2]*to[0]-from[0]*to[2],from[0]*to[1]-from[1]*to[0]};
    const float span=std::sqrt(axis[0]*axis[0]+axis[1]*axis[1]+axis[2]*axis[2]);
    if(!std::isfinite(span)) return false;
    if(span<1e-6f) return true;   // already along it
    for(int j=0;j<3;++j) axis[j]/=span;
    // Rodrigues about a unit axis. The short form this used instead --
    // v + (k x v) + (k x (k x v))/(1+cos) -- is exact on paper but divides
    // by a number that goes to zero as the turn approaches a half circle:
    // x1 at a right angle, x2 at 120 degrees, x250 at 175. In float that
    // multiplies the rounding error by the same amount, which is why the
    // shot started wandering past about 90 degrees of split however far the
    // bound was opened. This form has no such term.
    const float angle=std::atan2(span,cosine),c=std::cos(angle),s=std::sin(angle);
    for(int r=0;r<3;++r) {
        const float v[3]={rows[r][0],rows[r][1],rows[r][2]};
        const float k[3]={axis[1]*v[2]-axis[2]*v[1],axis[2]*v[0]-axis[0]*v[2],axis[0]*v[1]-axis[1]*v[0]};
        const float along=axis[0]*v[0]+axis[1]*v[1]+axis[2]*v[2];
        for(int j=0;j<3;++j) rows[r][j]=v[j]*c+k[j]*s+axis[j]*along*(1-c);
    }
    return true;
}
// A shoulder weapon that fires straight ahead: a Weapon_HeavyShoot on this
// hand's shoulder whose launch vector is +Z. That vector is weapon+0x350, (0,0,1)
// unless the weapon data gives a FireVector (68CCFA), and it is what 688680 builds
// the guide from. Of every back-mounted weapon in the game data only the javelin
// catapults (h_attach_needle01) have one, (0,1,1): their 45-degree lob is
// designed and stays theirs. The mortars, cannons, gatlings and disruptors have
// none (AngleAdjust 0 everywhere), so all of their shot leaves along the aim.
// The missiles are Weapon_HomingShoot and are not this.
bool FencerStraightShoulder(unsigned hand,void* weapon) noexcept {
    if(hand>1 || !g_fencerShoulder[hand] || !weapon) return false;
    if(!edf6vr::HasType(g_image,weapon,".?AVWeapon_HeavyShoot@@")) return false;
    __try {
        if(!edf6vr::Readable(weapon,0x360)) return false;
        const auto* v=reinterpret_cast<const float*>(static_cast<unsigned char*>(weapon)+0x350);
        const float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        return std::isfinite(n) && n>1e-4f && v[2]/n>0.99985f;   // within a degree of +Z
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The aim a hand's shells are turned onto: the shadow's smoothed aim for the
// left, the soldier's for the right -- the saved one inside the left tick, where
// the soldier holds the shadow (shot_origin.h reads them the same way).
bool FencerHandAimForward(unsigned hand,void* soldier,float out[3]) noexcept {
    float smooth[4]{};
    if(hand==0) {
        FencerShadow shadow{};
        AcquireSRWLockShared(&g_fencerShadowLock); shadow=g_fencerShadow; ReleaseSRWLockShared(&g_fencerShadowLock);
        if(!shadow.valid) return false;
        smooth[0]=shadow.smooth[0]; smooth[1]=shadow.smooth[1];
    } else if(g_fencerSwapLive) {
        std::memcpy(smooth,g_fencerSwapNative,sizeof smooth);
    } else {
        __try {
            if(!soldier || !edf6vr::Readable(soldier,0x1250)) return false;
            std::memcpy(smooth,static_cast<unsigned char*>(soldier)+0x1240,sizeof smooth);
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    if(!std::isfinite(smooth[0]) || !std::isfinite(smooth[1])) return false;
    FencerAimForward(smooth[0],smooth[1],out);
    return true;
}

// The hand's up at the aim: the grip frame turned by the shortest arc from its
// own forward onto the aim, which is how the Ranger's weapon frame is built.
// A controller's up is defined however it is held -- straight up, or back
// over the shoulder -- and no up worked out from an aim direction is.
bool FencerHandUp(const edf6vr::Quat& grip,const float want[3],float up[3]) noexcept {
    auto world=[&](const edf6vr::Vec3& v) {
        return edf6vr::RotateY(edf6vr::XrToGame(edf6vr::QuatRotate(grip,v)),g_yawOffset);
    };
    const auto ahead=world({0,0,-1}),top=world({0,1,0});
    float rows[3][4]={{top.x,top.y,top.z,0},{ahead.x,ahead.y,ahead.z,0},{0,0,0,0}};
    const float from[3]={ahead.x,ahead.y,ahead.z};
    // No arc from a half turn away: the raw up, squared to the aim below.
    if(!FencerTurnRows(rows,from,want)) { rows[0][0]=top.x; rows[0][1]=top.y; rows[0][2]=top.z; }
    const float along=rows[0][0]*want[0]+rows[0][1]*want[1]+rows[0][2]*want[2];
    for(int j=0;j<3;++j) up[j]=rows[0][j]-want[j]*along;
    const float size=std::sqrt(up[0]*up[0]+up[1]*up[1]+up[2]*up[2]);
    if(!std::isfinite(size) || size<0.2f) return false;
    for(int j=0;j<3;++j) up[j]/=size;
    return true;
}
void ResetFencerDual() noexcept {
    g_fencerActive.store(false);
    for(auto& w:g_fencerWeapons) w.store(nullptr);
    g_fencerLeftModel.store(nullptr); g_fencerRightModel.store(nullptr);
    g_fencerRoll[0]=g_fencerRoll[1]=0; g_fencerRollTarget[0]=g_fencerRollTarget[1]=0;
    g_fencerRollPrevValid[0]=g_fencerRollPrevValid[1]=false;
    // The mount decision goes with it. It used to be cleared to "hand" while
    // the votes that made it were left standing, and because a weapon already
    // seen 90 times is never re-counted, the mount was then stuck on that
    // guess until the weapon itself changed. Any moment the Fencer state has
    // to be dropped -- being picked up and shaken by an ant is one, the pose
    // cannot be read while it lasts -- left the equipment wrong long after the
    // player was back on his feet. Cleared together, the next update that
    // succeeds classifies the weapon again at once.
    g_fencerShoulder[0]=g_fencerShoulder[1]=false; g_fencerBodyYawValid=false;
    for(unsigned h=0;h<2;++h) {
        g_fencerMountWeapon[h]=nullptr; g_fencerMountSeen[h]=0;
        g_fencerMountVotes[h]=0; g_fencerMountShoulderVotes[h]=0;
    }
    g_fencerAxisMap[0]=g_fencerAxisMap[1]=FencerAxisMap{};
    g_fencerPoseMemo[0]=g_fencerPoseMemo[1]=FencerPoseMemo{};
    AcquireSRWLockExclusive(&g_fencerHandLock); g_fencerLeft={}; g_fencerRight={}; ReleaseSRWLockExclusive(&g_fencerHandLock);
    AcquireSRWLockExclusive(&g_fencerShadowLock); g_fencerShadow={}; ReleaseSRWLockExclusive(&g_fencerShadowLock);
}
bool FencerDrawnBarrel(unsigned hand,const void* weapon,float out[3]) noexcept {
    if(hand>1 || !weapon) return false;
    WeaponHoldCommand drawn{};
    AcquireSRWLockShared(&g_fencerHandLock); drawn=hand==0?g_fencerLeft:g_fencerRight; ReleaseSRWLockShared(&g_fencerHandLock);
    if(!drawn.tracked || drawn.weapon!=weapon || GetTickCount64()-drawn.refreshed>250) return false;
    const float* z=drawn.hand.axes[2];
    const float size=std::sqrt(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]);
    if(!std::isfinite(size) || size<1e-3f) return false;
    for(int j=0;j<3;++j) out[j]=z[j]/size;
    return true;
}
bool FencerWeaponActive(void* weapon) noexcept {
    return weapon && g_fencerActive.load(std::memory_order_relaxed)
        && (weapon==g_fencerWeapons[0].load(std::memory_order_relaxed) || weapon==g_fencerWeapons[1].load(std::memory_order_relaxed));
}
void FencerNoteShot(unsigned) noexcept {}   // the recoil comes from the game's own vector, in the tick below
void FencerResetGain() noexcept {
    for(unsigned a=0;a<2;++a) { g_fencerSlopeCount[a]=g_fencerSlopeNext[a]=0; g_fencerPrevPad[a]=0; }
}
// A sample: a steady pad well beyond the dead zone (so the dead zone's own
// error is small) against the look the game added. The pad of the previous
// update is kept so a pad that has just changed is not paired with a look
// that answered the old one.
void FencerLearnGain(unsigned axis,float deflection,float look) noexcept {
    if(axis>1) return;
    const float previous=g_fencerPrevPad[axis]; g_fencerPrevPad[axis]=deflection;
    const float m=std::fabs(deflection);
    if(m<0.6f || !std::isfinite(look) || look==0 || std::fabs(deflection-previous)>0.15f) return;
    const float slope=look/std::copysign(m-kFencerDeadzone,deflection);
    if(!std::isfinite(slope) || std::fabs(slope)>0.5f || std::fabs(slope)<1e-4f) return;
    g_fencerSlope[axis][g_fencerSlopeNext[axis]++%kFencerSlopeRing]=slope;
    if(g_fencerSlopeCount[axis]<kFencerSlopeRing) ++g_fencerSlopeCount[axis];
}
float FencerSlopeFor(unsigned axis) noexcept {
    if(axis>1 || g_fencerSlopeCount[axis]<5) return -0.032f;   // the log's value, the sign the pad has
    float copy[kFencerSlopeRing]{};
    const unsigned n=g_fencerSlopeCount[axis];
    std::memcpy(copy,g_fencerSlope[axis],n*sizeof(float));
    std::nth_element(copy,copy+n/2,copy+n);
    return copy[n/2];
}
// Look per unit of deflection, so that deflection*gain is the game's look.
float FencerGainFor(unsigned axis,float deflection) noexcept {
    const float m=std::fabs(deflection);
    if(!std::isfinite(m) || m<=kFencerDeadzone) return 0;
    return FencerSlopeFor(axis)*(m-kFencerDeadzone)/m;
}
float FencerClampPitch(float pitch,float limit) noexcept {
    float p=std::clamp(pitch,-1.5707963f,1.5707963f);
    if(limit>=0 && std::isfinite(limit)) p=std::clamp(p,-limit,limit);
    return p;
}
// The frame of an aim direction: rows right, up, forward, with no roll.
void FencerAimFrame(const float fwd[3],float frame[3][3]) noexcept {
    float r[3]={fwd[2],0,-fwd[0]};
    const float l=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(l>1e-2f) { r[0]/=l; r[2]/=l; } else { r[0]=1; r[2]=0; }
    const float u[3]={fwd[1]*r[2]-fwd[2]*r[1],fwd[2]*r[0]-fwd[0]*r[2],fwd[0]*r[1]-fwd[1]*r[0]};
    for(int j=0;j<3;++j) { frame[0][j]=r[j]; frame[1][j]=u[j]; frame[2][j]=fwd[j]; }
}
// The same frame with the horizontal right taken from the aim's yaw, which
// stays put as the pitch passes vertical (the direction's own horizontal part
// vanishes there and its frame flipped: the roll on a shoulder weapon aimed
// past 90 degrees).
void FencerAimFrameYaw(const float fwd[3],float yaw,float frame[3][3]) noexcept {
    const float r[3]={std::cos(yaw),0,-std::sin(yaw)};
    float u[3]={fwd[1]*r[2]-fwd[2]*r[1],fwd[2]*r[0]-fwd[0]*r[2],fwd[0]*r[1]-fwd[1]*r[0]};
    // |fwd x r| is the sine of the angle between them, and it was being used
    // unnormalised. r is the horizontal at this yaw, so the two line up when
    // the aim is a right angle away from it -- the frame collapsed there and
    // took the axis assignment with it, which is the left weapon turning
    // over once the hands passed ninety degrees apart. Normalise, and where
    // there is nothing left to normalise fall back to the direction's own
    // frame rather than to a degenerate one.
    const float span=std::sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
    if(!(span>1e-3f)) { FencerAimFrame(fwd,frame); return; }
    for(int j=0;j<3;++j) u[j]/=span;
    // Right is rebuilt square to the pair, so the frame stays orthonormal
    // however far the aim has swung from the yaw it was named by.
    const float right[3]={u[1]*fwd[2]-u[2]*fwd[1],u[2]*fwd[0]-u[0]*fwd[2],u[0]*fwd[1]-u[1]*fwd[0]};
    for(int j=0;j<3;++j) { frame[0][j]=right[j]; frame[1][j]=u[j]; frame[2][j]=fwd[j]; }
}
// The weapon's rows re-expressed exactly from one aim frame to another: the
// coordinates of each row in the native aim's frame, put back on the wanted
// frame. No snapping, so a model whose barrel is tilted against the aim (a
// mortar, some 37 degrees up) keeps its tilt; with the same frame in and out
// the rows come back untouched. For the mounted weapons, whose mount has no
// arm animation to strip and whose axes need not align with the aim (snapping
// swung a mortar's muzzle between "forward" and "up" on hardware).
void FencerReframeRows(const float native[3][4],float from[3][3],float to[3][3],float rows[3][4]) noexcept {
    for(int i=0;i<3;++i) {
        float c[3]{};
        for(int k=0;k<3;++k) c[k]=native[i][0]*from[k][0]+native[i][1]*from[k][1]+native[i][2]*from[k][2];
        for(int j=0;j<3;++j) rows[i][j]=c[0]*to[0][j]+c[1]*to[1][j]+c[2]*to[2][j];
        rows[i][3]=0;
    }
}
// The weapon's rows rebuilt on a frame of the wanted direction. The mount's
// rows carry the arm animation's roll, and the shortest-arc turn added roll
// of its own when the two aims differed (the "pulled" roll on the
// controllers): so the native rows are snapped onto the axes of the native
// aim's frame (the model's own authoring axes, without the animation), and
// put back on the wanted frame, rolled by the controller when asked.
// A spear, told by its model. Every spear model (h_pile_*) has a node named
// "pile_attack", or "pile_top" on the twin spear, and none of the swung ones
// (h_impact_*: blades, axes, hammers, the katana) has either. The class cannot
// say: Weapon_Swing holds the Blast Hole Spear, the Spine Driver and the twin
// spear, and also the Denjinto katana (sgott data/6/weapon, 2026-09-26). The
// game's own node lookup is asked on the model's registry (model+0xA0, beside
// its node array at +0xB0), and the answer is kept per weapon.
struct FencerNodeName { wchar_t text[8]{}; std::uint64_t size=0,capacity=7; };
// The index of the node called `key` in the model's node array, or -1.
int FencerNodeIndex(void* model,const void* nodes,unsigned count,const wchar_t* key) noexcept {
    if(!g_nodeLookup || !model || !nodes || !key || !count) return -1;
    auto* bytes=static_cast<unsigned char*>(model);
    __try { if(*reinterpret_cast<const void* const*>(bytes+0xB0)!=nodes) return -1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
    FencerNodeName name{};
    name.size=wcslen(key);
    if(name.size<=7) wmemcpy(name.text,key,name.size);
    else { *reinterpret_cast<const wchar_t**>(name.text)=key; name.capacity=name.size; }
    std::uintptr_t node=0;
    __try { node=reinterpret_cast<std::uintptr_t>(g_nodeLookup(bytes+0xA0,&name)); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
    const auto begin=reinterpret_cast<std::uintptr_t>(nodes);
    if(!node || node<begin || node-begin>=static_cast<std::uintptr_t>(count)*0x110 || (node-begin)%0x110) return -1;
    return static_cast<int>((node-begin)/0x110);
}
bool FencerModelHasNode(void* model,const void* nodes,unsigned count,const wchar_t* key) noexcept {
    return FencerNodeIndex(model,nodes,count,key)>=0;
}
// What a weapon is, as told by its model, is asked of the model every time, not
// kept per weapon pointer for good. A weapon freed at the end of a mission and
// another made later at the same address inherited the old one's answer: on
// hardware 2026-10-02 the right hand's spear of one mission (weapon EFB9A750,
// a 5-node model) and the right hand's Power Blade of a later one got the same
// address, and the blade was taken for a spear -- laid like one, never swung,
// its sheath not split off, so the game's sheath animation played around the
// hand. A node lookup is the game's own registry find, cheap per draw.
//
// A lookup is only trusted when the model's node table is the one given and it
// knows "body" or "polymesh" (every weapon but the two pillows has one); for a
// frame where it cannot be asked, the last trusted answer for that weapon is
// used (FencerAnswers), and that is refreshed on every trusted ask.
bool FencerModelTrusted(void* model,const void* nodes,unsigned count) noexcept {
    return FencerModelHasNode(model,nodes,count,L"body") || FencerModelHasNode(model,nodes,count,L"polymesh");
}
struct FencerAnswer { std::atomic<const void*> weapon{nullptr}; std::atomic<int> value{-1}; };
struct FencerAnswers {
    FencerAnswer slot[8]{};
    std::atomic<unsigned> next{0};
    bool Find(const void* weapon,int& value) const noexcept {
        for(const auto& s:slot)
            if(weapon && s.weapon.load(std::memory_order_acquire)==weapon) { value=s.value.load(std::memory_order_relaxed); return true; }
        return false;
    }
    void Put(const void* weapon,int value) noexcept {
        if(!weapon) return;
        for(auto& s:slot)
            if(s.weapon.load(std::memory_order_acquire)==weapon) { s.value.store(value,std::memory_order_relaxed); return; }
        auto& s=slot[next.fetch_add(1,std::memory_order_relaxed)%8];
        s.weapon.store(nullptr,std::memory_order_release);
        s.value.store(value,std::memory_order_relaxed);
        s.weapon.store(weapon,std::memory_order_release);
    }
};
std::atomic<unsigned> g_fencerSpearsSeen{0};
// Back-mounted, told by the model: every h_attach_* model and the shoulder
// cannon (h_cannon_shoulder01) -- the ones sgott data/6/weapon attaches to
// "backWeapon" -- has a node "joint", "hatch" or "hatch_c_0", and no hand weapon
// has any of them (all 65 H_*.RAB models checked, 2026-09-26; the gatling on
// the back is Weapon_HeavyShoot, not Weapon_Gatling). 1 back, 0 hand, -1 when
// the lookup cannot be trusted: it must first find "body" or "polymesh", one of
// which every weapon but the two pillows has, and those are hand by class.
std::atomic<unsigned> g_fencerMountByModel[2]{};
int FencerBackMounted(void*,void* model,const void* nodes,unsigned count) noexcept {
    if(!FencerModelTrusted(model,nodes,count)) return -1;
    return FencerModelHasNode(model,nodes,count,L"joint") || FencerModelHasNode(model,nodes,count,L"hatch")
        || FencerModelHasNode(model,nodes,count,L"hatch_c_0")?1:0;
}
// Which back-mounted model (kFencerBackModels) this is, by its root node's
// name; -1 when none answers.
int FencerBackModel(void*,void* model,const void* nodes,unsigned count) noexcept {
    for(int i=0;i<kFencerBackModelCount;++i) if(FencerModelHasNode(model,nodes,count,kFencerBackModels[i])) return i;
    return -1;
}
// A Fencer blade's sheath: the node "attach", which the game ties to the
// soldier's backWeapon bone (sgott HWEAPON027 ModelConstraint: backWeapon ->
// attach) while the hilt rides the hand. Carried with the rest it swung with the
// sword (the user, 2026-10-02), so the draw leaves it where the game put it.
// Blades are the models with both "attach" and "slide" (h_impact_blade03,
// h_impact_618_blade04); h_cannon_titaniainferno01 has "attach" alone. -1 when
// the weapon is not a blade (asked each time, see FencerModelTrusted).
std::atomic<unsigned long long> g_fencerSheathsKept{0};
FencerAnswers g_fencerSheathAnswers;
int FencerBladeSheath(void* weapon,void* model,const void* nodes,unsigned count) noexcept {
    int was=-1;
    const bool known=g_fencerSheathAnswers.Find(weapon,was);
    if(!FencerModelTrusted(model,nodes,count)) return known?was:-1;
    const int index=FencerModelHasNode(model,nodes,count,L"slide")?FencerNodeIndex(model,nodes,count,L"attach"):-1;
    if(!known || was!=index) {
        g_fencerSheathAnswers.Put(weapon,index);
        if(index>0) Log("FENCERBLADE sheath node %d stays on backWeapon (weapon %p)",index,weapon);
    }
    return index;
}
// Where the sheath is drawn: on the player's shoulder, the way a back-mounted
// weapon is (FencerShoulderOnHead) -- the eye plus the shoulder offsets in the
// body's yaw frame, on the weapon's own side, turned as the back mounts sit
// (kFencerShoulderRest, which is the backWeapon bone's pose, and the sheath
// rides that bone). Left on the game's back mount, it went everywhere the
// swing clips took the body (hardware 2026-10-02: "電磁刀は鞘が動いていました").
// Written into the command in the frame of its hand (sheathAxes/sheathPos);
// false (and the game's mount) without the body's yaw or with the shoulders
// left on the game's mounts.
bool FencerSheathCommand(unsigned hand,WeaponHoldCommand& out) noexcept {
    out.sheathValid=false;
    if(hand>1 || !g_fencerShoulderOnHead || !g_fencerBodyYawValid) return false;
    float axes[3][3]{};
    for(int k=0;k<3;++k) {
        const float n=std::sqrt(out.hand.axes[k][0]*out.hand.axes[k][0]+out.hand.axes[k][1]*out.hand.axes[k][1]+out.hand.axes[k][2]*out.hand.axes[k][2]);
        if(!std::isfinite(n) || n<1e-3f) return false;
        for(int j=0;j<3;++j) axes[k][j]=out.hand.axes[k][j]/n;
    }
    // The body's yaw frame, columns of kFencerShoulderRest: left (+R, the
    // body's left on hardware), up, forward.
    const float body[3][3]={{std::cos(g_fencerBodyYaw),0,-std::sin(g_fencerBodyYaw)},{0,1,0},{std::sin(g_fencerBodyYaw),0,std::cos(g_fencerBodyYaw)}};
    const float side=hand==1?-g_fencerShoulderSide:g_fencerShoulderSide;
    float at[3]{};
    for(int j=0;j<3;++j) at[j]=g_eyeWorld[j]+body[0][j]*side-body[2][j]*g_fencerShoulderBack-body[1][j]*g_fencerShoulderDown;
    for(int i=0;i<3;++i) {
        float row[3]{};
        for(int j=0;j<3;++j) row[j]=kFencerShoulderRest[i][0]*body[0][j]+kFencerShoulderRest[i][1]*body[1][j]+kFencerShoulderRest[i][2]*body[2][j];
        for(int k=0;k<3;++k) out.sheathAxes[i][k]=row[0]*axes[k][0]+row[1]*axes[k][1]+row[2]*axes[k][2];
    }
    for(int k=0;k<3;++k) {
        float d=0; for(int j=0;j<3;++j) d+=(at[j]-out.hand.palm[j])*axes[k][j];
        out.sheathPos[k]=d;
    }
    out.sheathValid=true;
    return true;
}
// The draw: the sheath's palette matrix rebuilt against the hand as drawn,
// each row keeping its own length and the W lanes kept.
std::atomic<unsigned long long> g_fencerSheathsPlaced{0};
void FencerPlaceSheath(const WeaponHoldCommand& command,edf6vr::Matrix& m) noexcept {
    float axes[3][3]{};
    for(int k=0;k<3;++k) {
        const float n=std::sqrt(command.hand.axes[k][0]*command.hand.axes[k][0]+command.hand.axes[k][1]*command.hand.axes[k][1]+command.hand.axes[k][2]*command.hand.axes[k][2]);
        if(!std::isfinite(n) || n<1e-3f) return;
        for(int j=0;j<3;++j) axes[k][j]=command.hand.axes[k][j]/n;
    }
    edf6vr::Matrix out=m;
    for(int i=0;i<3;++i) {
        const float length=std::sqrt(m.m[i][0]*m.m[i][0]+m.m[i][1]*m.m[i][1]+m.m[i][2]*m.m[i][2]);
        if(!std::isfinite(length) || length<1e-4f) return;
        for(int j=0;j<3;++j) out.m[i][j]=(command.sheathAxes[i][0]*axes[0][j]+command.sheathAxes[i][1]*axes[1][j]+command.sheathAxes[i][2]*axes[2][j])*length;
    }
    for(int j=0;j<3;++j) {
        out.m[3][j]=command.hand.palm[j]+command.sheathPos[0]*axes[0][j]+command.sheathPos[1]*axes[1][j]+command.sheathPos[2]*axes[2][j];
        if(!std::isfinite(out.m[3][j])) return;
    }
    m=out;
    g_fencerSheathsPlaced.fetch_add(1,std::memory_order_relaxed);
}
// A back mount's barrel hangs from its node "joint" (root -> body -> joint ->
// the barrel's own parts, in every back model that has one), and the game ties
// that node to the Fencer's backWeaponJoint bone (sgott: ModelConstraint
// "backWeaponJoint" -> "joint"), apart from the root, which rides backWeapon.
// So holding the root still did nothing for the barrel: the jump pitched it
// down and the dash swung it -- the guide line with it -- through the joint
// alone (hardware, 9786E4DB, with the fixed root in use on every update). The
// joint and everything under it are turned about the joint so that it sits at
// FencerJointTarget on the root: the barrel along the aim, the weapon's own
// parts (a recoiling cannon, a spinning nozzle) still moving under it.
//
// The joint's subtree runs from the joint to the node before the last: the last
// node is the mesh, a child of the root (h_attach_* and h_cannon_shoulder01
// MDBs, 2026-09-26).
struct FencerJoint { int index=-1; unsigned end=0; };
FencerJoint FencerBackJoint(void*,void* model,const void* nodes,unsigned count) noexcept {
    FencerJoint joint{};
    const int index=FencerNodeIndex(model,nodes,count,L"joint");
    if(index>=1 && count>=3 && static_cast<unsigned>(index)<=count-2) { joint.index=index; joint.end=count-2; }
    return joint;
}
// The joint's rows wanted in the root's frame. The root keeps kFencerShoulderRest
// against the aim frame, and the joint is to lie along the aim frame itself
// (its +Z, the barrel, down the aim; its +Y up), so: kFencerShoulderRest turned over.
void FencerJointTarget(float target[3][3]) noexcept {
    for(int i=0;i<3;++i) for(int k=0;k<3;++k) target[i][k]=kFencerShoulderRest[k][i];
}
bool FencerUnitRows(const edf6vr::Matrix& m,float out[3][3]) noexcept {
    for(int k=0;k<3;++k) {
        const float n=std::sqrt(m.m[k][0]*m.m[k][0]+m.m[k][1]*m.m[k][1]+m.m[k][2]*m.m[k][2]);
        if(!std::isfinite(n) || n<1e-4f) return false;
        for(int j=0;j<3;++j) out[k][j]=m.m[k][j]/n;
    }
    return true;
}
// The world turn M (a vector v goes to v M) that takes the joint as posed onto
// `target` on this root, and how far that is.
bool FencerJointTurn(const edf6vr::Matrix& root,const edf6vr::Matrix& joint,const float target[3][3],float M[3][3],float* degrees=nullptr) noexcept {
    float R[3][3]{},J[3][3]{},T[3][3]{};
    if(!FencerUnitRows(root,R) || !FencerUnitRows(joint,J)) return false;
    for(int k=0;k<3;++k) for(int j=0;j<3;++j) T[k][j]=target[k][0]*R[0][j]+target[k][1]*R[1][j]+target[k][2]*R[2][j];
    for(int a=0;a<3;++a) for(int b=0;b<3;++b) M[a][b]=J[0][a]*T[0][b]+J[1][a]*T[1][b]+J[2][a]*T[2][b];
    const float trace=M[0][0]+M[1][1]+M[2][2];
    if(!std::isfinite(trace)) return false;
    if(degrees) *degrees=std::acos(std::clamp((trace-1.0f)*0.5f,-1.0f,1.0f))*57.29578f;
    return true;
}
void FencerTurnPoint(float point[3],const float M[3][3],const float pivot[3]) noexcept {
    const float d[3]={point[0]-pivot[0],point[1]-pivot[1],point[2]-pivot[2]};
    for(int b=0;b<3;++b) point[b]=pivot[b]+d[0]*M[0][b]+d[1]*M[1][b]+d[2]*M[2][b];
}
void FencerTurnMatrix(edf6vr::Matrix& m,const float M[3][3],const float pivot[3]) noexcept {
    for(int r=0;r<3;++r) {
        const float v[3]={m.m[r][0],m.m[r][1],m.m[r][2]};
        for(int b=0;b<3;++b) m.m[r][b]=v[0]*M[0][b]+v[1]*M[1][b]+v[2]*M[2][b];
    }
    FencerTurnPoint(m.m[3],M,pivot);
}
// The largest angle between matching rows of two matrices, in degrees.
float FencerRowsApart(const edf6vr::Matrix& a,const edf6vr::Matrix& b) noexcept {
    float A[3][3]{},B[3][3]{};
    if(!FencerUnitRows(a,A) || !FencerUnitRows(b,B)) return 180.0f;
    float worst=0;
    for(int k=0;k<3;++k) worst=std::max(worst,std::acos(std::clamp(A[k][0]*B[k][0]+A[k][1]*B[k][1]+A[k][2]*B[k][2],-1.0f,1.0f))*57.29578f);
    return worst;
}
std::atomic<unsigned long long> g_fencerJointDraws{0},g_fencerJointCarried{0},g_fencerJointCarryLeft{0},g_fencerJointBarrels{0};
// How far the game had swung the joint off its target, on foot and otherwise
// (update thread only): sum, count, largest.
double g_fencerJointTurnSum[2]{}; unsigned g_fencerJointTurnCount[2]{}; float g_fencerJointTurnMax[2]{};
int g_fencerJointIndexSeen[2]={-1,-1};
// Which of the root and the joint each carried weapon matrix (+0x150, the
// entry's +0x50 and +0x90) keeps a fixed relation to: how much its rotation
// relative to each changes from one carry to the next, averaged. The one it
// is built from barely changes. For the log only; the carry still goes by
// whichever it is nearer (FENCERJOINT ride=).
struct FencerRideWatch { const void* weapon=nullptr; float relRoot[3][3]{},relJoint[3][3]{}; bool have=false; float changeRoot=0,changeJoint=0; unsigned samples=0; };
FencerRideWatch g_fencerRide[2][3]{};
void FencerWatchRide(unsigned hand,unsigned slot,const void* weapon,const edf6vr::Matrix& m,const edf6vr::Matrix& root,const edf6vr::Matrix& joint) noexcept {
    if(hand>1 || slot>2) return;
    auto& w=g_fencerRide[hand][slot];
    if(w.weapon!=weapon) { w={}; w.weapon=weapon; }
    float M[3][3]{},R[3][3]{},J[3][3]{};
    if(!FencerUnitRows(m,M) || !FencerUnitRows(root,R) || !FencerUnitRows(joint,J)) return;
    float rr[3][3]{},rj[3][3]{};
    for(int i=0;i<3;++i) for(int k=0;k<3;++k) {
        rr[i][k]=M[i][0]*R[k][0]+M[i][1]*R[k][1]+M[i][2]*R[k][2];
        rj[i][k]=M[i][0]*J[k][0]+M[i][1]*J[k][1]+M[i][2]*J[k][2];
    }
    if(w.have) {
        float dr=0,dj=0;
        for(int i=0;i<3;++i) for(int k=0;k<3;++k) { dr=std::max(dr,std::fabs(rr[i][k]-w.relRoot[i][k])); dj=std::max(dj,std::fabs(rj[i][k]-w.relJoint[i][k])); }
        w.changeRoot+=(dr-w.changeRoot)*0.05f; w.changeJoint+=(dj-w.changeJoint)*0.05f; ++w.samples;
    }
    std::memcpy(w.relRoot,rr,sizeof(rr)); std::memcpy(w.relJoint,rj,sizeof(rj)); w.have=true;
}
// FencerGuideCheck's watch on each hand's guide line (beside the guide hook, below).
struct FencerGuideWatch { const void* weapon=nullptr; float baseline=0; unsigned samples=0; bool haveBaseline=false; float worst[2]{}; unsigned spikes=0; };
FencerGuideWatch g_fencerGuideWatch[2]{};
unsigned g_fencerGuideSpikeLines=0;
// The draw: `source` (the palette as posed) with the joint's subtree turned
// onto its target, into `out`. False leaves the weapon as it was.
bool FencerStraightenJoint(const WeaponHoldCommand& command,const edf6vr::Matrix* source,std::size_t count,edf6vr::Matrix* out) noexcept {
    if(command.jointIndex<1 || count!=command.count || command.jointEnd>=count
       || command.jointEnd<static_cast<unsigned>(command.jointIndex)) return false;
    float M[3][3]{};
    if(!FencerJointTurn(source[0],source[command.jointIndex],command.jointTarget,M)) return false;
    const float pivot[3]={source[command.jointIndex].m[3][0],source[command.jointIndex].m[3][1],source[command.jointIndex].m[3][2]};
    for(std::size_t b=0;b<count;++b) out[b]=source[b];
    for(unsigned b=static_cast<unsigned>(command.jointIndex);b<=command.jointEnd;++b) FencerTurnMatrix(out[b],M,pivot);
    g_fencerJointDraws.fetch_add(1,std::memory_order_relaxed);
    return true;
}
FencerAnswers g_fencerSpearAnswers;
bool FencerIsSpear(void* weapon,void* model,const void* nodes,unsigned count) noexcept {
    int was=0;
    const bool known=g_fencerSpearAnswers.Find(weapon,was);
    if(!FencerModelTrusted(model,nodes,count)) return known && was>0;
    const bool spear=FencerModelHasNode(model,nodes,count,L"pile_attack") || FencerModelHasNode(model,nodes,count,L"pile_top");
    if(!known || (was>0)!=spear) {
        g_fencerSpearAnswers.Put(weapon,spear?1:0);
        if(spear) g_fencerSpearsSeen.fetch_add(1,std::memory_order_relaxed);
    }
    return spear;
}
// The answer FencerIsSpear last gave for a weapon; false for one it has not
// seen. For the shot hook, which has no model or node table to hand and runs on
// the game's thread while the update refreshes the answer.
bool FencerKnownSpear(const void* weapon) noexcept {
    int was=0;
    return g_fencerSpearAnswers.Find(weapon,was) && was>0;
}
// --- Melee swings: the game's own swing, only while it is swung -------------
//
// A hammer, a blade or the katana is laid on the hand like a spear (the model's
// +Y up the controller, +Z ahead; these models stand along +Y from the grip).
// While the game plays its swing, it is drawn as the game swings it instead
// ("平常時はスピアと同じく手に固定、攻撃モーション中だけゲーム本来の振り回し",
// 2026-10-02): its turn in the game's aim frame AND its hand's travel since the
// swing began -- the clips are whole-body, and the arm carries it 1.5 m in a
// smash -- both put on the controller, and not held back by the dash
// smoothing, which froze it through the lunging attacks (the jump smash
// carries the soldier 13 m). The change in and out is blended.
//
// The swing is the weapon's own word for it. Every Fencer weapon asks for an
// arm animation through 6956C0 (the hammer: its charge state 6A0460 with layer
// 1 and a loop, its swing state 6A0790 with layer 0 and the stage's speed),
// which sets weapon+0xEC4/EC5 to 1, the clip at +0xED4, the speed at +0xED0,
// the layer at +0xEC8; the soldier's arm state (5A2310) plays it and clears
// +0xEC4 when the clip has run out, and the hammer's idle state (6A06E0) clears
// it too. A press with no ammo, or while reloading, never leaves the hammer's
// idle state (6A0736: the trigger, ammo +0xBE8 > 0), so it asks for nothing.
// The charge (layer 1) is not a swing: it only unfolds the weapon (its own
// bones, which the carry keeps), so the weapon stays laid on the hand.
//
// The other hand is left alone ("回転とかしなければ、固定する必要はない"): guns,
// spears and these are laid on their controllers, the known back mounts use the
// fixed table on the head (FencerRestPose), and the shield shows its guard or
// its lowered pose, both fixed on the controller (FencerShieldPose) -- none of
// which read the game's arm.
constexpr float kFencerSwingInSec=0.05f,kFencerSwingOutSec=0.12f;
enum FencerSwingKind { kFencerSwingNone=0,kFencerSwingHammer,kFencerSwingKatana };
struct FencerSwing {
    void* weapon=nullptr; int kind=kFencerSwingNone;
    bool active=false,haveAt=false;
    double at=0,since=0;
    float refAt[3]{},ref[3][3]{};
    float weight=0,shift[3]{};      // the fraction shown; the hand's travel since the start (aim frame)
    float maxTurn=0,maxShift=0;
};
FencerSwing g_fencerSwing[2]{};
std::atomic<unsigned long long> g_fencerSwings[2]{};
// The weapon's arm animation request (weapon+0xEC4) and its layer (+0xEC8: 0 the
// swing, 1 the hammer's charge).
bool FencerArmAnimation(void* weapon,int& layer) noexcept {
    layer=-1;
    if(!weapon) return false;
    __try {
        const auto* w=static_cast<const unsigned char*>(weapon);
        layer=*reinterpret_cast<const int*>(w+0xEC8);
        return w[0xEC4]!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Rows (unit axes) to a quaternion and back, the same convention both ways.
edf6vr::Quat FencerRowsQuat(const float r[3][3]) noexcept {
    edf6vr::Quat q{};
    const float t=r[0][0]+r[1][1]+r[2][2];
    if(t>0) {
        const float s=std::sqrt(t+1.0f)*2; q.w=0.25f*s; q.x=(r[1][2]-r[2][1])/s; q.y=(r[2][0]-r[0][2])/s; q.z=(r[0][1]-r[1][0])/s;
    } else if(r[0][0]>r[1][1] && r[0][0]>r[2][2]) {
        const float s=std::sqrt(1.0f+r[0][0]-r[1][1]-r[2][2])*2; q.w=(r[1][2]-r[2][1])/s; q.x=0.25f*s; q.y=(r[1][0]+r[0][1])/s; q.z=(r[2][0]+r[0][2])/s;
    } else if(r[1][1]>r[2][2]) {
        const float s=std::sqrt(1.0f+r[1][1]-r[0][0]-r[2][2])*2; q.w=(r[2][0]-r[0][2])/s; q.x=(r[1][0]+r[0][1])/s; q.y=0.25f*s; q.z=(r[2][1]+r[1][2])/s;
    } else {
        const float s=std::sqrt(1.0f+r[2][2]-r[0][0]-r[1][1])*2; q.w=(r[0][1]-r[1][0])/s; q.x=(r[2][0]+r[0][2])/s; q.y=(r[2][1]+r[1][2])/s; q.z=0.25f*s;
    }
    return q;
}
void FencerQuatRows(const edf6vr::Quat& q,float r[3][3]) noexcept {
    const float x=q.x,y=q.y,z=q.z,w=q.w;
    r[0][0]=1-2*(y*y+z*z); r[0][1]=2*(x*y+w*z);   r[0][2]=2*(x*z-w*y);
    r[1][0]=2*(x*y-w*z);   r[1][1]=1-2*(x*x+z*z); r[1][2]=2*(y*z+w*x);
    r[2][0]=2*(x*z+w*y);   r[2][1]=2*(y*z-w*x);   r[2][2]=1-2*(x*x+y*y);
}
// From rows a to rows b by w along the shorter turn, each row its length from
// `length`. False if either set is degenerate.
bool FencerBlendRows(const float a[3][3],const float b[3][3],float w,const float length[3],float out[3][3]) noexcept {
    const float one[3]={1,1,1};
    float ua[3][3]{},ub[3][3]{};
    if(!FencerSquareRows(a,one,ua) || !FencerSquareRows(b,one,ub)) return false;
    edf6vr::Quat qa=FencerRowsQuat(ua),qb=FencerRowsQuat(ub);
    float d=qa.x*qb.x+qa.y*qb.y+qa.z*qb.z+qa.w*qb.w;
    if(d<0) { qb={-qb.x,-qb.y,-qb.z,-qb.w}; d=-d; }
    float ka=1-w,kb=w;
    if(d<0.9995f) {
        const float angle=std::acos(std::clamp(d,-1.0f,1.0f)),s=std::sin(angle);
        ka=std::sin((1-w)*angle)/s; kb=std::sin(w*angle)/s;
    }
    edf6vr::Quat q{qa.x*ka+qb.x*kb,qa.y*ka+qb.y*kb,qa.z*ka+qb.z*kb,qa.w*ka+qb.w*kb};
    const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!(n>1e-6f)) return false;
    q={q.x/n,q.y/n,q.z/n,q.w/n};
    float r[3][3]{}; FencerQuatRows(q,r);
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) out[i][j]=r[i][j]*length[i];
    return true;
}
// The weapon root's place from the body, in the axes of `frame`. From the
// globalSRT bone rather than the soldier's position: whether the game moves the
// soldier through a lunge or only that bone, the lunge is not the arm's swing.
bool FencerBodyLocal(void* soldier,const float point[3],const float frame[3][3],float at[3]) noexcept {
    float rows[3][3]{},o[3]{};
    if(!soldier || !g_nodeLookup || !edf6vr::ReadNamedBoneFrame(soldier,g_nodeLookup,L"globalSRT",rows,o)) return false;
    for(int k=0;k<3;++k) {
        at[k]=0;
        for(int j=0;j<3;++j) at[k]+=(point[j]-o[j])*frame[k][j];
        if(!std::isfinite(at[k]) || std::fabs(at[k])>10) return false;
    }
    return true;
}
const char* FencerSwingName(int kind) noexcept {
    return kind==kFencerSwingHammer?"hammer":kind==kFencerSwingKatana?"katana":"none";
}
void FencerSwingEnd(unsigned hand,FencerSwing& s,double now) noexcept {
    Log("FENCERSWING hand=%c kind=%s length=%.2fs turn max=%.0fdeg hand travel max=%.2fm",
        hand==1?'R':'L',FencerSwingName(s.kind),now-s.since,s.maxTurn,s.maxShift);
    s.active=false;
}
// The watch for one hand (above). c: the weapon's rows in the game's aim frame;
// at: its root from the body in the same frame, or null. Returns the fraction
// of the game's swing to show.
float FencerSwingUpdate(unsigned hand,void* weapon,int kind,const float c[3][3],const float* at) noexcept {
    if(hand>1) return 0;
    auto& s=g_fencerSwing[hand];
    LARGE_INTEGER ticks{},rate{};
    QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&rate);
    const double now=rate.QuadPart?static_cast<double>(ticks.QuadPart)/static_cast<double>(rate.QuadPart):0.0;
    if(s.weapon!=weapon || s.kind!=kind) {
        if(s.active) FencerSwingEnd(hand,s,now);
        s=FencerSwing{}; s.weapon=weapon; s.kind=kind; s.at=now;
    }
    const double dt=now-s.at; s.at=now;
    int layer=-1;
    const bool swinging=FencerArmAnimation(weapon,layer) && layer!=1;
    if(swinging && !s.active) {
        s.active=true; s.since=now; s.maxTurn=0; s.maxShift=0;
        std::memcpy(s.ref,c,sizeof(s.ref)); s.haveAt=at!=nullptr;
        for(int k=0;k<3;++k) { s.refAt[k]=at?at[k]:0; s.shift[k]=0; }
        g_fencerSwings[hand].fetch_add(1,std::memory_order_relaxed);
    } else if(!swinging && s.active) FencerSwingEnd(hand,s,now);
    if(s.active) {
        float shift=0;
        if(at && s.haveAt) {
            for(int k=0;k<3;++k) { s.shift[k]=at[k]-s.refAt[k]; shift+=s.shift[k]*s.shift[k]; }
            shift=std::sqrt(shift);
        }
        s.maxTurn=std::max(s.maxTurn,FencerRowsAngle(c,s.ref)); s.maxShift=std::max(s.maxShift,shift);
    }
    const float target=s.active?1.0f:0.0f;
    if(!(dt>0) || dt>0.5) s.weight=target;
    else {
        const float tau=target>s.weight?kFencerSwingInSec:kFencerSwingOutSec;
        s.weight+=(target-s.weight)*(1.0f-std::exp(-static_cast<float>(dt)/tau));
        if(std::fabs(target-s.weight)<1e-3f) s.weight=target;
    }
    return s.weight;
}
bool FencerRebuildRows(const float native[3][4],const float nativeDir[3],float wanted[3][3],float rows[3][4],FencerAxisMap* map=nullptr,void* weapon=nullptr,unsigned hand=2) noexcept {
    float from[3][3]{}; FencerAimFrame(nativeDir,from);
    float c[3][3]{};
    for(int i=0;i<3;++i) for(int k=0;k<3;++k) c[i][k]=native[i][0]*from[k][0]+native[i][1]*from[k][1]+native[i][2]*from[k][2];
    int axisOf[3]={-1,-1,-1}; float sign[3]={1,1,1}; bool used[3]={};
    for(int pass=0;pass<3;++pass) {
        int bi=-1,bk=-1; float best=-1;
        for(int i=0;i<3;++i) if(axisOf[i]<0) for(int k=0;k<3;++k) if(!used[k] && std::fabs(c[i][k])>best) { best=std::fabs(c[i][k]); bi=i; bk=k; }
        if(bi<0 || !std::isfinite(best)) return false;
        axisOf[bi]=bk; used[bk]=true; sign[bi]=c[bi][bk]<0?-1.0f:1.0f;
    }
    {
        // parity of the permutation times the signs must be +1
        int inversions=0;
        for(int i=0;i<3;++i) for(int k=i+1;k<3;++k) if(axisOf[i]>axisOf[k]) ++inversions;
        float det=(inversions%2)?-1.0f:1.0f;
        for(int i=0;i<3;++i) det*=sign[i];
        if(det<0) {
            // Never on the row that carries the aim. Flipping it turns the
            // weapon end for end -- the left gatling was measured 177
            // degrees out, firing backwards, because its forward row was
            // the weakest correlation of the three and this picked it. A
            // flipped roll or up is a look; a flipped forward is a shot in
            // the wrong direction. Repair the handedness on one of the
            // other two and leave the barrel alone.
            int weakest=-1; float weak=2;
            for(int i=0;i<3;++i) {
                if(axisOf[i]==2) continue;
                const float m=std::fabs(c[i][axisOf[i]]);
                if(m<weak) { weak=m; weakest=i; }
            }
            if(weakest>=0) sign[weakest]=-sign[weakest];
        }
    }
    if(hand<2) {
        const auto angle=[](const float a[3],const float b[3]) {
            float d=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
            d=d<-1?-1:(d>1?1:d);
            return std::acos(d)*57.29578f;
        };
        const float forward[3]={native[2][0],native[2][1],native[2][2]};
        g_fencerVsNativeDeg[hand].store(angle(forward,nativeDir),std::memory_order_relaxed);
        g_fencerVsWantedDeg[hand].store(angle(forward,wanted[2]),std::memory_order_relaxed);
    }
    if(map) {
        if(map->weapon!=weapon) { *map=FencerAxisMap{}; map->weapon=weapon; }
        float confidence=1;
        for(int i=0;i<3;++i) confidence=std::min(confidence,std::fabs(c[i][axisOf[i]]));
        if(hand<2) g_fencerMapConfidence[hand]=confidence;
        if(map->frozen) {
            bool same=true;
            for(int i=0;i<3;++i) if(axisOf[i]!=map->axisOf[i] || sign[i]!=map->sign[i]) same=false;
            if(!same) g_fencerMapOverrides.fetch_add(1,std::memory_order_relaxed);
            // A frozen answer is worth keeping only while the model still
            // agrees with it. Each stored axis was frozen with the sign that
            // made its correlation positive, so a correlation that has gone
            // NEGATIVE means that axis now points the opposite way -- and
            // forcing it anyway is what fired the left gatling 178 degrees
            // round, backwards. Where the evidence has turned against the
            // frozen map, this frame uses what was just measured instead.
            float worst=2;
            for(int i=0;i<3;++i) {
                const float agree=c[i][map->axisOf[i]]*map->sign[i];
                if(agree<worst) worst=agree;
            }
            if(worst<0) g_fencerMapContradicted.fetch_add(1,std::memory_order_relaxed);
            else for(int i=0;i<3;++i) { axisOf[i]=map->axisOf[i]; sign[i]=map->sign[i]; }
        } else if(confidence>0.9f) {
            bool same=true;
            for(int i=0;i<3;++i) if(axisOf[i]!=map->candidate[i] || sign[i]!=map->candidateSign[i]) same=false;
            if(same) ++map->agree; else { map->agree=1; for(int i=0;i<3;++i) { map->candidate[i]=axisOf[i]; map->candidateSign[i]=sign[i]; } }
            if(map->agree>=30) {
                for(int i=0;i<3;++i) { map->axisOf[i]=axisOf[i]; map->sign[i]=sign[i]; }
                map->frozen=true; g_fencerMapFrozen.fetch_add(1,std::memory_order_relaxed);
            }
        } else map->agree=0;
    }
    if(map) {
        if(map->lastValid) {
            bool differs=false,holds=true;
            for(int i=0;i<3;++i) {
                if(axisOf[i]!=map->lastAxisOf[i] || sign[i]!=map->lastSign[i]) differs=true;
                if(c[i][map->lastAxisOf[i]]*map->lastSign[i]<0.55f) holds=false;
            }
            if(differs && holds) {
                for(int i=0;i<3;++i) { axisOf[i]=map->lastAxisOf[i]; sign[i]=map->lastSign[i]; }
                g_fencerMapHeld.fetch_add(1,std::memory_order_relaxed);
            }
        }
        for(int i=0;i<3;++i) { map->lastAxisOf[i]=axisOf[i]; map->lastSign[i]=sign[i]; }
        map->lastValid=true;
    }
    for(int i=0;i<3;++i) { for(int j=0;j<3;++j) rows[i][j]=sign[i]*wanted[axisOf[i]][j]; rows[i][3]=0; }
    return true;
}
edf6vr::Quat FencerQuatMul(const edf6vr::Quat& a,const edf6vr::Quat& b) noexcept {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
// One weapon's destination for the draw and the shot origin. The draw carries
// the weapon's bones relative to its root bone (the palette's first matrix, a
// copy of node 0's world matrix at +0xB0), so the destination is that root:
// turned by the arc from the soldier's smoothed aim (where the mount points
// it) to the direction wanted, and placed either where it is (mounted) or at
// the controller, with the Ranger's grip trims along the turned rows. The
// weapon transform (+0x150) is not this frame: it sits about 1.5 m off the
// root, and placing the root there hung the weapon out in front. Its
// translation is where the game's shot leaves, so it is kept as the muzzle
// point, in the root's frame, for the flash and the bullet origin.
bool MakeFencerCommand(unsigned hand,void* soldier,const edf6vr::PlayerPose& pose,const edf6vr::WeaponPose& wp,
                       const float nativeDir[3],float nativeYaw,const float wantDir[3],float wantYaw,WeaponHoldCommand& out,
                       const float* posedDir=nullptr) noexcept {
    // Which aim the game actually posed this weapon along. For the right it
    // is the game's own. For the left the shadow is swapped into the aim
    // field for the duration of its tick, so the left weapon comes out of the
    // game already pointing down the shadow -- and rebuilding it FROM the
    // native aim then turned it by the gap between the two a second time.
    // Open the hands and the weapon left the aim by exactly that gap; bring
    // them back and it snapped right in one frame, which is what gave it away.
    const float* const posedFrom=posedDir?posedDir:nativeDir;
    if(!wp.nodes || !wp.nodeCount || wp.nodeCount>kMaxWeaponBones || !edf6vr::HasType(g_image,wp.model,".?AVAnimationModel@@")) return false;
    edf6vr::Matrix root{};
    __try { root=*reinterpret_cast<const edf6vr::Matrix*>(static_cast<const unsigned char*>(wp.nodes)+0xB0); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    float turned[3][4]={{root.m[0][0],root.m[0][1],root.m[0][2],0},{root.m[1][0],root.m[1][1],root.m[1][2],0},{root.m[2][0],root.m[2][1],root.m[2][2],0}};
    // Read before anything is turned: the barrel in the model's own frame.
    //
    // The transform's translation is written back below: the muzzle, moved to
    // the hand. Wherever the game has not posed the weapon again since -- a
    // reload is the suspect, from the gun that stayed pointing up after one --
    // what comes back is our own point, and measured against the game's root
    // it is no barrel at all; the arc would aim whatever it is, write the
    // result back, and read that next time. Our own point is recognised
    // exactly, and then the last barrel the game gave this weapon is used.
    // A back mount's barrel, straightened (FencerBackJoint). The game's weapon
    // transform is turned with it when it rides the joint rather than the root,
    // so the muzzle -- where the flash is drawn and the shot starts -- stays on
    // the barrel as it is drawn.
    FencerJoint joint{}; float jointTurn[3][3]{},jointPivot[3]{}; bool straighten=false,transformOnJoint=false;
    if(hand<2 && FencerBackMounted(wp.weapon,wp.model,wp.nodes,static_cast<unsigned>(wp.nodeCount))==1) {
        joint=FencerBackJoint(wp.weapon,wp.model,wp.nodes,static_cast<unsigned>(wp.nodeCount));
        g_fencerJointIndexSeen[hand]=joint.index;
        if(joint.index>=0) {
            __try {
                const auto jm=*reinterpret_cast<const edf6vr::Matrix*>(static_cast<const unsigned char*>(wp.nodes)+static_cast<std::size_t>(joint.index)*0x110+0xB0);
                float target[3][3]{}; FencerJointTarget(target);
                float degrees=0;
                straighten=FencerJointTurn(root,jm,target,jointTurn,&degrees);
                for(int j=0;j<3;++j) jointPivot[j]=jm.m[3][j];
                if(straighten && std::isfinite(degrees)) {
                    const int band=g_fencerSettled?0:1;
                    g_fencerJointTurnSum[band]+=degrees; ++g_fencerJointTurnCount[band];
                    g_fencerJointTurnMax[band]=std::max(g_fencerJointTurnMax[band],degrees);
                }
                transformOnJoint=straighten && FencerRowsApart(wp.world,jm)<FencerRowsApart(wp.world,root);
            } __except(EXCEPTION_EXECUTE_HANDLER) { straighten=false; transformOnJoint=false; }
        }
    }
    float barrelLocal[3]{}; bool haveBarrel=false;
    auto& memo=FencerBarrelFor(wp.weapon);
    bool ours=memo.haveWrote;
    for(int j=0;j<3 && ours;++j) if(wp.world.m[3][j]!=memo.wrote[j]) ours=false;
    if(ours && memo.haveBarrel) {
        for(int j=0;j<3;++j) barrelLocal[j]=memo.barrel[j];
        haveBarrel=true;
        if(hand<2) g_fencerStaleBarrel[hand].fetch_add(1,std::memory_order_relaxed);
    } else {
        float point[3]={wp.world.m[3][0],wp.world.m[3][1],wp.world.m[3][2]};
        if(transformOnJoint) { FencerTurnPoint(point,jointTurn,jointPivot); g_fencerJointBarrels.fetch_add(1,std::memory_order_relaxed); }
        __try { haveBarrel=edf6vr::WeaponLocalPoint(root,point,barrelLocal); }
        __except(EXCEPTION_EXECUTE_HANDLER) { haveBarrel=false; }
        if(haveBarrel && !ours) { for(int j=0;j<3;++j) memo.barrel[j]=barrelLocal[j]; memo.haveBarrel=true; }
    }
    if(haveBarrel && hand<2)
        g_fencerBarrelReach[hand]=std::sqrt(barrelLocal[0]*barrelLocal[0]+barrelLocal[1]*barrelLocal[1]+barrelLocal[2]*barrelLocal[2]);
    float palm[3]={root.m[3][0],root.m[3][1],root.m[3][2]};
    // Shoulder mount? Melee, shields and gatlings are always in the hands.
    // Otherwise the named bone nearest the root (FencerMountBones). A new
    // weapon is classified at once; a change for a weapon already seen needs
    // 30 consecutive updates (half a second). Unknown -> carried.
    if(hand<2) {
        bool vote=g_fencerShoulder[hand],decided=false;
        // The model says it outright, and is asked first. The bone-distance vote
        // below was taken while the weapon was still being swung into place, so a
        // mortar came out a hand weapon at some switches and followed the
        // controller -- pointing down in a jump, its guide line spinning.
        const int mounted=FencerBackMounted(wp.weapon,wp.model,wp.nodes,static_cast<unsigned>(wp.nodeCount));
        if(mounted>=0) {
            if(g_fencerMountWeapon[hand]!=wp.weapon || g_fencerShoulder[hand]!=(mounted==1)) g_fencerMountByModel[hand].fetch_add(1,std::memory_order_relaxed);
            g_fencerMountWeapon[hand]=wp.weapon; g_fencerShoulder[hand]=mounted==1;
            g_fencerMountSeen[hand]=kFencerMountDecideUpdates;
        } else if(edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_Swing@@") || edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_ImpactHammer@@")
           || edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_PileBanker@@") || edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_Shield@@")
           || edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_Gatling@@")) { vote=false; decided=true; }
        else {
            const float rootAt[3]={root.m[3][0],root.m[3][1],root.m[3][2]};
            g_fencerMountNearest[hand]=FencerNearestMountBone(rootAt,g_fencerMountDist[hand]);
            const int nearest=g_fencerMountNearest[hand];
            if(nearest>=0 && ((kFencerMountIsHand[nearest] && g_fencerMountDist[hand]<0.25f)
                              || (!kFencerMountIsHand[nearest] && g_fencerMountDist[hand]<0.6f))) { vote=!kFencerMountIsHand[nearest]; decided=true; }
            else {
                // No named bone under the root: the root's height against the
                // eye, only where it is unambiguous (hand-gun roots read
                // -0.32..-0.57 on hardware, shoulder roots -0.05..-0.39).
                const float dyRoot=root.m[3][1]-g_eyeWorld[1];
                if(std::isfinite(dyRoot)) { if(dyRoot>-0.28f) { vote=true; decided=true; } else if(dyRoot<-0.42f) { vote=false; decided=true; } }
            }
        }
        g_fencerMountDy[hand]=wp.world.m[3][1]-g_eyeWorld[1];
        if(mounted>=0) {
            // Decided by the model above; nothing to vote on.
        } else if(g_fencerMountWeapon[hand]!=wp.weapon) {
            g_fencerMountWeapon[hand]=wp.weapon; g_fencerShoulder[hand]=decided?vote:false;
            g_fencerMountVotes[hand]=decided?1:0; g_fencerMountShoulderVotes[hand]=(decided&&vote)?1:0; g_fencerMountSeen[hand]=1;
        } else if(g_fencerMountSeen[hand]<kFencerMountDecideUpdates) {
            ++g_fencerMountSeen[hand];
            if(decided) { ++g_fencerMountVotes[hand]; if(vote) ++g_fencerMountShoulderVotes[hand]; }
            if(g_fencerMountVotes[hand]) g_fencerShoulder[hand]=g_fencerMountShoulderVotes[hand]*2>g_fencerMountVotes[hand];
        }
    }
    const bool shoulder=hand<2 && g_fencerShoulder[hand];
    if(g_fencerHandWeapons && !shoulder) {
        float at[3]{},q[4]{};
        if(!edf6vr::g_openxr.GripPose(static_cast<int>(hand),at,q) || !edf6vr::NormalizedQuat({q[0],q[1],q[2],q[3]})) return false;
        const auto room=edf6vr::XrToGame({at[0],at[1],at[2]});
        const auto offset=edf6vr::RotateY({room.x-g_lastHeadXr.x,room.y-g_lastHeadXr.y,room.z-g_lastHeadXr.z},g_yawOffset);
        const float grip[3]={g_eyeWorld[0]+offset.x,g_eyeWorld[1]+offset.y,g_eyeWorld[2]+offset.z};
        // The wanted frame: the aim's, with the controller's own up when the
        // weapon rolls with the hand (FencerHandRoll), upright otherwise.
        float frame[3][3]{}; FencerAimFrameYaw(wantDir,wantYaw,frame);
        const float upright[2][3]={{frame[0][0],frame[0][1],frame[0][2]},{frame[1][0],frame[1][1],frame[1][2]}};
        if(g_fencerHandRoll && hand<2) {
            // The up is the controller's own.
            //
            // It used to be a roll angle measured against the world's up and
            // kept by adding up each update's twist, and within 45 degrees of
            // vertical the world's up says nothing about roll, so there it ran
            // on the sum alone and drifted: the slow roll at the sky. Nothing
            // about a controller's up is undefined at any angle.
            float up[3]{};
            if(FencerHandUp({q[0],q[1],q[2],q[3]},wantDir,up)) {
                for(int j=0;j<3;++j) frame[1][j]=up[j];
                // right = up x forward (the frame's own convention)
                frame[0][0]=frame[1][1]*wantDir[2]-frame[1][2]*wantDir[1];
                frame[0][1]=frame[1][2]*wantDir[0]-frame[1][0]*wantDir[2];
                frame[0][2]=frame[1][0]*wantDir[1]-frame[1][1]*wantDir[0];
                g_fencerRoll[hand]=std::atan2(up[0]*upright[0][0]+up[1]*upright[0][1]+up[2]*upright[0][2],
                                              up[0]*upright[1][0]+up[1]*upright[1][1]+up[2]*upright[1][2]);
            }
        }
        // The weapon's own axes on the hand's: row 2, the model's +Z, down the
        // aim; row 1, its +Y, along the controller's up; row 0 square to both.
        // That is the Ranger's weapon exactly, and nothing about it is read
        // from the game's pose.
        //
        // The barrel used to be the line from the weapon's root to its muzzle,
        // turned onto the aim. On a gatling the root is the handle, well off
        // the barrel's own line, and that line ran 27 degrees from the way the
        // gun faces (the log's rightRow2vsAim): the barrel pointed high and
        // the shots, which go along the aim, landed low. The length of each
        // row is kept, in case a model is scaled.
        //
        // Not the shield. Laid on the hand the shield faced the front, but it
        // no longer rose to guard or dropped to recover, and those are how the
        // player tells whether it is up; it takes one of its two fixed poses,
        // guard or lowered, on the hand's frame (FencerShieldPose). A spear only
        // thrusts along the aim, so it is laid on the hand like a gun ("spears
        // can be fixed"): the Flashing Spear is Weapon_PileBanker, the others
        // are Weapon_Swing and told from the katana that shares the class by
        // their model (FencerIsSpear). As a posed weapon the Blast Hole Spear
        // trailed every dash like the shield (2026-09-26). The blades, hammers
        // and the katana are laid on the hand too, and swing as the game swings
        // them while they are swung (FencerSwingUpdate).
        const bool shield=edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_Shield@@");
        const bool hammer=edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_ImpactHammer@@");
        const bool melee=hammer || (edf6vr::HasType(g_image,wp.weapon,".?AVWeapon_Swing@@")
            && !FencerIsSpear(wp.weapon,wp.model,wp.nodes,static_cast<unsigned>(wp.nodeCount)));
        float swingRows[3][3]{},swingShift[3]{},swingWeight=0;
        if(melee && hand<2) {
            float posed[3][3]{};
            FencerAimFrameYaw(nativeDir,nativeYaw,posed);
            float c[3][3]{},fromBody[3]{};
            for(int i=0;i<3;++i) for(int k=0;k<3;++k)
                c[i][k]=turned[i][0]*posed[k][0]+turned[i][1]*posed[k][1]+turned[i][2]*posed[k][2];
            const float point[3]={root.m[3][0],root.m[3][1],root.m[3][2]};
            const bool haveBody=FencerBodyLocal(soldier,point,posed,fromBody);
            swingWeight=FencerSwingUpdate(hand,wp.weapon,hammer?kFencerSwingHammer:kFencerSwingKatana,c,haveBody?fromBody:nullptr);
            // The game's swing on the hand: its turn in the aim frame, and its
            // hand's travel since the press, both on the hand's frame.
            const auto& sw=g_fencerSwing[hand];
            for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
                swingRows[i][j]=c[i][0]*frame[0][j]+c[i][1]*frame[1][j]+c[i][2]*frame[2][j];
                swingShift[j]+=sw.shift[i]*frame[i][j]*swingWeight;
            }
        }
        if(shield) {
            // A shield is re-expressed rather than snapped, the same cure the
            // shoulder weapons already have.
            //
            // The snap picks, out of 24 whole-axis mappings, whichever is nearest
            // at that moment, and a shield is a plate: two of its axes are close
            // enough in length for the choice to swap, so it braced forward one
            // time and lay flat to the side the next (the log: 67 freezes in a
            // session and 122332 updates where the frozen mapping disagreed with
            // the live one). That is the same flip that swung a mortar's muzzle
            // between forward and up, and re-expressing is what cured it there:
            // the rows are taken out of the game's aim frame and put on the
            // hand's, with no choice to get wrong.
            //
            // It is worth more than it looks. A block is decided from the aim
            // alone -- 599650 -> 5957C0 reads soldier+0x1244 and the direction the
            // hit came from, and never the weapon -- so a shield that LOOKS wrong
            // costs the player nothing directly, but they will turn the reticle to
            // put it right, and the reticle is the thing that was covering them.
            // A cosmetic flip in the shield is a real hole in the guard.
            //
            // The frame it is taken out of is the game's one aim, for either
            // hand. The shield is posed by the body's arm animation, and that
            // follows the soldier's aim, not the shadow swapped in for the left
            // weapon's tick: taken out of the shadow's frame, the left shield
            // turned with the right hand. The yaw keeps the frame steady past
            // vertical, where the direction alone has no horizontal to go by.
            float posed[3][3]{};
            FencerAimFrameYaw(nativeDir,nativeYaw,posed);
            float c[3][3]{};
            for(int i=0;i<3;++i) for(int k=0;k<3;++k)
                c[i][k]=turned[i][0]*posed[k][0]+turned[i][1]*posed[k][1]+turned[i][2]*posed[k][2];
            if(hand<2) {
                std::memcpy(g_fencerShieldNative[hand],c,sizeof(c)); g_fencerShieldSeen[hand]=true;
                FencerShieldPose(hand,wp.weapon,c);
            }
            for(int i=0;i<3;++i) {
                for(int j=0;j<3;++j) turned[i][j]=c[i][0]*frame[0][j]+c[i][1]*frame[1][j]+c[i][2]*frame[2][j];
                turned[i][3]=0;
            }
        } else if(melee) {
            // Laid on the hand as below, the game's swing blended over it.
            float length[3]{},laid[3][3]{},rows[3][3]{};
            for(int k=0;k<3;++k) {
                const float size=std::sqrt(turned[k][0]*turned[k][0]+turned[k][1]*turned[k][1]+turned[k][2]*turned[k][2]);
                length[k]=std::isfinite(size) && size>1e-3f?size:1.0f;
                for(int j=0;j<3;++j) laid[k][j]=frame[k][j]*length[k];
            }
            if(swingWeight>=1.0f) std::memcpy(rows,swingRows,sizeof(rows));
            else if(swingWeight<=0.0f || !FencerBlendRows(laid,swingRows,swingWeight,length,rows)) std::memcpy(rows,laid,sizeof(rows));
            for(int i=0;i<3;++i) { for(int j=0;j<3;++j) turned[i][j]=rows[i][j]; turned[i][3]=0; }
        } else if(g_fencerDirectAim) {
            for(int k=0;k<3;++k) {
                const float size=std::sqrt(turned[k][0]*turned[k][0]+turned[k][1]*turned[k][1]+turned[k][2]*turned[k][2]);
                const float scale=std::isfinite(size) && size>1e-3f?size:1.0f;
                for(int j=0;j<3;++j) turned[k][j]=frame[k][j]*scale;
            }
            if(hand<2) g_fencerDirectAimed[hand].fetch_add(1,std::memory_order_relaxed);
        } else if(!FencerRebuildRows(turned,posedFrom,frame,turned,hand<2?&g_fencerAxisMap[hand]:nullptr,wp.weapon,hand)) return false;
        // The Fencer's own trims along the upright frame. Hardware: +frame[0]
        // is the body's LEFT (v6 moved the hands inward), so the right hand
        // goes along -frame[0].
        const float side=hand==1?-g_fencerHandOutward:g_fencerHandOutward;
        for(unsigned j=0;j<3;++j) palm[j]=grip[j]+frame[2][j]*g_fencerHandAhead+upright[0][j]*side+upright[1][j]*g_fencerHandUp+swingShift[j];
    } else if(shoulder || g_fencerHandWeapons) {
        // The mount's pose relative to the game's aim, put on the wanted frame
        // through the shield's dash smoothing. Laying it straight on the aim
        // was tried (2026-09-25) and did not hold up on hardware: these are
        // not built along +Z the way the hand guns are. The root stays on the
        // game's mount, or moves to the player's shoulder: the eye plus the
        // offsets in the body's yaw frame (+R is the body's LEFT on hardware).
        float frame[3][3]{},nativeFrame[3][3]{};
        FencerAimFrameYaw(wantDir,wantYaw,frame); FencerAimFrameYaw(nativeDir,nativeYaw,nativeFrame);
        float c[3][3]{};
        for(int i=0;i<3;++i) for(int k=0;k<3;++k)
            c[i][k]=turned[i][0]*nativeFrame[k][0]+turned[i][1]*nativeFrame[k][1]+turned[i][2]*nativeFrame[k][2];
        if(shoulder) FencerRestPose(hand,wp.weapon,c,FencerBackModel(wp.weapon,wp.model,wp.nodes,static_cast<unsigned>(wp.nodeCount)),nativeDir[1]);
        else FencerSmoothPose(hand,wp.weapon,c);
        for(int i=0;i<3;++i) {
            for(int j=0;j<3;++j) turned[i][j]=c[i][0]*frame[0][j]+c[i][1]*frame[1][j]+c[i][2]*frame[2][j];
            turned[i][3]=0;
        }
        if(shoulder && g_fencerShoulderOnHead && g_fencerBodyYawValid) {
            const float R[3]={std::cos(g_fencerBodyYaw),0,-std::sin(g_fencerBodyYaw)};
            const float F[3]={std::sin(g_fencerBodyYaw),0,std::cos(g_fencerBodyYaw)};
            const float side=hand==1?-g_fencerShoulderSide:g_fencerShoulderSide;
            for(int j=0;j<3;++j) palm[j]=g_eyeWorld[j]+R[j]*side-F[j]*g_fencerShoulderBack;
            palm[1]-=g_fencerShoulderDown;
        }
    } else FencerTurnRows(turned,nativeDir,wantDir);
    out={};
    out.model=wp.model; out.weapon=wp.weapon; out.soldier=soldier;
    out.bodyNodes=pose.nodeArray; out.armsNode=pose.armsFound?pose.armsNode:pose.nodeArray;
    out.weaponNodes=wp.nodes; out.count=wp.nodeCount; out.objectId=pose.objectId;
    out.serial=++g_fencerSerial; out.refreshed=GetTickCount64(); out.updateThread=GetCurrentThreadId();
    for(unsigned j=0;j<3;++j) {
        out.hand.palm[j]=palm[j];
        out.handPos[j]=palm[j];
        for(unsigned i=0;i<3;++i) out.hand.axes[i][j]=turned[i][j];
        out.weaponWas[j]=wp.world.m[3][j];
        out.eyeWorld[j]=g_eyeWorld[j];
        out.rootWorld[j]=g_lastAim.position[j];
        out.step[j]=g_soldierStep[j];
    }
    for(unsigned k=0;k<3;++k) for(unsigned j=0;j<3;++j) out.handAxes[k][j]=root.m[k][j];
    if(FencerBladeSheath(wp.weapon,wp.model,wp.nodes,static_cast<unsigned>(wp.nodeCount))>0) FencerSheathCommand(hand,out);
    out.headRoom[0]=g_lastHeadXr.x; out.headRoom[1]=g_lastHeadXr.y; out.headRoom[2]=g_lastHeadXr.z;
    out.yawOffset=g_yawOffset;
    out.tracked=true; out.handIndex=hand; out.fencer=true; out.latch=false;
    if(straighten) { out.jointIndex=joint.index; out.jointEnd=joint.end; FencerJointTarget(out.jointTarget); }
    // The same barrel the aim was turned along, so it carries the same guard.
    out.muzzleLocalValid=haveBarrel;
    for(int j=0;j<3;++j) out.muzzleLocal[j]=barrelLocal[j];
    // The weapon transform's translation is where the game draws the flash and
    // starts the bullet, so it is moved to the carried muzzle the way the
    // Ranger's is (plugin.cpp), within the same reach of the soldier's root.
    if(g_fencerHandWeapons && out.muzzleLocalValid) {
        float muzzle[3]{},away=0;
        if(edf6vr::PlaceWeaponLocalPoint(out.hand,out.muzzleLocal,muzzle)) {
            for(int j=0;j<3;++j) away+=(muzzle[j]-g_eyeWorld[j])*(muzzle[j]-g_eyeWorld[j]);
            if(std::isfinite(away) && std::sqrt(away)<kWeaponMuzzleReach && edf6vr::WriteWeaponPosition(wp,muzzle)) {
                g_fencerMuzzleWrites.fetch_add(1,std::memory_order_relaxed);
                for(int j=0;j<3;++j) memo.wrote[j]=muzzle[j];
                memo.haveWrote=true;
            }
            else if(hand<2) g_fencerMuzzleFar[hand].fetch_add(1,std::memory_order_relaxed);
        }
    }
    return true;
}
// Under g_lock, at the end of the camera update. Advances the left aim by one
// tick the way the game advances the right, and builds the left weapon's
// draw command on its mount.
void BuildFencerHands(void* soldier,const edf6vr::PlayerPose& pose) noexcept {
    if(!g_fencerSplitAim || edf6vr::WeaponSlotCount(soldier)!=2) { ResetFencerDual(); return; }
    edf6vr::SoldierAim aim{};
    if(!edf6vr::ReadSoldierAim(soldier,aim)) { ResetFencerDual(); return; }
    {
        // How fast he is going across the ground, for the shield's dash hold.
        LARGE_INTEGER ticks{},rate{};
        QueryPerformanceCounter(&ticks); QueryPerformanceFrequency(&rate);
        const double now=rate.QuadPart?static_cast<double>(ticks.QuadPart)/static_cast<double>(rate.QuadPart):0.0;
        const double dt=now-g_fencerPrevAt;
        const ULONGLONG tick=GetTickCount64();
        // The press: the grip of a hand whose weapon dashes or jumps.
        static bool gripWas[2]{};
        for(unsigned h=0;h<2;++h) {
            const bool down=g_gripHeld[h];
            if(down && !gripWas[h]) {
                const int type=FencerSecondaryType(g_fencerWeapons[h].load(std::memory_order_relaxed));
                g_fencerSecondaryType[h]=type;
                if(type==1) g_fencerZoomHand=static_cast<int>(h);
                if(type==4 || type==5) {
                    g_fencerDashUntil=std::max(g_fencerDashUntil,tick+kFencerActionMinMs);
                    g_fencerActions[type==5?0:1].fetch_add(1,std::memory_order_relaxed);
                }
            }
            gripWas[h]=down;
        }
        {
            static ULONGLONG downUntil=0;
            float head[3]{};
            if(g_nodeLookup && edf6vr::ReadNamedBone(soldier,g_nodeLookup,L"head",head)) {
                const float height=head[1]-aim.position[1];
                if(std::isfinite(height) && height<kFencerDownHead) {
                    if(tick>=downUntil) g_fencerDowns.fetch_add(1,std::memory_order_relaxed);
                    downUntil=tick+kFencerDownTailMs;
                }
            }
            g_fencerDown=tick<downUntil;
        }
        if(g_fencerPrevAt>0 && dt>0.004 && dt<0.5) {
            const float dx=aim.position[0]-g_fencerPrevPos[0], dz=aim.position[2]-g_fencerPrevPos[2];
            const float dy=aim.position[1]-g_fencerPrevPos[1];
            const float speed=static_cast<float>(std::sqrt(dx*dx+dz*dz)/dt);
            const float climb=static_cast<float>(std::fabs(dy)/dt);
            if(std::isfinite(speed)) {
                g_fencerSpeed=speed; if(speed>g_fencerSpeedPeak) g_fencerSpeedPeak=speed;
                if(speed>kFencerDashSpeed) g_fencerDashUntil=std::max(g_fencerDashUntil,tick+kFencerDashTailMs);
                // A jump (or a dash) under way keeps its hold until he lands and slows.
                else if(tick<g_fencerDashUntil && std::isfinite(climb) && climb>kFencerAirSpeed)
                    g_fencerDashUntil=std::max(g_fencerDashUntil,tick+kFencerActionTailMs);
                // A jump no press started (support gear gives its own): rising or falling fast.
                if(std::isfinite(climb) && climb>kFencerJumpSpeed)
                    g_fencerDashUntil=std::max(g_fencerDashUntil,tick+kFencerActionMinMs);
                // On his feet: on the ground, no dash or jump hold, not firing. What a
                // shoulder weapon is learned from (FencerRestPose), standing or walking.
                g_fencerSettled=speed<kFencerDashSpeed && std::isfinite(climb) && climb<0.3f && tick>=g_fencerDashUntil
                    && tick-g_playerFireAt.load(std::memory_order_relaxed)>300 && !g_fencerDown;
                g_fencerWalking=speed>=0.6f;
                g_fencerOnFeet=speed<kFencerDashSpeed && std::isfinite(climb) && climb<0.3f && tick>=g_fencerDashUntil && !g_fencerDown;
            }
        }
        for(int j=0;j<3;++j) g_fencerPrevPos[j]=aim.position[j];
        g_fencerPrevAt=now;
        const bool wasHeld=g_fencerDashHold;
        g_fencerDashHold=tick<g_fencerDashUntil;
        if(wasHeld && !g_fencerDashHold) g_fencerHoldEndedAt=now;
    }
    // What the game did with the right hand's deflection this tick.
    edf6vr::FencerPadCommand pad{};
    AcquireSRWLockShared(&g_fencerPadLock); pad=g_fencerPad; ReleaseSRWLockShared(&g_fencerPadLock);
    // Not the pitch while the right aim sits on its clamp: the game then adds
    // only what is left to the limit, and the pitch gain collapsed from it.
    const bool pitchClamped=aim.pitchLimit>=0 && std::isfinite(aim.pitchLimit) && std::fabs(aim.pitch)>=aim.pitchLimit-0.1f;
    if(pad.valid) {
        if(!pitchClamped) FencerLearnGain(0,pad.y,aim.lookInput[0]);
        FencerLearnGain(1,pad.x,aim.lookInput[1]);
        g_fencerDeflection[1]+=(std::fabs(pad.x)+std::fabs(pad.y)-g_fencerDeflection[1])*0.02f;
    }
    // The left controller, as the right is read for the aim write, trim and
    // all: a turn in the controller's own axes (TrimAimRotation), not an
    // amount added to the angles, so it holds the same place on the hand
    // pointed straight up as it does at the horizon.
    float at[3]{},q[4]{};
    edf6vr::HeadBasis hand{};
    if(!edf6vr::g_openxr.HandPose(0,at,q)
       || !edf6vr::HeadBasisFromXr(edf6vr::TrimAimRotation({q[0],q[1],q[2],q[3]},g_weaponYaw,g_weaponPitch),hand)) { ResetFencerDual(); return; }
    const float leftYaw=std::remainder(hand.yaw+g_yawOffset,6.28318530718f);
    const float leftPitch=static_cast<float>(g_pitchSign)*hand.pitchUp;
    float coefficient=0.3f;
    __try {
        const float c=*reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+0x1260);
        if(std::isfinite(c) && c>0 && c<=1) coefficient=c;
        g_fencerRollCoefficient=coefficient;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    FencerShadow shadow{};
    AcquireSRWLockShared(&g_fencerShadowLock); shadow=g_fencerShadow; ReleaseSRWLockShared(&g_fencerShadowLock);
    if(!shadow.valid) {
        shadow.target[0]=aim.pitch; shadow.target[1]=aim.yaw; shadow.target[2]=aim.third; shadow.target[3]=1;
        for(int l=0;l<4;++l) shadow.smooth[l]=shadow.target[l];
        shadow.valid=true;
    }
    if(g_fencerHeavyAim) {
        // 1230 += look: the pursuit's deflection toward the left controller,
        // through the gain the game gives the right's.
        const auto pursuit=edf6vr::FencerPursuit(shadow.target[0],shadow.target[1],leftPitch,leftYaw,GetTickCount64());
        g_fencerDeflection[0]+=(std::fabs(pursuit.x)+std::fabs(pursuit.y)-g_fencerDeflection[0])*0.02f;
        shadow.target[0]+=pursuit.y*FencerGainFor(0,pursuit.y);
        shadow.target[1]=std::remainder(shadow.target[1]+pursuit.x*FencerGainFor(1,pursuit.x),6.28318530718f);
        shadow.target[0]=FencerClampPitch(shadow.target[0],aim.pitchLimit);
        // 1240 += (1230-1240)*1260, the yaw the short way round.
        for(int l=0;l<3;++l) {
            float d=shadow.target[l]-shadow.smooth[l];
            if(l==1) d=std::remainder(d,6.28318530718f);
            shadow.smooth[l]+=d*coefficient;
        }
        shadow.smooth[1]=std::remainder(shadow.smooth[1],6.28318530718f);
    } else {
        shadow.target[0]=FencerClampPitch(leftPitch,aim.pitchLimit); shadow.target[1]=leftYaw;
        for(int l=0;l<3;++l) shadow.smooth[l]=shadow.target[l];
    }
    AcquireSRWLockExclusive(&g_fencerShadowLock); g_fencerShadow=shadow; ReleaseSRWLockExclusive(&g_fencerShadowLock);

    // Slot 1 is the weapon the left trigger fires (hardware-confirmed: slot 0
    // is the right). Both weapons get a command: the right's direction is the
    // soldier's smoothed aim (the game's), the left's is the shadow's.
    const unsigned leftSlot=g_fencerSwapHands?0u:1u;
    edf6vr::WeaponPose left{},right{};
    if(!edf6vr::ReadWeaponPose(soldier,leftSlot,left) || !edf6vr::ReadWeaponPose(soldier,1u-leftSlot,right)
       || left.weapon==right.weapon) { ResetFencerDual(); g_fencerRefused.fetch_add(1,std::memory_order_relaxed); return; }
    if(g_fencerGainPair[0]!=left.weapon || g_fencerGainPair[1]!=right.weapon) {
        g_fencerGainPair[0]=left.weapon; g_fencerGainPair[1]=right.weapon;
        FencerResetGain(); g_fencerPairChanges.fetch_add(1,std::memory_order_relaxed);
    }
    float smoothAim[4]{};
    __try { std::memcpy(smoothAim,static_cast<const unsigned char*>(soldier)+0x1240,16); }
    __except(EXCEPTION_EXECUTE_HANDLER) { ResetFencerDual(); return; }
    float nativeDir[3]{},shadowDir[3]{};
    FencerAimForward(smoothAim[0],smoothAim[1],nativeDir);
    FencerAimForward(shadow.smooth[0],shadow.smooth[1],shadowDir);
    // The zoom panel looks where the zooming hand's weapon aims (scope.h): the
    // only hand holding a zoom weapon, or the one whose grip last asked for it.
    // The right's aim is the game's, the left's the shadow's.
    {
        const bool zoomLeft=FencerSecondaryType(left.weapon)==1,zoomRight=FencerSecondaryType(right.weapon)==1;
        g_fencerScopeHand=zoomLeft!=zoomRight?(zoomLeft?0:1):g_fencerZoomHand;
        const float* scopeAim=g_fencerScopeHand==0?shadow.smooth:smoothAim;
        g_scopeAim[0]=scopeAim[0];g_scopeAim[1]=scopeAim[1];g_scopeAimAt=GetTickCount64();
    }
    WeaponHoldCommand leftCommand{},rightCommand{};
    FencerResolveMountBones(soldier,pose.objectId);
    {
        // The body's yaw: the head's, followed slowly (about a third of a
        // second), so a glance sideways does not swing the shoulders.
        const float headYaw=std::remainder(g_lastHeadYaw+g_yawOffset,6.28318530718f);
        if(!g_fencerBodyYawValid) { g_fencerBodyYaw=headYaw; g_fencerBodyYawValid=true; }
        else g_fencerBodyYaw=std::remainder(g_fencerBodyYaw+std::remainder(headYaw-g_fencerBodyYaw,6.28318530718f)*0.05f,6.28318530718f);
    }
    if(!MakeFencerCommand(0,soldier,pose,left,nativeDir,smoothAim[1],shadowDir,shadow.smooth[1],leftCommand,shadowDir)
       || !MakeFencerCommand(1,soldier,pose,right,nativeDir,smoothAim[1],nativeDir,smoothAim[1],rightCommand)) {
        ResetFencerDual(); g_fencerRefused.fetch_add(1,std::memory_order_relaxed); return;
    }
    g_leftHoldCommand=leftCommand; g_holdCommand=rightCommand;
    AcquireSRWLockExclusive(&g_fencerHandLock); g_fencerLeft=leftCommand; g_fencerRight=rightCommand; ReleaseSRWLockExclusive(&g_fencerHandLock);
    g_fencerWeapons[0].store(left.weapon); g_fencerWeapons[1].store(right.weapon);
    g_fencerLeftModel.store(left.model); g_fencerRightModel.store(right.model);
    g_fencerActive.store(true);
    g_fencerBuilt.fetch_add(1,std::memory_order_relaxed);
    const auto tick=GetTickCount64();
    if(tick-g_fencerReportAt>5000) {
        g_fencerReportAt=tick;
        const unsigned known=g_fencerSlopeCount[0]+g_fencerSlopeCount[1];
        Log("FENCERDUAL on heavy=%d left=%p right=%p built=%llu refused=%llu ticks=%llu swapped=%llu kicks=%llu invalid=%llu shots L/R=%llu/%llu bulletTurns=%llu lastTurn=%.1fdeg rightRow2vsAim=%.1fdeg(%llu) drawTurn=%.1fdeg hands=%d muzzleWrites=%llu muzzleFar L/R=%llu/%llu laserMatrixCarries=%llu bulletSkips=%llu tickWrites L/R=%llu/%llu ownMatrix L/R=%llu/%llu guideUpdates=%llu guideCarries=%llu carrySkips=%llu adopted=%llu aligned L/R=%llu/%llu(%.1f/%.1fdeg) fromCone L/R=%llu/%llu shoulderFromCone L/R=%llu/%llu farOff L/R=%llu(%.0f)/%llu(%.0f) vsNative L/R=%.0f/%.0f vsWanted L/R=%.0f/%.0f axisMap frozen=%llu overrides=%llu contradicted=%llu held=%llu direct L/R=%llu/%llu staleBarrel L/R=%llu/%llu barrel L/R=%.2f/%.2f conf L/R=%.2f/%.2f coef=%.3f gainSamples=%u gain(pitch,yaw)@full=%.4f,%.4f slope=%.4f,%.4f trail L/R=%.2f/%.2f mount L/R=%s/%s bone=%s(%.2f)/%s(%.2f) dy=%.2f/%.2f roll=%.0f/%.0f pairChanges=%llu left target=(%.1f,%.1f) smooth=(%.1f,%.1f) right=(%.1f,%.1f) lastKick=(%.4f,%.4f,%.4f)",
            g_fencerHeavyAim,left.weapon,right.weapon,g_fencerBuilt.load(),g_fencerRefused.load(),g_fencerTicks.load(),g_fencerSwapped.load(),
            g_fencerKicks.load(),g_fencerInvalid.load(),g_shotCount[0].load(),g_shotCount[1].load(),
            g_fencerBulletTurns.load(),g_fencerBulletTurnDeg.load(),g_fencerRightRow2Deg.load(),g_fencerBulletRightChecks.load(),g_fencerDrawTurnDeg.load(),g_fencerHandWeapons?1:0,g_fencerMuzzleWrites.load(),g_fencerMuzzleFar[0].load(),g_fencerMuzzleFar[1].load(),g_fencerTransformTurns.load(),g_fencerBulletSkips.load(),g_fencerTickWrites[0].load(),g_fencerTickWrites[1].load(),
            g_fencerOwnMatrixCarries[0].load(),g_fencerOwnMatrixCarries[1].load(),g_fencerGuideUpdates.load(),g_fencerGuideCarries.load(),g_fencerCarrySkips.load(),g_fencerCarryAdopted.load(),
            g_fencerBulletAligned[0].load(),g_fencerBulletAligned[1].load(),g_fencerBulletAlignDeg[0].load(),g_fencerBulletAlignDeg[1].load(),
            g_fencerBulletFromCone[0].load(),g_fencerBulletFromCone[1].load(),
            g_fencerShoulderFromCone[0].load(),g_fencerShoulderFromCone[1].load(),
            g_fencerBulletFarOff[0].load(),g_fencerBulletFarOffDeg[0].load(),g_fencerBulletFarOff[1].load(),g_fencerBulletFarOffDeg[1].load(),
            g_fencerVsNativeDeg[0].load(),g_fencerVsNativeDeg[1].load(),g_fencerVsWantedDeg[0].load(),g_fencerVsWantedDeg[1].load(),
            g_fencerMapFrozen.load(),g_fencerMapOverrides.load(),g_fencerMapContradicted.load(),g_fencerMapHeld.load(),g_fencerDirectAimed[0].load(),g_fencerDirectAimed[1].load(),g_fencerStaleBarrel[0].load(),g_fencerStaleBarrel[1].load(),g_fencerBarrelReach[0],g_fencerBarrelReach[1],g_fencerMapConfidence[0],g_fencerMapConfidence[1],coefficient,known,
            FencerGainFor(0,1.0f),FencerGainFor(1,1.0f),FencerSlopeFor(0),FencerSlopeFor(1),g_fencerDeflection[0],g_fencerDeflection[1],g_fencerShoulder[0]?"shoulder":"hand",g_fencerShoulder[1]?"shoulder":"hand",
            FencerMountBoneName(0),g_fencerMountDist[0],FencerMountBoneName(1),g_fencerMountDist[1],g_fencerMountDy[0],g_fencerMountDy[1],g_fencerRoll[0]*57.29578f,g_fencerRoll[1]*57.29578f,g_fencerPairChanges.load(),
            shadow.target[0]*57.29578f,shadow.target[1]*57.29578f,shadow.smooth[0]*57.29578f,shadow.smooth[1]*57.29578f,
            aim.pitch*57.29578f,aim.yaw*57.29578f,g_fencerLastKick[0],g_fencerLastKick[1],g_fencerLastKick[2]);
        Log("DASHSMOOTH speed=%.1f peak=%.1fm/s held=%llu (updates a posed weapon was held through a dash or jump) "
            "pressed dash=%llu jump=%llu secondaryType L=%d R=%d (weapon+0x690: 4 jump, 5 dash, 6 reflect) "
            "shoulderRest fixed=%llu learnt=%llu used=%llu reported=%llu settled=%d mountByModel L/R=%u/%u",
            g_fencerSpeed,g_fencerSpeedPeak,g_fencerPoseSlowed.load(),g_fencerActions[0].load(),g_fencerActions[1].load(),
            g_fencerSecondaryType[0],g_fencerSecondaryType[1],g_fencerRestFixed.load(),g_fencerRestLearnt.load(),g_fencerRestUsed.load(),g_fencerRestReported.load(),g_fencerSettled?1:0,
            g_fencerMountByModel[0].load(),g_fencerMountByModel[1].load());
        g_fencerSpeedPeak=0;
        Log("FENCERJOINT index L/R=%d/%d draws=%llu joint off its target, on foot avg/max=%.1f/%.1fdeg (%u) moving or in the air avg/max=%.1f/%.1fdeg (%u) "
            "weapon matrices on the joint/root=%llu/%llu barrelsTurned=%llu",
            g_fencerJointIndexSeen[0],g_fencerJointIndexSeen[1],g_fencerJointDraws.load(),
            g_fencerJointTurnCount[0]?g_fencerJointTurnSum[0]/g_fencerJointTurnCount[0]:0.0,g_fencerJointTurnMax[0],g_fencerJointTurnCount[0],
            g_fencerJointTurnCount[1]?g_fencerJointTurnSum[1]/g_fencerJointTurnCount[1]:0.0,g_fencerJointTurnMax[1],g_fencerJointTurnCount[1],
            g_fencerJointCarried.load(),g_fencerJointCarryLeft.load(),g_fencerJointBarrels.load());
        for(int b=0;b<2;++b) { g_fencerJointTurnSum[b]=0; g_fencerJointTurnCount[b]=0; g_fencerJointTurnMax[b]=0; }
        Log("FENCERRIDE change against root/joint per carry (the smaller is what it is built from): "
            "L +150=%.3f/%.3f +50=%.3f/%.3f +90=%.3f/%.3f R +150=%.3f/%.3f +50=%.3f/%.3f +90=%.3f/%.3f",
            g_fencerRide[0][0].changeRoot,g_fencerRide[0][0].changeJoint,g_fencerRide[0][1].changeRoot,g_fencerRide[0][1].changeJoint,
            g_fencerRide[0][2].changeRoot,g_fencerRide[0][2].changeJoint,g_fencerRide[1][0].changeRoot,g_fencerRide[1][0].changeJoint,
            g_fencerRide[1][1].changeRoot,g_fencerRide[1][1].changeJoint,g_fencerRide[1][2].changeRoot,g_fencerRide[1][2].changeJoint);
        Log("FENCERGUIDE off its usual angle, on foot/in a dash or jump: L=%.1f/%.1fdeg R=%.1f/%.1fdeg usual L/R=%.1f/%.1f spikes L/R=%u/%u "
            "straightened L/R=%llu/%llu last=%.1f/%.1fdeg arcs L/R=%llu/%llu",
            g_fencerGuideWatch[0].worst[0],g_fencerGuideWatch[0].worst[1],g_fencerGuideWatch[1].worst[0],g_fencerGuideWatch[1].worst[1],
            g_fencerGuideWatch[0].baseline,g_fencerGuideWatch[1].baseline,g_fencerGuideWatch[0].spikes,g_fencerGuideWatch[1].spikes,
            g_fencerShoulderGuides[0].load(),g_fencerShoulderGuides[1].load(),g_fencerShoulderGuideDeg[0].load(),g_fencerShoulderGuideDeg[1].load(),
            g_fencerShoulderArcs[0].load(),g_fencerShoulderArcs[1].load());
        for(auto& w:g_fencerGuideWatch) { w.worst[0]=w.worst[1]=0; }
        Log("SHIELDSTATE shown lowered/guard=%llu/%llu offAnchor=%llu downs=%llu trigger L/R=%.2f/%.2f"
            " guard tests=%llu on the left aim=%llu blocked there=%llu sites=%u/4",
            g_fencerShieldShown[0].load(),g_fencerShieldShown[1].load(),
            g_fencerRestOffAnchor.load(),g_fencerDowns.load(),g_handTrigger[0].load(),g_handTrigger[1].load(),
            g_fencerGuardTests.load(),g_fencerGuardLeft.load(),g_fencerGuardLeftBlocked.load(),g_fencerGuardSites);
        for(unsigned h=0;h<2;++h) if(g_fencerShieldSeen[h]) {
            const auto& c=g_fencerShieldNative[h];
            Log("SHIELDPOSE %s native rows in the aim frame (left,up,forward): x=(%.2f,%.2f,%.2f) y=(%.2f,%.2f,%.2f) z=(%.2f,%.2f,%.2f)",
                h?"R":"L",c[0][0],c[0][1],c[0][2],c[1][0],c[1][1],c[1][2],c[2][0],c[2][1],c[2][2]);
            g_fencerShieldSeen[h]=false;
        }
    }
}
// The weapon's world matrices, carried from its native root onto the hand the
// model is drawn with (the bones' own carry): weapon+0x150, the weapon's own
// matrix, from which the fire path rebuilds the muzzle attachment
// (6904D2/694873 -> g_dualPose(entry, weapon+0x150)) -- the left mortar's
// shells and flash were born at the game's mount because only the entry was
// carried and the rebuild overwrote it -- and the first attachment entry's
// two matrices (+0x1D0 -> +0x50 and +0x90), which the grenade guide (688C30:
// +0x50 rows x weapon+0x350, +0x80 start) and the laser sight (6890A0: +0x90..)
// read in their OnUpdate. Called in the weapon tick and, because those
// updates can run between the weapon loop's rewrite and the tick, from hooks
// on the two OnUpdate slots as well.
// The same matrix is reached twice per update (the attachment's OnUpdate
// hook and the weapon tick). Carrying an already carried matrix applies the
// hand's turn a second time: the left shoulder laser came out along
// (2 x left aim - right aim), "pulled by the right hand" on hardware. So what
// was written is remembered per hand and address, and a matrix that still
// holds it is left alone; the game's own rewrite makes it differ again.
struct FencerCarried { unsigned char* at=nullptr; edf6vr::Matrix written{}; };
FencerCarried g_fencerCarried[2][3]{};
SRWLOCK g_fencerCarryLock=SRWLOCK_INIT;
// The turn the carry applies, per hand, as CarryWeaponBones does it: a
// direction d goes to sum_k (d . from[k]) to[k], from = the weapon's root rows
// (normalised), to = the hand's axes. The left's shot comes out of the game
// through it (shot_origin.h), so it is kept to be taken back out there.
struct FencerCarryTurn { const void* weapon=nullptr; float from[3][3]{},to[3][3]{}; bool valid=false; };
FencerCarryTurn g_fencerCarryTurn[2]{};
void FencerNoteCarryTurn(unsigned hand,const void* weapon,const edf6vr::Matrix& root,const WeaponHoldCommand& command) noexcept {
    if(hand>1) return;
    FencerCarryTurn turn{}; turn.weapon=weapon; turn.valid=true;
    for(int k=0;k<3;++k) {
        const float n=std::sqrt(root.m[k][0]*root.m[k][0]+root.m[k][1]*root.m[k][1]+root.m[k][2]*root.m[k][2]);
        if(!std::isfinite(n) || n<1e-4f) { turn.valid=false; break; }
        for(int j=0;j<3;++j) { turn.from[k][j]=root.m[k][j]/n; turn.to[k][j]=command.hand.axes[k][j]; }
    }
    AcquireSRWLockExclusive(&g_fencerCarryLock); g_fencerCarryTurn[hand]=turn; ReleaseSRWLockExclusive(&g_fencerCarryLock);
}
bool FencerCarryTurnApply(unsigned hand,const void* weapon,const float in[3],float out[3]) noexcept {
    if(hand>1) return false;
    FencerCarryTurn turn{};
    AcquireSRWLockShared(&g_fencerCarryLock); turn=g_fencerCarryTurn[hand]; ReleaseSRWLockShared(&g_fencerCarryLock);
    if(!turn.valid || turn.weapon!=weapon) return false;
    for(int j=0;j<3;++j) out[j]=0;
    for(int k=0;k<3;++k) {
        const float along=in[0]*turn.from[k][0]+in[1]*turn.from[k][1]+in[2]*turn.from[k][2];
        for(int j=0;j<3;++j) out[j]+=along*turn.to[k][j];
    }
    const float n=std::sqrt(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]);
    if(!std::isfinite(n) || n<0.5f) return false;
    for(int j=0;j<3;++j) out[j]/=n;
    return true;
}
void FencerCarryOne(unsigned hand,unsigned slot,unsigned char* at,const edf6vr::Matrix& root,const WeaponHoldCommand& command,std::atomic<unsigned long long>& counter) noexcept {
    AcquireSRWLockExclusive(&g_fencerCarryLock);
    auto& memo=g_fencerCarried[hand][slot];
    const auto native=*reinterpret_cast<const edf6vr::Matrix*>(at);
    if(memo.at==at && std::memcmp(&memo.written,&native,sizeof(native))==0) {
        ReleaseSRWLockExclusive(&g_fencerCarryLock);
        g_fencerCarrySkips.fetch_add(1,std::memory_order_relaxed);
        return;
    }
    edf6vr::Matrix carried{}; float before=0,after=0;
    // A back mount's matrices that ride the joint are straightened with it
    // (FencerStraightenJoint) before they are carried: the guide line and the
    // laser are read from them, and they span round in a dash with the joint.
    edf6vr::Matrix source=native;
    if(command.jointIndex>=1 && command.weaponNodes && static_cast<unsigned>(command.jointIndex)<command.count) {
        const auto jm=*reinterpret_cast<const edf6vr::Matrix*>(static_cast<const unsigned char*>(command.weaponNodes)+static_cast<std::size_t>(command.jointIndex)*0x110+0xB0);
        FencerWatchRide(hand,slot,command.weapon,native,root,jm);
        float M[3][3]{};
        if(FencerRowsApart(native,jm)<FencerRowsApart(native,root) && FencerJointTurn(root,jm,command.jointTarget,M)) {
            const float pivot[3]={jm.m[3][0],jm.m[3][1],jm.m[3][2]};
            FencerTurnMatrix(source,M,pivot);
            g_fencerJointCarried.fetch_add(1,std::memory_order_relaxed);
        } else g_fencerJointCarryLeft.fetch_add(1,std::memory_order_relaxed);
    }
    bool ok=edf6vr::CarryWeaponBones(root,command.hand,&source,1,&carried,before,after);
    if(ok) {
        float away=0;
        for(int j=0;j<3;++j) away+=(carried.m[3][j]-command.eyeWorld[j])*(carried.m[3][j]-command.eyeWorld[j]);
        ok=std::isfinite(away) && std::sqrt(away)<kWeaponMuzzleReach;
    }
    if(ok) {
        std::memcpy(at,&carried,sizeof(carried));
        memo.at=at; memo.written=carried;
        counter.fetch_add(1,std::memory_order_relaxed);
    }
    ReleaseSRWLockExclusive(&g_fencerCarryLock);
}
// After the weapon tick: the tick rebuilds the attachment entry from
// weapon+0x150, which was carried before it, so the entry now describes the
// carried weapon already. Remember it as written, or the attachment's
// OnUpdate hook carries it a second time -- the double turn that made the
// left shoulder laser move as a mirror of the right hand (log: the entry was
// carried twice per update, +0x150 and +0x90 once).
void FencerCarryAdopt(void* weapon,unsigned hand) noexcept {
    if(!weapon || hand>1) return;
    auto* bytes=static_cast<unsigned char*>(weapon);
    AcquireSRWLockExclusive(&g_fencerCarryLock);
    __try {
        const unsigned char* t=edf6vr::Readable(bytes,0x1E0)?*reinterpret_cast<unsigned char**>(bytes+0x1D0):nullptr;
        const unsigned char* at[3]={bytes+0x150,t?t+0x50:nullptr,t?t+0x90:nullptr};
        for(unsigned slot=0;slot<3;++slot) {
            auto& memo=g_fencerCarried[hand][slot];
            if(!at[slot] || memo.at!=at[slot] || !edf6vr::Readable(at[slot],64)) continue;
            if(std::memcmp(&memo.written,at[slot],64)!=0) {
                std::memcpy(&memo.written,at[slot],64);
                g_fencerCarryAdopted.fetch_add(1,std::memory_order_relaxed);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    ReleaseSRWLockExclusive(&g_fencerCarryLock);
}
// sourceOnly: carry weapon+0x150 alone. The attachment entries are rebuilt
// from it by the game at points of its own choosing -- after the tick, and
// again before the attachments' OnUpdate (v24 log: the entry changed between
// the adopt after the tick and the guide hook while +0x150 had not) -- so at
// OnUpdate the entry already describes the carried weapon, and carrying it
// there turned it twice: the left shoulder laser moved opposite to the right
// hand. The tick still carries all three before `original`.
void FencerCarryTransforms(void* weapon,const WeaponHoldCommand& command,unsigned hand,bool sourceOnly=false) noexcept {
    if(!weapon || !command.weaponNodes || hand>1) return;
    auto* bytes=static_cast<unsigned char*>(weapon);
    __try {
        if(!edf6vr::Readable(command.weaponNodes,0xF0) || !edf6vr::Readable(bytes,0x1E0,true)) return;
        const auto root=*reinterpret_cast<const edf6vr::Matrix*>(static_cast<const unsigned char*>(command.weaponNodes)+0xB0);
        FencerNoteCarryTurn(hand,weapon,root,command);
        FencerCarryOne(hand,0,bytes+0x150,root,command,g_fencerOwnMatrixCarries[hand]);
        if(sourceOnly) return;
        auto* t=*reinterpret_cast<unsigned char**>(bytes+0x1D0);
        if(edf6vr::Readable(t,0xD0,true)) { FencerCarryOne(hand,1,t+0x50,root,command,g_fencerTickWrites[hand]); FencerCarryOne(hand,2,t+0x90,root,command,g_fencerTransformTurns); }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
// The attachments' OnUpdate (WeaponAttachment_GrenadeGuide vtable 17E2498 and
// WeaponAttachment_LaserSight vtable 17E2458, slot 0x10): attachment+8 is the
// weapon. For a Fencer weapon in a hand the matrices are carried first.
using AttachmentUpdate=void(__fastcall*)(void*,void*);
AttachmentUpdate g_guideUpdateOriginal=nullptr,g_laserUpdateOriginal=nullptr;
void FencerBeforeAttachmentUpdate(void* attachment) noexcept {
    g_fencerGuideUpdates.fetch_add(1,std::memory_order_relaxed);
    if(!g_fencerActive.load(std::memory_order_relaxed)) return;
    void* weapon=nullptr;
    __try { if(edf6vr::Readable(attachment,0x10)) weapon=*reinterpret_cast<void**>(static_cast<unsigned char*>(attachment)+8); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if(!FencerWeaponActive(weapon)) return;
    const unsigned hand=weapon==g_fencerWeapons[0].load(std::memory_order_relaxed)?0u:1u;
    WeaponHoldCommand command{};
    AcquireSRWLockShared(&g_fencerHandLock); command=hand==0?g_fencerLeft:g_fencerRight; ReleaseSRWLockShared(&g_fencerHandLock);
    if(command.weapon!=weapon || !command.tracked || !ValidateHoldCommand(command,command.model)) return;
    FencerFollowSoldier(command);
    FencerCarryTransforms(weapon,command,hand,true);
    g_fencerGuideCarries.fetch_add(1,std::memory_order_relaxed);
}
// The probe reads the attachment BEFORE the update, where +0x30/+0x40/+0x80
// still hold the landing marker the last update worked out.
// The Fencer's carry runs first, so the guide's start is read after it and a
// Fencer weapon needs no further correction (GuideComputeDeltas refuses one).
// The line a shoulder weapon's guide was just built along, against the barrel
// as drawn (the joint's target carried onto the hand), per hand. A mortar's
// line leaves above the barrel by its own lob, so what is watched is the change
// from the angle it keeps on foot. The user saw the left line jump off for a
// moment at each jump (2026-09-26); FENCERGUIDESPIKE says which matrix the line
// was read from at that moment -- the one carried in the tick or the game's own.
void FencerGuideCheck(void* attachment) noexcept {
    if(!g_fencerActive.load(std::memory_order_relaxed)) return;
    void* weapon=nullptr;
    __try { if(edf6vr::Readable(attachment,0x10)) weapon=*reinterpret_cast<void**>(static_cast<unsigned char*>(attachment)+8); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if(!FencerWeaponActive(weapon)) return;
    const unsigned hand=weapon==g_fencerWeapons[0].load(std::memory_order_relaxed)?0u:1u;
    WeaponHoldCommand command{};
    AcquireSRWLockShared(&g_fencerHandLock); command=hand==0?g_fencerLeft:g_fencerRight; ReleaseSRWLockShared(&g_fencerHandLock);
    if(command.weapon!=weapon || command.jointIndex<1) return;
    float H[3][3]{},want[3]{};
    for(int k=0;k<3;++k) {
        const float n=std::sqrt(command.hand.axes[k][0]*command.hand.axes[k][0]+command.hand.axes[k][1]*command.hand.axes[k][1]+command.hand.axes[k][2]*command.hand.axes[k][2]);
        if(!(n>1e-4f)) return;
        for(int j=0;j<3;++j) H[k][j]=command.hand.axes[k][j]/n;
    }
    for(int j=0;j<3;++j) want[j]=command.jointTarget[2][0]*H[0][j]+command.jointTarget[2][1]*H[1][j]+command.jointTarget[2][2]*H[2][j];
    float start[3]{},dir[3]{};
    if(!GuideProbeArc(weapon,start,dir)) return;
    const float angle=std::acos(std::clamp(dir[0]*want[0]+dir[1]*want[1]+dir[2]*want[2],-1.0f,1.0f))*57.29578f;
    if(!std::isfinite(angle)) return;
    auto& w=g_fencerGuideWatch[hand];
    if(w.weapon!=weapon) { const unsigned spikes=w.spikes; w={}; w.weapon=weapon; w.spikes=spikes; }
    const int band=g_fencerDashHold?1:0;
    if(!band && g_fencerSettled) {
        if(!w.haveBaseline) { w.baseline=angle; w.haveBaseline=true; } else w.baseline+=(angle-w.baseline)*0.05f;
        ++w.samples;
    }
    if(!w.haveBaseline || w.samples<90) return;
    const float off=std::fabs(angle-w.baseline);
    w.worst[band]=std::max(w.worst[band],off);
    if(off>6.0f) {
        ++w.spikes;
        if(g_fencerGuideSpikeLines<40) {
            ++g_fencerGuideSpikeLines;
            // Was the line read from the matrix the tick carried, or the game's own?
            bool entryCarried=false,sourceCarried=false;
            __try {
                auto* bytes=static_cast<unsigned char*>(weapon);
                auto* t=edf6vr::Readable(bytes,0x1E0)?*reinterpret_cast<unsigned char**>(bytes+0x1D0):nullptr;
                if(t && edf6vr::Readable(t,0xD0)) {
                    edf6vr::Matrix entry{},source{};
                    std::memcpy(&entry,t+0x50,64); std::memcpy(&source,bytes+0x150,64);
                    AcquireSRWLockShared(&g_fencerCarryLock);
                    const auto& e=g_fencerCarried[hand][1]; const auto& o=g_fencerCarried[hand][0];
                    entryCarried=e.at==t+0x50 && std::memcmp(&e.written,&entry,64)==0;
                    sourceCarried=o.at==bytes+0x150 && std::memcmp(&o.written,&source,64)==0;
                    ReleaseSRWLockShared(&g_fencerCarryLock);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
            float muzzle[3]{},away=-1;
            if(command.muzzleLocalValid && edf6vr::PlaceWeaponLocalPoint(command.hand,command.muzzleLocal,muzzle)) {
                away=0; for(int j=0;j<3;++j) away+=(start[j]-muzzle[j])*(start[j]-muzzle[j]); away=std::sqrt(away);
            }
            Log("FENCERGUIDESPIKE %s off=%.1fdeg angle=%.1f usual=%.1f hold=%d settled=%d speed=%.1fm/s entry=%s source=%s startFromMuzzle=%.2fm commandAge=%llums",
                hand?"R":"L",off,angle,w.baseline,band,g_fencerSettled?1:0,g_fencerSpeed,entryCarried?"carried":"game's",sourceCarried?"carried":"game's",away,
                GetTickCount64()-command.refreshed);
        }
    }
}
void __fastcall HookGuideUpdate(void* attachment,void* context) {
    GuideProbeUpdate(attachment,0);
    FencerBeforeAttachmentUpdate(attachment);
    GuideRayBegin(attachment);
    __try { g_guideUpdateOriginal(attachment,context); }
    __finally { GuideRayEnd(); }
    FencerGuideCheck(attachment);
}
void __fastcall HookLaserUpdate(void* attachment,void* context) { GuideProbeUpdate(attachment,1); FencerBeforeAttachmentUpdate(attachment); g_laserUpdateOriginal(attachment,context); }
bool InstallFencerAttachmentHooks() noexcept {
    __try {
        auto guideSlot=reinterpret_cast<void**>(g_image.base+0x17E2498+0x10);
        auto laserSlot=reinterpret_cast<void**>(g_image.base+0x17E2458+0x10);
        if(*guideSlot!=g_image.base+0x688C30 || *laserSlot!=g_image.base+0x6890A0) return false;
        g_guideUpdateOriginal=reinterpret_cast<AttachmentUpdate>(g_image.base+0x688C30);
        g_laserUpdateOriginal=reinterpret_cast<AttachmentUpdate>(g_image.base+0x6890A0);
        bool changed=false;
        if(!edf6vr::ReplacePointer(guideSlot,reinterpret_cast<void*>(g_guideUpdateOriginal),reinterpret_cast<void*>(&HookGuideUpdate),changed)) return false;
        if(!edf6vr::ReplacePointer(laserSlot,reinterpret_cast<void*>(g_laserUpdateOriginal),reinterpret_cast<void*>(&HookLaserUpdate),changed)) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The guard, on the shield's own aim.
//
// Whether a hit is blocked is 5957C0(soldier, the hit's direction, a limit): it
// takes the guard value (+0x1A38; zero or less blocks nothing), turns the
// direction into the frame of the soldier's aim yaw (+0x1244) and blocks inside
// limit * guard. It never looks at the shield. In VR +0x1244 is the right
// hand's aim, so a left shield guarded wherever the RIGHT hand pointed: turned
// left and back to the front, the front was not covered (hardware 2026-10-01).
// The shield is drawn square in front of its controller since 2026-09-30, so
// the guard now goes the same way. With a shield in the left hand, the test
// runs on the left aim (the shadow, as the left weapon's tick has it). With
// shields in both hands it runs on the hand whose trigger is held further.
//
// Called from 599650 (damage), 59576A and 598459, and jumped to from 5957BA
// (595780 tail-calls it). All four are redirected.
using GuardTest=bool(*)(void* soldier,const float* from,float limit);
GuardTest g_guardOriginal=nullptr;
bool FencerLeftGuardYaw(void* soldier,float& yaw) noexcept {
    if(!soldier || !g_fencerActive.load(std::memory_order_relaxed)) return false;
    void* const leftWeapon=g_fencerWeapons[0].load(std::memory_order_relaxed);
    void* const rightWeapon=g_fencerWeapons[1].load(std::memory_order_relaxed);
    WeaponHoldCommand command{};
    AcquireSRWLockShared(&g_fencerHandLock); command=g_fencerLeft; ReleaseSRWLockShared(&g_fencerHandLock);
    if(command.soldier!=soldier || command.weapon!=leftWeapon || !leftWeapon) return false;
    if(!edf6vr::HasType(g_image,leftWeapon,".?AVWeapon_Shield@@")) return false;
    if(rightWeapon && edf6vr::HasType(g_image,rightWeapon,".?AVWeapon_Shield@@")
       && g_handTrigger[0].load(std::memory_order_relaxed)<=g_handTrigger[1].load(std::memory_order_relaxed)) return false;
    FencerShadow shadow{};
    AcquireSRWLockShared(&g_fencerShadowLock); shadow=g_fencerShadow; ReleaseSRWLockShared(&g_fencerShadowLock);
    if(!shadow.valid || !std::isfinite(shadow.smooth[1])) return false;
    yaw=shadow.smooth[1];
    return true;
}
bool HookGuardTest(void* soldier,const float* from,float limit) noexcept {
    g_fencerGuardTests.fetch_add(1,std::memory_order_relaxed);
    float yaw=0;
    if(!FencerLeftGuardYaw(soldier,yaw) || !edf6vr::Readable(static_cast<unsigned char*>(soldier)+0x1244,4,true))
        return g_guardOriginal(soldier,from,limit);
    auto* field=reinterpret_cast<float*>(static_cast<unsigned char*>(soldier)+0x1244);
    const float saved=*field;
    bool blocked=false;
    *field=yaw;
    __try { blocked=g_guardOriginal(soldier,from,limit); }
    __finally { *field=saved; }
    g_fencerGuardLeft.fetch_add(1,std::memory_order_relaxed);
    if(blocked) g_fencerGuardLeftBlocked.fetch_add(1,std::memory_order_relaxed);
    return blocked;
}
unsigned InstallFencerGuardHooks() noexcept {
    __try {
        auto* base=g_image.base;
        // The test as read: movss xmm0,[rcx+1A38] at +27 and movss xmm1,[rcx+1244] at +49.
        static const unsigned char guard[]={0xF3,0x0F,0x10,0x81,0x38,0x1A,0x00,0x00};
        static const unsigned char yaw[]={0xF3,0x0F,0x10,0x89,0x44,0x12,0x00,0x00};
        if(!edf6vr::Readable(base+0x5957C0,0x60) || std::memcmp(base+0x5957E7,guard,sizeof(guard))
           || std::memcmp(base+0x595809,yaw,sizeof(yaw))) return 0;
        g_guardOriginal=reinterpret_cast<GuardTest>(base+0x5957C0);
        unsigned sites=0; bool changed=false;
        for(const std::uint32_t call:{0x59576Au,0x598459u,0x5996A9u})
            if(edf6vr::RedirectCall(base+call,base+0x5957C0,reinterpret_cast<void*>(&HookGuardTest),changed)) ++sites;
        if(edf6vr::RedirectJump(base+0x5957BA,base+0x5957C0,reinterpret_cast<void*>(&HookGuardTest),changed)) ++sites;
        g_fencerGuardSites=sites;
        return sites;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// The left weapon's tick, on the weapon thread: the soldier carries the left
// aim while it runs, and the weapon's recoil goes to the left aim afterwards.
void FencerWeaponTick(void* weapon,void* context,DualTick original) noexcept {
    // Hand 0 (the left) gets the aim swap and the turned transform; both
    // hands get their transform's translation moved to the carried muzzle for
    // the tick, where a mortar's shell and the laser sight are born from it
    // (the update thread's write is overwritten by the weapon loop before
    // this tick runs, so it alone left the left shells at the game's mount).
    const unsigned hand=weapon==g_fencerWeapons[0].load(std::memory_order_relaxed)?0u:1u;
    WeaponHoldCommand command{};
    AcquireSRWLockShared(&g_fencerHandLock); command=hand==0?g_fencerLeft:g_fencerRight; ReleaseSRWLockShared(&g_fencerHandLock);
    const bool poseValid=command.weapon==weapon && command.tracked && ValidateHoldCommand(command,command.model);
    if(!poseValid) g_fencerInvalid.fetch_add(1,std::memory_order_relaxed);
    if(poseValid) FencerFollowSoldier(command);
    const auto previous=g_dualTick;
    g_dualTick={command,weapon,poseValid};
    HandAimNoteTick(weapon);   // this hand's direction, for the other players (hand_aim_sync.h)
    auto* soldier=static_cast<unsigned char*>(command.soldier);
    auto* bytes=static_cast<unsigned char*>(weapon);
    float saved[8]{}; bool swapped=false;
    // The weapon transform (+0x1D0) holds two world matrices the game derives
    // from the mount and its aim before this tick: +0x50 (rows) / +0x80
    // (translation), which a mortar's launch is computed from, and +0x90..
    // +0xC0, which the laser sight's update reads for its start and direction
    // (research: WeaponAttachment_LaserSight OnUpdate; turning only the first
    // left the left laser on the right reticle). Both are carried from the
    // weapon's native root onto the hand the model is drawn with, the same
    // carry as the bones, and left there: whatever reads them after this tick
    // sees the carried weapon, and the weapon loop rewrites them before the
    // next one.
    FencerShadow shadow{};
    AcquireSRWLockShared(&g_fencerShadowLock); shadow=g_fencerShadow; ReleaseSRWLockShared(&g_fencerShadowLock);
    if(poseValid) FencerCarryTransforms(weapon,command,hand);
    if(hand==0 && poseValid && shadow.valid && edf6vr::Readable(soldier,0x1AB0,true)) {
        __try {
            std::memcpy(saved,soldier+0x1230,32);
            std::memcpy(soldier+0x1230,shadow.target,16);
            std::memcpy(soldier+0x1240,shadow.smooth,16);
            swapped=true;
            std::memcpy(g_fencerSwapNative,saved+4,16);
            g_fencerSwapLive=true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { swapped=false; }
    }
    g_fencerTicks.fetch_add(1,std::memory_order_relaxed);
    if(swapped) g_fencerSwapped.fetch_add(1,std::memory_order_relaxed);
    __try { original(weapon,context); }
    __finally {
        g_fencerSwapLive=false;
        if(poseValid) FencerCarryAdopt(weapon,hand);
        // The game's recoil this tick, for the log (NoteFencerKick), either
        // hand, before the left's is moved onto the left aim below.
        __try {
            if(soldier && edf6vr::Readable(bytes+0xEE0,0x30,true) && (*reinterpret_cast<unsigned*>(bytes+0xEE0)&0x20u)
               && edf6vr::Readable(soldier+0x1AAC,4)) {
                const float* k=reinterpret_cast<const float*>(bytes+0xF00);
                const float scale=*reinterpret_cast<const float*>(soldier+0x1AAC);
                const float radians=std::sqrt(k[0]*k[0]+k[1]*k[1]+k[2]*k[2])*std::fabs(scale);
                if(std::isfinite(radians) && radians>1e-5f) NoteFencerKick(hand,radians);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if(swapped) {
            __try {
                FencerShadow after{};
                std::memcpy(after.target,soldier+0x1230,16);
                std::memcpy(after.smooth,soldier+0x1240,16);
                after.valid=true;
                std::memcpy(soldier+0x1230,saved,32);
                // 59B070..: the loop would add weapon+F00 * soldier+1AAC to the
                // soldier's two aims next tick. It is the left weapon's kick, so it
                // goes to the left aim instead, and the loop finds nothing.
                if(edf6vr::Readable(bytes+0xEE0,0x30,true) && (*reinterpret_cast<unsigned*>(bytes+0xEE0)&0x20u)) {
                    auto* kick=reinterpret_cast<float*>(bytes+0xF00);
                    const float scale=*reinterpret_cast<const float*>(soldier+0x1AAC);
                    const float size=std::fabs(kick[0])+std::fabs(kick[1])+std::fabs(kick[2]);
                    if(std::isfinite(scale) && std::isfinite(size) && size*std::fabs(scale)>1e-4f) {
                        const float limit=*reinterpret_cast<const float*>(soldier+0x1250);
                        for(int l=0;l<3;++l) {
                            if(!std::isfinite(kick[l])) continue;
                            after.target[l]+=kick[l]*scale; after.smooth[l]+=kick[l]*scale;
                            g_fencerLastKick[l]=kick[l]*scale;
                        }
                        after.target[0]=FencerClampPitch(after.target[0],limit);
                        after.smooth[0]=FencerClampPitch(after.smooth[0],limit);
                        kick[0]=kick[1]=kick[2]=0;
                        g_fencerKicks.fetch_add(1,std::memory_order_relaxed);
                    }
                }
                AcquireSRWLockExclusive(&g_fencerShadowLock); g_fencerShadow=after; ReleaseSRWLockExclusive(&g_fencerShadowLock);
            } __except(EXCEPTION_EXECUTE_HANDLER) { g_fencerInvalid.fetch_add(1,std::memory_order_relaxed); }
        }
        g_dualTick=previous;
    }
}
// The second reticle: the Fencer's left aim (its target, as the right reticle
// shows the soldier's) or the Ranger's left gun.
//
// The Ranger's comes from the left hold command, which AfterUpdate wipes on
// entry and PublishRangerDualHands builds again at its end. Read here, where
// the right reticle is published, it was always the wiped one, so the left gun
// never had a reticle (2026-10-04 test: 1342 left shots, aim2=0 throughout;
// the user: "レンジャーの左手装備時もレティクル出るようにして"). The Ranger's is
// published after that build, by PublishRangerSecondAim.
void PublishSecondAim() noexcept {
    auto reference=[&](float yaw,float pitchUp,float out[3]) {
        const auto r=edf6vr::AimToReference(yaw,pitchUp,g_yawOffset); out[0]=r.x; out[1]=r.y; out[2]=r.z;
    };
    if(g_fencerActive.load() && g_vrEnabled && g_handAiming) {
        FencerShadow shadow{};
        AcquireSRWLockShared(&g_fencerShadowLock); shadow=g_fencerShadow; ReleaseSRWLockShared(&g_fencerShadowLock);
        if(shadow.valid) {
            float d[3]{}; reference(shadow.target[1],static_cast<float>(g_pitchSign)*shadow.target[0],d);
            edf6vr::g_openxr.SetAimDirection2(d);
        } else edf6vr::g_openxr.SetAimDirection2(nullptr);
        return;
    }
    if(g_dualActive.load()) return;
    edf6vr::g_openxr.SetAimDirection2(nullptr);
}
// The Ranger's left gun's reticle, once AfterUpdate has built the left hold
// command (PublishRangerDualHands): along that hand's forward, which is the way
// its shots leave (RANGERDUAL offHand L=0.0deg over those 1342 shots). The
// Fencer's stays PublishSecondAim's; anything else has no second reticle.
void PublishRangerSecondAim(bool fps) noexcept {
    if(g_fencerActive.load()) return;
    if(fps && g_dualActive.load() && g_leftHoldCommand.tracked && g_vrEnabled && g_handAiming) {
        const float* f=g_leftHoldCommand.hand.axes[2];
        const float length=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
        if(std::isfinite(length) && length>0.5f) {
            const auto r=edf6vr::AimToReference(std::atan2(f[0],f[2]),std::asin(std::clamp(f[1]/length,-1.0f,1.0f)),g_yawOffset);
            const float d[3]={r.x,r.y,r.z};
            edf6vr::g_openxr.SetAimDirection2(d);
            return;
        }
    }
    edf6vr::g_openxr.SetAimDirection2(nullptr);
}
