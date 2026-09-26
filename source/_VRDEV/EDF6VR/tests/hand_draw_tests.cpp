#include "hand_draw.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <array>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
using namespace edf6vr;
static int failures=0;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)

int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)))return 2;
    const char shader[]=R"(
cbuffer Bones : register(b0) {float4 offsets[8];};
struct V {float4 p:POSITION;float4 w:BLENDWEIGHT;uint4 b:BLENDINDICES;};
float4 VS(V v):SV_POSITION {return v.p+offsets[v.b.x]*v.w.x+offsets[v.b.y]*v.w.y+offsets[v.b.z]*v.w.z+offsets[v.b.w]*v.w.w;}
float4 PS():SV_TARGET {return float4(.25,.8,1,1);}
)";
    ComPtr<ID3DBlob> vsCode,psCode,error;
    CHECK(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"VS","vs_5_0",0,0,&vsCode,&error)));
    CHECK(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&psCode,&error)));
    if(!vsCode || !psCode)return 2;
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11InputLayout> input;
    CHECK(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,&vs)));
    CHECK(SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,&ps)));
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R8G8B8A8_UINT,0,32,D3D11_INPUT_PER_VERTEX_DATA,0}};
    CHECK(SUCCEEDED(device->CreateInputLayout(elements,3,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),&input)));
    struct Vertex {float p[4],w[4];unsigned char b[4];};static_assert(sizeof(Vertex)==36);
    Vertex vertices[10]{};
    for(auto& v:vertices){v.p[3]=1;v.w[0]=1;}
    const float positions[6][2]={{-.8f,-.6f},{-.2f,-.6f},{-.5f,.6f},{.2f,-.6f},{.8f,-.6f},{.5f,.6f}};
    for(unsigned i=0;i<6;++i){auto& v=vertices[i+1];v.p[0]=positions[i][0];v.p[1]=positions[i][1];
        v.w[0]=.2f;v.w[1]=.3f;v.w[2]=.5f;v.b[0]=i<3?1:2;v.b[1]=i<3?3:4;v.b[2]=6;v.b[3]=255;}
    std::vector<unsigned char> bytes(16+sizeof(vertices));std::memcpy(bytes.data()+16,vertices,sizeof(vertices));
    const std::uint16_t indices[]={65535,65535,65535,0,1,2,3,4,5,6,7,8};
    auto buffer=[&](const void* data,UINT size,UINT bind) {
        ComPtr<ID3D11Buffer> out;D3D11_BUFFER_DESC d{};d.ByteWidth=size;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA init{data,0,0};CHECK(SUCCEEDED(device->CreateBuffer(&d,data?&init:nullptr,&out)));return out;
    };
    auto vb=buffer(bytes.data(),static_cast<UINT>(bytes.size()),D3D11_BIND_VERTEX_BUFFER);
    auto ib=buffer(indices,sizeof(indices),D3D11_BIND_INDEX_BUFFER);
    auto bones=buffer(nullptr,8*16,D3D11_BIND_CONSTANT_BUFFER);
    D3D11_TEXTURE2D_DESC td{};td.Width=128;td.Height=96;td.MipLevels=td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,readback;ComPtr<ID3D11RenderTargetView> rtv;
    CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&target)));CHECK(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&readback)));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;CHECK(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)));
    const int left[]={1},right[]={2};SetHandBones(left,1,right,1,8,3,4);
    const UINT stride=sizeof(Vertex),offset=16;const D3D11_VIEWPORT viewport{0,0,128,96,0,1};
    auto render=[&](float bodyOffset,bool drawLeft,bool drawRight,bool layer=false) {
        // The body on bone 6: slots 0 and 5 are the hands' spare (collapse) slots,
        // which the real draw fills with the wrist (PoseHandsIntoPalette).
        float matrices[8][4]{};matrices[6][0]=bodyOffset;matrices[6][1]=-bodyOffset;
        ctx->UpdateSubresource(bones.Get(),0,nullptr,matrices,0,0);
        auto* vertex=vb.Get();auto* constant=bones.Get();auto* output=rtv.Get();
        ctx->IASetVertexBuffers(0,1,&vertex,&stride,&offset);ctx->IASetIndexBuffer(ib.Get(),DXGI_FORMAT_R16_UINT,4);
        ctx->IASetInputLayout(input.Get());ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.Get(),nullptr,0);ctx->PSSetShader(ps.Get(),nullptr,0);ctx->VSSetConstantBuffers(0,1,&constant);
        ctx->RSSetState(raster.Get());ctx->RSSetViewports(1,&viewport);ctx->OMSetRenderTargets(1,&output,nullptr);
        const float clear[4]{};const auto before=ReadHandDrawStats().handDraws;
        for(unsigned attempt=0;attempt<100;++attempt) {
            ctx->ClearRenderTargetView(rtv.Get(),clear);
            {HandDrawScope scope(drawLeft,drawRight,layer);CHECK(HandDrawIntercept(ctx.Get(),9,1,1));}
            ctx->Flush();if(ReadHandDrawStats().handDraws>before)break;Sleep(1);
        }
        CHECK(ReadHandDrawStats().handDraws>before);
        ComPtr<ID3D11Buffer> restoredVb,restoredIb;UINT rs=0,ro=0,io=0;DXGI_FORMAT format{};
        ctx->IAGetVertexBuffers(0,1,&restoredVb,&rs,&ro);ctx->IAGetIndexBuffer(&restoredIb,&format,&io);
        CHECK(restoredVb.Get()==vb.Get() && rs==stride && ro==offset);
        CHECK(restoredIb.Get()==ib.Get() && format==DXGI_FORMAT_R16_UINT && io==4);
        ctx->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE map{};
        std::vector<unsigned char> pixels(128*96*4);
        if(SUCCEEDED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&map))) {
            for(unsigned y=0;y<96;++y)std::memcpy(pixels.data()+y*128*4,static_cast<unsigned char*>(map.pData)+y*map.RowPitch,128*4);
            ctx->Unmap(readback.Get(),0);
        } else CHECK(false);
        return pixels;
    };
    const auto before=render(0,true,true),revived=render(300,true,true);
    CHECK(before==revived);unsigned lit=0;for(std::size_t i=3;i<before.size();i+=4)if(before[i])++lit;
    CHECK(lit>1500);CHECK(ReadHandDrawStats().trianglesLeft==1 && ReadHandDrawStats().trianglesRight==1);
    CHECK(ReadHandDrawStats().verticesReweighted==6);
    const auto onlyLeft=render(-200,true,false);unsigned leftLit=0,rightLit=0;
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<128;++x)if(onlyLeft[(y*128+x)*4+3]){if(x<64)++leftLit;else ++rightLit;}
    CHECK(leftLit>700 && rightLit==0);
    // A rejected stereo replay must restore both input buffers as well.
    render(300,true,true,true);CHECK(ReadHandDrawStats().layerRejected>=2);
    ResetHandMeshes();std::printf("Hand skin isolation, revival displacement and GPU input restoration: %d failures\n",failures);
    return failures?1:0;
}
