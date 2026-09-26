// The armed pickups (V610 machine gun, V611 rockets), their cab (the user,
// 2026-09-25: the trucks' own interiors where they have one, and where the
// camera would see the back of the model, parts made to fit and coloured to
// match). These two have none: a double cab with nothing inside and the
// windows painted on. So a car's cab is built into it on the helicopter kit:
// the shell `margin` in from the body's skin, the windows cut where the
// albedo paints them (research/cockpit/truck_windows.py), each in a plain
// rubber surround. The driver sits on the right (the Japanese side), the eye
// at model (-.45,1.75,.05), so the body's centre line is cabin x -.45 (cx).
// Painted metal (the shell) in the body's white; dashboard, consoles and trim
// dark; seats, door cards and visors in a worn brown-grey cloth (Navy takes
// the shell's accent in this cabin). Health and ammunition in the binnacle
// behind the wheel, the radar in the middle of the dashboard, let in.
namespace pickup {
float CentreX() {const auto* c=hk::Find(CockpitKind::TruckPickup);return c?c->centreX:-.45f;}
// The binnacle's face, turned to the eye; the two gauges are let into it.
Panel Binnacle() {return FacingPilot({0,-.195f,.575f},.440f,.170f,0);}
// The centre stack's face, turned to the eye, standing out of the dashboard
// (its edge toward the driver 2 cm clear of the dashboard's face).
Panel Stack(float cx) {return FacingPilot({cx,-.320f,.565f},.270f,.270f,0);}
// The dashboard's section (z, y), from the front of its top over its rear
// edge, down its face and under it to the bulkhead. Its top meets the
// windscreen's foot in the middle (y -.06; the glass's corners run down to
// -.13 beside it, under the top), and falls away under the cowl ahead.
constexpr float kDashTop=-.080f;
constexpr float kDash[][2]={{.995f,-.110f},{.950f,kDashTop},{.705f,kDashTop},{.672f,-.095f},{.650f,-.130f},{.620f,-.460f},
    {.640f,-.500f},{.800f,-.600f},{.995f,-.620f}};
// Where the dashboard's face stands at height y (between its rear edge and its foot).
float DashFace(float y) {return .650f+(y+.130f)*((.620f-.650f)/(-.460f+.130f));}
// A plain housing on a panel, square-cornered and eased (a car's, not the
// Nix's chamfered octagon): its face 1.5 cm toward the eye, `thick` behind.
void Housing(std::vector<CockpitVertex>& v,const Panel& p,float w,float h,float thick,unsigned material,float edge=.006f) {
    RoundedPrism(v,{Point(p,-w/2,-h/2,.015f),Point(p,w/2,-h/2,.015f),Point(p,w/2,h/2,.015f),Point(p,-w/2,h/2,.015f)},
        Sub(Point(p,0,0,-thick),Point(p,0,0,.015f)),edge,material);
}
}
std::array<Panel,3> TruckPanels() {
    using namespace pickup;
    const auto b=Binnacle();const auto s=Stack(CentreX());
    // The screens in their housings' planes, 1.6 cm toward the eye (each
    // housing's face stands 1.5 cm out of its panel; the glass 4 mm on, 5 mm
    // proud of the housing, 3 mm in front of its bezel).
    auto in=[](const Panel& p,float x,float y,float w,float h,unsigned source){return Panel{Point(p,x,y,.016f),p.right,p.up,w,h,source};};
    return {in(s,0,0,.215f,.215f,0),in(b,-.100f,.005f,.160f,.085f,1),in(b,.100f,.005f,.160f,.085f,2)};
}
std::vector<CockpitVertex> TruckInterior() {
    using namespace pickup;
    std::vector<CockpitVertex> v;v.reserve(300000);
    const auto* c=hk::Find(CockpitKind::TruckPickup);if(!c)return v;
    auto spec=hk::Cabin(*c);const float floorY=spec.floorY,cx=spec.centreX;
    std::vector<std::vector<P>> windows;
    for(unsigned i=0;i<c->windowCount;++i)windows.push_back(hk::Loop(c->windows[i]));
    std::vector<CombatRegion> holes;
    for(const auto& w:windows)hk::Prisms(w,spec.cut,holes,spec.shallowEdges);
    hk::Shell(v,*c,spec,holes);
    // The windscreen wraps back at its sides, away from its mean plane, where
    // its frame stands: a wider, deeper surround reaches the pillars' shell.
    for(std::size_t i=0;i<windows.size();++i) {
        auto framed=spec;if(i==0){framed.frame=.022f;framed.framePast=.035f;}
        hk::Frame(v,windows[i],framed,&windows,.0012f*float(i),false);
    }
    auto wall=[&](float z,float y){return hk::WallX(*c,spec,z,y);};
    auto roof=[&](float z){return hk::Top(*c,z)-spec.margin;};
    // A part from wall to wall: along a section (z, y) of it, each point
    // reaching the shell's wall on both sides less `inset`, every strip
    // turned away from `inside` (a point within the section).
    auto loft=[&](std::initializer_list<std::array<float,2>> section,std::array<float,2> inside,unsigned material,float inset=.002f) {
        const std::vector<std::array<float,2>> s(section);
        for(std::size_t k=0;k+1<s.size();++k) {
            const float z0=s[k][0],y0=s[k][1],z1=s[k+1][0],y1=s[k+1][1];
            const float w0=wall(z0,y0)-inset,w1=wall(z1,y1)-inset;
            const P away{0,(y0+y1)*.5f-inside[1],(z0+z1)*.5f-inside[0]};
            hk::Facing(v,{cx-w0,y0,z0},{cx+w0,y0,z0},{cx+w1,y1,z1},{cx-w1,y1,z1},away,material);
        }
    };
    // The floor: a rubber mat over the deck, wall to wall.
    loft({{-1.030f,floorY+.004f},{.990f,floorY+.004f}},{0,floorY-1},Rubber);
    // The transmission tunnel down the middle, from under the rear seat to the bulkhead.
    const float tunnelTop=floorY+.140f;
    RoundedPrism(v,{{cx-.170f,floorY,-.560f},{cx+.170f,floorY,-.560f},{cx+.110f,tunnelTop,-.560f},{cx-.110f,tunnelTop,-.560f}},
        {0,0,.550f+.995f},.020f,Dark);
    // The dashboard, wall to wall under the windscreen.
    for(std::size_t k=0;k+1<std::size(kDash);++k)
        loft({{kDash[k][0],kDash[k][1]},{kDash[k+1][0],kDash[k+1][1]}},{.850f,-.380f},Dark);
    // The binnacle behind the wheel: a housing let into the face, the
    // dashboard's top carried back over it as a hood.
    {
        const auto b=Binnacle();
        Housing(v,b,.440f,.170f,.200f,Dark,.010f);
        RoundedPrism(v,{{-.235f,kDashTop-.024f,.470f},{.235f,kDashTop-.024f,.470f},{.235f,kDashTop-.024f,.720f},{-.235f,kDashTop-.024f,.720f}},
            {0,.024f,0},.008f,Dark);
        // Each gauge on a bezel plate a little larger than it, 2 mm proud
        // of the housing, just behind the glass.
        for(const auto& p:{TruckPanels()[1],TruckPanels()[2]}) {
            auto bezel=p;bezel.c=Point(p,0,0,-.014f);
            Housing(v,bezel,p.w+.018f,p.h+.018f,.006f,Rubber,.003f);
        }
    }
    // The centre stack: the radar's housing let into the dashboard, and a
    // console under it down to the tunnel, the gear lever behind that.
    {
        const auto s=Stack(cx);
        Housing(v,s,.270f,.270f,.240f,Dark,.010f);
        const auto radar=TruckPanels()[0];auto bezel=radar;bezel.c=Point(radar,0,0,-.014f);
        Housing(v,bezel,radar.w+.018f,radar.h+.018f,.006f,Rubber,.003f);
        Box(v,{cx-.100f,tunnelTop-.020f,.580f},{cx+.100f,-.440f,.800f},Dark);
        // Vents: either side of the stack and at each end of the dashboard.
        for(const float x:{cx-.215f,cx+.215f,2*cx-.300f,.300f}) {
            const float y=-.235f;
            const Panel vent{{x,y,DashFace(y)-.002f},{1,0,0},Unit(P{0,1,.096f}),0,0,0};
            Housing(v,vent,.110f,.050f,.010f,Vent,.004f);
        }
        // The gear lever: its boot on the tunnel, a rod leaning back, a knob.
        const P boot{cx+.050f,tunnelTop,.300f};
        const Panel foot{Add(boot,{0,.004f,0}),{1,0,0},{0,0,1},0,0,0};
        GripBody(v,foot,.100f,.100f,.020f,Rubber,.012f);
        const P knob{cx+.045f,-.560f,.240f};
        Rod(v,Add(boot,{0,.010f,0}),knob,.009f,Steel);
        Rod(v,Sub(knob,{0,.030f,0}),Add(knob,{0,.030f,0}),.024f,Dark);
        // The hand brake on the tunnel between the front seats.
        const P pivot{cx,tunnelTop+.020f,-.020f},grip{cx,tunnelTop+.130f,-.200f};
        Box(v,{cx-.030f,tunnelTop-.010f,-.080f},{cx+.030f,tunnelTop+.030f,.040f},Dark);
        Rod(v,pivot,grip,.012f,Steel);
        const auto along=Unit(Sub(grip,pivot));
        Rod(v,Sub(grip,Mul(along,.090f)),Add(grip,Mul(along,.010f)),.019f,Rubber);
    }
    // The glove box on the passenger's side.
    {
        const float y=-.330f;
        const Panel lid{{cx*2.f-.020f,y,DashFace(y)-.004f},{1,0,0},Unit(P{0,1,.096f}),0,0,0};
        Housing(v,lid,.360f,.160f,.010f,Dark,.006f);
        auto latch=lid;latch.c=Point(lid,0,.050f,.012f);
        Housing(v,latch,.070f,.020f,.010f,Steel,.004f);
    }
    // The steering wheel: its rim tilted to the driver, three spokes, a horn
    // pad on the hub and the column's shroud into the dashboard under the
    // binnacle, a stalk each side.
    {
        const P centre{0,-.370f,.330f},axis=Unit(P{0,.42f,-.91f}),up=Unit(Cross(P{1,0,0},axis));
        const P side=Cross(up,axis);
        const float r=.180f;
        std::vector<P> rim;
        for(int k=0;k<=36;++k){const float a=float(k)*6.2831853f/36;rim.push_back(Add(centre,Add(Mul(side,r*std::cos(a)),Mul(up,r*std::sin(a)))));}
        hk::Tube(v,rim,.016f,Rubber);
        for(const float a:{0.f,3.1415927f,-1.5707963f}) {
            const P at=Add(centre,Add(Mul(side,(r-.010f)*std::cos(a)),Mul(up,(r-.010f)*std::sin(a))));
            Beam(v,Add(centre,Mul(Sub(at,centre),.20f)),at,.012f,Dark);
        }
        const auto back=Mul(axis,-1.f);
        Rod(v,Add(centre,Mul(back,-.012f)),Add(centre,Mul(back,.060f)),.052f,Dark);
        const Panel pad{Add(centre,Mul(axis,.014f)),Mul(side,-1.f),up,0,0,0};
        GripBody(v,pad,.100f,.080f,.012f,Rubber,.010f);
        const P shroud0=Add(centre,Mul(back,.050f)),shroud1=Add(centre,Mul(back,.360f));
        Rod(v,shroud0,shroud1,.042f,Dark);
        for(const float s:{-1.f,1.f}) {
            const P base=Add(centre,Mul(back,.110f));
            Rod(v,Add(base,Mul(side,s*.030f)),Add(Add(base,Mul(side,s*.150f)),Mul(up,.020f)),.007f,Dark);
        }
    }
    // Pedals hanging from under the dashboard: clutch, brake, accelerator.
    for(const float x:{-.200f,-.040f,.140f}) {
        const bool gas=x>.1f;
        const P top{x,-.600f,.860f},pad{x,gas?-.800f:-.760f,gas?.790f:.730f};
        Rod(v,top,pad,.008f,Steel);
        const Panel face{pad,{1,0,0},Unit(P{0,.94f,.34f}),0,0,0};
        GripBody(v,face,gas?.060f:.080f,gas?.140f:.070f,.012f,Rubber,.006f);
    }
    // The front seats: cloth cushion and back on a dark base, a headrest on
    // two posts. Built at the driver's (x 0), copied to the passenger's (2 cx).
    for(const float x:{0.f,2*cx}) {
        const auto start=v.size();
        RoundedPrism(v,{{-.240f,-.740f,-.280f},{-.240f,-.740f,.200f},{-.240f,-.660f,.210f},{-.240f,-.630f,.180f},{-.240f,-.670f,-.260f}},
            {.480f,0,0},.030f,Navy);
        Box(v,{-.190f,floorY,-.240f},{.190f,-.735f,.120f},Dark);
        RoundedPrism(v,{{-.250f,-.700f,-.400f},{-.250f,-.680f,-.270f},{-.250f,.020f,-.360f},{-.250f,.000f,-.470f}},
            {.500f,0,0},.030f,Navy);
        RoundedPrism(v,{{-.130f,.050f,-.330f},{-.130f,.240f,-.300f},{-.130f,.250f,-.400f},{-.130f,.060f,-.430f}},
            {.260f,0,0},.025f,Navy);
        for(const float s:{-.070f,.070f})Rod(v,{s,-.020f,-.415f},{s,.070f,-.375f},.007f,Steel);
        if(x!=0)MovePart(v,start,{1,1,1},{x,0,0});
    }
    // The rear bench, wall to wall across the back of the cab.
    loft({{-.560f,floorY},{-.540f,-.700f}},{-.800f,-.800f},Dark);
    loft({{-.540f,-.700f},{-.560f,-.635f},{-.980f,-.650f}},{-.800f,-.800f},Navy);
    loft({{-.975f,-.650f},{-.930f,-.220f},{-.950f,-.175f},{-1.035f,-.175f}},{-1.000f,-.400f},Navy);
    // The doors: a cloth card, an armrest, a handle and a window crank on
    // each, the front doors from -.20 to .58, the rear from -.80 to -.40.
    for(const float side:{-1.f,1.f})for(const auto& door:{std::array<float,2>{-.180f,.580f},std::array<float,2>{-.790f,-.410f}}) {
        const float z0=door[0],z1=door[1];
        auto at=[&](float z,float y,float in){return P{cx+side*(wall(z,y)-in),y,z};};
        // The card: a thin cloth panel on the wall under the sill.
        {
            const float y0=-.620f,y1=-.440f,zm=(z0+z1)*.5f;
            const float in=wall(zm,-.53f);
            RoundedPrism(v,{{cx+side*(in+.004f),y0,z0+.050f},{cx+side*(in+.004f),y0,z1-.050f},{cx+side*(in+.004f),y1,z1-.050f},{cx+side*(in+.004f),y1,z0+.050f}},
                {-side*.012f,0,0},.004f,Navy);
        }
        // The armrest: a dark bar along the door, into the wall.
        {
            const float y0=-.420f,y1=-.360f,in=wall((z0+z1)*.5f,-.39f);
            RoundedPrism(v,{{cx+side*(in+.006f),y0,z0+.060f},{cx+side*(in-.070f),y0,z0+.060f},{cx+side*(in-.070f),y1,z0+.060f},{cx+side*(in+.006f),y1,z0+.060f}},
                {0,0,z1-z0-.120f},.012f,Dark);
        }
        // The handle, forward on the door, and the crank behind it.
        {
            const float y=-.260f,za=z1-.160f,zb=z1-.080f;
            const P a=at(za,y,.026f),b=at(zb,y,.026f);
            Beam(v,a,b,.007f,Steel);
            Beam(v,at(za,y,-.004f),a,.006f,Steel);Beam(v,at(zb,y,-.004f),b,.006f,Steel);
            const float zc=z0+.180f,yc=-.300f;
            const P boss=at(zc,yc,.020f);
            Rod(v,at(zc,yc,-.004f),boss,.022f,Dark);
            const P arm=Add(boss,{0,.070f,-.030f});
            Beam(v,boss,arm,.006f,Steel);
            Rod(v,arm,Add(arm,{-side*.035f,0,0}),.011f,Rubber);
        }
    }
    // The windscreen's header. The glass is arched across its width (its top
    // edge 13 cm further forward in the middle than at the corners), and the
    // kit's cut, straight across its mean plane, leaves the roof over it
    // ragged; a headliner strip covers it, from the roof's section at z .30
    // (1 cm in from the shell) down to 1 cm inside the glass's top edge,
    // where the frame's rubber takes over.
    {
        const auto& glass=windows.front();   // the windscreen: the furthest forward
        std::vector<P> edge;
        for(const auto& p:glass)if(p[1]>.270f)edge.push_back(p);
        std::sort(edge.begin(),edge.end(),[](const P& a,const P& b){return a[0]<b[0];});
        const float zr=.300f;const auto sec=hk::Cut(*c,spec,zr,40);
        // The shell's height at (x, zr): the flat roof, or where its wall is that far out.
        auto height=[&](float x) {
            const float out=std::fabs(x-cx);
            if(out<=sec.x.back())return sec.y.back();
            for(std::size_t j=sec.y.size()-1;j>0;--j)if(sec.x[j-1]>=out) {
                const float t=(out-sec.x[j])/std::max(1e-5f,sec.x[j-1]-sec.x[j]);
                return sec.y[j]+(sec.y[j-1]-sec.y[j])*t;
            }
            return sec.y.front();
        };
        for(std::size_t k=0;k+1<edge.size();++k) {
            const P f0=Add(edge[k],{0,-.012f,-.006f}),f1=Add(edge[k+1],{0,-.012f,-.006f});
            const P r0{edge[k][0],height(edge[k][0])-.010f,zr},r1{edge[k+1][0],height(edge[k+1][0])-.010f,zr};
            hk::Facing(v,f0,f1,r1,r0,{0,-1,0},Hull);
        }
    }
    // The roof: a dome light in the middle, a sun visor folded up over each
    // front seat behind the header, the mirror on its stem at the top of the
    // windscreen.
    {
        const float z=-.350f,y=roof(z);
        Box(v,{cx-.080f,y-.020f,z-.040f},{cx+.080f,y+.004f,z+.040f},Bezel);
        Box(v,{cx-.060f,y-.024f,z-.028f},{cx+.060f,y-.018f,z+.028f},Glass);
        for(const float x:{-.100f,2*cx+.100f}) {
            const float za=.060f,zb=.270f;
            const float top=std::min(hk::Cut(*c,spec,za,40).x.back(),hk::Cut(*c,spec,zb,40).x.back());
            const float half=std::min(.140f,top-.020f-std::fabs(x-cx));
            if(half<.08f)continue;
            RoundedPrism(v,{{x-half,roof(za)+.003f,za},{x-half,roof(zb)+.003f,zb},{x-half,roof(zb)-.015f,zb},{x-half,roof(za)-.015f,za}},
                {2*half,0,0},.006f,Navy);
            Rod(v,{x-half+.010f,roof(zb)-.012f,zb-.010f},{x+half-.010f,roof(zb)-.012f,zb-.010f},.006f,Dark);
        }
        const float zm=.340f;
        Rod(v,{cx,roof(zm)+.004f,zm},{cx,.240f,zm},.008f,Dark);
        const Panel mirror{{cx,.225f,zm+.012f},{1,0,0},{0,1,0},0,0,0};
        Housing(v,mirror,.240f,.070f,.030f,Dark,.012f);
        auto glass=mirror;glass.c=Point(mirror,0,0,.017f);
        Housing(v,glass,.215f,.050f,.004f,Steel,.002f);
    }
    // The windows' glass last: the translucent pass.
    for(std::size_t i=0;i<windows.size();++i)hk::Pane(v,windows[i],.0012f*float(i));
    return v;
}
