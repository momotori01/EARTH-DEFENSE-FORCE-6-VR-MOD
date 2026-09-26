// Checks the OpenXR-to-EDF6 conversions and the call-site redirection helper.
// No game code runs here; the redirection test builds its own call instruction.
#include "vr_math.h"
#include "vehicle_camera.h"
#include "image_profile.h"
#include "weapon_hold.h"
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
int main() {
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
    TestTurnAboutVertical();
    printf(failures?"FAILURES %d\n":"vr math and call redirection OK\n",failures);
    return failures?1:0;
}
