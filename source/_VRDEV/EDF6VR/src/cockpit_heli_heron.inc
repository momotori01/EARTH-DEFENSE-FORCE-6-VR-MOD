// Heron (602), rebuilt (the user, 2026-09-25, one machine at a time). A narrow
// cabin under a cluster of small panes, and right ahead of them a big raised
// nose at the eye's height (the user: the nose is in the way; delete it and
// put a see-through monitor in front instead). One pilot, 22 cm further back
// than before (10, and 12 more after the first test: the footrest and the
// collective were through things): the eye at model (0,2.12,2.30). Ahead of the canopy's front
// everything is monitor, its joints drawn over the nose's shape, and the game
// no longer draws the nose (HeliRebuiltHidden). The cabin is too narrow for
// screens beside the pilot: health, radar and ammunition stand one over
// another on a pedestal in the middle, see-through, in the view over the nose.
float HeronDeck(float) {return -1.105f;}
namespace heron {
constexpr float canopyFront=.590f;
// A screen `el` degrees below level, straight ahead, `d` from the eye.
P Ahead(float el,float d) {
    const float a=el*3.14159265f/180;
    return {0,kCockpitSeatedEye[1]-d*std::sin(a),kCockpitSeatedEye[2]+d*std::cos(a)};
}
}
std::array<Panel,3> HeronPanels() {
    using namespace heron;
    return {FacingPilot(Ahead(25.f,.85f),.280f,.280f,0),
            FacingPilot(Ahead(11.5f,.87f),.240f,.080f,1),
            FacingPilot(Ahead(40.5f,.80f),.220f,.130f,2)};
}
std::vector<CockpitVertex> HeronInterior() {
    using namespace heron;
    std::vector<CockpitVertex> v;v.reserve(300000);
    const auto* c=hk::Find(CockpitKind::Heli602);if(!c)return v;
    auto spec=hk::Cabin(*c);spec.shallowEdges=true;const float floorY=spec.floorY;
    std::vector<std::vector<P>> windows;
    for(unsigned i=0;i<c->windowCount;++i)windows.push_back(hk::Loop(c->windows[i]));
    std::vector<CombatRegion> holes;
    for(const auto& w:windows)hk::Prisms(w,spec.cut,holes,spec.shallowEdges);
    hk::Zones(spec,holes);
    hk::Shell(v,*c,spec,holes);
    // Shared sides: one mullion; each window's frame a step further in.
    for(std::size_t i=0;i<windows.size();++i)hk::Frame(v,windows[i],spec,&windows,.0012f*float(i));
    auto wall=[&](float z,float y){return hk::WallX(*c,spec,z,y);};
    // Ribs round the hood behind the pilot, broken at its windows.
    for(float z:{-.83f,-.43f})hk::Rib(v,*c,spec,holes,z,spec.sill,.036f,.024f,Dark,spec.frame-spec.cut-.003f);
    // The monitor's edge: a rail along each wall at the seat's underside (up
    // to the canopy's front, where the nose's monitor takes all), and across
    // the deck where the floor under the seat ends. (Beside the seat the
    // fuselage narrows to a V under the deck: the monitor there is the wall's
    // band down to where the V comes in past the seat's width.)
    const auto edge=hk::ZoneEdge(*c,spec);
    for(float side:{-1.f,1.f}) {
        std::vector<P> rail;for(const auto& p:edge)if(p[2]<=canopyFront+1e-3f)rail.push_back({side*p[0],p[1],p[2]});
        hk::Rail(v,rail,.012f,Dark);
    }
    {const float w=wall(.10f,floorY)-.010f;hk::Rail(v,{{-w,floorY+.012f,.10f},{w,floorY+.012f,.10f}},.012f,Dark);}
    // The canopy's front edge: a rail round the shell's section there, where
    // the glass and the walls end and the nose's monitor begins.
    {
        const auto sec=hk::Cut(*c,spec,canopyFront,40);
        std::vector<P> ring;
        for(unsigned j=0;j<=40;++j)ring.push_back({-(sec.x[j]-.010f),sec.y[j],canopyFront});
        for(int j=40;j>=0;--j)ring.push_back({sec.x[unsigned(j)]-.010f,sec.y[unsigned(j)],canopyFront});
        std::vector<P> run;
        for(const auto& p:ring) {
            if(hk::InHole(holes,{p[0],p[1],canopyFront-.012f},.0f)||p[1]<floorY+.005f) {
                if(run.size()>1)hk::Rail(v,run,.012f,Dark);
                run.clear();continue;
            }
            run.push_back(p);
        }
        if(run.size()>1)hk::Rail(v,run,.012f,Dark);
    }
    // The keel: a box beam down the middle of the monitor floor from under the
    // seat into the nose, carrying the stick, the footrest and the screens.
    const float keelTop=floorY+.060f,keelEnd=1.040f;
    Box(v,{-.045f,floorY,.10f},{.045f,keelTop,keelEnd},Dark);
    RoundedPrism(v,{{-.045f,floorY,keelEnd},{.045f,floorY,keelEnd},{.045f,floorY,keelEnd+.040f},{-.045f,floorY,keelEnd+.040f}},{0,.030f,0},.006f,Dark);
    for(float z:{.18f,.46f,.72f})for(float x:{-.028f,.028f})FrameBolt(v,{x,keelTop+.002f,z},{0,1,0},.005f);
    // Seat, on a low plinth that fits the fuselage's V under it (the seat's
    // feet stand in it), and behind it the Nix's machinery: squat, under the
    // windows behind the pilot's head and over the narrow keel.
    Seat(v);
    RoundedPrism(v,{{-.150f,floorY,-.05f},{.150f,floorY,-.05f},{.240f,-.930f,-.05f},{-.240f,-.930f,-.05f}},{0,0,.25f},.010f,Dark);
    {const auto start=v.size();RearMachinery(v);MovePart(v,start,{.60f,.60f,.86f},{0,-.250f,-.060f});}
    // Cyclic: the Proteus stick on its console, on the keel between the knees.
    ProteusStickOn(v,.26f,.36f,HeronDeck,-.64f,false);
    // Collective beside the left thigh, its pivot on a bracket off the wall
    // (the floor there is the fuselage's narrow V).
    {
        // Between the seat's side and the wall; its throw shorter, the cabin
        // narrowing ahead. The bracket runs in from the wall to the pivot.
        const P pivot{-.28f,-.80f,-.18f},grip{-.28f,-.56f,.14f};
        const float w=wall(-.18f,-.80f)-.004f;
        RoundedPrism(v,{{-w,-.85f,-.28f},{-.255f,-.85f,-.28f},{-.255f,-.85f,-.08f},{-w,-.85f,-.08f}},{0,.100f,0},.008f,Dark);
        for(float z:{-.25f,-.11f})FrameBolt(v,{-(w+.255f)*.5f,-.749f,z},{0,1,0},.006f);
        Axle(v,pivot,.060f,.028f,Steel);
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
        const auto back=Unit(Cross(plate.right,plate.up));
        const auto start=v.size();
        // Five cells across (the user: still through the nose's sides at
        // seven -- one hole less each side).
        hk::LightenedPlate(v,plate,5,3,.075f,.026f,.006f,Steel);
        for(auto i=start;i<v.size();++i){v[i].colour[0]=.26f;v[i].colour[1]=.28f;v[i].colour[2]=.30f;}
        const auto heel=Point(plate,0,-.125f,-.010f);
        hk::Tube(v,{{-.205f,heel[1],heel[2]},{.205f,heel[1],heel[2]}},.014f,Steel);
        for(float side:{-1.f,1.f}) {
            Beam(v,{side*.045f,heel[1]-.026f,heel[2]},{side*.160f,heel[1]-.026f,heel[2]},.012f,Dark);
            FrameBolt(v,{side*.125f,heel[1]-.026f-.012f,heel[2]},{0,-1,0},.005f);
        }
        for(float lx:{-.055f,.055f})for(float ly:{-.030f,.060f}) {
            const auto from=Point(plate,lx,ly,-.004f);
            const float t=(from[1]-keelTop)/-back[1];
            Rod(v,from,Add(from,Mul(back,t+.004f)),.010f,Steel);
        }
        auto shoe=plate;shoe.c=Add(Point(plate,0,.015f,0),Mul(back,.020f));
        GripBody(v,shoe,.160f,.130f,.018f,Dark,.006f);
    }
    // The screens: the Nix's open rims, see-through, one over another. The
    // ammunition's lower rim sits on a clamp at the top of a slim column from
    // the keel's end; each screen's upper corners carry the next one's lower
    // corners on short links, outside the glass.
    const auto screens=HeronPanels();
    for(const auto& p:screens)Instrument(v,p);
    {
        const auto& ammo=screens[2];const auto& radar=screens[0];const auto& health=screens[1];
        auto clamp=ammo;clamp.c=Point(ammo,0,-ammo.h*.5f-.045f,-.030f);
        GripBody(v,clamp,.110f,.050f,.040f,Dark,.008f);
        const auto top=Point(ammo,0,-ammo.h*.5f-.050f,-.040f);const P foot{0,keelTop,keelEnd-.040f};
        Rod(v,top,foot,.016f,Steel);
        Rod(v,foot,Add(foot,{0,.050f,0}),.028f,Dark);
        for(float x:{-.030f,.030f})FrameBolt(v,{x,keelTop+.002f,keelEnd-.040f},{0,1,0},.006f);
        auto link=[&](const Panel& lower,const Panel& upper) {
            for(float s:{-1.f,1.f}) {
                const auto a=Point(lower,s*(lower.w*.5f+.040f),lower.h*.5f+.010f,-.020f);
                const auto b=Point(upper,s*(upper.w*.5f+.040f),-upper.h*.5f-.010f,-.020f);
                Rod(v,a,b,.008f,Steel);
                for(const auto& e:{std::pair{a,lower},std::pair{b,upper}}) {
                    auto lug=e.second;lug.c=e.first;GripBody(v,lug,.026f,.026f,.020f,Dark,.004f);
                }
            }
        };
        link(ammo,radar);link(radar,health);
    }
    // The canopy's glass, then the monitors' joints, last: the translucent pass.
    for(std::size_t i=0;i<windows.size();++i)hk::Pane(v,windows[i],.0012f*float(i));
    // The nose's monitor: joints drawn over the shape of the nose that is not
    // drawn any more (the shell's section ahead of the canopy, rings every
    // 15 cm, lines along it), so the monitor reads as a see-through nose.
    {
        auto section=[&](float z){return hk::Cut(*c,spec,z,24);};
        for(float z:{.74f,.89f,1.04f}) {
            const auto sec=section(z);std::vector<P> ring;
            for(unsigned j=0;j<=24;++j)ring.push_back({-sec.x[j],sec.y[j],z});
            for(int j=24;j>=0;--j)ring.push_back({sec.x[unsigned(j)],sec.y[unsigned(j)],z});
            for(std::size_t k=0;k+1<ring.size();++k)if(Dot(Sub(ring[k+1],ring[k]),Sub(ring[k+1],ring[k]))>1e-6f)hk::Ink(v,ring[k],ring[k+1]);
        }
        for(float t:{.25f,.55f,.85f})for(float side:{-1.f,1.f}) {
            std::vector<P> line;
            for(float z=canopyFront;z<=spec.front+1e-3f;z+=.05f) {
                const auto sec=section(z);const unsigned j=unsigned(t*24);
                line.push_back({side*sec.x[j],sec.y[j],z});
            }
            for(std::size_t k=0;k+1<line.size();++k)hk::Ink(v,line[k],line[k+1]);
        }
        std::vector<P> ridge;
        for(float z=canopyFront;z<=spec.front+1e-3f;z+=.05f){const auto sec=section(z);ridge.push_back({0,sec.y.back(),z});}
        for(std::size_t k=0;k+1<ridge.size();++k)hk::Ink(v,ridge[k],ridge[k+1]);
    }
    for(float side:{-1.f,1.f})for(float z:{-.10f,.25f}) {
        const float top=-.66f;std::vector<P> line;
        for(int k=0;k<=8;++k){const float y=floorY+(top-floorY)*float(k)/8;line.push_back({side*wall(z,y),y,z});}
        for(std::size_t k=0;k+1<line.size();++k)hk::Ink(v,line[k],line[k+1]);
    }
    for(float z:{.25f})
        for(float side:{-1.f,1.f})hk::Ink(v,{side*.045f,floorY,z},{side*wall(z,floorY),floorY,z});
    return v;
}
