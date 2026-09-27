#include "cockpit_draw.h"
#include "cockpit_combat_shells.h"
#include "cockpit_heli_shells.h"
#include "cockpit_texture.h"
#include "cockpit_lighting.h"
#include "ui_capture.h"
#include "native_world.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <atomic>
#include <cstring>
#include <future>
#include <cmath>

namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vs;
ComPtr<ID3D11PixelShader> ps;
ComPtr<ID3D11ComputeShader> cropShader;
ComPtr<ID3D11Buffer> cropBuffer;
ComPtr<ID3D11ShaderResourceView> cropView;
ComPtr<ID3D11UnorderedAccessView> cropOutput;
ComPtr<ID3D11InputLayout> layout;
ComPtr<ID3D11Buffer> constants;
struct InteriorMesh {ComPtr<ID3D11Buffer> vertices;UINT vertexCount=0,displayCount=0,seamCount=0;float aspects[4]{};bool occlusion=false;};
InteriorMesh interiors[kCockpitKinds];
struct CapMesh {ComPtr<ID3D11Buffer> vertices;UINT count=0;};
CapMesh caps[kCockpitCapMeshes];
// Barga keeps its variant*5+part numbering; every other rig names its meshes.
unsigned CapIndex(const CockpitRig& rig,unsigned part) noexcept {
    return rig.kind==CockpitKind::Barga?rig.capVariant*5+part:rig.capMesh[part];
}
ComPtr<ID3D11BlendState> blend,hologramBlend;
ComPtr<ID3D11DepthStencilState> depth,hologramDepth;
ComPtr<ID3D11DepthStencilState> reversedCapDepth,reversedDepthWrite;
// The world's depth as a texture, for the tank caps' own-depth pass.
ComPtr<ID3D11Texture2D> capWorldTexture;ComPtr<ID3D11ShaderResourceView> capWorldDepth;
struct NativeCapDepth {ComPtr<ID3D11DepthStencilView> view;std::uint64_t frame=0;int eye=-1;bool reversed=false;} nativeCapDepth;
ComPtr<ID3D11RasterizerState> raster;
ComPtr<ID3D11Texture2D> zbuffer;
ComPtr<ID3D11DepthStencilView> zview;
ComPtr<ID3D11ShaderResourceView> atlas[4],hudView;
ComPtr<ID3D11Texture2D> hudTexture;
ComPtr<ID3D11SamplerState> sampler;
ComPtr<ID3D11SamplerState> environmentSampler;
UINT zwidth=0,zheight=0;
bool atlasReady=false;
CockpitDrawStats stats{};
thread_local const CockpitRig* scopedRig=nullptr;
struct Mesh {
    ComPtr<ID3D11Buffer> vb,ib,vstage,istage,kept;
    UINT stride=0,vo=0,io=0,count=0,start=0;INT base=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    enum Stage {Pending,Ready,Unsupported} stage=Pending;
    UINT keptCount=0,tries=0;
    std::uint64_t readyFrame=0;
    bool opaque=true;   // wrote depth when first seen: lamps and glows are not hull
};
std::vector<Mesh> meshes;
CockpitRig meshRig{};
// Combat vehicles' lid (cockpit_combat_shells.h): the hidden hull's triangles
// as the rig's meshes are read back. Cutting it takes tens of milliseconds,
// so a worker builds it; Present makes the buffer once it is done.
std::vector<float> lidHull;
CapMesh lid;bool lidDirty=false;
std::future<std::vector<CockpitVertex>> lidBuild;unsigned lidGeneration=0,lidBuilding=0;
void ResetLid() noexcept {lidHull.clear();lid={};lidDirty=false;++lidGeneration;}
constexpr char shader[]=R"(
cbuffer Data:register(b0) {row_major float4x4 model;row_major float4x4 view;row_major float4x4 projection;float4 options;float4 panelAspect;float4 materials;float4 cameraPosition;float4 paint;float4 accent;}
cbuffer NativeSystem:register(b1) {float4 nativeSystem[38];}
cbuffer NativeExtra:register(b2) {float4 nativeExtra[25];}
cbuffer NativeEnvironment:register(b3) {float4 envInfo[6];}
Texture2D<float4> Base:register(t0);Texture2D<float4> Emission:register(t1);Texture2D<float4> Roughness:register(t2);Texture2D<float4> Hud:register(t3);
Texture2D<float4> Normal:register(t5);
Texture2D<float> WorldDepth:register(t8);
TextureCubeArray<float4> Environment:register(t6);
struct EnvCell {uint4 maps0;uint4 maps1;uint4 corners0;uint4 corners1;uint4 count;};
StructuredBuffer<EnvCell> EnvironmentGrid:register(t7);
StructuredBuffer<float4> Crop:register(t4);RWStructuredBuffer<float4> CropOut:register(u0);
SamplerState Smooth:register(s0);
SamplerState EnvironmentSmooth:register(s1);
groupshared uint minX,minY,maxX,maxY,hits;
[numthreads(64,1,1)]
void crop(uint3 group:SV_GroupID,uint thread:SV_GroupIndex) {
 uint region=group.x+1; // health and ammunition; the circular radar is stable
 float4 fence=region==1?float4(0,0,.52,.30):float4(0,.30,.50,1);
 if(thread==0){minX=minY=128;maxX=maxY=hits=0;}GroupMemoryBarrierWithGroupSync();
 uint width,height;Hud.GetDimensions(width,height);
 uint lx=128,ly=128,hx=0,hy=0,n=0;
 for(uint i=thread;i<16384;i+=64) {
   uint2 cell=uint2(i%128,i/128);float2 uv=lerp(fence.xy,fence.zw,(cell+.5)/128);
   uint2 pixel=min(uint2(uv*float2(width,height)),uint2(width-1,height-1));
   float4 c=Hud.Load(int3(pixel,0));
   if(c.a>.035&&max(c.r,max(c.g,c.b))>.012){lx=min(lx,cell.x);ly=min(ly,cell.y);hx=max(hx,cell.x);hy=max(hy,cell.y);++n;}
 }
 if(n){InterlockedMin(minX,lx);InterlockedMin(minY,ly);InterlockedMax(maxX,hx);InterlockedMax(maxY,hy);InterlockedAdd(hits,n);}
 GroupMemoryBarrierWithGroupSync();
 if(thread==0&&hits>=4) {
   float2 low=float2(minX,minY)/128,high=(float2(maxX,maxY)+1)/128;
   float2 pad=(high-low)*.045+2.0/128;
   CropOut[region]=float4(lerp(fence.xy,fence.zw,max(0,low-pad)),lerp(fence.xy,fence.zw,min(1,high+pad)));
 }
}
struct Input {float3 position:POSITION;float3 normal:NORMAL;float3 colour:COLOR0;float2 uv:TEXCOORD0;float surface:TEXCOORD1;float2 visibility:TEXCOORD2;};
struct Output {float4 position:SV_Position;float3 colour:COLOR0;float2 uv:TEXCOORD0;nointerpolation float surface:TEXCOORD1;float2 visibility:TEXCOORD2;float3 normal:TEXCOORD3;float3 eye:TEXCOORD4;float3 lamp:TEXCOORD5;float3 world:TEXCOORD6;};
Output vertex(Input i) {
 Output o;float4 world=mul(float4(i.position,1),model),p=mul(world,view);
 o.world=world.xyz;o.eye=cameraPosition.xyz-world.xyz;o.normal=mul(float4(i.normal,0),model).xyz;
 o.lamp=mul(float4(normalize(float3(.35,.85,-.4)),0),model).xyz;p.xz=-p.xz;
 o.position=mul(p,projection);
 o.visibility=i.visibility;o.colour=i.colour;o.uv=i.uv;o.surface=i.surface;return o;
}
float3 decode(float3 c) {return lerp(pow((c+.055)/1.055,2.4),c/12.92,step(c,.04045));}
float3 encode(float3 c) {return lerp(1.055*pow(max(c,0),1.0/2.4)-.055,12.92*c,step(c,.0031308));}
float3 surfaceUV(float2 uv,float surface) {
 uint tile=(uint)(surface+.5);float2 origin=float2(tile%4,tile/4)*.25;
 float2 repeated=(uv-origin)*96; // 13cm swatches: native scuffs stay fine at seated distance
 return float3(origin+.0078125+frac(repeated)*.234375,0);
}
float3 lightSurface(float3 base,float metal,float rough,float3 n,float3 v,float3 l,float3 diffuse,float3 specular) {
 float nl=saturate(dot(n,l)),nv=max(.03,saturate(dot(n,v)));float3 h=normalize(v+l+float3(1e-6,0,0));
 float nh=saturate(dot(n,h)),vh=saturate(dot(v,h)),a=rough*rough,a2=a*a;
 float d=nh*nh*(a2-1)+1;float distribution=a2/max(3.141593*d*d,1e-5);
 float k=(rough+1)*(rough+1)*.125;
 float visibility=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
 float3 f0=lerp(float3(.04,.04,.04),base,metal),f=f0+(1-f0)*pow(1-vh,5);
 // EDF light colours are irradiance-like (native diffuse is base * N.L).
 return (base*(1-metal)*(1-f)*diffuse+3.141593*distribution*visibility*f*specular/max(4*nl*nv,1e-4))*nl;
}
float3 environmentAt(float3 world,float3 direction,float rough) {
 uint w,h,cubes,levels,cells,stride;Environment.GetDimensions(0,w,h,cubes,levels);EnvironmentGrid.GetDimensions(cells,stride);
 if(!cubes||!cells)return 0;
 uint3 dims=asuint(envInfo[3].xyz);if(any(dims==0)||any(dims>4096))return 0;
 float3 grid=(world-envInfo[0].xyz)*envInfo[1].xyz;
 uint3 cell=(uint3)clamp(floor(grid),float3(0,0,0),float3(dims-1));
 uint index=(cell.z*dims.y+cell.y)*dims.x+cell.x;if(index>=cells)return 0;
 EnvCell data=EnvironmentGrid[index];uint count=min(data.count.x,8u);if(!count)return 0;
 float3 part=saturate(grid-float3(cell));float3 colour=0;float weightSum=0;
 float lod=rough*(1.7-.7*rough)*float(levels-1);
 // Most cells reference one probe. Avoid eight identical texture fetches.
 if(count==1)return Environment.SampleLevel(EnvironmentSmooth,float4(direction,min(data.maps0.x,cubes-1)),lod).rgb;
 // The game's local grid stores up to eight map indices plus corner-to-map
 // indices. Follow those mappings; never assume cube zero belongs here.
 [unroll]for(uint corner=0;corner<8;++corner) {
   uint selected=corner<4?data.corners0[corner&3]:data.corners1[corner&3];selected=min(selected,count-1);
   uint map=selected<4?data.maps0[selected&3]:data.maps1[selected&3];
   float3 t=float3(corner&1,(corner>>1)&1,(corner>>2)&1);
   float3 axes=lerp(1-part,part,t);float weight=axes.x*axes.y*axes.z;
   colour+=Environment.SampleLevel(EnvironmentSmooth,float4(direction,min(map,cubes-1)),lod).rgb*weight;weightSum+=weight;
 }
 return colour/max(weightSum,1e-4);
}
float4 fragment(Output i):SV_Target {
 uint material=(uint)(i.surface+.5);
 uint cabin=(uint)(materials.w+.5); // 0 Nix, 1 Crawler, 2 Barga, 3..5 Proteus, 6..7 tanks
 // Tank caps draw into their own depth (so a cap hides its own back) and
 // take the world's occlusion from its depth here: 1 = less is nearer, 2 = reversed.
 if(cameraPosition.w>.5){float world=WorldDepth.Load(int3(i.position.xy,0));if(cameraPosition.w<1.5?i.position.z>world:i.position.z<world)discard;}
 if(material==20)return float4(0,0,0,1); // digital occlusion: no light, grain or reflection
 if(material==21) {
   float3 seam=i.colour;if(options.y<.5)seam=encode(seam);
   return float4(seam*.32,.32);
 }
 if(material==24) {
   // A helicopter's canopy glass (22 and 23 are the Barga's joint caps): a faint tint, and the sky in it where the
   // pane is seen edge on. Premultiplied, for the cabin's translucent pass.
   float3 n=normalize(i.normal),e=normalize(i.eye);float edge=pow(1-abs(dot(n,e)),3);
   float alpha=.07+.28*edge;float3 glass=lerp(i.colour*3,float3(.50,.60,.72),edge);
   if(options.y<.5)glass=encode(glass);
   return float4(glass*alpha,alpha);
 }
 float3 c;
 if(i.surface>=15.5&&i.surface<19.5) {
   // Blue, transmissive combiner glass with clipped corners. It is a real
   // display surface; scene colour remains visible through its background.
   // A cabin drawn as its mirror image (accent.w: the Brute's right door
   // gunner) reads its screens the right way round.
   float2 uv=i.uv;if(accent.w>.5)uv.x=1-uv.x;
   float2 edge=abs(uv-.5);clip(.945-edge.x-edge.y);
   uint id=(uint)(i.surface-16+.5);float4 rect=Crop[id];
   float aspect=panelAspect[id];
   float sourceAspect=(rect.z-rect.x)*options.w/(rect.w-rect.y);
   float2 fit=sourceAspect>aspect?float2(.93,.93*aspect/sourceAspect):float2(.93*sourceAspect/aspect,.93);
   float2 local=.5+(uv-.5)/fit;
   float4 h=0;
   if(options.x>.5&&all(local>=0)&&all(local<=1))h=Hud.SampleLevel(Smooth,lerp(rect.xy,rect.zw,local),0);
   if(options.z>.5&&options.y<.5)h.rgb=encode(h.rgb);
   if(options.z<.5&&options.y>.5)h.rgb=decode(h.rgb);
   h*=.96;
   float3 blue=cabin==2?float3(.012,.105,.035):cabin==1?float3(.19,.125,.008):float3(.008,.060,.17);if(options.y<.5)blue=encode(blue);
   // A screen let into a console is a solid: visibility.y is cleared on those
   // quads by the generator, the rest stay transmissive.
   float alpha=i.visibility.y<.5?1.0:.43;
   return float4(h.rgb+blue*alpha*(1-h.a),h.a+alpha*(1-h.a));
 } else {
   // Textures supply only paint grain and material finish; all hardware is 3D.
   bool steel=i.surface>1.5&&i.surface<2.5;
   bool soft=(i.surface>2.5&&i.surface<3.5)||(i.surface>11.5&&i.surface<12.5);
   float3 base=i.colour,detailNormal=float3(0,0,1);
   float finish=material>=22?.48:soft?.78:(steel?.24:.32),rough=finish+.03;
   if(materials.x>.5&&material<16){
     float2 uv=surfaceUV(i.uv,i.surface).xy;
     float2 dx=ddx(i.uv)*22.5,dy=ddy(i.uv)*22.5;
     // Native Nix paint and its aligned BC5 scuff normals, calibrated to the
     // cockpit palette offline. Roughness is authored, not guessed from the
     // unverified game's packed RGB material texture.
     base=Base.SampleGrad(Smooth,uv,dx,dy).rgb;
     rough=Roughness.SampleGrad(Smooth,uv,dx,dy).r;
     detailNormal=Normal.SampleGrad(Smooth,uv,dx,dy).rgb*2-1;
   }
   // Native metal/scuff maps, muted construction orange for crawler panels.
   // Preserve the approved coloured grip caps and warning markings.
   if(cabin==1&&material<7) {
     float luminance=dot(base,float3(.2126,.7152,.0722));
     base=luminance*((material==0||material==4||material==6)?float3(5.2,1.65,.14):float3(1.05,1.05,1.04));
   }
   // Barga is solid grey; the Proteus gunner pod uses the same treatment a
   // little darker, as asked, so the two giants do not read as one interior.
   // Combat vehicles: the vehicle's own paint (kCombatPaints), linear, so the
   // lid passes for the hull round it; the atlas keeps only its grain, about
   // each tile's mean. The lid (paint.w 2) has a second tone for trim on Navy.
   if(paint.w>.5&&(material==0||material==4||material==6||(paint.w>1.5&&material==14))) {
     float luminance=dot(base,float3(.2126,.7152,.0722));
     float mean=material==0?.048:material==4?.058:material==6?.041:.020;
     base=(material==14?accent.rgb:paint.rgb)*(materials.x>.5?clamp(luminance/mean,.65,1.45):1.0);
     detailNormal.xy*=.40;detailNormal.z=sqrt(saturate(1-dot(detailNormal.xy,detailNormal.xy)));
     rough=max(rough,.46);
   // Tanks: sand paint like a real vehicle interior, dark equipment stays dark.
   } else if(cabin>=6&&material<7) {
     float luminance=dot(base,float3(.2126,.7152,.0722));
     bool paint=material==0||material==4||material==6;
     base=luminance*(paint?float3(2.05,1.72,1.10):float3(1.10,1.02,.90));
     detailNormal.xy*=.40;detailNormal.z=sqrt(saturate(1-dot(detailNormal.xy,detailNormal.xy)));
     rough=max(rough,.46);
   } else if(cabin>=2&&material<7) {
     float luminance=dot(base,float3(.2126,.7152,.0722));
     float lift=material==0||material==4||material==6?1.9:1.05;
     base=luminance*(cabin>=3?lift*.74:lift);
     detailNormal.xy*=.30;detailNormal.z=sqrt(saturate(1-dot(detailNormal.xy,detailNormal.xy)));
     rough=max(rough,.44);
   }
   float3 eye=normalize(i.eye),lamp=normalize(i.lamp),n=normalize(i.normal);
   bool back=dot(n,eye)<0;float ao=saturate(back?i.visibility.y:i.visibility.x);if(back)n=-n;
   // Reconstruct tangent directions from the actual face UV derivatives;
   // imported/canted/mirrored parts cannot share a fixed world tangent.
   float3 dpdx=-ddx(i.eye),dpdy=-ddy(i.eye);
   float2 duvdx=ddx(i.uv),duvdy=ddy(i.uv);
   float det=duvdx.x*duvdy.y-duvdx.y*duvdy.x;
   if(materials.x>.5&&material<16&&abs(det)>1e-12) {
     float3 t=(dpdx*duvdy.y-dpdy*duvdx.y)/det;
     float3 b=(dpdy*duvdx.x-dpdx*duvdy.x)/det;
     t=normalize(t-n*dot(t,n));b=normalize(b-n*dot(b,n));
     n=normalize(t*detailNormal.x+b*detailNormal.y+n*detailNormal.z);
   }
   float metal=steel?.86:0;rough=clamp(rough,.20,.90);
   // Native colour patches are retained, but exposed metal needs a plausible
   // reflectance rather than using the dark paint's diffuse brightness as F0.
   if(steel)base=clamp(base*3,.16,.55);
   float facing=saturate(dot(n,eye));float3 f0=lerp(float3(.04,.04,.04),base,metal);
   float3 fresnel=f0+(max(1-rough,f0)-f0)*pow(1-facing,5);
   float3 ambient=float3(.14,.18,.24),direct=0;
   if(materials.y>.5) {
     ambient=max(0,nativeSystem[33].xyz);
     uint count=min(asuint(nativeExtra[19].x),4u);
     for(uint light=0;light<count;++light) {
       float3 direction=-nativeSystem[21+light*3].xyz;float len=dot(direction,direction);
       if(len>1e-8)direct+=lightSurface(base,metal,rough,n,eye,direction*rsqrt(len),max(0,nativeSystem[22+light*3].xyz),max(0,nativeSystem[23+light*3].xyz));
     }
   } else {
     direct=lightSurface(base,metal,rough,n,eye,lamp,float3(.80,.88,1.0),float3(1,1,1));
     direct+=lightSurface(base,metal,rough,n,eye,normalize(-lamp+float3(0,.8,0)),float3(.16,.12,.09),float3(.28,.22,.16));
   }
   float3 reflected=reflect(-eye,n);
   float3 environment=materials.z>.5?environmentAt(i.world,reflected,rough):lerp(float3(.035,.045,.06),float3(.55,.66,.80),smoothstep(-.15,.8,reflected.y));
   // Contact visibility comes from this cabin's actual geometry. No borrowed
   // screen AO (which belongs to the grass/buildings behind the cockpit).
   c=base*(1-metal)*ambient*ao+direct*lerp(.35,1,ao);
   c+=environment*fresnel*lerp(ao,1,rough*.35)*(soft?.16:1);
   c+=base*.035*ao; // very small cabin bounce, not the former 48% flat fill
   if(material==8||material==9) {
     c=(material==8&&cabin==2?float3(.055,.38,.14):cabin==1&&material==8?float3(.58,.38,.025):i.colour)*1.15;
     if(materials.x>.5)c+=Emission.Sample(Smooth,i.uv).rgb*.10;
   }
 }
 if(options.y<.5)c=encode(c);
 return float4(c,1);
}
)";
struct Constants {Matrix model,view,projection;float options[4]{},panelAspect[4]{},materials[4]{},cameraPosition[4]{},paint[4]{},accent[4]{};};
bool Staging(ID3D11Device* d,ID3D11DeviceContext* ctx,ID3D11Buffer* src,ComPtr<ID3D11Buffer>& dst) {
    D3D11_BUFFER_DESC desc{};src->GetDesc(&desc);if(!desc.ByteWidth||desc.ByteWidth>32*1024*1024)return false;
    desc.BindFlags=desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    desc.StructureByteStride=0;
    if(FAILED(d->CreateBuffer(&desc,nullptr,&dst)))return false;ctx->CopyResource(dst.Get(),src);return true;
}
void BuildMesh(ID3D11DeviceContext* ctx,Mesh& mesh) {
    D3D11_MAPPED_SUBRESOURCE v{},i{};
    if(FAILED(ctx->Map(mesh.vstage.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&v)))return;
    if(FAILED(ctx->Map(mesh.istage.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&i))) {ctx->Unmap(mesh.vstage.Get(),0);return;}
    D3D11_BUFFER_DESC vd{},id{};mesh.vstage->GetDesc(&vd);mesh.istage->GetDesc(&id);
    const bool wide=mesh.format==DXGI_FORMAT_R32_UINT;
    const auto offset=static_cast<std::uint64_t>(mesh.io)+static_cast<std::uint64_t>(mesh.start)*(wide?4:2);
    const auto end=offset+static_cast<std::uint64_t>(mesh.count)*(wide?4:2);
    std::vector<std::uint32_t> picked;CockpitSelection selected{};
    if(mesh.vo<vd.ByteWidth&&end<=id.ByteWidth) selected=SelectCockpitLimbs(
        static_cast<const unsigned char*>(v.pData)+mesh.vo,vd.ByteWidth-mesh.vo,mesh.stride,
        static_cast<const unsigned char*>(i.pData)+offset,mesh.count,wide,mesh.base,meshRig,picked);
    if(selected.valid&&mesh.opaque&&(IsCombatKind(meshRig.kind)||IsHeliKind(meshRig.kind))) {
        const auto before=lidHull.size();
        CollectCockpitHull(static_cast<const unsigned char*>(v.pData)+mesh.vo,vd.ByteWidth-mesh.vo,mesh.stride,
            static_cast<const unsigned char*>(i.pData)+offset,mesh.count,wide,mesh.base,meshRig,lidHull);
        lidDirty=lidDirty||lidHull.size()!=before;
    }
    ctx->Unmap(mesh.vstage.Get(),0);ctx->Unmap(mesh.istage.Get(),0);
    mesh.vstage.Reset();mesh.istage.Reset();mesh.stage=Mesh::Unsupported;
    if(!selected.valid) {++stats.unsupported;return;}
    if(!picked.empty()) {
        ComPtr<ID3D11Device> d;ctx->GetDevice(&d);
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(picked.size()*4);desc.BindFlags=D3D11_BIND_INDEX_BUFFER;desc.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{picked.data(),0,0};
        if(FAILED(d->CreateBuffer(&desc,&data,&mesh.kept))) {++stats.unsupported;return;}
    }
    mesh.stage=Mesh::Ready;mesh.keptCount=selected.kept;
    mesh.readyFrame=NativeWorldRenderFrame();
    stats.keptTriangles+=selected.kept/3;stats.removedTriangles+=selected.removed/3;
}
}
void DiscardCockpitFrame() noexcept {
    DiscardCockpitLighting();hudView.Reset();hudTexture.Reset();nativeCapDepth={};capWorldDepth.Reset();capWorldTexture.Reset();
}
void ReleaseCockpitDraw() noexcept {
    DiscardCockpitFrame();
    ReleaseCockpitLighting();
    for(auto& mesh:interiors)mesh={};constants.Reset();vs.Reset();ps.Reset();layout.Reset();blend.Reset();depth.Reset();raster.Reset();
    for(auto& cap:caps)cap={};
    hologramBlend.Reset();hologramDepth.Reset();reversedCapDepth.Reset();reversedDepthWrite.Reset();
    cropShader.Reset();cropBuffer.Reset();cropView.Reset();cropOutput.Reset();
    zbuffer.Reset();zview.Reset();device.Reset();zwidth=zheight=0;
    for(auto& t:atlas)t.Reset();hudView.Reset();hudTexture.Reset();sampler.Reset();environmentSampler.Reset();atlasReady=false;
    meshes.clear();meshRig={};stats.meshes=stats.keptTriangles=stats.removedTriangles=0;
    ResetLid();
}
bool PrepareCockpitDraw(ID3D11Device* d) noexcept {
    if(!d)return false;if(device&&device.Get()!=d)ReleaseCockpitDraw();device=d;
    bool cabins=true;for(const auto& mesh:interiors)cabins=cabins&&mesh.vertices;
    if(vs&&ps&&cropShader&&cropView&&cropOutput&&layout&&cabins&&caps[kCockpitCapMeshes-1].vertices&&constants&&blend&&depth&&hologramBlend&&hologramDepth&&reversedDepthWrite&&raster&&sampler&&environmentSampler)return true;
    ++stats.initializations;
    struct PrepareTimer {ULONGLONG start=GetTickCount64();~PrepareTimer(){stats.prepareMs=GetTickCount64()-start;}} timer;
    static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");if(!compiler)return false;
    auto compile=reinterpret_cast<decltype(&D3DCompile)>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    ComPtr<ID3DBlob> v,p,cropCode;
    if(!compile||FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vertex","vs_5_0",0,0,&v,nullptr))||
        FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"fragment","ps_5_0",0,0,&p,nullptr))||
        FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"crop","cs_5_0",0,0,&cropCode,nullptr)))return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs))||
        FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps))||
        FAILED(d->CreateComputeShader(cropCode->GetBufferPointer(),cropCode->GetBufferSize(),nullptr,&cropShader)))return false;
    const float rects[4][4]={{.775f,0,.977f,.335f},{0,0,.52f,.30f},{0,.30f,.50f,1},{0,0,1,1}};
    D3D11_BUFFER_DESC rb{};rb.ByteWidth=sizeof(rects);rb.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    rb.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;rb.StructureByteStride=16;D3D11_SUBRESOURCE_DATA rd{rects,0,0};
    if(FAILED(d->CreateBuffer(&rb,&rd,&cropBuffer))||FAILED(d->CreateShaderResourceView(cropBuffer.Get(),nullptr,&cropView))||
        FAILED(d->CreateUnorderedAccessView(cropBuffer.Get(),nullptr,&cropOutput)))return false;
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",1,DXGI_FORMAT_R32_FLOAT,0,44,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(FAILED(d->CreateInputLayout(elements,6,v->GetBufferPointer(),v->GetBufferSize(),&layout)))return false;
    // One immutable buffer per cabin kind, sharing shaders and atlas. A pose
    // selects its own kind; switching vehicles or rendering older eye pairs
    // never rebuilds it.
    for(unsigned index=0;index<kCockpitKinds;++index) {
        const auto kind=static_cast<CockpitKind>(index);auto& target=interiors[index];
        auto mesh=BuildCockpit(kind);target.vertexCount=static_cast<UINT>(mesh.size());target.seamCount=0;
        for(auto i=mesh.rbegin();i!=mesh.rend()&&(i->surface==21||i->surface==24);++i)++target.seamCount;   // ink, canopy glass
        target.occlusion=ApplyCockpitOcclusion(mesh,kind);
        const auto displays=BuildCockpitDisplays(kind);target.displayCount=static_cast<UINT>(displays.size());
        mesh.insert(mesh.end(),displays.begin(),displays.end());
        for(std::size_t i=0;i<displays.size();i+=6) {
            float w2=0,h2=0;for(unsigned k=0;k<3;++k) {
                const float w=displays[i+1].position[k]-displays[i].position[k],h=displays[i+2].position[k]-displays[i+1].position[k];w2+=w*w;h2+=h*h;
            }
            target.aspects[static_cast<unsigned>(displays[i].surface-16)]=std::sqrt(w2/h2);
        }
        D3D11_BUFFER_DESC vb{};vb.ByteWidth=static_cast<UINT>(mesh.size()*sizeof(CockpitVertex));vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;vb.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{mesh.data(),0,0};if(FAILED(d->CreateBuffer(&vb,&data,&target.vertices)))return false;
    }
    for(unsigned index=0;index<kCockpitCapMeshes;++index) {
        const auto mesh=BuildCockpitCap(index);auto& cap=caps[index];cap.count=static_cast<UINT>(mesh.size());
        if(mesh.empty())return false;
        D3D11_BUFFER_DESC vb{};vb.ByteWidth=static_cast<UINT>(mesh.size()*sizeof(CockpitVertex));vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;vb.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA data{mesh.data(),0,0};if(FAILED(d->CreateBuffer(&vb,&data,&cap.vertices)))return false;
    }
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=sizeof(Constants);cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC z{};z.DepthEnable=TRUE;z.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;z.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    D3D11_BLEND_DESC hb{};auto& ht=hb.RenderTarget[0];ht.BlendEnable=TRUE;
    ht.SrcBlend=D3D11_BLEND_ONE;ht.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;ht.BlendOp=D3D11_BLEND_OP_ADD;
    ht.SrcBlendAlpha=D3D11_BLEND_ZERO;ht.DestBlendAlpha=D3D11_BLEND_ONE;ht.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    ht.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
    auto hz=z;hz.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    auto rz=hz;rz.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
    if(FAILED(d->CreateDepthStencilState(&rz,&reversedCapDepth)))return false;
    auto rw=z;rw.DepthFunc=D3D11_COMPARISON_GREATER_EQUAL;
    if(FAILED(d->CreateDepthStencilState(&rw,&reversedDepthWrite)))return false;
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=4;
    if(FAILED(d->CreateSamplerState(&s,&sampler)))return false;
    s.MaxLOD=D3D11_FLOAT32_MAX;if(FAILED(d->CreateSamplerState(&s,&environmentSampler)))return false;
    atlasReady=LoadCockpitTexture(d,201,true,atlas[0])&&LoadCockpitTexture(d,202,true,atlas[1])&&LoadCockpitTexture(d,203,false,atlas[2])&&LoadCockpitTexture(d,204,false,atlas[3]);
    return SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants))&&
        SUCCEEDED(d->CreateBlendState(&b,&blend))&&SUCCEEDED(d->CreateDepthStencilState(&z,&depth))&&
        SUCCEEDED(d->CreateBlendState(&hb,&hologramBlend))&&SUCCEEDED(d->CreateDepthStencilState(&hz,&hologramDepth))&&SUCCEEDED(d->CreateRasterizerState(&r,&raster));
}
static bool DrawCockpitPass(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const Matrix& view,const Matrix& projection,const CockpitPose& pose,ID3D11Texture2D* hud,std::uint64_t frame,ID3D11DepthStencilView* nativeDepth,bool capsOnly,bool reverseDepth) noexcept {
    if(!ctx||!target||!ValidCamera(view)||!ValidCamera(pose.cabin))return false;
    ComPtr<ID3D11Device> d;ctx->GetDevice(&d);if(!PrepareCockpitDraw(d.Get()))return false;
    D3D11_TEXTURE2D_DESC td{};target->GetDesc(&td);if(!td.Width||!td.Height||td.SampleDesc.Count!=1)return false;
    DXGI_FORMAT format=td.Format;
    if(format==DXGI_FORMAT_R8G8B8A8_TYPELESS)format=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(format==DXGI_FORMAT_B8G8R8A8_TYPELESS)format=DXGI_FORMAT_B8G8R8A8_UNORM;
    D3D11_RENDER_TARGET_VIEW_DESC outputDesc{};outputDesc.Format=format;outputDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> output;if(FAILED(d->CreateRenderTargetView(target,&outputDesc,&output)))return false;
    // A combat vehicle's lid: a worker cuts it once its hull has been read
    // back, and its buffer is made here at Present; the caps pass draws it.
    if(!capsOnly) try {
        if(lidBuild.valid()&&lidBuild.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            const auto mesh=lidBuild.get();
            if(lidBuilding==lidGeneration&&!mesh.empty()) {
                CapMesh built;
                D3D11_BUFFER_DESC lb{};lb.ByteWidth=static_cast<UINT>(mesh.size()*sizeof(CockpitVertex));lb.BindFlags=D3D11_BIND_VERTEX_BUFFER;lb.Usage=D3D11_USAGE_IMMUTABLE;
                D3D11_SUBRESOURCE_DATA ld{mesh.data(),0,0};
                if(SUCCEEDED(d->CreateBuffer(&lb,&ld,&built.vertices))){built.count=static_cast<UINT>(mesh.size());lid=built;stats.lidTriangles=lid.count/3;}
            }
        }
        if(lidDirty&&!lidBuild.valid()) {
            lidDirty=false;lidBuilding=lidGeneration;
            lidBuild=std::async(std::launch::async,[kind=meshRig.kind,hull=lidHull]{return BuildCombatLid(kind,hull);});
        }
    } catch(...) {lidBuild={};}
    if(hudTexture.Get()!=hud) {
        hudView.Reset();hudTexture=hud;
        if(hud && FAILED(d->CreateShaderResourceView(hud,nullptr,&hudView))) {hudTexture.Reset();return false;}
    }
    // A tank's barrel caps are closed solids in the open air: drawn against the
    // world's depth alone, nothing stops a cap's collar and back plate painting
    // over its own face, and it reads as see-through. They get their own depth
    // (the cockpit's buffer, cleared) and meet the world's depth in the shader,
    // read as a texture. The game's depth is never written. Barga's lids sit in
    // the robot's body, whose depth already hides their backs: unchanged.
    bool ownDepth=false;
    if(capsOnly) {
        if(!nativeDepth||!pose.rig.capCount||pose.rig.capCount>5||
           (pose.rig.kind==CockpitKind::Barga&&(pose.rig.capVariant>=2||pose.rig.capCount!=5)))return false;
        ComPtr<ID3D11Resource> res;nativeDepth->GetResource(&res);ComPtr<ID3D11Texture2D> tex;D3D11_TEXTURE2D_DESC zd{};
        if(FAILED(res.As(&tex)))return false;tex->GetDesc(&zd);
        if(zd.Width!=td.Width||zd.Height!=td.Height||zd.SampleDesc.Count!=1)return false;
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};nativeDepth->GetDesc(&dv);
        DXGI_FORMAT read=DXGI_FORMAT_UNKNOWN;
        switch(zd.Format) {
        case DXGI_FORMAT_R24G8_TYPELESS:read=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;break;
        case DXGI_FORMAT_R32_TYPELESS:read=DXGI_FORMAT_R32_FLOAT;break;
        case DXGI_FORMAT_R32G8X24_TYPELESS:read=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;break;
        case DXGI_FORMAT_R16_TYPELESS:read=DXGI_FORMAT_R16_UNORM;break;
        default:break;
        }
        if(pose.rig.kind!=CockpitKind::Barga&&read!=DXGI_FORMAT_UNKNOWN&&(zd.BindFlags&D3D11_BIND_SHADER_RESOURCE)&&
           dv.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D) {
            if(capWorldTexture.Get()!=tex.Get()) {
                capWorldDepth.Reset();capWorldTexture.Reset();
                D3D11_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=read;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
                sd.Texture2D.MostDetailedMip=dv.Texture2D.MipSlice;sd.Texture2D.MipLevels=1;
                if(SUCCEEDED(d->CreateShaderResourceView(tex.Get(),&sd,&capWorldDepth)))capWorldTexture=tex;
            }
            ownDepth=capWorldDepth&&capWorldTexture.Get()==tex.Get();
            if(ownDepth)++stats.ownDepthCaps;
        }
    }
    if((!capsOnly||ownDepth)&&(!zview||zwidth!=td.Width||zheight!=td.Height)) {
        zview.Reset();zbuffer.Reset();D3D11_TEXTURE2D_DESC zd=td;zd.Format=DXGI_FORMAT_D32_FLOAT;
        zd.BindFlags=D3D11_BIND_DEPTH_STENCIL;zd.CPUAccessFlags=zd.MiscFlags=0;zd.Usage=D3D11_USAGE_DEFAULT;
        if(FAILED(d->CreateTexture2D(&zd,nullptr,&zbuffer))||FAILED(d->CreateDepthStencilView(zbuffer.Get(),nullptr,&zview)))return false;
        zwidth=td.Width;zheight=td.Height;
    }
    const auto kind=static_cast<unsigned>(pose.rig.kind);if(kind>=kCockpitKinds)return false;
    const auto& interior=interiors[kind];stats.occlusion=interior.occlusion;
    Constants data{CockpitLocalToWorld(pose.cabin,CockpitMirrored(pose.rig)),view,projection};
    data.cameraPosition[3]=ownDepth?(reverseDepth?2.f:1.f):0.f;
    // The shader's cabin index picks the Nix's colours for the Wagon's cabin.
    data.materials[3]=static_cast<float>(pose.rig.kind==CockpitKind::NixChest?0u:kind);
    if(IsCombatKind(pose.rig.kind)&&pose.rig.paint<std::size(kCombatPaints)) {
        const auto& p=kCombatPaints[pose.rig.paint];
        for(int k=0;k<3;++k){data.paint[k]=CombatPaintLinear(p.paint[k]);data.accent[k]=CombatPaintLinear(p.accent[k]);}
        data.paint[3]=1;
    } else if(const auto* heli=FindHeliShell(pose.rig.kind)) {
        // A helicopter's cockpit and canopy lining take its interior paint;
        // the pickups' seat cloth (Navy) takes the accent, as a lid's trim does.
        for(int k=0;k<3;++k){data.paint[k]=CombatPaintLinear(heli->paint[k]);data.accent[k]=CombatPaintLinear(heli->accent[k]);}
        data.paint[3]=pose.rig.kind==CockpitKind::TruckPickup?2.f:1.f;
    }
    // A mirrored cabin (CockpitMirrored): its screens' images turned back.
    data.accent[3]=CockpitMirrored(pose.rig)?1.f:0.f;
    Matrix camera{};if(!InvertCamera(view,camera))return false;
    std::memcpy(data.cameraPosition,camera.m[3],12);
    const auto lighting=GetCockpitLighting(ctx,frame);
    data.materials[1]=lighting.system&&lighting.extra?1.f:0;
    data.materials[2]=lighting.cubes&&lighting.grid&&lighting.environment?1.f:0;
    const auto srgb=[](DXGI_FORMAT f){return f==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB||f==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;};
    D3D11_TEXTURE2D_DESC hd{};if(hud)hud->GetDesc(&hd);
    data.options[0]=hud?1.f:0;data.options[1]=srgb(format)?1.f:0;data.options[2]=srgb(hd.Format)?1.f:0;
    // EDF lays out HUD glyphs in 16:9 logical UI coordinates, then stretches
    // that canvas into the render target (e.g. 2912x2700 in VR). The backing
    // texture's aspect must not stretch a circular radar a second time.
    data.options[3]=16.f/9.f;
    std::memcpy(data.panelAspect,interior.aspects,sizeof(interior.aspects));
    data.materials[0]=atlasReady?1.f:0;
    // Same angular projection as the world, own near plane for leaning close.
    if(!capsOnly){data.projection.m[2][2]=30.f/(.02f-30.f);data.projection.m[3][2]=.02f*30.f/(.02f-30.f);data.projection.m[2][3]=-1;}
    PauseUiCapture pause;
    ID3D11RenderTargetView* rt[8]{};ComPtr<ID3D11DepthStencilView> oldDsv;ctx->OMGetRenderTargets(8,rt,&oldDsv);
    ComPtr<ID3D11VertexShader> oldVs;ComPtr<ID3D11PixelShader> oldPs;ComPtr<ID3D11GeometryShader> oldGs;
    ComPtr<ID3D11HullShader> oldHs;ComPtr<ID3D11DomainShader> oldDs;
    ID3D11ClassInstance *vi[256]{},*pi[256]{},*gi[256]{},*hi[256]{},*di[256]{};UINT vn=256,pn=256,gn=256,hn=256,dn=256;
    ctx->VSGetShader(&oldVs,vi,&vn);ctx->PSGetShader(&oldPs,pi,&pn);ctx->GSGetShader(&oldGs,gi,&gn);ctx->HSGetShader(&oldHs,hi,&hn);ctx->DSGetShader(&oldDs,di,&dn);
    ComPtr<ID3D11InputLayout> oldLayout;D3D11_PRIMITIVE_TOPOLOGY topology{};ctx->IAGetInputLayout(&oldLayout);ctx->IAGetPrimitiveTopology(&topology);
    ComPtr<ID3D11Buffer> oldVb,oldCb;UINT stride=0,offset=0;ctx->IAGetVertexBuffers(0,1,&oldVb,&stride,&offset);ctx->VSGetConstantBuffers(0,1,&oldCb);
    D3D11_VIEWPORT vp[16]{};UINT vpCount=16;ctx->RSGetViewports(&vpCount,vp);ComPtr<ID3D11RasterizerState> oldRaster;ctx->RSGetState(&oldRaster);
    ComPtr<ID3D11DepthStencilState> oldDepth;UINT ref=0;ctx->OMGetDepthStencilState(&oldDepth,&ref);
    ComPtr<ID3D11BlendState> oldBlend;float factors[4]{};UINT mask=0;ctx->OMGetBlendState(&oldBlend,factors,&mask);
    ID3D11ShaderResourceView* oldInputs[9]{};ctx->PSGetShaderResources(0,9,oldInputs);
    ComPtr<ID3D11SamplerState> oldSampler,oldEnvSampler;ctx->PSGetSamplers(0,1,&oldSampler);ctx->PSGetSamplers(1,1,&oldEnvSampler);
    ComPtr<ID3D11Buffer> oldPixelCb;ctx->PSGetConstantBuffers(0,1,&oldPixelCb);
    ID3D11Buffer* oldLighting[3]{};ctx->PSGetConstantBuffers(1,3,oldLighting);
    if(hudView) {
        // GPU-only alpha bounds: no readback, CPU stall or different crop
        // between stereo eyes. A given captured HUD produces identical bounds.
        ComPtr<ID3D11ComputeShader> oldCompute;ID3D11ClassInstance* ci[256]{};UINT cn=256;ctx->CSGetShader(&oldCompute,ci,&cn);
        ComPtr<ID3D11ShaderResourceView> oldCropInput;ctx->CSGetShaderResources(3,1,&oldCropInput);
        ComPtr<ID3D11UnorderedAccessView> oldCropOutput;ctx->CSGetUnorderedAccessViews(0,1,&oldCropOutput);
        auto* input=hudView.Get();auto* outputCrop=cropOutput.Get();ctx->CSSetShader(cropShader.Get(),nullptr,0);
        ctx->CSSetShaderResources(3,1,&input);ctx->CSSetUnorderedAccessViews(0,1,&outputCrop,nullptr);ctx->Dispatch(2,1,1);
        outputCrop=oldCropOutput.Get();ctx->CSSetUnorderedAccessViews(0,1,&outputCrop,nullptr);input=oldCropInput.Get();ctx->CSSetShaderResources(3,1,&input);
        ctx->CSSetShader(oldCompute.Get(),ci,cn);for(UINT i=0;i<cn;++i)if(ci[i])ci[i]->Release();
    }
    ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
    if(!capsOnly)ctx->ClearDepthStencilView(zview.Get(),D3D11_CLEAR_DEPTH,1,0);
    else if(ownDepth)ctx->ClearDepthStencilView(zview.Get(),D3D11_CLEAR_DEPTH,reverseDepth?0.f:1.f,0);
    auto* out=output.Get();ctx->OMSetRenderTargets(1,&out,capsOnly&&!ownDepth?nativeDepth:zview.Get());D3D11_VIEWPORT full{0,0,float(td.Width),float(td.Height),0,1};ctx->RSSetViewports(1,&full);
    ID3D11DepthStencilState* capState=ownDepth?(reverseDepth?reversedDepthWrite.Get():depth.Get()):(reverseDepth?reversedCapDepth.Get():hologramDepth.Get());
    ctx->RSSetState(raster.Get());ctx->OMSetDepthStencilState(capsOnly?capState:depth.Get(),0);ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    auto* vb=interior.vertices.Get();const UINT vertexStride=sizeof(CockpitVertex),zero=0;ctx->IASetVertexBuffers(0,1,&vb,&vertexStride,&zero);
    ctx->IASetInputLayout(layout.Get());ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->GSSetShader(nullptr,nullptr,0);ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);
    ID3D11ShaderResourceView* inputs[]={atlas[0].Get(),atlas[1].Get(),atlas[2].Get(),hudView.Get(),cropView.Get(),atlas[3].Get(),lighting.cubes.Get(),lighting.grid.Get(),ownDepth?capWorldDepth.Get():nullptr};ctx->PSSetShaderResources(0,9,inputs);
    auto* smp=sampler.Get();ctx->PSSetSamplers(0,1,&smp);
    smp=environmentSampler.Get();ctx->PSSetSamplers(1,1,&smp);
    auto* cb=constants.Get();ctx->VSSetConstantBuffers(0,1,&cb);ctx->PSSetConstantBuffers(0,1,&cb);
    ID3D11Buffer* lightBuffers[]={lighting.system.Get(),lighting.extra.Get(),lighting.environment.Get()};ctx->PSSetConstantBuffers(1,3,lightBuffers);
    if((capsOnly||!frame)&&pose.rig.capCount&&(pose.rig.kind!=CockpitKind::Barga||pose.rig.capVariant<2)) {
        const auto cabin=data.model;
        for(unsigned part=0;part<pose.rig.capCount&&part<5;++part) {
            if(!ValidCamera(pose.rig.capFrames[part]))continue;
            const auto index=CapIndex(pose.rig,part);if(index>=kCockpitCapMeshes)continue;
            const auto& cap=caps[index];data.model=pose.rig.capFrames[part];
            ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);vb=cap.vertices.Get();ctx->IASetVertexBuffers(0,1,&vb,&vertexStride,&zero);
            ctx->Draw(cap.count,0);
        }
        // The combat lid, in the hull bone's frame (the first cap frame). With
        // its own depth only: without, the hull's far faces paint over its near.
        if((IsCombatKind(pose.rig.kind)||IsHeliKind(pose.rig.kind))&&lid.vertices&&meshRig.model==pose.rig.model&&(ownDepth||!capsOnly)&&ValidCamera(pose.rig.capFrames[0])) {
            const float painted=data.paint[3];if(painted>.5f)data.paint[3]=2;
            data.model=pose.rig.capFrames[0];ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
            vb=lid.vertices.Get();ctx->IASetVertexBuffers(0,1,&vb,&vertexStride,&zero);ctx->Draw(lid.count,0);++stats.lidDraws;
            data.paint[3]=painted;
        }
        data.model=cabin;ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
        vb=interior.vertices.Get();ctx->IASetVertexBuffers(0,1,&vb,&vertexStride,&zero);
    }
    if(!capsOnly) {
        ctx->Draw(interior.vertexCount-interior.seamCount,0);
        ctx->OMSetBlendState(hologramBlend.Get(),nullptr,~0u);ctx->OMSetDepthStencilState(hologramDepth.Get(),0);
        ctx->Draw(interior.seamCount+interior.displayCount,interior.vertexCount-interior.seamCount);if(hudView)++stats.instrumentFrames;++stats.interiors;
    }
    stats.textured=atlasReady;stats.nativeLights=data.materials[1]>.5f;stats.environment=data.materials[2]>.5f;
    ID3D11ShaderResourceView* empty[9]{};ctx->PSSetShaderResources(0,9,empty);
    ctx->OMSetRenderTargets(8,rt,oldDsv.Get());cb=oldCb.Get();ctx->VSSetConstantBuffers(0,1,&cb);vb=oldVb.Get();ctx->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    ctx->PSSetShaderResources(0,9,oldInputs);smp=oldSampler.Get();ctx->PSSetSamplers(0,1,&smp);cb=oldPixelCb.Get();ctx->PSSetConstantBuffers(0,1,&cb);
    smp=oldEnvSampler.Get();ctx->PSSetSamplers(1,1,&smp);ctx->PSSetConstantBuffers(1,3,oldLighting);
    for(auto* b:oldLighting)if(b)b->Release();
    for(auto* srv:oldInputs)if(srv)srv->Release();
    ctx->RSSetViewports(vpCount,vp);ctx->RSSetState(oldRaster.Get());ctx->OMSetDepthStencilState(oldDepth.Get(),ref);ctx->OMSetBlendState(oldBlend.Get(),factors,mask);
    ctx->IASetInputLayout(oldLayout.Get());ctx->IASetPrimitiveTopology(topology);ctx->VSSetShader(oldVs.Get(),vi,vn);ctx->PSSetShader(oldPs.Get(),pi,pn);ctx->GSSetShader(oldGs.Get(),gi,gn);
    ctx->HSSetShader(oldHs.Get(),hi,hn);ctx->DSSetShader(oldDs.Get(),di,dn);
    for(auto* t:rt)if(t)t->Release();for(UINT i=0;i<vn;++i)if(vi[i])vi[i]->Release();for(UINT i=0;i<pn;++i)if(pi[i])pi[i]->Release();
    for(UINT i=0;i<gn;++i)if(gi[i])gi[i]->Release();for(UINT i=0;i<hn;++i)if(hi[i])hi[i]->Release();for(UINT i=0;i<dn;++i)if(di[i])di[i]->Release();
    return true;
}
bool DrawCockpit(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const Matrix& view,const Matrix& projection,const CockpitPose& pose,ID3D11Texture2D* hud,std::uint64_t frame) noexcept {
    return DrawCockpitPass(ctx,target,view,projection,pose,hud,frame,nullptr,false,false);
}
bool DrawCockpitJointCaps(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const Matrix& view,const Matrix& projection,const CockpitPose& pose,ID3D11DepthStencilView* nativeDepth,bool reversed) noexcept {
    return DrawCockpitPass(ctx,target,view,projection,pose,nullptr,0,nativeDepth,true,reversed);
}
bool DrawNativeBargaCaps(ID3D11DeviceContext* ctx,ID3D11Texture2D* target,const Matrix& view,const Matrix& projection,const CockpitPose& pose,std::uint64_t frame,unsigned eye) noexcept {
    // Resource creation belongs to Present. Do not compile/decode between the
    // first cold pair's eyes. The next pair uses the prepared immutable data.
    if(!vs||!ps||!constants||nativeCapDepth.frame!=frame||nativeCapDepth.eye!=static_cast<int>(eye))return false;
    return DrawCockpitPass(ctx,target,view,projection,pose,nullptr,frame,nativeCapDepth.view.Get(),true,nativeCapDepth.reversed);
}
CockpitLimbScope::CockpitLimbScope(const CockpitRig& rig) noexcept:previous(scopedRig) {scopedRig=&rig;}
CockpitLimbScope::~CockpitLimbScope() noexcept {scopedRig=previous;}
bool CockpitLimbIntercept(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base) noexcept {
    if(!scopedRig||!ctx||!count||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    if(scopedRig->capCount&&NativeWorldRenderEye()>=0) {
        ComPtr<ID3D11DepthStencilView> dsv;ctx->OMGetRenderTargets(0,nullptr,&dsv);
        ComPtr<ID3D11DepthStencilState> state;UINT reference=0;ctx->OMGetDepthStencilState(&state,&reference);
        D3D11_DEPTH_STENCIL_DESC desc{};if(state)state->GetDesc(&desc);
        if(dsv&&(!state||(desc.DepthEnable&&desc.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL))) {
            nativeCapDepth.view=dsv;nativeCapDepth.frame=NativeWorldRenderFrame();nativeCapDepth.eye=NativeWorldRenderEye();
            nativeCapDepth.reversed=state&&(desc.DepthFunc==D3D11_COMPARISON_GREATER||desc.DepthFunc==D3D11_COMPARISON_GREATER_EQUAL);
        }
    }
    ComPtr<ID3D11Device> drawDevice;ctx->GetDevice(&drawDevice);
    // Limb selection uses native shaders and needs no cabin draw resources.
    // Compile/decode only at Present in DrawCockpit, after both native eyes and
    // their rendered head pose have been accepted; never stall between eyes.
    if(device&&device!=drawDevice)ReleaseCockpitDraw();device=drawDevice;
    if(meshRig.model!=scopedRig->model||meshRig.resource!=scopedRig->resource||meshRig.nodes!=scopedRig->nodes||meshRig.nodeCount!=scopedRig->nodeCount||
       meshRig.kind!=scopedRig->kind||meshRig.bodyBone!=scopedRig->bodyBone||meshRig.limb!=scopedRig->limb||
       meshRig.cutCount!=scopedRig->cutCount||meshRig.cutBones!=scopedRig->cutBones||meshRig.cutZ!=scopedRig->cutZ||
       meshRig.cutPosition60!=scopedRig->cutPosition60||meshRig.cutPosition68!=scopedRig->cutPosition68) {
        meshes.clear();meshRig=*scopedRig;stats.meshes=stats.keptTriangles=stats.removedTriangles=0;
        ResetLid();
    }
    ComPtr<ID3D11Buffer> vb,ib;UINT stride=0,vo=0,io=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    ctx->IAGetVertexBuffers(0,1,&vb,&stride,&vo);ctx->IAGetIndexBuffer(&ib,&format,&io);
    D3D11_PRIMITIVE_TOPOLOGY topology{};ctx->IAGetPrimitiveTopology(&topology);
    if(!vb||!ib||stride<24||stride>256||count%3||count>3000000||topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST||
        (format!=DXGI_FORMAT_R16_UINT&&format!=DXGI_FORMAT_R32_UINT))return false;
    Mesh* mesh=nullptr;
    for(auto& m:meshes) if(m.vb==vb&&m.ib==ib&&m.stride==stride&&m.vo==vo&&m.io==io&&m.count==count&&m.start==start&&m.base==base&&m.format==format) {mesh=&m;break;}
    if(!mesh) {
        if(meshes.size()>=32)return false;
        Mesh m{};m.vb=vb;m.ib=ib;m.stride=stride;m.vo=vo;m.io=io;m.count=count;m.start=start;m.base=base;m.format=format;
        // EDF6 blends even its solid materials; what writes depth is solid.
        ComPtr<ID3D11DepthStencilState> depthState;UINT stencilRef=0;ctx->OMGetDepthStencilState(&depthState,&stencilRef);
        D3D11_DEPTH_STENCIL_DESC dd{};if(depthState)depthState->GetDesc(&dd);
        m.opaque=!depthState||(dd.DepthEnable&&dd.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL);
        ComPtr<ID3D11Device> d;ctx->GetDevice(&d);
        if(!Staging(d.Get(),ctx,vb.Get(),m.vstage)||!Staging(d.Get(),ctx,ib.Get(),m.istage))return false;
        meshes.push_back(std::move(m));mesh=&meshes.back();stats.meshes=static_cast<unsigned>(meshes.size());
    }
    if(mesh->stage==Mesh::Pending) {
        if(++mesh->tries>600) {mesh->stage=Mesh::Unsupported;mesh->vstage.Reset();mesh->istage.Reset();++stats.unsupported;}
        else BuildMesh(ctx,*mesh);
        if(mesh->stage==Mesh::Pending) {++stats.pending;return false;} // native until validated
    }
    if(mesh->stage!=Mesh::Ready)return false;
    // A GPU readback can finish between the eyes. Begin using its selection
    // only on the NEXT native pair, so both eyes see the same warm-up geometry.
    if(mesh->readyFrame&&mesh->readyFrame==NativeWorldRenderFrame())return false;
    if(mesh->keptCount) {ctx->IASetIndexBuffer(mesh->kept.Get(),DXGI_FORMAT_R32_UINT,0);ctx->DrawIndexed(mesh->keptCount,0,base);ctx->IASetIndexBuffer(ib.Get(),format,io);}
    ++stats.filtered;return true;
}
namespace {
thread_local bool glassScope=false;
std::atomic<std::uint64_t> glassSeen{0},glassSkipped{0};
}
TruckGlassScope::TruckGlassScope() noexcept:previous(glassScope) {glassScope=true;}
TruckGlassScope::~TruckGlassScope() noexcept {glassScope=previous;}
bool TruckGlassIntercept(ID3D11DeviceContext* ctx,UINT count) noexcept {
    if(!glassScope||!ctx)return false;
    glassSeen.fetch_add(1,std::memory_order_relaxed);
    if(count>600)return false;   // the glass: 204 or 408 indices; a body's are thousands
    ComPtr<ID3D11DepthStencilState> state;UINT reference=0;ctx->OMGetDepthStencilState(&state,&reference);
    if(!state)return false;
    D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);
    // EDF6 blends even its solid materials; what writes depth is solid.
    if(!desc.DepthEnable||desc.DepthWriteMask==D3D11_DEPTH_WRITE_MASK_ALL)return false;
    glassSkipped.fetch_add(1,std::memory_order_relaxed);return true;
}
void TruckGlassCounts(std::uint64_t& seen,std::uint64_t& skipped) noexcept {
    seen=glassSeen.load(std::memory_order_relaxed);skipped=glassSkipped.load(std::memory_order_relaxed);
}
CockpitDrawStats ReadCockpitDrawStats() noexcept {return stats;} // render thread
}
