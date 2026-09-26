// Nereid (409), rebuilt (the user, 2026-09-25: every helicopter redone machine
// by machine, the old lined canopy and its small screens thrown away). One
// pilot, well back under the hood's rear (座席を多めにバック): the eye at
// model (0,2.28,2.55). A bomber too, so there is no dashboard: ahead of the
// seat the monitor's edge rises from the seat's underside to just under the
// windscreen and takes in the whole lower nose, and the three screens hang
// see-through in that view on slim mounts. Nix seat and machinery behind.
float NereidDeck(float) {return -1.105f;}
std::array<Panel,3> NereidPanels() {
    // The radar large ahead and below; health (a strip) and ammunition to
    // either side, closer, clear of the radar from the eye and of the walls.
    return {FacingPilot({0,-.300f,.620f},.320f,.320f,0),
            FacingPilot({-.300f,-.240f,.340f},.240f,.100f,1),
            FacingPilot({.300f,-.260f,.340f},.240f,.160f,2)};
}
std::vector<CockpitVertex> NereidInterior() {
    std::vector<CockpitVertex> v;v.reserve(260000);
    const auto* c=hk::Find(CockpitKind::HeliNereid);if(!c)return v;
    const auto spec=hk::Cabin(*c);const float floorY=spec.floorY;
    std::vector<std::vector<P>> windows;
    for(unsigned i=0;i<c->windowCount;++i)windows.push_back(hk::Loop(c->windows[i]));
    std::vector<CombatRegion> holes;
    for(const auto& w:windows)hk::Prisms(w,spec.cut,holes);
    hk::Zones(spec,holes);
    hk::Shell(v,*c,spec,holes);
    for(const auto& w:windows)hk::Frame(v,w,spec);
    // The hood over the pilot: airframe ribs round it, above the sill.
    for(float z:{-.58f,-.42f,-.26f,-.10f})hk::Rib(v,*c,spec,holes,z,spec.sill,.036f,.024f,Dark);
    // The monitor's edge: a rail up each wall where the trim ends, and on the
    // deck round the floor under the seat.
    const auto edge=hk::ZoneEdge(*c,spec);
    for(float side:{-1.f,1.f}) {
        std::vector<P> rail;for(const auto& p:edge)rail.push_back({side*p[0],p[1],p[2]});
        hk::Rail(v,rail,.012f,Dark);
        hk::Rail(v,{{side*edge.front()[0],floorY+.012f,-.30f},{side*.42f,floorY+.012f,-.30f},{side*.42f,floorY+.012f,.10f}},.012f,Dark);
    }
    hk::Rail(v,{{-.42f,floorY+.012f,.10f},{.42f,floorY+.012f,.10f}},.012f,Dark);
    // The keel: a box beam down the middle of the monitor floor from under
    // the seat, carrying the stick, the footrest and the radar; ahead of the
    // radar's foot the nose's floor is clear.
    const float keelTop=floorY+.060f,keelEnd=.920f;
    Box(v,{-.045f,floorY,.10f},{.045f,keelTop,keelEnd},Dark);
    RoundedPrism(v,{{-.045f,floorY,keelEnd},{.045f,floorY,keelEnd},{.045f,floorY,keelEnd+.050f},{-.045f,floorY,keelEnd+.050f}},{0,.030f,0},.006f,Dark);
    for(float z:{.18f,.46f,.72f})for(float x:{-.028f,.028f})FrameBolt(v,{x,keelTop+.002f,z},{0,1,0},.005f);
    // Seat, and behind it the Nix's machinery, drawn in to the hood's narrow
    // gable as the Depth Crawler's is to its pod.
    Seat(v);
    {const auto start=v.size();RearMachinery(v);MovePart(v,start,{.70f,.91f,.86f},{0,-.025f,-.060f});}
    // Cyclic: the Proteus stick on its console, on the keel between the knees.
    ProteusStickOn(v,.26f,.36f,NereidDeck,-.64f,false);
    // Collective beside the left thigh, on its own floor box.
    {
        const P pivot{-.34f,-.78f,-.14f},grip{-.34f,-.55f,.24f};
        RoundedPrism(v,{{-.40f,floorY,-.26f},{-.28f,floorY,-.26f},{-.28f,floorY,-.02f},{-.40f,floorY,-.02f}},{0,pivot[1]-floorY-.02f,0},.008f,Dark);
        Axle(v,pivot,.13f,.028f,Steel);
        for(float x:{-.407f,-.273f})FrameBolt(v,{x,pivot[1],pivot[2]},{x<-.34f?-1.f:1.f,0,0},.006f);
        Rod(v,pivot,grip,.017f,Steel);
        const auto along=Unit(Sub(grip,pivot));
        Rod(v,Sub(grip,Mul(along,.07f)),Add(grip,Mul(along,.07f)),.022f,Rubber);
        const auto head=Add(grip,Mul(along,.09f));
        Box(v,Sub(head,{.030f,.028f,.030f}),Add(head,{.030f,.034f,.030f}),Dark);
        for(int k=0;k<3;++k)Rod(v,Add(head,{-.012f+float(k)*.012f,.034f,-.008f}),Add(head,{-.012f+float(k)*.012f,.046f,-.008f}),.006f,k?Steel:Warning);
    }
    // Footrest: a lightened silver plate across both feet at 45 degrees, on
    // four short posts square to it down to the keel, its foot on a tube.
    {
        const P up=Unit(P{0,.70f,.71f});
        const Panel plate{{0,floorY+.160f,.660f},{1,0,0},up,0,0,0};
        const auto back=Unit(Cross(plate.right,plate.up));   // away from the feet
        const auto start=v.size();
        hk::LightenedPlate(v,plate,8,3,.075f,.026f,.006f,Steel);
        for(auto i=start;i<v.size();++i){v[i].colour[0]=.26f;v[i].colour[1]=.28f;v[i].colour[2]=.30f;}
        // The heel tube along its foot, on two arms out from the keel.
        const auto heel=Point(plate,0,-.125f,-.010f);
        hk::Tube(v,{{-.320f,heel[1],heel[2]},{.320f,heel[1],heel[2]}},.014f,Steel);
        for(float side:{-1.f,1.f}) {
            Beam(v,{side*.045f,heel[1]-.026f,heel[2]},{side*.250f,heel[1]-.026f,heel[2]},.012f,Dark);
            FrameBolt(v,{side*.200f,heel[1]-.026f-.012f,heel[2]},{0,-1,0},.005f);
        }
        for(float lx:{-.055f,.055f})for(float ly:{-.030f,.060f}) {
            const auto from=Point(plate,lx,ly,-.004f);
            const float t=(from[1]-keelTop)/-back[1];
            Rod(v,from,Add(from,Mul(back,t+.004f)),.010f,Steel);
        }
        auto shoe=plate;shoe.c=Add(Point(plate,0,.015f,0),Mul(back,.020f));
        GripBody(v,shoe,.160f,.130f,.018f,Dark,.006f);
    }
    // The front gear's roots (frontWheelSus, model (+-.618,.665,2.657)) rise up
    // to 18 cm over the fuselage's floor skin, into the monitor's view (the
    // user: their backs show; cap them like the tanks'). Each part over the
    // skin gets a tank barrel's fitted cap (surveyed extents, 2.2 cm clear),
    // its bowl turned up to the pilot, its collar down through the skin.
    {
        struct Root {float x0,x1,z0,z1,top;};
        constexpr Root roots[]={{.454f,.561f,-.142f,-.093f,-1.127f},{.340f,.525f,.122f,.321f,-1.079f}};
        constexpr float clear=.022f,skin=-1.360f;
        for(float side:{-1.f,1.f})for(const auto& r:roots) {
            const auto start=v.size();
            TankFittedCap(v,side*(r.x0+r.x1)*.5f,(r.z0+r.z1)*.5f,(r.x1-r.x0)*.5f+clear,(r.z1-r.z0)*.5f+clear,3.f,-(r.top+clear),-skin);
            // Its axis (bone z, away from the crew) down: (x,y,z) -> (x,-z,y).
            for(auto i=start;i<v.size();++i) {
                const P p{v[i].position[0],v[i].position[1],v[i].position[2]},n{v[i].normal[0],v[i].normal[1],v[i].normal[2]};
                const P q{p[0],-p[2],p[1]},m{n[0],-n[2],n[1]};
                for(int k=0;k<3;++k){v[i].position[k]=q[k];v[i].normal[k]=m[k];}
            }
            hk::DropSlivers(v,start);
        }
    }
    // The screens: the Nix's open rims, see-through (they hang in the view).
    const auto screens=NereidPanels();
    for(const auto& p:screens)Instrument(v,p);
    // The radar on a pedestal from the keel ahead of the footrest: a clamp
    // on its lower rim, a slim column, a bolted shoe. From the eye the column
    // falls below the screen, never behind it.
    {
        const auto& p=screens[0];
        auto clamp=p;clamp.c=Point(p,0,-p.h*.5f-.045f,-.030f);
        GripBody(v,clamp,.110f,.050f,.040f,Dark,.008f);
        const auto top=Point(p,0,-p.h*.5f-.050f,-.040f);const P foot{0,keelTop,.860f};
        Rod(v,top,foot,.016f,Steel);
        Rod(v,foot,Add(foot,{0,.050f,0}),.028f,Dark);
        for(float x:{-.030f,.030f})FrameBolt(v,{x,keelTop+.002f,.860f},{0,1,0},.006f);
    }
    // Health and ammunition on short arms from the walls, a knuckle half way.
    for(unsigned k=1;k<3;++k) {
        const auto& p=screens[k];const float side=k==1?-1.f:1.f;
        const auto back=Point(p,side*(p.w*.5f-.030f),0,-.075f);
        const P wall{side*(hk::WallX(*c,spec,back[2],back[1])-.004f),back[1],back[2]};
        auto socket=p;socket.c=Point(p,side*(p.w*.5f-.030f),0,-.058f);
        GripBody(v,socket,.050f,.070f,.030f,Dark,.006f);
        const P knuckle=Mul(Add(back,wall),.5f);
        Rod(v,back,knuckle,.014f,Steel);Rod(v,knuckle,wall,.014f,Steel);
        Axle(v,knuckle,.040f,.022f,Dark);
        const Panel mount{wall,{0,0,side},{0,1,0},0,0,0};
        GripBody(v,mount,.070f,.090f,.018f,Dark,.006f);
        for(float t:{-.030f,.030f})FrameBolt(v,Add(wall,{-side*.019f,t,0}),{-side,0,0},.005f);
    }
    // The canopy's glass, then the monitors' joints, last: the translucent pass.
    for(const auto& w:windows)hk::Pane(v,w);
    for(float side:{-1.f,1.f}) {
        // Up the walls: a joint every tile, from the deck to the monitor's edge.
        for(float z:{-.10f,.45f,.80f,1.20f}) {
            float top=floorY;for(const auto& zone:spec.zones)if(z>zone.z0&&z<zone.z1)top=std::max(top,hk::ZoneTop(zone,z));
            std::vector<P> line;
            for(int k=0;k<=8;++k){const float y=floorY+(top-floorY)*float(k)/8;line.push_back({side*hk::WallX(*c,spec,z,y),y,z});}
            for(std::size_t k=0;k+1<line.size();++k)hk::Ink(v,line[k],line[k+1]);
        }
        // Along the walls at knee height, ahead of the seat.
        for(float z0=.20f;z0<spec.front-.05f;z0+=.10f) {
            const float y=-.75f,z1=std::min(spec.front,z0+.10f);
            hk::Ink(v,{side*hk::WallX(*c,spec,z0,y),y,z0},{side*hk::WallX(*c,spec,z1,y),y,z1});
        }
    }
    // Across the floor ahead of the seat, from the keel to the walls.
    for(float z:{.45f,.80f,1.20f})
        for(float side:{-1.f,1.f})hk::Ink(v,{side*.045f,floorY,z},{side*hk::WallX(*c,spec,z,floorY),floorY,z});
    return v;
}
