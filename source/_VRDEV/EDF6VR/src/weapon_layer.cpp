#include "weapon_layer.h"
#include "weapon_stereo.h"
#include "cockpit_lighting.h"
#include "native_world.h"
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
enum State { Off,Armed,Recording,AwaitLighting,Lighting,Pending,Done };
std::atomic<State> state{Off};
thread_local bool privateDraw=false;
CaptureLogger logger=nullptr;
wchar_t directory[MAX_PATH]{};
ULONGLONG first=0;
ULONGLONG submitted=0;
unsigned settle=8000;
unsigned seen=0;
bool wantLighting=false,litCaptured=false;
ULONGLONG lightingStarted=0;
ComPtr<ID3D11DeviceContext> lightingContext;
ComPtr<ID3D11Buffer> systemStaging;
unsigned systemBytes=0;
int capturedPass=-1;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11RenderTargetView> savedRtv[8];
ComPtr<ID3D11DepthStencilView> savedDsv,dsv;
ComPtr<ID3D11DepthStencilState> savedDepth,depthState;
ComPtr<ID3D11Texture2D> depth;
struct Surface {
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11Texture2D> colour,resolved,staging;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11Resource> native;
    D3D11_TEXTURE2D_DESC desc{};
    unsigned bytes=0;
};
Surface surfaces[8];
Surface lit;
ComPtr<ID3D11UnorderedAccessView> litUav;
unsigned targetCount=0;
bool depthSeeded=false;
ComPtr<ID3D11Query> complete;
UINT stencilRef=0;
D3D11_TEXTURE2D_DESC desc{};
D3D11_VIEWPORT viewport{};
bool outputStayed=false;
void Log(const char* text) noexcept { if(logger) logger(text); }
void Release() noexcept {
    for(auto& t:savedRtv) t.Reset();
    savedDsv.Reset(); savedDepth.Reset(); dsv.Reset(); depthState.Reset();
    for(auto& surface:surfaces) surface=Surface{};
    lit=Surface{}; litUav.Reset(); systemStaging.Reset(); lightingContext.Reset();
    depth.Reset(); complete.Reset();
    context.Reset(); device.Reset();
}
void Restore() noexcept {
    ID3D11RenderTargetView* targets[8]{};
    for(int i=0;i<8;++i) targets[i]=savedRtv[i].Get();
    context->OMSetRenderTargets(8,targets,savedDsv.Get());
    context->OMSetDepthStencilState(savedDepth.Get(),stencilRef);
    for(auto& t:savedRtv) t.Reset(); // never keep native swapchain references
    savedDsv.Reset(); savedDepth.Reset();
    privateDraw=false;
}
unsigned BytesPerPixel(DXGI_FORMAT format) noexcept {
    switch(format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R32G32_FLOAT: return 8;
    case DXGI_FORMAT_R11G11B10_FLOAT: case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: return 4;
    case DXGI_FORMAT_R16_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R8G8_UNORM: return 2;
    case DXGI_FORMAT_R8_UNORM: return 1;
    default:return 0;
    }
}
bool CreateSurface(unsigned index,std::uint64_t& totalBytes) noexcept {
    auto& surface=surfaces[index];
    D3D11_RENDER_TARGET_VIEW_DESC view{}; savedRtv[index]->GetDesc(&view);
    if(view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D && view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS) return false;
    if(view.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D && view.Texture2D.MipSlice!=0) return false;
    surface.bytes=BytesPerPixel(view.Format); if(!surface.bytes) return false;
    ComPtr<ID3D11Resource> resource; savedRtv[index]->GetResource(&resource);
    ComPtr<ID3D11Texture2D> original;
    if(FAILED(resource.As(&original))) return false;
    original->GetDesc(&surface.desc); auto& sd=surface.desc;
    if(sd.Width!=desc.Width || sd.Height!=desc.Height || sd.ArraySize!=1
       || sd.SampleDesc.Count!=desc.SampleDesc.Count || sd.SampleDesc.Quality!=desc.SampleDesc.Quality) return false;
    totalBytes+=std::uint64_t(sd.Width)*sd.Height*surface.bytes*(sd.SampleDesc.Count+(sd.SampleDesc.Count>1?2:1));
    if(totalBytes>1024ull*1024*1024) return false;
    sd.Format=view.Format; sd.MipLevels=sd.ArraySize=1; sd.Usage=D3D11_USAGE_DEFAULT;
    sd.BindFlags=D3D11_BIND_RENDER_TARGET | (wantLighting?D3D11_BIND_SHADER_RESOURCE:0); sd.CPUAccessFlags=sd.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&sd,nullptr,&surface.colour)) ||
       FAILED(device->CreateRenderTargetView(surface.colour.Get(),&view,&surface.rtv))) return false;
    if(wantLighting && sd.SampleDesc.Count==1) {
        if(FAILED(device->CreateShaderResourceView(surface.colour.Get(),nullptr,&surface.srv))) return false;
        surface.native=original;
    }
    auto d=sd; d.SampleDesc={1,0}; d.BindFlags=0;
    if(sd.SampleDesc.Count>1) { if(FAILED(device->CreateTexture2D(&d,nullptr,&surface.resolved))) return false; }
    else surface.resolved=surface.colour;
    d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&surface.staging))) return false;
    const float clear[4]{}; context->ClearRenderTargetView(surface.rtv.Get(),clear);
    return true;
}
bool CreateTargets() noexcept {
    std::uint64_t totalBytes=std::uint64_t(desc.Width)*desc.Height*8*desc.SampleDesc.Count;
    // Reserve the largest supported lighting output + its CPU staging texture.
    if(wantLighting) totalBytes+=std::uint64_t(desc.Width)*desc.Height*32;
    for(unsigned i=0;i<8;++i) if(savedRtv[i] && !CreateSurface(i,totalBytes)) return false;
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
    if(FAILED(device->CreateQuery(&query,&complete))) return false;
    depthSeeded=false;
    if(targetCount>1 && savedDsv) {
        // A diagnostic capture of EDF's material pass must use its existing
        // prepass depth/stencil; Equal/stencil tests on a cleared DSV erase it.
        // Copy into PRIVATE memory. This is not the final wall-independent layer.
        ComPtr<ID3D11Resource> resource; savedDsv->GetResource(&resource);
        ComPtr<ID3D11Texture2D> original;
        if(FAILED(resource.As(&original))) return false;
        D3D11_TEXTURE2D_DESC d{}; original->GetDesc(&d);
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; savedDsv->GetDesc(&dv);
        if(d.Width!=desc.Width || d.Height!=desc.Height || d.ArraySize!=1 || d.MipLevels!=1 || d.SampleDesc.Count!=desc.SampleDesc.Count) return false;
        d.Usage=D3D11_USAGE_DEFAULT; d.CPUAccessFlags=d.MiscFlags=0; d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&depth)) || FAILED(device->CreateDepthStencilView(depth.Get(),&dv,&dsv))) return false;
        context->CopyResource(depth.Get(),original.Get()); depthState=savedDepth; depthSeeded=true;
        return true;
    }
    auto d=desc; d.Format=DXGI_FORMAT_D32_FLOAT; d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    d.MipLevels=d.ArraySize=1; d.Usage=D3D11_USAGE_DEFAULT; d.CPUAccessFlags=d.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&depth)) || FAILED(device->CreateDepthStencilView(depth.Get(),nullptr,&dsv))) return false;
    D3D11_DEPTH_STENCIL_DESC ds{};
    if(savedDepth) savedDepth->GetDesc(&ds);
    const bool reverse=ds.DepthFunc==D3D11_COMPARISON_GREATER || ds.DepthFunc==D3D11_COMPARISON_GREATER_EQUAL;
    ds={}; ds.DepthEnable=TRUE; ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc=reverse?D3D11_COMPARISON_GREATER_EQUAL:D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(device->CreateDepthStencilState(&ds,&depthState))) return false;
    context->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,reverse?0.f:1.f,0);
    return true;
}
}
bool InWeaponLayerCapture() noexcept { return privateDraw || InWeaponStereoReplay(); }
bool WeaponLayerCaptureWanted() noexcept { return state.load(std::memory_order_relaxed)==Armed; }
bool WeaponLayerCapturePending() noexcept {
    const auto s=state.load(std::memory_order_acquire); return s==Pending || s==AwaitLighting;
}
void ConfigureWeaponLayerCapture(bool enabled,const wchar_t* path,CaptureLogger log,unsigned settleMs,bool lighting) noexcept {
    // Configuration is load-time only, before the game starts drawing.
    logger=log; if(path) wcsncpy_s(directory,path,_TRUNCATE);
    first=0; seen=0; settle=settleMs;
    wantLighting=lighting; litCaptured=false; systemBytes=0;
    state.store(enabled?Armed:Off,std::memory_order_release);
    Log(enabled?"WEAPONLAYER one-shot capture armed (8 second settling period)":"WEAPONLAYER capture disabled");
}
bool BeginWeaponLayerCapture(ID3D11DeviceContext* ctx,int pass,unsigned width,unsigned height) noexcept {
    State expected=Armed;
    if(!ctx || privateDraw || pass<0 || pass>5 || !state.compare_exchange_strong(expected,Recording)) return false;
    context=ctx; ctx->GetDevice(&device);
    ID3D11RenderTargetView* targets[8]{}; ctx->OMGetRenderTargets(8,targets,&savedDsv);
    unsigned count=0; for(int i=0;i<8;++i) { savedRtv[i].Attach(targets[i]); if(targets[i]) ++count; }
    ctx->OMGetDepthStencilState(&savedDepth,&stencilRef);
    desc={};
    if(targets[0]) {
        ComPtr<ID3D11Resource> resource; targets[0]->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        if(SUCCEEDED(resource.As(&texture))) texture->GetDesc(&desc);
    }
    UINT n=1; ctx->RSGetViewports(&n,&viewport);
    if(!(seen&(1u<<pass))) {
        seen|=1u<<pass; char line[256];
        std::snprintf(line,sizeof(line),"WEAPONLAYER survey pass=%d ctxType=%u rtCount=%u size=%ux%u format=%u samples=%u viewport=%.0fx%.0f depth=%d",
            pass,ctx->GetType(),count,desc.Width,desc.Height,desc.Format,desc.SampleDesc.Count,viewport.Width,viewport.Height,savedDsv?1:0); Log(line);
        for(unsigned i=0;i<8;++i) if(targets[i]) {
            D3D11_RENDER_TARGET_VIEW_DESC v{}; targets[i]->GetDesc(&v);
            std::snprintf(line,sizeof(line),"WEAPONLAYER target slot=%u viewFormat=%u viewDimension=%u",i,v.Format,v.ViewDimension); Log(line);
        }
    }
    if(!first) first=GetTickCount64();
    if(!device || count==0 || !targets[0] || desc.Width!=width || desc.Height!=height
       || !width || !height || GetTickCount64()-first<settle) {
        const bool expired=GetTickCount64()-first>20000;
        if(expired) Log("WEAPONLAYER no eligible scene targets within 20 seconds; survey saved, native draw unchanged");
        Release(); state.store(expired?Done:Armed,std::memory_order_release); return false;
    }
    targetCount=count;
    if(!CreateTargets()) {
        Log("WEAPONLAYER capture refused: unsupported target or allocation failed; native draw unchanged");
        Release(); state.store(Done,std::memory_order_release); return false;
    }
    privateDraw=true;
    ID3D11RenderTargetView* privateTargets[8]{};
    for(unsigned i=0;i<8;++i) privateTargets[i]=surfaces[i].rtv.Get();
    ctx->OMSetRenderTargets(8,privateTargets,dsv.Get());
    ctx->OMSetDepthStencilState(depthState.Get(),depthSeeded?stencilRef:0);
    capturedPass=pass;
    char line[160]; std::snprintf(line,sizeof(line),"WEAPONLAYER private targets=%u depthSeeded=%d bound for ONE native weapon draw",targetCount,depthSeeded); Log(line);
    return true;
}
void EndWeaponLayerCapture() noexcept {
    if(!privateDraw || state.load()!=Recording) return;
    ID3D11RenderTargetView* actual[8]{}; context->OMGetRenderTargets(8,actual,nullptr);
    outputStayed=true;
    for(unsigned i=0;i<8;++i) {
        if(actual[i]!=surfaces[i].rtv.Get()) outputStayed=false;
        if(actual[i]) actual[i]->Release();
    }
    // Preserve state changes made by the native material code. Undo only our
    // substitution; restoring all pre-draw state would desynchronise EDF's cache.
    ComPtr<ID3D11DepthStencilState> actualDepth; UINT actualRef=0;
    context->OMGetDepthStencilState(&actualDepth,&actualRef);
    D3D11_DEPTH_STENCIL_DESC afterDepth{};
    if(actualDepth) actualDepth->GetDesc(&afterDepth);
    if(actualDepth.Get()!=depthState.Get()) { savedDepth=actualDepth; stencilRef=actualRef; }
    if(!outputStayed) {
        for(auto& t:savedRtv) t.Reset(); savedDsv.Reset();
        ID3D11RenderTargetView* nativeTargets[8]{};
        context->OMGetRenderTargets(8,nativeTargets,&savedDsv);
        for(int i=0;i<8;++i) savedRtv[i].Attach(nativeTargets[i]);
    } else {
        ComPtr<ID3D11DepthStencilView> actualDsv;
        context->OMGetRenderTargets(0,nullptr,&actualDsv);
        if(actualDsv.Get()!=dsv.Get()) savedDsv=actualDsv;
    }
    char stateLine[200];
    std::snprintf(stateLine,sizeof(stateLine),"WEAPONLAYER after native targetStayed=%d depthEnable=%d depthFunc=%u depthWrite=%u stencil=%d",
        outputStayed,afterDepth.DepthEnable,afterDepth.DepthFunc,afterDepth.DepthWriteMask,afterDepth.StencilEnable); Log(stateLine);
    Restore();
    for(auto& surface:surfaces) if(surface.colour) {
        if(surface.desc.SampleDesc.Count>1) context->ResolveSubresource(surface.resolved.Get(),0,surface.colour.Get(),0,surface.desc.Format);
        context->CopyResource(surface.staging.Get(),surface.resolved.Get());
    }
    const bool waitLighting=wantLighting && outputStayed && targetCount==7 && desc.SampleDesc.Count==1
        && context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE;
    if(waitLighting) {
        lightingContext=context; lightingStarted=GetTickCount64();
        context.Reset(); state.store(AwaitLighting,std::memory_order_release);
        Log("WEAPONLAYER material captured; awaiting matching native lighting Dispatch in this frame");
        return;
    }
    for(auto& surface:surfaces) surface.native.Reset();
    context->End(complete.Get());
    // Native buffers released in Restore. Pending holds only private images.
    context.Reset(); submitted=GetTickCount64(); state.store(Pending,std::memory_order_release);
}
void DispatchWeaponLighting(ID3D11DeviceContext* ctx,UINT x,UINT y,UINT z) noexcept {
    // Not a vtable hook: this call goes straight to the runtime even when invoked
    // from a patched EDF call site. Always preserve the native dispatch exactly.
    ctx->Dispatch(x,y,z);
    if(NativeWorldCockpit())CaptureCockpitLighting(ctx,NativeWorldRenderFrame(),x,y,z);
    ReplayWeaponStereoLighting(ctx,x,y,z);
    State expected=AwaitLighting;
    if(state.load(std::memory_order_acquire)!=AwaitLighting) return;
    if(ctx!=lightingContext.Get() || x!=(desc.Width+15)/16 || y!=(desc.Height+15)/16 || z!=1) {
        char line[180]; std::snprintf(line,sizeof(line),"WEAPONLAYER lighting candidate contextMatch=%d groups=%u,%u,%u expected=%u,%u,1",
            ctx==lightingContext.Get(),x,y,z,(desc.Width+15)/16,(desc.Height+15)/16); Log(line); return;
    }
    if(!state.compare_exchange_strong(expected,Lighting,std::memory_order_acq_rel)) return;
    ComPtr<ID3D11ShaderResourceView> saved[7];
    ID3D11ShaderResourceView* raw[7]{}; ctx->CSGetShaderResources(0,7,raw);
    bool matches=true; unsigned matchedMask=0;
    for(unsigned i=0;i<7;++i) {
        saved[i].Attach(raw[i]); ComPtr<ID3D11Resource> resource;
        if(saved[i]) saved[i]->GetResource(&resource);
        if(!surfaces[i].srv || !surfaces[i].native || resource.Get()!=surfaces[i].native.Get()) matches=false;
        else matchedMask|=1u<<i;
    }
    if(!matches) {
        char line[128]; std::snprintf(line,sizeof(line),"WEAPONLAYER lighting SRV identity mask=0x%02X expected=0x7F; native dispatch only",matchedMask); Log(line);
        state.store(AwaitLighting,std::memory_order_release); return;
    }
    ComPtr<ID3D11UnorderedAccessView> outputs[8]; ID3D11UnorderedAccessView* uavs[8]{};
    ctx->CSGetUnorderedAccessViews(0,8,uavs);
    bool oneOutput=uavs[0]!=nullptr;
    for(unsigned i=0;i<8;++i) { outputs[i].Attach(uavs[i]); if(i && uavs[i]) oneOutput=false; }
    ComPtr<ID3D11Resource> outputResource; ComPtr<ID3D11Texture2D> outputTexture;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uv{};
    if(oneOutput) {
        outputs[0]->GetDesc(&uv); outputs[0]->GetResource(&outputResource);
        if(SUCCEEDED(outputResource.As(&outputTexture))) outputTexture->GetDesc(&lit.desc);
    }
    bool ok=oneOutput && outputTexture && uv.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2D
        && uv.Texture2D.MipSlice==0 && lit.desc.Width==desc.Width && lit.desc.Height==desc.Height
        && lit.desc.ArraySize==1 && lit.desc.SampleDesc.Count==1;
    if(ok) {
        lit.bytes=BytesPerPixel(uv.Format); ok=lit.bytes!=0;
        auto d=lit.desc; d.Format=uv.Format; d.MipLevels=1; d.Usage=D3D11_USAGE_DEFAULT;
        d.BindFlags=D3D11_BIND_UNORDERED_ACCESS; d.CPUAccessFlags=d.MiscFlags=0; lit.desc=d;
        ok=ok && SUCCEEDED(device->CreateTexture2D(&d,nullptr,&lit.colour))
            && SUCCEEDED(device->CreateUnorderedAccessView(lit.colour.Get(),&uv,&litUav));
        d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ok=ok && SUCCEEDED(device->CreateTexture2D(&d,nullptr,&lit.staging));
    }
    if(ok) {
        ComPtr<ID3D11Buffer> cb; ctx->CSGetConstantBuffers(0,1,&cb);
        if(cb) {
            D3D11_BUFFER_DESC bd{}; cb->GetDesc(&bd);
            if(bd.ByteWidth>=608 && bd.ByteWidth<=65536) {
                systemBytes=bd.ByteWidth; bd.Usage=D3D11_USAGE_STAGING;
                bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0; bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                if(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&systemStaging))) ctx->CopyResource(systemStaging.Get(),cb.Get());
            }
        }
        ID3D11ShaderResourceView* inputs[7]{};
        for(unsigned i=0;i<7;++i) inputs[i]=surfaces[i].srv.Get();
        // Only touched CS resources are replaced. CS shader/CBs/samplers and all
        // graphics state stay as native code left them; restore exact bindings.
        ID3D11UnorderedAccessView* privateOutput=litUav.Get();
        ctx->CSSetUnorderedAccessViews(0,1,&privateOutput,nullptr);
        ctx->CSSetShaderResources(0,7,inputs);
        ctx->Dispatch(x,y,z);
        ID3D11UnorderedAccessView* none=nullptr;
        ctx->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        ctx->CSSetShaderResources(0,7,raw);
        ctx->CSSetUnorderedAccessViews(0,1,uavs,nullptr);
        ctx->CopyResource(lit.staging.Get(),lit.colour.Get()); litCaptured=true;
        Log("WEAPONLAYER native lighting replayed once into private colour; all 7 SRVs and u0 restored");
    } else {
        lit=Surface{}; litUav.Reset();
        Log("WEAPONLAYER lighting capture refused incompatible UAV/allocation; native dispatch preserved");
    }
    for(auto& surface:surfaces) surface.native.Reset();
    ctx->End(complete.Get()); lightingContext.Reset(); submitted=GetTickCount64();
    state.store(Pending,std::memory_order_release);
}
void PollWeaponLayerCapture(ID3D11DeviceContext* immediate) noexcept {
    // Present ends the capture frame. Never match later-frame buffers/constants.
    State expected=AwaitLighting;
    if(immediate && immediate->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE
       && state.compare_exchange_strong(expected,Lighting,std::memory_order_acq_rel)) {
        Log("WEAPONLAYER no matching lighting Dispatch before Present; saving material only");
        for(auto& surface:surfaces) surface.native.Reset();
        if(immediate!=lightingContext.Get() || GetTickCount64()-lightingStarted>5000) {
            Release(); state.store(Done,std::memory_order_release); return;
        }
        immediate->End(complete.Get()); lightingContext.Reset(); submitted=GetTickCount64();
        state.store(Pending,std::memory_order_release);
    }
    if(state.load(std::memory_order_acquire)!=Pending || !immediate || immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    ComPtr<ID3D11Device> current; immediate->GetDevice(&current);
    if(current.Get()!=device.Get() || GetTickCount64()-submitted>5000) {
        Log("WEAPONLAYER pending capture cancelled: device changed or query timed out");
        Release(); state.store(Done,std::memory_order_release); return;
    }
    BOOL ready=FALSE; const auto hr=immediate->GetData(complete.Get(),&ready,sizeof(ready),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(hr==S_FALSE || (hr==S_OK && !ready)) return;
    if(FAILED(hr)) { Log("WEAPONLAYER query failed"); Release(); state.store(Done); return; }
    D3D11_MAPPED_SUBRESOURCE maps[9]{},cbMap{};
    bool mappedAll=true,failed=false;
    for(unsigned i=0;i<9;++i) if((i==8?lit:surfaces[i]).staging) {
        const auto hrMap=immediate->Map((i==8?lit:surfaces[i]).staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&maps[i]);
        if(FAILED(hrMap)) { mappedAll=false; failed=hrMap!=DXGI_ERROR_WAS_STILL_DRAWING; break; }
    }
    if(mappedAll && systemStaging) {
        const auto hrMap=immediate->Map(systemStaging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&cbMap);
        if(FAILED(hrMap)) { mappedAll=false; failed=hrMap!=DXGI_ERROR_WAS_STILL_DRAWING; }
    }
    if(!mappedAll) {
        for(unsigned i=0;i<9;++i) if(maps[i].pData) immediate->Unmap((i==8?lit:surfaces[i]).staging.Get(),0);
        if(failed) { Log("WEAPONLAYER map failed"); Release(); state.store(Done); }
        return;
    }
    bool saved=true;
    try {
        std::filesystem::create_directories(directory);
        for(unsigned i=0;i<9;++i) if(maps[i].pData) {
            const auto& surface=i==8?lit:surfaces[i]; const auto& map=maps[i];
            wchar_t name[40]{};
            if(i==0) wcscpy_s(name,L"weapon_native");
            else if(i==8) wcscpy_s(name,L"weapon_lit"); else swprintf_s(name,L"weapon_target%u",i);
            const auto base=std::filesystem::path(directory)/name;
            const auto raw=base.wstring()+L".raw";
            FILE* f=nullptr; bool rawSaved=false;
            if(!_wfopen_s(&f,raw.c_str(),L"wb") && f) {
                rawSaved=true;
                for(unsigned y=0;y<desc.Height;++y)
                    if(std::fwrite(static_cast<unsigned char*>(map.pData)+std::size_t(y)*map.RowPitch,surface.bytes,desc.Width,f)!=desc.Width) rawSaved=false;
                if(std::fclose(f)) rawSaved=false;
            }
            saved=saved && rawSaved;
            const auto json=base.wstring()+L".json";
            if(!_wfopen_s(&f,json.c_str(),L"w") && f) {
                std::fprintf(f,"{\"width\":%u,\"height\":%u,\"format\":%u,\"samples\":%u,\"bytesPerPixel\":%u,\"pass\":%d,\"targetSlot\":%u,\"targetCount\":%u,\"depthSeeded\":%s,\"targetStayedBound\":%s,\"rawSaved\":%s}\n",
                    desc.Width,desc.Height,surface.desc.Format,desc.SampleDesc.Count,surface.bytes,capturedPass,i,targetCount,
                    depthSeeded?"true":"false",outputStayed?"true":"false",rawSaved?"true":"false");
                if(std::fclose(f)) saved=false;
            } else saved=false;
        }
        if(cbMap.pData) {
            FILE* f=nullptr; const auto path=std::filesystem::path(directory)/L"lighting_cb0.bin";
            if(!_wfopen_s(&f,path.c_str(),L"wb") && f) {
                if(std::fwrite(cbMap.pData,1,systemBytes,f)!=systemBytes) saved=false;
                if(std::fclose(f)) saved=false;
            } else saved=false;
        }
    } catch(...) { saved=false; }
    for(unsigned i=0;i<9;++i) if(maps[i].pData) immediate->Unmap((i==8?lit:surfaces[i]).staging.Get(),0);
    if(cbMap.pData) immediate->Unmap(systemStaging.Get(),0);
    char line[200]; std::snprintf(line,sizeof(line),"WEAPONLAYER capture finished saved=%d targetStayed=%d size=%ux%u targets=%u pass=%d; native rendering restored",
        saved,outputStayed,desc.Width,desc.Height,targetCount,capturedPass); Log(line);
    std::snprintf(line,sizeof(line),"WEAPONLAYER lighting saved=%d cb0Bytes=%u",litCaptured && saved,systemBytes); Log(line);
    Release(); state.store(Done,std::memory_order_release);
}
}
