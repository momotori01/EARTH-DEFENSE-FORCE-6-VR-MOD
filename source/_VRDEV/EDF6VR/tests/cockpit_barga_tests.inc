void BargaLayoutTest(bool baked=true) {
    const auto kind=CockpitKind::Barga;auto mesh=BuildBargaCockpit();
    CHECK(mesh.size()>10000&&mesh.size()%3==0);if(baked)CHECK(ApplyCockpitOcclusion(mesh,kind));
    unsigned invalid=0,outside=0,seams=0,degenerate=0;float maximum=0;Point farthest{};
    for(const auto& v:mesh) {
        const Point p{v.position[0],v.position[1],v.position[2]},n{v.normal[0],v.normal[1],v.normal[2]};
        const auto d=Sub(p,kBargaSphereCentre);const float radius=std::sqrt(Dot(d,d));
        if(radius>maximum){maximum=radius;farthest=p;}
        if(!std::isfinite(radius)||std::fabs(Dot(n,n)-1)>.002f)++invalid;
        if(radius>kBargaSphereRadius+.015f)++outside;
        if(v.surface==21){++seams;CHECK(std::fabs(radius-kBargaSphereRadius)<.0001f);}
    }
    for(std::size_t i=0;i<mesh.size();i+=3) {
        const Point a{mesh[i].position[0],mesh[i].position[1],mesh[i].position[2]},
            b{mesh[i+1].position[0],mesh[i+1].position[1],mesh[i+1].position[2]},c{mesh[i+2].position[0],mesh[i+2].position[1],mesh[i+2].position[2]};
        const auto n=Cross(Sub(b,a),Sub(c,a));if(Dot(n,n)<1e-20f)++degenerate;
    }
    std::printf("Barga: %zu triangles, max radius %.4f at %.4f %.4f %.4f, outside=%u invalid=%u degenerate=%u\n",mesh.size()/3,maximum,farthest[0],farthest[1],farthest[2],outside,invalid,degenerate);
    CHECK(invalid==0&&outside==0&&degenerate==0&&seams==11*160*6);
    mesh.resize(mesh.size()-seams); // seams do not block the panoramic image
    const auto eye=kCockpitSeatedEye;
    CHECK(!MeshBlocked(mesh,eye,{0,.10f,3}));CHECK(!MeshBlocked(mesh,eye,{0,3,0}));
    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
        auto wanted=eye;wanted[axis]+=sign*2;
        const auto stopped=ClampCockpitHead(wanted,.10f,kind),delta=Sub(stopped,eye),relative=Sub(stopped,kBargaSphereCentre);
        std::printf("barga head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",axis,sign,stopped[0],stopped[1],stopped[2]);
        CHECK(sign*delta[axis]>.05f);CHECK(Dot(relative,relative)<=1.049f*1.049f);
        CHECK(!MeshBlocked(mesh,eye,stopped));
    }
    for(float x:{-.7f,0.f,.7f})for(float y:{-.6f,.3f,.8f})for(float z:{-.7f,.3f,.9f}) {
        const auto p=ClampCockpitHead({x,y,z},.10f,kind),r=Sub(p,kBargaSphereCentre);
        CHECK(Dot(r,r)<1.049f*1.049f);CHECK(!MeshBlocked(mesh,eye,p));
    }
    const auto panels=BuildCockpitDisplays(kind);CHECK(panels.size()==18);
    for(unsigned i=0;i<3;++i) {
        const auto* p=panels.data()+i*6;Point centre{};
        for(unsigned k=0;k<3;++k)centre[k]=(p[0].position[k]+p[2].position[k])*.5f;
        CHECK(p->surface==16+i);CHECK(centre[0]*(i==1?1:-1)>0);CHECK(centre[1]*(i==2?1:-1)>0);
        CHECK(Dot(Sub(centre,eye),Sub(centre,eye))>.70f*.70f);
        const Point n{p->normal[0],p->normal[1],p->normal[2]};CHECK(std::fabs(Dot(n,Sub(eye,centre)))>.7f);
        CHECK(!MeshBlocked(mesh,eye,centre));
    }
    for(unsigned variant=0;variant<2;++variant)for(unsigned part=0;part<5;++part) {
        const auto cap=BuildBargaJointCap(variant,part);CHECK(cap.size()>=12&&cap.size()%3==0);
        for(const auto& v:cap){CHECK(v.surface==22||v.surface==23);CHECK(v.colour[0]>0&&v.colour[0]==v.colour[1]&&v.colour[1]==v.colour[2]);}
        if(part<3){CHECK(cap.size()>10000);unsigned smooth=0;
            for(std::size_t i=0;i<cap.size();i+=3){Point p[3],n[3];for(int j=0;j<3;++j){std::memcpy(p[j].data(),cap[i+j].position,12);std::memcpy(n[j].data(),cap[i+j].normal,12);}
                const auto area=Cross(Sub(p[1],p[0]),Sub(p[2],p[0]));CHECK(std::isfinite(Dot(area,area))&&Dot(area,area)>1e-18f);
                for(const auto& normal:n)CHECK(std::fabs(Dot(normal,normal)-1)<.002f);
                if(Dot(Sub(n[0],n[1]),Sub(n[0],n[1]))>1e-7f)++smooth;
            }CHECK(smooth>1000);
        }
    }
    CHECK(BuildBargaJointCap(2,0).empty()&&BuildBargaJointCap(0,5).empty());
}
