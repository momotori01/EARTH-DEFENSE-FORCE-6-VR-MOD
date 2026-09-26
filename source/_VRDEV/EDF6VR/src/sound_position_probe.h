// Included by plugin.cpp. Positional audio observation and opt-in near-field
// radius trial. Native source position, volume, pitch and voice lifetime stay
// unchanged; a private parameter copy can widen the native panning inner radius.
using SpatialSound=void(__fastcall*)(void*,void*,const void*,float);
using SoundScalar=void(__fastcall*)(void*,float);
SpatialSound g_spatialSound=nullptr;
SoundScalar g_soundPan=nullptr,g_soundVolume=nullptr;
bool g_soundPositionReady=false,g_soundPositionFix=false;
std::atomic<unsigned> g_soundRadiusApplied{0};
struct SoundPositionSample {
    const void* key=nullptr;
    float source[3]{},listener[3]{},local[3]{};
    float pan=0,volume=0,spread=0,appliedSpread=0,appliedPan=0;
    float placed[3]{};bool wasPlaced=false;
    bool havePan=false,haveVolume=false,vr=false,fps=false;
};
// A source this close has no direction left to hear.
//
// Measured with the trace: while boosting with a turn the horizontal distance
// falls to 0.029m, the source moves 1.131m between samples, and the pan angle
// jumps 179.87 degrees in one update -- the relative vector passing through the
// listener. Each ear hears that as a step, once per update, and on a sound that
// holds, a step per update is a tone: what the player hears as the sound tearing
// and arriving from one speaker only.
//
// 1.1.0 tried widening EDF's own inner radius here instead. A wider radius
// shortens the swing but cannot stop a vector from crossing zero, and the
// measurement says the radius was 0.000 in and 0.000 out -- the game does not
// use it for these voices at all.
//
// So the angle is faded to the front as the source arrives. At the distance
// where it flips there is nothing to lose: a sound on your own body is not to
// one side of you. Volume, pitch, position and voice lifetime are untouched,
// and past the fade distance the game's own angle is passed through exactly.
// Where a voice is heard from, decided once and then kept.
//
// The fault is not the angle, it is that the game emits the player's own sounds
// from the model the listener is standing inside, so a relative vector a few
// centimetres long is swung about by the model's own animation and passes
// through him. No rule about angles can fix that, because at that distance the
// angle carries no information to preserve. Giving each voice a position that
// does not do the impossible does fix it, and the three cases want three
// different positions:
//
//   the gun         a shot is an event at a place. Put it where the tracked
//                   weapon was when it fired, which is the hand, and it has a
//                   real direction for the first time.
//   the world       anything else that happens at a place -- landing in water,
//                   a footfall -- stays where it happened.
//   the headset     the booster and the soldier's own voice are carried, not
//                   emitted at a place, so they belong on the listener.
//
// Which of the last two a voice is cannot be told from distance or from how
// long it lasts; both were measured and both overlap completely. It can be told
// from whether the source travels with the listener. So a voice is frozen where
// it began, and once the listener has actually gone somewhere, the source is
// asked whether it came too.
enum class VoicePlace : unsigned { Underfoot, World, Back, Muzzle };
struct VoiceSpot {
    const void* key=nullptr;
    ULONGLONG start=0,at=0;
    float world[3]{},sourceStart[3]{},listenerStart[3]{};
    VoicePlace place=VoicePlace::World;
};
SRWLOCK g_voiceSpotLock=SRWLOCK_INIT;
VoiceSpot g_voiceSpots[256]{};
std::atomic<unsigned> g_voicesPlaced{0},g_voicesHeadLocked{0},g_voicesFromMuzzle{0};

bool PlaceVoice(const SoundPositionSample& s,ULONGLONG now,float out[3]) noexcept {
    if(!s.key || !s.vr || !s.fps || !g_soundPlaceVoices) return false;
    if(!TryAcquireSRWLockExclusive(&g_voiceSpotLock)) return false;
    VoiceSpot* slot=nullptr;auto oldest=&g_voiceSpots[0];
    for(auto& entry:g_voiceSpots) {
        if(entry.key==s.key) {slot=&entry;break;}
        if(entry.at<oldest->at) oldest=&entry;
    }
    const bool fresh=slot && now>=slot->at && now-slot->at<=250;
    if(!slot) slot=oldest;
    if(!fresh) {
        *slot={};slot->key=s.key;slot->start=now;
        for(unsigned j=0;j<3;++j) {
            slot->sourceStart[j]=s.source[j];slot->listenerStart[j]=s.listener[j];
            slot->world[j]=s.source[j];
        }
        // A voice beginning on the listener is one of his own. Heard from the
        // ground beneath him until it proves to be something he carries: a
        // footfall is short and never gets that far, which is what separates it
        // from the booster without having to know which is which.
        float onHim=0;
        for(unsigned j=0;j<3;++j) {
            const float d=s.source[j]-s.listener[j];onHim+=d*d;
        }
        if(std::isfinite(onHim) && onHim<=g_soundUnderfootMetres*g_soundUnderfootMetres)
            slot->place=VoicePlace::Underfoot;
        else slot->place=VoicePlace::World;
        // A shot that has just left the tracked weapon owns the voice that
        // starts with it. Nothing else in the moments after firing begins this
        // close to the listener.
        const auto fired=g_lastFireAt.load(std::memory_order_acquire);
        float reach=0;
        for(unsigned j=0;j<3;++j) {
            const float d=s.source[j]-s.listener[j];reach+=d*d;
        }
        if(fired && now>=fired && now-fired<=g_soundFireWindowMs
           && std::isfinite(reach) && reach<=g_soundCarriedMetres*g_soundCarriedMetres) {
            bool usable=true;
            float muzzle[3]{};
            for(unsigned j=0;j<3;++j) {
                muzzle[j]=g_lastFireMuzzle[j].load(std::memory_order_relaxed);
                if(!std::isfinite(muzzle[j])) usable=false;
            }
            if(usable) {
                for(unsigned j=0;j<3;++j) slot->world[j]=muzzle[j];
                slot->place=VoicePlace::Muzzle;
                g_voicesFromMuzzle.fetch_add(1,std::memory_order_relaxed);
            }
        }
    } else if(slot->place!=VoicePlace::Muzzle && slot->place!=VoicePlace::Back
              && now-slot->start>=g_soundCarriedAfterMs) {
        // Only a listener that has gone somewhere can answer the question.
        float listenerMoved=0,sourceMoved=0;
        for(unsigned j=0;j<3;++j) {
            const float l=s.listener[j]-slot->listenerStart[j];listenerMoved+=l*l;
            const float d=s.source[j]-slot->sourceStart[j];sourceMoved+=d*d;
        }
        listenerMoved=std::sqrt(listenerMoved);sourceMoved=std::sqrt(sourceMoved);
        if(std::isfinite(listenerMoved) && std::isfinite(sourceMoved)
           && listenerMoved>=g_soundCarriedMoveMetres) {
            slot->place=std::fabs(sourceMoved-listenerMoved)<=0.5f*listenerMoved+0.2f
                ?VoicePlace::Back:VoicePlace::World;
            if(slot->place==VoicePlace::Back) g_voicesHeadLocked.fetch_add(1,std::memory_order_relaxed);
        }
    }
    // Both carried kinds are placed against the head, not against the world, so
    // they are recomputed every update from wherever the listener now is.
    if(slot->place==VoicePlace::Underfoot) {
        for(unsigned j=0;j<3;++j) slot->world[j]=s.listener[j];
        slot->world[1]-=g_soundFootDropMetres;   // straight down, whichever way he faces
    } else if(slot->place==VoicePlace::Back) {
        float ahead[3]{};
        const auto seen=g_headBasisAt.load(std::memory_order_acquire);
        bool haveHead=seen && now>=seen && now-seen<=250;
        if(haveHead) {
            float length=0;
            for(unsigned j=0;j<3;++j) {
                ahead[j]=g_headBasis[2][j].load(std::memory_order_relaxed);
                length+=ahead[j]*ahead[j];
            }
            haveHead=std::isfinite(length) && length>0.25f && length<4.0f;
            if(haveHead) {
                length=std::sqrt(length);
                for(unsigned j=0;j<3;++j) ahead[j]/=length;
            }
        }
        for(unsigned j=0;j<3;++j)
            slot->world[j]=s.listener[j]-(haveHead?ahead[j]*g_soundBackBehindMetres:0.0f);
        slot->world[1]-=g_soundBackDownMetres;
    }
    for(unsigned j=0;j<3;++j) out[j]=slot->world[j];
    slot->key=s.key;slot->at=now;
    ReleaseSRWLockExclusive(&g_voiceSpotLock);
    bool moved=false;
    for(unsigned j=0;j<3;++j) if(out[j]!=s.source[j]) moved=true;
    if(moved) g_voicesPlaced.fetch_add(1,std::memory_order_relaxed);
    return moved;
}

// One entry per sounding voice, keyed by the position block the game hands in.
struct PanSlew { const void* key=nullptr; float native=0,applied=0; ULONGLONG at=0; unsigned steps=0; bool fixed=false; };
SRWLOCK g_panSlewLock=SRWLOCK_INIT;
PanSlew g_panSlew[256]{};
std::atomic<unsigned> g_soundVoicesFixed{0};

// A voice that has once jumped further than a source can travel stops being
// placed at all, and is walked to the front instead.
//
// Which sounds those are is not a guess and does not have to be a list. They
// are the ones the player named -- the Fencer's booster, landing in water, his
// own footsteps and voice -- and what they have in common is that they are
// emitted from the model the listener is standing inside, so their direction
// passes through him and inverts. Nothing else in the game does that, so
// nothing else is touched: this asks each voice whether it has done the
// impossible, and only then stops trying to place it.
//
// The two rules it replaces both failed for the same reason. A distance fade
// wide enough to stop the step held the Ranger's own gunfire at dead centre,
// and narrow enough to leave that alone it let a thirty degree step through
// every update. A step limit alone stopped the tearing but left the booster
// pinned wherever it had been -- "only from one side", which is exactly what a
// held angle sounds like. Neither could tell a sound that needs a direction
// from one that cannot have a sensible one.
//
// The walk to the front uses the same per-update limit, so the moment a voice
// is recognised is not itself a step.
float SteadyPan(const void* key,float pan,ULONGLONG now) noexcept {
    if(!key || !std::isfinite(pan) || !(g_soundPanStep>0)) return pan;
    float wanted=pan;
    // Never block the caller: a missed sample passes the game's own angle.
    if(!TryAcquireSRWLockExclusive(&g_panSlewLock)) return pan;
    PanSlew* slot=nullptr;auto oldest=&g_panSlew[0];
    for(auto& entry:g_panSlew) {
        if(entry.key==key) {slot=&entry;break;}
        if(entry.at<oldest->at) oldest=&entry;
    }
    const bool fresh=slot && now>=slot->at && now-slot->at<=250;
    if(!slot) slot=oldest;
    if(!fresh) {*slot={};slot->key=key;slot->applied=pan;}
    else {
        // Counted, not taken on the first one.
        //
        // A single large step is what a short sound can do on its way past and
        // never be heard doing; it is a sound that keeps doing it, update after
        // update, that turns into a tone. Measured at a threshold of 120 degrees
        // the applied angle still stepped a median of 45 and up to 117 -- every
        // one of those was a voice that never reached the threshold and was
        // passed through whole. The threshold belongs much lower, and the count
        // is what keeps the Ranger's own gunfire out of it.
        const float moved=std::remainder(pan-slot->native,360.f);
        if(g_soundPanFlip>0 && std::isfinite(moved) && std::fabs(moved)>g_soundPanFlip) {
            if(slot->steps<g_soundPanFlipCount) ++slot->steps;
            if(slot->steps>=g_soundPanFlipCount && !slot->fixed) {
                slot->fixed=true;
                g_soundVoicesFixed.fetch_add(1,std::memory_order_relaxed);
            }
        }
        // A voice that has not done the impossible is placed exactly where the
        // game placed it. Only the walk to the front is rationed, so nothing
        // that still has a direction is ever slowed down or held back.
        if(slot->fixed) {
            const float delta=std::remainder(0.0f-slot->applied,360.f);
            wanted=std::isfinite(delta) && std::fabs(delta)>g_soundPanStep
                ?std::remainder(slot->applied+(delta>0?g_soundPanStep:-g_soundPanStep),360.f)
                :0.0f;
        }
    }
    slot->key=key;slot->native=pan;slot->applied=wanted;slot->at=now;
    ReleaseSRWLockExclusive(&g_panSlewLock);
    return std::isfinite(wanted)?wanted:pan;
}
float NearFieldPan(const SoundPositionSample& s,float pan) noexcept {
    if(!s.vr || !s.fps || !std::isfinite(pan)) return pan;
    const float hold=g_soundNearFieldHold,fade=g_soundNearFieldFade;
    if(!(fade>hold)) return pan;
    // The azimuth is built from the horizontal pair in the listener's frame, so
    // that pair, and not the full distance, is what goes through zero.
    const float horizontal=std::hypot(s.local[0],s.local[2]);
    if(!std::isfinite(horizontal)) return pan;
    if(horizontal>=fade) return pan;
    if(horizontal<=hold) return 0;
    return pan*((horizontal-hold)/(fade-hold));
}
thread_local SoundPositionSample* g_soundSample=nullptr;
std::atomic<unsigned> g_soundViewMode{0};
std::atomic<unsigned> g_soundAttempts{0},g_soundAccepted{0},g_soundListeners{0};
bool ReadSoundPosition(void* manager,const void* position,SoundPositionSample& s) noexcept {
    __try {
        auto m=static_cast<const unsigned char*>(manager);
        // Single native listener only; do not guess which split-screen view won.
        if(!edf6vr::Readable(m,0x70) || !edf6vr::Readable(position,0x20))return false;
        const auto listeners=*reinterpret_cast<const std::uint64_t*>(m+0x68);
        g_soundListeners.store(static_cast<unsigned>(listeners),std::memory_order_relaxed);
        if(listeners!=1)return false;
        auto row=*reinterpret_cast<const unsigned char* const*>(m+0x58);
        if(!edf6vr::Readable(row,0x50))return false;
        std::memcpy(s.source,position,12);std::memcpy(s.listener,row,12);
        std::memcpy(&s.spread,static_cast<const unsigned char*>(position)+0x10,4);
        edf6vr::Matrix view{};std::memcpy(&view,row+0x10,sizeof(view));
        float square=0;
        for(unsigned j=0;j<3;++j) {
            const float d=s.source[j]-s.listener[j];square+=d*d;
            s.local[j]=s.source[0]*view.m[0][j]+s.source[1]*view.m[1][j]
                +s.source[2]*view.m[2][j]+view.m[3][j];
            if(!std::isfinite(s.local[j]))return false;
        }
        // Nearby is NOT proof of ownership. Log it as a nearby native voice.
        // Include stock third-person cameras as well as the FPS camera.
        if(!std::isfinite(square) || square>4096)return false;
        s.key=position;
        const auto mode=g_soundViewMode.load(std::memory_order_relaxed);
        s.vr=(mode&1)!=0;s.fps=(mode&2)!=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
struct SoundPositionWindow {
    SoundPositionSample last{};
    ULONGLONG at=0,start=0;
    unsigned samples=0;
    float panMin=180,panMax=-180,panJump=0,appliedJump=0,sourceStep=0,listenerStep=0;
    float horizontalMin=1e9f,horizontalMax=0,volumeMin=1e9f,volumeMax=0;
    void Add(const SoundPositionSample& s,ULONGLONG now) noexcept {
        if(last.key!=s.key || now<at || now-at>250 || last.vr!=s.vr || last.fps!=s.fps) {
            *this={};start=now;
        }
        if(samples) {
            panJump=std::max(panJump,std::fabs(std::remainder(s.pan-last.pan,360.f)));
            // The one that matters: what the game was actually given.
            appliedJump=std::max(appliedJump,std::fabs(std::remainder(s.appliedPan-last.appliedPan,360.f)));
            float ds=0,dl=0;
            for(unsigned j=0;j<3;++j) {
                ds+=(s.source[j]-last.source[j])*(s.source[j]-last.source[j]);
                dl+=(s.listener[j]-last.listener[j])*(s.listener[j]-last.listener[j]);
            }
            sourceStep=std::max(sourceStep,std::sqrt(ds));listenerStep=std::max(listenerStep,std::sqrt(dl));
        }
        const float horizontal=std::hypot(s.local[0],s.local[2]);
        horizontalMin=std::min(horizontalMin,horizontal);horizontalMax=std::max(horizontalMax,horizontal);
        panMin=std::min(panMin,s.pan);panMax=std::max(panMax,s.pan);
        if(s.haveVolume){volumeMin=std::min(volumeMin,s.volume);volumeMax=std::max(volumeMax,s.volume);}
        last=s;at=now;++samples;
    }
};
SRWLOCK g_soundWindowsLock=SRWLOCK_INIT;
SoundPositionWindow g_soundWindows[32]{};
void ReportSoundPosition(const SoundPositionSample& s,ULONGLONG now) noexcept {
    if(!s.havePan)return;
    // Audio and game update can run on different threads. No shared-camera lock,
    // allocation, stale game-object read or per-frame log in this observer.
    if(!TryAcquireSRWLockExclusive(&g_soundWindowsLock))return;
    SoundPositionWindow* target=nullptr;auto oldest=&g_soundWindows[0];
    for(auto& w:g_soundWindows) {
        if(w.last.key==s.key){target=&w;break;}
        if(w.at<oldest->at)oldest=&w;
    }
    if(!target)target=oldest;
    target->Add(s,now);
    ReleaseSRWLockExclusive(&g_soundWindowsLock);
}
void DrainSoundPositions(ULONGLONG now) noexcept {
    static ULONGLONG next=0;
    if(!g_spatialSound || now<next)return;
    next=now+5000;
    SoundPositionWindow copy[32]{};
    AcquireSRWLockExclusive(&g_soundWindowsLock);
    for(unsigned i=0;i<32;++i) {copy[i]=g_soundWindows[i];g_soundWindows[i]={};}
    ReleaseSRWLockExclusive(&g_soundWindowsLock);
    Log("SOUNDPOS coverage calls=%u accepted=%u nativeListeners=%u placed=%u head=%u muzzle=%u panChanged=%u voicesFixed=%u fix=%d",
        g_soundAttempts.exchange(0),g_soundAccepted.exchange(0),g_soundListeners.load(),
        g_voicesPlaced.exchange(0),g_voicesHeadLocked.exchange(0),g_voicesFromMuzzle.exchange(0),
        g_soundRadiusApplied.exchange(0),g_soundVoicesFixed.exchange(0),g_soundPositionFix);
    // Disk I/O runs with the existing camera diagnostics, never in audio calls.
    for(const auto& w:copy) {
    if(w.samples<10)continue;
    const auto& s=w.last;
    Log("SOUNDPOS vr=%d fps=%d nearby=%p samples=%u span=%llums pan=[%.2f %.2f] jump=%.2f->%.2fdeg "
        "horizontal=[%.3f %.3f] spread=%.3f->%.3f pan->%.2f volume=[%.3f %.3f] sourceStep=%.3f listenerStep=%.3f "
        "local=(%.3f,%.3f,%.3f) source=(%.3f,%.3f,%.3f) listener=(%.3f,%.3f,%.3f)",
        s.vr,s.fps,s.key,w.samples,w.at-w.start,w.panMin,w.panMax,w.panJump,w.appliedJump,
        w.horizontalMin,w.horizontalMax,s.spread,s.appliedSpread,s.appliedPan,w.volumeMin==1e9f?-1:w.volumeMin,w.volumeMax,
        w.sourceStep,w.listenerStep,s.local[0],s.local[1],s.local[2],s.source[0],s.source[1],s.source[2],
        s.listener[0],s.listener[1],s.listener[2]);
    }
}
void __fastcall HookSoundPan(void* player,float pan) {
    float wanted=pan;
    if(g_soundSample) {
        g_soundSample->pan=pan;g_soundSample->havePan=true;
        if(g_soundPositionReady && g_soundPositionFix) {
            wanted=NearFieldPan(*g_soundSample,pan);
            if(g_soundSample->vr && g_soundSample->fps)
                wanted=SteadyPan(g_soundSample->key,wanted,GetTickCount64());
        }
        g_soundSample->appliedPan=wanted;
        if(wanted!=pan) g_soundRadiusApplied.fetch_add(1,std::memory_order_relaxed);
    }
    g_soundPan(player,wanted);
}
void __fastcall HookSoundVolume(void* player,float volume) {
    if(g_soundSample){g_soundSample->volume=volume;g_soundSample->haveVolume=true;}
    g_soundVolume(player,volume);
}
void __fastcall HookSpatialSound(void* manager,void* player,const void* position,float volume) {
    SoundPositionSample s{};
    const auto now=GetTickCount64(),seen=g_vrAudioAt.load(std::memory_order_relaxed);
    const bool active=seen && now>=seen && now-seen<250;
    if(active)g_soundAttempts.fetch_add(1,std::memory_order_relaxed);
    const bool observe=active && ReadSoundPosition(manager,position,s);
    if(observe)g_soundAccepted.fetch_add(1,std::memory_order_relaxed);
    if(observe) s.appliedSpread=s.spread; // the native inner radius is passed through
    // A private copy of the parameter block; the game's own is never edited.
    alignas(16) float parameters[8]{};
    const void* input=position;
    float placed[3]{};
    if(observe && g_soundPositionFix && g_soundPositionReady && PlaceVoice(s,now,placed)) {
        std::memcpy(parameters,position,sizeof(parameters));
        for(unsigned j=0;j<3;++j) parameters[j]=placed[j];
        input=parameters;
        for(unsigned j=0;j<3;++j) s.placed[j]=placed[j];
        s.wasPlaced=true;
    }
    auto previous=g_soundSample;g_soundSample=observe?&s:nullptr;
    __try {g_spatialSound(manager,player,input,volume);}
    __finally {g_soundSample=previous;}
    if(observe)ReportSoundPosition(s,now);
}
bool InstallSoundPositionProbe() noexcept {
    constexpr unsigned sites[]={0x7AEE60,0x7ACF98,0x7ACEE0};
    constexpr unsigned targets[]={0x7ACD20,0xFE765C,0xFE8008};
    void* hooks[]={reinterpret_cast<void*>(&HookSpatialSound),reinterpret_cast<void*>(&HookSoundPan),
        reinterpret_cast<void*>(&HookSoundVolume)};
    if(!g_image.base || g_spatialSound)return false;
    // Validate all three original CALLs before modifying any. Partial installs
    // are harmless passthrough observers, not a partially enabled audio fix.
    for(unsigned i=0;i<3;++i) {
        auto call=g_image.base+sites[i];std::int32_t rel=0;
        if(!edf6vr::Readable(call,5) || *call!=0xE8)return false;
        std::memcpy(&rel,call+1,4);
        if(call+5+rel!=g_image.base+targets[i])return false;
    }
    g_spatialSound=reinterpret_cast<SpatialSound>(g_image.base+targets[0]);
    g_soundPan=reinterpret_cast<SoundScalar>(g_image.base+targets[1]);
    g_soundVolume=reinterpret_cast<SoundScalar>(g_image.base+targets[2]);
    for(unsigned i=0;i<3;++i) {
        bool changed=false;
        if(!edf6vr::RedirectCall(g_image.base+sites[i],g_image.base+targets[i],hooks[i],changed))return false;
    }
    g_soundPositionReady=true;
    return true;
}
