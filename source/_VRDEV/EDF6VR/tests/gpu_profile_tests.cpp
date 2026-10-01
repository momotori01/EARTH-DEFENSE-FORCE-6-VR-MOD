#include "gpu_profile.h"
#include "gpu_split.h"
#include "warp_trial.h"
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
    printf("GPU timestamp/trial: %d failures, copy samples A=%llu B=%llu\n",failures,counts[0],counts[1]);
    return failures?1:0;
}
