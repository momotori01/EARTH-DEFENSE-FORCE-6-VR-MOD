// Read-only sampling from the existing local Fencer update. No new native hook,
// sound playback call, release delay or audio parameter write.
struct GatlingContinuity {
    void* weapon=nullptr;void* handle=nullptr;
    unsigned samples=0,active=0,missing=0,changed=0,spinLost=0;
    bool wasSpinning=false;
    float low=1.f,high=0.f;
};
GatlingContinuity g_gatlingContinuity[8]{};
void ObserveGatlingSound(unsigned slot,void* weapon) noexcept {
    if(slot>=8)return;
    auto& state=g_gatlingContinuity[slot];
    if(state.weapon!=weapon) {state={};state.weapon=weapon;}
    __try {
        if(!weapon || !edf6vr::HasType(g_image,weapon,".?AVWeapon_Gatling@@")
           || !edf6vr::Readable(weapon,0x1670))return;
        auto bytes=static_cast<unsigned char*>(weapon);
        const auto count=*reinterpret_cast<int*>(bytes+0x1574);
        const auto total=*reinterpret_cast<int*>(bytes+0x1578);
        if(total<=0)return;
        const float spin=static_cast<float>(count)/static_cast<float>(total);
        auto handle=*reinterpret_cast<void**>(bytes+0x1668);
        ++state.samples;state.low=std::min(state.low,spin);state.high=std::max(state.high,spin);
        if(spin>.01f) {
            ++state.active;if(!handle)++state.missing;
            if(state.wasSpinning && handle!=state.handle)++state.changed;
        } else if(state.wasSpinning)++state.spinLost;
        state.wasSpinning=spin>.01f;state.handle=handle;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void ReportFencerContinuity(ULONGLONG now) noexcept {
    static ULONGLONG next=0;if(now<next)return;next=now+5000;
    edf6vr::InputContinuity snapshot{};
    AcquireSRWLockExclusive(&g_inputContinuityLock);
    snapshot=g_inputContinuity;g_inputContinuity.maxGap=0;g_inputContinuity.absentHands=0;
    ReleaseSRWLockExclusive(&g_inputContinuityLock);
    Log("FENCERAUDIO modPad polls=%llu windowAbsent=%llu windowMaxPollGapMs=%llu vr=%d channels=LT,RT,LB,RB,Y,L3 edgesCumulative=1",
        snapshot.polls,snapshot.absentHands,snapshot.maxGap,g_vrEnabled);
    for(unsigned i=0;i<snapshot.channels.size();++i) {
        const auto& c=snapshot.channels[i];
        Log("FENCERAUDIO channel=%u down=%d rises=%llu falls=%llu shortOffLe150Ms=%llu",i,c.on,c.rises,c.falls,c.shortGaps);
    }
    for(unsigned i=0;i<8;++i) {
        auto& c=g_gatlingContinuity[i];if(!c.samples)continue;
        Log("FENCERAUDIO gatling slot=%u samples=%u spin=[%.3f %.3f] active=%u noHandle=%u handleChangesWhileSpinning=%u spinLost=%u",
            i,c.samples,c.low,c.high,c.active,c.missing,c.changed,c.spinLost);
        // Keep transition history, clear just the reporting window.
        c.samples=c.active=c.missing=c.changed=c.spinLost=0;c.low=1;c.high=0;
    }
}
