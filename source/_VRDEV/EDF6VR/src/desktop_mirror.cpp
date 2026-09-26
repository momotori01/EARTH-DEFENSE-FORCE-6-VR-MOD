#include "desktop_mirror.h"
#include "vr_math.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
struct Image { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_TEXTURE2D_DESC desc{}; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN; bool ready=false; };
Image images[4];
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
struct Plane { float centre[4]{},right[4]{},up[4]{}; }; // centre.w=enabled; right/up.w=size
std::atomic<float> uiScale{1.35f};
struct Constants { float fov[4]{}; float sourceFov[4]{}; float output[4]{}; Plane panel{},reticle{}; };
constexpr char shader[]=R"(
Texture2D<float4> World:register(t0),Panel:register(t1),Reticle:register(t2);
SamplerState Smooth:register(s0);
struct Plane { float4 centre; float4 right; float4 up; };
cbuffer Params:register(b0) { float4 Fov; float4 SourceFov; float4 Output; Plane Ui; Plane Aim; };
struct Vertex { float4 position:SV_Position; float2 uv:TEXCOORD0; };
Vertex vertex(uint id:SV_VertexID) {
 Vertex v; v.uv=float2((id<<1)&2,id&2); v.position=float4(v.uv.x*2-1,1-v.uv.y*2,0,1); return v;
}
float4 overlay(Texture2D<float4> image,Plane p,float3 ray,float4 under) {
 if(p.centre.w<.5) return under;
 float3 normal=cross(p.right.xyz,p.up.xyz); float denom=dot(normal,ray);
 if(abs(denom)<.00001) return under;
 float t=dot(normal,p.centre.xyz)/denom; if(t<=0) return under;
 float3 local=ray*t-p.centre.xyz;
 float2 uv=float2(.5+dot(local,p.right.xyz)/p.right.w,.5-dot(local,p.up.xyz)/p.up.w);
 if(any(uv<0)||any(uv>1)) return under;
 float4 c=image.SampleLevel(Smooth,uv,0);
 // OpenXR layers use premultiplied alpha unless UNPREMULTIPLIED is requested.
 return float4(c.rgb+under.rgb*(1-c.a),1);
}
float4 pixel(Vertex v):SV_Target {
 float3 ray=float3(lerp(Fov.x,Fov.y,v.uv.x),lerp(Fov.z,Fov.w,v.uv.y),-1);
 float2 worldUv=float2((ray.x-SourceFov.x)/(SourceFov.y-SourceFov.x),(SourceFov.z-ray.y)/(SourceFov.z-SourceFov.w));
 float4 c=World.SampleLevel(Smooth,worldUv,0);
 c=overlay(Panel,Ui,ray,c); c=overlay(Reticle,Aim,ray,c);
 if(Output.x>.5) c.rgb=lerp(1.055*pow(max(c.rgb,0),1.0/2.4)-.055,12.92*c.rgb,step(c.rgb,.0031308));
 return float4(c.rgb,1);
})";
bool Srgb(DXGI_FORMAT f) noexcept { return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; }
DXGI_FORMAT Family(DXGI_FORMAT f) noexcept {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    default:return DXGI_FORMAT_UNKNOWN;
    }
}
bool Build(ID3D11Device* d) noexcept {
    if(device.Get()!=d) { ReleaseDesktopMirror();device=d; }
    if(vs && ps && constants && sampler && blend && depth && raster) return true;
    static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll"); if(!compiler) return false;
    auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    if(!compile) return false;
    ComPtr<ID3DBlob> v,p;
    if(FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",0,0,&p,nullptr))) return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)) ||
       FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps))) return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
    D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC z{};z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;r.MultisampleEnable=TRUE;
    return SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants)) && SUCCEEDED(d->CreateSamplerState(&s,&sampler))
        && SUCCEEDED(d->CreateBlendState(&b,&blend)) && SUCCEEDED(d->CreateDepthStencilState(&z,&depth))
        && SUCCEEDED(d->CreateRasterizerState(&r,&raster));
}
Quat Q(const XrQuaternionf& q) noexcept { return {q.x,q.y,q.z,q.w}; }
Vec3 V(const XrVector3f& v) noexcept {return {v.x,v.y,v.z};}
Vec3 Add(Vec3 a,Vec3 b) noexcept {return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b) noexcept {return {a.x-b.x,a.y-b.y,a.z-b.z};}
Plane ProjectPlane(const XrCompositionLayerQuad* quad,const XrPosef& eye,const XrPosef* head) noexcept {
    Plane out{};
    if(!quad || quad->eyeVisibility==XR_EYE_VISIBILITY_LEFT || quad->size.width<=0 || quad->size.height<=0
        || !NormalizedQuat(Q(eye.orientation)) || !NormalizedQuat(Q(quad->pose.orientation))) return out;
    auto centre=V(quad->pose.position);
    auto right=QuatRotate(Q(quad->pose.orientation),{1,0,0});
    auto up=QuatRotate(Q(quad->pose.orientation),{0,1,0});
    if(head) {
        if(!NormalizedQuat(Q(head->orientation))) return out;
        centre=Add(V(head->position),QuatRotate(Q(head->orientation),centre));
        right=QuatRotate(Q(head->orientation),right);up=QuatRotate(Q(head->orientation),up);
    }
    const Quat inverse{-eye.orientation.x,-eye.orientation.y,-eye.orientation.z,eye.orientation.w};
    centre=QuatRotate(inverse,Sub(centre,V(eye.position)));right=QuatRotate(inverse,right);up=QuatRotate(inverse,up);
    out={{centre.x,centre.y,centre.z,1},{right.x,right.y,right.z,quad->size.width},{up.x,up.y,up.z,quad->size.height}};
    return out;
}
}
bool SnapshotMirror(ID3D11DeviceContext* ctx,ID3D11Texture2D* source,MirrorImage slot,DXGI_FORMAT format) noexcept {
    if(!ctx || !source || static_cast<unsigned>(slot)>=4) return false;
    ComPtr<ID3D11Device> d;ctx->GetDevice(&d);
    if(device.Get()!=d.Get()) {ReleaseDesktopMirror();device=d;}
    auto& image=images[static_cast<unsigned>(slot)];image.ready=false;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    if(desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 || Family(format)==DXGI_FORMAT_UNKNOWN
        || Family(format)!=Family(desc.Format)) return false;
    if(!image.texture || image.desc.Width!=desc.Width || image.desc.Height!=desc.Height || image.format!=format) {
        image={};desc.Format=Family(format);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags=desc.MiscFlags=0;
        if(FAILED(d->CreateTexture2D(&desc,nullptr,&image.texture))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=format;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
        if(FAILED(d->CreateShaderResourceView(image.texture.Get(),&srv,&image.srv))) {image={};return false;}
        image.desc=desc;image.format=format;
    }
    ctx->CopyResource(image.texture.Get(),source);image.ready=true;return true;
}
bool InsideDesktopMirror() noexcept { return drawing; }
void SetDesktopMirrorUiScale(float scale) noexcept {
    uiScale.store(std::isfinite(scale) && scale>=0.5f && scale<=2.5f?scale:1.0f,std::memory_order_relaxed);
}
void InvalidateMirrorImage(MirrorImage slot) noexcept { if(static_cast<unsigned>(slot)<4) images[static_cast<unsigned>(slot)].ready=false; }
void ReleaseDesktopMirror() noexcept {
    for(auto& image:images) image={};
    vs.Reset();ps.Reset();constants.Reset();sampler.Reset();blend.Reset();depth.Reset();raster.Reset();device.Reset();
}
bool RenderDesktopMirror(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,MirrorImage world,
    const XrPosef& eye,const XrFovf& fov,const XrPosef& head,
    const XrCompositionLayerQuad* ui,const XrCompositionLayerQuad* reticle,float horizontalFovDegrees,
    float presentedAspect) noexcept {
    const auto slot=static_cast<unsigned>(world);
    if(!ctx || !target || slot>1 || !images[slot].ready) return false;
    ComPtr<ID3D11Device> d;ctx->GetDevice(&d);
    if(d.Get()!=device.Get() || !Build(d.Get())) return false;
    D3D11_TEXTURE2D_DESC td{};target->GetDesc(&td);
    if(Family(td.Format)==DXGI_FORMAT_UNKNOWN || td.ArraySize!=1 || !td.Width || !td.Height) return false;
    D3D11_RENDER_TARGET_VIEW_DESC view{};view.Format=td.Format;
    if(view.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS) view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(view.Format==DXGI_FORMAT_B8G8R8A8_TYPELESS) view.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    view.ViewDimension=td.SampleDesc.Count>1?D3D11_RTV_DIMENSION_TEXTURE2DMS:D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> targetView;if(FAILED(d->CreateRenderTargetView(target,&view,&targetView))) return false;
    Constants data{};
    data.fov[0]=std::tan(fov.angleLeft);data.fov[1]=std::tan(fov.angleRight);
    data.fov[2]=std::tan(fov.angleUp);data.fov[3]=std::tan(fov.angleDown);
    for(float value:data.fov) if(!std::isfinite(value)) return false;
    if(data.fov[1]<=data.fov[0] || data.fov[2]<=data.fov[3]) return false;
    std::memcpy(data.sourceFov,data.fov,sizeof(data.fov));
    if(std::isfinite(horizontalFovDegrees) && horizontalFovDegrees>0) {
        const float degrees=std::fmin(140.f,std::fmax(45.f,horizontalFovDegrees));
        // The shape it will be seen at, not the shape it is stored in.
        const float aspect=presentedAspect>0.1f && presentedAspect<10.0f
            ?presentedAspect:float(td.Width)/float(td.Height);
        float halfWidth=std::tan(degrees*0.00872664626f);
        // Fit inside available source rays, with no stretching or edge smear.
        const float available=std::fmin(std::fmin(-data.fov[0],data.fov[1]),
                                      std::fmin(data.fov[2],-data.fov[3])*aspect);
        if(available<=0) return false;
        halfWidth=std::fmin(halfWidth,available);
        data.fov[0]=-halfWidth;data.fov[1]=halfWidth;
        data.fov[2]=halfWidth/aspect;data.fov[3]=-halfWidth/aspect;
    }
    data.output[0]=Srgb(images[slot].format) && !Srgb(view.Format)?1.f:0.f;
    if(images[2].ready) data.panel=ProjectPlane(ui,eye,&head);
    // Larger on the desktop, where it is read off a monitor at a distance
    // rather than hung in front of the eyes: its width and height only, about
    // its own centre. Nothing here reaches the headset's layer.
    if(data.panel.centre[3]>.5f) {
        const float scale=uiScale.load(std::memory_order_relaxed);
        data.panel.right[3]*=scale; data.panel.up[3]*=scale;
    }
    if(images[3].ready) data.reticle=ProjectPlane(reticle,eye,nullptr);
    ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
    // State backup/restore follows weapon_composite and the UEVR D3D11 path.
    // A target view is local to this call: ResizeBuffers never sees a held reference.
    DrawScope mirrorScope;
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
    ID3D11ShaderResourceView* drawInputs[]={images[slot].srv.Get(),images[2].srv.Get(),images[3].srv.Get()};ctx->PSSetShaderResources(0,3,drawInputs);
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
}
