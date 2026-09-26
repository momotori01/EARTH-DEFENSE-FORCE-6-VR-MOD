#include "render_pose.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace edf6vr;
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
using Row=std::map<std::string,std::string>;
std::vector<Row> Csv(const std::string& path) {
    std::ifstream in(path);CHECK(in.good());std::string line;std::vector<std::string> header;std::vector<Row> rows;
    while(std::getline(in,line)) {
        if(line.empty()||line[0]=='#') continue;
        if(line.back()=='\r') line.pop_back();
        std::stringstream ss(line);std::string cell;std::vector<std::string> fields;
        while(std::getline(ss,cell,',')) fields.push_back(cell);
        if(header.empty()) {header=fields;continue;}
        CHECK(fields.size()==header.size());Row row;
        for(unsigned i=0;i<fields.size()&&i<header.size();++i) row[header[i]]=fields[i];
        rows.push_back(row);
    }
    return rows;
}
float F(const Row& r,const std::string& key) {return std::stof(r.at(key));}
std::int64_t I(const Row& r,const std::string& key) {return std::stoll(r.at(key));}
Matrix M(const Row& row,const char* prefix) {
    Matrix m{};for(int i=0;i<16;++i) m.m[i/4][i%4]=F(row,std::string(prefix)+std::to_string(i));return m;
}
HmdSample H(const Row& row) {
    HmdSample h{};h.orientation={F(row,"qx"),F(row,"qy"),F(row,"qz"),F(row,"qw")};
    h.position={F(row,"px"),F(row,"py"),F(row,"pz")};h.orientationValid=h.positionValid=I(row,"valid")!=0;
    h.frame=I(row,"sample");h.displayTime=I(row,"predicted");return h;
}
Matrix Identity() {Matrix m{};for(int i=0;i<4;++i)m.m[i][i]=1;return m;}
void Replay() {
    const std::string prefix=std::string(EDF6VR_SOURCE_DIR)+"/research/runtime/motion_0.110_20260912-042017-83800";
    const auto cameras=Csv(prefix+"_cameras.csv"),frames=Csv(prefix+"_frames.csv"),matches=Csv(prefix+"_matches.csv");
    std::ifstream in(prefix+"_frames.csv");std::string meta;std::getline(in,meta);
    const auto frequency=std::stoll(meta.substr(meta.find("frequency=")+10));
    std::map<std::int64_t,std::int64_t> expected;
    for(const auto& match:matches) if(!I(match,"ambiguous")) expected[I(match,"frame")]=I(match,"sample");
    RenderCameraHistory history;unsigned cursor=0,found=0,compared=0,different=0;
    for(const auto& frame:frames) {
        if(!I(frame,"viewReads")||I(frame,"count")!=2) continue;
        const auto now=I(frame,"viewAt");
        while(cursor<cameras.size()&&I(cameras[cursor],"qpc")<=now) {
            history.Record(M(cameras[cursor],"camera"),H(cameras[cursor]),I(cameras[cursor],"qpc"));++cursor;
        }
        HmdSample got{};const bool ok=history.Match(M(frame,"view"),now,frequency/2,got);CHECK(ok);
        if(ok) ++found;
        const auto match=expected.find(I(frame,"id"));
        if(ok&&match!=expected.end()) {++compared;if(got.frame!=static_cast<std::uint64_t>(match->second)) ++different;}
    }
    printf("Recorded native view replay: matched=%u independent unambiguous=%u different=%u\n",found,compared,different);
    CHECK(found==2223 && compared==2170 && different==0);
}
}
int main() {
    Matrix old=Identity(),newer=Identity();old.m[3][0]=10;newer.m[3][0]=11;
    Matrix view=Identity();view.m[3][0]=-10;
    HmdSample a{};a.orientationValid=a.positionValid=true;a.orientation={0,0,0,1};a.frame=1;
    HmdSample b=a;b.frame=2;
    RenderCameraHistory h;h.Record(old,a,100);h.Record(newer,b,110);HmdSample out{};
    CHECK(h.Match(view,120,50,out)&&out.frame==1); // latest shared camera is wrong
    CHECK(!h.Match(view,200,50,out)); // old session / stale camera rejected
    CHECK(!h.Match(view,90,50,out)); // no future camera allowed
    CHECK(!h.Match(Matrix{},120,50,out));
    {
        // The stick turn rides with the camera it built; none recorded, none given.
        RenderCameraHistory turned;float yaw=0;
        turned.Record(old,a,100,.25f);turned.Record(newer,b,110,.30f);
        CHECK(turned.Match(view,120,50,out,&yaw)&&out.frame==1&&yaw==.25f);
        RenderCameraHistory plain;plain.Record(old,a,100);yaw=0;
        CHECK(plain.Match(view,120,50,out,&yaw)&&!std::isfinite(yaw));
        CHECK(!NativeRenderYaw(yaw));   // nothing observed on this thread
    }
    // Small animation-basis scale errors are valid native matrices. At world
    // coordinates around 450m, a transpose used as the inverse introduces more
    // than the 2cm matching tolerance, despite an exactly matching camera.
    {
        RenderCameraHistory animated;auto jump=Identity(),nativeView=Identity();
        jump.m[0][0]=1.0001f;jump.m[1][1]=.9999f;jump.m[3][0]=450;jump.m[3][1]=14;
        nativeView.m[0][0]=1/jump.m[0][0];nativeView.m[1][1]=1/jump.m[1][1];
        nativeView.m[3][0]=-450/jump.m[0][0];nativeView.m[3][1]=-14/jump.m[1][1];
        animated.Record(jump,a,100);CHECK(animated.Match(nativeView,120,50,out)&&out.frame==a.frame);
    }
    for(unsigned i=0;i<140;++i) {b.frame=3+i;h.Record(newer,b,130+i);}
    CHECK(!h.Match(view,300,500,out)); // overwritten history cannot leak
    ConfigureRenderedPose(true);RecordRenderedCamera(old,a);RecordRenderedCamera(newer,b);
    BeginNativeRenderPose();ObserveNativeRenderView(view);
    bool foreign=false;std::thread t([&]{HmdSample other{};foreign=ConsumeNativeRenderPose(other);});t.join();CHECK(!foreign);
    CHECK(ConsumeNativeRenderPose(out)&&out.frame==1);CHECK(!ConsumeNativeRenderPose(out));
    // A moving vehicle can be consumed immediately after its camera write,
    // before tracing/bookkeeping or the next logic update. Every jump sample
    // must already have matching metadata at that publication boundary.
    Matrix shared=Identity();
    for(unsigned tick=0;tick<300;++tick) {
        auto jump=Identity();jump.m[3][0]=450.f+tick*.35f;
        jump.m[3][1]=8.f+std::sin(tick*.12f)*4; jump.m[3][2]=-350.f+tick*.8f;
        auto sample=a;sample.frame=1000+tick;
        PublishRenderedCamera(shared,jump,sample);
        Matrix nativeView=Identity();for(int axis=0;axis<3;++axis)nativeView.m[3][axis]=-shared.m[3][axis];
        BeginNativeRenderPose();ObserveNativeRenderView(nativeView);
        CHECK(ConsumeNativeRenderPose(out)&&out.frame==sample.frame);
    }
    RecordRenderedCamera(old,a);
    BeginNativeRenderPose();ObserveNativeRenderView(view);BeginNativeRenderPose();CHECK(!ConsumeNativeRenderPose(out));
    BeginNativeRenderPose();ObserveNativeRenderView(view);ResetRenderedPoseHistory();CHECK(!ConsumeNativeRenderPose(out));
    ConfigureRenderedPose(false);RecordRenderedCamera(old,a);BeginNativeRenderPose();ObserveNativeRenderView(view);CHECK(!ConsumeNativeRenderPose(out));
    // Current head looks forward at (2,3,4). Rendered head yawed 90 degrees at
    // (5,6,7). Eye cant remains +/-10deg; IPD rotates onto the old Z axis.
    a.orientation={0,std::sqrt(.5f),0,std::sqrt(.5f)};a.position={5,6,7};
    XrSpaceLocation current{XR_TYPE_SPACE_LOCATION};current.locationFlags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
    current.pose={{0,0,0,1},{2,3,4}};
    XrView eyes[2]{};for(unsigned i=0;i<2;++i) {const float sign=i?1.0f:-1.0f;eyes[i].pose={{0,sign*std::sin(.08726646f),0,std::cos(.08726646f)},{2+sign*.032f,3,4}};}
    XrPosef poses[2]{};const auto flags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    CHECK(RenderedEyePoses(a,current,eyes,flags,2,poses));
    CHECK(std::fabs(poses[0].position.x-5)<1e-5f && std::fabs(poses[0].position.z-7.032f)<1e-5f);
    CHECK(std::fabs(poses[1].position.z-6.968f)<1e-5f);
    CHECK(std::fabs(poses[0].orientation.y-std::sin(.6981317f))<1e-5f); // 80 degrees
    CHECK(std::fabs(poses[1].orientation.y-std::sin(.8726646f))<1e-5f); // 100 degrees
    CHECK(!RenderedEyePoses(a,current,eyes,0,2,poses));CHECK(!RenderedEyePoses(a,current,eyes,flags,1,poses));
    eyes[1].pose.orientation={};const auto unchanged=poses[0];
    CHECK(!RenderedEyePoses(a,current,eyes,flags,2,poses));CHECK(poses[0].position.z==unchanged.position.z);
    // Two genuinely parallel images must not claim the runtime's +/-10 degree
    // optical cant used above. Both cameras share the rendered head rotation.
    CHECK(ParallelRenderedEyePoses(a,.064f,poses));
    CHECK(std::fabs(poses[0].position.z-7.032f)<1e-5f && std::fabs(poses[1].position.z-6.968f)<1e-5f);
    for(const auto& p:poses) {
        CHECK(std::fabs(p.orientation.y-std::sqrt(.5f))<1e-6f);
        CHECK(std::fabs(p.orientation.w-std::sqrt(.5f))<1e-6f);
    }
    const Quat rotations[]={
        {0,0,0,1},
        {std::sqrt(.5f),0,0,std::sqrt(.5f)},
        {0,0,std::sqrt(.5f),std::sqrt(.5f)},
        {.5f,.5f,.5f,.5f},
        {0,0,0,1.0001f}
    };
    for(const auto& q:rotations) {
        a.orientation=q;CHECK(ParallelRenderedEyePoses(a,.064f,poses));
        const auto l=poses[0].position,r=poses[1].position;
        CHECK(std::fabs((l.x+r.x)*.5f-a.position.x)<1e-6f);
        CHECK(std::fabs((l.y+r.y)*.5f-a.position.y)<1e-6f);
        CHECK(std::fabs((l.z+r.z)*.5f-a.position.z)<1e-6f);
        const auto pq=poses[0].orientation;
        const auto half=QuatRotate(Quat{pq.x,pq.y,pq.z,pq.w},Vec3{.032f,0,0});
        CHECK(std::fabs(r.x-l.x-half.x*2)<1e-6f);
        CHECK(std::fabs(r.y-l.y-half.y*2)<1e-6f);
        CHECK(std::fabs(r.z-l.z-half.z*2)<1e-6f);
        CHECK(std::fabs(std::sqrt((r.x-l.x)*(r.x-l.x)+(r.y-l.y)*(r.y-l.y)+(r.z-l.z)*(r.z-l.z))-.064f)<1e-6f);
        const auto rq=poses[1].orientation;
        CHECK(pq.x==rq.x && pq.y==rq.y && pq.z==rq.z && pq.w==rq.w);
        CHECK(std::fabs(pq.x*pq.x+pq.y*pq.y+pq.z*pq.z+pq.w*pq.w-1)<1e-6f);
    }
    const auto beforeFailure=poses[0];
    for(float invalidIpd:{0.0f,-.064f,.251f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        CHECK(!ParallelRenderedEyePoses(a,invalidIpd,poses));
    CHECK(poses[0].position.x==beforeFailure.position.x && poses[0].orientation.w==beforeFailure.orientation.w);
    CHECK(!ParallelRenderedEyePoses(a,.064f,nullptr));
    a.orientationValid=false;CHECK(!ParallelRenderedEyePoses(a,.064f,poses));a.orientationValid=true;
    a.positionValid=false;CHECK(!ParallelRenderedEyePoses(a,.064f,poses));a.positionValid=true;
    a.orientation={0,0,0,0};CHECK(!ParallelRenderedEyePoses(a,.064f,poses));
    a.orientation={0,0,0,2};CHECK(!ParallelRenderedEyePoses(a,.064f,poses));
    a.orientation={0,0,0,1};a.position.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!ParallelRenderedEyePoses(a,.064f,poses));
    Replay();printf("Render pose failures=%d\n",failures);return failures?1:0;
}
