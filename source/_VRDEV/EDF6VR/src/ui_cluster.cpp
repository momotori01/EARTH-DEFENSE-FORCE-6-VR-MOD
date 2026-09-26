#include "ui_cluster.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <algorithm>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vs;
ComPtr<ID3D11PixelShader> psPass,psEncode,psDecode,psZero;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11RasterizerState> raster;
ComPtr<ID3D11SamplerState> sampler;
ComPtr<ID3D11Buffer> constants;
ComPtr<ID3D11Texture2D> viewedTexture;
ComPtr<ID3D11ShaderResourceView> view;
constexpr char shader[]=R"(
Texture2D<float4> Source:register(t0); SamplerState Smooth:register(s0);
cbuffer Rects:register(b0) { float4 Dst; float4 Src; }
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V vertex(uint id:SV_VertexID) {
 float2 c=float2((id==1||id==2||id==4)?1:0,(id==2||id==4||id==5)?1:0);
 float2 d=lerp(Dst.xy,Dst.zw,c);
 V o; o.p=float4(d.x*2-1,1-d.y*2,0,1); o.uv=lerp(Src.xy,Src.zw,c); return o;
}
float4 shadePass(V i):SV_Target {
 // Empty pixels leave what is under them, so pieces may be packed to overlap.
 float4 c=Source.SampleLevel(Smooth,i.uv,0); clip(c.a-.002); return c;
}
float4 shadeEncode(V i):SV_Target {
 float4 c=shadePass(i); c.rgb=lerp(1.055*pow(max(c.rgb,0),1.0/2.4)-.055,12.92*c.rgb,step(c.rgb,.0031308)); return c;
}
float4 shadeDecode(V i):SV_Target {
 float4 c=shadePass(i); c.rgb=lerp(pow((c.rgb+.055)/1.055,2.4),c.rgb/12.92,step(c.rgb,.04045)); return c;
}
float4 shadeZero(V i):SV_Target { return float4(0,0,0,0); })";
bool Build(ID3D11Device* d) noexcept {
    if(device.Get()==d && vs && psPass && psEncode && psDecode && psZero && blend && depth && raster && sampler && constants) return true;
    ReleaseUiCluster(); device=d;
    static auto compiler=LoadLibraryW(L"d3dcompiler_47.dll"); if(!compiler) return false;
    auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    if(!compile) return false;
    ComPtr<ID3DBlob> v,p,e,dd,z;
    if(FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadePass","ps_5_0",0,0,&p,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadeEncode","ps_5_0",0,0,&e,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadeDecode","ps_5_0",0,0,&dd,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadeZero","ps_5_0",0,0,&z,nullptr))) return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
       FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&psPass)) ||
       FAILED(d->CreatePixelShader(e->GetBufferPointer(),e->GetBufferSize(),nullptr,&psEncode)) ||
       FAILED(d->CreatePixelShader(dd->GetBufferPointer(),dd->GetBufferSize(),nullptr,&psDecode)) ||
       FAILED(d->CreatePixelShader(z->GetBufferPointer(),z->GetBufferSize(),nullptr,&psZero))) return false;
    // Straight copies: the panel's own pixels, colour and alpha, land unchanged.
    D3D11_BLEND_DESC b{}; b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC zd{}; zd.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC r{}; r.FillMode=D3D11_FILL_SOLID; r.CullMode=D3D11_CULL_NONE; r.DepthClipEnable=TRUE;
    D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD=D3D11_FLOAT32_MAX;
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=32; cb.Usage=D3D11_USAGE_DYNAMIC; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    return SUCCEEDED(d->CreateBlendState(&b,&blend)) && SUCCEEDED(d->CreateDepthStencilState(&zd,&depth))
        && SUCCEEDED(d->CreateRasterizerState(&r,&raster)) && SUCCEEDED(d->CreateSamplerState(&s,&sampler))
        && SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants));
}
bool Srgb(DXGI_FORMAT f) noexcept { return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; }
DXGI_FORMAT TargetFormat(DXGI_FORMAT f) noexcept {
    if(f==DXGI_FORMAT_R8G8B8A8_TYPELESS) return DXGI_FORMAT_R8G8B8A8_UNORM;
    if(f==DXGI_FORMAT_B8G8R8A8_TYPELESS) return DXGI_FORMAT_B8G8R8A8_UNORM;
    if(Srgb(f) || f==DXGI_FORMAT_R8G8B8A8_UNORM || f==DXGI_FORMAT_B8G8R8A8_UNORM) return f;
    return DXGI_FORMAT_UNKNOWN;
}
// Everything the draws below touch, put back afterwards.
struct SavedState {
    ID3D11RenderTargetView* rt[8]{}; ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11HullShader> hs; ComPtr<ID3D11DomainShader> ds;
    ID3D11ClassInstance* vi[256]{},*pi[256]{},*gi[256]{},*hi[256]{},*di[256]{}; UINT vn=256,pn=256,gn=256,hn=256,dn=256;
    ComPtr<ID3D11InputLayout> layout; D3D11_PRIMITIVE_TOPOLOGY topology{};
    D3D11_VIEWPORT vp[16]{}; UINT vpCount=16;
    ComPtr<ID3D11RasterizerState> raster; ComPtr<ID3D11DepthStencilState> depth; UINT ref=0;
    ComPtr<ID3D11BlendState> blend; float factor[4]{}; UINT mask=0;
    ID3D11ShaderResourceView* srv=nullptr; ID3D11SamplerState* sampler=nullptr; ID3D11Buffer* cb=nullptr; ID3D11Buffer* vcb=nullptr;
    void Take(ID3D11DeviceContext* ctx) noexcept {
        ctx->OMGetRenderTargets(8,rt,&dsv);
        ctx->VSGetShader(&vs,vi,&vn); ctx->PSGetShader(&ps,pi,&pn); ctx->GSGetShader(&gs,gi,&gn);
        ctx->HSGetShader(&hs,hi,&hn); ctx->DSGetShader(&ds,di,&dn);
        ctx->IAGetInputLayout(&layout); ctx->IAGetPrimitiveTopology(&topology);
        ctx->RSGetViewports(&vpCount,vp); ctx->RSGetState(&raster);
        ctx->OMGetDepthStencilState(&depth,&ref); ctx->OMGetBlendState(&blend,factor,&mask);
        ctx->PSGetShaderResources(0,1,&srv); ctx->PSGetSamplers(0,1,&sampler); ctx->PSGetConstantBuffers(0,1,&cb);
        ctx->VSGetConstantBuffers(0,1,&vcb);
    }
    void Restore(ID3D11DeviceContext* ctx) noexcept {
        ID3D11ShaderResourceView* none=nullptr; ctx->PSSetShaderResources(0,1,&none);
        ctx->OMSetRenderTargets(8,rt,dsv.Get()); ctx->PSSetShaderResources(0,1,&srv); ctx->PSSetSamplers(0,1,&sampler);
        ctx->PSSetConstantBuffers(0,1,&cb); ctx->VSSetConstantBuffers(0,1,&vcb);
        ctx->RSSetViewports(vpCount,vp); ctx->RSSetState(raster.Get()); ctx->OMSetDepthStencilState(depth.Get(),ref);
        ctx->OMSetBlendState(blend.Get(),factor,mask);
        ctx->IASetInputLayout(layout.Get()); ctx->IASetPrimitiveTopology(topology);
        ctx->VSSetShader(vs.Get(),vi,vn); ctx->PSSetShader(ps.Get(),pi,pn); ctx->GSSetShader(gs.Get(),gi,gn);
        ctx->HSSetShader(hs.Get(),hi,hn); ctx->DSSetShader(ds.Get(),di,dn);
        for(auto* t:rt) if(t) t->Release(); if(srv) srv->Release(); if(sampler) sampler->Release(); if(cb) cb->Release(); if(vcb) vcb->Release();
        for(UINT i=0;i<vn;++i) if(vi[i]) vi[i]->Release(); for(UINT i=0;i<pn;++i) if(pi[i]) pi[i]->Release();
        for(UINT i=0;i<gn;++i) if(gi[i]) gi[i]->Release(); for(UINT i=0;i<hn;++i) if(hi[i]) hi[i]->Release();
        for(UINT i=0;i<dn;++i) if(di[i]) di[i]->Release();
    }
};
bool SetRects(ID3D11DeviceContext* ctx,const float dst[4],const float src[4]) noexcept {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(ctx->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) return false;
    std::memcpy(mapped.pData,dst,16); std::memcpy(static_cast<char*>(mapped.pData)+16,src,16);
    ctx->Unmap(constants.Get(),0);
    return true;
}
// Binds target for drawing quads; the caller draws and restores.
bool Begin(ID3D11Device* d,ID3D11DeviceContext* ctx,ID3D11Texture2D* target,ComPtr<ID3D11RenderTargetView>& rtv,D3D11_TEXTURE2D_DESC& td) noexcept {
    target->GetDesc(&td);
    const auto format=TargetFormat(td.Format);
    if(td.SampleDesc.Count!=1 || format==DXGI_FORMAT_UNKNOWN || !Build(d)) return false;
    D3D11_RENDER_TARGET_VIEW_DESC vd{}; vd.Format=format; vd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    if(FAILED(d->CreateRenderTargetView(target,&vd,&rtv))) return false;
    auto* r=rtv.Get(); ctx->OMSetRenderTargets(1,&r,nullptr);
    D3D11_VIEWPORT full{0,0,float(td.Width),float(td.Height),0,1}; ctx->RSSetViewports(1,&full);
    ctx->RSSetState(raster.Get()); ctx->OMSetDepthStencilState(depth.Get(),0); ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0); ctx->GSSetShader(nullptr,nullptr,0);
    ctx->HSSetShader(nullptr,nullptr,0); ctx->DSSetShader(nullptr,nullptr,0);
    auto* c=constants.Get(); ctx->PSSetConstantBuffers(0,1,&c); ctx->VSSetConstantBuffers(0,1,&c); // the quad's corners come from it
    return true;
}
}
void ReleaseUiCluster() noexcept {
    vs.Reset(); psPass.Reset(); psEncode.Reset(); psDecode.Reset(); psZero.Reset(); blend.Reset(); depth.Reset();
    raster.Reset(); sampler.Reset(); constants.Reset(); view.Reset(); viewedTexture.Reset(); device.Reset();
}
bool ComposeUiCluster(ID3D11Device* d,ID3D11DeviceContext* ctx,ID3D11Texture2D* source,ID3D11Texture2D* target,
                      const UiClusterLayout& layout) noexcept {
    if(!d || !ctx || !source || !target || !layout.count || layout.count>8) return false;
    if(!(layout.canvasWidth>0.01f) || !(layout.canvasHeight>0.01f)) return false;
    D3D11_TEXTURE2D_DESC sd{}; source->GetDesc(&sd);
    if(sd.SampleDesc.Count!=1) return false;
    SavedState saved; saved.Take(ctx);
    ComPtr<ID3D11RenderTargetView> rtv; D3D11_TEXTURE2D_DESC td{};
    bool ok=Begin(d,ctx,target,rtv,td);
    if(ok && viewedTexture.Get()!=source) {
        view.Reset(); viewedTexture=source;
        if(FAILED(d->CreateShaderResourceView(source,nullptr,&view))) { viewedTexture.Reset(); ok=false; }
    }
    if(ok) {
        const bool srcSrgb=Srgb(sd.Format), dstSrgb=Srgb(TargetFormat(td.Format));
        auto* pixel=srcSrgb==dstSrgb?psPass.Get():(srcSrgb?psEncode.Get():psDecode.Get());
        const FLOAT clear[4]{}; ctx->ClearRenderTargetView(rtv.Get(),clear);
        ctx->PSSetShader(pixel,nullptr,0);
        auto* srv=view.Get(); ctx->PSSetShaderResources(0,1,&srv);
        auto* smp=sampler.Get(); ctx->PSSetSamplers(0,1,&smp);
        for(unsigned i=0;i<layout.count && ok;++i) {
            const auto& item=layout.items[i];
            const float w=(item.source.u1-item.source.u0)*item.scale/layout.canvasWidth;
            const float h=(item.source.v1-item.source.v0)*item.scale/layout.canvasHeight;
            if(!(w>0) || !(h>0)) continue;
            const float dst[4]={item.x,item.y,item.x+w,item.y+h};
            const float src[4]={item.source.u0,item.source.v0,item.source.u1,item.source.v1};
            ok=SetRects(ctx,dst,src);
            if(ok) ctx->Draw(6,0);
        }
    }
    saved.Restore(ctx);
    return ok;
}
bool CutUiRects(ID3D11Device* d,ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const UiRect* rects,unsigned count) noexcept {
    if(!d || !ctx || !target || !rects || !count) return false;
    SavedState saved; saved.Take(ctx);
    ComPtr<ID3D11RenderTargetView> rtv; D3D11_TEXTURE2D_DESC td{};
    bool ok=Begin(d,ctx,target,rtv,td);
    if(ok) {
        ctx->PSSetShader(psZero.Get(),nullptr,0);
        for(unsigned i=0;i<count && ok;++i) {
            const float dst[4]={rects[i].u0,rects[i].v0,rects[i].u1,rects[i].v1};
            const float src[4]={0,0,1,1};
            ok=SetRects(ctx,dst,src);
            if(ok) ctx->Draw(6,0);
        }
    }
    saved.Restore(ctx);
    return ok;
}
}
