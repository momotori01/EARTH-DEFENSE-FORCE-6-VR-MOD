// Tank driver's cabin, one geometry for every tank: Blacker (V505), Varias
// (V601), EMC (V510), Epsilon railgun (403) and the Titan (404). The hulls,
// turrets and barrels were measured by research/cockpit/survey_tanks.py.
//
// The cabin sits in the hull a little left of the centreline with the pilot's
// shoulders up in the turret ring. Everything above the sill is turret, and
// the turret is screen: no mesh there, only the seam ink, and the native
// turret is hidden, so the hull deck and the barrel on its dish are in view.
// Everything below the sill is the inside of the hull and is closed on every
// side, dressed with the equipment a tank is full of rather than bare plate.
// The cabin is hull-fixed; the turret and its barrel turn around it.
//
// Cabin coordinates as every cabin: +X pilot right, +Y up, +Z forward, the
// seated eye at kCockpitSeatedEye. The station itself is the Proteus
// driver's - seat, foot rests on their axle, the console with twin sticks and
// embedded screens - and the back is Nix's rear machinery, not a hatch.
namespace {
constexpr float kTankDeck=-1.22f,kTankSill=-.20f,kTankBand=.36f,kTankRoof=.60f,kTankRoofInset=.22f;
// Plan octagon in (x,z), walking round the cabin.
const std::array<std::array<float,2>,8> kTankPlan{{{-.44f,1.30f},{-.80f,.92f},{-.80f,-.36f},{-.46f,-.72f},
    {.46f,-.72f},{.80f,-.36f},{.80f,.92f},{.44f,1.30f}}};
constexpr float kTankCentreZ=.29f;
P TankCorner(unsigned i,float y) {return {kTankPlan[i%8][0],y,kTankPlan[i%8][1]};}
P TankOutward(unsigned i) {
    const auto a=kTankPlan[i%8],b=kTankPlan[(i+1)%8];
    P n{b[1]-a[1],0,a[0]-b[0]};
    const P mid{(a[0]+b[0])*.5f,0,(a[1]+b[1])*.5f-kTankCentreZ};
    if(Dot(n,mid)<0)n=Mul(n,-1);
    return Unit(n);
}
// A panel lying on the inside face of wall i, at fraction t along it and
// height y, facing into the cabin.
Panel TankWallPanel(unsigned i,float t,float y,float inset=0) {
    const auto a=TankCorner(i,y),b=TankCorner(i+1,y);
    const auto out=TankOutward(i);
    const auto along=Unit(Sub(b,a));
    // right must make Cross(right,up) point OUT of the cabin, so the panel's
    // face (Point(...,forward>0)) is on the cabin side.
    P right=along;if(Dot(Cross(right,{0,1,0}),out)<0)right=Mul(right,-1);
    return {Add(Add(a,Mul(Sub(b,a),t)),Mul(out,-inset)),right,{0,1,0},0,0,0};
}
// A lidded box, stood on the wall on two brackets, with its loom.
void TankWallBox(std::vector<CockpitVertex>& v,unsigned wall,float t,float y,float w,float h,float depth) {
    auto face=TankWallPanel(wall,t,y,depth-.015f);
    ProteusCase(v,face,w,h,depth,true);
    const auto out=TankOutward(wall);
    for(float u:{-h*.30f,h*.30f}) {
        auto bracket=TankWallPanel(wall,t,y+u,.004f);
        GripBody(v,bracket,w+.06f,.035f,.012f,Dark,.003f);
        for(float x:{-(w*.5f+.018f),w*.5f+.018f})FrameBolt(v,Point(bracket,x,0,.0165f),Mul(out,-1),.004f);
    }
}
void TankDial(std::vector<CockpitVertex>& v,const Panel& p,float r) {
    GripBody(v,p,r*2.3f,r*2.3f,.030f,Dark,.004f);
    auto face=p;face.c=Point(p,0,0,.016f);
    Part(v,referenceGaugeBody,face,r*2,r*2,r*2,Steel);
    Part(v,referenceGaugeMarks,face,r*2,r*2,.025f,Amber);
}
// Hull side, pilot's left: the radio and fire-control rack, a loom along the
// wall into a junction box, and the fire extinguisher strapped by the knee.
void TankLeftWall(std::vector<CockpitVertex>& v) {
    constexpr unsigned wall=1;   // (-.80,.92) -> (-.80,-.36)
    const auto out=TankOutward(wall);
    TankWallBox(v,wall,.55f,-.46f,.40f,.30f,.16f);
    TankWallBox(v,wall,.55f,-.86f,.40f,.30f,.20f);
    {   // instruments on the upper box
        auto face=TankWallPanel(wall,.55f,-.46f,.16f-.015f);
        auto dial=face;dial.c=Point(face,-.10f,.02f,.024f+.015f);
        TankDial(v,dial,.045f);
        for(int j=0;j<3;++j) {
            auto knob=face;knob.c=Point(face,.04f+j*.055f,.05f,.039f);
            GripBody(v,knob,.040f,.040f,.014f,Dark,.003f);
            Rod(v,Point(knob,0,0,.013f),Point(knob,0,0,.028f),.010f,j?Steel:Warning);
        }
        for(int j=0;j<4;++j) {
            auto lamp=face;lamp.c=Point(face,-.12f+j*.07f,-.09f,.039f);
            GripBody(v,lamp,.050f,.026f,.012f,Dark,.003f);
            Face(v,lamp,.034f,.014f,j==0?Warning:Cyan,.017f);
        }
    }
    {   // loom along the wall above the rack, into a junction box
        const auto a=Add(TankCorner(wall,-.26f),Mul(out,-.035f)),b=Add(TankCorner(wall+1,-.26f),Mul(out,-.035f));
        for(float dy:{0.f,.026f})Rod(v,Add(Add(a,Mul(Sub(b,a),.05f)),{0,dy,0}),Add(Add(a,Mul(Sub(b,a),.95f)),{0,dy,0}),.011f,Cable);
        for(float t:{.15f,.40f,.70f,.90f}) {
            // The clip is a block the loom passes through, on the wall.
            auto clip=TankWallPanel(wall,t,-.247f,.050f);
            GripBody(v,clip,.040f,.070f,.050f,Dark,.003f);
            FrameBolt(v,Point(clip,0,.024f,.0165f),Mul(out,-1),.004f);
        }
        auto box=TankWallPanel(wall,.55f,-.26f,.07f-.015f);
        GripBody(v,box,.12f,.10f,.07f,Dark,.005f);
        auto lid=box;lid.c=Point(box,0,0,.009f);
        GripBody(v,lid,.10f,.08f,.012f,Bezel,.004f);
        for(float x:{-.036f,.036f})for(float y:{-.026f,.026f})FrameBolt(v,Point(lid,x,y,.0165f),Mul(out,-1),.004f);
        const auto drop=Point(box,0,-.05f,-.035f);
        Rod(v,drop,Add(drop,{0,-.08f,0}),.012f,Cable);
    }
    {   // extinguisher by the knee, in two straps
        const P base=Add(TankCorner(wall,-1.20f),Add(Mul(Unit(Sub(TankCorner(wall+1,0),TankCorner(wall,0))),.16f),Mul(out,-.10f)));
        Rod(v,base,Add(base,{0,.46f,0}),.065f,Warning);
        Rod(v,Add(base,{0,.46f,0}),Add(base,{0,.52f,0}),.030f,Steel);
        Rod(v,Add(base,{0,.52f,0}),Add(base,{0,.55f,0}),.018f,Dark);
        for(float y:{.12f,.34f}) {
            Rod(v,Add(base,{0,y-.012f,0}),Add(base,{0,y+.012f,0}),.070f,Dark);
            Beam(v,Add(base,Add({0,y,0},Mul(out,.068f))),Add(base,Add({0,y,0},Mul(out,.100f))),.012f,Dark);
        }
    }
}
// Hull side, pilot's right: the hydraulic manifold (three runs, a valve and a
// gauge on a manifold block) and a rack of stowed rounds in their cradle.
void TankRightWall(std::vector<CockpitVertex>& v,bool rounds=true) {
    constexpr unsigned wall=5;   // (.80,-.36) -> (.80,.92)
    const auto out=TankOutward(wall);
    const auto a=TankCorner(wall,0),b=TankCorner(wall+1,0);
    const auto along=Unit(Sub(b,a));
    auto at=[&](float t,float y,float inset){return Add(Add(Add(a,Mul(Sub(b,a),t)),{0,y,0}),Mul(out,-inset));};
    // Manifold block and its three runs.
    auto block=TankWallPanel(wall,.30f,-.58f,.10f-.015f);
    GripBody(v,block,.18f,.30f,.10f,Dark,.008f);
    for(float x:{-.070f,.070f})for(float y:{-.12f,.12f})FrameBolt(v,Point(block,x,y,.0165f),Mul(out,-1),.005f);
    for(unsigned k=0;k<3;++k) {
        const float y=-.48f-k*.10f,r=k==1?.022f:.018f;
        Rod(v,at(.02f,y,.06f),at(.26f,y,.06f),r,k==2?Cable:Bezel);
        Rod(v,at(.34f,y,.06f),at(.96f,y,.06f),r,k==2?Cable:Bezel);
        for(float t:{.14f,.62f,.90f}) {
            auto clamp=TankWallPanel(wall,t,y,.075f);
            GripBody(v,clamp,.040f,.060f,.075f,Dark,.004f);
        }
    }
    {   // valve with its handwheel on the top run, gauge on the block
        const auto stem=at(.50f,-.48f,.06f);
        Rod(v,stem,Add(stem,Mul(out,-.09f)),.014f,Steel);
        const auto hub=Add(stem,Mul(out,-.09f));
        Rod(v,Sub(hub,Mul(out,-.012f)),Add(hub,Mul(out,-.012f)),.020f,Dark);
        for(int k=0;k<5;++k) {
            const float t=6.2831853f*k/5;
            const auto spoke=Add(Mul(along,std::cos(t)),Mul(P{0,1,0},std::sin(t)));
            Beam(v,hub,Add(hub,Mul(spoke,.070f)),.006f,Warning);
        }
        for(int k=0;k<20;++k) {
            const float t0=6.2831853f*k/20,t1=6.2831853f*(k+1)/20;
            Beam(v,Add(hub,Mul(Add(Mul(along,std::cos(t0)),Mul(P{0,1,0},std::sin(t0))),.070f)),
                   Add(hub,Mul(Add(Mul(along,std::cos(t1)),Mul(P{0,1,0},std::sin(t1))),.070f)),.009f,Warning);
        }
        auto dial=block;dial.c=Point(block,0,.06f,.024f);
        TankDial(v,dial,.050f);
    }
    // Rounds: four, lying fore-and-aft in a bolted cradle low on the wall.
    if(!rounds)return;
    for(unsigned k=0;k<4;++k) {
        const float y=-1.08f+(k%2)*.13f,inset=.10f+(k/2)*.14f;
        const auto rear=at(.36f,y,inset),front=at(.92f,y,inset);
        Rod(v,rear,Add(rear,Mul(Sub(front,rear),.74f)),.058f,Bezel);
        Taper(v,Add(rear,Mul(Sub(front,rear),.74f)),front,.058f,.024f,Steel);
        Rod(v,Sub(rear,Mul(along,.012f)),Add(rear,Mul(along,.012f)),.062f,Amber);
    }
    for(float t:{.44f,.78f}) {
        const auto foot=at(t,-1.20f,.17f);
        Slab(v,Add(foot,Mul(out,.13f)),Add(foot,Mul(out,-.13f)),Add(foot,Add(Mul(out,-.13f),{0,.24f,0})),Add(foot,Add(Mul(out,.13f),{0,.24f,0})),
            Mul(along,.030f),Dark);
    }
}
// The front, behind and below the console: the hull's front bulkhead with a
// cable tray across it and a vent on each canted corner.
void TankFrontWall(std::vector<CockpitVertex>& v) {
    for(unsigned wall:{0u,6u}) {
        const auto out=TankOutward(wall);
        auto vent=TankWallPanel(wall,.5f,-.55f,.004f);
        GripBody(v,vent,.22f,.20f,.020f,Dark,.004f);
        auto grille=vent;grille.c=Point(vent,0,0,.016f);
        Part(v,referenceVent,grille,.19f,.17f,.018f,Steel);
        for(float x:{-.090f,.090f})for(float y:{-.080f,.080f})FrameBolt(v,Point(vent,x,y,.0165f),Mul(out,-1),.004f);
        auto plate=TankWallPanel(wall,.5f,-.95f,.004f);
        Part(v,referenceServicePlate,plate,.24f,.16f,.016f,Steel);
    }
    {   // cable tray across the front bulkhead, under the console
        const auto out=TankOutward(7);
        const auto a=Add(TankCorner(7,-.70f),Mul(out,-.06f)),b=Add(TankCorner(8,-.70f),Mul(out,-.06f));
        const auto along=Unit(Sub(b,a));
        const auto from=Add(a,Mul(along,.08f)),to=Sub(b,Mul(along,.08f));
        Slab(v,Add(from,{0,-.02f,0}),Add(to,{0,-.02f,0}),Add(to,{0,.02f,0}),Add(from,{0,.02f,0}),Mul(out,-.07f),Dark);
        for(float dx:{-.020f,0.f,.020f})
            Rod(v,Add(Add(from,{0,.030f,0}),Mul(out,-.035f+dx)),Add(Add(to,{0,.030f,0}),Mul(out,-.035f+dx)),.010f,Cable);
    }
}
// The floor: tread plate, rails, tie-down rings, a sump and a hatch.
void TankFloor(std::vector<CockpitVertex>& v) {
    // Deck: one closed plate under the whole plan, laid in the octagon.
    {
        std::vector<P> plan;for(unsigned i=0;i<8;++i)plan.push_back(TankCorner(i,kTankDeck));
        RoundedPrism(v,plan,{0,-.05f,0},.010f,Steel);
    }
    for(float x:{-.62f,-.30f,.30f,.62f})
        Beam(v,{x,kTankDeck+.008f,-.30f},{x,kTankDeck+.008f,x*x>.2f?.80f:1.08f},.016f,Dark);
    for(float z:{.30f,.62f})Beam(v,{-.70f,kTankDeck+.006f,z},{.70f,kTankDeck+.006f,z},.012f,Dark);
    for(float x:{-.50f,.50f})for(float z:{.05f,.80f}) {
        Panel ring{{x,kTankDeck+.004f,z},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,ring,.050f,.050f,.014f,Steel,.004f);
        Rod(v,Point(ring,0,0,.012f),Point(ring,0,0,.024f),.008f,Steel);
    }
    Panel hatch{{-.50f,kTankDeck+.004f,.46f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,hatch,.30f,.26f,.016f,Dark,.006f);
    auto lid=hatch;lid.c=Point(hatch,0,0,.004f);
    Part(v,referenceServicePlate,lid,.26f,.22f,.016f,Steel);
    Beam(v,Point(hatch,-.10f,-.13f,.020f),Point(hatch,.10f,-.13f,.020f),.008f,Warning);
    Panel sump{{.50f,kTankDeck+.004f,.46f},{1,0,0},{0,0,1},0,0,0};
    Part(v,referenceServicePlate,sump,.22f,.18f,.016f,Steel);
    for(float x:{-.090f,.090f})for(float z:{-.070f,.070f})FrameBolt(v,Point(sump,x,z,.013f),{0,1,0},.004f);
}
// The sill is the turret race: a heavy ring on top of the hull walls all the
// way round, which is also the bottom frame of the turret screens.
void TankRace(std::vector<CockpitVertex>& v) {
    for(unsigned i=0;i<8;++i) {
        const auto a=TankCorner(i,kTankSill),b=TankCorner(i+1,kTankSill);
        const auto out=TankOutward(i);
        Slab(v,Add(a,{0,-.06f,0}),Add(b,{0,-.06f,0}),b,a,Mul(out,-.070f),Bezel);
        Beam(v,Add(Add(a,Mul(out,-.074f)),{0,-.030f,0}),Add(Add(b,Mul(out,-.074f)),{0,-.030f,0}),.012f,Dark);
        const auto along=Unit(Sub(b,a));const float length=std::sqrt(Dot(Sub(b,a),Sub(b,a)));
        for(float d=.10f;d<length-.05f;d+=.16f)
            FrameBolt(v,Add(Add(a,Mul(along,d)),Add(Mul(out,-.036f),{0,.0015f,0})),{0,1,0},.005f);
    }
    for(unsigned i=0;i<8;++i) {   // a bracket under the race at every corner
        const auto c=TankCorner(i,kTankSill-.06f);
        const auto in=Mul(Unit(Add(TankOutward(i),TankOutward(i+7))),-1);
        Panel bracket{Add(c,Add(Mul(in,.040f),{0,-.08f,0})),Unit(Cross({0,1,0},in)),{0,1,0},0,0,0};
        if(Dot(Cross(bracket.right,bracket.up),in)>0)bracket.right=Mul(bracket.right,-1);
        GripBody(v,bracket,.07f,.16f,.050f,Dark,.004f);
        for(float y:{-.05f,.05f})FrameBolt(v,Point(bracket,0,y,.0165f),in,.004f);
    }
}
// Walls: the hull's inside from deck to sill, every face closed.
void TankWalls(std::vector<CockpitVertex>& v) {
    for(unsigned i=0;i<8;++i) {
        const auto out=TankOutward(i);
        const auto a=TankCorner(i,0),b=TankCorner(i+1,0);const auto along=Unit(Sub(b,a));
        // Each slab runs 5 cm past both corners, outside its neighbour, so the
        // corners are closed: meeting edge to edge left hairline cracks the
        // world showed through.
        // Every other slab's top is half a millimetre lower: where they
        // overlap at a corner their tops would lie in one plane and flicker.
        const auto a0=Sub(a,Mul(along,.05f)),b0=Add(b,Mul(along,.05f));const float top=kTankSill-(i%2?.0205f:.02f);
        Slab(v,Add(a0,{0,kTankDeck-.05f,0}),Add(b0,{0,kTankDeck-.05f,0}),Add(b0,{0,top,0}),Add(a0,{0,top,0}),Mul(out,.040f),Hull);
        // A horizontal stiffener on every face, stopped short of the corners.
        const float length=std::sqrt(Dot(Sub(b,a),Sub(b,a)));
        for(float y:{-.72f})if(length>.25f)
            Slab(v,Add(Add(a,Mul(along,.05f)),{0,y-.02f,0}),Add(Add(b,Mul(along,-.05f)),{0,y-.02f,0}),
                   Add(Add(b,Mul(along,-.05f)),{0,y+.02f,0}),Add(Add(a,Mul(along,.05f)),{0,y+.02f,0}),Mul(out,-.022f),Hull);
        (void)length;
    }
}
// Nix's rear machinery, on a base that reaches this deck: the sloping power
// enclosure behind the seat, the heat-exchanger spine with twin fans, and the
// deep cabinets flanking the backrest with their coolant accumulators.
void TankRearMachinery(std::vector<CockpitVertex>& v) {
    const auto power=GeneratorFront();
    const std::array<std::array<float,2>,8> body{{{-.27f,-.61f},{-.33f,-.49f},{-.33f,.19f},{-.27f,.31f},
        {.27f,.31f},{.33f,.19f},{.33f,-.49f},{.27f,-.61f}}};
    Armour(v,power,body,.18f,Hull,.013f);
    auto lower=power;lower.c=Point(power,0,-.46f,.016f);
    Part(v,referenceServicePlate,lower,.36f,.17f,.020f,Steel);
    // The enclosure's plinth: from under its lip to the deck.
    RoundedPrism(v,{{-.28f,kTankDeck+.004f,-.62f},{.28f,kTankDeck+.004f,-.62f},{.28f,kTankDeck+.004f,-.30f},{-.28f,kTankDeck+.004f,-.30f}},
        {0,-.95f-kTankDeck,0},.010f,Dark);
    Panel header{{0,.255f,-.50f},{-1,0,0},{0,1,0},0,0,0};
    GripBody(v,header,.62f,.25f,.075f,Hull);
    for(float s:{-1.f,1.f}) {
        auto fan=header;fan.c=Point(header,s*.145f,.017f,.018f);
        GripBody(v,fan,.21f,.18f,.020f,Dark);
        fan.c=Point(fan,0,0,.016f);Part(v,referenceVent,fan,.18f,.145f,.020f,Steel);
        Panel cabinet{{s*.414f,-.32f,-.435f},{-1,0,0},{0,1,0},0,0,0};
        const std::array<std::array<float,2>,8> shape{{{-.082f,-.89f},{-.112f,-.78f},{-.112f,.47f},{-.063f,.70f},
            {.063f,.70f},{.112f,.47f},{.112f,-.78f},{.082f,-.89f}}};
        Armour(v,cabinet,shape,.15f,Bezel,.013f);
        auto cooling=cabinet;cooling.c=Point(cabinet,0,.26f,.017f);
        GripBody(v,cooling,.17f,.30f,.018f,Dark);
        cooling.c=Point(cooling,0,0,.017f);Part(v,referenceVent,cooling,.14f,.25f,.020f,Steel);
        auto service=cabinet;service.c=Point(cabinet,0,-.095f,.017f);
        Part(v,referenceServicePlate,service,.17f,.235f,.022f,Steel);
        auto connector=cabinet;connector.c=Point(cabinet,0,-.39f,.017f);
        Part(v,referenceConnector,connector,.10f,.085f,.030f,Steel);
        for(float y:{-.80f,.46f})for(float x:{-.057f,.057f})
            FrameBolt(v,Point(cabinet,x,y,.0165f),{0,0,1},.005f);
        const P tank{s*.405f,.245f,-.382f};
        Rod(v,Add(tank,{0,-.105f,0}),Add(tank,{0,.105f,0}),.037f,Steel);
        for(float y:{-.068f,.068f}) {
            Rod(v,Add(tank,{0,y-.008f,0}),Add(tank,{0,y+.008f,0}),.041f,Dark);
            Beam(v,Add(tank,{0,y,-.015f}),Add(tank,{0,y,-.065f}),.021f,Dark);
        }
        const P elbow{s*.405f,.365f,-.382f},entry{s*.282f,.365f,-.466f};
        Rod(v,Add(tank,{0,.10f,0}),elbow,.012f,Bezel);
        Beam(v,elbow,entry,.012f,Cable);
        Rod(v,entry,{entry[0],.35f,-.495f},.015f,Steel);
    }
}
// Rear walls behind the machinery: a pipe run and two boxes each side, so
// the corners seen past the cabinets are equipment too.
void TankRearWalls(std::vector<CockpitVertex>& v) {
    // The cabinets stand close in front of the canted rear walls; only a
    // grille shows past each one, low by the side wall.
    for(unsigned wall:{2u,4u}) {
        const auto out=TankOutward(wall);
        auto vent=TankWallPanel(wall,wall==2?.18f:.82f,-.66f,.004f);
        GripBody(v,vent,.14f,.22f,.020f,Dark,.004f);
        auto grille=vent;grille.c=Point(vent,0,0,.016f);
        Part(v,referenceVent,grille,.12f,.19f,.018f,Steel);
        for(float y:{-.090f,.090f})FrameBolt(v,Point(vent,0,y,.0165f),Mul(out,-1),.004f);
    }
    {
        constexpr unsigned wall=3;
        const auto out=TankOutward(wall);
        auto plate=TankWallPanel(wall,.5f,-.85f,.004f);
        Part(v,referenceServicePlate,plate,.30f,.20f,.016f,Steel);
        for(float x:{-.12f,.12f})for(float y:{-.08f,.08f})FrameBolt(v,Point(plate,x,y,.013f),Mul(out,-1),.004f);
    }
}
// Turret screens: seams only. Low and high edges of the vertical band, the
// roof's inner octagon, the slanted joints between them, and a cross on top.
void TankSeams(std::vector<CockpitVertex>& v) {
    auto inset=[](unsigned i,float d,float y) {
        const auto c=TankCorner(i,y);const auto n=Unit(Add(TankOutward(i),TankOutward(i+7)));
        return Sub(c,Mul(n,d/std::max(.5f,Dot(n,TankOutward(i)))));
    };
    for(unsigned i=0;i<8;++i) {
        const auto out=TankOutward(i);
        const auto lift=Mul(out,.028f);
        MonitorSeam(v,Add(TankCorner(i,kTankSill+.030f),lift),Add(TankCorner(i+1,kTankSill+.030f),lift),{0,1,0},Mul(out,.004f));
        MonitorSeam(v,Add(TankCorner(i,kTankBand),lift),Add(TankCorner(i+1,kTankBand),lift),{0,1,0},Mul(out,.004f));
        MonitorSeam(v,Add(inset(i,kTankRoofInset,kTankRoof+.020f),{0,0,0}),inset(i+1,kTankRoofInset,kTankRoof+.020f),out,{0,.004f,0});
        // Corner joints: up the band, then up the roof's slope.
        const auto corner=Unit(Add(TankOutward(i),TankOutward(i+7)));
        MonitorSeam(v,Add(TankCorner(i,kTankSill+.030f),Mul(corner,.028f)),Add(TankCorner(i,kTankBand),Mul(corner,.028f)),Cross(corner,{0,1,0}),Mul(corner,.004f));
        MonitorSeam(v,Add(TankCorner(i,kTankBand),Mul(corner,.028f)),inset(i,kTankRoofInset,kTankRoof+.020f),Cross(corner,{0,1,0}),{0,.004f,0});
    }
    MonitorSeam(v,{-.58f,kTankRoof+.020f,kTankCentreZ},{.58f,kTankRoof+.020f,kTankCentreZ},{0,0,1},{0,.004f,0});
    MonitorSeam(v,{0,kTankRoof+.020f,-.50f},{0,kTankRoof+.020f,1.08f},{1,0,0},{0,.004f,0});
}
std::vector<CockpitVertex> TankInterior() {
    std::vector<CockpitVertex> v;v.reserve(320000);
    TankFloor(v);
    TankWalls(v);
    TankRace(v);
    TankLeftWall(v);
    TankRightWall(v);
    TankFrontWall(v);
    TankRearWalls(v);
    TankRearMachinery(v);
    // The Proteus driver's station, exactly as approved there.
    CabinSeat(v,kTankDeck,{0,0,0});
    CabinFootStrut(v,kTankDeck,{0,0,0});
    CabinConsole(v);
    CabinCluster(v,proteusCabinPanels,true);
    CabinConsoleDetail(v);
    TankSeams(v);
    return v;
}
// A barrel's end cap: the Barga dish, reshaped onto a rounded outline that
// just encloses the barrel's own section where it is cut, so the barrel and
// where it points stay in view (the round dishes hid both). The outline is a
// superellipse: centre (cx,cy), half-widths a,b, squareness n (2 is an
// ellipse, 8 nearly a rectangle). The bowl's deepest point is at faceZ and
// faces the crew; its return collar runs forward to collarZ over the start of
// the barrel. Bone-local game coordinates. Values from capfit (see the
// cockpit handoff): the section of the original mesh over the cut's open
// span, its hull, and the smallest such outline 12 mm clear of it.
void TankFittedCap(std::vector<CockpitVertex>& v,float cx,float cy,float a,float b,float n,float faceZ,float collarZ) {
    constexpr float reference=4.0583674f,angle=.5192239f,thickness=.10f;
    const float rim=(reference+thickness)*std::sin(angle);
    const float k=std::sqrt(a*b)/rim;   // depth follows the section's mean size
    const float seal=reference+std::max(.205f,(collarZ-faceZ)/k);
    const auto start=v.size();
    BargaDish(v,{{0,0,0},{0,0,1},{1,0,0},{0,1,0},{0,0,seal},{0,0,1},reference,angle});
    auto map=[&](P p)->P{
        const float r=std::sqrt(p[0]*p[0]+p[1]*p[1]);
        P q{cx,cy,faceZ+(p[2]-reference)*k};
        if(r>1e-7f) {
            const float c=std::fabs(p[0]/r),s=std::fabs(p[1]/r);
            const float edge=1.f/std::pow(std::pow(c/a,n)+std::pow(s/b,n),1.f/n);
            q[0]+=p[0]*edge/rim;q[1]+=p[1]*edge/rim;
        }
        return q;};
    // Positions through the map; normals through its inverse transpose,
    // taken from the map's Jacobian at each vertex.
    for(auto i=start;i<v.size();++i) {
        const P p{v[i].position[0],v[i].position[1],v[i].position[2]},normal{v[i].normal[0],v[i].normal[1],v[i].normal[2]};
        constexpr float e=1e-3f;P column[3];
        for(int d=0;d<3;++d){P lo=p,hi=p;lo[d]-=e;hi[d]+=e;column[d]=Mul(Sub(map(hi),map(lo)),1/(2*e));}
        const auto turned=Unit(Add(Add(Mul(Cross(column[1],column[2]),normal[0]),Mul(Cross(column[2],column[0]),normal[1])),Mul(Cross(column[0],column[1]),normal[2])));
        const auto q=map(p);
        for(int d=0;d<3;++d){v[i].position[d]=q[d];v[i].normal[d]=turned[d];}
    }
}
// Turret deck: where the hull's own turret well is open (the railgun's and
// the Titan's), a floor over it at the hull's deck height, drawn on the body
// bone so it stays with the hull. Radial plates, a race, hatches and vents.
void TankDeckCap(std::vector<CockpitVertex>& v,float y,float radius) {
    constexpr unsigned slices=64;
    {
        std::vector<P> disc;for(unsigned i=0;i<slices;++i) {
            const float a=6.2831853f*i/slices;disc.push_back({radius*std::cos(a),y,radius*std::sin(a)});
        }
        RoundedPrism(v,disc,{0,-.06f,0},.012f,Hull);
    }
    // Race ring and its bolts near the rim, plate joints on the radials.
    const float race=radius-.12f;
    for(unsigned i=0;i<slices;++i) {
        const float a=6.2831853f*i/slices,b=6.2831853f*(i+1)/slices;
        Beam(v,{race*std::cos(a),y+.012f,race*std::sin(a)},{race*std::cos(b),y+.012f,race*std::sin(b)},.022f,Bezel);
        if(i%2==0)FrameBolt(v,{(race-.05f)*std::cos(a),y+.0015f,(race-.05f)*std::sin(a)},{0,1,0},.008f);
    }
    for(unsigned i=0;i<8;++i) {
        const float a=6.2831853f*i/8+.3926991f;
        Beam(v,{.30f*std::cos(a),y+.004f,.30f*std::sin(a)},{(race-.06f)*std::cos(a),y+.004f,(race-.06f)*std::sin(a)},.006f,Dark);
    }
    // Equipment on the deck, spaced round the ring outside any cabin.
    const unsigned count=radius>2?10u:4u;
    for(unsigned i=0;i<count;++i) {
        const float a=6.2831853f*(i+.5f)/count;
        const float r=radius>2?radius*.62f:radius*.80f;
        const P at{r*std::cos(a),y,r*std::sin(a)};
        const P radial{std::cos(a),0,std::sin(a)};
        // Lying on the deck: Cross(right,radial) points down, so the face is up.
        Panel flat{Add(at,{0,.002f,0}),Cross({0,1,0},radial),radial,0,0,0};
        if(i%2==0) {
            GripBody(v,flat,radius>2?.60f:.20f,radius>2?.40f:.16f,.020f,Dark,.006f);
            auto grille=flat;grille.c=Point(flat,0,0,.004f);
            Part(v,referenceVent,grille,radius>2?.54f:.18f,radius>2?.34f:.13f,.020f,Steel);
        } else {
            Part(v,referenceServicePlate,flat,radius>2?.70f:.22f,radius>2?.46f:.16f,.018f,Steel);
        }
    }
}
}
std::vector<CockpitVertex> BuildCockpitCap(unsigned index) {
    std::vector<CockpitVertex> v;
    if(index<10)return BuildBargaJointCap(index/5,index%5);
    // Fitted to each barrel's section at its cut (capfit). The Blacker's and
    // the Varias' caps sit on the recoiling barrel's own rear end, the
    // mantlet block being hidden, so the cap moves with the barrel.
    switch(index) {
    case kCapBlacker:TankFittedCap(v,0,0,.1743f,.1743f,2,-.0072f,.0784f);break;
    case kCapVarias:TankFittedCap(v,0,0,.1061f,.1061f,2,-1.3602f,-1.3134f);break;
    case kCapRailgun:TankFittedCap(v,0,.2844f,.2332f,.3145f,8,3.6773f,3.7413f);break;
    case kCapEmc:TankFittedCap(v,0,-.0332f,.6211f,1.5860f,8,.9429f,1.3169f);break;
    case kCapTitanMain:TankFittedCap(v,0,-.0161f,.6347f,.5208f,8,1.9661f,2.2101f);break;
    case kCapTitanSub:TankFittedCap(v,0,.0473f,.2737f,.2202f,8,2.1549f,2.1989f);break;
    case kCapRailgunMg:TankFittedCap(v,0,.2010f,.0807f,.0807f,2,.8618f,.9058f);break;
    case kCapKebler:TankFittedCap(v,0,0,.1433f,.1257f,3,.5891f,.7071f);break;
    case kCapRailgunDeck:TankDeckCap(v,1.41f,1.40f);break;
    case kCapTitanDeck:TankDeckCap(v,4.02f,4.70f);break;
    default:break;
    }
    return v;
}
