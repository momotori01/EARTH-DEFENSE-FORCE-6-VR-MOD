#include "scope_draw.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vs;
ComPtr<ID3D11PixelShader> ps;
ComPtr<ID3D11Buffer> constants;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11RasterizerState> raster;
ComPtr<ID3D11SamplerState> sampler;
std::atomic<unsigned long long> drawn{0},refused{0},failed{0};
std::atomic<unsigned> lastRefusal{0};
std::atomic<float> fieldTan{0},wantTan{0};
struct Constants {
    float corner[4][4]{};   // clip positions of the surface's corners, uv (-1,-1) (-1,1) (1,-1) (1,1)
    float mode[4]{};        // mode, source sRGB, target sRGB, style
    float frame0[4]{};      // tanHalf, aspect, shape
    float frame1[4]{};      // mode 1: tan left, right, top, bottom
    float reticle[4]{};     // the reticle's turn in the surface: cos, sin (the weapon's roll)
    float origin[4]{},right[4]{},up[4]{},forward[4]{};
    float cameraRight[4]{},cameraUp[4]{},cameraForward[4]{};
    float sourceClip[4][4]{};
};
constexpr char shader[]=R"(
cbuffer Data:register(b0) {
 float4 corner[4];float4 mode;float4 frame0;float4 frame1;float4 reticle;
 float4 origin;float4 rightW;float4 upW;float4 forwardW;
 float4 cameraRight;float4 cameraUp;float4 cameraForward;row_major float4x4 sourceClip;
}
Texture2D source:register(t0);SamplerState linearClamp:register(s0);
struct V {float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vertex(uint id:SV_VertexID) {
 const uint index[6]={0,1,2,2,1,3};
 const float2 uvs[4]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,1)};
 V o;uint i=index[id];o.uv=uvs[i];o.position=corner[i];return o;
}
float3 Decode(float3 c){return pow(max(c,0),2.2);}
float3 Encode(float3 c){return pow(max(c,0),1/2.2);}
float4 fragment(V i):SV_Target {
 const float aspect=frame0.y;
 const bool round=frame0.z<.5;
 // How far out: 1 at the edge, round or square.
 float edge=round?length(i.uv):max(abs(i.uv.x),abs(i.uv.y));
 float aa=max(fwidth(edge),1e-4);
 float alpha=1-smoothstep(1-1.5*aa,1,edge);
 if(alpha<=0) discard;
 // The ray this point of the surface shows, in the world.
 float3 d=forwardW.xyz+(i.uv.x*aspect*rightW.xyz+i.uv.y*upW.xyz)*frame0.x;
 float3 c;
 if(mode.x<1.5) {
  // The scope's own view: the ray in its camera, inside its frustum.
  float z=max(dot(d,cameraForward.xyz),1e-5);
  float tx=dot(d,cameraRight.xyz)/z,ty=dot(d,cameraUp.xyz)/z;
  float2 t=float2((tx-frame1.x)/(frame1.y-frame1.x),(frame1.z-ty)/(frame1.z-frame1.w));
  c=source.SampleLevel(linearClamp,t,0).rgb;
 } else {
  // Digital zoom: the ray looked up in this eye's own picture.
  float4 q=mul(float4(origin.xyz+d*1000,1),sourceClip);
  float2 n=q.xy/max(q.w,1e-4);
  c=source.SampleLevel(linearClamp,float2(.5+.5*n.x,.5-.5*n.y),0).rgb;
 }
 if(mode.y>.5) c=Encode(c);                  // work in display-encoded values
 // Duplex reticle: hairlines through the centre, posts outside half the field,
 // turned with the weapon (the picture stays upright, as through a real scope).
 float2 r=float2(dot(i.uv,float2(reticle.x,-reticle.y)),dot(i.uv,reticle.yx));
 float2 a=abs(float2(r.x*aspect,r.y));float px=max(fwidth(a.y),1e-4);
 float reach=round?1:max(aspect,1);
 float hair=max((1-smoothstep(.004,.004+px,a.y))*step(a.x,reach),(1-smoothstep(.004,.004+px,a.x))*step(a.y,1));
 float post=max((1-smoothstep(.018,.018+px,a.y))*step(.5,a.x),(1-smoothstep(.018,.018+px,a.x))*step(.5,a.y));
 float centre=length(float2(i.uv.x*aspect,i.uv.y));
 if(mode.w<.5) {
  c*=1-.92*saturate(max(hair,post));
  c=lerp(c,float3(1,.08,.05),1-smoothstep(.010,.010+px,centre));
  c*=1-.6*smoothstep(.9,1,edge);             // the scope's dark rim, outer tenth
 } else {
  // Hologram: a light reticle, scan lines, a glowing edge, a little see-through.
  float3 glow=mode.w<1.5?float3(.55,.85,1):float3(.6,1,.72);
  c=c*.92+glow*.04;
  c*=.95+.05*sin(i.uv.y*260);
  c=lerp(c,glow,.75*saturate(max(hair,post)));
  c=lerp(c,float3(1,.08,.05),1-smoothstep(.010,.010+px,centre));
  float rim=smoothstep(.88,.965,edge);
  c=lerp(c,glow,rim*.85);
  alpha*=lerp(.88,1,rim);
 }
 if(mode.z>.5) c=Decode(c);                   // sRGB target: the hardware encodes
 return float4(c*alpha,alpha);
})";
bool Build(ID3D11Device* d) {
    if(device.Get()!=d) {vs.Reset();ps.Reset();constants.Reset();blend.Reset();depth.Reset();raster.Reset();sampler.Reset();device=d;}
    if(vs && ps && constants && blend && depth && raster && sampler) return true;
    static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");if(!compiler) return false;
    auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    ComPtr<ID3DBlob> v,p;
    if(!compile || FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"fragment","ps_5_0",0,0,&p,nullptr))) return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
       FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps))) return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_BLEND_DESC b{};auto& rt=b.RenderTarget[0];rt.BlendEnable=TRUE;
    rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=FALSE;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=FALSE;
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD=D3D11_FLOAT32_MAX;
    return SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants)) && SUCCEEDED(d->CreateBlendState(&b,&blend)) &&
        SUCCEEDED(d->CreateDepthStencilState(&z,&depth)) && SUCCEEDED(d->CreateRasterizerState(&r,&raster)) &&
        SUCCEEDED(d->CreateSamplerState(&s,&sampler));
}
bool Refuse(unsigned why) noexcept {refused.fetch_add(1,std::memory_order_relaxed);lastRefusal.store(why,std::memory_order_relaxed);return false;}
bool Srgb(DXGI_FORMAT f) noexcept {return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;}
DXGI_FORMAT Typed(DXGI_FORMAT f) noexcept {
    if(f==DXGI_FORMAT_R8G8B8A8_TYPELESS) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(f==DXGI_FORMAT_B8G8R8A8_TYPELESS) return DXGI_FORMAT_B8G8R8A8_UNORM;
    return f;
}
void Copy3(float out[4],const float in[3]) noexcept {out[0]=in[0];out[1]=in[1];out[2]=in[2];out[3]=0;}
}
ScopeDrawStats ReadScopeDrawStats() noexcept {
    return {drawn.load(std::memory_order_relaxed),refused.load(std::memory_order_relaxed),failed.load(std::memory_order_relaxed),
            lastRefusal.load(std::memory_order_relaxed),fieldTan.load(std::memory_order_relaxed),wantTan.load(std::memory_order_relaxed)};
}
void NoteScopeSkip(unsigned why,float field,float want) noexcept {
    refused.fetch_add(1,std::memory_order_relaxed);lastRefusal.store(why,std::memory_order_relaxed);
    if(field>0) {fieldTan.store(field,std::memory_order_relaxed);wantTan.store(want,std::memory_order_relaxed);}
}
bool DrawScopeLens(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const ScopeDrawInput& in) noexcept {
    if(!ctx || !target || !in.source || !in.lens.valid || (in.mode!=1 && in.mode!=2)) return Refuse(1);
    if(!(in.tanHalf>0) || !std::isfinite(in.tanHalf) || !(in.aspect>0) || !std::isfinite(in.aspect)) return Refuse(2);
    if(in.mode==1 && !(in.tanRight>in.tanLeft && in.tanTop>in.tanBottom)) return Refuse(3);
    // The surface's corners, through this eye's camera. A rectangle keeps its
    // own edges (a screen, a box sight); a round lens is laid out by the
    // picture's up, so the picture stands as its camera saw it.
    const auto& L=in.lens;
    float up[3]={L.shape?L.up[0]:in.up[0],L.shape?L.up[1]:in.up[1],L.shape?L.up[2]:in.up[2]};
    const float d=up[0]*L.normal[0]+up[1]*L.normal[1]+up[2]*L.normal[2];
    for(int j=0;j<3;++j) up[j]-=L.normal[j]*d;
    const float lu=std::sqrt(up[0]*up[0]+up[1]*up[1]+up[2]*up[2]);
    if(!(lu>1e-4f)) return Refuse(4);
    for(float& c:up) c/=lu;
    // right = up x normal: with the normal facing back (-forward) that is forward x up.
    const float right[3]={up[1]*L.normal[2]-up[2]*L.normal[1],up[2]*L.normal[0]-up[0]*L.normal[2],up[0]*L.normal[1]-up[1]*L.normal[0]};
    const float halfWidth=L.shape?L.halfWidth:L.radius;
    Constants data{};
    const float uv[4][2]={{-1,-1},{-1,1},{1,-1},{1,1}};
    for(int i=0;i<4;++i) {
        float p[3]{};
        for(int j=0;j<3;++j) p[j]=L.centre[j]+right[j]*uv[i][0]*halfWidth+up[j]*uv[i][1]*L.radius;
        if(!ScopeClip(in.view,in.projection,p,data.corner[i]) || !(data.corner[i][3]>.01f)) return Refuse(5);
    }
    D3D11_TEXTURE2D_DESC td{};target->GetDesc(&td);
    D3D11_TEXTURE2D_DESC sd{};in.source->GetDesc(&sd);
    if(!td.Width || !td.Height || td.SampleDesc.Count!=1 || sd.SampleDesc.Count!=1) return Refuse(6);
    const DXGI_FORMAT targetFormat=Typed(td.Format),sourceFormat=Typed(sd.Format);
    data.mode[0]=float(in.mode);data.mode[1]=Srgb(sourceFormat)?1.f:0.f;data.mode[2]=Srgb(targetFormat)?1.f:0.f;data.mode[3]=float(in.style);
    data.frame0[0]=in.tanHalf;data.frame0[1]=in.aspect;data.frame0[2]=float(L.shape);
    data.frame1[0]=in.tanLeft;data.frame1[1]=in.tanRight;data.frame1[2]=in.tanTop;data.frame1[3]=in.tanBottom;
    // A round surface is laid out by the picture's up, which follows the head; its
    // reticle is turned onto the weapon's own up (lens.up) in that plane. A
    // rectangle already stands on the weapon's (or the panel's) own up.
    data.reticle[0]=1;data.reticle[1]=0;
    if(!L.shape) {
        float w[3]={L.up[0],L.up[1],L.up[2]};
        const float n=w[0]*L.normal[0]+w[1]*L.normal[1]+w[2]*L.normal[2];
        for(int j=0;j<3;++j) w[j]-=L.normal[j]*n;
        const float lw=std::sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);
        if(lw>1e-4f) {
            const float c=(w[0]*up[0]+w[1]*up[1]+w[2]*up[2])/lw,s=(w[0]*right[0]+w[1]*right[1]+w[2]*right[2])/lw;
            if(std::isfinite(c) && std::isfinite(s)) {data.reticle[0]=c;data.reticle[1]=s;}
        }
    }
    Copy3(data.origin,in.origin);Copy3(data.right,in.right);Copy3(data.up,in.up);Copy3(data.forward,in.forward);
    Copy3(data.cameraRight,in.cameraRight);Copy3(data.cameraUp,in.cameraUp);Copy3(data.cameraForward,in.cameraForward);
    std::memcpy(data.sourceClip,in.sourceClip.m,sizeof(data.sourceClip));
    ComPtr<ID3D11Device> dev;ctx->GetDevice(&dev);
    if(!Build(dev.Get())) {failed.fetch_add(1,std::memory_order_relaxed);return false;}
    D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=targetFormat;rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=sourceFormat;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
    ComPtr<ID3D11RenderTargetView> output;ComPtr<ID3D11ShaderResourceView> picture;
    if(FAILED(dev->CreateRenderTargetView(target,&rv,&output)) || FAILED(dev->CreateShaderResourceView(in.source,&sv,&picture))) {
        failed.fetch_add(1,std::memory_order_relaxed);return false;
    }
    // State saved and put back, as CompositeWeaponImage does.
    ID3D11RenderTargetView* rt[8]{};ComPtr<ID3D11DepthStencilView> oldDsv;ctx->OMGetRenderTargets(8,rt,&oldDsv);
    ComPtr<ID3D11VertexShader> oldVs;ComPtr<ID3D11PixelShader> oldPs;ComPtr<ID3D11GeometryShader> oldGs;
    ComPtr<ID3D11HullShader> oldHs;ComPtr<ID3D11DomainShader> oldDs;
    ID3D11ClassInstance *vi[256]{},*pi[256]{},*gi[256]{},*hi[256]{},*di[256]{};UINT vn=256,pn=256,gn=256,hn=256,dn=256;
    ctx->VSGetShader(&oldVs,vi,&vn);ctx->PSGetShader(&oldPs,pi,&pn);ctx->GSGetShader(&oldGs,gi,&gn);
    ctx->HSGetShader(&oldHs,hi,&hn);ctx->DSGetShader(&oldDs,di,&dn);
    ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology{};ctx->IAGetInputLayout(&layout);ctx->IAGetPrimitiveTopology(&topology);
    D3D11_VIEWPORT vp[16]{};UINT vpCount=16;ctx->RSGetViewports(&vpCount,vp);
    ComPtr<ID3D11RasterizerState> oldRaster;ctx->RSGetState(&oldRaster);
    ComPtr<ID3D11DepthStencilState> oldDepth;UINT ref=0;ctx->OMGetDepthStencilState(&oldDepth,&ref);
    ComPtr<ID3D11BlendState> oldBlend;float factor[4]{};UINT mask=0;ctx->OMGetBlendState(&oldBlend,factor,&mask);
    ComPtr<ID3D11Buffer> oldVsCb,oldPsCb;ctx->VSGetConstantBuffers(0,1,&oldVsCb);ctx->PSGetConstantBuffers(0,1,&oldPsCb);
    ID3D11ShaderResourceView* oldInput=nullptr;ctx->PSGetShaderResources(0,1,&oldInput);
    ID3D11SamplerState* oldSampler=nullptr;ctx->PSGetSamplers(0,1,&oldSampler);
    ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
    auto* targetView=output.Get();ctx->OMSetRenderTargets(1,&targetView,nullptr);
    D3D11_VIEWPORT full{0,0,float(td.Width),float(td.Height),0,1};ctx->RSSetViewports(1,&full);
    ctx->RSSetState(raster.Get());ctx->OMSetDepthStencilState(depth.Get(),0);ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);
    ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);
    auto* cb=constants.Get();ctx->VSSetConstantBuffers(0,1,&cb);ctx->PSSetConstantBuffers(0,1,&cb);
    auto* srv=picture.Get();ctx->PSSetShaderResources(0,1,&srv);
    auto* smp=sampler.Get();ctx->PSSetSamplers(0,1,&smp);
    ctx->Draw(6,0);
    ID3D11ShaderResourceView* none=nullptr;ctx->PSSetShaderResources(0,1,&none);
    ctx->OMSetRenderTargets(8,rt,oldDsv.Get());ctx->PSSetShaderResources(0,1,&oldInput);ctx->PSSetSamplers(0,1,&oldSampler);
    auto* vcb=oldVsCb.Get();auto* pcb=oldPsCb.Get();ctx->VSSetConstantBuffers(0,1,&vcb);ctx->PSSetConstantBuffers(0,1,&pcb);
    ctx->RSSetViewports(vpCount,vp);ctx->RSSetState(oldRaster.Get());ctx->OMSetDepthStencilState(oldDepth.Get(),ref);ctx->OMSetBlendState(oldBlend.Get(),factor,mask);
    ctx->IASetInputLayout(layout.Get());ctx->IASetPrimitiveTopology(topology);
    ctx->VSSetShader(oldVs.Get(),vi,vn);ctx->PSSetShader(oldPs.Get(),pi,pn);ctx->GSSetShader(oldGs.Get(),gi,gn);
    ctx->HSSetShader(oldHs.Get(),hi,hn);ctx->DSSetShader(oldDs.Get(),di,dn);
    for(auto* t:rt) if(t) t->Release();
    if(oldInput) oldInput->Release();
    if(oldSampler) oldSampler->Release();
    for(UINT i=0;i<vn;++i) if(vi[i]) vi[i]->Release();
    for(UINT i=0;i<pn;++i) if(pi[i]) pi[i]->Release();
    for(UINT i=0;i<gn;++i) if(gi[i]) gi[i]->Release();
    for(UINT i=0;i<hn;++i) if(hi[i]) hi[i]->Release();
    for(UINT i=0;i<dn;++i) if(di[i]) di[i]->Release();
    drawn.fetch_add(1,std::memory_order_relaxed);
    return true;
}
}
