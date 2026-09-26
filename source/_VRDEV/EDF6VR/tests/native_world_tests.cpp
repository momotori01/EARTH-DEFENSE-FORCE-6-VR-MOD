// Public production API + real D3D11 WARP resources. GPU readback is test-only.
#include "native_world.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

using Microsoft::WRL::ComPtr;
using namespace edf6vr;
namespace {
int failures=0;
const char* testName="setup";
std::uint64_t nextFrame=1;
constexpr float kIpd=.064f;
#define CHECK(x) do { if(!(x)) {std::printf("FAIL %s:%d: %s\n",testName,__LINE__,#x);++failures;} } while(false)

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    bool Create() {
        const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,
            nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        CHECK(SUCCEEDED(hr));return SUCCEEDED(hr);
    }
};
struct Target {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    unsigned width=64,height=48;
    bool Create(Gpu& gpu,unsigned w=64,unsigned h=48,unsigned samples=1,
                DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM) {
        width=w;height=h;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;
        desc.MipLevels=desc.ArraySize=1;desc.Format=format;desc.SampleDesc.Count=samples;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        auto hr=gpu.device->CreateTexture2D(&desc,nullptr,&texture);
        CHECK(SUCCEEDED(hr));if(FAILED(hr))return false;
        hr=gpu.device->CreateRenderTargetView(texture.Get(),nullptr,&rtv);
        CHECK(SUCCEEDED(hr));return SUCCEEDED(hr);
    }
    void Colour(Gpu& gpu,float red,float green,float blue) {
        const float colour[]={red,green,blue,1};gpu.context->ClearRenderTargetView(rtv.Get(),colour);
    }
};
Matrix Identity() {
    Matrix out{};for(unsigned i=0;i<4;++i)out.m[i][i]=1;return out;
}
Matrix Camera(float yaw=0,float pitch=0,float roll=0) {
    Matrix out{};CHECK(RotateCamera(Identity(),yaw,pitch,out));
    const float c=std::cos(roll),s=std::sin(roll);const Matrix before=out;
    for(unsigned j=0;j<3;++j) {
        out.m[0][j]=c*before.m[0][j]+s*before.m[1][j];
        out.m[1][j]=-s*before.m[0][j]+c*before.m[1][j];
    }
    out.m[3][0]=10;out.m[3][1]=3;out.m[3][2]=-7;
    CHECK(ValidCamera(out));return out;
}
Matrix ViewOf(Matrix camera,unsigned eye,float ipd) {
    for(unsigned j=0;j<3;++j)camera.m[3][j]+=camera.m[0][j]*(eye?-1.f:1.f)*ipd*.5f;
    Matrix view{};view.m[3][3]=1;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)view.m[i][j]=camera.m[j][i];
    for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)view.m[3][j]-=camera.m[3][k]*view.m[k][j];
    CHECK(ValidCamera(view));return view;
}
Matrix Projection(float farPlane=1000) {
    Matrix p{};p.m[0][0]=1.1f;p.m[1][1]=1.9f;
    p.m[2][2]=farPlane/(farPlane-.1f);p.m[2][3]=1;
    p.m[3][2]=-.1f*farPlane/(farPlane-.1f);return p;
}
void Arm(float ipd=kIpd) {
    ClearNativeWorldImages();ConfigureNativeWorld(true);RefreshNativeWorld(true,ipd);
    CHECK(NativeWorldEnabled());CHECK(NativeWorldLive());
    CHECK(std::fabs(NativeWorldSeparation()-ipd)<.000001f);
    CHECK(NativeWorldRenderEye()==-1 && NativeWorldRenderFrame()==0);
}
void Observe(unsigned mode,const Matrix& view,const Matrix& projection,
             bool refresh=true,bool viewPresent=true,bool projectionPresent=true) {
    BeginNativeWorldView(mode,refresh);
    if(viewPresent)ObserveNativeWorldView(view);
    if(projectionPresent)ObserveNativeWorldProjection(projection);
    EndNativeWorldView();
}
void Eye(Gpu& gpu,Target& source,std::uint64_t frame,unsigned eye,
         const Matrix& view,const Matrix& projection) {
    BeginNativeWorldEye(frame,eye);
    CHECK(NativeWorldRenderEye()==static_cast<int>(eye));CHECK(NativeWorldRenderFrame()==frame);
    Observe(0,view,projection);
    EndNativeWorldEye(frame,eye,gpu.context.Get(),source.texture.Get());
    CHECK(NativeWorldRenderEye()==-1 && NativeWorldRenderFrame()==0);
}
std::array<unsigned char,4> Pixel(Gpu& gpu,ID3D11Texture2D* source) {
    std::array<unsigned char,4> pixel{};CHECK(source!=nullptr);if(!source)return pixel;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);CHECK(desc.SampleDesc.Count==1);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    desc.MiscFlags=0;ComPtr<ID3D11Texture2D> staging;
    const auto made=gpu.device->CreateTexture2D(&desc,nullptr,&staging);CHECK(SUCCEEDED(made));if(FAILED(made))return pixel;
    gpu.context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE mapped{};
    const auto read=gpu.context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);CHECK(SUCCEEDED(read));
    if(SUCCEEDED(read)) {
        const auto* p=static_cast<const unsigned char*>(mapped.pData)+(desc.Height/2)*mapped.RowPitch+(desc.Width/2)*4;
        for(unsigned i=0;i<4;++i)pixel[i]=p[i];gpu.context->Unmap(staging.Get(),0);
    }
    return pixel;
}
NativeWorldImages Pair(Gpu& gpu,Target& source,const Matrix& camera,float ipd=kIpd) {
    const auto frame=nextFrame++;const auto projection=Projection();
    Eye(gpu,source,frame,0,ViewOf(camera,0,ipd),projection);
    Eye(gpu,source,frame,1,ViewOf(camera,1,ipd),projection);
    return TakeNativeWorldImages(source.width,source.height);
}
void ExpectRejected(unsigned width=64,unsigned height=48) {
    auto pair=TakeNativeWorldImages(width,height);
    CHECK(pair.attempted && !pair.ready);CHECK(!pair.eye[0] && !pair.eye[1]);
    auto again=TakeNativeWorldImages(width,height);CHECK(!again.attempted && !again.ready);
}

void TestImages(Gpu& gpu,Target& source) {
    testName="same-frame independent GPU snapshots";Arm();
    const auto frame=nextFrame++;const auto camera=Camera(37,20,.41f);const auto projection=Projection();
    source.Colour(gpu,1,0,0);Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),projection);
    source.Colour(gpu,0,1,0);Eye(gpu,source,frame,1,ViewOf(camera,1,kIpd),projection);
    source.Colour(gpu,0,0,1); // Native target reused after both captures.
    auto pair=TakeNativeWorldImages(source.width,source.height);
    CHECK(pair.attempted && pair.ready && pair.frame==frame);
    CHECK(pair.eye[0] && pair.eye[1] && pair.eye[0].Get()!=pair.eye[1].Get());
    CHECK(pair.eye[0].Get()!=source.texture.Get() && pair.eye[1].Get()!=source.texture.Get());
    auto left=Pixel(gpu,pair.eye[0].Get()),right=Pixel(gpu,pair.eye[1].Get());
    CHECK(left[0]==255 && left[1]==0 && left[2]==0 && left[3]==255);
    CHECK(right[0]==0 && right[1]==255 && right[2]==0 && right[3]==255);
    const auto again=TakeNativeWorldImages(source.width,source.height);
    CHECK(!again.attempted && !again.ready && !again.eye[0] && !again.eye[1]);
    ClearNativeWorldImages(); // Returned ComPtrs own their images independently.
    CHECK(!TakeNativeWorldImages(source.width,source.height).attempted);
    CHECK(Pixel(gpu,pair.eye[0].Get())==left && Pixel(gpu,pair.eye[1].Get())==right);
    pair={};

    testName="configured IPD and rotated camera right axis";
    for(float ipd:{.029f,.064f,.075f,.12f}) {
        Arm(ipd);auto exact=Pair(gpu,source,camera,ipd);CHECK(exact.ready);
    }
    testName="MSAA native captures resolve independently";
    UINT levels=0;CHECK(SUCCEEDED(gpu.device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM,4,&levels)));
    CHECK(levels>0);
    if(levels) {
        Target msaa;if(msaa.Create(gpu,source.width,source.height,4)) {
            Arm();const auto msaaFrame=nextFrame++;
            msaa.Colour(gpu,1,0,0);Eye(gpu,msaa,msaaFrame,0,ViewOf(camera,0,kIpd),projection);
            msaa.Colour(gpu,0,1,0);Eye(gpu,msaa,msaaFrame,1,ViewOf(camera,1,kIpd),projection);
            msaa.Colour(gpu,0,0,1);auto resolved=TakeNativeWorldImages(msaa.width,msaa.height);CHECK(resolved.ready);
            CHECK(Pixel(gpu,resolved.eye[0].Get())[0]==255);CHECK(Pixel(gpu,resolved.eye[1].Get())[1]==255);
        }
    }
}

enum class Fault { MissingMain,DuplicateMain,MissingView,MissingProjection,NoViewerRefresh,
    WrongEyeSign,WrongIpd,RotationMismatch,ProjectionMismatch,NonfiniteProjection,InvalidView,
    OnlyLeftFar,OnlyRightFar,DuplicateFar,FarWrongEye,FarProjectionMismatch,FarNoRefresh };
void TestViewGate(Gpu& gpu,Target& source) {
    const auto camera=Camera(27,-15,.19f);const auto projection=Projection();
    const std::array<Fault,17> faults={Fault::MissingMain,Fault::DuplicateMain,Fault::MissingView,
        Fault::MissingProjection,Fault::NoViewerRefresh,Fault::WrongEyeSign,Fault::WrongIpd,
        Fault::RotationMismatch,Fault::ProjectionMismatch,Fault::NonfiniteProjection,Fault::InvalidView,
        Fault::OnlyLeftFar,Fault::OnlyRightFar,Fault::DuplicateFar,Fault::FarWrongEye,
        Fault::FarProjectionMismatch,Fault::FarNoRefresh};
    for(const auto fault:faults) {
        char name[96]{};std::snprintf(name,sizeof(name),"view/projection gate fault %u",static_cast<unsigned>(fault));testName=name;
        Arm();const auto frame=nextFrame++;
        for(unsigned eye=0;eye<2;++eye) {
            BeginNativeWorldEye(frame,eye);
            auto view=ViewOf(camera,eye,kIpd);auto p=projection;
            if(fault==Fault::WrongEyeSign)view=ViewOf(camera,1-eye,kIpd);
            if(fault==Fault::WrongIpd)view=ViewOf(camera,eye,kIpd*.5f);
            if(eye==1 && fault==Fault::RotationMismatch)view=ViewOf(Camera(29,-15,.19f),eye,kIpd);
            if(eye==1 && fault==Fault::ProjectionMismatch)p.m[0][0]+=.02f;
            if(fault==Fault::NonfiniteProjection)p.m[1][1]=std::numeric_limits<float>::quiet_NaN();
            if(eye==1 && fault==Fault::InvalidView)view.m[0][0]=100;
            const bool skipMain=eye==1 && fault==Fault::MissingMain;
            if(!skipMain)Observe(0,view,p,!(eye==1 && fault==Fault::NoViewerRefresh),
                !(eye==1 && fault==Fault::MissingView),!(eye==1 && fault==Fault::MissingProjection));
            if(eye==1 && fault==Fault::DuplicateMain)Observe(0,view,p);
            const bool hasFar=(fault==Fault::OnlyLeftFar && eye==0)||(fault==Fault::OnlyRightFar && eye==1)
                ||fault==Fault::DuplicateFar||fault==Fault::FarWrongEye||fault==Fault::FarProjectionMismatch||fault==Fault::FarNoRefresh;
            if(hasFar) {
                auto farView=ViewOf(camera,eye,kIpd);auto farProjection=Projection(8000);
                if(eye==1 && fault==Fault::FarWrongEye)farView=ViewOf(camera,0,kIpd);
                if(eye==1 && fault==Fault::FarProjectionMismatch)farProjection.m[0][0]+=.02f;
                Observe(1,farView,farProjection,!(eye==1 && fault==Fault::FarNoRefresh));
                if(eye==1 && fault==Fault::DuplicateFar)Observe(1,farView,farProjection);
            }
            EndNativeWorldEye(frame,eye,gpu.context.Get(),source.texture.Get());
        }
        ExpectRejected(source.width,source.height);
    }
    testName="valid Far pair and shadow view does not contaminate Main";Arm();const auto frame=nextFrame++;
    for(unsigned eye=0;eye<2;++eye) {
        BeginNativeWorldEye(frame,eye);Observe(0,ViewOf(camera,eye,kIpd),projection);
        Observe(1,ViewOf(camera,eye,kIpd),Projection(8000));
        // Shadow cameras legitimately have different positions/projections and
        // do not use the Main VIEW_CHANGED refresh gate.
        Observe(2,ViewOf(Camera(-80,70,.5f),0,.1f),Projection(90),false);
        EndNativeWorldEye(frame,eye,gpu.context.Get(),source.texture.Get());
    }
    CHECK(TakeNativeWorldImages(source.width,source.height).ready);
}

void TestLifecycle(Gpu& gpu,Target& source) {
    const auto camera=Camera();const auto p=Projection();
    testName="missing second eye";Arm();auto frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);ExpectRejected();
    testName="frame mismatch";Arm();frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);
    BeginNativeWorldEye(frame+100,1);Observe(0,ViewOf(camera,1,kIpd),p);
    EndNativeWorldEye(frame+100,1,gpu.context.Get(),source.texture.Get());ExpectRejected();
    testName="right eye cannot start pair";Arm();frame=nextFrame++;
    BeginNativeWorldEye(frame,1);Observe(0,ViewOf(camera,1,kIpd),p);
    EndNativeWorldEye(frame,1,gpu.context.Get(),source.texture.Get());
    auto rightOnly=TakeNativeWorldImages(source.width,source.height);CHECK(!rightOnly.ready);
    testName="cancel invalidates incomplete eye";Arm();frame=nextFrame++;
    BeginNativeWorldEye(frame,0);Observe(0,ViewOf(camera,0,kIpd),p);CancelNativeWorldEye(frame);
    CHECK(NativeWorldRenderEye()==-1);ExpectRejected();
    testName="begin before previous eye ends";Arm();frame=nextFrame++;
    BeginNativeWorldEye(frame,0);BeginNativeWorldEye(frame+1,0);
    Observe(0,ViewOf(camera,0,kIpd),p);EndNativeWorldEye(frame+1,0,gpu.context.Get(),source.texture.Get());
    Eye(gpu,source,frame+1,1,ViewOf(camera,1,kIpd),p);ExpectRejected();
    testName="missing copy source";Arm();frame=nextFrame++;
    BeginNativeWorldEye(frame,0);Observe(0,ViewOf(camera,0,kIpd),p);
    EndNativeWorldEye(frame,0,gpu.context.Get(),nullptr);ExpectRejected();
    testName="unfinished pair cannot be consumed";Arm();frame=nextFrame++;
    BeginNativeWorldEye(frame,0);Observe(0,ViewOf(camera,0,kIpd),p);ExpectRejected();CancelNativeWorldEye(frame);

    testName="F11/mission live reset invalidates completed pair";Arm();frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);Eye(gpu,source,frame,1,ViewOf(camera,1,kIpd),p);
    RefreshNativeWorld(false,kIpd);CHECK(!NativeWorldLive());RefreshNativeWorld(true,kIpd);
    CHECK(NativeWorldLive());ExpectRejected();
    testName="configuration reset invalidates completed pair";Arm();frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);Eye(gpu,source,frame,1,ViewOf(camera,1,kIpd),p);
    ConfigureNativeWorld(false);CHECK(!NativeWorldEnabled() && !NativeWorldLive());ExpectRejected();
    testName="invalid physical eye separation rejects live request";
    for(float ipd:{0.f,.02f,.2f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Arm();RefreshNativeWorld(true,ipd);CHECK(!NativeWorldLive());
    }
    testName="invalid physical override cannot bypass geometry validation";
    for(float physical:{-.064f,.02f,.2f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Arm();RefreshNativeWorld(true,kIpd,physical);CHECK(!NativeWorldLive());
    }
    Arm();RefreshNativeWorld(true,kIpd,.072f);CHECK(NativeWorldLive());
    RefreshNativeWorld(true,kIpd,0);CHECK(NativeWorldLive()); // omitted override uses geometry IPD
    testName="explicit clear drops attempt and resets render cursor";Arm();frame=nextFrame++;
    BeginNativeWorldEye(frame,0);ClearNativeWorldImages();
    CHECK(NativeWorldRenderEye()==-1 && NativeWorldRenderFrame()==0);
    auto empty=TakeNativeWorldImages(source.width,source.height);CHECK(!empty.attempted && !empty.ready);
}

void TestResources(Gpu& gpu,Target& source) {
    const auto camera=Camera();const auto p=Projection();
    Target large,otherFormat;
    if(!large.Create(gpu,128,96)||!otherFormat.Create(gpu,64,48,1,DXGI_FORMAT_B8G8R8A8_UNORM))return;
    testName="different eye dimensions rejected";Arm();auto frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);Eye(gpu,large,frame,1,ViewOf(camera,1,kIpd),p);ExpectRejected();
    testName="different eye formats rejected";Arm();frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);Eye(gpu,otherFormat,frame,1,ViewOf(camera,1,kIpd),p);ExpectRejected();
    testName="present resize rejects old-sized pair";Arm();frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);Eye(gpu,source,frame,1,ViewOf(camera,1,kIpd),p);ExpectRejected(128,96);
    testName="new dimensions allocate fresh matching pair";
    RefreshNativeWorld(true,kIpd);auto resized=Pair(gpu,large,camera);CHECK(resized.ready);
    if(resized.eye[0]) {D3D11_TEXTURE2D_DESC desc{};resized.eye[0]->GetDesc(&desc);CHECK(desc.Width==128 && desc.Height==96);}
    resized={};

    Gpu other;if(!other.Create())return;Target alien;if(!alien.Create(other))return;
    testName="source and context devices must match";Arm();frame=nextFrame++;
    BeginNativeWorldEye(frame,0);Observe(0,ViewOf(camera,0,kIpd),p);
    EndNativeWorldEye(frame,0,gpu.context.Get(),alien.texture.Get());ExpectRejected();
    testName="two eyes cannot come from different devices";Arm();frame=nextFrame++;
    Eye(gpu,source,frame,0,ViewOf(camera,0,kIpd),p);Eye(other,alien,frame,1,ViewOf(camera,1,kIpd),p);ExpectRejected();
    testName="deferred contexts cannot publish native pair";Arm();frame=nextFrame++;
    ComPtr<ID3D11DeviceContext> deferred;CHECK(SUCCEEDED(gpu.device->CreateDeferredContext(0,&deferred)));
    BeginNativeWorldEye(frame,0);Observe(0,ViewOf(camera,0,kIpd),p);
    EndNativeWorldEye(frame,0,deferred.Get(),source.texture.Get());ExpectRejected();
    testName="clear permits real GPU device replacement";Arm();
    auto recovered=Pair(other,alien,camera);CHECK(recovered.ready);
    if(recovered.eye[0]) {ComPtr<ID3D11Device> owner;recovered.eye[0]->GetDevice(&owner);CHECK(owner.Get()==other.device.Get());}
    recovered={};ClearNativeWorldImages();
}
}
int main() {
    Gpu gpu;if(!gpu.Create())return 1;Target source;if(!source.Create(gpu))return 1;
    TestImages(gpu,source);TestViewGate(gpu,source);TestLifecycle(gpu,source);TestResources(gpu,source);
    ConfigureNativeWorld(false);ClearNativeWorldImages();gpu.context->ClearState();gpu.context->Flush();
    std::printf("Native world pair camera gates / independent GPU images / lifecycle: %d failures\n",failures);
    return failures?1:0;
}
