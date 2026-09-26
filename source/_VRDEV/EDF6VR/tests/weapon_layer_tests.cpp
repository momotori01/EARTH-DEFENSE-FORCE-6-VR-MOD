#include "weapon_layer.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
using namespace edf6vr;
static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
void Report(const char* s) { puts(s); }
void Run(bool deferred,unsigned samples,bool mrt=false,bool lighting=false,bool skipDispatch=false) {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> immediate,ctx;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&immediate)));
    if(!device) return;
    if(deferred) CHECK(SUCCEEDED(device->CreateDeferredContext(0,&ctx))); else ctx=immediate;
    if(!ctx) return;
    const auto dir=std::filesystem::temp_directory_path()/
        (L"EDF6VR-weapon-layer-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(samples)+(mrt?L"-mrt":L"")+(lighting?L"-lit":L"")+(skipDispatch?L"-skip":L""));
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=desc.Height=64; desc.MipLevels=desc.ArraySize=1;
    desc.SampleDesc={samples,0}; desc.Format=mrt?DXGI_FORMAT_R11G11B10_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    if(lighting) desc.BindFlags|=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> target; ComPtr<ID3D11RenderTargetView> rtv;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    ID3D11RenderTargetView* targets[7]={rtv.Get()};
    ComPtr<ID3D11Texture2D> extra[6],nativeDepth;
    ComPtr<ID3D11RenderTargetView> extraViews[6]; ComPtr<ID3D11DepthStencilView> nativeDsv;
    if(mrt) {
        const DXGI_FORMAT formats[]={DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM};
        for(unsigned i=0;i<6;++i) {
            auto d=desc; d.Format=formats[i];
            CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&extra[i])));
            CHECK(SUCCEEDED(device->CreateRenderTargetView(extra[i].Get(),nullptr,&extraViews[i])));
            targets[i+1]=extraViews[i].Get();
        }
        auto d=desc; d.Format=DXGI_FORMAT_D32_FLOAT; d.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&nativeDepth)));
        CHECK(SUCCEEDED(device->CreateDepthStencilView(nativeDepth.Get(),nullptr,&nativeDsv)));
        ctx->ClearDepthStencilView(nativeDsv.Get(),D3D11_CLEAR_DEPTH,0.5f,0); // native depth prepass
    }
    ctx->OMSetRenderTargets(mrt?7:1,targets,nativeDsv.Get());
    const float background[]={0.9f,0.1f,0.1f,1}; ctx->ClearRenderTargetView(rtv.Get(),background);
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=TRUE; depth.DepthFunc=D3D11_COMPARISON_EQUAL;
    ComPtr<ID3D11DepthStencilState> ds; CHECK(SUCCEEDED(device->CreateDepthStencilState(&depth,&ds)));
    ctx->OMSetDepthStencilState(ds.Get(),37);
    D3D11_VIEWPORT vp{0,0,64,64,0,1}; ctx->RSSetViewports(1,&vp);
    const char shader[]=R"(
float4 vs(uint id:SV_VertexID):SV_Position {
    float2 p[3]={float2(-.7,-.7),float2(0,.7),float2(.7,-.7)};
    return float4(p[id],.5,1);
}
float4 ps():SV_Target { return float4(.2,.6,.8,1); }
)";
    const char shaderMrt[]=R"(
struct Output { float3 a:SV_Target0; float4 b:SV_Target1; float4 c:SV_Target2;
float2 d:SV_Target3; float e:SV_Target4; float4 f:SV_Target5; float4 g:SV_Target6; };
Output ps() { Output o; o.a=float3(.2,.6,.8); o.b=float4(.2,.6,.8,1); o.c=o.b;
o.d=float2(.2,.6); o.e=.6; o.f=o.b; o.g=o.b; return o; }
)";
    ComPtr<ID3DBlob> vb,pb,errors;
    CHECK(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vb,&errors)));
    CHECK(SUCCEEDED(D3DCompile(mrt?shaderMrt:shader,mrt?sizeof(shaderMrt):sizeof(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&pb,&errors)));
    if(!vb || !pb) return;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    device->CreateVertexShader(vb->GetBufferPointer(),vb->GetBufferSize(),nullptr,&vs);
    device->CreatePixelShader(pb->GetBufferPointer(),pb->GetBufferSize(),nullptr,&ps);
    ctx->VSSetShader(vs.Get(),nullptr,0); ctx->PSSetShader(ps.Get(),nullptr,0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ConfigureWeaponLayerCapture(false,dir.c_str(),Report,0);
    CHECK(!BeginWeaponLayerCapture(ctx.Get(),1,64,64));
    ConfigureWeaponLayerCapture(true,dir.c_str(),Report,0,lighting);
    CHECK(!BeginWeaponLayerCapture(ctx.Get(),1,128,64)); // wrong scene size
    const bool began=BeginWeaponLayerCapture(ctx.Get(),1,64,64); CHECK(began);
    if(!began) return;
    CHECK(InWeaponLayerCapture());
    CHECK(!BeginWeaponLayerCapture(ctx.Get(),1,64,64)); // no nested borrowing
    ctx->Draw(3,0); // native submission is issued exactly once, into private RT
    if(deferred) ctx->OMSetDepthStencilState(nullptr,55); // native material changed its cached state
    EndWeaponLayerCapture(); CHECK(!InWeaponLayerCapture());
    ComPtr<ID3D11RenderTargetView> restored; ComPtr<ID3D11DepthStencilState> restoredDepth; UINT ref=0;
    ctx->OMGetRenderTargets(1,&restored,nullptr); ctx->OMGetDepthStencilState(&restoredDepth,&ref);
    CHECK(restored.Get()==rtv.Get());
    if(mrt) {
        ID3D11RenderTargetView* all[7]{}; ComPtr<ID3D11DepthStencilView> restoredDsv;
        ctx->OMGetRenderTargets(7,all,&restoredDsv);
        CHECK(restoredDsv.Get()==nativeDsv.Get());
        for(unsigned i=0;i<7;++i) { CHECK(all[i]==targets[i]); if(all[i]) all[i]->Release(); }
    }
    CHECK(deferred?(restoredDepth.Get()==nullptr && ref==55):(restoredDepth.Get()==ds.Get() && ref==37));
    ComPtr<ID3D11Texture2D> lightOutput;
    if(lighting && !skipDispatch) {
        const char csCode[]=R"(
Texture2D<float3> source:register(t0);
RWTexture2D<float4> result:register(u0);
cbuffer System:register(b0) { float4 factor[38]; }
[numthreads(16,16,1)] void cs(uint3 p:SV_DispatchThreadID) { result[p.xy]=float4(source.Load(int3(p.xy,0))*factor[0].x,1); }
)";
        ComPtr<ID3DBlob> code; CHECK(SUCCEEDED(D3DCompile(csCode,sizeof(csCode),nullptr,nullptr,nullptr,"cs","cs_5_0",0,0,&code,&errors)));
        ComPtr<ID3D11ComputeShader> cs; CHECK(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs)));
        auto d=desc; d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&lightOutput)));
        ComPtr<ID3D11UnorderedAccessView> output; CHECK(SUCCEEDED(device->CreateUnorderedAccessView(lightOutput.Get(),nullptr,&output)));
        ComPtr<ID3D11ShaderResourceView> sources[7]; ID3D11ShaderResourceView* raw[7]{};
        for(unsigned i=0;i<7;++i) {
            CHECK(SUCCEEDED(device->CreateShaderResourceView(i?extra[i-1].Get():target.Get(),nullptr,&sources[i]))); raw[i]=sources[i].Get();
        }
        float system[152]{}; system[0]=.5f; D3D11_BUFFER_DESC bd{};
        bd.ByteWidth=sizeof(system); bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA init{system,0,0}; ComPtr<ID3D11Buffer> cb;
        CHECK(SUCCEEDED(device->CreateBuffer(&bd,&init,&cb)));
        ctx->OMSetRenderTargets(0,nullptr,nullptr);
        ctx->CSSetShader(cs.Get(),nullptr,0); ID3D11Buffer* cbRaw=cb.Get(); ctx->CSSetConstantBuffers(0,1,&cbRaw);
        ID3D11UnorderedAccessView* outRaw=output.Get(); ctx->CSSetUnorderedAccessViews(0,1,&outRaw,nullptr);
        // Matching dimensions alone must not select an unrelated lighting pass.
        ctx->CSSetShaderResources(0,6,raw);
        DispatchWeaponLighting(ctx.Get(),4,4,1); CHECK(WeaponLayerCapturePending());
        // Change the native light after the rejected pass. A false-positive
        // one-shot capture above would retain .5 and fail the .25 output below.
        system[0]=.25f; ctx->UpdateSubresource(cb.Get(),0,nullptr,system,0,0);
        ctx->CSSetShaderResources(0,7,raw); DispatchWeaponLighting(ctx.Get(),4,4,1);
        ID3D11ShaderResourceView* restoredInputs[7]{}; ctx->CSGetShaderResources(0,7,restoredInputs);
        for(unsigned i=0;i<7;++i) { CHECK(restoredInputs[i]==raw[i]); if(restoredInputs[i]) restoredInputs[i]->Release(); }
        ComPtr<ID3D11UnorderedAccessView> actualOutput; ctx->CSGetUnorderedAccessViews(0,1,&actualOutput);
        CHECK(actualOutput.Get()==output.Get());
        ComPtr<ID3D11Buffer> actualCb; ctx->CSGetConstantBuffers(0,1,&actualCb); CHECK(actualCb.Get()==cb.Get());
        ComPtr<ID3D11ComputeShader> actualCs; ctx->CSGetShader(&actualCs,nullptr,nullptr); CHECK(actualCs.Get()==cs.Get());
        // Native output still comes from the red world target, never our weapon.
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> stage; device->CreateTexture2D(&d,nullptr,&stage); ctx->CopyResource(stage.Get(),lightOutput.Get());
        D3D11_MAPPED_SUBRESOURCE m{}; CHECK(SUCCEEDED(ctx->Map(stage.Get(),0,D3D11_MAP_READ,0,&m)));
        if(m.pData) { const float value=*reinterpret_cast<const float*>(static_cast<const unsigned char*>(m.pData)+32*m.RowPitch+32*16);
            CHECK(value>.22f && value<.23f); ctx->Unmap(stage.Get(),0); }
    }
    if(deferred) { ComPtr<ID3D11CommandList> list; CHECK(SUCCEEDED(ctx->FinishCommandList(FALSE,&list))); immediate->ExecuteCommandList(list.Get(),TRUE); }
    // A skipped lighting dispatch ends its event at Present. Emulate that before
    // the test-only Flush; real Present submits the event on the next frame.
    PollWeaponLayerCapture(immediate.Get());
    immediate->Flush(); // tests only; production never flushes/waits for the GPU
    for(int i=0;i<2000 && !std::filesystem::exists(dir/L"weapon_native.raw");++i) { PollWeaponLayerCapture(immediate.Get()); Sleep(1); }
    std::ifstream file(dir/L"weapon_native.raw",std::ios::binary);
    std::vector<unsigned char> pixels((std::istreambuf_iterator<char>(file)),{});
    CHECK(pixels.size()==64*64*4);
    unsigned coloured=0,clear=0;
    for(std::size_t i=0;i+3<pixels.size();i+=4) {
        if(mrt) {
            unsigned packed=0; std::memcpy(&packed,&pixels[i],4);
            if(packed) ++coloured; else ++clear;
        } else {
            if(pixels[i+1]>100 && pixels[i+2]>150 && pixels[i]<100) ++coloured;
            if(pixels[i+3]==0) ++clear;
        }
    }
    CHECK(coloured>200 && clear>200); // actual native colour with transparent surroundings
    if(lighting && !skipDispatch) {
        std::ifstream litFile(dir/L"weapon_lit.raw",std::ios::binary);
        std::vector<char> litBytes((std::istreambuf_iterator<char>(litFile)),{});
        CHECK(litBytes.size()==64*64*16);
        if(litBytes.size()==64*64*16) { float value=0; std::memcpy(&value,litBytes.data()+(32*64+32)*16,4); CHECK(value>.045f && value<.055f); }
        CHECK(std::filesystem::file_size(dir/L"lighting_cb0.bin")==608);
    } else CHECK(!std::filesystem::exists(dir/L"weapon_lit.raw"));
    if(mrt) for(unsigned i=1;i<7;++i) {
        const auto path=dir/(L"weapon_target"+std::to_wstring(i)+L".raw");
        CHECK(std::filesystem::exists(path));
        std::ifstream input(path,std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
        unsigned nonzero=0; for(auto b:bytes) if(b) ++nonzero;
        CHECK(nonzero>200); // Equal-depth pass must survive the copied prepass depth
    }
    CHECK(!WeaponLayerCaptureWanted()); CHECK(!BeginWeaponLayerCapture(ctx.Get(),1,64,64));
    // Original target must still contain only its pre-capture background.
    auto single=desc; single.SampleDesc={1,0}; single.BindFlags=0;
    ComPtr<ID3D11Texture2D> resolved,readback; device->CreateTexture2D(&single,nullptr,&resolved);
    if(samples>1) immediate->ResolveSubresource(resolved.Get(),0,target.Get(),0,desc.Format); else immediate->CopyResource(resolved.Get(),target.Get());
    single.Usage=D3D11_USAGE_STAGING; single.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    device->CreateTexture2D(&single,nullptr,&readback); immediate->CopyResource(readback.Get(),resolved.Get());
    D3D11_MAPPED_SUBRESOURCE map{}; CHECK(SUCCEEDED(immediate->Map(readback.Get(),0,D3D11_MAP_READ,0,&map)));
    if(map.pData) { const auto pixel=static_cast<const unsigned char*>(map.pData)+32*map.RowPitch+32*4;
        if(mrt) { unsigned word=0; std::memcpy(&word,pixel,4); CHECK((word&2047)>((word>>11)&2047)); }
        else CHECK(pixel[0]>200 && pixel[1]<50 && pixel[2]<50);
        immediate->Unmap(readback.Get(),0); }
    std::printf("private colour: deferred=%d MSAA=%u coloured=%u transparent=%u\n",deferred,samples,coloured,clear);
}
unsigned dispatchCount=0,dispatchX=0,dispatchY=0,dispatchZ=0;
void STDMETHODCALLTYPE FakeDispatch(ID3D11DeviceContext*,UINT x,UINT y,UINT z) {
    ++dispatchCount; dispatchX=x; dispatchY=y; dispatchZ=z;
}
void CheckLightingSites(const wchar_t* imagePath) {
    const auto module=LoadLibraryExW(imagePath,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    ImageProfile image{}; char reason[200]{};
    CHECK(module && CheckImage(module,image,reason,sizeof(reason))); if(!image.base) return;
    CHECK(InstallWeaponLightingCaptureHooks(image));
    CHECK(!InstallWeaponLightingCaptureHooks(image)); // already patched/conflicting bytes rejected
    void* table[42]{}; table[41]=reinterpret_cast<void*>(&FakeDispatch); void** object=table;
    ConfigureWeaponLayerCapture(false,nullptr,Report);
    for(auto site:{0x1131198,0x1131233}) {
        CHECK(image.base[site]==0xE8 && image.base[site+5]==0x90 && image.base[site+6]==0x90);
        std::int32_t rel=0; std::memcpy(&rel,image.base+site+1,4);
        auto* thunk=image.base+site+5+rel;
        using Call=void (*)(ID3D11DeviceContext*,UINT,UINT,UINT);
        reinterpret_cast<Call>(thunk)(reinterpret_cast<ID3D11DeviceContext*>(&object),240,135,1);
    }
    CHECK(dispatchCount==2 && dispatchX==240 && dispatchY==135 && dispatchZ==1);
    FreeLibrary(module);
}
int wmain(int argc,wchar_t** argv) {
    Run(false,1); Run(true,4); Run(false,1,true);
    Run(false,1,true,true); Run(false,1,true,true,true);
    if(argc>1) CheckLightingSites(argv[1]);
    printf("Weapon layer capture: %d failures\n",failures); return failures?1:0;
}
