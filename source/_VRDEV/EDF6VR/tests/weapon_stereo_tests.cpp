#include "weapon_stereo.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace edf6vr;
static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
using Matrix=std::array<float,16>;
Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix c{};
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) for(int k=0;k<4;++k) c[y*4+x]+=a[y*4+k]*b[k*4+x];
    return c;
}
Matrix Inverse(const Matrix& a) {
    double m[4][8]{};
    for(int y=0;y<4;++y) for(int x=0;x<4;++x) { m[y][x]=a[y*4+x]; m[y][x+4]=x==y?1:0; }
    for(int i=0;i<4;++i) {
        int row=i; for(int j=i+1;j<4;++j) if(std::fabs(m[j][i])>std::fabs(m[row][i])) row=j;
        for(int x=0;x<8;++x) std::swap(m[i][x],m[row][x]);
        const auto d=m[i][i]; for(auto& v:m[i]) v/=d;
        for(int j=0;j<4;++j) if(j!=i) { const auto f=m[j][i]; for(int x=0;x<8;++x) m[j][x]-=m[i][x]*f; }
    }
    Matrix r{}; for(int y=0;y<4;++y) for(int x=0;x<4;++x) r[y*4+x]=float(m[y][x+4]); return r;
}
void Report(const char* s) { puts(s); }
std::vector<unsigned char> Read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary); return {(std::istreambuf_iterator<char>(f)),{}};
}
void Run(bool lighting=false,bool skipLighting=false,bool live=false,bool typeless=false,bool multiple=false) {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> ctx;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
    if(!device) return;
    constexpr UINT w=256,h=128;
    D3D11_TEXTURE2D_DESC td{}; td.Width=w; td.Height=h; td.MipLevels=td.ArraySize=1;
    td.SampleDesc={1,0}; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> tex[7]; ComPtr<ID3D11RenderTargetView> rtv[7];
    ComPtr<ID3D11ShaderResourceView> materialSrv[7];
    ID3D11RenderTargetView* rt[7]{};
    for(unsigned i=0;i<7;++i) {
        auto materialDesc=td; if(i==4) materialDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        CHECK(SUCCEEDED(device->CreateTexture2D(&materialDesc,nullptr,&tex[i])));
        CHECK(SUCCEEDED(device->CreateRenderTargetView(tex[i].Get(),nullptr,&rtv[i])));
        CHECK(SUCCEEDED(device->CreateShaderResourceView(tex[i].Get(),nullptr,&materialSrv[i])));
        rt[i]=rtv[i].Get(); const float black[4]{}; ctx->ClearRenderTargetView(rt[i],black);
    }
    // A nearer world prepass hides both triangles in the native Equal pass.
    // Stereo replay must use fresh weapon-only depth and still show them.
    auto depthDesc=td; depthDesc.Format=DXGI_FORMAT_D32_FLOAT; depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> worldDepth; ComPtr<ID3D11DepthStencilView> worldDsv;
    device->CreateTexture2D(&depthDesc,nullptr,&worldDepth); device->CreateDepthStencilView(worldDepth.Get(),nullptr,&worldDsv);
    ctx->ClearDepthStencilView(worldDsv.Get(),D3D11_CLEAR_DEPTH,0,0);
    ctx->OMSetRenderTargets(7,rt,worldDsv.Get());
    D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1}; ctx->RSSetViewports(1,&vp);
    D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthEnable=TRUE; dd.DepthFunc=D3D11_COMPARISON_EQUAL;
    ComPtr<ID3D11DepthStencilState> depth; device->CreateDepthStencilState(&dd,&depth); ctx->OMSetDepthStencilState(depth.Get(),77);
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster; device->CreateRasterizerState(&rd,&raster); ctx->RSSetState(raster.Get());
    const char code[]=R"(
cbuffer System:register(b0) { float4 rows[38]; }
struct Vertex { float4 p:SV_Position; float3 v:TEXCOORD0; };
Vertex vs(float3 p:POSITION) { float4 v=float4(p,1); Vertex o; o.p=float4(dot(rows[12],v),dot(rows[13],v),dot(rows[14],v),dot(rows[15],v)); o.v=float3(dot(rows[0],v),dot(rows[1],v),dot(rows[2],v)); return o; }
struct Out { float4 colour:SV_Target0; float4 spec:SV_Target1; float4 normal:SV_Target2;
float4 rough:SV_Target3; float4 view:SV_Target4; float4 light:SV_Target5; float4 lightSpec:SV_Target6; };
Out ps(Vertex v,uint id:SV_PrimitiveID) { Out o; o.colour=v.v.z> -1.5?float4(1,0,0,1):float4(0,1,0,1); o.normal=float4(0,0,1,rows[33].w); o.spec=.2; o.rough=.4; o.view=float4(v.v,1); o.light=0; o.lightSpec=0; return o; }
[numthreads(1,1,1)] void cs() {}
)";
    ComPtr<ID3DBlob> vb,pb,csb,errors;
    CHECK(SUCCEEDED(D3DCompile(code,sizeof(code),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vb,&errors)));
    CHECK(SUCCEEDED(D3DCompile(code,sizeof(code),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&pb,&errors)));
    CHECK(SUCCEEDED(D3DCompile(code,sizeof(code),nullptr,nullptr,nullptr,"cs","cs_5_0",0,0,&csb,&errors)));
    if(!vb || !pb || !csb) return;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps; ComPtr<ID3D11ComputeShader> cs;
    device->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs);
    device->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps);
    device->CreateComputeShader(csb->GetBufferPointer(),csb->GetBufferSize(),nullptr,&cs);
    ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(ps.Get(),nullptr,0); ctx->CSSetShader(cs.Get(),nullptr,0);
    const Matrix view={.8f,0,.6f,-3, 0,1,0,-2, -.6f,0,.8f,4, 0,0,0,1};
    const Matrix projection={1,0,0,0, 0,1,0,0, 0,0,-1.0001f,-.10001f, 0,0,-1,0};
    const Matrix inverse=Inverse(view),vpMat=Multiply(projection,view),invVp=Inverse(vpMat);
    std::array<float,152> constants{};
    std::memcpy(constants.data(),view.data(),64); std::memcpy(constants.data()+16,inverse.data(),64);
    std::memcpy(constants.data()+32,projection.data(),64); std::memcpy(constants.data()+48,vpMat.data(),64);
    std::memcpy(constants.data()+64,invVp.data(),64);
    for(unsigned i=80;i<152;++i) constants[i]=float(i)*.017f;
    constants[135]=1;
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=608; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data{constants.data(),0,0};
    ComPtr<ID3D11Buffer> cb,badCb; device->CreateBuffer(&bd,&data,&cb); device->CreateBuffer(&bd,&data,&badCb);
    auto* c=cb.Get(); ctx->VSSetConstantBuffers(0,1,&c); ctx->PSSetConstantBuffers(0,1,&c); ctx->CSSetConstantBuffers(0,1,&c);
    // Populated compute SRV/UAV must survive both eye transforms.
    bd={}; bd.ByteWidth=16; bd.StructureByteStride=16; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    ComPtr<ID3D11Buffer> sb,ub; ComPtr<ID3D11ShaderResourceView> srv; ComPtr<ID3D11UnorderedAccessView> uav;
    bd.BindFlags=D3D11_BIND_SHADER_RESOURCE; device->CreateBuffer(&bd,nullptr,&sb); device->CreateShaderResourceView(sb.Get(),nullptr,&srv);
    bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS; device->CreateBuffer(&bd,nullptr,&ub); device->CreateUnorderedAccessView(ub.Get(),nullptr,&uav);
    auto* s=srv.Get(); auto* u=uav.Get(); ctx->CSSetShaderResources(0,1,&s); ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    const float cameraVertices[6][3]={{-.12f,.28f,-1},{0,.52f,-1},{.12f,.28f,-1},{-.24f,-1.04f,-2},{0,-.56f,-2},{.24f,-1.04f,-2}};
    float vertices[6][3]{};
    for(int i=0;i<6;++i) for(int y=0;y<3;++y) {
        vertices[i][y]=inverse[y*4+3]; for(int k=0;k<3;++k) vertices[i][y]+=inverse[y*4+k]*cameraVertices[i][k];
    }
    bd={}; bd.ByteWidth=sizeof(vertices); bd.BindFlags=D3D11_BIND_VERTEX_BUFFER; data.pSysMem=vertices;
    ComPtr<ID3D11Buffer> vertex,index; device->CreateBuffer(&bd,&data,&vertex);
    const unsigned indices[]={0,1,2,3,4,5}; bd.ByteWidth=sizeof(indices); bd.BindFlags=D3D11_BIND_INDEX_BUFFER; data.pSysMem=indices;
    device->CreateBuffer(&bd,&data,&index);
    D3D11_INPUT_ELEMENT_DESC input{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    ComPtr<ID3D11InputLayout> layout;
    CHECK(SUCCEEDED(device->CreateInputLayout(&input,1,vb->GetBufferPointer(),vb->GetBufferSize(),&layout)));
    auto* v=vertex.Get(); UINT stride=12,offset=0; ctx->IASetVertexBuffers(0,1,&v,&stride,&offset);
    ctx->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R32_UINT,0); ctx->IASetInputLayout(layout.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const auto dir=std::filesystem::temp_directory_path()/(L"EDF6VR-stereo-"+std::to_wstring(GetCurrentProcessId())+(lighting?L"-lit":L"")+(skipLighting?L"-skip":L""));
    ConfigureWeaponStereoCapture(false,dir.c_str(),Report,0);
    CHECK(!BeginWeaponStereoCapture(ctx.Get(),1,w,h));
    ConfigureWeaponStereoCapture(true,dir.c_str(),Report,0,lighting);
    if(live) { ConfigureWeaponStereoLive(true); SetWeaponStereoEyes(0,.064f); }
    if(multiple) {
        const float wrongAnchor[3]={900,800,700}; SetWeaponStereoOrigin(wrongAnchor,0);
        SetWeaponStereoNativeCamera(0,.064f); // Must discard the previous class's hand anchor.
    }
    ComPtr<ID3D11Buffer> overlapVertex;
    if(multiple) {
        float farther[3][3]{};
        for(int i=0;i<3;++i) for(int y=0;y<3;++y) {
            farther[i][y]=inverse[y*4+3];
            for(int k=0;k<3;++k) farther[i][y]+=inverse[y*4+k]*cameraVertices[i][k]*2;
        }
        D3D11_BUFFER_DESC d{}; d.ByteWidth=sizeof(farther); d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{farther,0,0};
        CHECK(SUCCEEDED(device->CreateBuffer(&d,&initial,&overlapVertex)));
    }
    for(unsigned frame=0;frame<(live?4u:1u);++frame) {
    const unsigned phase=live && frame>0?frame-1:frame;
    if(live) {
        if(typeless) {
            // A camera from a different walking tick is +/-10cm away, while
            // weapon vertices and their published centre remain unchanged.
            const float centre[3]={inverse[3],inverse[7],inverse[11]};
            SetWeaponStereoOrigin(centre,0);
            auto displaced=view; displaced[3]+=(frame%2?.1f:-.1f); displaced[7]+=.05f;
            const Matrix matrices[]={displaced,Inverse(displaced),projection,Multiply(projection,displaced),Inverse(Multiply(projection,displaced))};
            for(unsigned m=0;m<5;++m) std::memcpy(constants.data()+m*16,matrices[m].data(),64);
            ctx->UpdateSubresource(cb.Get(),0,nullptr,constants.data(),0,0);
        }
        if(frame==1) {
            FinishWeaponStereoFrame(); // VR-off/menu: no weapon drawn this frame
            CHECK(WeaponStereoLive() && WeaponStereoCaptureWanted());
            ctx->OMSetRenderTargets(0,nullptr,worldDsv.Get());
            CHECK(!BeginWeaponDepthIsolation(ctx.Get())); // re-enable must warm up again
        }
        ID3D11ShaderResourceView* none[7]{}; ctx->CSSetShaderResources(0,7,none);
        ctx->CSSetShader(cs.Get(),nullptr,0); ctx->CSSetConstantBuffers(0,1,&c);
        ctx->CSSetShaderResources(0,1,&s); ctx->CSSetUnorderedAccessViews(0,1,&u,nullptr);
        ctx->OMSetRenderTargets(0,nullptr,worldDsv.Get());
        const bool isolated=BeginWeaponDepthIsolation(ctx.Get()); CHECK(isolated==(phase>0));
        if(isolated) {
            ComPtr<ID3D11DepthStencilView> sink; ctx->OMGetRenderTargets(0,nullptr,&sink);
            CHECK(sink.Get()!=worldDsv.Get()); CHECK(InWeaponLayerCapture());
            ctx->ClearDepthStencilView(sink.Get(),D3D11_CLEAR_DEPTH,.75f,0);
            EndWeaponDepthIsolation(); CHECK(!InWeaponLayerCapture());
            sink.Reset(); ctx->OMGetRenderTargets(0,nullptr,&sink); CHECK(sink.Get()==worldDsv.Get());
            auto readDesc=depthDesc; readDesc.Usage=D3D11_USAGE_STAGING; readDesc.BindFlags=0; readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> readDepth; device->CreateTexture2D(&readDesc,nullptr,&readDepth);
            ctx->CopyResource(readDepth.Get(),worldDepth.Get()); D3D11_MAPPED_SUBRESOURCE m{};
            CHECK(SUCCEEDED(ctx->Map(readDepth.Get(),0,D3D11_MAP_READ,0,&m)));
            if(m.pData) { CHECK(*static_cast<float*>(m.pData)==0); ctx->Unmap(readDepth.Get(),0); }
        }
        ctx->OMSetRenderTargets(7,rt,worldDsv.Get());
    }
    CHECK(!BeginWeaponStereoCapture(ctx.Get(),0,w,h));
    CHECK(WeaponStereoCaptureWanted(multiple));
    const bool began=BeginWeaponStereoCapture(ctx.Get(),1,w,h,multiple); CHECK(began); if(!began) return;
    D3D11_QUERY_DESC qd{D3D11_QUERY_PIPELINE_STATISTICS,0}; ComPtr<ID3D11Query> query; device->CreateQuery(&qd,&query);
    ctx->Begin(query.Get()); DrawWeaponStereo(ctx.Get(),multiple?3:6,0,0);
    if(multiple) {
        EndWeaponStereoCapture();
        CHECK(!WeaponStereoCaptureWanted());
        CHECK(WeaponStereoCaptureWanted(true)); // tracked-hand caller must reach append
        CHECK(!BeginWeaponStereoCapture(ctx.Get(),1,w,h)); // old single-weapon API cannot join
        CHECK(!BeginWeaponStereoCapture(ctx.Get(),1,w+1,h,true));
        ID3D11RenderTargetView* different[7]{}; for(unsigned i=0;i<7;++i) different[i]=rt[i];
        different[0]=rt[1]; different[1]=rt[0]; ctx->OMSetRenderTargets(7,different,worldDsv.Get());
        CHECK(!BeginWeaponStereoCapture(ctx.Get(),1,w,h,true));
        ctx->OMSetRenderTargets(7,rt,worldDsv.Get());
        CHECK(BeginWeaponStereoCapture(ctx.Get(),1,w,h,true));
        // A farther part covering the first model must NOT erase its near red
        // triangle. The separate green triangle must also survive composition.
        auto* fartherBuffer=overlapVertex.Get(); ctx->IASetVertexBuffers(0,1,&fartherBuffer,&stride,&offset);
        DrawWeaponStereo(ctx.Get(),3,0,0);
        ctx->IASetVertexBuffers(0,1,&v,&stride,&offset); DrawWeaponStereo(ctx.Get(),3,3,0);
    }
    auto* bad=badCb.Get(); ctx->PSSetConstantBuffers(0,1,&bad);
    if(!live) DrawWeaponStereo(ctx.Get(),6,0,0); // mismatch: native once, no stereo
    ctx->PSSetConstantBuffers(0,1,&c); ctx->End(query.Get());
    EndWeaponStereoCapture(); CHECK(!InWeaponStereoReplay()); CHECK(!InWeaponLayerCapture());
    ComPtr<ID3D11Buffer> vc,pc,cc; ctx->VSGetConstantBuffers(0,1,&vc); ctx->PSGetConstantBuffers(0,1,&pc); ctx->CSGetConstantBuffers(0,1,&cc);
    CHECK(vc.Get()==cb.Get() && pc.Get()==cb.Get() && cc.Get()==cb.Get());
    ComPtr<ID3D11ShaderResourceView> ss; ComPtr<ID3D11UnorderedAccessView> uu; ComPtr<ID3D11ComputeShader> css;
    ctx->CSGetShaderResources(0,1,&ss); ctx->CSGetUnorderedAccessViews(0,1,&uu); ctx->CSGetShader(&css,nullptr,nullptr);
    CHECK(ss.Get()==srv.Get() && uu.Get()==uav.Get() && css.Get()==cs.Get());
    ID3D11RenderTargetView* rr[7]{}; ComPtr<ID3D11DepthStencilView> restoredDsv;
    ctx->OMGetRenderTargets(7,rr,&restoredDsv); CHECK(restoredDsv.Get()==worldDsv.Get());
    for(unsigned i=0;i<7;++i) { CHECK(rr[i]==rt[i]); if(rr[i]) rr[i]->Release(); }
    UINT ref=0; ComPtr<ID3D11DepthStencilState> dss; ctx->OMGetDepthStencilState(&dss,&ref); CHECK(dss.Get()==depth.Get() && ref==77);
    if(lighting && !skipLighting) {
        const char lightCode[]=R"(
cbuffer System:register(b0) { float4 rows[38]; }
Texture2D<float4> albedo:register(t0); Texture2D<float4> normal:register(t2); Texture2D<float4> position:register(t4);
Texture2D<float2> ao:register(t9); SamplerState aoSampler:register(s11);
RWTexture2D<float4> output:register(u0);
[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID) {
 uint w,h; albedo.GetDimensions(w,h); if(id.x>=w || id.y>=h) return;
 float4 p=position[id.xy]; float2 ndc=float2(dot(rows[8],p),dot(rows[9],p))/-p.z;
 float2 expected=float2((id.x+.5)*2/w-1,1-(id.y+.5)*2/h);
 float mask=normal[id.xy].w; if(mask && any(abs(ndc-expected)>.005)) mask=0;
 uint aw,ah; ao.GetDimensions(aw,ah);
 float ambient=ao.SampleLevel(aoSampler,float2(id.xy)/(2*float2(aw,ah)),0).y;
 output[id.xy]=float4(albedo[id.xy].rg*rows[33].w*ambient,rows[0].w+3.1,mask);
})";
        ComPtr<ID3DBlob> lightBlob; CHECK(SUCCEEDED(D3DCompile(lightCode,sizeof(lightCode),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&lightBlob,&errors)));
        ComPtr<ID3D11ComputeShader> lightShader;
        if(lightBlob) device->CreateComputeShader(lightBlob->GetBufferPointer(),lightBlob->GetBufferSize(),nullptr,&lightShader);
        auto outputDesc=td; outputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; outputDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> litTarget; ComPtr<ID3D11UnorderedAccessView> litView;
        device->CreateTexture2D(&outputDesc,nullptr,&litTarget); device->CreateUnorderedAccessView(litTarget.Get(),nullptr,&litView);
        auto lightConstants=constants; lightConstants[135]=.25f; // later lighting-stage data, not geometry's 1.0
        D3D11_BUFFER_DESC lcd{}; lcd.ByteWidth=608; lcd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA lcData{lightConstants.data(),0,0}; ComPtr<ID3D11Buffer> lightCb;
        device->CreateBuffer(&lcd,&lcData,&lightCb);
        ctx->OMSetRenderTargets(0,nullptr,nullptr);
        ID3D11ShaderResourceView* inputs[7]{}; for(unsigned i=0;i<7;++i) inputs[i]=materialSrv[i].Get();
        auto* lo=litView.Get(); auto* lc=lightCb.Get();
        auto aoDesc=td; aoDesc.Width=aoDesc.Height=1; aoDesc.Format=DXGI_FORMAT_R32G32_FLOAT; aoDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        const float aoValue[2]={live?0.f:1.f,live?0.f:1.f}; D3D11_SUBRESOURCE_DATA aoData{aoValue,sizeof(aoValue),0};
        ComPtr<ID3D11Texture2D> aoTexture; ComPtr<ID3D11ShaderResourceView> aoView; ComPtr<ID3D11SamplerState> aoSampler;
        CHECK(SUCCEEDED(device->CreateTexture2D(&aoDesc,&aoData,&aoTexture)));
        CHECK(SUCCEEDED(device->CreateShaderResourceView(aoTexture.Get(),nullptr,&aoView)));
        D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=live?D3D11_TEXTURE_ADDRESS_BORDER:D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc=D3D11_COMPARISON_ALWAYS; sampler.MaxLOD=D3D11_FLOAT32_MAX;
        CHECK(SUCCEEDED(device->CreateSamplerState(&sampler,&aoSampler)));
        auto* aoPtr=aoView.Get(); auto* samplerPtr=aoSampler.Get();
        ctx->CSSetShaderResources(9,1,&aoPtr); ctx->CSSetSamplers(11,1,&samplerPtr);
        ctx->CSSetShader(lightShader.Get(),nullptr,0); ctx->CSSetConstantBuffers(0,1,&lc); ctx->CSSetUnorderedAccessViews(0,1,&lo,nullptr);
        inputs[6]=nullptr; ctx->CSSetShaderResources(0,7,inputs);
        DispatchWeaponLighting(ctx.Get(),w/16,h/16,1); // only 6 matching resources: must reject
        inputs[6]=materialSrv[6].Get(); ctx->CSSetShaderResources(0,7,inputs);
        DispatchWeaponLighting(ctx.Get(),w/16,h/16,1);
        ID3D11ShaderResourceView* restored[7]{}; ctx->CSGetShaderResources(0,7,restored);
        for(unsigned i=0;i<7;++i) { CHECK(restored[i]==inputs[i]); if(restored[i]) restored[i]->Release(); }
        css.Reset(); cc.Reset(); uu.Reset(); ctx->CSGetShader(&css,nullptr,nullptr); ctx->CSGetConstantBuffers(0,1,&cc); ctx->CSGetUnorderedAccessViews(0,1,&uu);
        CHECK(css.Get()==lightShader.Get() && cc.Get()==lightCb.Get() && uu.Get()==litView.Get());
        ComPtr<ID3D11ShaderResourceView> restoredAo; ComPtr<ID3D11SamplerState> restoredSampler;
        ctx->CSGetShaderResources(9,1,&restoredAo); ctx->CSGetSamplers(11,1,&restoredSampler);
        CHECK(restoredAo.Get()==aoView.Get() && restoredSampler.Get()==aoSampler.Get());
    }
    PollWeaponStereoCapture(ctx.Get()); // present boundary ends unmatched lighting before test-only Flush
    if(live) {
        if(skipLighting) { CHECK(!WeaponStereoLive()); ConfigureWeaponStereoCapture(false,nullptr,Report); return; }
        double centres[2]{};
        for(unsigned eye=0;eye<2;++eye) {
            auto targetDesc=td; if(eye) targetDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            if(typeless) targetDesc.Format=eye?DXGI_FORMAT_B8G8R8A8_TYPELESS:DXGI_FORMAT_R8G8B8A8_TYPELESS;
            ComPtr<ID3D11Texture2D> target; ComPtr<ID3D11RenderTargetView> targetView;
            D3D11_RENDER_TARGET_VIEW_DESC targetViewDesc{}; targetViewDesc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            targetViewDesc.Format=typeless?(eye?DXGI_FORMAT_B8G8R8A8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM):targetDesc.Format;
            CHECK(SUCCEEDED(device->CreateTexture2D(&targetDesc,nullptr,&target)));
            CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(),&targetViewDesc,&targetView)));
            const float background[4]={0,0,0,.6f}; ctx->ClearRenderTargetView(targetView.Get(),background);
            // Frame 2 deliberately fails the right-eye submission: no cached pair next frame.
            const bool composite=phase==2 && eye==1?false:CompositeWeaponStereo(ctx.Get(),target.Get(),eye);
            CHECK(composite==(phase>0 && !(phase==2 && eye==1)));
            if(!composite) continue;
            ComPtr<ID3D11VertexShader> restoredVs; ComPtr<ID3D11PixelShader> restoredPs;
            ctx->VSGetShader(&restoredVs,nullptr,nullptr); ctx->PSGetShader(&restoredPs,nullptr,nullptr);
            CHECK(restoredVs.Get()==vs.Get() && restoredPs.Get()==ps.Get());
            ComPtr<ID3D11InputLayout> restoredLayout; ctx->IAGetInputLayout(&restoredLayout); CHECK(restoredLayout.Get()==layout.Get());
            D3D11_VIEWPORT restoredVp{}; UINT n=1; ctx->RSGetViewports(&n,&restoredVp); CHECK(std::memcmp(&restoredVp,&vp,sizeof(vp))==0);
            targetDesc.Usage=D3D11_USAGE_STAGING; targetDesc.BindFlags=0; targetDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> readTarget; device->CreateTexture2D(&targetDesc,nullptr,&readTarget); ctx->CopyResource(readTarget.Get(),target.Get());
            D3D11_MAPPED_SUBRESOURCE m{}; CHECK(SUCCEEDED(ctx->Map(readTarget.Get(),0,D3D11_MAP_READ,0,&m)));
            unsigned covered=0,nearCount=0,farCount=0;
            if(m.pData) {
                for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
                    const auto* p=static_cast<const unsigned char*>(m.pData)+y*m.RowPitch+x*4;
                    CHECK(p[3]==153); // preserve destination alpha
                    const unsigned red=typeless && eye?2u:0u,blue=typeless && eye?0u:2u;
                    if(p[red] || p[1]) { ++covered; CHECK(std::abs(int(std::max(p[red],p[1]))-137)<=1); if(p[red]) { centres[eye]+=x+.5; ++nearCount; } if(p[1]) ++farCount; }
                    else CHECK(p[blue]==0); // native lighting writes blue everywhere; coverage must reject it
                }
                ctx->Unmap(readTarget.Get(),0);
            }
            CHECK(covered>200 && covered<w*h/4); CHECK(nearCount>100); if(multiple) CHECK(farCount>100); if(nearCount) centres[eye]/=nearCount;
            if(typeless) CHECK(std::fabs(centres[eye]-(eye?119.808:128.0))<1);
        }
        if(phase==1) CHECK(std::fabs(centres[0]-centres[1]-8.192)<1);
        // One lost eye skips that frame only: the live path stays armed, and the
        // native weapon is drawn again until the next frame warms up.
        FinishWeaponStereoFrame(); CHECK(WeaponStereoLive()); CHECK(WeaponStereoSkippedPairs()==(phase==2?1u:0u));
        CHECK(!CompositeWeaponStereo(ctx.Get(),tex[0].Get(),0)); // no stale image
        D3D11_QUERY_DATA_PIPELINE_STATISTICS stats{};
        ctx->Flush(); HRESULT hr=S_FALSE; for(int i=0;i<500 && hr==S_FALSE;++i) { hr=ctx->GetData(query.Get(),&stats,sizeof(stats),0); if(hr==S_FALSE) Sleep(1); }
        CHECK(hr==S_OK); CHECK(stats.IAPrimitives==(multiple?(phase?6u:9u):(phase?4u:6u))); // native omitted only after depth warmup
        if(phase==2) {
            ctx->OMSetRenderTargets(0,nullptr,worldDsv.Get()); CHECK(!BeginWeaponDepthIsolation(ctx.Get()));
            ctx->Begin(query.Get()); DrawWeaponStereo(ctx.Get(),6,0,0); ctx->End(query.Get()); ctx->Flush();
            hr=S_FALSE; for(int i=0;i<500 && hr==S_FALSE;++i) { hr=ctx->GetData(query.Get(),&stats,sizeof(stats),0); if(hr==S_FALSE) Sleep(1); }
            CHECK(hr==S_OK && stats.IAPrimitives==2); // failure resumes original draw
            ConfigureWeaponStereoCapture(false,nullptr,Report); return;
        }
        continue;
    }
    ctx->Flush(); // test-only submission; production relies on Present
    for(int i=0;i<500 && WeaponStereoCapturePending();++i) { PollWeaponStereoCapture(ctx.Get()); Sleep(10); }
    CHECK(!WeaponStereoCapturePending());
    D3D11_QUERY_DATA_PIPELINE_STATISTICS stats{};
    CHECK(ctx->GetData(query.Get(),&stats,sizeof(stats),0)==S_OK);
    CHECK(stats.IAPrimitives==8); // 2 primitives * (native+left+right+rejected-native)
    double centre[2][2]{};
    for(unsigned eye=0;eye<2;++eye) {
        const auto pixels=Read(dir/(eye?L"right_albedo.raw":L"left_albedo.raw"));
        CHECK(pixels.size()==w*h*4); if(pixels.size()!=w*h*4) continue;
        unsigned counts[2]{};
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
            const auto i=(y*w+x)*4;
            for(unsigned t=0;t<2;++t) if(pixels[i+t]>200) { centre[eye][t]+=x+.5; ++counts[t]; }
        }
        for(unsigned t=0;t<2;++t) { CHECK(counts[t]>100); if(counts[t]) centre[eye][t]/=counts[t]; }
        const auto bytes=Read(dir/(eye?L"right_cb0.bin":L"left_cb0.bin")); CHECK(bytes.size()==608);
        if(bytes.size()!=608) continue;
        std::array<float,152> actual{}; std::memcpy(actual.data(),bytes.data(),608);
        auto adjusted=view; adjusted[3]-=eye?.032f:-.032f;
        const Matrix expected[]={adjusted,Inverse(adjusted),projection,Multiply(projection,adjusted),Inverse(Multiply(projection,adjusted))};
        for(unsigned m=0;m<5;++m) for(unsigned i=0;i<16;++i) CHECK(std::fabs(actual[m*16+i]-expected[m][i])<.00005f);
        CHECK(std::memcmp(actual.data()+80,constants.data()+80,288)==0);
    }
    CHECK(std::fabs((centre[0][0]-centre[1][0])-8.192)<1);
    CHECK(std::fabs((centre[0][1]-centre[1][1])-4.096)<1);
    printf("Geometry disparity: near %.3f px, far %.3f px; pair at %ls\n",centre[0][0]-centre[1][0],centre[0][1]-centre[1][1],dir.c_str());
    // Copying/transforming the camera never writes into the game's source CB.
    const auto original=Read(dir/L"native_cb0.bin"); CHECK(original.size()==608 && std::memcmp(original.data(),constants.data(),608)==0);
    if(lighting && !skipLighting) {
        auto half=[](std::uint16_t bits) { return std::ldexp(1.0f+float(bits&1023)/1024,int((bits>>10)&31)-15); };
        for(unsigned eye=0;eye<2;++eye) {
            const auto bytes=Read(dir/(eye?L"right_lit.raw":L"left_lit.raw")); CHECK(bytes.size()==w*h*8);
            unsigned litPixels=0;
            for(std::size_t i=0;i+8<=bytes.size();i+=8) {
                std::uint16_t value[4]{}; std::memcpy(value,bytes.data()+i,8);
                if(half(value[0])>.2f || half(value[1])>.2f) {
                    ++litPixels; CHECK(std::fabs(std::max(half(value[0]),half(value[1]))-.25f)<.002f);
                    CHECK(std::fabs(half(value[2])-(eye?.068f:.132f))<.002f); CHECK(half(value[3])>.99f);
                }
            }
            CHECK(litPixels>200);
            const auto lightBytes=Read(dir/(eye?L"lighting_right_cb0.bin":L"lighting_left_cb0.bin")); CHECK(lightBytes.size()==608);
            if(lightBytes.size()==608) { float ambient=0; std::memcpy(&ambient,lightBytes.data()+135*4,4); CHECK(ambient==.25f); }
        }
    } else CHECK(!std::filesystem::exists(dir/L"left_lit.raw"));
    bd={}; bd.ByteWidth=608; bd.Usage=D3D11_USAGE_STAGING; bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> readback; device->CreateBuffer(&bd,nullptr,&readback); ctx->CopyResource(readback.Get(),cb.Get());
    D3D11_MAPPED_SUBRESOURCE map{}; CHECK(SUCCEEDED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)));
    if(map.pData) { CHECK(std::memcmp(map.pData,constants.data(),608)==0); ctx->Unmap(readback.Get(),0); }
    td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> worldRead; device->CreateTexture2D(&td,nullptr,&worldRead); ctx->CopyResource(worldRead.Get(),tex[0].Get());
    CHECK(SUCCEEDED(ctx->Map(worldRead.Get(),0,D3D11_MAP_READ,0,&map)));
    if(map.pData) {
        unsigned nonzero=0;
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w*4;++x) if(static_cast<unsigned char*>(map.pData)[y*map.RowPitch+x]) ++nonzero;
        CHECK(nonzero==0); ctx->Unmap(worldRead.Get(),0); // native world still occludes; private eyes do not
    }
    }
}
unsigned nativeCount=0,lastCount=0,lastStart=0; int lastBase=0;
void STDMETHODCALLTYPE FakeDraw(ID3D11DeviceContext*,UINT count,UINT start,INT base) { ++nativeCount; lastCount=count; lastStart=start; lastBase=base; }
void CheckSites(const wchar_t* path) {
    const auto module=LoadLibraryExW(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    ImageProfile image{}; char reason[200]{}; CHECK(module && CheckImage(module,image,reason,sizeof(reason))); if(!image.base) return;
    CHECK(InstallWeaponStereoHooks(image)); CHECK(!InstallWeaponStereoHooks(image));
    void* table[13]{}; table[12]=reinterpret_cast<void*>(&FakeDraw); void** object=table;
    ConfigureWeaponStereoCapture(false,nullptr,Report);
    for(auto site:{0x10FF556,0x10FF5C1}) {
        CHECK(image.base[site]==0xE8 && image.base[site+5]==0x90);
        std::int32_t rel=0; std::memcpy(&rel,image.base+site+1,4);
        using Call=void (*)(ID3D11DeviceContext*,UINT,UINT,INT);
        reinterpret_cast<Call>(image.base+site+5+rel)(reinterpret_cast<ID3D11DeviceContext*>(&object),123,7,-4);
    }
    CHECK(nativeCount==2 && lastCount==123 && lastStart==7 && lastBase==-4); FreeLibrary(module);
}
// The survival policy on its own, without a GPU in the way.
void CheckPairPolicy() {
    unsigned strikes=0;
    // A hitch: one lost eye among good frames never turns the path off.
    for(int i=0;i<1000;++i) {
        const auto outcome=(i%37==5)?WeaponPairOutcome::Incomplete
            :((i%37==6)?WeaponPairOutcome::Warmup:WeaponPairOutcome::Live);
        CHECK(KeepWeaponStereo(outcome,strikes));
    }
    CHECK(strikes==0);
    // A path that fails every live frame gives up, even though the warm-up
    // frame after each failure succeeds. That is the case a naive "reset on any
    // good frame" would never catch.
    strikes=0; bool kept=true; unsigned frames=0;
    while(kept && frames<1000) {
        kept=KeepWeaponStereo(frames%2?WeaponPairOutcome::Warmup:WeaponPairOutcome::Incomplete,strikes);
        ++frames;
    }
    CHECK(!kept); CHECK(strikes==kWeaponPairStrikeLimit); CHECK(frames<2*kWeaponPairStrikeLimit);
    // A complete pair forgives what came before it.
    strikes=kWeaponPairStrikeLimit-1;
    CHECK(KeepWeaponStereo(WeaponPairOutcome::Live,strikes)); CHECK(strikes==0);
    CHECK(KeepWeaponStereo(WeaponPairOutcome::Incomplete,strikes)); CHECK(strikes==1);
}
int wmain(int argc,wchar_t** argv) { CheckPairPolicy(); Run(); Run(true); Run(true,true); Run(true,false,true); Run(true,false,true,true); Run(true,true,true); Run(true,false,true,false,true); if(argc>1) CheckSites(argv[1]); printf("Stereo: %d failures\n",failures); return failures?1:0; }
