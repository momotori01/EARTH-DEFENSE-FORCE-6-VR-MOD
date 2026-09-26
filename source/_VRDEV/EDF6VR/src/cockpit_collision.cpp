#include "cockpit.h"
#include "cockpit_combat_shells.h"
#include "cockpit_heli_shells.h"
#include <algorithm>
#include <cmath>
namespace edf6vr {
namespace {
using P=std::array<float,3>;
P Sub(P a,P b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
P Add(P a,P b){return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
P Mul(P a,float s){return {a[0]*s,a[1]*s,a[2]*s};}
float Dot(P a,P b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
P Cross(P a,P b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float EdgeDistance(P p,P a,P b){const auto e=Sub(b,a);const float n=Dot(e,e);
    const auto d=Sub(p,Add(a,Mul(e,n>1e-12f?std::clamp(Dot(Sub(p,a),e)/n,0.f,1.f):0.f)));return Dot(d,d);}
float TriangleDistance(P p,P a,P b,P c){
    const auto n=Cross(Sub(b,a),Sub(c,a));const float nn=Dot(n,n),d=Dot(Sub(p,a),n);
    if(nn>1e-12f){const auto q=Sub(p,Mul(n,d/nn));
        if(Dot(Cross(Sub(b,a),Sub(q,a)),n)>=0&&Dot(Cross(Sub(c,b),Sub(q,b)),n)>=0&&Dot(Cross(Sub(a,c),Sub(q,c)),n)>=0)return d*d/nn;}
    return std::min({EdgeDistance(p,a,b),EdgeDistance(p,b,c),EdgeDistance(p,c,a)});
}
float BoundsDistance(P p,P lo,P hi){float result=0;
    for(int k=0;k<3;++k){const float d=p[k]-std::clamp(p[k],lo[k],hi[k]);result+=d*d;}return result;}
// Filleted render geometry has more triangles. Keep collision on that exact
// surface (including the transparent combiners), but prune it spatially rather
// than scanning every triangle at each step. Built once, no per-look allocation.
struct CollisionMesh {
    struct Triangle {P a,b,c,lo,hi;};
    struct Node {P lo,hi;std::size_t begin,end,left=0,right=0;};
    std::vector<Triangle> triangles;std::vector<Node> nodes;
    float forwardBoundary=0;
    std::size_t Build(std::size_t begin,std::size_t end) {
        Node n{triangles[begin].lo,triangles[begin].hi,begin,end};
        for(auto i=begin+1;i<end;++i)for(int k=0;k<3;++k){n.lo[k]=std::min(n.lo[k],triangles[i].lo[k]);n.hi[k]=std::max(n.hi[k],triangles[i].hi[k]);}
        const auto index=nodes.size();nodes.push_back(n);
        if(end-begin>8) {
            int axis=0;for(int k=1;k<3;++k)if(n.hi[k]-n.lo[k]>n.hi[axis]-n.lo[axis])axis=k;
            const auto middle=begin+(end-begin)/2;
            std::nth_element(triangles.begin()+begin,triangles.begin()+middle,triangles.begin()+end,
                [axis](const Triangle& a,const Triangle& b){return a.lo[axis]+a.hi[axis]<b.lo[axis]+b.hi[axis];});
            const auto left=Build(begin,middle),right=Build(middle,end);
            nodes[index].left=left;nodes[index].right=right;
        }
        return index;
    }
    explicit CollisionMesh(CockpitKind kind) {
        auto mesh=BuildCockpit(kind);const auto displays=BuildCockpitDisplays(kind);
        for(const auto& v:displays)forwardBoundary=std::max(forwardBoundary,v.position[2]);
        // Side screens do not bound the open forward cabin. Their actual
        // faces stop sideways motion; the nose limits a lower forward lean.
        if(kind==CockpitKind::Crawler)forwardBoundary=.90f;
        // Proteus: the consoles sit beside the legs, so the panes say nothing
        // about the long pod ahead. The measured nose skin is the real limit.
        if(kind==CockpitKind::ProteusGunner||kind==CockpitKind::TitanGunner)forwardBoundary=1.24f;
        if(kind==CockpitKind::Tank)forwardBoundary=1.30f;
        if(IsCombatKind(kind))forwardBoundary=2.f;
        if(IsHeliKind(kind))forwardBoundary=.70f;
        // The door gunner leans out of his doorway, past its screens on the walls.
        if(kind==CockpitKind::HeliBruteGunner)forwardBoundary=.45f;
        // Tandem cabin: the nose frame, and the same value carried back for
        // the rear station whose build is shifted onto its own eye.
        if(kind==CockpitKind::ProteusDriver)forwardBoundary=1.40f;
        if(kind==CockpitKind::ProteusMissile)forwardBoundary=1.40f+1.18f;
        mesh.insert(mesh.end(),displays.begin(),displays.end());
        triangles.reserve(mesh.size()/3);nodes.reserve(mesh.size()/6);
        // Screen ink is not a surface; a helicopter's canopy glass is.
        for(std::size_t i=0;i<mesh.size();i+=3){const int surface=static_cast<int>(mesh[i].surface+.5f);if(surface>=20&&surface!=24)continue;Triangle t{};
            for(int k=0;k<3;++k){t.a[k]=mesh[i].position[k];t.b[k]=mesh[i+1].position[k];t.c[k]=mesh[i+2].position[k];
                t.lo[k]=std::min({t.a[k],t.b[k],t.c[k]});t.hi[k]=std::max({t.a[k],t.b[k],t.c[k]});}triangles.push_back(t);}
        if(!triangles.empty())Build(0,triangles.size());
    }
    void Nearest(std::size_t index,P p,float& best) const {
        const auto& n=nodes[index];if(BoundsDistance(p,n.lo,n.hi)>=best)return;
        if(n.right) {
            auto first=n.left,second=n.right;
            if(BoundsDistance(p,nodes[first].lo,nodes[first].hi)>BoundsDistance(p,nodes[second].lo,nodes[second].hi))std::swap(first,second);
            Nearest(first,p,best);Nearest(second,p,best);
        } else for(auto i=n.begin;i<n.end;++i){const auto& t=triangles[i];
            if(BoundsDistance(p,t.lo,t.hi)<best)best=std::min(best,TriangleDistance(p,t.a,t.b,t.c));}
    }
};
const CollisionMesh& CabinCollision(CockpitKind kind){
    if(kind==CockpitKind::ProteusGunner){static const CollisionMesh proteus(CockpitKind::ProteusGunner);return proteus;}
    if(kind==CockpitKind::TitanGunner){static const CollisionMesh titan(CockpitKind::TitanGunner);return titan;}
    if(kind==CockpitKind::Tank){static const CollisionMesh tank(CockpitKind::Tank);return tank;}
    if(kind==CockpitKind::CombatNegling){static const CollisionMesh negling(CockpitKind::CombatNegling);return negling;}
    if(kind==CockpitKind::CombatGrape){static const CollisionMesh grape(CockpitKind::CombatGrape);return grape;}
    if(kind==CockpitKind::CombatCaliban){static const CollisionMesh caliban(CockpitKind::CombatCaliban);return caliban;}
    if(kind==CockpitKind::HeliNereid){static const CollisionMesh nereid(CockpitKind::HeliNereid);return nereid;}
    if(kind==CockpitKind::Heli602){static const CollisionMesh h602(CockpitKind::Heli602);return h602;}
    if(kind==CockpitKind::Heli506){static const CollisionMesh h506(CockpitKind::Heli506);return h506;}
    if(kind==CockpitKind::HeliBrute){static const CollisionMesh brute(CockpitKind::HeliBrute);return brute;}
    if(kind==CockpitKind::HeliBruteGunner){static const CollisionMesh gunner(CockpitKind::HeliBruteGunner);return gunner;}
    if(kind==CockpitKind::ProteusDriver){static const CollisionMesh driver(CockpitKind::ProteusDriver);return driver;}
    if(kind==CockpitKind::ProteusMissile){static const CollisionMesh missile(CockpitKind::ProteusMissile);return missile;}
    if(kind==CockpitKind::Barga){static const CollisionMesh barga(CockpitKind::Barga);return barga;}
    if(kind==CockpitKind::Crawler){static const CollisionMesh crawler(CockpitKind::Crawler);return crawler;}
    if(kind==CockpitKind::NixChest){static const CollisionMesh wagon(CockpitKind::NixChest);return wagon;}
    if(kind==CockpitKind::TruckPickup){static const CollisionMesh pickup(CockpitKind::TruckPickup);return pickup;}
    static const CollisionMesh nix(CockpitKind::Nix);return nix;
}
float Clearance(P p,CockpitKind kind){const auto& mesh=CabinCollision(kind);float best=100;
    if(!mesh.nodes.empty())mesh.Nearest(0,p,best);return std::sqrt(best);}
}
void PrepareCockpitCollision() noexcept {for(unsigned k=0;k<kCockpitKinds;++k)CabinCollision(static_cast<CockpitKind>(k));}
std::array<float,3> ClampCockpitHead(P desired,float radius,CockpitKind kind) noexcept {
    constexpr auto origin=kCockpitSeatedEye;
    for(float v:desired)if(!std::isfinite(v))return origin;
    if(!std::isfinite(radius)||radius<.04f||radius>.22f)return origin;
    if(Dot(Sub(desired,origin),Sub(desired,origin))<1e-12f)return desired;
    // No per-axis tracking allowance: these are the shell's inner extents
    // (side pods, aft bulkhead, floor, rear crown, instrument row). They close
    // the open windows for collision only; actual hardware stops the head first.
    const float margin=radius+.002f;
    const bool crawler=kind==CockpitKind::Crawler;
    if(kind==CockpitKind::Barga) {
        const auto relative=Sub(desired,kBargaSphereCentre);const float limit=kBargaSphereRadius-margin;
        if(Dot(relative,relative)>limit*limit) {
            // Intersect the requested straight head movement with the sphere.
            const auto d=Sub(desired,origin),o=Sub(origin,kBargaSphereCentre);
            const float a=Dot(d,d),b=Dot(o,d),c=Dot(o,o)-limit*limit;
            desired=Add(origin,Mul(d,(-b+std::sqrt(std::max(0.f,b*b-a*c)))/a));
        }
    } else if(kind==CockpitKind::ProteusDriver||kind==CockpitKind::ProteusMissile) {
        // Tandem cabin: the camera is free inside it. The swept mesh test
        // below stops the head at the furniture and the decks; this only
        // stands in for the screens, which are not mesh - the side walls, the
        // roof, the nose and the deck plane over the chin void. The rear
        // station's build is shifted so its own eye is at the origin.
        const bool back=kind==CockpitKind::ProteusMissile;
        const float up=back?-.56f:0.f,aft=back?1.18f:0.f;   // cabin -> build frame
        auto half=[](float z){return z<.20f?.86f:std::max(.56f,.86f-.25f*(z-.20f));};
        auto roof=[](float z){return z<-.20f?1.04f:std::max(.56f,1.04f-.26f*(z+.20f));};
        auto deck=[](float z){return z<-.62f?-.66f:-1.22f;};
        desired[2]=std::clamp(desired[2],-1.82f+aft+margin,1.30f+aft-margin);
        const float zc=desired[2]-aft;
        desired[0]=std::clamp(desired[0],-half(zc)+margin,half(zc)-margin);
        desired[1]=std::clamp(desired[1],deck(zc)+up+margin+.02f,roof(zc)+up-margin);
    } else if(const auto* shell=FindCombatShell(kind)) {
        // Combat vehicle: above the sill the compartment's see-through faces
        // are planes, not mesh - keep the head inside them. Below the sill,
        // inside the lower box. The mesh sweep below stops it at everything real.
        const auto c=CombatCabin(*shell);
        desired[0]=std::clamp(desired[0],c.xLeft+margin,c.xRight-margin);
        desired[2]=std::clamp(desired[2],c.zRear+margin,c.zFront+1.f);
        desired[1]=std::max(desired[1],c.floor+margin);
        for(int pass=0;pass<4;++pass)for(unsigned i=0;i<c.planeCount;++i) {
            const P n{c.n[i][0],c.n[i][1],c.n[i][2]};const float over=Dot(n,desired)-(c.d[i]-margin);
            if(over>0)desired=Sub(desired,Mul(n,over));
        }
        if(desired[1]<c.sill)desired[2]=std::min(desired[2],c.zFront-margin);
    } else if(kind==CockpitKind::HeliBruteGunner) {
        // The booth: between its walls, under its ceiling, over its deck, and
        // out of the door as far as a lean with the hands on the lever takes
        // the head; the doorway's frame and the screens are mesh.
        desired[0]=std::clamp(desired[0],-.525f+margin,.525f-margin);
        desired[1]=std::clamp(desired[1],-1.09f+margin,.50f-margin);
        desired[2]=std::clamp(desired[2],-.70f+margin,.45f-margin);
    } else if(IsHeliKind(kind)) {
        // Helicopter: the canopy's glass is mesh and stops the head; the lined
        // hull round it is not, so the head stays under the hood - its sides,
        // its roof falling toward the windscreen - and over the floor.
        // Each machine's limits are measured with its shell (heli_shells.py).
        const auto& s=*FindHeliShell(kind);
        desired[0]=std::clamp(desired[0],-s.clampHalfWidth+margin,s.clampHalfWidth-margin);
        desired[2]=std::clamp(desired[2],s.clampRear+margin,s.clampFront-margin);
        const float roof=std::clamp(s.roofTop-s.roofSlope*(desired[2]+.34f),s.roofMin,s.roofTop);
        desired[1]=std::clamp(desired[1],s.clampFloor+margin,roof-margin);
    } else if(kind==CockpitKind::Tank) {
        // Tank: everything above the sill is turret screen, not mesh. This is
        // the screen - the plan octagon, the band and the sloped roof - and
        // the swept mesh test below stops the head at everything real.
        constexpr float plan[8][2]={{-.44f,1.30f},{-.80f,.92f},{-.80f,-.36f},{-.46f,-.72f},{.46f,-.72f},{.80f,-.36f},{.80f,.92f},{.44f,1.30f}};
        // Square sides first, then the four canted corners cut the motion short.
        desired[0]=std::clamp(desired[0],-.80f+margin,.80f-margin);
        desired[2]=std::clamp(desired[2],-.72f+margin,1.30f-margin);
        auto edge=[&](int i,P p,float& nx,float& nz) {
            const float ax=plan[i][0],az=plan[i][1],bx=plan[(i+1)%8][0],bz=plan[(i+1)%8][1];
            nx=bz-az;nz=ax-bx;const float length=std::sqrt(nx*nx+nz*nz);nx/=length;nz/=length;
            if(nx*((ax+bx)*.5f)+nz*((az+bz)*.5f-.29f)<0){nx=-nx;nz=-nz;}
            return (ax-p[0])*nx+(az-p[2])*nz;   // distance inside this edge
        };
        {
            auto motion=Sub(desired,origin);float fraction=1;
            for(int i:{0,2,4,6}) {
                float nx=0,nz=0;const float start=edge(i,origin,nx,nz)-margin;
                const float closing=motion[0]*nx+motion[2]*nz;   // growth of the outward distance
                if(closing>0)fraction=std::min(fraction,std::max(0.f,start)/closing);
            }
            desired=Add(origin,Mul(motion,std::max(0.f,fraction)));
        }
        float inside=1e9f;
        for(int i=0;i<8;++i){float nx=0,nz=0;inside=std::min(inside,edge(i,desired,nx,nz));}
        const float roof=.36f+std::clamp(inside,0.f,.22f)/.22f*.24f;
        desired[1]=std::clamp(desired[1],-1.20f+margin,roof-margin);
    } else if(kind==CockpitKind::TitanGunner) {
        // The Proteus pod with the right side brought in to .42.
        desired[0]=std::clamp(desired[0],-.62f+margin,.42f-margin);
        desired[1]=std::clamp(desired[1],-.91f+margin,.65f-margin);
        desired[2]=std::clamp(desired[2],-.48f+margin,1.22f-margin);
        auto motion=Sub(desired,origin);float fraction=1;
        for(float side:{-1.f,1.f}) {
            const P normal{side,0,2.67f};const float along=Dot(normal,motion);
            if(along>0)fraction=std::min(fraction,(3.911f-margin*2.851f-Dot(normal,origin))/along);
        }
        desired=Add(origin,Mul(motion,std::max(0.f,fraction)));
    } else if(kind==CockpitKind::ProteusGunner) {
        // Straight gun pod: parallel side skins, a floor ramp climbing toward
        // the nose and a blunt nose cone. The swept mesh test below follows
        // the ramp, the reclined seat and the overhead gun trunk exactly.
        desired[0]=std::clamp(desired[0],-.62f+margin,.62f-margin);
        desired[1]=std::clamp(desired[1],-.91f+margin,.65f-margin);
        desired[2]=std::clamp(desired[2],-.48f+margin,1.22f-margin);
        auto motion=Sub(desired,origin);float fraction=1;
        for(float side:{-1.f,1.f}) {
            // Nose cone, inset by the cabin's own armour and loom allowance.
            const P normal{side,0,2.67f};const float along=Dot(normal,motion);
            if(along>0)fraction=std::min(fraction,(3.911f-margin*2.851f-Dot(normal,origin))/along);
        }
        desired=Add(origin,Mul(motion,std::max(0.f,fraction)));
    } else {
    const float halfWidth=crawler?.51f:.68f;
    desired[0]=std::clamp(desired[0],-halfWidth+margin,halfWidth-margin);
    // The Nix cabin: its deck at -1.11 (in the waist; the Wagon's at -.79, in
    // the chest), its ceiling over the head at .514.
    desired[1]=std::clamp(desired[1],(kind==CockpitKind::NixChest?-.79f:-1.11f)+margin,(crawler?.39f:.514f)-margin);
    desired[2]=std::clamp(desired[2],-.56f+margin,CabinCollision(kind).forwardBoundary-margin);
    // Follow the inward-folding upper sides between the shoulder and crown;
    // a head cannot pass outside the diagonal rail through its open window.
    auto motion=Sub(desired,origin);float fraction=1;
    for(float side:{-1.f,1.f}) {
        const P normal{side,crawler?.30f:.85f,0};const float along=Dot(normal,motion);
        if(along>0)fraction=std::min(fraction,((crawler?.57f:1.088f)-margin*std::sqrt(Dot(normal,normal))-Dot(normal,origin))/along);
    }
    if(!crawler) {
        // The chest's low front top over the neck shroud and the screens
        // ahead of it (research/cockpit/robot_fit.py): .465 over the pilot,
        // falling to about .37 over the knees.
        const P normal{0,1,.10f};const float along=Dot(normal,motion);
        if(along>0)fraction=std::min(fraction,(.455f-margin*std::sqrt(Dot(normal,normal))-Dot(normal,origin))/along);
    }
    if(crawler) {
        // Sloping front canopy: world hull survey, not an arbitrary tracking cap.
        const P normal{0,1,.55f};const float along=Dot(normal,motion);
        if(along>0)fraction=std::min(fraction,(.445f-margin*std::sqrt(Dot(normal,normal))-Dot(normal,origin))/along);
    }
    desired=Add(origin,Mul(motion,std::max(0.f,fraction)));
    }
    const auto motion=Sub(desired,origin);
    const float length=std::sqrt(Dot(motion,motion));if(length<1e-6f)return desired;
    const auto neutralClearance=[&](){if(crawler){static const float c=Clearance(origin,CockpitKind::Crawler);return c;}
        if(kind==CockpitKind::Barga){static const float b=Clearance(origin,CockpitKind::Barga);return b;}
        if(kind==CockpitKind::ProteusGunner){static const float p=Clearance(origin,CockpitKind::ProteusGunner);return p;}
        if(kind==CockpitKind::TitanGunner){static const float t=Clearance(origin,CockpitKind::TitanGunner);return t;}
        if(kind==CockpitKind::Tank){static const float k=Clearance(origin,CockpitKind::Tank);return k;}
        if(kind==CockpitKind::CombatNegling){static const float c=Clearance(origin,CockpitKind::CombatNegling);return c;}
        if(kind==CockpitKind::CombatGrape){static const float c=Clearance(origin,CockpitKind::CombatGrape);return c;}
        if(kind==CockpitKind::CombatCaliban){static const float c=Clearance(origin,CockpitKind::CombatCaliban);return c;}
        if(kind==CockpitKind::HeliNereid){static const float h=Clearance(origin,CockpitKind::HeliNereid);return h;}
        if(kind==CockpitKind::Heli602){static const float h=Clearance(origin,CockpitKind::Heli602);return h;}
        if(kind==CockpitKind::Heli506){static const float h=Clearance(origin,CockpitKind::Heli506);return h;}
        if(kind==CockpitKind::HeliBrute){static const float h=Clearance(origin,CockpitKind::HeliBrute);return h;}
        if(kind==CockpitKind::HeliBruteGunner){static const float g=Clearance(origin,CockpitKind::HeliBruteGunner);return g;}
        if(kind==CockpitKind::ProteusDriver){static const float d=Clearance(origin,CockpitKind::ProteusDriver);return d;}
        if(kind==CockpitKind::ProteusMissile){static const float m=Clearance(origin,CockpitKind::ProteusMissile);return m;}
        if(kind==CockpitKind::NixChest){static const float w=Clearance(origin,CockpitKind::NixChest);return w;}
        if(kind==CockpitKind::TruckPickup){static const float p=Clearance(origin,CockpitKind::TruckPickup);return p;}
        static const float n=Clearance(origin,CockpitKind::Nix);return n;}();
    if(length+radius+.002f<=neutralClearance)return desired;
    const auto direction=Mul(motion,1/length);float travelled=0;
    for(int i=0;i<32;++i){const float gap=(i?Clearance(Add(origin,Mul(direction,travelled)),kind):neutralClearance)-radius-.002f;
        if(gap<=.0002f)break;
        if(gap>=length-travelled)return desired;
        travelled+=gap*.95f;
    }
    return Add(origin,Mul(direction,travelled));
}
bool ConstrainCockpitCamera(const Matrix& cabin,Matrix& camera,float radius,CockpitKind kind,bool mirrored) noexcept {
    if(!ValidCamera(cabin)||!ValidCamera(camera))return false;
    const auto basis=CockpitLocalToWorld(cabin,mirrored);P local{};
    for(int k=0;k<3;++k)for(int j=0;j<3;++j)local[k]+=(camera.m[3][j]-cabin.m[3][j])*basis.m[k][j];
    local=ClampCockpitHead(local,radius,kind);
    for(int j=0;j<3;++j){camera.m[3][j]=cabin.m[3][j];for(int k=0;k<3;++k)camera.m[3][j]+=local[k]*basis.m[k][j];}
    return true;
}
}
