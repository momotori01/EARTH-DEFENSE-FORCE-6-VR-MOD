#include "weapon_composite.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <initializer_list>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vs;
ComPtr<ID3D11PixelShader> psLinear,psEncoded;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11RasterizerState> raster;
constexpr char shader[]=R"(
Texture2D<float4> Lit:register(t0); Texture2D<float4> Normal:register(t1);
float4 vertex(uint id:SV_VertexID):SV_Position {
 float2 uv=float2((id<<1)&2,id&2); return float4(uv.x*2-1,1-uv.y*2,0,1);
}
float4 shadeLinear(float4 p:SV_Position):SV_Target {
 float a=saturate(Normal.Load(int3(p.xy,0)).a); clip(a-.001);
 return float4(saturate(Lit.Load(int3(p.xy,0)).rgb),a);
}
float4 encoded(float4 p:SV_Position):SV_Target {
 float4 c=shadeLinear(p);
 c.rgb=lerp(1.055*pow(c.rgb,1.0/2.4)-.055,12.92*c.rgb,step(c.rgb,.0031308)); return c;
})";
bool Build(ID3D11Device* d) noexcept {
    if(device.Get()==d && vs && psLinear && psEncoded && blend && depth && raster) return true;
    ReleaseWeaponComposite(); device=d;
    static auto compiler=LoadLibraryW(L"d3dcompiler_47.dll"); if(!compiler) return false;
    auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    if(!compile) return false;
    ComPtr<ID3DBlob> v,l,e;
    if(FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"shadeLinear","ps_5_0",0,0,&l,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"encoded","ps_5_0",0,0,&e,nullptr))) return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
       FAILED(d->CreatePixelShader(l->GetBufferPointer(),l->GetBufferSize(),nullptr,&psLinear)) ||
       FAILED(d->CreatePixelShader(e->GetBufferPointer(),e->GetBufferSize(),nullptr,&psEncoded))) return false;
    D3D11_BLEND_DESC b{}; auto& t=b.RenderTarget[0]; t.BlendEnable=TRUE;
    t.SrcBlend=D3D11_BLEND_SRC_ALPHA; t.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; t.BlendOp=D3D11_BLEND_OP_ADD;
    t.SrcBlendAlpha=D3D11_BLEND_ZERO; t.DestBlendAlpha=D3D11_BLEND_ONE; t.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
    D3D11_DEPTH_STENCIL_DESC z{}; z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC r{}; r.FillMode=D3D11_FILL_SOLID; r.CullMode=D3D11_CULL_NONE; r.DepthClipEnable=TRUE;
    return SUCCEEDED(d->CreateBlendState(&b,&blend)) && SUCCEEDED(d->CreateDepthStencilState(&z,&depth)) && SUCCEEDED(d->CreateRasterizerState(&r,&raster));
}
}
void ReleaseWeaponComposite() noexcept { vs.Reset(); psLinear.Reset(); psEncoded.Reset(); blend.Reset(); depth.Reset(); raster.Reset(); device.Reset(); }
bool CompositeWeaponImage(ID3D11Device* d,ID3D11DeviceContext* ctx,ID3D11ShaderResourceView* colour,
    ID3D11ShaderResourceView* normal,ID3D11Texture2D* target) noexcept {
    if(!d || !ctx || !colour || !normal || !target) return false;
    D3D11_TEXTURE2D_DESC td{}; target->GetDesc(&td);
    // SteamVR returns typeless D3D textures even for the typed XR swapchain.
    // Match eye_warp's explicit UNORM RTV, with shader-side sRGB encoding.
    DXGI_FORMAT targetFormat=td.Format;
    if(targetFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS) targetFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(targetFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS) targetFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
    const bool srgb=td.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || td.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if(!srgb && targetFormat!=DXGI_FORMAT_R8G8B8A8_UNORM && targetFormat!=DXGI_FORMAT_B8G8R8A8_UNORM) return false;
    for(auto* input:{colour,normal}) {
        ComPtr<ID3D11Resource> resource; input->GetResource(&resource); ComPtr<ID3D11Texture2D> texture;
        if(FAILED(resource.As(&texture))) return false;
        D3D11_TEXTURE2D_DESC in{}; texture->GetDesc(&in);
        if(in.Width!=td.Width || in.Height!=td.Height || in.SampleDesc.Count!=1 || td.SampleDesc.Count!=1) return false;
    }
    if(!Build(d)) return false;
    D3D11_RENDER_TARGET_VIEW_DESC viewDesc{}; viewDesc.Format=targetFormat; viewDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> targetView; if(FAILED(d->CreateRenderTargetView(target,&viewDesc,&targetView))) return false;
    // Based on the existing warp and UEVR state backups; class instances and GS
    // are retained too. No view of an XR-owned target is cached past this call.
    ID3D11RenderTargetView* rt[8]{}; ComPtr<ID3D11DepthStencilView> oldDsv; ctx->OMGetRenderTargets(8,rt,&oldDsv);
    ComPtr<ID3D11VertexShader> oldVs; ComPtr<ID3D11PixelShader> oldPs; ComPtr<ID3D11GeometryShader> oldGs;
    ComPtr<ID3D11HullShader> oldHs; ComPtr<ID3D11DomainShader> oldDs;
    ID3D11ClassInstance* vi[256]{},*pi[256]{},*gi[256]{}; UINT vn=256,pn=256,gn=256;
    ID3D11ClassInstance* hi[256]{},*di[256]{}; UINT hn=256,dn=256;
    ctx->VSGetShader(&oldVs,vi,&vn); ctx->PSGetShader(&oldPs,pi,&pn); ctx->GSGetShader(&oldGs,gi,&gn);
    ctx->HSGetShader(&oldHs,hi,&hn); ctx->DSGetShader(&oldDs,di,&dn);
    ComPtr<ID3D11InputLayout> layout; D3D11_PRIMITIVE_TOPOLOGY topology{};
    ctx->IAGetInputLayout(&layout); ctx->IAGetPrimitiveTopology(&topology);
    D3D11_VIEWPORT vp[16]{}; UINT count=16; ctx->RSGetViewports(&count,vp);
    ComPtr<ID3D11RasterizerState> oldRaster; ctx->RSGetState(&oldRaster);
    ComPtr<ID3D11DepthStencilState> oldDepth; UINT ref=0; ctx->OMGetDepthStencilState(&oldDepth,&ref);
    ComPtr<ID3D11BlendState> oldBlend; float factor[4]{}; UINT mask=0; ctx->OMGetBlendState(&oldBlend,factor,&mask);
    ID3D11ShaderResourceView* inputs[2]{}; ctx->PSGetShaderResources(0,2,inputs);
    auto* rtv=targetView.Get(); ctx->OMSetRenderTargets(1,&rtv,nullptr);
    D3D11_VIEWPORT full{0,0,float(td.Width),float(td.Height),0,1}; ctx->RSSetViewports(1,&full);
    ctx->RSSetState(raster.Get()); ctx->OMSetDepthStencilState(depth.Get(),0); ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->IASetInputLayout(nullptr); ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(srgb?psLinear.Get():psEncoded.Get(),nullptr,0); ctx->GSSetShader(nullptr,nullptr,0);
    ctx->HSSetShader(nullptr,nullptr,0); ctx->DSSetShader(nullptr,nullptr,0);
    ID3D11ShaderResourceView* drawInputs[]={colour,normal}; ctx->PSSetShaderResources(0,2,drawInputs);
    ctx->Draw(3,0);
    ID3D11ShaderResourceView* none[2]{}; ctx->PSSetShaderResources(0,2,none);
    ctx->OMSetRenderTargets(8,rt,oldDsv.Get()); ctx->PSSetShaderResources(0,2,inputs);
    ctx->RSSetViewports(count,vp); ctx->RSSetState(oldRaster.Get()); ctx->OMSetDepthStencilState(oldDepth.Get(),ref); ctx->OMSetBlendState(oldBlend.Get(),factor,mask);
    ctx->IASetInputLayout(layout.Get()); ctx->IASetPrimitiveTopology(topology);
    ctx->VSSetShader(oldVs.Get(),vi,vn); ctx->PSSetShader(oldPs.Get(),pi,pn); ctx->GSSetShader(oldGs.Get(),gi,gn);
    ctx->HSSetShader(oldHs.Get(),hi,hn); ctx->DSSetShader(oldDs.Get(),di,dn);
    for(auto* t:rt) if(t) t->Release(); for(auto* i:inputs) if(i) i->Release();
    for(UINT i=0;i<vn;++i) if(vi[i]) vi[i]->Release(); for(UINT i=0;i<pn;++i) if(pi[i]) pi[i]->Release(); for(UINT i=0;i<gn;++i) if(gi[i]) gi[i]->Release();
    for(UINT i=0;i<hn;++i) if(hi[i]) hi[i]->Release(); for(UINT i=0;i<dn;++i) if(di[i]) di[i]->Release();
    return true;
}
}
