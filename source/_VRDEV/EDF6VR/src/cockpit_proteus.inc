// Proteus MK2 gunner station, inside one arm-mounted gun pod.
// Cabin origin is gun_mount_l / gun_mount_r + (+-.179,-1.10,2.34) in that
// bone's own frame. Every envelope number is measured from v614_proteus_mk2.mdb
// by research/cockpit/survey_proteus.py and research/cockpit/proteus_probe.py.
//
// The real compartment: side skins at +-.7285, a flat structural deck at +.736,
// a floor plane climbing 28.1 degrees toward the nose, a blunt nose cone from
// z=1.22 and the pod's own tunnel running aft past z=-.58. The arm carries no
// load into the turret except through its own pivot, so the top of the cabin
// is a screen like the sides rather than a lining: the gunner looks up at the
// weapon and at the bay's underside directly. Only the aft bay is covered,
// which is all it takes to hide the back of that pivot. Pitch is -40..+20
// degrees and yaw -50..+25 (SGO begaruta_pilot_class).
//
// The interior is inset from those skins by kProteusLiner: real armour and the
// loom behind the display panels take that space, so the cabin is deliberately
// a little tighter than the bare measurement.
namespace {
const Panel proteusPanels[]={
 FacingPilot({-.405f,-.045f,.58f},.24f,.24f,0),  // radar, left console
 FacingPilot({ .415f,-.085f,.555f},.26f,.13f,1), // slim health strip, right console
 FacingPilot({ .395f, .150f,.640f},.24f,.12f,2), // single weapon ammunition, above it
};
// Native pod skins, in cabin coordinates.
float ProteusFloor(float z) {return z<-.94f?-1.553f:z<-.54f?-1.453f+.25f*(z+.54f):-1.1644f+.5344f*z;}
float ProteusDeck(float z) {return z<1.06f?.736f:.736f-.25f*(z-1.06f);}
float ProteusSkin(float z) {return z<1.22f?.7285f:std::max(.16f,.7285f-2.67f*(z-1.22f));}
// Armour and cable runs behind the panels; the cabin surface is inside them.
constexpr float kProteusLiner=.075f;
// Footprint of the stick pedestal on the floor. The centre keel runs up to
// these faces and stops there: the pedestal is bolted to the keel, not
// crossed by it, and the seat pan ends 50mm aft of kProteusStickAft.
constexpr float kProteusStickAft=.545f,kProteusStickFore=.650f;
float ProteusInner(float z) {return ProteusSkin(z)-kProteusLiner;}
float ProteusRoof(float z) {return ProteusDeck(z)-kProteusLiner;}
float ProteusPan(float z) {return ProteusFloor(z)+kProteusLiner;}
// The Titan's gunner sits in the same pod with the right side brought in:
// no right armrest, so the wall comes to the seat. Left side unchanged.
constexpr float kTitanRight=.42f;
float PodSide(float s,float w,bool titan) {return s>0&&titan?std::min(w,kTitanRight):w;}
// Titan / Railgun gunner: the radar on the armrest's nose, the easiest to see
// (27 degrees left), and behind it over the armrest's middle (55 degrees) one
// frame holding health above ammunition, two slim strips: those read fine
// narrow. From the seat nothing overlaps. The gunner rarely reads them; the
// station is laid out for its look.
Panel StackRow(const Panel& frame,float h,float y,unsigned source) {Panel p=frame;p.c=Point(frame,0,y,0);p.h=h;p.source=source;return p;}
const Panel titanStack=FacingPilot({-.56f,-.03f,.30f},.22f,.194f,1);
const Panel titanPanels[]={
 FacingPilot({-.46f,-.05f,.80f},.22f,.14f,0),    // radar, on the armrest's nose
 StackRow(titanStack,.090f,.052f,1),             // health, upper strip
 StackRow(titanStack,.090f,-.052f,2),            // ammunition, lower strip
};
// Octagonal station outline, inset from the lining by the frame's own reach.
std::array<P,8> ProteusRing(float z,float inset) {
    const float w=ProteusInner(z)-inset,f=ProteusPan(z)+inset+.030f,r=ProteusRoof(z)-inset;
    const float c=std::min(.16f,std::min(w,r-f)*.28f);
    return {{{-w+c,f,z},{w-c,f,z},{w,f+c,z},{w,r-c,z},{w-c,r,z},{-w+c,r,z},{-w,r-c,z},{-w,f+c,z}}};
}
// Rotate a finished part about a transverse axis. The reclined seat is the
// SAME shared cabin seat, tipped on its hip line, not a second seat model.
void TurnPart(std::vector<CockpitVertex>& v,std::size_t start,std::size_t end,P pivot,float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    for(auto i=start;i<end;++i) {
        auto& vert=v[i];
        const float y=vert.position[1]-pivot[1],z=vert.position[2]-pivot[2];
        vert.position[1]=pivot[1]+y*c+z*s;vert.position[2]=pivot[2]-y*s+z*c;
        const float ny=vert.normal[1],nz=vert.normal[2];
        vert.normal[1]=ny*c+nz*s;vert.normal[2]=-ny*s+nz*c;
    }
}
// One transverse station: a closed octagonal hoop with real corner gussets.
void ProteusStation(std::vector<CockpitVertex>& v,float z,float radius) {
    const auto ring=ProteusRing(z,.040f);
    for(unsigned i=0;i<8;++i)Beam(v,ring[i],ring[(i+1)%8],radius,Steel);
    for(unsigned i=0;i<8;++i)FrameJoint(v,ring[i],ring[(i+7)%8],ring[(i+1)%8],radius);
}
// Centre stick: the user's reference HOTAS, traced. The shape comes straight
// from src/cockpit_proteus_stick.inc, which tools/extract_proteus_stick.py
// welds, triangulates and decimates out of
// References/Cockpit/low_poly_hands_on_throttle_and_stick_for_vr.glb.
// Scaled so the grip head is 65mm across and 93mm deep - a real hand - which
// makes the whole unit .243m tall, so it stands on its own console between the
// knees instead of floating over the floor ramp.
#include "cockpit_proteus_stick.inc"
template<std::size_t N>
void StickPart(std::vector<CockpitVertex>& v,const float(&mesh)[N][3],P origin,P side,P up,P fwd,float scale,unsigned material,bool mirrored=true) {
    const auto start=v.size();
    auto point=[&](const float* q){return Add(origin,Add(Mul(side,q[0]*scale),Add(Mul(up,q[1]*scale),Mul(fwd,q[2]*scale))));};
    // Mirrored across the centreline, so the winding is reversed with it.
    for(std::size_t i=0;i<N;i+=3){
        if(mirrored)Triangle(v,point(mesh[i]),point(mesh[i+2]),point(mesh[i+1]),material);
        else Triangle(v,point(mesh[i]),point(mesh[i+1]),point(mesh[i+2]),material);
    }
    SmoothReference(v,start);
}
// The pedestal stands clear ahead of the seat pan, which is where a centre
// stick belongs: the first fit had the column rising through the cushion.
// The deck it stands on is a parameter, so the tandem cabin's flat floor and
// the gun pod's climbing ramp both carry the same unit.
// keel: the loom runs into the Proteus cabin's keel duct ahead of the stick;
// without one (the helicopters) it goes down into the deck.
void ProteusStickOn(std::vector<CockpitVertex>& v,float front,float back,float(*deck)(float),float plateY,bool keel=true) {
    constexpr float scale=.243f;
    const float half=.058f;
    auto ProteusPan=[deck](float z){return deck(z);};
    // The reference grip is a left-hand unit; mirror it for the right hand.
    const P side{-1,0,0},axis{0,.9397f,-.3420f},fwd{0,.3420f,.9397f};
    const P plate{0,plateY,(front+back)*.5f-.003f};   // underside of the traced base plate
    // Console: a wedge that meets the climbing floor square and carries the
    // gimbal at hand height. Bolted covers on both cheeks, cable gland below.
    RoundedPrism(v,{{-half,ProteusPan(front),front},{-half,ProteusPan(back),back},
        {-half,plate[1],back},{-half,plate[1],front}},{2*half,0,0},.008f,Dark);
    for(float s:{-1.f,1.f}) {
        // right follows s, or the cover plate grows into the console on one side.
        Panel cheek{{s*(half+.001f),(plate[1]+ProteusPan((front+back)*.5f))*.5f,(front+back)*.5f},{0,0,s},{0,1,0},0,0,0};
        GripBody(v,cheek,.085f,.330f,.006f,Bezel,.004f);
        for(float a:{-.026f,.026f})for(float b:{-.125f,0.f,.125f})FrameBolt(v,Point(cheek,a,b,.0125f),{s,0,0},.004f);
    }
    // Loom out of the pedestal and into the keel duct just ahead of it. That
    // run is what the keel is hollow for, and where the stick's signals go.
    Panel gland{{0,ProteusPan(back)+.030f,back+.004f},{1,0,0},{0,1,0},0,0,0};
    Part(v,referenceConnector,gland,.075f,.060f,.020f,Steel);
    Beam(v,{0,ProteusPan(back)+.030f,back+.012f},{0,ProteusPan(back+.055f)+.050f,back+.055f},.014f,Cable);
    Rod(v,{0,ProteusPan(back+.055f)+.056f,back+.055f},{0,ProteusPan(back+.055f)+(keel?.026f:-.005f),back+.055f},.024f,Steel);
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
void ProteusStick(std::vector<CockpitVertex>& v) {
    ProteusStickOn(v,kProteusStickAft,kProteusStickFore,ProteusPan,-.4035f);
}
// A serviceable equipment box: a bolted, hinged, latched cover on a case, not a
// blank block. Every box in this cabin is one of these.
void ProteusCase(std::vector<CockpitVertex>& v,const Panel& p,float w,float h,float depth,bool plate) {
    GripBody(v,p,w,h,depth,Dark,.008f);
    // GripBody runs from -thick to +.015 about the panel, so the case face is
    // always at +.015 whatever its depth. The lid sits 3mm into that face:
    // flush would be two coplanar surfaces, and it flickers.
    auto lid=p;lid.c=Point(p,0,0,.024f);
    GripBody(v,lid,w-.020f,h-.020f,.012f,Bezel,.005f);
    const auto normal=Mul(Unit(Cross(p.right,p.up)),-1);
    // The lid is an octagon with corners cut 18% of its short side: the corner
    // bolts stay inside that chamfer or they hang over the case face.
    const float cut=std::min(w-.020f,h-.020f)*.18f;
    const float insetX=w*.5f-.026f,insetY=h*.5f-std::max(.026f,cut-.008f);
    for(float x:{-insetX,insetX})for(float y:{-insetY,insetY})FrameBolt(v,Point(lid,x,y,.0155f),normal,.005f);
    if(h>.30f)for(float x:{-insetX,insetX})FrameBolt(v,Point(lid,x,0,.0155f),normal,.005f);
    // Hinge knuckles along one long edge, captive latches on the other. The
    // knuckles straddle the case's edge and the lid's, set into both.
    for(float y:{-h*.28f,h*.28f}) {
        Rod(v,Point(p,-w*.5f+.004f,y-.022f,.019f),Point(p,-w*.5f+.004f,y+.022f,.019f),.009f,Steel);
        auto latch=lid;latch.c=Point(lid,w*.5f-.004f,y,.014f);
        GripBody(v,latch,.024f,.042f,.010f,Steel,.003f);
        Beam(v,Point(latch,0,-.012f,.014f),Point(latch,0,.012f,.014f),.005f,Dark);
    }
    if(plate) {
        auto label=lid;label.c=Point(lid,0,h*.5f-.062f,.014f);
        Part(v,referenceServicePlate,label,w*.46f,.050f,.010f,Steel);
    }
}
// Short service passage aft of the seat, ending at a closed pressure hatch.
// The pod's own tunnel really does run on toward the shoulder, so this is the
// honest way to close the cabin: a way through, not a blank plate.
void ProteusRearPassage(std::vector<CockpitVertex>& v,bool titan) {
    constexpr float face=-.58f,end=-1.10f,w=.315f;
    constexpr float floorY=-1.00f,roofY=.35f;  // raised deck through the passage
    // Where the passage shell lands on the end wall. The wall's own front
    // face is at end-.05; the shell used to stop at the hatch instead, so a
    // slot ran right around the section above and below the door.
    constexpr float shut=end-.05f;
    Panel wall{{0,-.22f,face},{-1,0,0},{0,1,0},0,0,0};
    for(float s:{-1.f,1.f}) {
        // Out to the hull skin and down past the floor: these cheeks are what
        // keep daylight out of the corners beside the hatchway.
        // The wall panel faces aft (right is -X): its s=-1 side is the pilot's right.
        const bool narrow=titan&&s<0;
        const std::array<std::array<float,2>,8> cheek=narrow?
            std::array<std::array<float,2>,8>{{{-.34f,-1.23f},{-.40f,-1.235f},{-kTitanRight-.004f,-1.20f},{-kTitanRight-.004f,.85f},
            {-.40f,.95f},{-.35f,.95f},{-.33f,.80f},{-.33f,-1.19f}}}:
            std::array<std::array<float,2>,8>{{{s*.34f,-1.23f},{s*.62f,-1.235f},{s*.726f,-1.15f},{s*.726f,.82f},
            {s*.60f,.95f},{s*.35f,.95f},{s*.33f,.80f},{s*.33f,-1.19f}}};
        Armour(v,wall,cheek,.070f,Hull,.012f);
        if(narrow)continue;   // no room for the equipment box on the narrow side
        auto box=wall;box.c=Point(wall,s*.50f,-.06f,.014f);
        ProteusCase(v,box,.24f,.52f,.060f,true);
        auto vent=box;vent.c=Point(box,0,.16f,.030f);
        Part(v,referenceVent,vent,.16f,.13f,.018f,Steel);
        auto plug=box;plug.c=Point(box,0,-.20f,.032f);
        Part(v,referenceConnector,plug,.10f,.08f,.022f,Steel);
    }
    const std::array<std::array<float,2>,8> sill{{{-.30f,-1.22f},{.30f,-1.22f},{.37f,-1.15f},{.37f,-.80f},
        {.30f,-.73f},{-.30f,-.73f},{-.37f,-.80f},{-.37f,-1.15f}}};
    Armour(v,wall,sill,.070f,Hull,.010f);
    const std::array<std::array<float,2>,8> header{{{-.30f,.54f},{.30f,.54f},{.37f,.60f},{.37f,.88f},
        {.30f,.95f},{-.30f,.95f},{-.37f,.88f},{-.37f,.60f}}};
    Armour(v,wall,header,.070f,Hull,.010f);
    // The doorway frame is one mitred rectangle: four members on shared
    // corners, each bend carrying a bolted gusset. Crossed tube ends left the
    // bends bare, which is what the gunner saw from the seat.
    const std::array<P,4> jamb{{{-.348f,-1.02f,face},{.348f,-1.02f,face},{.348f,.29f,face},{-.348f,.29f,face}}};
    for(unsigned i=0;i<4;++i)Beam(v,jamb[i],jamb[(i+1)%4],.024f,Steel);
    for(unsigned i=0;i<4;++i)FrameJoint(v,jamb[i],jamb[(i+3)%4],jamb[(i+1)%4],.024f);
    // Passage walls, floor and roof, from the hatchway to the closed door.
    for(float s:{-1.f,1.f})
        Slab(v,{s*w,floorY,face},{s*w,roofY,face},{s*w,roofY,shut},{s*w,floorY,shut},{s*.030f,0,0},Hull);
    const float lap=w+.030f;   // the deck and roof lap over the side plates
    Slab(v,{-lap,floorY,face},{lap,floorY,face},{lap,floorY,shut},{-lap,floorY,shut},{0,-.030f,0},Steel);
    Slab(v,{-lap,roofY,face},{lap,roofY,face},{lap,roofY,shut},{-lap,roofY,shut},{0,.030f,0},Hull);
    for(float s:{-1.f,1.f}) {
        // Pipework the passage exists to show: runs, clamps, a valve, a gauge.
        const float turn=end+.20f;   // where the runs leave, well clear of the door
        for(unsigned k=0;k<4;++k) {
            const float y=-.66f+k*.20f,r=k==1?.030f:k==3?.020f:.024f;
            Rod(v,{s*(w-.055f),y,face-.02f},{s*(w-.055f),y,turn},r,k==2?Cable:Bezel);
            // Every run turns into the hull ahead of the door through an elbow
            // and a bolted wall flange. Nothing crosses the hatch.
            Panel bend{{s*(w-.055f),y,turn},{1,0,0},{0,1,0},0,0,0};
            GripBody(v,bend,r*2.5f,r*2.5f,r*2.5f,Dark,.004f);
            Rod(v,{s*(w-.055f),y,turn},{s*(w-.008f),y,turn},r*.85f,k==2?Cable:Bezel);
            Panel through{{s*(w-.004f),y,turn},{0,0,1},{0,1,0},0,0,0};
            GripBody(v,through,.074f,.074f,.014f,Dark,.004f);
            for(float t:{-.026f,.026f})FrameBolt(v,Point(through,t,-.026f,-s*.0085f),{-s,0,0},.004f);
        }
        for(float z:{face-.14f,turn+.10f}) {   // both clamps land ON the runs
            Panel clamp{{s*(w-.026f),-.36f,z},{0,0,1},{0,1,0},0,0,0};
            GripBody(v,clamp,.090f,.660f,.022f,Dark,.004f);
            for(float y:{-.29f,.29f})FrameBolt(v,Point(clamp,0,y,.0135f),{s,0,0},.004f);
        }
    }
    // Cable tray bolted up against the passage roof, the loom lying in it, and
    // the lamp hung under the tray on two brackets with its end fittings.
    const float mid=(face+end)*.5f;
    Panel tray{{0,roofY-.014f,mid},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,tray,.180f,.430f,.030f,Dark,.005f);
    for(float z:{mid-.170f,mid+.170f})FrameBolt(v,{0,roofY-.046f,z},{0,-1,0},.005f);   // on the tray's underside
    for(float x:{-.045f,.045f}) {
        Rod(v,{x,roofY-.044f,face-.02f},{x,roofY-.044f,end+.205f},.020f,Cable);
        Panel land{{x,roofY-.044f,end+.205f},{1,0,0},{0,1,0},0,0,0};
        GripBody(v,land,.072f,.072f,.018f,Dark,.004f);
    }
    for(float d:{-.074f,.074f}) {
        Rod(v,{0,roofY-.026f,mid+d},{0,roofY-.062f,mid+d},.010f,Dark);
        Rod(v,{0,roofY-.066f,mid+d-.012f},{0,roofY-.066f,mid+d+.012f},.030f,Dark);
    }
    Rod(v,{0,roofY-.066f,mid-.074f},{0,roofY-.066f,mid+.074f},.024f,Amber); // passage lamp
    // The closed hatch: dogged shut, with its wheel, hinges and warning stripe.
    Panel door{{0,-.33f,end},{-1,0,0},{0,1,0},0,0,0};
    // The leaf fills the passage: a door someone can actually walk through,
    // 610mm by 1260mm, with the frame closing the section around it.
    const std::array<std::array<float,2>,8> leaf{{{-.26f,-.63f},{.26f,-.63f},{.305f,-.57f},{.305f,.57f},
        {.26f,.63f},{-.26f,.63f},{-.305f,.57f},{-.305f,-.57f}}};
    // The leaf is dark: it is the one thing in here that moves.
    Armour(v,door,leaf,.055f,Dark,.014f);
    // Two flat stiffeners, and nothing else standing off the face. The
    // stamped plate that used to cover it threw tabs and chevrons of its own
    // up to 85mm proud, and they read as parts stuck on the door.
    for(float x:{-.078f,.078f})
        Beam(v,Point(door,x,-.54f,.020f),Point(door,x,.54f,.020f),.014f,Dark);
    Rod(v,Point(door,0,0,.020f),Point(door,0,0,.052f),.028f,Steel);
    for(int k=0;k<4;++k) {
        const float a=1.5707963f*k+.7853981f;
        Beam(v,Point(door,.026f*std::cos(a),.026f*std::sin(a),.050f),
               Point(door,.105f*std::cos(a),.105f*std::sin(a),.050f),.013f,Dark);
        Rod(v,Point(door,.105f*std::cos(a),.105f*std::sin(a),.040f),
              Point(door,.105f*std::cos(a),.105f*std::sin(a),.058f),.017f,Steel);
    }
    for(float y:{-.44f,0.f,.44f}) {
        Rod(v,Point(door,-.320f,y,-.010f),Point(door,-.320f,y,.028f),.024f,Dark);
        FrameBolt(v,Point(door,-.320f,y,.032f),{0,0,1},.006f);
    }
    Beam(v,Point(door,-.22f,-.67f,.012f),Point(door,.22f,-.67f,.012f),.014f,Warning);
    // The end wall covers the WHOLE passage section, or the corners around
    // the hatch leak daylight from the pod tunnel behind it.
    Panel frame{{0,-.33f,end-.06f},{-1,0,0},{0,1,0},0,0,0};
    const std::array<std::array<float,2>,8> surround{{{-.30f,-.76f},{.30f,-.76f},{.38f,-.68f},{.38f,.68f},
        {.30f,.76f},{-.30f,.76f},{-.38f,.68f},{-.38f,-.68f}}};
    Armour(v,frame,surround,.060f,Hull,.010f);
}
// Seams on the OUTER skin, where the live world image sits. Two continuous
// bands and four short side joints; nothing crosses the forward sight line,
// and the roof slot is left clear so the weapon stays readable.
void ProteusMainMonitorSeams(std::vector<CockpitVertex>& v,bool titan) {
    const float x=ProteusInner(0)+.030f,xr=PodSide(1,ProteusInner(0),titan)+.030f,nr=PodSide(1,.60f,titan);
    auto low=[](float z){return ProteusPan(z)+.030f;};
    auto high=[](float z){return ProteusRoof(z)-.055f;};
    MonitorSeamPath(v,std::array<P,11>{{{-x,low(-.60f),-.60f},{-x,low(.36f),.36f},{-x,low(.96f),.96f},
        {-.60f,low(1.20f),1.20f},{-.38f,low(1.34f),1.34f},{0,low(1.44f)+.010f,1.44f},
        {.38f,low(1.34f),1.34f},{nr,low(1.20f),1.20f},
        {xr,low(.96f),.96f},{xr,low(.36f),.36f},{xr,low(-.60f),-.60f}}});
    MonitorSeamPath(v,std::array<P,11>{{{-x,high(-.60f),-.60f},{-x,high(.36f),.36f},{-x,high(.96f),.96f},
        {-.60f,high(1.20f),1.20f},{-.38f,high(1.34f),1.34f},{0,high(1.44f)-.005f,1.44f},
        {.38f,high(1.34f),1.34f},{nr,high(1.20f),1.20f},
        {xr,high(.96f),.96f},{xr,high(.36f),.36f},{xr,high(-.60f),-.60f}}});
    for(float s:{-1.f,1.f})for(float z:{0.f,.86f})
        MonitorSeam(v,{s>0?xr:-x,high(z),z},{s>0?xr:-x,low(z),z},{0,0,1},{s*.014f,0,0});
    // The top is screen as well now. Two runs along it, clear of the sight
    // line forward and of the weapon overhead.
    auto roof=[](float z){return ProteusDeck(z)-.012f;};
    for(float s:{-1.f,1.f})
        MonitorSeamPath(v,std::array<P,5>{{{s*PodSide(s,.560f,titan)*(titan&&s>0?.90f:1.f),roof(titan?.44f:.16f),titan?.44f:.16f},{s*PodSide(s,.520f,titan)*(titan&&s>0?.90f:1.f),roof(.60f),.60f},
            {s*PodSide(s,.470f,titan)*(titan&&s>0?.90f:1.f),roof(1.00f),1.00f},{s*.380f*(titan&&s>0?.90f:1.f),roof(1.22f),1.22f},{s*.270f,roof(1.32f),1.32f}}});
}
std::vector<CockpitVertex> ProteusInterior(bool titan) {
    std::vector<CockpitVertex> v;v.reserve(380000);
    constexpr float tail=-.58f,nose=1.22f;
    // No frame in here: as in the tandem cabin, the structure that carries
    // the screens is outside them. Three stations remain only as the places
    // where the keel is clamped and the floor plate is jointed.
    constexpr float stations[3]={-.50f,.24f,1.14f};
    // Centre keel: the spine of the floor plate and the duct carrying the
    // stick's loom forward. It lies on the plate along its whole length, runs
    // UNDER every frame hoop with a saddle clamping the two together, and
    // butts into the stick pedestal. It used to stand tall enough to pass
    // straight through each hoop, which is what the gunner saw.
    auto keel=[](float z){return ProteusPan(z)+.016f;};
    Beam(v,{0,keel(tail),tail},{0,keel(kProteusStickAft-.006f),kProteusStickAft-.006f},.016f,Steel);
    Beam(v,{0,keel(kProteusStickFore+.006f),kProteusStickFore+.006f},{0,keel(nose),nose},.016f,Steel);
    for(const float z:stations) {
        Panel saddle{{0,ProteusPan(z)+.040f,z},{1,0,0},{0,1,0},0,0,0};
        GripBody(v,saddle,.072f,.064f,.030f,Dark,.004f);
        for(float x:{-.024f,.024f})FrameBolt(v,Point(saddle,x,-.018f,.0165f),{0,0,-1},.004f);
    }
    // Roof. The arm reaches the turret only through its own pivot, so nothing
    // structural belongs over the gunner: the top is the same screen as the
    // sides, and the weapon and the bay's underside are simply in view. The
    // one panel left is the aft bay, which hides the back of that pivot.
    // On the Titan the cover runs forward over the head: the sub-cannon's own
    // base sits right above it and is what it hides.
    const float cover=titan?.40f:-.20f;   // just the aft bay; the head has open sky over it
    {
        const float wa=ProteusSkin(tail)-.004f,wb=ProteusSkin(cover)-.004f;
        const float wra=PodSide(1,ProteusInner(tail),titan)==ProteusInner(tail)?wa:kTitanRight+.004f;
        const float wrb=PodSide(1,ProteusInner(cover),titan)==ProteusInner(cover)?wb:kTitanRight+.004f;
        const float ya=ProteusRoof(tail),yb=ProteusRoof(cover);
        Slab(v,{-wa,ya,tail},{wra,ya,tail},{wrb,yb,cover},{-wb,yb,cover},{0,.026f,0},Hull);
        // Its leading edge is a real folded rail, not a cut line.
        Beam(v,{-wb+.026f,yb-.024f,cover},{wrb-.026f,yb-.024f,cover},.022f,Bezel);
        for(float s:{-1.f,1.f})Beam(v,{s*((s>0?wrb:wb)-.026f),yb-.024f,cover},{s*((s>0?wra:wa)-.026f),ya-.024f,tail},.018f,Bezel);
    }
    ProteusRearPassage(v,titan);
    // Forward screen frame: the floor plate turns up into the sill across the
    // nose exactly as it does along the sides.
    {
        const float wn=ProteusInner(nose),wr=PodSide(1,wn,titan),pan=ProteusPan(nose);
        Slab(v,{-wn,pan+.028f,nose},{wr,pan+.028f,nose},{wr,pan-.012f,nose},{-wn,pan-.012f,nose},{0,0,-.014f},Hull);
        Beam(v,{-wn+.007f,pan+.028f,nose},{wr-.007f,pan+.028f,nose},.017f,Bezel);
    }
    // Floor pan on the real climbing plane, carried out to the lining on both
    // sides. It used to stop 70mm short of it and the gunner could see the
    // ground down that slot. At the edge the plate turns up into the sill,
    // which is the bottom frame of the outer screen.
    for(unsigned bay=0;bay<3;++bay) {
        const float a=bay?stations[bay]:tail,b=bay<2?stations[bay+1]:nose;
        const float la=ProteusInner(a),lb=ProteusInner(b),ra=PodSide(1,la,titan),rb=PodSide(1,lb,titan);
        Slab(v,{-la,ProteusPan(a),a},{ra,ProteusPan(a),a},{rb,ProteusPan(b),b},{-lb,ProteusPan(b),b},{0,-.046f,0},Steel);
        for(float s:{-1.f,1.f}) {
            const float wa=s>0?ra:la,wb=s>0?rb:lb;
            // The riser starts below the plate's own surface: butted exactly on
            // it, the shared edge is a crack a grazing ray can still find.
            Slab(v,{s*wa,ProteusPan(a)-.012f,a},{s*wa,ProteusPan(a)+.028f,a},
                {s*wb,ProteusPan(b)+.028f,b},{s*wb,ProteusPan(b)-.012f,b},{s*.014f,0,0},Hull);
            Beam(v,{s*(wa-.007f),ProteusPan(a)+.028f,a},{s*(wb-.007f),ProteusPan(b)+.028f,b},.017f,Bezel);
        }
    }
    for(float s:{-1.f,1.f}) {
        Beam(v,{s*.30f,ProteusPan(tail)+.006f,tail},{s*.30f,ProteusPan(nose)+.006f,nose},.022f,Dark);
        if(!(titan&&s>0))Beam(v,{s*.55f,ProteusPan(tail)+.006f,tail},{s*.55f,ProteusPan(nose)+.006f,nose},.020f,Dark);
    }
    // Sump cover, bolted into the floor plate beside the seat rails. Off the
    // centreline, because the keel runs there.
    // Lying in the floor plate, so it follows the ramp instead of sitting level.
    Panel drain{{titan?-.42f:.42f,ProteusPan(-.20f)+.004f,-.20f},{1,0,0},{0,.4718f,.8817f},0,0,0};
    Part(v,referenceServicePlate,drain,.20f,.16f,.016f,Steel);
    for(float x:{-.082f,.082f})for(float z:{-.062f,.062f})FrameBolt(v,Point(drain,x,z,.013f),{0,1,0},.004f);
    // Seat. The pan is tipped only 3 degrees - all but flat, near enough the
    // floor ramp ahead of it - and the backrest takes the other 21, so it still
    // lies 42 degrees off vertical. The pilot sits 50mm higher and 60mm further
    // back in the pod than the first fit, which is what makes a flat pan fit
    // over the climbing floor. The occupant turns with the backrest, so the
    // placement solves for the shared .76m hip-to-eye line landing exactly on
    // kCockpitSeatedEye: hip ends up at (0,-.594,.209).
    const auto seat=v.size();std::size_t pan=0;
    Seat(v,true,&pan);
    TurnPart(v,seat,pan,{0,-.66f,-.10f},.3665191f);
    TurnPart(v,seat,v.size(),{0,-.66f,-.10f},.0523599f);
    // 30mm further aft than the hip-to-eye solution alone would put it, so the
    // pan's front lip clears the stick pedestal standing ahead of it.
    MovePart(v,seat,{1,1,1},{0,.0657f,.2791f});
    auto panUnder=[](float z){return -.7543f+.05247f*(z-.0171f);};
    for(float s:{-1.f,1.f}) {
        const float x=s*.205f;
        RoundedPrism(v,{{x-.030f,ProteusPan(-.02f),-.02f},{x-.030f,ProteusPan(.47f),.47f},
            {x-.030f,panUnder(.47f)+.004f,.47f},{x-.030f,panUnder(-.02f)+.004f,-.02f}},{.060f,0,0},.006f,Dark);
        for(float z:{.06f,.22f,.40f})
            FrameBolt(v,{x+s*.031f,(ProteusPan(z)+panUnder(z))*.5f,z},{s,0,0},.005f);
        Beam(v,{x,panUnder(.01f)+.012f,.01f},{x,panUnder(.44f)+.012f,.44f},.016f,Steel);
        // Nothing else stands beside the head. The harness reels that used to
        // sit here read as a box floating next to the gunner's ear.
    }
    ProteusStick(v);
    // Side consoles: serviceable equipment cases at elbow height, carrying the
    // three displays. Bolted, hinged, latched lids - not decorative blocks.
    for(float s:{-1.f,1.f}) {
        if(titan&&s>0)continue;   // the Titan's gunner has only the left armrest
        Panel lid{{s*.495f,-.268f,.44f},{1,0,0},{0,.106f,.9944f},0,0,0};
        Panel body=lid;body.c=Point(lid,0,0,-.010f);
        ProteusCase(v,body,.150f,.80f,.085f,true);
        // Three switches in one block, in line BEHIND the yellow one: the
        // two that ran forward sat under the screen's stalk, out of reach.
        // Set 3 mm into the lid: butted on it, the two faces flicker. The
        // Titan's armrest carries its screens instead.
        if(!titan) {
            auto sw=lid;sw.c=Point(lid,0,-.105f,.049f);
            GripBody(v,sw,.048f,.230f,.014f,Dark,.003f);
            for(int k=0;k<3;++k)Rod(v,Point(sw,0,.085f-k*.085f,.013f),Point(sw,0,.085f-k*.085f,.026f),.010f,k?Steel:Warning);
        }
        // Two fixings: a bracket through the side screen to the frame that
        // runs outside it, and one leg under its forward end. Both are made as
        // joints - socketed gussets and bolted plates. Nothing hangs from the
        // roof and nothing reaches forward from the bulkhead behind.
        const float wall=ProteusInner(stations[1]);
        const float armY=Point(lid,0,(stations[1]-.44f)/.9944f,0)[1];
        const P wallFoot{s*(wall-.012f),armY,stations[1]},armFace{s*.568f,armY,stations[1]};
        Beam(v,wallFoot,armFace,.020f,Steel);
        FrameJoint(v,wallFoot,armFace,{wallFoot[0],armY+.10f,stations[1]},.020f);
        Panel armPad{{s*.574f,armY,stations[1]},{0,0,1},{0,1,0},0,0,0};
        GripBody(v,armPad,.100f,.110f,.014f,Dark,.004f);
        for(float t:{-.032f,.032f})for(float u:{-.036f,.036f})
            FrameBolt(v,Point(armPad,t,u,s*.0085f),{s,0,0},.004f);
        // The wall plate: the bracket's foot on the screen, bolted through to
        // the frame behind it. Set 5 mm inside the screen line so the plate
        // and the screen are never coplanar.
        Panel frameSeat{{s*(wall-.020f),armY,stations[1]},{0,0,1},{0,1,0},0,0,0};
        GripBody(v,frameSeat,.086f,.120f,.014f,Dark,.004f);
        for(float u:{-.040f,.040f})FrameBolt(v,Point(frameSeat,0,u,s>0?-.0155f:.0165f),{-s,0,0},.004f);
        // The forward leg: a pad under the armrest's front end, a post, and a
        // bolted shoe on the floor plate clear of the seat rails.
        constexpr float legTop=.396f;
        const Panel legPad{Point(lid,0,legTop,-.050f),{1,0,0},{0,-.106f,-.9944f},0,0,0};
        GripBody(v,legPad,.092f,.092f,.016f,Dark,.004f);
        for(float t:{-.030f,.030f})FrameBolt(v,Point(legPad,t,.030f,.0115f),{0,-.9944f,.106f},.004f);
        const auto legHead=Point(legPad,0,0,.008f);
        const float legZ=legHead[2];
        const P legFoot{s*.470f,ProteusPan(legZ)+.004f,legZ};
        Beam(v,legHead,legFoot,.019f,Steel);
        Axle(v,legHead,.052f,.022f,Steel);            // pinned head under the pad
        Panel legShoe{{s*.470f,ProteusPan(legZ),legZ},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,legShoe,.078f,.092f,.016f,Dark,.003f);
        for(float t:{-.026f,.026f})FrameBolt(v,Point(legShoe,t,.030f,.0155f),{0,1,0},.004f);
        FrameJoint(v,legFoot,legHead,{s*.470f,ProteusPan(legZ)+.004f,legZ-.10f},.019f);
        // The armrest's own loom comes down the wall under the bracket's plate
        // and enters underneath the armrest through a gland.
        const P tap{s*(wall-.030f),armY-.150f,stations[1]};
        Panel clip{tap,{0,0,-s},{0,1,0},0,0,0};
        GripBody(v,clip,.050f,.060f,.016f,Dark,.003f);
        const auto gland=Point(lid,0,(stations[1]-.44f)/.9944f,-.052f);
        Beam(v,tap,gland,.016f,Cable);
        const Panel plug{Point(lid,0,(stations[1]-.44f)/.9944f,-.058f),{1,0,0},{0,-.106f,-.9944f},0,0,0};
        GripBody(v,plug,.062f,.062f,.022f,Dark,.004f);
    }
    // Foot boards, laid the same way as the floor ramp in front of them: the
    // sole rests on a 33 degree slope, it is not a wall to kick. The first fit
    // had this tilted the other way and there was nowhere to put a foot.
    for(float s:{-1.f,1.f}) {
        const float z=.95f;
        Panel board{{s*.215f,-.490f,z},{1,0,0},{0,.5446f,.8387f},0,0,0};
        GripBody(v,board,.185f,.270f,.026f,Steel,.005f);
        auto tread=board;tread.c=Point(board,0,0,.016f);
        GripBody(v,tread,.150f,.230f,.008f,Rubber,.003f);
        for(int k=0;k<5;++k)Beam(v,Point(tread,-.060f,-.076f+k*.038f,.017f),Point(tread,.060f,-.076f+k*.038f,.017f),.0032f,Bezel);
        Beam(v,Point(board,-.076f,-.134f,.020f),Point(board,.076f,-.134f,.020f),.012f,Dark);
        Beam(v,Point(board,0,.100f,.018f),Point(board,0,.152f,.036f),.010f,Rubber);
        for(float x:{-.066f,.066f})FrameBolt(v,Point(board,x,-.108f,.0165f),Mul(Unit(Cross(board.right,board.up)),-1),.004f);
        // Stays at both ends of the board, each one starting in a pad on the
        // board's own underside and landing on a bolted shoe on the floor.
        // They used to begin 17mm clear of the board and only held its heel.
        // One stay at each end, on the board's centreline: a pair at each end
        // put the floor shoes into the seat rails beside them.
        for(float end:{-.118f,.112f}) {
            constexpr float x=0;
            // The pad stands proud of the board's underside: flush, the two
            // bottom faces are coplanar and flicker. The stay enters the pad.
            auto pad=board;pad.c=Point(board,x,end,-.020f);
            GripBody(v,pad,.052f,.048f,.014f,Dark,.003f);
            const auto heel=Point(board,x,end,-.030f);
            const float foot=z+(end<0?-.13f:.13f);
            const P shoeFoot{s*.215f+x,ProteusPan(foot),foot};
            Rod(v,heel,shoeFoot,.014f,Steel);
            Panel shoe{shoeFoot,{1,0,0},{0,0,1},0,0,0};
            GripBody(v,shoe,.068f,.076f,.014f,Dark,.003f);
            FrameBolt(v,Point(shoe,0,0,.0155f),{0,1,0},.004f);
        }
    }
    std::vector<Panel> mounts;
    if(titan)mounts={titanPanels[0],titanStack};else mounts.assign(std::begin(proteusPanels),std::end(proteusPanels));
    for(const auto& p:mounts) {
        Instrument(v,p);
        const float s=p.c[0]<0?-1.f:1.f;
        const auto hinge=Point(p,-s*(p.w*.5f+.012f),-p.h*.5f+.022f,-.055f);
        const Panel arm{{s*.495f,-.268f,.44f},{1,0,0},{0,.106f,.9944f},0,0,0};
        const P root=Point(arm,0,(p.c[2]-.44f)/.9944f,.0445f);   // on the lid, not over it
        Beam(v,hinge,root,.014f,Steel);
        auto shoe=p;shoe.c=Point(p,-s*(p.w*.5f+.012f),-p.h*.5f+.022f,-.042f);
        GripBody(v,shoe,.030f,.060f,.014f,Dark,.002f);
        Axle(v,root,.046f,.024f,Dark);
        FrameBolt(v,Add(root,{s*.026f,0,0}),{s,0,0},.005f);
    }
    if(titan) {
        // The stack's two strips share one frame; a bar across parts them.
        Beam(v,Point(titanStack,-titanStack.w*.5f,0,.008f),Point(titanStack,titanStack.w*.5f,0,.008f),.007f,Dark);
    }
    ProteusMainMonitorSeams(v,titan);
    return v;
}
}
