#pragma once
#include <cstdint>
namespace edf6vr {
// One trial per process/instance. Leaving the mission aborts rather than
// comparing a menu/loading screen against gameplay. Does not edit the INI.
struct WarpTrial {
    std::uint64_t start=0;
    bool started=false, finished=false;
    int phase=0; // 0 normal/warmup, 1 baseline64, 2 candidate32, 3 done, 4 aborted
    float Update(bool enabled,bool scene,std::uint64_t now,float configured) noexcept {
        if(!enabled) { if(started && !finished) { finished=true; phase=4; } return configured; }
        if(finished) return configured;
        if(!started) { if(!scene) return configured; start=now; started=true; }
        if(!scene) { finished=true; phase=4; return configured; }
        const auto elapsed=now-start;
        if(elapsed>=60000) { finished=true; phase=3; return configured; }
        phase=elapsed<20000?0:(elapsed<40000?1:2);
        return phase==2?32.0f:64.0f;
    }
};
}
