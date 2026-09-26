#include "pad_capture.h"
#define SetPadState CaptureTestPad
#include "../src/plugin.cpp"
#undef SetPadState
#include "../src/input_continuity.h" // standalone historical observer test, not part of the DLL
static int failures=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
int main() {
    edf6vr::GestureHold hold{};
    for(unsigned t=0;t<3000;t+=10) CHECK(!hold.Step(true,t));
    CHECK(hold.Step(true,3000));CHECK(!hold.Step(true,3010));
    CHECK(!hold.Step(false,3020));CHECK(!hold.Step(true,3030));
    CHECK(!hold.Step(true,7000)); // pause/tracking gap cannot complete a hold
    for(unsigned t=7010;t<10000;t+=10) CHECK(!hold.Step(true,t));
    CHECK(hold.Step(true,10000));CHECK(!hold.Step(true,10)); // reset clock
    hold={};CHECK(!hold.Step(true,100));CHECK(hold.Pulse(100));
    CHECK(!hold.Pulse(179));CHECK(hold.Pulse(180));
    CHECK(!hold.Step(false,221));CHECK(!hold.Pulse(500));
    CHECK(!hold.Step(true,600));CHECK(hold.Pulse(600));
    CHECK(!hold.Step(true,900));CHECK(hold.start==900); // gap restarts feedback too
    CHECK(hold.Pulse(900));
    for(unsigned t=910;t<3900;t+=10)hold.Step(true,t);
    CHECK(hold.Step(true,3900));CHECK(!hold.Pulse(3900));
    const float head[]={5,1.6f,8},other[]={4,1,8};
    const float beside[]={5,1.6f,8.18f}; // yaw=90, right=(-cos yaw,0,sin yaw)
    const float inFront[]={5.18f,1.6f,8};
    CHECK(edf6vr::NearTemple(head,beside,1.5707963f,.18f,0,0,.15f,.05f));
    CHECK(!edf6vr::NearTemple(head,inFront,1.5707963f,.18f,0,0,.15f,.05f));
    CHECK(!edf6vr::NearTemple(nullptr,beside,0,.18f,0,0,.15f,.05f));
    // The cached gameplay head is deliberately wrong. A menu has no soldier
    // updates; BuildPad must use the fresh explicit pose and retain Start/Back.
    g_lastHeadXr={100,100,100};g_lastHeadYaw=0;
    g_vrEnabled=false;g_fpsEnabled=false;g_adjustEnabled=false;g_padMode=1;
    edf6vr::ControllerState c{};c.present[1]=true;c.present[0]=false;
    c.stickClick[0]=c.stickClick[1]=true;c.stick[1][1]=1;c.trigger[1]=1;
    g_padSeenButtons=0;
    BuildPad(c,true,head,beside,other,1.5707963f,false);
    CHECK(g_gestureOn);
    CHECK((g_padSeenButtons&(kPadStart|kPadBack|kPadUp))==(kPadStart|kPadBack|kPadUp));
    CHECK(!(g_padSeenButtons&(kPadY|kPadLeftThumb|kPadRightThumb)));
    CHECK(g_padSeenRight==255); // gesture never interrupts held firing trigger
    g_padSeenButtons=0;
    BuildPad(c,false,head,beside,other,1.5707963f,false);
    CHECK(!g_gestureOn);CHECK((g_padSeenButtons&kPadY)!=0);
    // A tracked left hand can request height reset without a tracked right hand.
    g_vrEnabled=true;g_fpsEnabled=true;c.present[0]=true;c.stick[0][1]=1;
    const auto now=GetTickCount64();
    g_heightHold={now-3001,now,true,false};
    BuildPad(c,false,head,other,beside,1.5707963f,true);
    CHECK(g_heightResetRequested.exchange(false));
    CHECK(edf6vr::capturedTestPad.leftY>30000); // height hold/reset never consumes walking
    BuildPad(c,false,head,other,beside,1.5707963f,true);
    CHECK(!g_heightResetRequested.load()); // one shot until release
    c.stick[0][1]=0;BuildPad(c,false,head,other,beside,1.5707963f,true);
    CHECK(!g_heightHold.active);
    // RIGHT (weapon) uses exactly the same continuous hold as UP. Cancellation
    // must not enter editing or change the six placement values, even just
    // before 3s. LEFT is the same hold aimed at the hand model.
    g_adjustEnabled=true;g_adjust=Adjusting::Off;g_adjustHold={};
    c.stick[0][0]=1;
    CHECK(Adjusting(c,true,head,beside,1.5707963f));
    CHECK(g_adjust==Adjusting::Counting && !g_adjustHold.fired && !g_handAdjust);
    BuildPad(c,false,head,other,beside,1.5707963f,true);
    CHECK(g_adjust==Adjusting::Counting && edf6vr::capturedTestPad.leftX>30000);
    auto tick=GetTickCount64();g_adjustHold={tick-2999,tick,true,false};
    c.stick[0][0]=0;
    CHECK(!Adjusting(c,true,head,beside,1.5707963f));
    CHECK(g_adjust!=Adjusting::Active && !g_adjustHold.active);
    c.stick[0][0]=-1;
    CHECK(Adjusting(c,true,head,beside,1.5707963f));
    CHECK(g_adjust==Adjusting::Counting && g_handAdjust);
    g_adjust=Adjusting::Off;g_adjustHold={};
    c.stick[0][0]=1;
    CHECK(Adjusting(c,true,head,beside,1.5707963f));
    CHECK(g_adjust==Adjusting::Counting);
    CHECK(!Adjusting(c,false,head,beside,1.5707963f)); // left tracking lost
    CHECK(!g_adjustHold.active);
    CHECK(Adjusting(c,true,head,beside,1.5707963f));
    tick=GetTickCount64();g_adjustHold={tick-3001,tick,true,false};
    CHECK(Adjusting(c,true,head,beside,1.5707963f));
    CHECK(g_adjust==Adjusting::Active && g_adjustHold.fired);
    BuildPad(c,false,head,other,beside,1.5707963f,true);
    CHECK(edf6vr::capturedTestPad.leftX>30000); // editing owns other inputs, locomotion remains live
    g_adjustEnabled=false;Adjusting(c,true,head,beside,1.5707963f);c.stick[0][0]=0;
    edf6vr::InputContinuity trace{};edf6vr::PadState pad{};pad.rightTrigger=255;
    for(unsigned t=0;t<1000;t+=10)trace.Observe(pad,true,t);
    CHECK(trace.channels[1].rises==1 && trace.channels[1].falls==0);
    pad.rightTrigger=0;trace.Observe(pad,true,1000);
    pad.rightTrigger=255;trace.Observe(pad,true,1020);
    CHECK(trace.channels[1].shortGaps==1 && trace.channels[1].rises==2);
    pad.rightTrigger=0;trace.Observe(pad,false,1030);
    pad.rightTrigger=255;trace.Observe(pad,true,1300);
    CHECK(trace.channels[1].shortGaps==1 && trace.absentHands==1 && trace.maxGap==270);
    printf("Gesture input: %d failures\n",failures);return failures?1:0;
}
