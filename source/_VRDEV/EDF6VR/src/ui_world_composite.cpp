#include "ui_world_composite.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vs;
ComPtr<ID3D11PixelShader> psPass,psEncode,psDecode;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11RasterizerState> raster;
ComPtr<ID3D11SamplerState> sampler;
ComPtr<ID3D11Buffer> constants;
ComPtr<ID3D11Texture2D> viewedTexture;
ComPtr<ID3D11ShaderResourceView> view;
constexpr char shader[]=R"(
Texture2D<float4> Overlay:register(t0); SamplerState Smooth:register(s0);
cbuffer Placement:register(b0) { float2 Scale; float OffsetY; float Tint; };
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V vertex(uint id:SV_VertexID) {
 V o; float2 uv=float2((id<<1)&2,id&2); o.uv=uv; o.p=float4(uv.x*2-1,1-uv.y*2,0,1); return o;
}
float4 shadePass(V i):SV_Target {
 float2 uv=.5+(i.uv-.5)/Scale; uv.y-=OffsetY;
 clip(min(min(uv.x,uv.y),min(1-uv.x,1-uv.y)));
 float4 c=Overlay.SampleLevel(Smooth,uv,0); clip(c.a-.002);
 if(Tint>.5) c.rgb=float3(c.a,0,c.a); // premultiplied magenta: the world copy, unmistakably
 return c;
}
float4 shadeEncode(V i):SV_Target {
 float4 c=shadePass(i); c.rgb=lerp(1.055*pow(max(c.rgb,0),1.0/2.4)-.055,12.92*c.rgb,step(c.rgb,.0031308)); return c;
}
float4 shadeDecode(V i):SV_Target {
 float4 c=shadePass(i); c.rgb=lerp(pow((c.rgb+.055)/1.055,2.4),c.rgb/12.92,step(c.rgb,.04045)); return c;
})";
bool Build(ID3D11Device* d) noexcept {
    if(device.Get()==d && vs && psPass && psEncode && psDecode && blend && depth && raster && sampler && constants) return true;
    ReleaseOverlayComposite(); device=d;
    static auto compiler=LoadLibraryW(L"d3dcompiler_47.dll"); if(!compiler) return false;
    auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    if(!compile) return false;
    ComPtr<ID3DBlob> v,p,e,dd;
    if(FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadePass","ps_5_0",0,0,&p,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadeEncode","ps_5_0",0,0,&e,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadeDecode","ps_5_0",0,0,&dd,nullptr))) return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
       FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&psPass)) ||
       FAILED(d->CreatePixelShader(e->GetBufferPointer(),e->GetBufferSize(),nullptr,&psEncode)) ||
       FAILED(d->CreatePixelShader(dd->GetBufferPointer(),dd->GetBufferSize(),nullptr,&psDecode))) return false;
    // The capture accumulates premultiplied colour (see ui_capture AlphaBlend).
    D3D11_BLEND_DESC b{}; auto& t=b.RenderTarget[0]; t.BlendEnable=TRUE;
    t.SrcBlend=D3D11_BLEND_ONE; t.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp=D3D11_BLEND_OP_ADD;
    t.SrcBlendAlpha=D3D11_BLEND_ZERO; t.DestBlendAlpha=D3D11_BLEND_ONE; t.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_DEPTH_STENCIL_DESC z{}; z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC r{}; r.FillMode=D3D11_FILL_SOLID; r.CullMode=D3D11_CULL_NONE; r.DepthClipEnable=TRUE;
    D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD=D3D11_FLOAT32_MAX;
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=16; cb.Usage=D3D11_USAGE_DYNAMIC; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(d->CreateBlendState(&b,&blend)) && SUCCEEDED(d->CreateDepthStencilState(&z,&depth))
        && SUCCEEDED(d->CreateRasterizerState(&r,&raster)) && SUCCEEDED(d->CreateSamplerState(&s,&sampler))
        && SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants));
}
bool Srgb(DXGI_FORMAT f) noexcept {
    return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}
}
void ReleaseOverlayComposite() noexcept {
    vs.Reset(); psPass.Reset(); psEncode.Reset(); psDecode.Reset(); blend.Reset(); depth.Reset();
    raster.Reset(); sampler.Reset(); constants.Reset(); view.Reset(); viewedTexture.Reset(); device.Reset();
}
bool CompositeOverlayImage(ID3D11Device* d,ID3D11DeviceContext* ctx,ID3D11Texture2D* overlay,ID3D11Texture2D* target,
                           float scaleX,float scaleY,float offsetY,bool debugTint) noexcept {
    if(!d || !ctx || !overlay || !target) return false;
    if(!(scaleX>=0.05f && scaleX<=20.0f) || !(scaleY>=0.05f && scaleY<=20.0f) || !(offsetY>-1.0f && offsetY<1.0f)) return false;
    D3D11_TEXTURE2D_DESC td{}; target->GetDesc(&td);
    D3D11_TEXTURE2D_DESC od{}; overlay->GetDesc(&od);
    if(!td.SampleDesc.Count || od.SampleDesc.Count!=1) return false;
    // SteamVR hands back typeless images; eye_warp writes them through UNORM.
    DXGI_FORMAT targetFormat=td.Format;
    if(targetFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS) targetFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(targetFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS) targetFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
    if(!Srgb(targetFormat) && targetFormat!=DXGI_FORMAT_R8G8B8A8_UNORM && targetFormat!=DXGI_FORMAT_B8G8R8A8_UNORM) return false;
    if(!Build(d)) return false;
    if(viewedTexture.Get()!=overlay) {
        view.Reset(); viewedTexture=overlay;
        if(FAILED(d->CreateShaderResourceView(overlay,nullptr,&view))) { viewedTexture.Reset(); return false; }
    }
    const bool srcSrgb=Srgb(od.Format), dstSrgb=Srgb(targetFormat);
    auto* pixel=srcSrgb==dstSrgb?psPass.Get():(srcSrgb?psEncode.Get():psDecode.Get());
    D3D11_RENDER_TARGET_VIEW_DESC viewDesc{}; viewDesc.Format=targetFormat;
    viewDesc.ViewDimension=td.SampleDesc.Count>1?D3D11_RTV_DIMENSION_TEXTURE2DMS:D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> targetView; if(FAILED(d->CreateRenderTargetView(target,&viewDesc,&targetView))) return false;
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(ctx->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
        const float values[4]={scaleX,scaleY,offsetY,debugTint?1.0f:0.0f}; std::memcpy(mapped.pData,values,sizeof(values));
        ctx->Unmap(constants.Get(),0);
    }
    ID3D11RenderTargetView* rt[8]{}; ComPtr<ID3D11DepthStencilView> oldDsv; ctx->OMGetRenderTargets(8,rt,&oldDsv);
    ComPtr<ID3D11VertexShader> oldVs; ComPtr<ID3D11PixelShader> oldPs; ComPtr<ID3D11GeometryShader> oldGs;
    ComPtr<ID3D11HullShader> oldHs; ComPtr<ID3D11DomainShader> oldDs;
    ID3D11ClassInstance* vi[256]{},*pi[256]{},*gi[256]{},*hi[256]{},*di[256]{}; UINT vn=256,pn=256,gn=256,hn=256,dn=256;
    ctx->VSGetShader(&oldVs,vi,&vn); ctx->PSGetShader(&oldPs,pi,&pn); ctx->GSGetShader(&oldGs,gi,&gn);
    ctx->HSGetShader(&oldHs,hi,&hn); ctx->DSGetShader(&oldDs,di,&dn);
    ComPtr<ID3D11InputLayout> layout; D3D11_PRIMITIVE_TOPOLOGY topology{};
    ctx->IAGetInputLayout(&layout); ctx->IAGetPrimitiveTopology(&topology);
    D3D11_VIEWPORT vp[16]{}; UINT count=16; ctx->RSGetViewports(&count,vp);
    ComPtr<ID3D11RasterizerState> oldRaster; ctx->RSGetState(&oldRaster);
    ComPtr<ID3D11DepthStencilState> oldDepth; UINT ref=0; ctx->OMGetDepthStencilState(&oldDepth,&ref);
    ComPtr<ID3D11BlendState> oldBlend; float factor[4]{}; UINT mask=0; ctx->OMGetBlendState(&oldBlend,factor,&mask);
    ID3D11ShaderResourceView* input=nullptr; ctx->PSGetShaderResources(0,1,&input);
    ID3D11SamplerState* oldSampler=nullptr; ctx->PSGetSamplers(0,1,&oldSampler);
    ID3D11Buffer* oldConstants=nullptr; ctx->PSGetConstantBuffers(0,1,&oldConstants);
    auto* rtv=targetView.Get(); ctx->OMSetRenderTargets(1,&rtv,nullptr);
    D3D11_VIEWPORT full{0,0,float(td.Width),float(td.Height),0,1}; ctx->RSSetViewports(1,&full);
    ctx->RSSetState(raster.Get()); ctx->OMSetDepthStencilState(depth.Get(),0); ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(pixel,nullptr,0); ctx->GSSetShader(nullptr,nullptr,0);
    ctx->HSSetShader(nullptr,nullptr,0); ctx->DSSetShader(nullptr,nullptr,0);
    auto* srv=view.Get(); ctx->PSSetShaderResources(0,1,&srv);
    auto* smp=sampler.Get(); ctx->PSSetSamplers(0,1,&smp);
    auto* placement=constants.Get(); ctx->PSSetConstantBuffers(0,1,&placement);
    ctx->Draw(3,0);
    ID3D11ShaderResourceView* none=nullptr; ctx->PSSetShaderResources(0,1,&none);
    ctx->OMSetRenderTargets(8,rt,oldDsv.Get()); ctx->PSSetShaderResources(0,1,&input); ctx->PSSetSamplers(0,1,&oldSampler);
    ctx->PSSetConstantBuffers(0,1,&oldConstants);
    ctx->RSSetViewports(count,vp); ctx->RSSetState(oldRaster.Get()); ctx->OMSetDepthStencilState(oldDepth.Get(),ref); ctx->OMSetBlendState(oldBlend.Get(),factor,mask);
    ctx->IASetInputLayout(layout.Get()); ctx->IASetPrimitiveTopology(topology);
    ctx->VSSetShader(oldVs.Get(),vi,vn); ctx->PSSetShader(oldPs.Get(),pi,pn); ctx->GSSetShader(oldGs.Get(),gi,gn);
    ctx->HSSetShader(oldHs.Get(),hi,hn); ctx->DSSetShader(oldDs.Get(),di,dn);
    for(auto* t:rt) if(t) t->Release(); if(input) input->Release(); if(oldSampler) oldSampler->Release(); if(oldConstants) oldConstants->Release();
    for(UINT i=0;i<vn;++i) if(vi[i]) vi[i]->Release(); for(UINT i=0;i<pn;++i) if(pi[i]) pi[i]->Release(); for(UINT i=0;i<gn;++i) if(gi[i]) gi[i]->Release();
    for(UINT i=0;i<hn;++i) if(hi[i]) hi[i]->Release(); for(UINT i=0;i<dn;++i) if(di[i]) di[i]->Release();
    return true;
}
}
