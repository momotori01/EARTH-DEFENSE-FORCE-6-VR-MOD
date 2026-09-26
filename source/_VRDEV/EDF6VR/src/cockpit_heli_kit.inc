// Rebuilt helicopter cockpits (research/cockpit/heli_cockpit.py -> kHeliCockpits):
// the kit every machine is built from, one machine at a time. Cabin
// coordinates: +X the pilot's right, +Y up, +Z forward, the seated eye at
// (0,.10,-.10).
//  - Shell: the fuselage's inside, `margin` in from its skin, every 5 cm along
//    the cabin and in 40 steps from the deck over the top. Below the sill its
//    walls are never wider than above, so the trim runs straight down instead
//    of following a sponson's bulge.
//  - Windows: cut out of the shell square to their glass (the window grown by
//    `cut`, as a prism along its normal), each framed by a ring from 1 cm over
//    the glass to `frame` outside it, as deep as the shell stands in, so the
//    cut edge is always behind a frame.
//  - Monitors: zones of the shell left out, so the world shows through; their
//    edges railed, their tile joints drawn as the Nix's ink.
//  - Glass: faint panes on the windows (surface 24), last with the ink.
void Rivet(std::vector<CockpitVertex>& v,P c,P axis);   // cockpit_heli.inc
namespace hk {
// The cabin as heli_cockpit.py chose it (its deck, ends, sill, monitor zones
// and sizes): the shell `margin` in from the skin, cut `cut` past the glass,
// the frames reaching `frame` past the glass and `framePast` past the shell.
struct Spec {
    float floorY=-1.105f,rear=-.70f,front=1.60f,margin=.016f,sill=-.30f;
    float frame=.020f,cut=.010f,framePast=.008f;
    // A window's cut only 3 cm deep at its edge, deepening to 20 cm under its
    // middle (a long, strongly curved pane's fan centre lies far under its
    // glass, and a cut as deep all over reaches down the steep walls beside
    // its edges, past its thin frame). Set by the machine: the Nereid and the
    // Euros keep the cut they were approved with.
    bool shallowEdges=false;
    // A domed roof (the Brute's): its width falls to nothing at the top (DomeWidth).
    bool domeTop=false;
    // The fuselage's centre line (cabin x): off the eye for a side-by-side
    // pilot (the Brute's). Walls, zones and ribs stand about it; WallX and
    // Cut give half widths from it.
    float centreX=0;
    std::vector<HeliZone> zones;
};
const HeliCockpit* Find(CockpitKind kind) {
    for(const auto& c:kHeliCockpits)if(c.kind==kind)return &c;
    return nullptr;
}
Spec Cabin(const HeliCockpit& c) {
    Spec s;s.floorY=c.floorY;s.rear=c.rear;s.front=c.front;s.sill=c.sill;
    s.margin=c.margin;s.cut=c.cut;s.frame=c.frame;s.framePast=c.framePast;s.centreX=c.centreX;
    for(unsigned i=0;i<c.zoneCount;++i)s.zones.push_back(c.zones[i]);
    return s;
}
// A zone's top at z (linear from its rear to its front).
float ZoneTop(const HeliZone& z,float at) {return z.top0+(z.top1-z.top0)*std::clamp((at-z.z0)/(z.z1-z.z0),0.f,1.f);}
// The skin's half width at (z,y), bilinear over the table; -1 where it has none.
float Width(const HeliCockpit& c,float z,float y) {
    const float fz=(z-c.z0)/.05f,fy=(y-c.y0)/.05f;
    if(fz<0||fy<0||fz>float(c.zn-1)||fy>float(c.yn-1))return -1;
    const unsigned iz=std::min(unsigned(fz),c.zn-2),iy=std::min(unsigned(fy),c.yn-2);
    const float tz=fz-float(iz),ty=fy-float(iy);
    const float a=c.width[iz][iy],b=c.width[iz+1][iy],d=c.width[iz][iy+1],e=c.width[iz+1][iy+1];
    if(a<0||b<0||d<0||e<0)return std::max({a,b,d,e})<0?-1.f:std::max({a,b,d,e});
    return (a*(1-tz)+b*tz)*(1-ty)+(d*(1-tz)+e*tz)*ty;
}
// The half width under a domed roof (the Brute's): where the samples above
// run out of skin, the width falls to nothing at the top on the centre line,
// a chord inside the dome, never the last row's width carried up past it.
float DomeWidth(const HeliCockpit& c,float z,float y) {
    const float fz=(z-c.z0)/.05f,fy=(y-c.y0)/.05f;
    if(fz<0||fy<0||fz>float(c.zn-1)||fy>float(c.yn-1))return -1;
    const unsigned iz=std::min(unsigned(fz),c.zn-2),iy=std::min(unsigned(fy),c.yn-2);
    const float tz=fz-float(iz),ty=fy-float(iy);
    auto column=[&](unsigned k) {
        const float a=c.width[k][iy],d=c.width[k][iy+1];
        if(a>=0&&d>=0)return a+(d-a)*ty;
        if(a<0)return d;
        const float ya=c.y0+.05f*float(iy),yt=c.top[k];
        return yt<=ya+1e-4f?a:std::max(0.f,a*(yt-y)/(yt-ya));
    };
    const float a=column(iz),b=column(iz+1);
    if(a<0||b<0)return std::max(a,b);
    return a*(1-tz)+b*tz;
}
float Top(const HeliCockpit& c,float z) {
    const float fz=std::clamp((z-c.z0)/.05f,0.f,float(c.zn-1));
    const unsigned iz=std::min(unsigned(fz),c.zn-2);const float t=fz-float(iz);
    return c.top[iz]*(1-t)+c.top[iz+1]*t;
}
std::vector<P> Loop(const HeliWindow& w) {
    std::vector<P> p;for(unsigned i=0;i<w.count;++i)p.push_back({w.p[i][0],w.p[i][1],w.p[i][2]});
    return p;
}
P Centre(const std::vector<P>& p) {P c{};for(const auto& q:p)c=Add(c,Mul(q,1.f/float(p.size())));return c;}
// Out of the cabin, away from the eye (Newell's normal).
P Outward(const std::vector<P>& p) {
    P n{};for(std::size_t k=0;k<p.size();++k){const auto& a=p[k];const auto& b=p[(k+1)%p.size()];
        n=Add(n,P{(a[1]-b[1])*(a[2]+b[2]),(a[2]-b[2])*(a[0]+b[0]),(a[0]-b[0])*(a[1]+b[1])});}
    n=Unit(n);
    if(Dot(n,Sub(Centre(p),kCockpitSeatedEye))<0)n=Mul(n,-1);
    return n;
}
// The outline pushed out in its own plane by `by` (in with a negative one), mitred.
std::vector<P> Grown(const std::vector<P>& p,float by) {
    const auto n=Outward(p),c=Centre(p);std::vector<P> out;
    const std::size_t count=p.size();
    for(std::size_t i=0;i<count;++i) {
        const auto& a=p[(i+count-1)%count];const auto& b=p[i];const auto& d=p[(i+1)%count];
        auto away=[&](P from,P to){auto o=Unit(Cross(Sub(to,from),n));if(Dot(o,Sub(c,Mul(Add(from,to),.5f)))>0)o=Mul(o,-1);return o;};
        const auto o0=away(a,b),o1=away(b,d);
        const float k=std::max(.35f,1+Dot(o0,o1));
        out.push_back(Add(b,Mul(Add(o0,o1),by/k)));
    }
    return out;
}
// The window grown by `by`, as prisms along its normal: the regions
// CombatCutWindows cuts away, one per triangle of a fan from the centre (so a
// window need not be convex). Each reaches from 5 cm under its own lowest
// corner (just past the shell there: a curved window's corners stand well off
// its mean plane) to 12 cm over its highest; no deeper, or at a corner it
// bites the next face's wall.
void Prisms(const std::vector<P>& window,float by,std::vector<CombatRegion>& out,bool shallowEdges=false) {
    const auto g=Grown(window,by);const auto n=Outward(window),c=Centre(g);
    for(std::size_t i=0;i<g.size();++i) {
        const float low=std::min({Dot(n,c),Dot(n,g[i]),Dot(n,g[(i+1)%g.size()])}),
            high=std::max({Dot(n,c),Dot(n,g[i]),Dot(n,g[(i+1)%g.size()])});
        const P tri[3]={c,g[i],g[(i+1)%g.size()]};
        CombatRegion r{};
        for(int k=0;k<3;++k) {
            const auto& a=tri[k];const auto& b=tri[(k+1)%3];
            auto m=Unit(Cross(Sub(b,a),n));const P inside=Mul(Add(Add(tri[0],tri[1]),tri[2]),1.f/3);
            if(Dot(m,Sub(inside,a))>0)m=Mul(m,-1);
            for(int j=0;j<3;++j)r.n[r.count][j]=m[j];r.d[r.count++]=Dot(m,a);
        }
        for(int j=0;j<3;++j)r.n[r.count][j]=n[j];
        r.d[r.count++]=high+.12f;
        if(shallowEdges) {
            // Its floor through the edge 3 cm under the glass and the fan's
            // centre 20 cm under it.
            const P a=Sub(g[i],Mul(n,.03f)),b=Sub(g[(i+1)%g.size()],Mul(n,.03f)),d=Sub(c,Mul(n,.20f));
            auto m=Cross(Sub(b,a),Sub(d,a));
            if(Dot(m,m)>1e-12f) {
                m=Unit(m);if(Dot(m,Sub(Add(c,Mul(n,.05f)),a))>0)m=Mul(m,-1);
                for(int j=0;j<3;++j)r.n[r.count][j]=m[j];r.d[r.count++]=Dot(m,a);
            }
        } else {
            for(int j=0;j<3;++j)r.n[r.count][j]=-n[j];
            r.d[r.count++]=-(low-.05f);
        }
        out.push_back(r);
    }
}
void Zones(const Spec& s,std::vector<CombatRegion>& out) {
    for(const auto& z:s.zones)for(float side:{-1.f,1.f}) {
        if(z.xMin<=0&&side<0)continue;
        CombatRegion r{};
        auto plane=[&](P n,float d){for(int j=0;j<3;++j)r.n[r.count][j]=n[j];r.d[r.count++]=d;};
        plane({0,0,-1},-z.z0);plane({0,0,1},z.z1);
        // Under the top: y - k z <= top0 - k z0.
        const float k=(z.top1-z.top0)/(z.z1-z.z0),l=std::sqrt(1+k*k);
        plane({0,1/l,-k/l},(z.top0-k*z.z0)/l);
        if(z.xMin>0)plane({-side,0,0},-z.xMin-side*s.centreX);   // side*(x - centre) >= xMin
        out.push_back(r);
    }
}
bool InZone(const Spec& s,P p) {
    for(const auto& z:s.zones)if(p[2]>z.z0&&p[2]<z.z1&&p[1]<ZoneTop(z,p[2])&&std::fabs(p[0]-s.centreX)>=z.xMin)return true;
    return false;
}
// The shell's section at z: row j (0 = deck .. rows = top) at height y[j] and
// half width x[j]. Above the sill it follows the skin; below, it goes straight
// down at the sill's width unless the skin comes in closer. Where the deck
// hangs below a shallow fuselage (no skin at that height) the wall goes on
// down at the sill's width: the pilot's footwell, in the monitor's view.
struct Section {float z;std::vector<float> x,y;};
Section Cut(const HeliCockpit& c,const Spec& s,float z,unsigned rows) {
    Section out{z,{},{}};
    const float top=Top(c,z)-s.margin;
    auto skin=[&](float y){const float w=s.domeTop?DomeWidth(c,z,y):Width(c,z,y);return w<0?-1.f:std::max(0.f,w-s.margin);};
    const float atSill=std::max(0.f,skin(std::min(s.sill,top)));
    for(unsigned j=0;j<=rows;++j) {
        const float y=s.floorY+(top-s.floorY)*float(j)/float(rows);
        const float w=skin(y);
        out.y.push_back(y);out.x.push_back(y<s.sill?(w<0?atSill:std::min(w,atSill)):std::max(0.f,w));
    }
    return out;
}
// Where the shell's wall stands at (z,y) on the pilot's right.
float WallX(const HeliCockpit& c,const Spec& s,float z,float y) {
    const auto sec=Cut(c,s,z,40);
    for(unsigned j=0;j+1<sec.y.size();++j)if(y<=sec.y[j+1]) {
        const float t=std::clamp((y-sec.y[j])/std::max(1e-5f,sec.y[j+1]-sec.y[j]),0.f,1.f);
        return sec.x[j]+(sec.x[j+1]-sec.x[j])*t;
    }
    return sec.x.back();
}
void Emit(std::vector<CockpitVertex>& v,const std::vector<P>& poly,const std::vector<CombatRegion>& holes,unsigned material) {
    CombatPolygon in;for(const auto& p:poly)in.push_back(p);
    std::vector<CombatPolygon> pieces;CombatCutWindows(in,holes,0,pieces);
    for(const auto& q:pieces)for(std::size_t k=1;k+1<q.size();++k) {
        const P a=q[0],b=q[k],d=q[k+1];
        const auto n=Cross(Sub(b,a),Sub(d,a));if(Dot(n,n)<1e-14f)continue;
        Triangle(v,a,b,d,material);
    }
}
// The shell: its walls, roof, deck, and the plates closing its ends.
void Shell(std::vector<CockpitVertex>& v,const HeliCockpit& c,const Spec& s,const std::vector<CombatRegion>& holes) {
    constexpr unsigned rows=40;
    std::vector<Section> st;
    for(float z=s.rear;z<=s.front+1e-4f;z+=.05f)st.push_back(Cut(c,s,z,rows));
    const float cx=s.centreX;
    // About the centre line; on it (cx 0) x itself, so a -0 stays -0 and the
    // centred machines' vertices (and their baked occlusion) are unchanged.
    auto at=[&](float x,float y,float z){return P{cx==0?x:cx+x,y,z};};
    for(std::size_t i=0;i+1<st.size();++i) {
        const auto& a=st[i];const auto& b=st[i+1];
        for(unsigned j=0;j<rows;++j) {
            Emit(v,{at(a.x[j],a.y[j],a.z),at(b.x[j],b.y[j],b.z),at(b.x[j+1],b.y[j+1],b.z),at(a.x[j+1],a.y[j+1],a.z)},holes,Hull);
            Emit(v,{at(-a.x[j+1],a.y[j+1],a.z),at(-b.x[j+1],b.y[j+1],b.z),at(-b.x[j],b.y[j],b.z),at(-a.x[j],a.y[j],a.z)},holes,Hull);
        }
        // The roof across the top row, the deck across the bottom one.
        Emit(v,{at(-a.x[rows],a.y[rows],a.z),at(a.x[rows],a.y[rows],a.z),at(b.x[rows],b.y[rows],b.z),at(-b.x[rows],b.y[rows],b.z)},holes,Hull);
        Emit(v,{at(-a.x[0],a.y[0],a.z),at(-b.x[0],b.y[0],b.z),at(b.x[0],b.y[0],b.z),at(a.x[0],a.y[0],a.z)},holes,Steel);
    }
    // End plates: the rear bulkhead (facing forward) and the nose (facing aft).
    for(const bool rearEnd:{true,false}) {
        const auto& e=rearEnd?st.front():st.back();
        std::vector<P> ring;
        for(unsigned j=0;j<=rows;++j)ring.push_back(at(e.x[j],e.y[j],e.z));
        for(int j=int(rows);j>=0;--j)ring.push_back(at(-e.x[j],e.y[j],e.z));
        const P mid=at(0,(e.y.front()+e.y.back())*.5f,e.z);
        for(std::size_t k=0;k<ring.size();++k) {
            const auto& a=ring[k];const auto& b=ring[(k+1)%ring.size()];
            if(rearEnd)Emit(v,{mid,b,a},holes,Hull);else Emit(v,{mid,a,b},holes,Hull);
        }
    }
}
// A window's frame: a ring from 1 cm over the glass to `frame` outside it,
// from 4 mm in to `framePast` past the shell (it passes through the shell's
// cut edge, so no sightline runs between the two at any angle), with a
// row of rivets round it.
// With `others` (the machine's other windows): a side this window shares with
// another (both ends within 1.2 cm of one of its sides) is framed only to the
// shared edge, the other window framing its own half: one mullion, not two
// frames lying one on the other (flicker).
// `step` sets the whole frame (and the window's glass, Pane) that much further
// in: nearly coplanar windows' frames overlapping at a corner then do not lie
// in one plane (flicker). A car's window (`riveted` false) is a plain rubber
// surround.
void Frame(std::vector<CockpitVertex>& v,const std::vector<P>& window,const Spec& s,const std::vector<std::vector<P>>* others=nullptr,float step=0,bool riveted=true) {
    const auto n=Outward(window);
    const auto in=Grown(window,-.010f);auto out=Grown(window,s.frame);
    const auto skin=Mul(n,-.004f-step),depth=Mul(n,-(s.margin+s.framePast));
    const std::size_t count=window.size();
    auto near=[](P a,P b){return Dot(Sub(a,b),Sub(a,b))<.012f*.012f;};
    std::vector<bool> shared(count,false);
    if(others)for(std::size_t i=0;i<count;++i) {
        const auto& a=window[i];const auto& b=window[(i+1)%count];
        for(const auto& o:*others) {
            if(&o==&window||o.size()<3)continue;
            for(std::size_t k=0;k<o.size()&&!shared[i];++k) {
                const auto& p=o[k];const auto& q=o[(k+1)%o.size()];
                shared[i]=(near(a,p)&&near(b,q))||(near(a,q)&&near(b,p));
            }
        }
    }
    for(std::size_t i=0;i<count;++i) {
        const std::size_t j=(i+1)%count;
        const P o0=shared[i]?window[i]:out[i],o1=shared[i]?window[j]:out[j];
        RoundedPrism(v,{Add(o0,skin),Add(o1,skin),Add(in[j],skin),Add(in[i],skin)},depth,.003f,Dark);
        // Where a shared side meets a framed one, the corner between the
        // two outer edges is filled.
        if(shared[i]!=shared[(i+count-1)%count]) {
            const P t0=Add(window[i],skin),t1=Add(out[i],skin),t2=Add(in[i],skin);
            const auto f=Cross(Sub(t1,t0),Sub(t2,t0));
            if(Dot(f,f)>1e-10f) {   // a plain prism, each face turned out
                const P b0=Add(t0,depth),b1=Add(t1,depth),b2=Add(t2,depth),mid=Mul(Add(Add(t0,t1),t2),1.f/3);
                auto tri=[&](P a,P b2_,P c,P toward){if(Dot(Cross(Sub(b2_,a),Sub(c,a)),toward)<0)Triangle(v,a,c,b2_,Dark);else Triangle(v,a,b2_,c,Dark);};
                tri(t0,t1,t2,n);tri(b0,b1,b2,Mul(n,-1));
                const P ring[3]={t0,t1,t2},low[3]={b0,b1,b2};
                for(int k=0;k<3;++k) {
                    const int l=(k+1)%3;const auto away=Sub(Mul(Add(ring[k],ring[l]),.5f),mid);
                    tri(ring[k],ring[l],low[l],away);tri(ring[k],low[l],low[k],away);
                }
            }
        }
        if(shared[i]||!riveted)continue;   // no rivets on a half mullion, nor on a car
        const auto a=Add(Mul(Add(in[i],o0),.5f),Add(skin,depth)),b=Add(Mul(Add(in[j],o1),.5f),Add(skin,depth));
        // Only on a long side: on a short one at a sharp corner the mitred
        // ring leaves its face away from the midline.
        const float length=std::sqrt(Dot(Sub(b,a),Sub(b,a)));if(length<.16f)continue;
        const unsigned rivets=unsigned((length-.08f)/.09f)+1;
        for(unsigned k=0;k<rivets;++k) {
            const float t=(.04f+(length-.08f)*(rivets>1?float(k)/float(rivets-1):.5f))/length;
            Rivet(v,Add(a,Mul(Sub(b,a),t)),Mul(n,-1));
        }
    }
}
// The window's glass, 8 mm in from the skin: behind the frame's face (4 mm),
// so its edge is buried in the frame and no grazing sightline slips between
// the two. Faint (surface 24), seen through.
void Pane(std::vector<CockpitVertex>& v,const std::vector<P>& window,float step=0) {
    const auto n=Outward(window),c=Add(Centre(window),Mul(n,-.008f-step));
    const auto start=v.size();
    for(std::size_t k=0;k<window.size();++k) {
        const auto a=Add(window[k],Mul(n,-.008f-step)),b=Add(window[(k+1)%window.size()],Mul(n,-.008f-step));
        if(Dot(Cross(Sub(a,c),Sub(b,c)),n)>0)Triangle(v,c,b,a,Glass);else Triangle(v,c,a,b,Glass);
    }
    for(auto i=start;i<v.size();++i){v[i].surface=24;v[i].colour[0]=.020f;v[i].colour[1]=.070f;v[i].colour[2]=.060f;}
}
// A joint between monitor tiles: the Nix's ink (surface 21), a centimetre
// toward the eye, as wide to the eye wherever it stands.
void Ink(std::vector<CockpitVertex>& v,P a,P b) {
    const P eye=kCockpitSeatedEye;
    auto near=[&](P p){return Add(p,Mul(Unit(Sub(eye,p)),.010f));};
    a=near(a);b=near(b);
    const float length=std::sqrt(Dot(Sub(b,a),Sub(b,a)));if(length<1e-4f)return;
    const unsigned pieces=std::max(1u,unsigned(std::ceil(length/.10f)));
    auto half=[&](P x){const auto view=Sub(x,eye);return .0028f*std::max(.7f,std::sqrt(Dot(view,view)));};
    const auto start=v.size();
    for(unsigned k=0;k<pieces;++k) {
        const P p=Add(a,Mul(Sub(b,a),float(k)/float(pieces))),q=Add(a,Mul(Sub(b,a),float(k+1)/float(pieces)));
        const auto w=Unit(Cross(Sub(b,a),Sub(Mul(Add(p,q),.5f),eye)));
        Quad(v,Sub(p,Mul(w,half(p))),Add(p,Mul(w,half(p))),Add(q,Mul(w,half(q))),Sub(q,Mul(w,half(q))),Dark);
    }
    for(auto i=start;i<v.size();++i){v[i].surface=21;for(auto& col:v[i].colour)col=.012f;}
}
// The monitor's edge up the wall on the pilot's right, from the rear of the
// first zone to the front of the last: the highest zone top at each z, 1 cm
// in from the shell, every `step`.
std::vector<P> ZoneEdge(const HeliCockpit& c,const Spec& s,float step=.05f) {
    std::vector<P> out;if(s.zones.empty())return out;
    float z0=s.zones.front().z0,z1=s.zones.front().z1;
    for(const auto& z:s.zones){z0=std::min(z0,z.z0);z1=std::max(z1,z.z1);}
    z1=std::min(z1,s.front);   // a zone may run on past the shell (to hide the nose ahead)
    auto top=[&](float z,bool after){float y=s.floorY;
        for(const auto& zone:s.zones)if(after?(z>=zone.z0&&z<zone.z1):(z>zone.z0&&z<=zone.z1))y=std::max(y,ZoneTop(zone,z));return y;};
    auto at=[&](float z,float y){return P{s.centreX==0?WallX(c,s,z,y)-.010f:s.centreX+WallX(c,s,z,y)-.010f,y,z};};
    out.push_back(at(z0,s.floorY));
    for(float z=z0;z<z1-1e-4f;) {
        const float next=std::min(z1,z+step);
        const float a=top(z,true),before=top(z,false);
        if(std::fabs(a-before)>1e-3f||z==z0)out.push_back(at(z,a));
        out.push_back(at(next,top(next,false)));
        z=next;
    }
    return out;
}
// A tube along a polyline, one piece: a ring of 12 at every point, square to
// the mean of its two segments, smooth-shaded, capped at both ends.
void Tube(std::vector<CockpitVertex>& v,const std::vector<P>& points,float radius,unsigned material) {
    std::vector<P> pts;
    for(const auto& p:points)if(pts.empty()||Dot(Sub(p,pts.back()),Sub(p,pts.back()))>1e-6f)pts.push_back(p);
    if(pts.size()<2)return;
    constexpr int around=12;
    std::vector<std::array<P,around>> rings,normals;
    P side{};
    for(std::size_t k=0;k<pts.size();++k) {
        const auto t=Unit(Sub(pts[std::min(k+1,pts.size()-1)],pts[k>0?k-1:0]));
        // Carry the ring's orientation along (no twist).
        if(k==0)side=Unit(Cross(t,std::fabs(t[1])<.95f?P{0,1,0}:P{1,0,0}));
        else side=Unit(Sub(side,Mul(t,Dot(side,t))));
        const auto up=Cross(t,side);
        std::array<P,around> ring{},normal{};
        for(int i=0;i<around;++i) {
            const float a=float(i)*6.2831853f/around;
            normal[i]=Add(Mul(side,std::cos(a)),Mul(up,std::sin(a)));ring[i]=Add(pts[k],Mul(normal[i],radius));
        }
        rings.push_back(ring);normals.push_back(normal);
    }
    // Wound outward (the vertex normals must agree with the faces').
    const bool outward=Dot(Cross(Sub(rings[1][0],rings[0][0]),Sub(rings[0][1],rings[0][0])),normals[0][0])>0;
    for(std::size_t k=0;k+1<rings.size();++k)for(int i=0;i<around;++i) {
        const int j=(i+1)%around;const auto start=v.size();
        if(outward) {
            Quad(v,rings[k][i],rings[k+1][i],rings[k+1][j],rings[k][j],material);
            const P n[6]={normals[k][i],normals[k+1][i],normals[k+1][j],normals[k][i],normals[k+1][j],normals[k][j]};
            for(int m=0;m<6;++m)for(int q=0;q<3;++q)v[start+m].normal[q]=n[m][q];
        } else {
            Quad(v,rings[k][i],rings[k][j],rings[k+1][j],rings[k+1][i],material);
            const P n[6]={normals[k][i],normals[k][j],normals[k+1][j],normals[k][i],normals[k+1][j],normals[k+1][i]};
            for(int m=0;m<6;++m)for(int q=0;q<3;++q)v[start+m].normal[q]=n[m][q];
        }
    }
    for(const bool first:{true,false}) {
        const auto& ring=first?rings.front():rings.back();const auto& c=first?pts.front():pts.back();
        const auto away=first?Sub(pts[0],pts[1]):Sub(pts.back(),pts[pts.size()-2]);
        for(int i=0;i<around;++i){const int j=(i+1)%around;
            if(Dot(Cross(Sub(ring[i],c),Sub(ring[j],c)),away)>0)Triangle(v,c,ring[i],ring[j],material);else Triangle(v,c,ring[j],ring[i],material);}
    }
}
// A rail along a polyline on the shell: a smooth tube along each run, broken
// at a corner (more than 25 degrees), where a block joins the two.
void Rail(std::vector<CockpitVertex>& v,const std::vector<P>& points,float radius,unsigned material) {
    std::vector<P> run;
    for(std::size_t k=0;k<points.size();++k) {
        run.push_back(points[k]);
        if(k==0||k+1==points.size())continue;
        const auto a=Sub(points[k],points[k-1]),b=Sub(points[k+1],points[k]);
        if(Dot(a,a)<1e-8f||Dot(b,b)<1e-8f||Dot(Unit(a),Unit(b))>.906f)continue;
        Tube(v,run,radius,material);run={points[k]};
        const P half{radius*1.5f,radius*1.5f,radius*1.5f};
        Box(v,Sub(points[k],half),Add(points[k],half),material);
    }
    Tube(v,run,radius,material);
}
// Is a point inside one of the regions cut out of the shell (n.p <= d on all its planes)?
bool InHole(const std::vector<CombatRegion>& holes,P p,float by=0) {
    for(const auto& r:holes) {
        bool in=true;for(unsigned i=0;i<r.count&&in;++i)in=r.n[i][0]*p[0]+r.n[i][1]*p[1]+r.n[i][2]*p[2]<=r.d[i]+by;
        if(in)return true;
    }
    return false;
}
// Drop the triangles from `first` on that have no area left (a large part
// scaled down: its innermost rings collapse).
void DropSlivers(std::vector<CockpitVertex>& v,std::size_t first) {
    std::size_t out=first;
    for(std::size_t i=first;i+2<v.size();i+=3) {
        const P a{v[i].position[0],v[i].position[1],v[i].position[2]},b{v[i+1].position[0],v[i+1].position[1],v[i+1].position[2]},
            c{v[i+2].position[0],v[i+2].position[1],v[i+2].position[2]};
        const auto n=Cross(Sub(b,a),Sub(c,a));if(Dot(n,n)<1e-14f)continue;
        if(out!=i)for(int k=0;k<3;++k)v[out+k]=v[i+k];
        out+=3;
    }
    v.resize(out);
}
// A quad turned so its face looks along `toward`.
void Facing(std::vector<CockpitVertex>& v,P a,P b,P c,P d,P toward,unsigned material) {
    if(Dot(Cross(Sub(b,a),Sub(c,a)),toward)<0)Quad(v,a,d,c,b,material);else Quad(v,a,b,c,d,material);
}
// An airframe rib round the shell's section at z, above `from`: a flat strip
// standing `height` in from the shell, `width` along the cabin, broken where
// a window or a monitor is cut (`clear` past the cut: to a frame's outer edge).
void Rib(std::vector<CockpitVertex>& v,const HeliCockpit& c,const Spec& s,const std::vector<CombatRegion>& holes,float z,float from,float width,float height,unsigned material,float clear=.004f) {
    const auto sec=Cut(c,s,z,40);
    const float top=sec.y.back(),half=sec.x.back();
    std::vector<P> line;
    const float cx=s.centreX;
    auto across=[&](float x){return cx==0?x:cx+x;};   // as Shell: -0 stays -0 on the centre line
    for(unsigned j=0;j<=40;++j)if(sec.y[j]>=from)line.push_back({across(-sec.x[j]),sec.y[j],z});   // up the left wall
    for(int k=1;k<8;++k)line.push_back({across(-half+2*half*float(k)/8),top,z});                     // across the roof
    for(int j=40;j>=0;--j)if(sec.y[unsigned(j)]>=from)line.push_back({across(sec.x[unsigned(j)]),sec.y[unsigned(j)],z});   // down the right
    // Each point's way into the cabin: square to the section, toward its middle.
    const P middle{cx,(from+top)*.5f,z};
    std::vector<std::vector<P>> runs(1),ins(1);
    for(std::size_t k=0;k<line.size();++k) {
        const auto t=Sub(line[std::min(k+1,line.size()-1)],line[k>0?k-1:0]);
        P in=Unit(P{-t[1],t[0],0});if(Dot(in,Sub(middle,line[k]))<0)in=Mul(in,-1);
        if(InHole(holes,line[k],clear)||InHole(holes,Add(line[k],Mul(in,height)),clear)) {
            if(!runs.back().empty()){runs.emplace_back();ins.emplace_back();}
            continue;
        }
        runs.back().push_back(line[k]);ins.back().push_back(in);
    }
    const P along{0,0,width*.5f};
    for(std::size_t r=0;r<runs.size();++r) {
        const auto& p=runs[r];const auto& n=ins[r];if(p.size()<3)continue;
        auto at=[&](std::size_t k,float a,float b){return Add(Add(p[k],Mul(n[k],b-.004f)),Mul(along,a));};
        for(std::size_t k=0;k+1<p.size();++k) {
            const auto inward=Mul(Add(n[k],n[k+1]),.5f);
            Facing(v,at(k,-1,height),at(k+1,-1,height),at(k+1,1,height),at(k,1,height),inward,material);   // face
            Facing(v,at(k,1,0),at(k,1,height),at(k+1,1,height),at(k+1,1,0),{0,0,1},material);             // front side
            Facing(v,at(k,-1,0),at(k+1,-1,0),at(k+1,-1,height),at(k,-1,height),{0,0,-1},material);        // rear side
        }
        const auto last=p.size()-1;
        Facing(v,at(0,-1,0),at(0,-1,height),at(0,1,height),at(0,1,0),Sub(p[0],p[1]),material);
        Facing(v,at(last,-1,0),at(last,1,0),at(last,1,height),at(last,-1,height),Sub(p[last],p[last-1]),material);
    }
}
// A lightened plate: `columns` x `rows` square cells, each with a round hole,
// `thick` deep behind the face (panel frame: right, up; the face toward -normal).
void LightenedPlate(std::vector<CockpitVertex>& v,const Panel& p,unsigned columns,unsigned rows,float cell,float hole,float thick,unsigned material) {
    const auto n=Unit(Cross(p.right,p.up)),back=Mul(n,thick);
    constexpr int around=16;
    for(unsigned i=0;i<columns;++i)for(unsigned j=0;j<rows;++j) {
        const float cx=(float(i)-(float(columns)-1)*.5f)*cell,cy=(float(j)-(float(rows)-1)*.5f)*cell;
        for(int k=0;k<around;++k) {
            auto ring=[&](int m,float r){const float a=float(m)*6.2831853f/around;return Point(p,cx+r*std::cos(a),cy+r*std::sin(a));};
            auto edge=[&](int m){const float a=float(m)*6.2831853f/around;const float ca=std::cos(a),sa=std::sin(a);
                const float t=.5f*cell/std::max(std::fabs(ca),std::fabs(sa));return Point(p,cx+t*ca,cy+t*sa);};
            const P h0=ring(k,hole),h1=ring(k+1,hole),e0=edge(k),e1=edge(k+1);
            Quad(v,e0,e1,h1,h0,material);                                    // face
            Quad(v,Add(h0,back),Add(h1,back),Add(e1,back),Add(e0,back),material);   // back
            Quad(v,h0,h1,Add(h1,back),Add(h0,back),material);                // the hole's wall
        }
    }
    // The outer edge.
    const float hw=.5f*cell*float(columns),hh=.5f*cell*float(rows);
    const P c[4]={Point(p,-hw,-hh),Point(p,hw,-hh),Point(p,hw,hh),Point(p,-hw,hh)};
    for(int k=0;k<4;++k)Quad(v,c[(k+1)%4],c[k],Add(c[k],back),Add(c[(k+1)%4],back),material);
}
}
