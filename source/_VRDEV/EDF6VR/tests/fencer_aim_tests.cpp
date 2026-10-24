#include "fencer_aim.h"
#include "fencer_mount.h"
#include <cmath>
#include <cstdio>
static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
int main() {
    using edf6vr::FencerAimState; using edf6vr::FencerAimStep; using edf6vr::FencerAimKick;
    const float rate=3.14159265f, smoothing=0.12f;
    // First sample: takes the target as it is.
    { FencerAimState s; FencerAimStep(s,0.5f,0.2f,10.0,rate,smoothing); CHECK(s.valid && std::fabs(s.yaw-0.5f)<1e-6f && std::fabs(s.pitch-0.2f)<1e-6f); }
    // Held on a new target it converges, and never faster than the rate.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,smoothing);
        double t=0; float last=0;
        for(int i=0;i<200;++i) { t+=1.0/90; FencerAimStep(s,1.0f,0,t,rate,smoothing); CHECK(s.yaw>=last-1e-6f); CHECK(s.yaw-last<=rate/90+1e-4f); last=s.yaw; }
        CHECK(std::fabs(s.yaw-1.0f)<0.01f && std::fabs(s.pitch)<1e-6f);
    }
    // The rate cap: a 90 degree step at 180 deg/s takes about half a second.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,0.0f);
        double t=0; int steps=0;
        while(std::fabs(s.yaw-1.5707963f)>0.01f && steps<1000) { t+=1.0/90; FencerAimStep(s,1.5707963f,0,t,rate,0.0f); ++steps; }
        CHECK(steps>=44 && steps<=47);
    }
    // Yaw wraps: from +170 to -170 degrees goes the short way, through 180.
    {
        FencerAimState s; FencerAimStep(s,2.96706f,0,0,rate,0.0f);
        FencerAimStep(s,-2.96706f,0,0.05,rate,0.0f);
        CHECK(s.yaw>2.96706f || s.yaw<-2.96706f);
    }
    // Recoil throws the pitch up and the pursuit brings it back down.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,smoothing);
        FencerAimKick(s,0.1f); CHECK(std::fabs(s.pitch-0.1f)<1e-6f);
        double t=0; for(int i=0;i<90;++i) { t+=1.0/90; FencerAimStep(s,0,0,t,rate,smoothing); }
        CHECK(s.pitch<0.001f && s.pitch>=0);
    }
    // Nonsense is ignored; a long hitch is treated as a tenth of a second.
    {
        FencerAimState s; FencerAimStep(s,0,0,0,rate,smoothing);
        FencerAimStep(s,NAN,0,1,rate,smoothing); CHECK(s.yaw==0);
        FencerAimStep(s,1.0f,0,5.0,rate,0.0f); CHECK(s.yaw<=rate*0.1f+1e-5f && s.yaw>0);
    }
    // --- How a hand weapon sits on each arm (fencer_mount.h) ---------------
    using edf6vr::FencerMount; using edf6vr::FencerMountLay;
    auto det=[](const float m[3][3]) {
        return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    };
    auto near3=[](const float a[3],float x,float y,float z,float within=1e-3f) {
        return std::fabs(a[0]-x)<within && std::fabs(a[1]-y)<within && std::fabs(a[2]-z)<within;
    };
    // The hand's frame the rows are laid on: left, up, ahead.
    for(unsigned hand=0;hand<2;++hand) {
        float lay[3][3]{};
        // "arms": the same way round in either hand.
        FencerMountLay(FencerMount::Arms,hand,lay);
        CHECK(near3(lay[0],1,0,0) && near3(lay[1],0,1,0) && near3(lay[2],0,0,1));
        // "armsFlip": as built in the right hand, turned half round about the
        // barrel in the left. A turn, never a mirror image.
        FencerMountLay(FencerMount::Flip,hand,lay);
        const float turn=hand==0?-1.0f:1.0f;
        CHECK(near3(lay[0],turn,0,0) && near3(lay[1],0,turn,0) && near3(lay[2],0,0,1));
        CHECK(std::fabs(det(lay)-1)<1e-4f);
        // The Arm Hound: the arm's line through the model lies along the hand's
        // ahead, the box is on the outer side of the arm (its -X), upside down
        // in the left arm, and its nose points 10.4 degrees in.
        FencerMountLay(FencerMount::Forearm,hand,lay);
        CHECK(std::fabs(det(lay)-1)<1e-3f);
        for(int i=0;i<3;++i) CHECK(std::fabs(lay[i][0]*lay[i][0]+lay[i][1]*lay[i][1]+lay[i][2]*lay[i][2]-1)<2e-3f);
        const float arm[3]={-edf6vr::kFencerForearmLean,0,edf6vr::kFencerForearmAlong};
        float along[3]{};
        for(int j=0;j<3;++j) along[j]=arm[0]*lay[0][j]+arm[1]*lay[1][j]+arm[2]*lay[2][j];
        CHECK(near3(along,0,0,1,2e-3f));
        CHECK(hand==0?-lay[0][0]>0.9f:-lay[0][0]<-0.9f);       // -X: to the left of the left arm, to the right of the right
        CHECK(std::fabs(lay[1][1]-turn)<1e-4f);                // its top: down in the left arm
        CHECK(hand==0?lay[2][0]<-0.15f:lay[2][0]>0.15f);       // its nose: towards the other arm
        CHECK(std::fabs(std::asin(std::fabs(lay[2][0]))*57.29578f-10.4f)<0.2f);
        // The palm sits at kFencerForearmPalm of the model, whatever the hand's frame.
        const float frame[3][3]={{0.6f,0,-0.8f},{0,1,0},{0.8f,0,0.6f}};   // a hand turned 53 degrees to its left
        float rows[3][3]{};
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) rows[i][j]=(lay[i][0]*frame[0][j]+lay[i][1]*frame[1][j]+lay[i][2]*frame[2][j])*2.0f;   // rows of any length
        const float palm[3]={10,1.3f,-4};
        float root[3]{};
        CHECK(edf6vr::FencerMountRoot(rows,palm,edf6vr::kFencerForearmPalm,root));
        float at[3]{};
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) at[i]+=(palm[j]-root[j])*rows[i][j]/2.0f;
        CHECK(near3(at,edf6vr::kFencerForearmPalm[0],edf6vr::kFencerForearmPalm[1],edf6vr::kFencerForearmPalm[2],1e-4f));
        // Inside the box's open end: x 0.03..0.19, y -0.15..0.16 of the model (h_missile_arm01 at z -0.05).
        CHECK(at[0]>0.04f && at[0]<0.18f && std::fabs(at[1])<0.1f);
    }
    {
        const float none[3][3]={{0,0,0},{0,1,0},{0,0,1}};
        const float palm[3]={0,0,0}; float root[3]{};
        CHECK(!edf6vr::FencerMountRoot(none,palm,edf6vr::kFencerForearmPalm,root));
    }
    // --- A left weapon's lunge: the root motion's step onto the left aim ----
    {
        auto bodyAt=[](float yawDegrees,float body[3][3]) {   // a body facing that yaw: rows X (left), Y, Z (ahead)
            const float a=yawDegrees/57.29578f,c=std::cos(a),s=std::sin(a);
            const float r[3][3]={{c,0,-s},{0,1,0},{s,0,c}};
            for(int i=0;i<3;++i) for(int j=0;j<3;++j) body[i][j]=r[i][j];
        };
        auto world=[](const float body[3][3],const float step[3],float out[3]) {
            for(int j=0;j<3;++j) out[j]=step[0]*body[0][j]+step[1]*body[1][j]+step[2]*body[2][j];
        };
        float body[3][3]{},out[3]{};
        bodyAt(30,body);                              // the body faces the right hand's aim, 30 degrees
        // A tick of the twin spear's dash: 0.48 m straight ahead of the body.
        float step[3]={0,0,0.48f};
        world(body,step,out);
        CHECK(std::fabs(std::atan2(out[0],out[2])*57.29578f-30)<0.01f);     // the game's own: along the body
        CHECK(edf6vr::FencerTurnMove(body,-50/57.29578f,step));             // the left hand aims at -50
        world(body,step,out);
        CHECK(std::fabs(std::atan2(out[0],out[2])*57.29578f+50)<0.01f);     // now along the left aim
        CHECK(std::fabs(std::sqrt(out[0]*out[0]+out[1]*out[1]+out[2]*out[2])-0.48f)<1e-5f);   // as far
        // A heavy cannon's kick goes back along the left aim; a rise stays a rise.
        float kick[3]={0,0.05f,-0.2f};
        CHECK(edf6vr::FencerTurnMove(body,-50/57.29578f,kick));
        world(body,kick,out);
        CHECK(std::fabs(std::atan2(-out[0],-out[2])*57.29578f+50)<0.01f && std::fabs(out[1]-0.05f)<1e-6f);
        // The left aim where the body already faces: nothing changes.
        float same[3]={0.1f,0.02f,0.3f};
        CHECK(edf6vr::FencerTurnMove(body,30/57.29578f,same));
        CHECK(near3(same,0.1f,0.02f,0.3f,1e-5f));
        // Rows of any length (a scaled body) give the same step in the world.
        float big[3][3]{}; bodyAt(30,big);
        for(auto& row:big) for(float& v:row) v*=2;
        float scaled[3]={0,0,0.24f};                  // 0.48 m of the world along rows twice as long
        CHECK(edf6vr::FencerTurnMove(big,-50/57.29578f,scaled));
        world(big,scaled,out);
        CHECK(std::fabs(std::atan2(out[0],out[2])*57.29578f+50)<0.01f && std::fabs(std::sqrt(out[0]*out[0]+out[2]*out[2])-0.48f)<1e-5f);
        const float none[3][3]={{1,0,0},{0,0,0},{0,0,1}};
        float lost[3]={0,0,1};
        CHECK(!edf6vr::FencerTurnMove(none,0,lost) && lost[2]==1);
        float bad[3]={0,0,1};
        CHECK(!edf6vr::FencerTurnMove(body,NAN,bad) && bad[2]==1);
    }
    // Whose attack moves the soldier.
    {
        bool began=false;
        auto step=[&](edf6vr::FencerMoveOwner& owner,bool left,bool right,bool leftMelee,bool rightMelee) {
            const bool playing[2]={left,right},melee[2]={leftMelee,rightMelee};
            return owner.Update(playing,melee,began);
        };
        // Two of a kind (two blades here): the one that began last among those playing.
        edf6vr::FencerMoveOwner blades;
        CHECK(step(blades,false,false,true,true)==-1 && !began);
        CHECK(step(blades,true,false,true,true)==0 && began);        // the left's begins
        CHECK(step(blades,true,false,true,true)==0 && !began);       // and goes on
        CHECK(step(blades,true,true,true,true)==1 && !began);        // the right's begins later: it moves him
        CHECK(step(blades,true,false,true,true)==0 && !began);       // the right's ends: back to the left's, no new direction
        CHECK(step(blades,false,false,true,true)==-1);
        CHECK(step(blades,false,true,true,true)==1 && !began);
        CHECK(step(blades,true,true,true,true)==0 && began);         // the left's begins while the right's plays
        CHECK(step(blades,false,false,true,true)==-1);
        CHECK(step(blades,true,true,true,true)==0 && began);         // both at once: the left's
        // A twin spear in the left hand, a gun in the right: the dash is the spear's
        // whenever the gun fires, before it, during it or after.
        edf6vr::FencerMoveOwner spearAndGun;
        CHECK(step(spearAndGun,false,true,true,false)==1);           // only the gun: its kick is its own
        CHECK(step(spearAndGun,true,true,true,false)==0 && began);   // the dash begins under fire
        CHECK(step(spearAndGun,true,false,true,false)==0);
        CHECK(step(spearAndGun,true,true,true,false)==0 && !began);  // a shot in the middle of the dash
        CHECK(step(spearAndGun,false,true,true,false)==1);
        // The other way round: the right hand's roller under the left gun's fire stays the game's.
        edf6vr::FencerMoveOwner gunAndRoller;
        CHECK(step(gunAndRoller,true,false,false,true)==0 && began); // only the left gun: its kick goes its way
        CHECK(step(gunAndRoller,true,true,false,true)==1);
        CHECK(step(gunAndRoller,false,true,false,true)==1);
        CHECK(step(gunAndRoller,true,true,false,true)==1 && began);  // the left gun fires again: still the roller's
    }
    // --- A swung weapon's rows from its root as it is now -------------------
    {
        // The game's aim frame and the hand's: 40 degrees apart in yaw, the hand rolled 20.
        const float c40=std::cos(0.6981317f),s40=std::sin(0.6981317f),c20=std::cos(0.34906585f),s20=std::sin(0.34906585f);
        const float from[3][3]={{1,0,0},{0,1,0},{0,0,1}};
        const float yawed[3][3]={{c40,0,-s40},{0,1,0},{s40,0,c40}};
        float to[3][3]{};
        for(int j=0;j<3;++j) { to[0][j]=c20*yawed[0][j]+s20*yawed[1][j]; to[1][j]=-s20*yawed[0][j]+c20*yawed[1][j]; to[2][j]=yawed[2][j]; }
        // The Force Blade's swing (recoil_hammer_Swing_armR): its muzzle (+Z) points 27.8 degrees
        // right of the aim one tick before the fire event and 1.5 at it.
        auto swing=[](float yawLeftDegrees,float root[3][3]) {
            const float a=yawLeftDegrees/57.29578f,c=std::cos(a),s=std::sin(a);
            const float r[3][3]={{c,0,-s},{0,1,0},{s,0,c}};
            for(int i=0;i<3;++i) for(int j=0;j<3;++j) root[i][j]=r[i][j];
        };
        float before[3][3]{},atEvent[3][3]{},rows[3][3]{};
        swing(-27.8f,before); swing(-1.5f,atEvent);
        // Same frames: the rows are the root's own.
        CHECK(edf6vr::FencerSwingRows(atEvent,from,from,rows));
        for(int i=0;i<3;++i) CHECK(near3(rows[i],atEvent[i][0],atEvent[i][1],atEvent[i][2],1e-5f));
        // Onto the hand: the muzzle leaves 1.5 degrees off the HAND's ahead from
        // the root at the event, and 27.8 from the root a tick before.
        auto offAhead=[&](const float r[3][3]) {
            float out[3][3]{};
            CHECK(edf6vr::FencerSwingRows(r,from,to,out));
            CHECK(std::fabs(det(out)-1)<1e-4f);
            const float d=out[2][0]*to[2][0]+out[2][1]*to[2][1]+out[2][2]*to[2][2];
            return std::acos(d>1?1:d)*57.29578f;
        };
        CHECK(std::fabs(offAhead(atEvent)-1.5f)<0.05f);
        CHECK(std::fabs(offAhead(before)-27.8f)<0.05f);
        // A point fixed in the root (the muzzle) carried with those rows is the
        // root's own point turned from the one frame to the other.
        float out[3][3]{};
        CHECK(edf6vr::FencerSwingRows(atEvent,from,to,out));
        const float local[3]={0.2f,1.7f,0.4f};
        float native[3]{},carried[3]{},turned[3]{};
        for(int j=0;j<3;++j) for(int i=0;i<3;++i) { native[j]+=local[i]*atEvent[i][j]; carried[j]+=local[i]*out[i][j]; }
        for(int j=0;j<3;++j) for(int k=0;k<3;++k) turned[j]+=(native[0]*from[k][0]+native[1]*from[k][1]+native[2]*from[k][2])*to[k][j];
        CHECK(near3(carried,turned[0],turned[1],turned[2],1e-5f));
        const float bad[3][3]={{NAN,0,0},{0,1,0},{0,0,1}};
        CHECK(!edf6vr::FencerSwingRows(bad,from,to,out));
    }
    {
        // A gatling's feed belt. Its drum hangs on the player's body, wherever
        // the game's animation has it (FencerBeltDrum).
        const float ahead[3][3]={{1,0,0},{0,1,0},{0,0,1}},eye[3]={10,20,30},up[3]={0,1,0};
        const float gameA[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{3,4,5,1}};                       // as the game has the node...
        const float gameB[4][4]={{0,0,-2,0},{0,2,0,0},{2,0,0,0},{-7,1,9,1}};                     // ...and in a reload: elsewhere, turned, twice the size
        float left[4][4]{},right[4][4]{},again[4][4]{};
        CHECK(edf6vr::FencerBeltDrum(0,ahead,eye,gameA,left) && edf6vr::FencerBeltDrum(1,ahead,eye,gameA,right));
        // Behind the shoulder on the gun's own side, the two sides mirrored.
        CHECK(std::fabs(left[3][0]-10.417f)<1e-4f && std::fabs(left[3][1]-19.872f)<1e-4f && std::fabs(left[3][2]-29.623f)<1e-4f);
        CHECK(std::fabs(right[3][0]-9.583f)<1e-4f && right[3][1]==left[3][1] && right[3][2]==left[3][2]);
        CHECK(left[3][3]==1 && left[0][3]==0);
        // Nothing of the game's pose is left in it but the size of its rows.
        CHECK(edf6vr::FencerBeltDrum(0,ahead,eye,gameB,again));
        for(int j=0;j<3;++j) CHECK(again[3][j]==left[3][j]);
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) CHECK(std::fabs(again[i][j]-2*left[i][j])<1e-5f);
        // The body turned a quarter to its left (yaw 90: its forward along +X, its left along -Z): the drum goes with it.
        const float turned[3][3]={{0,0,-1},{0,1,0},{1,0,0}};
        float quarter[4][4]{};
        CHECK(edf6vr::FencerBeltDrum(0,turned,eye,gameA,quarter));
        CHECK(std::fabs(quarter[3][0]-(10-0.377f))<1e-4f && std::fabs(quarter[3][2]-(30-0.417f))<1e-4f && std::fabs(quarter[3][1]-19.872f)<1e-4f);

        // Its two links ride with the gun (FencerBeltLink). A gun's root: turned
        // `yaw` to its left, pitched up, rolled about its barrel, at `at`.
        const auto gun=[](float yaw,float pitch,float roll,float x,float y,float z,float out[4][4]) {
            const float k=0.01745329252f,cy=std::cos(yaw*k),sy=std::sin(yaw*k),cp=std::cos(pitch*k),sp=std::sin(pitch*k),cr=std::cos(roll*k),sr=std::sin(roll*k);
            const float f[3]={sy*cp,sp,cy*cp},r[3]={cy,0,-sy};
            const float u[3]={f[1]*r[2]-f[2]*r[1],f[2]*r[0]-f[0]*r[2],f[0]*r[1]-f[1]*r[0]};
            for(int j=0;j<3;++j) { out[0][j]=r[j]*cr+u[j]*sr; out[1][j]=u[j]*cr-r[j]*sr; out[2][j]=f[j]; }
            out[0][3]=out[1][3]=out[2][3]=0; out[3][0]=x; out[3][1]=y; out[3][2]=z; out[3][3]=1;
        };
        // A link in its gun's own axes: its place from the root, and its rows.
        const auto inGun=[](const float g[4][4],const float node[4][4],float place[3],float rows[3][3]) {
            for(int k=0;k<3;++k) {
                place[k]=(node[3][0]-g[3][0])*g[k][0]+(node[3][1]-g[3][1])*g[k][1]+(node[3][2]-g[3][2])*g[k][2];
                for(int i=0;i<3;++i) rows[i][k]=node[i][0]*g[k][0]+node[i][1]*g[k][1]+node[i][2]*g[k][2];
            }
        };
        float level[4][4]{},link[4][4]{},place[3]{},rows[3][3]{};
        gun(0,0,0,10.28f,19.7f,30.45f,level);
        // Held level: the lower link behind and below the root, on the gun's outer side, where the game has it at a level aim.
        CHECK(edf6vr::FencerBeltLink(0,1,level,up,gameA,link));
        inGun(level,link,place,rows);
        CHECK(near3(place,0.117f,-0.457f,-0.711f,2e-3f));
        CHECK(edf6vr::FencerBeltLink(1,1,level,up,gameA,link));
        inGun(level,link,place,rows);
        CHECK(near3(place,-0.117f,-0.457f,-0.711f,2e-3f));
        // Turned, rolled, carried anywhere: the link is where it was against the gun -- the strip's last stretch,
        // which hangs half on this link and half on the gun, keeps its shape.
        for(unsigned hand=0;hand<2;++hand) for(unsigned n=0;n<2;++n) for(float pitch:{-60.0f,0.0f,35.0f,80.0f}) {
            float base[4][4]{},baseLink[4][4]{},basePlace[3]{},baseRows[3][3]{};
            gun(0,pitch,0,0,0,0,base);
            CHECK(edf6vr::FencerBeltLink(hand,n,base,up,gameA,baseLink));
            inGun(base,baseLink,basePlace,baseRows);
            CHECK(std::sqrt(basePlace[0]*basePlace[0]+basePlace[1]*basePlace[1]+basePlace[2]*basePlace[2])<1.0f && basePlace[2]<-0.6f);
            const float poses[4][5]={{70,0,3,-2,8},{-140,25,-1,5,0},{15,-40,0.3f,0.1f,0.6f},{180,90,7,7,7}};
            for(const auto& pose:poses) {
                float moved[4][4]{},movedLink[4][4]{},movedPlace[3]{},movedRows[3][3]{};
                gun(pose[0],pitch,pose[1],pose[2],pose[3],pose[4],moved);
                CHECK(edf6vr::FencerBeltLink(hand,n,moved,up,gameA,movedLink));
                inGun(moved,movedLink,movedPlace,movedRows);
                CHECK(near3(movedPlace,basePlace[0],basePlace[1],basePlace[2],2e-4f));
                for(int i=0;i<3;++i) CHECK(near3(movedRows[i],baseRows[i][0],baseRows[i][1],baseRows[i][2],2e-4f));
            }
        }
        // From straight down to straight up: square rows, no jump (under 1.5 cm a degree), never further than the
        // game takes it, and the right gun's link the left's mirror.
        float before[2][3]{}; bool first=true;
        for(int degrees=-90;degrees<=90;++degrees) {
            float g[4][4]{};
            gun(0,static_cast<float>(degrees),0,0,0,0,g);
            for(unsigned n=0;n<2;++n) {
                float l[4][4]{},r[4][4]{},pl[3]{},pr[3]{},rl[3][3]{},rr[3][3]{};
                CHECK(edf6vr::FencerBeltLink(0,n,g,up,gameA,l) && edf6vr::FencerBeltLink(1,n,g,up,gameA,r));
                inGun(g,l,pl,rl); inGun(g,r,pr,rr);
                for(int i=0;i<3;++i) for(int k=0;k<3;++k) {
                    const float dot=l[i][0]*l[k][0]+l[i][1]*l[k][1]+l[i][2]*l[k][2];
                    CHECK(std::fabs(dot-(i==k?1.0f:0.0f))<1e-4f);
                    CHECK(std::fabs(rr[i][k]-((i==0)!=(k==0)?-rl[i][k]:rl[i][k]))<1e-4f);
                }
                CHECK(std::fabs(pr[0]+pl[0])<1e-5f && std::fabs(pr[1]-pl[1])<1e-5f && std::fabs(pr[2]-pl[2])<1e-5f);
                CHECK(pl[2]<-0.6f && pl[2]>-0.9f && std::fabs(pl[0])<0.25f && pl[1]>-0.75f && pl[1]<0.45f);
                if(!first) CHECK(std::sqrt((pl[0]-before[n][0])*(pl[0]-before[n][0])+(pl[1]-before[n][1])*(pl[1]-before[n][1])+(pl[2]-before[n][2])*(pl[2]-before[n][2]))<0.015f);
                for(int j=0;j<3;++j) before[n][j]=pl[j];
            }
            first=false;
        }
        // The size of the game's rows and its W lane are kept; nothing else of its pose.
        float sized[4][4]{},plain[4][4]{};
        CHECK(edf6vr::FencerBeltLink(0,0,level,up,gameB,sized) && edf6vr::FencerBeltLink(0,0,level,up,gameA,plain));
        for(int j=0;j<3;++j) CHECK(sized[3][j]==plain[3][j]);
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) CHECK(std::fabs(sized[i][j]-2*plain[i][j])<1e-5f);
        CHECK(sized[3][3]==1 && sized[1][3]==0);
        // The world's up is the caller's: a level gun in a world lying on its side is a level gun.
        const float sideUp[3]={1,0,0};
        float lying[4][4]={{0,-1,0,0},{1,0,0,0},{0,0,1,0},{0,0,0,1}},lyingLink[4][4]{},lyingPlace[3]{},lyingRows[3][3]{};
        CHECK(edf6vr::FencerBeltLink(0,1,lying,sideUp,gameA,lyingLink));
        inGun(lying,lyingLink,lyingPlace,lyingRows);
        CHECK(near3(lyingPlace,0.117f,-0.457f,-0.711f,2e-3f));
        // No such link, no such hand, a gun or a node that is no matrix: nothing written.
        float untouched[4][4]{{9,9,9,9},{9,9,9,9},{9,9,9,9},{9,9,9,9}};
        const float flat[4][4]={{0,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
        CHECK(!edf6vr::FencerBeltLink(0,2,level,up,gameA,untouched) && !edf6vr::FencerBeltLink(2,0,level,up,gameA,untouched));
        CHECK(!edf6vr::FencerBeltLink(0,0,flat,up,gameA,untouched) && !edf6vr::FencerBeltLink(0,0,level,up,flat,untouched));
        CHECK(!edf6vr::FencerBeltDrum(2,ahead,eye,gameA,untouched) && !edf6vr::FencerBeltDrum(0,ahead,eye,flat,untouched));
        CHECK(untouched[0][0]==9 && untouched[3][3]==9);
    }
    {
        // The backpack boosters held on the standing chest (FencerBoosterHeld).
        const float body[3][3]={{1,0,0},{0,1,0},{0,0,1}},eye[3]={5,10,20};
        // The standing chest as the constants put it, and a booster 0.66 behind it: unchanged.
        float chest[3][3]{},chestAt[3]{};
        for(int k=0;k<3;++k) for(int j=0;j<3;++j) chest[k][j]=edf6vr::kFencerMuneRows[k][j];
        for(int j=0;j<3;++j) chestAt[j]=eye[j]+edf6vr::kFencerMunePlace[j];
        float live[4][4]{};
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) live[i][j]=(i==j?0.5f:0.0f);
        const float local[3]={0.08f,0.661f,-0.158f};   // ブースターL1 in the chest's axes
        for(int j=0;j<3;++j) live[3][j]=chestAt[j]+local[0]*chest[0][j]+local[1]*chest[1][j]+local[2]*chest[2][j];
        live[3][3]=1;
        float held[4][4]{};
        CHECK(edf6vr::FencerBoosterHeld(live,chest,chestAt,body,eye,held));
        for(int i=0;i<4;++i) CHECK(near3(held[i],live[i][0],live[i][1],live[i][2],1e-4f));
        CHECK(held[3][1]<eye[1]-0.1f && held[3][2]<eye[2]-0.5f);                // behind and below the eye
        // A dash tips the chest 70 degrees forward about its left-right axis and drops it: the booster, which
        // went with it (over the head), comes back to the same held place, turned the same against the body.
        const float t=1.2217f,ct=std::cos(t),st=std::sin(t);
        float tipped[3][3]{},tippedAt[3]{chestAt[0],chestAt[1]-0.3f,chestAt[2]+0.2f};
        for(int k=0;k<3;++k) {
            const float* r=chest[k];
            tipped[k][0]=r[0]; tipped[k][1]=r[1]*ct-r[2]*st; tipped[k][2]=r[1]*st+r[2]*ct;   // about x
        }
        float moved[4][4]{};
        for(int i=0;i<3;++i) { moved[i][0]=live[i][0]; moved[i][1]=live[i][1]*ct-live[i][2]*st; moved[i][2]=live[i][1]*st+live[i][2]*ct; moved[i][3]=0; }
        for(int j=0;j<3;++j) moved[3][j]=tippedAt[j]+local[0]*tipped[0][j]+local[1]*tipped[1][j]+local[2]*tipped[2][j];
        moved[3][3]=1;
        CHECK(moved[3][1]>live[3][1]+0.1f);                                    // the game's: thrown up
        float again[4][4]{};
        CHECK(edf6vr::FencerBoosterHeld(moved,tipped,tippedAt,body,eye,again));
        for(int i=0;i<4;++i) CHECK(near3(again[i],held[i][0],held[i][1],held[i][2],1e-4f));
        // The body turned a quarter: the booster goes round with it, staying behind.
        const float turned[3][3]={{0,0,-1},{0,1,0},{1,0,0}};
        float quarter[4][4]{};
        CHECK(edf6vr::FencerBoosterHeld(live,chest,chestAt,turned,eye,quarter));
        const float back[3]={held[3][0]-eye[0],held[3][1]-eye[1],held[3][2]-eye[2]};
        CHECK(near3(quarter[3],eye[0]+back[2],eye[1]+back[1],eye[2]-back[0],1e-4f));
        // No chest, nothing.
        float none[4][4]{{9,9,9,9},{9,9,9,9},{9,9,9,9},{9,9,9,9}};
        const float flat[3][3]{};
        CHECK(!edf6vr::FencerBoosterHeld(live,flat,chestAt,body,eye,none) && none[0][0]==9);
    }
    {
        // The Wing Diver's streamer source (DiverStreamerHeld): at her standing chest it is where it was; a way of
        // length 1 stays 1; a zero way is refused.
        const float body[3][3]={{1,0,0},{0,1,0},{0,0,1}},eye[3]={0,10,0};
        float chest[3][3]{},chestAt[3]{};
        for(int k=0;k<3;++k) for(int j=0;j<3;++j) chest[k][j]=edf6vr::kDiverMuneRows[k][j];
        for(int j=0;j<3;++j) chestAt[j]=eye[j]+edf6vr::kDiverMunePlace[j];
        const float place[4]={0.27f,9.71f,-0.26f,1},way[4]={0,0.6f,-0.8f,0.5f};
        float heldPlace[4]{},heldWay[4]{};
        CHECK(edf6vr::DiverStreamerHeld(place,way,chest,chestAt,body,eye,heldPlace,heldWay));
        CHECK(near3(heldPlace,place[0],place[1],place[2],1e-4f) && heldPlace[3]==1);
        CHECK(near3(heldWay,way[0],way[1],way[2],1e-4f) && heldWay[3]==0.5f);
        const float none[4]={0,0,0,0};
        CHECK(!edf6vr::DiverStreamerHeld(place,none,chest,chestAt,body,eye,heldPlace,heldWay));
    }
    std::printf("Fencer heavy aim: %d failures\n",failures);
    return failures?1:0;
}
