#include "eye_check.h"
#include "eye_check_sampler.h"
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>

namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
constexpr ULONGLONG kEveryMs=5000, kGiveUpMs=2000;
// Two frames in a row, each: the left strips on top, the right cells below.
struct Slot { ComPtr<ID3D11Texture2D> staging; };
Slot slots[2];
DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
unsigned width=0,height=0;
int phase=0;                     // 0 idle, 1 first frame copied, 2 both copied and waiting to read
bool firstRead=false;            // phase 2: the first frame is already read
ID3D11Device* owner=nullptr;     // the device the staging textures belong to (compared, not held)
ULONGLONG nextAt=0,copiedAt=0;
std::unique_ptr<EyeStrips> strips[2];
// The comparison (about 20M differences) runs on its own thread; no new sample
// is taken, and the strips are left alone, until it has logged.
std::atomic<bool> analysing{false};
double readMs=0;                 // render-thread time spent turning the readback into strips
double Ms(LARGE_INTEGER from) { LARGE_INTEGER to{},rate{}; QueryPerformanceCounter(&to); QueryPerformanceFrequency(&rate); return rate.QuadPart?(to.QuadPart-from.QuadPart)*1000.0/rate.QuadPart:0; }

bool Prepare(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& d) {
    if(slots[0].staging && device==owner && d.Format==format && d.Width==width && d.Height==height) return true;
    slots[0].staging.Reset(); slots[1].staging.Reset();
    D3D11_TEXTURE2D_DESC s{};
    s.Width=kEyeStrips*kEyeCellW; s.Height=kEyeStripH+kEyeCellH; s.MipLevels=1; s.ArraySize=1;
    s.Format=d.Format; s.SampleDesc={1,0}; s.Usage=D3D11_USAGE_STAGING; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    for(auto& slot:slots) if(FAILED(device->CreateTexture2D(&s,nullptr,&slot.staging))) { slots[0].staging.Reset(); return false; }
    format=d.Format; width=d.Width; height=d.Height; owner=device;
    if(!strips[0]) { strips[0]=std::make_unique<EyeStrips>(); strips[1]=std::make_unique<EyeStrips>(); }
    return true;
}
void Copy(ID3D11DeviceContext* ctx,ID3D11Texture2D* left,ID3D11Texture2D* right,ID3D11Texture2D* staging) {
    for(unsigned s=0;s<kEyeStrips;++s) {
        unsigned x=0,y=0;
        if(!EyeStripOrigin(s,width,height,x,y)) continue;
        const D3D11_BOX l{x,y,0,x+kEyeStripW,y+kEyeStripH,1};
        ctx->CopySubresourceRegion(staging,0,s*kEyeCellW+kEyeSearchX,0,0,left,0,&l);
        const D3D11_BOX r{x-kEyeSearchX,y-kEyeSearchY,0,x+kEyeStripW+kEyeSearchX,y+kEyeStripH+kEyeSearchY,1};
        ctx->CopySubresourceRegion(staging,0,s*kEyeCellW,kEyeStripH,0,right,0,&r);
    }
}
// false while the GPU has not finished the copies.
bool Read(ID3D11DeviceContext* ctx,ID3D11Texture2D* staging,EyeStrips& out,EyePixel pixel) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(ctx->Map(staging,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped))) return false;
    const unsigned bytes=EyePixelBytes(pixel);
    const auto* base=static_cast<const unsigned char*>(mapped.pData);
    for(unsigned s=0;s<kEyeStrips;++s) {
        unsigned x=0,y=0;
        out.valid[s]=EyeStripOrigin(s,width,height,x,y);
        if(!out.valid[s]) continue;
        for(unsigned j=0;j<kEyeStripH;++j) {
            const unsigned char* row=base+static_cast<size_t>(j)*mapped.RowPitch+(static_cast<size_t>(s)*kEyeCellW+kEyeSearchX)*bytes;
            for(unsigned i=0;i<kEyeStripW;++i) out.left[s][j][i]=EyeGreen(pixel,row+static_cast<size_t>(i)*bytes);
        }
        for(unsigned j=0;j<kEyeCellH;++j) {
            const unsigned char* row=base+static_cast<size_t>(kEyeStripH+j)*mapped.RowPitch+static_cast<size_t>(s)*kEyeCellW*bytes;
            for(unsigned i=0;i<kEyeCellW;++i) out.right[s][j][i]=EyeGreen(pixel,row+static_cast<size_t>(i)*bytes);
        }
    }
    ctx->Unmap(staging,0);
    return true;
}
}

void EyeCheckFrame(ID3D11DeviceContext* ctx,ID3D11Texture2D* left,ID3D11Texture2D* right,EyeCheckLog log) noexcept {
    if(!ctx || !left || !right || !log || left==right) return;
    const auto now=GetTickCount64();
    if(phase==0 && (now<nextAt || analysing.load())) return;
    D3D11_TEXTURE2D_DESC d{},e{};
    left->GetDesc(&d); right->GetDesc(&e);
    const EyePixel pixel=EyePixelOf(static_cast<unsigned>(d.Format));
    if(d.Width!=e.Width || d.Height!=e.Height || d.Format!=e.Format || d.SampleDesc.Count!=1 || pixel==EyePixel::Unknown) {
        if(now>=nextAt) {
            char line[160]{};
            std::snprintf(line,sizeof(line),"EYECHECK cannot compare: left %ux%u format=%u samples=%u, right %ux%u format=%u",
                d.Width,d.Height,static_cast<unsigned>(d.Format),d.SampleDesc.Count,e.Width,e.Height,static_cast<unsigned>(e.Format));
            log(line);
        }
        phase=0; firstRead=false; nextAt=now+kEveryMs*6; return;
    }
    ComPtr<ID3D11Device> device; ctx->GetDevice(&device);
    // Resized, or a new device, mid-sample: start again.
    if(phase!=0 && (d.Format!=format || d.Width!=width || d.Height!=height || device.Get()!=owner)) { phase=0; firstRead=false; }
    if(phase==0) {
        if(!device || !Prepare(device.Get(),d)) { nextAt=now+kEveryMs*6; return; }
        Copy(ctx,left,right,slots[0].staging.Get());
        phase=1; return;
    }
    if(phase==1) {
        Copy(ctx,left,right,slots[1].staging.Get());
        phase=2; copiedAt=now; return;
    }
    // Both copied: read when the GPU is done, never waiting for it.
    if(now-copiedAt>kGiveUpMs) { phase=0; firstRead=false; nextAt=now+kEveryMs; return; }
    LARGE_INTEGER started{}; QueryPerformanceCounter(&started);
    if(!firstRead) { if(!Read(ctx,slots[0].staging.Get(),*strips[0],pixel)) return; readMs=Ms(started); }
    firstRead=true;
    QueryPerformanceCounter(&started);
    if(!Read(ctx,slots[1].staging.Get(),*strips[1],pixel)) return;
    readMs+=Ms(started);
    firstRead=false; phase=0; nextAt=now+kEveryMs;
    analysing.store(true);
    const unsigned w=width,h=height,f=static_cast<unsigned>(format);
    const double read=readMs;
    try {
        std::thread([log,w,h,f,read]() {
            LARGE_INTEGER started{}; QueryPerformanceCounter(&started);
            const auto r=AnalyzeEyes(*strips[0],*strips[1]);
            const double compare=Ms(started);
            char shifts[kEyeStrips*16]{}; int used=0;
            for(unsigned s=0;s<kEyeStrips && used>=0 && used<static_cast<int>(sizeof(shifts));++s) {
                if(!strips[1]->valid[s]) used+=std::snprintf(shifts+used,sizeof(shifts)-used,"%s-",s?" ":"");
                else if(!r.judged[s]) used+=std::snprintf(shifts+used,sizeof(shifts)-used,"%s?",s?" ":"");
                else used+=std::snprintf(shifts+used,sizeof(shifts)-used,"%s%+d/%+d",s?" ":"",r.dx[s],r.dy[s]);
            }
            char line[640]{};
            std::snprintf(line,sizeof(line),
                "EYECHECK %s. differ: L/R=%.3f L/previous R=%.3f motion L=%.3f R=%.3f; shift px (x/y, + = nearer is further right in the left eye) [%s] "
                "right-way=%d wrong-way=%d level=%d vertical=%d; %ux%u format=%u; cost %.2f ms game thread, %.1f ms own thread",
                r.verdict,r.lr,r.lrPrevious,r.motionLeft,r.motionRight,shifts,r.positive,r.negative,r.level,r.vertical,w,h,f,read,compare);
            log(line);
            analysing.store(false);
        }).detach();
    } catch(...) { analysing.store(false); }
}
void ResetEyeCheck() noexcept {
    slots[0].staging.Reset(); slots[1].staging.Reset();
    phase=0; firstRead=false; nextAt=0; format=DXGI_FORMAT_UNKNOWN; width=height=0; owner=nullptr;
}
}
