#pragma once
#include <cmath>
// Snap turning ([VR] SnapTurn: degrees a step, 0 = off, the default): on foot,
// the turning stick pushed sideways turns the view by that much at once
// instead of turning it smoothly (the user, 2026-11-03: "ゲーム内設定で変更出来る
// スナップターン実装できる？角度は30ずつで、スライダーで自分で調整可能。既定値はオフ").
// The Start menu's "VR setting" and EDF6 VR setting.exe step it by 45 degrees
// (first 30; the user, the same day: "じゃあ４５刻みにして").
//
// One step a push: the stick has to come back under kSnapTurnRelease before
// it turns again. Only a push nearer level than upright counts, so the stick's
// up and down buttons (stick_buttons.h: cones of 40 and 37 degrees about
// vertical) never turn as well. After a gap -- a vehicle, a menu, an air
// strike, the hook not running -- a stick already pushed waits to be let go,
// so leaving a seat with the stick held over does not turn on the first tick.
namespace edf6vr {
constexpr float kSnapTurnPress=0.6f,kSnapTurnRelease=0.3f;
constexpr unsigned long long kSnapTurnGapMs=250;
struct SnapTurn { bool held=false; unsigned long long at=0; };
// -1 a step to the left, +1 a step to the right, 0 none. x, y: the turning
// stick (right and up positive); now: milliseconds.
inline int UpdateSnapTurn(SnapTurn& s,float x,float y,unsigned long long now) noexcept {
    const bool fresh=!s.at || now<s.at || now-s.at>kSnapTurnGapMs;
    s.at=now;
    if(!std::isfinite(x) || !std::isfinite(y)) { s.held=true; return 0; }
    const float across=std::fabs(x);
    if(fresh) { s.held=across>=kSnapTurnRelease; return 0; }
    if(s.held) { if(across<kSnapTurnRelease) s.held=false; return 0; }
    if(across<=kSnapTurnPress || std::fabs(y)>across) return 0;
    s.held=true;
    return x>0?1:-1;
}
}
