#include "weapon_stereo.h"
#include "cockpit_draw.h"
#include "weapon_composite.h"
#include "hand_draw.h"
#include <cmath>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
enum State { Off,Armed,Recording,AwaitLighting,Lighting,Pending,Ready,Done };
bool collectingModels=false;
unsigned modelBatches=0,drawsBeforeBatch=0;
std::atomic<State> state{Off};
thread_local bool recording=false,replaying=false;
CaptureLogger logger=nullptr;
wchar_t directory[MAX_PATH]{};
unsigned settle=8000,width=0,height=0,draws=0,rejected=0,rejectMask=0;
ULONGLONG first=0,submitted=0;
bool wantLighting=false,litCaptured=false;
bool liveMode=false,suppressReady=false,depthHidden=false;
unsigned composed=0;
unsigned loggedTargetFormat=0;
ULONGLONG lastLiveLog=0,liveFrames=0;
unsigned pairStrikes=0,skippedPairs=0;   // see KeepWeaponStereo
// The hands share the frame's targets with the weapon. Whichever of the two
// arrives first in a frame clears them; the epoch says which frame that was.
unsigned frameEpoch=0,clearedEpoch=~0u;
bool handsDrawn=false;
unsigned handDraws=0,handRejected=0,handRejectMask=0;
unsigned weaponDrawsThisFrame=0;   // 0 in a frame whose layer holds only the hands
thread_local bool isolatingDepth=false;
ComPtr<ID3D11DepthStencilView> worldDsv,isolatedDsv;
ComPtr<ID3D11Texture2D> isolatedDepth;
ComPtr<ID3D11DeviceContext> depthContext;
// Capture defaults about the native camera; live mode sets HMD-relative offsets.
float shift[2]={-.032f,.032f};
bool originValid=false;
float origin[3]{};
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11RenderTargetView> native[7];
ComPtr<ID3D11DepthStencilState> depthState;
ComPtr<ID3D11ComputeShader> transform;
ComPtr<ID3D11Buffer> source,result,eyeCb[2],parameters[2],cbStaging[6];
ComPtr<ID3D11ShaderResourceView> sourceSrv;
ComPtr<ID3D11UnorderedAccessView> resultUav;
ComPtr<ID3D11Query> complete;
struct Image {
    ComPtr<ID3D11Texture2D> gpu,staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    unsigned bytes=0;
};
Image targets[2][7]; // native MRT indices; only 0 and 2 get CPU staging
Image lit[2]; // capture: shared GPU + staging; live: two persistent GPU images
ComPtr<ID3D11UnorderedAccessView> litUav[2];
ComPtr<ID3D11ShaderResourceView> neutralAo;
ComPtr<ID3D11SamplerState> neutralAoSampler;
ComPtr<ID3D11Texture2D> depth[2];
ComPtr<ID3D11DepthStencilView> dsv[2];
void Log(const char* s) noexcept { if(logger) logger(s); }
void Release() noexcept {
    for(auto& n:native) n.Reset();
    for(auto& eye:targets) for(auto& t:eye) t=Image{};
    for(auto& t:lit) t=Image{}; for(auto& u:litUav) u.Reset();
    neutralAo.Reset(); neutralAoSampler.Reset();
    for(auto& t:depth) t.Reset(); for(auto& t:dsv) t.Reset();
    for(auto& t:eyeCb) t.Reset(); for(auto& t:parameters) t.Reset();
    for(auto& t:cbStaging) t.Reset();
    source.Reset(); result.Reset(); sourceSrv.Reset(); resultUav.Reset();
    depthState.Reset(); transform.Reset(); complete.Reset(); context.Reset(); device.Reset();
    worldDsv.Reset(); isolatedDsv.Reset(); isolatedDepth.Reset(); depthContext.Reset();
    suppressReady=depthHidden=false;
    collectingModels=false; modelBatches=drawsBeforeBatch=0;
    ReleaseWeaponComposite();
}
unsigned PixelBytes(DXGI_FORMAT f) noexcept {
    switch(f) {
    case DXGI_FORMAT_R11G11B10_FLOAT: case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R16G16_FLOAT: return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
    default:return 0;
    }
}
// xgl_system is 38 float4 rows (608 bytes). Rows are stored explicitly;
// there is no HLSL matrix-major convention or CPU readback involved here.
constexpr char shader[]=R"(
StructuredBuffer<uint4> src:register(t0);
RWStructuredBuffer<uint4> dst:register(u0);
cbuffer Eye:register(b0) { float dx; float anchored; float2 padding; float3 origin; float pad; };
[numthreads(64,1,1)] void main(uint i:SV_DispatchThreadID) {
 if(i>=38) return;
 float4 v=asfloat(src[i]);
 float3 right=float3(asfloat(src[4]).x,asfloat(src[5]).x,asfloat(src[6]).x);
 float3 camera=float3(asfloat(src[4]).w,asfloat(src[5]).w,asfloat(src[6]).w);
 float3 delta=right*dx;
 if(anchored!=0) delta+=origin-camera;
 if(i<3) v.w-=dot(v.xyz,delta);             // view camera translation
 if(i>=4 && i<7) v.w+=delta[i-4];           // inverse view camera origin
 if(i>=12 && i<16) v.w-=dot(v.xyz,delta);    // VP, retaining native rotation/projection
 if(i>=16 && i<19) v+=delta[i-16]*asfloat(src[19]);
 dst[i]=asuint(v);                           // inverse(P V')
})";
bool Create() noexcept {
    // Capture peaks at 1012.5 MiB at 4K. Live keeps seven MRTs per eye,
    // two lit outputs and private depth, no staging: about 822.7 MiB.
    D3D11_TEXTURE2D_DESC td[7]{};
    DXGI_FORMAT formats[7]{};
    std::uint64_t bytes=std::uint64_t(width)*height*8;
    // Live uses 2 lit GPU images + a private native-depth sink (no CPU copies).
    if(wantLighting) bytes+=std::uint64_t(width)*height*24;
    for(unsigned t=0;t<7;++t) {
        if(!wantLighting && t!=0 && t!=2) continue;
        if(!native[t]) return false;
        D3D11_RENDER_TARGET_VIEW_DESC rv{}; native[t]->GetDesc(&rv);
        ComPtr<ID3D11Resource> resource; native[t]->GetResource(&resource);
        ComPtr<ID3D11Texture2D> tex;
        if(FAILED(resource.As(&tex))) return false;
        tex->GetDesc(&td[t]); auto& d=td[t];
        if(rv.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || rv.Texture2D.MipSlice!=0 ||
           d.Width!=width || d.Height!=height || d.SampleDesc.Count!=1 || d.ArraySize!=1 || !PixelBytes(rv.Format)) return false;
        formats[t]=rv.Format;
        bytes+=std::uint64_t(width)*height*PixelBytes(rv.Format)*(!liveMode && (t==0 || t==2)?4:2);
    }
    if(bytes>1024ull*1024*1024) return false;
    for(unsigned eye=0;eye<2;++eye) {
        for(unsigned t=0;t<7;++t) {
            if(!wantLighting && t!=0 && t!=2) continue;
            auto& out=targets[eye][t]; auto d=td[t];
            d.Format=out.format=formats[t]; out.bytes=PixelBytes(d.Format);
            d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_RENDER_TARGET | (wantLighting?D3D11_BIND_SHADER_RESOURCE:0);
            d.CPUAccessFlags=d.MiscFlags=0; d.MipLevels=1;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&out.gpu)) ||
               FAILED(device->CreateRenderTargetView(out.gpu.Get(),nullptr,&out.rtv))) return false;
            if(wantLighting && FAILED(device->CreateShaderResourceView(out.gpu.Get(),nullptr,&out.srv))) return false;
            if(!liveMode && (t==0 || t==2)) {
                d.Usage=D3D11_USAGE_STAGING; d.BindFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                if(FAILED(device->CreateTexture2D(&d,nullptr,&out.staging))) return false;
            }
            const float clear[4]{}; context->ClearRenderTargetView(out.rtv.Get(),clear);
        }
        auto d=td[0]; d.Format=DXGI_FORMAT_D32_FLOAT; d.MipLevels=1;
        d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_DEPTH_STENCIL; d.CPUAccessFlags=d.MiscFlags=0;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&depth[eye])) ||
           FAILED(device->CreateDepthStencilView(depth[eye].Get(),nullptr,&dsv[eye]))) return false;
        context->ClearDepthStencilView(dsv[eye].Get(),D3D11_CLEAR_DEPTH,1,0);
    }
    D3D11_DEPTH_STENCIL_DESC ds{}; ds.DepthEnable=TRUE;
    ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    if(FAILED(device->CreateDepthStencilState(&ds,&depthState))) return false;
    // Same optional compiler dependency as eye_warp; no new DLL load requirement
    // when this diagnostic is disabled.
    static const HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");
    if(!compiler) return false;
    const auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    if(!compile) return false;
    ComPtr<ID3DBlob> blob,error;
    if(FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&blob,&error))) {
        if(error) Log(static_cast<const char*>(error->GetBufferPointer())); return false;
    }
    if(FAILED(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&transform))) return false;
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=608; bd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride=16;
    if(FAILED(device->CreateBuffer(&bd,nullptr,&source)) ||
       FAILED(device->CreateShaderResourceView(source.Get(),nullptr,&sourceSrv))) return false;
    bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    if(FAILED(device->CreateBuffer(&bd,nullptr,&result)) ||
       FAILED(device->CreateUnorderedAccessView(result.Get(),nullptr,&resultUav))) return false;
    bd.MiscFlags=bd.StructureByteStride=0; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    for(auto& cb:eyeCb) if(FAILED(device->CreateBuffer(&bd,nullptr,&cb))) return false;
    bd.BindFlags=0; bd.Usage=D3D11_USAGE_STAGING; bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(!liveMode) for(auto& cb:cbStaging) if(FAILED(device->CreateBuffer(&bd,nullptr,&cb))) return false;
    bd={}; bd.ByteWidth=32; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    for(unsigned eye=0;eye<2;++eye) {
        const float p[8]={shift[eye],originValid?1.f:0.f,0,0,origin[0],origin[1],origin[2],0}; D3D11_SUBRESOURCE_DATA data{p,0,0};
        if(FAILED(device->CreateBuffer(&bd,&data,&parameters[eye]))) return false;
    }
    D3D11_QUERY_DESC q{D3D11_QUERY_EVENT,0};
    return liveMode || SUCCEEDED(device->CreateQuery(&q,&complete));
}
void TransformBuffers(ID3D11DeviceContext* ctx,ID3D11Buffer* cb) noexcept {
    // Preserve exactly the compute slots we borrow, including class instances.
    ComPtr<ID3D11ComputeShader> cs; ID3D11ClassInstance* instances[256]{}; UINT n=256;
    ComPtr<ID3D11Buffer> oldCb; ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ctx->CSGetShader(&cs,instances,&n); ctx->CSGetConstantBuffers(0,1,&oldCb);
    ctx->CSGetShaderResources(0,1,&srv); ctx->CSGetUnorderedAccessViews(0,1,&uav);
    ctx->CopyResource(source.Get(),cb);
    ctx->CSSetShader(transform.Get(),nullptr,0);
    auto* s=sourceSrv.Get(); auto* u=resultUav.Get();
    ctx->CSSetShaderResources(0,1,&s); ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    for(unsigned eye=0;eye<2;++eye) {
        auto* p=parameters[eye].Get(); ctx->CSSetConstantBuffers(0,1,&p);
        ctx->Dispatch(1,1,1);
        ctx->CopyResource(eyeCb[eye].Get(),result.Get());
    }
    s=nullptr; u=nullptr; ctx->CSSetShaderResources(0,1,&s); ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    s=srv.Get(); u=uav.Get(); auto* b=oldCb.Get();
    ctx->CSSetShaderResources(0,1,&s); ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    ctx->CSSetConstantBuffers(0,1,&b); ctx->CSSetShader(cs.Get(),instances,n);
    for(unsigned i=0;i<n;++i) if(instances[i]) instances[i]->Release();
    if(!liveMode && draws==0) {
        ctx->CopyResource(cbStaging[0].Get(),cb);
        for(unsigned eye=0;eye<2;++eye) ctx->CopyResource(cbStaging[eye+1].Get(),eyeCb[eye].Get());
    }
}
}
bool InWeaponStereoReplay() noexcept { return replaying || isolatingDepth; }
bool WeaponStereoCaptureWanted(bool appendModels) noexcept {
    const auto current=state.load(std::memory_order_relaxed);
    return current==Armed || (appendModels && liveMode && collectingModels && current==AwaitLighting);
}
bool WeaponStereoCapturePending() noexcept { const auto s=state.load(std::memory_order_acquire); return s==Pending || s==AwaitLighting; }
void ConfigureWeaponStereoCapture(bool enabled,const wchar_t* path,CaptureLogger log,unsigned settleMs,bool lighting) noexcept {
    Release();
    logger=log; if(path) wcsncpy_s(directory,path,_TRUNCATE);
    settle=settleMs; first=0; draws=rejected=rejectMask=0;
    wantLighting=lighting; litCaptured=false;
    liveMode=false; suppressReady=depthHidden=false; shift[0]=-.032f; shift[1]=.032f;
    originValid=false;
    state.store(enabled?Armed:Off,std::memory_order_release);
    Log(enabled?"WEAPONSTEREO one-shot GPU mesh pair armed; native view +/-32mm":"WEAPONSTEREO disabled");
    if(enabled && lighting) Log("WEAPONSTEREO native lighting for both eyes enabled; same-frame matching required");
}
void ConfigureWeaponStereoLive(bool enabled) noexcept {
    liveMode=enabled; suppressReady=depthHidden=false; composed=0;
    pairStrikes=skippedPairs=0;
    loggedTargetFormat=0;
    if(enabled) { wantLighting=true; settle=0; state.store(Armed); Log("WEAPONLIVE enabled; warm up before suppressing native colour/depth"); }
}
bool WeaponStereoLive() noexcept { return liveMode && state.load()!=Off && state.load()!=Done; }
void SetWeaponStereoOrigin(const float centre[3],float offset) noexcept {
    if(!liveMode || !centre || !std::isfinite(offset)) return;
    for(unsigned j=0;j<3;++j) if(!std::isfinite(centre[j]) || std::fabs(centre[j])>1e6f) return;
    for(unsigned j=0;j<3;++j) origin[j]=centre[j];
    originValid=true;
}
void SetWeaponStereoEyes(float left,float right) noexcept {
    if(!liveMode || !std::isfinite(left) || !std::isfinite(right) || std::fabs(left)>.3f || std::fabs(right)>.3f) return;
    shift[0]=left; shift[1]=right;
}
void SetWeaponStereoNativeCamera(float left,float right) noexcept {
    if(!liveMode || !std::isfinite(left) || !std::isfinite(right) || std::fabs(left)>.3f || std::fabs(right)>.3f) return;
    originValid=false;
    SetWeaponStereoEyes(left,right);
}
void ClearLiveFrame(ID3D11DeviceContext* ctx) noexcept {
    if(clearedEpoch==frameEpoch) return;   // the hands got here first this frame
    clearedEpoch=frameEpoch;
    for(unsigned eye=0;eye<2;++eye) {
        for(auto& t:targets[eye]) if(t.rtv) { const float clear[4]{}; ctx->ClearRenderTargetView(t.rtv.Get(),clear); }
        ctx->ClearDepthStencilView(dsv[eye].Get(),D3D11_CLEAR_DEPTH,1,0);
        const float p[8]={shift[eye],originValid?1.f:0.f,0,0,origin[0],origin[1],origin[2],0}; ctx->UpdateSubresource(parameters[eye].Get(),0,nullptr,p,0,0);
    }
}
bool BeginWeaponStereoCapture(ID3D11DeviceContext* ctx,int pass,unsigned w,unsigned h,bool appendModels) noexcept {
    if(!ctx || pass!=1 || recording || ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    // Resume the same frame's targets before native lighting, without clearing
    // the first weapon's colour/depth. Other contexts and scene targets cannot append.
    if(appendModels && liveMode && collectingModels && state.load()==AwaitLighting) {
        if(ctx!=context.Get() || width!=w || height!=h) return false;
        ID3D11RenderTargetView* rt[7]{}; ctx->OMGetRenderTargets(7,rt,nullptr);
        bool same=true;
        for(unsigned i=0;i<7;++i) { same=same && rt[i]==native[i].Get(); if(rt[i]) rt[i]->Release(); }
        if(!same) return false;
        State expected=AwaitLighting; if(!state.compare_exchange_strong(expected,Recording)) return false;
        recording=true; drawsBeforeBatch=draws;
        return true;
    }
    State expected=Armed; if(!state.compare_exchange_strong(expected,Recording)) return false;
    if(!first) first=GetTickCount64();
    if(GetTickCount64()-first<settle) { state.store(Armed); return false; }
    ComPtr<ID3D11Device> actualDevice; ctx->GetDevice(&actualDevice);
    if(device && (device.Get()!=actualDevice.Get() || width!=w || height!=h)) Release();
    context=ctx; device=actualDevice; width=w; height=h;
    ID3D11RenderTargetView* rt[7]{}; ctx->OMGetRenderTargets(7,rt,nullptr);
    for(unsigned i=0;i<7;++i) native[i].Attach(rt[i]);
    if(liveMode && targets[0][0].gpu) {
        for(unsigned i=0;i<7;++i) {
            D3D11_RENDER_TARGET_VIEW_DESC rv{}; if(native[i]) native[i]->GetDesc(&rv);
            ComPtr<ID3D11Resource> r; ComPtr<ID3D11Texture2D> t;
            if(native[i]) native[i]->GetResource(&r);
            D3D11_TEXTURE2D_DESC desc{}; if(r && SUCCEEDED(r.As(&t))) t->GetDesc(&desc);
            if(!t || rv.Format!=targets[0][i].format || desc.Width!=w || desc.Height!=h ||
               desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || rv.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || rv.Texture2D.MipSlice!=0) {
                Log("WEAPONLIVE material layout changed; native rendering restored"); Release(); state.store(Off); return false;
            }
        }
    }
    UINT count=1; D3D11_VIEWPORT vp{}; ctx->RSGetViewports(&count,&vp);
    if(!w || !h || !native[0] || !native[2] || count!=1 || vp.TopLeftX!=0 || vp.TopLeftY!=0 ||
       vp.Width!=float(w) || vp.Height!=float(h) || vp.MinDepth!=0 || vp.MaxDepth!=1 ||
       (!(liveMode && targets[0][0].gpu) && !Create())) {
        Log("WEAPONSTEREO refused unsupported scene or allocation; native rendering unchanged");
        Release(); state.store(Done); return false;
    }
    if(liveMode) {
        draws=rejected=rejectMask=0; litCaptured=false; composed=0; weaponDrawsThisFrame=0;
        ClearLiveFrame(ctx);
    }
    collectingModels=appendModels && liveMode;
    modelBatches=0; drawsBeforeBatch=draws;
    recording=true;
    if(!liveMode) Log("WEAPONSTEREO collecting own weapon DrawIndexed calls; native rendering preserved");
    return true;
}
// Each pixel shader the layer replays, reported once with how it writes:
// alpha to coverage, blending, and the colour write mask. Diagnostic only.
// The Air Raider's radio comes out as a field of dots, and whether its
// material is a coverage- or blend-transparent one -- which the private
// targets would handle differently from the game's -- or an opaque one
// dithered in its own shader decides where the fix goes.
static void NoteWeaponMaterial(ID3D11DeviceContext* ctx) noexcept {
    static const void* seen[32]{};
    ComPtr<ID3D11PixelShader> ps; ctx->PSGetShader(&ps,nullptr,nullptr);
    for(auto& s:seen) {
        if(s==ps.Get()) return;
        if(s) continue;
        s=ps.Get();
        ComPtr<ID3D11BlendState> blend; float factor[4]{}; UINT sampleMask=0;
        ctx->OMGetBlendState(&blend,factor,&sampleMask);
        D3D11_BLEND_DESC d{}; if(blend) blend->GetDesc(&d);
        ComPtr<ID3D11RasterizerState> raster; ctx->RSGetState(&raster);
        D3D11_RASTERIZER_DESC r{}; if(raster) raster->GetDesc(&r);
        char line[256];
        std::snprintf(line,sizeof(line),"WEAPONMAT pixel shader %p a2c=%d independent=%d blend0=%d src=%d dst=%d op=%d write0=0x%X write1=0x%X sampleMask=0x%X cull=%d",
            ps.Get(),d.AlphaToCoverageEnable?1:0,d.IndependentBlendEnable?1:0,d.RenderTarget[0].BlendEnable?1:0,
            int(d.RenderTarget[0].SrcBlend),int(d.RenderTarget[0].DestBlend),int(d.RenderTarget[0].BlendOp),
            unsigned(d.RenderTarget[0].RenderTargetWriteMask),unsigned(d.RenderTarget[1].RenderTargetWriteMask),sampleMask,raster?int(r.CullMode):-1);
        Log(line);
        return;
    }
}
// One DrawIndexed, issued again for each eye into the private targets with the
// camera constants moved to that eye. On refusal the reason is added to `mask`
// and logged the first time it is seen.
bool ReplayIndexed(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base,unsigned& mask,const char* tag) noexcept {
    ComPtr<ID3D11Buffer> vsCb,psCb;
    ctx->VSGetConstantBuffers(0,1,&vsCb); ctx->PSGetConstantBuffers(0,1,&psCb);
    D3D11_BUFFER_DESC b{}; if(vsCb) vsCb->GetDesc(&b);
    unsigned why=(!vsCb || vsCb.Get()!=psCb.Get() || b.ByteWidth!=608)?1u:0u;
    ComPtr<ID3D11GeometryShader> gs; ComPtr<ID3D11HullShader> hs; ComPtr<ID3D11DomainShader> ds;
    ctx->GSGetShader(&gs,nullptr,nullptr); ctx->HSGetShader(&hs,nullptr,nullptr); ctx->DSGetShader(&ds,nullptr,nullptr);
    if(gs || hs || ds) why|=2;
    ID3D11UnorderedAccessView* uavs[8]{};
    ctx->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,8,uavs);
    for(auto* u:uavs) if(u) { why|=4; u->Release(); }
    ID3D11RenderTargetView* rt[8]{}; ComPtr<ID3D11DepthStencilView> oldDsv;
    ctx->OMGetRenderTargets(8,rt,&oldDsv);
    if(rt[0]!=native[0].Get() || rt[2]!=native[2].Get()) why|=8;
    if(why) {
        if((why & ~mask)!=0) {
            D3D11_BUFFER_DESC psDesc{}; if(psCb) psCb->GetDesc(&psDesc);
            char line[256]; std::snprintf(line,sizeof(line),"%s reject mask=0x%X VSb0=%u PSb0=%u sameCB=%d GS=%d HS=%d DS=%d nativeTargets=%d",tag,why,b.ByteWidth,psDesc.ByteWidth,vsCb.Get()==psCb.Get(),gs?1:0,hs?1:0,ds?1:0,(why&8)?0:1); Log(line);
        }
        mask|=why;
        for(auto* t:rt) if(t) t->Release();
        return false;
    }
    NoteWeaponMaterial(ctx);
    ComPtr<ID3D11DepthStencilState> oldDepth; UINT ref=0; ctx->OMGetDepthStencilState(&oldDepth,&ref);
    replaying=true; // hide only our private OM binds from the world/UI survey
    TransformBuffers(ctx,vsCb.Get());
    for(unsigned eye=0;eye<2;++eye) {
        ID3D11RenderTargetView* out[7]{};
        for(unsigned i=0;i<7;++i) out[i]=targets[eye][i].rtv.Get();
        ctx->OMSetRenderTargets(7,out,dsv[eye].Get());
        ctx->OMSetDepthStencilState(depthState.Get(),0);
        auto* cb=eyeCb[eye].Get(); ctx->VSSetConstantBuffers(0,1,&cb); ctx->PSSetConstantBuffers(0,1,&cb);
        ctx->DrawIndexed(count,start,base); // same mesh, indices, material and bone palette; new camera
    }
    auto* cb=vsCb.Get(); ctx->VSSetConstantBuffers(0,1,&cb); cb=psCb.Get(); ctx->PSSetConstantBuffers(0,1,&cb);
    ctx->OMSetRenderTargets(8,rt,oldDsv.Get()); ctx->OMSetDepthStencilState(oldDepth.Get(),ref);
    for(auto* t:rt) if(t) t->Release();
    replaying=false;
    return true;
}
static std::atomic<unsigned long long> replaysTotal{0};
static thread_local int siteProbePass=-1;
static std::atomic<unsigned long long> siteProbeCounts[4]{};
void SetWeaponSiteProbe(int pass) noexcept { siteProbePass=pass; }
unsigned long long WeaponSiteProbeCount(int pass) noexcept {
    return pass>=0 && pass<4?siteProbeCounts[pass].load(std::memory_order_relaxed):0;
}
void DrawWeaponStereo(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base) noexcept {
    if(siteProbePass>=0 && siteProbePass<4) siteProbeCounts[siteProbePass].fetch_add(1,std::memory_order_relaxed);
    if(CockpitLimbIntercept(ctx,count,start,base)) return;
    if(TruckGlassIntercept(ctx,count)) return;
    // The body's own draws, while the hands are being taken out of them.
    if(HandDrawIntercept(ctx,count,start,base)) return;
    const bool omitNative=recording && liveMode && depthHidden && context.Get()==ctx;
    if(!omitNative) ctx->DrawIndexed(count,start,base);
    if(!recording || replaying || context.Get()!=ctx) return;
    if(!ReplayIndexed(ctx,count,start,base,rejectMask,"WEAPONSTEREO")) { ++rejected; return; }
    ++draws; ++weaponDrawsThisFrame;
    replaysTotal.fetch_add(1,std::memory_order_relaxed);
}
void EndWeaponStereoCapture() noexcept {
    if(!recording) return;
    recording=false;
    if(!liveMode) { char line[192]; std::snprintf(line,sizeof(line),"WEAPONSTEREO pair submitted draws=%u rejected=%u rejectMask=0x%X size=%ux%u",draws,rejected,rejectMask,width,height); Log(line); }
    if(draws>drawsBeforeBatch) ++modelBatches;
    if(!draws && collectingModels) { state.store(AwaitLighting,std::memory_order_release); return; }
    if(!draws) { Release(); state.store(Done); return; }
    for(auto& eye:targets) for(auto& t:eye) if(t.staging) context->CopyResource(t.staging.Get(),t.gpu.Get());
    if(wantLighting) {
        submitted=GetTickCount64(); state.store(AwaitLighting,std::memory_order_release);
        if(!liveMode) Log("WEAPONSTEREO awaiting native lighting in this frame"); return;
    }
    for(auto& n:native) n.Reset();
    context->End(complete.Get()); context.Reset(); submitted=GetTickCount64(); state.store(Pending,std::memory_order_release);
}
void ReplayWeaponStereoLighting(ID3D11DeviceContext* ctx,UINT x,UINT y,UINT z) noexcept {
    if(state.load(std::memory_order_acquire)!=AwaitLighting || ctx!=context.Get() ||
       x!=(width+15)/16 || y!=(height+15)/16 || z!=1) return;
    ID3D11ShaderResourceView* raw[7]{}; ctx->CSGetShaderResources(0,7,raw);
    ComPtr<ID3D11ShaderResourceView> saved[7]; unsigned mask=0;
    for(unsigned i=0;i<7;++i) {
        saved[i].Attach(raw[i]); ComPtr<ID3D11Resource> a,b;
        if(saved[i]) saved[i]->GetResource(&a);
        if(native[i]) native[i]->GetResource(&b);
        if(a && a.Get()==b.Get()) mask|=1u<<i;
    }
    if(mask!=0x7F) {
        char line[128]; std::snprintf(line,sizeof(line),"WEAPONSTEREO lighting rejected SRV identity mask=0x%X",mask); Log(line); return;
    }
    ID3D11UnorderedAccessView* uavs[8]{}; ctx->CSGetUnorderedAccessViews(0,8,uavs);
    ComPtr<ID3D11UnorderedAccessView> outputs[8]; bool one=uavs[0]!=nullptr;
    for(unsigned i=0;i<8;++i) { outputs[i].Attach(uavs[i]); if(i && uavs[i]) one=false; }
    ComPtr<ID3D11Resource> resource; ComPtr<ID3D11Texture2D> output;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uv{}; D3D11_TEXTURE2D_DESC td{};
    if(one) {
        outputs[0]->GetDesc(&uv); outputs[0]->GetResource(&resource);
        if(SUCCEEDED(resource.As(&output))) output->GetDesc(&td);
    }
    ComPtr<ID3D11Buffer> cb; ctx->CSGetConstantBuffers(0,1,&cb);
    D3D11_BUFFER_DESC bd{}; if(cb) cb->GetDesc(&bd);
    if(!one || !output || uv.ViewDimension!=D3D11_UAV_DIMENSION_TEXTURE2D || uv.Texture2D.MipSlice!=0 ||
       uv.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT || td.Width!=width || td.Height!=height ||
       td.ArraySize!=1 || td.SampleDesc.Count!=1 || bd.ByteWidth!=608) {
        Log("WEAPONSTEREO lighting rejected output/CB layout; native only"); return;
    }
    if(collectingModels && !draws) { Release(); state.store(Armed); return; }
    State expected=AwaitLighting; if(!state.compare_exchange_strong(expected,Lighting)) return;
    td.Format=uv.Format; td.MipLevels=1; td.Usage=D3D11_USAGE_DEFAULT;
    td.BindFlags=D3D11_BIND_UNORDERED_ACCESS|(liveMode?D3D11_BIND_SHADER_RESOURCE:0); td.CPUAccessFlags=td.MiscFlags=0;
    bool ok=true;
    if(!lit[0].gpu) {
        ok=SUCCEEDED(device->CreateTexture2D(&td,nullptr,&lit[0].gpu));
        if(liveMode) ok=ok && SUCCEEDED(device->CreateTexture2D(&td,nullptr,&lit[1].gpu));
        else lit[1].gpu=lit[0].gpu;
        for(auto& image:lit) {
            image.format=uv.Format; image.bytes=8;
            if(liveMode) ok=ok && SUCCEEDED(device->CreateShaderResourceView(image.gpu.Get(),nullptr,&image.srv));
            else {
                auto stagingDesc=td; stagingDesc.BindFlags=0; stagingDesc.Usage=D3D11_USAGE_STAGING; stagingDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                ok=ok && SUCCEEDED(device->CreateTexture2D(&stagingDesc,nullptr,&image.staging));
            }
        }
    }
    // t9 is screen-space world AO, sampled with s11. The weapon is absent
    // from world depth, so those pixels describe the background (e.g. grass).
    // Neutralize only this borrowed input; keep material AO and real lights.
    if(liveMode && ok && !neutralAo) {
        D3D11_TEXTURE2D_DESC ao{}; ao.Width=ao.Height=ao.MipLevels=ao.ArraySize=1;
        ao.Format=DXGI_FORMAT_R32G32_FLOAT; ao.SampleDesc.Count=1; ao.Usage=D3D11_USAGE_IMMUTABLE; ao.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const float white[2]={1,1}; D3D11_SUBRESOURCE_DATA initial{white,sizeof(white),0};
        ComPtr<ID3D11Texture2D> texture;
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc=D3D11_COMPARISON_ALWAYS; sampler.MaxLOD=D3D11_FLOAT32_MAX;
        ok=SUCCEEDED(device->CreateTexture2D(&ao,&initial,&texture)) &&
           SUCCEEDED(device->CreateShaderResourceView(texture.Get(),nullptr,&neutralAo)) &&
           SUCCEEDED(device->CreateSamplerState(&sampler,&neutralAoSampler));
        if(ok) Log("WEAPONLIGHT world screen AO excluded from weapon (t9/s11); native lighting retained");
    }
    if(ok) {
        // Lighting's CB may contain later light/fog updates. Transform THAT CB
        // with the same eye shifts, retaining its non-camera fields unchanged.
        TransformBuffers(ctx,cb.Get());
        if(!liveMode) {
            ctx->CopyResource(cbStaging[3].Get(),cb.Get());
            for(unsigned eye=0;eye<2;++eye) ctx->CopyResource(cbStaging[eye+4].Get(),eyeCb[eye].Get());
        }
        ComPtr<ID3D11ShaderResourceView> originalAo;
        ComPtr<ID3D11SamplerState> originalAoSampler;
        if(liveMode) {
            ctx->CSGetShaderResources(9,1,&originalAo); ctx->CSGetSamplers(11,1,&originalAoSampler);
            auto* ao=neutralAo.Get(); auto* sampler=neutralAoSampler.Get();
            ctx->CSSetShaderResources(9,1,&ao); ctx->CSSetSamplers(11,1,&sampler);
        }
        for(unsigned eye=0;eye<2;++eye) {
            ID3D11ShaderResourceView* inputs[7]{};
            for(unsigned i=0;i<7;++i) inputs[i]=targets[eye][i].srv.Get();
            if(!litUav[eye] && FAILED(device->CreateUnorderedAccessView(lit[eye].gpu.Get(),&uv,&litUav[eye]))) { ok=false; break; }
            auto* u=litUav[eye].Get(); auto* b=eyeCb[eye].Get();
            const float clear[4]{}; ctx->ClearUnorderedAccessViewFloat(u,clear);
            ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
            ctx->CSSetShaderResources(0,7,inputs); ctx->CSSetConstantBuffers(0,1,&b);
            ctx->Dispatch(x,y,z);
            if(!liveMode) ctx->CopyResource(lit[eye].staging.Get(),lit[eye].gpu.Get());
        }
        ID3D11UnorderedAccessView* none=nullptr; ctx->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        ctx->CSSetShaderResources(0,7,raw); ctx->CSSetUnorderedAccessViews(0,1,uavs,nullptr);
        auto* b=cb.Get(); ctx->CSSetConstantBuffers(0,1,&b);
        if(liveMode) {
            auto* ao=originalAo.Get(); auto* sampler=originalAoSampler.Get();
            ctx->CSSetShaderResources(9,1,&ao); ctx->CSSetSamplers(11,1,&sampler);
        }
        litCaptured=ok;
        if(!liveMode) Log("WEAPONSTEREO native lighting replayed for both eyes; SRV7/UAV0/CB0 restored");
    } else Log("WEAPONSTEREO lighting allocation failed; unlit fallback");
    for(auto& n:native) n.Reset(); // never retain native targets beyond this frame
    if(liveMode) {
        context.Reset();
        if(!litCaptured || rejected) { Log("WEAPONLIVE lighting/draw failed; native rendering resumes next frame"); Release(); state.store(Off); }
        else state.store(Ready,std::memory_order_release);
        return;
    }
    ctx->End(complete.Get()); context.Reset(); submitted=GetTickCount64(); state.store(Pending,std::memory_order_release);
}
void PollWeaponStereoCapture(ID3D11DeviceContext* ctx) noexcept {
    if(!WeaponStereoCapturePending() || !ctx) return;
    State expected=AwaitLighting;
    if(state.compare_exchange_strong(expected,Lighting)) {
        if(liveMode) { Log("WEAPONLIVE missing same-frame lighting; disabled, native resumes next frame"); Release(); state.store(Off); return; }
        Log("WEAPONSTEREO no matching lighting before Present; saving unlit pair only");
        if(ctx!=context.Get()) { Release(); state.store(Done); return; }
        for(auto& n:native) n.Reset();
        ctx->End(complete.Get()); context.Reset(); submitted=GetTickCount64(); state.store(Pending);
    }
    ComPtr<ID3D11Device> actual; ctx->GetDevice(&actual);
    if(actual.Get()!=device.Get() || GetTickCount64()-submitted>5000) {
        Log("WEAPONSTEREO readback cancelled (device/timeout)"); Release(); state.store(Done); return;
    }
    const auto hr=ctx->GetData(complete.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(hr==S_FALSE) return;
    if(FAILED(hr)) { Log("WEAPONSTEREO query failed"); Release(); state.store(Done); return; }
    ID3D11Resource* resources[12]={targets[0][0].staging.Get(),targets[0][2].staging.Get(),targets[1][0].staging.Get(),targets[1][2].staging.Get(),cbStaging[0].Get(),cbStaging[1].Get(),cbStaging[2].Get(),lit[0].staging.Get(),lit[1].staging.Get(),cbStaging[3].Get(),cbStaging[4].Get(),cbStaging[5].Get()};
    const unsigned resourceCount=litCaptured?12:7;
    D3D11_MAPPED_SUBRESOURCE maps[12]{}; unsigned mapped=0; HRESULT resultHr=S_OK;
    for(;mapped<resourceCount;++mapped) {
        resultHr=ctx->Map(resources[mapped],0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&maps[mapped]);
        if(FAILED(resultHr)) break;
    }
    if(mapped!=resourceCount) {
        for(unsigned i=0;i<mapped;++i) ctx->Unmap(resources[i],0);
        if(resultHr!=DXGI_ERROR_WAS_STILL_DRAWING) { Log("WEAPONSTEREO map failed"); Release(); state.store(Done); }
        return;
    }
    bool saved=true;
    try {
        std::filesystem::create_directories(directory);
        for(unsigned i=0;i<resourceCount;++i) {
            const wchar_t* names[]={L"left_albedo.raw",L"left_normal.raw",L"right_albedo.raw",L"right_normal.raw",L"native_cb0.bin",L"left_cb0.bin",L"right_cb0.bin",L"left_lit.raw",L"right_lit.raw",L"lighting_native_cb0.bin",L"lighting_left_cb0.bin",L"lighting_right_cb0.bin"};
            FILE* f=nullptr; const auto path=std::filesystem::path(directory)/names[i];
            if(_wfopen_s(&f,path.c_str(),L"wb") || !f) { saved=false; continue; }
            if(i<4 || i==7 || i==8) {
                const auto& t=i<4?targets[i/2][(i%2)*2]:lit[i-7];
                for(unsigned y=0;y<height;++y)
                    if(std::fwrite(static_cast<const char*>(maps[i].pData)+std::size_t(y)*maps[i].RowPitch,t.bytes,width,f)!=width) saved=false;
            } else if(std::fwrite(maps[i].pData,1,608,f)!=608) saved=false;
            if(std::fclose(f)) saved=false;
        }
        FILE* f=nullptr; const auto path=std::filesystem::path(directory)/L"stereo.json";
        if(!_wfopen_s(&f,path.c_str(),L"w") && f) {
            std::fprintf(f,"{\"width\":%u,\"height\":%u,\"albedoFormat\":%u,\"normalFormat\":%u,\"drawsPerEye\":%u,\"rejectedDraws\":%u,\"rejectMask\":%u,\"leftShiftM\":-0.032,\"rightShiftM\":0.032,\"origin\":\"native camera (not HMD centre)\",\"lighting\":%s,\"litFormat\":%u,\"worldDepthUsed\":false,\"saved\":%s}\n",width,height,targets[0][0].format,targets[0][2].format,draws,rejected,rejectMask,litCaptured?"true":"false",lit[0].format,saved?"true":"false");
            if(std::fclose(f)) saved=false;
        } else saved=false;
    } catch(...) { saved=false; }
    for(unsigned i=0;i<resourceCount;++i) ctx->Unmap(resources[i],0);
    char line[160]; std::snprintf(line,sizeof(line),"WEAPONSTEREO pair saved=%d drawsPerEye=%u rejected=%u; native rendering preserved",saved,draws,rejected); Log(line);
    if(wantLighting) Log(litCaptured && saved?"WEAPONSTEREO both lit eye images saved":"WEAPONSTEREO lit pair not saved; inspect capture status");
    Release(); state.store(Done,std::memory_order_release);
}
bool BeginWeaponDepthIsolation(ID3D11DeviceContext* ctx) noexcept {
    if(!liveMode || !suppressReady || state.load()!=Armed || isolatingDepth || !ctx || !device) return false;
    ID3D11RenderTargetView* rt[8]{}; ComPtr<ID3D11DepthStencilView> original;
    ctx->OMGetRenderTargets(8,rt,&original);
    bool noColour=true; for(auto* t:rt) if(t) { noColour=false; t->Release(); }
    if(!noColour || !original) return false;
    ComPtr<ID3D11Resource> resource; original->GetResource(&resource); ComPtr<ID3D11Texture2D> texture;
    if(FAILED(resource.As(&texture))) return false;
    D3D11_TEXTURE2D_DESC td{}; texture->GetDesc(&td); D3D11_DEPTH_STENCIL_VIEW_DESC vd{}; original->GetDesc(&vd);
    if(td.Width!=width || td.Height!=height || td.SampleDesc.Count!=1 || td.ArraySize!=1 || vd.ViewDimension!=D3D11_DSV_DIMENSION_TEXTURE2D) return false;
    D3D11_TEXTURE2D_DESC prior{}; if(isolatedDepth) isolatedDepth->GetDesc(&prior);
    if(!isolatedDepth || prior.Format!=td.Format || prior.Width!=td.Width || prior.Height!=td.Height) {
        isolatedDsv.Reset(); isolatedDepth.Reset(); td.MipLevels=1; td.Usage=D3D11_USAGE_DEFAULT;
        td.BindFlags=D3D11_BIND_DEPTH_STENCIL; td.CPUAccessFlags=td.MiscFlags=0; vd.Flags=0;
        if(FAILED(device->CreateTexture2D(&td,nullptr,&isolatedDepth)) ||
           FAILED(device->CreateDepthStencilView(isolatedDepth.Get(),&vd,&isolatedDsv))) return false;
    }
    const UINT clear=D3D11_CLEAR_DEPTH|((vd.Format==DXGI_FORMAT_D24_UNORM_S8_UINT || vd.Format==DXGI_FORMAT_D32_FLOAT_S8X24_UINT)?D3D11_CLEAR_STENCIL:0);
    ctx->ClearDepthStencilView(isolatedDsv.Get(),clear,1,0);
    worldDsv=original; depthContext=ctx; isolatingDepth=true;
    ctx->OMSetRenderTargets(0,nullptr,isolatedDsv.Get());
    return true;
}
void EndWeaponDepthIsolation() noexcept {
    if(!isolatingDepth || !depthContext) return;
    ComPtr<ID3D11DepthStencilView> actual; depthContext->OMGetRenderTargets(0,nullptr,&actual);
    const bool stayed=actual.Get()==isolatedDsv.Get();
    if(stayed) depthContext->OMSetRenderTargets(0,nullptr,worldDsv.Get());
    worldDsv.Reset(); depthContext.Reset(); isolatingDepth=false;
    depthHidden=stayed;
    if(!stayed) { Log("WEAPONLIVE depth target changed inside weapon; disabled"); Release(); state.store(Off); }
}
bool CompositeWeaponStereo(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,unsigned eye) noexcept {
    // depthHidden says the native weapon was kept out of the picture, so the
    // private one does not double it. A frame with only the hands in the layer
    // has no native weapon to double.
    if(!liveMode || eye>1 || state.load(std::memory_order_acquire)!=Ready || !litCaptured
       || !(depthHidden || !weaponDrawsThisFrame)) return false;
    D3D11_TEXTURE2D_DESC desc{}; if(target) target->GetDesc(&desc);
    if(loggedTargetFormat!=unsigned(desc.Format)) {
        loggedTargetFormat=unsigned(desc.Format); char line[192];
        std::snprintf(line,sizeof(line),"WEAPONLIVE XR target format=%u size=%ux%u sample=%u bind=0x%X weapon=%ux%u",
            unsigned(desc.Format),desc.Width,desc.Height,desc.SampleDesc.Count,desc.BindFlags,width,height); Log(line);
    }
    if(CompositeWeaponImage(device.Get(),ctx,lit[eye].srv.Get(),targets[eye][2].srv.Get(),target)) { composed|=1u<<eye; return true; }
    Log("WEAPONLIVE composite failed; native resumes next frame"); return false;
}
bool KeepWeaponStereo(WeaponPairOutcome outcome,unsigned& strikes) noexcept {
    if(outcome==WeaponPairOutcome::Live) { strikes=0; return true; }
    // A warm-up frame proves nothing either way, so it neither forgives an
    // earlier strike nor adds one. Otherwise a path that failed every live
    // frame would be forgiven by the native frame after each failure and never
    // give up, flickering between a missing eye and the old wobble forever.
    if(outcome==WeaponPairOutcome::Warmup) return true;
    return ++strikes<kWeaponPairStrikeLimit;
}
unsigned WeaponStereoSkippedPairs() noexcept { return skippedPairs; }
unsigned long long WeaponStereoReplays() noexcept { return replaysTotal.load(std::memory_order_relaxed); }
void FinishWeaponStereoFrame() noexcept {
    ++frameEpoch; handsDrawn=false;
    if(!liveMode) return;
    const auto s=state.load(std::memory_order_acquire);
    if(s==Ready) {
        const auto outcome=!depthHidden?WeaponPairOutcome::Warmup
            :(composed==3?WeaponPairOutcome::Live:WeaponPairOutcome::Incomplete);
        const bool keep=KeepWeaponStereo(outcome,pairStrikes);
        if(outcome!=WeaponPairOutcome::Incomplete) {
            ++liveFrames; suppressReady=true;
            if(GetTickCount64()-lastLiveLog>5000) {
                lastLiveLog=GetTickCount64(); char line[224];
                std::snprintf(line,sizeof(line),"WEAPONLIVE frames=%llu depthHidden=%d compositeMask=%u draws=%u models=%u shift=(%.5f,%.5f)m skippedPairs=%u hands=%u rejected=%u",liveFrames,depthHidden,composed,draws,modelBatches,shift[0],shift[1],skippedPairs,handDraws,handRejected); Log(line);
            }
            state.store(Armed,std::memory_order_release);
        } else if(keep) {
            // This frame lost an eye; the next warms up on the native weapon and
            // the one after is live again. Clearing suppressReady is what makes
            // the next frame draw the native weapon rather than hide it. The
            // private targets are kept, exactly as on a good frame: releasing
            // them here would reallocate some 800 MB on the frame after a hitch,
            // which is a hitch of its own. Nothing stale can be shown, because
            // the next capture clears litCaptured before anything is composited.
            ++skippedPairs;
            char line[160];
            std::snprintf(line,sizeof(line),"WEAPONLIVE incomplete eye pair mask=%u; frame skipped, live again after warm-up (strike %u of %u)",composed,pairStrikes,kWeaponPairStrikeLimit);
            Log(line);
            suppressReady=false;
            state.store(Armed,std::memory_order_release);
        } else {
            char line[160];
            std::snprintf(line,sizeof(line),"WEAPONLIVE %u incomplete eye pairs with no complete one between; disabled, native from now on",kWeaponPairStrikeLimit);
            Log(line);
            Release(); state.store(Off);
        }
    } else if(s==Armed) {
        suppressReady=false;
        if(device) Release(); // menu/VR-off frame: drop private allocations, no stale hand image
    }
    depthHidden=false; composed=0;
}
bool WeaponStereoPairReady() noexcept {
    return liveMode && state.load(std::memory_order_acquire)==Ready && depthHidden && litCaptured;
}
void DiscardWeaponStereoFrame() noexcept {
    if(!liveMode || recording || replaying || isolatingDepth) return;
    const auto current=state.load();
    if(current!=Ready && current!=AwaitLighting) return;
    // A rejected world frame must not permanently disable working private
    // weapon stereo merely because there was no projection to composite onto.
    Release();state.store(Armed,std::memory_order_release);composed=0;
}
bool HandStereoAvailable(ID3D11DeviceContext* ctx) noexcept {
    if(!liveMode || !ctx || replaying) return false;
    const auto s=state.load(std::memory_order_acquire);
    if(s!=Armed && s!=Recording && s!=AwaitLighting) return false;
    return first && GetTickCount64()-first>=settle;
}
bool HandStereoDrawnThisFrame() noexcept { return handsDrawn; }
bool ReplayHandStereo(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base) noexcept {
    if(!HandStereoAvailable(ctx)) return false;
    ID3D11RenderTargetView* rt[7]{}; ctx->OMGetRenderTargets(7,rt,nullptr);
    auto drop=[&]() { for(auto* t:rt) if(t) t->Release(); };
    if(state.load(std::memory_order_acquire)==Armed) {
        // Before the weapon this frame: adopt the pass's targets the way
        // BeginWeaponStereoCapture would, so its clear does not wipe the hands.
        ComPtr<ID3D11Device> actual; ctx->GetDevice(&actual);
        ComPtr<ID3D11Resource> r; ComPtr<ID3D11Texture2D> t; D3D11_TEXTURE2D_DESC td{};
        if(rt[0]) rt[0]->GetResource(&r);
        if(!rt[0] || !rt[2] || !r || FAILED(r.As(&t))) { drop(); return false; }
        t->GetDesc(&td);
        if((device && device.Get()!=actual.Get()) || (width && (width!=td.Width || height!=td.Height))
           || (context && context.Get()!=ctx)) { drop(); return false; }
        if(targets[0][0].gpu) for(unsigned i=0;i<7;++i) {
            D3D11_RENDER_TARGET_VIEW_DESC rv{}; if(rt[i]) rt[i]->GetDesc(&rv);
            if(!rt[i] || rv.Format!=targets[0][i].format) { drop(); return false; }
        }
        context=ctx; device=actual; width=td.Width; height=td.Height;
        for(unsigned i=0;i<7;++i) native[i]=rt[i];
        if(!targets[0][0].gpu && !Create()) { drop(); return false; }
        draws=rejected=rejectMask=0; litCaptured=false; composed=0; weaponDrawsThisFrame=0;
        ClearLiveFrame(ctx);
    } else if(ctx!=context.Get() || !targets[0][0].gpu) { drop(); return false; }
    drop();
    const bool ok=ReplayIndexed(ctx,count,start,base,handRejectMask,"HANDSTEREO");
    if(!ok) { ++handRejected; return false; }
    handsDrawn=true; ++handDraws; ++draws;
    // With something in the targets the frame goes on to lighting whether or
    // not the weapon is drawn at all -- it is culled when it points behind the
    // body. The weapon's own capture resumes these targets the way a second
    // model does (appendModels), so nothing here is cleared twice.
    State expected=Armed;
    if(state.compare_exchange_strong(expected,AwaitLighting)) collectingModels=true;
    return true;
}
bool InstallWeaponStereoHooks(const ImageProfile& image) noexcept {
    // 10FF4E0's two DrawIndexed sites, called by material loop 1103DA0.
    // Include the vtable load (3 bytes) to make a complete 6-byte patch span.
    constexpr unsigned sites[]={0x10FF556,0x10FF5C1};
    constexpr unsigned char expected[]={0x48,0x8B,0x01,0xFF,0x50,0x60};
    if(!image.base) return false;
    for(auto rva:sites) if(!Readable(image.base+rva,6) || std::memcmp(image.base+rva,expected,6)) return false;
    auto* thunk=AllocateNearThunk(image.base+sites[0],reinterpret_cast<void*>(&DrawWeaponStereo));
    if(!thunk) return false;
    unsigned char patch[2][6]{};
    for(unsigned i=0;i<2;++i) {
        const auto distance=reinterpret_cast<std::intptr_t>(thunk)-reinterpret_cast<std::intptr_t>(image.base+sites[i]+5);
        if(distance<std::numeric_limits<std::int32_t>::min() || distance>std::numeric_limits<std::int32_t>::max()) {
            VirtualFree(thunk,0,MEM_RELEASE); return false;
        }
        const auto rel=static_cast<std::int32_t>(distance);
        patch[i][0]=0xE8; std::memcpy(patch[i]+1,&rel,4); patch[i][5]=0x90;
    }
    auto* firstSite=image.base+sites[0]; const auto length=sites[1]-sites[0]+6; DWORD old=0;
    if(!VirtualProtect(firstSite,length,PAGE_EXECUTE_READWRITE,&old)) { VirtualFree(thunk,0,MEM_RELEASE); return false; }
    for(unsigned i=0;i<2;++i) { std::memcpy(image.base+sites[i],patch[i],6); RecordPatch(image.base+sites[i],6,"material loop DrawIndexed"); }
    FlushInstructionCache(GetCurrentProcess(),firstSite,length);
    DWORD ignored=0; VirtualProtect(firstSite,length,old,&ignored);
    return true;
}
}
