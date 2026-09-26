#pragma once
#include "xinput_bridge.h"
#include <array>
#include <cstdint>

namespace edf6vr {
// Read-only diagnostic. A short release is evidence of a pulse, not proof of
// a tracking failure: an intentional quick tap also counts. Never debounce it.
struct InputContinuity {
    struct Channel {bool on=false,seen=false;std::uint64_t offAt=0,rises=0,falls=0,shortGaps=0;};
    std::array<Channel,6> channels{}; // LT RT LB RB Y L3
    std::uint64_t polls=0,last=0,maxGap=0,absentHands=0;
    void Observe(const PadState& pad,bool anyHand,std::uint64_t now) noexcept {
        if(polls && now>=last && now-last>maxGap)maxGap=now-last;
        last=now;++polls;if(!anyHand)++absentHands;
        const bool down[]={pad.leftTrigger>30,pad.rightTrigger>30,
            (pad.buttons&0x100)!=0,(pad.buttons&0x200)!=0,
            (pad.buttons&0x8000)!=0,(pad.buttons&0x40)!=0};
        for(unsigned i=0;i<channels.size();++i) {
            auto& c=channels[i];
            if(down[i] && !c.on) {
                ++c.rises;
                if(c.seen && now>=c.offAt && now-c.offAt<=150)++c.shortGaps;
            } else if(!down[i] && c.on) {++c.falls;c.offAt=now;c.seen=true;}
            c.on=down[i];
        }
    }
};
}
