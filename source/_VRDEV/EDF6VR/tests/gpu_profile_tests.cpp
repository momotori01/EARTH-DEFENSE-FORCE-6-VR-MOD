#include "gpu_profile.h"
#include "gpu_split.h"
#include "warp_trial.h"
#include "scope_draw.h"
#include <cmath>
#include <cstdlib>
#include <vector>
#include <wrl/client.h>
#include <cstdio>
#include <initializer_list>
using namespace edf6vr;
using Microsoft::WRL::ComPtr;
int main() {
    int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
    WarpTrial trial;
    CHECK(trial.Update(true,false,100,64)==64 && !trial.started);
    CHECK(trial.Update(true,true,1000,64)==64 && trial.phase==0);
    CHECK(trial.Update(true,true,21000,64)==64 && trial.phase==1);
    CHECK(trial.Update(true,true,41000,64)==32 && trial.phase==2);
    CHECK(trial.Update(true,true,61000,64)==64 && trial.phase==3);
    CHECK(trial.Update(true,true,81000,48)==48 && trial.phase==3); // never repeats
    WarpTrial abort; abort.Update(true,true,1000,64);
    CHECK(abort.Update(true,false,42000,64)==64 && abort.phase==4);
    CHECK(abort.Update(true,true,43000,64)==64 && abort.phase==4);
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    if(!device || !context) return 1;
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=desc.Height=128;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D11Texture2D> a,b;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&a)));
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&b)));
    if(!a || !b) return 1;
    EnableGpuProfile(true);
    for(int phase:{1,2}) for(int frame=0;frame<32;++frame) {
        BeginGpuFrame(device.Get(),context.Get(),true,128,128,phase==1?64:32,phase);
        { GpuScope scope(GpuStage::Copy); for(int k=0;k<4;++k) context->CopyResource(b.Get(),a.Get()); }
        EndGpuFrame();
        // Standalone test has no native Present to submit the command buffer.
        // Only the test flushes. Production profiler never does this.
        context->Flush(); PollGpuProfile();
    }
    const auto deadline=GetTickCount64()+2000;
    std::uint64_t counts[2]{};
    do {
        PollGpuProfile(); auto result=DrainGpuProfile(); CHECK(result.invalid==0);
        for(int phase:{1,2}) {
            const auto& g=result.phase[phase];
            counts[phase-1]+=g.gpu[static_cast<unsigned>(GpuStage::Copy)].count;
            CHECK(g.gpu[static_cast<unsigned>(GpuStage::Resolve)].count==0);
            if(g.gpu[0].count) { CHECK(g.width==128 && g.height==128); CHECK(g.steps==(phase==1?64u:32u)); }
        }
        SwitchToThread();
    } while((!counts[0] || !counts[1]) && GetTickCount64()<deadline);
    CHECK(counts[0]>0 && counts[1]>0);
    ResetGpuProfile(); DrainGpuProfile(); EnableGpuProfile(false);
    BeginGpuFrame(device.Get(),context.Get(),true,128,128,64,0); EndGpuFrame();
    CHECK(DrainGpuProfile().phase[0].frames.count==0);
    ResetGpuProfile();
    // The frame split: frames present to present, model runs by eye; off does nothing.
    {
        static int eye=-1;SetGpuSplitEyeSource([]() noexcept {return eye;});
        EnableGpuSplit(false);GpuSplitPresent(device.Get(),context.Get());
        { GpuSplitScope s(context.Get()); }
        CHECK(DrainGpuSplit().frames==0);
        EnableGpuSplit(true);
        GpuSplitStats split{};
        const auto until=GetTickCount64()+3000;
        // Every frame alike: three model draws for each eye, then the rest.
        do {
            GpuSplitPresent(device.Get(),context.Get());
            for(int e:{0,1}) { eye=e; for(int k=0;k<3;++k){ GpuSplitScope s(context.Get()); context->CopyResource(b.Get(),a.Get()); } }
            eye=-1; context->CopyResource(b.Get(),a.Get());   // not a model draw: the rest
            { GpuSplitScope other(nullptr); }                  // another context is not counted
            context->Flush();
            const auto s=DrainGpuSplit(); split.frames+=s.frames;
            for(int e=0;e<3;++e){split.runs[e]+=s.runs[e];split.draws[e]+=s.draws[e];}
            SwitchToThread();
        } while(split.frames<20&&GetTickCount64()<until);
        CHECK(split.frames>=20);
        CHECK(split.runs[0]==split.frames&&split.runs[1]==split.frames&&split.runs[2]==0);   // one run an eye a frame
        CHECK(split.draws[0]==3.0*split.frames&&split.draws[1]==3.0*split.frames);
        EnableGpuSplit(false);
    }
    // The scope picture drawn for real (scope_draw.cpp's shader on WARP): three
    // panels side by side show ONE picture between them -- the left panel its
    // left part, the right its right -- and a panel alone shows its own middle.
    // The source's red runs 0..255 left to right, its green top to bottom.
    {
        const unsigned sw=300,sh=100,tw=400,th=400;
        std::vector<unsigned char> pixels(sw*sh*4);
        for(unsigned y=0;y<sh;++y) for(unsigned x=0;x<sw;++x) {
            auto* p=&pixels[(y*sw+x)*4];
            p[0]=static_cast<unsigned char>(255.0*(x+.5)/sw);p[1]=static_cast<unsigned char>(255.0*(y+.5)/sh);p[2]=0;p[3]=255;
        }
        D3D11_TEXTURE2D_DESC sd{};sd.Width=sw;sd.Height=sh;sd.MipLevels=sd.ArraySize=sd.SampleDesc.Count=1;
        sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{pixels.data(),sw*4,0};
        ComPtr<ID3D11Texture2D> source,target,staging;
        CHECK(SUCCEEDED(device->CreateTexture2D(&sd,&init,&source)));
        D3D11_TEXTURE2D_DESC td=sd;td.Width=tw;td.Height=th;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)));
        D3D11_TEXTURE2D_DESC rd=td;rd.BindFlags=0;rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        CHECK(SUCCEEDED(device->CreateTexture2D(&rd,nullptr,&staging)));
        if(source && target && staging) {
            Matrix identity{};for(int i=0;i<4;++i) identity.m[i][i]=1;
            // Facing the camera (normal -z), standing on +y: screen right is -x in
            // the world, and with this camera a world x lands at clip -x.
            ScopeLensFrame panels[3]{};
            for(auto& f:panels) {f.valid=true;f.shape=1;f.normal[2]=-1;f.up[1]=1;f.radius=.4f;f.halfWidth=.2f;}
            panels[1].centre[0]=.4f;     // on the left of the picture
            panels[2].centre[0]=-.4f;    // on the right
            ScopeArrangeSurfaces(panels,3);
            auto draw=[&](const ScopeLensFrame& lens) {
                ScopeDrawInput in{};
                in.lens=lens;in.view=identity;in.projection=identity;in.source=source.Get();in.mode=1;in.style=ScopeStyleLens;
                in.tanHalf=.5f;in.aspect=lens.halfWidth/lens.radius;
                in.forward[2]=1;in.right[0]=1;in.up[1]=1;
                in.cameraForward[2]=1;in.cameraRight[0]=1;in.cameraUp[1]=1;
                in.tanLeft=-.75f;in.tanRight=.75f;in.tanTop=.5f;in.tanBottom=-.5f;
                return DrawScopeLens(context.Get(),target.Get(),in);
            };
            auto read=[&](unsigned x,unsigned y,int& r,int& g) {
                context->CopyResource(staging.Get(),target.Get());
                D3D11_MAPPED_SUBRESOURCE map{};
                if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) {r=g=-1;return;}
                const auto* p=static_cast<const unsigned char*>(map.pData)+y*map.RowPitch+x*4;
                r=p[0];g=p[1];context->Unmap(staging.Get(),0);
            };
            auto clear=[&]() {
                ComPtr<ID3D11RenderTargetView> view;device->CreateRenderTargetView(target.Get(),nullptr,&view);
                const float black[4]{};context->ClearRenderTargetView(view.Get(),black);
            };
            clear();
            for(const auto& f:panels) CHECK(draw(f));
            CHECK(ReadScopeDrawStats().failed==0);
            int r=0,g=0;
            // Half way out to the right and up on each panel (clear of the reticle and the rim).
            read(220,160,r,g);CHECK(std::abs(r-149)<=3 && std::abs(g-64)<=3);   // middle: picture x +0.25 -> u .583
            read(140,160,r,g);CHECK(std::abs(r-64)<=3 && std::abs(g-64)<=3);    // left:   picture x -0.75 -> u .25
            read(300,160,r,g);CHECK(std::abs(r-234)<=3 && std::abs(g-64)<=3);   // right:  picture x +1.25 -> u .917
            // Across a seam the picture runs on: just left and just right of the middle panel's left edge.
            int r1=0,r2=0,g1=0;
            read(152,160,r1,g1);read(166,160,r2,g1);CHECK(r2>r1 && r2-r1<20);
            read(60,160,r,g);CHECK(r==0 && g==0);                               // outside the panels: untouched
            // The middle panel alone, as a single screen is drawn: the same middle part.
            clear();
            ScopeLensFrame alone=panels[0];ScopeArrangeSurfaces(&alone,1);
            CHECK(draw(alone));
            read(220,160,r,g);CHECK(std::abs(r-149)<=3 && std::abs(g-64)<=3);
            read(140,160,r,g);CHECK(r==0 && g==0);
        }
    }
    printf("GPU timestamp/trial: %d failures, copy samples A=%llu B=%llu\n",failures,counts[0],counts[1]);
    return failures?1:0;
}
