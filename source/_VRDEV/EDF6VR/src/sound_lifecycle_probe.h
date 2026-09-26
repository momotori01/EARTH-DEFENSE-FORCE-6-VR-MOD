// Observe existing calls only: no extra CRI queries, sound writes or I/O here.
using SoundManagerUpdate=void(__fastcall*)(void*,float);
using SoundAlive=bool(__fastcall*)(void*);
using SoundPlaybackStatus=int(__fastcall*)(unsigned);
SoundManagerUpdate g_soundManagerUpdate=nullptr;
SoundAlive g_soundAlive=nullptr;
SoundPlaybackStatus g_soundPlaybackStatus=nullptr;
bool g_soundLifecycleReady=false;
thread_local void* g_observedSoundManager=nullptr;
struct SoundLifeSample {
    void* voice=nullptr;unsigned id=0,mode=0;
    int status=-1;bool keep=false;
    float pitch=0,volume=0,fade=0;
};
thread_local SoundLifeSample* g_soundLifeSample=nullptr;
struct SoundLifeWindow {
    SoundLifeSample last{};
    ULONGLONG at=0;
    unsigned samples=0,idChanges=0,stateChanges=0,removed=0;
    float pitchMin=1e9f,pitchMax=-1e9f,volumeMin=1e9f,volumeMax=-1e9f;
    void Add(const SoundLifeSample& s,ULONGLONG now) noexcept {
        if(last.voice!=s.voice || last.mode!=s.mode || now<at || now-at>1000)*this={};
        if(samples){if(last.id!=s.id)++idChanges;if(last.status!=s.status)++stateChanges;}
        ++samples;if(!s.keep)++removed;
        pitchMin=std::min(pitchMin,s.pitch);pitchMax=std::max(pitchMax,s.pitch);
        volumeMin=std::min(volumeMin,s.volume*s.fade);volumeMax=std::max(volumeMax,s.volume*s.fade);
        last=s;at=now;
    }
};
struct SoundLifeTotals {
    unsigned updates=0,checks=0,accepted=0,status[5]{},removed=0;
    unsigned gapsOver50=0,fastUnder2=0,mode=0,modesSeen=0;
    double gapMax=0,costMax=0,dtMin=1e9,dtMax=0;
};
SRWLOCK g_soundLifeLock=SRWLOCK_INIT;
SoundLifeWindow g_soundLifeWindows[64]{};
SoundLifeTotals g_soundLifeTotals{};
std::atomic<unsigned> g_soundLifeDropped{0};
void RecordSoundLife(const SoundLifeSample* sample,ULONGLONG now) noexcept {
    if(!TryAcquireSRWLockExclusive(&g_soundLifeLock)){++g_soundLifeDropped;return;}
    ++g_soundLifeTotals.checks;
    if(sample) {
        const auto& s=*sample;
        ++g_soundLifeTotals.accepted;
        ++g_soundLifeTotals.status[s.status>=0 && s.status<4?s.status:4];
        if(!s.keep)++g_soundLifeTotals.removed;
        auto* target=&g_soundLifeWindows[0];
        for(auto& w:g_soundLifeWindows) {
            if(w.last.voice==s.voice){target=&w;break;}
            if(w.at<target->at)target=&w;
        }
        target->Add(s,now);
    }
    ReleaseSRWLockExclusive(&g_soundLifeLock);
}
int __fastcall HookSoundPlaybackStatus(unsigned id) {
    const int result=g_soundPlaybackStatus(id);
    if(g_soundLifeSample)g_soundLifeSample->status=result;
    return result;
}
bool __fastcall HookSoundAlive(void* voice) {
    SoundLifeSample sample{};bool observe=false;
    if(g_observedSoundManager) {
        __try {
            auto b=static_cast<const unsigned char*>(voice);
            SoundPositionSample position{};
            if(edf6vr::Readable(b,0x61) && b[0x60]
                && ReadSoundPosition(g_observedSoundManager,b+0x40,position)) {
                sample.voice=voice;sample.id=*reinterpret_cast<const unsigned*>(b+0x10);
                sample.mode=(position.vr?1u:0u)|(position.fps?2u:0u);
                sample.pitch=*reinterpret_cast<const float*>(b+0x28);
                sample.volume=*reinterpret_cast<const float*>(b+0x2c);
                sample.fade=*reinterpret_cast<const float*>(b+0x38);
                observe=true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    auto previous=g_soundLifeSample;g_soundLifeSample=observe?&sample:nullptr;
    bool keep=false;
    __try {keep=g_soundAlive(voice);}
    __finally {g_soundLifeSample=previous;}
    sample.keep=keep;
    if(g_observedSoundManager)RecordSoundLife(observe?&sample:nullptr,GetTickCount64());
    return keep;
}
void __fastcall HookSoundManagerUpdate(void* manager,float dt) {
    const auto now=GetTickCount64(),seen=g_fencerProbeAt.load();
    const bool active=g_soundLifecycleReady && seen && now>=seen && now-seen<250;
    // QPC resolves sub-millisecond double updates that GetTickCount64 cannot.
    const double start=active?Now():0;
    auto previous=g_observedSoundManager;g_observedSoundManager=active?manager:nullptr;
    __try {g_soundManagerUpdate(manager,dt);}
    __finally {g_observedSoundManager=previous;}
    static thread_local double last=0;
    static thread_local unsigned lastMode=0;
    if(!active){last=0;return;}
    const double cost=(Now()-start)*1000;
    const auto mode=g_soundViewMode.load();
    const double gap=last && lastMode==mode?(start-last)*1000:0;last=start;lastMode=mode;
    if(!TryAcquireSRWLockExclusive(&g_soundLifeLock)){++g_soundLifeDropped;return;}
    auto& t=g_soundLifeTotals;++t.updates;t.mode=mode;
    t.modesSeen|=1u<<(mode&3);
    t.gapMax=std::max(t.gapMax,gap);t.costMax=std::max(t.costMax,cost);
    if(gap>50)++t.gapsOver50;
    if(gap>0 && gap<2)++t.fastUnder2;
    t.dtMin=std::min(t.dtMin,static_cast<double>(dt));t.dtMax=std::max(t.dtMax,static_cast<double>(dt));
    ReleaseSRWLockExclusive(&g_soundLifeLock);
}
void DrainSoundLifecycle(ULONGLONG now) noexcept {
    static ULONGLONG next=0;if(!g_soundLifecycleReady || now<next)return;next=now+5000;
    SoundLifeWindow windows[64]{};SoundLifeTotals totals{};
    AcquireSRWLockExclusive(&g_soundLifeLock);
    totals=g_soundLifeTotals;g_soundLifeTotals={};
    for(unsigned i=0;i<64;++i){windows[i]=g_soundLifeWindows[i];g_soundLifeWindows[i]={};}
    ReleaseSRWLockExclusive(&g_soundLifeLock);
    Log("SOUNDLIFE vr=%u fps=%u modesSeen=%u updates=%u maxGapMs=%.3f gapsOver50=%u fastUnder2=%u maxCostMs=%.3f nativeDt=[%.6f %.6f] checks=%u nearby=%u rawStatus0123Other=%u,%u,%u,%u,%u nativeRemove=%u dropped=%u",
        totals.mode&1,(totals.mode>>1)&1,totals.modesSeen,totals.updates,totals.gapMax,totals.gapsOver50,totals.fastUnder2,totals.costMax,
        totals.updates?totals.dtMin:0,totals.dtMax,totals.checks,totals.accepted,totals.status[0],totals.status[1],totals.status[2],totals.status[3],totals.status[4],totals.removed,g_soundLifeDropped.exchange(0));
    for(const auto& w:windows)if(w.samples>=10 || w.removed || w.idChanges) {
        const auto& s=w.last;
        Log("SOUNDLIFE voice=%p id=%u mode=%u samples=%u idChanges=%u stateChanges=%u lastRaw=%d nativeRemove=%u pitchScalar=[%.4f %.4f] volumeTimesFade=[%.4f %.4f]",
            s.voice,s.id,s.mode,w.samples,w.idChanges,w.stateChanges,s.status,w.removed,w.pitchMin,w.pitchMax,w.volumeMin,w.volumeMax);
    }
}
bool InstallSoundLifecycleProbe() noexcept {
    const unsigned sites[]={0x705957,0x7AF590,0x7A8A57};
    const unsigned targets[]={0x7AF3E0,0x7A8A50,0xFE24B8};
    void* hooks[]={reinterpret_cast<void*>(&HookSoundManagerUpdate),reinterpret_cast<void*>(&HookSoundAlive),reinterpret_cast<void*>(&HookSoundPlaybackStatus)};
    for(unsigned i=0;i<3;++i) {
        auto call=g_image.base+sites[i];std::int32_t rel=0;
        if(!edf6vr::Readable(call,5) || *call!=0xE8)return false;
        std::memcpy(&rel,call+1,4);if(call+5+rel!=g_image.base+targets[i])return false;
    }
    g_soundManagerUpdate=reinterpret_cast<SoundManagerUpdate>(g_image.base+targets[0]);
    g_soundAlive=reinterpret_cast<SoundAlive>(g_image.base+targets[1]);
    g_soundPlaybackStatus=reinterpret_cast<SoundPlaybackStatus>(g_image.base+targets[2]);
    for(unsigned i=0;i<3;++i) {
        bool changed=false;
        if(!edf6vr::RedirectCall(g_image.base+sites[i],g_image.base+targets[i],hooks[i],changed))return false;
    }
    g_soundLifecycleReady=true;return true;
}
