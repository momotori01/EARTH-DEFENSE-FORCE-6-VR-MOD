// Checks the OpenXR-to-EDF6 conversions and the call-site redirection helper.
// No game code runs here; the redirection test builds its own call instruction.
#include "vr_math.h"
#include "vehicle_camera.h"
#include "image_profile.h"
#include "weapon_hold.h"
#include "angle_field_finder.h"
#include "body_tumble.h"
#include "openxr_session.h"
#include "ranger_holster.h"
#include "scope.h"
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

using namespace edf6vr;

static Quat FromAxisAngle(float x,float y,float z,float radians) {
    const float s=std::sin(radians*0.5f);
    return Quat{x*s,y*s,z*s,std::cos(radians*0.5f)};
}

static int g_redirected=0;
static int __fastcall Target(int value) { return value*2; }
static int __fastcall Replacement(int value) { ++g_redirected; return value*3; }

static void TestHeadBasis() {
    HeadBasis basis{};
    CHECK(HeadBasisFromXr(Quat{},basis));
    // Identity in OpenXR looks along -Z; the half turn about Y makes that +Z,
    // which is where EDF6 measures yaw 0.
    CHECK(std::fabs(basis.yaw)<1e-5f);
    CHECK(std::fabs(basis.pitchUp)<1e-5f);
    CHECK(std::fabs(basis.forward.z-1)<1e-5f);
    CHECK(std::fabs(basis.up.y-1)<1e-5f);

    // Turning left in OpenXR is a positive rotation about +Y. The mapping is a
    // rotation, not a mirror, so game yaw turns the same way by the same amount.
    // A mirror here is what made yaw run backwards in the 2026-09-08 game test.
    CHECK(HeadBasisFromXr(FromAxisAngle(0,1,0,0.5f),basis));
    CHECK(std::fabs(basis.yaw-0.5f)<1e-4f);
    CHECK(std::fabs(basis.pitchUp)<1e-4f);

    // Pitching up in OpenXR is a positive rotation about +X.
    CHECK(HeadBasisFromXr(FromAxisAngle(1,0,0,0.4f),basis));
    CHECK(basis.pitchUp>0.39f && basis.pitchUp<0.41f);
    CHECK(std::fabs(basis.yaw)<1e-4f);

    // A pure roll keeps yaw and pitch at zero and tips the up vector sideways.
    CHECK(HeadBasisFromXr(FromAxisAngle(0,0,1,0.3f),basis));
    CHECK(std::fabs(basis.pitchUp)<1e-4f);
    CHECK(std::fabs(basis.up.x-std::sin(0.3f))<1e-4f);

    Quat bad{}; bad.w=2;
    CHECK(!HeadBasisFromXr(bad,basis));
    bad=Quat{}; bad.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!HeadBasisFromXr(bad,basis));

    for(float a=-3.1f;a<3.1f;a+=0.13f) for(float b=-1.5f;b<1.5f;b+=0.11f) {
        const Quat yaw=FromAxisAngle(0,1,0,a), pitch=FromAxisAngle(1,0,0,b);
        const Quat combined{
            yaw.w*pitch.x+yaw.x*pitch.w+yaw.y*pitch.z-yaw.z*pitch.y,
            yaw.w*pitch.y-yaw.x*pitch.z+yaw.y*pitch.w+yaw.z*pitch.x,
            yaw.w*pitch.z+yaw.x*pitch.y-yaw.y*pitch.x+yaw.z*pitch.w,
            yaw.w*pitch.w-yaw.x*pitch.x-yaw.y*pitch.y-yaw.z*pitch.z};
        CHECK(HeadBasisFromXr(combined,basis));
        CHECK(std::fabs(basis.pitchUp-b)<2e-3f);
        const float difference=std::fabs(std::atan2(std::sin(basis.yaw-a),std::cos(basis.yaw-a)));
        CHECK(difference<2e-3f);
    }

    // Right up to the pole. Yaw is taken from the forward row while there is a
    // horizontal part worth reading, and from the up row once there is not; the
    // swap used to happen a twentieth of a degree from vertical, so just off it
    // the heading was being read out of numerical noise and span. A Wing Diver
    // flies where she aims, and went with it. Both ends of the sweep, because
    // the up row points along the heading at one pole and against it at the
    // other.
    for(int sign:{1,-1}) for(float off=0.30f;off>=0.0f;off-=0.002f) {
        const float b=sign*(1.5707963f-off);
        for(float a:{0.0f,1.1f,-2.4f,3.0f}) {
            const Quat yaw=FromAxisAngle(0,1,0,a), pitch=FromAxisAngle(1,0,0,b);
            const Quat combined{
                yaw.w*pitch.x+yaw.x*pitch.w+yaw.y*pitch.z-yaw.z*pitch.y,
                yaw.w*pitch.y-yaw.x*pitch.z+yaw.y*pitch.w+yaw.z*pitch.x,
                yaw.w*pitch.z+yaw.x*pitch.y-yaw.y*pitch.x+yaw.z*pitch.w,
                yaw.w*pitch.w-yaw.x*pitch.x-yaw.y*pitch.y-yaw.z*pitch.z};
            HeadBasis pole{};
            CHECK(HeadBasisFromXr(combined,pole));
            const float off2=std::fabs(std::atan2(std::sin(pole.yaw-a),std::cos(pole.yaw-a)));
            CHECK(off2<3e-3f);
        }
    }
}

// A mirror would invert horizontal rotation, so pin the determinant down.
static void TestMappingIsARotation() {
    const Vec3 x=XrToGame(Vec3{1,0,0}), y=XrToGame(Vec3{0,1,0}), z=XrToGame(Vec3{0,0,1});
    const float determinant=x.x*(y.y*z.z-y.z*z.y)-x.y*(y.x*z.z-y.z*z.x)+x.z*(y.x*z.y-y.y*z.x);
    CHECK(std::fabs(determinant-1)<1e-5f);
    // Vertical is untouched, which is why head height and pitch were unaffected.
    CHECK(y.x==0 && y.y==1 && y.z==0);
    // A positive OpenXR turn about +Y must raise game yaw by the same amount.
    // The mirror this replaced lowered it, which is the bug the game test found.
    HeadBasis left{}, right{};
    CHECK(HeadBasisFromXr(FromAxisAngle(0,1,0,0.6f),left));
    CHECK(HeadBasisFromXr(FromAxisAngle(0,1,0,-0.6f),right));
    CHECK(std::fabs(left.yaw-0.6f)<1e-4f && std::fabs(right.yaw+0.6f)<1e-4f);
}

static void TestRotateY() {
    const Vec3 forward{0,0,1};
    for(float a=-3.0f;a<3.0f;a+=0.25f) {
        const Vec3 turned=RotateY(forward,a);
        CHECK(std::fabs(std::atan2(turned.x,turned.z)-a)<1e-4f);
        const Vec3 back=RotateY(turned,-a);
        CHECK(std::fabs(back.x)<1e-4f && std::fabs(back.z-1)<1e-4f);
    }
}

static void TestRoll() {
    Matrix camera{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{5,6,7,1}}}, rolled{};
    float roll=0;
    CHECK(RollCameraToUp(camera,Vec3{0,1,0},rolled,roll));
    CHECK(std::fabs(roll)<1e-5f);
    CHECK(!std::memcmp(&rolled,&camera,sizeof(camera)));

    const float angle=0.4f;
    const Vec3 leaning{std::sin(angle),std::cos(angle),0};
    CHECK(RollCameraToUp(camera,leaning,rolled,roll));
    CHECK(std::fabs(roll-angle)<1e-4f);
    // Forward and position must survive untouched: that is what keeps the aim.
    CHECK(!std::memcmp(rolled.m[2],camera.m[2],sizeof(camera.m[2])));
    CHECK(!std::memcmp(rolled.m[3],camera.m[3],sizeof(camera.m[3])));
    CHECK(std::fabs(rolled.m[1][0]-leaning.x)<1e-4f);
    CHECK(std::fabs(rolled.m[1][1]-leaning.y)<1e-4f);
    CHECK(ValidCamera(rolled));

    // Head aimed along the camera axis leaves the roll undefined; keep it as is.
    CHECK(RollCameraToUp(camera,Vec3{0,0,1},rolled,roll));
    CHECK(roll==0);
    CHECK(!std::memcmp(&rolled,&camera,sizeof(camera)));
    CHECK(!RollCameraToUp(camera,Vec3{0,std::numeric_limits<float>::quiet_NaN(),0},rolled,roll));
}

static void TestMoveBasis() {
    for(int truth=0;truth<kMoveBasisCount;++truth) {
        MoveBasis basis{};
        const float speed=6.5f;
        for(int i=0;i<60;++i) {
            const float angle=static_cast<float>(i)*0.21f;
            const float inX=std::cos(angle), inZ=std::sin(angle);
            float localX=0, localZ=0;
            MoveBasisApply(truth,inX,inZ,localX,localZ);
            MoveBasisObserve(basis,inX,inZ,localX*speed,localZ*speed);
        }
        CHECK(MoveBasisSolve(basis));
        CHECK(basis.candidate==truth);
        CHECK(std::fabs(basis.scale-speed)<0.05f);
        float inX=0, inZ=0;
        // Ask for one metre per second along world +X while facing +Z.
        CHECK(RoomScaleInput(basis,0,speed,0,inX,inZ));
        float localX=0, localZ=0;
        MoveBasisApply(truth,inX,inZ,localX,localZ);
        CHECK(std::fabs(localX*speed-speed)<0.01f);
        CHECK(std::fabs(localZ)<0.01f);
        // Requests beyond full deflection are clamped, never scaled up.
        CHECK(RoomScaleInput(basis,0,speed*10,0,inX,inZ));
        CHECK(std::fabs(std::sqrt(inX*inX+inZ*inZ)-1)<1e-4f);
    }
    MoveBasis idle{};
    for(int i=0;i<200;++i) MoveBasisObserve(idle,0.01f,0,0.01f,0);
    CHECK(!MoveBasisSolve(idle));
    float inX=1, inZ=1;
    CHECK(!RoomScaleInput(idle,0,1,0,inX,inZ));
    CHECK(inX==0 && inZ==0);
}

// The synthetic call site fabricates code at runtime, so this function opts out
// of the Control Flow Guard check on its own indirect calls.
__declspec(guard(ignore)) static void TestRedirect() {
    using Call=int (__fastcall*)(int);
    CHECK(AllocateNearThunk(nullptr,reinterpret_cast<void*>(&Target))==nullptr);
    // A call rel32 only reaches +/-2GB, so the site has to live near the target.
    auto page=static_cast<unsigned char*>(AllocateNearThunk(reinterpret_cast<void*>(&Target),
                                                            reinterpret_cast<void*>(&Target)));
    CHECK(page!=nullptr);
    if(!page) return;
    const auto reach=page-reinterpret_cast<unsigned char*>(&Target);
    CHECK(reach<0x7FFFFFF0 && reach>-0x7FFFFFF0);
    CHECK(reinterpret_cast<Call>(page)(4)==8);

    DWORD previous=0;
    CHECK(VirtualProtect(page,0x1000,PAGE_EXECUTE_READWRITE,&previous)!=0);
    // call rel32 / ret -- a synthetic call site for RedirectCall to rewrite.
    unsigned char* site=page+0x40;
    site[0]=0xE8;
    const auto relative=static_cast<std::int32_t>(reinterpret_cast<unsigned char*>(&Target)-(site+5));
    std::memcpy(site+1,&relative,sizeof(relative));
    site[5]=0xC3;
    // jmp rel32 -- a tail call, for RedirectJump.
    unsigned char* jump=page+0x60;
    jump[0]=0xE9;
    const auto jumpRelative=static_cast<std::int32_t>(reinterpret_cast<unsigned char*>(&Target)-(jump+5));
    std::memcpy(jump+1,&jumpRelative,sizeof(jumpRelative));
    DWORD restored=0;
    CHECK(VirtualProtect(page,0x1000,PAGE_EXECUTE_READ,&restored)!=0);
    FlushInstructionCache(GetCurrentProcess(),page,0x1000);
    const auto call=reinterpret_cast<Call>(site);
    CHECK(call(21)==42);

    bool changed=false;
    // A mismatched expectation must leave the site alone.
    CHECK(!RedirectCall(site,reinterpret_cast<void*>(&Replacement),reinterpret_cast<void*>(&Replacement),changed));
    CHECK(!changed);
    CHECK(call(21)==42);
    CHECK(!RedirectCall(page+0x80,reinterpret_cast<void*>(&Target),reinterpret_cast<void*>(&Replacement),changed));
    CHECK(!changed);

    CHECK(RedirectCall(site,reinterpret_cast<void*>(&Target),reinterpret_cast<void*>(&Replacement),changed));
    CHECK(changed);
    CHECK(call(21)==63);
    CHECK(g_redirected==1);

    const auto tail=reinterpret_cast<Call>(jump);
    CHECK(tail(21)==42);
    CHECK(!RedirectCall(jump,reinterpret_cast<void*>(&Target),reinterpret_cast<void*>(&Replacement),changed));
    CHECK(!changed);
    CHECK(!RedirectJump(site,reinterpret_cast<void*>(&Replacement),reinterpret_cast<void*>(&Replacement),changed));
    CHECK(!changed);
    CHECK(RedirectJump(jump,reinterpret_cast<void*>(&Target),reinterpret_cast<void*>(&Replacement),changed));
    CHECK(changed);
    CHECK(tail(21)==63);
    CHECK(g_redirected==2);
    VirtualFree(page,0,MEM_RELEASE);
}

static HeadBasis SupportAim(const Vec3& gap) {
    const float n=std::sqrt(gap.x*gap.x+gap.y*gap.y+gap.z*gap.z);
    HeadBasis h{};h.forward={gap.x/n,gap.y/n,gap.z/n};h.up={0,1,0};
    h.yaw=std::atan2(h.forward.x,h.forward.z);h.pitchUp=std::asin(h.forward.y);
    return h;
}

static void TestLateralSupportKeepsPitch() {
    constexpr float side=.0302f;
    // The support sits on the gun's lateral centreline. The old raw hand line
    // was yawed by the gun's 3 cm lateral placement. Correct that attachment
    // without changing the originally calibrated height or pitch at all.
    for(float yaw:{-2.9f,-.8f,0.f,1.2f,3.1f}) for(float reach:{.15f,.234f,.55f})
        for(float height:{-.25f,-.05f,0.f,.08f,.3f}) {
            const auto grip=FromAxisAngle(0,1,0,yaw);
            const auto gap=RotateY({-side,height,reach},yaw);
            auto aim=SupportAim(gap);const auto before=aim;
            CHECK(CorrectTwoHandLateralYaw(grip,gap,side,0,0,0,aim));
            CHECK(std::fabs(std::atan2(std::sin(aim.yaw-yaw),std::cos(aim.yaw-yaw)))<2e-5f);
            CHECK(aim.pitchUp==before.pitchUp && aim.forward.y==before.forward.y);
            CHECK(!std::memcmp(&aim.up,&before.up,sizeof aim.up));
            CHECK(std::fabs(aim.forward.x*aim.forward.x+aim.forward.y*aim.forward.y+aim.forward.z*aim.forward.z-1)<1e-5f);
        }
    // Reproduce the user's original aim-pose stock sample and placement/angle
    // trims. Model vertical placement is deliberately not a solver input.
    // Pitch stays bit-identical even with roll, angle calibration and a grip
    // rotation different from the aim rotation.
    const auto grip=FromAxisAngle(1,0,0,-.65f);
    const Vec3 gap{-.05f,-.05f,.22f};
    for(float roll:{0.f,.042f,.7f,1.57f}) for(float pitchTrim:{-.046f,.15f}) {
        auto aim=SupportAim(gap);const auto before=aim;
        CHECK(CorrectTwoHandLateralYaw(grip,gap,side,roll,.015f,pitchTrim,aim));
        CHECK(aim.pitchUp==before.pitchUp && aim.forward.y==before.forward.y);
        CHECK(std::clamp(aim.pitchUp+pitchTrim,-1.5f,1.5f)==std::clamp(before.pitchUp+pitchTrim,-1.5f,1.5f));
        CHECK(!std::memcmp(&aim.up,&before.up,sizeof aim.up));
    }
    auto aim=SupportAim(gap);const auto unchanged=aim;
    CHECK(CorrectTwoHandLateralYaw({},gap,0,0,0,0,aim));
    CHECK(!std::memcmp(&aim,&unchanged,sizeof aim));
    CHECK(!CorrectTwoHandLateralYaw({}, {0,.3f,0},side,0,0,0,aim)); // vertical: yaw undefined
    CHECK(!CorrectTwoHandLateralYaw({}, {.01f,0,.01f},side,0,0,0,aim));
    CHECK(!CorrectTwoHandLateralYaw({0,0,0,0},gap,side,0,0,0,aim));
    CHECK(!CorrectTwoHandLateralYaw({},gap,std::numeric_limits<float>::quiet_NaN(),0,0,0,aim));
    CHECK(!std::memcmp(&aim,&unchanged,sizeof aim));
}

// The one-handed trim rides on the controller: in the controller's own axes
// the trimmed barrel is the same vector however the controller is held --
// level, straight up, or carried on over the top toward the back -- and at the
// horizon it is still the old yaw-then-pitch trim.
static void TestTrimIsRigid() {
    const float yawRight=7.0686f*3.14159265f/180, pitchUp=-12.9656f*3.14159265f/180;
    const Vec3 local{std::sin(yawRight)*std::cos(pitchUp),std::sin(pitchUp),
                     -std::cos(yawRight)*std::cos(pitchUp)};
    HeadBasis level{};
    CHECK(HeadBasisFromXr(TrimAimRotation(Quat{},yawRight,pitchUp),level));
    CHECK(std::fabs(level.yaw+yawRight)<1e-4f);
    CHECK(std::fabs(level.pitchUp-pitchUp)<1e-4f);
    Vec3 last{};bool have=false;
    for(float turn:{0.f,1.3f,-2.6f}) for(float roll:{0.f,.9f,-1.6f})
        for(float pitch=-1.8f;pitch<=1.81f;pitch+=.02f) {
            const auto q=QuatMultiply(QuatMultiply(FromAxisAngle(0,1,0,turn),FromAxisAngle(1,0,0,pitch)),
                                      FromAxisAngle(0,0,1,roll));
            const auto trimmed=TrimAimRotation(q,yawRight,pitchUp);
            CHECK(NormalizedQuat(trimmed));
            const auto ahead=QuatRotate(trimmed,{0,0,-1});
            const auto back=QuatRotate(Quat{-q.x,-q.y,-q.z,q.w},ahead);
            CHECK(std::fabs(back.x-local.x)<1e-4f && std::fabs(back.y-local.y)<1e-4f
                  && std::fabs(back.z-local.z)<1e-4f);
            // Nothing jumps as the controller passes the vertical.
            if(have && pitch>-1.79f) {
                const float dx=ahead.x-last.x,dy=ahead.y-last.y,dz=ahead.z-last.z;
                CHECK(std::sqrt(dx*dx+dy*dy+dz*dz)<.03f);
            }
            last=ahead;have=true;
        }
}

// A hand placed about the eye with one yaw offset, turned onto another, is
// where it would have been placed with the second one.
void TestTurnAboutVertical() {
    const float eye[3]={450.f,1.6f,-380.f};
    const Vec3 room{.21f,-.33f,.47f},rows[3]={{.8f,.0f,.6f},{.0f,1.f,.0f},{-.6f,.0f,.8f}};
    const float first=.4f,second=.47f;
    float axes[3][3],point[3];
    for(int k=0;k<3;++k) { const Vec3 r=RotateY(rows[k],first); axes[k][0]=r.x; axes[k][1]=r.y; axes[k][2]=r.z; }
    const Vec3 at=RotateY(room,first);
    point[0]=eye[0]+at.x; point[1]=eye[1]+at.y; point[2]=eye[2]+at.z;
    TurnAboutVertical(axes,point,eye,second-first);
    for(int k=0;k<3;++k) {
        const Vec3 r=RotateY(rows[k],second);
        CHECK(std::fabs(axes[k][0]-r.x)<1e-5f && std::fabs(axes[k][1]-r.y)<1e-5f && std::fabs(axes[k][2]-r.z)<1e-5f);
    }
    const Vec3 want=RotateY(room,second);
    CHECK(std::fabs(point[0]-eye[0]-want.x)<1e-4f && std::fabs(point[1]-eye[1]-want.y)<1e-4f
          && std::fabs(point[2]-eye[2]-want.z)<1e-4f);
}
// Weapon scopes (scope.h): the field, the frustum, the camera and the lens.
static void TestScope() {
    auto approx=[](float a,float b,float e){return std::fabs(a-b)<=e;};
    CHECK(approx(ScopeTanHalf(.25f,4.f),.0625f,1e-6f));
    CHECK(approx(ScopeTanHalf(.25f,.5f),.25f,1e-6f));             // no magnification below 1
    CHECK(approx(ScopeTanHalf(5.f,1.f),1.f,1e-6f));               // clamped to a frustum's worth
    CHECK(ScopeTanHalf(std::numeric_limits<float>::quiet_NaN(),4.f)==0);
    // Umbra's raw frustum (the VR camera's, as logged): made symmetric, the
    // height from the field, the width from the surface's aspect (never narrower).
    const float in[6]={-.148001f,.148001f,.137478f,-.137478f,.1f,1000.f};
    float out[6]{};
    CHECK(ScopeFrustum(in,.05f,1.f,out));
    CHECK(approx(out[2]/out[4],.05f,1e-5f) && approx(out[3]/out[4],-.05f,1e-5f));
    CHECK(approx(out[1]-out[0],out[2]-out[3],1e-7f) && out[0]<0 && out[1]>0);
    CHECK(out[4]==in[4] && out[5]==in[5]);
    CHECK(ScopeFrustum(in,.05f,16.f/9.f,out) && approx((out[1]-out[0])/(out[2]-out[3]),16.f/9.f,1e-4f));
    CHECK(ScopeFrustum(in,.05f,.5f,out) && approx(out[1]-out[0],out[2]-out[3],1e-7f));
    const float flat[6]={-.1f,.1f,0,0,.1f,1000.f};
    CHECK(!ScopeFrustum(flat,.05f,1.f,out));
    CHECK(!ScopeFrustum(in,.05f,std::numeric_limits<float>::quiet_NaN(),out));
    // The camera: the head turned onto the aim, moved to the muzzle.
    Matrix head{};head.m[0][0]=1;head.m[1][1]=1;head.m[2][2]=1;head.m[3][3]=1;
    head.m[3][0]=5;head.m[3][1]=1.6f;head.m[3][2]=-2;
    const float origin[3]={5.2f,1.4f,-1.6f};
    float forward[3]={.3f,.1f,1.f};
    Matrix scope{};
    CHECK(ScopeCamera(head,origin,forward,scope));
    const float lf=std::sqrt(.3f*.3f+.1f*.1f+1.f);
    CHECK(approx(scope.m[2][0],.3f/lf,1e-5f) && approx(scope.m[2][1],.1f/lf,1e-5f) && approx(scope.m[2][2],1.f/lf,1e-5f));
    CHECK(approx(scope.m[3][0],5.2f,1e-6f) && approx(scope.m[3][1],1.4f,1e-6f) && approx(scope.m[3][2],-1.6f,1e-6f));
    CHECK(scope.m[1][1]>.9f);                                    // the head's up kept, no roll
    const float* r0=scope.m[0];const float* r1=scope.m[1];const float* r2=scope.m[2];
    CHECK(approx(r0[0],r1[1]*r2[2]-r1[2]*r2[1],1e-5f) && approx(r0[1],r1[2]*r2[0]-r1[0]*r2[2],1e-5f));   // row0 = row1 x row2
    float same[3]={0,0,2};
    CHECK(ScopeCamera(head,origin,same,scope) && approx(scope.m[0][0],1,1e-6f) && approx(scope.m[1][1],1,1e-6f));
    float behind[3]={0,0,-1};
    CHECK(!ScopeCamera(head,origin,behind,scope));
    // The lens: the eyepiece through the weapon root, seen from the eye.
    Matrix root{};root.m[0][0]=1;root.m[1][1]=1;root.m[2][2]=1;root.m[3][3]=1;root.m[3][0]=1;root.m[3][1]=2;root.m[3][2]=3;
    const ScopeLensSpec spec{L"test",nullptr,0,{0.f,.1f,-.05f},.02f,.02f,{0.f,0.f,-1.f},{0.f,1.f,0.f},false};
    const float eye[3]={1.f,2.1f,2.5f};
    ScopeLensFrame lens{};
    CHECK(ScopeLensFrameFrom(root,spec,eye,1.f,lens));
    CHECK(approx(lens.centre[0],1,1e-6f) && approx(lens.centre[1],2.1f,1e-6f) && approx(lens.centre[2],2.95f,1e-6f));
    CHECK(approx(lens.normal[2],-1,1e-6f) && approx(lens.up[1],1,1e-6f));
    CHECK(approx(lens.lensTan,.02f/.45f,1e-5f));
    CHECK(ScopeLensFrameFrom(root,spec,eye,2.f,lens) && approx(lens.radius,.04f,1e-6f));
    const float onLens[3]={1.f,2.1f,2.95f};
    CHECK(!ScopeLensFrameFrom(root,spec,onLens,1.f,lens));      // the eye at the lens: no field
    // A screen in its node's frame: its own facing and up, rectangular.
    const ScopeLensSpec screen{L"screen",L"screen",1,{0.f,0.f,0.f},.04f,.02f,{0.f,-1.f,0.f},{0.f,0.f,-1.f},false};
    CHECK(ScopeLensFrameFrom(root,screen,eye,1.f,lens) && lens.shape==1);
    CHECK(approx(lens.normal[1],-1,1e-6f) && approx(lens.up[2],-1,1e-6f));
    CHECK(approx(lens.radius,.02f,1e-6f) && approx(lens.halfWidth,.04f,1e-6f));
    // The generic holographic monitor and the table's own rows.
    CHECK(ScopeHoloSpec().holo && !ScopeHoloSpec().node && ScopeHoloSpec().shape==0);
    // The Laser Guide Kit shows the picture on its three panels: three rows in a
    // row, the centre one leading; a scope has one.
    {
        int guide=-1,sniper=-1;
        for(int i=0;i<ScopeLensCount();++i) {
            const auto* row=ScopeLensAt(i);
            if(guide<0 && !std::wcscmp(row->node,L"e_targetmarker_normal01")) guide=i;
            if(sniper<0 && !std::wcscmp(row->node,L"s_sniper_mmf01")) sniper=i;
        }
        CHECK(guide>=0 && ScopeLensSurfaces(guide)==3 && sniper>=0 && ScopeLensSurfaces(sniper)==1);
        CHECK(guide>=0 && !std::wcscmp(ScopeLensAt(guide)->frame,L"screen") && !std::wcscmp(ScopeLensAt(guide+1)->frame,L"screen_l")
              && !std::wcscmp(ScopeLensAt(guide+2)->frame,L"screen_r"));
        ScopeLensFrame frames[2]{};frames[0].radius=1;frames[1].radius=2;
        PublishScopeLenses(frames,2);
        ScopeLensFrame back[kScopeMaxSurfaces]{};
        CHECK(ReadScopeLenses(back)==2 && back[1].radius==2 && ReadScopeLens().radius==1);
        PublishScopeLenses(nullptr,0);
    }
    for(int i=0;i<ScopeLensCount();++i) {
        const auto* row=ScopeLensAt(i);
        CHECK(row && row->node && row->halfWidth>0 && row->halfHeight>0 && (!row->holo || row->shape==0));
        if(row && !row->frame) CHECK(row->halfWidth>=.0244f && row->halfHeight>=.0244f);   // none too small to see
    }
    // One matrix for world -> clip agrees with the step-by-step projection.
    Matrix view{};view.m[0][0]=.8f;view.m[0][2]=.6f;view.m[1][1]=1;view.m[2][0]=-.6f;view.m[2][2]=.8f;view.m[3][3]=1;
    view.m[3][0]=.3f;view.m[3][1]=-1.2f;view.m[3][2]=4;
    Matrix projection{};projection.m[0][0]=.7f;projection.m[1][1]=1.2f;projection.m[2][2]=1.0001f;projection.m[2][3]=1;projection.m[3][2]=-.1f;
    const float point[3]={2,1,7};
    float clip[4]{};
    CHECK(ScopeClip(view,projection,point,clip));
    const Matrix m=ScopeWorldToClip(view,projection);
    for(int j=0;j<4;++j) {
        const float v=point[0]*m.m[0][j]+point[1]*m.m[1][j]+point[2]*m.m[2][j]+m.m[3][j];
        CHECK(approx(v,clip[j],1e-4f));
    }
    // A rigid view's inverse is its camera.
    const Matrix camera=ScopeCameraOf(view);
    float back[3]{};
    for(int j=0;j<3;++j) back[j]=camera.m[3][0]*view.m[0][j]+camera.m[3][1]*view.m[1][j]+camera.m[3][2]*view.m[2][j]+view.m[3][j];
    CHECK(approx(back[0],0,1e-5f) && approx(back[1],0,1e-5f) && approx(back[2],0,1e-5f));
    CHECK(ScopeLensCount()>=10 && ScopeLensAt(0) && !ScopeLensAt(-1) && !ScopeLensAt(ScopeLensCount()));
}

int main() {
    {
        // The Nix's arm aim: a controller's ray into the world exactly as the
        // view is put there (the head's own forward lands on the view's).
        Matrix seat{};
        const float yawSeat=0.7f;
        seat.m[0][0]=std::cos(yawSeat); seat.m[0][2]=-std::sin(yawSeat);
        seat.m[1][1]=1;
        seat.m[2][0]=std::sin(yawSeat); seat.m[2][2]=std::cos(yawSeat);
        seat.m[3][0]=10; seat.m[3][1]=7; seat.m[3][2]=-3; seat.m[3][3]=1;
        const Quat reference=FromAxisAngle(0,1,0,0.4f);
        const Quat head=FromAxisAngle(0.3f,0.9f,0.2f,0.8f);
        const float hl=std::sqrt(head.x*head.x+head.y*head.y+head.z*head.z+head.w*head.w);
        const Quat q{head.x/hl,head.y/hl,head.z/hl,head.w/hl};
        Matrix view{}; float localYaw=0;
        CHECK(ComposeVehicleCamera(seat,reference,q,Vec3{0.1f,-0.2f,0.3f},view,localYaw));
        Vec3 point{},direction{};
        CHECK(VehicleReferenceToWorld(seat,reference,Vec3{0.1f,-0.2f,0.3f},QuatRotate(q,Vec3{0,0,-1}),point,direction));
        CHECK(std::fabs(direction.x-view.m[2][0])<1e-4f && std::fabs(direction.y-view.m[2][1])<1e-4f && std::fabs(direction.z-view.m[2][2])<1e-4f);
        CHECK(std::fabs(point.x-view.m[3][0])<1e-4f && std::fabs(point.y-view.m[3][1])<1e-4f && std::fabs(point.z-view.m[3][2])<1e-4f);
        // The cone: 45 degrees to the side cut to 20, 30 up cut to 20, 5 left alone.
        const float d=3.14159265f/180.0f;
        Vec3 out{}; bool cut=false;
        CHECK(AimCone(Vec3{0,0,1},Vec3{1,0,1},20*d,out,&cut) && cut);
        CHECK(std::fabs(std::acos(out.z)-20*d)<1e-3f && std::fabs(out.y)<1e-5f && out.x>0);
        CHECK(AimCone(Vec3{0,0,1},Vec3{0,std::sin(30*d),std::cos(30*d)},20*d,out,&cut) && cut && std::fabs(out.y-std::sin(20*d))<1e-4f);
        CHECK(AimCone(Vec3{0,0,1},Vec3{std::sin(5*d),0,std::cos(5*d)},20*d,out,&cut) && !cut
              && std::fabs(out.x-std::sin(5*d))<1e-4f && std::fabs(out.z-std::cos(5*d))<1e-4f);
        // Behind: still only 20 degrees round.
        CHECK(AimCone(Vec3{0,0,1},Vec3{0.1f,0,-1},20*d,out,&cut) && cut && std::fabs(std::acos(out.z)-20*d)<1e-3f);
        // The turn takes the one direction onto the other and is a rotation.
        float M[3][3]{};
        const Vec3 a{0.2f,-0.1f,1.0f},b{0.5f,0.2f,0.8f};
        CHECK(TurnBetween(a,b,M));
        const float la=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z),lb=std::sqrt(b.x*b.x+b.y*b.y+b.z*b.z);
        float turned[3]{};
        for(int j=0;j<3;++j) turned[j]=(a.x*M[0][j]+a.y*M[1][j]+a.z*M[2][j])/la;
        CHECK(std::fabs(turned[0]-b.x/lb)<1e-4f && std::fabs(turned[1]-b.y/lb)<1e-4f && std::fabs(turned[2]-b.z/lb)<1e-4f);
        const float det=M[0][0]*(M[1][1]*M[2][2]-M[1][2]*M[2][1])-M[0][1]*(M[1][0]*M[2][2]-M[1][2]*M[2][0])+M[0][2]*(M[1][0]*M[2][1]-M[1][1]*M[2][0]);
        CHECK(std::fabs(det-1)<1e-4f);
        CHECK(TurnBetween(a,a,M) && M[0][0]==1 && M[1][1]==1 && M[2][2]==1 && M[0][1]==0);
        CHECK(!TurnBetween(Vec3{0,0,1},Vec3{0,0,-1},M));
    }
    {
        // The headset's own recenter seen as LOCAL stepping against STAGE.
        const Vec3 p{1,0,2};
        const Quat q=FromAxisAngle(0,1,0,0.5f);
        CHECK(!ReferenceSpaceJumped(p,q,p,q));
        CHECK(!ReferenceSpaceJumped(p,q,Vec3{1.01f,0,2},q));            // 1 cm
        CHECK(ReferenceSpaceJumped(p,q,Vec3{1.05f,0,2},q));             // 5 cm
        CHECK(!ReferenceSpaceJumped(p,q,p,FromAxisAngle(0,1,0,0.5f+0.017f)));   // 1 degree
        CHECK(ReferenceSpaceJumped(p,q,p,FromAxisAngle(0,1,0,0.5f+0.06f)));     // 3.4 degrees
        CHECK(ReferenceSpaceJumped(p,q,p,FromAxisAngle(0,1,0,0.5f+3.14159f)));  // turned round
        const Quat minus{-q.x,-q.y,-q.z,-q.w};
        CHECK(!ReferenceSpaceJumped(p,q,p,minus));                      // the same turn
        CHECK(!ReferenceSpaceJumped(p,q,Vec3{std::numeric_limits<float>::quiet_NaN(),0,2},q));
    }
    TestTrimIsRigid();
    TestLateralSupportKeepsPitch();
    // Same soldier shot at different recenter headings has the same tracking
    // direction. Camera/head rotation is deliberately not an input here.
    for(float offset:{0.0f,1.2f,-600.0f}) {
        const auto a=AimToReference(offset+0.4f,0.25f,offset);
        CHECK(std::fabs(a.x+std::sin(0.4f)*std::cos(0.25f))<0.00005f);
        CHECK(std::fabs(a.y-std::sin(0.25f))<0.00005f);
        CHECK(std::fabs(a.z+std::cos(0.4f)*std::cos(0.25f))<0.00005f);
    }
    // Vehicle presentation inherits native slope/turn, recenters all 3 rotation
    // axes, and applies tracking translation once at metre scale.
    const Matrix native{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{100,200,300,1}}};
    const Quat ref=FromAxisAngle(0,1,0,.7f);
    Matrix out{};float yaw=0;
    CHECK(ComposeVehicleCamera(native,ref,ref,{},out,yaw));
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(std::fabs(out.m[i][j]-native.m[i][j])<.0001f);
    CHECK(ComposeVehicleCamera(native,Quat{},Quat{},{.3f,.2f,-.4f},out,yaw));
    CHECK(std::fabs(out.m[3][0]-99.7f)<.0001f && std::fabs(out.m[3][1]-200.2f)<.0001f && std::fabs(out.m[3][2]-300.4f)<.0001f);
    Matrix slope=native;float roll=0;
    CHECK(RollCameraToUp(native,{-.5f,.8660254f,0},slope,roll));
    CHECK(ComposeVehicleCamera(slope,ref,ref,{},out,yaw));
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(std::fabs(out.m[i][j]-slope.m[i][j])<.0001f);
    // Boarding while looking up/down and rolled establishes a level heading,
    // not that tilted head pose as the seat's permanent forward direction.
    for(float pitch:{-.8f,.7f,1.45f})for(float turn:{-.9f,.5f}) {
        const auto h=FromAxisAngle(0,1,0,turn),p=FromAxisAngle(1,0,0,pitch);
        const Quat tilted{h.w*p.x,h.y*p.w,-h.y*p.x,h.w*p.w};Quat level{};
        CHECK(VehicleHeadingReference(tilted,level));CHECK(level.x==0&&level.z==0);
        CHECK(ComposeVehicleCamera(native,level,h,{},out,yaw));
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)CHECK(std::fabs(out.m[i][j]-native.m[i][j])<.0001f);
        CHECK(ComposeVehicleCamera(slope,level,h,{},out,yaw));
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)CHECK(std::fabs(out.m[i][j]-slope.m[i][j])<.0001f);
        CHECK(ComposeVehicleCamera(native,level,tilted,{},out,yaw));CHECK(std::fabs(out.m[2][1]-std::sin(pitch))<.0001f);
    }
    CHECK(ComposeVehicleCamera(native,Quat{},FromAxisAngle(0,1,0,.5f),{},out,yaw));
    CHECK(std::fabs(yaw-.5f)<.0001f);
    float x=0,y=1;RebaseVehicleStick(1.57079633f,x,y);CHECK(std::fabs(x+1)<.0001f && std::fabs(y)<.0001f);
    x=y=0;RebaseVehicleStick(.8f,x,y);CHECK(x==0 && y==0); // HMD alone never injects input.
    {   // The stick's forward is the machine's: a hull a quarter turn left of
        // the native camera turns stick-forward into stick-left, whatever the
        // camera's pitch; a hull straight ahead leaves the stick alone.
        Matrix camera=native;float stick=0;
        CHECK(VehicleStickYaw(camera,{camera.m[0][0],camera.m[0][1],camera.m[0][2]},stick)&&std::fabs(stick-1.57079633f)<.0001f);
        x=0;y=1;RebaseVehicleStick(stick,x,y);CHECK(std::fabs(x+1)<.0001f&&std::fabs(y)<.0001f);
        CHECK(VehicleStickYaw(camera,{camera.m[2][0],camera.m[2][1],camera.m[2][2]},stick)&&std::fabs(stick)<.0001f);
        Matrix down{};CHECK(RotateCamera(native,0,-35,down));
        CHECK(VehicleStickYaw(down,{-native.m[0][0],-native.m[0][1],-native.m[0][2]},stick)&&std::fabs(stick+1.57079633f)<.0001f);
        CHECK(!VehicleStickYaw(camera,{0,1,0},stick));   // a hull on its nose has no heading
    }
    {   // The gun hand as a right stick in a vehicle: above the horizon is up,
        // left of the cabin's front is left, a small deadzone straight ahead,
        // full at the full angle; the cabin's heading is the reference.
        constexpr float d=.01745329252f,dead=3*d,full=30*d;
        auto aim=[&](float yawLeft,float pitchUp){return QuatMultiply(FromAxisAngle(0,1,0,yawLeft*d),FromAxisAngle(1,0,0,pitchUp*d));};
        const Quat ahead{};HandAimAngles angles{};
        CHECK(HandAimStick(ahead,aim(0,0),0,dead,full,x,y)&&x==0&&y==0);
        CHECK(HandAimStick(ahead,aim(2,-2.5f),0,dead,full,x,y)&&x==0&&y==0);          // inside the deadzone: still
        CHECK(HandAimStick(ahead,aim(0,16.5f),0,dead,full,x,y,&angles)&&x==0&&std::fabs(y-.5f)<.001f&&std::fabs(angles.up-16.5f*d)<.0001f);
        CHECK(HandAimStick(ahead,aim(0,-45),0,dead,full,x,y)&&x==0&&y==-1);           // past full: a full push
        CHECK(HandAimStick(ahead,aim(16.5f,0),0,dead,full,x,y)&&std::fabs(x+.5f)<.001f&&std::fabs(y)<.0001f);   // left
        CHECK(HandAimStick(ahead,aim(-60,0),0,dead,full,x,y)&&x==1&&std::fabs(y)<.0001f);                       // right
        CHECK(HandAimStick(ahead,aim(0,90),0,dead,full,x,y)&&std::fabs(x)<.0001f&&y==1);                        // straight up: no spin
        // The cabin's heading, not the room's: a cabin a quarter turn left, the
        // hand along it, is straight ahead; the head plays no part.
        const Quat cabin=FromAxisAngle(0,1,0,90*d);
        CHECK(HandAimStick(cabin,aim(90,0),0,dead,full,x,y)&&std::fabs(x)<.0001f&&std::fabs(y)<.0001f);
        CHECK(HandAimStick(cabin,aim(60,10),0,dead,full,x,y)&&x>.9f&&std::fabs(y-7.f/27)<.01f);
        CHECK(!HandAimStick(ahead,Quat{0,0,0,0},0,dead,full,x,y)&&x==0&&y==0);
        CHECK(!HandAimStick(ahead,aim(0,20),0,full,dead,x,y));                    // full inside the deadzone
        // The user's settings: level at 14 up, deadzone 10, full at 15. Held at
        // 14 up it is still; 24 up is the deadzone's edge; 29 up is full;
        // the horizon itself is 14 below level, past the deadzone: a push down.
        const float level=14*d,dead2=10*d,full2=15*d;
        CHECK(HandAimStick(ahead,aim(0,14),level,dead2,full2,x,y,&angles)&&x==0&&y==0&&std::fabs(angles.up-14*d)<.0001f);
        CHECK(HandAimStick(ahead,aim(0,23.9f),level,dead2,full2,x,y)&&y==0);
        CHECK(HandAimStick(ahead,aim(0,26.5f),level,dead2,full2,x,y)&&std::fabs(y-.5f)<.001f);
        CHECK(HandAimStick(ahead,aim(0,29.5f),level,dead2,full2,x,y)&&y==1);
        CHECK(HandAimStick(ahead,aim(0,0),level,dead2,full2,x,y)&&std::fabs(y+.8f)<.001f);
        // Which switch each cabin answers to (the user's grouping).
        CHECK(HandAimClassOf(CockpitKind::Nix)==HandAimClass::Nix&&HandAimClassOf(CockpitKind::NixChest)==HandAimClass::Nix);
        CHECK(HandAimClassOf(CockpitKind::Crawler)==HandAimClass::Depth&&HandAimClassOf(CockpitKind::Barga)==HandAimClass::Barga);
        CHECK(HandAimClassOf(CockpitKind::Tank)==HandAimClass::Tank&&HandAimClassOf(CockpitKind::CombatCaliban)==HandAimClass::Combat);
        CHECK(HandAimClassOf(CockpitKind::Heli602)==HandAimClass::Heli&&HandAimClassOf(CockpitKind::HeliBrute)==HandAimClass::Heli);
        CHECK(HandAimClassOf(CockpitKind::ProteusDriver)==HandAimClass::Gunner&&HandAimClassOf(CockpitKind::ProteusMissile)==HandAimClass::Gunner);
        CHECK(HandAimClassOf(CockpitKind::TitanGunner)==HandAimClass::Gunner&&HandAimClassOf(CockpitKind::HeliBruteGunner)==HandAimClass::BruteGunner);
        CHECK(HandAimClassOf(CockpitKind::TruckPickup)==HandAimClass::None);
        {   // A rider's look: the file its body model was loaded from (body model +0x88 -> +0x20 -> the path).
            static unsigned char soldier[0x1000];static unsigned char block[512];static wchar_t name[128];
            auto wear=[&](const wchar_t* path){std::memset(block,0,sizeof(block));std::memset(name,0,sizeof(name));wcscpy_s(name,path);
                const auto q=reinterpret_cast<std::uint64_t>(name);std::memcpy(block+0x20,&q,8);
                const auto p=reinterpret_cast<std::uint64_t>(block);std::memcpy(soldier+kBodyModelOffset+0x88,&p,8);};
            wear(L"APP:\\OBJECT\\P505_RANGER.MRAB");CHECK(CrewLookOf(soldier,1)==1&&CrewLookOf(soldier,2)==-1);
            wear(L"APP:\\OBJECT\\p601_proto_ranger.MRAB");CHECK(CrewLookOf(soldier,1)==2);
            wear(L"APP:\\OBJECT\\P504_PROTO_AIRRADER.MRAB");CHECK(CrewLookOf(soldier,3)==3);
            wear(L"APP:\\OBJECT\\P605_RANGER_X.MRAB");CHECK(CrewLookOf(soldier,1)==-1);
            std::memset(soldier,0,sizeof(soldier));CHECK(CrewLookOf(soldier,1)==-1&&CrewLookOf(nullptr,1)==-1);
            // Its colours: the block (0.5,0.5,0.5,0) | colour 1 | colour 2 (4th 0.5),
            // by the player's route (body model +0x458 -> +0xF0), a friend's
            // (+0xF0 -> +0x40 -> +0x30), or found by the search; a near miss is no block.
            const float colours[12]={.5f,.5f,.5f,0, .361f,0,.439f,0, .98f,.8624f,.9722f,.5f};
            alignas(8) static float block2[16];alignas(8) static unsigned char table[0x200],a1[0x200],b1[0x200],c1[0x200];
            float main[4]{},sub[4]{};int route=-1;
            auto put=[](unsigned char* at,const void* p){const auto v=reinterpret_cast<std::uint64_t>(p);std::memcpy(at,&v,8);};
            std::memcpy(block2,colours,sizeof(colours));put(table+0xF0,block2);put(soldier+kBodyModelOffset+0x458,table);
            CHECK(CrewColoursOf(soldier,main,sub,&route)&&route==2&&main[0]==.361f&&main[2]==.439f&&sub[1]==.8624f&&main[3]==1&&sub[3]==1);
            std::memset(soldier,0,sizeof(soldier));std::memset(table,0,sizeof(table));
            put(a1+0x40,b1);put(b1+0x30,block2);put(soldier+kBodyModelOffset+0xF0,a1);
            static unsigned char other[0x3000];put(other+kBodyModelOffset+0xF0,a1);
            CHECK(CrewColoursOf(other,main,sub,&route)&&route==1&&sub[0]==.98f);
            static unsigned char third[0x3000];put(third+0x30,c1);put(c1+0x120,block2);   // two deep, no known route
            CHECK(CrewColoursOf(third,main,sub,&route)&&route==4&&main[1]==0);
            // A page of its own, so the search finds nothing past it.
            static unsigned char fourth[0x3000];auto* wrong=static_cast<float*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
            std::memcpy(wrong,colours,sizeof(colours));wrong[1]=.49f;put(fourth+0x40,wrong);
            CHECK(!CrewColoursOf(fourth,main,sub,&route)&&route==0);
            CHECK(!CrewColoursOf(nullptr,main,sub));
        }
        CHECK(HandAimStick(ahead,aim(12,14),level,dead2,full2,x,y)&&std::fabs(x+.328f)<.005f&&y==0);   // left, at level (11.64 deg across)
    }
    CHECK(!ComposeVehicleCamera(native,Quat{},Quat{0,0,0,0},{},out,yaw));
    CHECK(!ComposeVehicleCamera(native,Quat{},Quat{},{0,0,100},out,yaw));
    VehicleEntryLevel entry{};Matrix entryCamera{},nextCamera{},entryView{},nextView{};
    CHECK(RotateCamera(native,0,-25,entryCamera));CHECK(LevelVehicleAtEntry(entry,entryCamera,entryView));
    CHECK(std::fabs(entryView.m[2][1])<.0001f);
    CHECK(RotateCamera(native,0,-10,nextCamera));CHECK(LevelVehicleAtEntry(entry,nextCamera,nextView));
    CHECK(std::fabs(nextView.m[2][1]-std::sin(15.f*.01745329252f))<.0001f); // later pitch is inherited
    CHECK(nextView.m[3][0]==native.m[3][0]&&nextView.m[3][1]==native.m[3][1]&&nextView.m[3][2]==native.m[3][2]);
    // A cabin view is levelled about its own right row: the forward row comes
    // to the horizon, the right row is untouched, and up stays up.
    for(float p:{0.52359878f,-0.3f,1.2f}) {
        const float c=std::cos(p),sn=std::sin(p);
        const Matrix pitched{{{1,0,0,0},{0,c,sn,0},{0,-sn,c,0},{100,200,300,1}}};
        Matrix levelled{};float measured=0;
        CHECK(LevelCameraPitch(pitched,levelled,measured));
        CHECK(std::fabs(measured-p)<.0001f);
        CHECK(std::fabs(levelled.m[2][1])<.0001f);
        CHECK(levelled.m[1][1]>.9999f);
        for(int j=0;j<3;++j) CHECK(levelled.m[0][j]==pitched.m[0][j]);
        for(int j=0;j<4;++j) CHECK(levelled.m[3][j]==pitched.m[3][j]);
    }
    // Already level: nothing moves, and nothing is reported.
    Matrix keep{};float none=1;
    CHECK(LevelCameraPitch(native,keep,none));
    CHECK(none==0);
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(keep.m[i][j]==native.m[i][j]);
    // Slope roll rides through: the right row is kept, so the lean is kept.
    Matrix rolledLevel{};float rolledPitch=0;
    CHECK(LevelCameraPitch(slope,rolledLevel,rolledPitch));
    CHECK(std::fabs(rolledLevel.m[2][1])<.0001f);
    for(int j=0;j<3;++j) CHECK(rolledLevel.m[0][j]==slope.m[0][j]);
    TestHeadBasis();
    TestMappingIsARotation();
    TestRotateY();
    TestRoll();
    TestMoveBasis();
    TestRedirect();
    TestScope();
    TestTurnAboutVertical();
    printf(failures?"FAILURES %d\n":"vr math and call redirection OK\n",failures);
    // The gun camera's angles found in memory by watching them move: a float
    // that follows the yaw in radians and one that follows the pitch in
    // negative degrees are picked out of noise and constants.
    {
        AngleFieldFinder finder;finder.Reset(16);
        float knownYaw=0,knownPitch=0,region[16]{};
        auto fill=[&](int step){for(int i=0;i<16;++i)region[i]=float(i)*.37f+std::sin(float(step*7+i))*.5f;
            region[3]=knownYaw;region[7]=-knownPitch*57.2957795f;region[9]=1.f;};
        for(int step=0;step<40;++step) {
            float dy=0,dp=0;
            if(step%2)dy=.03f;else dp=-.02f;
            if(step==0){dy=dp=0;}
            knownYaw+=dy;knownPitch+=dp;fill(step);
            finder.Sample(region,dy,dp);
        }
        AngleFieldFinder::Best yawBest[2]{},pitchBest[2]{};
        CHECK(finder.Top(false,yawBest,2)>=1);CHECK(finder.Top(true,pitchBest,2)>=1);
        CHECK(yawBest[0].offset==12&&yawBest[0].kind==AngleFieldFinder::kRadians&&yawBest[0].hits==finder.YawEvents());
        CHECK(pitchBest[0].offset==28&&pitchBest[0].kind==AngleFieldFinder::kNegDegrees&&pitchBest[0].hits==finder.PitchEvents());
        CHECK(finder.YawEvents()==20&&finder.PitchEvents()==19);
    }
    {
        // The view turning with the body (body_tumble.h).
        using namespace edf6vr;
        const float deg=0.0174532925f;
        // Waist rows pitched back by a (about +X, the body's right), spine along its up row.
        auto pitched=[&](float a,float rows[3][3],float spine[3]) {
            const float c=std::cos(a*deg),s=std::sin(a*deg);
            const float r[3][3]={{1,0,0},{0,c,s},{0,-s,c}};
            for(int i=0;i<3;++i) for(int j=0;j<3;++j) rows[i][j]=r[i][j];
            for(int j=0;j<3;++j) spine[j]=r[1][j];
        };
        BodyTumble t{}; float rows[3][3]{},spine[3]{},R[3][3]{};
        pitched(0,rows,spine); CHECK(UpdateBodyTumble(t,rows,spine,25,45,75,R));
        CHECK(t.haveRef && std::fabs(R[0][0]-1)<1e-5f && std::fabs(R[1][1]-1)<1e-5f);      // upright: no turn
        pitched(20,rows,spine); UpdateBodyTumble(t,rows,spine,25,45,75,R);
        CHECK(t.applied==0 && std::fabs(R[2][2]-1)<1e-5f);                                // a lean: nothing
        pitched(0,rows,spine); UpdateBodyTumble(t,rows,spine,25,45,75,R);                // upright again: reference kept
        pitched(40,rows,spine); UpdateBodyTumble(t,rows,spine,25,45,75,R);
        CHECK(std::fabs(t.angle-40)<0.1f && t.applied==0);                                // tipped, under the start
        pitched(60,rows,spine); UpdateBodyTumble(t,rows,spine,25,45,75,R);
        CHECK(std::fabs(t.applied-30)<0.2f);                                               // half way through the ease
        pitched(90,rows,spine); UpdateBodyTumble(t,rows,spine,25,45,75,R);
        float up[3]={0,1,0},turned[3]{}; TumbleApply(R,up,turned);
        CHECK(std::fabs(t.applied-90)<0.2f && std::fabs(turned[2]-1)<1e-3f);             // all of it: up goes where the spine went
        // A whole back flip: the view's up follows the spine all the way round, no jump through upside down.
        float last[3]={0,1,0}; bool smooth=true,followed=true;
        for(int a=90;a<=270;a+=5) {
            pitched(static_cast<float>(a),rows,spine); UpdateBodyTumble(t,rows,spine,25,45,75,R);
            TumbleApply(R,up,turned);
            const float step=std::acos(std::clamp(turned[0]*last[0]+turned[1]*last[1]+turned[2]*last[2],-1.0f,1.0f))/deg;
            if(a>90 && step>5.5f) smooth=false;
            if(std::fabs(turned[0]-spine[0])+std::fabs(turned[1]-spine[1])+std::fabs(turned[2]-spine[2])>1e-3f) followed=false;
            for(int j=0;j<3;++j) last[j]=turned[j];
        }
        CHECK(smooth && followed);
        // Scaling about a half turn keeps the axis: a quarter of 180 about X is 45 about X.
        float half[3][3]={{1,0,0},{0,-1,0},{0,0,-1}},quarter[3][3]{};
        TumbleScale(half,0.25f,quarter);
        CHECK(std::fabs(TumbleAngle(quarter)/deg-45)<0.1f && std::fabs(quarter[0][0]-1)<1e-4f);
        // Bad input changes nothing.
        float zero[3][3]{}; CHECK(!UpdateBodyTumble(t,zero,spine,25,45,75,R));
        // Only a quick tip turns the view: a flip reaches 45 at once, a free-fall pose settles in slowly.
        BodyTumble q{};
        pitched(0,rows,spine); UpdateBodyTumble(q,rows,spine,25,45,75,R,10.0,0.35);
        pitched(30,rows,spine); UpdateBodyTumble(q,rows,spine,25,45,75,R,10.05,0.35);
        pitched(90,rows,spine); UpdateBodyTumble(q,rows,spine,25,45,75,R,10.15,0.35);
        CHECK(q.followed && std::fabs(q.applied-90)<0.2f);
        pitched(0,rows,spine); UpdateBodyTumble(q,rows,spine,25,45,75,R,11.0,0.35);
        CHECK(q.spellDone && q.doneFollowed && std::fabs(q.doneMax-90)<0.2f && q.doneReachMs>0 && q.doneReachMs<200);
        BodyTumble slow{};
        pitched(0,rows,spine); UpdateBodyTumble(slow,rows,spine,25,45,75,R,20.0,0.35);
        pitched(30,rows,spine); UpdateBodyTumble(slow,rows,spine,25,45,75,R,20.1,0.35);
        pitched(44,rows,spine); UpdateBodyTumble(slow,rows,spine,25,45,75,R,20.4,0.35);
        pitched(95,rows,spine); UpdateBodyTumble(slow,rows,spine,25,45,75,R,20.8,0.35);
        CHECK(!slow.followed && slow.applied==0 && std::fabs(R[1][1]-1)<1e-5f);          // held level all spell
        pitched(0,rows,spine); UpdateBodyTumble(slow,rows,spine,25,45,75,R,22.0,0.35);
        CHECK(slow.spellDone && !slow.doneFollowed);
    }
    {
        // Left-handed mode: the whole controller state trades hands; the sticks can go back alone.
        edf6vr::ControllerState s{};
        s.present[0]=true; s.trigger[0]=.25f; s.trigger[1]=.75f; s.squeeze[1]=.5f; s.squeezeIsForce[0]=true;
        s.stick[0][0]=-.5f; s.stick[0][1]=.3f; s.stick[1][0]=.9f; s.stick[1][1]=-.1f;
        s.stickClick[1]=true; s.stickTouch[0]=true; s.lower[0]=true; s.upper[1]=true; s.menu[0]=true;
        edf6vr::ControllerState w=s; edf6vr::SwapControllerHands(w);
        CHECK(w.present[1] && !w.present[0] && w.trigger[1]==.25f && w.trigger[0]==.75f && w.squeeze[0]==.5f && w.squeezeIsForce[1]);
        CHECK(w.stick[1][0]==-.5f && w.stick[1][1]==.3f && w.stick[0][0]==.9f && w.stick[0][1]==-.1f);
        CHECK(w.stickClick[0] && !w.stickClick[1] && w.stickTouch[1] && w.lower[1] && w.upper[0] && w.menu[1]);
        edf6vr::ControllerState twice=w; edf6vr::SwapControllerHands(twice);
        CHECK(!std::memcmp(&twice,&s,sizeof(s)));
        edf6vr::ControllerState sticksBack=w; edf6vr::UnswapControllerSticks(sticksBack);
        CHECK(sticksBack.stick[0][0]==-.5f && sticksBack.stick[1][0]==.9f && sticksBack.stickClick[1] && sticksBack.stickTouch[0]);
        CHECK(sticksBack.trigger[1]==.25f && sticksBack.lower[1]);   // everything else stays swapped
        // The holster's temple zone on the right side when mirrored.
        const float head[3]={0,1.6f,0},leftTemple[3]={.18f,1.6f,0},rightTemple[3]={-.18f,1.6f,0};
        CHECK(edf6vr::InLeftShoulder(head,leftTemple,0,false) && !edf6vr::InLeftShoulder(head,rightTemple,0,false));
        CHECK(edf6vr::InLeftShoulder(head,rightTemple,0,false,.18f,0,0,.15f,.19f,.05f,.09f,true)
              && !edf6vr::InLeftShoulder(head,leftTemple,0,false,.18f,0,0,.15f,.19f,.05f,.09f,true));
    }
    return failures?1:0;
}
