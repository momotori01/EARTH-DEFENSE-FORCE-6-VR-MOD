// Proteus MK2 tandem cockpit, inside the machine's upper body: the driver in
// front on the centre stick, the missile operator a step higher behind them on
// a pair of levers. One cabin, two stations, and the same walk-through hatch at
// the back that the gun pods have.
//
// Cabin coordinates are the usual +X pilot right, +Y up, +Z forward, with the
// origin at the DRIVER's eye reference. The body was measured in the neck
// bone's own frame (x=up, y=forward, z=right) by
// research/cockpit/survey_proteus_body.py; the cabin sits well inside it.
//
// There is NO frame in here. The structure that carries the screens is outside
// them, where the machine's own shell is, so the cabin is furniture on a deck:
// two seats, a dashboard, consoles, the equipment bank, the gangway and the
// hatch. Sides, roof and nose are screen, divided by straight seams; the deck
// stops short of the nose so the legs and missiles are in view below.
//
// The crew route is fixed: in through the hatch, forward along the RIGHT side,
// then into either seat. The right side stays a gangway with a handrail and the
// left wall carries the equipment bank.
namespace {
// The rear station is this far up and back from the driver's, so the missile
// operator's own eye is kCockpitSeatedEye + (0,kCabinRearUp,kCabinRearBack).
constexpr float kCabinRearUp=.56f,kCabinRearBack=-1.18f;
constexpr float kCabinFloor=-1.22f;    // driver's deck
constexpr float kCabinStep=-.66f;      // rear deck, one step up
constexpr float kCabinRiser=-.62f;     // where one becomes the other
constexpr float kCabinTail=-1.92f;     // rear bulkhead face
constexpr float kCabinNose=1.42f;      // where the nose screen ends
constexpr float kCabinChin=1.16f;      // the deck stops here; ahead is screen
constexpr float kCabinWalk=.46f;       // inboard edge of the right-hand gangway
// Screen lines. Narrower than the body: the shell and its structure are
// outside the screens, so the cabin is the room inside them.
float CabinHalf(float z) {return z<.20f?.860f:std::max(.560f,.860f-.250f*(z-.20f));}
float CabinRoof(float z) {return z<-.20f?1.040f:std::max(.560f,1.040f-.260f*(z+.20f));}
float CabinDeck(float z) {return z<kCabinRiser?kCabinStep:kCabinFloor;}
const P kCabinRearEye{0,.10f+kCabinRearUp,-.10f+kCabinRearBack};
Panel FacingSeat(P eye,P centre,float w,float h,unsigned source) {
    const auto out=Unit(Sub(centre,eye));
    const auto right=Unit(Cross(P{0,1,0},out));
    return {centre,right,Cross(out,right),w,h,source};
}
// Three joined screens for each station: the centre square to its pilot, the
// two wings sharing its edges and bent toward the pilot so they read squarely.
// Driver: on the brow of the console. Operator: hung from the ceiling arm.
constexpr float kCabinWingBend=.5235988f;   // 30 degrees
Panel CabinCentre(bool rear) {
    // The operator's set hangs high, just over the line of sight, under the roof.
    // The driver's centre screen is the console block's own face, leaning
    // forward 23 degrees, which at that height is within 6 degrees of square
    // to the eye.
    // The operator's set sits just under the roof line, tilted 30 degrees
    // down, read with a glance up: 11 degrees off square to the eye.
    return rear?Panel{{0,.91f,-.50f},{1,0,0},{0,.8660f,-.5f},.30f,.22f,0}:Panel{{0,-.17f,.74f},{1,0,0},{0,.9186f,.3931f},.30f,.28f,0};
}
Panel CabinWing(bool rear,float s) {
    const auto c=CabinCentre(rear);
    const auto toward=Mul(Unit(Cross(c.right,c.up)),-1.f);   // out of the screen, at the pilot
    const P right=Add(Mul(c.right,std::cos(kCabinWingBend)),Mul(toward,s*std::sin(kCabinWingBend)));
    const float w=.24f,h=rear?.18f:.20f;
    const auto centre=Add(Add(c.c,Mul(c.right,s*c.w*.5f)),Mul(right,s*w*.5f));
    return {centre,right,c.up,w,h,s<0?1u:2u};
}
const Panel proteusCabinPanels[]={
 CabinCentre(false),CabinWing(false,-1),CabinWing(false,1),
 CabinCentre(true),CabinWing(true,-1),CabinWing(true,1),
};
// The joined cluster: each screen in its own block, the shared edges covered
// by a hinge barrel, so the three read as one folded instrument. Embedded,
// the blocks are deep and wide enough to be the console's own brow.
void CabinCluster(std::vector<CockpitVertex>& v,const Panel* three,bool embedded) {
    for(unsigned k=0;k<3;++k) {
        const auto& p=three[k];
        Instrument(v,p);
        auto box=p;box.c=Point(p,0,0,-.030f);
        // Embedded: the centre screen sits flush in the block's face; each
        // wing has a deep housing that runs back into the block behind it.
        // Hung in the air, the three are bare frames: see-through, joined only
        // by their hinge barrels.
        if(embedded&&k)GripBody(v,box,p.w+.120f,p.h+.124f,.150f,Dark,.012f);
    }
    const auto& c=three[0];
    for(float s:{-1.f,1.f}) {
        const auto top=Point(c,s*c.w*.5f,c.h*.5f+.055f,-.020f),bottom=Point(c,s*c.w*.5f,-c.h*.5f-.055f,-.020f);
        const auto along=Unit(Sub(top,bottom));
        Rod(v,top,bottom,.030f,Steel);
        for(const auto& e:{top,bottom})Rod(v,Sub(e,Mul(along,.008f)),Add(e,Mul(along,.008f)),.034f,Dark);
        FrameBolt(v,Add(top,Mul(along,.011f)),along,.006f);
        FrameBolt(v,Sub(bottom,Mul(along,.011f)),Mul(along,-1.f),.006f);
    }
}
// The shared seat at its own eighteen degrees, the angle Nix and the Crawler
// sit at, on a bolted plinth. Side rails bolt to the seat base.
void CabinSeat(std::vector<CockpitVertex>& v,float deck,P move) {
    const auto seat=v.size();
    Seat(v,true);
    MovePart(v,seat,{1,1,1},move);
    const float yb=-.81f+move[1];                    // underside of the seat base
    for(float s:{-1.f,1.f}) {
        const float x=s*.262f+move[0];
        const float back=-.28f+move[2],front=.18f+move[2];
        RoundedPrism(v,{{x-.020f,yb+.100f,back},{x-.020f,yb+.100f,front},
            {x-.020f,yb+.030f,front},{x-.020f,yb+.030f,back}},{.040f,0,0},.006f,Dark);
        for(float z:{back+.08f,back+.23f,back+.38f})
            FrameBolt(v,{x+s*.021f,yb+.065f,z},{s,0,0},.005f);
    }
    // The rails close round the base's front: a cross rail let 1 cm into the
    // base's front face, and a bolted plate over each corner joint.
    RoundedPrism(v,{{move[0]-.282f,yb+.100f,.17f+move[2]},{move[0]+.282f,yb+.100f,.17f+move[2]},
        {move[0]+.282f,yb+.100f,.21f+move[2]},{move[0]-.282f,yb+.100f,.21f+move[2]}},{0,-.070f,0},.006f,Dark);
    for(float s:{-1.f,1.f}) {
        Panel corner{{move[0]+s*.283f,yb+.065f,.19f+move[2]},{0,0,s},{0,1,0},0,0,0};
        GripBody(v,corner,.070f,.080f,.010f,Dark,.004f);
        for(float u:{-.025f,.025f})FrameBolt(v,Point(corner,0,u,.0165f),{s,0,0},.004f);
    }
    const float z0=move[2]-.28f,z1=move[2]+.18f;
    RoundedPrism(v,{{move[0]-.145f,deck+.006f,z0},{move[0]+.145f,deck+.006f,z0},
        {move[0]+.145f,deck+.006f,z1},{move[0]-.145f,deck+.006f,z1}},{0,yb+.012f-deck-.006f,0},.012f,Dark);
    Panel flange{{move[0],deck,(z0+z1)*.5f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,flange,.380f,.560f,.020f,Steel,.006f);
    // GripBody plates are octagons (corners cut 18% of the short side): the
    // bolts sit inside that chamfer, or they hang over the deck.
    for(float t:{-.150f,.150f})for(float u:{-.200f,.200f})FrameBolt(v,Point(flange,t,u,.0165f),{0,1,0},.005f);
    Panel cap{{move[0],yb+.004f,(z0+z1)*.5f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,cap,.360f,.480f,.018f,Bezel,.005f);
    for(float t:{-.130f,.130f})for(float u:{-.170f,.170f})FrameBolt(v,Point(cap,t,u,.0165f),{0,1,0},.004f);
}
// Foot rests: one strut out of the front of the seat base, a cross bar at its
// end, and a tread each side on a short stub. Nothing stands on the deck.
void CabinFootStrut(std::vector<CockpitVertex>& v,float deck,P move) {
    // One strut out of the seat base to an octagonal block. An axle runs out
    // of both sides of the block under the treads, and the treads pivot on it
    // when pressed. Four short diagonal slots mark the block's top.
    const P start{move[0],-.748f+move[1],.190f+move[2]};
    const P block{move[0],deck+.27f,.56f+move[2]};
    Beam(v,Add(start,{0,0,-.03f}),Add(block,{0,0,-.06f}),.028f,Dark);
    {
        const float x=block[0],y=block[1]+.05f,z=block[2],w=.085f,c=.032f;
        RoundedPrism(v,{{x-w+c,y,z-w},{x+w-c,y,z-w},{x+w,y,z-w+c},{x+w,y,z+w-c},
            {x+w-c,y,z+w},{x-w+c,y,z+w},{x-w,y,z+w-c},{x-w,y,z-w+c}},{0,-.100f,0},.006f,Dark);
        for(float sx:{-1.f,1.f})for(float sz:{-1.f,1.f})
            Beam(v,{x+sx*.018f,y-.002f,z+sz*.018f},{x+sx*.052f,y-.002f,z+sz*.052f},.004f,Rubber);
    }
    // The axle: through the block, out to the treads' far edges, on a collar
    // at each side of the block and capped at each end.
    const P axleC{block[0],deck+.25f,block[2]+.04f};
    Rod(v,Add(axleC,{-.275f,0,0}),Add(axleC,{.275f,0,0}),.014f,Steel);
    for(float s:{-1.f,1.f}) {
        Axle(v,Add(axleC,{s*.095f,0,0}),.030f,.022f,Dark);
        FrameBolt(v,Add(axleC,{s*.276f,0,0}),{s,0,0},.006f);
        Panel board{{move[0]+s*.1825f,deck+.28f,block[2]+.02f},{1,0,0},{0,.5446f,.8387f},0,0,0};
        GripBody(v,board,.185f,.250f,.026f,Steel,.005f);
        auto tread=board;tread.c=Point(board,0,0,.016f);
        GripBody(v,tread,.150f,.210f,.008f,Rubber,.003f);
        for(int k=0;k<5;++k)Beam(v,Point(tread,-.060f,-.066f+k*.033f,.017f),Point(tread,.060f,-.066f+k*.033f,.017f),.0032f,Bezel);
        Beam(v,Point(board,-.076f,-.124f,.020f),Point(board,.076f,-.124f,.020f),.012f,Dark);
        Beam(v,Point(board,0,.092f,.018f),Point(board,0,.142f,.036f),.010f,Rubber);
        // Bearing block under the tread, round the axle.
        auto bearing=board;bearing.c=Point(board,0,0,-.030f);
        GripBody(v,bearing,.070f,.060f,.030f,Dark,.004f);
    }
}
void CabinStick(std::vector<CockpitVertex>& v,float s,P plate) {
    // The traced grip's own frame (side, axis, forward), rolled about its
    // forward axis so the stick leans toward the centreline.
    const P axis{-s*.7071068f,.6644630f,-.2418448f},side{-s*.7071068f,-.6644630f,.2418448f},fwd{0,.3420201f,.9396926f};
    constexpr float scale=.243f;
    Panel pad{Sub(plate,Mul(axis,.045f)),side,fwd,0,0,0};
    GripBody(v,pad,.130f,.125f,.040f,Dark,.006f);
    for(float a:{-.045f,.045f})for(float b:{-.042f,.042f})FrameBolt(v,Point(pad,a,b,-.0405f),axis,.004f);
    StickPart(v,proteusStickBase,plate,side,axis,fwd,scale,proteusStickBaseMaterial,s>0);
    StickPart(v,proteusStickBoot,plate,side,axis,fwd,scale,proteusStickBootMaterial,s>0);
    StickPart(v,proteusStickCollar,plate,side,axis,fwd,scale,proteusStickCollarMaterial,s>0);
    StickPart(v,proteusStickCable,plate,side,axis,fwd,scale,proteusStickCableMaterial,s>0);
    StickPart(v,proteusStickBody,plate,side,axis,fwd,scale,proteusStickBodyMaterial,s>0);
    StickPart(v,proteusStickTrigger,plate,side,axis,fwd,scale,proteusStickTriggerMaterial,s>0);
    StickPart(v,proteusStickPaddle,plate,side,axis,fwd,scale,proteusStickPaddleMaterial,s>0);
    StickPart(v,proteusStickButtonA,plate,side,axis,fwd,scale,proteusStickButtonAMaterial,s>0);
    StickPart(v,proteusStickButtonB,plate,side,axis,fwd,scale,proteusStickButtonBMaterial,s>0);
    StickPart(v,proteusStickButtonC,plate,side,axis,fwd,scale,proteusStickButtonCMaterial,s>0);
    StickPart(v,proteusStickButtonD,plate,side,axis,fwd,scale,proteusStickButtonDMaterial,s>0);
    StickPart(v,proteusStickButtonE,plate,side,axis,fwd,scale,proteusStickButtonEMaterial,s>0);
    StickPart(v,proteusStickButtonF,plate,side,axis,fwd,scale,proteusStickButtonFMaterial,s>0);
    StickPart(v,proteusStickButtonG,plate,side,axis,fwd,scale,proteusStickButtonGMaterial,s>0);
}
// The top face of each rising arm is what both pilots look at most - the
// driver looks down on it, the operator sees it over the seat - so that is
// where the detail goes: a finned service cover over the stick's loom, a
// status lamp block up by the screens, and the lead from a port at the arm's
// foot into the cover's lower end.
void CabinArmFace(std::vector<CockpitVertex>& v,float s) {
    const P start{s*.52f,-.34f,.42f};             // the arm's top face at the armrest
    const P d{0,.4856f,.8742f};                   // up the arm
    // Panels sit 2 mm off the face: a part's base on the face itself is two
    // coplanar surfaces, and it flickers.
    auto on=[&](float along,float across){Panel q{Add(Add(start,Mul(d,along)),P{across,0,0}),{1,0,0},d,0,0,0};q.c=Point(q,0,0,.002f);return q;};
    // Narrower than the face, so the arm's own edge frames it as a recess.
    // The arm's top face runs into the block's face at about 34 cm up the
    // arm, so everything on it stops short of that.
    const auto cover=on(.15f,0);
    Part(v,referenceServicePlate,cover,.120f,.240f,.014f,Steel);
    for(float a:{-.048f,.048f})for(float b:{-.105f,.105f})FrameBolt(v,Point(cover,a,b,.012f),{0,.8742f,-.4856f},.004f);
    for(int k=0;k<5;++k)Beam(v,Point(cover,-.036f+k*.018f,-.070f,.022f),Point(cover,-.036f+k*.018f,.070f,.022f),.005f,Bezel);
    Beam(v,Point(cover,-.045f,-.100f,.022f),Point(cover,.045f,-.100f,.022f),.007f,Warning);
    const auto lamps=on(.30f,0);
    GripBody(v,lamps,.120f,.050f,.014f,Bezel,.003f);
    for(int k=0;k<4;++k){auto l=lamps;l.c=Point(lamps,-.039f+k*.026f,0,0);Face(v,l,.016f,.026f,k==0?Warning:k==1?Amber:Cyan,.020f);}
    Panel port{{s*.49f,-.338f,.395f},{1,0,0},{0,0,1},0,0,0};
    Part(v,referenceConnector,port,.050f,.045f,.020f,Steel);
    const P a=Point(port,0,.018f,.018f),b=Point(cover,-s*.030f,-.125f,.012f);
    Rod(v,a,b,.007f,Cable);
}
// The driver's console, as sketched. A block leaning forward, trapezoid in
// side view, with the three screens let into its face. It stands on ONE
// pylon that leans down and forward to the deck ahead of the pedals, on the
// same slope as the block's face, so face and pylon read as one line. Each
// armrest is a box with its rear top corner chamfered away from the seat,
// and it bends upward in one piece into the block's outer end; nothing in
// here stands vertical and nothing crosses a screen.
void CabinConsole(std::vector<CockpitVertex>& v,float deck=kCabinFloor) {
    // Block: side profile (y,z) rear-bottom, rear-top, front-top, front-bottom.
    RoundedPrism(v,{{.61f,-.31f,.68f},{.61f,-.03f,.80f},{.61f,-.03f,.96f},{.61f,-.31f,1.00f}},{-1.22f,0,0},.014f,Dark);
    Beam(v,{-.58f,-.024f,.81f},{.58f,-.024f,.81f},.012f,Bezel);
    // Pylon: tapered, leaning 25 degrees, its top buried in the block's
    // underside and its foot on the deck short of the deck's end.
    // On a raised deck the pylon is simply shorter, on the same lean.
    const float footY=deck==kCabinFloor?-1.21f:deck+.01f,lean=(-.29f-footY)/.92f;
    const float backZ=deck==kCabinFloor?1.06f:.70f+.36f*lean,frontZ=deck==kCabinFloor?1.14f:.86f+.28f*lean;
    RoundedPrism(v,{{.08f,-.29f,.70f},{.08f,-.29f,.86f},{.08f,footY,frontZ},{.08f,footY,backZ}},{-.16f,0,0},.012f,Dark);
    Panel foot{{0,deck,deck==kCabinFloor?1.10f:(backZ+frontZ)*.5f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,foot,.220f,.120f,.020f,Steel,.006f);
    for(float t:{-.085f,.085f})for(float u:{-.040f,.040f})FrameBolt(v,Point(foot,t,u,.0165f),{0,1,0},.005f);
    for(float s:{-1.f,1.f}) {
        const float xo=s*.60f;
        // Armrest: chamfered at its rear top corner, open-ended forward into the arm.
        std::vector<P> wing{{xo,-.49f,.42f},{xo,-.49f,-.06f},{xo,-.41f,-.06f},{xo,-.34f,.01f},{xo,-.34f,.42f}};
        std::vector<P> arm{{xo,-.49f,.42f},{xo,-.34f,.42f},{xo,-.14f,.78f},{xo,-.29f,.78f}};
        if(s<0){std::reverse(wing.begin(),wing.end());std::reverse(arm.begin(),arm.end());}
        RoundedPrism(v,wing,{-s*.16f,0,0},.010f,Dark);
        RoundedPrism(v,arm,{-s*.16f,0,0},.010f,Dark);
        Panel rest{{s*.52f,-.34f,.12f},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,rest,.120f,.220f,.010f,Rubber,.006f);
        // A lidded compartment let into the armrest's outer face, and a vent
        // on the arm above it where the loom from the block runs warm.
        Panel bin{{xo+s*.001f,-.415f,.16f},{0,0,s},{0,1,0},0,0,0};
        ProteusCase(v,bin,.160f,.090f,.050f,false);
        Panel vent{{xo+s*.001f,-.300f,.62f},{0,0,s},{0,1,0},0,0,0};
        Part(v,referenceVent,vent,.10f,.07f,.014f,Steel);
        // Stick on the armrest's front, leaning inward.
        const P axis{-s*.7071068f,.6644630f,-.2418448f};
        const P wingTop{s*.52f,-.34f,.33f};
        const auto plateAt=Add(wingTop,Mul(axis,.105f));
        Beam(v,Add(wingTop,{0,-.02f,0}),Sub(plateAt,Mul(axis,.05f)),.030f,Dark);
        CabinStick(v,s,plateAt);
        CabinArmFace(v,s);
    }
}
// Dressing on the console, every piece with a job and a fixing:
// - the screens' loom runs down the pylon's back into a junction box on the
//   deck, under two clips;
// - a cooling duct hangs under the block from the pylon out into each arm;
// - two lidded breaker boxes sit on the block's top, outboard of the hinges.
void CabinConsoleDetail(std::vector<CockpitVertex>& v,float deck=kCabinFloor) {
    const P t{0,-.9313f,.3641f},n{0,-.3641f,-.9313f};   // down the pylon's back, and out of it
    const float lowY=deck+.07f,lowZ=deck==kCabinFloor?1.021f:.70f+.36f*(-.29f-lowY)/.92f-.0155f;
    const P top{0,-.31f,.692f},bottom{0,lowY,lowZ};
    for(float x:{-.028f,0.f,.028f}) {
        const auto a=Add(Add(top,{x,0,0}),Mul(n,.022f)),b=Add(Add(bottom,{x,0,0}),Mul(n,.022f));
        Rod(v,a,b,.010f,Cable);
        Rod(v,b,{x,deck+.045f,lowZ-.036f},.010f,Cable);
    }
    for(float along:{.28f,.66f}) {
        const auto at=Add(top,Mul(Sub(bottom,top),along));
        Panel clip{Add(at,Mul(n,.026f)),{1,0,0},Mul(t,-1.f),0,0,0};
        GripBody(v,clip,.110f,.036f,.016f,Dark,.004f);
        for(float x:{-.045f,.045f})FrameBolt(v,Point(clip,x,0,.0155f),n,.004f);
    }
    Panel box{{0,deck,lowZ-.036f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,box,.120f,.090f,.050f,Dark,.006f);
    auto lid=box;lid.c=Point(box,0,0,.024f);
    GripBody(v,lid,.100f,.070f,.012f,Bezel,.004f);
    for(float x:{-.040f,.040f})for(float z:{-.025f,.025f})FrameBolt(v,Point(lid,x,z,.0155f),{0,1,0},.004f);
    for(float sgn:{-1.f,1.f}) {
        Panel duct{{sgn*.25f,-.355f,.72f},{1,0,0},{0,0,-1},0,0,0};
        GripBody(v,duct,.380f,.100f,.060f,Dark,.008f);
        for(float x:{.10f,.42f}) {
            Panel flange{{sgn*x,-.361f,.72f},{1,0,0},{0,0,-1},0,0,0};
            GripBody(v,flange,.022f,.124f,.080f,Steel,.004f);
            for(float z:{-.048f,.048f})FrameBolt(v,Point(flange,0,z,.0155f),{0,-1,0},.004f);
        }
        Panel breaker{{sgn*.36f,-.03f,.88f},{1,0,0},{0,0,1},0,0,0};
        ProteusCase(v,breaker,.200f,.110f,.050f,true);
    }
}
// The hood over the driver's seat back carries the operator's centre screen. It
// is bolted to the seat back's case and stays outside the driver's head room.
void CabinRearHood(std::vector<CockpitVertex>& v) {
    // The case ends below the headrest: its top used to run 7 cm into the
    // seat and out through the headrest's back.
    Panel back{{0,-.26f,-.42f},{1,0,0},{0,.9781f,.2079f},0,0,0};
    ProteusCase(v,back,.420f,.300f,.070f,true);
    auto duct=back;duct.c=Point(back,0,.09f,.030f);
    Part(v,referenceVent,duct,.16f,.09f,.018f,Steel);
    for(float s:{-1.f,1.f}) {
        auto bottle=back;bottle.c=Point(back,s*.290f,-.06f,.030f);
        GripBody(v,bottle,.100f,.170f,.100f,Bezel,.020f);
        Rod(v,Point(bottle,0,.085f,0),Point(bottle,0,.125f,0),.020f,Steel);
        for(float t:{-.060f,.060f})Beam(v,Point(bottle,-.056f,t,0),Point(bottle,.056f,t,0),.012f,Dark);
        Beam(v,Point(bottle,0,.125f,0),Point(back,s*.170f,.105f,.046f),.014f,Cable);
    }
}
// The operator's screens hang from an articulated arm out of the ceiling: a
// carriage on the roof line, an upper arm down and back on a pivot, a forearm
// to the yoke on the cluster's back, every joint on an axle with its bolts.
// Offset to the operator's right so the sight line straight ahead is clear.
void CabinCeilingArm(std::vector<CockpitVertex>& v) {
    constexpr float x=.16f;
    const P root{x,CabinRoof(-.06f)-.050f,-.06f};
    Panel carriage{root,{1,0,0},{0,0,-1},0,0,0};
    GripBody(v,carriage,.180f,.240f,.034f,Dark,.006f);
    for(float t:{-.065f,.065f})for(float u:{-.095f,.095f})FrameBolt(v,Point(carriage,t,u,.0155f),{0,-1,0},.005f);
    // Upper arm back along the roof, forearm down to the back of the cluster.
    const P shoulder{x,.92f,-.06f},elbow{x,.93f,-.22f};
    const auto c=CabinCentre(true);
    const auto yoke=Point(c,x,-.08f,-.100f);
    for(float t:{-.030f,.030f})Slab(v,{x+t-.010f,root[1]-.017f,-.10f},{x+t+.010f,root[1]-.017f,-.10f},{x+t+.010f,root[1]-.017f,-.02f},{x+t-.010f,root[1]-.017f,-.02f},{0,-.070f,0},Dark);
    Axle(v,shoulder,.100f,.034f,Steel);
    for(float t:{-1.f,1.f})FrameBolt(v,Add(shoulder,{t*.052f,0,0}),{t,0,0},.006f);
    Beam(v,shoulder,elbow,.028f,Dark);
    Axle(v,elbow,.096f,.036f,Steel);
    for(float t:{-1.f,1.f})FrameBolt(v,Add(elbow,{t*.050f,0,0}),{t,0,0},.006f);
    Beam(v,elbow,yoke,.026f,Dark);
    Axle(v,yoke,.090f,.032f,Steel);
    for(float t:{-1.f,1.f})FrameBolt(v,Add(yoke,{t*.047f,0,0}),{t,0,0},.006f);
    // A plate on the back of the centre frame carries two cheeks and the yoke axle.
    auto plate=c;plate.c=Point(c,x,-.08f,-.071f);
    GripBody(v,plate,.130f,.110f,.012f,Dark,.004f);
    for(float t:{-.052f,.052f})Beam(v,Point(c,x+t,-.125f,-.095f),Point(c,x+t,-.035f,-.095f),.011f,Dark);
    // The screens' loom, out of the carriage and along the arm's right face,
    // clipped on each segment, into the cluster's back beside the yoke.
    const P legs[]={{x+.055f,root[1]-.020f,-.06f},{x+.055f,.92f,-.06f},{x+.055f,.90f,-.28f},{x+.055f,.795f,-.385f},Point(c,x+.075f,-.08f,-.083f)};
    Rod(v,legs[0],legs[1],.009f,Cable);
    for(unsigned k=1;k+1<5;++k)Rod(v,legs[k],legs[k+1],.009f,Cable);
    for(unsigned k=1;k<3;++k) {
        const auto mid=Mul(Add(legs[k],legs[k+1]),.5f);
        Panel clip{{x+.050f,mid[1],mid[2]},{0,0,1},{0,1,0},0,0,0};
        GripBody(v,clip,.030f,.040f,.022f,Dark,.003f);
        FrameBolt(v,Point(clip,0,-.014f,.0155f),{1,0,0},.003f);
    }
}
// The floor is worked, not a bare plate: tread panels, tie-down rails, service
// covers and the gangway stripe on the right.
void CabinFloorDetail(std::vector<CockpitVertex>& v,float deck,float from,float to) {
    const float half=std::min(CabinHalf(from),CabinHalf(to));
    for(float s:{-1.f,1.f})
        Beam(v,{s*(half-.10f),deck+.008f,from+.06f},{s*(half-.10f),deck+.008f,to-.06f},.020f,Dark);
    for(float z=from+.16f;z<to-.30f;z+=.34f) {
        Beam(v,{-half+.14f,deck+.006f,z},{half-.14f,deck+.006f,z},.014f,Dark);
        for(float x:{-(half-.26f),.30f}) {
            Panel ring{{x,deck+.004f,z},{1,0,0},{0,0,1},0,0,0};
            GripBody(v,ring,.052f,.052f,.014f,Steel,.004f);
            Rod(v,Point(ring,0,0,.012f),Point(ring,0,0,.026f),.008f,Steel);
        }
    }
    for(float z:{from+.32f,to-.36f}) {
        Panel cover{{-half*.52f,deck+.004f,z},{1,0,0},{0,0,1},0,0,0};
        Part(v,referenceServicePlate,cover,.26f,.22f,.016f,Steel);
        for(float t:{-.105f,.105f})for(float u:{-.085f,.085f})FrameBolt(v,Point(cover,t,u,.013f),{0,1,0},.004f);
    }
    Slab(v,{kCabinWalk,deck+.004f,from+.05f},{half-.05f,deck+.004f,from+.05f},
        {half-.05f,deck+.004f,to-.05f},{kCabinWalk,deck+.004f,to-.05f},{0,.012f,0},Bezel);
    Beam(v,{kCabinWalk-.022f,deck+.018f,from+.07f},{kCabinWalk-.022f,deck+.018f,to-.07f},.012f,Warning);
    for(float z=from+.22f;z<to-.18f;z+=.26f)
        Beam(v,{kCabinWalk+.06f,deck+.018f,z},{half-.12f,deck+.018f,z},.008f,Dark);
}
// The left wall is the equipment bank: racked boxes with instruments, a bottle,
// pipework and a loom, standing on the rear deck with their backs to the screen.
void CabinLeftBank(std::vector<CockpitVertex>& v) {
    const float x=-CabinHalf(-.70f)+.110f;
    for(unsigned k=0;k<3;++k) {
        const float z=-1.62f+k*.46f;
        Panel face{{x,-.20f+(k==2?.10f:0.f),z},{0,0,1},{0,1,0},0,0,0};
        ProteusCase(v,face,.420f,.480f,.080f,true);
        if(k==1) {
            auto gauge=face;gauge.c=Point(face,-.12f,.12f,.046f);
            GripBody(v,gauge,.090f,.090f,.022f,Dark,.004f);
            Rod(v,Point(gauge,0,0,.014f),Point(gauge,0,0,.024f),.032f,Bezel);
            Rod(v,Point(gauge,0,0,.022f),Point(gauge,0,0,.027f),.026f,Amber);
            for(int j=0;j<3;++j) {
                // Inside the lid's octagon: its half width is .20, its chamfer .072.
                auto knob=face;knob.c=Point(face,.035f+j*.068f,.11f,.046f);
                GripBody(v,knob,.050f,.050f,.016f,Dark,.003f);
                Rod(v,Point(knob,0,0,.013f),Point(knob,0,0,.030f),.012f,j?Steel:Warning);
            }
        } else {
            for(int j=0;j<4;++j) {
                auto lamp=face;lamp.c=Point(face,-.13f+j*.086f,-.17f,.044f);
                GripBody(v,lamp,.060f,.030f,.014f,Dark,.003f);
                Face(v,lamp,.040f,.016f,j?Cyan:Warning,.016f);
            }
        }
        RoundedPrism(v,{{x-.045f,kCabinStep+.004f,z-.19f},{x+.045f,kCabinStep+.004f,z-.19f},
            {x+.045f,kCabinStep+.004f,z+.19f},{x-.045f,kCabinStep+.004f,z+.19f}},{0,-.44f-(k==2?-.10f:0.f)-kCabinStep-.004f,0},.008f,Dark);
    }
    Panel bottle{{x+.130f,-.48f,-.30f},{0,0,1},{0,1,0},0,0,0};
    GripBody(v,bottle,.520f,.160f,.160f,Bezel,.030f);
    for(float z:{-.46f,-.14f}) {
        Panel strap{{x+.140f,-.48f,z},{0,0,1},{0,1,0},0,0,0};
        GripBody(v,strap,.044f,.200f,.180f,Dark,.006f);
        Rod(v,{x+.05f,-.56f,z},{x+.05f,kCabinStep,z},.016f,Steel);
    }
    Rod(v,{x+.012f,-.40f,-.02f},{x+.012f,-.40f,.42f},.026f,Bezel);
    Rod(v,{x+.012f,-.33f,-.02f},{x+.012f,-.33f,.42f},.020f,Cable);
    for(float z:{.06f,.34f}) {
        Panel clip{{x+.044f,-.365f,z},{0,0,1},{0,1,0},0,0,0};
        GripBody(v,clip,.060f,.130f,.024f,Dark,.004f);
        FrameBolt(v,Point(clip,0,-.050f,.0135f),{1,0,0},.004f);
        Rod(v,{x+.03f,-.43f,z},{x+.03f,kCabinFloor,z},.014f,Steel);
    }
}
// The right side is the gangway: shallow lockers a shoulder can pass, standing
// on the rear deck, and a handrail on its own posts from the hatch to the
// driver's station.
void CabinRightWalk(std::vector<CockpitVertex>& v) {
    const float x=CabinHalf(-.70f)-.012f;
    for(unsigned k=0;k<2;++k) {
        const float z=-1.56f+k*.52f;
        Panel door{{x,-.18f,z},{0,0,-1},{0,1,0},0,0,0};
        ProteusCase(v,door,.460f,.400f,.050f,true);
        RoundedPrism(v,{{x-.062f,kCabinStep+.004f,z-.20f},{x-.010f,kCabinStep+.004f,z-.20f},
            {x-.010f,kCabinStep+.004f,z+.20f},{x-.062f,kCabinStep+.004f,z+.20f}},{0,-.38f-kCabinStep-.004f,0},.008f,Dark);
    }
    // Hand height over the step deck; it drops with the step and ends beside
    // the driver's console, under the driver's eye line to the side screen.
    const float rail=CabinHalf(-.20f)-.120f;
    const P a{rail,.16f,-1.84f},b{rail,.16f,-.72f},c{rail-.06f,-.30f,-.40f};
    Rod(v,a,b,.024f,Steel);Rod(v,b,c,.024f,Steel);
    for(const P& p:{a,b,c}) {
        const float deck=CabinDeck(p[2])+.016f;   // the gangway plate's top
        Rod(v,p,{p[0],deck+.004f,p[2]},.016f,Steel);
        Panel foot{{p[0],deck,p[2]},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,foot,.080f,.080f,.016f,Dark,.004f);
        for(float t:{-.026f,.026f})FrameBolt(v,Point(foot,t,.026f,.0155f),{0,1,0},.004f);
    }
}
// Rear bulkhead, short passage and the walk-through hatch. Taller than the gun
// pod's: this cabin has the height to spare.
void ProteusCabinAft(std::vector<CockpitVertex>& v) {
    constexpr float face=kCabinTail,end=face-.52f,shut=end-.05f,w=.38f;
    constexpr float floorY=kCabinStep,roofY=.92f;
    const float hw=CabinHalf(face),hr=CabinRoof(face);
    Panel wall{{0,.10f,face},{-1,0,0},{0,1,0},0,0,0};
    for(float s:{-1.f,1.f}) {
        const float top=hr-.07f;   // the wall panel sits at y=.10; 3 cm past the roof line
        const std::array<std::array<float,2>,8> cheek{{{s*.40f,-.86f},{s*.70f,-.88f},{s*(hw-.004f),-.78f},{s*(hw-.004f),top-.06f},
            {s*.72f,top},{s*.42f,top},{s*.40f,top-.14f},{s*.38f,-.82f}}};
        Armour(v,wall,cheek,.070f,Hull,.012f);
    }
    const std::array<std::array<float,2>,8> sill{{{-.38f,-.96f},{.38f,-.96f},{.44f,-.90f},{.44f,-.74f},
        {.38f,-.68f},{-.38f,-.68f},{-.44f,-.74f},{-.44f,-.90f}}};
    Armour(v,wall,sill,.070f,Hull,.010f);
    const std::array<std::array<float,2>,8> header{{{-.38f,.78f},{.38f,.78f},{.44f,.84f},{.44f,hr-.13f},
        {.38f,hr-.07f},{-.38f,hr-.07f},{-.44f,hr-.13f},{-.44f,.84f}}};
    Armour(v,wall,header,.070f,Hull,.010f);
    const std::array<P,4> jamb{{{-.412f,floorY-.030f,face},{.412f,floorY-.030f,face},
        {.412f,roofY+.020f,face},{-.412f,roofY+.020f,face}}};
    for(unsigned i=0;i<4;++i)Beam(v,jamb[i],jamb[(i+1)%4],.024f,Steel);
    for(unsigned i=0;i<4;++i)FrameJoint(v,jamb[i],jamb[(i+3)%4],jamb[(i+1)%4],.024f);
    const float lap=w+.030f;
    for(float s:{-1.f,1.f})
        Slab(v,{s*w,floorY,face},{s*w,roofY,face},{s*w,roofY,shut},{s*w,floorY,shut},{s*.030f,0,0},Hull);
    Slab(v,{-lap,floorY,face},{lap,floorY,face},{lap,floorY,shut},{-lap,floorY,shut},{0,-.030f,0},Steel);
    Slab(v,{-lap,roofY,face},{lap,roofY,face},{lap,roofY,shut},{-lap,roofY,shut},{0,.030f,0},Hull);
    for(float s:{-1.f,1.f}) {
        const float turn=end+.18f;
        for(unsigned k=0;k<4;++k) {
            const float y=floorY+.22f+k*.24f,r=k==1?.030f:k==3?.020f:.024f;
            Rod(v,{s*(w-.055f),y,face-.02f},{s*(w-.055f),y,turn},r,k==2?Cable:Bezel);
            Panel bend{{s*(w-.055f),y,turn},{1,0,0},{0,1,0},0,0,0};
            GripBody(v,bend,r*2.5f,r*2.5f,r*2.5f,Dark,.004f);
            Rod(v,{s*(w-.055f),y,turn},{s*(w-.008f),y,turn},r*.85f,k==2?Cable:Bezel);
            Panel through{{s*(w-.004f),y,turn},{0,0,1},{0,1,0},0,0,0};
            GripBody(v,through,.074f,.074f,.014f,Dark,.004f);
            for(float t:{-.026f,.026f})FrameBolt(v,Point(through,t,-.026f,-s*.0085f),{-s,0,0},.004f);
        }
        for(float z:{face-.14f,turn+.10f}) {
            Panel clamp{{s*(w-.026f),floorY+.58f,z},{0,0,1},{0,1,0},0,0,0};
            GripBody(v,clamp,.090f,.800f,.022f,Dark,.004f);
            for(float y:{-.36f,.36f})FrameBolt(v,Point(clamp,0,y,.0135f),{s,0,0},.004f);
        }
    }
    const float mid=(face+end)*.5f;
    Panel tray{{0,roofY-.014f,mid},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,tray,.180f,.400f,.030f,Dark,.005f);
    for(float z:{mid-.155f,mid+.155f})FrameBolt(v,{0,roofY-.046f,z},{0,-1,0},.005f);   // on the tray's underside
    for(float x:{-.045f,.045f}) {
        Rod(v,{x,roofY-.044f,face-.02f},{x,roofY-.044f,end+.185f},.020f,Cable);
        Panel land{{x,roofY-.044f,end+.185f},{1,0,0},{0,1,0},0,0,0};
        GripBody(v,land,.072f,.072f,.018f,Dark,.004f);
    }
    for(float d:{-.074f,.074f}) {
        Rod(v,{0,roofY-.026f,mid+d},{0,roofY-.062f,mid+d},.010f,Dark);
        Rod(v,{0,roofY-.066f,mid+d-.012f},{0,roofY-.066f,mid+d+.012f},.030f,Dark);
    }
    Rod(v,{0,roofY-.066f,mid-.074f},{0,roofY-.066f,mid+.074f},.024f,Amber);
    const float doorY=(floorY+roofY)*.5f;
    Panel door{{0,doorY,end},{-1,0,0},{0,1,0},0,0,0};
    const std::array<std::array<float,2>,8> leaf{{{-.32f,-.74f},{.32f,-.74f},{.365f,-.68f},{.365f,.68f},
        {.32f,.74f},{-.32f,.74f},{-.365f,.68f},{-.365f,-.68f}}};
    Armour(v,door,leaf,.055f,Dark,.014f);
    for(float x:{-.090f,.090f})Beam(v,Point(door,x,-.64f,.020f),Point(door,x,.64f,.020f),.014f,Dark);
    Rod(v,Point(door,0,0,.020f),Point(door,0,0,.052f),.028f,Steel);
    for(int k=0;k<4;++k) {
        const float a=1.5707963f*k+.7853981f;
        Beam(v,Point(door,.026f*std::cos(a),.026f*std::sin(a),.050f),
               Point(door,.105f*std::cos(a),.105f*std::sin(a),.050f),.013f,Dark);
        Rod(v,Point(door,.105f*std::cos(a),.105f*std::sin(a),.040f),
              Point(door,.105f*std::cos(a),.105f*std::sin(a),.058f),.017f,Steel);
    }
    for(float y:{-.50f,0.f,.50f}) {
        Rod(v,Point(door,-.378f,y,-.010f),Point(door,-.378f,y,.028f),.024f,Dark);
        FrameBolt(v,Point(door,-.378f,y,.032f),{0,0,1},.006f);
    }
    Beam(v,Point(door,-.26f,-.78f,.012f),Point(door,.26f,-.78f,.012f),.014f,Warning);
    Panel frame{{0,doorY,end-.06f},{-1,0,0},{0,1,0},0,0,0};
    const std::array<std::array<float,2>,8> surround{{{-.36f,-.90f},{.36f,-.90f},{.44f,-.82f},{.44f,.82f},
        {.36f,.90f},{-.36f,.90f},{-.44f,.82f},{-.44f,-.82f}}};
    Armour(v,frame,surround,.060f,Hull,.010f);
}
// Screen divisions: straight seams on the screen lines - each sill, each roof
// edge, two runs over the roof, the nose, and uprights at two stations.
void ProteusCabinSeams(std::vector<CockpitVertex>& v) {
    auto sill=[](float z){return CabinDeck(z)+.060f;};
    for(float s:{-1.f,1.f}) {
        const float a=kCabinTail+.02f,b=kCabinNose;
        MonitorSeamPath(v,std::array<P,6>{{{s*(CabinHalf(a)+.028f),sill(a),a},{s*(CabinHalf(kCabinRiser)+.028f),kCabinStep+.060f,kCabinRiser-.02f},
            {s*(CabinHalf(kCabinRiser)+.028f),kCabinFloor+.060f,kCabinRiser+.02f},{s*(CabinHalf(.20f)+.028f),sill(.20f),.20f},
            {s*(CabinHalf(kCabinChin)+.028f),sill(kCabinChin),kCabinChin},{s*(CabinHalf(b)+.028f),sill(b)+.120f,b}}});
        MonitorSeamPath(v,std::array<P,5>{{{s*(CabinHalf(a)+.028f),CabinRoof(a),a},{s*(CabinHalf(-.20f)+.028f),CabinRoof(-.20f),-.20f},
            {s*(CabinHalf(.20f)+.028f),CabinRoof(.20f),.20f},
            {s*(CabinHalf(kCabinChin)+.028f),CabinRoof(kCabinChin),kCabinChin},{s*(CabinHalf(b)+.028f),CabinRoof(b),b}}});
        MonitorSeamPath(v,std::array<P,3>{{{s*.46f,CabinRoof(a)+.028f,a},{s*.42f,CabinRoof(-.20f)+.028f,-.20f},
            {s*.30f,CabinRoof(b)+.028f,b}}});
        for(float z:{-.40f,.50f})
            MonitorSeam(v,{s*(CabinHalf(z)+.028f),CabinRoof(z),z},{s*(CabinHalf(z)+.028f),sill(z),z},{0,0,1},{s*.014f,0,0});
    }
    MonitorSeam(v,{-CabinHalf(kCabinNose)-.028f,CabinRoof(kCabinNose),kCabinNose+.03f},
        {CabinHalf(kCabinNose)+.028f,CabinRoof(kCabinNose),kCabinNose+.03f},{0,1,0},{0,0,.014f});
    MonitorSeam(v,{-CabinHalf(kCabinNose)-.028f,sill(kCabinNose)+.120f,kCabinNose+.03f},
        {CabinHalf(kCabinNose)+.028f,sill(kCabinNose)+.120f,kCabinNose+.03f},{0,1,0},{0,0,.014f});
}
std::vector<CockpitVertex> ProteusCabinInterior(bool rear) {
    std::vector<CockpitVertex> v;v.reserve(320000);
    // The screen line is straight to z=.20 and tapers after it: the deck edge
    // follows it in two pieces, and the aft piece runs back under the riser.
    Slab(v,{-CabinHalf(kCabinRiser),kCabinFloor,kCabinRiser-.06f},{CabinHalf(kCabinRiser),kCabinFloor,kCabinRiser-.06f},
        {CabinHalf(.20f),kCabinFloor,.20f},{-CabinHalf(.20f),kCabinFloor,.20f},{0,-.046f,0},Steel);
    Slab(v,{-CabinHalf(.20f),kCabinFloor,.20f},{CabinHalf(.20f),kCabinFloor,.20f},
        {CabinHalf(kCabinChin),kCabinFloor,kCabinChin},{-CabinHalf(kCabinChin),kCabinFloor,kCabinChin},{0,-.046f,0},Steel);
    Slab(v,{-CabinHalf(kCabinTail),kCabinStep,kCabinTail},{CabinHalf(kCabinTail),kCabinStep,kCabinTail},
        {CabinHalf(kCabinRiser),kCabinStep,kCabinRiser},{-CabinHalf(kCabinRiser),kCabinStep,kCabinRiser},{0,-.046f,0},Steel);
    Slab(v,{-CabinHalf(kCabinRiser),kCabinStep,kCabinRiser},{CabinHalf(kCabinRiser),kCabinStep,kCabinRiser},
        {CabinHalf(kCabinRiser),kCabinFloor+.010f,kCabinRiser},{-CabinHalf(kCabinRiser),kCabinFloor+.010f,kCabinRiser},{0,0,.040f},Hull);
    Beam(v,{-CabinHalf(kCabinRiser)+.05f,kCabinStep+.006f,kCabinRiser+.022f},
           {CabinHalf(kCabinRiser)-.05f,kCabinStep+.006f,kCabinRiser+.022f},.020f,Bezel);
    for(float s:{-1.f,1.f})for(unsigned bay=0;bay<3;++bay) {
        const float a=bay==0?kCabinTail:bay==1?kCabinRiser:.20f,b=bay==0?kCabinRiser+.04f:bay==1?.20f:kCabinChin;
        const float y=bay?kCabinFloor:kCabinStep;
        Slab(v,{s*CabinHalf(a),y-.012f,a},{s*CabinHalf(a),y+.058f,a},
            {s*CabinHalf(b),y+.058f,b},{s*CabinHalf(b),y-.012f,b},{s*.016f,0,0},Hull);
        Beam(v,{s*(CabinHalf(a)-.008f),y+.072f,a+.012f},{s*(CabinHalf(b)-.008f),y+.072f,b-.012f},.018f,Bezel);
    }
    {
        const float w=CabinHalf(kCabinChin);
        Slab(v,{-w,kCabinFloor-.012f,kCabinChin},{w,kCabinFloor-.012f,kCabinChin},
            {w,kCabinFloor+.056f,kCabinChin},{-w,kCabinFloor+.056f,kCabinChin},{0,0,.016f},Hull);
        Beam(v,{-w+.02f,kCabinFloor+.070f,kCabinChin+.008f},{w-.02f,kCabinFloor+.070f,kCabinChin+.008f},.018f,Bezel);
    }
    CabinFloorDetail(v,kCabinFloor,kCabinRiser,kCabinChin);
    CabinFloorDetail(v,kCabinStep,kCabinTail,kCabinRiser);
    CabinLeftBank(v);
    CabinRightWalk(v);
    ProteusCabinAft(v);
    CabinSeat(v,kCabinFloor,{0,0,0});
    CabinFootStrut(v,kCabinFloor,{0,0,0});
    CabinConsole(v);
    CabinCluster(v,proteusCabinPanels,true);
    CabinConsoleDetail(v);
    CabinSeat(v,kCabinStep,{0,kCabinRearUp,kCabinRearBack});
    CabinFootStrut(v,kCabinStep,{0,kCabinRearUp,kCabinRearBack});
    for(float s:{-1.f,1.f}) {
        const auto lever=v.size();PalmLever(v,s);
        MovePart(v,lever,{1,1,1},{-s*.020f,kCabinRearUp+.090f,kCabinRearBack});
        // The lever's guide plate sits in a console head; a pedestal from the
        // rear deck carries the head, its top buried in the head's underside.
        Panel base{{s*.330f,kCabinRearUp-.470f,kCabinRearBack+.125f},{1,0,0},{0,.16f,.987117f},0,0,0};
        auto head=base;head.c=Point(base,0,0,-.048f);
        GripBody(v,head,.250f,.360f,.140f,Dark,.008f);
        const float x=s*.330f,z=kCabinRearBack+.125f;
        RoundedPrism(v,{{x-.090f,kCabinStep+.004f,z-.140f},{x+.090f,kCabinStep+.004f,z-.140f},
            {x+.090f,kCabinStep+.004f,z+.140f},{x-.090f,kCabinStep+.004f,z+.140f}},{0,-.030f-kCabinStep-.004f,0},.010f,Dark);
        Panel foot{{x,kCabinStep,z},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,foot,.200f,.320f,.020f,Steel,.006f);
        for(float t:{-.080f,.080f})for(float u:{-.130f,.130f})FrameBolt(v,Point(foot,t,u,.0165f),{0,1,0},.004f);
        // A lidded box on the head's outer face, and the lever's loom down the
        // pedestal's back into a gland on the deck.
        // The head leans 9 degrees; its side face does too, and so must the bin.
        Panel bin{{s*.456f,-.016f,z+.018f},{0,s*.16f,s*.987117f},{0,.987117f,-.16f},0,0,0};
        ProteusCase(v,bin,.100f,.080f,.040f,false);
        for(float dx:{-.020f,.020f}) {
            Rod(v,{x+dx,kCabinRearUp-.47f-.09f,z-.140f-.014f},{x+dx,kCabinStep+.040f,z-.140f-.014f},.009f,Cable);
            Rod(v,{x+dx,kCabinStep+.040f,z-.140f-.014f},{x+dx,kCabinStep+.040f,z-.205f},.009f,Cable);
        }
        Panel clip{{x,kCabinRearUp-.47f-.30f,z-.140f-.020f},{1,0,0},{0,1,0},0,0,0};
        GripBody(v,clip,.070f,.036f,.016f,Dark,.004f);
        for(float dx:{-.026f,.026f})FrameBolt(v,Point(clip,dx,0,.0155f),{0,0,-1},.004f);
        Panel gland{{x,kCabinStep,z-.230f},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,gland,.090f,.070f,.050f,Dark,.006f);
        for(float dx:{-.030f,.030f})FrameBolt(v,Point(gland,dx,-.022f,.0155f),{0,1,0},.004f);
    }
    CabinRearHood(v);
    CabinCluster(v,proteusCabinPanels+3,false);
    CabinCeilingArm(v);
    ProteusCabinSeams(v);
    if(rear)MovePart(v,0,{1,1,1},{0,-kCabinRearUp,-kCabinRearBack});
    return v;
}
}
