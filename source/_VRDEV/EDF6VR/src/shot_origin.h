// Included by plugin.cpp inside its private namespace. Execution-breakpoint
// dispatch is by RVA, never by DR slot: FireSites can safely be reordered.
struct FreshShot {
    DWORD64 bullet=0,weapon=0;
    ULONGLONG born=0;
    DWORD thread=0;
    bool initialized=false,wrote=false,nativePose=false;
    float original[3]{},wanted[3]{};
};
constexpr unsigned kFreshShots=32;
FreshShot g_freshShots[kFreshShots]{};
unsigned g_freshNext=0;
struct FencerShotPath { DWORD64 weapon=0,caller=0; unsigned count=0; };
struct ShotInitReport {
    FencerShotPath fencer[16]{};
    unsigned constructed=0,initialized=0,verified=0,writes=0,rejected=0;
    DWORD64 bullet=0,weapon=0,ctorCaller=0;
    float original[3]{},wanted[3]{},world[3]{},simulation[3]{};
    bool wrote=false;
};
SRWLOCK g_shotReportLock=SRWLOCK_INIT;
ShotInitReport g_shotReport{};
// Where a Fencer shot goes against the reticle it is aimed by, per hand (0 left,
// 1 right), summed until the next report: the origin from the eye and the miss
// at the reticle's 15 m and at 50 m, along the aim frame's left (the hardware's
// +R, so the left weapon's origin reads positive) and up. A parallel offset
// misses by the same at both; a turned shot misses by more at 50 m.
struct FencerShotAim { unsigned shots=0,shoulder=0,builtAlong[3]{},byCarry=0; double origin[3]{},miss15[2]{},miss50[2]{},turn[2]{},residual=0,residualMax=0; };
FencerShotAim g_fencerShotAim[2]{};

DWORD64 OwnedShotWeapon(DWORD64 value,bool parameter) noexcept {
    __try {
        auto camera=static_cast<unsigned char*>(g_lastCamera);
        if(!edf6vr::Readable(camera,edf6vr::kSoldierOffset+8)) return 0;
        auto soldier=*reinterpret_cast<void**>(camera+edf6vr::kSoldierOffset);
        if(soldier!=*reinterpret_cast<void**>(camera+edf6vr::kSourceOffset)
           || !edf6vr::IsSupportedSoldier(g_image,soldier)) return 0;
        if(g_dualActive.load()) {
            const auto dual=ReadRangerDual();
            if(CurrentRangerPair(dual,soldier)) for(auto object:dual.weapons) {
                const auto p=reinterpret_cast<DWORD64>(object);
                if(parameter?(value==p+0x800 || value==p+0x9E0):value==p)return p;
            }
        }
        for(unsigned slot=0;slot<edf6vr::WeaponSlotCount(soldier);++slot) {
            edf6vr::WeaponPose weapon{};
            if(!edf6vr::ReadWeaponPose(soldier,slot,weapon)) continue;
            auto p=reinterpret_cast<DWORD64>(weapon.weapon);
            // 696FD0 selects InitParam at weapon+800, or +9E0 for its alternate path.
            if(parameter ? (value==p+0x800 || value==p+0x9E0) : value==p) return p;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

FreshShot* FindFreshShot(DWORD64 bullet) noexcept {
    for(auto& shot:g_freshShots)
        if(shot.bullet==bullet && shot.thread==GetCurrentThreadId()
           && GetTickCount64()-shot.born<1000) return &shot;
    return nullptr;
}

bool HandleShotSite(unsigned rva,CONTEXT* context) noexcept {
    __try {
        if(rva==0x696FD0) return OwnedShotWeapon(context->Rcx,false)!=0;
        if(rva==0x22E9C0) {
            const auto weapon=OwnedShotWeapon(context->Rdx,true);
            if(!weapon || !context->Rcx) return false;
            NoteRecoilShot(weapon);
            auto& shot=g_freshShots[g_freshNext++%kFreshShots];
            shot={}; shot.bullet=context->Rcx; shot.weapon=weapon;
            shot.born=GetTickCount64(); shot.thread=GetCurrentThreadId();
            shot.nativePose=FencerOwnsWeaponPose(*reinterpret_cast<void**>(static_cast<unsigned char*>(g_lastCamera)+edf6vr::kSoldierOffset));
            // A Fencer weapon riding a controller leaves its shot from the carried muzzle.
            if(shot.nativePose && g_fencerActive.load(std::memory_order_relaxed) && g_fencerHandWeapons
               && (weapon==reinterpret_cast<DWORD64>(g_fencerWeapons[0].load(std::memory_order_relaxed))
                   || weapon==reinterpret_cast<DWORD64>(g_fencerWeapons[1].load(std::memory_order_relaxed)))) shot.nativePose=false;
            if(g_dualTick.active && weapon==reinterpret_cast<DWORD64>(g_dualTick.weapon) && !g_dualTick.command.fencer) shot.nativePose=true;
            const auto caller=context->Rsp?*reinterpret_cast<DWORD64*>(context->Rsp):0;
            AcquireSRWLockExclusive(&g_shotReportLock);
            ++g_shotReport.constructed;
            g_shotReport.ctorCaller=caller;
            if(shot.nativePose) for(auto& path:g_shotReport.fencer) {
                if(!path.count || (path.weapon==weapon && path.caller==caller)) {
                    path.weapon=weapon; path.caller=caller; ++path.count; break;
                }
            }
            ReleaseSRWLockExclusive(&g_shotReportLock);
            if(g_watchBullets) {
                context->Dr3=shot.bullet+0xCC0;
                context->Dr7=(context->Dr7&~(0xFull<<28))|(1ull<<6)|(1ull<<28)|(3ull<<30);
            }
            return true;
        }
        if(rva==0x231CC0) {
            if(context->Rcx<0x140) return false;
            auto shot=FindFreshShot(context->Rcx-0x140);
            if(!shot || shot->initialized) return false;
            // Only the constructor's proven call. Do not patch arbitrary setters,
            // another object's matrix, or a reinitialization whose ABI is unknown.
            if(context->Rdx!=shot->bullet+0x60 || !context->Rsp
               || *reinterpret_cast<DWORD64*>(context->Rsp)!=reinterpret_cast<DWORD64>(g_image.base)+0x22EA4A
               || !edf6vr::Readable(reinterpret_cast<void*>(context->Rdx),64,true)) {
                AcquireSRWLockExclusive(&g_shotReportLock); ++g_shotReport.rejected; ReleaseSRWLockExclusive(&g_shotReportLock);
                return false;
            }
            auto at=reinterpret_cast<float*>(context->Rdx+0x30);
            for(int j=0;j<3;++j) {
                if(!std::isfinite(at[j])) return false;
                shot->original[j]=shot->wanted[j]=at[j];
            }
            shot->initialized=true;
            // The Fencer's left weapon fires along the game's one aim, from an
            // emitter of its own (not 696FD0), so its bullet is turned here to
            // the left aim: rows 0..2 of the matrix by the shortest arc from
            // its forward (row 2) to the shadow's smoothed direction. Position
            // is left where the mount put it. Right-weapon bullets are only
            // measured against the soldier's aim, to confirm row 2 is the way.
            int diagHand=-1,diagBuiltAlong=0; bool diagShoulder=false,diagByCarry=false; float diagAim[3]{},diagResidual=0;
            if(g_fencerActive.load(std::memory_order_relaxed)) {
                auto rows=reinterpret_cast<float(*)[4]>(context->Rdx);
                const float r2[3]={rows[2][0],rows[2][1],rows[2][2]};
                const float r2len=std::sqrt(r2[0]*r2[0]+r2[1]*r2[1]+r2[2]*r2[2]);
                if(std::isfinite(r2len) && r2len>0.5f) {
                    const float from[3]={r2[0]/r2len,r2[1]/r2len,r2[2]/r2len};
                    const bool isLeft=shot->weapon==reinterpret_cast<DWORD64>(g_fencerWeapons[0].load(std::memory_order_relaxed));
                    const bool isRight=!isLeft && shot->weapon==reinterpret_cast<DWORD64>(g_fencerWeapons[1].load(std::memory_order_relaxed));
                    void* soldier=*reinterpret_cast<void**>(static_cast<unsigned char*>(g_lastCamera)+edf6vr::kSoldierOffset);
                    edf6vr::SoldierAim aim{};
                    FencerShadow shadow{};
                    AcquireSRWLockShared(&g_fencerShadowLock); shadow=g_fencerShadow; ReleaseSRWLockShared(&g_fencerShadowLock);
                    if((isLeft || isRight) && soldier && edf6vr::ReadSoldierAim(soldier,aim)) {
                        // The game's own smoothed aim: inside the left tick the
                        // soldier holds the shadow, so take the saved one.
                        float smooth[4]{};
                        if(g_fencerSwapLive) std::memcpy(smooth,g_fencerSwapNative,16);
                        else std::memcpy(smooth,static_cast<unsigned char*>(soldier)+0x1240,16);
                        float nativeDir[3]{},want[3]{};
                        FencerAimForward(smooth[0],smooth[1],nativeDir);
                        if(isLeft && shadow.valid) FencerAimForward(shadow.smooth[0],shadow.smooth[1],want);
                        else std::memcpy(want,nativeDir,sizeof(want));
                        const unsigned hand=isLeft?0u:1u;
                        // The reticle's direction: the shadow's target for the left
                        // (PublishSecondAim), the soldier's target for the right.
                        diagHand=static_cast<int>(hand); diagShoulder=g_fencerShoulder[hand];
                        if(isLeft && shadow.valid) FencerAimForward(shadow.target[0],shadow.target[1],diagAim);
                        else FencerAimForward(aim.pitch,aim.yaw,diagAim);
                        const float cw=std::clamp(from[0]*want[0]+from[1]*want[1]+from[2]*want[2],-1.0f,1.0f);
                        if(isRight) {
                            const float c=std::clamp(from[0]*nativeDir[0]+from[1]*nativeDir[1]+from[2]*nativeDir[2],-1.0f,1.0f);
                            g_fencerRightRow2Deg.store(std::acos(c)*57.29578f,std::memory_order_relaxed);
                            g_fencerBulletRightChecks.fetch_add(1,std::memory_order_relaxed);
                        }
                        // Blades and hammers sweep their hit along their own rows: left alone.
                        const bool sweeps=edf6vr::HasType(g_image,reinterpret_cast<void*>(shot->weapon),".?AVWeapon_Swing@@")
                            || edf6vr::HasType(g_image,reinterpret_cast<void*>(shot->weapon),".?AVWeapon_ImpactHammer@@")
                            || edf6vr::HasType(g_image,reinterpret_cast<void*>(shot->weapon),".?AVWeapon_Shield@@");
                        // 45 degrees was the angle the two aims were opened to,
                        // not a broken matrix: a hand weapon is posed on the
                        // game's aim and wants its own, so the gap IS the split.
                        // It was worth refusing while a reversed axis map could
                        // put 178 degrees through here; with that fixed the
                        // bound only stops the player from opening his hands.
                        // Past a half turn there is no shortest arc, so stop
                        // short of one: 135 degrees is past anything reachable.
                        const bool handShot=g_fencerHandWeapons && !g_fencerShoulder[hand] && !sweeps;
                        constexpr float kAlignFloor=-0.996f;   // 175 degrees
                        // Down the barrel as it was drawn, not an aim read
                        // again a tick later: the drawn weapon's row 2 is its
                        // +Z, which is the barrel, so this is exactly where
                        // the player sees it pointing.
                        float barrel[3]={want[0],want[1],want[2]};
                        // The direction the game aimed this shot along, before its
                        // accuracy cone: the soldier's aim for the right, and for
                        // the left whichever of that and the shadow it was built
                        // nearer. Turning the shot itself onto the barrel took the
                        // cone out with it -- the hand gatling fired in one straight
                        // line (2026-09-26). Turned from where it was aimed, the
                        // cone comes through the turn, as the Ranger's left gun's does.
                        //
                        // The left's shot is not built along the shadow, as was
                        // assumed, but turned by the gap between the two aims
                        // twice: the swapped aim turns it once and the weapon
                        // matrix carried onto the left hand turns it again. On
                        // hardware every left weapon shot off its reticle by as
                        // much as the hands were apart -- 9 degrees at 9 apart,
                        // 33 at 35, 1 with the hands together (FENCERSHOTAIM,
                        // 2026-09-26) -- and to the side the left hand was
                        // opened to. So the doubled direction is a third
                        // candidate, and the nearest of the three is taken.
                        //
                        // The second turn is the carry's own (FencerCarryTurnApply:
                        // the weapon's root onto the drawn hand), not the arc from
                        // one aim to the other. That arc left the hand's roll out,
                        // and with the hands opened -- when the left hand also
                        // rolls -- the left gatling fired down (-17 to -25 degrees,
                        // 2026-09-26) while its sideways error had gone.
                        float aimed[3]={nativeDir[0],nativeDir[1],nativeDir[2]};
                        int builtAlong=0;   // 0 the soldier's aim, 1 the shadow, 2 the shadow turned twice
                        if(isLeft && shadow.valid) {
                            float twice[3][4]={{want[0],want[1],want[2],0},{0,1,0,0},{0,0,1,0}};
                            float carried[3]{};
                            const bool byCarry=FencerCarryTurnApply(0,reinterpret_cast<void*>(shot->weapon),want,carried);
                            if(byCarry) for(int j=0;j<3;++j) twice[0][j]=carried[j];
                            diagByCarry=byCarry;
                            const bool haveTwice=byCarry || FencerTurnRows(twice,nativeDir,want);
                            const float toNative=from[0]*nativeDir[0]+from[1]*nativeDir[1]+from[2]*nativeDir[2];
                            const float toShadow=from[0]*want[0]+from[1]*want[1]+from[2]*want[2];
                            const float toTwice=haveTwice?from[0]*twice[0][0]+from[1]*twice[0][1]+from[2]*twice[0][2]:-2.0f;
                            if(toTwice>toShadow && toTwice>toNative) { for(int j=0;j<3;++j) aimed[j]=twice[0][j]; builtAlong=2; }
                            else if(toShadow>toNative) { std::memcpy(aimed,want,sizeof(aimed)); builtAlong=1; }
                        }
                        diagBuiltAlong=builtAlong;
                        diagResidual=std::acos(std::clamp(from[0]*aimed[0]+from[1]*aimed[1]+from[2]*aimed[2],-1.0f,1.0f))*57.29578f;
                        float cb=cw;
                        if(handShot && FencerDrawnBarrel(hand,reinterpret_cast<void*>(shot->weapon),barrel))
                            cb=std::clamp(aimed[0]*barrel[0]+aimed[1]*barrel[1]+aimed[2]*barrel[2],-1.0f,1.0f);
                        // 135 was still a wall the player could reach. The gap
                        // is the split he asked for, so the only angle that has
                        // to be refused is the half turn itself, where there is
                        // no shortest arc to turn along.
                        if(handShot && cb<=kAlignFloor) {
                            g_fencerBulletFarOff[hand].fetch_add(1,std::memory_order_relaxed);
                            const float off=std::acos(cb)*57.29578f;
                            if(off>g_fencerBulletFarOffDeg[hand].load(std::memory_order_relaxed))
                                g_fencerBulletFarOffDeg[hand].store(off,std::memory_order_relaxed);
                        }
                        if(handShot && cb>kAlignFloor) {
                            // A weapon carried in the hand shoots straight along
                            // its hand's aim. Its bullet comes out of the carried
                            // weapon matrix, whose model axes were snapped onto
                            // the aim and so lose the model's own tilt (the left
                            // spear shot off its reticle).
                            if(cb<0.99999f && FencerTurnRows(rows,aimed,barrel)) {
                                g_fencerBulletAligned[hand].fetch_add(1,std::memory_order_relaxed);
                                g_fencerBulletAlignDeg[hand].store(std::acos(cb)*57.29578f,std::memory_order_relaxed);
                            }
                        } else if(isLeft && shadow.valid) {
                            // Shoulder weapons keep their lob: the arc from the
                            // direction it was built along to the shadow's, and
                            // nothing where it was built along the shadow already.
                            const float c=std::clamp(aimed[0]*want[0]+aimed[1]*want[1]+aimed[2]*want[2],-1.0f,1.0f);
                            if(builtAlong==1) g_fencerBulletSkips.fetch_add(1,std::memory_order_relaxed);
                            else if(FencerTurnRows(rows,aimed,want)) {
                                g_fencerBulletTurns.fetch_add(1,std::memory_order_relaxed);
                                g_fencerBulletTurnDeg.store(std::acos(c)*57.29578f,std::memory_order_relaxed);
                            }
                        }
                    }
                }
            }
            if(g_dualShot.active && shot->weapon==reinterpret_cast<DWORD64>(g_dualTick.weapon)) {
                const auto hand=g_dualShot.hand;
                for(unsigned j=0;j<3;++j)g_dualInitPosition[hand][j]=at[j];
                ++g_dualInitializers[hand];
                // The left gun's shot leaves along the weapon transform carried
                // onto the hand, and that carry divides by the model's node root
                // -- the animation. Drawing the left gun while the right one
                // reloads swings that root, and the log caught shots leaving up
                // to 34 degrees off the hand where the ordinary figure is 0.0 to
                // 0.2 (RANGERDUAL offHand/worst). The rotation is systematic, so
                // it is taken out by turning the bullet from the direction this
                // shot was aimed along onto the hand's own forward: the accuracy
                // cone, which was added after that direction, is carried through
                // the same turn and survives. The right gun leaves along the
                // game's aim and is not touched.
                if(hand==0 && !g_dualShot.fencer && g_dualShot.haveFireTurn) {
                    float cosine=0;
                    for(int j=0;j<3;++j) cosine+=g_dualShot.fireFrom[j]*g_dualShot.fireTo[j];
                    cosine=std::clamp(cosine,-1.0f,1.0f);
                    // Under a quarter of a degree there is nothing worth taking
                    // out. There is no upper limit: the whole point is that the
                    // animation's excursion can be large, and the direction it
                    // is turned onto was built without the animation.
                    auto rows=reinterpret_cast<float(*)[4]>(context->Rdx);
                    if(cosine<0.99999f && FencerTurnRows(rows,g_dualShot.fireFrom,g_dualShot.fireTo)) {
                        g_dualAimTurnDeg.store(std::acos(cosine)*57.29578f,std::memory_order_relaxed);
                        g_dualAimTurns.fetch_add(1,std::memory_order_relaxed);
                    }
                }
            }
            const bool fencerHand=g_fencerActive.load(std::memory_order_relaxed) && g_fencerHandWeapons
                && (shot->weapon==reinterpret_cast<DWORD64>(g_fencerWeapons[0].load(std::memory_order_relaxed))
                    || shot->weapon==reinterpret_cast<DWORD64>(g_fencerWeapons[1].load(std::memory_order_relaxed)));
            if(!shot->nativePose && g_shotFromMuzzle
               && ((g_muzzleWantValid && shot->weapon==reinterpret_cast<DWORD64>(g_weaponObject)) || fencerHand)) {
                float current[3]{};
                const bool fresh=ResolveTrackedMuzzle(reinterpret_cast<void*>(shot->weapon),current,GetTickCount64());
                float distance=0;
                for(int j=0;j<3;++j) {
                    const float d=current[j]-at[j]; distance+=d*d;
                }
                if(fresh && std::isfinite(distance) && distance<25.0f) {
                    for(int j=0;j<3;++j) shot->wanted[j]=current[j];
                    shot->wrote=true;
                }
            } else if(!shot->nativePose && !g_shotFromMuzzle && g_fireOriginLift!=0) {
                shot->wanted[1]+=g_fireOriginLift;
                shot->wrote=std::isfinite(shot->wanted[1]);
            }
            if(shot->wrote) for(int j=0;j<3;++j) at[j]=shot->wanted[j];
            if(diagHand>=0) {
                // The shot as it now leaves, against its reticle.
                const auto rows=reinterpret_cast<const float(*)[4]>(context->Rdx);
                float d[3]={rows[2][0],rows[2][1],rows[2][2]};
                const float dl=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                const float al=std::sqrt(diagAim[0]*diagAim[0]+diagAim[1]*diagAim[1]+diagAim[2]*diagAim[2]);
                if(std::isfinite(dl) && dl>0.5f && std::isfinite(al) && al>0.5f) {
                    for(float& v:d) v/=dl;
                    float a[3]={diagAim[0]/al,diagAim[1]/al,diagAim[2]/al};
                    const float yaw=std::atan2(a[0],a[2]);
                    const float L[3]={std::cos(yaw),0,-std::sin(yaw)};
                    // up: square to the aim and to L
                    const float U[3]={a[1]*L[2]-a[2]*L[1],a[2]*L[0]-a[0]*L[2],a[0]*L[1]-a[1]*L[0]};
                    const float o[3]={at[0]-g_eyeWorld[0],at[1]-g_eyeWorld[1],at[2]-g_eyeWorld[2]};
                    const auto dot=[](const float* x,const float* y) { return x[0]*y[0]+x[1]*y[1]+x[2]*y[2]; };
                    const float along=dot(d,a);
                    if(along>0.2f) {
                        AcquireSRWLockExclusive(&g_shotReportLock);
                        auto& m=g_fencerShotAim[diagHand];
                        ++m.shots; if(diagShoulder) ++m.shoulder;
                        ++m.builtAlong[diagBuiltAlong<0 || diagBuiltAlong>2?0:diagBuiltAlong];
                        if(diagByCarry) ++m.byCarry;
                        if(std::isfinite(diagResidual)) { m.residual+=diagResidual; if(diagResidual>m.residualMax) m.residualMax=diagResidual; }
                        m.origin[0]+=dot(o,L); m.origin[1]+=dot(o,U); m.origin[2]+=dot(o,a);
                        const float range[2]={15.0f,50.0f};
                        for(int r=0;r<2;++r) {
                            const float t=(range[r]-dot(o,a))/along;
                            const float miss[3]={o[0]+d[0]*t-a[0]*range[r],o[1]+d[1]*t-a[1]*range[r],o[2]+d[2]*t-a[2]*range[r]};
                            double* into=r?m.miss50:m.miss15;
                            into[0]+=dot(miss,L); into[1]+=dot(miss,U);
                        }
                        m.turn[0]+=std::asin(std::clamp(dot(d,L),-1.0f,1.0f))*57.29578f;
                        m.turn[1]+=std::asin(std::clamp(dot(d,U),-1.0f,1.0f))*57.29578f;
                        ReleaseSRWLockExclusive(&g_shotReportLock);
                    }
                }
            }
            // Where the firing sound should come from, for the one voice that
            // starts in the moments after this.
            //
            // Of the weapon that fired, not of the primary one: with the Ranger
            // pair out, g_muzzleWant is the right hand and the left hand's shot
            // was arriving from it. ResolveTrackedMuzzle already picks the frame
            // that belongs to the weapon it is given.
            {
                float fired[3]{};
                const auto when=GetTickCount64();
                if(ResolveTrackedMuzzle(reinterpret_cast<void*>(shot->weapon),fired,when)) {
                    for(int j=0;j<3;++j)
                        g_lastFireMuzzle[j].store(fired[j],std::memory_order_relaxed);
                    g_lastFireAt.store(when,std::memory_order_release);
                }
            }
            // The game now initializes position, velocity and collision state from
            // its own matrix. No post-spawn CC0 write, no fixed-per-frame teleport.
            AcquireSRWLockExclusive(&g_shotReportLock);
            ++g_shotReport.initialized;
            if(shot->wrote) ++g_shotReport.writes;
            ReleaseSRWLockExclusive(&g_shotReportLock);
            return true;
        }
        if(rva==0x22EA4A) {
            auto shot=FindFreshShot(context->Rsi); // constructor's nonvolatile this
            if(!shot || !shot->initialized) return false;
            float world[3]{},simulation[3]{};
            for(int j=0;j<3;++j) {
                world[j]=reinterpret_cast<float*>(shot->bullet+0x90)[j];
                simulation[j]=reinterpret_cast<float*>(shot->bullet+0xCC0)[j];
            }
            AcquireSRWLockExclusive(&g_shotReportLock);
            ++g_shotReport.verified; g_shotReport.bullet=shot->bullet; g_shotReport.weapon=shot->weapon;
            g_shotReport.wrote=shot->wrote;
            for(int j=0;j<3;++j) {
                g_shotReport.original[j]=shot->original[j]; g_shotReport.wanted[j]=shot->wanted[j];
                g_shotReport.world[j]=world[j]; g_shotReport.simulation[j]=simulation[j];
            }
            ReleaseSRWLockExclusive(&g_shotReportLock);
            shot->bullet=0; // exactly once; recycled objects need another constructor
            return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

void ReportShotInit() noexcept {
    ShotInitReport report{};
    FencerShotAim aims[2]{};
    AcquireSRWLockExclusive(&g_shotReportLock);
    report=g_shotReport;
    for(int h=0;h<2;++h) { aims[h]=g_fencerShotAim[h]; g_fencerShotAim[h]={}; }
    ReleaseSRWLockExclusive(&g_shotReportLock);
    for(int h=0;h<2;++h) if(aims[h].shots) {
        const double n=aims[h].shots;
        Log("FENCERSHOTAIM %s shots=%u shoulder=%u builtAlong aim/shadow/twice=%u/%u/%u byCarry=%u residual=%.2f(max %.2f)deg "
            "origin left/up/ahead=%.2f/%.2f/%.2fm turn left/up=%.2f/%.2fdeg "
            "miss@15m left/up=%.2f/%.2fm miss@50m left/up=%.2f/%.2fm (left = the hardware's +R)",
            h?"R":"L",aims[h].shots,aims[h].shoulder,aims[h].builtAlong[0],aims[h].builtAlong[1],aims[h].builtAlong[2],
            aims[h].byCarry,aims[h].residual/n,aims[h].residualMax,aims[h].origin[0]/n,aims[h].origin[1]/n,aims[h].origin[2]/n,
            aims[h].turn[0]/n,aims[h].turn[1]/n,aims[h].miss15[0]/n,aims[h].miss15[1]/n,aims[h].miss50[0]/n,aims[h].miss50[1]/n);
    }
    static unsigned shown=0;
    if(report.constructed==shown) return;
    shown=report.constructed;
    const auto base=reinterpret_cast<DWORD64>(g_image.base);
    Log("SHOTINIT ctor=%u init=%u verified=%u writes=%u rejected=%u bullet=%llX weapon=%llX ctorCaller=+%llX wrote=%d",
        report.constructed,report.initialized,report.verified,report.writes,report.rejected,
        report.bullet,report.weapon,report.ctorCaller>=base?report.ctorCaller-base:0,report.wrote?1:0);
    for(const auto& path:report.fencer) if(path.count)
        Log("FENCERSHOT weapon=%llX caller=+%llX constructed=%u nativeOrigin=1",
            path.weapon,path.caller>=base?path.caller-base:0,path.count);
    Log("SHOTINIT original=(%.4f,%.4f,%.4f) wanted=(%.4f,%.4f,%.4f) world=(%.4f,%.4f,%.4f) simulation=(%.4f,%.4f,%.4f)",
        report.original[0],report.original[1],report.original[2],report.wanted[0],report.wanted[1],report.wanted[2],
        report.world[0],report.world[1],report.world[2],report.simulation[0],report.simulation[1],report.simulation[2]);
}
