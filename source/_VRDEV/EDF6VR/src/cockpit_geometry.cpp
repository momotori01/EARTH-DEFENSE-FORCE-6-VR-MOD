#include "cockpit.h"
#include "cockpit_combat_shells.h"
#include "cockpit_heli_shells.h"
#include <cmath>
#include <algorithm>
#include <iterator>
#include <map>

namespace edf6vr {
namespace {
using P=std::array<float,3>;
P Add(P a,P b) {return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
P Mul(P a,float s) {return {a[0]*s,a[1]*s,a[2]*s};}
P Sub(P a,P b) {return Add(a,Mul(b,-1));}
P Cross(P a,P b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
P Unit(P a) {return Mul(a,1/std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]));}
enum Material {Hull,Dark,Steel,Rubber,Bezel,Vent,Switch,Warning,Cyan,Amber,WineRed,Gauge,Cable,Glass,Navy,MilitaryGreen};
const P colours[]={{.032f,.050f,.074f},{.012f,.021f,.028f},{.075f,.100f,.12f},{.009f,.013f,.017f},
 {.035f,.060f,.080f},{.018f,.025f,.031f},{.026f,.043f,.06f},{.50f,.27f,.018f},
 {.015f,.45f,.62f},{.8f,.22f,.016f},{.095f,.008f,.020f},{.010f,.020f,.026f},
 {.025f,.032f,.04f},{.003f,.012f,.018f},{.009f,.020f,.052f},{.040f,.065f,.025f}};
#include "cockpit_reference.inc"
#include "cockpit_instruments.inc"
void Triangle(std::vector<CockpitVertex>& v,P a,P b,P c,unsigned mat) {
    const auto n=Unit(Cross(Sub(b,a),Sub(c,a))),colour=colours[mat];
    // Shared planar coordinates, including imported triangles. Previously all
    // imported geometry sampled a single texel. Keep every face inside its
    // material swatch with padding for mip filtering.
    int axis=0;for(int k=1;k<3;++k)if(std::fabs(n[k])>std::fabs(n[axis]))axis=k;
    const int u=(axis+1)%3,w=(axis+2)%3;
    for(const auto p:{a,b,c})v.push_back({{p[0],p[1],p[2]},{n[0],n[1],n[2]},{colour[0],colour[1],colour[2]},
        {(mat%4)*.25f+.125f+p[u]*.080f,(mat/4)*.25f+.125f+p[w]*.080f},float(mat)});
}
void Quad(std::vector<CockpitVertex>& v,P a,P b,P c,P d,unsigned mat) {
    Triangle(v,a,b,c,mat);Triangle(v,a,c,d,mat);
}
#include "cockpit_rounding.inc"
void Box(std::vector<CockpitVertex>& v,P a,P b,unsigned m) {
    const auto size=Sub(b,a);const float r=std::min(.006f,std::min({size[0],size[1],size[2]})*.12f);
    RoundedPrism(v,{{a[0],a[1],a[2]},{b[0],a[1],a[2]},{b[0],b[1],a[2]},{a[0],b[1],a[2]}},
        {0,0,size[2]},r,m);
}
// Closed plate with real return faces: texture or two-sided rendering cannot
// substitute for its depth when seen from the side in a headset.
void Slab(std::vector<CockpitVertex>& v,P a,P b,P c,P d,P extrusion,unsigned m) {
    RoundedPrism(v,{a,b,c,d},extrusion,std::min(.006f,std::sqrt(Dot(extrusion,extrusion))*.14f),m);
}
void Beam(std::vector<CockpitVertex>& v,P a,P b,float radius,unsigned m) {
    const auto z=Unit(Sub(b,a)),x=Unit(Cross(z,std::fabs(z[1])<.95f?P{0,1,0}:P{1,0,0})),y=Cross(z,x);
    std::vector<P> front;
    for(const auto xy:{P{-1,-1,0},P{1,-1,0},P{1,1,0},P{-1,1,0}})
        front.push_back(Add(a,Add(Mul(x,xy[0]*radius),Mul(y,xy[1]*radius))));
    // Structural members read as rectangular extrusions. Only the edge is
    // eased; hoses retain a broader radius instead of sharing this finish.
    RoundedPrism(v,front,Sub(b,a),m==Cable?radius*.65f:std::min(.004f,radius*.16f),m);
}
void ReferenceBeam(std::vector<CockpitVertex>& v,P a,P b,float width,float depth) {
    const auto start=v.size();
    const auto along=Sub(b,a),z=Unit(along),x=Unit(Cross(z,std::fabs(z[1])<.95f?P{0,1,0}:P{1,0,0})),y=Cross(z,x);
    auto point=[&](const float* p){return Add(a,Add(Mul(along,p[2]),Add(Mul(x,p[0]*width),Mul(y,p[1]*depth))));};
    for(std::size_t i=0;i<std::size(referenceColumn);i+=3)Triangle(v,point(referenceColumn[i]),point(referenceColumn[i+1]),point(referenceColumn[i+2]),Hull);
    SmoothReference(v,start);
}
// A canted instrument face; all dimensions are in the seat frame.
struct Panel {P c,right,up;float w,h;unsigned source;};
const Panel panels[]={
 {{0,-.40f,.91f},{1,0,0},{0,.96f,.28f},.37f,.37f,0}, // radar
 {{-.44f,-.41f,.77f},{.90035f,0,.43517f},{-.10444f,.97077f,.21608f},.40f,.27f,1}, // health on the pilot's left
 {{.44f,-.41f,.77f},{.90035f,0,-.43517f},{.10444f,.97077f,.21608f},.40f,.34f,2}, // ammunition on the right
 {{0,-.67f,.70f},{1,0,0},{0,.82f,.573f},.46f,.20f,3}, // raised above pedal travel, tucked below radar
};
// The Combat Wagon's cabin (NixChest): the same cabin with its lower half
// fitted into the chest, which sits on the truck's mount. The radar is
// raised clear of the lower display, which is over the raised pedals' toes.
const Panel chestPanels[]={
 {{0,-.32f,.93f},{1,0,0},{0,.96f,.28f},.37f,.37f,0},
 {{-.44f,-.41f,.77f},{.90035f,0,.43517f},{-.10444f,.97077f,.21608f},.40f,.27f,1},
 {{.44f,-.41f,.77f},{.90035f,0,-.43517f},{.10444f,.97077f,.21608f},.40f,.34f,2},
 {{0,-.62f,.90f},{1,0,0},{0,.82f,.573f},.46f,.20f,3},
};
P Point(const Panel& p,float x,float y,float forward=0) {
    return Add(Add(p.c,Add(Mul(p.right,x),Mul(p.up,y))),Mul(Unit(Cross(p.right,p.up)),-forward));
}
void Face(std::vector<CockpitVertex>& v,const Panel& p,float w,float h,unsigned m,float inset=0) {
    const float depth=m==Dark?.085f:m==Bezel?.045f:m==Glass?.004f:.012f;
    Slab(v,Point(p,-w/2,h/2,inset),Point(p,w/2,h/2,inset),Point(p,w/2,-h/2,inset),Point(p,-w/2,-h/2,inset),
        Mul(Unit(Cross(p.right,p.up)),depth),m);
}
template<std::size_t N>void Part(std::vector<CockpitVertex>& v,const float(&mesh)[N][3],const Panel& p,float w,float h,float d,unsigned material) {
    const auto start=v.size();
    auto point=[&](const float* q){return Point(p,q[0]*w,q[1]*h,q[2]*d);};
    for(std::size_t i=0;i<N;i+=3)Triangle(v,point(mesh[i]),point(mesh[i+1]),point(mesh[i+2]),material);
    SmoothReference(v,start);
}
// Keep the armour layout, but roll the edge into the side wall rather than
// exposing a single angular bevel. Mounting faces retain their original plane.
void Armour(std::vector<CockpitVertex>& v,const Panel& p,const std::array<std::array<float,2>,8>& shape,float depth,unsigned material,float bevel=.025f) {
    std::vector<P> front;for(const auto& q:shape)front.push_back(Point(p,q[0],q[1],bevel));
    RoundedPrism(v,front,Sub(Point(p,0,0,-depth),Point(p,0,0,bevel)),
        std::min(.012f,(depth+bevel)*.14f),material);
}
void GripBody(std::vector<CockpitVertex>& v,const Panel& p,float w,float h,float thick,unsigned mat,float edge=0);
void Instrument(std::vector<CockpitVertex>& v,const Panel& p) {
    // The actual octagonal monitor rim from Quaternius, open through its back.
    Part(v,referenceMonitor,p,p.w+.063f,p.h+.063f,.058f,Steel);
    const std::array<std::array<float,2>,8> outline{{{-.5f,.40f},{-.40f,.5f},{.40f,.5f},{.5f,.40f},{.5f,-.40f},{.40f,-.5f},{-.40f,-.5f},{-.5f,-.40f}}};
    for(int i=0;i<8;++i){int j=(i+1)%8;Beam(v,Point(p,outline[i][0]*p.w,outline[i][1]*p.h,.006f),Point(p,outline[j][0]*p.w,outline[j][1]*p.h,.006f),.0025f,Cyan);}
    for(float s:{-1.f,1.f}) {
        // On short frames the former lamp overlapped a corner guard at the
        // same .017m plane. Centre it between guards on its own raised socket.
        auto q=p;q.c=Point(p,s*(p.w/2+.012f),0,.001f);
        GripBody(v,q,.020f,.065f,.014f,Dark);
        auto lens=q;lens.c=Point(q,0,0,.023f);Face(v,lens,.012f,.050f,Amber);
        // Separate corner guards, captive screw heads and short retaining
        // clips. All sit on the rim, leaving the useful glass area open.
        for(float t:{-1.f,1.f}) {
            auto cap=p;cap.c=Point(p,s*(p.w/2+.012f),t*(p.h/2-.023f),.002f);
            GripBody(v,cap,.032f,.055f,.017f,Dark);
            Beam(v,Point(cap,0,t*.009f,.016f),Point(cap,0,t*.009f,.021f),.0045f,Steel);
            // The slot cuts into the screw head. A 1mm line parked exactly on
            // the head's top face is two coplanar surfaces, and it flickers.
            Beam(v,Point(cap,-.002f,t*.009f,.0205f),Point(cap,.002f,t*.009f,.0205f),.0025f,Rubber);
            auto clip=p;clip.c=Point(p,s*p.w*.24f,t*(p.h/2+.018f),.001f);
            GripBody(v,clip,.046f,.016f,.012f,Bezel);
        }
    }
    auto rail=p;rail.c=Point(p,0,-p.h/2-.020f,.001f);
    GripBody(v,rail,p.w*.34f,.019f,.016f,Dark);
    for(int k=0;k<3;++k)Beam(v,Point(rail,(k-1)*.012f,-.003f,.016f),Point(rail,(k-1)*.012f,.003f,.016f),.0014f,Steel);
}
// Closed control housings and seat pads, with tangent fillets around every
// edge (including the corners of the previous octagonal chamfer).
void GripBody(std::vector<CockpitVertex>& v,const Panel& p,float w,float h,float thick,unsigned mat,float edge) {
    const float cut=std::min(w,h)*.18f;
    const float xy[8][2]={{-w/2+cut,h/2},{w/2-cut,h/2},{w/2,h/2-cut},{w/2,-h/2+cut},
        {w/2-cut,-h/2},{-w/2+cut,-h/2},{-w/2,-h/2+cut},{-w/2,h/2-cut}};
    std::vector<P> front;for(const auto& q:xy)front.push_back(Point(p,q[0],q[1],.015f));
    const float soft=std::min({.045f,w*.22f,h*.22f,(thick+.015f)*.42f});
    const float radius=edge>0?std::min(edge,soft):mat==Rubber?soft:std::min(.006f,soft*.45f);
    RoundedPrism(v,front,Sub(Point(p,0,0,-thick),Point(p,0,0,.015f)),radius,mat);
}
void Axle(std::vector<CockpitVertex>& v,P c,float length,float radius,unsigned mat) {
    constexpr int segments=24;
    const float r=std::min(radius*.20f,length*.24f);
    const int fillets=r<.003f?2:3;
    auto radial=[](float a){return P{0,std::cos(a),std::sin(a)};};
    auto point=[&](float a,float x,float rho){return Add(c,Add(P{x,0,0},Mul(radial(a),rho)));};
    for(int k=0;k<segments;++k) {
        const float a=k*6.2831853f/segments,b=(k+1)*6.2831853f/segments;const auto na=radial(a),nb=radial(b);
        CurvedQuad(v,point(a,-length/2+r,radius),point(b,-length/2+r,radius),point(b,length/2-r,radius),
            point(a,length/2-r,radius),na,nb,nb,na,mat);
        for(float s:{-1.f,1.f}) {
            const P cap{s,0,0};CurvedTriangle(v,Add(c,{s*length/2,0,0}),point(a,s*length/2,radius-r),point(b,s*length/2,radius-r),cap,cap,cap,mat);
            for(int j=0;j<fillets;++j) {
                auto normal=[&](float angle,int step){const float t=step*1.5707963f/fillets;return Add(Mul(radial(angle),std::cos(t)),Mul(cap,std::sin(t)));};
                auto p=[&](float angle,int step){const auto n=normal(angle,step);return Add(point(angle,s*(length/2-r),radius-r),Mul(n,r));};
                CurvedQuad(v,p(a,j),p(b,j),p(b,j+1),p(a,j+1),normal(a,j),normal(b,j),normal(b,j+1),normal(a,j+1),mat);
            }
        }
    }
}
void FrameBolt(std::vector<CockpitVertex>& v,P centre,P axis,float radius=.005f) {
    const auto start=v.size();Axle(v,{},.004f,radius,Steel);
    axis=Unit(axis);const auto y=Unit(Cross(axis,std::fabs(axis[1])<.95f?P{0,1,0}:P{1,0,0})),z=Cross(axis,y);
    for(auto i=start;i<v.size();++i) {
        auto& vert=v[i];const P p{vert.position[0],vert.position[1],vert.position[2]},n{vert.normal[0],vert.normal[1],vert.normal[2]};
        const auto placed=Add(centre,Add(Add(Mul(axis,p[0]),Mul(y,p[1])),Mul(z,p[2])));
        const auto normal=Add(Add(Mul(axis,n[0]),Mul(y,n[1])),Mul(z,n[2]));
        for(int k=0;k<3;++k){vert.position[k]=placed[k];vert.normal[k]=normal[k];}
    }
    const auto cap=Add(centre,Mul(axis,.003f));Beam(v,Add(cap,Mul(y,-radius*.55f)),Add(cap,Mul(y,radius*.55f)),radius*.13f,Rubber);
}
void Taper(std::vector<CockpitVertex>& v,P a,P b,float ra,float rb,unsigned material,unsigned segments=24) {
    const auto delta=Sub(b,a);const float length=std::sqrt(Dot(delta,delta));
    if(length<1e-6f)return;
    const auto axis=Mul(delta,1/length);
    const auto x=Unit(Cross(axis,std::fabs(axis[1])<.95f?P{0,1,0}:P{1,0,0})),y=Cross(axis,x);
    const float slope=(ra-rb)/length;
    auto ring=[&](P c,float r,float t){return Add(c,Add(Mul(x,r*std::cos(t)),Mul(y,r*std::sin(t))));};
    auto normal=[&](float t){return Unit(Add(Add(Mul(x,std::cos(t)),Mul(y,std::sin(t))),Mul(axis,slope)));};
    for(unsigned i=0;i<segments;++i) {
        const float t0=i*6.2831853f/segments,t1=(i+1)*6.2831853f/segments;
        CurvedQuad(v,ring(a,ra,t0),ring(b,rb,t0),ring(b,rb,t1),ring(a,ra,t1),normal(t0),normal(t0),normal(t1),normal(t1),material);
    }
}
void Rod(std::vector<CockpitVertex>& v,P a,P b,float radius,unsigned material) {
    const auto start=v.size();const auto delta=Sub(b,a);const float length=std::sqrt(Dot(delta,delta));
    Axle(v,{},length,radius,material);
    const auto axis=Mul(delta,1/length),y=Unit(Cross(axis,std::fabs(axis[1])<.95f?P{0,1,0}:P{1,0,0})),z=Cross(axis,y);
    const auto centre=Mul(Add(a,b),.5f);
    for(auto i=start;i<v.size();++i) {
        auto& vert=v[i];const P p{vert.position[0],vert.position[1],vert.position[2]},n{vert.normal[0],vert.normal[1],vert.normal[2]};
        const auto placed=Add(centre,Add(Add(Mul(axis,p[0]),Mul(y,p[1])),Mul(z,p[2])));
        const auto normal=Add(Add(Mul(axis,n[0]),Mul(y,n[1])),Mul(z,n[2]));
        for(int k=0;k<3;++k){vert.position[k]=placed[k];vert.normal[k]=normal[k];}
    }
}
// Socket collars follow BOTH members; a bolted gusset bridges their bend.
// This gives each butt joint visible thickness and a credible load path.
void FrameJoint(std::vector<CockpitVertex>& v,P centre,P first,P second,float radius) {
    const auto a=Unit(Sub(first,centre)),b=Unit(Sub(second,centre));auto normal=Cross(a,b);
    if(Dot(normal,normal)<.01f){Box(v,Sub(centre,{radius,radius,radius}),Add(centre,{radius,radius,radius}),Dark);return;}
    normal=Unit(normal);if(Dot(normal,centre)>0)normal=Mul(normal,-1);
    const float reach=radius*2.8f;
    for(const auto dir:{a,b})Beam(v,Add(centre,Mul(dir,-radius*.25f)),Add(centre,Mul(dir,reach)),radius+.007f,Dark);
    const auto face=Add(centre,Mul(normal,radius+.013f));
    RoundedPrism(v,{Sub(face,Mul(Unit(Add(a,b)),radius*.65f)),Add(face,Mul(a,reach)),Add(face,Mul(b,reach))},
        Mul(normal,-.011f),.003f,Steel);
    for(const auto dir:{a,b})FrameBolt(v,Add(Add(face,Mul(dir,reach*.69f)),Mul(normal,.0025f)),normal);
}
void PalmLever(std::vector<CockpitVertex>& v,float side) {
    const auto start=v.size();
    // Author the right-hand assembly, then mirror it with its normals/winding.
    // A real recessed fore/aft guide: the opening is NOT a dark decal on a
    // solid deck. Four cover pieces surround the well, with a carriage below.
    Panel base{{.35f,-.56f,.125f},{1,0,0},{0,.16f,.987117f},0,0,0};
    auto floor=base;floor.c=Point(base,0,0,-.048f);GripBody(v,floor,.2125f,.3315f,.048f,Dark);
    auto cover=[&](float x,float z,float w,float h){auto p=base;p.c=Point(base,x,z);GripBody(v,p,w,h,.040f,Hull);};
    cover(-.0291f,0,.1543f,.3315f);cover(.0961f,0,.0203f,.3315f);
    for(float s:{-1.f,1.f})cover(.067f,s*.1416f,.042f,.0523f);
    for(float x:{.048f,.086f}) {
        auto lip=base;lip.c=Point(base,x,0,.017f);GripBody(v,lip,.006f,.238f,.013f,Steel);
        Rod(v,Point(base,x,-.110f,-.018f),Point(base,x,.110f,-.018f),.0035f,Steel);
    }
    for(float z:{-.109f,.109f}) {
        auto stop=base;stop.c=Point(base,.067f,z,-.020f);GripBody(v,stop,.028f,.012f,.012f,Rubber);
    }
    // The carriage at the front of its travel (the user, 2026-10-01: the
    // seated crew figure sat cramped with it at -.040; every cabin with this
    // lever gets the room): the lever above it moves forward with it.
    constexpr float carriage=.075f;
    const P travel=Mul(base.up,carriage+.040f);
    auto slide=base;slide.c=Point(base,.067f,carriage,-.016f);GripBody(v,slide,.032f,.055f,.015f,Steel);
    auto boot=base;boot.c=Point(base,.067f,carriage,.009f);GripBody(v,boot,.031f,.042f,.014f,Rubber);
    for(float x:{-.088f,.095f})for(float z:{-.145f,.145f})
        FrameBolt(v,Point(base,x,z,.0165f),{0,.987117f,-.16f},.0035f);

    // Reference grip: three round switches on the end face, one finger
    // trigger behind it, a knurled sleeve and a single outboard hinge.
    const P pivot=Add(P{.421f,-.37917635f,.18067110f},travel);
    const P top{0,.81915204f,-.57357644f},front{0,.57357644f,.81915204f};
    auto point=[&](float x,float y,float z){return Add(pivot,Add(P{x,0,0},Add(Mul(top,y),Mul(front,z))));};
    auto localDirection=[&](P p){return Unit(Add(P{p[0],0,0},Add(Mul(top,p[1]),Mul(front,p[2]))));};
    auto button=[&](const Panel& p,float x,float y,float height,float radius,float depth,unsigned mat) {
        Rod(v,Point(p,x,y,height),Point(p,x,y,height+depth),radius,mat);
    };

    // Fine diamond relief is real moulded geometry, with a flat crown and
    // sloping sides per lozenge. It wraps around the complete rubber sleeve.
    // End collars cover the boundary rows, so no lattice edges float free.
    // Slimmer than first drawn (was 2.2 by 2.05 cm radii; the user: too thick).
    constexpr float gripBegin=-.126f,gripEnd=-.012f,ry=.017f,rz=.016f;
    auto gripPoint=[&](float x,float angle,float lift){return point(x,(ry+lift)*std::cos(angle),(rz+lift)*std::sin(angle));};
    auto gripNormal=[&](float angle){return Unit(Add(Mul(top,std::cos(angle)/ry),Mul(front,std::sin(angle)/rz)));};
    for(int k=0;k<32;++k) {
        const float a=k*6.2831853f/32,b=(k+1)*6.2831853f/32;
        const auto na=gripNormal(a),nb=gripNormal(b);
        CurvedQuad(v,gripPoint(gripBegin,a,-.0006f),gripPoint(gripBegin,b,-.0006f),
            gripPoint(gripEnd,b,-.0006f),gripPoint(gripEnd,a,-.0006f),na,nb,nb,na,Rubber);
        for(float x:{gripBegin,gripEnd}) {
            const P n{x==gripBegin?-1.f:1.f,0,0};
            CurvedTriangle(v,point(x,0,0),gripPoint(x,a,-.0006f),gripPoint(x,b,-.0006f),n,n,n,Rubber);
        }
    }
    constexpr int rows=16,around=16;
    constexpr float pitch=(gripEnd-gripBegin)/rows,arc=6.2831853f/around;
    auto facet=[&](P a,P b,P c,P d,P outward,unsigned mat) {
        if(Dot(Cross(Sub(b,a),Sub(c,a)),outward)<0)std::swap(b,d);
        Quad(v,a,b,c,d,mat);
    };
    for(int i=1;i<rows;++i)for(int j=0;j<around;++j) {
        const float x=gripBegin+i*pitch,angle=(j+(i%2)*.5f)*arc;
        const float uv[4][2]={{-pitch,0},{0,arc*.5f},{pitch,0},{0,-arc*.5f}};
        P bottom[4],raised[4];
        for(int k=0;k<4;++k){bottom[k]=gripPoint(x+uv[k][0],angle+uv[k][1],0);
            raised[k]=gripPoint(x+uv[k][0]*.60f,angle+uv[k][1]*.60f,.00085f);}
        const auto out=gripNormal(angle);
        facet(raised[0],raised[1],raised[2],raised[3],out,Cable);
        for(int k=0;k<4;++k){const int n=(k+1)%4;facet(bottom[k],bottom[n],raised[n],raised[k],out,Rubber);}
    }
    for(float x:{-.125f,-.013f}) {
        Axle(v,point(x,0,0),.011f,.022f,Hull);
        Axle(v,point(x+(x<-.1f?.005f:-.005f),0,0),.003f,.0225f,Steel);
    }
    // Neck between the control block and the grip. Flat relieved shoulders
    // keep the silhouette mechanical, while the sleeve stays comfortable.
    Panel neck{point(-.139f,0,0),front,top,0,0,0};
    GripBody(v,neck,.047f,.047f,.018f,Steel,.002f);

    // The end is an oblique cut across the grip, not a perpendicular cap.
    // Tilt the complete three-switch face and its housing 45 degrees toward
    // the thumb; the upper edge runs back toward the sleeve.
    const auto thumbNormal=localDirection({-.70710678f,.70710678f,0});
    const auto thumbRight=Unit(Cross(thumbNormal,top)),thumbUp=Cross(thumbRight,thumbNormal);
    Panel head{point(-.164f,-.009f,0),thumbRight,thumbUp,0,0,0};
    const float outline[8][2]={{-.034f,.014f},{-.021f,.031f},{.021f,.031f},{.034f,.014f},
        {.034f,-.018f},{.020f,-.030f},{-.020f,-.030f},{-.034f,-.018f}};
    std::vector<P> face;for(const auto& q:outline)face.push_back(Point(head,q[0],q[1],.015f));
    RoundedPrism(v,face,Mul(thumbNormal,-.045f),.0015f,Steel);
    // The clipped shoulders belong to the housing. They have no sockets or
    // coloured caps: the annotated reference marks only the central controls.
    for(float s:{-1.f,1.f}) {
        std::vector<P> shoulder;
        for(const auto& q:{P{.032f,.012f,0},P{.020f,.029f,0},P{.010f,.024f,0},P{.015f,.008f,0}})
            shoulder.push_back(Point(head,s*q[0],q[1],.015f+.23f*(q[1]+.006f)));
        RoundedPrism(v,shoulder,Mul(thumbNormal,-.043f),.001f,Steel);
    }
    // Three circular end-face controls: small upper, large central, small
    // lower. Coloured caps are painted, not emissive indicators; mounting
    // rings and the annotated shoulder pieces remain bare metal.
    button(head,0,.022f,.015f,.0095f,.002f,Rubber);
    button(head,0,.022f,.017f,.008f,.005f,MilitaryGreen);
    button(head,0,-.009f,.015f,.015f,.002f,Rubber);
    button(head,0,-.009f,.017f,.014f,.003f,Steel);
    button(head,0,-.009f,.020f,.011f,.0015f,Switch);
    button(head,0,-.009f,.0215f,.0095f,.0025f,WineRed);
    auto tab=head;tab.c=Point(head,0,-.038f,-.003f);
    GripBody(v,tab,.024f,.024f,.020f,Steel,.0015f);
    button(tab,0,-.002f,.015f,.008f,.002f,Rubber);
    button(tab,0,-.002f,.017f,.0065f,.005f,Navy);

    // The fourth marked control is the round finger trigger behind the
    // head, not an additional crown button. Its short hinged paddle clears
    // the grip so it can be squeezed with the index finger.
    for(float x:{-.152f,-.120f})
        Beam(v,point(x,-.018f,.010f),point(x,-.026f,.016f),.0035f,Steel);
    Axle(v,point(-.136f,-.026f,.016f),.036f,.0035f,Steel);
    RoundedPrism(v,{point(-.149f,-.025f,.015f),point(-.149f,-.030f,.019f),
        point(-.149f,-.041f,-.008f),point(-.149f,-.035f,-.013f)},
        {.026f,0,0},.0015f,Steel);
    Panel trigger{point(-.136f,-.038f,-.002f),{1,0,0},Mul(front,-1),0,0,0};
    button(trigger,0,0,0,.012f,.002f,Rubber);
    button(trigger,0,0,.002f,.0105f,.006f,WineRed);

    // A thin arm connects the outboard collar directly to the existing
    // sliding carriage. Its round trunnion is part of the grip's end cap.
    Beam(v,Point(base,.067f,carriage,.016f),pivot,.017f,Bezel);
    Axle(v,pivot,.034f,.024f,Steel);
    Axle(v,Add(pivot,{.020f,0,0}),.006f,.016f,Dark);
    FrameBolt(v,Add(pivot,{.025f,0,0}),{1,0,0},.006f);
    if(side<0) {
        for(auto i=start;i<v.size();++i){v[i].position[0]*=-1;v[i].normal[0]*=-1;}
        for(auto i=start;i<v.size();i+=3)std::swap(v[i+1],v[i+2]);
    }
}
void Dial(std::vector<CockpitVertex>& v,P c,float r) {
    Panel p{c,{1,0,0},{0,1,0},0,0,0};
    GripBody(v,p,r*2.3f,r*2.3f,.045f,Dark);
    p.c=Point(p,0,0,.02f);
    Part(v,referenceGaugeBody,p,r*2,r*2,r*2,Steel);
    // Reference ticks and needle are actual raised meshes, not a dial image.
    Part(v,referenceGaugeMarks,p,r*2,r*2,.025f,Amber);
}
void ShoulderPod(std::vector<CockpitVertex>& v,float side,bool chest=false) {
    const auto start=v.size();
    Panel p{{.68f,0,0},{0,0,-1},{0,1,0},0,0,0};
    const std::array<std::array<float,2>,8> shell{{{.53f,-.33f},{.54f,.28f},{.32f,.49f},{-.14f,.49f},{-.37f,.29f},{-.37f,-.40f},{-.25f,-.52f},{.12f,-.52f}}};
    // The inner face stays fixed; a deeper closed return masks the cut torso
    // when the pilot leans beside the window and looks back along the arm.
    Armour(v,p,shell,.34f,Hull,.035f);
    auto inset=p;inset.c=Point(p,0,0,.039f);auto shape=shell;for(auto& q:shape){q[0]*=.78f;q[1]*=.78f;}
    Armour(v,inset,shape,.012f,Bezel,.008f);
    // Diagonal reinforcing strips and real recessed louvers, away from the window.
    // All on the inset's face (x .633 here), ahead of where the bucket
    // surround's side wall meets the pod's face: behind that line the wall
    // stands inboard of the pod and they ran into it.
    for(int k=0;k<3;++k) {
        const float y=-.12f+k*.065f;
        Beam(v,{.626f,y,-.05f},{.626f,y+.035f,.10f},.013f,Rubber);
    }
    // Clear of the rivet at (.24,.12) between them.
    Beam(v,{.626f,.27f,.03f},{.626f,.31f,.16f},.009f,Steel);
    Beam(v,{.6305f,.19f,.03f},{.6305f,.215f,.15f},.0035f,Cyan);
    Axle(v,{.64f,-.29f,-.32f},.095f,.057f,Steel);
    Axle(v,{.585f,-.29f,-.32f},.019f,.030f,Rubber);
    for(const auto q:{P{.26f,-.31f,0},P{.24f,.12f,0},P{-.22f,.12f,0},P{-.28f,-.27f,0}})
        Axle(v,{.625f,q[0],q[1]},.012f,.010f,Steel);
    // The lower bolster reaches down beside the seat to the deck's level.
    Panel hip{{.64f,chest?-.60f:-.73f,-.17f},{0,0,-1},{0,1,0},0,0,0};
    const std::array<std::array<float,2>,8> lower=chest?
        std::array<std::array<float,2>,8>{{{.20f,-.13f},{.22f,.18f},{.12f,.30f},{-.20f,.27f},{-.28f,.15f},{-.25f,-.10f},{-.08f,-.19f},{.12f,-.19f}}}:
        std::array<std::array<float,2>,8>{{{.32f,-.24f},{.34f,.20f},{.18f,.39f},{-.20f,.34f},{-.28f,.18f},{-.25f,-.20f},{-.08f,-.36f},{.18f,-.36f}}};
    Armour(v,hip,lower,.16f,Hull);
    // Incline the whole pod and its attached details. The upper shoulder
    // flares out, the lower bolster tucks in beside the seat, like a wedge.
    for(std::size_t i=start;i<v.size();++i){v[i].position[0]+=.18f*v[i].position[1];
        // Hide the inboard/rear connection while revealing the outer shoulder;
        // its front stays behind the chest's front edge.
        v[i].position[2]-=.30f;
        const auto n=Unit({v[i].normal[0],v[i].normal[1]-.18f*v[i].normal[0],v[i].normal[2]});
        for(int k=0;k<3;++k)v[i].normal[k]=n[k];}
    // Mirror the complete solid right pod to the pilot's left.
    if(side<0) {
        for(std::size_t i=start;i<v.size();++i){v[i].position[0]=-v[i].position[0];v[i].normal[0]=-v[i].normal[0];}
        for(std::size_t i=start;i<v.size();i+=3)std::swap(v[i+1],v[i+2]);
    }
}
Panel SeatBack() {
    // Eighteen degrees from vertical about the existing hip contact point.
    const P up{0,.9510565f,-.3090170f};
    return {Add({0,-.6555f,-.1878f},Mul(up,.30f)),{-1,0,0},up,0,0,0};
}
Panel GeneratorFront() {
    auto front=SeatBack();front.c=Point(front,0,0,-.155f);return front;
}
// panBegin, when asked for, reports where the horizontal pan starts in the
// buffer. A reclined cabin can then tip the backrest further than the pan
// without building a second seat.
void Seat(std::vector<CockpitVertex>& v,bool suspended=false,std::size_t* panBegin=nullptr) {
    // Seat dimensions stay relative to the fixed modelling origin: cushion
    // 66cm below it. kCockpitSeatedEye raises/retracts only the camera, leaving
    // the seat, controls and approved outer shell in their existing positions.
    const auto back=SeatBack();
    const std::array<std::array<float,2>,8> shell{{{-.22f,-.27f},{-.28f,.15f},{-.20f,.45f},{.20f,.45f},{.28f,.15f},{.22f,-.27f},{.16f,-.32f},{-.16f,-.32f}}};
    Armour(v,back,shell,.075f,Steel,.018f);
    auto cushion=back;cushion.c=Point(back,0,.015f,.028f);GripBody(v,cushion,.35f,.45f,.023f,Rubber);
    for(float s:{-1.f,1.f}) {
        auto bolster=back;bolster.c=Point(back,s*.202f,-.015f,.041f);GripBody(v,bolster,.088f,.36f,.042f,Bezel,.019f);
        auto pad=back;pad.c=Point(back,s*.082f,.10f,.055f);GripBody(v,pad,.13f,.21f,.015f,Dark,.012f);
        const auto hinge=Point(back,-s*.235f,.15f,-.018f);
        Axle(v,hinge,.073f,.053f,Steel);
        Axle(v,Add(hinge,{s*.042f,0,0}),.015f,.031f,Warning);
        // Pedestal posts remain below the cushion; the compact machinery
        // enclosure takes the rear space formerly occupied by long braces.
        if(!suspended) {
        Rod(v,{s*.18f,-1.10f,.00f},{s*.18f,-.80f,.03f},.023f,Steel);
        Rod(v,{s*.18f,-1.075f,.0025f},{s*.18f,-.92f,.018f},.034f,Dark);
        Panel foot{{s*.18f,-1.105f,0},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,foot,.115f,.13f,.018f,Bezel);
        for(float z:{-.044f,.044f})FrameBolt(v,Point(foot,0,z,.018f),{0,1,0});
        // Short isolator mounts bridge the generator casing and the seat.
        const auto machine=GeneratorFront();
        for(float height:{-.14f,.19f}) {
            const auto a=Point(machine,-s*.18f,height,.021f),b=Point(back,-s*.18f,height,-.070f);
            auto plate=machine;plate.c=Point(machine,-s*.18f,height,.014f);
            GripBody(v,plate,.095f,.075f,.017f,Dark);
            Rod(v,a,b,.021f,Steel);Axle(v,b,.061f,.025f,Rubber);
        }
        }
    }
    auto lumbar=back;lumbar.c=Point(back,0,-.19f,.065f);GripBody(v,lumbar,.30f,.13f,.027f,Rubber);
    auto head=back;head.c=Point(back,0,.36f,.065f);GripBody(v,head,.265f,.19f,.06f,Hull);
    head.c=Point(head,0,0,.016f);GripBody(v,head,.215f,.135f,.014f,Rubber);
    if(panBegin)*panBegin=v.size();
    Panel base{{0,-.70f,-.05f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,base,.50f,.50f,.11f,Hull);
    base.c=Point(base,0,0,.022f);GripBody(v,base,.43f,.45f,.035f,Rubber);
}
void BucketSurround(std::vector<CockpitVertex>& v,bool chest=false) {
    // A continuous wrap behind the pilot: its foot on the deck (below the
    // chest, in the waist; the Wagon's on the chest's floor), its top under
    // the groove between the neck ridge and the shoulder blocks; the folded
    // returns climb into the shoulders.
    Panel rear{{0,-.24f,-.66f},{-1,0,0},{0,1,0},0,0,0};
    const float foot=chest?-.54f:-.90f,corner=chest?-.46f:-.82f;
    const std::array<std::array<float,2>,8> outline{{{-.60f,foot},{-.70f,corner},{-.70f,.67f},{-.62f,.755f},
        {.62f,.755f},{.70f,.67f},{.70f,corner},{.60f,foot}}};
    // Bring the inner bulkhead 30cm forward. The outer returns still seal
    // the hull, but the pilot no longer sits in front of a deep empty well.
    auto well=rear;well.c[2]=-.56f;
    auto inner=outline;for(auto& q:inner){q[0]*=.67f;q[1]*=.90f;}
    Armour(v,well,inner,.09f,Hull,.012f);
    for(int i=0;i<8;++i){const int j=(i+1)%8;
        Slab(v,Point(rear,outline[i][0],outline[i][1]),Point(rear,outline[j][0],outline[j][1]),
            Point(well,inner[j][0],inner[j][1]),Point(well,inner[i][0],inner[i][1]),{0,0,-.055f},Steel);
        Beam(v,Point(well,inner[i][0],inner[i][1],.018f),Point(well,inner[j][0],inner[j][1],.018f),.023f,Bezel);
    }
    for(float s:{-1.f,1.f}) {
        Slab(v,{s*.55f,chest?-.77f:-1.13f,-.68f},{s*.68f,chest?-.73f:-1.13f,-.29f},
            {s*.68f,.48f,-.29f},{s*.55f,.48f,-.68f},{s*.09f,0,-.015f},Hull);
        // Fold the upper return inward and up into the shoulder block. The
        // taper removes the tall outboard plate without opening a seam at its base.
        const P a{s*.55f,.48f,-.68f},b{s*.68f,.48f,-.29f};
        const P d{s*.41f,.72f,-.54f},c=Add(d,Mul(Sub(b,a),.565f));
        auto outside=Unit(Cross(Sub(d,a),Sub(b,a)));if(Dot(outside,P{s,1,0})<0)outside=Mul(outside,-1);
        Slab(v,a,b,c,d,Mul(outside,.055f),Hull);
    }
}
void RearMachinery(std::vector<CockpitVertex>& v,bool chest=false) {
    // A sloping central power enclosure follows the reclined seat. Its closed
    // rear intersects the bulkhead deliberately, like a machine behind a
    // removable interior cover, rather than another plate floating in space.
    // In the Wagon's chest the deck is 30 cm higher: the enclosure, the
    // cabinets and the looms end on it.
    const auto power=GeneratorFront();
    // Literal values for both, so the other cabins' baked geometry is unchanged.
    const float bottom=chest?-.41f:-.61f,chamfer=chest?-.29f:-.49f;
    const std::array<std::array<float,2>,8> body{{{-.27f,bottom},{-.33f,chamfer},{-.33f,.19f},{-.27f,.31f},
        {.27f,.31f},{.33f,.19f},{.33f,chamfer},{.27f,bottom}}};
    Armour(v,power,body,.18f,Hull,.013f);
    auto lower=power;lower.c=Point(power,0,chest?-.26f:-.46f,.016f);
    Part(v,referenceServicePlate,lower,.36f,.17f,.020f,Steel);
    // Raised heat-exchanger spine above the headrest, with paired intakes.
    Panel header{{0,.255f,-.50f},{-1,0,0},{0,1,0},0,0,0};
    GripBody(v,header,.62f,.25f,.075f,Hull);
    for(float s:{-1.f,1.f}) {
        auto fan=header;fan.c=Point(header,s*.145f,.017f,.018f);
        GripBody(v,fan,.21f,.18f,.020f,Dark);
        fan.c=Point(fan,0,0,.016f);Part(v,referenceVent,fan,.18f,.145f,.020f,Steel);
        // Tapered deep housings flank the backrest closely. Their front faces
        // carry the fittings; the original fittings on the old wall are gone.
        Panel cabinet{{s*.414f,-.32f,-.435f},{-1,0,0},{0,1,0},0,0,0};
        const float foot=chest?-.44f:-.60f,shoulder=chest?-.32f:-.48f;
        const std::array<std::array<float,2>,8> shape{{{-.082f,foot},{-.112f,shoulder},{-.112f,.47f},{-.063f,.70f},
            {.063f,.70f},{.112f,.47f},{.112f,shoulder},{.082f,foot}}};
        Armour(v,cabinet,shape,.15f,Bezel,.013f);
        auto cooling=cabinet;cooling.c=Point(cabinet,0,.26f,.017f);
        GripBody(v,cooling,.17f,.30f,.018f,Dark);
        cooling.c=Point(cooling,0,0,.017f);Part(v,referenceVent,cooling,.14f,.25f,.020f,Steel);
        auto service=cabinet;service.c=Point(cabinet,0,-.095f,.017f);
        Part(v,referenceServicePlate,service,.17f,.235f,.022f,Steel);
        auto connector=cabinet;connector.c=Point(cabinet,0,chest?-.30f:-.39f,.017f);
        Part(v,referenceConnector,connector,.10f,.085f,.030f,Steel);
        // The bolts sit on the cabinet's face (its bevel, 13 mm out).
        for(float y:{chest?-.35f:-.51f,.46f})for(float x:{-.057f,.057f})
            FrameBolt(v,Point(cabinet,x,y,.015f),{0,0,1},.005f);
        // Short coolant accumulators are strapped to the upper cabinet;
        // their elbows enter the cabinet and exchanger, not the seat space.
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
        // Cable channel terminates in the lower enclosure and floor duct.
        const P from{s*.414f,chest?-.66f:-.80f,-.407f},bend=chest?P{s*.47f,-.745f,-.43f}:P{s*.49f,-.96f,-.43f},
            to=chest?P{s*.59f,-.745f,-.55f}:P{s*.65f,-.965f,-.55f};
        Beam(v,from,bend,.018f,Cable);Beam(v,bend,to,.018f,Cable);
        FrameJoint(v,bend,from,to,.018f);
    }
}
// The canopy in the Nix chest (cabin origin Y=7.18): the knees under the
// chest's low front top, the brows and crown inside the shoulder blocks, the
// crown's cross rail under the groove between the neck ridge and the
// shoulders. The feet stand on the floor sill as they did before the cabin
// was fitted to the chest: the lower half hangs into the waist (the user:
// comfort before a strict fit), the roof keeps its place in the chest, the
// knees stay low, clear of the view out at the eyes' height.
P RearCrown(float side) {return {side*.45f,.48f,-.51f};}
P CanopyBrow(float side) {return {side*.56f,.60f,-.30f};}
P NixCanopyFoot(float side,bool chest=false) {
    // In the Wagon's chest the foot is on the sill's outboard step, outside
    // the side monitors.
    return chest?P{side*.62f,-.50f,.80f}:P{side*.715f,-.98f,.79f};
}
P NixCanopyKnee(float side) {return {side*.72f,.02f,.60f};}
// World-view monitor joints are thin ink ribbons, not extra structural bars.
// Append them after the opaque interior for the existing translucent draw pass;
// surface 21 is also excluded from the physical head collision mesh.
void MonitorSeam(std::vector<CockpitVertex>& v,P a,P b,P across,P bulge) {
    const auto start=v.size();
    // Shallow bowed monitor tiles, with both ends fixed to the real frame.
    // Each tile can have its own curvature; this is not a panoramic sphere.
    auto edge=[&](float t,float side) {
        const auto along=Unit(Add(Sub(b,a),Mul(bulge,4*(1-2*t))));
        const auto half=Mul(Unit(Sub(across,Mul(along,Dot(across,along)))),side*.002f);
        return Add(Add(a,Mul(Sub(b,a),t)),Add(Mul(bulge,4*t*(1-t)),half));
    };
    for(unsigned i=0;i<24;++i) {
        const float t=float(i)/24,u=float(i+1)/24;
        Quad(v,edge(t,-1),edge(t,1),edge(u,1),edge(u,-1),Dark);
    }
    for(auto i=start;i<v.size();++i){v[i].surface=21;for(auto& c:v[i].colour)c=.012f;}
}
template<std::size_t N>
void MonitorSeamPath(std::vector<CockpitVertex>& v,const std::array<P,N>& points) {
    const auto start=v.size();
    // Continuous seams on the OUTER screen skin. Catmull-Rom joins preserve
    // a smooth tangent through the front/side corners and never end on rails.
    for(std::size_t span=0;span+1<N;++span) {
        const auto b=points[span],c=points[span+1];
        const auto a=span?points[span-1]:Sub(Mul(b,2),c);
        const auto d=span+2<N?points[span+2]:Sub(Mul(c,2),b);
        const auto q0=Mul(b,2),q1=Sub(c,a),q2=Add(Sub(Mul(a,2),Mul(b,5)),Sub(Mul(c,4),d));
        const auto q3=Add(Sub(Mul(b,3),a),Sub(d,Mul(c,3)));
        auto edge=[&](float t,float side) {
            const auto p=Mul(Add(q0,Mul(Add(q1,Mul(Add(q2,Mul(q3,t)),t)),t)),.5f);
            const auto along=Add(q1,Add(Mul(q2,2*t),Mul(q3,3*t*t)));
            const auto width=Unit(Cross(along,Sub(p,kCockpitSeatedEye)));
            return Add(p,Mul(width,side*.002f));
        };
        for(unsigned i=0;i<24;++i) {
            const float t=float(i)/24,u=float(i+1)/24;
            Quad(v,edge(t,-1),edge(t,1),edge(u,1),edge(u,-1),Dark);
        }
    }
    for(auto i=start;i<v.size();++i){v[i].surface=21;for(auto& c:v[i].colour)c=.012f;}
}
void NixMainMonitorSeams(std::vector<CockpitVertex>& v,bool chest=false) {
    const auto foot=NixCanopyFoot(1,chest),knee=NixCanopyKnee(1);
    const auto low=Add(foot,Mul(Sub(knee,foot),.16f/(knee[1]-foot[1])));
    // Screens sit outside the cage, with clearance beyond its outer faces.
    // Both bands continue behind the rails into the adjacent monitor tiles;
    // their ends disappear into the rear enclosure rather than meeting a beam.
    // The large central windshield has no vertical or horizontal division.
    // The lower band round the floor sill; the upper arch under the chest
    // top, at +.39..+.53 over the cabin.
    const float x=(chest?low[0]:foot[0])+.070f,y=low[1],z=low[2]+.075f;
    if(chest)MonitorSeamPath(v,std::array<P,7>{{{-.72f,y,-.62f},{-.64f,y,.32f},{-x,y,z},
        {0,y,z+.11f},{x,y,z},{.64f,y,.32f},{.72f,y,-.62f}}});
    else MonitorSeamPath(v,std::array<P,7>{{{-.75f,y,-.62f},{-x,y,.32f},{-x,y,z},
        {0,y,z+.11f},{x,y,z},{x,y,.32f},{.75f,y,-.62f}}});
    MonitorSeamPath(v,std::array<P,7>{{{-.74f,knee[1],-.62f},{-.74f,knee[1],.20f},
        {-.52f,.37f,.40f},{0,.40f,.40f},{.52f,.37f,.40f},
        {.74f,knee[1],.20f},{.74f,knee[1],-.62f}}});
    for(float side:{-1.f,1.f}) {
        // Side tile T joints end on the screen bands, not the inner structure.
        if(chest)MonitorSeam(v,{side*.74f,knee[1],.20f},{side*.64f,y,.32f},{0,0,1},{side*.020f,0,.008f});
        else MonitorSeam(v,{side*.74f,knee[1],.20f},{side*x,y,.32f},{0,0,1},{side*.025f,0,.010f});
    }
}
// The crown rail carries both canopy rails and the neck cover; its ends run
// out into the side walls through bolted flanges on their faces. (Posts from
// its ends down to the deck stood in the corner between the rear machinery
// cabinet, the side wall and the shoulder pod, and ran into all three.)
void RearStructure(std::vector<CockpitVertex>& v,bool chest=false) {
    (void)chest;
    Beam(v,RearCrown(-1),RearCrown(1),.034f,Steel);
    for(float s:{-1.f,1.f}) {
        const P crown=RearCrown(s);
        const P wallA{s*.55f,0,-.68f},wallB{s*.68f,0,-.29f};
        const P outside=Unit(P{-(wallB[2]-wallA[2])*s,0,(wallB[0]-wallA[0])*s});   // off the wall, inboard
        const P right=Unit(P{-outside[2]*s,0,outside[0]*s});
        // 4.5 cm inside the wall (.09 thick), under its top at .48, and
        // forward enough that the seated eye sees its flange clear of the
        // coolant accumulator on the rear machinery cabinet.
        const P anchor{s*.707f,.38f,-.350f};
        Beam(v,crown,anchor,.034f,Steel);
        FrameJoint(v,crown,anchor,RearCrown(-s),.036f);
        // The canopy's third branch enters a socket in the same crown node.
        const auto incoming=Unit(Sub(CanopyBrow(s),crown));
        Beam(v,Add(crown,Mul(incoming,-.015f)),Add(crown,Mul(incoming,.115f)),.047f,Dark);
        const P normal{0,0,1};
        FrameBolt(v,Add(Add(crown,Mul(incoming,.079f)),Mul(normal,.054f)),normal,.006f);
        // The flange where the rail enters the wall's face.
        auto off=[&](P p){return Dot(Sub(p,{wallA[0],p[1],wallA[2]}),outside);};
        const auto entry=Add(crown,Mul(Sub(anchor,crown),off(crown)/(off(crown)-off(anchor))));
        Panel flange{entry,right,{0,1,0},0,0,0};
        GripBody(v,flange,.13f,.13f,.030f,Dark);
        for(float a:{-1.f,1.f})for(float b:{-1.f,1.f})
            FrameBolt(v,Point(flange,a*.050f,b*.050f,.017f),outside);
        // A smaller duct cassette lies on the folded cap, ahead of the hoop.
        // Derive its plane from the cap so neither outlet nor connector sinks
        // into the armour when the canopy moves inward.
        const P capBase{s*.55f,.48f,-.68f},across{s*.13f,0,.39f},lift{-s*.14f,.24f,.14f};
        const auto capUp=Unit(Sub(lift,Mul(right,Dot(lift,right))));
        // Turn the cassette 25 degrees in the cap plane, with its complete
        // mounting flange inside the tapered face and clear of the crown rail.
        const float angle=s*.4363323f;
        const auto cassetteRight=Add(Mul(right,std::cos(angle)),Mul(capUp,std::sin(angle)));
        const auto cassetteUp=Add(Mul(right,-std::sin(angle)),Mul(capUp,std::cos(angle)));
        Panel equipment{Add(capBase,Add(Mul(across,.57f),Mul(lift,.39f))),cassetteRight,cassetteUp,0,0,0};
        const auto inward=Mul(Unit(Cross(cassetteRight,cassetteUp)),-1);
        GripBody(v,equipment,.14f,.14f,.022f,Dark);
        auto grille=equipment;grille.c=Point(equipment,0,.026f,.019f);
        Part(v,referenceVent,grille,.115f,.072f,.022f,Steel);
        // On the cassette's face (15 mm out), as its grille.
        auto jack=equipment;jack.c=Point(equipment,0,-.043f,.015f);
        Part(v,referenceConnector,jack,.042f,.033f,.025f,Steel);
        for(float a:{-1.f,1.f})
            FrameBolt(v,Point(equipment,a*.049f,-.045f,.017f),inward,.004f);
    }
}
void FloorAndDucts(std::vector<CockpitVertex>& v) {
    // The deck below the chest, in the waist (the user: the cabin may stick
    // out below), where it was before the cabin was fitted to the chest.
    Panel floor{{0,-1.115f,0},{1,0,0},{0,0,1},0,0,0};
    // Trim the platform to the bent sill instead of leaving a square plinth.
    // The aft pan overlaps the rear well and side returns, sealing its base.
    const std::array<std::array<float,2>,8> aft{{{-.57f,-.93f},{.57f,-.93f},{.71f,-.67f},{.69f,-.40f},
        {.67f,.20f},{-.67f,.20f},{-.69f,-.40f},{-.71f,-.67f}}};
    Armour(v,floor,aft,.075f,Steel,.008f);
    for(float s:{-1.f,1.f}) {
        Slab(v,{s*.40f,-1.11f,.20f},{s*.67f,-1.11f,.20f},{s*.58f,-1.11f,.73f},
            {s*.40f,-1.11f,.85f},{0,-.07f,0},Steel);
        Slab(v,{s*.40f,-1.11f,.85f},{s*.58f,-1.11f,.73f},{s*.41f,-1.11f,1.01f},
            {0,-1.11f,1.01f},{0,-.07f,0},Steel);
        Beam(v,{s*.67f,-1.065f,-.66f},{s*.65f,-1.065f,.18f},.035f,Steel);
        Beam(v,{s*.65f,-1.065f,.18f},{s*.56f,-1.065f,.73f},.035f,Steel);
        Beam(v,{s*.56f,-1.065f,.73f},{s*.39f,-1.065f,.99f},.035f,Steel);
        FrameJoint(v,{s*.65f,-1.065f,.18f},{s*.67f,-1.065f,-.66f},{s*.56f,-1.065f,.73f},.035f);
        FrameJoint(v,{s*.56f,-1.065f,.73f},{s*.65f,-1.065f,.18f},{s*.39f,-1.065f,.99f},.035f);
        // Two rounded duct casings join at the service band. Each mounting
        // face is planar, so the grille/cover retain their working clearance.
        const float yz[2][6][2]={{{-.87f,-.61f},{-.84f,-.53f},{-.84f,.20f},
            {-1.095f,.20f},{-1.095f,-.60f},{-1.025f,-.70f}},
            {{-.84f,.16f},{-.84f,.59f},{-.88f,.75f},{-1.035f,.83f},{-1.095f,.73f},{-1.095f,.16f}}};
        for(int section=0;section<2;++section) {
            std::vector<P> face;
            for(const auto& q:yz[section]) {
                const float y=q[0],z=q[1],x=.65f-(section?.12f*(z-.18f):0)+.22f*(y+1.095f);
                face.push_back({s*x,y,z});
            }
            RoundedPrism(v,face,{s*.08f,0,0},.007f,Hull,Steel);
        }
        auto ductFrame=[&](float y,float z) {
            const auto right=Unit(P{z>.18f?.12f:0,0,-s});
            const auto normal=Unit(Cross(right,P{s*.22f,1,0}));
            const float x=.65f-.12f*std::max(0.f,z-.18f)+.22f*(y+1.095f);
            return Panel{{s*x,y,z},right,Cross(normal,right),0,0,0};
        };
        auto outlet=ductFrame(-.943f,.55f);
        // The vent and the plate sit a millimetre into their cases' faces
        // (at +.015): flush, the two faces would flicker.
        GripBody(v,outlet,.225f,.15f,.04f,Dark);
        outlet.c=Point(outlet,0,0,.014f);Part(v,referenceVent,outlet,.19f,.12f,.025f,Steel);
        auto service=ductFrame(-.935f,-.35f);
        GripBody(v,service,.25f,.16f,.04f,Bezel);
        service.c=Point(service,0,0,.014f);Part(v,referenceServicePlate,service,.23f,.135f,.024f,Steel);
        // Raised seam bands indicate removable sections and hold the casing.
        for(float z:{-.47f,.16f,.48f}) {
            const float x=.65f-.12f*std::max(0.f,z-.18f);
            Beam(v,{s*(x+.003f),-1.065f,z},{s*(x+.052f),-.867f,z},.007f,Steel);
        }
        Panel pipe{{s*.50f,-1.11f,.38f},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,pipe,.15f,.26f,.015f,Dark);pipe.c=Point(pipe,0,0,.015f);
        Part(v,referencePipeCassette,pipe,.13f,.24f,.027f,Bezel);
    }
}
void ChestFloorAndDucts(std::vector<CockpitVertex>& v) {
    // The Wagon's deck on the chest's bottom plate: the aft pan under the
    // seat, and ahead of it the observation pane over the deep centre well,
    // between two raised sills where the plate steps up.
    Panel floor{{0,-.79f,0},{1,0,0},{0,0,1},0,0,0};
    const std::array<std::array<float,2>,8> aft{{{-.35f,-.90f},{.35f,-.90f},{.48f,-.66f},{.52f,.12f},
        {.40f,.20f},{-.40f,.20f},{-.52f,.12f},{-.48f,-.66f}}};
    Armour(v,floor,aft,.030f,Steel,.008f);
    // Front lip ahead of the pane, carrying the dashboard crossmember.
    Slab(v,{-.33f,-.79f,.94f},{.33f,-.79f,.94f},{.30f,-.79f,1.04f},{-.30f,-.79f,1.04f},{0,-.03f,0},Steel);
    for(float s:{-1.f,1.f}) {
        // Sill: the bottom plate's step beside the well and its outboard step
        // under the canopy's foot. No frame rail along the pane: the side
        // screens come down over the sill, and the right one ran through it.
        Slab(v,{s*.33f,-.58f,.20f},{s*.50f,-.58f,.20f},{s*.48f,-.58f,.88f},{s*.33f,-.58f,.94f},{0,-.02f,0},Steel);
        Slab(v,{s*.48f,-.58f,.72f},{s*.68f,-.58f,.72f},{s*.66f,-.58f,.86f},{s*.48f,-.58f,.86f},{0,-.02f,0},Steel);
        Slab(v,{s*.33f,-.58f,.20f},{s*.33f,-.58f,.94f},{s*.33f,-.79f,.94f},{s*.33f,-.79f,.20f},{s*.02f,0,0},Steel);
        Beam(v,{s*.48f,-.765f,-.66f},{s*.52f,-.765f,.12f},.025f,Steel);
        Beam(v,{s*.52f,-.765f,.12f},{s*.40f,-.765f,.20f},.025f,Steel);
        FrameJoint(v,{s*.52f,-.765f,.12f},{s*.48f,-.765f,-.66f},{s*.40f,-.765f,.20f},.025f);
        // Two rounded duct casings along the rising side of the bottom plate.
        // Each mounting face is planar, so the grille/cover keep their clearance.
        const float yz[2][6][2]={{{-.60f,-.60f},{-.58f,-.53f},{-.58f,.18f},
            {-.76f,.18f},{-.76f,-.58f},{-.72f,-.66f}},
            {{-.56f,.22f},{-.56f,.56f},{-.58f,.66f},{-.63f,.70f},{-.66f,.64f},{-.66f,.22f}}};
        for(int section=0;section<2;++section) {
            std::vector<P> face;
            for(const auto& q:yz[section]) {
                const float y=q[0],z=q[1],x=(section?.52f:.55f)+.22f*(y+.76f);
                face.push_back({s*x,y,z});
            }
            RoundedPrism(v,face,{s*.07f,0,0},.007f,Hull,Steel);
        }
        auto ductFrame=[&](float y,float z,float base) {
            const auto right=P{0,0,-s};
            const auto normal=Unit(Cross(right,P{s*.22f,1,0}));
            const float x=base+.22f*(y+.76f);
            return Panel{{s*x,y,z},right,Cross(normal,right),0,0,0};
        };
        auto outlet=ductFrame(-.615f,.44f,.52f);
        // The vent and the plate sit a millimetre into their cases' faces
        // (at +.015): flush, the two faces would flicker.
        GripBody(v,outlet,.20f,.075f,.03f,Dark);
        outlet.c=Point(outlet,0,0,.014f);Part(v,referenceVent,outlet,.17f,.055f,.022f,Steel);
        auto service=ductFrame(-.67f,-.20f,.55f);
        GripBody(v,service,.25f,.14f,.04f,Bezel);
        service.c=Point(service,0,0,.014f);Part(v,referenceServicePlate,service,.23f,.12f,.024f,Steel);
        // Raised seam bands indicate removable sections and hold the casing.
        for(float z:{-.47f,.10f})Beam(v,{s*.553f,-.76f,z},{s*.593f,-.58f,z},.007f,Steel);
        Beam(v,{s*.523f,-.66f,.40f},{s*.543f,-.56f,.40f},.007f,Steel);
        // Aft of the side monitor, whose lower rim comes down to the sill.
        Panel pipe{{s*.43f,-.58f,.40f},{1,0,0},{0,0,1},0,0,0};
        GripBody(v,pipe,.13f,.26f,.012f,Dark);pipe.c=Point(pipe,0,0,.012f);
        Part(v,referencePipeCassette,pipe,.11f,.24f,.022f,Bezel);
    }
}
// boltAt: the clamp's bolts over its centre plane, seated on its 15 mm face.
// shoeY: the rear mount's shoe centre (the carrier runs at -1.143).
void PedalAssembly(std::vector<CockpitVertex>& v,float side,float stance=.26f,float sill=.595f,float rearMount=0,float boltAt=.017f,float shoeY=-1.09f) {
    const float x=side*stance;
    const P pivot{x,-1.085f,.515f};
    if(rearMount<=0) {
    // A bolted clamp on the existing floor sill carries a cantilever under
    // the heel. Keep the central observation pane free of a new crossbar.
    const P anchor{side*sill,-1.028f,.515f};
    Panel mounting{anchor,{1,0,0},{0,0,1},0,0,0};
    GripBody(v,mounting,.125f,.13f,.028f,Dark);
    for(float z:{-.042f,.042f})FrameBolt(v,Point(mounting,0,z,boltAt),{0,1,0});
    Beam(v,Add(anchor,{0,-.024f,0}),{anchor[0],-1.143f,.515f},.026f,Dark);
    Beam(v,{anchor[0],-1.143f,.515f},{x-side*.119f,-1.143f,.515f},.021f,Steel);
    Beam(v,{x,-1.143f,.515f},{x,-1.143f,.725f},.020f,Steel);
    } else {
    // A narrow well leaves no sill beside the pane: the carrier runs back
    // under the heel to a bolted shoe on the deck's front edge, and a short
    // cross arm reaches both pillow blocks.
    Beam(v,{x,-1.143f,rearMount-.06f},{x,-1.143f,.725f},.020f,Steel);
    Beam(v,{x-.119f,-1.143f,.515f},{x+.119f,-1.143f,.515f},.018f,Steel);
    Panel shoe{{x,shoeY,rearMount-.05f},{1,0,0},{0,0,1},0,0,0};
    GripBody(v,shoe,.09f,.10f,.038f,Dark);
    }
    // Two pillow blocks carry a transverse shaft. The pedal's bottom edge
    // rotates around that shaft, rather than floating over an empty window.
    for(float s:{-1.f,1.f}) {
        const P centre{x+s*.107f,-1.115f,.515f};
        Box(v,Sub(centre,{.019f,.031f,.032f}),Add(centre,{.019f,.031f,.032f}),Dark);
        Axle(v,{centre[0],pivot[1],pivot[2]},.043f,.027f,Bezel);
        FrameBolt(v,{centre[0]+s*.024f,pivot[1],pivot[2]},{s,0,0},.010f);
    }
    Axle(v,pivot,.235f,.016f,Steel);
    Panel pedal{Add(pivot,{0,.065f,.1125833f}),{1,0,0},{0,.5f,.8660254f},0,0,0};
    GripBody(v,pedal,.185f,.265f,.025f,Steel);
    auto pad=pedal;pad.c=Point(pedal,0,.004f,.016f);
    GripBody(v,pad,.145f,.208f,.009f,Rubber);
    for(int k=0;k<5;++k)
        Beam(v,Point(pad,-.061f,(k-2)*.038f,.018f),Point(pad,.061f,(k-2)*.038f,.018f),.0035f,Bezel);
    // Protective side lips and a raised toe lip, clear of the heel pivot.
    for(float s:{-1.f,1.f})Beam(v,Point(pedal,s*.084f,-.095f,.009f),Point(pedal,s*.084f,.11f,.018f),.009f,Dark);
    Beam(v,Point(pedal,-.075f,.122f,.015f),Point(pedal,.075f,.122f,.015f),.010f,Dark);
    // Telescopic return damper: lower clevis on the carrier, upper clevis on
    // the pedal's underside. Each end can rotate as the pedal is depressed.
    const P lower{x,-1.117f,.710f};const auto upper=Point(pedal,0,.052f,-.038f);
    const auto delta=Sub(upper,lower),axis=Unit(delta),join=Add(lower,Mul(delta,.61f));
    Beam(v,{x,-1.143f,.710f},lower,.024f,Dark);
    Rod(v,lower,join,.014f,Bezel);Rod(v,join,upper,.007f,Steel);
    Rod(v,Add(join,Mul(axis,-.006f)),Add(join,Mul(axis,.006f)),.018f,Dark);
    for(const auto end:{lower,upper})Axle(v,end,.069f,.014f,Steel);
    Beam(v,upper,Point(pedal,0,.052f,-.022f),.019f,Dark);
}
void CanopyCowl(std::vector<CockpitVertex>& v) {
    // Neck shroud under the chest top. Ahead, a low deck as far as the screen
    // arch: the Nix's head sits on the chest right above it, so the deck
    // hides its underside and neck. Over the pilot's head a flat ceiling on
    // the crown rail, closed back to the rear bulkhead: the side decks and a
    // centre panel between them (the raised hood there was an empty box the
    // user did not want), with a service plate, ribs, a vent and a lamp.
    const std::array<std::array<float,2>,8> front{{{-.28f,-.10f},{.28f,-.10f},{.30f,.00f},{.28f,.30f},
        {.20f,.38f},{-.20f,.38f},{-.28f,.30f},{-.30f,.00f}}};
    {
        std::vector<P> face;for(const auto& q:front)face.push_back({q[0],.435f,q[1]});
        RoundedPrism(v,face,{0,.030f,0},.008f,Hull);
    }
    for(float s:{-1.f,1.f}) {
        Slab(v,{s*.17f,.514f,-.66f},{s*.42f,.514f,-.66f},{s*.42f,.514f,-.10f},{s*.17f,.514f,-.10f},{0,.030f,0},Hull);
        Slab(v,{s*.17f,.465f,-.10f},{s*.30f,.465f,-.10f},{s*.30f,.514f,-.10f},{s*.17f,.514f,-.10f},{0,0,-.020f},Hull);
    }
    Slab(v,{-.17f,.514f,-.66f},{.17f,.514f,-.66f},{.17f,.514f,-.10f},{-.17f,.514f,-.10f},{0,.030f,0},Hull);
    Slab(v,{-.17f,.465f,-.10f},{.17f,.465f,-.10f},{.17f,.514f,-.10f},{-.17f,.514f,-.10f},{0,0,-.020f},Hull);
    Panel plate{{0,.432f,.12f},{1,0,0},{0,0,-1},0,0,0};   // Cross(right,up) = +y: fittings hang below
    Part(v,referenceServicePlate,plate,.24f,.14f,.020f,Steel);
    // The centre panel: its service plate ahead of the crown rail, two ribs
    // from the rail to the front lip beside it, a vent behind the rail and
    // a lamp over the brow.
    Panel hatch{{0,.511f,-.31f},{1,0,0},{0,0,-1},0,0,0};
    Part(v,referenceServicePlate,hatch,.22f,.24f,.020f,Steel);
    for(float s:{-1.f,1.f})Beam(v,{s*.145f,.508f,-.480f},{s*.145f,.508f,-.118f},.008f,Steel);
    Panel grille{{0,.511f,-.60f},{1,0,0},{0,0,-1},0,0,0};
    Part(v,referenceVent,grille,.20f,.07f,.018f,Steel);
    Panel lamp{{0,.514f,-.155f},{1,0,0},{0,0,-1},0,0,0};
    GripBody(v,lamp,.10f,.04f,.012f,Dark);
    auto lens=lamp;lens.c=Point(lamp,0,0,.017f);Face(v,lens,.08f,.025f,Amber);
    for(float s:{-1.f,1.f}) {
        // Clear of the crown rail (its face at z -.476).
        Panel vent{{s*.31f,.511f,-.34f},{1,0,0},{0,0,-1},0,0,0};
        Part(v,referenceVent,vent,.14f,.26f,.018f,Steel);
        Beam(v,{s*.21f,.422f,.30f},{s*.29f,.422f,.02f},.010f,Steel);
        Beam(v,{s*.17f,.425f,.32f},{s*.21f,.425f,.24f},.004f,Cyan);
        // Mounting pads where the side decks rest on the crown rail.
        Panel mount{{s*.31f,.500f,-.51f},{1,0,0},{0,0,-1},0,0,0};
        GripBody(v,mount,.15f,.06f,.018f,Dark);
        for(float x:{-.048f,.048f})FrameBolt(v,Point(mount,x,0,.016f),{0,-1,0});
    }
}

#include "cockpit_crawler.inc"
}
std::vector<CockpitVertex> BuildNixCockpit(bool chest) {
    std::vector<CockpitVertex> v;v.reserve(330000);
    // The cabin in the Nix chest (cabin origin Y=7.18): its canopy under the
    // chest top (research/cockpit/robot_fit.py), the roof 33 cm over the eyes;
    // the lower half (deck, pedals, lower screens) where it was before the
    // cabin was fitted to the chest, hanging into the waist. The compact
    // canopy is tied to the front instrument carrier and the neck shroud.
    // chest: the Combat Wagon's, whose chest sits on the truck's mount, so
    // its lower half stays in the chest (as the Nix's was on 2026-09-25).
    const auto& screens=chest?chestPanels:panels;
    for(float s:{-1.f,1.f}) {
        const P foot=NixCanopyFoot(s,chest),knee=NixCanopyKnee(s),brow=CanopyBrow(s),rear=RearCrown(s);
        ReferenceBeam(v,foot,knee,.075f,.070f);ReferenceBeam(v,knee,brow,.072f,.065f);Beam(v,brow,rear,.032f,Hull);
        const P sillRear{s*.67f,-.68f,-.46f};
        const auto sillFront=Add(foot,Mul(Sub(knee,foot),.30f/(knee[1]-foot[1])));
        Beam(v,sillRear,sillFront,.035f,Steel);
        if(chest) {
            // The foot stands on the pane's sill; a brace from the sill's inner
            // wall carries the crossmember.
            const P carrier{s*.30f,-.755f,.99f};
            Beam(v,{s*.345f,-.70f,.90f},carrier,.024f,Steel);
            FrameJoint(v,foot,knee,{foot[0],-.58f,foot[2]},.039f);
        } else {
            const P carrier{s*.65f,-.98f,.87f};
            Beam(v,foot,carrier,.028f,Steel);
            FrameJoint(v,foot,knee,carrier,.039f);
        }
        FrameJoint(v,knee,foot,brow,.040f);
        FrameJoint(v,brow,knee,rear,.037f);
        FrameJoint(v,sillFront,sillRear,knee,.036f);
        if(chest) {
            Panel base{{foot[0],-.58f,foot[2]},{1,0,0},{0,0,1},0,0,0};
            GripBody(v,base,.11f,.12f,.018f,Dark);
        }
        // Bolted back-of-rim brackets attach both side monitors to these
        // uprights. The short crosspieces end inside real sockets, not glass.
        const auto& panel=screens[s<0?1:2];
        auto mount=panel;mount.c=Point(panel,s*(panel.w/2+.019f),0,-.035f);
        GripBody(v,mount,.042f,.10f,.034f,Dark);
        const auto rail=Add(foot,Mul(Sub(knee,foot),(mount.c[1]-foot[1])/(knee[1]-foot[1])));
        Beam(v,Point(mount,0,0,-.014f),rail,.018f,Steel);
        for(float y:{-.030f,.030f})FrameBolt(v,Point(mount,0,y,.020f),Mul(Unit(Cross(panel.right,panel.up)),-1),.004f);
        // Shoulder fairing: wraps aft of the side window, across the entire
        // upper-arm attachment cone. Forward side aperture stays open.
        ShoulderPod(v,s,chest);
        const float x=s*.35f;
        // Keep the front edge and lever in place, extend the pedestal aft by
        // 24cm as one continuous support down to the deck.
        Panel pedestal{{x,-.672f,-.0244f},{1,0,0},{0,.10f,.995f},0,0,0};
        GripBody(v,pedestal,.225f,.64f,chest?.13f:.268f,Dark);
        auto rearDeck=pedestal;rearDeck.c=Point(pedestal,0,-.175f,.016f);
        GripBody(v,rearDeck,.190f,.24f,.022f,Hull);
        PalmLever(v,s);
        // Gauge pods beside the lower display, on legs to the crossmember.
        if(chest) {
            Panel pod{{s*.30f,-.64f,.93f},{1,0,0},{0,.96f,.28f},0,0,0};
            GripBody(v,pod,.12f,.16f,.06f,Hull);
            Dial(v,{s*.30f,-.64f,.892f},.048f);
            Beam(v,{s*.30f,-.70f,.955f},{s*.30f,-.755f,.99f},.018f,Steel);
        } else {
            Panel pod{{s*.32f,-.70f,.80f},{1,0,0},{0,.96f,.28f},0,0,0};
            GripBody(v,pod,.18f,.16f,.08f,Hull);
            Dial(v,{s*.32f,-.70f,.762f},.054f);
            Beam(v,{s*.32f,-.70f,.85f},{s*.32f,-.98f,.87f},.022f,Steel);
        }
    }
    BucketSurround(v,chest);
    RearStructure(v,chest);
    RearMachinery(v,chest);
    if(chest) {
        // The seat stands on the deck itself: the chest's bottom plate is right
        // under it. Short isolator mounts bridge the generator casing and the seat.
        Seat(v,true);
        const auto back=SeatBack(),machine=GeneratorFront();
        for(float s:{-1.f,1.f})for(float height:{-.14f,.19f}) {
            const auto a=Point(machine,-s*.18f,height,.021f),b=Point(back,-s*.18f,height,-.070f);
            auto plate=machine;plate.c=Point(machine,-s*.18f,height,.014f);
            GripBody(v,plate,.095f,.075f,.017f,Dark);
            Rod(v,a,b,.021f,Steel);Axle(v,b,.061f,.025f,Rubber);
        }
    } else {
        // The seat on its pedestal posts, bolted to the deck.
        Seat(v);
    }
    // The neck shroud conceals the head's neck and underside under the chest
    // top; the low forward skylight stays open ahead of it.
    CanopyCowl(v);
    // Open dashboard: the hologram panes must see the world, including below.
    // In the Wagon's chest the crossmember lies on the deck's front lip.
    if(chest)Beam(v,{-.31f,-.755f,.99f},{.31f,-.755f,.99f},.035f,Hull);
    else {
        Beam(v,{-.67f,-.98f,.87f},{.67f,-.98f,.87f},.035f,Hull);
        for(float s:{-1.f,1.f})Beam(v,{s*.65f,-.98f,.87f},{s*.65f,-1.12f,.87f},.024f,Hull);
    }
    for(const auto& panel:screens)Instrument(v,panel);
    // Every emitter frame is physically carried by the lower crossmember;
    // thin legs leave the transparent centres unobstructed. In the Wagon's
    // chest the side monitors hang on their canopy brackets, so only their
    // inboard legs run down.
    const P carrierAt=chest?P{0,-.755f,.99f}:P{0,-.98f,.87f};
    const float clampDepth=chest?.030f:.043f;
    for(unsigned i=0;i<std::size(screens);++i)for(float s:{-1.f,1.f}) {
        const auto& p=screens[i];
        if(chest&&((i==1&&s<0)||(i==2&&s>0)))continue;
        // The leg terminates inside a bolted rear clamp on the vertical rim,
        // not in empty space below the cut corner of the monitor.
        const auto end=Point(p,s*(p.w/2+.014f),-p.h/2+.025f,-.024f);
        if(chest&&i==0) {
            // The Wagon's lower display is right under the radar: the radar's
            // legs stand on the gauge pods beside it, clear of its glass.
            Beam(v,end,{s*.30f,-.575f,.97f},.012f,Steel);
            auto mount=p;mount.c=Point(p,s*(p.w/2+.014f),-p.h/2+.025f,-.030f);
            GripBody(v,mount,.038f,.080f,.025f,Dark);
            continue;
        }
        const P foot{end[0],carrierAt[1],carrierAt[2]};Beam(v,end,foot,.012f,Steel);
        auto mount=p;mount.c=Point(p,s*(p.w/2+.014f),-p.h/2+.025f,-.030f);
        GripBody(v,mount,.038f,.080f,.025f,Dark);
        Box(v,Sub(foot,{.023f,.043f,clampDepth}),Add(foot,{.023f,.043f,clampDepth}),Dark);
        FrameBolt(v,Add(foot,{0,0,chest?-.033f:-.045f}),{0,0,-1},.004f);
    }
    // The radar has the same open rim as its neighbours: no overhanging
    // dashboard or ornamental annunciator. A shallow service seam and two
    // captive retainers finish the existing top rail without raising it.
    const auto& radar=screens[0];
    Beam(v,Point(radar,-.060f,radar.h/2+.020f,.003f),
        Point(radar,.060f,radar.h/2+.020f,.003f),.0012f,Rubber);   // on the rail's face
    for(float s:{-1.f,1.f}) {
        auto clip=radar;clip.c=Point(radar,s*radar.w*.24f,radar.h/2+.018f,.015f);
        Beam(v,Point(clip,0,0,.002f),Point(clip,0,0,.006f),.003f,Steel);
        Beam(v,Point(clip,-.0015f,0,.007f),Point(clip,.0015f,0,.007f),.0008f,Rubber);
    }
    // Floor observation panes retained for animated legs. One uninterrupted
    // lower observation window; no central mullion. The pedals are clamped
    // to the floor sills; in the Wagon's chest they ride on carriers run back
    // to the deck, their heels on the pane's level as the chest's well allows.
    if(chest) {
        ChestFloorAndDucts(v);
        for(float side:{-1.f,1.f}) {
            const auto start=v.size();
            PedalAssembly(v,side,.22f,.595f,.20f);
            MovePart(v,start,{1,1,1},{0,.255f,0});
        }
    } else {
        FloorAndDucts(v);
        for(float side:{-1.f,1.f})PedalAssembly(v,side);
    }
    NixMainMonitorSeams(v,chest);
    return v;
}
std::vector<CockpitVertex> BuildCrawlerCockpit() {return CrawlerInterior();}
// Shared original seat/controls, separate spherical cabin and closure meshes.
#include "cockpit_barga.inc"
std::vector<CockpitVertex> BuildBargaCockpit() {return BargaInterior();}
// Shared seat/instruments again, inside one measured Proteus gun pod.
#include "cockpit_proteus.inc"
#include "cockpit_proteus_cabin.inc"
#include "cockpit_tank.inc"
#include "cockpit_combat.inc"
#include "cockpit_heli_kit.inc"
#include "cockpit_heli_nereid.inc"
#include "cockpit_heli_euros.inc"
#include "cockpit_heli_heron.inc"
#include "cockpit_heli_brute.inc"
#include "cockpit_truck_pickup.inc"
#include "cockpit_heli.inc"
#include "cockpit_brute_gunner.inc"
std::vector<CockpitVertex> BuildProteusGunnerCockpit() {return ProteusInterior(false);}
std::vector<CockpitVertex> BuildTitanGunnerCockpit() {return ProteusInterior(true);}
std::vector<CockpitVertex> BuildTankCockpit() {return TankInterior();}
std::vector<CockpitVertex> BuildProteusCabinCockpit(bool rear) {return ProteusCabinInterior(rear);}
std::vector<CockpitVertex> BuildCockpit(CockpitKind kind) {
    if(kind==CockpitKind::ProteusDriver)return BuildProteusCabinCockpit(false);
    if(kind==CockpitKind::ProteusMissile)return BuildProteusCabinCockpit(true);
    if(kind==CockpitKind::ProteusGunner)return BuildProteusGunnerCockpit();
    if(kind==CockpitKind::TitanGunner)return BuildTitanGunnerCockpit();
    if(kind==CockpitKind::Tank)return BuildTankCockpit();
    if(IsCombatKind(kind))return CombatInterior(kind);
    if(IsHeliKind(kind))return HeliInterior(kind);
    if(kind==CockpitKind::HeliBruteGunner)return BruteGunnerInterior();
    if(kind==CockpitKind::Barga)return BuildBargaCockpit();
    return kind==CockpitKind::Crawler?BuildCrawlerCockpit():BuildNixCockpit(kind==CockpitKind::NixChest);
}
std::vector<CockpitVertex> BuildCombatLid(CockpitKind kind,const std::vector<float>& hull,bool fittings) {
    return IsHeliKind(kind)?HeliLining(kind,hull):CombatLid(kind,hull,fittings);
}
std::vector<CockpitVertex> BuildHeliLining(CockpitKind kind,const std::vector<float>& hull) {return HeliLining(kind,hull);}
std::vector<CockpitVertex> BuildCombatDecor(CockpitKind kind,const std::vector<float>& hull) {return CombatDecor(kind,hull);}
std::vector<CockpitVertex> BuildCockpitDisplays(CockpitKind kind) {
    std::vector<CockpitVertex> v;
    const bool tandem=kind==CockpitKind::ProteusDriver||kind==CockpitKind::ProteusMissile;
    // The tank driver's screens are the Proteus driver's, let into the console;
    // the combat vehicles use the same station.
    const bool tank=kind==CockpitKind::Tank||IsCombatKind(kind);
    // Helicopters: let into the instrument panel, opaque; a rebuilt one's
    // (kHeliCockpits) hang in the view, see-through.
    const bool heli=IsHeliKind(kind)&&!hk::Find(kind);
    const auto heliScreens=HeliPanels(kind);
    const bool doorGunner=kind==CockpitKind::HeliBruteGunner;
    const auto chosen=doorGunner?bruteGunnerPanels:IsHeliKind(kind)?heliScreens.data():tandem||tank?proteusCabinPanels:kind==CockpitKind::ProteusGunner?proteusPanels:
        kind==CockpitKind::TitanGunner?titanPanels:
        kind==CockpitKind::Barga?bargaPanels:kind==CockpitKind::Crawler?crawlerPanels:kind==CockpitKind::NixChest?chestPanels:panels;
    for(unsigned panel=0;panel<(kind==CockpitKind::Nix||kind==CockpitKind::NixChest?4u:tandem?6u:3u);++panel) {
        const auto& p=chosen[panel];
        const float w=p.w-.010f,h=p.h-.010f;
        const auto start=v.size();Quad(v,Point(p,-w/2,h/2,.004f),Point(p,w/2,h/2,.004f),Point(p,w/2,-h/2,.004f),Point(p,-w/2,-h/2,.004f),Glass);
        const unsigned corners[]={0,1,2,0,2,3};
        for(unsigned i=0;i<6;++i) {
            auto& vert=v[start+i];const auto k=corners[i];vert.surface=16.f+float(p.source);
            vert.uv[0]=(k==1||k==2)?1.f:0.f;vert.uv[1]=(k>=2)?1.f:0.f;
            // The driver's screens are let into the console: opaque. The
            // operator's hang in the air and stay see-through. The tank
            // gunner's stand on stalks and an arm off to the side: opaque, so
            // the yoke behind the radar does not show through it.
            if(((tandem||tank)&&panel<3)||kind==CockpitKind::TitanGunner||heli||HeliScreenOpaque(kind,panel))vert.visibility[1]=0;
        }
    }
    // The rear station's build is shifted, and its screens go with it.
    if(kind==CockpitKind::ProteusMissile)MovePart(v,0,{1,1,1},{0,-kCabinRearUp,-kCabinRearBack});
    return v;
}
}
