// Rounded closed solids. Offset the supporting planes inward, then sweep a
// circular fillet over each edge and a spherical patch over each corner.
// The original envelope and mounting planes stay fixed; normals are analytic,
// not a blanket smooth-shading pass across large flat armour faces.
float Dot(P a,P b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
void CurvedTriangle(std::vector<CockpitVertex>& v,P a,P b,P c,P na,P nb,P nc,unsigned material) {
    if(Dot(Cross(Sub(b,a),Sub(c,a)),Add(Add(na,nb),nc))<0) {std::swap(b,c);std::swap(nb,nc);}
    const auto first=v.size();Triangle(v,a,b,c,material);
    const P normals[]={na,nb,nc};
    for(int i=0;i<3;++i)for(int k=0;k<3;++k)v[first+i].normal[k]=normals[i][k];
}
void CurvedQuad(std::vector<CockpitVertex>& v,P a,P b,P c,P d,P na,P nb,P nc,P nd,unsigned material) {
    CurvedTriangle(v,a,b,c,na,nb,nc,material);CurvedTriangle(v,a,c,d,na,nc,nd,material);
}
void RoundedSolid(std::vector<CockpitVertex>& v,const std::vector<P>& points,
    const std::vector<std::vector<unsigned>>& faces,float radius,unsigned material,unsigned sideMaterial=~0u) {
    struct Edge {unsigned a,b,front,back;};
    std::vector<P> normals;std::vector<Edge> edges;
    std::vector<std::vector<unsigned>> adjacent(points.size());
    P centre{};for(const auto& p:points)centre=Add(centre,p);centre=Mul(centre,1.f/static_cast<float>(points.size()));
    for(unsigned f=0;f<faces.size();++f) {
        const auto& face=faces[f];P n{},mid{};
        for(unsigned i=0;i<face.size();++i) {
            const auto a=face[i],b=face[(i+1)%face.size()];
            n=Add(n,Cross(Sub(points[a],centre),Sub(points[b],centre)));mid=Add(mid,points[a]);
            adjacent[a].push_back(f);
            const auto lo=std::min(a,b),hi=std::max(a,b);
            const auto found=std::find_if(edges.begin(),edges.end(),[&](const Edge& e){return e.a==lo&&e.b==hi;});
            if(found==edges.end())edges.push_back({lo,hi,f,~0u});else found->back=f;
        }
        n=Unit(n);mid=Mul(mid,1.f/static_cast<float>(face.size()));
        if(Dot(n,Sub(mid,centre))<0)n=Mul(n,-1);
        normals.push_back(n);
    }
    // These are simple prisms: precisely three planes meet at every vertex.
    // Limit the fillet before offset corners could consume a short edge.
    std::vector<P> offset(points.size());
    for(unsigned i=0;i<points.size();++i) {
        const auto& fs=adjacent[i];const auto a=normals[fs[0]],b=normals[fs[1]],c=normals[fs[2]];
        const float det=Dot(a,Cross(b,c));
        offset[i]=Mul(Add(Add(Cross(b,c),Cross(c,a)),Cross(a,b)),1.f/det);
    }
    for(const auto& edge:edges) {
        const auto length=Sub(points[edge.b],points[edge.a]);const float distance=std::sqrt(Dot(length,length));
        const float consumed=Dot(Sub(offset[edge.b],offset[edge.a]),Mul(length,1.f/distance));
        if(consumed>0)radius=std::min(radius,distance*.72f/consumed);
    }
    std::vector<P> core(points.size());
    for(unsigned i=0;i<points.size();++i)core[i]=Sub(points[i],Mul(offset[i],radius));
    const int steps=radius<.005f?2:3;
    for(unsigned f=0;f<faces.size();++f) {
        const auto& face=faces[f];const auto n=normals[f];const auto move=Mul(n,radius);
        const auto mat=sideMaterial!=~0u&&f>=2?sideMaterial:material;
        // Convex fan without an extra central vertex, including small lenses.
        for(unsigned i=1;i+1<face.size();++i)CurvedTriangle(v,Add(core[face[0]],move),
            Add(core[face[i]],move),Add(core[face[i+1]],move),n,n,n,mat);
    }
    for(const auto& edge:edges) {
        const auto n0=normals[edge.front],n1=normals[edge.back];
        const auto mat=sideMaterial!=~0u?sideMaterial:material;
        for(int j=0;j<steps;++j) {
            const float t0=float(j)/steps,t1=float(j+1)/steps;
            const auto a=Unit(Add(Mul(n0,1-t0),Mul(n1,t0))),b=Unit(Add(Mul(n0,1-t1),Mul(n1,t1)));
            CurvedQuad(v,Add(core[edge.a],Mul(a,radius)),Add(core[edge.b],Mul(a,radius)),
                Add(core[edge.b],Mul(b,radius)),Add(core[edge.a],Mul(b,radius)),a,a,b,b,mat);
        }
    }
    for(unsigned k=0;k<core.size();++k) {
        const auto& fs=adjacent[k];
        auto normal=[&](int i,int j){return Unit(Add(Add(Mul(normals[fs[0]],float(steps-i-j)),
            Mul(normals[fs[1]],float(i))),Mul(normals[fs[2]],float(j))));};
        auto triangle=[&](P a,P b,P c){CurvedTriangle(v,Add(core[k],Mul(a,radius)),
            Add(core[k],Mul(b,radius)),Add(core[k],Mul(c,radius)),a,b,c,material);};
        for(int i=0;i<steps;++i)for(int j=0;j<steps-i;++j) {
            triangle(normal(i,j),normal(i+1,j),normal(i,j+1));
            if(i+j+1<steps)triangle(normal(i+1,j),normal(i+1,j+1),normal(i,j+1));
        }
    }
}
void RoundedPrism(std::vector<CockpitVertex>& v,const std::vector<P>& front,P extrusion,
    float radius,unsigned material,unsigned sideMaterial=~0u) {
    const auto count=static_cast<unsigned>(front.size());std::vector<P> points=front;
    for(const auto& p:front)points.push_back(Add(p,extrusion));
    std::vector<std::vector<unsigned>> faces(2);
    for(unsigned i=0;i<count;++i){faces[0].push_back(i);faces[1].push_back(count+i);}
    for(unsigned i=0;i<count;++i){const auto j=(i+1)%count;faces.push_back({i,j,count+j,count+i});}
    RoundedSolid(v,points,faces,radius,material,sideMaterial);
}

// Reference objects already have cut/chamfered edges. Smooth only connected
// shallow creases within each object, preserving vents and perpendicular faces.
// Corner-angle weights avoid seams caused by differently triangulated faces.
void SmoothReference(std::vector<CockpitVertex>& v,std::size_t start) {
    struct Corner {std::size_t vertex;P normal;float weight;};
    std::map<std::array<int,3>,std::vector<Corner>> groups;
    for(std::size_t i=start;i<v.size();i+=3)for(int j=0;j<3;++j) {
        const auto& vert=v[i+j];const P p{vert.position[0],vert.position[1],vert.position[2]};
        auto delta=[&](int k){const auto& q=v[i+k];return Unit(Sub(P{q.position[0],q.position[1],q.position[2]},p));};
        const auto& b=v[i+(j+1)%3];const auto& c=v[i+(j+2)%3];
        const auto areaVector=Cross(Sub(P{b.position[0],b.position[1],b.position[2]},p),Sub(P{c.position[0],c.position[1],c.position[2]},p));
        const float area=std::sqrt(Dot(areaVector,areaVector));
        const float angle=std::acos(std::clamp(Dot(delta((j+1)%3),delta((j+2)%3)),-1.f,1.f));
        const std::array<int,3> key{int(std::lround(p[0]*100000)),int(std::lround(p[1]*100000)),int(std::lround(p[2]*100000))};
        groups[key].push_back({i+j,{vert.normal[0],vert.normal[1],vert.normal[2]},angle*area});
    }
    for(const auto& entry:groups)for(const auto& corner:entry.second) {
        P n{};for(const auto& other:entry.second)if(Dot(corner.normal,other.normal)>.48f)n=Add(n,Mul(other.normal,other.weight));
        n=Unit(n);for(int k=0;k<3;++k)v[corner.vertex].normal[k]=n[k];
    }
}
