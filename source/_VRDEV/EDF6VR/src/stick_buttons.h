#pragma once
#include <cmath>
// The turning stick's up and down as buttons, on foot ([VR] RightStickButtons):
// up is Y, down is L3. Sideways is turning, so a push only becomes a button
// inside a cone about straight up or straight down -- a turn pushed a little
// off level went past the old test, which looked at the height alone
// (y > edge: 53 degrees either side of vertical at full push), and pressed Y
// or L3 by mistake (the user, 2026-10-12). The cone is given for each: 40
// degrees either side of straight up, 37 of straight down (the user on
// hardware: "下の判定範囲はもう少し狭くていい"). Once pressed it stays pressed until
// the stick comes back under three quarters of the edge, wherever it has
// drifted sideways, so a held button neither flickers at the edge nor lets go
// when the thumb rolls over into a turn.
namespace edf6vr {
struct StickButtons { bool up=false,down=false; };
inline void UpdateStickButtons(StickButtons& s,float x,float y,float edge,float upDegrees,float downDegrees) noexcept {
    if(!std::isfinite(x) || !std::isfinite(y)) { s={}; return; }
    const float across=std::fabs(x),along=std::fabs(y);
    const bool inUp=across<=along*std::tan(upDegrees*0.01745329252f);
    const bool inDown=across<=along*std::tan(downDegrees*0.01745329252f);
    const float release=edge*0.75f;
    s.up=s.up?y>release:(y>edge && inUp);
    s.down=s.down?y<-release:(y<-edge && inDown);
}
}
