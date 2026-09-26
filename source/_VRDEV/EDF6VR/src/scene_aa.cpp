#include "scene_aa.h"
#include "ui_capture.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
std::atomic<bool> enabled{true};
thread_local bool drawing=false;
struct DrawScope { bool previous=drawing;DrawScope() {drawing=true;} ~DrawScope() {drawing=previous;} };
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vs;
ComPtr<ID3D11PixelShader> ps;
ComPtr<ID3D11Buffer> constants;
ComPtr<ID3D11SamplerState> sampler;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11RasterizerState> raster;
ComPtr<ID3D11Texture2D> scratch;
ComPtr<ID3D11ShaderResourceView> input;
UINT width=0,height=0;DXGI_FORMAT family=DXGI_FORMAT_UNKNOWN;
unsigned long long applied=0,bypassed=0,failed=0;
constexpr char shader[]=R"(
Texture2D<float4> Image:register(t0); SamplerState Smooth:register(s0);
cbuffer Params:register(b0) { float4 Pixel; };
struct Vertex { float4 position:SV_Position;float2 uv:TEXCOORD0; };
Vertex vertex(uint id:SV_VertexID) {
 Vertex o;o.uv=float2((id<<1)&2,id&2);o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
float Luma(float3 c) {return dot(c,float3(.299,.587,.114));}
float4 pixel(Vertex v):SV_Target {
 float2 uv=v.uv,px=Pixel.xy;
 float4 c=Image.SampleLevel(Smooth,uv,0);
 float nw=Luma(Image.SampleLevel(Smooth,uv+px*float2(-1,-1),0).rgb);
 float ne=Luma(Image.SampleLevel(Smooth,uv+px*float2(1,-1),0).rgb);
 float sw=Luma(Image.SampleLevel(Smooth,uv+px*float2(-1,1),0).rgb);
 float se=Luma(Image.SampleLevel(Smooth,uv+px*float2(1,1),0).rgb);
 float m=Luma(c.rgb),lo=min(m,min(min(nw,ne),min(sw,se))),hi=max(m,max(max(nw,ne),max(sw,se)));
 if(hi-lo>=max(.0312,hi*.125)) {
  float2 dir=float2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
  float reduce=max((nw+ne+sw+se)*(.25*.125),1.0/128);
  dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),-8,8)*px;
  float3 a=.5*(Image.SampleLevel(Smooth,uv+dir*(-1.0/6),0).rgb+Image.SampleLevel(Smooth,uv+dir*(1.0/6),0).rgb);
  float3 b=.5*a+.25*(Image.SampleLevel(Smooth,uv-dir*.5,0).rgb+Image.SampleLevel(Smooth,uv+dir*.5,0).rgb);
  float lb=Luma(b);c.rgb=(lb<lo || lb>hi)?a:b;
 }
 // The scratch holds encoded bytes. A typed sRGB RTV encodes on write, so
 // decode first for that target only. Typeless XR images use an UNORM RTV.
 if(Pixel.z>0) c.rgb=lerp(pow((c.rgb+.055)/1.055,2.4),c.rgb/12.92,step(c.rgb,.04045));
 return c;
})";
bool Build(ID3D11Device* d) noexcept {
 if(device.Get()!=d) {ReleaseSceneAA();device=d;}
 if(vs && ps && constants && sampler && blend && depth && raster) return true;
 static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");if(!compiler) return false;
 auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
 if(!compile) return false;
 ComPtr<ID3DBlob> v,p;
 if(FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr)) ||
    FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",0,0,&p,nullptr))) return false;
 if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
    FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps))) return false;
 D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
 D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
 D3D11_DEPTH_STENCIL_DESC z{};z.DepthFunc=D3D11_COMPARISON_ALWAYS;
 D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
 return SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants)) && SUCCEEDED(d->CreateSamplerState(&s,&sampler))
  && SUCCEEDED(d->CreateBlendState(&b,&blend)) && SUCCEEDED(d->CreateDepthStencilState(&z,&depth)) && SUCCEEDED(d->CreateRasterizerState(&r,&raster));
}
}
void EnableSceneAA(bool value) noexcept {enabled.store(value);}
bool SceneAAEnabled() noexcept {return enabled.load();}
bool ToggleSceneAA() noexcept {bool previous=enabled.load();while(!enabled.compare_exchange_weak(previous,!previous)) {} return !previous;}
bool InsideSceneAA() noexcept {return drawing;}
void ReleaseSceneAA() noexcept {
 scratch.Reset();input.Reset();width=height=0;family=DXGI_FORMAT_UNKNOWN;
 vs.Reset();ps.Reset();constants.Reset();sampler.Reset();blend.Reset();depth.Reset();raster.Reset();device.Reset();
}
static bool FilterSceneAA(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,bool active) noexcept {
 if(!active) return true; // No copies, allocations, state changes or GPU work when OFF.
 if(!ctx || !target) return false;
 D3D11_TEXTURE2D_DESC td{};target->GetDesc(&td);
 if(!td.Width || !td.Height || td.SampleDesc.Count!=1 || td.ArraySize!=1 || td.MipLevels!=1) return false;
 DXGI_FORMAT format=td.Format;
 if(format==DXGI_FORMAT_R8G8B8A8_TYPELESS) format=DXGI_FORMAT_R8G8B8A8_UNORM;
 if(format==DXGI_FORMAT_B8G8R8A8_TYPELESS) format=DXGI_FORMAT_B8G8R8A8_UNORM;
 const bool srgb=format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
 const bool rgba=format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
 if(!rgba && format!=DXGI_FORMAT_B8G8R8A8_UNORM && format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) return false;
 const auto storage=rgba?DXGI_FORMAT_R8G8B8A8_TYPELESS:DXGI_FORMAT_B8G8R8A8_TYPELESS;
 ComPtr<ID3D11Device> d;ctx->GetDevice(&d);if(!Build(d.Get())) return false;
 if(!scratch || width!=td.Width || height!=td.Height || family!=storage) {
  scratch.Reset();input.Reset();auto desc=td;desc.Format=storage;desc.Usage=D3D11_USAGE_DEFAULT;
  desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=desc.MiscFlags=0;
  if(FAILED(d->CreateTexture2D(&desc,nullptr,&scratch))) return false;
  D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=rgba?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_B8G8R8A8_UNORM;
  sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
  if(FAILED(d->CreateShaderResourceView(scratch.Get(),&sv,&input))) {scratch.Reset();return false;}
  width=td.Width;height=td.Height;family=storage;
 }
 D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=format;rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
 ComPtr<ID3D11RenderTargetView> targetView;if(FAILED(d->CreateRenderTargetView(target,&rv,&targetView))) return false;
 ctx->CopyResource(scratch.Get(),target);
 const float data[4]={1.f/td.Width,1.f/td.Height,srgb?1.f:0.f,0};ctx->UpdateSubresource(constants.Get(),0,nullptr,data,0,0);
 PauseUiCapture pause;DrawScope scope;
    ID3D11RenderTargetView* rt[8]{};ComPtr<ID3D11DepthStencilView> oldDsv;ctx->OMGetRenderTargets(8,rt,&oldDsv);
    ComPtr<ID3D11VertexShader> oldVs;ComPtr<ID3D11PixelShader> oldPs;ComPtr<ID3D11GeometryShader> oldGs;
    ComPtr<ID3D11HullShader> oldHs;ComPtr<ID3D11DomainShader> oldDs;
    ID3D11ClassInstance* vi[256]{},*pi[256]{},*gi[256]{},*hi[256]{},*di[256]{};UINT vn=256,pn=256,gn=256,hn=256,dn=256;
    ctx->VSGetShader(&oldVs,vi,&vn);ctx->PSGetShader(&oldPs,pi,&pn);ctx->GSGetShader(&oldGs,gi,&gn);
    ctx->HSGetShader(&oldHs,hi,&hn);ctx->DSGetShader(&oldDs,di,&dn);
    ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology{};
    ctx->IAGetInputLayout(&layout);ctx->IAGetPrimitiveTopology(&topology);
    D3D11_VIEWPORT vp[16]{};UINT count=16;ctx->RSGetViewports(&count,vp);
    ComPtr<ID3D11RasterizerState> oldRaster;ctx->RSGetState(&oldRaster);
    ComPtr<ID3D11DepthStencilState> oldDepth;UINT ref=0;ctx->OMGetDepthStencilState(&oldDepth,&ref);
    ComPtr<ID3D11BlendState> oldBlend;float factor[4]{};UINT mask=0;ctx->OMGetBlendState(&oldBlend,factor,&mask);
    ID3D11ShaderResourceView* inputs[3]{};ctx->PSGetShaderResources(0,3,inputs);
    ComPtr<ID3D11SamplerState> oldSampler;ctx->PSGetSamplers(0,1,&oldSampler);
    ComPtr<ID3D11Buffer> oldConstants;ctx->PSGetConstantBuffers(0,1,&oldConstants);
    auto* rtv=targetView.Get();ctx->OMSetRenderTargets(1,&rtv,nullptr);
    D3D11_VIEWPORT full{0,0,float(td.Width),float(td.Height),0,1};ctx->RSSetViewports(1,&full);
    ctx->RSSetState(raster.Get());ctx->OMSetDepthStencilState(depth.Get(),0);ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);
    ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);
    ID3D11ShaderResourceView* drawInputs[]={input.Get()};ctx->PSSetShaderResources(0,1,drawInputs);
    auto* cb=constants.Get();ctx->PSSetConstantBuffers(0,1,&cb);auto* s=sampler.Get();ctx->PSSetSamplers(0,1,&s);
    ctx->Draw(3,0);
    ID3D11ShaderResourceView* none[3]{};ctx->PSSetShaderResources(0,3,none);
    ctx->OMSetRenderTargets(8,rt,oldDsv.Get());ctx->PSSetShaderResources(0,3,inputs);
    auto* oldCb=oldConstants.Get();ctx->PSSetConstantBuffers(0,1,&oldCb);auto* oldS=oldSampler.Get();ctx->PSSetSamplers(0,1,&oldS);
    ctx->RSSetViewports(count,vp);ctx->RSSetState(oldRaster.Get());ctx->OMSetDepthStencilState(oldDepth.Get(),ref);ctx->OMSetBlendState(oldBlend.Get(),factor,mask);
    ctx->IASetInputLayout(layout.Get());ctx->IASetPrimitiveTopology(topology);
    ctx->VSSetShader(oldVs.Get(),vi,vn);ctx->PSSetShader(oldPs.Get(),pi,pn);ctx->GSSetShader(oldGs.Get(),gi,gn);
    ctx->HSSetShader(oldHs.Get(),hi,hn);ctx->DSSetShader(oldDs.Get(),di,dn);
    for(auto* t:rt) if(t) t->Release();for(auto* i:inputs) if(i) i->Release();
    for(UINT i=0;i<vn;++i) if(vi[i]) vi[i]->Release();for(UINT i=0;i<pn;++i) if(pi[i]) pi[i]->Release();for(UINT i=0;i<gn;++i) if(gi[i]) gi[i]->Release();
    for(UINT i=0;i<hn;++i) if(hi[i]) hi[i]->Release();for(UINT i=0;i<dn;++i) if(di[i]) di[i]->Release();
    return true;
}
bool ApplySceneAA(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,bool active) noexcept {
    const bool ok=FilterSceneAA(ctx,target,active);
    if(!active) ++bypassed; else if(ok) ++applied; else ++failed;
    return ok;
}
const char* SceneAAStatus() noexcept {
    static char status[192];
    std::snprintf(status,sizeof(status),"SCENEAA configured=%d appliedEyes=%llu bypassedEyes=%llu failures=%llu size=%ux%u control=INI",
        SceneAAEnabled()?1:0,applied,bypassed,failed,width,height);
    return status;
}
}
