// Brute (Vehicle410_heli) door gunners, seats 1 (left) and 2 (right), rebuilt
// (the user, 2026-09-25, after the helicopters). Each sits in his side's
// forward door panel, facing out: the model's own recessed door panel over
// the gun (model z .74-1.78, y 1.58-2.70) is the doorway, and it is all that
// is cut out of the hull (cockpit.cpp), so the fuselage round it, the stub
// wing under it and the wing over it stay the model's own (the user: keep the
// outside as it is, he leans out to look fore and aft; do not cut the wing).
// The seat on the wall's line, 10 cm further out than before and 13 cm lower,
// the head clear of the doorway's top when he leans out; the Proteus driver's
// seat pan and footrest, the footrest and the Proteus gunner's grip out of
// the door over the stub wing, the grip upright on a stout leaning column
// from the footrest's centre block. Behind him a closed booth: walls fore and
// aft, a ceiling, a deck, the hatch into the cabin, and round the doorway a
// lining on the skin's own line. The door has slid inward into the forward
// wall, on his right: its edge shows in a slit there. The screens are see-through, on swing arms
// from the walls reaching just out of the door, with grab handles: radar on
// the left, health and ammunition on the right.
//
// Cabin coordinates as every cabin: +X the gunner's right (forward, at the
// left door: the cockpit is drawn mirrored in x against the model, as every
// heli cabin is, HeliCabinPoint), +Y up, +Z out of the door. The right door's
// gunner (seat 2) has the mirror image (CockpitMirrored): the machine is
// symmetric, this booth is not, so there +X is forward too, on his left. The cabin origin is model (±1.20, 2.32,
// 1.26) (vehicle_camera.cpp), the eye (±1.10, 2.42, 1.26). Every number below
// is measured on the model (research/cockpit/helis) in these coordinates.
namespace doorgun {
constexpr float kFloor=-1.09f;    // the booth's deck, model y 1.23
constexpr float kCeiling=.50f;    // model y 2.82, inside the wing's root
constexpr float kHatch=-.70f;     // the hatch into the cabin behind the seat
constexpr float kHalf=.525f;      // the walls, just outside the doorway's sides
// The doorway: the door panel's outline on the skin, clockwise seen from
// inside, from its top aft corner (its top forward corner is cut short under
// the engine intake).
constexpr P kDoor[12]={{-.388f,.389f,-.121f},{.218f,.371f,-.150f},{.522f,.215f,-.103f},{.523f,.047f,-.016f},
    {.519f,-.308f,.149f},{.520f,-.569f,.254f},{.332f,-.737f,.305f},{-.387f,-.736f,.314f},
    {-.522f,-.577f,.277f},{-.521f,-.312f,.190f},{-.522f,.039f,.060f},{-.519f,.256f,-.036f}};
// The skin (its z) on each wall's line, aft (x -.525) and forward (x +.525),
// every 10 cm up from the deck, the last at the ceiling.
constexpr float kWallY[17]={-1.09f,-.99f,-.89f,-.79f,-.69f,-.59f,-.49f,-.39f,-.29f,-.19f,-.09f,.01f,.11f,.21f,.31f,.41f,.50f};
constexpr float kWallSkin[2][17]={{.214f,.266f,.314f,.325f,.301f,.279f,.248f,.215f,.181f,.144f,.108f,.071f,.029f,-.016f,-.067f,-.127f,-.183f},
                                  {.229f,.274f,.315f,.319f,.283f,.259f,.221f,.181f,.139f,.093f,.047f,.000f,-.049f,-.101f,-.168f,-.235f,-.284f}};
float WallSkin(float side,float y) {
    const auto& s=kWallSkin[side<0?0:1];
    if(y<=kWallY[0])return s[0];
    for(int i=0;i<16;++i)if(y<=kWallY[i+1])return s[i]+(s[i+1]-s[i])*(y-kWallY[i])/(kWallY[i+1]-kWallY[i]);
    return s[16];
}
// The skin over the doorway, up to the ceiling, and under it, down to the
// deck: columns across (2 cm on into the walls), each from the doorway's edge
// (kHeaderY, kSillY), its z at 0, 1/3, 2/3 and all the way.
constexpr float kHeaderX[12]={-.545f,-.519f,-.450f,-.388f,-.250f,-.100f,.050f,.218f,.300f,.400f,.522f,.545f};
constexpr float kHeaderY[12]={.260f,.260f,.330f,.393f,.389f,.384f,.380f,.375f,.333f,.282f,.219f,.219f};
constexpr float kHeaderZ[12][4]={{-.038f,-.084f,-.132f,-.182f},{-.038f,-.085f,-.133f,-.183f},{-.083f,-.116f,-.152f,-.187f},
    {-.123f,-.146f,-.168f,-.190f},{-.130f,-.152f,-.175f,-.198f},{-.137f,-.160f,-.183f,-.206f},{-.144f,-.168f,-.194f,-.216f},
    {-.153f,-.181f,-.209f,-.221f},{-.140f,-.176f,-.211f,-.223f},{-.125f,-.172f,-.221f,-.248f},{-.106f,-.169f,-.232f,-.283f},
    {-.111f,-.174f,-.237f,-.290f}};
constexpr float kSillX[11]={-.545f,-.522f,-.450f,-.387f,-.200f,.000f,.200f,.332f,.430f,.520f,.545f};
constexpr float kSillY[11]={-.581f,-.581f,-.666f,-.740f,-.740f,-.741f,-.741f,-.741f,-.653f,-.573f,-.573f};
constexpr float kSillZ[11][4]={{.277f,.316f,.299f,.214f},{.277f,.316f,.300f,.214f},{.297f,.329f,.287f,.215f},
    {.314f,.331f,.275f,.216f},{.312f,.330f,.278f,.219f},{.310f,.330f,.280f,.222f},{.307f,.329f,.281f,.224f},
    {.306f,.329f,.281f,.226f},{.279f,.322f,.293f,.228f},{.255f,.299f,.303f,.229f},{.253f,.298f,.303f,.229f}};
// The door's slit in the forward wall (the gunner's right): 3 cm wide, 7-10 cm in from the skin,
// from just over the sill to under the doorway's top.
constexpr float kSlitLow=-.69f,kSlitHigh=.31f,kSlitFront=.070f,kSlitBack=.100f;
}
// The gunner's screens, each turned to the eye: the radar on the left arm,
// health over ammunition on the right; see-through. Stowed back against the
// walls, low beside his hips, only their outer corners out of the door:
// screens he pulls out when he wants them (the user, after the first test:
// out in the doorway they were too close and in the way; lower, where he drew).
const Panel bruteGunnerPanels[]={
 FacingPilot({-.415f,-.300f,.060f},.240f,.240f,0),   // radar
 FacingPilot({.415f,-.195f,.030f},.240f,.100f,1),    // health (10 cm: its rim's corner caps meet, not overlap)
 FacingPilot({.415f,-.390f,.040f},.220f,.130f,2),    // ammunition
};
std::vector<CockpitVertex> BruteGunnerInterior() {
    using namespace doorgun;
    std::vector<CockpitVertex> v;v.reserve(200000);
    // The lining round the doorway, 1.5 cm inside the skin (clear of the
    // header's own faces, which the eye sees from under them; its edge row
    // .5 cm, meeting the doorway), on its measured
    // shape: over the doorway up to the ceiling, under it down to the deck,
    // 2 cm on into the walls.
    {
        auto header=[](unsigned i,unsigned k){return P{kHeaderX[i],kHeaderY[i]+(kCeiling-kHeaderY[i])*float(k)/3,kHeaderZ[i][k]-(k?.015f:.005f)};};
        auto sill=[](unsigned i,unsigned k){return P{kSillX[i],kSillY[i]+(kFloor-kSillY[i])*float(k)/3,kSillZ[i][k]-(k?.015f:.005f)};};
        for(unsigned i=0;i+1<12;++i)for(unsigned k=0;k<3;++k)
            hk::Facing(v,header(i,k),header(i+1,k),header(i+1,k+1),header(i,k+1),{0,0,-1},Hull);
        for(unsigned i=0;i+1<11;++i)for(unsigned k=0;k<3;++k)
            hk::Facing(v,sill(i,k),sill(i+1,k),sill(i+1,k+1),sill(i,k+1),{0,0,-1},Hull);
    }
    // The walls aft and forward, from the hatch to the skin; the forward one
    // (his right) parted by the door's slit.
    for(float side:{-1.f,1.f}) {
        const float x=side*kHalf;
        for(int k=0;k<16;++k) {
            const float a=kWallY[k],b=kWallY[k+1];
            const float fa=WallSkin(side,a)-.005f,fb=WallSkin(side,b)-.005f;
            if(side>0&&a>=kSlitLow-1e-3f&&b<=kSlitHigh+1e-3f) {
                hk::Facing(v,{x,a,kHatch},{x,a,fa-kSlitBack},{x,b,fb-kSlitBack},{x,b,kHatch},{-1,0,0},Hull);
                hk::Facing(v,{x,a,fa-kSlitFront},{x,a,fa},{x,b,fb},{x,b,fb-kSlitFront},{-1,0,0},Hull);
                // The slit's sides and its back, 3 cm deep in the wall.
                hk::Facing(v,{x,a,fa-kSlitBack},{x+.030f,a,fa-kSlitBack},{x+.030f,b,fb-kSlitBack},{x,b,fb-kSlitBack},{0,0,1},Dark);
                hk::Facing(v,{x,a,fa-kSlitFront},{x+.030f,a,fa-kSlitFront},{x+.030f,b,fb-kSlitFront},{x,b,fb-kSlitFront},{0,0,-1},Dark);
                hk::Facing(v,{x+.030f,a,fa-kSlitBack},{x+.030f,a,fa-kSlitFront},{x+.030f,b,fb-kSlitFront},{x+.030f,b,fb-kSlitBack},{-1,0,0},Dark);
            } else {
                hk::Facing(v,{x,a,kHatch},{x,a,fa},{x,b,fb},{x,b,kHatch},{-side,0,0},Hull);
            }
        }
        if(side>0)for(float y:{kSlitLow,kSlitHigh}) {   // the slit's ends, closed
            const float f=WallSkin(side,y)-.005f;
            hk::Facing(v,{x,y,f-kSlitBack},{x+.030f,y,f-kSlitBack},{x+.030f,y,f-kSlitFront},{x,y,f-kSlitFront},{0,y<0?1.f:-1.f,0},Dark);
        }
        // A frame of the airframe standing out of the wall behind the seat.
        const float z=-.62f;
        Slab(v,{side*(kHalf-.035f),kFloor,z},{side*(kHalf-.035f),kFloor,z+.050f},{side*(kHalf-.035f),kCeiling,z+.050f},{side*(kHalf-.035f),kCeiling,z},{side*.034f,0,0},Bezel);
    }
    // The door itself in the slit: its steel edge a little in from the slit's
    // mouth, a pull recessed in it at hand height.
    {
        const float x=kHalf+.018f,mid=(kSlitFront+kSlitBack)*.5f;
        auto at=[&](float y){return P{x,y,WallSkin(1,y)-.005f-mid};};
        Beam(v,at(kSlitLow+.02f),at(kSlitHigh-.02f),.009f,Steel);
        const auto pull=at(-.22f);
        Box(v,{pull[0]-.014f,pull[1]-.060f,pull[2]-.006f},{pull[0]-.009f,pull[1]+.060f,pull[2]+.006f},Dark);
    }
    // Ceiling, deck and the hatch's wall close the booth; the ceiling and the
    // deck meet the lining's top and bottom rows.
    for(unsigned i=0;i+1<12;++i) {
        const float a=kHeaderX[i],b=kHeaderX[i+1];
        hk::Facing(v,{a,kCeiling,kHatch},{b,kCeiling,kHatch},{b,kCeiling,kHeaderZ[i+1][3]-.015f},{a,kCeiling,kHeaderZ[i][3]-.015f},{0,-1,0},Hull);
    }
    for(unsigned i=0;i+1<11;++i) {
        const float a=kSillX[i],b=kSillX[i+1];
        hk::Facing(v,{a,kFloor,kHatch},{b,kFloor,kHatch},{b,kFloor,kSillZ[i+1][3]-.015f},{a,kFloor,kSillZ[i][3]-.015f},{0,1,0},Hull);
    }
    hk::Facing(v,{-kHalf,kFloor,kHatch},{kHalf,kFloor,kHatch},{kHalf,kCeiling,kHatch},{-kHalf,kCeiling,kHatch},{0,0,1},Hull);
    // The doorway's frame: a rail round its outline, over the lining's edge.
    {
        std::vector<P> ring(std::begin(kDoor),std::end(kDoor));ring.push_back(kDoor[0]);
        hk::Rail(v,ring,.016f,Dark);
    }
    // The hatch into the cabin: closed, a frame, a small window, hinges and
    // the latch handle.
    {
        const float z=kHatch;
        Panel hatch{{0,-.33f,z+.004f},{-1,0,0},{0,1,0},0,0,0};   // Cross(right,up) = -z: into the hatch
        GripBody(v,hatch,.780f,1.300f,.030f,Steel,.006f);
        auto window=hatch;window.c=Point(hatch,0,.42f,.020f);GripBody(v,window,.30f,.22f,.006f,Glass,.004f);
        for(float y:{-.85f,.15f})Axle(v,{-.37f,y,z+.030f},.12f,.024f,Dark);
        Panel latch{{.30f,-.30f,z+.035f},{-1,0,0},{0,1,0},0,0,0};
        GripBody(v,latch,.06f,.16f,.020f,Dark,.004f);
        Beam(v,{.30f,-.25f,z+.060f},{.22f,-.25f,z+.060f},.010f,Warning);
    }
    // Overhead: a dome light, a vent, a loom along the forward wall, and a
    // grab bar across the doorway's head, for a lean out.
    {
        Panel light{{0,kCeiling-.002f,-.46f},{1,0,0},{0,0,-1},0,0,0};   // Cross(right,up) = +y: into the ceiling
        GripBody(v,light,.14f,.08f,.020f,Dark,.004f);
        Box(v,{-.050f,kCeiling-.018f,-.485f},{.050f,kCeiling-.010f,-.435f},Amber);
        Panel vent{{.26f,kCeiling-.002f,-.58f},{1,0,0},{0,0,-1},0,0,0};
        Part(v,referenceVent,vent,.14f,.12f,.016f,Steel);
        Rod(v,{-(kHalf-.02f),kCeiling-.05f,kHatch+.05f},{-(kHalf-.02f),kCeiling-.05f,-.36f},.014f,Cable);
        const float y=.43f,z=-.33f;
        Rod(v,{-.30f,y,z},{.30f,y,z},.014f,Steel);
        for(float x:{-.26f,.26f}) {
            Rod(v,{x,y,z},{x,kCeiling,z},.010f,Steel);
            Axle(v,{x,kCeiling-.006f,z},.030f,.018f,Dark);
        }
        for(float x:{-.20f,.20f})Rod(v,{x-.060f,y,z},{x+.060f,y,z},.017f,Rubber);
    }
    // Seat: the Proteus driver's pan and base on its plinth, on the deck at
    // the wall's line.
    CabinSeat(v,kFloor,{0,0,0});
    // Footrest: the Proteus driver's, its block and treads out of the door
    // over the stub wing; the strut out of the seat base's front runs level
    // over the doorway's sill, then down to the block.
    {
        constexpr float deck=-1.22f;   // the driver's deck: the treads' height under the seat
        const P block{0,deck+.27f,.56f};
        const P a{0,-.700f,.19f},b{0,-.665f,.42f},c{0,block[1]+.035f,block[2]-.020f};
        Beam(v,a,b,.028f,Dark);Beam(v,b,c,.028f,Dark);FrameJoint(v,b,a,c,.028f);
        {
            const float x=block[0],y=block[1]+.05f,z=block[2],w=.085f,cut=.032f;
            RoundedPrism(v,{{x-w+cut,y,z-w},{x+w-cut,y,z-w},{x+w,y,z-w+cut},{x+w,y,z+w-cut},
                {x+w-cut,y,z+w},{x-w+cut,y,z+w},{x-w,y,z+w-cut},{x-w,y,z-w+cut}},{0,-.100f,0},.006f,Dark);
        }
        const P axleC{block[0],deck+.25f,block[2]+.04f};
        Rod(v,Add(axleC,{-.275f,0,0}),Add(axleC,{.275f,0,0}),.014f,Steel);
        for(float s:{-1.f,1.f}) {
            Axle(v,Add(axleC,{s*.095f,0,0}),.030f,.022f,Dark);
            FrameBolt(v,Add(axleC,{s*.276f,0,0}),{s,0,0},.006f);
            Panel board{{s*.1825f,deck+.28f,block[2]+.02f},{1,0,0},{0,.5446f,.8387f},0,0,0};
            GripBody(v,board,.185f,.250f,.026f,Steel,.005f);
            auto tread=board;tread.c=Point(board,0,0,.016f);
            GripBody(v,tread,.150f,.210f,.008f,Rubber,.003f);
            for(int k=0;k<5;++k)Beam(v,Point(tread,-.060f,-.066f+k*.033f,.017f),Point(tread,.060f,-.066f+k*.033f,.017f),.0032f,Bezel);
            Beam(v,Point(board,-.076f,-.124f,.020f),Point(board,.076f,-.124f,.020f),.012f,Dark);
            Beam(v,Point(board,0,.092f,.018f),Point(board,0,.142f,.036f),.010f,Rubber);
            auto bearing=board;bearing.c=Point(board,0,0,-.030f);
            GripBody(v,bearing,.070f,.060f,.030f,Dark,.004f);
        }
        // The grip: the Proteus gunner's, upright, on a stout column leaning
        // back to the gunner (the user: not a thin upright bar) out of a fork
        // on the block's top; a collar and a pad square to the grip at its head.
        const P foot{0,block[1]+.080f,block[2]+.040f},head{0,-.575f,.440f};
        for(float s:{-1.f,1.f})
            RoundedPrism(v,{{s*.042f-.009f,block[1]+.050f,foot[2]-.040f},{s*.042f+.009f,block[1]+.050f,foot[2]-.040f},
                {s*.042f+.009f,block[1]+.050f,foot[2]+.040f},{s*.042f-.009f,block[1]+.050f,foot[2]+.040f}},{0,.060f,0},.004f,Dark);
        Axle(v,foot,.100f,.022f,Steel);
        ReferenceBeam(v,foot,head,.055f,.048f);
        Rod(v,Sub(head,{0,.030f,0}),Add(head,{0,.012f,0}),.040f,Dark);
        const P axis{0,1,0},side{-1,0,0},fwd{0,0,1};
        const P plate=Add(head,{0,.060f,0});
        Panel pad{Sub(plate,Mul(axis,.045f)),side,fwd,0,0,0};
        GripBody(v,pad,.130f,.125f,.040f,Dark,.006f);
        for(float u:{-.045f,.045f})for(float w:{-.042f,.042f})FrameBolt(v,Point(pad,u,w,-.0405f),axis,.004f);
        constexpr float scale=.243f;
        StickPart(v,proteusStickBase,plate,side,axis,fwd,scale,proteusStickBaseMaterial);
        StickPart(v,proteusStickBoot,plate,side,axis,fwd,scale,proteusStickBootMaterial);
        StickPart(v,proteusStickCollar,plate,side,axis,fwd,scale,proteusStickCollarMaterial);
        StickPart(v,proteusStickCable,plate,side,axis,fwd,scale,proteusStickCableMaterial);
        StickPart(v,proteusStickBody,plate,side,axis,fwd,scale,proteusStickBodyMaterial);
        StickPart(v,proteusStickTrigger,plate,side,axis,fwd,scale,proteusStickTriggerMaterial);
        StickPart(v,proteusStickPaddle,plate,side,axis,fwd,scale,proteusStickPaddleMaterial);
        StickPart(v,proteusStickButtonA,plate,side,axis,fwd,scale,proteusStickButtonAMaterial);
        StickPart(v,proteusStickButtonB,plate,side,axis,fwd,scale,proteusStickButtonBMaterial);
        StickPart(v,proteusStickButtonC,plate,side,axis,fwd,scale,proteusStickButtonCMaterial);
        StickPart(v,proteusStickButtonD,plate,side,axis,fwd,scale,proteusStickButtonDMaterial);
        StickPart(v,proteusStickButtonE,plate,side,axis,fwd,scale,proteusStickButtonEMaterial);
        StickPart(v,proteusStickButtonF,plate,side,axis,fwd,scale,proteusStickButtonFMaterial);
        StickPart(v,proteusStickButtonG,plate,side,axis,fwd,scale,proteusStickButtonGMaterial);
    }
    // The screens: the Nix's open rims, see-through. Each on a swing arm from
    // its wall -- a wall plate, two links on upright pins, a clamp on the
    // rim's wall-side edge -- reaching just out of the door; health rides on
    // ammunition by a short link; a grab handle under the radar and under the
    // ammunition to swing them.
    const auto& radar=bruteGunnerPanels[0];const auto& health=bruteGunnerPanels[1];const auto& ammo=bruteGunnerPanels[2];
    for(const auto& p:bruteGunnerPanels)Instrument(v,p);
    auto hub=[&](const Panel& p,float s,float y){return Point(p,s*(p.w*.5f+.050f),y,-.015f);};
    auto clamp=[&](const Panel& p,const P& at) {
        auto c=p;c.c=at;GripBody(v,c,.040f,.050f,.030f,Dark,.004f);
        Rod(v,Point(p,(at[0]>p.c[0]?1.f:-1.f)*(p.w*.5f+.028f),Dot(Sub(at,p.c),p.up),-.015f),at,.008f,Steel);
    };
    auto arm=[&](float s,const P& end) {
        const P base{s*(kHalf-.012f),end[1],end[2]-.220f},elbow{s*(kHalf-.048f),end[1],end[2]-.100f};
        Panel plate{{s*(kHalf-.009f),end[1],base[2]},{0,0,-s},{0,1,0},0,0,0};
        GripBody(v,plate,.080f,.120f,.008f,Dark,.004f);
        for(float u:{-.040f,.040f})FrameBolt(v,Point(plate,0,u,.009f),{-s,0,0},.005f);
        for(const auto& j:{base,elbow})Rod(v,Sub(j,{0,.030f,0}),Add(j,{0,.030f,0}),.020f,Dark);
        Rod(v,base,elbow,.013f,Steel);Rod(v,elbow,end,.013f,Steel);
        Rod(v,Sub(end,{0,.022f,0}),Add(end,{0,.022f,0}),.017f,Dark);
    };
    {
        const auto end=hub(radar,-1,-.075f);
        clamp(radar,end);arm(-1,end);
    }
    {
        const auto end=hub(ammo,1,-ammo.h*.5f);
        clamp(ammo,end);arm(1,end);
        const auto low=hub(ammo,1,ammo.h*.5f),high=hub(health,1,-health.h*.5f);
        clamp(ammo,low);clamp(health,high);
        Rod(v,low,high,.010f,Steel);
    }
    auto handle=[&](const Panel& p) {
        const float y0=-p.h*.5f-.028f,y1=-p.h*.5f-.080f,x=p.w*.38f;
        for(float s:{-1.f,1.f})Rod(v,Point(p,s*x,y0,-.012f),Point(p,s*x,y1,-.012f),.008f,Steel);
        Rod(v,Point(p,-x,y1,-.012f),Point(p,x,y1,-.012f),.009f,Steel);
        Rod(v,Point(p,-x*.60f,y1,-.012f),Point(p,x*.60f,y1,-.012f),.015f,Rubber);
    };
    handle(radar);handle(ammo);
    return v;
}
