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
// The gatlings' feed belt.
//
// A gatling's data ties its root to the body's arms bone and three of its
// nodes to three bones of the body's back (ModelConstraint: chain_base ->
// chain_bp, the drum by the shoulder blade; chain0 -> chain0 and chain1 ->
// chain1, the two links of the loop that hangs from it), and the belt's last
// stretch runs from the lower link into the gun. Carried with the gun's root,
// as every node of a held weapon is, the drum and the loop went wherever the
// arm's animation was not: in a reload they swung up in front of the player
// (user 2026-10-14: "リロードアニメーション中は前に出てきて視界に入る ... ベルトの
// 根本だけ肩に固定").
//
// The drum hangs on the player's own body -- the eye and the body's facing --
// where the body has it at a level aim. The two links ride with the gun, where
// the game's own aim poses keep them against the gun's root: from straight
// down to straight up they stay within some 40 cm in the root's axes while
// they travel over a metre in the body (research/fencer/gatling_belt_aim.py),
// because the strip's last stretch hangs 56% on the lower link and 44% on the
// gun (gatling_belt_weights.py). The first cut hung the links on the body too,
// and that stretch came off the gun as soon as the gun left the level hip
// (user: "背中にベルトは固定されたけど銃を上に向けた時とか変な挙動"). Nothing of the
// body's animation is left in any of the three.
//
// kFencerDrumPlace / kFencerDrumRows: the drum from the eye in the body's
// frame (left, up, forward) for the left gun (the right's is mirrored left to
// right), and its rows in that frame, left gun then right gun: stand_base +
// stand_waist_poseAim, the stance's six degrees of twist about the vertical
// taken out (gatling_belt.py --fixed; the places are the two sides' mean).
// kFencerBeltLinks: the left gun's links (chain0, then chain1) at the nine
// pitches kFencerBeltPitch of its root -- place, then rows, in the root's own
// axes -- the mean of the game's left side and its right side's mirror
// (gatling_belt_aim.py --header). The right gun's are their mirror.
inline constexpr const wchar_t* kFencerBeltNodes[3]={L"chain_bp",L"chain0",L"chain1"};
inline constexpr float kFencerDrumPlace[3]={0.417f,-0.128f,-0.377f};
inline constexpr float kFencerDrumRows[2][3][3]={
    {{-0.9999f,-0.0106f,-0.0054f},{-0.0115f,0.9731f,0.2299f},{0.0028f,0.2299f,-0.9732f}},
    {{0.9999f,0.0106f,0.0054f},{-0.0115f,0.9731f,0.2299f},{-0.0028f,-0.2299f,0.9732f}}};
inline constexpr float kFencerBeltPitch[9]={-87.7f,-68.2f,-45.4f,-22.6f,-0.0f,22.5f,45.5f,69.6f,82.6f};
inline constexpr float kFencerBeltLinks[2][9][4][3]={
    {   // chain0
     {{-0.136f,-0.293f,-0.787f},{0.7543f,0.0978f,0.6492f},{0.0330f,0.9819f,-0.1863f},{-0.6557f,0.1619f,0.7374f}},
     {{-0.089f,-0.202f,-0.806f},{0.7660f,0.0820f,0.6376f},{0.0457f,0.9824f,-0.1812f},{-0.6412f,0.1680f,0.7487f}},
     {{-0.033f,-0.116f,-0.816f},{0.7812f,0.0691f,0.6204f},{0.0463f,0.9847f,-0.1680f},{-0.6225f,0.1600f,0.7661f}},
     {{0.025f,-0.053f,-0.797f},{0.8027f,0.0481f,0.5944f},{0.0387f,0.9904f,-0.1324f},{-0.5951f,0.1293f,0.7932f}},
     {{0.082f,-0.025f,-0.788f},{0.8285f,0.0314f,0.5591f},{0.0221f,0.9958f,-0.0887f},{-0.5595f,0.0859f,0.8244f}},
     {{0.094f,0.057f,-0.776f},{0.7724f,-0.0323f,0.6344f},{-0.0018f,0.9986f,0.0532f},{-0.6352f,-0.0422f,0.7712f}},
     {{0.115f,0.134f,-0.779f},{0.7116f,-0.1344f,0.6896f},{0.0026f,0.9820f,0.1886f},{-0.7026f,-0.1324f,0.6992f}},
     {{0.146f,0.250f,-0.753f},{0.6708f,-0.2209f,0.7080f},{0.0030f,0.9554f,0.2953f},{-0.7417f,-0.1960f,0.6415f}},
     {{0.194f,0.383f,-0.701f},{0.6212f,-0.3037f,0.7224f},{-0.0214f,0.9150f,0.4030f},{-0.7834f,-0.2658f,0.5619f}},
    },
    {   // chain1
     {{-0.160f,-0.700f,-0.689f},{0.9881f,0.0385f,-0.1490f},{-0.1425f,0.5954f,-0.7907f},{0.0583f,0.8025f,0.5938f}},
     {{-0.090f,-0.551f,-0.673f},{0.9962f,0.0408f,-0.0771f},{-0.0841f,0.6857f,-0.7230f},{0.0234f,0.7267f,0.6865f}},
     {{-0.018f,-0.470f,-0.655f},{0.9995f,0.0310f,-0.0117f},{-0.0312f,0.7646f,-0.6437f},{-0.0110f,0.6437f,0.7652f}},
     {{0.053f,-0.435f,-0.657f},{0.9971f,0.0150f,0.0742f},{0.0284f,0.8344f,-0.5504f},{-0.0701f,0.5509f,0.8316f}},
     {{0.117f,-0.457f,-0.711f},{0.9880f,-0.0448f,0.1476f},{0.1068f,0.8891f,-0.4450f},{-0.1113f,0.4554f,0.8833f}},
     {{0.115f,-0.314f,-0.707f},{0.9922f,-0.0652f,0.1061f},{0.1005f,0.9223f,-0.3731f},{-0.0735f,0.3808f,0.9217f}},
     {{0.125f,-0.239f,-0.687f},{0.9943f,-0.0683f,0.0818f},{0.0891f,0.9537f,-0.2873f},{-0.0584f,0.2930f,0.9543f}},
     {{0.150f,-0.209f,-0.660f},{0.9942f,-0.0576f,0.0905f},{0.0729f,0.9817f,-0.1758f},{-0.0787f,0.1813f,0.9803f}},
     {{0.207f,-0.205f,-0.694f},{0.9875f,-0.0360f,0.1536f},{0.0428f,0.9983f,-0.0408f},{-0.1519f,0.0468f,0.9873f}},
    },
};
// A belt node rebuilt: `rows` its unit rows and `place` its place in the drawn
// world; `in` is the node as the game has it, for the length of each row and
// its W lane only.
inline bool FencerBeltPut(const float rows[3][3],const float place[3],const float in[4][4],float out[4][4]) noexcept {
    float made[4][4]{};
    for(int i=0;i<3;++i) {
        const float length=std::sqrt(in[i][0]*in[i][0]+in[i][1]*in[i][1]+in[i][2]*in[i][2]);
        if(!std::isfinite(length) || length<1e-4f) return false;
        for(int j=0;j<3;++j) {
            made[i][j]=rows[i][j]*length;
            if(!std::isfinite(made[i][j]) || !std::isfinite(place[j])) return false;
        }
        made[i][3]=in[i][3];
        made[3][i]=place[i];
    }
    made[3][3]=in[3][3];
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) out[i][j]=made[i][j];
    return true;
}
// The drum of hand `hand`'s gun: `body` the body's unit rows (left, up,
// forward), `eye` its eye.
inline bool FencerBeltDrum(unsigned hand,const float body[3][3],const float eye[3],const float in[4][4],float out[4][4]) noexcept {
    if(hand>1) return false;
    float rows[3][3]{},place[3]{};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j)
        rows[i][j]=kFencerDrumRows[hand][i][0]*body[0][j]+kFencerDrumRows[hand][i][1]*body[1][j]+kFencerDrumRows[hand][i][2]*body[2][j];
    const float side=hand==1?-kFencerDrumPlace[0]:kFencerDrumPlace[0];
    for(int j=0;j<3;++j) place[j]=eye[j]+side*body[0][j]+kFencerDrumPlace[1]*body[1][j]+kFencerDrumPlace[2]*body[2][j];
    return FencerBeltPut(rows,place,in,out);
}
// Link `link` (0 the upper, chain0; 1 the lower, chain1) of hand `hand`'s gun:
// `gun` the gun's root as it is drawn, `up` the world's up. The link's place
// and rows against the root are read off the table at the root's pitch, between
// the two pitches beside it.
inline bool FencerBeltLink(unsigned hand,unsigned link,const float gun[4][4],const float up[3],const float in[4][4],float out[4][4]) noexcept {
    if(hand>1 || link>1) return false;
    float axes[3][3]{};
    for(int i=0;i<3;++i) {
        const float length=std::sqrt(gun[i][0]*gun[i][0]+gun[i][1]*gun[i][1]+gun[i][2]*gun[i][2]);
        if(!std::isfinite(length) || length<1e-4f) return false;
        for(int j=0;j<3;++j) axes[i][j]=gun[i][j]/length;
    }
    const float rise=std::clamp(axes[2][0]*up[0]+axes[2][1]*up[1]+axes[2][2]*up[2],-1.0f,1.0f);
    if(!std::isfinite(rise)) return false;
    const float pitch=std::asin(rise)*57.29578f;
    constexpr int last=static_cast<int>(sizeof(kFencerBeltPitch)/sizeof(kFencerBeltPitch[0]))-1;
    int k=0; while(k<last-1 && pitch>kFencerBeltPitch[k+1]) ++k;
    const float t=std::clamp((pitch-kFencerBeltPitch[k])/(kFencerBeltPitch[k+1]-kFencerBeltPitch[k]),0.0f,1.0f);
    // In the root's axes; the right gun's is the left's mirror, left to right.
    float local[4][3]{};
    for(int i=0;i<4;++i) for(int j=0;j<3;++j) {
        const float value=kFencerBeltLinks[link][k][i][j]*(1-t)+kFencerBeltLinks[link][k+1][i][j]*t;
        const bool flipped=hand==1 && (i==0?j==0:(i==1)!=(j==0));       // the place's x; a row's x or the x row, not both
        local[i][j]=flipped?-value:value;
    }
    // The rows between two pitches, made square again.
    for(int i=1;i<4;++i) {
        for(int before=1;before<i;++before) {
            const float along=local[i][0]*local[before][0]+local[i][1]*local[before][1]+local[i][2]*local[before][2];
            for(int j=0;j<3;++j) local[i][j]-=along*local[before][j];
        }
        const float length=std::sqrt(local[i][0]*local[i][0]+local[i][1]*local[i][1]+local[i][2]*local[i][2]);
        if(length<1e-4f) return false;
        for(int j=0;j<3;++j) local[i][j]/=length;
    }
    float rows[3][3]{},place[3]{};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j)
        rows[i][j]=local[i+1][0]*axes[0][j]+local[i+1][1]*axes[1][j]+local[i+1][2]*axes[2][j];
    for(int j=0;j<3;++j) place[j]=gun[3][j]+local[0][0]*axes[0][j]+local[0][1]*axes[1][j]+local[0][2]*axes[2][j];
    return FencerBeltPut(rows,place,in,out);
}
// The backpack boosters.
//
// The Fencer's Booster effects (HeavyArmor ctor 565130, heavy_booster_setting)
// sit on four attach points of the chest bone mune -- ブースターL1/R1/L2/R2,
// 0.66 and 0.57 out from it and 0.158 to either side (OBJECT/P607_FENCER.SGO,
// animation_model block; research/vehicle/fencer_boosters.py) -- and copy the
// attach point's matrix every update. The dash and jump clips tip the chest
// forward and back, and the boosters went with it: standing they are 0.66 m
// behind the eye and 0.16 below; in the dashes they rose to 0.38-0.52 m above
// it and in the back dash and the boost recovery came 0.47-0.58 m in front of
// it (research/fencer/booster_fixed.py) -- in view every time (the user,
// 2026-10-24: "ダッシュブースタージャンプブースターが元のアニメーションと一緒に移動
// してるせいで、毎回視界に映り込む ... ブースターは消さずに固定にできる？").
// So each booster keeps its place and turn against the chest as the game has
// them this frame, and the chest is put where it stands at a level aim, from
// the eye in the body's frame (left, up, forward), the stance's 6.4 degrees of
// twist taken out (the gatling drum's way): kFencerMunePlace / kFencerMuneRows
// (booster_fixed.py). Drawn as ever; only where.
inline constexpr float kFencerMunePlace[3]={0.002f,-0.389f,-0.035f};
inline constexpr float kFencerMuneRows[3][3]={{-0.0099f,0.9731f,0.2300f},{-0.0040f,0.2299f,-0.9732f},{-0.9999f,-0.0106f,0.0016f}};
// Something carried by the chest held on the standing chest: `live` its matrix
// as the game made it, `chestRows` / `chestAt` the chest bone this frame,
// `body` the body's unit rows (left, up, forward), `eye` its eye, and the
// chest's standing `place` (from the eye) and `rows`, in the body's frame.
// The rows keep their lengths and the W lane is kept.
inline bool StandingChestHeld(const float live[4][4],const float chestRows[3][3],const float chestAt[3],
                              const float body[3][3],const float eye[3],const float place[3],const float rows[3][3],
                              float out[4][4]) noexcept {
    float chest[3][3]{};
    for(int k=0;k<3;++k) {
        const float length=std::sqrt(chestRows[k][0]*chestRows[k][0]+chestRows[k][1]*chestRows[k][1]+chestRows[k][2]*chestRows[k][2]);
        if(!std::isfinite(length) || length<1e-4f) return false;
        for(int j=0;j<3;++j) chest[k][j]=chestRows[k][j]/length;
    }
    float standing[3][3]{},standingAt[3]{};
    for(int k=0;k<3;++k) for(int j=0;j<3;++j)
        standing[k][j]=rows[k][0]*body[0][j]+rows[k][1]*body[1][j]+rows[k][2]*body[2][j];
    for(int j=0;j<3;++j) standingAt[j]=eye[j]+place[0]*body[0][j]+place[1]*body[1][j]+place[2]*body[2][j];
    float made[4][4]{};
    for(int i=0;i<4;++i) {
        float local[3]{};
        for(int k=0;k<3;++k) {
            float d=0;
            for(int j=0;j<3;++j) d+=(i<3?live[i][j]:live[3][j]-chestAt[j])*chest[k][j];
            local[k]=d;
        }
        for(int j=0;j<3;++j) {
            made[i][j]=(i<3?0.0f:standingAt[j])+local[0]*standing[0][j]+local[1]*standing[1][j]+local[2]*standing[2][j];
            if(!std::isfinite(made[i][j])) return false;
        }
        made[i][3]=live[i][3];
    }
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) out[i][j]=made[i][j];
    return true;
}
// A Fencer's booster (`live`) on his standing chest.
inline bool FencerBoosterHeld(const float live[4][4],const float chestRows[3][3],const float chestAt[3],
                              const float body[3][3],const float eye[3],float out[4][4]) noexcept {
    return StandingChestHeld(live,chestRows,chestAt,body,eye,kFencerMunePlace,kFencerMuneRows,out);
}
// The Wing Diver's streamers (PaleWingStreamer, the white lines she trails in
// flight): six sources on her backpack's wing joints (P606_WINGDIVER.SGO
// pale_wing_streamer_setting -- streamer_l/r on booster6B_joint2, _lt/_rt/_lb/_rb
// on booster6B_joint1; research/vehicle/wingdiver_streamers.py). Her flight
// clips lean her body and bring them 0.26-0.71 m in front of the eye (fly_axis,
// the rolls): a line across the middle of the view. Held on her standing chest
// they stay 0.06-0.23 m behind it in every flight clip, the wings' own spread
// against the chest kept (wingdiver_streamer_held.py; the user, 2026-10-24:
// "アニメーションに追従してるのが問題だから背中のウイングの位置に発生源を固定して").
// Her chest at stand_hand, from the eye in the body's frame, untwisted.
inline constexpr float kDiverMunePlace[3]={0.004f,-0.350f,0.040f};
inline constexpr float kDiverMuneRows[3][3]={{-0.0218f,0.9997f,0.0111f},{0.0057f,0.0112f,-0.9999f},{-0.9997f,-0.0218f,-0.0059f}};
// A streamer's source as her update hands it over: its place and its way (the
// attach point's translation and its negated third row), each a vec4 whose W
// is kept.
inline bool DiverStreamerHeld(const float place[4],const float way[4],const float chestRows[3][3],const float chestAt[3],
                              const float body[3][3],const float eye[3],float heldPlace[4],float heldWay[4]) noexcept {
    const float live[4][4]={{1,0,0,0},{0,1,0,0},{way[0],way[1],way[2],way[3]},{place[0],place[1],place[2],place[3]}};
    float out[4][4]{};
    // The way's length is the attach point's own scale; it is kept as the rows' are.
    if(!(way[0]*way[0]+way[1]*way[1]+way[2]*way[2]>1e-8f)) return false;
    if(!StandingChestHeld(live,chestRows,chestAt,body,eye,kDiverMunePlace,kDiverMuneRows,out)) return false;
    for(int j=0;j<4;++j) { heldWay[j]=out[2][j]; heldPlace[j]=out[3][j]; }
    return true;
}
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
