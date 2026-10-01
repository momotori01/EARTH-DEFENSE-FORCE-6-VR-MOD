// Included by plugin.cpp after fencer_dual.h, inside its private namespace.
//
// Nothing of the game's is written in this file but the two sight widths
// (HookLaserPrepare, ThinGuideLine). It watches the two
// attachment classes that draw a weapon's sight, and it corrects the two calls
// that consume the arc by rewriting only the CALLER'S OWN STACK buffers -- the
// three vectors handed to the line builder, and the ray a landing marker is
// looked up with. The weapon matrices themselves are read and left alone.
//
// What it is for: the Ranger's laser sight and thrown-weapon guide are drawn
// from the game's own weapon matrices (the model on the body), so they come
// out of the wrong place, and every attempt so far wrote those matrices and
// broke the discharge or the aim. Nothing here writes them. What each class
// does with them, read out of the shipped code:
//
//   WeaponAttachment_GrenadeGuide (vtable 17E2498)
//     OnUpdate      688C30  steps an arc from the weapon transform and casts
//                           one ray per fourth step (11BD380 builds ray items
//                           of 0xD0 bytes: origin, direction, 1/direction),
//                           after clearing attachment+0x30 (point), +0x40
//                           (range, 1000) and +0x80 (hit flag). That is the
//                           landing marker, not the line.
//     draw prepare  688680  builds the drawn arc: start = weapon+0x1D0 ->
//                           +0x80, velocity = rows(+50,+60,+70) x weapon+0x350
//                           normalised x weapon+0x894, the step velocity =
//                           that plus weapon->vt+0xB0() (the owner's own
//                           motion), gravity x weapon+0x8E0,
//                           and 6888D9 hands the three to 6899F0, which fills
//                           the point list of the object at attachment+0x10
//                           (its count lands at +0x88). Then the marker object
//                           at attachment+0x70 is placed from attachment+0x50.
//   WeaponAttachment_LaserSight (vtable 17E2458): OnUpdate 6890A0, prepare
//                           688970. The same two questions apply to it.
//
// The probe below answered the question the counters had left open. In every
// five-second window exactly half the prepared guides were ours, and the other
// half is a second attachment on Weapon_Sub -- the Ranger's sub weapon, which
// the mod tracks in no hand at all, prepared and visible every frame. That is
// the guide that stayed on the model however carefully ours was corrected.
// Moving that one onto the hand was tried and taken back out: the hand was
// harder to read past, and it was not what was wrong with it. What IS wrong
// with it is in the arc itself -- 688680 adds the owner's own motion
// (vt+0xB0) to the throw before walking it, and the thrown object does not
// take that motion, so the drawn parabola leaned with every roll and change
// of direction while the throw went where it was aimed. That term is now
// taken out of the walk (match 3 keeps the game start and the game
// direction, and only stops the arc from being led by the running).
// The probe also measured the correction before it was applied, and the
// measurement is what this file now does:
//   right hand   offset 0.20..0.37 m, turn 0.0..0.2 deg
//   left hand    offset 0.58..2.50 m, turn 16..116 deg
// The right weapon is aimed by the game along the aim the mod writes from the
// controller, so its direction is already the hand's and only its start is
// out. The left weapon has no aim of its own and must be turned as well. The
// carried start came out equal to the published tracked muzzle to within a
// centimetre, which is the point the bullets already leave from.
struct GuideProbeEntry {
    void* attachment=nullptr;
    void* weapon=nullptr;
    void* owner=nullptr;            // weapon+0x120, the soldier it belongs to
    const char* type=nullptr;       // the weapon class
    unsigned long long updates=0,draws=0,prepared=0; // prepared: draws that reached the arc
    DWORD updateThread=0,drawThread=0;
    unsigned char klass=0;          // 0 grenade guide, 1 laser sight
    unsigned char match=0;          // 0 none, 1 the right weapon, 2 the left, 3 a sub weapon
    unsigned char tracked=0;        // that command validated when it was seen
    unsigned char visible=0;        // the byte the prepare writes to its line object
    unsigned char hit=0;            // attachment+0x80, the previous update ray result
    int steps=0;                    // weapon+0x898, rays cast per update
    float speed=0,gravity=0;        // weapon+0x894, weapon+0x8E0
    unsigned long long points=0;    // arc points after the prepare (object+0x10 -> +0x88)
    float start[3]{};               // where the game starts the arc
    float direction[3]{};           // and which way, normalised
    float carriedStart[3]{};        // the same point carried onto the hand
    float muzzle[3]{};              // the published tracked muzzle of that weapon
    float offset=0;                 // |carriedStart - start|, how far the guide is out
    float turn=0;                   // degrees between direction and its carried self
    float markerPoint[3]{};         // attachment+0x30
    float markerRange=0;            // attachment+0x40
    float markerPlace[3]{};         // attachment+0x50, what the prepare gives the marker
    unsigned char hasMarker=0;      // attachment+0x70
    bool touched=false;             // seen since the last report
};
constexpr unsigned kGuideProbeEntries=8;
SRWLOCK g_guideProbeLock=SRWLOCK_INIT;
GuideProbeEntry g_guideProbe[kGuideProbeEntries]{};
std::atomic<unsigned long long> g_guideProbeDropped{0},g_guideProbeUpdates{0},g_guideProbeDraws{0};

using GuidePrepare=void*(__fastcall*)(void*,void*,void*,void*);
GuidePrepare g_guidePrepareOriginal=nullptr,g_laserPrepareOriginal=nullptr;

// The table slot for this attachment, or null when the table is full. Called
// with the lock held.
GuideProbeEntry* GuideProbeSlot(void* attachment,unsigned char klass) noexcept {
    for(auto& entry:g_guideProbe) if(entry.attachment==attachment) return &entry;
    for(auto& entry:g_guideProbe) if(!entry.attachment) {
        entry={}; entry.attachment=attachment; entry.klass=klass; return &entry;
    }
    g_guideProbeDropped.fetch_add(1,std::memory_order_relaxed);
    return nullptr;
}
// Where the soldier stands this instant, which the hand commands are
// anchored against (the same field ResolveTrackedMuzzle re-anchors on).
bool GuideSoldierRoot(void* soldier,float* out) noexcept {
    __try {
        if(!edf6vr::Readable(soldier,0x9C)) return false;
        const auto* root=reinterpret_cast<const float*>(static_cast<unsigned char*>(soldier)+0x90);
        for(int j=0;j<3;++j) { if(!std::isfinite(root[j])) return false; out[j]=root[j]; }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* GuideWeaponOwner(void* weapon) noexcept {
    __try {
        if(!edf6vr::Readable(weapon,0x128)) return nullptr;
        return *reinterpret_cast<void**>(static_cast<unsigned char*>(weapon)+0x120);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
// Which hand command owns this weapon, without validating anything yet.
unsigned GuideProbeMatch(void* weapon,WeaponHoldCommand& command) noexcept {
    if(!weapon) return 0;
    if(g_dualActive.load(std::memory_order_relaxed) && g_leftHoldCommand.weapon==weapon
       && weapon!=g_holdCommand.weapon) { command=g_leftHoldCommand; return 2; }
    if(g_holdCommand.weapon==weapon) { command=g_holdCommand; return 1; }
    // A weapon of our own soldier that is held in neither hand -- the sub
    // weapon, whose guide the game draws all the same, from the model on the
    // body. Its direction is the game's and is left alone: the throw really
    // does leave the body, and the direction is built from the aim, so it is
    // steady through a roll. Only the start is moved, onto the right hand, so
    // that the line stops coming out of the model's hip.
    if(g_holdCommand.tracked && g_holdCommand.soldier
       && GuideWeaponOwner(weapon)==g_holdCommand.soldier) { command=g_holdCommand; return 3; }
    return 0;
}
// The arc as the game has it this moment, read out of the weapon the same way
// 688C30 and 688680 read it.
bool GuideProbeArc(void* weapon,float* start,float* direction) noexcept {
    __try {
        auto* bytes=static_cast<unsigned char*>(weapon);
        if(!edf6vr::Readable(bytes,0x900)) return false;
        auto* entries=*reinterpret_cast<unsigned char**>(bytes+0x1D0);
        if(!edf6vr::Readable(entries,0x90)) return false;
        const auto* local=reinterpret_cast<const float*>(bytes+0x350);
        for(int j=0;j<3;++j) start[j]=reinterpret_cast<const float*>(entries+0x80)[j];
        float length=0;
        for(int j=0;j<3;++j) {
            float value=0;
            for(int k=0;k<3;++k) value+=local[k]*reinterpret_cast<const float*>(entries+0x50+k*16)[j];
            direction[j]=value; length+=value*value;
        }
        length=std::sqrt(length);
        if(!std::isfinite(length) || length<1e-4f) return false;
        for(int j=0;j<3;++j) direction[j]/=length;
        for(int j=0;j<3;++j) if(!std::isfinite(start[j])) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The same start and direction carried from the weapon native node root onto
// the hand the model is drawn with -- the carry the bones and, at a discharge,
// the muzzle attachment already get. The probe reports it as a distance and
// an angle; the correction below is built from it.
bool GuideProbeCarry(const WeaponHoldCommand& command,const float* start,const float* direction,
                     float* carriedStart,float* carriedDirection) noexcept {
    __try {
        if(!edf6vr::Readable(command.weaponNodes,0xF0)) return false;
        const auto root=*reinterpret_cast<const edf6vr::Matrix*>(
            static_cast<const unsigned char*>(command.weaponNodes)+0xB0);
        edf6vr::Matrix source{},carried{};
        for(int j=0;j<3;++j) { source.m[0][j]=direction[j]; source.m[3][j]=start[j]; }
        source.m[3][3]=1;
        float before=0,after=0;
        if(!edf6vr::CarryWeaponBones(root,command.hand,&source,1,&carried,before,after)) return false;
        for(int j=0;j<3;++j) { carriedStart[j]=carried.m[3][j]; carriedDirection[j]=carried.m[0][j]; }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Everything both hooks record about one call.
void GuideProbeSee(void* attachment,unsigned char klass,bool draw) noexcept {
    (draw?g_guideProbeDraws:g_guideProbeUpdates).fetch_add(1,std::memory_order_relaxed);
    void* weapon=nullptr;
    __try {
        if(!edf6vr::Readable(attachment,0x90)) return;
        weapon=*reinterpret_cast<void**>(static_cast<unsigned char*>(attachment)+8);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    GuideProbeEntry sample{};
    sample.weapon=weapon;
    sample.klass=klass;
    __try {
        auto* bytes=static_cast<unsigned char*>(attachment);
        for(int j=0;j<3;++j) {
            sample.markerPoint[j]=reinterpret_cast<const float*>(bytes+0x30)[j];
            sample.markerPlace[j]=reinterpret_cast<const float*>(bytes+0x50)[j];
        }
        sample.markerRange=*reinterpret_cast<const float*>(bytes+0x40);
        sample.hit=*(bytes+0x80)?1:0;
        sample.hasMarker=*reinterpret_cast<void**>(bytes+0x70)?1:0;
        auto* line=*reinterpret_cast<unsigned char**>(bytes+0x10);
        if(edf6vr::Readable(line,0x140)) {
            sample.points=*reinterpret_cast<const std::uint64_t*>(line+0x88);
            sample.visible=*(line+0x138)?1:0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try {
        auto* bytes=static_cast<unsigned char*>(weapon);
        if(edf6vr::Readable(bytes,0x900)) {
            sample.owner=*reinterpret_cast<void**>(bytes+0x120);
            sample.steps=*reinterpret_cast<const int*>(bytes+0x898);
            sample.speed=*reinterpret_cast<const float*>(bytes+0x894);
            sample.gravity=*reinterpret_cast<const float*>(bytes+0x8E0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    WeaponHoldCommand command{};
    sample.match=static_cast<unsigned char>(GuideProbeMatch(weapon,command));
    const bool arc=GuideProbeArc(weapon,sample.start,sample.direction);
    if(sample.match) {
        sample.tracked=command.tracked && ValidateHoldCommand(command,command.model)?1:0;
        if(arc && sample.tracked) {
            float carried[3]{};
            if(GuideProbeCarry(command,sample.start,sample.direction,sample.carriedStart,carried)) {
                float away=0,along=0;
                for(int j=0;j<3;++j) {
                    const float d=sample.carriedStart[j]-sample.start[j];
                    away+=d*d; along+=carried[j]*sample.direction[j];
                }
                sample.offset=std::sqrt(away);
                sample.turn=std::acos(std::clamp(along,-1.0f,1.0f))*57.29578f;
            }
        }
        float muzzle[3]{};
        if(ResolveTrackedMuzzle(weapon,muzzle,GetTickCount64()))
            for(int j=0;j<3;++j) sample.muzzle[j]=muzzle[j];
    }
    const DWORD thread=GetCurrentThreadId();
    AcquireSRWLockExclusive(&g_guideProbeLock);
    if(auto* entry=GuideProbeSlot(attachment,klass)) {
        const auto updates=entry->updates,draws=entry->draws,prepared=entry->prepared;
        const auto keptPoints=entry->points;
        const DWORD updateThread=entry->updateThread,drawThread=entry->drawThread;
        const char* type=entry->type;
        if(!type && weapon) type=edf6vr::TypeName(g_image,weapon);
        *entry=sample;
        entry->attachment=attachment;
        entry->type=type;
        entry->updates=updates+(draw?0:1);
        entry->draws=draws+(draw?1:0);
        entry->prepared=prepared+(draw && sample.visible?1:0);
        // The point count belongs to the prepare; an update would read the
        // list as the last prepare left it, which says nothing new.
        if(!draw) entry->points=keptPoints;
        entry->updateThread=draw?updateThread:thread;
        entry->drawThread=draw?thread:drawThread;
        entry->touched=true;
    }
    ReleaseSRWLockExclusive(&g_guideProbeLock);
}
void GuideProbeUpdate(void* attachment,unsigned klass) noexcept {
    GuideProbeSee(attachment,static_cast<unsigned char>(klass),false);
}

// ---------------------------------------------------------------------------
// The correction.
//
// Two vectors do all of it, worked out once per call from the same carry the
// drawn bones get (the weapon's own node root onto the hand):
//
//   delta   = the muzzle, as the mod already places it in the hand, minus the
//             game's start
//   deltaV  = the same velocity turned onto the hand's own forward, and ZERO
//             for the right hand, whose direction is the game's aim and is
//             never touched.
//
// Neither is measured against the model's animation, and that is the whole of
// what the first build got wrong. Dividing the weapon transform (+0x1D0, the
// weapon pass, built from the aim) by the node root (the animation pass, which
// the roll turns) mixes two clocks: standing still they agree, but through a
// roll they disagreed by up to 147 degrees and 2.7 m, and the line swung with
// the body although the game's own start never rolls. So the start is
// PlaceWeaponLocalPoint(hand, command.muzzleLocal) -- an offset measured once
// inside a single simulation snapshot, which is the point the muzzle flash and
// the shot already leave from -- and the turn is the shortest arc between two
// directions that no animation touches: the soldier's aim, which the game
// built the direction from, and the hand's own forward (hand.axes[2], the
// controller's ahead).
//
// The game's arc from the start S with the per-step velocity v is
//   p(k) = S + k*v + k*(k-1)/2 * gravity,
// and ours is the same arc from S+delta with v+deltaV, so
//   p'(k) = p(k) + delta + k*deltaV
// for every point of it, with gravity untouched -- the lob and the speed stay
// the game's. The drawn line is handed only S and v (6899F0 walks the rest),
// and the landing rays walk k in fours.
struct GuideDeltas {
    bool valid=false;
    unsigned hand=1;      // 1 the right weapon or a sub weapon, 0 the left
    bool ownStart=false;  // the start stays the game's (a sub weapon)
    float delta[3]{};     // how far the start moves
    float wanted[3]{};    // the step the arc should walk with: the weapon's
                          // own throw, with no owner motion added to it
    float start[3]{};     // the game's start, which the ray walk begins at
};
std::atomic<unsigned long long> g_guideLineMoved[2]{},g_guideLineRefused{0},g_guideNoMuzzle{0},
    g_guideRayUpdates{0},g_guideRaysMoved{0},g_guideRayOutOfStep{0},g_guideNoAim{0};
// offset: how far the start moved. lob: the angle between the soldier's aim and
// the direction the game built from it, which is the weapon's own tilt and must
// stay small and steady -- it is the check that the aim really is what the
// direction is made of. turn: the arc applied to the left hand.
std::atomic<float> g_guideLineOffset[2]{},g_guideLob{0},g_guideTurn{0},g_guideCatchUp{0},
    g_guideAbovePalm[2]{},g_guideOffBarrel[2]{};
// How much of the arc was the owner's own motion, over one report window
// rather than as a snapshot (0 least, 1 most). This is the term taken out:
// the game adds the soldier's movement to the throw when it draws the
// guide, and the thrown object does not take it, so the line leaned with
// every roll and change of direction while the throw went where it was
// aimed. In per-step units, beside a throw of weapon+0x894 per step.
std::atomic<float> g_guideLead[2]={1e9f,0.0f};
void GuideKeepSpan(std::atomic<float>* span,float value) noexcept {
    if(!std::isfinite(value)) return;
    for(unsigned i=0;i<2;++i) {
        float held=span[i].load(std::memory_order_relaxed);
        while((i==0?value<held:value>held)
              && !span[i].compare_exchange_weak(held,value,std::memory_order_relaxed)) {}
    }
}

// One vector by the shortest arc that takes `from` onto `to`. Reuses the
// Fencer's turn, which takes three rows; the other two are not used.
bool GuideTurnOne(float v[3],const float from[3],const float to[3]) noexcept {
    float rows[3][4]{{v[0],v[1],v[2],0},{0,0,0,0},{0,0,0,0}};
    if(!FencerTurnRows(rows,from,to)) return false;
    for(int j=0;j<3;++j) { if(!std::isfinite(rows[0][j])) return false; v[j]=rows[0][j]; }
    return true;
}
// The soldier's own aim, as a world direction. The guide's direction is built
// from it (the weapon transform is rebuilt from the mount and this aim), so it
// is the `from` of the turn, and it does not roll with an animation.
bool GuideAimForward(void* soldier,float out[3]) noexcept {
    __try {
        if(!edf6vr::Readable(soldier,0x1250)) return false;
        const auto* smooth=reinterpret_cast<const float*>(static_cast<const unsigned char*>(soldier)+0x1240);
        if(!std::isfinite(smooth[0]) || !std::isfinite(smooth[1])) return false;
        FencerAimForward(smooth[0],smooth[1],out);
        float length=0;
        for(int j=0;j<3;++j) length+=out[j]*out[j];
        return std::isfinite(length) && std::fabs(length-1)<0.05f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool GuideAttachmentWeapon(void* attachment,void*& weapon) noexcept {
    __try {
        if(!edf6vr::Readable(attachment,0x10)) return false;
        weapon=*reinterpret_cast<void**>(static_cast<unsigned char*>(attachment)+8);
        return weapon!=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool GuideComputeDeltas(void* weapon,GuideDeltas& out) noexcept {
    out={};
    // A Fencer weapon already carries its own transforms onto the hand inside
    // its tick, so its guide starts at the hand and must not be moved twice.
    if(!weapon || FencerWeaponActive(weapon)) return false;
    WeaponHoldCommand command{};
    const unsigned match=GuideProbeMatch(weapon,command);
    if(!match || !command.tracked || !ValidateHoldCommand(command,command.model)) return false;
    float start[3]{},direction[3]{},speed=0;
    if(!GuideProbeArc(weapon,start,direction)) return false;
    __try {
        if(!edf6vr::Readable(weapon,0x900)) return false;
        speed=*reinterpret_cast<const float*>(static_cast<unsigned char*>(weapon)+0x894);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if(!std::isfinite(speed) || speed<0) speed=0;
    // A sub weapon is carried by neither hand and its throw leaves the body,
    // so its line is left starting where the game starts it: that is where
    // the throw comes from, and the player asked for it there rather than at
    // a hand it would be read past. Only its walk is corrected.
    const bool ownStart=match==3;
    float muzzle[3]{start[0],start[1],start[2]};
    if(!ownStart) {
        if(!command.muzzleLocalValid
           || !edf6vr::PlaceWeaponLocalPoint(command.hand,command.muzzleLocal,muzzle)) {
            g_guideNoMuzzle.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
    }
    // That point is an absolute world position, anchored on the eye as it was
    // when the hand was published, and a running soldier has moved on since.
    // Read live it lagged him by up to a metre and a half, and because the
    // whole arc is shifted onto it, the line and its landing marker swam about
    // while moving or rolling. The offset is his own, so it is re-anchored on
    // where he stands NOW -- the same thing ResolveTrackedMuzzle does with the
    // published muzzle frame, and what the drawn weapon gets from
    // WeaponCatchUpBody.
    if(!ownStart) {
        float root[3]{},travelled=0;
        if(GuideSoldierRoot(command.soldier,root)) {
            float moved[3]{};
            for(int j=0;j<3;++j) {
                moved[j]=root[j]-command.rootWorld[j];
                travelled+=moved[j]*moved[j];
            }
            travelled=std::sqrt(travelled);
            // A teleport or a mission change is not travel: leave it.
            if(std::isfinite(travelled) && travelled<20.0f) {
                for(int j=0;j<3;++j) muzzle[j]+=moved[j];
                g_guideCatchUp.store(travelled,std::memory_order_relaxed);
            }
        }
    }
    if(!ownStart) {
        float away=0;
        for(int j=0;j<3;++j) { const float d=muzzle[j]-command.eyeWorld[j]; away+=d*d; }
        away=std::sqrt(away);
        if(!std::isfinite(away) || away>=kWeaponMuzzleReach) return false;
    }
    // The weapon's own tilt off the aim, which the turn must preserve.
    float aim[3]{};
    const bool haveAim=GuideAimForward(command.soldier,aim);
    if(haveAim) {
        float along=0;
        for(int j=0;j<3;++j) along+=aim[j]*direction[j];
        if(match==1) g_guideLob.store(std::acos(std::clamp(along,-1.0f,1.0f))*57.29578f,std::memory_order_relaxed);
    }
    float velocity[3]{},turned[3]{};
    for(int j=0;j<3;++j) velocity[j]=turned[j]=direction[j]*speed;
    if(match==2) {
        float ahead[3]{},length=0;
        for(int j=0;j<3;++j) { ahead[j]=command.hand.axes[2][j]; length+=ahead[j]*ahead[j]; }
        length=std::sqrt(length);
        if(!haveAim || !std::isfinite(length) || length<0.5f) {
            g_guideNoAim.fetch_add(1,std::memory_order_relaxed);
            return false;
        }
        for(int j=0;j<3;++j) ahead[j]/=length;
        if(!GuideTurnOne(turned,aim,ahead)) { g_guideNoAim.fetch_add(1,std::memory_order_relaxed); return false; }
        float along=0;
        for(int j=0;j<3;++j) along+=aim[j]*ahead[j];
        g_guideTurn.store(std::acos(std::clamp(along,-1.0f,1.0f))*57.29578f,std::memory_order_relaxed);
    }
    // Two numbers about the line as the player sees it leave the weapon they
    // are holding, because the same weapon read differently in each hand: how
    // far above the palm it starts, and how far off the barrel it goes. For
    // the right hand the direction is the game's, so `offBarrel` is the
    // honest gap between where the shot goes and where the model points.
    {
        const unsigned hand=match==2?0u:1u;
        float above=0,along=0,length=0;
        for(int j=0;j<3;++j) {
            above+=(muzzle[j]-command.hand.palm[j])*command.hand.axes[1][j];
            along+=turned[j]*command.hand.axes[2][j];
            length+=turned[j]*turned[j];
        }
        length=std::sqrt(length);
        g_guideAbovePalm[hand].store(above,std::memory_order_relaxed);
        if(std::isfinite(length) && length>1e-4f)
            g_guideOffBarrel[hand].store(std::acos(std::clamp(along/length,-1.0f,1.0f))*57.29578f,
                                         std::memory_order_relaxed);
    }
    for(int j=0;j<3;++j) {
        out.start[j]=start[j];
        out.delta[j]=muzzle[j]-start[j];
        out.wanted[j]=turned[j];
        if(!std::isfinite(out.delta[j]) || !std::isfinite(out.wanted[j])) { out={}; return false; }
    }
    out.ownStart=ownStart;
    out.hand=match==2?0u:1u;
    out.valid=true;
    return true;
}
// The line. 6888D9 is the one call that turns the start, the velocity and
// gravity into the drawn arc, and all three sit on 688680's own stack. The
// middle one is a VELOCITY, not a second point: 688848 asks the weapon for its
// owner's own motion (vt+0xB0) and adds the throw to it, and 689A21 reads it
// as the step 6899F0 walks the arc with. Adding the start's shift to it, as an
// earlier reading of this call did, would not move the line but launch it.
using GuideArc=void*(__fastcall*)(void*,float*,float*,float*);
GuideArc g_guideArcOriginal=nullptr;
thread_local GuideDeltas g_guideLine{};
void* __fastcall HookGuideArc(void* points,float* start,float* velocity,float* gravity) {
    const auto carry=g_guideLine;
    if(carry.valid && start && velocity) {
        __try {
            float moved[3]{};
            bool ok=true;
            for(int j=0;j<3;++j) {
                moved[j]=start[j]+carry.delta[j];
                if(!std::isfinite(moved[j]) || !std::isfinite(carry.wanted[j])) ok=false;
            }
            if(ok) {
                float offset=0,lead=0;
                for(int j=0;j<3;++j) {
                    offset+=carry.delta[j]*carry.delta[j];
                    const float d=velocity[j]-carry.wanted[j];
                    lead+=d*d;
                    start[j]=moved[j]; velocity[j]=carry.wanted[j];
                }
                g_guideLineOffset[carry.hand].store(std::sqrt(offset),std::memory_order_relaxed);
                GuideKeepSpan(g_guideLead,std::sqrt(lead));
                g_guideLineMoved[carry.hand].fetch_add(1,std::memory_order_relaxed);
            } else g_guideLineRefused.fetch_add(1,std::memory_order_relaxed);
        } __except(EXCEPTION_EXECUTE_HANDLER) { g_guideLineRefused.fetch_add(1,std::memory_order_relaxed); }
    }
    return g_guideArcOriginal(points,start,velocity,gravity);
}
// The landing rays. 688C30 walks the same arc and casts one ray every fourth
// step, from the last point it cast from to the one it has reached, so the
// n-th ray spans k = 4n-3 to k = 4n+1 (the first spans 0 to 1). The walk is
// followed rather than assumed: a ray whose first point is not where the last
// one ended -- and the first ray's is the game's own start -- means the shape
// of the loop is not what was read out of it, and then nothing is corrected.
// The game's own walk is carried in registers, so correcting the item on its
// stack cannot feed back into it.
using GuideRayQueue=void*(__fastcall*)(void*,void*,unsigned char*,int,int);
GuideRayQueue g_guideRayOriginal=nullptr;
thread_local GuideDeltas g_guideRay{};
thread_local unsigned g_guideRayIndex=0;
thread_local bool g_guideRayHaveLast=false;
thread_local float g_guideRayLast[3]{};
// What each step of the walk must be moved by, learned from the first ray.
thread_local float g_guideRayStep[3]{};
void GuideRayBegin(void* attachment) noexcept {
    void* weapon=nullptr;
    g_guideRay={}; g_guideRayIndex=0; g_guideRayHaveLast=false;
    for(int j=0;j<3;++j) g_guideRayStep[j]=0;
    if(!GuideAttachmentWeapon(attachment,weapon)) return;
    if(GuideComputeDeltas(weapon,g_guideRay))
        g_guideRayUpdates.fetch_add(1,std::memory_order_relaxed);
}
void GuideRayEnd() noexcept { g_guideRay={}; g_guideRayHaveLast=false; }
void* __fastcall HookGuideRay(void* queue,void* set,unsigned char* items,int count,int flag) {
    if(g_guideRay.valid && items && count==1) {
        __try {
            auto* first=reinterpret_cast<float*>(items);
            auto* second=reinterpret_cast<float*>(items+0x10);
            const float* expected=g_guideRayHaveLast?g_guideRayLast:g_guideRay.start;
            float gap=0;
            for(int j=0;j<3;++j) { const float d=first[j]-expected[j]; gap+=d*d; }
            if(std::isfinite(gap) && gap<0.0025f) {   // five centimetres
                const unsigned n=g_guideRayIndex;
                // The first ray spans one step, so it hands us the step the
                // game is walking with -- the throw plus the owner motion we
                // are taking out. Nothing else can be read from here, and it
                // is exact.
                if(!n) {
                    for(int j=0;j<3;++j)
                        g_guideRayStep[j]=g_guideRay.wanted[j]-(second[j]-first[j]);
                }
                const float from=n?static_cast<float>(4*n-3):0.0f,to=static_cast<float>(4*n+1);
                for(int j=0;j<3;++j) g_guideRayLast[j]=second[j];
                g_guideRayHaveLast=true;
                for(int j=0;j<3;++j) {
                    first[j]+=g_guideRay.delta[j]+from*g_guideRayStep[j];
                    second[j]+=g_guideRay.delta[j]+to*g_guideRayStep[j];
                }
                ++g_guideRayIndex;
                g_guideRaysMoved.fetch_add(1,std::memory_order_relaxed);
            } else {
                g_guideRay.valid=false;
                g_guideRayOutOfStep.fetch_add(1,std::memory_order_relaxed);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { g_guideRay.valid=false; }
    }
    return g_guideRayOriginal(queue,set,items,count,flag);
}
bool InstallGuideCarry(bool& changed) noexcept {
    changed=false;
    __try {
        // Both call sites are checked against the shipped bytes: 6888D9 is the
        // arc builder inside the draw preparation, 688FFD the ray queue inside
        // the update. 688748 is the read of the start both of them share.
        constexpr unsigned char reads[]={0x0F,0x10,0x80,0x80,0x00,0x00,0x00};
        if(std::memcmp(g_image.base+0x688748,reads,sizeof(reads))) return false;
        constexpr unsigned char rays[]={0x41,0xB9,0x01,0x00,0x00,0x00,0x4C,0x8D,0x45,0xC0};
        if(std::memcmp(g_image.base+0x688FEF,rays,sizeof(rays))) return false;
        g_guideArcOriginal=reinterpret_cast<GuideArc>(g_image.base+0x6899F0);
        g_guideRayOriginal=reinterpret_cast<GuideRayQueue>(g_image.base+0x11BD380);
        if(!edf6vr::RedirectCall(g_image.base+0x6888D9,reinterpret_cast<void*>(g_guideArcOriginal),
                                 reinterpret_cast<void*>(&HookGuideArc),changed)) return false;
        return edf6vr::RedirectCall(g_image.base+0x688FFD,reinterpret_cast<void*>(g_guideRayOriginal),
                                    reinterpret_cast<void*>(&HookGuideRay),changed);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The sight lines' thickness, each a share of the game's own ([Render]
// LaserSightWidth, ThrowGuideWidth, VehicleAimLineWidth; the user asked for half,
// 2026-10-01, and for each to be set on its own). These are the only writes in this file, and
// both touch a width that nothing but the drawing reads:
// - The laser's beam is a model that its draw preparation scales across by
//   attachment+0xB0 (688A97..688AC0: rows 0 and 1 x +0xB0, row 2 x the length
//   +0xB4, then 688B6A sets the model's matrix). The width is lent for that call
//   and put back.
// - The guide's arc is the line object at attachment+0x10 (WeaponAimLine,
//   vtable 17E2418), whose width the guide's constructor sets once to 0.02
//   (68785F: +0x134). That value is set to its share when the guide is prepared.
// - A vehicle weapon's aim line is a WeaponAimLine of its own (Weapon_VehicleShoot
//   +0x1638, made at 6B3406), given 0.10 through the line's width setter 687CE0
//   (movss [rcx+0x134],xmm1) from 6B34C7, the setter's only caller. That call is
//   redirected to HookAimLineWidth, which passes the share on.
constexpr float kGuideLineWidth=.02f;
std::atomic<unsigned long long> g_sightLaserLent{0},g_sightGuideThinned{0},g_sightVehicleLines{0};
std::atomic<float> g_sightLaserWidth{0},g_sightVehicleWidth{0};
using AimLineWidth=void(__fastcall*)(void*,float);
AimLineWidth g_aimLineWidthOriginal=nullptr;
void __fastcall HookAimLineWidth(void* line,float width) {
    if(std::isfinite(width) && width>0) {
        g_sightVehicleWidth.store(width,std::memory_order_relaxed);
        g_sightVehicleLines.fetch_add(1,std::memory_order_relaxed);
        width*=g_vehicleLineScale;
    }
    g_aimLineWidthOriginal(line,width);
}
void ThinGuideLine(void* attachment) noexcept {
    __try {
        auto* line=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(attachment)+0x10);
        if(!line) return;
        auto* width=reinterpret_cast<float*>(line+0x134);
        const float want=kGuideLineWidth*g_guideWidthScale;
        if(*width==kGuideLineWidth && want!=kGuideLineWidth) {
            *width=want;
            g_sightGuideThinned.fetch_add(1,std::memory_order_relaxed);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void* __fastcall HookGuidePrepare(void* attachment,void* a,void* b,void* c) {
    void* weapon=nullptr;
    const auto previous=g_guideLine;
    g_guideLine={};
    if(GuideAttachmentWeapon(attachment,weapon)) GuideComputeDeltas(weapon,g_guideLine);
    ThinGuideLine(attachment);
    void* result=nullptr;
    __try { result=g_guidePrepareOriginal(attachment,a,b,c); }
    __finally { g_guideLine=previous; GuideProbeSee(attachment,0,true); }
    return result;
}
// While the scope is up: the held weapon's laser against the scope's line, as
// the game hands it to the draw (attachment+0x90 the start, +0xA0 the direction).
// The angle shows a laser that sways with the body while the aim does not; the
// offset, a start that wanders off the scope's camera. Peaks per report window,
// with the soldier's step per update in the same window. Read only.
std::atomic<float> g_laserScopeAnglePeak{0},g_laserScopeAngleLast{0},g_laserScopeOffsetPeak{0},g_laserScopeOffsetLast{0},
    g_laserScopeStepPeak{0};
std::atomic<unsigned long long> g_laserScopeSamples{0};
void PeakStore(std::atomic<float>& peak,float v) noexcept { if(v>peak.load(std::memory_order_relaxed)) peak.store(v,std::memory_order_relaxed); }
void LaserAgainstScope(void* attachment) noexcept {
    const auto view=edf6vr::ReadScopeView();
    if(!view.active || GetTickCount64()-view.at>250) return;
    __try {
        auto* bytes=static_cast<unsigned char*>(attachment);
        if(*reinterpret_cast<void**>(bytes+8)!=g_holdCommand.weapon) return;
        const auto* o=reinterpret_cast<const float*>(bytes+0x90);
        const auto* d=reinterpret_cast<const float*>(bytes+0xA0);
        const float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(!(length>1e-6f) || !std::isfinite(length)) return;
        const float* f=view.forward;
        const float cosine=std::clamp((d[0]*f[0]+d[1]*f[1]+d[2]*f[2])/length,-1.f,1.f);
        const float angle=std::acos(cosine)*57.2957795f;
        float carry[3]{};
        if(view.kind!=edf6vr::ScopeHoloPanel) ScopeLaserCarry(carry);   // the scope's camera takes it too
        const float rel[3]={o[0]-view.origin[0]-carry[0],o[1]-view.origin[1]-carry[1],o[2]-view.origin[2]-carry[2]};
        const float along=rel[0]*f[0]+rel[1]*f[1]+rel[2]*f[2];
        float off=0;
        for(int j=0;j<3;++j) {const float p=rel[j]-f[j]*along;off+=p*p;}
        off=std::sqrt(off);
        const float step=std::sqrt(g_soldierStep[0]*g_soldierStep[0]+g_soldierStep[2]*g_soldierStep[2]);
        if(!std::isfinite(angle) || !std::isfinite(off)) return;
        g_laserScopeAngleLast.store(angle,std::memory_order_relaxed);PeakStore(g_laserScopeAnglePeak,angle);
        g_laserScopeOffsetLast.store(off,std::memory_order_relaxed);PeakStore(g_laserScopeOffsetPeak,off);
        if(std::isfinite(step)) PeakStore(g_laserScopeStepPeak,step);
        g_laserScopeSamples.fetch_add(1,std::memory_order_relaxed);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void* __fastcall HookLaserPrepare(void* attachment,void* a,void* b,void* c) {
    LaserAgainstScope(attachment);
    float* width=nullptr;float kept=0;
    __try {
        width=reinterpret_cast<float*>(static_cast<unsigned char*>(attachment)+0xB0);
        kept=*width;
        if(std::isfinite(kept) && kept>0 && g_laserWidthScale!=1.f) {
            *width=kept*g_laserWidthScale;
            g_sightLaserWidth.store(kept,std::memory_order_relaxed);
            g_sightLaserLent.fetch_add(1,std::memory_order_relaxed);
        } else width=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { width=nullptr; }
    void* result=nullptr;
    __try { result=g_laserPrepareOriginal(attachment,a,b,c); }
    __finally { if(width) *width=kept; GuideProbeSee(attachment,1,true); }
    return result;
}
// The vehicle aim line's width (see HookAimLineWidth): the setter's bytes and the
// one call to it are checked against the shipped code first.
bool InstallAimLineWidth(bool& changed) noexcept {
    changed=false;
    __try {
        constexpr unsigned char setter[]={0xF3,0x0F,0x11,0x89,0x34,0x01,0x00,0x00,0xC3};
        if(std::memcmp(g_image.base+0x687CE0,setter,sizeof(setter))) return false;
        g_aimLineWidthOriginal=reinterpret_cast<AimLineWidth>(g_image.base+0x687CE0);
        return edf6vr::RedirectCall(g_image.base+0x6B34C7,reinterpret_cast<void*>(g_aimLineWidthOriginal),
                                    reinterpret_cast<void*>(&HookAimLineWidth),changed);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool InstallGuideProbe(bool& changed) noexcept {
    changed=false;
    __try {
        auto guide=reinterpret_cast<void**>(g_image.base+0x17E2498+0x18);
        auto laser=reinterpret_cast<void**>(g_image.base+0x17E2458+0x18);
        if(*guide!=g_image.base+0x688680 || *laser!=g_image.base+0x688970) return false;
        g_guidePrepareOriginal=reinterpret_cast<GuidePrepare>(g_image.base+0x688680);
        g_laserPrepareOriginal=reinterpret_cast<GuidePrepare>(g_image.base+0x688970);
        if(!edf6vr::ReplacePointer(guide,reinterpret_cast<void*>(g_guidePrepareOriginal),
                                   reinterpret_cast<void*>(&HookGuidePrepare),changed)) return false;
        return edf6vr::ReplacePointer(laser,reinterpret_cast<void*>(g_laserPrepareOriginal),
                                      reinterpret_cast<void*>(&HookLaserPrepare),changed);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ReportGuideProbe() noexcept {
    GuideProbeEntry entries[kGuideProbeEntries]{};
    AcquireSRWLockExclusive(&g_guideProbeLock);
    for(unsigned i=0;i<kGuideProbeEntries;++i) {
        entries[i]=g_guideProbe[i];
        g_guideProbe[i].touched=false;
    }
    ReleaseSRWLockExclusive(&g_guideProbeLock);
    Log("LASERSCOPE samples=%llu angle peak=%.2fdeg last=%.2fdeg offset peak=%.3fm last=%.3fm step peak=%.3fm",
        g_laserScopeSamples.load(),g_laserScopeAnglePeak.exchange(0.f),g_laserScopeAngleLast.load(),
        g_laserScopeOffsetPeak.exchange(0.f),g_laserScopeOffsetLast.load(),g_laserScopeStepPeak.exchange(0.f));
    Log("SIGHTLINE laser x%.2f lent=%llu width=%.4f guide x%.2f thinned=%llu (0.02 -> %.4f) vehicle x%.2f lines=%llu width=%.3f",
        g_laserWidthScale,g_sightLaserLent.load(),g_sightLaserWidth.load(),g_guideWidthScale,g_sightGuideThinned.load(),
        kGuideLineWidth*g_guideWidthScale,g_vehicleLineScale,g_sightVehicleLines.load(),g_sightVehicleWidth.load());
    Log("GUIDEPROBE updates=%llu draws=%llu dropped=%llu dual=%d right=%p left=%p soldier=%p",
        g_guideProbeUpdates.load(),g_guideProbeDraws.load(),g_guideProbeDropped.load(),
        g_dualActive.load()?1:0,g_holdCommand.weapon,g_leftHoldCommand.weapon,g_vrSoldier);
    Log("GUIDECARRY line L/R=%llu/%llu refused=%llu noMuzzle=%llu noAim=%llu "
        "offset L/R=%.2f/%.2fm lob R=%.1fdeg turn L=%.1fdeg "
        "abovePalm L/R=%.2f/%.2fm offBarrel L/R=%.1f/%.1fdeg catchUp=%.2fm "
        "lead=%.3f..%.3f/step "
        "rayUpdates=%llu rays=%llu outOfStep=%llu",
        g_guideLineMoved[0].load(),g_guideLineMoved[1].load(),g_guideLineRefused.load(),
        g_guideNoMuzzle.load(),g_guideNoAim.load(),
        g_guideLineOffset[0].load(),g_guideLineOffset[1].load(),
        g_guideLob.load(),g_guideTurn.load(),
        g_guideAbovePalm[0].load(),g_guideAbovePalm[1].load(),
        g_guideOffBarrel[0].load(),g_guideOffBarrel[1].load(),
        g_guideCatchUp.load(),
        g_guideLead[0].load()>1e8f?0.0f:g_guideLead[0].load(),g_guideLead[1].load(),
        g_guideRayUpdates.load(),g_guideRaysMoved.load(),g_guideRayOutOfStep.load());
    // Least and most belong to the window just reported, not to the session.
    g_guideLead[0]=1e9f; g_guideLead[1]=0;
    static const char* const kMatch[]={"none","right","left","sub"};
    for(unsigned i=0;i<kGuideProbeEntries;++i) {
        const auto& e=entries[i];
        if(!e.attachment || !e.touched) continue;
        Log("GUIDEPROBE [%u] %s att=%p weapon=%p type=%s owner=%p match=%s tracked=%u "
            "upd=%llu/t%lu draw=%llu/t%lu prepared=%llu",
            i,e.klass?"laser":"guide",e.attachment,e.weapon,e.type?e.type:"?",e.owner,
            kMatch[e.match<4?e.match:0],e.tracked,e.updates,e.updateThread,e.draws,e.drawThread,e.prepared);
        Log("GUIDEPROBE [%u]   steps=%d speed=%.2f gravity=%.3f points=%llu visible=%u "
            "marker=%u hit=%u range=%.2f at=(%.2f,%.2f,%.2f) place=(%.2f,%.2f,%.2f)",
            i,e.steps,e.speed,e.gravity,e.points,e.visible,e.hasMarker,e.hit,e.markerRange,
            e.markerPoint[0],e.markerPoint[1],e.markerPoint[2],
            e.markerPlace[0],e.markerPlace[1],e.markerPlace[2]);
        Log("GUIDEPROBE [%u]   start=(%.2f,%.2f,%.2f) dir=(%.3f,%.3f,%.3f) carried=(%.2f,%.2f,%.2f) "
            "muzzle=(%.2f,%.2f,%.2f) offset=%.2fm turn=%.1fdeg",
            i,e.start[0],e.start[1],e.start[2],e.direction[0],e.direction[1],e.direction[2],
            e.carriedStart[0],e.carriedStart[1],e.carriedStart[2],
            e.muzzle[0],e.muzzle[1],e.muzzle[2],e.offset,e.turn);
    }
}
