#pragma once
#include <cmath>
namespace edf6vr {
// How the game hangs a weapon on a Fencer's arm, and what that means for a
// weapon laid on a controller.
//
// Each weapon's data names the body bone its model's root rides
// (ModelConstraint; all 231 weapons with a Fencer model in the installed
// game's WEAPON/*.SGO, read 2026-10-12, and the bones of P607_FENCER):
//   "arms"           arms_l / arms_r -- the guns, cannons, gatlings, hand
//                    missiles, hammers, blades, the katana and most spears.
//                    At a level aim both are the body's left, up and forward
//                    exactly: the weapon is the same way round in either hand.
//   "armsFlip"       armsFlip_r is arms_r; armsFlip_l is arms_l turned half
//                    round about its own Z (the barrel). The shields, the
//                    Jackhammers and the twin spear: built for the right hand,
//                    and upside down in the left so the same side faces out.
//   "armsLowerFlip"  a bone of the forearm, turned half round in the left arm
//                    too. The Arm Hound alone: a box the forearm goes through.
// No bone has a negative scale, so nothing is ever mirrored: the left weapon of
// a pair is the right one turned over. Laid on the controllers the same way
// round, the left Arm Hound, Jackhammer and twin spear had their outer side
// facing in (the user, 2026-10-12: "アームハウンドは右側が正しい向きでした").
enum class FencerMount { Arms, Flip, Forearm };
// The models of the two that are not "arms", by the name of their root node
// (the shields are armsFlip as well, and take their own two poses).
inline constexpr const wchar_t* kFencerFlipModels[]={L"h_pile_punch01",L"h_pile_crowe01",L"h_pile_619_drill01",L"h_pile_Twin"};
inline constexpr const wchar_t* kFencerForearmModels[]={L"h_missile_arm01"};
// The Arm Hound against the arm it sits on, from the bones' rest pose: the
// forearm runs through the model along (-0.181, 0, 0.983) of its own axes (the
// box points 10.4 degrees in from the arm), and the middle of the palm, 8 cm
// past the wrist, is at kFencerForearmPalm -- inside the box's open end, the
// black slot ("アームハウンドはスクショの黒いスリットに腕が入るように取り付けられます").
inline constexpr float kFencerForearmLean=0.181f,kFencerForearmAlong=0.983f;
inline constexpr float kFencerForearmPalm[3]={0.0755f,0.0f,-0.062f};
// The weapon's own X, Y and Z along the hand's left, up and ahead (hand 0 the
// left). Arms: as they are. Flip: turned half round about ahead in the left
// hand. Forearm: the arm's line through the model put on ahead, upside down in
// the left hand.
inline void FencerMountLay(FencerMount mount,unsigned hand,float lay[3][3]) noexcept {
    const bool left=hand==0;
    const float turn=left && mount!=FencerMount::Arms?-1.0f:1.0f;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) lay[i][j]=0;
    lay[0][0]=turn; lay[1][1]=turn; lay[2][2]=1;
    if(mount==FencerMount::Forearm) {
        lay[0][0]=turn*kFencerForearmAlong; lay[0][2]=-kFencerForearmLean;
        lay[2][0]=turn*kFencerForearmLean;  lay[2][2]=kFencerForearmAlong;
    }
}
// Where the model's root goes for the palm to sit at `at` of the model's own
// axes: rows are the weapon's X, Y, Z in the world (any length).
inline bool FencerMountRoot(const float rows[3][3],const float palm[3],const float at[3],float root[3]) noexcept {
    for(int j=0;j<3;++j) root[j]=palm[j];
    for(int i=0;i<3;++i) {
        const float n=std::sqrt(rows[i][0]*rows[i][0]+rows[i][1]*rows[i][1]+rows[i][2]*rows[i][2]);
        if(!std::isfinite(n) || n<1e-4f) return false;
        for(int j=0;j<3;++j) root[j]-=at[i]*rows[i][j]/n;
    }
    return std::isfinite(root[0]) && std::isfinite(root[1]) && std::isfinite(root[2]);
}
// --- A left weapon's lunge -------------------------------------------------
//
// An attack that carries the soldier -- the twin spear's dash (12 or 24 m), the
// rollers (15, 30, 65 m), the jump smash (13 m), a heavy cannon's kick (2 to 10
// m back) -- moves him by the clip's root motion: the globalSRT node's step
// each tick (always along the body's own Z in the clips, OBJECT/
// EDF6HEAVYARMOR.CAS), which the soldier's update takes from the animation
// (1166ED0, called at 5758C0) and lays along the body's rows at soldier+0x5E0.
// The body faces the game's one aim, which is the right hand's, so a left
// weapon's lunge went where the right hand pointed (the user, 2026-10-12:
// "左手に装備した移動するタイプの武器の移動先が、右手の向きで決まっている。左手武器は
// 左手の方向に移動して").
//
// One step of that motion, in the body's own axes (`body`: its X, Y and Z rows
// in the world), turned about the vertical so that what went the way the body
// faces goes along `yaw` (the game's: atan2(x, z)).
inline bool FencerTurnMove(const float body[3][3],float yaw,float step[3]) noexcept {
    const float turn=yaw-std::atan2(body[2][0],body[2][2]),c=std::cos(turn),s=std::sin(turn);
    if(!std::isfinite(c) || !std::isfinite(s)) return false;
    float world[3]{};
    for(int j=0;j<3;++j) world[j]=step[0]*body[0][j]+step[1]*body[1][j]+step[2]*body[2][j];
    const float turned[3]={world[0]*c+world[2]*s,world[1],-world[0]*s+world[2]*c};
    float out[3]{};
    for(int k=0;k<3;++k) {
        const float n=body[k][0]*body[k][0]+body[k][1]*body[k][1]+body[k][2]*body[k][2];
        if(!std::isfinite(n) || n<1e-6f) return false;
        out[k]=(turned[0]*body[k][0]+turned[1]*body[k][1]+turned[2]*body[k][2])/n;
        if(!std::isfinite(out[k])) return false;
    }
    for(int k=0;k<3;++k) step[k]=out[k];
    return true;
}
// Whose attack is moving the soldier, of the two weapons' arm animations.
//
// Which clip a step came from is not told, so it goes by what can move him: of
// the Fencer's 226 clips only 49 carry root motion, every one a weapon's
// attack -- all the melee ones (a swing 0.2 to 0.8 m, a thrust 0.5 to 1.5, the
// lunges above), and of the guns' only the heavy kicks. So a melee weapon in
// its attack moves him whatever the other hand's gun is doing (a gatling held
// down, a cannon's shot in the middle of a dash); two melee weapons at once,
// or two guns, go by which began last (the left's on a tie). Update once a
// tick; it returns 0 (left), 1 (right) or -1, and says when the left's has
// just begun, which is when its direction is taken.
struct FencerMoveOwner {
    bool on[2]{};
    unsigned long long began[2]{},tick=0;
    int Update(const bool playing[2],const bool melee[2],bool& leftBegan) noexcept {
        ++tick;
        leftBegan=playing[0] && !on[0];
        for(int h=0;h<2;++h) { if(playing[h] && !on[h]) began[h]=tick; on[h]=playing[h]; }
        const bool lunging[2]={on[0] && melee[0],on[1] && melee[1]};
        if(lunging[0]!=lunging[1]) return lunging[0]?0:1;
        int owner=-1;
        for(int h=0;h<2;++h) if(on[h] && (owner<0 || began[h]>began[owner])) owner=h;
        return owner;
    }
};
// A swung weapon's rows on the hand, from where its root is now.
//
// A blade, a hammer or the katana in its swing is drawn as the game swings it:
// its rows in the game's aim frame (`from`: left, up, ahead as unit rows), put
// on the hand's frame (`to`). That turn from one frame to the other does not
// depend on the swing, so it can be applied to the root at any moment -- and it
// has to be, for the shot. The slash leaves along the weapon's muzzle on the
// animation's fire event, and the muzzle is carried onto the hand before each
// of the weapon's ticks; carried onto the rows worked out at the last update,
// it was the swing as it had been one tick earlier, and a Force Blade turns 25
// degrees in a tick: its wave left 26 degrees to the right of the aim every
// time (hardware 2026-10-12, 23 swings: 25.6 +/- 1.0; the clip's event is where
// the muzzle points straight ahead, and one tick before it points 27.8 degrees
// right). With the rows from the tick's own root the same hardware read 0.2 to
// 0.8 degrees over 63 swings. `root`: the root's unit rows now.
inline bool FencerSwingRows(const float root[3][3],const float from[3][3],const float to[3][3],float out[3][3]) noexcept {
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
        float v=0;
        for(int k=0;k<3;++k) v+=(root[i][0]*from[k][0]+root[i][1]*from[k][1]+root[i][2]*from[k][2])*to[k][j];
        if(!std::isfinite(v)) return false;
        out[i][j]=v;
    }
    return true;
}
}
