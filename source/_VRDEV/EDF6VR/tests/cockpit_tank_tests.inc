// Tank driver's cabin and the Titan's gunner pod. The plan octagon and the
// sill repeat here rather than being imported, so a slip in the generator
// cannot validate itself.
namespace tankcab {
constexpr float kDeck=-1.22f,kSill=-.20f,kBand=.36f,kRoof=.60f;
constexpr float kPlan[8][2]={{-.44f,1.30f},{-.80f,.92f},{-.80f,-.36f},{-.46f,-.72f},{.46f,-.72f},{.80f,-.36f},{.80f,.92f},{.44f,1.30f}};
// Signed distance inside the plan octagon (negative outside).
float Inside(float x,float z) {
    float best=1e9f;
    for(int i=0;i<8;++i) {
        const float ax=kPlan[i][0],az=kPlan[i][1],bx=kPlan[(i+1)%8][0],bz=kPlan[(i+1)%8][1];
        float nx=bz-az,nz=ax-bx;const float l=std::sqrt(nx*nx+nz*nz);nx/=l;nz/=l;
        if(nx*((ax+bx)*.5f)+nz*((az+bz)*.5f-.29f)<0){nx=-nx;nz=-nz;}
        best=std::min(best,(ax-x)*nx+(az-z)*nz);
    }
    return best;
}
}
void CheckMeshSanity(const std::vector<CockpitVertex>& mesh) {
    CHECK(mesh.size()>1000&&mesh.size()%3==0);
    for(const auto& v:mesh) {
        float n=0;for(int k=0;k<3;++k){CHECK(std::isfinite(v.position[k]));n+=v.normal[k]*v.normal[k];}
        CHECK(std::fabs(n-1)<.001f);
    }
    for(std::size_t i=0;i<mesh.size();i+=3) {
        Point a{},b{},c{},n{};std::memcpy(a.data(),mesh[i].position,12);std::memcpy(b.data(),mesh[i+1].position,12);
        std::memcpy(c.data(),mesh[i+2].position,12);std::memcpy(n.data(),mesh[i].normal,12);
        const auto normal=Cross(Sub(b,a),Sub(c,a));CHECK(Dot(normal,normal)>1e-18f&&Dot(normal,n)>0);
    }
}
void TankLayoutTest(bool baked=true) {
    using namespace tankcab;
    {
        auto mesh=BuildCockpit(CockpitKind::Tank);
        std::printf("Tank driver cabin: %zu triangles\n",mesh.size()/3);
        CheckMeshSanity(mesh);
        if(baked)CHECK(ApplyCockpitOcclusion(mesh,CockpitKind::Tank));
        unsigned outside=0;
        for(const auto& v:mesh) {
            const float x=v.position[0],y=v.position[1],z=v.position[2];
            // Walls stand .04 outside the plan and the seams .028 outside it;
            // the rear machinery (Nix's) stands up into the band behind the head.
            const bool ok=Inside(x,z)>-.075f&&y>kDeck-.08f&&y<kRoof+.05f;
            if(!ok&&++outside<6)std::printf("Tank vertex outside the cabin: %.3f %.3f %.3f\n",x,y,z);
            CHECK(ok);
        }
        const auto eye=kCockpitSeatedEye;
        // Six-axis head movement, free inside, stopped at the screens.
        for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
            auto desired=eye;desired[axis]+=sign*4;
            const auto stopped=ClampCockpitHead(desired,.10f,CockpitKind::Tank);
            std::printf("tank head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",axis,sign,stopped[0],stopped[1],stopped[2]);
            CHECK(sign*(stopped[axis]-eye[axis])>.035f);
            CHECK(std::fabs(stopped[axis]-desired[axis])>.05f);
            CHECK(Inside(stopped[0],stopped[2])>.09f&&stopped[1]<kRoof);
            CHECK(!MeshBlocked(mesh,eye,stopped));
        }
        // The turret is screen: open overhead and all round above the sill.
        unsigned sky=0;
        for(float x:{-.30f,0.f,.30f})for(float z:{.20f,.60f,1.00f})if(!MeshBlocked(mesh,eye,{x,4.f,z}))++sky;
        std::printf("tank sky open %u of 9\n",sky);CHECK(sky==9);
        unsigned band=0;
        for(int k=0;k<16;++k) {
            const float a=6.2831853f*k/16;
            if(!MeshBlocked(mesh,eye,{eye[0]+6*std::sin(a),eye[1]+.10f,eye[2]+6*std::cos(a)}))++band;
        }
        std::printf("tank band open %u of 16\n",band);CHECK(band>=11);
        // Below the sill it is the hull's inside, closed every way round.
        // Rays from the eye to a point 2 cm outside the wall, at heights below
        // the sill, all the way round: every one must hit the cabin.
        unsigned leaks=0;
        for(int k=0;k<48;++k)for(float y:{kSill-.04f,-.45f,-.75f,-1.05f,kDeck+.03f}) {
            const float a=6.2831853f*k/48,dx=std::sin(a),dz=std::cos(a);
            float r=0;while(Inside(eye[0]+dx*r,.29f+dz*r)>-.02f)r+=.005f;
            const Point outward{eye[0]+dx*r,y,.29f+dz*r};
            if(!MeshBlocked(mesh,eye,outward)&&++leaks<6)std::printf("tank hull leak toward %.2f %.2f %.2f\n",outward[0],outward[1],outward[2]);
            if(!MeshBlocked(mesh,{eye[0],-.60f,.29f},outward)&&++leaks<6)std::printf("tank hull leak (low) toward %.2f %.2f %.2f\n",outward[0],outward[1],outward[2]);
        }
        CHECK(leaks==0);
        CHECK(MeshBlocked(mesh,eye,{0,kDeck-.30f,.30f}));
        // The station: sticks on both wings, a seat under the pilot.
        for(float s:{-1.f,1.f})CHECK(MeshBlocked(mesh,{s*.15f,-.240f,.284f},{s*.55f,-.240f,.284f}));
        CHECK(MeshBlocked(mesh,{0,-.40f,0},{0,kDeck-.10f,0}));
        const auto displays=BuildCockpitDisplays(CockpitKind::Tank);CHECK(displays.size()==18);
        for(const auto& v:displays)CHECK(v.visibility[1]<.5f);
    }
    {
        auto mesh=BuildCockpit(CockpitKind::TitanGunner);
        std::printf("Titan gunner pod: %zu triangles\n",mesh.size()/3);
        CheckMeshSanity(mesh);
        if(baked)CHECK(ApplyCockpitOcclusion(mesh,CockpitKind::TitanGunner));
        unsigned wide=0;
        for(const auto& v:mesh) {
            // Right side brought in to .42 (walls and seams stand just outside).
            const bool ok=v.position[0]<.49f;
            if(!ok&&++wide<6)std::printf("Titan pod vertex right of the wall: %.3f %.3f %.3f\n",v.position[0],v.position[1],v.position[2]);
            CHECK(ok);
        }
        // No right armrest; the left one carries the three screens.
        CHECK(!MeshBlocked(mesh,{.25f,-.25f,.44f},{.40f,-.25f,.44f}));
        CHECK(MeshBlocked(mesh,{-.30f,-.24f,.44f},{-.60f,-.24f,.44f}));
        const auto displays=BuildCockpitDisplays(CockpitKind::TitanGunner);CHECK(displays.size()==18);
        float from[3]{},to[3]{},middle[3]{},top[3]{},bottom[3]{};
        for(unsigned i=0;i<18;i+=6) {
            Point c{},n{};for(int k=0;k<3;++k){n[k]=displays[i].normal[k];for(unsigned j=0;j<6;++j)c[k]+=displays[i+j].position[k]/6;}
            CHECK(c[0]<-.40f);
            // Faces the seat (the stacked strips share their frame's facing).
            const auto toSeat=Sub(kCockpitSeatedEye,c);
            CHECK(std::fabs(Dot(n,toSeat))/std::sqrt(Dot(toSeat,toSeat))>.99f);
            // The screen's span of bearing from the seat, left of straight ahead.
            from[i/6]=1e9f;to[i/6]=-1e9f;top[i/6]=-1e9f;bottom[i/6]=1e9f;
            for(unsigned j=0;j<6;++j) {
                const float bearing=std::atan2(kCockpitSeatedEye[0]-displays[i+j].position[0],displays[i+j].position[2]-kCockpitSeatedEye[2])*57.29578f;
                from[i/6]=std::min(from[i/6],bearing);to[i/6]=std::max(to[i/6],bearing);
                top[i/6]=std::max(top[i/6],displays[i+j].position[1]);bottom[i/6]=std::min(bottom[i/6],displays[i+j].position[1]);
            }
            middle[i/6]=(from[i/6]+to[i/6])*.5f;
            std::printf("gunner screen %u: %.1f..%.1f degrees left, %.3f..%.3f high\n",i/6,from[i/6],to[i/6],bottom[i/6],top[i/6]);
        }
        // The radar first and easiest to see, clear of the stack; health
        // stacked over ammunition in one column, not touching.
        CHECK(to[0]+3<from[1]&&to[0]+3<from[2]);
        CHECK(std::fabs(middle[1]-middle[2])<1.5f);
        CHECK(bottom[1]>top[2]+.008f);
        // The roof cover now reaches over the head: the barrel's base is hidden.
        CHECK(MeshBlocked(mesh,kCockpitSeatedEye,{0,2.f,.20f}));
        CHECK(!MeshBlocked(mesh,kCockpitSeatedEye,{0,2.f,2.50f}));
        const auto eye=kCockpitSeatedEye;
        for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
            auto desired=eye;desired[axis]+=sign*2;
            const auto stopped=ClampCockpitHead(desired,.10f,CockpitKind::TitanGunner);
            std::printf("titan head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",axis,sign,stopped[0],stopped[1],stopped[2]);
            CHECK(sign*(stopped[axis]-eye[axis])>.035f);
            CHECK(!MeshBlocked(mesh,eye,stopped));
        }
    }
    // Every cap mesh builds, closed and finite.
    for(unsigned i=0;i<kCockpitCapMeshes;++i) {
        const auto cap=BuildCockpitCap(i);CHECK(!cap.empty()&&cap.size()%3==0);
        for(const auto& v:cap){float n=0;for(int k=0;k<3;++k){CHECK(std::isfinite(v.position[k]));n+=v.normal[k]*v.normal[k];}CHECK(std::fabs(n-1)<.001f);}
    }
}
// Combat vehicles: each cockpit lies inside its shell (and so inside the
// real hull, which research/cockpit/combat_shells.py --check confirms); it is
// open ahead, to both sides and overhead, closed behind and below the sill.
void CombatLayoutTest(bool baked=true) {
    for(const auto& shell:kCombatShells) {
        const auto kind=shell.kind;const auto c=CombatCabin(shell);
        auto mesh=BuildCockpit(kind);
        std::printf("Combat cabin %u: %zu triangles\n",static_cast<unsigned>(kind),mesh.size()/3);
        CheckMeshSanity(mesh);
        if(baked)CHECK(ApplyCockpitOcclusion(mesh,kind));
        // Inside: above the sill within every plane, below it within the box.
        // Above the sill (and in the ledge's 3 cm under it) within every plane;
        // below that within the lower box, stepped in over the tracks.
        auto planes=[&](const float* p,float tol) {
            for(unsigned i=0;i<c.planeCount;++i)if(c.n[i][0]*p[0]+c.n[i][1]*p[1]+c.n[i][2]*p[2]>c.d[i]+tol)return false;
            return true;
        };
        auto inside=[&](const float* p,float tol) {
            if(p[1]>=c.sill-.034f&&planes(p,tol))return true;
            if(p[0]<c.xLeft-tol||p[0]>c.xRight+tol||p[2]<c.zRear-tol||p[2]>c.zFront+tol||p[1]<c.floor-tol||p[1]>c.sill+tol)return false;
            const bool stepped=c.xInner>c.xLeft+.01f&&c.sponson>c.floor+.01f;
            return !(stepped&&p[1]<c.sponson-tol&&p[0]<c.xInner-tol);
        };
        unsigned out=0,ink=0,seen=0;
        const float o[3]={shell.eye[0],shell.eye[1]-kCockpitSeatedEye[1],shell.eye[2]-kCockpitSeatedEye[2]};
        auto regions=CombatCutRegions(shell,.005f);
        if(CombatCutNarrow(shell)&&CombatCutCapped(shell)&&regions.size()>1)regions.erase(regions.begin());
        for(std::size_t i=mesh.size();i>0&&mesh[i-1].surface==21;--i)++ink;
        for(const auto& v:mesh) {
            if(v.surface==21) {
                // Monitor ink: where the outside is seen (model coordinates).
                const CombatPoint p{o[0]-v.position[0],v.position[1]+o[1],v.position[2]+o[2]};
                bool in=false;
                for(const auto& r:regions){bool all=true;for(unsigned k=0;k<r.count;++k)all=all&&CombatDot(r.n[k],p)<=r.d[k];in=in||all;}
                if(in)++seen;
                continue;
            }
            const bool ok=inside(v.position,.004f);
            if(!ok&&++out<6)std::printf("combat %u vertex outside its shell: %.3f %.3f %.3f\n",static_cast<unsigned>(kind),v.position[0],v.position[1],v.position[2]);
            CHECK(ok);
        }
        std::printf("combat %u monitor ink: %u vertices, %u in sight\n",static_cast<unsigned>(kind),ink,seen);
        CHECK(ink>=6*4*3);   // lines, all at the end of the mesh
        CHECK(seen==ink);
        const auto eye=kCockpitSeatedEye;
        // Open: straight ahead a little down, left and right at eye level, up.
        auto open=[&](Point dir){return !MeshBlocked(mesh,eye,{eye[0]+dir[0]*6,eye[1]+dir[1]*6,eye[2]+dir[2]*6});};
        CHECK(open({0,-.05f,1}));CHECK(open({-1,.05f,.2f}));CHECK(open({1,.05f,.2f}));CHECK(open({.10f,1,.25f}));
        // Closed behind, and below the sill all round.
        // (Up and back is the roof pane: rays at eye level only.)
        for(float a:{150.f,180.f,210.f})for(float up:{-.05f,0.f}) {
            const float r=a*.0174533f;CHECK(!open({std::sin(r),up,std::cos(r)}));
        }
        for(int k=0;k<24;++k) {
            const float a=6.2831853f*k/24;const Point down{std::sin(a),-.9f,std::cos(a)};
            CHECK(!open(down));
        }
        // Head: free inside, kept in by the panes and the box.
        for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
            auto desired=eye;desired[axis]+=sign*3;
            const auto stopped=ClampCockpitHead(desired,.10f,kind);
            std::printf("combat %u head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",static_cast<unsigned>(kind),axis,sign,stopped[0],stopped[1],stopped[2]);
            CHECK(sign*(stopped[axis]-eye[axis])>.02f);
            CHECK(inside(stopped.data(),.0f));
            CHECK(!MeshBlocked(mesh,eye,stopped));
        }
        const auto displays=BuildCockpitDisplays(kind);CHECK(displays.size()==18);
        for(const auto& v:displays)CHECK(v.visibility[1]<.5f);
    }
}
// Combat vehicles' cut (cockpit_combat_shells.h): above the sill, from behind
// the seat forward, the hull goes. The game leaves out the hull triangles that
// meet the cut (SelectCockpitLimbs); the lid (CollectCockpitHull, then
// BuildCombatLid) draws what of them lies outside it and covers the cut with a
// deck and a rear cover. From the seat: open ahead, to both sides and up, the
// deck below, nothing of the lid in the cut. The real hulls come from the
// local research survey (research/cockpit/combat, never shipped); a box
// stands in without them.
void CombatLidTest(const std::filesystem::path& survey={},const std::filesystem::path& exportTo={}) {
    auto half=[](float f)->std::uint16_t{std::uint32_t x=0;std::memcpy(&x,&f,4);if(f==0)return 0;
        return static_cast<std::uint16_t>(((x>>16)&0x8000)|((((x>>23)&255)-112)<<10)|((x>>13)&1023));};
    // Nine positions per triangle into a 60 byte vertex buffer, all on one bone.
    auto vertices=[&](const std::vector<std::array<float,9>>& tris,const std::vector<unsigned char>& bones) {
        std::vector<unsigned char> vb(60*3*tris.size(),0);
        for(std::size_t t=0;t<tris.size();++t)for(unsigned k=0;k<3;++k) {
            auto* v=vb.data()+(t*3+k)*60;
            for(unsigned c=0;c<3;++c){const auto h=half(tris[t][k*3+c]);std::memcpy(v+2*c,&h,2);}
            const float w[4]={1,0,0,0};std::memcpy(v+40,w,16);v[56]=bones[t];
        }
        return vb;
    };
    {   // Halves at byte 0; only the hull bone's triangles.
        const std::vector<std::array<float,9>> tris{{.5f,1,2,1.5f,1,2,.5f,2,2},{0,0,0,1,0,0,0,1,0}};
        const auto vb=vertices(tris,{2,1});
        const std::uint16_t indices[6]={0,1,2,3,4,5};
        CockpitRig rig{};rig.kind=CockpitKind::CombatNegling;rig.nodeCount=4;rig.bodyBone=2;
        std::vector<float> hull;CollectCockpitHull(vb.data(),vb.size(),60,indices,6,false,0,rig,hull);
        CHECK(hull.size()==9);
        if(hull.size()==9)for(unsigned k=0;k<9;++k)CHECK(hull[k]==tris[0][k]);
    }
    {   // The game's own draw: the Negling's sill is 1.70, its cut from z -0.10.
        const std::vector<std::array<float,9>> tris{
            {0,1.5f,1,.5f,1.5f,1,0,1.5f,1.5f},   // under the sill: drawn
            {0,2,1,.5f,2,1,0,2,1.5f},            // in the cut: left to the lid
            {0,1.5f,1,.5f,2,1,0,2,1.5f},         // across the sill: left to the lid
            {0,2,-1,.5f,2,-1,0,2,-.5f},          // over the sill behind the cut: drawn
            {0,2,1,.5f,2,1,0,2,1.5f}};           // the same in the cut, another bone: drawn
        const auto vb=vertices(tris,{2,2,2,2,1});
        std::vector<std::uint16_t> indices(15);for(unsigned i=0;i<15;++i)indices[i]=static_cast<std::uint16_t>(i);
        CockpitRig rig{};rig.kind=CockpitKind::CombatNegling;rig.nodeCount=4;rig.bodyBone=2;rig.limb={};rig.limb[1]=rig.limb[2]=1;
        std::vector<std::uint32_t> kept;
        const auto result=SelectCockpitLimbs(vb.data(),vb.size(),60,indices.data(),15,false,0,rig,kept);
        CHECK(result.valid&&result.kept==9&&result.removed==6);
        CHECK(kept==std::vector<std::uint32_t>({0,1,2,9,10,11,12,13,14}));
    }
    const char* stems[]={"vehicle402_rocket","vehicle401_striker","v507_rescuetank"};
    for(unsigned s=0;s<std::size(kCombatShells);++s) {
        const auto& shell=kCombatShells[s];
        std::vector<float> hull;
        {
            std::string bodyName;for(const wchar_t* ch=shell.body;*ch;++ch)bodyName.push_back(static_cast<char>(*ch));
            std::ifstream f;if(!survey.empty())f.open(survey/(std::string(stems[s])+"__"+bodyName+".obj"));
            std::string line;
            while(f&&std::getline(f,line))if(line.size()>2&&line[0]=='v'&&line[1]==' ') {
                std::istringstream in(line.substr(2));float x=0,y=0,z=0;
                if(in>>x>>y>>z){hull.push_back(x);hull.push_back(y);hull.push_back(z);}
            }
        }
        const bool real=hull.size()>=900&&hull.size()%9==0;
        if(!survey.empty())CHECK(real);
        if(!real) {   // a box round the compartment, a metre behind and ahead of it
            const float x0=-2,x1=2,y0=shell.floor-.5f,z0=shell.cutRear-1,z1=shell.zFront+1;
            const float y1=std::max(shell.planes[0].d,CombatCutCapped(shell)?shell.cutTop:0.f)+.3f;   // over any kept visor
            const float c[8][3]={{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
            const int q[6][4]={{0,1,2,3},{4,5,6,7},{0,1,5,4},{3,2,6,7},{0,3,7,4},{1,2,6,5}};
            for(const auto& f:q) {
                const int tris[2][3]={{f[0],f[1],f[2]},{f[0],f[2],f[3]}};
                for(const auto& t:tris)for(int k=0;k<3;++k)for(int j=0;j<3;++j)hull.push_back(c[t[k]][j]);
            }
        }
        const auto lid=BuildCombatLid(shell.kind,hull),bare=BuildCombatLid(shell.kind,hull,false);
        const auto fittings=BuildCombatDecor(shell.kind,hull);
        std::printf("Combat lid %u (%s hull, %zu triangles): %zu triangles, %zu of them fittings\n",static_cast<unsigned>(shell.kind),real?"real":"box",hull.size()/9,lid.size()/3,fittings.size()/3);
        CHECK(lid.size()>=6&&lid.size()%3==0&&lid.size()==bare.size()+fittings.size());
        CHECK(fittings.size()>=300);   // every vehicle carries its fittings
        for(const auto& v:lid){float n=0;for(int k=0;k<3;++k){CHECK(std::isfinite(v.position[k]));n+=v.normal[k]*v.normal[k];}CHECK(std::fabs(n-1)<.001f);}
        // Nothing of the lid proper in the cut (model = bone local + bind origin).
        unsigned inside=0;
        const auto regions=CombatCutRegions(shell,.005f);
        for(const auto& v:bare) {
            const CombatPoint p{v.position[0]+shell.bodyOrigin[0],v.position[1]+shell.bodyOrigin[1],v.position[2]+shell.bodyOrigin[2]};
            bool in=false;
            for(const auto& r:regions){bool all=true;for(unsigned k=0;k<r.count;++k)all=all&&CombatDot(r.n[k],p)<=r.d[k];in=in||all;}
            if(in&&++inside<4)std::printf("combat lid %u vertex in the cut: %.3f %.3f %.3f\n",static_cast<unsigned>(shell.kind),p[0],p[1],p[2]);
        }
        CHECK(inside==0);
        // The fittings: inside the vehicle's original body (under the hull over
        // them, within its sides) and outside the cockpit (its upper compartment
        // and the box under the sill). Model coordinates.
        if(real) {
            std::vector<unsigned> all;for(std::size_t i=0;i+9<=hull.size();i+=9)all.push_back(static_cast<unsigned>(i));
            const auto cabin=CombatCabin(shell);
            const float o[3]={shell.eye[0],shell.eye[1]-kCockpitSeatedEye[1],shell.eye[2]-kCockpitSeatedEye[2]};
            unsigned out=0,cockpit=0;
            for(std::size_t i=0;i<fittings.size();i+=7) {   // a sample of the vertices
                const auto& p=fittings[i].position;
                float r=0,l=0;
                const bool under=p[1]<=CombatHullAbove(hull,p[0],p[2],shell.cutFloor+.01f)+.003f;
                const bool within=CombatHullSpan(hull,all,p[1],p[2],r,l)&&p[0]>=r-.003f&&p[0]<=l+.003f;
                if((!under||!within)&&++out<6)std::printf("combat fitting %u outside the body (under %d within %d): %.3f %.3f %.3f\n",static_cast<unsigned>(shell.kind),under?1:0,within?1:0,p[0],p[1],p[2]);
                // Cabin coordinates: +X is model -X.
                const float c[3]={-(p[0]-o[0]),p[1]-o[1],p[2]-o[2]};
                bool upper=c[1]>cabin.sill+.01f;
                for(unsigned k=0;k<cabin.planeCount;++k)upper=upper&&cabin.n[k][0]*c[0]+cabin.n[k][1]*c[1]+cabin.n[k][2]*c[2]<cabin.d[k]-.01f;
                const bool lower=c[1]<cabin.sill&&c[0]>cabin.xLeft+.01f&&c[0]<cabin.xRight-.01f&&c[2]>cabin.zRear+.01f&&c[2]<cabin.zFront-.01f;
                if((upper||lower)&&++cockpit<6)std::printf("combat fitting %u in the cockpit: %.3f %.3f %.3f\n",static_cast<unsigned>(shell.kind),p[0],p[1],p[2]);
            }
            CHECK(out==0);CHECK(cockpit==0);
        }
        const Point eye{shell.eye[0]-shell.bodyOrigin[0],shell.eye[1]-shell.bodyOrigin[1],shell.eye[2]-shell.bodyOrigin[2]};
        auto blocked=[&](Point dir,float reach){return MeshBlocked(lid,eye,{eye[0]+dir[0]*reach,eye[1]+dir[1]*reach,eye[2]+dir[2]*reach});};
        if(CombatCutCapped(shell)&&shell.eye[2]<shell.cutTopRear) {
            CHECK(blocked({0,1,0},1.5f));                      // under the kept visor: its ceiling
            CHECK(!blocked({0,.7f,.7f},3));                    // up and ahead, past its front
        }
        else CHECK(!blocked({0,1,0},.9f));                     // up
        CHECK(!blocked({0,.02f,1},6));                         // ahead
        const float past=CombatCutNarrow(shell)?.6f:.05f;      // ahead of any kept pillars
        CHECK(!blocked({-1,.02f,past},4));                     // right (model -X)
        CHECK(!blocked({1,.02f,past},4));                      // left
        CHECK(blocked({0,-1,0},1.5f));                         // the deck under the seat
        CHECK(blocked({-.7f,-.1f,-.7f},4));                    // back and to the right, low: the rear cover
        if(real&&!exportTo.empty()) {   // for the preview only, beside the survey (never shipped)
            std::ofstream f(exportTo/("lid_"+std::string(stems[s])+".obj")),k(exportTo/("keep_"+std::string(stems[s])+".obj"));
            f<<"# EDF6VR combat lid from the research survey hull, hull bone local (+X model left); research only\n";
            for(const auto& v:lid)f<<"v "<<v.position[0]<<' '<<v.position[1]<<' '<<v.position[2]<<'\n';
            int material=-1;   // the surface per face, as usemtl m<surface>
            for(std::size_t i=0;i<lid.size();i+=3) {
                const int m=static_cast<int>(lid[i].surface+.5f);
                if(m!=material){f<<"usemtl m"<<m<<'\n';material=m;}
                f<<"f "<<i+1<<' '<<i+2<<' '<<i+3<<'\n';
            }
            k<<"# EDF6VR combat hull the game still draws, from the research survey, hull bone local; research only\n";
            std::size_t n=0;
            for(std::size_t i=0;i+9<=hull.size();i+=9) {
                if(CombatCutMeets(shell,&hull[i]))continue;
                for(unsigned j=0;j<3;++j)k<<"v "<<hull[i+j*3]-shell.bodyOrigin[0]<<' '<<hull[i+j*3+1]-shell.bodyOrigin[1]<<' '<<hull[i+j*3+2]-shell.bodyOrigin[2]<<'\n';
                k<<"f "<<n+1<<' '<<n+2<<' '<<n+3<<'\n';n+=3;
            }
            CHECK(bool(f)&&bool(k));
        }
    }
}
void ExportCaps(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    for(unsigned i=10;i<kCockpitCapMeshes;++i) {
        const auto cap=BuildCockpitCap(i);std::ofstream f(folder/("cap_"+std::to_string(i)+".obj"));
        f<<"# EDF6VR cap mesh "<<i<<", in its bone's local game coordinates (+X model left)\n";
        for(const auto& v:cap)f<<"v "<<v.position[0]<<' '<<v.position[1]<<' '<<v.position[2]<<'\n';
        for(std::size_t k=0;k<cap.size();k+=3)f<<"f "<<k+1<<' '<<k+2<<' '<<k+3<<'\n';
        CHECK(bool(f));
    }
}
