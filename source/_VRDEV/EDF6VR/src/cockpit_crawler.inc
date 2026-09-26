// Depth Crawler: dimensions measured against both Groundrobo MDBs.
// Cabin origin is body + (0,1.16292,.05), i.e. bind Y=2.45m.
// Keep adult-scale seat/controls; contract the surrounding shell, not the pilot.
Panel FacingPilot(P centre,float w,float h,unsigned source) {
    const auto out=Unit(Sub(centre,kCockpitSeatedEye));
    const auto right=Unit(Cross(P{0,1,0},out));
    return {centre,right,Cross(out,right),w,h,source};
}
const Panel crawlerPanels[]={
 // Side windows, 74-87cm from the neutral eye: health left, radar/ammo right.
 // Preserve a clear centre window and direct each face toward the pilot.
 FacingPilot({.32f,-.06f,.55f},.24f,.24f,0),
 FacingPilot({-.32f,-.17f,.55f},.26f,.13f,1), // slim health strip, like Barga
 FacingPilot({.32f,-.38f,.55f},.26f,.22f,2),
};
void MovePart(std::vector<CockpitVertex>& v,std::size_t start,P scale,P offset) {
    for(auto i=start;i<v.size();++i){P normal{};for(int k=0;k<3;++k){v[i].position[k]=v[i].position[k]*scale[k]+offset[k];normal[k]=v[i].normal[k]/scale[k];}
        normal=Unit(normal);std::copy(normal.begin(),normal.end(),v[i].normal);}
}
// Bolted removable fairing. The rounded folded edge has thickness; grille and
// fasteners are on its inward-facing surface, never buried in the cover.
void CrawlerCover(std::vector<CockpitVertex>& v,const Panel& p,float w,float h,float depth) {
    GripBody(v,p,w,h,depth,Hull,.009f);
    auto inset=p;inset.c=Point(p,0,0,.018f);GripBody(v,inset,w-.04f,h-.04f,.012f,Bezel,.008f);
    const float edgeInset=std::min(w,h)*.10f+.017f;
    for(float x:{-1.f,1.f})for(float y:{-1.f,1.f})
        FrameBolt(v,Point(p,x*(w*.5f-edgeInset),y*(h*.5f-edgeInset),.035f),Mul(Unit(Cross(p.right,p.up)),-1),.004f);
}
P CrawlerCurve(const std::array<P,4>& p,float t) {
    const float u=1-t;
    return Add(Add(Mul(p[0],u*u*u),Mul(p[1],3*u*u*t)),Add(Mul(p[2],3*u*t*t),Mul(p[3],t*t*t)));
}
P CrawlerTangent(const std::array<P,4>& p,float t) {
    const float u=1-t;
    return Unit(Add(Add(Mul(Sub(p[1],p[0]),u*u),Mul(Sub(p[2],p[1]),2*u*t)),Mul(Sub(p[3],p[2]),t*t)));
}
// A continuous rectangular tube swept around the rounded nose. Smooth along
// the bow, flat across its four sides, with small real fillets on the edges.
void CrawlerBow(std::vector<CockpitVertex>& v,const std::array<P,4>& p,float radius) {
    struct Ring {P centre,tangent;std::array<P,16> points,normals;};
    std::array<Ring,21> rings{};const float fillet=.003f;
    for(unsigned i=0;i<rings.size();++i) {
        auto& r=rings[i];const float t=float(i)/(rings.size()-1),u=1-t;r.centre=CrawlerCurve(p,t);
        r.tangent=Unit(Add(Add(Mul(Sub(p[1],p[0]),u*u),Mul(Sub(p[2],p[1]),2*u*t)),Mul(Sub(p[3],p[2]),t*t)));
        const auto a=Unit(Cross(r.tangent,P{1,0,0})),b=Cross(r.tangent,a);
        for(unsigned corner=0;corner<4;++corner)for(unsigned step=0;step<4;++step) {
            const float angle=(float(corner)+float(step)/3)*1.5707963f;
            const float ca=std::cos(angle),sa=std::sin(angle);
            const float sx=corner==0||corner==3?1.f:-1.f,sy=corner<2?1.f:-1.f;
            const auto k=corner*4+step;r.normals[k]=Add(Mul(a,ca),Mul(b,sa));
            r.points[k]=Add(r.centre,Add(Mul(a,sx*(radius-fillet)+fillet*ca),Mul(b,sy*(radius-fillet)+fillet*sa)));
        }
    }
    for(unsigned i=0;i+1<rings.size();++i)for(unsigned k=0;k<16;++k) {
        const unsigned j=(k+1)%16;const auto& a=rings[i];const auto& b=rings[i+1];
        CurvedQuad(v,a.points[k],a.points[j],b.points[j],b.points[k],a.normals[k],a.normals[j],b.normals[j],b.normals[k],Steel);
    }
    for(unsigned i:{0u,20u})for(unsigned k=0;k<16;++k) {
        const auto& r=rings[i];const auto n=Mul(r.tangent,i?1.f:-1.f);
        CurvedTriangle(v,r.centre,r.points[k],r.points[(k+1)%16],n,n,n,Steel);
    }
}
// Each monitor has its own cantilever. A straight shared upright crossed the
// curved canopy bow: use a hollow saddle around the bow, not a second tube
// through it. Both the saddle and its return faces follow the actual curve.
void CrawlerMonitorMount(std::vector<CockpitVertex>& v,const std::array<P,4>& bow,const Panel& p) {
    const float s=p.c[0]>0?1.f:-1.f;
    const auto hinge=Point(p,s*(p.w*.5f+.010f),0,-.070f);
    float lo=0,hi=1;
    for(unsigned i=0;i<20;++i) {const float mid=(lo+hi)*.5f;
        if(CrawlerCurve(bow,mid)[1]>hinge[1])lo=mid;else hi=mid;}
    const float t=(lo+hi)*.5f;
    const auto centre=CrawlerCurve(bow,t),tangent=CrawlerTangent(bow,t);
    const auto a=Unit(Cross(tangent,P{1,0,0})),b=Cross(tangent,a),inboard=Mul(b,s);
    constexpr unsigned count=5,sides=8;
    std::array<std::array<std::array<P,sides>,2>,count> rings{};
    const auto delta=Sub(CrawlerCurve(bow,t+.001f),CrawlerCurve(bow,t-.001f));
    const float halfSpan=.042f/std::sqrt(Dot(delta,delta))*.002f;
    for(unsigned i=0;i<count;++i) {
        const float u=t+halfSpan*(2.f*i/(count-1)-1.f);
        const auto c=CrawlerCurve(bow,u),direction=CrawlerTangent(bow,u);
        const auto x=Unit(Cross(direction,P{1,0,0})),y=Cross(direction,x);
        for(unsigned layer=0;layer<2;++layer) {
            const float r=layer?.032f:.0243f,cut=layer?.003f:.0008f;
            const float xy[sides][2]={{r,r-cut},{r-cut,r},{-r+cut,r},{-r,r-cut},
                {-r,-r+cut},{-r+cut,-r},{r-cut,-r},{r,-r+cut}};
            for(unsigned j=0;j<sides;++j)rings[i][layer][j]=Add(c,Add(Mul(x,xy[j][0]),Mul(y,xy[j][1])));
        }
    }
    for(unsigned i=0;i+1<count;++i)for(unsigned j=0;j<sides;++j) {
        const unsigned k=(j+1)%sides;
        Quad(v,rings[i][1][j],rings[i][1][k],rings[i+1][1][k],rings[i+1][1][j],Dark);
        Quad(v,rings[i][0][k],rings[i][0][j],rings[i+1][0][j],rings[i+1][0][k],Dark);
    }
    for(unsigned j=0;j<sides;++j) {const unsigned k=(j+1)%sides;
        Quad(v,rings[0][0][j],rings[0][0][k],rings[0][1][k],rings[0][1][j],Steel);
        Quad(v,rings[count-1][0][k],rings[count-1][0][j],rings[count-1][1][j],rings[count-1][1][k],Steel);
    }
    // Bolted saddle face and a stand-off hinge: every arm ends outside the
    // main tube. A large inward-facing wing knob is reachable by the pilot.
    Panel pad{Sub(centre,Mul(a,.032f)),b,tangent,0,0,0};
    GripBody(v,pad,.056f,.075f,.004f,Steel,.002f);
    for(float y:{-.026f,.026f})FrameBolt(v,Point(pad,0,y,.0165f),Mul(a,-1),.0045f);
    const auto pivot=Sub(centre,Mul(a,.066f));
    Beam(v,Point(pad,0,0,.012f),pivot,.012f,Steel);
    Rod(v,Sub(pivot,Mul(inboard,.018f)),Add(pivot,Mul(inboard,.018f)),.020f,Steel);
    const auto knob=Add(pivot,Mul(inboard,.029f));
    Rod(v,Add(pivot,Mul(inboard,.017f)),Add(pivot,Mul(inboard,.035f)),.018f,Dark);
    Beam(v,Sub(knob,Mul(tangent,.029f)),Add(knob,Mul(tangent,.029f)),.0075f,Dark);
    FrameBolt(v,Add(pivot,Mul(inboard,.0365f)),inboard,.004f);
    Beam(v,hinge,pivot,.010f,Steel);
    // Separate saddle release lever suggests sliding/reclamping the mount
    // along the canopy bow; the wing knob above locks the arm's angle.
    const auto release=Add(Add(centre,Mul(inboard,.037f)),Mul(tangent,.022f));
    Rod(v,Sub(release,Mul(inboard,.011f)),Add(release,Mul(inboard,.005f)),.009f,Steel);
    Beam(v,release,Add(release,Add(Mul(tangent,.034f),Mul(inboard,.007f))),.006f,Dark);
    auto shoe=p;shoe.c=Point(p,s*(p.w*.5f+.010f),0,-.055f);
    GripBody(v,shoe,.027f,.066f,.013f,Dark,.002f);
    Rod(v,Add(hinge,{0,-.024f,0}),Add(hinge,{0,.024f,0}),.013f,Steel);
}
// Native leg bearings are visible below the cabin. These are only the narrow
// floor rails and pedal mounting feet, with no opaque front-leg liners.
void CrawlerFloorRail(std::vector<CockpitVertex>& v,float s) {
    CrawlerBow(v,{{{s*.505f,-1.196f,-.56f},{s*.555f,-1.196f,.46f},
        {s*.49f,-1.196f,.74f},{s*.37f,-1.196f,.91f}}},.021f);
    // The pedal's outboard bearing block bolts to this crossmember.
    const P a{s*.36f,-1.143f,.515f},b{s*.493f,-1.196f,.515f};
    Beam(v,a,b,.018f,Steel);
    Panel foot{b,{1,0,0},{0,0,1},0,0,0};
    GripBody(v,foot,.070f,.075f,.018f,Dark,.003f);
    FrameBolt(v,Point(foot,0,0,.0165f),{0,1,0},.005f);
}
// A deep, chamfered cheek follows the hull beside the shoulder. Its sloping
// inner face and angled ends replace the flat full-height side wall.
void CrawlerWeaponCheek(std::vector<CockpitVertex>& v,float s) {
    const std::array<std::array<float,2>,8> profile{{
        {-.47f,-.55f},{-.38f,-.68f},{.12f,-.68f},{.27f,-.50f},
        {.27f,.12f},{.11f,.18f},{-.34f,.15f},{-.47f,.03f}}};
    std::vector<P> face;
    for(const auto& zy:profile)face.push_back({s*(.474f-.10f*(zy[1]+.20f)),zy[1],zy[0]});
    const P outward=Unit(P{s,.10f,0});
    RoundedPrism(v,face,Mul(outward,.074f),.008f,Hull);
    // Captive perimeter seam, a recessed cooler and a small connector make
    // this a weapon feed/cooling enclosure instead of an unexplained box.
    for(unsigned i=0;i<face.size();++i) {
        const auto j=(i+1)%face.size();
        const auto a=Add(Mul(Sub(face[i],P{s*.474f,-.20f,-.16f}),.92f),P{s*.474f,-.20f,-.16f});
        const auto b=Add(Mul(Sub(face[j],P{s*.474f,-.20f,-.16f}),.92f),P{s*.474f,-.20f,-.16f});
        Beam(v,Sub(a,Mul(outward,.002f)),Sub(b,Mul(outward,.002f)),.0015f,Dark);
    }
    Panel weapon{{s*.474f,-.20f,-.17f},{0,0,-s},Unit(P{-s*.10f,1,0}),0,0,0};
    auto hatch=weapon;hatch.c=Point(weapon,0,0,.012f);
    GripBody(v,hatch,.35f,.28f,.013f,Dark,.004f);
    auto cooling=hatch;cooling.c=Point(hatch,0,.015f,.023f);
    Part(v,referenceVent,cooling,.29f,.18f,.015f,Steel);
    auto connector=weapon;connector.c=Point(weapon,0,-.285f,.012f);
    Part(v,referenceConnector,connector,.11f,.10f,.019f,Steel);
    for(float u:{-.245f,.245f})for(float y:{-.29f,.235f})
        // This pressing's surface is at depth zero, not GripBody's +15mm.
        // Seat the underside of each 4mm screw head into that exact plane.
        FrameBolt(v,Point(weapon,u,y,.0015f),Mul(outward,-1),.005f);
    const auto port=Point(connector,0,-.04f,.020f);
    const P end{s*.435f,-.83f,-.20f};
    Rod(v,port,end,.009f,Cable);
    Axle(v,end,.020f,.020f,Steel); // gland seated on the console's outer face
    // Visible edge tabs carry the shell on the rear post and on a lower rail.
    // The old diagonal brace cut through the sloping inner face of the pod.
    auto rearTab=weapon;rearTab.c={s*.458f,-.10f,-.490f};
    GripBody(v,rearTab,.120f,.075f,.035f,Dark,.003f);
    Beam(v,{s*.493f,-.10f,-.532f},{s*.53f,-.10f,-.532f},.017f,Steel);
    for(float u:{-.035f,.035f})FrameBolt(v,Point(rearTab,u,0,.0165f),Mul(outward,-1),.004f);
    auto lowerTab=weapon;lowerTab.c={s*.514f,-.665f,.04f};
    GripBody(v,lowerTab,.11f,.085f,.035f,Dark,.003f);
    Beam(v,{s*.55f,-.710f,.04f},{s*.53f,-.745f,.14f},.017f,Steel);
    for(float u:{-.033f,.033f})FrameBolt(v,Point(lowerTab,u,0,.0165f),Mul(outward,-1),.004f);
}
std::array<P,4> CrawlerCanopyBow(float side) {
    return {{{side*.48f,.40f,.10f},{side*.57f,.37f,.60f},
        {side*.47f,-.65f,1.04f},{side*.37f,-1.14f,.91f}}};
}
void CrawlerMainMonitorSeams(std::vector<CockpitVertex>& v) {
    // The canopy is the inner support; the bowed screen skin and its seams
    // pass outside it. Continuous paths have no loose ends on a frame member.
    // Keep the forward centre clear, with joints at the roof and footwell.
    const auto low=CrawlerCurve(CrawlerCanopyBow(1),.70f);
    const float y=low[1],x=low[0]+.060f,z=low[2]+.055f;
    MonitorSeamPath(v,std::array<P,7>{{{-.575f,y,-.62f},{-.58f,y,.57f},{-x,y,z},
        {0,y,z+.075f},{x,y,z},{.58f,y,.57f},{.575f,y,-.62f}}});
    MonitorSeamPath(v,std::array<P,7>{{{-.51f,.20f,-.62f},{-.565f,.20f,.20f},
        {-.43f,.43f,.32f},{0,.447f,.38f},{.43f,.43f,.32f},
        {.565f,.20f,.20f},{.51f,.20f,-.62f}}});
}
std::vector<CockpitVertex> CrawlerInterior() {
    std::vector<CockpitVertex> v;v.reserve(420000);
    // Rounded nose bows sit inside the crawler's bulbous body. No neck cowl.
    const P rear[2]={{-.44f,.34f,-.54f},{.44f,.34f,-.54f}};
    Beam(v,rear[0],rear[1],.026f,Steel);
    Beam(v,{-.52f,-1.09f,-.55f},{.52f,-1.09f,-.55f},.028f,Steel);
    Beam(v,{-.37f,-1.14f,.91f},{.37f,-1.14f,.91f},.022f,Steel);
    for(float s:{-1.f,1.f}) {
        const P heel{s*.52f,-1.09f,-.55f},shoulder{s*.53f,.02f,-.53f},crown{s*.44f,.34f,-.54f};
        const auto bow=CrawlerCanopyBow(s);const auto brow=bow.front(),toe=bow.back();
        const P chain[]={heel,shoulder,crown,brow};
        for(unsigned i=0;i<3;++i)Beam(v,chain[i],chain[i+1],.024f,Steel);
        for(unsigned i=1;i<3;++i)FrameJoint(v,chain[i],chain[i-1],chain[i+1],.026f);
        CrawlerBow(v,bow,.024f);
        FrameJoint(v,brow,crown,CrawlerCurve(bow,.10f),.026f);
        FrameJoint(v,crown,shoulder,{-s*.44f,.34f,-.54f},.028f);
        const P sill{s*.53f,-.745f,.14f};
        const std::array<P,4> waist{{sill,{s*.63f,-.86f,.49f},{s*.52f,-1.04f,.80f},toe}};
        Beam(v,heel,sill,.025f,Steel);CrawlerBow(v,waist,.023f);
        FrameJoint(v,sill,heel,CrawlerCurve(waist,.12f),.027f);
        FrameJoint(v,toe,CrawlerCurve(bow,.9f),{-s*.37f,-1.14f,.91f},.024f);
        for(const auto& panel:crawlerPanels)if(panel.c[0]*s>0)CrawlerMonitorMount(v,bow,panel);
        CrawlerWeaponCheek(v,s);
        CrawlerFloorRail(v,s);
        // Rear upright and low truss surround the cheek. No diagonal member
        // runs through its face; the two edge tabs carry it instead.
        // Console pedestal bears on a rear post and an inclined forward stay.
        const P rearFoot{s*.32f,-1.095f,-.20f},rearTop{s*.32f,-.953f,-.20f};
        const P frontFoot{s*.32f,-1.095f,0},frontTop{s*.32f,-.912f,.20f};
        for(auto pair:{std::pair<P,P>{rearFoot,rearTop},std::pair<P,P>{frontFoot,frontTop}}) {
            Beam(v,pair.first,pair.second,.023f,Dark);
            Panel shoe{pair.first,{1,0,0},{0,0,1},0,0,0};GripBody(v,shoe,.11f,.095f,.020f,Steel);
            for(float dz:{-.028f,.028f})FrameBolt(v,Point(shoe,0,dz,.0165f),{0,1,0});
        }
        // Original reference grips, full size, brought just 3cm inward.
        const auto leverStart=v.size();PalmLever(v,s);MovePart(v,leverStart,{1,1,1},{-s*.030f,0,0});
        Panel pedestal{{s*.32f,-.672f,-.0244f},{1,0,0},{0,.10f,.995f},0,0,0};
        GripBody(v,pedestal,.225f,.64f,.268f,Dark);
        auto accessDeck=pedestal;accessDeck.c=Point(pedestal,0,-.19f,.016f);
        Part(v,referenceServicePlate,accessDeck,.17f,.16f,.016f,Steel);
        // Unscaled pedal faces/bearings land on dedicated lower-rail brackets.
        PedalAssembly(v,s,.21f,.36f);
    }
    // Closed aft tub, low roof header and close-fitting power module.
    Panel wall{{0,0,-.58f},{-1,0,0},{0,1,0},0,0,0};
    const std::array<std::array<float,2>,8> rearOutline{{{-.50f,-1.13f},{.50f,-1.13f},{.54f,-1.06f},{.56f,.01f},
        {.45f,.365f},{-.45f,.365f},{-.56f,.01f},{-.54f,-1.06f}}};
    Armour(v,wall,rearOutline,.06f,Hull,.009f);
    auto start=v.size();RearMachinery(v);MovePart(v,start,{.82f,.91f,.86f},{0,-.025f,-.060f});
    Seat(v);
    // Solid pan only under the seat. Ahead of it, the existing pedal
    // carriers and the side rails bridge a clear lower observation opening.
    Panel floor{{0,-1.115f,-.29f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,floor,1.03f,.64f,.055f,Steel);
    // Only the central base/cabin coupling needs a new cover. The native
    // column reaches bind Y=1.584 (local -.866); a compact plinth below the
    // seat surrounds it and meets the original turntable at Y~1.10.
    Panel coupling{{0,-.835f,-.05f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,coupling,.62f,.62f,.50f,Dark,.012f);
    // Close the broad turntable-to-cabin seam, not only the narrow centre
    // column. A shallow rounded cap sits above the native 61cm-radius base;
    // its outer bearings and the four leg crossmembers stay fully visible.
    std::vector<P> turntableCap;
    for(unsigned i=0;i<32;++i){const float a=float(i)*6.2831853f/32;
        turntableCap.push_back({.596f*std::cos(a),-1.262f,-.05f+.596f*std::sin(a)});}
    RoundedPrism(v,turntableCap,{0,-.065f,0},.004f,Steel);
    for(unsigned i=0;i<8;++i){const float a=(float(i)+.5f)*6.2831853f/8;
        FrameBolt(v,{.515f*std::cos(a),-1.2605f,-.05f+.515f*std::sin(a)},{0,1,0},.005f);}
    Panel service{{0,-1.055f,.267f},{1,0,0},{0,-1,0},0,0,0};
    Part(v,referenceServicePlate,service,.23f,.17f,.016f,Steel);
    for(float x:{-.255f,.255f})FrameBolt(v,{x,-.8185f,.17f},{0,1,0},.005f);
    // Compact saddle at the actual native gatling joint (Y=-1.095/Z=.866).
    // The carrier sits below and ahead of the pedal sweep, not through it.
    Panel bed{{0,-1.203f,.83f},{1,0,0},{0,1,0},0,0,0};
    GripBody(v,bed,.72f,.075f,.10f,Dark,.004f);
    Panel keel{{0,-1.197f,.02f},{1,0,0},{0,1,0},0,0,0};
    GripBody(v,keel,.105f,.065f,.81f,Hull,.003f);
    Panel raceway{{0,-1.153f,.44f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,raceway,.085f,.57f,.009f,Dark,.002f);
    for(float z:{.21f,.44f,.67f})FrameBolt(v,{0,-1.134f,z},{0,1,0},.004f);
    Beam(v,{0,-1.143f,.02f},{0,-1.197f,.045f},.037f,Dark);
    Panel gunRoot{{0,-1.095f,.82f},{1,0,0},{0,1,0},0,0,0};
    CrawlerCover(v,gunRoot,.15f,.16f,.11f);
    Rod(v,{0,-1.095f,.91f},{0,-1.095f,.945f},.062f,Steel);
    auto feed=gunRoot;feed.c=Point(gunRoot,0,0,.039f);
    Part(v,referenceServicePlate,feed,.097f,.105f,.012f,Steel);
    for(float side:{-1.f,1.f}) {
        const P a{side*.065f,-1.172f,.864f},b{side*.15f,-1.165f,.87f};
        Beam(v,a,b,.012f,Steel);FrameBolt(v,b,{0,1,0},.005f);
    }
    for(const auto& p:crawlerPanels)Instrument(v,p);
    CrawlerMainMonitorSeams(v);
    return v;
}
