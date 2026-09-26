// Read-only observation of CRI's existing voice-buffer underrun warning.
// The mixer thread performs one relaxed counter increment: no locks, allocation,
// logging, buffer edits or extra audio-engine calls. Reporting is on game update.
using AudioWarning=void(__fastcall*)(unsigned,const char*);
AudioWarning g_audioWarning=nullptr;
using AudioWritable=unsigned(__fastcall*)(void*);
AudioWritable g_audioWritable=nullptr;
std::atomic<unsigned long long> g_outputCalls{0},g_outputEmpty{0},g_outputInvalid{0},g_outputMaxGap{0};
std::atomic<unsigned> g_outputCapacity{0},g_outputRate{0},g_outputThread{0};
unsigned __fastcall HookAudioWritable(void* output) {
    const unsigned result=g_audioWritable(output); // original GetCurrentPadding exactly once
    g_outputCalls.fetch_add(1,std::memory_order_relaxed);
    __try {
        auto p=static_cast<const unsigned char*>(output);
        const unsigned capacity=*reinterpret_cast<const unsigned*>(p+0x58);
        const unsigned rate=*reinterpret_cast<const unsigned*>(p+0x1C);
        // Native returns min(sampleRate, capacity-padding), or -1/0 on failure.
        // Only a sub-second buffer and valid native state permit inference.
        if(!*reinterpret_cast<void* const*>(p+0x40) || p[5] || !capacity || capacity>=rate
           || rate<8000 || rate>384000 || result>capacity) {
            ++g_outputInvalid;return result;
        }
        g_outputCapacity.store(capacity,std::memory_order_relaxed);
        g_outputRate.store(rate,std::memory_order_relaxed);
        g_outputThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
        if(result==capacity)g_outputEmpty.fetch_add(1,std::memory_order_relaxed);
        thread_local const void* previousOutput=nullptr;
        thread_local ULONGLONG previousTime=0;
        const auto now=GetTickCount64();
        if(previousOutput==output && previousTime && now>=previousTime) {
            const auto gap=now-previousTime;
            auto old=g_outputMaxGap.load(std::memory_order_relaxed);
            while(gap>old && !g_outputMaxGap.compare_exchange_weak(old,gap,std::memory_order_relaxed)){}
        }
        previousOutput=output;previousTime=now;
    } __except(EXCEPTION_EXECUTE_HANDLER){++g_outputInvalid;}
    return result;
}
bool InstallAudioOutputHealth(bool& changed) noexcept {
    changed=false;
    constexpr unsigned char site[]={0x48,0x8B,0xCF,0xE8,0x48,0xF5,0xFF,0xFF,0x48,0x8B,0x4F,0x10};
    constexpr unsigned char padding[]={0x48,0x8B,0x01,0x48,0x8D,0x54,0x24,0x30,0x8B,0x7B,0x1C,0xFF,0x50,0x30};
    if(!g_image.base || std::memcmp(g_image.base+0x100B078,site,sizeof site)
       || std::memcmp(g_image.base+0x100A5F2,padding,sizeof padding))return false;
    g_audioWritable=reinterpret_cast<AudioWritable>(g_image.base+0x100A5C8);
    return edf6vr::RedirectCall(g_image.base+0x100B07B,reinterpret_cast<void*>(g_audioWritable),
        reinterpret_cast<void*>(&HookAudioWritable),changed);
}
void ReportAudioOutput() noexcept {
    Log("AUDIOOUTPUT calls=%llu empty=%llu invalid=%llu maxGapMs=%llu capacityFrames=%u sampleRate=%u thread=%u (empty includes startup; not a glitch count)",
        g_outputCalls.exchange(0),g_outputEmpty.exchange(0),g_outputInvalid.exchange(0),g_outputMaxGap.exchange(0),
        g_outputCapacity.load(),g_outputRate.load(),g_outputThread.load());
}
std::atomic<unsigned long long> g_audioUnderruns{0};
void __fastcall HookAudioUnderrun(unsigned level,const char* text) {
    g_audioUnderruns.fetch_add(1,std::memory_order_relaxed);
    g_audioWarning(level,text);
}
bool InstallAudioHealth(bool& changed) noexcept {
    changed=false;
    constexpr unsigned char site[]={0x48,0x8D,0x15,0xEF,0xA6,0x88,0x00,
        0xB9,0x01,0,0,0,0xE8,0xF5,0x65,0x01,0x00,0x0F,0xBA,0x73,0x30,0x07};
    constexpr char warning[]="W2015080610:Voice buffer underrun.";
    if(!g_image.base || std::memcmp(g_image.base+0x106C0AA,site,sizeof site)
       || std::memcmp(g_image.base+0x18F67A0,warning,sizeof warning)) return false;
    g_audioWarning=reinterpret_cast<AudioWarning>(g_image.base+0x10826B0);
    return edf6vr::RedirectCall(g_image.base+0x106C0B6,reinterpret_cast<void*>(g_audioWarning),
        reinterpret_cast<void*>(&HookAudioUnderrun),changed);
}
