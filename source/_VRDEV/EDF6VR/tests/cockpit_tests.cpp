#include "cockpit.h"
#include "cockpit_combat_shells.h"
#include "cockpit_heli_shells.h"
#include "cockpit_draw.h"
#include "cockpit_lighting.h"
#include "vehicle_camera.h"
#include "crew_figures.h"
#include "native_world.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <limits>
#include <algorithm>
using namespace edf6vr;
using Microsoft::WRL::ComPtr;
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
Matrix Identity() {Matrix m{};for(int i=0;i<4;++i)m.m[i][i]=1;return m;}
Matrix Inverse(const Matrix& m) {Matrix v=Identity();for(int i=0;i<3;++i)for(int j=0;j<3;++j)v.m[i][j]=m.m[j][i];
    for(int j=0;j<3;++j)for(int k=0;k<3;++k)v.m[3][j]-=m.m[3][k]*v.m[k][j];return v;}
CockpitRig Rig() {CockpitRig r{};r.model=reinterpret_cast<void*>(1);r.nodes=reinterpret_cast<void*>(2);r.resource=reinterpret_cast<void*>(3);r.nodeCount=3;r.limb[1]=1;return r;}
using Point=std::array<float,3>;
Point Sub(Point a,Point b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Point Cross(Point a,Point b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float Dot(Point a,Point b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool MeshBlocked(const std::vector<CockpitVertex>& mesh,Point origin,Point target,float edgeTolerance=0) {
    // Native joints are metres across but their face triangles can be tiny.
    // Double intermediates avoid cancellation on long rays near a fan pole.
    auto sub=[](const auto& a,const auto& b){return std::array<double,3>{double(a[0])-b[0],double(a[1])-b[1],double(a[2])-b[2]};};
    auto cross=[](const auto& a,const auto& b){return std::array<double,3>{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    auto dot=[](const auto& a,const auto& b){return double(a[0])*b[0]+double(a[1])*b[1]+double(a[2])*b[2];};
    const auto direction=sub(target,origin);
    for(std::size_t t=0;t<mesh.size();t+=3) {
        if(mesh[t].surface==21||mesh[t].surface==24)continue; // screen ink and canopy glass are seen through
        Point p[3]{};for(int k=0;k<3;++k)std::memcpy(p[k].data(),mesh[t+k].position,12);
        const auto e1=sub(p[1],p[0]),e2=sub(p[2],p[0]),h=cross(direction,e2);const double det=dot(e1,h);if(std::fabs(det)<.000001)continue;
        const double inv=1/det;const auto s=sub(origin,p[0]);const double u=dot(s,h)*inv;if(u<-edgeTolerance||u>1+edgeTolerance)continue;
        const auto q=cross(s,e1);const double v=dot(direction,q)*inv;if(v<-edgeTolerance||u+v>1+edgeTolerance)continue;
        const double distance=dot(e2,q)*inv;if(distance>.0001&&distance<.9999)return true;
    }
    return false;
}
bool TriangleInBox(const CockpitVertex* triangle,Point lo,Point hi) {
    Point p[3]{},half{};for(int k=0;k<3;++k){half[k]=(hi[k]-lo[k])*.5f;
        for(int j=0;j<3;++j)p[j][k]=triangle[j].position[k]-(hi[k]+lo[k])*.5f;}
    const Point axes[]={{1,0,0},{0,1,0},{0,0,1}};
    const auto separate=[&](Point n){const float r=std::fabs(n[0])*half[0]+std::fabs(n[1])*half[1]+std::fabs(n[2])*half[2];
        const float a=Dot(n,p[0]),b=Dot(n,p[1]),c=Dot(n,p[2]);return std::min({a,b,c})>r||std::max({a,b,c})<-r;};
    for(const auto axis:axes)if(separate(axis))return false;
    for(int i=0;i<3;++i)for(const auto axis:axes)if(separate(Cross(Sub(p[(i+1)%3],p[i]),axis)))return false;
    return !separate(Cross(Sub(p[1],p[0]),Sub(p[2],p[0])));
}
bool Blocked(Point origin,Point target) {
    static const auto mesh=BuildNixCockpit();return MeshBlocked(mesh,origin,target);
}
void VisibilityTest() {
    CHECK(!Blocked({0,0,0},{0,0,20})); // front panoramic opening
    CHECK(!Blocked({0,0,0},{3,0,3})); // angled side opening
    // The cabin sits in the chest (origin Y=7.18): real Nix parts below are
    // their bind positions less that origin. The head rests on the chest right
    // over the screens, so the neck shroud runs forward to the screen arch.
    CHECK(!Blocked({0,0,0},{0,2,12})); // forward view under the neck shroud
    CHECK(Blocked({0,0,0},{0,1.01f,.73f})); // head hidden when seated normally
    CHECK(!Blocked({0,0,.26f},{0,1.01f,.73f})); // only its leading tip after leaning
    CHECK(Blocked({0,0,.26f},{0,.56f,.12f})); // neck stays behind the solid cowl
    for(float x:{-.60f,0.f,.60f})for(float y:{-.5f,.10f,.5f})for(float z:{.60f,.86f}) {
        const auto eye=ClampCockpitHead({x,y,z});
        const bool hidden=Blocked(eye,{0,.56f,.12f});
        if(!hidden)std::printf("Exposed neck: eye %.3f %.3f %.3f\n",eye[0],eye[1],eye[2]);
        CHECK(hidden); // full forward lean still hides the neck connection
    }
    for(float s:{-1.f,1.f}) {
        CHECK(Blocked({0,0,0},{s*1.816f,.108f,-.15f})); // actual upper-arm attachment
        CHECK(Blocked({s*.12f,.08f,.12f},{s*1.816f,.108f,-.15f})); // modest lean
        CHECK(!Blocked({0,0,0},{s*3,0,2})); // forward side view of moving arms
        CHECK(!Blocked({0,0,0},{s*3,2,1.5f})); // upper side window
        for(float x:{-.60f,0.f,.60f})for(float y:{-.5f,.10f,.5f})for(float z:{-.4f,0.f,.3f,.6f,.86f}) {
            const auto head=ClampCockpitHead({x,y,z});
            for(float ry:{-.142f,.293f})for(float rz:{-.45f,-.28f}) {
                const bool hidden=Blocked(head,{s*1.20f,ry,rz});
                if(!hidden)std::printf("Exposed torso connection: eye %.3f %.3f %.3f -> %.2f %.3f %.2f\n",head[0],head[1],head[2],s*1.20f,ry,rz);
                CHECK(hidden); // torso-side rear connection; outer shoulder may show
            }
        }
    }
    CHECK(Blocked({0,0,0},{0,0,-2})); // rear bulkhead hides internal connections
    // The head support should be just behind the seated head, not 32cm above
    // the eyes. Its upper edge must also leave normal upward head motion free.
    CHECK(Blocked({0,0,0},{0,0,-.40f})); // head pad after reclining about the hip
    CHECK(!Blocked({0,.20f,0},{0,.20f,-.40f}));
    CHECK(Blocked({0,-.58f,.10f},{0,-.72f,.10f})); // cushion at seated hip height
    for(float s:{-1.f,1.f}) {
        CHECK(Blocked({s*.414f,0,0},{s*.414f,0,-.49f})); // compact rear machinery beside the seat
        CHECK(!Blocked({s*.21f,-.45f,.135f},{s*.31f,-.45f,.135f})); // fingers enter below the free lever end
        // The fore/aft lever guide must have depth, not a black strip over a
        // solid cap. The open stretch behind its carriage (at the front of its
        // travel) clears the deck height and then reaches the bottom of the
        // recessed channel.
        CHECK(!Blocked({s*.417f,-.50f,.080f},{s*.417f,-.575f,.080f}));
        CHECK(Blocked({s*.417f,-.575f,.080f},{s*.417f,-.65f,.080f}));
    }
    for(float x:{-.60f,0.f,.60f})for(float y:{-.5f,.10f,.5f}) {
        const auto eye=ClampCockpitHead({x,y,-.4f});
        for(float bx:{-2.f,-1.f,0.f,1.f,2.f})for(float by:{-3.f,-2.f,-.8f,0.f,.8f})
            CHECK(Blocked(eye,{bx,by,-2})); // no open seams behind the bucket surround
    }
    for(float s:{-1.f,1.f}) {
        // The old floor ended before the rear shell, exposing the ground in
        // the downward gap beside the seat. Probe both bottom corner seams.
        for(float z:{-.50f,-.65f,-.80f})CHECK(Blocked({s*.25f,-.15f,-.15f},{s*.56f,-1.5f,z}));
        CHECK(!Blocked({0,0,0},{s*3,0,1.5f})); // wider side opening after moving pods aft
    }
    CHECK(!Blocked({0,0,.28f},{0,-1.12f,.5f})); // removed lower-window central bar
    for(float s:{-1.f,1.f}) {
        // Approach clears the raised 30-degree tread; the final foot motion
        // meets the tread itself, not a panel or an unrelated support beam.
        CHECK(!Blocked({s*.26f,-.65f,.45f},{s*.26f,-.95f,.64f}));
        CHECK(Blocked({s*.26f,-.95f,.64f},{s*.26f,-1.035f,.64f}));
    }
    CHECK(Blocked({0,0,0},{1.3f,-1.887f,0})); // hip connection behind console
    CHECK(!Blocked({0,0,.28f},{1.337f,-6.7428f,.0496f})); // real Nix toe, modest forward lean
    CHECK(!Blocked({0,0,.28f},{-1.337f,-6.7428f,.0496f}));
}
void ClearanceTest() {
    constexpr auto origin=kCockpitSeatedEye;
    const auto neutral=ClampCockpitHead(origin);CHECK(neutral==origin);
    const auto lean=ClampCockpitHead({0,0,.26f});CHECK(std::fabs(lean[2]-.26f)<.0001f);
    const auto fullLean=ClampCockpitHead({0,0,.60f});CHECK(std::fabs(fullLean[2]-.60f)<.0001f);
    const auto displays=BuildCockpitDisplays();float front=0;
    for(const auto& v:displays)front=std::max(front,v.position[2]);
    for(float x:{-2.f,0.f,2.f})for(float y:{-2.f,0.f,2.f})for(float z:{-2.f,0.f,2.f}) {
        const auto p=ClampCockpitHead({x,y,z});
        CHECK(std::fabs(p[0])<=.579f&&p[1]>=-1.009f&&p[1]<=.649f&&p[2]>=-.459f&&p[2]+.101f<=front);
        CHECK(!Blocked(origin,p)); // no teleport through thin solids
        CHECK(!MeshBlocked(displays,origin,p));
        // Probe a sphere, including oblique directions toward the canted panes.
        for(float dx:{-1.f,0.f,1.f})for(float dy:{-1.f,0.f,1.f})for(float dz:{-1.f,0.f,1.f}) {
            const float length=std::sqrt(dx*dx+dy*dy+dz*dz);if(length==0)continue;
            const Point q{p[0]+dx*.099f/length,p[1]+dy*.099f/length,p[2]+dz*.099f/length};
            CHECK(!Blocked(p,q));CHECK(!MeshBlocked(displays,p,q));
        }
    }
    // Lean down to the radar: the centre of the glass is transparent visually,
    // but it must stop the head before either eye passes through it.
    for(float radius:{.06f,.10f,.18f}) {
        const auto p=ClampCockpitHead({0,-.30f,2},radius);
        const float gap=Dot(Sub(Point{0,-.39888f,.90616f},p),Point{0,-.28f,.96f});
        if(!(p[2]>.60f&&gap>=radius+.001f&&gap<radius+.008f))std::printf("radar lean r=%.2f -> %.3f %.3f %.3f gap %.4f\n",radius,p[0],p[1],p[2],gap);
        CHECK(p[2]>.60f&&gap>=radius+.001f&&gap<radius+.008f);
        CHECK(MeshBlocked(displays,p,{0,-.30f,1.1f}));
        const auto beyond=ClampCockpitHead({0,0,2},radius);
        CHECK(beyond[2]+radius<=front-.001f);
    }
    for(float side:{-1.f,1.f}) {
        const Point desired{side*.45f,.10f,-.10f};const auto p=ClampCockpitHead(desired);
        CHECK(std::fabs(p[0]-desired[0])<.001f); // beyond the old 32cm stop
    }
    const auto down=ClampCockpitHead({0,-.45f,.12f});CHECK(std::fabs(down[1]+.45f)<.001f);
    const auto highForward=ClampCockpitHead({0,.54f,.40f});CHECK(highForward[1]>.30f); // up to the neck shroud, under the chest top
    Matrix cabin{},camera{};CHECK(RotateCamera(Identity(),35,20,cabin));cabin.m[3][0]=100;cabin.m[3][1]=20;
    const auto seated=CockpitSeatedCamera(cabin);
    for(int j=0;j<3;++j)CHECK(std::fabs(seated.m[3][j]-cabin.m[3][j]-.10f*cabin.m[1][j]+.10f*cabin.m[2][j])<.0001f);
    camera=seated;CHECK(ConstrainCockpitCamera(cabin,camera));
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)CHECK(std::fabs(camera.m[i][j]-seated.m[i][j])<.0001f);
    camera=seated;for(int j=0;j<3;++j)camera.m[3][j]+=cabin.m[0][j]*2;
    CHECK(ConstrainCockpitCamera(cabin,camera));
    for(int i=0;i<3;++i)for(int j=0;j<4;++j)CHECK(camera.m[i][j]==cabin.m[i][j]);
    float distance=0;for(int j=0;j<3;++j)distance+=std::pow(camera.m[3][j]-seated.m[3][j],2.f);CHECK(distance>.40f*.40f&&distance<.59f*.59f);
    camera=seated;for(int j=0;j<3;++j)camera.m[3][j]+=cabin.m[2][j]*.60f;
    CHECK(ConstrainCockpitCamera(cabin,camera));
    for(int j=0;j<3;++j)CHECK(std::fabs(camera.m[3][j]-seated.m[3][j]-cabin.m[2][j]*.60f)<.0001f);
    CHECK(ClampCockpitHead({std::numeric_limits<float>::quiet_NaN(),0,0})==origin);
}
// Nothing runs through a live screen's glass: lines across the glass 1 cm
// apart, 1 cm inside its octagonal rim, meet no solid part (the user saw
// parts through the Nix's screens after its lower half was pressed into the
// chest). seen: the seated eye sees the whole glass, nothing solid in front.
void ScreenClearTest(CockpitKind kind,bool seen) {
    const auto mesh=BuildCockpit(kind);const auto displays=BuildCockpitDisplays(kind);
    for(std::size_t k=0;k+5<displays.size();k+=6) {
        Point c[4]{};const std::size_t corner[4]={0,1,2,5};
        for(int i=0;i<4;++i)std::memcpy(c[i].data(),displays[k+corner[i]].position,12);
        const float width=std::sqrt(Dot(Sub(c[1],c[0]),Sub(c[1],c[0]))),height=std::sqrt(Dot(Sub(c[3],c[0]),Sub(c[3],c[0])));
        auto at=[&](float x,float y){Point p{};for(int j=0;j<3;++j)p[j]=(c[0][j]+c[2][j])*.5f+(c[1][j]-c[0][j])*x/width+(c[3][j]-c[0][j])*y/height;return p;};
        // The rim's octagon (Instrument): |x|/W+|y|/H <= .90 on the panel's
        // own size, 5 mm round the glass.
        const float W=width+.01f,H=height+.01f,m=.01f;
        auto reach=[&](float across,float half,float a,float b){return std::min(half-m,a*(.90f-std::fabs(across)/b-m/W-m/H));};
        unsigned crossed=0,hidden=0;
        for(float y=-height/2+m;y<=height/2-m;y+=.01f){const float x=reach(y,width/2,W,H);if(x>0)crossed+=MeshBlocked(mesh,at(-x,y),at(x,y));}
        for(float x=-width/2+m;x<=width/2-m;x+=.01f){const float y=reach(x,height/2,H,W);if(y>0)crossed+=MeshBlocked(mesh,at(x,-y),at(x,y));}
        for(int i=0;i<13;++i)for(int j=0;j<13;++j)hidden+=MeshBlocked(mesh,kCockpitSeatedEye,at(width*(.44f*i/6-.44f),height*(.44f*j/6-.44f)));
        if(crossed||(seen&&hidden))std::printf("Screen %zu of cabin %u: %u lines crossed, %u of 169 hidden\n",k/6,unsigned(kind),crossed,hidden);
        CHECK(!crossed);if(seen)CHECK(!hidden);
    }
}
// The Combat Wagon's cabin: the Nix's with its lower half kept in the chest,
// which sits on the truck's mount.
void NixChestTest(bool baked=true) {
    auto mesh=BuildCockpit(CockpitKind::NixChest);CHECK(mesh.size()>1000&&mesh.size()%3==0);
    std::printf("Wagon cabin geometry: %zu triangles\n",mesh.size()/3);
    if(baked)CHECK(ApplyCockpitOcclusion(mesh,CockpitKind::NixChest));
    CHECK(BuildCockpitDisplays(CockpitKind::NixChest).size()==24);
    for(float s:{-1.f,1.f}) {
        // Its pedals ride on carriers raised to the chest's deck.
        CHECK(!MeshBlocked(mesh,{s*.22f,-.40f,.45f},{s*.22f,-.70f,.64f}));
        CHECK(MeshBlocked(mesh,{s*.22f,-.70f,.64f},{s*.22f,-.785f,.64f}));
    }
    // Its lower display sits under the radar, which hides the display's top.
    ScreenClearTest(CockpitKind::NixChest,false);
}
void HistoryTest() {
    CockpitHistory h;CockpitPose p{};p.camera=p.cabin=Identity();p.camera.m[3][0]=.032f;p.rig=Rig();p.at=1000;h.Record(p);
    auto newer=p;newer.at=1010;newer.cabin.m[3][2]=10;newer.camera.m[3][2]=10;h.Record(newer);
    CockpitPose found{};CHECK(h.Match(Inverse(p.camera),1015,found)&&found.at==1000&&found.cabin.m[3][2]==0);
    CHECK(h.Match(Inverse(newer.camera),1015,found)&&found.at==1010);
    {
        CockpitHistory animated;auto jump=p;jump.camera.m[0][0]=1.0001f;jump.camera.m[3][0]=450;
        animated.Record(jump);Matrix exactView=Identity();exactView.m[0][0]=1/jump.camera.m[0][0];
        exactView.m[3][0]=-450/jump.camera.m[0][0];
        CHECK(animated.Match(exactView,1015,found)&&found.camera.m[3][0]==450);
    }
    CHECK(!h.Match(Inverse(p.camera),1251,found));CHECK(!h.Match(Inverse(p.camera),900,found));
    auto wrong=p.camera;wrong.m[3][0]=-.032f;CHECK(!h.Match(Inverse(wrong),1015,found));
    wrong=p.camera;wrong.m[0][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!h.Match(wrong,1015,found));
    h.Reset();CHECK(!h.Match(Inverse(p.camera),1015,found));
    for(unsigned i=0;i<150;++i) {p.at=2000+i;p.camera.m[3][2]=float(i);h.Record(p);}
    CHECK(h.Match(Inverse(p.camera),2150,found)&&found.at==2149);
    ConfigureCockpit(true);p.at=GetTickCount64();PublishCockpit(p);CHECK(MatchCockpit(Inverse(p.camera),found));
    ConfigureCockpit(false);CHECK(!MatchCockpit(Inverse(p.camera),found));ConfigureCockpit(true);CHECK(!MatchCockpit(Inverse(p.camera),found));ConfigureCockpit(false);
}
struct SkinVertex {float position[3];float weights[4];unsigned char bones[4];};
void SelectTest() {
    SkinVertex v[7]{};for(auto& x:v)x.weights[0]=1;
    for(unsigned i=4;i<7;++i)v[i].bones[0]=1;
    std::uint16_t idx[]={0,1,2,3,4,5};std::vector<std::uint32_t> out;
    auto rig=Rig();auto r=SelectCockpitLimbs(reinterpret_cast<unsigned char*>(v),sizeof(v),sizeof(SkinVertex),idx,6,false,1,rig,out);
    CHECK(r.valid&&r.kept==3&&r.removed==3&&out[0]==3&&out[2]==5);
    std::uint32_t wide[]={1,2,3,4,5,6};r=SelectCockpitLimbs(reinterpret_cast<unsigned char*>(v),sizeof(v),sizeof(SkinVertex),wide,6,true,0,rig,out);
    CHECK(r.valid&&r.kept==3&&out[0]==4);
    v[4].weights[0]=.6f;v[4].weights[1]=.4f;v[4].bones[1]=0;
    r=SelectCockpitLimbs(reinterpret_cast<unsigned char*>(v),sizeof(v),sizeof(SkinVertex),wide,6,true,0,rig,out);CHECK(r.valid&&r.kept==3);
    v[4].weights[0]=.4f;v[4].weights[1]=.6f;
    r=SelectCockpitLimbs(reinterpret_cast<unsigned char*>(v),sizeof(v),sizeof(SkinVertex),wide,6,true,0,rig,out);CHECK(r.valid&&r.kept==0);
    v[4].weights[0]=std::numeric_limits<float>::quiet_NaN();
    r=SelectCockpitLimbs(reinterpret_cast<unsigned char*>(v),sizeof(v),sizeof(SkinVertex),wide,6,true,0,rig,out);CHECK(!r.valid&&out.empty());
    v[4].weights[0]=.4f;v[4].bones[0]=250;
    r=SelectCockpitLimbs(reinterpret_cast<unsigned char*>(v),sizeof(v),sizeof(SkinVertex),wide,6,true,0,rig,out);CHECK(!r.valid&&out.empty());
    r=SelectCockpitLimbs(reinterpret_cast<unsigned char*>(v),sizeof(v),sizeof(SkinVertex),wide,6,true,50,rig,out);CHECK(!r.valid&&out.empty());
}
unsigned char* nodes=nullptr;bool alternate=false,unknown=false,crawlerMock=false,goldMock=false,wagonMock=false,gravisMock=false;
// Combat Frame Gravis: its chest (mune), the belly under the cabin, the rest kept.
const wchar_t* gravisBones[]={L"v608_oldrobot",L"mune",L"hara",L"koshi",L"spine",L"momo_l",L"sune_l",L"foot_l",L"heel_l",L"toe_l",
    L"foot_roll_l",L"momo_r",L"sune_r",L"foot_r",L"heel_r",L"toe_r",L"foot_roll_r",L"arm_l",L"arm_slide_l",L"arm_r",L"arm_slide_r",
    L"booster_l",L"booster_r",L"head",L"eye"};
// Combat Wagon: the truck, its wheels and the Nix chest on its bed (robo_*).
const wchar_t* wagonBones[]={L"v607_robotruck",L"robo_body",L"body",L"tireB0_l",L"tireB0_r",L"tireB1_l",L"tireB1_r",L"tireF_l",L"tireF_r",
    L"robo_head",L"robo_jowan_l",L"robo_kawan_l",L"robo_kata_r",L"robo_weaponShoulder_r",L"robo_spine",L"robo_body_burner_l",
    L"robo_body_burner_r",L"robo_kata_l"};
const wchar_t* boneNames[]={L"v504_begaruta",L"body",L"jowan_l",L"kawan_l",L"jowan_r",L"kawan_r",L"momo_l",L"sune0_l",L"sune1_l",L"ashi_l",L"heel_l",L"toe_l",L"footPanel_l",L"momo_r",L"sune0_r",L"sune1_r",L"ashi_r",L"heel_r",L"toe_r",L"footPanel_r"};
const wchar_t* crawlerBones[]={L"mdl",L"globalSRT",L"aim_center",L"body",L"gatling_joint",
    L"leg_b0_l",L"leg_b1_l",L"leg_b2_l",L"leg_b3_l",L"heel_b_l",L"leg_b0_r",L"leg_b1_r",L"leg_b2_r",L"leg_b3_r",L"heel_b_r",
    L"leg_f0_l",L"leg_f1_l",L"leg_f2_l",L"leg_f3_l",L"heel_f_l",L"leg_f0_r",L"leg_f1_r",L"leg_f2_r",L"leg_f3_r",L"heel_f_r",
    L"light",L"weapon_node_l",L"weapon_joint_l",L"weapon_node_r",L"weapon_joint_r",L"groundrobo"};
void* __fastcall Lookup(void*,const void* name) {
    const auto* bytes=static_cast<const unsigned char*>(name);const auto n=*reinterpret_cast<const std::uint64_t*>(bytes+16);
    const auto* text=n<=7?reinterpret_cast<const wchar_t*>(bytes):*reinterpret_cast<const wchar_t* const*>(bytes);
    if(unknown)return nullptr;
    if(crawlerMock) {for(unsigned i=0;i<std::size(crawlerBones);++i)if(!std::wcscmp(text,crawlerBones[i]))return nodes+(i+(goldMock?1:0))*0x110;return nullptr;}
    if(wagonMock) {for(unsigned i=0;i<std::size(wagonBones);++i)if(!std::wcscmp(text,wagonBones[i]))return nodes+i*0x110;return nullptr;}
    if(gravisMock) {for(unsigned i=0;i<std::size(gravisBones);++i)if(!std::wcscmp(text,gravisBones[i]))return nodes+i*0x110;return nullptr;}
    if(!std::wcscmp(text,L"v612_nix"))return alternate?nodes:nullptr;
    if(alternate&&!std::wcscmp(text,L"v504_begaruta"))return nullptr;
    for(unsigned i=0;i<sizeof(boneNames)/sizeof(boneNames[0]);++i)if(!std::wcscmp(text,boneNames[i]))return nodes+i*0x110;
    return nullptr;
}
template<class T>T& At(void* p,unsigned o) {return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+o);}
void VehicleTest(const wchar_t* dll) {
    auto module=LoadLibraryExW(dll,nullptr,DONT_RESOLVE_DLL_REFERENCES);ImageProfile image{};char reason[256]{};
    CHECK(module&&CheckImage(module,image,reason,sizeof(reason)));if(!image.base)return;
    alignas(16) unsigned char vehicle[0x1500]{},boneBytes[32*0x110]{},resource[16]{},seats[2*0x340]{};
    nodes=boneBytes;At<void*>(vehicle,0xE40)=image.base+kModelVtableRva;At<void*>(vehicle,0xEE0)=resource;
    At<void*>(vehicle,0xEF0)=nodes;At<std::uint64_t>(vehicle,0xF00)=32;
    At<void*>(vehicle,0x608)=seats;At<std::uint64_t>(vehicle,0x618)=2; // seat array and its count
    Matrix body=Identity();body.m[3][1]=6.31278f;At<Matrix>(nodes+0x110,0xB0)=body;
    VehicleSeat seat{};seat.vehicle=vehicle;seat.cameraOwner=vehicle;seat.seat=seats;
    auto before=std::vector<unsigned char>(vehicle,vehicle+sizeof(vehicle));auto boneBefore=std::vector<unsigned char>(boneBytes,boneBytes+sizeof(boneBytes));
    CockpitRig rig{};Matrix cabin{};CHECK(PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));
    CHECK(std::fabs(cabin.m[3][1]-7.17978f)<.001f&&std::fabs(cabin.m[3][2]-.15f)<.001f);
    CHECK(cabin.m[3][1]>6.363f&&cabin.m[3][1]<8.453f); // torso, not former head camera
    CHECK(rig.limb[2]&&rig.limb[6]&&!rig.limb[1]);CHECK(ValidateCockpitModel(image,vehicle+0xE40,rig));
    CHECK(!ValidateCockpitModel(image,vehicle+0xE48,rig));At<void*>(vehicle,0xEF0)=nullptr;CHECK(!ValidateCockpitModel(image,vehicle+0xE40,rig));At<void*>(vehicle,0xEF0)=nodes;
    CHECK(!std::memcmp(before.data(),vehicle,sizeof(vehicle))&&!std::memcmp(boneBefore.data(),boneBytes,sizeof(boneBytes)));
    const auto original=cabin;seat.seat=seats+0x340;CHECK(!PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));CHECK(!std::memcmp(&original,&cabin,sizeof(cabin)));seat.seat=seats;
    seat.cameraOwner=resource;CHECK(!PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));seat.cameraOwner=vehicle;
    unknown=true;CHECK(!PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));unknown=false;
    alternate=true;const float axes[3][4]={{0,1,0,0},{0,0,-1,0},{-1,0,0,0}};std::memcpy(body.m,axes,sizeof(axes));At<Matrix>(nodes+0x110,0xB0)=body;
    CHECK(PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));for(int i=0;i<4;++i)for(int j=0;j<4;++j)CHECK(std::fabs(cabin.m[i][j]-original.m[i][j])<.0001f);
    alternate=false;crawlerMock=true;
    for(bool gold:{false,true}) {
        goldMock=gold;At<std::uint64_t>(vehicle,0xF00)=gold?32:31;
        body=Identity();body.m[3][1]=1.28708f;const unsigned b=gold?4:3;At<Matrix>(nodes+b*0x110,0xB0)=body;
        CHECK(PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));CHECK(rig.kind==CockpitKind::Crawler);
        CHECK(std::fabs(cabin.m[3][1]-2.45f)<.0001f&&std::fabs(cabin.m[3][2]-.05f)<.0001f);
        CHECK(rig.bodyBone==b&&!rig.limb[b]&&rig.limb[gold?6:5]&&rig.limb[gold?16:15]&&rig.limb[gold?30:29]);
        CHECK(!rig.limb[gold?26:25]); // light unit is hidden only in the cockpit
        const auto savedRig=rig;const auto savedCabin=cabin;
        seat.seat=seats+0x340;CHECK(!PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));seat.seat=seats;
        CHECK(!std::memcmp(&savedRig,&rig,sizeof(rig))&&!std::memcmp(&savedCabin,&cabin,sizeof(cabin)));
        // Wall/ceiling poses preserve all three body axes, including roll.
        for(float angle:{90.f,180.f}) {
            const float r=angle*3.14159265f/180;body=Identity();body.m[1][1]=body.m[2][2]=std::cos(r);body.m[1][2]=std::sin(r);body.m[2][1]=-std::sin(r);
            At<Matrix>(nodes+b*0x110,0xB0)=body;CHECK(PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));
            for(int k=0;k<3;++k)for(int j=0;j<3;++j)CHECK(cabin.m[k][j]==body.m[k][j]);
            auto eye=CockpitSeatedCamera(cabin);CHECK(ConstrainCockpitCamera(cabin,eye,.10f,rig.kind));CHECK(ValidCamera(eye));
        }
    }
    crawlerMock=goldMock=false;At<std::uint64_t>(vehicle,0xF00)=32;body=Identity();body.m[3][1]=6.31278f;At<Matrix>(nodes+0x110,0xB0)=body;
    CHECK(PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));CHECK(rig.kind==CockpitKind::Nix&&!rig.limb[25]);
    // The Combat Wagon carries the same Nix chest on its truck: the cabin
    // follows robo_body (bound like the V612's chest) at the Nix's offset, the
    // chest and its joints go, the truck, wheels, head and arms stay.
    wagonMock=true;
    {
        const float chestAxes[3][4]={{0,1,0,0},{0,0,-1,0},{-1,0,0,0}};Matrix chest=Identity();std::memcpy(chest.m,chestAxes,sizeof(chestAxes));
        chest.m[3][1]=3.693f;chest.m[3][2]=-1.7879f;At<Matrix>(nodes+0x110,0xB0)=chest;
        rig={};   // a different machine: its own model, so a fresh selection
        // Its chest sits on the truck's mount: the cabin with its lower half in the chest.
        CHECK(PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));CHECK(rig.kind==CockpitKind::NixChest);
        CHECK(std::fabs(cabin.m[3][1]-4.56f)<.001f&&std::fabs(cabin.m[3][2]+1.6379f)<.001f);
        for(int k=0;k<3;++k)for(int j=0;j<3;++j)CHECK(std::fabs(cabin.m[k][j]-(k==j?1.f:0.f))<.0001f);
        CHECK(!rig.limb[1]&&!rig.limb[14]&&!rig.limb[15]&&!rig.limb[16]&&!rig.limb[17]);
        for(unsigned i=2;i<14;++i)CHECK(rig.limb[i]);
    }
    wagonMock=false;
    // The Gravis: the Nix's cabin in its chest, the deck through the hidden
    // belly and spine, over the waist.
    gravisMock=true;
    {
        const float chestAxes[3][4]={{0,1,0,0},{0,0,-1,0},{-1,0,0,0}};Matrix chest=Identity();std::memcpy(chest.m,chestAxes,sizeof(chestAxes));
        chest.m[3][1]=7.2408f;At<Matrix>(nodes+0x110,0xB0)=chest;
        rig={};CHECK(PlaceVehicleCockpit(image,seat,Lookup,cabin,rig));CHECK(rig.kind==CockpitKind::Nix);
        CHECK(std::fabs(cabin.m[3][1]-7.58f)<.001f&&std::fabs(cabin.m[3][2]-.10f)<.001f);
        for(int k=0;k<3;++k)for(int j=0;j<3;++j)CHECK(std::fabs(cabin.m[k][j]-(k==j?1.f:0.f))<.0001f);
        CHECK(!rig.limb[1]&&!rig.limb[2]&&!rig.limb[4]);   // chest, belly, spine
        for(unsigned i=3;i<std::size(gravisBones);++i)if(i!=4)CHECK(rig.limb[i]);
    }
    gravisMock=false;
    FreeLibrary(module);
}
std::vector<unsigned char> ReadPixels(ID3D11Device* d,ID3D11DeviceContext* ctx,ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&staging)));ctx->CopyResource(staging.Get(),texture);
    D3D11_MAPPED_SUBRESOURCE map{};CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    std::vector<unsigned char> pixels(desc.Width*desc.Height*4);
    if(map.pData){for(unsigned y=0;y<desc.Height;++y)std::memcpy(pixels.data()+y*desc.Width*4,static_cast<unsigned char*>(map.pData)+y*map.RowPitch,desc.Width*4);ctx->Unmap(staging.Get(),0);}return pixels;
}
void Bitmap(const std::filesystem::path& path,const std::vector<unsigned char>& rgba,unsigned w,unsigned h) {
    BITMAPFILEHEADER file{};file.bfType=0x4D42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+w*h*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=static_cast<LONG>(w);info.biHeight=-static_cast<LONG>(h);info.biPlanes=1;info.biBitCount=32;
    auto bgra=rgba;for(std::size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(&file),sizeof(file));f.write(reinterpret_cast<const char*>(&info),sizeof(info));f.write(reinterpret_cast<const char*>(bgra.data()),bgra.size());CHECK(bool(f));
}
void ExportMesh(const std::filesystem::path& folder,CockpitKind kind=CockpitKind::Nix) {
    std::filesystem::create_directories(folder);const auto mesh=BuildCockpit(kind);std::ofstream f(folder/L"nix_cockpit.obj");
    std::ofstream binary(folder/L"nix_cockpit_vertices.bin",std::ios::binary);
    binary.write(reinterpret_cast<const char*>(mesh.data()),mesh.size()*sizeof(CockpitVertex));CHECK(bool(binary));
    if(kind==CockpitKind::Barga)for(unsigned variant=0;variant<2;++variant)for(unsigned part=0;part<5;++part) {
        const auto cap=BuildBargaJointCap(variant,part);
        std::ofstream output(folder/("joint_cap_"+std::to_string(variant)+"_"+std::to_string(part)+".bin"),std::ios::binary);
        output.write(reinterpret_cast<const char*>(cap.data()),cap.size()*sizeof(CockpitVertex));CHECK(bool(output));
    }
    f<<"# EDF6VR cockpit with adapted Quaternius Column_Slim; see reference_parts.json; metres; +X right +Y up +Z forward; origin seated eyes\n";
    for(const auto& v:mesh)f<<"v "<<v.position[0]<<' '<<v.position[1]<<' '<<v.position[2]<<' '<<v.colour[0]<<' '<<v.colour[1]<<' '<<v.colour[2]<<'\n';
    for(const auto& v:mesh)f<<"vn "<<v.normal[0]<<' '<<v.normal[1]<<' '<<v.normal[2]<<'\n';
    for(const auto& v:mesh)f<<"vt "<<v.uv[0]<<' '<<1-v.uv[1]<<'\n';
    for(std::size_t i=0;i<mesh.size();i+=3) {
        f<<"usemtl tile_"<<unsigned(mesh[i].surface)<<'\n';
        f<<"f ";for(std::size_t k=i;k<i+3;++k)f<<k+1<<'/'<<k+1<<'/'<<k+1<<' ';f<<'\n';
    }CHECK(bool(f));
    const auto panes=BuildCockpitDisplays(kind);std::ofstream panels(folder/L"live_panels.json");
    panels<<"[";for(std::size_t k=0;k<panes.size();k+=6){if(k)panels<<',';panels<<"[";
        for(int c:{0,1,2,5}){if(c)panels<<',';const auto& p=panes[k+c];panels<<'['<<p.position[0]<<','<<p.position[2]<<','<<p.position[1]<<']';}panels<<"]";}panels<<"]";CHECK(bool(panels));
}
void FixtureTest(const std::filesystem::path& folder,bool crawler=false,bool barga=false,bool proteus=false) {
    unsigned fixtures=0,keptTotal=0,removedTotal=0;
    for(const auto& file:std::filesystem::directory_iterator(folder)) {
        if(file.path().extension()!=L".nixfixture")continue;
        std::ifstream f(file.path(),std::ios::binary);char magic[4]{};std::uint32_t header[6]{};f.read(magic,4);f.read(reinterpret_cast<char*>(header),sizeof(header));
        CHECK(!std::memcmp(magic,"NIX1",4));const auto stride=header[0],vc=header[1],ic=header[2];
        CHECK(stride<256&&vc<1000000&&ic<3000000&&header[3]<=256);if(stride>=256||vc>=1000000||ic>=3000000)return;
        auto rig=Rig();rig.nodeCount=header[3];f.read(reinterpret_cast<char*>(rig.limb.data()),256);
        if(crawler){rig.kind=CockpitKind::Crawler;rig.bodyBone=file.path().filename().string().find("gold")!=std::string::npos?4u:3u;}
        if(barga)rig.kind=CockpitKind::Barga;
        if(proteus) {
            // The one bone the mask drops is the rider's own pod; the gun bay
            // above it is kept by position, so the selector needs its index.
            rig.kind=CockpitKind::ProteusGunner;
            for(unsigned i=0;i<rig.nodeCount;++i)if(!rig.limb[i]){rig.bodyBone=i;break;}
        }
        std::vector<unsigned char> verticesBytes(static_cast<std::size_t>(stride)*vc);std::vector<std::uint16_t> indices(ic);
        f.read(reinterpret_cast<char*>(verticesBytes.data()),verticesBytes.size());f.read(reinterpret_cast<char*>(indices.data()),indices.size()*2);CHECK(bool(f));
        const auto unchanged=verticesBytes;std::vector<std::uint32_t> selected;
        const auto result=SelectCockpitLimbs(verticesBytes.data(),verticesBytes.size(),stride,indices.data(),ic,false,0,rig,selected);
        CHECK(result.valid&&result.kept==header[4]&&result.removed==header[5]&&result.kept+result.removed==ic);CHECK(verticesBytes==unchanged);
        if(crawler||barga||proteus) {
            auto expectedPath=file.path();expectedPath.replace_extension(L".visible");
            std::ifstream expectedFile(expectedPath,std::ios::binary);
            std::vector<std::uint32_t> expected(header[4]);
            expectedFile.read(reinterpret_cast<char*>(expected.data()),expected.size()*4);
            CHECK(bool(expectedFile)&&selected==expected); // every triangle, not just counts
            // Reclassifying exactly the same vertices as Nix must not expose
            // its body. Only the two main Crawler meshes gain 3,080 base faces.
            if(crawler) {
            const auto expectedBase=file.path().stem().string().find("_0_0")!=std::string::npos?3080u:0u;
            auto other=rig;other.kind=CockpitKind::Nix;
            const auto withoutBase=SelectCockpitLimbs(verticesBytes.data(),verticesBytes.size(),stride,indices.data(),ic,false,0,other,selected);
            CHECK(withoutBase.valid&&result.kept==withoutBase.kept+expectedBase*3);
            }
            // The selector works with a shared vertex buffer/baseVertex and
            // 32-bit index buffers as well as the archive's uint16 indices.
            auto padded=verticesBytes;padded.insert(padded.begin(),stride,0);
            std::vector<std::uint32_t> wide(indices.begin(),indices.end());
            const auto shifted=SelectCockpitLimbs(padded.data(),padded.size(),stride,wide.data(),ic,true,1,rig,selected);
            CHECK(shifted.valid&&shifted.kept==result.kept&&shifted.removed==result.removed);
        }
        keptTotal+=result.kept/3;removedTotal+=result.removed/3;++fixtures;
    }
    CHECK(proteus?(fixtures==6&&keptTotal==34588&&removedTotal==2118):
        barga?(fixtures==16&&keptTotal==223956&&removedTotal==55016):
        crawler?(fixtures==11&&keptTotal==15040&&removedTotal==10459):(fixtures==5&&keptTotal>25000&&removedTotal>15000));
    std::printf("Actual %s fixture meshes=%u kept=%u removed=%u triangles\n",
        proteus?"Proteus":barga?"Barga":crawler?"Crawler":"Nix",fixtures,keptTotal,removedTotal);
}
void CrawlerLayoutTest(bool baked=true) {
    const auto kind=CockpitKind::Crawler;auto mesh=BuildCrawlerCockpit();
    std::printf("Crawler geometry: %zu triangles\n",mesh.size()/3);
    CHECK(mesh.size()>1000&&mesh.size()%3==0);if(baked)CHECK(ApplyCockpitOcclusion(mesh,kind));
    for(const auto& v:mesh) {
        float n=0;for(int k=0;k<3;++k){CHECK(std::isfinite(v.position[k]));n+=v.normal[k]*v.normal[k];}
        CHECK(std::fabs(n-1)<.001f);CHECK(std::fabs(v.position[0])<.60f);CHECK(v.position[1]<.47f);
    }
    for(std::size_t i=0;i<mesh.size();i+=3) {
        Point a{},b{},c{},n{};std::memcpy(a.data(),mesh[i].position,12);std::memcpy(b.data(),mesh[i+1].position,12);std::memcpy(c.data(),mesh[i+2].position,12);std::memcpy(n.data(),mesh[i].normal,12);
        const auto normal=Cross(Sub(b,a),Sub(c,a));CHECK(Dot(normal,normal)>1e-18f&&Dot(normal,n)>0);
    }
    const auto eye=kCockpitSeatedEye;
    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
        auto desired=eye;desired[axis]+=sign*2;const auto stopped=ClampCockpitHead(desired,.10f,kind);
        std::printf("crawler head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",axis,sign,stopped[0],stopped[1],stopped[2]);
        CHECK(sign*(stopped[axis]-eye[axis])>.035f);CHECK(std::fabs(stopped[axis]-desired[axis])>.05f);
        CHECK(!MeshBlocked(mesh,eye,stopped));
    }
    CHECK(!MeshBlocked(mesh,eye,{0,eye[1],10})); // forward observation
    CHECK(!MeshBlocked(mesh,eye,{0,10,eye[2]})); // no Nix neck occluder
    // The central column cover alone left the wide rotary coupling open
    // below the feet. Seal its annular top even from a forward/side lean.
    for(const auto desired:{eye,Point{-.32f,-.10f,.29f},Point{.32f,-.10f,.29f}}) {
        const auto from=ClampCockpitHead(desired,.10f,kind);
        for(unsigned i=0;i<16;++i) {
            const float angle=float(i)*6.2831853f/16;
            CHECK(MeshBlocked(mesh,from,{.55f*std::cos(angle),-1.335f,-.05f+.55f*std::sin(angle)}));
        }
    }
    for(float side:{-1.f,1.f}) {
        // Former diagonal member emerged through this lower cheek surface.
        // Its inner face is x=.514 here; the space in front must stay clear.
        CHECK(!MeshBlocked(mesh,{side*.489f,-.60f,.10f},{side*.5136f,-.60f,.10f}));
        // No secondary upright may reappear through the curved canopy.
        // Probe the free spans above/below the individual monitor mounts.
        for(float y:{.09f,.02f,-.28f,-.56f})
            CHECK(!MeshBlocked(mesh,{side*.466f,y,.69f},{side*.514f,y,.69f}));
    }
    for(float x:{-.40f,-.18f,0.f,.18f,.40f})for(float z:{-.10f,.08f,.29f}) {
        const auto from=ClampCockpitHead({x,.10f,z},.10f,kind);
        for(float side:{-1.f,1.f}) {
            // Only the weapon backs need the new cheek. Leg bearings now
            // belong to the retained native undercarriage, not cabin walls.
            CHECK(MeshBlocked(mesh,from,{side*.22f,-1.03f,.16f})); // seat/base coupling
            const Point socket{side*.961498f,1.976391f-2.45f,-.10929f-.05f};
            if(!MeshBlocked(mesh,from,socket))std::printf("Crawler exposed socket: eye %.3f %.3f %.3f side %.0f\n",from[0],from[1],from[2],side);
            CHECK(MeshBlocked(mesh,from,socket));
        }
    }
    for(float s:{-1.f,1.f}) {
        unsigned open=0;for(float z:{.68f,.80f,.92f,1.04f,1.16f})
            if(!MeshBlocked(mesh,eye,{s*1.15f,-.32f,z}))++open;
        CHECK(open>=3); // broad side opening around individual monitor arms
    }
    // The exterior-shaped cheeks cover the actual weapon socket area while
    // leaving the front leg platform visible through the lower side opening.
    for(const auto desired:{Point{0,.10f,-.10f},Point{-.36f,.18f,.25f},Point{.36f,.18f,.25f}}) {
        const auto from=ClampCockpitHead(desired,.10f,kind);
        for(float s:{-1.f,1.f}) {
            for(float z:{-.29f,-.03f})for(float y:{-.60f,-.34f}) {
                if(!MeshBlocked(mesh,from,{s*.96f,y,z}))std::printf("Crawler exposed gun area: eye %.3f %.3f %.3f to %.2f %.2f %.2f\n",from[0],from[1],from[2],s*.96f,y,z);
                CHECK(MeshBlocked(mesh,from,{s*.96f,y,z}));
            }
        }
    }
    for(float side:{-1.f,1.f}) {
        unsigned visible=0;
        for(float x:{.82f,1.0f,1.18f})for(float z:{1.05f,1.28f,1.50f})
            if(!MeshBlocked(mesh,eye,{side*x,-1.22f,z}))++visible;
        CHECK(visible>=5); // moving front legs and native base, not orange walls
    }
    CHECK(MeshBlocked(mesh,eye,{0,-1.60f,.72f})); // connected gatling keel / footwell bed
    const auto displays=BuildCockpitDisplays(kind);CHECK(displays.size()==18);
    for(const auto& p:displays)CHECK(p.position[2]>.24f); // moved well away from the pilot
    for(unsigned i=0;i<18;i+=6) {
        Point n{},c{};for(int k=0;k<3;++k){n[k]=displays[i].normal[k];for(unsigned j=0;j<6;++j)c[k]+=displays[i+j].position[k]/6;}
        const auto toEye=Sub(eye,c);CHECK(std::fabs(Dot(n,toEye))/std::sqrt(Dot(toEye,toEye))>.999f);
        CHECK(Dot(toEye,toEye)>.70f*.70f);
        CHECK((i==6?c[0]<-.2f:c[0]>.2f)); // health left; radar and ammo right
        // Scan the usable glass plane: covers and supporting members must
        // not penetrate a 4cm corridor around it, including near the corners.
        for(float u:{.06f,.12f,.5f,.88f,.94f})for(float t:{.06f,.12f,.5f,.88f,.94f}) {
            if(std::fabs(u-.5f)+std::fabs(t-.5f)>.85f)continue; // clipped glass corners
            Point p{},front{},back{};
            for(int k=0;k<3;++k){p[k]=displays[i].position[k]+u*(displays[i+1].position[k]-displays[i].position[k])
                    +t*(displays[i+2].position[k]-displays[i+1].position[k]);
                front[k]=p[k]+n[k]*.020f;back[k]=p[k]-n[k]*.020f;}
            CHECK(!MeshBlocked(mesh,front,back));
        }
    }
    // The orange receiver/liner used to occupy the pedal faces and their
    // travel. Keep both full toe/heel envelopes free of any hull triangles.
    for(std::size_t i=0;i<mesh.size();i+=3)if(mesh[i].surface==0)for(float s:{-1.f,1.f}) {
        const Point lo{std::min(s*.10f,s*.32f),-1.10f,.51f},hi{std::max(s*.10f,s*.32f),-.91f,.78f};
        CHECK(!TriangleInBox(mesh.data()+i,lo,hi));
    }
    CHECK(ClampCockpitHead({0,-.15f,.70f},.10f,kind)[2]>.40f);
}
void GpuTest(const std::filesystem::path& folder) {
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> ctx;CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&ctx)));if(!d)return;
    constexpr UINT width=960,height=600;D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.ArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> rtv;CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&texture)));CHECK(SUCCEEDED(d->CreateRenderTargetView(texture.Get(),nullptr,&rtv)));
    auto* bound=rtv.Get();ctx->OMSetRenderTargets(1,&bound,nullptr);D3D11_VIEWPORT vp{7,9,75,81,.1f,.9f};ctx->RSSetViewports(1,&vp);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_BLEND_DESC bdSaved{};bdSaved.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> blendSaved;CHECK(SUCCEEDED(d->CreateBlendState(&bdSaved,&blendSaved)));
    const float factorsSaved[]={.1f,.2f,.3f,.4f};ctx->OMSetBlendState(blendSaved.Get(),factorsSaved,0x13579bdf);
    D3D11_DEPTH_STENCIL_DESC zdSaved{};zdSaved.DepthFunc=D3D11_COMPARISON_ALWAYS;
    ComPtr<ID3D11DepthStencilState> depthSaved;CHECK(SUCCEEDED(d->CreateDepthStencilState(&zdSaved,&depthSaved)));ctx->OMSetDepthStencilState(depthSaved.Get(),23);
    Matrix projection{};projection.m[0][0]=.90f/(float(width)/height);projection.m[1][1]=.90f;projection.m[2][3]=-1;
    projection.m[2][2]=1000.f/(.1f-1000.f);projection.m[3][2]=.1f*1000.f/(.1f-1000.f);
    CockpitPose pose{};pose.cabin=Identity();pose.rig=Rig();
    const float blue[]={.18f,.42f,.68f,1};std::vector<unsigned char> eyes[2];
    for(unsigned eye=0;eye<2;++eye) {
        ctx->ClearRenderTargetView(rtv.Get(),blue);auto camera=Identity();camera.m[3][0]=eye?-.032f:.032f;
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));eyes[eye]=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        const auto center=(height/2*width+width/2)*4;CHECK(eyes[eye][center+2]>160&&eyes[eye][center]<50); // central opening shows world
        unsigned interior=0;for(std::size_t i=0;i<eyes[eye].size();i+=4)if(eyes[eye][i+2]!=eyes[eye][center+2])++interior;
        // The open cockpit leaves the forward view clear; inside the chest the
        // neck shroud closes the view overhead (the Nix's head sits there).
        CHECK(interior>width*height/10&&interior<width*height/2);
        std::printf("HUD-free forward view eye %u: %.1f percent unobstructed\n",eye,100.f*(1.f-float(interior)/(width*height)));
        if(!folder.empty())Bitmap(folder/(eye?L"cockpit_right.bmp":L"cockpit_left.bmp"),eyes[eye],width,height);
    }
    CHECK(eyes[0]!=eyes[1]);
    // Shared device resources must retain the selected cabin/theme for each
    // immutable eye pose, even while alternating Nix and Crawler every draw.
    const auto preparedKinds=ReadCockpitDrawStats().initializations;
    for(unsigned eye=0;eye<2;++eye) {
        auto camera=CockpitSeatedCamera(Identity());camera.m[3][0]=eye?-.032f:.032f;
        pose.rig.kind=CockpitKind::Crawler;ctx->ClearRenderTargetView(rtv.Get(),blue);
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
        const auto crawlerPixels=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        CHECK(crawlerPixels!=eyes[eye]);CHECK(ReadCockpitDrawStats().occlusion);
        if(!folder.empty())Bitmap(folder/(eye?L"crawler_right.bmp":L"crawler_left.bmp"),crawlerPixels,width,height);
        // Yellow transparent glass is sampled at the radar centre, away from
        // frames and native HUD. Switching backgrounds must remain visible.
        Matrix radarCamera{};
        const float towardRadar[]={-.32f-camera.m[3][0],-.06f-camera.m[3][1],.55f-camera.m[3][2]};
        CHECK(LookAlong(camera,towardRadar,radarCamera));
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(radarCamera),projection,pose));
        const auto yellow=ReadPixels(d.Get(),ctx.Get(),texture.Get());const auto pixel=(height/2*width+width/2)*4;
        const float black[]={0,0,0,1};ctx->ClearRenderTargetView(rtv.Get(),black);
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(radarCamera),projection,pose));const auto dark=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        CHECK(dark[pixel]>dark[pixel+1]&&dark[pixel+1]>dark[pixel+2]*2);
        CHECK(yellow[pixel+2]>dark[pixel+2]+40);
        pose.rig.kind=CockpitKind::Barga;
        const float towardBarga[]={.48f-camera.m[3][0],-.46f-camera.m[3][1],.72f-camera.m[3][2]};
        CHECK(LookAlong(camera,towardBarga,radarCamera));ctx->ClearRenderTargetView(rtv.Get(),black);
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(radarCamera),projection,pose));const auto green=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        CHECK(green[pixel+1]>green[pixel]*2&&green[pixel+1]>green[pixel+2]);CHECK(ReadCockpitDrawStats().occlusion);
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
        if(!folder.empty())Bitmap(folder/(eye?L"barga_right.bmp":L"barga_left.bmp"),ReadPixels(d.Get(),ctx.Get(),texture.Get()),width,height);
        for(unsigned variant=0;variant<2;++variant) {
            // Real grey waist dish moved into the opening: lit solid paint,
            // with native-depth occlusion preserved for moving fists.
            const auto cap=BuildBargaJointCap(variant,2);Point centre{};
            for(const auto& p:cap)for(int k=0;k<3;++k)centre[k]+=p.position[k]/float(cap.size());
            Matrix capFrame=Identity();capFrame.m[1][1]=capFrame.m[2][2]=0;capFrame.m[1][2]=1;capFrame.m[2][1]=-1;
            const float target[]={camera.m[3][0],camera.m[3][1],8.f};
            for(int j=0;j<3;++j){capFrame.m[3][j]=target[j];for(int k=0;k<3;++k)capFrame.m[3][j]-=centre[k]*capFrame.m[k][j];}
            pose.rig.capCount=3;pose.rig.capVariant=variant;pose.rig.capFrames={};pose.rig.capFrames[2]=capFrame;
            ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
            const auto capped=ReadPixels(d.Get(),ctx.Get(),texture.Get());CHECK(capped[pixel]>0&&capped[pixel+1]>0&&capped[pixel+2]>0);CHECK(capped[pixel+2]!=173);
            D3D11_TEXTURE2D_DESC dd=td;dd.Format=DXGI_FORMAT_D32_FLOAT;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
            ComPtr<ID3D11Texture2D> worldZ;ComPtr<ID3D11DepthStencilView> worldDsv;
            CHECK(SUCCEEDED(d->CreateTexture2D(&dd,nullptr,&worldZ)));CHECK(SUCCEEDED(d->CreateDepthStencilView(worldZ.Get(),nullptr,&worldDsv)));
            pose.rig.capCount=5;
            for(bool reversed:{false,true})for(bool occluded:{false,true}) {
                auto capProjection=projection;
                if(reversed){capProjection.m[2][2]=.1f/(1000.f-.1f);capProjection.m[3][2]=.1f*1000.f/(1000.f-.1f);}
                const float depthValue=occluded==reversed?1.f:0.f;
                ctx->ClearDepthStencilView(worldDsv.Get(),D3D11_CLEAR_DEPTH,depthValue,0);ctx->ClearRenderTargetView(rtv.Get(),blue);
                CHECK(DrawCockpitJointCaps(ctx.Get(),texture.Get(),Inverse(camera),capProjection,pose,worldDsv.Get(),reversed));
                const auto pixels=ReadPixels(d.Get(),ctx.Get(),texture.Get());
                if(occluded)CHECK(pixels[pixel+2]>160&&pixels[pixel]<50);else CHECK(pixels[pixel]>0&&pixels[pixel+1]>0&&pixels[pixel+2]!=173);
                const auto depthPixels=ReadPixels(d.Get(),ctx.Get(),worldZ.Get());float afterDepth=-1;std::memcpy(&afterDepth,depthPixels.data()+pixel,4);CHECK(afterDepth==depthValue);
            }
        }
        {
            // Tank barrel cap: drawn into its own depth, meeting the world's
            // depth read as a texture. Shown in the open, hidden behind a
            // nearer world, and the game's depth is left exactly as it was.
            const auto cap=BuildCockpitCap(kCapRailgun);Point centre{};
            for(const auto& p:cap)for(int k=0;k<3;++k)centre[k]+=p.position[k]/float(cap.size());
            Matrix capFrame=Identity();
            const float target[]={camera.m[3][0],camera.m[3][1],8.f};
            for(int j=0;j<3;++j)capFrame.m[3][j]=target[j]-centre[j];
            const auto savedKind=pose.rig.kind;pose.rig.kind=CockpitKind::Tank;
            pose.rig.capCount=1;pose.rig.capMesh={};pose.rig.capMesh[0]=static_cast<unsigned char>(kCapRailgun);pose.rig.capFrames={};pose.rig.capFrames[0]=capFrame;
            D3D11_TEXTURE2D_DESC dd=td;dd.Format=DXGI_FORMAT_R32_TYPELESS;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> worldZ;ComPtr<ID3D11DepthStencilView> worldDsv;CHECK(SUCCEEDED(d->CreateTexture2D(&dd,nullptr,&worldZ)));
            D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
            CHECK(SUCCEEDED(d->CreateDepthStencilView(worldZ.Get(),&dv,&worldDsv)));
            for(bool reversed:{false,true})for(bool occluded:{false,true}) {
                auto capProjection=projection;
                if(reversed){capProjection.m[2][2]=.1f/(1000.f-.1f);capProjection.m[3][2]=.1f*1000.f/(1000.f-.1f);}
                const float depthValue=occluded==reversed?1.f:0.f;
                ctx->ClearDepthStencilView(worldDsv.Get(),D3D11_CLEAR_DEPTH,depthValue,0);ctx->ClearRenderTargetView(rtv.Get(),blue);
                const auto before=ReadCockpitDrawStats().ownDepthCaps;
                CHECK(DrawCockpitJointCaps(ctx.Get(),texture.Get(),Inverse(camera),capProjection,pose,worldDsv.Get(),reversed));
                CHECK(ReadCockpitDrawStats().ownDepthCaps==before+1);   // its own depth, not the old path
                const auto pixels=ReadPixels(d.Get(),ctx.Get(),texture.Get());
                if(occluded)CHECK(pixels[pixel+2]>160&&pixels[pixel]<50);else CHECK(pixels[pixel]>0&&pixels[pixel+1]>0&&pixels[pixel+2]!=173);
                const auto depthPixels=ReadPixels(d.Get(),ctx.Get(),worldZ.Get());float afterDepth=-1;std::memcpy(&afterDepth,depthPixels.data()+pixel,4);CHECK(afterDepth==depthValue);
            }
            pose.rig.kind=savedKind;pose.rig.capMesh={};
        }
        pose.rig.capCount=0;
        // Proteus gunner pod: solid grey a shade darker than Barga, the same
        // blue combiner tint as Nix on the left console radar (the red tint
        // was tried and withdrawn).
        pose.rig.kind=CockpitKind::ProteusGunner;
        const float towardProteus[]={-.405f-camera.m[3][0],-.045f-camera.m[3][1],.58f-camera.m[3][2]};
        CHECK(LookAlong(camera,towardProteus,radarCamera));ctx->ClearRenderTargetView(rtv.Get(),black);
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(radarCamera),projection,pose));
        const auto pale=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        CHECK(pale[pixel+2]>pale[pixel]&&pale[pixel+2]>pale[pixel+1]);CHECK(ReadCockpitDrawStats().occlusion);
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
        if(!folder.empty())Bitmap(folder/(eye?L"proteus_right.bmp":L"proteus_left.bmp"),ReadPixels(d.Get(),ctx.Get(),texture.Get()),width,height);
        pose.rig.kind=CockpitKind::Nix;camera=Identity();camera.m[3][0]=eye?-.032f:.032f;
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
        CHECK(ReadPixels(d.Get(),ctx.Get(),texture.Get())==eyes[eye]);
    }
    CHECK(ReadCockpitDrawStats().initializations==preparedKinds);

    ComPtr<ID3D11RenderTargetView> restored;ctx->OMGetRenderTargets(1,&restored,nullptr);CHECK(restored.Get()==rtv.Get());
    D3D11_VIEWPORT after{};UINT n=1;ctx->RSGetViewports(&n,&after);CHECK(!std::memcmp(&after,&vp,sizeof(vp)));
    D3D11_PRIMITIVE_TOPOLOGY topology{};ctx->IAGetPrimitiveTopology(&topology);CHECK(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    // The HUD is sampled onto the actual screens in both eyes; source pixels
    // stay unchanged and the central window still shows the outside world.
    D3D11_TEXTURE2D_DESC hd=td;hd.Width=160;hd.Height=90;hd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<unsigned> hudPixels(160*90,0xff00ff00);D3D11_SUBRESOURCE_DATA hudData{hudPixels.data(),160*4,0};
    ComPtr<ID3D11Texture2D> hud;CHECK(SUCCEEDED(d->CreateTexture2D(&hd,&hudData,&hud)));
    // Empty HUD leaves the same blue glass as an absent HUD. The glass itself
    // remains transmissive; it never becomes an opaque blue backing plate.
    std::fill(hudPixels.begin(),hudPixels.end(),0u);ctx->UpdateSubresource(hud.Get(),0,nullptr,hudPixels.data(),160*4,0);
    for(float pitch:{0.f,-40.f})for(unsigned eye=0;eye<2;++eye) {
        Matrix camera{};CHECK(RotateCamera(Identity(),0,pitch,camera));camera.m[3][0]=eye?-.032f:.032f;
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
        const auto bare=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose,hud.Get()));
        CHECK(ReadPixels(d.Get(),ctx.Get(),texture.Get())==bare);
    }
    std::fill(hudPixels.begin(),hudPixels.end(),0xff00ff00);ctx->UpdateSubresource(hud.Get(),0,nullptr,hudPixels.data(),160*4,0);
    const auto unchangedHud=ReadPixels(d.Get(),ctx.Get(),hud.Get());
    // Leave unrelated compute state bound; cockpit auto-fit must restore it.
    constexpr char sentinel[]="RWStructuredBuffer<float4> O:register(u0);[numthreads(1,1,1)]void main(){O[0]=float4(1,2,3,4);}";
    ComPtr<ID3DBlob> sentinelCode;CHECK(SUCCEEDED(D3DCompile(sentinel,sizeof(sentinel),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&sentinelCode,nullptr)));
    ComPtr<ID3D11ComputeShader> sentinelShader;CHECK(SUCCEEDED(d->CreateComputeShader(sentinelCode->GetBufferPointer(),sentinelCode->GetBufferSize(),nullptr,&sentinelShader)));
    D3D11_BUFFER_DESC sentinelDesc{};sentinelDesc.ByteWidth=16;sentinelDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;sentinelDesc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;sentinelDesc.StructureByteStride=16;
    ComPtr<ID3D11Buffer> sentinelBuffer;ComPtr<ID3D11UnorderedAccessView> sentinelUav;
    CHECK(SUCCEEDED(d->CreateBuffer(&sentinelDesc,nullptr,&sentinelBuffer)));CHECK(SUCCEEDED(d->CreateUnorderedAccessView(sentinelBuffer.Get(),nullptr,&sentinelUav)));
    ComPtr<ID3D11ShaderResourceView> sentinelInput;CHECK(SUCCEEDED(d->CreateShaderResourceView(hud.Get(),nullptr,&sentinelInput)));
    auto* savedUav=sentinelUav.Get();auto* savedInput=sentinelInput.Get();ctx->CSSetShader(sentinelShader.Get(),nullptr,0);ctx->CSSetUnorderedAccessViews(0,1,&savedUav,nullptr);ctx->CSSetShaderResources(3,1,&savedInput);
    // Normal mapping now borrows PS t5. Preserve the game's resource in that
    // slot, including draws with HUD auto-fit and multiple stereo passes.
    ctx->PSSetShaderResources(5,1,&savedInput);
    for(unsigned eye=0;eye<2;++eye) {
        ctx->ClearRenderTargetView(rtv.Get(),blue);auto camera=Identity();camera.m[3][0]=eye?-.032f:.032f;
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose,hud.Get()));
        const auto shown=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        unsigned green=0;for(std::size_t i=0;i<shown.size();i+=4)if(shown[i]<10&&shown[i+1]>245&&shown[i+2]<12)++green;
        CHECK(green>10000&&green<width*height/3); // symbols blend over the blue world
        const auto centre=(height/2*width+width/2)*4;CHECK(shown[centre+2]>160&&shown[centre]<50);
        if(!folder.empty())Bitmap(folder/(eye?L"cockpit_hud_right.bmp":L"cockpit_hud_left.bmp"),shown,width,height);
    }
    CHECK(ReadPixels(d.Get(),ctx.Get(),hud.Get())==unchangedHud);
    // Distinct original HUD regions must reach their designated instruments,
    // rather than repeating or mirroring a whole-screen image on every MFD.
    for(unsigned y=0;y<90;++y)for(unsigned x=0;x<160;++x)
        hudPixels[y*160+x]=y<27?(x<80?0xff0000ff:0xff00ff00):(x<80?0xffff00ff:0xff00ffff);
    ctx->UpdateSubresource(hud.Get(),0,nullptr,hudPixels.data(),160*4,0);
    for(unsigned eye=0;eye<2;++eye) {
        ctx->ClearRenderTargetView(rtv.Get(),blue);auto camera=Identity();camera.m[3][0]=eye?-.032f:.032f;
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose,hud.Get()));
        const auto shown=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        const auto armour=(445*width+320)*4,radar=(419*width+480)*4,ammo=(456*width+640)*4;
        std::printf("HUD eye %u probes health=%u,%u,%u radar=%u,%u,%u ammo=%u,%u,%u\n",eye,shown[armour],shown[armour+1],shown[armour+2],shown[radar],shown[radar+1],shown[radar+2],shown[ammo],shown[ammo+1],shown[ammo+2]);
        CHECK(shown[armour]>225&&shown[armour+1]<16&&shown[armour+2]<25);
        CHECK(shown[radar]<10&&shown[radar+1]>230&&shown[radar+2]<25);
        CHECK(shown[ammo]>225&&shown[ammo+1]<16&&shown[ammo+2]>225);
        if(!folder.empty())Bitmap(folder/(eye?L"cockpit_routing_right.bmp":L"cockpit_routing_left.bmp"),shown,width,height);
    }
    // Long health bars, the first/last of four weapons and a lower-panel
    // message must all survive their crops. Test actual rasterized markers
    // against premultiplied blending, including a half-alpha message glyph.
    struct Marker {unsigned source;float u,v;unsigned rgba;};
    const Marker marks[]={{1,.46f,.13f,0xff0000ff},{2,.20f,.39f,0xffff00ff},
        {2,.20f,.92f,0xff00ffff},{0,.875f,.17f,0xff00ff00},{3,.62f,.56f,0x80808080}};
    std::fill(hudPixels.begin(),hudPixels.end(),0u);
    for(const auto& m:marks)for(unsigned y=0;y<90;++y)for(unsigned x=0;x<160;++x)
        if(std::fabs((x+.5f)/160-m.u)<.03f&&std::fabs((y+.5f)/90-m.v)<.035f)hudPixels[y*160+x]=m.rgba;
    ctx->UpdateSubresource(hud.Get(),0,nullptr,hudPixels.data(),160*4,0);
    const auto displays=BuildCockpitDisplays();
    for(unsigned eye=0;eye<2;++eye) {
        Matrix camera{};CHECK(RotateCamera(Identity(),0,-25,camera));camera.m[3][0]=eye?-.032f:.032f;
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
        const auto bare=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose,hud.Get()));
        const auto shown=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        for(const auto& m:marks) {
            const CockpitVertex* top=nullptr;
            for(std::size_t k=0;k<displays.size();k+=6)if(displays[k].surface==16.f+m.source)top=&displays[k];
            CHECK(top!=nullptr);if(!top)continue;
            int x0=int(width),x1=0,y0=int(height),y1=0;
            for(int corner:{0,1,2,5}) {
                float p[4]={top[corner].position[0],top[corner].position[1],top[corner].position[2],1};
                auto transform=[&](const Matrix& matrix){float q[4]{};for(int j=0;j<4;++j)for(int k=0;k<4;++k)q[j]+=p[k]*matrix.m[k][j];std::memcpy(p,q,sizeof(p));};
                transform(CockpitLocalToWorld(pose.cabin));transform(Inverse(camera));p[0]=-p[0];p[2]=-p[2];transform(projection);
                const int x=int((p[0]/p[3]+1)*width/2),y=int((1-p[1]/p[3])*height/2);
                x0=(std::min)(x0,x);x1=(std::max)(x1,x);y0=(std::min)(y0,y);y1=(std::max)(y1,y);
            }
            unsigned matched=0;
            for(int y=(std::max)(0,y0);y<(std::min)(int(height),y1);++y)for(int x=(std::max)(0,x0);x<(std::min)(int(width),x1);++x) {
                const auto at=(y*width+x)*4;const float alpha=((m.rgba>>24)&255)/255.f*.96f;
                bool same=true;float change=0;
                for(unsigned c=0;c<3;++c) {
                    const float expected=((m.rgba>>(c*8))&255)*.96f+bare[at+c]*(1-alpha);
                    same=same&&std::fabs(shown[at+c]-expected)<3;change+=std::fabs(float(shown[at+c])-bare[at+c]);
                }
                if(same&&change>20)++matched;
                CHECK(shown[at+3]==bare[at+3]);
            }
            std::printf("Fitted HUD eye %u source %u marker %.2f,%.2f pixels=%u\n",eye,m.source,m.u,m.v,matched);
            CHECK(matched>12); // edge markers survive and are readable after automatic fitting
            if(m.source==1)CHECK(matched>1500); // real alpha bounds expand a sparse health marker, removing old dead space
        }
        if(!folder.empty())Bitmap(folder/(eye?L"cockpit_transparent_right.bmp":L"cockpit_transparent_left.bmp"),shown,width,height);
    }
    ComPtr<ID3D11BlendState> gotBlend;float gotFactors[4]{};UINT gotMask=0;ctx->OMGetBlendState(&gotBlend,gotFactors,&gotMask);
    CHECK(gotBlend==blendSaved&&gotMask==0x13579bdf&&!std::memcmp(gotFactors,factorsSaved,sizeof(gotFactors)));
    ComPtr<ID3D11DepthStencilState> gotDepth;UINT gotRef=0;ctx->OMGetDepthStencilState(&gotDepth,&gotRef);CHECK(gotDepth==depthSaved&&gotRef==23);
    ComPtr<ID3D11ComputeShader> gotCs;ctx->CSGetShader(&gotCs,nullptr,nullptr);CHECK(gotCs==sentinelShader);
    ComPtr<ID3D11ShaderResourceView> gotCsInput;ctx->CSGetShaderResources(3,1,&gotCsInput);CHECK(gotCsInput==sentinelInput);
    ComPtr<ID3D11UnorderedAccessView> gotCsOutput;ctx->CSGetUnorderedAccessViews(0,1,&gotCsOutput);CHECK(gotCsOutput==sentinelUav);
    ComPtr<ID3D11ShaderResourceView> gotPsNormal;ctx->PSGetShaderResources(5,1,&gotPsNormal);CHECK(gotPsNormal==sentinelInput);
    savedUav=nullptr;savedInput=nullptr;ctx->CSSetUnorderedAccessViews(0,1,&savedUav,nullptr);ctx->CSSetShaderResources(3,1,&savedInput);ctx->CSSetShader(nullptr,nullptr,0);
    ctx->PSSetShaderResources(5,1,&savedInput);
    // A red world behind empty glass must receive the blue tint while remaining
    // visible, with its original alpha. This also covers absence of a HUD source.
    const float redWorld[]={.6f,.1f,.04f,.37f};ctx->ClearRenderTargetView(rtv.Get(),redWorld);
    CHECK(DrawCockpit(ctx.Get(),texture.Get(),Identity(),projection,pose));
    const auto tinted=ReadPixels(d.Get(),ctx.Get(),texture.Get());const auto glassAt=(419*width+480)*4;
    const float blueGlass[]={.008f,.060f,.17f};
    for(unsigned c=0;c<3;++c) {
        const float encoded=blueGlass[c]<=.0031308f?blueGlass[c]*12.92f:1.055f*std::pow(blueGlass[c],1.f/2.4f)-.055f;
        CHECK(std::fabs(tinted[glassAt+c]-255*(encoded*.43f+redWorld[c]*.57f))<2);
    }
    CHECK(std::abs(int(tinted[glassAt+3])-94)<=1);
    // HUD capture is stretched into the VR target, but authored in 16:9 UI
    // coordinates. A logical circle must stay circular on the physical radar
    // for both a desktop source and the real headset's near-square aspect.
    int circleBounds[2][4]{};
    for(unsigned source=0;source<2;++source) {
        const unsigned hw=source?324u:320u,hh=source?300u:180u;
        auto desc=hd;desc.Width=hw;desc.Height=hh;std::vector<unsigned> pixels(hw*hh,0);
        for(unsigned y=0;y<hh;++y)for(unsigned x=0;x<hw;++x) {
            const float dx=((x+.5f)/hw-.875f)*(16.f/9.f),dy=(y+.5f)/hh-.17f;
            if(dx*dx+dy*dy<.12f*.12f)pixels[y*hw+x]=0xff00ff00;
        }
        D3D11_SUBRESOURCE_DATA initial{pixels.data(),hw*4,0};ComPtr<ID3D11Texture2D> circle;
        CHECK(SUCCEEDED(d->CreateTexture2D(&desc,&initial,&circle)));
        Matrix camera{};CHECK(RotateCamera(Identity(),0,-16.26f,camera));camera.m[3][1]=-.135f;
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose,circle.Get()));
        const auto shown=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        auto& box=circleBounds[source];box[0]=box[2]=10000;box[1]=box[3]=-1;
        for(unsigned y=240;y<350;++y)for(unsigned x=420;x<540;++x) {
            const auto at=(y*width+x)*4;
            if(shown[at]<20&&shown[at+1]>220&&shown[at+2]<35) {
                box[0]=(std::min)(box[0],int(x));box[1]=(std::max)(box[1],int(x));
                box[2]=(std::min)(box[2],int(y));box[3]=(std::max)(box[3],int(y));
            }
        }
        const int cw=box[1]-box[0]+1,ch=box[3]-box[2]+1;
        std::printf("Logical radar circle source %ux%u: %dx%d pixels\n",hw,hh,cw,ch);
        CHECK(cw>50&&ch>50&&std::abs(cw-ch)<=3);
        if(!folder.empty())Bitmap(folder/(source?L"radar_square_capture.bmp":L"radar_wide_capture.bmp"),shown,width,height);
    }
    for(unsigned k=0;k<4;++k)CHECK(std::abs(circleBounds[0][k]-circleBounds[1][k])<=2);
    ctx->OMSetBlendState(nullptr,nullptr,~0u);ctx->OMSetDepthStencilState(nullptr,0);
    CHECK(ReadCockpitDrawStats().textured&&ReadCockpitDrawStats().instrumentFrames>=2);
    // Interior samples on the actual amber radar-frame lamps, across subpixel
    // camera motion. A categorical material ID must never interpolate out of
    // its emissive branch. The grip caps are painted, non-emissive controls.
    const auto solid=BuildNixCockpit();
    const Point lampNormal{0,.28f,-.96f},lampUp{0,.96f,.28f};
    unsigned unstable=0,samples=0;
    unsigned char buttonBaseline[2][3]{};bool haveButtonBaseline[2]{};
    for(int step=0;step<5;++step) {
        Matrix camera{};CHECK(RotateCamera(Identity(),float(step-2),-26.f,camera));
        camera.m[3][0]=step*.0003f;camera.m[3][1]=-.18f;camera.m[3][2]=.45f;
        const auto view=Inverse(camera);ctx->ClearRenderTargetView(rtv.Get(),blue);
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),view,projection,pose));
        const auto pixels=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        if(!folder.empty()&&step==2)Bitmap(folder/L"cockpit_lamp_stability.bmp",pixels,width,height);
        for(unsigned button=0;button<2;++button) {
            const float side=button?1.f:-1.f;
            auto lamp=[&](const auto& v){return unsigned(v.surface)==9u&&v.position[0]*side>.18f&&
                v.position[0]*side<.22f&&v.position[1]>-.44f&&v.position[1]<-.37f&&v.position[2]>.87f&&v.position[2]<.91f;};
            float front=-100;Point lo{100,100,100},hi{-100,-100,-100};
            for(const auto& v:solid)if(lamp(v)) {
                const Point p{v.position[0],v.position[1],v.position[2]};front=(std::max)(front,Dot(p,lampNormal));
            }
            for(const auto& v:solid)if(lamp(v)) {
                const Point p{v.position[0],v.position[1],v.position[2]};if(Dot(p,lampNormal)<front-.0001f)continue;
                for(int k=0;k<3;++k){lo[k]=(std::min)(lo[k],p[k]);hi[k]=(std::max)(hi[k],p[k]);}
            }
            for(int u=-2;u<=2;++u)for(int w=-2;w<=2;++w) {
                // The narrow status lenses: sample the flat central face
                // inside its rolled edge, including during subpixel motion.
                Point p{};for(int k=0;k<3;++k)p[k]=(lo[k]+hi[k])*.5f+lampUp[k]*w*.001f;p[0]+=u*.001f;
                p[0]=-p[0];Point q{};
                for(int j=0;j<3;++j){q[j]=view.m[3][j];for(int k=0;k<3;++k)q[j]+=p[k]*view.m[k][j];}
                const int px=int((.5f-.5f*q[0]*projection.m[0][0]/q[2])*width);
                const int py=int((.5f-.5f*q[1]*projection.m[1][1]/q[2])*height);
                CHECK(px>=0&&px<int(width)&&py>=0&&py<int(height));
                if(px<0||px>=int(width)||py<0||py>=int(height))continue;
                const auto at=(py*width+px)*4;
                // Check the amber emission as well as spatial/temporal
                // constancy so a dark or missing lamp cannot pass.
                const bool amber=pixels[at]>=220&&pixels[at+1]>=110&&pixels[at+1]<=180&&pixels[at+2]<90;
                if(!haveButtonBaseline[button]) {
                    for(int channel=0;channel<3;++channel)buttonBaseline[button][channel]=pixels[at+channel];
                    haveButtonBaseline[button]=true;
                }
                bool stable=amber;
                for(int channel=0;channel<3;++channel)
                    if(std::abs(int(pixels[at+channel])-int(buttonBaseline[button][channel]))>2)stable=false;
                ++samples;if(!stable){++unstable;
                    std::printf("Lamp sample %u step %d uv %d,%d rgb %u,%u,%u baseline %u,%u,%u\n",button,step,u,w,
                        unsigned(pixels[at]),unsigned(pixels[at+1]),unsigned(pixels[at+2]),
                        unsigned(buttonBaseline[button][0]),unsigned(buttonBaseline[button][1]),unsigned(buttonBaseline[button][2]));}
            }
        }
    }
    std::printf("Status lamp stability: %u unstable samples of %u\n",unstable,samples);CHECK(samples==250&&unstable==0);
    CHECK(ReadCockpitDrawStats().occlusion);
    // Native light constants are frozen on GPU for the stereo pair; local
    // environment indices, new PS bindings and frame expiry must be correct.
    {
        float systemData[152]{},extraData[100]{},environmentData[24]{};
        systemData[132]=systemData[133]=systemData[134]=.06f;
        for(unsigned k=0;k<3;++k){environmentData[k]=-10;environmentData[4+k]=.05f;environmentData[8+k]=20;const unsigned one=1;std::memcpy(environmentData+12+k,&one,4);}
        ComPtr<ID3D11Buffer> systemCb,extraCb,environmentCb,gridBuffer;
        auto constants=[&](const void* data,UINT bytes,ComPtr<ID3D11Buffer>& output){
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            D3D11_SUBRESOURCE_DATA sub{data,0,0};CHECK(SUCCEEDED(d->CreateBuffer(&desc,&sub,&output)));};
        constants(systemData,sizeof(systemData),systemCb);constants(extraData,sizeof(extraData),extraCb);constants(environmentData,sizeof(environmentData),environmentCb);
        unsigned cell[20]{};cell[0]=1;cell[16]=1; // grid selects green cube ONE, not red cube zero
        D3D11_BUFFER_DESC gridDesc{};gridDesc.ByteWidth=sizeof(cell);gridDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        gridDesc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;gridDesc.StructureByteStride=sizeof(cell);
        D3D11_SUBRESOURCE_DATA cellData{cell,0,0};CHECK(SUCCEEDED(d->CreateBuffer(&gridDesc,&cellData,&gridBuffer)));
        ComPtr<ID3D11ShaderResourceView> gridView,cubesView;
        CHECK(SUCCEEDED(d->CreateShaderResourceView(gridBuffer.Get(),nullptr,&gridView)));
        D3D11_TEXTURE2D_DESC cubeDesc{};cubeDesc.Width=cubeDesc.Height=2;cubeDesc.MipLevels=1;cubeDesc.ArraySize=12;
        cubeDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;cubeDesc.SampleDesc.Count=1;cubeDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;cubeDesc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        float cubePixels[12][16]{};D3D11_SUBRESOURCE_DATA cubeData[12]{};
        for(unsigned face=0;face<12;++face){for(unsigned pixel=0;pixel<4;++pixel){
            cubePixels[face][pixel*4]=(face<6?.9f:.015f);cubePixels[face][pixel*4+1]=(face<6?.015f:.9f);cubePixels[face][pixel*4+2]=.015f;cubePixels[face][pixel*4+3]=1;}
            cubeData[face]={cubePixels[face],32,0};}
        ComPtr<ID3D11Texture2D> cubeTexture;CHECK(SUCCEEDED(d->CreateTexture2D(&cubeDesc,cubeData,&cubeTexture)));
        D3D11_SHADER_RESOURCE_VIEW_DESC cubeViewDesc{};cubeViewDesc.Format=cubeDesc.Format;cubeViewDesc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        cubeViewDesc.TextureCubeArray.MipLevels=1;cubeViewDesc.TextureCubeArray.NumCubes=2;
        CHECK(SUCCEEDED(d->CreateShaderResourceView(cubeTexture.Get(),&cubeViewDesc,&cubesView)));
        auto litDesc=td;litDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;litDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> lightingOutput;ComPtr<ID3D11UnorderedAccessView> lightingUav;
        CHECK(SUCCEEDED(d->CreateTexture2D(&litDesc,nullptr,&lightingOutput)));CHECK(SUCCEEDED(d->CreateUnorderedAccessView(lightingOutput.Get(),nullptr,&lightingUav)));
        auto* cb=systemCb.Get();ctx->CSSetConstantBuffers(0,1,&cb);cb=extraCb.Get();ctx->CSSetConstantBuffers(2,1,&cb);cb=environmentCb.Get();ctx->CSSetConstantBuffers(10,1,&cb);
        auto* srv=cubesView.Get();ctx->CSSetShaderResources(8,1,&srv);srv=gridView.Get();ctx->CSSetShaderResources(10,1,&srv);
        auto* uav=lightingUav.Get();ctx->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        ID3D11Buffer* pixelCbs[]={systemCb.Get(),extraCb.Get(),environmentCb.Get()};ctx->PSSetConstantBuffers(1,3,pixelCbs);
        ID3D11ShaderResourceView* pixelInputs[]={sentinelInput.Get(),sentinelInput.Get()};ctx->PSSetShaderResources(6,2,pixelInputs);
        D3D11_SAMPLER_DESC samplerDesc{};samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        ComPtr<ID3D11SamplerState> oldEnvSampler;CHECK(SUCCEEDED(d->CreateSamplerState(&samplerDesc,&oldEnvSampler)));auto* envSampler=oldEnvSampler.Get();ctx->PSSetSamplers(1,1,&envSampler);
        CHECK(CaptureCockpitLighting(ctx.Get(),1001,(width+15)/16,(height+15)/16,1));
        CHECK(!GetCockpitLighting(ctx.Get(),1000).system);CHECK(GetCockpitLighting(ctx.Get(),1001).system!=systemCb);
        Matrix camera{};CHECK(RotateCamera(Identity(),0,-40,camera));
        auto render=[&](std::uint64_t frame){ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose,nullptr,frame));return ReadPixels(d.Get(),ctx.Get(),texture.Get());};
        const auto green=render(1001);CHECK(ReadCockpitDrawStats().nativeLights&&ReadCockpitDrawStats().environment);
        systemData[132]=systemData[133]=systemData[134]=10;ctx->UpdateSubresource(systemCb.Get(),0,nullptr,systemData,0,0);
        CHECK(render(1001)==green); // changes to the game's CB cannot corrupt the already captured pair
        systemData[132]=systemData[133]=systemData[134]=.06f;ctx->UpdateSubresource(systemCb.Get(),0,nullptr,systemData,0,0);
        cell[0]=0;ctx->UpdateSubresource(gridBuffer.Get(),0,nullptr,cell,0,0);
        CHECK(CaptureCockpitLighting(ctx.Get(),1002,(width+15)/16,(height+15)/16,1));const auto red=render(1002);
        unsigned greenReflections=0,redReflections=0;
        for(std::size_t at=0;at<red.size();at+=4){if(green[at+1]>green[at]+25&&green[at+1]>green[at+2]+25)++greenReflections;
            if(red[at]>red[at+1]+25&&red[at]>red[at+2]+25)++redReflections;}
        std::printf("Native local environment: green=%u red=%u reflective pixels\n",greenReflections,redReflections);
        CHECK(greenReflections>1000&&redReflections>1000&&green!=red);
        // Two probes interpolate across the cell rather than snapping to map 0.
        cell[0]=0;cell[1]=1;cell[16]=2;for(unsigned k=0;k<8;++k)cell[8+k]=k&1;
        ctx->UpdateSubresource(gridBuffer.Get(),0,nullptr,cell,0,0);
        CHECK(CaptureCockpitLighting(ctx.Get(),1002,(width+15)/16,(height+15)/16,1));const auto mixed=render(1002);
        unsigned blended=0;for(std::size_t at=0;at<mixed.size();at+=4)
            if(mixed[at]>green[at]+5&&mixed[at]<red[at]-5&&mixed[at+1]>red[at+1]+5&&mixed[at+1]<green[at+1]-5)++blended;
        CHECK(blended>1000);std::printf("Native environment interpolation: %u blended pixels\n",blended);
        if(!folder.empty()){Bitmap(folder/L"cockpit_environment_green.bmp",green,width,height);Bitmap(folder/L"cockpit_environment_red.bmp",red,width,height);}
        render(1003);CHECK(!ReadCockpitDrawStats().nativeLights&&!ReadCockpitDrawStats().environment);
        CHECK(!CaptureCockpitLighting(ctx.Get(),1003,1,1,1));CHECK(!GetCockpitLighting(ctx.Get(),1003).system);
        // Environment may be absent in a scene, while real ambient/direct lighting still works.
        srv=nullptr;ctx->CSSetShaderResources(8,1,&srv);systemData[132]=.01f;systemData[133]=.03f;systemData[134]=.8f;ctx->UpdateSubresource(systemCb.Get(),0,nullptr,systemData,0,0);
        CHECK(CaptureCockpitLighting(ctx.Get(),1004,(width+15)/16,(height+15)/16,1));CHECK(render(1004)!=red);
        CHECK(ReadCockpitDrawStats().nativeLights&&!ReadCockpitDrawStats().environment);
        // Native light vectors point along the ray: downward sunlight must
        // illuminate upward surfaces. Changing its direction changes shading.
        systemData[132]=systemData[133]=systemData[134]=.015f;
        systemData[85]=-1;systemData[88]=systemData[89]=systemData[90]=1;
        systemData[92]=systemData[93]=systemData[94]=1;
        const unsigned lightCount=1;std::memcpy(extraData+76,&lightCount,4);
        ctx->UpdateSubresource(systemCb.Get(),0,nullptr,systemData,0,0);ctx->UpdateSubresource(extraCb.Get(),0,nullptr,extraData,0,0);
        CHECK(CaptureCockpitLighting(ctx.Get(),1005,(width+15)/16,(height+15)/16,1));const auto sunDown=render(1005);
        systemData[85]=1;ctx->UpdateSubresource(systemCb.Get(),0,nullptr,systemData,0,0);
        CHECK(CaptureCockpitLighting(ctx.Get(),1006,(width+15)/16,(height+15)/16,1));const auto sunUp=render(1006);
        unsigned relit=0;for(std::size_t at=0;at<sunDown.size();at+=4)if(std::abs(int(sunDown[at])-int(sunUp[at]))>20)++relit;
        CHECK(relit>1000);std::printf("Native directional lighting: %u relit pixels\n",relit);
        ComPtr<ID3D11Buffer> gotSystem;ctx->CSGetConstantBuffers(0,1,&gotSystem);CHECK(gotSystem==systemCb);
        for(unsigned i=0;i<3;++i){ComPtr<ID3D11Buffer> got;ctx->PSGetConstantBuffers(i+1,1,&got);CHECK(got.Get()==pixelCbs[i]);}
        for(unsigned i=6;i<8;++i){ComPtr<ID3D11ShaderResourceView> got;ctx->PSGetShaderResources(i,1,&got);CHECK(got==sentinelInput);}
        ComPtr<ID3D11SamplerState> gotSampler;ctx->PSGetSamplers(1,1,&gotSampler);CHECK(gotSampler==oldEnvSampler);
        DiscardCockpitLighting();CHECK(!GetCockpitLighting(ctx.Get(),1006).system);
        cb=nullptr;ctx->CSSetConstantBuffers(0,1,&cb);ctx->CSSetConstantBuffers(2,1,&cb);ctx->CSSetConstantBuffers(10,1,&cb);
        srv=nullptr;ctx->CSSetShaderResources(8,1,&srv);ctx->CSSetShaderResources(10,1,&srv);uav=nullptr;ctx->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        for(unsigned i=1;i<=3;++i)ctx->PSSetConstantBuffers(i,1,&cb);for(unsigned i=6;i<8;++i)ctx->PSSetShaderResources(i,1,&srv);envSampler=nullptr;ctx->PSSetSamplers(1,1,&envSampler);
    }
    if(!folder.empty()) {
        auto camera=Identity();CHECK(RotateCamera(Identity(),-40,-20,camera));ctx->ClearRenderTargetView(rtv.Get(),blue);
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));Bitmap(folder/L"cockpit_look_side.bmp",ReadPixels(d.Get(),ctx.Get(),texture.Get()),width,height);
        camera=Identity();CHECK(RotateCamera(Identity(),0,-40,camera));ctx->ClearRenderTargetView(rtv.Get(),blue);
        CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));Bitmap(folder/L"cockpit_look_down.bmp",ReadPixels(d.Get(),ctx.Get(),texture.Get()),width,height);
    }
    // Render the real interception with two separated test triangles. Native
    // inputs are immutable, body triangle drops, limb triangle survives.
    const char shader[]=R"(struct I{float3 p:POSITION;};float4 vs(I i):SV_Position{return float4(i.p,1);}float4 ps():SV_Target{return float4(1,0,0,1);})";
    ComPtr<ID3DBlob> vbcode,pbcode;CHECK(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vbcode,nullptr)));
    CHECK(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&pbcode,nullptr)));
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11InputLayout> il;
    CHECK(SUCCEEDED(d->CreateVertexShader(vbcode->GetBufferPointer(),vbcode->GetBufferSize(),nullptr,&vs)));
    CHECK(SUCCEEDED(d->CreatePixelShader(pbcode->GetBufferPointer(),pbcode->GetBufferSize(),nullptr,&ps)));
    D3D11_INPUT_ELEMENT_DESC el{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};CHECK(SUCCEEDED(d->CreateInputLayout(&el,1,vbcode->GetBufferPointer(),vbcode->GetBufferSize(),&il)));
    SkinVertex skin[]={{{-.8f,-.6f,0},{1,0,0,0},{0,0,0,0}},{{-.5f,.6f,0},{1,0,0,0},{0,0,0,0}},{{-.2f,-.6f,0},{1,0,0,0},{0,0,0,0}},
        {{.2f,-.6f,0},{1,0,0,0},{1,0,0,0}},{{.5f,.6f,0},{1,0,0,0},{1,0,0,0}},{{.8f,-.6f,0},{1,0,0,0},{1,0,0,0}}};
    std::uint16_t idx[]={0,1,2,3,4,5};D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(skin);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{skin,0,0};ComPtr<ID3D11Buffer> vb,ib;CHECK(SUCCEEDED(d->CreateBuffer(&bd,&initial,&vb)));bd.ByteWidth=sizeof(idx);bd.BindFlags=D3D11_BIND_INDEX_BUFFER;initial.pSysMem=idx;CHECK(SUCCEEDED(d->CreateBuffer(&bd,&initial,&ib)));
    auto* raw=vb.Get();const UINT stride=sizeof(SkinVertex),offset=0;ctx->IASetVertexBuffers(0,1,&raw,&stride,&offset);ctx->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R16_UINT,0);
    ctx->IASetInputLayout(il.Get());ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);
    D3D11_VIEWPORT full{0,0,float(width),float(height),0,1};ctx->RSSetViewports(1,&full);
    CHECK(!CockpitLimbIntercept(ctx.Get(),6,0,0));
    // Cold limb filtering must not compile cabin shaders/decode its textures
    // between the two native eyes (the camera heartbeat expires after 250ms).
    ReleaseCockpitDraw();const auto beforeColdFilter=ReadCockpitDrawStats().initializations;
    auto rig=Rig();bool filtered=false;
    {CockpitLimbScope scope(rig);for(int i=0;i<100&&!filtered;++i) {ctx->ClearRenderTargetView(rtv.Get(),blue);filtered=CockpitLimbIntercept(ctx.Get(),6,0,0);ctx->Flush();}}
    CHECK(filtered);const auto pixels=ReadPixels(d.Get(),ctx.Get(),texture.Get());
    const auto left=(height/2*width+width/4)*4,right=(height/2*width+3*width/4)*4;
    CHECK(pixels[left]<50&&pixels[left+2]>160);CHECK(pixels[right]>250&&pixels[right+2]==0);
    ComPtr<ID3D11Buffer> saved;DXGI_FORMAT format{};UINT io=1;ctx->IAGetIndexBuffer(&saved,&format,&io);CHECK(saved.Get()==ib.Get()&&format==DXGI_FORMAT_R16_UINT&&io==0);
    CHECK(!CockpitLimbIntercept(ctx.Get(),6,0,0));CHECK(ReadCockpitDrawStats().keptTriangles==1&&ReadCockpitDrawStats().removedTriangles==1);
    const auto afterColdFilter=ReadCockpitDrawStats().initializations;CHECK(afterColdFilter==beforeColdFilter);
    CHECK(PrepareCockpitDraw(d.Get()));const auto prepared=ReadCockpitDrawStats().initializations;
    // Reproduce the mount failure: a slow left eye expires the heartbeat, the
    // right eye is rejected, and Present clears the failed pair. This must NOT
    // evict device resources or pending/ready limb selections and repeat the
    // expensive initialization on every subsequent frame.
    ConfigureNativeWorld(true);ConfigureCockpit(true);
    for(unsigned retry=0;retry<2;++retry) {
        RefreshNativeWorld(true,.064f);pose.camera=Identity();pose.camera.m[3][0]=.032f;pose.at=GetTickCount64();PublishCockpit(pose);
        const std::uint64_t frame=600+retry*2;
        BeginNativeWorldEye(frame,0);BeginNativeWorldView(0,true);ObserveNativeWorldView(Inverse(pose.camera));ObserveNativeWorldProjection(projection);EndNativeWorldView();
        Sleep(275);CHECK(!NativeWorldLive());EndNativeWorldEye(frame,0,ctx.Get(),texture.Get());
        auto rightCamera=pose.camera;rightCamera.m[3][0]=-.032f;
        BeginNativeWorldEye(frame,1);BeginNativeWorldView(0,true);ObserveNativeWorldView(Inverse(rightCamera));ObserveNativeWorldProjection(projection);EndNativeWorldView();
        EndNativeWorldEye(frame,1,ctx.Get(),texture.Get());CHECK(!TakeNativeWorldImages(width,height).ready);
        ClearNativeWorldImages();CHECK(!TakeNativeWorldImages(width,height).attempted);
        CHECK(ReadCockpitDrawStats().meshes==1&&ReadCockpitDrawStats().keptTriangles==1);
        CHECK(PrepareCockpitDraw(d.Get()));CHECK(ReadCockpitDrawStats().initializations==prepared);
        RefreshNativeWorld(true,.064f);pose.at=GetTickCount64();PublishCockpit(pose);
        for(unsigned eye=0;eye<2;++eye) {
            BeginNativeWorldEye(frame+1,eye);BeginNativeWorldView(0,true);
            ObserveNativeWorldView(Inverse(eye?rightCamera:pose.camera));ObserveNativeWorldProjection(projection);EndNativeWorldView();
            EndNativeWorldEye(frame+1,eye,ctx.Get(),texture.Get());
        }
        const auto recovered=TakeNativeWorldImages(width,height);CHECK(recovered.ready&&recovered.cockpitMatched);
    }
    std::printf("Cockpit timeout recovery: initialization count %llu -> %llu; cold limb filter %llu -> %llu\n",
        prepared,ReadCockpitDrawStats().initializations,beforeColdFilter,afterColdFilter);
    // Match the rendered camera, then latch one cabin for BOTH eyes even if a
    // newer logic pose arrives before the right eye. No cabin after dismount.
    ConfigureNativeWorld(true);RefreshNativeWorld(true,.064f);ConfigureCockpit(true);
    pose.camera=Identity();pose.camera.m[3][0]=.032f;pose.at=GetTickCount64();PublishCockpit(pose);
    BeginNativeWorldEye(700,0);BeginNativeWorldView(0,true);ObserveNativeWorldView(Inverse(pose.camera));ObserveNativeWorldProjection(projection);
    CHECK(NativeWorldCockpit()!=nullptr&&NativeWorldCockpit()->cabin.m[3][2]==0);
    EndNativeWorldView();CHECK(NativeWorldCockpit()==nullptr);
    EndNativeWorldEye(700,0,ctx.Get(),texture.Get());
    auto newer=pose;newer.cabin.m[3][2]=10;newer.camera.m[3][2]=10;PublishCockpit(newer);
    BeginNativeWorldEye(700,1);BeginNativeWorldView(2,true);CHECK(NativeWorldCockpit()==nullptr);EndNativeWorldView();
    auto rightEye=pose.camera;rightEye.m[3][0]=-.032f;
    BeginNativeWorldView(0,true);ObserveNativeWorldView(Inverse(rightEye));ObserveNativeWorldProjection(projection);
    CHECK(NativeWorldCockpit()!=nullptr&&NativeWorldCockpit()->cabin.m[3][2]==0);EndNativeWorldView();
    EndNativeWorldEye(700,1,ctx.Get(),texture.Get());const auto pair=TakeNativeWorldImages(width,height);
    CHECK(pair.ready&&pair.cockpitMatched&&pair.cockpit.cabin.m[3][2]==0);
    CHECK(std::fabs(pair.view[0].m[3][0]+.032f)<.0001f&&std::fabs(pair.view[1].m[3][0]-.032f)<.0001f);
    ClearCockpitPose();BeginNativeWorldEye(701,0);BeginNativeWorldView(0,true);ObserveNativeWorldView(Inverse(pose.camera));EndNativeWorldView();
    CHECK(NativeWorldCockpit()==nullptr);CancelNativeWorldEye(701);ConfigureCockpit(false);ConfigureNativeWorld(false);ClearNativeWorldImages();
    const auto beforeRelease=ReadCockpitDrawStats().initializations;
    ReleaseCockpitDraw();CHECK(PrepareCockpitDraw(d.Get()));CHECK(ReadCockpitDrawStats().initializations==beforeRelease+1);
    ComPtr<ID3D11Device> otherDevice;ComPtr<ID3D11DeviceContext> otherContext;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&otherDevice,nullptr,&otherContext)));
    CHECK(PrepareCockpitDraw(otherDevice.Get()));CHECK(ReadCockpitDrawStats().initializations==beforeRelease+2);
    CHECK(PrepareCockpitDraw(d.Get()));CHECK(ReadCockpitDrawStats().initializations==beforeRelease+3);ReleaseCockpitDraw();
}
}
// Opt-in standalone hardware measurement, never part of CTest. This measures
// the added stereo cabin/HUD draw, not an in-game on/off FPS comparison.
int BenchmarkCockpit(UINT width,UINT height) {
    if(width<320||height<320||width>8192||height>8192)return 2;
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> ctx;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&ctx)))return 2;
    ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC adapterDesc{};
    if(SUCCEEDED(d.As(&dx))&&SUCCEEDED(dx->GetAdapter(&adapter)))adapter->GetDesc(&adapterDesc);
    std::printf("Cockpit isolated benchmark: %ls, %ux%u per eye, stereo pair, fallback illumination, live HUD\n",adapterDesc.Description,width,height);
    D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.ArraySize=td.MipLevels=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target[2],hud;ComPtr<ID3D11RenderTargetView> rtv[2];
    for(unsigned i=0;i<2;++i) {
        if(FAILED(d->CreateTexture2D(&td,nullptr,&target[i]))||FAILED(d->CreateRenderTargetView(target[i].Get(),nullptr,&rtv[i])))return 2;
    }
    td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    std::vector<unsigned> pixels(static_cast<std::size_t>(width)*height,0);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)
        if((y*90/height)%20<3&&x<width/3)pixels[static_cast<std::size_t>(y)*width+x]=0xff38ce80;
    D3D11_SUBRESOURCE_DATA hudData{pixels.data(),width*4,0};
    if(FAILED(d->CreateTexture2D(&td,&hudData,&hud)))return 2;
    ComPtr<ID3D11Query> disjoint,start,end;D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
    if(FAILED(d->CreateQuery(&qd,&disjoint)))return 2;qd.Query=D3D11_QUERY_TIMESTAMP;
    if(FAILED(d->CreateQuery(&qd,&start))||FAILED(d->CreateQuery(&qd,&end)))return 2;
    CockpitPose pose{};pose.cabin=Identity();pose.rig=Rig();
    Matrix projection{};projection.m[0][0]=.90f/(float(width)/height);projection.m[1][1]=.90f;
    projection.m[2][3]=-1;projection.m[2][2]=1000.f/(.1f-1000.f);projection.m[3][2]=.1f*1000.f/(.1f-1000.f);
    const float blue[]={.18f,.42f,.68f,1};LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
    for(float pitch:{0.f,-40.f}) {
        std::vector<double> gpu,cpu;Matrix views[2];
        for(unsigned eye=0;eye<2;++eye){Matrix camera{};RotateCamera(Identity(),0,pitch,camera);camera.m[3][0]=eye?-.032f:.032f;views[eye]=Inverse(camera);}
        for(unsigned frame=0;frame<80;++frame) {
            for(auto& out:rtv)ctx->ClearRenderTargetView(out.Get(),blue);
            ctx->Begin(disjoint.Get());ctx->End(start.Get());LARGE_INTEGER a{},b{};QueryPerformanceCounter(&a);
            for(unsigned eye=0;eye<2;++eye)if(!DrawCockpit(ctx.Get(),target[eye].Get(),views[eye],projection,pose,hud.Get()))return 2;
            QueryPerformanceCounter(&b);ctx->End(end.Get());ctx->End(disjoint.Get());ctx->Flush();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dq{};UINT64 t0=0,t1=0;const auto deadline=GetTickCount64()+5000;
            HRESULT hr;
            while((hr=ctx->GetData(disjoint.Get(),&dq,sizeof(dq),D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE&&GetTickCount64()<deadline)Sleep(1);
            if(hr!=S_OK)return 2;
            if(ctx->GetData(start.Get(),&t0,sizeof(t0),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK||ctx->GetData(end.Get(),&t1,sizeof(t1),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK)return 2;
            if(frame>=16&&!dq.Disjoint&&dq.Frequency&&t1>=t0){gpu.push_back((t1-t0)*1000.0/dq.Frequency);cpu.push_back((b.QuadPart-a.QuadPart)*1000.0/frequency.QuadPart);}
        }
        if(gpu.size()<32)return 2;std::sort(gpu.begin(),gpu.end());std::sort(cpu.begin(),cpu.end());
        std::printf("pitch=%.0f samples=%zu GPU stereo median=%.4f p95=%.4f ms CPU submit median=%.4f ms\n",pitch,gpu.size(),gpu[gpu.size()/2],gpu[gpu.size()*95/100],cpu[cpu.size()/2]);
    }
    ReleaseCockpitDraw();std::puts("Excludes native-world rendering, vehicle filtering, head collision, XR compositor and real scene lighting. NOT an in-game FPS delta.");return 0;
}
#include "cockpit_barga_tests.inc"
#include "cockpit_barga_rigs.inc"
#include "vehicle_crew_tests.inc"
#include "crew_figure_tests.inc"
#include "cockpit_proteus_tests.inc"
#include "cockpit_cabin_tests.inc"
#include "cockpit_tank_tests.inc"
#include "cockpit_heli_tests.inc"
int wmain(int argc,wchar_t** argv) {
    if(argc==2&&!std::wcscmp(argv[1],L"--proteus-layout")){ProteusLayoutTest(false);return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--cabin-layout")){CabinLayoutTest(false);return failures?1:0;}
    if((argc==3||argc==4)&&!std::wcscmp(argv[1],L"--combat-lids")){CombatLidTest(argv[2],argc==4?argv[3]:L"");return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--tank-layout")){TankLayoutTest(false);CombatLayoutTest(false);CombatLidTest();return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--tank-baked")){TankLayoutTest(true);CombatLayoutTest(true);CombatLidTest();return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--heli-layout")){HeliLayoutTest(false);BruteGunnerTest(false);HeliLiningTest();return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--heli-baked")){HeliLayoutTest(true);BruteGunnerTest(true);HeliLiningTest();return failures?1:0;}
    if((argc==3||argc==4)&&!std::wcscmp(argv[1],L"--heli-lining")){HeliLiningTest(argv[2],argc==4?argv[3]:L"");return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--heli-export")){ExportMesh(argv[2],CockpitKind::HeliNereid);return failures?1:0;}
    if(argc==4&&!std::wcscmp(argv[1],L"--heli-export")){ExportMesh(argv[2],kHeliShells[std::wcstoul(argv[3],nullptr,10)].kind);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--brute-gunner-export")){ExportMesh(argv[2],CockpitKind::HeliBruteGunner);return failures?1:0;}
    if(argc==4&&!std::wcscmp(argv[1],L"--combat-export")) {
        // --combat-export <folder> negling|grape|caliban
        const auto kind=!std::wcscmp(argv[3],L"grape")?CockpitKind::CombatGrape:!std::wcscmp(argv[3],L"caliban")?CockpitKind::CombatCaliban:CockpitKind::CombatNegling;
        ExportMesh(argv[2],kind);return failures?1:0;
    }
    if(argc==3&&!std::wcscmp(argv[1],L"--tank-export")){ExportMesh(argv[2],CockpitKind::Tank);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--titan-export")){ExportMesh(argv[2],CockpitKind::TitanGunner);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--caps-export")){ExportCaps(argv[2]);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--proteus-export")){ExportMesh(argv[2],CockpitKind::ProteusGunner);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--driver-export")){ExportMesh(argv[2],CockpitKind::ProteusDriver);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--missile-export")){ExportMesh(argv[2],CockpitKind::ProteusMissile);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--proteus-fixtures")){FixtureTest(argv[2],false,false,true);return failures?1:0;}
    if(argc==4&&!std::wcscmp(argv[1],L"--barga-rigs")){BargaRigTest(argv[2],argv[3]);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--crew")){VehicleCrewTest(argv[2]);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--crew-figures")){CrewFigureTest(argv[2]);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--crew-draw")){CrewDrawTest(argv[2]);return failures?1:0;}
    if(argc==4&&!std::wcscmp(argv[1],L"--crew-draw")){CrewDrawTest(argv[2],argv[3]);return failures?1:0;}
    if(argc==6&&!std::wcscmp(argv[1],L"--crew-export")){CrewFigureExport(argv[2],argv[3],static_cast<unsigned>(_wtoi(argv[4])),static_cast<unsigned>(_wtoi(argv[5])));return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--barga-layout")){BargaLayoutTest(false);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--barga-export")){ExportMesh(argv[2],CockpitKind::Barga);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--barga-fixtures")){FixtureTest(argv[2],false,true);return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--crawler-layout")){CrawlerLayoutTest(false);return failures?1:0;}
    if(argc==2&&!std::wcscmp(argv[1],L"--layout")) {VisibilityTest();ClearanceTest();ScreenClearTest(CockpitKind::Nix,true);NixChestTest(false);return failures?1:0;}
    if(argc==4&&!std::wcscmp(argv[1],L"--benchmark"))return BenchmarkCockpit(static_cast<UINT>(std::wcstoul(argv[2],nullptr,10)),static_cast<UINT>(std::wcstoul(argv[3],nullptr,10)));
    if(argc==3&&!std::wcscmp(argv[1],L"--crawler-export")){ExportMesh(argv[2],CockpitKind::Crawler);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--export-only")){ExportMesh(argv[2]);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--nix-chest-export")){ExportMesh(argv[2],CockpitKind::NixChest);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--crawler-fixtures")){FixtureTest(argv[2],true);return failures?1:0;}
    if(argc==3&&!std::wcscmp(argv[1],L"--fixtures")) {FixtureTest(argv[2]);return failures?1:0;}
    if(argc<2)return 2;std::filesystem::path folder;if(argc==4&&!std::wcscmp(argv[2],L"--export")) {folder=argv[3];ExportMesh(folder);}
    const auto meshStart=GetTickCount64();auto mesh=BuildNixCockpit();CHECK(mesh.size()>1000&&mesh.size()%3==0);
    const auto meshMs=GetTickCount64()-meshStart;
    const auto collisionStart=GetTickCount64();ClampCockpitHead({.30f,-.28f,-.19f});
    std::printf("Cockpit geometry: %zu triangles, cold mesh %llums, cold collision tree+sweep %llums\n",
        mesh.size()/3,meshMs,GetTickCount64()-collisionStart);
    CHECK(ApplyCockpitOcclusion(mesh));unsigned contact=0,open=0;
    for(const auto& v:mesh)for(float a:v.visibility){CHECK(std::isfinite(a)&&a>=.2f&&a<=1);if(a<.5f)++contact;if(a>.98f)++open;}
    CHECK(contact>1000&&open>1000);
    auto changed=mesh;changed[0].position[0]+=.001f;CHECK(!ApplyCockpitOcclusion(changed));
    for(const auto& v:mesh) {
        for(float p:v.position)CHECK(std::isfinite(p));
        float length=0;for(float n:v.normal){CHECK(std::isfinite(n));length+=n*n;}
        CHECK(std::fabs(length-1.f)<.001f);
    }
    // Rounded surfaces need distinct unit normals inside their triangles;
    // importing/re-exporting flat normals used to expose every polygon edge.
    unsigned curved=0;
    // Every solid triangle must cover a finite patch of its own material tile.
    // This also guards imported parts against the former single-texel UVs.
    for(std::size_t i=0;i<mesh.size();i+=3) {
        const auto& a=mesh[i];const auto& b=mesh[i+1];const auto& c=mesh[i+2];
        const Point pa{a.position[0],a.position[1],a.position[2]},pb{b.position[0],b.position[1],b.position[2]},pc{c.position[0],c.position[1],c.position[2]};
        const auto geometric=Cross(Sub(pb,pa),Sub(pc,pa));
        CHECK(Dot(geometric,geometric)>1e-18f);
        bool varying=false;
        for(unsigned k=0;k<3;++k) {
            const auto& vertex=mesh[i+k];const Point n{vertex.normal[0],vertex.normal[1],vertex.normal[2]};
            CHECK(Dot(geometric,n)>0); // includes mirrored shoulder-pod winding
            for(int j=0;j<3;++j)if(std::fabs(vertex.normal[j]-a.normal[j])>.0001f)varying=true;
        }
        if(varying)++curved;
        if(a.surface==21)continue; // untextured screen joints do not use the material atlas
        const float area=(b.uv[0]-a.uv[0])*(c.uv[1]-a.uv[1])-(b.uv[1]-a.uv[1])*(c.uv[0]-a.uv[0]);
        CHECK(std::isfinite(area)&&std::fabs(area)>1e-12f);
        const unsigned tile=static_cast<unsigned>(a.surface);
        for(unsigned k=0;k<3;++k){const auto& p=mesh[i+k];
            CHECK(p.uv[0]>=(tile%4)*.25f+.005f&&p.uv[0]<=(tile%4+1)*.25f-.005f);
            CHECK(p.uv[1]>=(tile/4)*.25f+.005f&&p.uv[1]<=(tile/4+1)*.25f-.005f);}
    }
    CHECK(curved>5000);
    HistoryTest();VisibilityTest();ClearanceTest();ScreenClearTest(CockpitKind::Nix,true);NixChestTest();CrawlerLayoutTest();BargaLayoutTest();ProteusLayoutTest();CabinLayoutTest();TankLayoutTest();CombatLayoutTest();CombatLidTest();HeliLayoutTest();BruteGunnerTest();HeliLiningTest();SelectTest();VehicleTest(argv[1]);GpuTest(folder);
    std::printf("Cockpit chest anchor, ownership, pose history, limb selection, stereo GPU and restoration: %d failures\n",failures);return failures?1:0;
}
