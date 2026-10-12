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
    std::printf("Fencer heavy aim: %d failures\n",failures);
    return failures?1:0;
}
