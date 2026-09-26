#include "aim_hud.h"
#include "ui_capture.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
thread_local bool drawing=false;
struct DrawScope {bool previous=drawing;DrawScope(){drawing=true;}~DrawScope(){drawing=previous;}};
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vs,frameVs;
ComPtr<ID3D11PixelShader> ps,framePs;
ComPtr<ID3D11Buffer> constants,frameConstants;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11RasterizerState> raster;
struct Constants {float pixel[4]{};float points[AimHudSnapshot::capacity][4]{};};
// The lock-on sight's edges: two frames of four, as (x0, y0, x1, y1) in NDC.
struct FrameConstants {float pixel[4]{};float edges[8][4]{};};
constexpr char frameShader[]=R"(
cbuffer Frame:register(b0) {float4 pixelSize;float4 edges[8];}
struct V {float4 position:SV_Position;};
V vertex(uint id:SV_VertexID) {
 // A thin band along each edge, a constant number of pixels wide, run on by
 // its half width at both ends so the corners close.
 float4 e=edges[id/6];uint corner=id%6;
 float2 along=(e.zw-e.xy)/pixelSize.xy;float len=max(length(along),1e-4);along/=len;
 float2 across=float2(-along.y,along.x);
 const float2 corners[6]={float2(0,-1),float2(0,1),float2(1,-1),float2(1,-1),float2(0,1),float2(1,1)};
 float2 c=corners[corner];
 float2 p=lerp(e.xy,e.zw,c.x)+(across*c.y+along*(c.x*2-1))*pixelSize.z*pixelSize.xy;
 V o;o.position=float4(p,0,1);return o;
}
float4 fragment(V i):SV_Target {return float4(float3(1,.15,.08)*.9,.9);}
)";
constexpr char shader[]=R"(
cbuffer Data:register(b0) {float4 pixelSize;float4 points[128];}
struct V {float4 position:SV_Position;float2 local:TEXCOORD0;nointerpolation float kind:TEXCOORD1;};
V vertex(uint id:SV_VertexID,uint instance:SV_InstanceID) {
 const float2 corners[6]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
 V o;o.local=corners[id];o.kind=points[instance].z;
 o.position=float4(points[instance].xy+o.local*pixelSize.xy,0,1);return o;
}
float4 fragment(V i):SV_Target {
 float2 a=abs(i.local);float m=max(a.x,a.y);
 // The game selects the points and the native cursor/after variant. A dotted
 // candidate circle remains distinct from a target's corner bracket.
 float edgeDistance=i.kind>1.5?abs(length(i.local)-.70):max(abs(m-.75),.42-min(a.x,a.y));
 float aa=max(fwidth(edgeDistance),.008);
 float ink=1-smoothstep(.045-aa,.045+aa,edgeDistance);
 float alpha=1-smoothstep(.09-aa,.09+aa,edgeDistance);
 float3 colour=i.kind>1.5?float3(1,.75,.05):(i.kind>.5?float3(.5,1,.6):float3(1,.15,.08));
 return float4(colour*ink,alpha);
})";
bool Build(ID3D11Device* d) {
    if(device.Get()!=d) {vs.Reset();ps.Reset();frameVs.Reset();framePs.Reset();constants.Reset();frameConstants.Reset();
        blend.Reset();depth.Reset();raster.Reset();device=d;}
    if(vs && ps && frameVs && framePs && constants && frameConstants && blend && depth && raster) return true;
    static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");if(!compiler)return false;
    auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    ComPtr<ID3DBlob> v,p;
    if(!compile || FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"fragment","ps_5_0",0,0,&p,nullptr))) return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
       FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps))) return false;
    ComPtr<ID3DBlob> fv,fp;
    if(FAILED(compile(frameShader,sizeof(frameShader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&fv,nullptr)) ||
       FAILED(compile(frameShader,sizeof(frameShader),nullptr,nullptr,nullptr,"fragment","ps_5_0",0,0,&fp,nullptr)) ||
       FAILED(d->CreateVertexShader(fv->GetBufferPointer(),fv->GetBufferSize(),nullptr,&frameVs)) ||
       FAILED(d->CreatePixelShader(fp->GetBufferPointer(),fp->GetBufferSize(),nullptr,&framePs))) return false;
    D3D11_BUFFER_DESC fb{};fb.ByteWidth=sizeof(FrameConstants);fb.Usage=D3D11_USAGE_DEFAULT;fb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(d->CreateBuffer(&fb,nullptr,&frameConstants))) return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_BLEND_DESC b{};auto& rt=b.RenderTarget[0];rt.BlendEnable=TRUE;
    rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=FALSE;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
    return SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants)) && SUCCEEDED(d->CreateBlendState(&b,&blend)) &&
        SUCCEEDED(d->CreateDepthStencilState(&z,&depth)) && SUCCEEDED(d->CreateRasterizerState(&r,&raster));
}
}
bool InsideAimHudDraw() noexcept {return drawing;}
bool DrawAimHud(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const Matrix& view,const Matrix& projection,
                const AimHudSnapshot& snapshot,float scale) noexcept {
    if(!snapshot.count && !snapshot.frameCount) return true;
    if(!ctx || !target || snapshot.count>AimHudSnapshot::capacity || snapshot.frameCount>2 || !std::isfinite(scale) || scale<.05f || scale>4) return false;
    D3D11_TEXTURE2D_DESC desc{};target->GetDesc(&desc);if(!desc.Width || !desc.Height) return false;
    // The lock-on sight: its corners through this eye's own camera, so the
    // frame stands round the cone in the world, in both eyes at infinity. A
    // frame with a corner behind the eye is not drawn.
    FrameConstants frame{};unsigned edges=0;
    frame.pixel[0]=2.f/desc.Width;frame.pixel[1]=2.f/desc.Height;frame.pixel[2]=std::max(1.f,1.1f*(float(desc.Height)/1080.f));   // half width, pixels
    for(unsigned f=0;f<snapshot.frameCount;++f) {
        float ndc[4][2]{};bool ok=true;
        for(unsigned c=0;c<4&&ok;++c)ok=ProjectAimHudAhead(view,projection,snapshot.frames[f].corners[c],ndc[c]);
        if(!ok) continue;
        for(unsigned c=0;c<4;++c) {
            auto& e=frame.edges[edges++];e[0]=ndc[c][0];e[1]=ndc[c][1];e[2]=ndc[(c+1)%4][0];e[3]=ndc[(c+1)%4][1];
        }
    }
    Constants data{};unsigned count=0;
    // Constant ANGULAR-ish screen size. Scaling a bracket cannot move its target.
    const float radius=20.f*scale*(float(desc.Height)/1080.f);
    data.pixel[0]=2*radius/desc.Width;data.pixel[1]=2*radius/desc.Height;
    for(unsigned i=0;i<snapshot.count;++i) {
        float ndc[2]{};if(!ProjectAimHud(view,projection,snapshot.points[i].world,ndc)) continue;
        data.points[count][0]=ndc[0];data.points[count][1]=ndc[1];data.points[count][2]=float(snapshot.points[i].kind);++count;
    }
    if(!count && !edges) return true;
    ComPtr<ID3D11Device> d;ctx->GetDevice(&d);if(!Build(d.Get())) return false;
    ComPtr<ID3D11RenderTargetView> output;
    if(FAILED(d->CreateRenderTargetView(target,nullptr,&output))) return false;
    PauseUiCapture pause;DrawScope scope;
    ID3D11RenderTargetView* rt[8]{};ComPtr<ID3D11DepthStencilView> oldDsv;ctx->OMGetRenderTargets(8,rt,&oldDsv);
    ComPtr<ID3D11VertexShader> oldVs;ComPtr<ID3D11PixelShader> oldPs;ComPtr<ID3D11GeometryShader> oldGs;
    ComPtr<ID3D11HullShader> oldHs;ComPtr<ID3D11DomainShader> oldDs;
    ID3D11ClassInstance *vi[256]{},*pi[256]{},*gi[256]{},*hi[256]{},*di[256]{};
    UINT vn=256,pn=256,gn=256,hn=256,dn=256;
    ctx->VSGetShader(&oldVs,vi,&vn);ctx->PSGetShader(&oldPs,pi,&pn);ctx->GSGetShader(&oldGs,gi,&gn);
    ctx->HSGetShader(&oldHs,hi,&hn);ctx->DSGetShader(&oldDs,di,&dn);
    ComPtr<ID3D11InputLayout> layout;D3D11_PRIMITIVE_TOPOLOGY topology{};
    ctx->IAGetInputLayout(&layout);ctx->IAGetPrimitiveTopology(&topology);
    D3D11_VIEWPORT vp[16]{};UINT vpCount=16;ctx->RSGetViewports(&vpCount,vp);
    ComPtr<ID3D11RasterizerState> oldRaster;ctx->RSGetState(&oldRaster);
    ComPtr<ID3D11DepthStencilState> oldDepth;UINT ref=0;ctx->OMGetDepthStencilState(&oldDepth,&ref);
    ComPtr<ID3D11BlendState> oldBlend;float factors[4]{};UINT mask=0;ctx->OMGetBlendState(&oldBlend,factors,&mask);
    ComPtr<ID3D11Buffer> oldCb;ctx->VSGetConstantBuffers(0,1,&oldCb);
    ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
    auto* targetView=output.Get();ctx->OMSetRenderTargets(1,&targetView,nullptr);
    D3D11_VIEWPORT full{0,0,float(desc.Width),float(desc.Height),0,1};ctx->RSSetViewports(1,&full);
    ctx->RSSetState(raster.Get());ctx->OMSetDepthStencilState(depth.Get(),0);ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);
    ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);
    auto* cb=constants.Get();ctx->VSSetConstantBuffers(0,1,&cb);
    if(count)ctx->DrawInstanced(6,count,0,0);
    if(edges) {
        ctx->UpdateSubresource(frameConstants.Get(),0,nullptr,&frame,0,0);
        cb=frameConstants.Get();ctx->VSSetConstantBuffers(0,1,&cb);
        ctx->VSSetShader(frameVs.Get(),nullptr,0);ctx->PSSetShader(framePs.Get(),nullptr,0);
        ctx->Draw(6*edges,0);
    }
    ctx->OMSetRenderTargets(8,rt,oldDsv.Get());auto* prevCb=oldCb.Get();ctx->VSSetConstantBuffers(0,1,&prevCb);
    ctx->RSSetViewports(vpCount,vp);ctx->RSSetState(oldRaster.Get());ctx->OMSetDepthStencilState(oldDepth.Get(),ref);
    ctx->OMSetBlendState(oldBlend.Get(),factors,mask);ctx->IASetInputLayout(layout.Get());ctx->IASetPrimitiveTopology(topology);
    ctx->VSSetShader(oldVs.Get(),vi,vn);ctx->PSSetShader(oldPs.Get(),pi,pn);ctx->GSSetShader(oldGs.Get(),gi,gn);
    ctx->HSSetShader(oldHs.Get(),hi,hn);ctx->DSSetShader(oldDs.Get(),di,dn);
    for(auto* t:rt) if(t)t->Release();
    for(UINT i=0;i<vn;++i) if(vi[i])vi[i]->Release();for(UINT i=0;i<pn;++i) if(pi[i])pi[i]->Release();
    for(UINT i=0;i<gn;++i) if(gi[i])gi[i]->Release();for(UINT i=0;i<hn;++i) if(hi[i])hi[i]->Release();
    for(UINT i=0;i<dn;++i) if(di[i])di[i]->Release();
    return true;
}
}
