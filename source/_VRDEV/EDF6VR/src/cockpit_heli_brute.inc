// Brute (410), rebuilt (the user, 2026-09-25, one machine at a time). A big
// utility helicopter, two seats side by side; the camera takes the left one,
// 10 cm further back than before: the eye at model (.30,2.85,3.90), so the
// fuselage's centre line is cabin x .30 (cx), and everything the machine has
// once (the shell, the windows, the screens) stands about it. The co-pilot's
// seat, stick and footrest are the pilot's, 2 cx across; its collective
// outboard, mirrored. The windows come down to 64 cm under the eye: below
// them the monitor, beside both seats and from the seat pans forward, the
// nose's lower half too. The screens are for both pilots (the user): one row
// in the middle under the windscreen, health, radar, ammunition, each turned
// to the point between the two eyes; see-through, in the monitor's view, on
// columns from a cross beam over the floor.
float BruteDeck(float) {return -1.105f;}
namespace brute {
constexpr float keelEnd=.860f,beamZ0=.800f;
// The point between the two eyes, the screens turned to it.
P Mid(float cx) {return {cx,kCockpitSeatedEye[1],kCockpitSeatedEye[2]};}
Panel FacingMid(float cx,P centre,float w,float h,unsigned source) {
    const auto out=Unit(Sub(centre,Mid(cx)));
    const auto right=Unit(Cross(P{0,1,0},out));
    return {centre,right,Cross(out,right),w,h,source};
}
float CentreX() {const auto* c=hk::Find(CockpitKind::HeliBrute);return c?c->centreX:.30f;}
}
std::array<Panel,3> BrutePanels() {
    using namespace brute;
    const float cx=CentreX();
    // Radar in the middle, its top just under the windscreen's lowest glass;
    // health ahead of the pilot, ammunition ahead of the co-pilot.
    return {FacingMid(cx,{cx,-.560f,.950f},.400f,.400f,0),
            FacingMid(cx,{cx-.460f,-.560f,.930f},.360f,.110f,1),
            FacingMid(cx,{cx+.450f,-.560f,.930f},.340f,.190f,2)};
}
std::vector<CockpitVertex> BruteInterior() {
    using namespace brute;
    std::vector<CockpitVertex> v;v.reserve(420000);
    const auto* c=hk::Find(CockpitKind::HeliBrute);if(!c)return v;
    auto spec=hk::Cabin(*c);spec.domeTop=true;const float floorY=spec.floorY,cx=spec.centreX;
    std::vector<std::vector<P>> windows;
    for(unsigned i=0;i<c->windowCount;++i)windows.push_back(hk::Loop(c->windows[i]));
    std::vector<CombatRegion> holes;
    for(const auto& w:windows)hk::Prisms(w,spec.cut,holes,spec.shallowEdges);
    hk::Zones(spec,holes);
    hk::Shell(v,*c,spec,holes);
    // The windscreen's two panes share the centre line: one mullion there.
    for(std::size_t i=0;i<windows.size();++i)hk::Frame(v,windows[i],spec,&windows,.0012f*float(i));
    auto wall=[&](float z,float y){return hk::WallX(*c,spec,z,y);};
    // A rib round the cabin behind the door windows, over the machinery.
    hk::Rib(v,*c,spec,holes,-.620f,spec.sill,.036f,.024f,Dark,spec.frame-spec.cut-.003f);
    // The monitor's edge: a rail along each wall at the seats' underside,
    // across the nose's front, and on the deck round the floor under the
    // seats (the monitor beside them starts .68 out from the centre line).
    const float zoneX=spec.zones[0].xMin;
    {
        const auto edge=hk::ZoneEdge(*c,spec);
        for(float side:{-1.f,1.f}) {
            std::vector<P> rail;for(const auto& p:edge)rail.push_back({cx+side*(p[0]-cx),p[1],p[2]});
            hk::Rail(v,rail,.012f,Dark);
            const float z0=spec.zones[0].z0;
            hk::Rail(v,{{cx+side*(wall(z0,floorY)-.010f),floorY+.012f,z0},{cx+side*zoneX,floorY+.012f,z0},{cx+side*zoneX,floorY+.012f,.10f}},.012f,Dark);
        }
        hk::Rail(v,{{cx-zoneX,floorY+.012f,.10f},{cx+zoneX,floorY+.012f,.10f}},.012f,Dark);
        const float top=spec.zones[1].top0,w=wall(spec.front,top)-.010f,z=spec.front-.012f;
        hk::Rail(v,{{cx-w,top,z},{cx+w,top,z}},.012f,Dark);
    }
    // The keels under both pilots, from under the seats to a cross beam
    // ahead of the footrests that carries the three screens' columns.
    const float keelTop=floorY+.060f;
    for(float x:{0.f,2*cx}) {
        Box(v,{x-.045f,floorY,.10f},{x+.045f,keelTop,beamZ0},Dark);
        for(float z:{.18f,.46f,.72f})for(float dx:{-.028f,.028f})FrameBolt(v,{x+dx,keelTop+.002f,z},{0,1,0},.005f);
    }
    const float beamHalf=.470f;
    RoundedPrism(v,{{cx-beamHalf,floorY,beamZ0},{cx+beamHalf,floorY,beamZ0},{cx+beamHalf,floorY,keelEnd},{cx-beamHalf,floorY,keelEnd}},{0,.060f,0},.006f,Dark);
    // Seats side by side, the Nix's seat on its own two posts (no machinery
    // behind: the rear bulkhead carries the door to the cabin, the user's).
    for(float x:{0.f,2*cx}) {
        const auto start=v.size();Seat(v,true);
        for(float s:{-1.f,1.f}) {
            Rod(v,{s*.18f,-1.10f,.00f},{s*.18f,-.80f,.03f},.023f,Steel);
            Rod(v,{s*.18f,-1.075f,.0025f},{s*.18f,-.92f,.018f},.034f,Dark);
            Panel foot{{s*.18f,-1.105f,0},{1,0,0},{0,0,1},0,0,0};
            GripBody(v,foot,.115f,.13f,.018f,Bezel);
            for(float z:{-.044f,.044f})FrameBolt(v,Point(foot,0,z,.018f),{0,1,0});
        }
        if(x!=0)MovePart(v,start,{1,1,1},{x,0,0});
    }
    // The door to the cabin behind, in the middle of the rear bulkhead
    // between the seats: a frame round the opening, the leaf with a small
    // window, hinges on the left, a lever handle on the right, a kick plate.
    {
        const float z=spec.rear,half=.310f,top=.420f,chamfer=.080f;
        const float jamb=.050f,proud=.030f;
        auto outline=[&](float in,float depth) {
            const float l=cx-half+in,r=cx+half-in,t=top-in,bot=floorY;
            return std::vector<P>{{l,bot,z+depth},{r,bot,z+depth},{r,t-chamfer,z+depth},{r-chamfer,t,z+depth},{l+chamfer,t,z+depth},{l,t-chamfer,z+depth}};
        };
        // The frame: jambs and a header, chamfered at the top corners.
        {
            // Every side but the sill (0-1): mitred strips, outer outline to inner.
            const auto o=outline(-jamb,0),i=outline(0,0);
            for(std::size_t k=1;k<o.size();++k) {
                const std::size_t n=(k+1)%o.size();
                RoundedPrism(v,{o[k],o[n],i[n],i[k]},{0,0,proud},.006f,Dark);
            }
        }
        // The leaf, 1 cm inside the frame, and its window.
        RoundedPrism(v,outline(.010f,0),{0,0,.022f},.008f,Bezel);
        const float face=z+.022f;
        Box(v,{cx-.130f,.080f,face-.004f},{cx+.130f,.300f,face+.010f},Dark);
        Box(v,{cx-.105f,.105f,face+.006f},{cx+.105f,.275f,face+.012f},Glass);
        for(float x:{-.115f,.115f})for(float y:{.092f,.288f})FrameBolt(v,{cx+x,y,face+.010f},{0,0,1},.005f);
        Box(v,{cx-half+.030f,floorY+.004f,face-.004f},{cx+half-.030f,floorY+.130f,face+.008f},Steel);
        for(float x:{-.20f,0.f,.20f})FrameBolt(v,{cx+x,floorY+.067f,face+.008f},{0,0,1},.005f);
        // Hinges: knuckles on the frame's left edge.
        for(float y:{-.820f,-.100f}) {
            Rod(v,{cx-half-.012f,y-.060f,z+proud+.012f},{cx-half-.012f,y+.060f,z+proud+.012f},.016f,Steel);
            Box(v,{cx-half-.012f,y-.045f,face-.002f},{cx-half+.080f,y+.045f,face+.008f},Steel);
        }
        // The handle: a round boss and a lever toward the hinge side.
        const P boss{cx+half-.075f,-.200f,face};
        Rod(v,boss,Add(boss,{0,0,.030f}),.030f,Dark);
        Rod(v,Add(boss,{0,0,.030f}),Add(boss,{-.120f,-.010f,.045f}),.013f,Steel);
        Rod(v,Add(boss,{-.120f,-.010f,.040f}),Add(boss,{-.120f,-.010f,.058f}),.017f,Rubber);
    }
    // Cyclics: the Proteus stick on its console, on each keel between the knees.
    for(float x:{0.f,2*cx}) {
        const auto start=v.size();ProteusStickOn(v,.26f,.36f,BruteDeck,-.64f,false);
        if(x!=0)MovePart(v,start,{1,1,1},{x,0,0});
    }
    // Collectives beside the outer thighs (the pilot's on the left, the
    // co-pilot's on the right), each on its own floor box by the seat.
    for(float side:{-1.f,1.f}) {
        const float px=cx+side*(.32f+cx);   // -.32 for the pilot, clear of the seat's pan
        const P pivot{px,-.78f,-.14f},grip{px,-.55f,.24f};
        RoundedPrism(v,{{px-.055f,floorY,-.26f},{px+.055f,floorY,-.26f},{px+.055f,floorY,-.02f},{px-.055f,floorY,-.02f}},{0,pivot[1]-floorY-.02f,0},.008f,Dark);
        Axle(v,pivot,.13f,.028f,Steel);
        for(float dx:{-.067f,.067f})FrameBolt(v,{px+dx,pivot[1],pivot[2]},{dx<0?-1.f:1.f,0,0},.006f);
        Rod(v,pivot,grip,.017f,Steel);
        const auto along=Unit(Sub(grip,pivot));
        Rod(v,Sub(grip,Mul(along,.07f)),Add(grip,Mul(along,.07f)),.022f,Rubber);
        const auto head=Add(grip,Mul(along,.09f));
        Box(v,Sub(head,{.030f,.028f,.030f}),Add(head,{.030f,.034f,.030f}),Dark);
        for(int k=0;k<3;++k)Rod(v,Add(head,{-.012f+float(k)*.012f,.034f,-.008f}),Add(head,{-.012f+float(k)*.012f,.046f,-.008f}),.006f,k?Steel:Warning);
    }
    // Footrests: the lightened silver plate across both feet at 45 degrees,
    // on four short posts square to it down to the keel, its heel on a tube.
    for(float x:{0.f,2*cx}) {
        const P up=Unit(P{0,.70f,.71f});
        const Panel plate{{x,floorY+.160f,.660f},{1,0,0},up,0,0,0};
        const auto back=Unit(Cross(plate.right,plate.up));
        const auto start=v.size();
        hk::LightenedPlate(v,plate,5,3,.075f,.026f,.006f,Steel);
        for(auto i=start;i<v.size();++i){v[i].colour[0]=.26f;v[i].colour[1]=.28f;v[i].colour[2]=.30f;}
        const auto heel=Point(plate,0,-.125f,-.010f);
        hk::Tube(v,{{x-.205f,heel[1],heel[2]},{x+.205f,heel[1],heel[2]}},.014f,Steel);
        for(float side:{-1.f,1.f}) {
            Beam(v,{x+side*.045f,heel[1]-.026f,heel[2]},{x+side*.160f,heel[1]-.026f,heel[2]},.012f,Dark);
            FrameBolt(v,{x+side*.125f,heel[1]-.026f-.012f,heel[2]},{0,-1,0},.005f);
        }
        for(float lx:{-.055f,.055f})for(float ly:{-.030f,.060f}) {
            const auto from=Point(plate,lx,ly,-.004f);
            const float t=(from[1]-keelTop)/-back[1];
            Rod(v,from,Add(from,Mul(back,t+.004f)),.010f,Steel);
        }
        auto shoe=plate;shoe.c=Add(Point(plate,0,.015f,0),Mul(back,.020f));
        GripBody(v,shoe,.160f,.130f,.018f,Dark,.006f);
    }
    // The chin sensor's probe (body mesh, under the nose's front): its rear
    // end and the flange where it meets the skin (cabin z 1.70-1.73) are
    // inside the fuselage and show through the monitor from above (the user:
    // its back shows; cap it). A tank barrel's fitted cap over both, 2.2 cm
    // clear, its bowl turned back to the pilot (its face 6 cm behind the
    // probe's end: the bowl is 4.8 cm deep), its collar forward through the
    // flange; the probe runs on out of it under the chin.
    {
        const auto start=v.size();
        constexpr float clear=.022f;
        TankFittedCap(v,cx,-1.813f,.190f+clear,.142f+clear,3.f,1.700f-.060f,1.730f+.030f);
        hk::DropSlivers(v,start);
    }
    // The screens: the Nix's open rims, see-through. Each on a clamp on its
    // lower rim, a slim column down to the cross beam, leaning back toward
    // the eye so that from the seat it falls below its screen; a tie bar
    // across the three columns under the radar's clamp makes them one panel.
    const auto screens=BrutePanels();
    for(const auto& p:screens)Instrument(v,p);
    std::vector<P> tie;
    const float tieY=Point(screens[0],0,-screens[0].h*.5f-.050f,-.040f)[1]-.040f;
    for(const auto& p:screens) {
        auto clamp=p;clamp.c=Point(p,0,-p.h*.5f-.045f,-.030f);
        GripBody(v,clamp,.110f,.050f,.040f,Dark,.008f);
        const auto top=Point(p,0,-p.h*.5f-.050f,-.040f);const P foot{top[0],keelTop,beamZ0+.020f};
        Rod(v,top,foot,.016f,Steel);
        Rod(v,foot,Add(foot,{0,.050f,0}),.028f,Dark);
        for(float dx:{-.030f,.030f})FrameBolt(v,{foot[0]+dx,keelTop+.002f,foot[2]},{0,1,0},.006f);
        const float t=(top[1]-tieY)/(top[1]-foot[1]);
        const P at=Add(top,Mul(Sub(foot,top),t));
        tie.push_back(at);
        Rod(v,Sub(at,{0,.022f,0}),Add(at,{0,.022f,0}),.024f,Dark);   // the collar where it meets the column
    }
    std::swap(tie[0],tie[1]);   // left to right: health, radar, ammunition
    hk::Rail(v,tie,.012f,Steel);
    // The canopy's glass, then the monitors' joints, last: the translucent pass.
    for(std::size_t i=0;i<windows.size();++i)hk::Pane(v,windows[i],.0012f*float(i));
    const float top=spec.zones[1].top0;
    for(float side:{-1.f,1.f})for(float z:{-.10f,.40f,.80f,1.20f}) {
        std::vector<P> line;
        for(int k=0;k<=8;++k){const float y=floorY+(top-floorY)*float(k)/8;line.push_back({cx+side*wall(z,y),y,z});}
        for(std::size_t k=0;k+1<line.size();++k)hk::Ink(v,line[k],line[k+1]);
    }
    for(float z:{.40f,1.20f})hk::Ink(v,{cx-wall(z,floorY),floorY,z},{cx+wall(z,floorY),floorY,z});
    hk::Ink(v,{cx,floorY,keelEnd},{cx,floorY,spec.front});
    return v;
}
