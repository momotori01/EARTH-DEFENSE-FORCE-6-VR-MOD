#include "scope.h"
#include "native_world_view.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace edf6vr {
namespace {
// Eyepieces and screens, from each scope's own part of the mesh (a connected
// piece: scratchpad parts.py), its rear face over the last 25 mm (rear_faces.py),
// 2026-10-01. Round ones: centre and radius; rectangular sights: half width and
// height. brust and stingray01 take their part's box (the rear ring is cut flat).
// Variants sharing a mesh share a row (fivestar02 -> fivestar01,
// thundersniper02/03 -> 01). goliath01 and hornet01 carry the scope on the right
// of the tube. Low confidence: 603_powerful (a box behind a forward tube; the box
// is taken). 623_thunderpowerful01 (Rising Limit Custom) shows on the screen at the
// back of its top rail, not in the ring sight (asked on hardware 2026-10-01): the
// screen is a 14 x 37 mm recess at z .076 in a small frame (rising_box.py).
// The Air Raider monitors are given in their "screen" node's frame:
// - e_targetmarker_normal01 is the Laser Guide Kit's model, also used by the
//   missile, satellite and drone weapons. Its screen is a lid that the zoom
//   stands up behind its hinge, the underside then facing the shooter
//   (SCOPESCREEN, hardware 2026-10-01: rows (1,0,0)(0,0,1)(0,-1,0)).
// - e_targetmarker_gan01 is the Guide Beacon Gun's model, also used by the
//   artillery requests and some drones. Its screen is a plate beside the body,
//   facing back (bind pose).
// e_markerlauncher_normal01's weapons (Life Spout / Plasma Battery / Power and
// Guard Assist Gun) are all marked disabled (mukou) in the installed game's data.
constexpr float kBack[3]={0.f,0.f,-1.f},kUp[3]={0.f,1.f,0.f};
// Measured sizes are kept in the rows, but no eyepiece or sight is shown smaller
// than KFF50LS's 49 mm eyepiece, the smallest the user passed on hardware. The
// Rising Limit Custom's 14 x 37 mm screen could not be seen (2026-10-01); the user
// asked for these a little larger, spilling past a small rim if need be.
// The screens below keep their own sizes.
constexpr float kMinHalf=.0244f;
constexpr float Visible(float half) {return half<kMinHalf?kMinHalf:half;}
#define ROUND(node,x,y,z,r) {node,nullptr,0,{x,y,z},Visible(r),Visible(r),{kBack[0],kBack[1],kBack[2]},{kUp[0],kUp[1],kUp[2]}}
#define BOX(node,x,y,z,w,h) {node,nullptr,1,{x,y,z},Visible(w),Visible(h),{kBack[0],kBack[1],kBack[2]},{kUp[0],kUp[1],kUp[2]}}
// Zoom weapons with no scope: the holographic monitor 4.5 cm over the highest
// point of the sight area (z -0.1..0.3, |x| < 6 cm; 0.14-0.39 m over the root,
// so no one height serves), 5 cm ahead of the root, 7 cm across.
#define HOLO(node,y) {node,nullptr,0,{0.f,y,.05f},.035f,.035f,{kBack[0],kBack[1],kBack[2]},{kUp[0],kUp[1],kUp[2]},true}
// Shoulder launchers never carry a sight on top (the user, 2026-10-02): their
// holographic monitor goes on the left (+x), behind a side box where there is one.
#define HOLO_AT(node,x,y,z) {node,nullptr,0,{x,y,z},.035f,.035f,{kBack[0],kBack[1],kBack[2]},{kUp[0],kUp[1],kUp[2]},true}
constexpr ScopeLensSpec kLenses[]={
    ROUND(L"s_sniper_mmf01",          0.f,.1663f,-.0139f,.0244f),
    ROUND(L"s_sniper_SVU01",          0.f,.1480f,-.0840f,.0252f),
    ROUND(L"s_sniper_raythunder01",   0.f,.1967f,-.0161f,.0209f),
    ROUND(L"s_sniper_rsnr",           0.f,.2387f,-.0540f,.0244f),
    ROUND(L"s_sniper_fivestar01",     0.f,.2108f,-.0081f,.0279f),
    ROUND(L"s_sniper_brust",          0.f,.2128f,.0420f,.0135f),   // the upper eyepiece (the user, 2026-10-02)
    ROUND(L"s_sniper_eagleEye",       0.f,.1871f,-.0108f,.0253f),
    ROUND(L"s_sniper_raythunderz",    0.f,.2120f,.0155f,.0331f),
    ROUND(L"p_sniper_thundersniper01",0.f,.1709f,.1038f,.0178f),
    ROUND(L"p_sniper_LRSL01",         0.f,.1808f,-.0093f,.0215f),
    ROUND(L"s_rocket_goliath01",      .1230f,.1390f,-.0335f,.0309f),
    // Rocket launchers look through the lens on the left of the tube (+x), not
    // the top rail (the user, hardware 2026-10-02): its rear face, side_lens.py.
    ROUND(L"s_rocket_stingray01",     .0814f,.1242f,.0880f,.0165f),
    BOX(L"s_rocket_stingrayMMF",      .1121f,.1566f,.0220f,.0240f,.0295f),
    // Prominence's scope is the framed panel at the back of its left side box (the
    // user, 2026-10-02): the recessed face, z .074, 65 x 98 mm (prominence_panel.py).
    BOX(L"s_missile_prominence01",    .1855f,.1470f,.0740f,.0325f,.0490f),
    // The small sights on top that the user counts as lenses (2026-10-02): the
    // window of each one's rear face (research/scope/top_lens.py).
    BOX(L"s_assault_AF16s",           0.f,.1919f,.1070f,.0100f,.0080f),    // T1/T2 Stork
    BOX(L"s_assault_AF100",           0.f,.1901f,.0210f,.0160f,.0100f),    // T5/TZ Stork
    BOX(L"s_assault_601_powerful",    0.f,.2180f,-.1450f,.0225f,.0180f),   // MA9E/MA10E Slade
    BOX(L"s_assault_blasterriflenormal01",0.f,.1715f,.0840f,.0145f,.0255f),// Destruction / Burst Blazer
    BOX(L"s_grenade_grenadelauncherUMAX",0.f,.2393f,.1030f,.0165f,.0180f), // UMFF
    ROUND(L"s_grenade_grenadelaunchersp01",0.f,.2211f,.1870f,.0195f),     // Sticky
    // Wing Diver guns whose rear square the user counts as the sight (2026-10-02):
    BOX(L"p_sniper_thundersniperinferno",0.f,.1944f,.0740f,.0125f,.0205f), // Bolt Shooter ZF
    BOX(L"p_sniper_MONSTER01",        0.f,.1734f,.1340f,.0130f,.0305f),    // MONSTER family (01/02/03 alike)
    BOX(L"p_sniper_rising",           0.f,.2126f,.1710f,.0145f,.0085f),    // Raijin / Raijin alpha
    ROUND(L"s_rocket_602_powerful",   .1550f,.1347f,.0260f,.0150f),
    ROUND(L"s_rocket_hornet01",       .1513f,.1059f,.1746f,.0167f),
    BOX(L"s_sniper_hercules",         0.f,.2357f,.0838f,.0258f,.0336f),
    BOX(L"s_sniper_603_powerful",     0.f,.2568f,-.0291f,.0511f,.0322f),
    BOX(L"e_markerlauncher_normal01", 0.f,.2320f,-.0104f,.0410f,.0261f),
    BOX(L"p_sniper_623_thunderpowerful01",0.f,.1925f,.0760f,.0070f,.0187f),
    {L"e_targetmarker_normal01",L"screen",1,{0.f,-.0033f,-.0661f},.0220f,.0665f,{0.f,-1.f,0.f},{0.f,0.f,-1.f}},
    // ...and its two side panels, which the held pose (the standby clip of
    // WEAPON/E_TARGETMARKER_NORMAL01.CAS) opens 45 degrees about the centre
    // panel's long edges, like a three-way mirror. Each is a plate 45 x 130 mm in
    // its bone's y-z plane; the face that turns to the shooter is -x on the left
    // panel and +x on the right (research/scope/marker_panels.py; the user wants
    // all three to show the picture, 2026-10-02).
    {L"e_targetmarker_normal01",L"screen_l",1,{-.0004f,-.0227f,-.0027f},.0227f,.0650f,{-1.f,0.f,0.f},{0.f,0.f,-1.f}},
    {L"e_targetmarker_normal01",L"screen_r",1,{.0004f,-.0227f,-.0027f},.0227f,.0650f,{1.f,0.f,0.f},{0.f,0.f,-1.f}},
    {L"e_targetmarker_gan01",L"screen",1,{.0420f,.0010f,-.0060f},.0410f,.0360f,{0.f,0.f,-1.f},{0.f,1.f,0.f}},
    HOLO(L"s_assault_af14",0.208f),
    HOLO(L"s_assault_AF14st",0.188f),
    HOLO(L"s_assault_AF14rar",0.191f),
    HOLO(L"s_assault_AF20rar",0.240f),
    HOLO(L"s_assault_AF99st",0.234f),
    HOLO(L"s_assault_g130_01",0.208f),
    HOLO(L"s_spray_asidgun02",0.329f),
    HOLO(L"s_spray_asidgun03",0.329f),
    HOLO(L"s_rebirther_gan01",0.290f),
    HOLO(L"s_rebirther_aerial01",0.290f),
    HOLO(L"p_sniper_LRSLinferno",0.227f),
    // Goliath ZD/ZDMY: no side box; beside the tube (x +-.145, centre y .198), where
    // goliath01 has its lens.
    HOLO_AT(L"s_rocket_goliathZ",.190f,.200f,-.030f),
};
#undef ROUND
#undef BOX
#undef HOLO
#undef HOLO_AT
SRWLOCK lock=SRWLOCK_INIT;
ScopeView view{};
ScopeLensFrame lenses[kScopeMaxSurfaces]{};
int lensCount=0;
bool Finite3(const float v[3]) noexcept {return std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);}
float Length(const float v[3]) noexcept {return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
}
// A zoom weapon in none of the rows above: the holographic monitor 0.30 m over
// the root, clear of the tallest sight area measured (0.39 m with the monitor).
constexpr ScopeLensSpec kHolo={nullptr,nullptr,0,{0.f,.30f,.05f},.035f,.035f,{0.f,0.f,-1.f},{0.f,1.f,0.f},true};
const ScopeLensSpec& ScopeHoloSpec() noexcept {return kHolo;}
int ScopeLensCount() noexcept {return static_cast<int>(sizeof(kLenses)/sizeof(kLenses[0]));}
const ScopeLensSpec* ScopeLensAt(int index) noexcept {return index>=0&&index<ScopeLensCount()?&kLenses[index]:nullptr;}
void PublishScopeView(const ScopeView& v) noexcept {AcquireSRWLockExclusive(&lock);view=v;ReleaseSRWLockExclusive(&lock);}
ScopeView ReadScopeView() noexcept {AcquireSRWLockShared(&lock);const auto v=view;ReleaseSRWLockShared(&lock);return v;}
void PublishScopeLenses(const ScopeLensFrame* frames,int count) noexcept {
    count=std::clamp(count,0,kScopeMaxSurfaces);
    AcquireSRWLockExclusive(&lock);
    for(int i=0;i<kScopeMaxSurfaces;++i) lenses[i]=i<count?frames[i]:ScopeLensFrame{};
    lensCount=count;
    ReleaseSRWLockExclusive(&lock);
}
int ReadScopeLenses(ScopeLensFrame out[kScopeMaxSurfaces]) noexcept {
    AcquireSRWLockShared(&lock);
    for(int i=0;i<kScopeMaxSurfaces;++i) out[i]=lenses[i];
    const int count=lensCount;
    ReleaseSRWLockShared(&lock);
    return count;
}
void PublishScopeLens(const ScopeLensFrame& f) noexcept {PublishScopeLenses(&f,1);}
ScopeLensFrame ReadScopeLens() noexcept {AcquireSRWLockShared(&lock);const auto f=lenses[0];ReleaseSRWLockShared(&lock);return f;}
int ScopeLensSurfaces(int first) noexcept {
    const auto* lead=ScopeLensAt(first);
    if(!lead) return 0;
    int count=1;
    while(count<kScopeMaxSurfaces) {
        const auto* next=ScopeLensAt(first+count);
        if(!next || !next->node || !lead->node || std::wcscmp(next->node,lead->node)) break;
        ++count;
    }
    return count;
}

float ScopeTanHalf(float lensTan,float zoom) noexcept {
    if(!std::isfinite(lensTan) || !std::isfinite(zoom)) return 0;
    lensTan=std::clamp(lensTan,.02f,1.2f);zoom=std::clamp(zoom,1.f,40.f);
    return std::clamp(lensTan/zoom,.002f,1.f);
}
bool ScopeCamera(const Matrix& head,const float origin[3],const float forward[3],Matrix& out) noexcept {
    if(!Finite3(origin) || !Finite3(forward)) return false;
    float f[3]={forward[0],forward[1],forward[2]};const float lf=Length(f);
    if(!(lf>1e-4f)) return false;
    for(float& c:f) c/=lf;
    float h[3]={head.m[2][0],head.m[2][1],head.m[2][2]};const float lh=Length(h);
    if(!(lh>1e-4f)) return false;
    for(float& c:h) c/=lh;
    const float cosine=std::clamp(h[0]*f[0]+h[1]*f[1]+h[2]*f[2],-1.f,1.f);
    if(cosine<-.98f) return false;   // aiming behind the head: no sensible roll
    // Rodrigues about h x f, applied to the head's up.
    float axis[3]={h[1]*f[2]-h[2]*f[1],h[2]*f[0]-h[0]*f[2],h[0]*f[1]-h[1]*f[0]};
    const float s=Length(axis);
    float up[3]={head.m[1][0],head.m[1][1],head.m[1][2]};
    if(s>1e-6f) {
        for(float& c:axis) c/=s;
        const float angle=std::atan2(s,cosine),cs=std::cos(angle),sn=std::sin(angle);
        const float v[3]={up[0],up[1],up[2]};
        const float k[3]={axis[1]*v[2]-axis[2]*v[1],axis[2]*v[0]-axis[0]*v[2],axis[0]*v[1]-axis[1]*v[0]};
        const float along=axis[0]*v[0]+axis[1]*v[1]+axis[2]*v[2];
        for(int j=0;j<3;++j) up[j]=v[j]*cs+k[j]*sn+axis[j]*along*(1-cs);
    }
    const float d=up[0]*f[0]+up[1]*f[1]+up[2]*f[2];
    for(int j=0;j<3;++j) up[j]-=f[j]*d;
    const float lu=Length(up);
    if(!(lu>1e-4f)) return false;
    for(float& c:up) c/=lu;
    out=Matrix{};
    for(int j=0;j<3;++j) {out.m[1][j]=up[j];out.m[2][j]=f[j];out.m[3][j]=origin[j];}
    out.m[0][0]=up[1]*f[2]-up[2]*f[1];out.m[0][1]=up[2]*f[0]-up[0]*f[2];out.m[0][2]=up[0]*f[1]-up[1]*f[0];
    out.m[3][3]=1;
    return ValidNativeWorldCamera(out);
}
bool ScopeFrustum(const float in[6],float tanHalf,float aspect,float out[6]) noexcept {
    for(int i=0;i<6;++i) if(!std::isfinite(in[i])) return false;
    const float nearPlane=in[4],height=in[2]-in[3];
    if(!(nearPlane>0) || !(height>0) || !(in[1]>in[0]) || !std::isfinite(tanHalf) || !(tanHalf>0) || !std::isfinite(aspect)) return false;
    // Signs as the engine's own: left < right, top > bottom.
    const float wide=tanHalf*std::max(aspect,1.f);
    out[0]=-nearPlane*wide;out[1]=nearPlane*wide;
    out[2]=(in[2]>0?1.f:-1.f)*nearPlane*tanHalf;out[3]=-out[2];
    out[4]=in[4];out[5]=in[5];
    return true;
}
bool ScopeLensFrameFrom(const Matrix& root,const ScopeLensSpec& spec,const float eye[3],float scale,ScopeLensFrame& out) noexcept {
    out=ScopeLensFrame{};
    if(!Finite3(eye) || !std::isfinite(scale) || !(scale>0)) return false;
    float c[3]{},n[3]{},v[3]{};
    for(int j=0;j<3;++j) {
        c[j]=spec.centre[0]*root.m[0][j]+spec.centre[1]*root.m[1][j]+spec.centre[2]*root.m[2][j]+root.m[3][j];
        n[j]=spec.normal[0]*root.m[0][j]+spec.normal[1]*root.m[1][j]+spec.normal[2]*root.m[2][j];
        v[j]=spec.up[0]*root.m[0][j]+spec.up[1]*root.m[1][j]+spec.up[2]*root.m[2][j];
    }
    const float r1[3]={root.m[1][0],root.m[1][1],root.m[1][2]};
    const float l1=Length(r1),ln=Length(n),lv=Length(v);
    if(!Finite3(c) || !(l1>1e-4f) || !(ln>1e-4f) || !(lv>1e-4f)) return false;
    for(int j=0;j<3;++j) {out.centre[j]=c[j];out.normal[j]=n[j]/ln;out.up[j]=v[j]/lv;}
    out.shape=spec.shape;
    out.radius=spec.halfHeight*l1*scale;
    out.halfWidth=spec.halfWidth*l1*scale;
    const float to[3]={c[0]-eye[0],c[1]-eye[1],c[2]-eye[2]};const float distance=Length(to);
    if(!(distance>.02f) || !(out.radius>0)) return false;
    out.lensTan=out.radius/distance;
    out.valid=std::isfinite(out.lensTan);
    return out.valid;
}
bool ScopeClip(const Matrix& v,const Matrix& p,const float world[3],float clip[4]) noexcept {
    float e[4]{};
    for(unsigned j=0;j<4;++j) {e[j]=v.m[3][j];for(unsigned k=0;k<3;++k) e[j]+=world[k]*v.m[k][j];}
    e[0]=-e[0];e[2]=-e[2];   // EDF VIEW_CHANGED Ry(pi), as the native world
    for(unsigned j=0;j<4;++j) {clip[j]=0;for(unsigned k=0;k<4;++k) clip[j]+=e[k]*p.m[k][j];}
    for(unsigned j=0;j<4;++j) if(!std::isfinite(clip[j])) return false;
    return true;
}
Matrix ScopeWorldToClip(const Matrix& v,const Matrix& p) noexcept {
    Matrix flipped=v;
    for(int i=0;i<4;++i) {flipped.m[i][0]=-v.m[i][0];flipped.m[i][2]=-v.m[i][2];}
    Matrix out{};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {float s=0;for(int k=0;k<4;++k) s+=flipped.m[i][k]*p.m[k][j];out.m[i][j]=s;}
    return out;
}
Matrix ScopeCameraOf(const Matrix& v) noexcept {
    Matrix out{};out.m[3][3]=1;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) out.m[i][j]=v.m[j][i];
    for(int j=0;j<3;++j) for(int k=0;k<3;++k) out.m[3][j]-=v.m[3][k]*out.m[k][j];
    return out;
}
}
