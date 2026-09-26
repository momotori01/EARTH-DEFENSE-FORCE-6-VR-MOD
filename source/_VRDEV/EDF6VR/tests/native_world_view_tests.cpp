#include "native_world_view.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace {
int failures=0;
void Check(bool ok,const char* label) { if(!ok) {std::printf("FAIL %s\n",label);++failures;} }
bool Same(const edf6vr::Matrix& a,const edf6vr::Matrix& b) {return !std::memcmp(&a,&b,sizeof(a));}
bool Near(float a,float b) {return std::fabs(a-b)<0.00003f;}
edf6vr::Matrix Identity(float x=4,float y=5,float z=6) {
    return {{{1,0,0,0},{0,1,0,0},{0,0,1,0},{x,y,z,1}}};
}
}
int main() {
    using namespace edf6vr;
    NativeWorldViewCache cache;
    int objects[NativeWorldViewCache::kCapacity+1]{};
    void* camera=&objects[0];
    Matrix original=Identity(90,91,92),shifted=original;
    const Matrix sentinel=original;
    Check(ClassifyNativeWorldResolve(0x11B6B22)==NativeWorldViewKind::Main,"exact Main return RVA");
    Check(ClassifyNativeWorldResolve(0x11B680E)==NativeWorldViewKind::Far,"exact Far return RVA");
    for(auto address:{0u,0x11B7007u,0x11B71D7u,0x11B6B1Du,0x11B6809u})
        Check(ClassifyNativeWorldResolve(address)==NativeWorldViewKind::Other,"exclude shadow, CALL start and unknown");
    Check(!cache.Prepare(camera,1,0,.064f,1000,original,shifted),"missing original");
    Check(Same(original,sentinel)&&Same(shifted,sentinel),"failure outputs unchanged");
    const Matrix bases[]={
        Identity(),
        {{{0,0,-1,0},{0,1,0,0},{1,0,0,0},{4,5,6,1}}}, // +90 yaw, right along -Z
        {{{0,1,0,0},{-1,0,0,0},{0,0,1,0},{4,5,6,1}}}, // roll, right along +Y
        {{{-1,0,0,0},{0,1,0,0},{0,0,-1,0},{4,5,6,1}}},
        {{{.94972f,-.04307f,.31012f,0},{.02211f,.99725f,.07078f,0},
          {-.31231f,-.06037f,.94806f,0},{421.65228f,1.58371f,-387.78143f,1}}}
    };
    for(const auto& basis:bases) {
        // Independent projection oracle: EDF converts inverse(camera) by Ry(pi)
        // before its RH perspective projection (11E3C33, pi at1C369C0).
        // A physical XR-right point maps to -row0 in EDF. Compare a world
        // landmark against standard XR left/right pinhole images, not against
        // the same camera-delta sign used by the production validation gate.
        constexpr float ipd=.064f,fx=.63f,fy=1.12f;
        Matrix left=basis;
        for(unsigned j=0;j<3;++j)left.m[3][j]+=basis.m[0][j]*NativeWorldCameraOffset(0,ipd);
        cache.Reset();cache.Record(camera,left,1000);
        Check(cache.Prepare(camera,5,0,ipd,1000,original,shifted),"physical left baseline");
        for(float depth:{1.5f,5.f,40.f}) {
            float ndc[2]{};
            for(unsigned eye=0;eye<2;++eye) {
                Check(cache.Prepare(camera,5,eye,ipd,1001,original,shifted),"landmark eye snapshot");
                float v[3]{};
                for(unsigned j=0;j<3;++j) {
                    const float point=basis.m[3][j]-.12f*basis.m[0][j]+.08f*basis.m[1][j]+depth*basis.m[2][j];
                    for(unsigned k=0;k<3;++k)v[k]+=(point-shifted.m[3][j])*basis.m[k][j];
                }
                // After Ry(pi): x=-v.x, y=v.y, z=-v.z; RH clip.w=-z.
                ndc[eye]=-v[0]*fx/v[2];
                const float xrEyeX=(eye?1.f:-1.f)*ipd*.5f;
                Check(Near(ndc[eye],(.12f-xrEyeX)*fx/depth),"EDF landmark equals XR eye projection");
                Check(Near(v[1]*fy/v[2],.08f*fy/depth),"no vertical disparity through EDF conversion");
            }
            Check(ndc[0]>ndc[1],"near world has correct crossed disparity, not reversed depth");
        }
        cache.Reset();cache.Record(camera,basis,1000);
        Check(ValidNativeWorldCamera(basis),"native rigid row-vector basis accepted");
        Check(!cache.Prepare(camera,1,1,.064f,1000,original,shifted),"right cannot start a pair");
        Check(cache.Prepare(camera,1,0,.064f,1000,original,shifted),"left latched");
        Check(Same(original,basis)&&Same(shifted,basis),"left is existing native left eye");
        Check(cache.Prepare(camera,1,1,.064f,1001,original,shifted),"right prepared");
        Check(Same(original,basis),"returned baseline immutable");
        Matrix expected=basis;
        for(unsigned j=0;j<3;++j) expected.m[3][j]-=basis.m[0][j]*.064f;
        Check(Same(shifted,expected),"right follows all world axes of row0");
        for(unsigned n=0;n<500;++n) {
            Check(cache.Prepare(camera,1,n&1,.064f,1001,original,shifted),"repeated preparation");
            Check(Same(shifted,(n&1)?expected:basis),"no accumulation across repeated preparation");
        }
        Matrix newer=Identity(20,30,40);
        cache.Record(camera,newer,1002);
        Check(cache.Prepare(camera,1,0,.064f,1003,original,shifted)&&Same(shifted,basis),"repeat left cannot relatch newer camera");
        Check(cache.Prepare(camera,1,1,.064f,1003,original,shifted)&&Same(shifted,expected),"right uses first-eye snapshot after new original");
        Check(cache.Prepare(camera,2,0,.064f,1004,original,shifted)&&Same(shifted,newer),"next pair takes new original");
        Check(!cache.Prepare(camera,1,1,.064f,1005,original,shifted),"old frame rejected");
        Check(!cache.Prepare(camera,1,0,.064f,1005,original,shifted),"cannot relatch backward frame");
    }
    cache.Reset();const auto basis=Identity();cache.Record(camera,basis,1000);
    Check(!cache.Prepare(camera,1,0,.064f,999,original,shifted),"future original timestamp rejected");
    Check(!cache.Prepare(camera,1,0,.064f,1251,original,shifted),"stale original rejected");
    Check(cache.Prepare(camera,1,0,.064f,1000,original,shifted),"fresh first eye");
    Check(!cache.Prepare(camera,1,1,.065f,1001,original,shifted),"IPD cannot change between pair");
    Check(!cache.Prepare(camera,2,1,.064f,1001,original,shifted),"wrong right frame rejected");
    Check(!cache.Prepare(camera,1,1,.064f,999,original,shifted),"future pair timestamp rejected");
    Check(!cache.Prepare(camera,1,1,.064f,1251,original,shifted),"stale pair rejected");
    cache.Record(camera,basis,1251);
    Check(!cache.Prepare(camera,1,0,.064f,1251,original,shifted),"fresh original cannot revive expired same pair");
    Check(cache.Prepare(camera,2,0,.064f,1251,original,shifted),"new token may start fresh pair");
    for(float ipd:{0.0f,-.064f,.251f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        Check(!cache.Prepare(camera,2,1,ipd,1251,original,shifted),"invalid IPD rejected");
    Check(!cache.Prepare(nullptr,2,0,.064f,1251,original,shifted),"null camera rejected");
    Check(!cache.Prepare(camera,0,0,.064f,1251,original,shifted),"zero frame rejected");
    Check(!cache.Prepare(camera,2,2,.064f,1251,original,shifted),"invalid eye rejected");
    Check(!cache.Prepare(camera,2,1,.064f,1251,original,original),"aliased outputs rejected");
    cache.Reset();Check(!cache.Prepare(camera,2,1,.064f,1251,original,shifted),"reset invalidates pending pair");
    for(unsigned test=0;test<7;++test) {
        Matrix invalid=basis;
        if(test==0) invalid.m[0][0]=1.2f;
        if(test==1) invalid.m[1][0]=.2f;
        if(test==2) invalid.m[0][3]=.01f;
        if(test==3) invalid.m[3][3]=0;
        if(test==4) invalid.m[3][1]=std::numeric_limits<float>::quiet_NaN();
        if(test==5) invalid.m[2][2]=std::numeric_limits<float>::infinity();
        if(test==6) invalid.m[0][0]=-1;
        cache.Record(camera,basis,1000);cache.Record(camera,invalid,1001);
        Check(!ValidNativeWorldCamera(invalid),"scaled, sheared, projective, nonfinite or reflected camera rejected");
        Check(!cache.Prepare(camera,3,0,.064f,1001,original,shifted),"invalid original does not retain previous valid source");
    }
    cache.Reset();
    for(unsigned n=0;n<NativeWorldViewCache::kCapacity;++n) cache.Record(&objects[n],Identity(float(n),0,0),1000+n);
    Check(cache.Prepare(&objects[0],1,0,.064f,1020,original,shifted),"old slot recently used");
    cache.Record(&objects[NativeWorldViewCache::kCapacity],basis,1021);
    Check(cache.Prepare(&objects[0],1,1,.064f,1021,original,shifted),"recently used pair retained when full");
    Check(!cache.Prepare(&objects[1],1,0,.064f,1021,original,shifted),"oldest entry evicted at fixed capacity");
    for(unsigned n=2;n<NativeWorldViewCache::kCapacity;++n)
        Check(cache.Prepare(&objects[n],1,0,.064f,1021,original,shifted)&&Near(shifted.m[3][0],float(n)),"other camera states isolated");

    // Real synchronized facade: a producer can update the original during a
    // pair without racing or changing the immutable right-eye destination.
    ResetNativeWorldCameras();RecordNativeWorldCamera(camera,basis);
    // Models Prepare immediately after the first native matrix setter, even if
    // the native first process skips its resolve call for an existing snapshot.
    Check(PrepareNativeWorldEye(camera,1,0,.064f,original,shifted),"synchronized facade setter-time left");
    std::thread producer([&] {for(unsigned n=0;n<10000;++n) RecordNativeWorldCamera(camera,Identity(float(n),2,3));});
    Matrix right{};Check(PrepareNativeWorldEye(camera,1,1,.064f,original,right),"synchronized facade right during producer");
    producer.join();
    Check(Near(right.m[3][0],3.936f)&&Near(right.m[3][1],5)&&Near(right.m[3][2],6),"concurrent publish cannot replace pair");
    ResetNativeWorldCameras();Check(!PrepareNativeWorldEye(camera,1,1,.064f,original,shifted),"synchronized facade reset");
    std::printf("Native world view cache: %d failures\n",failures);
    return failures?1:0;
}
