// Included in plugin.cpp's private namespace after class_support.h.
// MuzzleFlash01/02 submit their own world matrix at effect+60. Scale a
// caller-local copy at the two mesh draw CALLs, never a weapon/bone/shot field.
std::atomic<float> g_muzzleFlashScale{0.5f};
std::atomic<unsigned long long> g_flashDraws{0},g_flashOwned{0},g_flashScaled{0};
using FlashMeshDraw=void(__fastcall*)(void*,void*,const edf6vr::Matrix*,void*,unsigned,bool,float,void*);
FlashMeshDraw g_originalFlashMesh=nullptr;
constexpr unsigned kFlashMeshTarget=0x11548F0;
constexpr unsigned kFlashMeshCalls[]={0x2BFC02,0x2C0D32};

// Native flash preparation copies the retained attachment pointer (+120) to
// effect+60 AFTER the temporary firing pose has been restored. Correct both
// that draw matrix and its bounds at the two synchronous bounds submissions.
// Do not replace the retained pointer with stack storage or move weapon nodes.
using FlashBounds=void(__fastcall*)(void*,const edf6vr::Matrix*);
FlashBounds g_originalFlashBounds=nullptr;
constexpr unsigned kFlashBoundsTarget=0x11B30C0;
constexpr unsigned kFlashBoundsCalls[]={0x2BFC89,0x2C0DB9};
std::atomic<unsigned long long> g_flashLeftPrepared{0},g_flashLeftRejected{0};
// Why a carry was refused, so one run says which gate is dropping the
// Fencer's flash back onto the body instead of the held weapon. The counter
// alone only said that a quarter of them were refused.
// 0 not our weapon  1 casing frame stale/mismatched  2 effect class
// 3 already carried  4 skeleton moved  5 out of range  6 carry failed
std::atomic<unsigned long long> g_flashLeftWhy[7]{};
// Where the carried flash ended up against the muzzle the weapon was drawn
// with, per hand. Two guesses at this have now missed, so measure it: if the
// flash is landing on the hand's line while the weapon is drawn a few degrees
// off it, the gap is the weapon's misalignment times the lever, and it grows
// as the aim swings off centre -- which is what was reported.
std::atomic<float> g_flashGapLast[2]{},g_flashGapMax[2]{};
std::atomic<float> g_flashGapUp[2]{};
std::atomic<unsigned long long> g_flashGapSamples[2]{};
// Does the carried matrix still stand when the mesh is drawn? The bounds
// hook writes it into effect+0x60 and the mesh draw is handed that same
// address, so it should -- unless the game writes the native attachment back
// in between, in which case everything carried here is thrown away and the
// flash is drawn on the body's weapon, which sits higher than the held one.
struct FlashCarryMark { const void* effect=nullptr; float at[3]{}; unsigned hand=2;
    edf6vr::Matrix carried{}; ULONGLONG at_time=0; };
FlashCarryMark g_flashMarks[8]{};
unsigned g_flashMarkNext=0;
std::atomic<unsigned long long> g_flashCarryKept[2]{},g_flashCarryLost[2]{};
std::atomic<float> g_flashCarryLostBy[2]{};
// Which hand each carry belonged to. The left flash was reported to follow
// the RIGHT hand and to sit about 30 cm above the muzzle, narrowing as the
// aim rises -- the shape of an offset that is fixed to the body rather than
// to the weapon, i.e. a flash left where the game had it. If the left's
// carries are the ones being written back over, that is exactly this.
std::atomic<unsigned> g_flashLastHand{2};
// Where the carried flash actually lands, against the two anchors we know:
// the palm it was carried onto and the root it was carried from. If it sits
// on the palm then the weapon's own muzzle offset is being dropped; if it
// sits on the root the carry is not moving it at all.
std::atomic<float> g_flashToPalm[2]{},g_flashToRoot[2]{};
using EfsFlashCreate=void*(__fastcall*)(void*,void*,void*);
FlashBounds g_originalEfsFlashUpdate=nullptr;
EfsFlashCreate g_originalEfsFlashCreate=nullptr;
constexpr unsigned kEfsFlashUpdateCalls[]={0x2BA9C0,0x2BAF60};
constexpr unsigned kEfsFlashUpdateTarget=0x2BB020,kEfsFlashCreateCall=0x2BAD96,kEfsFlashCreateTarget=0x2EC5C0;
std::atomic<unsigned long long> g_efsFlashUpdates{0},g_efsFlashLeftUpdates{0},g_efsFlashLeftCreates{0};

bool PrepareLeftFlash(void* secondary,const edf6vr::Matrix* source,
                      edf6vr::Matrix& wanted,ULONGLONG now,bool efs=false) noexcept {
    // The Ranger's left gun, or either of the Fencer's weapons when they ride
    // the controllers (fencer_dual.h): the flash is carried from the weapon's
    // native root to the hand the same way the weapon is drawn.
    const bool fencer=g_fencerActive.load(std::memory_order_relaxed) && g_fencerHandWeapons;
    if(!secondary || !source || !(g_dualActive.load() || fencer) || g_nativeTactical || (!fencer && g_weaponArmBone)) return false;
    // Counted only while one of ours is actually in a hand.
    auto why=[&](int n)->bool { ++g_flashLeftWhy[n]; return false; };
    __try {
        const auto address=reinterpret_cast<std::uintptr_t>(secondary);
        if(address<0x130) return false;
        auto effect=reinterpret_cast<unsigned char*>(address-0x130);
        if(!edf6vr::Readable(effect,0x128,true) || !edf6vr::Readable(source,64)) return false;
        const auto parent=*reinterpret_cast<void**>(effect+0x38);
        if(!parent) return false;
        unsigned hand=2;
        if(fencer) {
            if(parent==g_fencerWeapons[0].load(std::memory_order_relaxed)) hand=0;
            else if(parent==g_fencerWeapons[1].load(std::memory_order_relaxed)) hand=1;
        } else if(parent==g_dualWeapons[0].load()) hand=0;
        if(hand>1) return why(0);
        CasingFrame frame{};
        AcquireSRWLockShared(&g_classLock); frame=hand==0?g_leftCasingFrame:g_casingFrame; ReleaseSRWLockShared(&g_classLock);
        if(frame.weapon!=parent || !frame.soldier || now<frame.time || now-frame.time>250) return why(1);
        if(!fencer) {
            const auto state=ReadRangerDual();
            if(frame.soldier!=state.soldier || frame.objectId!=state.id || parent!=state.weapons[0]
               || !CurrentRangerPair(state,frame.soldier)) return false;
        }
        if((efs ? *reinterpret_cast<void**>(effect)!=g_image.base+0x17A6068
                   : (*reinterpret_cast<void**>(effect)!=g_image.base+0x17A6478
                      && *reinterpret_cast<void**>(effect)!=g_image.base+0x17A6578))) return why(2);
        // If preparation ever runs synchronously inside emission, its native
        // source is already carried. Never apply the same transform twice.
        if(g_dualShot.active && g_dualTick.weapon==parent) return why(3);
        auto weapon=static_cast<unsigned char*>(parent);
        auto soldier=static_cast<unsigned char*>(frame.soldier);
        auto registry=weapon+edf6vr::kWeaponModelOffset+0xA0;
        auto nodes=*reinterpret_cast<unsigned char**>(registry+0x10);
        const auto count=*reinterpret_cast<std::uint64_t*>(registry+0x20);
        if(!nodes || nodes!=frame.nodes || !count || count>64 || !edf6vr::Readable(nodes,0xF0)) return why(4);
        // The root the DRAW used, when the draw left one behind. Falls back
        // to the animation node for a publisher that carries none.
        const auto nativeRoot=frame.weaponRootValid?frame.weaponRoot
            :*reinterpret_cast<const edf6vr::Matrix*>(nodes+0xB0);
        float travel=0,reach=0,sourceReach=0;
        for(unsigned j=0;j<3;++j) {
            const float root=*reinterpret_cast<float*>(soldier+0x90+j*4);
            const float delta=root-frame.root[j],local=frame.hand.palm[j]-frame.root[j];
            const float lever=source->m[3][j]-nativeRoot.m[3][j];
            if(!std::isfinite(delta) || !std::isfinite(local) || !std::isfinite(lever)) return false;
            travel+=delta*delta;reach+=local*local;sourceReach+=lever*lever;
            frame.hand.palm[j]=root+local;
        }
        if(travel>=400 || reach>=25 || sourceReach>=25) return why(5);
        // The weapon's own motion, when the draw left both ends of it: the
        // flash is bolted to the weapon, so it takes the same rigid move.
        // Rebuilding a move of its own from the hand frame put it about 30
        // degrees off, which at a gatling's 1.1 m barrel is 60 cm of daylight
        // between the flash and the muzzle.
        if(frame.weaponRootValid && frame.weaponDrawnValid) {
            const auto& from=frame.weaponRoot; const auto& to=frame.weaponDrawn;
            edf6vr::Matrix back{};   // rigid inverse of `from`, rows are its axes
            for(int i=0;i<3;++i) for(int j=0;j<3;++j) back.m[i][j]=from.m[j][i];
            for(int j=0;j<3;++j)
                back.m[3][j]=-(from.m[3][0]*back.m[0][j]+from.m[3][1]*back.m[1][j]
                              +from.m[3][2]*back.m[2][j]);
            back.m[3][3]=1;
            edf6vr::Matrix step{};   // back then to
            for(int i=0;i<4;++i) for(int j=0;j<4;++j) {
                float sum=0;
                for(int k=0;k<4;++k) sum+=back.m[i][k]*to.m[k][j];
                step.m[i][j]=sum;
            }
            for(int i=0;i<4;++i) for(int j=0;j<4;++j) {
                float sum=0;
                for(int k=0;k<4;++k) sum+=source->m[i][k]*step.m[k][j];
                wanted.m[i][j]=sum;
            }
            for(int i=0;i<4;++i) for(int j=0;j<4;++j)
                if(!std::isfinite(wanted.m[i][j])) return why(6);
        } else {
            float before=0,after=0;
            if(!edf6vr::CarryWeaponBones(nativeRoot,frame.hand,source,1,&wanted,before,after)) return why(6);
        }
        // Measurement only; nothing below changes what is drawn.
        MuzzleFrame muzzle{};
        AcquireSRWLockShared(&g_classLock);
        muzzle=hand==0?g_leftMuzzleFrame:g_muzzleFrame;
        ReleaseSRWLockShared(&g_classLock);
        if(muzzle.weapon==parent && now>=muzzle.time && now-muzzle.time<=250) {
            float gap2=0;
            for(int j=0;j<3;++j) { const float d=wanted.m[3][j]-muzzle.point[j]; gap2+=d*d; }
            const float gap=std::sqrt(gap2);
            if(std::isfinite(gap)) {
                g_flashGapLast[hand].store(gap,std::memory_order_relaxed);
                g_flashGapUp[hand].store(wanted.m[3][1]-muzzle.point[1],std::memory_order_relaxed);
                if(gap>g_flashGapMax[hand].load(std::memory_order_relaxed))
                    g_flashGapMax[hand].store(gap,std::memory_order_relaxed);
                ++g_flashGapSamples[hand];
            }
            // Sit the flash on the muzzle the shot actually leaves from.
            // Carrying it by the weapon's motion should land there and did
            // not; this is the same point the bullet origin uses, so the two
            // cannot disagree. The carried rotation is kept -- only where it
            // sits is taken from the muzzle.
            if(fencer && std::isfinite(muzzle.point[0]) && std::isfinite(muzzle.point[1])
               && std::isfinite(muzzle.point[2]))
                for(int j=0;j<3;++j) wanted.m[3][j]=muzzle.point[j];
        }
        float toPalm=0,toRoot=0;
        for(int j=0;j<3;++j) {
            const float p=wanted.m[3][j]-frame.hand.palm[j];
            const float r=wanted.m[3][j]-nativeRoot.m[3][j];
            toPalm+=p*p; toRoot+=r*r;
        }
        g_flashLastHand.store(hand,std::memory_order_relaxed);
        g_flashToPalm[hand].store(std::sqrt(toPalm),std::memory_order_relaxed);
        g_flashToRoot[hand].store(std::sqrt(toRoot),std::memory_order_relaxed);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// EfsMuzzleFlash owns a child-effect group at +130, not a mesh renderer.
// Its native updater synchronously copies the attachment into live children
// (including native local-offset transforms). Pass the carried root to that
// updater; do not traverse children or change their lifetime/size settings.
void __fastcall HookEfsFlashUpdate(void* group,const edf6vr::Matrix* source) {
    ++g_efsFlashUpdates;
    alignas(16) edf6vr::Matrix wanted{};
    if(PrepareLeftFlash(group,source,wanted,GetTickCount64(),true)) {
        ++g_efsFlashLeftUpdates;
        g_originalEfsFlashUpdate(group,&wanted);
    } else g_originalEfsFlashUpdate(group,source);
}

void* __fastcall HookEfsFlashCreate(void* group,void* output,void* parameter) {
    alignas(16) edf6vr::Matrix wanted{},saved{};
    if(!edf6vr::Readable(parameter,sizeof(saved),true)
       || !PrepareLeftFlash(group,static_cast<const edf6vr::Matrix*>(parameter),wanted,GetTickCount64(),true))
        return g_originalEfsFlashCreate(group,output,parameter);
    // This exact CALL passes the constructor's stack InitParam. Borrow only
    // its first matrix and restore it even on native exceptions; every other
    // parameter, native output and return value retains its original identity.
    std::memcpy(&saved,parameter,sizeof(saved));
    __try {
        std::memcpy(parameter,&wanted,sizeof(wanted));
        ++g_efsFlashLeftCreates;
        return g_originalEfsFlashCreate(group,output,parameter);
    } __finally { std::memcpy(parameter,&saved,sizeof(saved)); }
}

void __fastcall HookFlashBounds(void* secondary,const edf6vr::Matrix* source) {
    alignas(16) edf6vr::Matrix wanted{};
    if(PrepareLeftFlash(secondary,source,wanted,GetTickCount64())) {
        auto effect=static_cast<unsigned char*>(secondary)-0x130;
        std::memcpy(effect+0x60,&wanted,sizeof(wanted));
        auto& mark=g_flashMarks[g_flashMarkNext++&7];
        mark.effect=effect;
        mark.hand=g_flashLastHand.load(std::memory_order_relaxed);
        mark.carried=wanted; mark.at_time=GetTickCount64();
        for(int j=0;j<3;++j) mark.at[j]=wanted.m[3][j];
        ++g_flashLeftPrepared;
        // Same transformed pose for culling and both eyes' later mesh draws.
        // 11B30C0 consumes its matrix synchronously; native passes a stack copy.
        g_originalFlashBounds(secondary,&wanted);
    } else {
        if(g_dualActive.load() || g_fencerActive.load(std::memory_order_relaxed)) ++g_flashLeftRejected;
        g_originalFlashBounds(secondary,source);
    }
}

bool PrepareMuzzleFlash(const edf6vr::Matrix* source,edf6vr::Matrix& scaled,ULONGLONG now) noexcept {
    const float scale=g_muzzleFlashScale.load(std::memory_order_relaxed);
    if(!source || !std::isfinite(scale) || scale<0.1f || scale>=1.0f || g_nativeTactical) return false;
    __try {
        const auto address=reinterpret_cast<std::uintptr_t>(source);
        if(address<0x60) return false;
        const auto effect=reinterpret_cast<const unsigned char*>(address-0x60);
        if(!edf6vr::Readable(effect,0x128))return false;
        const auto parent=*reinterpret_cast<void* const*>(effect+0x38);
        MuzzleFrame frame{};
        AcquireSRWLockShared(&g_classLock);frame=parent==g_leftMuzzleFrame.weapon?g_leftMuzzleFrame:g_muzzleFrame;ReleaseSRWLockShared(&g_classLock);
        if(!frame.weapon || !frame.soldier || now<frame.time || now-frame.time>250)return false;
        const auto soldier=static_cast<const unsigned char*>(frame.soldier);
        const auto weapon=static_cast<const unsigned char*>(frame.weapon);
        // 68DB95: weapon+4B8 (InitParam+30) = weapon.
        // 2BA55A -> 118AF20: child effect+38 = that exact parent.
        // Only accept known mesh-flash classes and the live local owner's id.
        if(!edf6vr::Readable(effect,0x128) || !edf6vr::Readable(weapon,0x128)
           || !edf6vr::Readable(soldier,0x318)
           || (*reinterpret_cast<void* const*>(effect)!=g_image.base+0x17A6478
               && *reinterpret_cast<void* const*>(effect)!=g_image.base+0x17A6578)
           || *reinterpret_cast<void* const*>(effect+0x38)!=frame.weapon
           || *reinterpret_cast<void* const*>(weapon+0x120)!=frame.soldier
           // Every player class. The Ranger was the only one shrunk, on the
           // reasoning that he alone brings the weapon to his shoulder -- but
           // the Fencer's gatling turned out to read as far too big as well,
           // so the reasoning was wrong: in VR the flash is close whatever the
           // stance. Wing Diver and Air Raider are in on the same ground; they
           // hold theirs at the controllers too.
           || !edf6vr::IsSupportedSoldier(g_image,soldier)
           || *reinterpret_cast<const std::uint32_t*>(soldier+0x314)!=frame.objectId) return false;
        ++g_flashOwned;
        const edf6vr::Matrix native=*source;
        for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j)
            if(!std::isfinite(native.m[i][j])) return false;
        scaled=native;
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) scaled.m[i][j]*=scale;
        // Translation and homogeneous lanes remain native. Never scale the
        // distance from the player/world origin or write into source storage.
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall HookFlashMesh(void* renderer,void* context,const edf6vr::Matrix* source,
                             void* mesh,unsigned flags,bool option,float value,void* extra) {
    ++g_flashDraws;
    // The draw is handed effect+0x60 itself, which is where the bounds hook
    // put the carried matrix. Measurement only.
    const edf6vr::Matrix* carriedNow=nullptr;
    if(source && edf6vr::Readable(source,64)) {
        const auto effect=reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(source)-0x60);
        for(auto& mark:g_flashMarks) if(mark.effect==effect) {
            float square=0;
            for(int j=0;j<3;++j) { const float d=source->m[3][j]-mark.at[j]; square+=d*d; }
            const float moved=std::sqrt(square);
            if(!std::isfinite(moved)) break;
            const unsigned h=mark.hand<2?mark.hand:1u;
            // One draw per mark: effect objects are pooled, so a mark left
            // lying about is a different flash's matrix waiting to be put on
            // the wrong weapon.
            const ULONGLONG when=mark.at_time; mark.at_time=0; mark.effect=nullptr;
            if(!when) break;
            if(moved<0.001f) ++g_flashCarryKept[h];
            else {
                ++g_flashCarryLost[h];
                if(moved>g_flashCarryLostBy[h].load(std::memory_order_relaxed))
                    g_flashCarryLostBy[h].store(moved,std::memory_order_relaxed);
                // The game put the native attachment back between the bounds
                // call and the draw -- two flashes in five, by up to two and a
                // half metres, which left them hanging at the body's weapon
                // where it sits above the held one. Draw from the carried copy
                // instead; the write-back cannot reach a matrix of our own.
                if(GetTickCount64()-when<=40) carriedNow=&mark.carried;
            }
            break;
        }
    }
    alignas(16) edf6vr::Matrix scaled{};
    const edf6vr::Matrix* selected=carriedNow?carriedNow:source;
    if(PrepareMuzzleFlash(selected,scaled,GetTickCount64())) {selected=&scaled; ++g_flashScaled;}
    // Original 11548F0 -> FDDD0 consumes the matrix into draw constants
    // synchronously. Forward once and preserve all non-matrix parameters.
    g_originalFlashMesh(renderer,context,selected,mesh,flags,option,value,extra);
}
bool CheckMuzzleFlashProfile() noexcept {
    constexpr unsigned char owner[]={0x48,0x89,0xB6,0xB8,0x04,0,0,0x48,0x89,0xBE,0xC0,0x04,0,0};
    constexpr unsigned char parent[]={0x48,0x89,0x5F,0x38};
    constexpr unsigned char matrix[]={0x4C,0x8D,0x83,0x30,0xFF,0xFF,0xFF};
    constexpr unsigned char args[]={0x48,0xC7,0x44,0x24,0x38,0,0,0,0,
        0xF3,0x0F,0x11,0x44,0x24,0x30,0xC6,0x44,0x24,0x28,0,0x89,0x44,0x24,0x20};
    constexpr unsigned char preparation[]={
        0x48,0x8B,0x83,0x20,0x01,0,0,0x48,0x8D,0x8B,0x30,0x01,0,0,0x48,0x8D,0x54,0x24,0x20,
        0x0F,0x28,0,0x0F,0x29,0x43,0x60,0x0F,0x28,0x48,0x10,0x0F,0x29,0x4B,0x70,
        0x0F,0x28,0x40,0x20,0x0F,0x29,0x83,0x80,0,0,0,0x0F,0x28,0x48,0x30,0x0F,0x29,0x8B,0x90,0,0,0,
        0x0F,0x28,0,0x0F,0x28,0x48,0x10,0x0F,0x29,0x44,0x24,0x20,0x0F,0x28,0x40,0x20,
        0x0F,0x29,0x4C,0x24,0x30,0x0F,0x28,0x48,0x30,0x0F,0x29,0x44,0x24,0x40,0x0F,0x29,0x4C,0x24,0x50};
    __try {
        if(std::memcmp(g_image.base+0x68DB95,owner,sizeof(owner))
           || std::memcmp(g_image.base+0x118AF35,parent,sizeof(parent))) return false;
        void* efsTable=g_image.base+0x17A6068;
        constexpr unsigned char efsCtorArgs[]={0x49,0x8B,0x95,0x20,0x01,0,0,0x48,0x8B,0xCF};
        constexpr unsigned char efsTickArgs[]={0x48,0x8B,0x96,0x20,0x01,0,0,0x48,0x8D,0x8E,0x30,0x01,0,0};
        constexpr unsigned char efsCreateArgs[]={0x4C,0x8D,0x45,0x20,0x48,0x8D,0x55,0,0x48,0x8B,0xCF};
        if(!edf6vr::HasType(g_image,&efsTable,".?AVEfsMuzzleFlash@@")
           || std::memcmp(g_image.base+0x2BA9B6,efsCtorArgs,sizeof(efsCtorArgs))
           || std::memcmp(g_image.base+0x2BAF52,efsTickArgs,sizeof(efsTickArgs))
           || std::memcmp(g_image.base+0x2BAD8B,efsCreateArgs,sizeof(efsCreateArgs))) return false;
        for(const auto call:{kEfsFlashUpdateCalls[0],kEfsFlashUpdateCalls[1],kEfsFlashCreateCall}) {
            const auto target=call==kEfsFlashCreateCall?kEfsFlashCreateTarget:kEfsFlashUpdateTarget;
            if(g_image.base[call]!=0xE8 || g_image.base+call+5+
               *reinterpret_cast<const std::int32_t*>(g_image.base+call+1)!=g_image.base+target) return false;
        }
        for(unsigned i=0;i<2;++i) {
            const unsigned call=kFlashMeshCalls[i];
            const unsigned vt=i?0x17A6578:0x17A6478;
            void* table=g_image.base+vt;
            const unsigned bounds=kFlashBoundsCalls[i];
            if(std::memcmp(g_image.base+bounds-sizeof(preparation),preparation,sizeof(preparation))
               || g_image.base[bounds]!=0xE8
               || g_image.base+bounds+5+*reinterpret_cast<const std::int32_t*>(g_image.base+bounds+1)
                    !=g_image.base+kFlashBoundsTarget) return false;
            if(!edf6vr::HasType(g_image,&table,i?".?AVMuzzleFlash02@@":".?AVMuzzleFlash01@@")
               || std::memcmp(g_image.base+call-0x33,matrix,sizeof(matrix))
               || std::memcmp(g_image.base+call-sizeof(args),args,sizeof(args))
               || g_image.base[call]!=0xE8
               || g_image.base+call+5+*reinterpret_cast<const std::int32_t*>(g_image.base+call+1)
                    !=g_image.base+kFlashMeshTarget) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool InstallMuzzleFlashScale(bool& changed) noexcept {
    changed=false;
    if(!CheckMuzzleFlashProfile()) return false;
    g_originalFlashMesh=reinterpret_cast<FlashMeshDraw>(g_image.base+kFlashMeshTarget);
    g_originalFlashBounds=reinterpret_cast<FlashBounds>(g_image.base+kFlashBoundsTarget);
    g_originalEfsFlashUpdate=reinterpret_cast<FlashBounds>(g_image.base+kEfsFlashUpdateTarget);
    g_originalEfsFlashCreate=reinterpret_cast<EfsFlashCreate>(g_image.base+kEfsFlashCreateTarget);
    for(const auto call:kFlashMeshCalls) {
        bool wrote=false;
        const bool ok=edf6vr::RedirectCall(g_image.base+call,reinterpret_cast<void*>(g_originalFlashMesh),
                                         reinterpret_cast<void*>(&HookFlashMesh),wrote);
        changed|=wrote;
        if(!ok) return false;
    }
    for(const auto call:kFlashBoundsCalls) {
        bool wrote=false;
        const bool ok=edf6vr::RedirectCall(g_image.base+call,reinterpret_cast<void*>(g_originalFlashBounds),
                                         reinterpret_cast<void*>(&HookFlashBounds),wrote);
        changed|=wrote;
        if(!ok) return false;
    }
    for(const auto call:kEfsFlashUpdateCalls) {
        bool wrote=false;
        const bool ok=edf6vr::RedirectCall(g_image.base+call,reinterpret_cast<void*>(g_originalEfsFlashUpdate),
                                         reinterpret_cast<void*>(&HookEfsFlashUpdate),wrote);
        changed|=wrote;
        if(!ok) return false;
    }
    bool wrote=false;
    const bool ok=edf6vr::RedirectCall(g_image.base+kEfsFlashCreateCall,reinterpret_cast<void*>(g_originalEfsFlashCreate),
                                     reinterpret_cast<void*>(&HookEfsFlashCreate),wrote);
    changed|=wrote;
    if(!ok) return false;
    return true;
}
