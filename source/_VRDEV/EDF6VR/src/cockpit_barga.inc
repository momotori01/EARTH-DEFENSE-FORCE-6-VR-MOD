// 2.30m panoramic sphere, independent of the giant's native model scale.
// Local +X right, +Y up, +Z forward. No opaque panoramic sphere: the live
// stereo world supplies the all-round image behind subtle panel seams.
namespace {
const Panel bargaPanels[]={
    FacingPilot({-.48f,-.46f,.72f},.34f,.34f,0), // radar, lower left
    FacingPilot({ .48f,-.43f,.74f},.38f,.15f,1), // health, lower right
    FacingPilot({-.47f, .43f,.64f},.34f,.13f,2), // one weapon, upper left
};
P BargaSphere(float latitude,float longitude,float radius=kBargaSphereRadius) {
    return Add(kBargaSphereCentre,{radius*std::cos(latitude)*std::sin(longitude),radius*std::sin(latitude),radius*std::cos(latitude)*std::cos(longitude)});
}
// Smooth rectangular-section latitude ring. Broad flat faces and small edge
// fillets retain an engineered frame instead of a round hose or faceted rods.
void BargaRing(std::vector<CockpitVertex>& v,float height) {
    const float dy=height-kBargaSphereCentre[1],r=std::sqrt(kBargaSphereRadius*kBargaSphereRadius-dy*dy)-.018f;
    constexpr unsigned slices=144,sides=16;
    struct Ring{P p[sides],n[sides];};std::array<Ring,slices+1> rings{};
    for(unsigned i=0;i<=slices;++i) {
        const float angle=float(i)*6.2831853f/slices;const P radial{std::sin(angle),0,std::cos(angle)};
        const P c{r*radial[0],height,r*radial[2]};
        for(unsigned j=0;j<sides;++j) {
            const unsigned corner=j/4,step=j%4;const float a=(float(corner)+float(step)/3)*1.5707963f;
            const float x=(corner==0||corner==3?1.f:-1.f)*.013f+.003f*std::cos(a);
            const float y=(corner<2?1.f:-1.f)*.022f+.003f*std::sin(a);
            rings[i].p[j]=Add(c,Add(Mul(radial,x),P{0,y,0}));
            rings[i].n[j]=Add(Mul(radial,std::cos(a)),P{0,std::sin(a),0});
        }
    }
    for(unsigned i=0;i<slices;++i)for(unsigned j=0;j<sides;++j) {
        const unsigned k=(j+1)%sides;const auto& a=rings[i];const auto& b=rings[i+1];
        CurvedQuad(v,a.p[j],a.p[k],b.p[k],b.p[j],a.n[j],a.n[k],b.n[k],b.n[j],Steel);
    }
}
P BargaRingPoint(float height,float angle) {
    const float y=height-kBargaSphereCentre[1],r=std::sqrt(kBargaSphereRadius*kBargaSphereRadius-y*y)-.018f;
    return {r*std::sin(angle),height,r*std::cos(angle)};
}
Panel BargaRingPad(float height,float angle) {
    const auto c=BargaRingPoint(height,angle);
    const auto n=Unit(Sub(c,kBargaSphereCentre));
    const P right{std::cos(angle),0,-std::sin(angle)};
    return {Sub(c,Mul(n,.020f)),right,Cross(n,right),0,0,0};
}
void BargaPanelMount(std::vector<CockpitVertex>& v,const Panel& p,float height) {
    const float s=p.c[0]<0?-1.f:1.f;
    const auto hinge=Point(p,s*(p.w*.5f+.013f),0,-.070f);
    const float angle=std::atan2(hinge[0],hinge[2]);const auto pad=BargaRingPad(height,angle);
    GripBody(v,pad,.095f,.085f,.026f,Dark,.004f);
    for(float x:{-.031f,.031f})FrameBolt(v,Point(pad,x,0,.0165f),Mul(Unit(Cross(pad.right,pad.up)),-1),.005f);
    const auto root=Point(pad,0,0,.056f),surface=Point(pad,0,0,.012f);
    Beam(v,surface,root,.018f,Steel);Beam(v,root,hinge,.013f,Steel);
    Rod(v,Sub(root,Mul(pad.right,.023f)),Add(root,Mul(pad.right,.023f)),.025f,Dark);
    auto shoe=p;shoe.c=Point(p,s*(p.w*.5f+.013f),0,-.055f);
    GripBody(v,shoe,.032f,.064f,.016f,Dark,.002f);
    Rod(v,Add(hinge,{0,-.027f,0}),Add(hinge,{0,.027f,0}),.014f,Steel);
    const auto knob=Add(root,Mul(pad.right,s*.030f));
    Rod(v,Add(root,Mul(pad.right,s*.018f)),Add(root,Mul(pad.right,s*.038f)),.018f,Dark);
    Beam(v,Add(knob,{0,-.028f,0}),Add(knob,{0,.028f,0}),.007f,Dark);
}
void BargaSeam(std::vector<CockpitVertex>& v,bool meridian,float fixed) {
    constexpr unsigned n=160;const auto start=v.size();
    for(unsigned i=0;i<n;++i) {
        const float a=meridian?(-1.56f+3.12f*i/n):6.2831853f*i/n;
        const float b=meridian?(-1.56f+3.12f*(i+1)/n):6.2831853f*(i+1)/n;
        const float half=.0019f; // readable black tile joints, still translucent
        Quad(v,BargaSphere(meridian?a:fixed-half,meridian?fixed-half:a),
            BargaSphere(meridian?a:fixed+half,meridian?fixed+half:a),
            BargaSphere(meridian?b:fixed+half,meridian?fixed+half:b),
            BargaSphere(meridian?b:fixed-half,meridian?fixed-half:b),Dark);
    }
    for(auto i=start;i<v.size();++i){v[i].surface=21;for(auto& c:v[i].colour)c=.012f;}
}
std::vector<CockpitVertex> BargaInterior() {
    std::vector<CockpitVertex> v;v.reserve(280000);
    Seat(v,true);
    // A compact suspended chair spine replaces floor legs and the Nix's
    // generator mounts. Four bolts connect the seat shell to the same spine.
    auto rear=SeatBack();rear.c=Point(rear,0,-.055f,-.083f);
    GripBody(v,rear,.40f,.64f,.095f,Dark,.010f);
    for(float x:{-.145f,.145f})for(float y:{-.22f,.23f})FrameBolt(v,Point(rear,x,y,.0165f),Mul(Unit(Cross(rear.right,rear.up)),-1),.006f);
    Panel under{{0,-.84f,-.06f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,under,.44f,.46f,.10f,Hull,.009f);
    auto hatch=under;hatch.c=Point(under,0,0,-.101f);hatch.up=Mul(hatch.up,-1);
    Part(v,referenceServicePlate,hatch,.28f,.27f,.018f,Steel);
    // Two clevis arms carry the chair back to a shaped rear screen anchor.
    Panel wall{{0,-.57f,-.980f},{-1,0,0},{0,1,0},0,0,0};
    GripBody(v,wall,.40f,.33f,.027f,Hull,.010f);
    for(float s:{-1.f,1.f}) {
        const P seatEnd{s*.16f,-.75f,-.28f},bend{s*.19f,-.77f,-.58f},end{s*.14f,-.61f,-.950f};
        Beam(v,seatEnd,bend,.042f,Steel);Beam(v,bend,end,.039f,Steel);
        FrameJoint(v,bend,seatEnd,end,.037f);Axle(v,seatEnd,.070f,.064f,Dark);Axle(v,end,.060f,.053f,Dark);
        FrameBolt(v,Point(wall,s*.12f,.095f,.0165f),{0,0,1},.007f);
        FrameBolt(v,Point(wall,s*.12f,-.095f,.0165f),{0,0,1},.007f);
        // Controls and their full-size fore/aft mechanism are seat-mounted.
        const auto begin=v.size();PalmLever(v,s);MovePart(v,begin,{1,1,1},{0,.025f,-.08f});
        Beam(v,{s*.22f,-.79f,-.14f},{s*.35f,-.70f,-.10f},.034f,Steel);
        Beam(v,{s*.35f,-.70f,-.10f},{s*.35f,-.535f,-.10f},.031f,Dark);
        // Feet hang from the chair pan via angled structural legs. The
        // pedal assembly is unchanged in size, moved 10cm up and 10cm back.
        // Keep the support shoe outside the pedal AND its outer shaft cap.
        // The old 33cm anchor put its 12.5cm shoe across the heel face.
        constexpr float pedalAnchor=.435f;
        const auto pedalStart=v.size();PedalAssembly(v,s,.22f,pedalAnchor);MovePart(v,pedalStart,{1,1,1},{0,.10f,-.10f});
        const P hip{s*.18f,-.89f,.075f},knee{s*.25f,-1.005f,.25f},foot{s*pedalAnchor,-.953f,.415f};
        Beam(v,hip,knee,.030f,Steel);Beam(v,knee,foot,.028f,Steel);FrameJoint(v,knee,hip,foot,.025f);
        Axle(v,hip,.067f,.043f,Dark);
    }
    constexpr float lower=-.66f,upper=.43f;
    BargaRing(v,lower);BargaRing(v,upper);
    // The two ring levels land on a rear meridian bracket on the spherical
    // screen. They do not sprout from the pilot's shoulder or pass through it.
    const P railTop=BargaRingPoint(upper,3.14159265f),railBottom=BargaRingPoint(lower,3.14159265f);
    CrawlerBow(v,{{railBottom,{0,-.45f,-1.125f},{0,.10f,-1.10f},railTop}},.025f);
    for(float level:{upper,lower}) {
        const auto saddle=BargaRingPad(level,3.14159265f);GripBody(v,saddle,.13f,.10f,.028f,Dark,.003f);
        for(float x:{-.04f,.04f})FrameBolt(v,Point(saddle,x,0,.0165f),Mul(Unit(Cross(saddle.right,saddle.up)),-1),.005f);
    }
    Beam(v,{0,-.60f,-.986f},{0,-.60f,-1.040f},.041f,Dark); // anchor to rear ring backbone
    // Discrete camera/processor units seated on the ring, with vents, lenses,
    // service hatches, and clamps. Leave the forward view and live HUD open.
    for(const auto spec:{P{upper,1.20f,0},P{upper,-1.34f,1},P{upper,2.32f,0},P{lower,-1.62f,1},P{lower,1.63f,0}}) {
        auto p=BargaRingPad(spec[0],spec[1]);GripBody(v,p,.18f,.11f,.030f,Dark,.005f);
        p.c=Point(p,0,0,.041f);GripBody(v,p,.23f,.13f,.045f,Hull,.006f);
        auto grille=p;grille.c=Point(p,-.047f,0,.026f);Part(v,referenceVent,grille,.105f,.073f,.012f,Steel);
        // The lens barrel stands on the housing's face (15 mm out).
        const auto front=Point(p,.070f,0,.027f),axis=Mul(Unit(Cross(p.right,p.up)),-1);
        Rod(v,Sub(front,Mul(axis,.012f)),front,.026f,Steel);Rod(v,front,Add(front,Mul(axis,.003f)),.018f,Dark);
        for(float x:{-.095f,.095f})FrameBolt(v,Point(p,x,.043f,.0165f),axis,.004f);
    }
    for(const auto& p:bargaPanels){Instrument(v,p);BargaPanelMount(v,p,p.source==2?upper:lower);}
    // Last in the buffer: draw these thin seams with transparency, not as
    // opaque cockpit walls. The image stays visible in every direction.
    for(float latitude:{-.85f,-.15f,.75f})BargaSeam(v,false,latitude);
    for(unsigned k=0;k<8;++k)BargaSeam(v,true,(float(k)+.5f)*6.2831853f/8);
    return v;
}
#include "cockpit_barga_caps.inc"
void DishFinish(std::vector<CockpitVertex>& v,std::size_t first,bool rim=false) {
    for(auto i=first;i<v.size();++i){v[i].surface=rim?23.f:22.f;for(auto& c:v[i].colour)c=rim?.065f:.090f;}
}
void BargaDish(std::vector<CockpitVertex>& v,const BargaDishProfile& d) {
    constexpr unsigned slices=96,rings=16;constexpr float thickness=.10f;
    auto radial=[&](float azimuth){return Add(Mul(d.right,std::cos(azimuth)),Mul(d.up,std::sin(azimuth)));};
    auto direction=[&](float theta,float azimuth){return Add(Mul(d.axis,std::cos(theta)),Mul(radial(azimuth),std::sin(theta)));};
    // Analytic normals give a continuous, shallow bowl. The
    // pilot-facing skin is concave, matching the enlarged panoramic sphere.
    {
        const float r=d.radius,sign=-1.f;
        for(unsigned ring=0;ring<rings;++ring)for(unsigned segment=0;segment<slices;++segment) {
            const float a=d.angle*ring/rings,b=d.angle*(ring+1)/rings;
            const float u=6.2831853f*segment/slices,w=6.2831853f*(segment+1)/slices;
            const auto na=direction(a,u),nb=direction(b,u),nc=direction(b,w),nd=direction(a,w);
            const auto start=v.size();
            if(!ring)CurvedTriangle(v,Add(d.centre,Mul(na,r)),Add(d.centre,Mul(nb,r)),Add(d.centre,Mul(nc,r)),Mul(na,sign),Mul(nb,sign),Mul(nc,sign),Hull);
            else CurvedQuad(v,Add(d.centre,Mul(na,r)),Add(d.centre,Mul(nb,r)),Add(d.centre,Mul(nc,r)),Add(d.centre,Mul(nd,r)),Mul(na,sign),Mul(nb,sign),Mul(nc,sign),Mul(nd,sign),Hull);
            DishFinish(v,start,ring==rings-1);
        }
    }
    // A half-round edge joins the face to its return collar; no thin silhouette or
    // sharp polygon corners. The narrow dark-grey lip is the same solid paint.
    constexpr unsigned edgeSteps=10;
    for(unsigned s=0;s<slices;++s)for(unsigned j=0;j<edgeSteps;++j) {
        const float u=6.2831853f*s/slices,w=6.2831853f*(s+1)/slices;
        auto edge=[&](float azimuth,unsigned step,P& p,P& n) {
            const float t=3.14159265f*step/edgeSteps;
            const auto dir=direction(d.angle,azimuth),tangent=Add(Mul(d.axis,-std::sin(d.angle)),Mul(radial(azimuth),std::cos(d.angle)));
            n=Add(Mul(dir,-std::cos(t)),Mul(tangent,std::sin(t)));
            p=Add(d.centre,Add(Mul(dir,d.radius+thickness*.5f),Mul(n,thickness*.5f)));
        };
        P a,b,c,e,na,nb,nc,ne;edge(u,j,a,na);edge(w,j,b,nb);edge(w,j+1,c,nc);edge(u,j+1,e,ne);
        const auto start=v.size();CurvedQuad(v,a,b,c,e,na,nb,nc,ne,Hull);DishFinish(v,start,true);
    }
    // Closed return collar, seated 16cm inside the actual native opening.
    // The old second curved skin left an air gap behind the rim, visible
    // during punches. This skirt and its back now enclose the whole lip.
    auto seal=[&](P p){return Add(p,Mul(d.sealAxis,Dot(Sub(d.sealCentre,p),d.sealAxis)));};
    const auto backCentre=seal(Add(d.centre,Mul(d.axis,(d.radius+thickness)*std::cos(d.angle))));
    for(unsigned s=0;s<slices;++s) {
        const float u=6.2831853f*s/slices,w=6.2831853f*(s+1)/slices;
        const auto a=Add(d.centre,Mul(direction(d.angle,u),d.radius+thickness));
        const auto b=Add(d.centre,Mul(direction(d.angle,w),d.radius+thickness));
        const auto c=seal(b),e=seal(a);
        auto sideNormal=[&](float angle){const auto tangent=Add(Mul(d.right,-std::sin(angle)),Mul(d.up,std::cos(angle)));
            auto n=Unit(Cross(tangent,d.sealAxis));if(Dot(n,radial(angle))<0)n=Mul(n,-1);return n;};
        const auto na=sideNormal(u),nb=sideNormal(w);const auto first=v.size();
        CurvedQuad(v,a,b,c,e,na,nb,nb,na,Hull);
        CurvedTriangle(v,backCentre,e,c,d.sealAxis,d.sealAxis,d.sealAxis,Hull);DishFinish(v,first);
    }
    // Twin concentric guide channels sit in the spherical outer race. Their
    // recessed middle and raised lips read as a sliding bearing, not painted
    // concentric circles. All surfaces follow the same spherical curvature.
    auto band=[&](float from,float to,float offset,bool dark) {
        for(unsigned s=0;s<slices;++s) {
            const float a=6.2831853f*s/slices,b=6.2831853f*(s+1)/slices;
            const auto na=direction(from,a),nb=direction(to,a),nc=direction(to,b),nd=direction(from,b);
            const float r=d.radius-offset;const auto start=v.size();
            CurvedQuad(v,Add(d.centre,Mul(na,r)),Add(d.centre,Mul(nb,r)),Add(d.centre,Mul(nc,r)),Add(d.centre,Mul(nd,r)),Mul(na,-1),Mul(nb,-1),Mul(nc,-1),Mul(nd,-1),Hull);
            // Close both raised edges back to the dish face.
            for(float t:{from,to}) {
                const auto u=direction(t,a),w=direction(t,b);
                Quad(v,Add(d.centre,Mul(u,r)),Add(d.centre,Mul(w,r)),Add(d.centre,Mul(w,d.radius)),Add(d.centre,Mul(u,d.radius)),Hull);
            }
            DishFinish(v,start,dark);
        }
    };
    for(float centre:{.855f,.925f}) {
        band(d.angle*(centre-.018f),d.angle*(centre+.018f),.006f,true);
        band(d.angle*(centre-.026f),d.angle*(centre-.018f),.016f,false);
        band(d.angle*(centre+.018f),d.angle*(centre+.026f),.016f,false);
    }
    // Small keeper shoes bridge the outer race, with accessible fasteners.
    // The narrower front race stays exposed between the shoes.
    for(unsigned i=0;i<6;++i) {
        const float a=6.2831853f*(float(i)+.5f)/6;const float t=d.angle*.935f;
        const auto n=direction(t,a),right=Add(Mul(d.right,-std::sin(a)),Mul(d.up,std::cos(a)));
        Panel shoe{Add(d.centre,Mul(n,d.radius-.010f)),right,Cross(n,right),0,0,0};
        const auto start=v.size();GripBody(v,shoe,.22f,.14f,.025f,Dark,.009f);DishFinish(v,start,true);
        for(float x:{-.069f,.069f}){const auto bolt=v.size();FrameBolt(v,Point(shoe,x,0,.0165f),Mul(n,-1),.020f);DishFinish(v,bolt);}
    }
}
}
std::vector<CockpitVertex> BuildBargaJointCap(unsigned variant,unsigned part) {
    std::vector<CockpitVertex> v;if(variant>1||part>=5)return v;
    if(part<3){BargaDish(v,bargaDishes[variant][part]);return v;}
    const auto& cap=bargaCaps[variant][part];P centre{};
    for(unsigned i=0;i<cap.count;++i)centre=Add(centre,Mul(cap.points[i],1.f/cap.count));
    for(unsigned i=0;i<cap.count;++i)Triangle(v,centre,cap.points[i],cap.points[(i+1)%cap.count],Dark);
    DishFinish(v,0);
    return v;
}
