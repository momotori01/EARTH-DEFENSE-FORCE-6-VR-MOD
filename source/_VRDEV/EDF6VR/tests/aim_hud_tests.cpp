#include "../src/aim_hud.cpp"
#include <wrl/client.h>
#include <cstdio>
#include <limits>
using namespace edf6vr;
using Microsoft::WRL::ComPtr;
namespace {int failures=0,forwarded=0;void __fastcall FakePrepare(void*,void*) {++forwarded;}}
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    alignas(16) unsigned char camera[0xB40]{},before[0xB40]{},records[0x60]{};
    auto slot=camera+0x720;
    *reinterpret_cast<void**>(slot+0x1D8)=records;
    *reinterpret_cast<std::uint64_t*>(slot+0x1E0)=2;
    *reinterpret_cast<std::uint64_t*>(slot+0x1E8)=2;
    slot[0x140]=1;
    float pos[4]={0,0,5,1};std::memcpy(records+0x10,pos,16);
    pos[0]=1;std::memcpy(records+0x40,pos,16);records[0x51]=1;
    std::memcpy(before,camera,sizeof(camera));
    AimHudSnapshot snap{};CHECK(ReadNativeAimHud(camera,snap));
    CHECK(snap.lockWeapon && snap.count==2 && snap.points[1].world[0]==1 && snap.points[1].kind==1);
    CHECK(!std::memcmp(before,camera,sizeof(camera)));
    *reinterpret_cast<std::uint64_t*>(slot+0x1E8)=3;
    CHECK(!ReadNativeAimHud(camera,snap) && snap.count==0);
    *reinterpret_cast<std::uint64_t*>(slot+0x1E8)=2;
    *reinterpret_cast<float*>(records+0x10)=std::numeric_limits<float>::quiet_NaN();
    CHECK(ReadNativeAimHud(camera,snap) && snap.count==1);
    *reinterpret_cast<float*>(records+0x10)=0;
    CHECK(snap.frameCount==0);   // the flag alone, no sight: no frame
    {   // The sight: identity rows at (0,0,0), 10 and 5 degrees half angles.
        alignas(16) unsigned char sight[0xB40]{};auto s=sight+0x720;s[0x140]=1;
        const float rows[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 3,4,5,1};std::memcpy(s+0x150,rows,sizeof(rows));
        const float half[2]={.174533f,.0872665f};std::memcpy(s+0x190,half,sizeof(half));
        AimHudSnapshot f{};CHECK(ReadNativeAimHud(sight,f) && f.frameCount==1 && f.count==0);
        CHECK(std::fabs(f.frames[0].corners[0][0]-(3-1000*std::tan(.174533f)))<.01f);
        CHECK(std::fabs(f.frames[0].corners[2][1]-(4+1000*std::tan(.0872665f)))<.01f);
        CHECK(std::fabs(f.frames[0].corners[1][2]-1005)<.01f);
        const float bad=std::numeric_limits<float>::quiet_NaN();std::memcpy(s+0x190,&bad,4);
        CHECK(ReadNativeAimHud(sight,f) && f.frameCount==0);
    }
    auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    ImageProfile image{};char reason[256]{};
    CHECK(module && CheckImage(module,image,reason,sizeof(reason)));
    if(!image.base)return 2;
    bool changed=false;CHECK(InstallAimHud(image,changed) && changed);
    CHECK(!InstallAimHud(image,changed) && !changed); // conflicting slots never overwritten
    original=&FakePrepare;
    SetAimHudSource(camera,true,.5f);Prepared(camera,nullptr);
    CHECK(forwarded==1 && ReadAimHud().count==2);
    sourceAt=GetTickCount64()-251;CHECK(ReadAimHud().count==0);
    SetAimHudSource(camera,true,.8f);Prepared(camera,nullptr);
    CHECK(ReadAimHud().count==2 && AimHudScale()==.8f);
    SetAimHudSource(camera,false,.8f);CHECK(!ReadAimHud().lockWeapon && ReadAimHud().count==0);
    {   // The sight only for a source that asks for it (vehicle seats).
        alignas(16) unsigned char sight[0xB40]{};auto s=sight+0x720;s[0x140]=1;
        const float rows[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};std::memcpy(s+0x150,rows,sizeof(rows));
        const float half[2]={.2f,.1f};std::memcpy(s+0x190,half,sizeof(half));
        SetAimHudSource(sight,true,.5f);Prepared(sight,nullptr);CHECK(ReadAimHud().frameCount==0);
        SetAimHudSource(sight,true,.5f,true);Prepared(sight,nullptr);CHECK(ReadAimHud().frameCount==1);
        SetAimHudSource(sight,false,.5f,true);CHECK(ReadAimHud().frameCount==0);
    }
    Matrix view{},projection{};for(unsigned i=0;i<4;++i)view.m[i][i]=1;
    projection.m[0][0]=1;projection.m[1][1]=1;projection.m[2][3]=-1;
    const float world[3]={0,0,5};float xy[2]{};
    CHECK(ProjectAimHud(view,projection,world,xy) && xy[0]==0 && xy[1]==0);
    // Physical left eye is +EDF X; left sprite must have positive disparity.
    view.m[3][0]=-.032f;CHECK(ProjectAimHud(view,projection,world,xy) && std::fabs(xy[0]-.0064f)<.00001f);
    view.m[3][0]=.032f;CHECK(ProjectAimHud(view,projection,world,xy) && std::fabs(xy[0]+.0064f)<.00001f);
    const float behind[3]={0,0,-5};CHECK(!ProjectAimHud(view,projection,behind,xy));view.m[3][0]=0;
    // Run the real GPU shader, preserve the scene outside each little bracket,
    // and preserve every D3D state touched by the pass.
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> ctx;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&ctx)));
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=256;td.MipLevels=td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,staging;ComPtr<ID3D11RenderTargetView> rtv;
    CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&target)));CHECK(SUCCEEDED(d->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&staging)));
    auto* bound=rtv.Get();ctx->OMSetRenderTargets(1,&bound,nullptr);
    const float blue[4]={0,0,1,1};ctx->ClearRenderTargetView(bound,blue);
    snap={};snap.count=1;snap.points[0].world[2]=5;
    CHECK(DrawAimHud(ctx.Get(),target.Get(),view,projection,snap,2.f));
    ComPtr<ID3D11RenderTargetView> restored;ctx->OMGetRenderTargets(1,&restored,nullptr);CHECK(restored.Get()==rtv.Get());
    ctx->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};
    CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    unsigned red=0,blueCount=0;
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) {
        auto p=static_cast<unsigned char*>(map.pData)+y*map.RowPitch+x*4;
        if(p[0]>32)++red;if(p[0]==0 && p[1]==0 && p[2]==255)++blueCount;
    }
    CHECK(red>16 && blueCount>65000);ctx->Unmap(staging.Get(),0);
    {   // The lock-on sight: a red outline round the middle, the middle itself untouched.
        ctx->ClearRenderTargetView(bound,blue);
        snap={};snap.frameCount=1;
        const float square[4][2]={{-2.5f,-2.5f},{2.5f,-2.5f},{2.5f,2.5f},{-2.5f,2.5f}};
        for(unsigned c=0;c<4;++c){snap.frames[0].corners[c][0]=square[c][0];snap.frames[0].corners[c][1]=square[c][1];snap.frames[0].corners[c][2]=5;}
        CHECK(DrawAimHud(ctx.Get(),target.Get(),view,projection,snap,.5f));
        ctx->CopyResource(staging.Get(),target.Get());
        CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
        auto at=[&](unsigned x,unsigned y){return static_cast<unsigned char*>(map.pData)+y*map.RowPitch+x*4;};
        // NDC +-0.5 on a 256 target: the edges at pixels 64 and 192.
        CHECK(at(64,128)[0]>128 && at(192,128)[0]>128 && at(128,64)[0]>128 && at(128,192)[0]>128);
        CHECK(at(128,128)[0]==0 && at(128,128)[2]==255 && at(10,10)[2]==255);
        ctx->Unmap(staging.Get(),0);
        // A corner behind the eye: nothing drawn.
        ctx->ClearRenderTargetView(bound,blue);snap.frames[0].corners[0][2]=-5;
        CHECK(DrawAimHud(ctx.Get(),target.Get(),view,projection,snap,.5f));
        ctx->CopyResource(staging.Get(),target.Get());
        CHECK(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
        CHECK(at(64,128)[2]==255 && at(192,128)[2]==255);ctx->Unmap(staging.Get(),0);
    }
    FreeLibrary(module);
    std::printf("Native aim HUD reader, ownership, projection, GPU: %d failures\n",failures);return failures?1:0;
}
