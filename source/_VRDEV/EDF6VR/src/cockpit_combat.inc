// Combat vehicles (Grape, Negling, Caliban): the tank's station set into the
// driver's compartment of the real vehicle, left of the centreline, entirely
// inside its hull (cockpit_combat_shells.h, research/cockpit/combat_shells.py).
// Above the sill the compartment is bounded by the vehicle's own faces round
// the driver - roof, front faces, left side - and an inner partition on the
// right. The cockpit's walls are monitors (CombatMonitorSeams), the frame and
// the body beyond them. Behind the head a bulkhead closes it from sill to
// roof. Below the sill it is a closed box that steps in over the tracks or
// wheels, with a vehicle's stowage in it.
struct CombatFace {P n;float d;int kind;std::vector<P> poly;};   // kind: 0 pane, 1 bulkhead, 2 sill
std::vector<P> CombatClip(const std::vector<P>& poly,P n,float d) {
    std::vector<P> out;
    for(std::size_t i=0;i<poly.size();++i) {
        const auto& a=poly[i];const auto& b=poly[(i+1)%poly.size()];
        const float da=Dot(n,a)-d,db=Dot(n,b)-d;
        if(da<=1e-6f)out.push_back(a);
        if((da<-1e-6f&&db>1e-6f)||(da>1e-6f&&db<-1e-6f))out.push_back(Add(a,Mul(Sub(b,a),da/(da-db))));
    }
    return out;
}
std::vector<P> CombatPlaneQuad(P n,float d) {
    const P ref=std::fabs(n[1])<.9f?P{0,1,0}:P{1,0,0};
    const auto u=Unit(Cross(n,ref)),w=Cross(n,u);const auto c=Mul(n,d);
    return {Add(c,Add(Mul(u,-10),Mul(w,-10))),Add(c,Add(Mul(u,10),Mul(w,-10))),Add(c,Add(Mul(u,10),Mul(w,10))),Add(c,Add(Mul(u,-10),Mul(w,10)))};
}
// The compartment above the sill: every bounding plane's face.
std::vector<CombatFace> CombatUpperFaces(const CombatCabinShell& c) {
    std::vector<CombatFace> planes;
    for(unsigned i=0;i<c.planeCount;++i)planes.push_back({{c.n[i][0],c.n[i][1],c.n[i][2]},c.d[i],c.pane[i]?0:1,{}});
    planes.push_back({{0,-1,0},-c.sill,2,{}});
    for(std::size_t i=0;i<planes.size();++i) {
        auto poly=CombatPlaneQuad(planes[i].n,planes[i].d);
        for(std::size_t j=0;j<planes.size()&&poly.size()>=3;++j)if(j!=i)poly=CombatClip(poly,planes[j].n,planes[j].d);
        planes[i].poly=poly.size()>=3?poly:std::vector<P>{};
    }
    return planes;
}
// A plate on a face polygon, its thickness taken inward.
void CombatPlate(std::vector<CockpitVertex>& v,const std::vector<P>& poly,P outward,float thick,unsigned m) {
    if(poly.size()<3)return;
    RoundedPrism(v,poly,Mul(outward,-thick),.004f,m);
}
// Monitor division lines. The user: the cockpit's walls are monitors, with the
// frame and the body beyond them; the monitors take in the whole view the cut
// opened, roughly following the body - no frames. The cockpit's own faces
// round the driver (roof, chamfer or slanted side, side, front, glacis,
// windscreen) are carried across the vehicle, those facing left mirrored to
// the right and the inner partition dropped, and a line runs wherever two of
// them meet, where the outside is seen: the cut, less the Caliban's cab behind
// its pillars, which its covers close. Model coordinates, as segments.
std::vector<std::array<P,2>> CombatSeamLines(const CombatShell& s) {
    struct Plane {P n;float d;bool face;};
    std::vector<Plane> planes;
    for(unsigned i=0;i<s.planeCount&&i<8;++i) {
        const auto& q=s.planes[i];
        if(!q.pane||q.n[0]<-.05f)continue;   // the bulkhead, the inner partition
        planes.push_back({{q.n[0],q.n[1],q.n[2]},q.d,true});
        if(q.n[0]>.05f)planes.push_back({{-q.n[0],q.n[1],q.n[2]},q.d,true});
    }
    planes.push_back({{0,-1,0},-s.cutFloor,false});
    planes.push_back({{0,0,-1},-s.cutRear,false});
    auto seen=CombatCutRegions(s,.012f);
    if(CombatCutNarrow(s)&&CombatCutCapped(s)&&seen.size()>1)seen.erase(seen.begin());
    std::vector<std::array<P,2>> out;
    for(std::size_t i=0;i<planes.size();++i)for(std::size_t j=i+1;j<planes.size();++j) {
        if(!planes[i].face||!planes[j].face)continue;
        const auto& a=planes[i];const auto& b=planes[j];
        auto u=Cross(a.n,b.n);const float length=std::sqrt(Dot(u,u));if(length<.05f)continue;
        u=Mul(u,1/length);
        // The point of both planes nearest the origin along u.
        const auto p0=Mul(Add(Mul(Cross(b.n,u),a.d),Mul(Cross(u,a.n),b.d)),1/Dot(a.n,Cross(b.n,u)));
        float t0=-100,t1=100;bool empty=false;
        for(std::size_t k=0;k<planes.size()&&!empty;++k) {
            if(k==i||k==j)continue;
            const float along=Dot(planes[k].n,u),room=planes[k].d-Dot(planes[k].n,p0);
            if(std::fabs(along)<1e-6f){empty=room<0;continue;}
            if(along>0)t1=std::min(t1,room/along);else t0=std::max(t0,room/along);
        }
        if(empty||t1-t0<.02f)continue;
        // The parts in sight: each region's interval, merged.
        std::vector<std::pair<float,float>> spans;
        for(const auto& r:seen) {
            float r0=t0,r1=t1;
            for(unsigned k=0;k<r.count&&r0<r1;++k) {
                const P n{r.n[k][0],r.n[k][1],r.n[k][2]};
                const float along=Dot(n,u),room=r.d[k]-Dot(n,p0);
                if(std::fabs(along)<1e-6f){if(room<0)r1=r0;continue;}
                if(along>0)r1=std::min(r1,room/along);else r0=std::max(r0,room/along);
            }
            if(r1-r0>.001f)spans.push_back({r0,r1});
        }
        std::sort(spans.begin(),spans.end());
        for(std::size_t k=0;k<spans.size();) {
            auto [from,to]=spans[k];
            for(++k;k<spans.size()&&spans[k].first<=to+.001f;++k)to=std::max(to,spans[k].second);
            if(to-from>=.05f)out.push_back({Add(p0,Mul(u,from)),Add(p0,Mul(u,to))});
        }
    }
    return out;
}
// The lines as the Nix's screen ink (surface 21, drawn with the cabin's
// translucent pass), in cabin coordinates, as wide to the eye as the Nix's
// wherever they stand. Appended last.
void CombatMonitorSeams(std::vector<CockpitVertex>& v,const CombatShell& s) {
    const float o[3]={s.eye[0],s.eye[1]-kCockpitSeatedEye[1],s.eye[2]-kCockpitSeatedEye[2]};
    auto cabin=[&](const P& m){return P{-(m[0]-o[0]),m[1]-o[1],m[2]-o[2]};};
    const P eye{kCockpitSeatedEye[0],kCockpitSeatedEye[1],kCockpitSeatedEye[2]};
    const auto start=v.size();
    for(const auto& line:CombatSeamLines(s)) {
        const P a=cabin(line[0]),b=cabin(line[1]);
        const float length=std::sqrt(Dot(Sub(b,a),Sub(b,a)));
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
// A convex face polygon moved in by `by` from every edge (in its own plane),
// then each corner cut `cut` along both its edges. Empty if too small.
std::vector<P> CombatInset(const std::vector<P>& q,P n,float by,float cut) {
    const std::size_t count=q.size();if(count<3)return {};
    P centre{};for(const auto& p:q)centre=Add(centre,Mul(p,1.f/count));
    std::vector<P> in(count);
    for(std::size_t i=0;i<count;++i) {
        const auto& prev=q[(i+count-1)%count];const auto& cur=q[i];const auto& next=q[(i+1)%count];
        auto inward=[&](P a,P b){auto d=Unit(Cross(n,Sub(b,a)));if(Dot(d,Sub(centre,a))<0)d=Mul(d,-1);return d;};
        const auto d0=inward(prev,cur),d1=inward(cur,next),e0=Unit(Sub(cur,prev));
        const float denom=Dot(e0,d1);if(std::fabs(denom)<1e-4f)return {};
        in[i]=Add(cur,Add(Mul(d0,by),Mul(e0,(by-Dot(Mul(d0,by),d1))/denom)));
    }
    // Still the same way round, and no edge turned inside out.
    for(std::size_t i=0;i<count;++i) {
        const auto a=Sub(q[(i+1)%count],q[i]),b=Sub(in[(i+1)%count],in[i]);
        if(Dot(a,b)<=0)return {};
    }
    std::vector<P> out;
    for(std::size_t i=0;i<count;++i) {
        const auto& prev=in[(i+count-1)%count];const auto& cur=in[i];const auto& next=in[(i+1)%count];
        const auto a=Sub(prev,cur),b=Sub(next,cur);
        const float la=std::sqrt(Dot(a,a)),lb=std::sqrt(Dot(b,b));
        if(la<1e-4f||lb<1e-4f)continue;
        out.push_back(Add(cur,Mul(a,std::min(cut,la*.3f)/la)));
        out.push_back(Add(cur,Mul(b,std::min(cut,lb*.3f)/lb)));
    }
    return out;
}
// Inside a convex polygon lying across x and y (the bulkhead's plane).
bool CombatInsideXY(const std::vector<P>& q,float x,float y) {
    float sign=0;
    for(std::size_t i=0;i<q.size();++i) {
        const auto& a=q[i];const auto& b=q[(i+1)%q.size()];
        const float c=(b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]);
        if(std::fabs(c)<1e-7f)continue;
        if(sign==0)sign=c;else if((c>0)!=(sign>0))return false;
    }
    return true;
}
// The bulkhead behind the seat: an armoured bulkhead, not a sheet. The closing
// plate stays whole (the rear is shut); on it an armour panel with its corners
// cut deep and its edges eased, a raised inner plate on that, bolted all round
// in the step between them, and either side of the head a service case with
// its hinged, latched lid and data plate, and an air grille in its housing.
void CombatBulkhead(std::vector<CockpitVertex>& v,const std::vector<P>& poly,P outward) {
    if(poly.size()<3)return;
    const auto in=Mul(outward,-1);   // toward the seat
    CombatPlate(v,poly,outward,.030f,Hull);
    const float face=poly[0][2]+in[2]*.030f;   // the plate's face, cabin z
    auto onto=[&](const std::vector<P>& q,float z){std::vector<P> out;for(auto p:q){p[2]=z;out.push_back(p);}return out;};
    const auto armour=onto(CombatInset(poly,outward,.035f,.085f),face);
    if(armour.size()<3)return;
    RoundedPrism(v,armour,Mul(in,.022f),.008f,Hull);
    const float armourFace=face+in[2]*.022f;
    const auto inner=onto(CombatInset(poly,outward,.090f,.060f),armourFace);
    if(inner.size()>=3) {
        RoundedPrism(v,inner,Mul(in,.008f),.003f,Hull);
        // Bolts round the step, every 16 cm or so along each edge.
        const auto line=onto(CombatInset(poly,outward,.062f,.075f),armourFace+in[2]*.0015f);
        for(std::size_t i=0;i<line.size();++i) {
            const auto a=line[i],b=line[(i+1)%line.size()];const auto d=Sub(b,a);
            const float length=std::sqrt(Dot(d,d));if(length<.05f)continue;
            const unsigned n=std::max(1u,static_cast<unsigned>(length/.16f));
            for(unsigned k=0;k<n;++k)FrameBolt(v,Add(a,Mul(d,(k+.5f)/n)),in,.0055f);
        }
    }
    const float innerFace=armourFace+in[2]*.008f;
    // Either side of the head (the headrest keeps x within +-.22).
    float y0=1e9f,y1=-1e9f;for(const auto& p:inner)y0=std::min(y0,p[1]),y1=std::max(y1,p[1]);
    if(inner.size()<3)return;
    const float y=(y0+y1)*.5f-.01f;   // a little low: clear of the cut top corners
    auto fits=[&](float x,float w,float h){
        for(float sx:{-1.f,1.f})for(float sy:{-1.f,1.f})if(!CombatInsideXY(inner,x+sx*(w*.5f+.012f),y+sy*(h*.5f+.012f)))return false;
        return true;
    };
    auto place=[&](float side,float w,float h,float& x){
        for(float width=w;width>=w*.5f;width-=.02f){x=side*(.22f+width*.5f);if(fits(x,width,h))return width;}
        return 0.f;
    };
    const P right{-1,0,0},up{0,1,0};   // Cross(right,up) = -z: into the wall
    // Over the headrest, a grab handle on two stand-offs, each on a bolted pad.
    if(CombatInsideXY(inner,-.20f,y1-.035f)&&CombatInsideXY(inner,.20f,y1-.035f)) {
        const float hy=y1-.040f,out=innerFace+in[2]*.045f;
        for(float sx:{-1.f,1.f}) {
            Panel pad{{sx*.17f,hy,innerFace+in[2]*.010f},right,up,0,0,0};
            GripBody(v,pad,.050f,.040f,.010f,Dark,.003f);
            Rod(v,{sx*.17f,hy,innerFace+in[2]*.022f},{sx*.17f,hy,out},.009f,Steel);
        }
        Rod(v,{-.185f,hy,out},{.185f,hy,out},.012f,Steel);
    }
    float x=0;
    if(const float w=place(-1,.30f,.18f,x);w>0) {
        Panel p{{x,y,innerFace+in[2]*.060f},right,up,0,0,0};
        ProteusCase(v,p,w,.18f,.060f,true);
    }
    if(const float w=place(1,.28f,.15f,x);w>0) {
        Panel p{{x,y,innerFace+in[2]*.016f},right,up,0,0,0};
        GripBody(v,p,w,.15f,.016f,Dark,.004f);
        auto g=p;g.c=Point(p,0,0,.014f);Part(v,referenceVent,g,w-.04f,.11f,.016f,Steel);
        for(float sx:{-1.f,1.f})FrameBolt(v,Point(p,sx*(w*.5f-.018f),0,.0165f),in,.004f);
    }
}
// Axis-aligned wall plate: a rectangle on one face of the lower box.
void CombatBoxWall(std::vector<CockpitVertex>& v,int axis,float at,float sign,float a0,float a1,float b0,float b1,float thick=.030f) {
    // axis: the wall's normal axis; sign: +1 if the outside is the + side.
    std::vector<P> q;
    for(auto [s,t]:{std::pair{a0,b0},std::pair{a1,b0},std::pair{a1,b1},std::pair{a0,b1}}) {
        P p{};p[axis]=at;p[(axis+1)%3]=s;p[(axis+2)%3]=t;q.push_back(p);
    }
    P out{};out[axis]=sign;
    CombatPlate(v,q,out,thick,Hull);
}
// The sill ledge: the compartment's floor at the sill, outside the lower box.
void CombatLedge(std::vector<CockpitVertex>& v,const std::vector<P>& sill,const CombatCabinShell& c) {
    if(sill.size()<3)return;
    std::vector<std::vector<P>> strips;
    strips.push_back(CombatClip(sill,{1,0,0},c.xLeft));                       // left of the box
    auto rest=CombatClip(sill,{-1,0,0},-c.xLeft);
    strips.push_back(CombatClip(rest,{0,0,-1},-c.zFront));                     // ahead of it
    rest=CombatClip(rest,{0,0,1},c.zFront);
    strips.push_back(CombatClip(rest,{-1,0,0},-c.xRight));                     // right of it
    strips.push_back(CombatClip(CombatClip(rest,{1,0,0},c.xRight),{0,0,1},c.zRear));   // behind it
    // Thickened downward, into the hull under the sill: upward, the plate's
    // edge would stand through the sloping front and side faces.
    for(const auto& s:strips)if(s.size()>=3)CombatPlate(v,s,{0,1,0},.030f,Hull);
}
// Fuel cans standing on the floor against the right wall.
void CombatCans(std::vector<CockpitVertex>& v,float wallX,float floor,float z0) {
    constexpr float width=.165f,length=.345f,height=.47f;
    const float x=wallX-.035f-width*.5f;
    {   // tray
        std::vector<P> tray{{wallX-.03f,floor,z0-.02f},{wallX-.03f-width-.05f,floor,z0-.02f},
            {wallX-.03f-width-.05f,floor,z0+2*length+.06f},{wallX-.03f,floor,z0+2*length+.06f}};
        RoundedPrism(v,tray,{0,.024f,0},.004f,Dark);
    }
    for(int k=0;k<2;++k) {
        const float zc=z0+length*.5f+k*(length+.02f);const float y0=floor+.024f;
        RoundedPrism(v,{{x-width*.5f,y0,zc-length*.5f},{x+width*.5f,y0,zc-length*.5f},{x+width*.5f,y0,zc+length*.5f},{x-width*.5f,y0,zc+length*.5f}},
            {0,height,0},.014f,Hull);
        for(float s:{-1.f,1.f})
            Beam(v,{x-width*.5f-.002f,y0+.06f,zc-s*length*.36f},{x-width*.5f-.002f,y0+height-.07f,zc+s*length*.36f},.006f,Hull);
        for(float k2:{-.06f,0.f,.06f})Beam(v,{x,y0+height+.018f,zc+k2-.06f},{x,y0+height+.018f,zc+k2-.052f},.012f,Dark);
        Beam(v,{x,y0+height+.030f,zc-.13f},{x,y0+height+.030f,zc+.01f},.008f,Dark);
        Rod(v,{x,y0+height-.004f,zc+.12f},{x,y0+height+.036f,zc+.12f},.024f,Warning);
    }
    for(float y:{.18f,.34f}) {
        const float yy=floor+.024f+y,xs=x-width*.5f-.008f;
        Beam(v,{xs,yy,z0+.01f},{xs,yy,z0+2*length+.01f},.010f,Rubber);
        for(float z:{z0+.01f,z0+2*length+.01f}) {
            Beam(v,{xs,yy,z},{wallX-.034f,yy,z},.010f,Rubber);
            Panel plate{{wallX-.030f,yy,z},{0,0,1},{0,1,0},0,0,0};
            if(Dot(Cross(plate.right,plate.up),P{1,0,0})<0)plate.right=Mul(plate.right,-1);
            GripBody(v,plate,.050f,.060f,.012f,Dark,.003f);
            FrameBolt(v,Point(plate,0,0,.0165f),{-1,0,0},.004f);
        }
    }
}
// Radio and intercom rack on the left wall, on two brackets, with its loom.
void CombatRadio(std::vector<CockpitVertex>& v,float wallX,float y,float z,float lowest) {
    // The case's back on the wall plate (its face stands depth less .015 off it).
    Panel face{{wallX+.030f+.080f,y,z},{0,0,-1},{0,1,0},0,0,0};
    if(Dot(Cross(face.right,face.up),P{-1,0,0})<0)face.right=Mul(face.right,-1);
    ProteusCase(v,face,.30f,.20f,.095f,true);
    for(float u:{-.06f,.06f}) {
        Panel bracket{{wallX+.030f+.004f,y+u,z},{0,0,-1},{0,1,0},0,0,0};
        if(Dot(Cross(bracket.right,bracket.up),P{-1,0,0})<0)bracket.right=Mul(bracket.right,-1);
        GripBody(v,bracket,.36f,.035f,.012f,Dark,.003f);
        for(float x:{-.168f,.168f})FrameBolt(v,Point(bracket,x,0,.0165f),{1,0,0},.004f);
    }
    // The loom down the wall into the shelf (or the floor) below.
    Rod(v,{wallX+.04f,y-.10f,z+.10f},{wallX+.04f,std::max(y-.35f,lowest+.012f),z+.28f},.012f,Cable);
}
// A vent in the front wall and a cable tray across it.
void CombatFrontWall(std::vector<CockpitVertex>& v,float wallZ,float x0,float x1,float y) {
    for(float t:{.30f,.72f}) {
        Panel vent{{x0+(x1-x0)*t,y,wallZ-.030f},{-1,0,0},{0,1,0},0,0,0};
        if(Dot(Cross(vent.right,vent.up),P{0,0,1})<0)vent.right=Mul(vent.right,-1);
        GripBody(v,vent,.20f,.14f,.016f,Dark,.004f);
        auto grille=vent;grille.c=Point(vent,0,0,.014f);Part(v,referenceVent,grille,.17f,.11f,.016f,Steel);
    }
    Slab(v,{x0+.06f,y+.14f,wallZ-.030f},{x1-.06f,y+.14f,wallZ-.030f},{x1-.06f,y+.18f,wallZ-.030f},{x0+.06f,y+.18f,wallZ-.030f},{0,0,-.06f},Dark);
}
// Behind the seat, on the bulkhead: an electronics cabinet and a fire
// extinguisher, both below the roof of the lowest compartment.
void CombatRear(std::vector<CockpitVertex>& v,const CombatCabinShell& c) {
    const float z=c.zRear+.030f;
    Panel box{{0,-.52f,z+.10f},{-1,0,0},{0,1,0},0,0,0};
    if(Dot(Cross(box.right,box.up),P{0,0,-1})<0)box.right=Mul(box.right,-1);
    ProteusCase(v,box,.56f,.34f,.10f,true);
    auto vent=box;vent.c=Point(box,.15f,.07f,.024f+.015f);Part(v,referenceVent,vent,.16f,.10f,.014f,Steel);
    const float bx=c.xRight-.16f;
    Rod(v,{bx,-.80f,z+.09f},{bx,-.40f,z+.09f},.055f,Warning);
    Rod(v,{bx,-.40f,z+.09f},{bx,-.36f,z+.09f},.022f,Dark);
    for(float y:{-.72f,-.48f})Beam(v,{bx-.06f,y,z+.02f},{bx+.06f,y,z+.02f},.010f,Dark);
}
// The lid's fittings, in model coordinates: what a vehicle opened this way
// would carry round its opening, in its own manner - the military two with a
// lip along the cut edges, an engine grille, a stowage box and a crew hatch;
// the Caliban's cab with a window seal, a passenger's dash, a medical kit, an
// extinguisher, its red stripe and cross, door handles and sun visors. Each is
// fixed to the deck, the rear cover, a side cover or the ceiling, stays inside
// the vehicle's original body (under the hull over it, within its sides) and
// clear of the cockpit, and keeps low and to the sides: ahead stays open.
// Hull takes the vehicle's paint and Navy its second tone (the lid's shader).
std::vector<CockpitVertex> CombatDecor(CockpitKind kind,const std::vector<float>& hull) {
    std::vector<CockpitVertex> v;
    const auto* s=FindCombatShell(kind);if(!s||hull.size()<9)return v;
    const float level=s->cutFloor;
    std::vector<unsigned> all;for(std::size_t i=0;i+9<=hull.size();i+=9)all.push_back(static_cast<unsigned>(i));
    // Clear height over a rectangle of the deck.
    auto room=[&](float x0,float z0,float x1,float z1) {
        float r=1e9f;
        for(float x:{x0,(x0+x1)*.5f,x1})for(float z:{z0,(z0+z1)*.5f,z1})r=std::min(r,CombatHullAbove(hull,x,z,level+.01f)-level);
        return r;
    };
    auto edge=[&](float y,float z,float side,float& x) {
        float r=0,l=0;if(!CombatHullSpan(hull,all,y,z,r,l))return false;
        x=side>0?l:r;return true;
    };
    // Within the cockpit (its compartment over the sill, or the box under it),
    // with a margin: where the cockpit reaches the hull's own side, a lip stops.
    const auto cabin=CombatCabin(*s);
    const float o[3]={s->eye[0],s->eye[1]-kCockpitSeatedEye[1],s->eye[2]-kCockpitSeatedEye[2]};
    auto inCockpit=[&](P p) {
        const float c[3]={-(p[0]-o[0]),p[1]-o[1],p[2]-o[2]};constexpr float margin=.015f;
        bool upper=c[1]>cabin.sill;
        for(unsigned k=0;k<cabin.planeCount;++k)upper=upper&&cabin.n[k][0]*c[0]+cabin.n[k][1]*c[1]+cabin.n[k][2]*c[2]<cabin.d[k]+margin;
        const bool lower=c[1]<=cabin.sill+margin&&c[0]>cabin.xLeft-margin&&c[0]<cabin.xRight+margin&&c[2]>cabin.zRear-margin&&c[2]<cabin.zFront+margin;
        return upper||lower;
    };
    auto box=[&](float x0,float y0,float z0,float x1,float y1,float z1,unsigned m) {
        Box(v,{std::min(x0,x1),std::min(y0,y1),std::min(z0,z1)},{std::max(x0,x1),std::max(y0,y1),std::max(z0,z1)},m);
    };
    // A lip along a cut edge of the deck: its outer face just inside the hull's
    // skin, following it up, the rest inward; bolted every 40 cm.
    auto lip=[&](float z0,float z1,float side,float height,float thick,unsigned m,bool bolts) {
        bool have=false;P a0{},b0{},c0{},d0{};float bolted=-1e9f;
        for(float z=z0;z<=z1+.001f;z+=.05f) {
            const float at=std::min(z,z1);float low=0,high=0;
            bool ok=edge(level+.004f,at,side,low)&&edge(level+height,at,side,high);
            if(ok){low-=side*.010f;high-=side*.010f;if(side*(high-low)>0)high=low;}
            const P a{low,level,at},b{high,level+height,at},c{high-side*thick,level+height,at},d{low-side*thick,level,at};
            for(const auto& q:{b,c})ok=ok&&CombatHullAbove(hull,q[0],at,level+.01f)>=q[1]+.004f;
            for(const auto& q:{a,b,c,d})ok=ok&&!inCockpit(q);
            if(ok&&!have)Quad(v,a,b,c,d,m);
            if(ok&&have){Quad(v,a0,a,b,b0,m);Quad(v,b0,b,c,c0,m);Quad(v,c0,c,d,d0,m);}
            if(!ok&&have)Quad(v,a0,b0,c0,d0,m);
            if(ok&&bolts&&at-bolted>=.40f){FrameBolt(v,Add(Mul(Add(c,d),.5f),P{-side*.0015f,0,0}),{-side,0,0},.006f);bolted=at;}
            have=ok;a0=a;b0=b;c0=c;d0=d;
        }
        if(have)Quad(v,a0,b0,c0,d0,m);
    };
    // A flat louvred grille in a housing, on a surface facing `out` (up or +z).
    auto grille=[&](P centre,P out,float w,float h) {
        const P right=out[1]>.5f?P{1,0,0}:P{-1,0,0},up=out[1]>.5f?P{0,0,1}:P{0,1,0};
        Panel p{Add(centre,Mul(out,.016f)),right,up,0,0,0};
        GripBody(v,p,w,h,.016f,Dark,.004f);
        auto g=p;g.c=Point(p,0,0,.014f);Part(v,referenceVent,g,w-.04f,h-.04f,.016f,Steel);
    };
    if(kind==CockpitKind::CombatNegling||kind==CockpitKind::CombatGrape) {
        const bool negling=kind==CockpitKind::CombatNegling;
        // Lips along both cut edges, from the rear cover forward.
        for(float side:{-1.f,1.f})lip(s->cutRear+.001f,s->zFront+.9f,side,.050f,.035f,Hull,true);
        // On the rear cover, right of the cockpit's bulkhead: an air grille.
        const float coverY=level+(negling?.17f:.22f);
        grille({negling?-.80f:-.66f,coverY,s->cutRear},{0,0,1},negling?.44f:.40f,negling?.18f:.16f);
        if(negling) {
            // The right deck, back to front: engine grille, stowage box, crew hatch.
            if(room(-1.12f,.02f,-.52f,.50f)>.06f)grille({-.82f,level,.26f},{0,1,0},.60f,.48f);
            if(room(-1.14f,.75f,-.58f,1.21f)>.23f) {
                Panel lid{{-.86f,level+.16f,.98f},{1,0,0},{0,0,1},0,0,0};
                ProteusCase(v,lid,.56f,.46f,.16f,false);
            }
            if(room(-1.14f,1.42f,-.54f,2.02f)>.12f) {
                const float cx=-.84f,cz=1.72f;
                Rod(v,{cx,level,cz},{cx,level+.030f,cz},.30f,Hull);             // ring
                Rod(v,{cx,level+.030f,cz},{cx,level+.055f,cz},.265f,Navy);     // lid, the other tone
                for(int k=0;k<8;++k) {
                    const float t=k*.785398f+.3927f;
                    FrameBolt(v,{cx+.283f*std::cos(t),level+.0315f,cz+.283f*std::sin(t)},{0,1,0},.006f);
                }
                box(-1.17f,level,1.60f,-1.11f,level+.060f,1.84f,Steel);        // hinge block
                Rod(v,{-1.14f,level+.060f,1.58f},{-1.14f,level+.060f,1.86f},.018f,Steel);
                for(float z:{cz-.08f,cz+.08f})Rod(v,{cx+.16f,level+.055f,z},{cx+.16f,level+.082f,z},.008f,Steel);
                Rod(v,{cx+.16f,level+.082f,cz-.09f},{cx+.16f,level+.082f,cz+.09f},.011f,Steel);   // handle
            }
            // Panel joints across the right deck, between the fittings.
            for(float z:{.62f,1.33f})Beam(v,{-1.20f,level+.0015f,z},{-.46f,level+.0015f,z},.004f,Dark);
            // Lifting eyes at the front corners.
            for(float x:{-1.02f,1.02f})if(room(x-.03f,2.86f,x+.03f,2.98f)>.07f) {
                box(x-.035f,level,2.85f,x+.035f,level+.006f,2.99f,Steel);
                for(float z:{2.88f,2.96f})Rod(v,{x,level+.006f,z},{x,level+.046f,z},.011f,Steel);
                Rod(v,{x,level+.046f,2.877f},{x,level+.046f,2.963f},.011f,Steel);
            }
        } else {
            // The Grape carries troops: a roof hatch on the right deck, and a
            // stowage box ahead of it.
            if(room(-1.02f,.52f,-.56f,1.28f)>.08f) {
                RoundedPrism(v,{{-1.02f,level,.52f},{-.56f,level,.52f},{-.56f,level,1.28f},{-1.02f,level,1.28f}},{0,.028f,0},.004f,Hull);
                RoundedPrism(v,{{-.995f,level+.028f,.545f},{-.585f,level+.028f,.545f},{-.585f,level+.028f,1.255f},{-.995f,level+.028f,1.255f}},{0,.020f,0},.004f,Navy);
                for(float x:{-1.008f,-.572f})for(float z:{.532f,1.268f})FrameBolt(v,{x,level+.0295f,z},{0,1,0},.005f);
                for(float z:{.64f,1.16f})Rod(v,{-1.035f,level+.034f,z-.06f},{-1.035f,level+.034f,z+.06f},.016f,Steel);   // hinges
                for(float z:{.84f,.96f})Rod(v,{-.64f,level+.048f,z},{-.64f,level+.072f,z},.008f,Steel);
                Rod(v,{-.64f,level+.072f,.83f},{-.64f,level+.072f,.97f},.011f,Steel);                               // handle
            }
            if(room(-.98f,1.50f,-.54f,1.96f)>.18f) {
                Panel lid{{-.76f,level+.12f,1.73f},{1,0,0},{0,0,1},0,0,0};
                ProteusCase(v,lid,.44f,.44f,.12f,false);
            }
            for(float z:{.40f,1.40f,2.10f})Beam(v,{-1.08f,level+.0015f,z},{-.50f,level+.0015f,z},.004f,Dark);
        }
    } else if(kind==CockpitKind::CombatCaliban) {
        const float wide=s->cutHalfWidth,back=s->cutRear,top=s->cutTop,to=s->cutWideFrom;
        // The window seal along the door tops, ahead of the cut's side covers.
        for(float side:{-1.f,1.f})lip(to+.005f,4.50f,side,.030f,.030f,Dark,false);
        // The passenger's side of the dash, ahead of the right seat.
        if(room(-1.30f,3.98f,-.18f,4.42f)>.20f) {
            box(-1.28f,level,4.02f,-.20f,level+.090f,4.40f,Hull);
            box(-1.30f,level+.090f,3.98f,-.18f,level+.130f,4.42f,Dark);     // the pad over it
            box(-.95f,level+.020f,4.012f,-.50f,level+.075f,4.020f,Hull);    // glovebox door
            box(-.75f,level+.040f,4.004f,-.69f,level+.055f,4.012f,Steel);   // its latch
        }
        // A medical kit strapped down behind the right seat, its red cross to the cab.
        if(room(-1.30f,2.48f,-.80f,2.86f)>.25f) {
            box(-1.30f,level,2.48f,-.80f,level+.220f,2.86f,Hull);
            box(-1.303f,level+.172f,2.477f,-.797f,level+.178f,2.863f,Dark);   // the lid's seal
            for(float x:{-1.20f,-.90f}) {
                box(x-.022f,level,2.470f,x+.022f,level+.224f,2.480f,Dark);
                box(x-.022f,level+.220f,2.470f,x+.022f,level+.224f,2.870f,Dark);
                box(x-.022f,level,2.860f,x+.022f,level+.224f,2.870f,Dark);
            }
            box(-1.13f,level+.075f,2.860f,-.97f,level+.125f,2.864f,Navy);
            box(-1.075f,level+.020f,2.860f,-1.025f,level+.160f,2.864f,Navy);
        }
        // The cab's back wall right of the driver's bulkhead: the red stripe (it
        // runs on round the side covers), the cross above, an extinguisher.
        box(-wide+.005f,level+.51f,back,-.12f,level+.61f,back+.005f,Navy);
        box(-.97f,4.20f,back,-.63f,4.30f,back+.005f,Navy);
        box(-.85f,4.08f,back,-.75f,4.42f,back+.005f,Navy);
        box(-1.42f,level+.15f,back,-1.26f,level+.21f,back+.060f,Dark);            // bracket
        box(-1.42f,level+.35f,back,-1.26f,level+.40f,back+.060f,Dark);
        Rod(v,{-1.34f,level+.05f,back+.115f},{-1.34f,level+.43f,back+.115f},.055f,Navy);
        Rod(v,{-1.34f,level+.43f,back+.115f},{-1.34f,level+.49f,back+.115f},.022f,Dark);
        for(float side:{-1.f,1.f}) {
            const float x=side*wide;
            box(x,level+.51f,back,x-side*.005f,level+.61f,2.98f,Navy);        // the stripe along the side cover, to the pillar
            box(x,level,2.45f,x-side*.090f,level+.080f,2.95f,Dark);            // armrest on the door top
            for(float z:{2.64f,2.84f})Rod(v,{x,level+.27f,z},{x-side*.045f,level+.27f,z},.008f,Steel);
            Rod(v,{x-side*.045f,level+.27f,2.62f},{x-side*.045f,level+.27f,2.86f},.012f,Steel);   // door handle
        }
        // Under the ceiling (the tall body's, behind the visor's own gap): a
        // dome light, a grab handle over the right door, and over the seats the
        // rescue console - siren and light switches, two red buttons, map lights.
        box(-.45f,top-.025f,2.40f,-.15f,top,2.56f,Dark);
        box(-.42f,top-.032f,2.42f,-.18f,top-.025f,2.54f,Glass);
        for(float z:{2.62f,2.88f})Rod(v,{-1.40f,top,z},{-1.40f,top-.08f,z},.008f,Dark);
        Rod(v,{-1.40f,top-.08f,2.60f},{-1.40f,top-.08f,2.90f},.012f,Dark);
        box(-.55f,top-.070f,2.70f,1.00f,top,2.94f,Dark);
        for(int k=0;k<6;++k){const float x=-.42f+k*.075f;box(x-.012f,top-.084f,2.875f,x+.012f,top-.070f,2.895f,Steel);}
        for(float x:{.06f,.16f})Rod(v,{x,top-.070f,2.885f},{x,top-.082f,2.885f},.020f,Navy);
        for(float x:{.55f,.85f})box(x-.06f,top-.075f,2.76f,x+.06f,top-.070f,2.84f,Glass);
    }
    return v;
}
// The lid, in the hull bone's frame (model coordinates less the bone's bind
// origin): of every hull triangle the game no longer draws (it meets the cut,
// cockpit_combat_shells.h), the part outside the cut; and the cut's covers,
// each running out to the hull's outer skin row by row (1 cm), or to the cut's
// own sides: a deck on the cut's floor (the sill, or the Caliban's window
// frames), a cover on the rear plane, covers on the cut's sides where it stops
// short of the hull's walls, and, where it stops short of what stands above
// (the Caliban's visor), a ceiling under that and a cover on its front.
// Looking over the side, up or back past the cockpit the rider meets the body,
// never its inside. Every face is turned to the seat and drawn from both sides.
std::vector<CockpitVertex> CombatLid(CockpitKind kind,const std::vector<float>& hull,bool fittings) {
    std::vector<CockpitVertex> v;
    const auto* s=FindCombatShell(kind);if(!s)return v;
    const P origin{s->bodyOrigin[0],s->bodyOrigin[1],s->bodyOrigin[2]};
    const P eye=Sub(P{s->eye[0],s->eye[1],s->eye[2]},origin);
    auto emit=[&](const CombatPolygon& poly) {
        for(std::size_t k=1;k+1<poly.size();++k) {
            const P a=Sub(P{poly[0][0],poly[0][1],poly[0][2]},origin);
            P b=Sub(P{poly[k][0],poly[k][1],poly[k][2]},origin),c=Sub(P{poly[k+1][0],poly[k+1][1],poly[k+1][2]},origin);
            const auto n=Cross(Sub(b,a),Sub(c,a));if(Dot(n,n)<1e-10f)continue;
            if(Dot(n,Sub(eye,a))<0)std::swap(b,c);
            Triangle(v,a,b,c,Hull);
        }
    };
    const auto cut=CombatCutRegions(*s);const bool capped=CombatCutCapped(*s);
    const float top=capped?s->cutTop:1e9f;
    std::vector<CombatPolygon> parts;
    float zMax=-1e9f,yMax=-1e9f;
    for(std::size_t i=2;i<hull.size();i+=3)zMax=std::max(zMax,hull[i]);
    std::vector<unsigned> atSill,atRear,atCeiling,atFront;
    for(std::size_t i=0;i+9<=hull.size();i+=9) {
        const float* t=&hull[i];
        float y0=1e9f,y1=-1e9f,z0=1e9f,z1=-1e9f;
        for(int k=0;k<3;++k){y0=std::min(y0,t[k*3+1]);y1=std::max(y1,t[k*3+1]);z0=std::min(z0,t[k*3+2]);z1=std::max(z1,t[k*3+2]);}
        zMax=std::max(zMax,z1);yMax=std::max(yMax,y1);
        const auto index=static_cast<unsigned>(i);
        if(y0<=s->cutFloor&&y1>=s->cutFloor-.002f)atSill.push_back(index);
        if(z0<=s->cutRear&&z1>=s->cutRear-.002f&&y1>=s->cutFloor&&y0<=top)atRear.push_back(index);
        if(capped&&y0<=top+.002f&&y1>=top&&z1>=s->cutRear&&z0<=s->cutTopRear)atCeiling.push_back(index);
        if(capped&&z0<=s->cutTopRear&&z1>=s->cutTopRear-.002f&&y1>=top)atFront.push_back(index);
        if(!CombatCutMeets(*s,t))continue;
        parts.clear();
        CombatCutWindows({{t[0],t[1],t[2]},{t[3],t[4],t[5]},{t[6],t[7],t[8]}},cut,0,parts);
        for(const auto& p:parts)emit(p);
    }
    // A cover: rows 1 cm apart along a (from a0 to a1), each across the hull
    // on the line at (y,z) = at(a), no wider than the cut there (half width
    // wide(a)); corner(x,a) places the cover's points.
    auto cover=[&](const std::vector<unsigned>& near,float a0,float a1,auto at,auto wide,auto corner) {
        bool have=false;float r0=0,l0=0,b0=0;
        for(float a=a0;a<=a1+.005f;a+=.01f) {
            const float b=std::min(a,a1);const auto [y,z]=at(b);float r=0,l=0;
            bool ok=CombatHullSpan(hull,near,y,z,r,l);
            const float w=wide(b);r=std::max(r,-w);l=std::min(l,w);ok=ok&&l-r>.02f;
            if(ok&&have)emit({corner(r0,b0),corner(l0,b0),corner(l,b),corner(r,b)});
            have=ok;r0=r;l0=l;b0=b;
        }
    };
    const float rear=s->cutRear,front=s->cutTopRear,level=CombatCutFloor(*s,rear);
    const auto wideAt=[&](float z){return CombatCutHalfWidth(*s,z);};
    const auto wideRear=[&](float){return CombatCutHalfWidth(*s,rear);};
    // The deck: just under the cut's floor, from the rear plane forward.
    cover(atSill,rear,zMax,[&](float z){return std::pair{level-.001f,z};},wideAt,
        [&](float x,float z){return CombatPoint{x,level,z};});
    // The rear cover: just behind the rear plane, from the deck up (to the cap).
    cover(atRear,level,std::min(yMax,top),[&](float y){return std::pair{y,rear-.001f};},wideRear,
        [&](float x,float y){return CombatPoint{x,y,rear};});
    if(CombatCutNarrow(*s)) {
        // The cut's sides, where it stops short of the hull's own walls.
        const float w=s->cutHalfWidth,high=std::min(yMax,top),to=s->cutWideFrom;
        for(float side:{-w,w})emit({{side,level,rear},{side,level,to},{side,high,to},{side,high,rear}});
    }
    if(capped) {
        // The ceiling under what is kept (as far as the sides are kept: ahead of
        // them the kept visor shows its own underside), and a cover on its front.
        cover(atCeiling,rear,CombatCutNarrow(*s)?std::min(front,s->cutWideFrom):front,[&](float z){return std::pair{top+.001f,z};},wideAt,
            [&](float x,float z){return CombatPoint{x,top,z};});
        cover(atFront,top,yMax,[&](float y){return std::pair{y,front-.001f};},[](float){return 1e9f;},
            [&](float x,float y){return CombatPoint{x,y,front};});
    }
    if(fittings)for(auto f:CombatDecor(kind,hull)) {
        for(int k=0;k<3;++k)f.position[k]-=origin[k];
        v.push_back(f);
    }
    return v;
}
std::vector<CockpitVertex> CombatInterior(CockpitKind kind) {
    std::vector<CockpitVertex> v;v.reserve(260000);
    const auto* shell=FindCombatShell(kind);if(!shell)return v;
    const auto c=CombatCabin(*shell);
    const auto faces=CombatUpperFaces(c);
    // Bulkhead above the sill, and the sill ledge round the lower box.
    for(const auto& f:faces) {
        if(f.kind==1)CombatBulkhead(v,f.poly,f.n);
        if(f.kind==2)CombatLedge(v,f.poly,c);
    }
    // Lower box: wide from the sponson to the sill, narrow below it.
    const bool stepped=c.xInner>c.xLeft+.01f&&c.sponson>c.floor+.01f;
    const float lowTop=stepped?c.sponson:c.sill;
    CombatBoxWall(v,0,c.xLeft,-1,stepped?c.sponson:c.floor,c.sill,c.zRear,c.zFront);      // left, upper
    CombatBoxWall(v,0,c.xRight,1,c.floor,c.sill,c.zRear,c.zFront);                        // right
    CombatBoxWall(v,2,c.zRear,-1,c.xLeft,c.xRight,stepped?c.sponson:c.floor,c.sill);      // rear, upper
    CombatBoxWall(v,2,c.zFront,1,c.xLeft,c.xRight,stepped?c.sponson:c.floor,c.sill);      // front, upper
    if(stepped) {
        CombatBoxWall(v,0,c.xInner,-1,c.floor,c.sponson,c.zRear,c.zFront);                 // left, lower
        CombatBoxWall(v,2,c.zRear,-1,c.xInner,c.xRight,c.floor,c.sponson);
        CombatBoxWall(v,2,c.zFront,1,c.xInner,c.xRight,c.floor,c.sponson);
        CombatBoxWall(v,1,c.sponson,-1,c.zRear,c.zFront,c.xLeft,c.xInner);                  // the shelf over the tracks
    }
    // The floor is 6 cm deep: the station's feet and boxes are set into it.
    CombatBoxWall(v,1,c.floor,-1,c.zRear,c.zFront,stepped?c.xInner:c.xLeft,c.xRight,.060f);   // floor
    const float deck=c.floor+.060f;
    const float innerLeft=stepped?c.xInner:c.xLeft;
    // Floor rails and a hatch.
    for(float x:{innerLeft+.12f,c.xRight-.12f})Beam(v,{x,deck+.010f,c.zRear+.10f},{x,deck+.010f,c.zFront-.10f},.012f,Dark);
    // Equipment: radio on the left wall, fuel cans against the right, front vents, rear cabinet.
    CombatRadio(v,c.xLeft,(c.sill+(stepped?c.sponson:c.floor))*.5f,.10f,(stepped?c.sponson:c.floor)+.030f);
    CombatCans(v,c.xRight,deck,.18f);
    CombatFrontWall(v,c.zFront,innerLeft,c.xRight,(c.floor+lowTop)*.5f);
    CombatRear(v,c);
    // The station.
    CabinSeat(v,deck,{0,0,0});
    CabinFootStrut(v,deck,{0,0,0});
    CabinConsole(v,deck);
    CabinCluster(v,proteusCabinPanels,true);
    CabinConsoleDetail(v,deck);
    CombatMonitorSeams(v,*shell);   // last: the translucent pass draws the trailing ink
    return v;
}
