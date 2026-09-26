// Helicopter cockpits (cockpit_heli_shells.h): the pilot sits under the real
// canopy. The canopy's own panes (measured from the model) are the windows,
// framed along their edges; the armour round them stays, lined at run time
// (HeliLining), except where it blocked the view ahead: there it is a monitor
// onto the world (HeliShell::monitors), outlined as the Nix's screen joints
// (HeliMonitorSeams). Below the sill a tub fitted inside the fuselage.
// Instruments are let into the panel as a real attack helicopter's are: radar
// in the middle. At the feet the floor is a monitor onto the ground below.
// Cabin coordinates: +X the pilot's right, +Y up, +Z forward.

// The lining: the canopy's own hull seen from inside. Of the hull triangles
// round the canopy, those the pilot would see from behind - the game draws the
// outside of them, so from the seat they are not there - are drawn turned to
// the seat, 4 mm in, less what lies in a monitor window. The glass stays open;
// what faces the seat (the nose seen through the windscreen) is the game's
// own. The hull bone's frame.
std::vector<CockpitVertex> HeliLining(CockpitKind kind,const std::vector<float>& hull) {
    std::vector<CockpitVertex> v;
    const auto* s=FindHeliShell(kind);if(!s)return v;
    if(hk::Find(kind))return v;   // a rebuilt machine's own shell closes its cabin
    const P origin{s->bodyOrigin[0],s->bodyOrigin[1],s->bodyOrigin[2]};
    const P eye{s->eye[0],s->eye[1],s->eye[2]};
    // The monitor windows as regions: inside where n.p <= n.eye on every edge.
    std::vector<CombatRegion> windows;
    for(unsigned m=0;m<s->monitorCount;++m) {
        CombatRegion r{};
        for(unsigned i=0;i<s->monitors[m].count&&r.count<16;++i) {
            HeliMonitorPlane(s->monitors[m],i,r.n[r.count]);
            r.d[r.count]=r.n[r.count][0]*eye[0]+r.n[r.count][1]*eye[1]+r.n[r.count][2]*eye[2];++r.count;
        }
        windows.push_back(r);
    }
    for(std::size_t i=0;i+9<=hull.size();i+=9) {
        const float* t=&hull[i];
        const float c[3]={(t[0]+t[3]+t[6])/3,(t[1]+t[4]+t[7])/3,(t[2]+t[5]+t[8])/3};
        if(!HeliRegion(*s,c)||HeliGlass(*s,t))continue;
        const P a{t[0],t[1],t[2]},b{t[3],t[4],t[5]},d{t[6],t[7],t[8]};
        auto n=Cross(Sub(b,a),Sub(d,a));if(Dot(n,n)<1e-12f)continue;
        n=Unit(n);
        // Faces the seat (the game's), or edge on to it (nothing to see).
        if(Dot(n,Unit(Sub(eye,P{c[0],c[1],c[2]})))>-.06f)continue;
        const auto in=Mul(n,-.004f);
        // A thin frame strip between two panes, once moved in, lies over the
        // glass: leave it to the canopy frame.
        const float moved[9]={a[0]+in[0],a[1]+in[1],a[2]+in[2],b[0]+in[0],b[1]+in[1],b[2]+in[2],d[0]+in[0],d[1]+in[1],d[2]+in[2]};
        if(HeliGlass(*s,moved))continue;
        // Less what a monitor window shows through.
        std::vector<CombatPolygon> pieces;
        CombatCutWindows({Add(a,in),Add(d,in),Add(b,in)},windows,0,pieces);
        for(const auto& poly:pieces)
            for(std::size_t k=1;k+1<poly.size();++k)Triangle(v,Sub(poly[0],origin),Sub(poly[k],origin),Sub(poly[k+1],origin),Hull);
    }
    return v;
}
namespace heli {
float deckHeight=-1.105f;
float Deck(float) {return deckHeight;}
// The tub's half width at z, at the floor and at the sill (stations, linear).
float TubWidth(const HeliShell& s,float z,bool sill) {
    const unsigned n=s.stationCount;const auto& st=s.stations;const int k=sill?2:1;
    if(z<=st[0][0])return st[0][k];
    for(unsigned i=0;i+1<n;++i)if(z<=st[i+1][0]) {
        const float t=(z-st[i][0])/(st[i+1][0]-st[i][0]);return st[i][k]+(st[i+1][k]-st[i][k])*t;
    }
    return st[n-1][k];
}
}
// The instrument panel, the displays let into it (radar in the middle, health
// on the left, ammunition on the right), each facing the pilot. Cabin
// coordinates. The panel's face passes through (0,-.38,.50) square to the eye;
// the displays stand 12 mm proud of it. A narrow machine's panel is narrower
// at its top edge, and its three screens shrink together to stay let into it.
std::array<Panel,3> HeliPanels(CockpitKind kind) {
    if(kind==CockpitKind::HeliNereid)return NereidPanels();
    if(kind==CockpitKind::Heli506)return EurosPanels();
    if(kind==CockpitKind::Heli602)return HeronPanels();
    if(kind==CockpitKind::HeliBrute)return BrutePanels();
    if(kind==CockpitKind::TruckPickup)return TruckPanels();
    float scale=1;
    if(const auto* s=FindHeliShell(kind)) {
        const float o1=s->eye[1]-kCockpitSeatedEye[1],floorY=s->floor-o1,sillY=s->sill-o1;
        const float y=-.38f+.13f*.781f,z=.50f+.13f*.625f;   // the face's top edge
        const float ws=heli::TubWidth(*s,z,true),wf=heli::TubWidth(*s,z,false);
        const float half=ws+(wf-ws)*(sillY-y)/(sillY-floorY)-.012f;
        scale=std::clamp((half-.005f)/.288f,.55f,1.f);
    }
    if(const auto* s=FindHeliShell(kind);s&&s->sideBySide>0) {
        // Two seats: the three screens side by side in the middle of the one
        // panel, between the pilots, each turned to the left seat.
        const float cx=s->eye[0];
        return {FacingPilot({cx,-.3725f,.4906f},.150f*scale,.150f*scale,0),
                FacingPilot({cx-.205f*scale,-.3725f,.4906f},.120f*scale,.120f*scale,1),
                FacingPilot({cx+.205f*scale,-.3725f,.4906f},.120f*scale,.120f*scale,2)};
    }
    return {FacingPilot({0,-.3725f,.4906f},.150f*scale,.150f*scale,0),
            FacingPilot({-.205f*scale,-.3725f,.4906f},.120f*scale,.120f*scale,1),
            FacingPilot({.205f*scale,-.3725f,.4906f},.120f*scale,.120f*scale,2)};
}
// A multi-function display's bezel: the case let into the panel, a raised
// frame round the glass and a row of push-buttons down each side.
void HeliDisplayBezel(std::vector<CockpitVertex>& v,const Panel& p) {
    // The case behind the glass: its face 5 mm behind the display.
    auto box=p;box.c=Point(p,0,0,-.020f);
    GripBody(v,box,p.w+.046f,p.h+.046f,.050f,Dark,.004f);
    const float hw=p.w*.5f+.012f,hh=p.h*.5f+.012f;
    for(const auto& [a,b]:{std::pair{P{-hw,-hh,0},P{hw,-hh,0}},std::pair{P{hw,-hh,0},P{hw,hh,0}},
            std::pair{P{hw,hh,0},P{-hw,hh,0}},std::pair{P{-hw,hh,0},P{-hw,-hh,0}}})
        Beam(v,Point(p,a[0],a[1],.020f),Point(p,b[0],b[1],.020f),.0045f,Bezel);
    for(float side:{-1.f,1.f})for(int k=0;k<4;++k) {
        auto key=p;key.c=Point(p,side*(p.w*.5f+.0175f),(k-1.5f)*p.h*.24f,.020f);
        GripBody(v,key,.012f,.016f,.006f,k==0?Warning:Steel,.002f);
    }
}
// A domed rivet head, 7 mm across, standing on a face along its axis: a
// light hexagonal dome, for rows of them along the frames.
void Rivet(std::vector<CockpitVertex>& v,P c,P axis) {
    constexpr float r=.0035f,h=.0022f;
    axis=Unit(axis);const auto x=Unit(Cross(axis,std::fabs(axis[1])<.95f?P{0,1,0}:P{1,0,0})),y=Cross(axis,x);
    auto ring=[&](int k,float radius,float up){const float a=k*1.0471976f;return Add(c,Add(Mul(axis,up),Add(Mul(x,radius*std::cos(a)),Mul(y,radius*std::sin(a)))));};
    const auto top=Add(c,Mul(axis,h));
    auto face=[&](P a,P b,P d){if(Dot(Cross(Sub(b,a),Sub(d,a)),Sub(Mul(Add(Add(a,b),d),1.f/3),c))<0)std::swap(b,d);Triangle(v,a,b,d,Steel);};
    for(int k=0;k<6;++k) {
        const auto a0=ring(k,r,-.0005f),a1=ring(k+1,r,-.0005f),b0=ring(k,r*.7f,h*.6f),b1=ring(k+1,r*.7f,h*.6f);
        face(a0,a1,b1);face(a0,b1,b0);face(b0,b1,top);
    }
}
// The canopy's frame: along each pane's edges a narrow band lying inside the
// pane, thickened inward. Where two panes meet, their bands make one member
// over the joint; where a pane meets the armour, its band is the frame. A row
// of rivets holds each band, 9 cm apart down its middle.
void HeliFrames(std::vector<CockpitVertex>& v,const std::vector<CombatFace>& faces) {
    constexpr float width=.016f,depth=.014f;
    for(const auto& f:faces) {
        const auto& q=f.poly;const std::size_t n=q.size();if(n<3)continue;
        P centre{};for(const auto& p:q)centre=Add(centre,Mul(p,1.f/n));
        std::vector<P> in(n);bool ok=true;
        for(std::size_t i=0;i<n&&ok;++i) {
            const auto& prev=q[(i+n-1)%n];const auto& cur=q[i];const auto& next=q[(i+1)%n];
            auto inward=[&](P a,P b){auto d=Unit(Cross(f.n,Sub(b,a)));if(Dot(d,Sub(centre,a))<0)d=Mul(d,-1);return d;};
            const auto d0=inward(prev,cur),d1=inward(cur,next);
            const auto e0=Unit(Sub(cur,prev));
            const float denom=Dot(e0,d1);
            if(std::fabs(denom)<1e-4f){ok=false;break;}
            in[i]=Add(cur,Add(Mul(d0,width),Mul(e0,(width-Dot(Mul(d0,width),d1))/denom)));
            // A corner too sharp for the band: pull its inner point back to the centre's side.
            if(Dot(Sub(in[i],cur),Sub(in[i],cur))>.02f)in[i]=Add(cur,Mul(Unit(Sub(centre,cur)),.05f));
        }
        if(!ok)continue;
        for(std::size_t i=0;i<n;++i) {
            const auto& a=q[i];const auto& b=q[(i+1)%n];
            if(Dot(Sub(b,a),Sub(b,a))<.0004f)continue;
            RoundedPrism(v,{a,b,in[(i+1)%n],in[i]},Mul(f.n,-depth),.002f,Dark);
            // Clear of the corners, where a sharp one pulls the band back.
            const float length=std::sqrt(Dot(Sub(b,a),Sub(b,a)));if(length<.14f)continue;
            auto across=Unit(Cross(f.n,Sub(b,a)));if(Dot(across,Sub(centre,a))<0)across=Mul(across,-1);
            const unsigned count=static_cast<unsigned>((length-.10f)/.09f)+1;
            for(unsigned k=0;k<count;++k) {
                const float t=.05f+(length-.10f)*(count>1?float(k)/(count-1):.5f);
                Rivet(v,Add(Add(a,Mul(Sub(b,a),t/length)),Add(Mul(across,width*.5f),Mul(f.n,-depth))),Mul(f.n,-1));
            }
        }
    }
}
// The monitors' joints (HeliShell::seams, research/cockpit/heli_shells.py):
// each window's outline where it cuts the lined armour, ending on the frames,
// and a band across it at mid height, drawn as the Nix's screen ink (surface
// 21), a centimetre toward the eye off the armour, as wide to the eye as the
// combat vehicles' wherever they stand. Appended last, after the glass.
void HeliMonitorSeams(std::vector<CockpitVertex>& v,const HeliShell& s) {
    const P eye{kCockpitSeatedEye[0],kCockpitSeatedEye[1],kCockpitSeatedEye[2]};
    const auto start=v.size();
    auto near=[&](P p){return Add(p,Mul(Unit(Sub(eye,p)),.010f));};
    for(unsigned i=0;i<s.seamCount;++i) {
        const auto ca=HeliCabinPoint(s,s.seams[i][0]),cb=HeliCabinPoint(s,s.seams[i][1]);
        const P a=near({ca[0],ca[1],ca[2]}),b=near({cb[0],cb[1],cb[2]});
        const float length=std::sqrt(Dot(Sub(b,a),Sub(b,a)));if(length<1e-4f)continue;
        const unsigned pieces=std::max(1u,static_cast<unsigned>(std::ceil(length/.10f)));
        auto half=[&](P x){const auto view=Sub(x,eye);return .0028f*std::max(.7f,std::sqrt(Dot(view,view)));};
        for(unsigned k=0;k<pieces;++k) {
            const P p=Add(a,Mul(Sub(b,a),float(k)/pieces)),q=Add(a,Mul(Sub(b,a),float(k+1)/pieces));
            const auto w=Unit(Cross(Sub(b,a),Sub(Mul(Add(p,q),.5f),eye)));
            Quad(v,Sub(p,Mul(w,half(p))),Add(p,Mul(w,half(p))),Add(q,Mul(w,half(q))),Sub(q,Mul(w,half(q))),Dark);
        }
    }
    for(auto i=start;i<v.size();++i){v[i].surface=21;for(auto& c:v[i].colour)c=.012f;}
}
std::vector<CockpitVertex> HeliInterior(CockpitKind kind) {
    if(kind==CockpitKind::HeliNereid)return NereidInterior();
    if(kind==CockpitKind::Heli506)return EurosInterior();
    if(kind==CockpitKind::Heli602)return HeronInterior();
    if(kind==CockpitKind::HeliBrute)return BruteInterior();
    if(kind==CockpitKind::TruckPickup)return TruckInterior();
    std::vector<CockpitVertex> v;v.reserve(240000);
    const auto* s=FindHeliShell(kind);if(!s)return v;
    auto cabin=[&](const float* m){const auto c=HeliCabinPoint(*s,m);return P{c[0],c[1],c[2]};};
    const float o1=s->eye[1]-kCockpitSeatedEye[1];
    const float floorY=s->floor-o1,sillY=s->sill-o1;
    heli::deckHeight=floorY;
    const float rear=s->stations[0][0],nose=s->stations[s->stationCount-1][0];
    // A tandem machine: the pilot's tub ends at pilotNose, and past it the
    // empty front station stands on its own (higher) floor.
    const float pilotNose=s->pilotNose,frontY=s->stations[s->stationCount-1][3]-o1;
    const bool tandem=s->frontSeat>0;
    // Two seats side by side (the Brute): the camera's seat is the left one,
    // so the cockpit's centreline is cx to the pilot's right. What is
    // symmetric about the machine is built about x = 0 and moved there; the
    // pilot's seat and controls are copied to the co-pilot's, 2 cx across.
    const bool pair=s->sideBySide>0;const float cx=pair?s->eye[0]:0.f;
    auto wf=[&](float z){return heli::TubWidth(*s,z,false);};
    auto ws=[&](float z){return heli::TubWidth(*s,z,true);};
    // The foot monitor: the footwell floor under the instrument panel, past
    // the seat cushion's shadow, is a screen onto the ground, framed like any
    // other; the pedals stand over it as the Nix's do. It keeps 6 cm of floor
    // either side and ends short of a short nose.
    const float footRear=.44f,footFront=std::min(.85f,pilotNose-.08f);
    const float footHalf=pair?std::min(.30f,cx-.10f):std::min(.44f,std::min(wf(footRear),wf(footFront))-.06f);
    const auto tubStart=v.size();
    // Tub: floor, walls up to the sill, the nose bulkhead. Every piece has
    // its thickness outward, so its inside face is where it was measured.
    std::vector<float> cuts;for(unsigned i=0;i<s->stationCount;++i)cuts.push_back(s->stations[i][0]);
    for(float z:{footRear,footFront})cuts.push_back(z);
    std::sort(cuts.begin(),cuts.end());
    for(std::size_t i=0;i+1<cuts.size();++i) {
        const float a=cuts[i],b=cuts[i+1];if(b-a<.005f)continue;
        const bool hole=a>=footRear-.001f&&b<=footFront+.001f;
        const float fy=a<pilotNose-.001f?floorY:frontY;
        if(!hole)Slab(v,{-wf(a),fy,a},{wf(a),fy,a},{wf(b),fy,b},{-wf(b),fy,b},{0,-.030f,0},Hull);
        else if(!pair)for(float side:{-1.f,1.f})
            Slab(v,{side*footHalf,fy,a},{side*wf(a),fy,a},{side*wf(b),fy,b},{side*footHalf,fy,b},{0,-.030f,0},Hull);
        else {   // a monitor under each pilot's feet, at -cx and +cx about the centreline
            for(float side:{-1.f,1.f})
                Slab(v,{side*(cx+footHalf),fy,a},{side*wf(a),fy,a},{side*wf(b),fy,b},{side*(cx+footHalf),fy,b},{0,-.030f,0},Hull);
            Slab(v,{-(cx-footHalf),fy,a},{cx-footHalf,fy,a},{cx-footHalf,fy,b},{-(cx-footHalf),fy,b},{0,-.030f,0},Hull);
        }
        for(float side:{-1.f,1.f}) {
            Slab(v,{side*wf(a),fy,a},{side*ws(a),sillY,a},{side*ws(b),sillY,b},{side*wf(b),fy,b},{side*.022f,0,0},Hull);
            // The sill: a folded rail along the wall's top, under the canopy's own frame.
            Beam(v,{side*(ws(a)-.012f),sillY-.012f,a},{side*(ws(b)-.012f),sillY-.012f,b},.014f,Bezel);
        }
    }
    Slab(v,{-wf(nose),tandem?frontY:floorY,nose},{wf(nose),tandem?frontY:floorY,nose},{ws(nose),sillY,nose},{-ws(nose),sillY,nose},{0,0,.022f},Hull);
    // The step up to the front station's floor at the pilot's nose.
    if(tandem&&frontY>floorY+.005f)
        Slab(v,{-wf(pilotNose),floorY,pilotNose},{wf(pilotNose),floorY,pilotNose},{wf(pilotNose),frontY,pilotNose},{-wf(pilotNose),frontY,pilotNose},{0,0,.022f},Hull);
    // The foot monitor's frame: a raised rim round the opening, bolted to the
    // floor, and the ink of the tile joint across it.
    if(!pair) {
        const P c[4]={{-footHalf,floorY+.006f,footRear},{footHalf,floorY+.006f,footRear},{footHalf,floorY+.006f,footFront},{-footHalf,floorY+.006f,footFront}};
        for(int i=0;i<4;++i)Beam(v,c[i],c[(i+1)%4],.012f,Dark);
        // On the floor round the opening, a short or narrow one's too.
        for(float x:{-.75f*footHalf,0.f,.75f*footHalf})for(float z:{footRear-.026f,footFront+.026f})FrameBolt(v,{x,floorY+.0015f,z},{0,1,0},.005f);
        for(float side:{-1.f,1.f})for(float t:{.27f,.73f})
            FrameBolt(v,{side*(footHalf+.026f),floorY+.0015f,footRear+(footFront-footRear)*t},{0,1,0},.005f);
    } else for(float at:{-cx,cx}) {
        const P c[4]={{at-footHalf,floorY+.006f,footRear},{at+footHalf,floorY+.006f,footRear},{at+footHalf,floorY+.006f,footFront},{at-footHalf,floorY+.006f,footFront}};
        for(int i=0;i<4;++i)Beam(v,c[i],c[(i+1)%4],.012f,Dark);
        for(float x:{-.20f,0.f,.20f})for(float z:{footRear-.026f,footFront+.026f})FrameBolt(v,{at+x,floorY+.0015f,z},{0,1,0},.005f);
    }
    // Rear bulkhead behind the seat, from the floor to the shoulders; the
    // canopy's lined hull closes the rest. Nix-like equipment on it: an
    // electronics case, a vent, the looms running down into the floor.
    {
        // Two seats side by side: the bulkhead closes the whole width behind them.
        const float top=.28f,topHalf=pair?ws(rear)-.03f:std::min(.27f,ws(rear)-.03f);
        std::vector<P> shape{{-wf(rear),floorY,rear},{wf(rear),floorY,rear},{ws(rear),sillY,rear},{topHalf,top,rear},{-topHalf,top,rear},{-ws(rear),sillY,rear}};
        RoundedPrism(v,shape,{0,0,-.030f},.006f,Hull);
        // The case stands behind the headrest (its back at -.415) only where
        // there is room for it and its lid: not in the 602's or 506's.
        const float wall=std::min(wf(rear),ws(rear));
        if(rear<-.55f) {
            Panel box{{0,-.02f,rear+.090f},{-1,0,0},{0,1,0},0,0,0};   // Cross(right,up) = -z: into the bulkhead
            ProteusCase(v,box,std::min(.42f,2*wall-.10f),.26f,.090f,true);
        }
        for(float side:{-1.f,1.f}) {
            const float ventX=std::min(.30f,wall-.10f);
            Panel vent{{side*ventX,-.46f,rear+.016f},{-1,0,0},{0,1,0},0,0,0};
            GripBody(v,vent,.16f,.22f,.016f,Dark,.004f);
            auto grille=vent;grille.c=Point(vent,0,0,.014f);Part(v,referenceVent,grille,.12f,.18f,.016f,Steel);
            Rod(v,{side*.16f,-.16f,rear+.030f},{side*.16f,floorY+.012f,rear+.030f},.012f,Cable);
            for(float y:{-.40f,-.75f}) {
                Panel clip{{side*.16f,y,rear+.012f},{-1,0,0},{0,1,0},0,0,0};
                GripBody(v,clip,.040f,.030f,.012f,Dark,.003f);
            }
        }
    }
    if(pair)MovePart(v,tubStart,{1,1,1},{cx,0,0});
    // Seat: the shared cabin seat, upright, on a bolted pedestal.
    const auto pilotStart=v.size();
    Seat(v,true);
    for(float side:{-1.f,1.f}) {
        Rod(v,{side*.18f,floorY+.005f,.00f},{side*.18f,-.80f,.03f},.023f,Steel);
        Rod(v,{side*.18f,floorY+.030f,.0025f},{side*.18f,floorY+.185f,.018f},.034f,Dark);
        Panel foot{{side*.18f,floorY,0},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,foot,.115f,.13f,.018f,Bezel);
        for(float z:{-.044f,.044f})FrameBolt(v,Point(foot,0,z,.018f),{0,1,0});
    }
    // Cyclic: the Proteus gunner's traced stick on its pedestal between the knees.
    ProteusStickOn(v,.26f,.36f,heli::Deck,-.64f,false);
    // Collective beside the left thigh: a pivot on its own floor box, the
    // lever running forward and up to a grip with its switch head.
    const auto collectiveStart=v.size();
    {
        const P pivot{-.34f,-.78f,-.14f},grip{-.34f,-.55f,.24f};
        RoundedPrism(v,{{-.40f,floorY,-.26f},{-.28f,floorY,-.26f},{-.28f,floorY,-.02f},{-.40f,floorY,-.02f}},{0,pivot[1]-floorY-.02f,0},.008f,Dark);
        Axle(v,pivot,.13f,.028f,Steel);
        for(float x:{-.407f,-.273f})FrameBolt(v,{x,pivot[1],pivot[2]},{x<-.34f?-1.f:1.f,0,0},.006f);   // on the axle's ends
        Rod(v,pivot,grip,.017f,Steel);
        const auto along=Unit(Sub(grip,pivot));
        Rod(v,Sub(grip,Mul(along,.07f)),Add(grip,Mul(along,.07f)),.022f,Rubber);
        const auto head=Add(grip,Mul(along,.09f));
        Box(v,Sub(head,{.030f,.028f,.030f}),Add(head,{.030f,.034f,.030f}),Dark);
        for(int k=0;k<3;++k)Rod(v,Add(head,{-.012f+k*.012f,.034f,-.008f}),Add(head,{-.012f+k*.012f,.046f,-.008f}),.006f,k?Steel:Warning);
    }
    const auto collectiveEnd=v.size();
    // Pedals, as the Nix's: clamped to the floor beside the foot monitor, the
    // carriers across it.
    for(float side:{-1.f,1.f}) {
        const auto start=v.size();
        // A footwell too narrow for the clamp outboard of the pedal (the
        // 506's) carries it on a shoe on the floor behind the foot monitor.
        const float clamp=std::min(footHalf+.08f,wf(.515f)-.07f);
        if(clamp-.18f>=.15f)PedalAssembly(v,side,.18f,clamp);
        else PedalAssembly(v,side,.18f,clamp,footRear+.02f,.017f,-1.143f+.019f);
        MovePart(v,start,{1,1,1},{0,floorY+1.143f,.0f});
    }
    if(pair) {
        // The co-pilot's seat, stick, collective and pedals: the pilot's, 2 cx
        // to his right. Between the two, a pedestal console under the panel.
        // Seats this close leave no room between them for the co-pilot's
        // collective: his is on his right, against the wall.
        const auto end=v.size();
        for(auto i=pilotStart;i<end;++i) {
            if(i>=collectiveStart&&i<collectiveEnd)continue;
            auto copy=v[i];copy.position[0]+=2*cx;v.push_back(copy);
        }
        for(auto i=collectiveStart;i<collectiveEnd;i+=3)for(int k:{0,2,1}) {
            auto copy=v[i+k];copy.position[0]=2*cx-copy.position[0];copy.normal[0]=-copy.normal[0];v.push_back(copy);
        }
        const float x0=cx-.12f,x1=cx+.08f,z0=.04f,z1=.44f,top=-.56f;
        if(2*cx>=.80f) {
        RoundedPrism(v,{{x0,floorY,z0},{x1,floorY,z0},{x1,floorY,z1},{x0,floorY,z1}},{0,top-floorY,0},.008f,Hull);
        Panel plate{{(x0+x1)*.5f,top+.002f,(z0+z1)*.5f},{1,0,0},{0,0,1},0,0,0};   // Cross(right,up) = -y: into the console
        GripBody(v,plate,x1-x0-.020f,z1-z0-.030f,.012f,Dark,.004f);
        for(int r=0;r<4;++r)for(int k=0;k<3;++k)
            Rod(v,{x0+.04f+k*.06f,top+.014f,z0+.07f+r*.09f},{x0+.04f+k*.06f,top+.034f,z0+.07f+r*.09f},.0055f,r==0&&k==1?Warning:Steel);
        for(int k=0;k<2;++k)Dial(v,{x0+.05f+k*.10f,top+.016f,z1-.05f},.030f);
        for(float z:{z0+.035f,z1-.035f})for(float x:{x0+.030f,x1-.030f})FrameBolt(v,{x,top+.0155f,z},{0,1,0},.004f);   // clear of the plate's cut corners
        }
    }
    // Side consoles along the walls at elbow height: serviceable cases, the
    // right one with a switch row.
    for(float side:{-1.f,1.f}) {
        if(pair)break;   // two seats: the windows come down to the seats; the pedestal console instead
        // The left one stands outboard of the collective.
        const float z0=-.34f,z1=.18f,top=-.40f,inner=side<0?.44f:.30f;
        // Its outer face meets the sloping wall at its top edge.
        auto wall=[&](float z){return ws(z)+(wf(z)-ws(z))*(sillY-top)/(sillY-floorY);};
        const float outer=std::min(wall(z0),wall(z1))-.012f;
        if(outer<=inner+.06f)continue;
        RoundedPrism(v,{{side*inner,floorY,z0},{side*outer,floorY,z0},{side*outer,floorY,z1},{side*inner,floorY,z1}},{0,top-floorY,0},.008f,Hull);
        // The top: a bolted switch plate, rows of toggles and a keypad on the right.
        Panel plate{{side*(inner+outer)*.5f,top+.002f,(z0+z1)*.5f},{1,0,0},{0,0,1},0,0,0};   // Cross(right,up) = -y: into the console
        GripBody(v,plate,outer-inner-.020f,z1-z0-.030f,.012f,Dark,.004f);
        const float w=outer-inner-.05f;
        for(float z:{z0+.07f,z0+.16f,z1-.10f})for(int k=0;k<4;++k) {
            const float x=side*(inner+.025f+w*(k+.5f)/4);
            Rod(v,{x,top+.014f,z},{x,top+.034f,z},.0055f,k==0&&z<z0+.1f?Warning:Steel);
        }
        // The keypad's columns that fit on the plate.
        if(side>0)for(int r=0;r<3;++r)for(int c=0;c<3;++c)if(inner+.052f+c*.030f<=outer-.030f)
            Box(v,{side*(inner+.03f+c*.030f),top+.014f,z1-.070f+r*.022f},{side*(inner+.052f+c*.030f),top+.022f,z1-.052f+r*.022f},Bezel);
        for(float z:{z0+.035f,z1-.035f})for(float x:{inner+.030f,outer-.030f})FrameBolt(v,{side*x,top+.0155f,z},{0,1,0},.004f);   // clear of the plate's cut corners
    }
    // The instrument panel under the windscreen: a face turned to the pilot,
    // the displays let into it, a glareshield over them.
    {
        const auto panelStart=v.size();
        const P centre{0,-.38f,.50f};
        const auto facing=Unit(Sub(kCockpitSeatedEye,centre));     // toward the eye
        const auto up=Unit(Sub(P{0,1,0},Mul(facing,Dot(P{0,1,0},facing))));
        const auto across=Cross(up,facing);
        auto at=[&](float x,float y,float back){return Add(centre,Add(Add(Mul(across,x),Mul(up,y)),Mul(facing,-back)));};
        // The face spans wall to wall; over and under it a deck runs forward
        // between the walls to the nose bulkhead, closing the panel's box.
        auto wall=[&](float y,float z){return ws(z)+(wf(z)-ws(z))*(sillY-y)/(sillY-floorY)-.012f;};
        const auto bottom=at(0,-.13f,0),top=at(0,.13f,0);
        const float hb=wall(bottom[1],bottom[2]),ht=wall(top[1],top[2]);
        std::vector<P> face{at(-hb,-.13f,0),at(hb,-.13f,0),at(ht,.13f,0),at(-ht,.13f,0)};
        RoundedPrism(v,face,Mul(facing,-.030f),.008f,Hull);
        auto deck=[&](float y,float from,float thick) {
            std::vector<float> zs{from};
            for(unsigned i=0;i<s->stationCount;++i)if(s->stations[i][0]>from+.01f&&s->stations[i][0]<=pilotNose+.001f)zs.push_back(s->stations[i][0]);
            for(std::size_t i=0;i+1<zs.size();++i) {
                const float a=zs[i],b=zs[i+1];
                Slab(v,{-wall(y,a),y,a},{wall(y,a),y,a},{wall(y,b),y,b},{-wall(y,b),y,b},{0,thick,0},Hull);
            }
        };
        deck(bottom[1]-.004f,bottom[2]-.012f,-.020f);
        deck(top[1],top[2]-.03f,.020f);
        // A tandem machine: a partition at the pilot's nose closes his panel
        // box and footwell, from the front station's floor to the top deck.
        if(tandem)Slab(v,{-wall(frontY,pilotNose),frontY,pilotNose},{wall(frontY,pilotNose),frontY,pilotNose},
            {wall(top[1],pilotNose),top[1]+.020f,pilotNose},{-wall(top[1],pilotNose),top[1]+.020f,pilotNose},{0,0,.020f},Hull);
        // Glareshield: a hood on the deck over the top of the face, its
        // rolled edge standing out toward the pilot, its sides on the walls.
        {
            const float y=top[1]+.020f,z0=top[2]-.075f,z1=top[2]+.10f,w1=wall(y,z1)-.004f;
            Slab(v,{-ht,y,z0},{ht,y,z0},{w1,y,z1},{-w1,y,z1},{0,.020f,0},Dark);
            Beam(v,{-ht,y+.010f,z0-.004f},{ht,y+.010f,z0-.004f},.014f,Rubber);
        }
        if(pair)MovePart(v,panelStart,{1,1,1},{cx,0,0});
        for(const auto& p:HeliPanels(kind))HeliDisplayBezel(v,p);
        const auto stripStart=v.size();
        // Under the displays, a strip of switches and two small gauges.
        for(int k=0;k<6;++k)Rod(v,at(-.15f+k*.06f,-.105f,.002f),at(-.15f+k*.06f,-.105f,-.024f),.007f,k==2?Warning:Steel);   // out of the face
        for(float x:{-.27f,.27f}) {
            Rod(v,at(x,-.02f,.004f),at(x,-.02f,-.012f),.030f,Dark);        // standby gauge case
            Rod(v,at(x,-.02f,-.012f),at(x,-.02f,-.015f),.022f,Glass);
        }
        if(pair) {
            // Beside the screens in the middle, each pilot's round flight
            // instruments, as on the S-64 reference: two rows of three in a
            // bolted cluster, outboard of the screens.
            for(float side:{-1.f,1.f}) {
                const float seat=side*(cx+.20f);
                Panel cluster{at(seat,-.02f,.002f),across,up,0,0,0};
                GripBody(v,cluster,.32f,.22f,.010f,Dark,.004f);
                for(int r=0;r<2;++r)for(int k=0;k<3;++k) {
                    const auto c=at(seat-.10f+k*.10f,-.070f+r*.10f,-.012f);
                    Rod(v,c,Add(c,Mul(facing,-.014f)),.040f,Dark);
                    Rod(v,Add(c,Mul(facing,.001f)),Add(c,Mul(facing,-.002f)),.033f,Glass);
                    Rod(v,Add(c,Mul(facing,.002f)),Add(c,Add(Mul(facing,.002f),Mul(up,.030f))),.003f,Warning);
                }
            }
            MovePart(v,stripStart,{1,1,1},{cx,0,0});
        }
    }
    // The empty front station of a tandem machine: a low seat on its floor
    // (it has 0.9 m under the canopy), and the gunner's panel under the
    // windscreen with two screens, dark.
    if(tandem) {
        // Pan 18 cm over its floor, the back leaning 18 degrees behind it,
        // its headrest a hand below the pilot's eyes.
        const float pan=frontY+.18f,z=s->frontSeat;
        Panel seat{{0,pan,z+.10f},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,seat,.36f,.22f,.05f,Hull);
        auto cushion=seat;cushion.c=Point(seat,0,0,.012f);GripBody(v,cushion,.32f,.20f,.018f,Rubber);
        const P lean{0,.9510565f,-.3090170f};
        const float height=std::min(.62f,-.04f-pan);
        Panel back{Add({0,pan,z},Mul(lean,height*.5f)),{-1,0,0},lean,0,0,0};
        const std::array<std::array<float,2>,8> shell{{{-.18f,-height*.5f},{-.22f,-height*.2f},{-.20f,height*.4f},{-.13f,height*.5f},
            {.13f,height*.5f},{.20f,height*.4f},{.22f,-height*.2f},{.18f,-height*.5f}}};
        Armour(v,back,shell,.055f,Steel,.014f);
        auto pad=back;pad.c=Point(back,0,-height*.08f,.030f);GripBody(v,pad,.30f,height*.62f,.020f,Rubber);
        auto head=back;head.c=Point(back,0,height*.40f,.050f);GripBody(v,head,.20f,.10f,.045f,Hull);
        for(float side:{-1.f,1.f}) {
            Rod(v,{side*.14f,frontY+.005f,z+.10f},{side*.14f,pan-.05f,z+.10f},.018f,Steel);
            Panel foot{{side*.14f,frontY,z+.10f},{1,0,0},{0,0,1},0,0,0};
            GripBody(v,foot,.08f,.10f,.014f,Bezel);
        }
        auto wallF=[&](float y,float z){return ws(z)+(wf(z)-ws(z))*(sillY-y)/(sillY-frontY)-.012f;};
        const float z0=nose-.12f,y0=frontY+.30f,y1=std::min(sillY,frontY+.50f);
        const float b0=wallF(y0,z0),b1=wallF(y1,z0+.08f);
        RoundedPrism(v,{{-b0,y0,z0},{b0,y0,z0},{b1,y1,z0+.08f},{-b1,y1,z0+.08f}},{0,0,.030f},.008f,Hull);
        Slab(v,{-b1,y1,z0+.06f},{b1,y1,z0+.06f},{wallF(y1,nose),y1,nose},{-wallF(y1,nose),y1,nose},{0,.018f,0},Dark);
        for(float x:{-.09f,.09f}) {
            const float yc=(y0+y1)*.5f,zc=z0+.04f-.006f;
            Box(v,{x-.075f,yc-.060f,zc-.012f},{x+.075f,yc+.060f,zc},Bezel);
            Box(v,{x-.062f,yc-.048f,zc-.016f},{x+.062f,yc+.048f,zc-.010f},Glass);
        }
    }
    // Canopy: along every pane's edge a frame, and the pane's own glass.
    {
        std::vector<CombatFace> faces;
        const P eye=kCockpitSeatedEye;
        for(unsigned i=0;i<s->paneCount;++i) {
            const auto& pane=s->panes[i];if(pane.count<3)continue;
            CombatFace f{};for(unsigned k=0;k<pane.count;++k)f.poly.push_back(cabin(pane.p[k]));
            auto n=Cross(Sub(f.poly[1],f.poly[0]),Sub(f.poly[2],f.poly[0]));
            // Its first three corners nearly in line: the normal from every edge.
            if(Dot(n,n)<1e-10f){n={};for(std::size_t k=0;k<f.poly.size();++k)n=Add(n,Cross(f.poly[k],f.poly[(k+1)%f.poly.size()]));}
            float area=0;for(std::size_t k=1;k+1<f.poly.size();++k){const auto c=Cross(Sub(f.poly[k],f.poly[0]),Sub(f.poly[k+1],f.poly[0]));area+=.5f*std::sqrt(Dot(c,c));}
            n=Unit(n);if(Dot(n,Sub(f.poly[0],eye))<0)n=Mul(n,-1);   // outward, away from the pilot
            f.n=n;f.d=Dot(n,f.poly[0]);f.kind=0;
            // A sliver of glass narrower than its own frame would be (a joint
            // strip: under 3 cm across, twice its area over its outline) is the
            // frame, a solid member; a small pane wider than that is a window
            // (the user: the 602's triangle over its windscreen must be clear).
            float outline=0;for(std::size_t k=0;k<f.poly.size();++k){const auto e=Sub(f.poly[(k+1)%f.poly.size()],f.poly[k]);outline+=std::sqrt(Dot(e,e));}
            if(area<.022f&&2*area/std::max(outline,1e-4f)<.030f){RoundedPrism(v,f.poly,Mul(n,-.014f),.002f,Dark);continue;}
            faces.push_back(f);
        }
        HeliFrames(v,faces);
        const auto glass=v.size();
        for(const auto& f:faces) {
            P centre{};for(const auto& p:f.poly)centre=Add(centre,Mul(p,1.f/f.poly.size()));
            // The glass sits in the frame, 6 mm in from the pane's outer face.
            std::vector<P> inner;for(const auto& p:f.poly)inner.push_back(Add(Add(p,Mul(Sub(centre,p),.04f)),Mul(f.n,-.006f)));
            for(std::size_t k=1;k+1<inner.size();++k) {
                const auto c=Cross(Sub(inner[k],inner[0]),Sub(inner[k+1],inner[0]));if(Dot(c,c)<1e-12f)continue;
                Triangle(v,inner[0],inner[k],inner[k+1],Glass);
            }
        }
        for(auto i=glass;i<v.size();++i){v[i].surface=24;v[i].colour[0]=.020f;v[i].colour[1]=.070f;v[i].colour[2]=.060f;}
    }
    HeliMonitorSeams(v,*s);
    // A sliver pane built solid can round off to nothing at a corner: drop
    // triangles with no area left (none in the Nereid or the 602).
    std::vector<CockpitVertex> kept;kept.reserve(v.size());
    for(std::size_t i=0;i+2<v.size();i+=3) {
        const P a{v[i].position[0],v[i].position[1],v[i].position[2]},b{v[i+1].position[0],v[i+1].position[1],v[i+1].position[2]},
            c{v[i+2].position[0],v[i+2].position[1],v[i+2].position[2]};
        const auto n=Cross(Sub(b,a),Sub(c,a));if(Dot(n,n)<=1e-18f)continue;
        kept.insert(kept.end(),v.begin()+i,v.begin()+i+3);
    }
    return kept;
}
