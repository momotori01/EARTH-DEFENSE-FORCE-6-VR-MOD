// Euros (506), rebuilt (the user, 2026-09-25, one machine at a time). Tandem
// canopies, but the front one's nose is too thin for anyone: a long dashboard
// packed with machinery fills it, under the front canopy's glass, from the
// pilot's knees to the nose. One pilot under the rear canopy, 10 cm further
// back than before: the eye at model (0,2.13,1.37). The fuselage is shallow
// there, so the footwell hangs below its floor in the monitor's view: from the
// seat forward the floor and the walls to the seat's underside are monitor.
// The radar is let into the dashboard's face (opaque, inside the body); health
// and ammunition hang see-through on arms from the walls, in the view.
void HeliDisplayBezel(std::vector<CockpitVertex>& v,const Panel& p);
float EurosDeck(float) {return -1.105f;}
// The dashboard: its face from the leg tunnel's roof up to its top, turned to
// the eye through the radar's centre; the top plate at dashTop to the nose.
namespace euros {
constexpr float dashTop=-.220f,tunnelRoof=-.550f,frontBlock=.900f;
constexpr P radarCentre{0,-.385f,.535f};
}
std::array<Panel,3> EurosPanels() {
    return {FacingPilot(euros::radarCentre,.280f,.280f,0),
            FacingPilot({-.280f,-.220f,.320f},.240f,.100f,1),
            FacingPilot({.280f,-.240f,.320f},.240f,.160f,2)};
}
std::vector<CockpitVertex> EurosInterior() {
    using namespace euros;
    std::vector<CockpitVertex> v;v.reserve(300000);
    const auto* c=hk::Find(CockpitKind::Heli506);if(!c)return v;
    const auto spec=hk::Cabin(*c);const float floorY=spec.floorY;
    std::vector<std::vector<P>> windows;
    for(unsigned i=0;i<c->windowCount;++i)windows.push_back(hk::Loop(c->windows[i]));
    std::vector<CombatRegion> holes;
    for(const auto& w:windows)hk::Prisms(w,spec.cut,holes);
    hk::Zones(spec,holes);
    hk::Shell(v,*c,spec,holes);
    for(const auto& w:windows)hk::Frame(v,w,spec);
    // Airframe ribs: behind the canopy over the machinery, and the arch
    // between the two canopies.
    for(float z:{-.62f,-.47f,-.32f,.265f})hk::Rib(v,*c,spec,holes,z,spec.sill,.036f,.024f,Dark,spec.frame-spec.cut-.003f);
    auto wall=[&](float z,float y){return hk::WallX(*c,spec,z,y);};
    // The monitor's edge: a rail along each wall (under the windows beside
    // the pilot, down at the footwell), and on the deck round the floor under
    // the seat.
    const auto edge=hk::ZoneEdge(*c,spec);
    for(float side:{-1.f,1.f}) {
        std::vector<P> rail;for(const auto& p:edge)rail.push_back({side*p[0],p[1],p[2]});
        hk::Rail(v,rail,.012f,Dark);
        hk::Rail(v,{{side*(wall(-.50f,floorY)-.010f),floorY+.012f,-.50f},{side*.40f,floorY+.012f,-.50f},{side*.40f,floorY+.012f,.10f}},.012f,Dark);
    }
    hk::Rail(v,{{-.40f,floorY+.012f,.10f},{.40f,floorY+.012f,.10f}},.012f,Dark);
    // The keel: a box beam down the middle of the monitor floor from under the
    // seat to the dashboard's front block, carrying the stick and the footrest.
    const float keelTop=floorY+.060f;
    Box(v,{-.045f,floorY,.10f},{.045f,keelTop,frontBlock},Dark);
    for(float z:{.18f,.46f,.72f})for(float x:{-.028f,.028f})FrameBolt(v,{x,keelTop+.002f,z},{0,1,0},.005f);
    // Seat, and behind it the Nix's machinery: narrowed and set down into the
    // low gable behind the canopy (its cabinets' tops under the roof's slope).
    Seat(v);
    {const auto start=v.size();RearMachinery(v);MovePart(v,start,{.55f,1.f,.86f},{0,-.150f,-.060f});}
    // Cyclic: the Proteus stick on its console, on the keel between the knees.
    ProteusStickOn(v,.26f,.36f,EurosDeck,-.64f,false);
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
        const auto back=Unit(Cross(plate.right,plate.up));
        const auto start=v.size();
        hk::LightenedPlate(v,plate,8,3,.075f,.026f,.006f,Steel);
        for(auto i=start;i<v.size();++i){v[i].colour[0]=.26f;v[i].colour[1]=.28f;v[i].colour[2]=.30f;}
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
    // The dashboard's body, wall to wall: its face (turned to the eye through
    // the radar), the leg tunnel's roof under it, the front block's face down
    // to the deck, and the top plate forward to the nose.
    const auto screens=EurosPanels();
    const auto& radar=screens[0];
    const auto faceUp=radar.up,faceOut=Unit(Cross(radar.right,radar.up));   // faceOut: away from the pilot
    const float faceUpper=(dashTop-radarCentre[1])/faceUp[1],faceLower=(tunnelRoof-radarCentre[1])/faceUp[1];
    const P faceTop=Add(radarCentre,Mul(faceUp,faceUpper)),faceFoot=Add(radarCentre,Mul(faceUp,faceLower));
    constexpr float inset=.004f;
    auto across=[&](P a,P b,P toward,unsigned material,unsigned steps) {
        // A strip from a to b (both on the centre line), wall to wall at each end.
        for(unsigned k=0;k<steps;++k) {
            const P p=Add(a,Mul(Sub(b,a),float(k)/float(steps))),q=Add(a,Mul(Sub(b,a),float(k+1)/float(steps)));
            const float wp=wall(p[2],p[1])-inset,wq=wall(q[2],q[1])-inset;
            hk::Facing(v,{-wp,p[1],p[2]},{wp,p[1],p[2]},{wq,q[1],q[2]},{-wq,q[1],q[2]},toward,material);
        }
    };
    across(faceFoot,faceTop,Mul(faceOut,-1),Hull,8);
    across({0,tunnelRoof,faceFoot[2]},{0,tunnelRoof,frontBlock},{0,-1,0},Hull,6);
    across({0,floorY,frontBlock},{0,tunnelRoof,frontBlock},{0,0,-1},Hull,8);
    across({0,dashTop,faceTop[2]},{0,dashTop,spec.front},{0,1,0},Hull,unsigned((spec.front-faceTop[2])/.05f)+1);
    // Its edges: a rounded lip along the face's top and a rail at its foot,
    // and a rail along the top plate at each wall.
    {
        const float w0=wall(faceTop[2],faceTop[1])-inset,w1=wall(faceFoot[2],faceFoot[1])-inset;
        hk::Tube(v,{{-w0,faceTop[1],faceTop[2]},{w0,faceTop[1],faceTop[2]}},.014f,Dark);
        hk::Tube(v,{{-w1,faceFoot[1],faceFoot[2]},{w1,faceFoot[1],faceFoot[2]}},.012f,Dark);
        const float wb=wall(frontBlock,tunnelRoof)-inset;
        hk::Tube(v,{{-wb,tunnelRoof,frontBlock},{wb,tunnelRoof,frontBlock}},.012f,Dark);
        const float wf=wall(frontBlock,floorY)-inset;
        hk::Tube(v,{{-wf,floorY+.012f,frontBlock},{wf,floorY+.012f,frontBlock}},.012f,Dark);
        for(float side:{-1.f,1.f}) {
            std::vector<P> rail;
            for(float z=faceTop[2];z<spec.front-.02f;z+=.05f)rail.push_back({side*(wall(z,dashTop)-.016f),dashTop+.012f,z});
            hk::Rail(v,rail,.012f,Dark);
        }
    }
    // The radar let into the face, and small instruments either side of it.
    HeliDisplayBezel(v,radar);
    for(float side:{-1.f,1.f})for(int k=0;k<2;++k) {
        const P at=Add(Add(radarCentre,Mul(radar.right,side*(.270f+.090f*float(k)))),Mul(faceUp,.050f-.100f*float(k)));
        const Panel dial{at,radar.right,faceUp,0,0,0};
        Axle(v,Add(Point(dial,0,0,-.004f),Mul(faceOut,.010f)),.028f,.034f,Dark);
        Face(v,Panel{Point(dial,0,0,.011f),radar.right,faceUp,0,0,0},.040f,.040f,k?Amber:Cyan);
    }
    for(float side:{-1.f,1.f})for(int k=0;k<5;++k) {
        const P at=Add(Add(radarCentre,Mul(radar.right,side*(.200f+.035f*float(k)))),Mul(faceUp,-.165f));
        const Panel key{at,radar.right,faceUp,0,0,0};
        GripBody(v,key,.022f,.030f,.008f,k==2?Warning:Steel,.003f);
    }
    // On the top plate, forward to the nose: the machinery packed in it. A
    // finned cooling spine down the middle, a canister either side of it,
    // cable looms along the walls, vents and service plates on the spine,
    // and a gyro drum at the nose.
    {
        const float z0=faceTop[2]+.040f,z1=spec.front-.060f,spineEnd=z1-.200f,spineTop=dashTop+.070f;
        // The spine: a cooling fin block on its rear half (the fins stand on
        // it, 2 mm into it), two vents ahead of them.
        RoundedPrism(v,{{-.075f,dashTop,z0},{.075f,dashTop,z0},{.075f,dashTop,spineEnd},{-.075f,dashTop,spineEnd}},{0,.070f,0},.012f,Dark);
        for(int k=0;k<7;++k) {
            const float z=z0+.050f+.050f*float(k);
            RoundedPrism(v,{{-.062f,spineTop-.002f,z},{.062f,spineTop-.002f,z},{.062f,spineTop-.002f,z+.010f},{-.062f,spineTop-.002f,z+.010f}},{0,.032f,0},.003f,Steel);
        }
        for(float z:{z0+.430f,z0+.565f}) {
            const Panel vent{{0,spineTop+.001f,z},{1,0,0},{0,0,1},0,0,0};
            Part(v,referenceVent,vent,.110f,.130f,.012f,Steel);
        }
        // Canisters lying fore and aft, strapped down.
        for(float side:{-1.f,1.f}) {
            const float x=side*.170f,cz=z0+.22f;
            const P axisA{x,dashTop+.050f,cz-.150f},axisB{x,dashTop+.050f,cz+.150f};
            Rod(v,axisA,axisB,.048f,Steel);
            for(float t:{-.090f,.090f}) {
                Rod(v,{x,dashTop+.050f,cz+t-.009f},{x,dashTop+.050f,cz+t+.009f},.053f,Dark);
                Box(v,{x-.060f,dashTop,cz+t-.012f},{x+.060f,dashTop+.012f,cz+t+.012f},Dark);
            }
            // Its line into the spine.
            hk::Tube(v,{Add(axisB,{0,0,-.020f}),{x,dashTop+.050f,cz+.200f},{side*.090f,dashTop+.040f,cz+.240f},{side*.060f,dashTop+.040f,cz+.240f}},.010f,Cable);
            // Cable looms along the wall, clamped.
            std::vector<P> loom;
            for(float z=z0;z<z1;z+=.06f)loom.push_back({side*(wall(z,dashTop+.030f)-.045f),dashTop+.028f,z});
            hk::Tube(v,loom,.013f,Cable);
            for(std::size_t k=1;k+1<loom.size();k+=3) {
                const P a=loom[k];
                Box(v,{a[0]-.022f,dashTop,a[2]-.010f},{a[0]+.022f,dashTop+.046f,a[2]+.010f},Dark);
            }
        }
        // Service plates on the spine's flanks.
        for(float side:{-1.f,1.f})for(float z:{z0+.20f,z0+.56f}) {
            const Panel plate{{side*.076f,dashTop+.035f,z},{0,0,-side},{0,1,0},0,0,0};
            Part(v,referenceServicePlate,plate,.120f,.050f,.010f,Steel);
        }
        // The gyro drum at the nose, on a plinth ahead of the spine.
        {
            const float z=z1-.080f;
            Box(v,{-.090f,dashTop,z-.070f},{.090f,dashTop+.030f,z+.070f},Dark);
            Axle(v,{0,dashTop+.030f+.045f,z},.090f,.060f,Steel);
            for(float x:{-.040f,.040f})FrameBolt(v,{x,dashTop+.032f,z-.060f},{0,1,0},.005f);
        }
    }
    // Health and ammunition on short arms from the walls, a knuckle half way.
    for(const auto& p:screens)if(p.source)Instrument(v,p);
    for(unsigned k=1;k<3;++k) {
        const auto& p=screens[k];const float side=k==1?-1.f:1.f;
        // Its mount above the side monitor's edge.
        const auto backAt=Point(p,side*(p.w*.5f-.030f),0,-.075f);
        const float mountY=std::max(backAt[1],-.190f);
        const P atWall{side*(wall(backAt[2],mountY)-.004f),mountY,backAt[2]};
        auto socket=p;socket.c=Point(p,side*(p.w*.5f-.030f),0,-.058f);
        GripBody(v,socket,.050f,.070f,.030f,Dark,.006f);
        const P knuckle=Mul(Add(backAt,atWall),.5f);
        Rod(v,backAt,knuckle,.014f,Steel);Rod(v,knuckle,atWall,.014f,Steel);
        Axle(v,knuckle,.040f,.022f,Dark);
        const Panel mount{atWall,{0,0,side},{0,1,0},0,0,0};
        GripBody(v,mount,.070f,.090f,.018f,Dark,.006f);
        for(float t:{-.030f,.030f})FrameBolt(v,Add(atWall,{-side*.019f,t,0}),{-side,0,0},.005f);
    }
    // The canopy's glass, then the monitors' joints, last: the translucent pass.
    for(const auto& w:windows)hk::Pane(v,w);
    for(float side:{-1.f,1.f}) {
        // Up the walls, a joint every tile, to the monitor's edge there.
        for(float z:{-.30f,-.05f,.20f,.45f,.75f}) {
            float top=floorY;for(const auto& zone:spec.zones)if(z>zone.z0&&z<zone.z1)top=std::max(top,hk::ZoneTop(zone,z));
            std::vector<P> line;
            for(int k=0;k<=10;++k){const float y=floorY+(top-floorY)*float(k)/10;line.push_back({side*wall(z,y),y,z});}
            for(std::size_t k=0;k+1<line.size();++k)hk::Ink(v,line[k],line[k+1]);
        }
        // Along the side monitor at the seat's height.
        for(float z0=-.48f;z0<.60f;z0+=.08f) {
            const float y=-.46f,z1=std::min(.60f,z0+.08f);
            hk::Ink(v,{side*wall(z0,y),y,z0},{side*wall(z1,y),y,z1});
        }
    }
    for(float z:{.45f,.70f})
        for(float side:{-1.f,1.f})hk::Ink(v,{side*.045f,floorY,z},{side*wall(z,floorY),floorY,z});
    return v;
}
