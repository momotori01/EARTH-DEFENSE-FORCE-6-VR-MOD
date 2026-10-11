#include <map>
#include <memory>
#include <string>
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
    // The turning stick's up/down buttons (stick_buttons.h): pressed only within
    // 40 degrees of straight up or 37 of straight down, held until the stick comes back.
    {
        const float edge=0.6f,angle=40.0f,down=37.0f;
        auto at=[](float degreesFromUp,float push,float& x,float& y) {
            x=push*std::sin(degreesFromUp*0.01745329f); y=push*std::cos(degreesFromUp*0.01745329f);
        };
        float x=0,y=0;
        edf6vr::StickButtons s{};
        at(0,1,x,y);   edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.up && !s.down);
        s={}; at(180,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.up && s.down);
        // A full turn pushed 45 or 50 degrees off vertical pressed Y before (y 0.71, 0.64 > 0.6): not now.
        for(float off:{45.0f,50.0f,90.0f,130.0f,135.0f}) {
            s={}; at(off,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.up && !s.down);
            s={}; at(-off,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.up && !s.down);
        }
        // Inside the cone, both sides, and not before the edge.
        s={}; at(35,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.up);
        s={}; at(-35,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.up);
        s={}; at(145,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.down);
        s={}; at(180-36,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.down);
        s={}; at(180+36,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.down);
        s={}; at(180-38,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.down);   // down: 37 degrees
        s={}; at(180+38,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.down);
        s={}; at(38,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.up);           // up keeps 40
        s={}; at(0,0.55f,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.up);
        // Held: rolling over into a turn keeps it until the stick is back under 0.45.
        s={}; at(0,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.up);
        at(60,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(s.up);       // y 0.5
        at(70,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.up);      // y 0.34
        at(60,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.up);      // not pressed again from the side
        // Straight through the middle: up lets go, down presses.
        s={}; at(0,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down);
        at(180,1,x,y); edf6vr::UpdateStickButtons(s,x,y,edge,angle,down); CHECK(!s.up && s.down);
        // A stick that is not a number presses nothing.
        edf6vr::UpdateStickButtons(s,std::nanf(""),1,edge,angle,down); CHECK(!s.up && !s.down);
    }
    {
        // The Start menu's "VR setting" rows (vr_menu_items.h): the state the INI
        // is in, the state a press goes to, and what the row shows.
        using namespace edf6vr;
        static_assert(kVrMenuItemCount==19,"the rows of the VR setting menu");
        const auto& scope=kVrMenuItems[0];
        CHECK(VrMenuStateOf(scope,L"1",nullptr)==0 && VrMenuNext(scope,0,L"1")==1);                          // Native -> Digital
        CHECK(!wcscmp(scope.states[VrMenuNext(scope,VrMenuStateOf(scope,L"2",nullptr),L"2")].value,L"0"));   // Digital -> Off
        CHECK(!wcscmp(scope.states[VrMenuNext(scope,VrMenuStateOf(scope,L"0",nullptr),L"0")].value,L"1"));   // Off -> Native
        const auto& strike=kVrMenuItems[1];
        CHECK(!wcscmp(strike.key,L"AirStrikeCamera"));
        CHECK(VrMenuStateOf(strike,L"0",nullptr)==0 && VrMenuNext(strike,0,L"0")==1 && VrMenuNext(strike,1,L"1")==0);
        CHECK(VrMenuStateOf(strike,L"2",nullptr)==2 && VrMenuNext(strike,2,L"2")==0);   // the hand-set level horizon: shown, a press leaves it
        CHECK(!wcscmp(VrMenuLabel(strike,0,true,L"0"),L"2D表示（軽い）") && !wcscmp(VrMenuLabel(strike,1,false,L"1"),L"VR (lower FPS)"));
        const auto& stick=kVrMenuItems[2];
        CHECK(VrMenuStateOf(stick,L"1",nullptr)==0 && VrMenuStateOf(stick,L"0",nullptr)==1 && VrMenuStateOf(stick,L"7",nullptr)==0);
        CHECK(!wcscmp(stick.states[VrMenuNext(stick,0,L"1")].value,L"0") && !wcscmp(stick.states[VrMenuNext(stick,1,L"0")].value,L"1"));
        const auto& buzz=kVrMenuItems[4];
        CHECK(VrMenuStateOf(buzz,L"0.5",nullptr)==2 && VrMenuStateOf(buzz,L"0.5000",nullptr)==2 && VrMenuStateOf(buzz,L"0.6",nullptr)==-1);
        CHECK(VrMenuNext(buzz,-1,L"0.6")==3);                                   // between two steps: the step above
        CHECK(VrMenuNext(buzz,4,L"1")==0);                                      // and round again
        CHECK(VrMenuNext(buzz,-1,L"3")==0 && VrMenuNext(buzz,-1,L"abc")==0);
        CHECK(!wcscmp(VrMenuLabel(buzz,-1,false,L"0.6"),L"0.6") && !wcscmp(VrMenuLabel(buzz,1,true,L"0.25"),L"0.25"));
        const auto& hud=kVrMenuItems[6];
        CHECK(hud.key2 && !wcscmp(hud.key2,L"UiClusterPlace"));
        CHECK(VrMenuStateOf(hud,L"1",L"0")==0 && VrMenuStateOf(hud,L"1",L"2")==2 && VrMenuStateOf(hud,L"0",L"2")==3);
        CHECK(VrMenuNext(hud,2,L"1")==3 && hud.states[3].value2==nullptr);      // off leaves the place as it is
        CHECK(VrMenuNext(hud,3,L"0")==0);
        for(int i=0;i<kVrMenuItemCount;++i) {
            const auto& item=kVrMenuItems[i];
            CHECK(item.cycle>=2 && item.cycle<=item.count && item.count<=8);
            CHECK(VrMenuStateOf(item,item.fallback,item.fallback2)>=0);         // the default is one of its states
            for(int s=0;s<item.count;++s) {
                CHECK(VrMenuStateOf(item,item.states[s].value,item.states[s].value2?item.states[s].value2:L"0")==s);
                CHECK(VrMenuLabel(item,s,false,L"")[0] && VrMenuLabel(item,s,true,L"")[0]);
            }
        }
        // The callback's arguments as the game hands them over: a std::map of wide strings.
        std::map<std::wstring,std::wstring> arguments{{L"item",L"17"},{L"other",L"x"},{L"a_long_key_name_over_seven",L"3"}};
        const auto* map=reinterpret_cast<const unsigned char*>(&arguments);
        int item=-1;
        CHECK(VrMenuArgument(map,L"item",item) && item==17);
        CHECK(VrMenuArgument(map,L"a_long_key_name_over_seven",item) && item==3);
        CHECK(!VrMenuArgument(map,L"none",item));
        CHECK(!VrMenuArgument(map,L"other",item));                              // not a number
        std::map<std::wstring,std::wstring> none;
        CHECK(!VrMenuArgument(reinterpret_cast<const unsigned char*>(&none),L"item",item));
        VrMenuString name; VrMenuName(name,L"VrVal",7);
        CHECK(!wcscmp(name.text,L"VrVal07") && name.size==7 && name.capacity==7);
        static_assert(sizeof(VrMenuString)==sizeof(std::wstring) && sizeof(VrMenuShared)==sizeof(std::shared_ptr<int>),"the game's own shapes");
    }
    printf("Gesture input: %d failures\n",failures);return failures?1:0;
}
