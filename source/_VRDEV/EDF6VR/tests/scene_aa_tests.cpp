#include "scene_aa.h"
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace edf6vr;
using Microsoft::WRL::ComPtr;
namespace {
int failures=0;
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
    if(!ctx) return 1;
    EnableSceneAA(false);CHECK(!SceneAAEnabled());CHECK(ToggleSceneAA());CHECK(!ToggleSceneAA());
    CHECK(ApplySceneAA(nullptr,nullptr,false)); // OFF does not need any GPU resources.
    for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                     DXGI_FORMAT_R8G8B8A8_TYPELESS,DXGI_FORMAT_B8G8R8A8_TYPELESS}) {
        for(unsigned side:{64u,96u,64u}) {
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=side;d.MipLevels=d.ArraySize=1;
            d.Format=format;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> target,readback,other;
            CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&target)));
            CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&other)));
            d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&readback)));
            std::vector<unsigned char> source(side*side*4);
            for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) {
                const auto i=(y*side+x)*4;const unsigned char shade=y>x/2+side/4?220:24;
                source[i]=source[i+1]=source[i+2]=shade;source[i+3]=173;
            }
            auto upload=[&] {ctx->UpdateSubresource(target.Get(),0,nullptr,source.data(),side*4,0);};
            auto read=[&] {
                ctx->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE m{};
                std::vector<unsigned char> out(source.size());
                if(FAILED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&m))) {++failures;return out;}
                for(unsigned y=0;y<side;++y) std::memcpy(out.data()+y*side*4,static_cast<unsigned char*>(m.pData)+y*m.RowPitch,side*4);
                ctx->Unmap(readback.Get(),0);return out;
            };
            upload();CHECK(ApplySceneAA(ctx.Get(),target.Get(),false));CHECK(read()==source);
            D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            rv.Format=format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:
                format==DXGI_FORMAT_B8G8R8A8_TYPELESS?DXGI_FORMAT_B8G8R8A8_UNORM:format;
            ComPtr<ID3D11RenderTargetView> native;
            CHECK(SUCCEEDED(device->CreateRenderTargetView(other.Get(),&rv,&native)));
            auto* rt=native.Get();ctx->OMSetRenderTargets(1,&rt,nullptr);
            D3D11_VIEWPORT vp{4,6,16,22,.2f,.9f};ctx->RSSetViewports(1,&vp);
            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            const float blend[4]={.1f,.2f,.3f,.4f};ctx->OMSetBlendState(nullptr,blend,0x13579);
            ctx->OMSetDepthStencilState(nullptr,37);
            ComPtr<ID3D11Buffer> cb;D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&cb)));auto* raw=cb.Get();ctx->PSSetConstantBuffers(0,1,&raw);
            CHECK(ApplySceneAA(ctx.Get(),target.Get(),true));CHECK(!InsideSceneAA());
            auto out=read();unsigned smoothed=0;
            for(size_t i=0;i<out.size();i+=4) {
                if(out[i]>30 && out[i]<214) ++smoothed;
                CHECK(out[i+3]==173);
            }
            CHECK(smoothed>side/2);CHECK(out[0]==24);CHECK(out[(side*side-1)*4]==220);
            ComPtr<ID3D11RenderTargetView> restored;ctx->OMGetRenderTargets(1,&restored,nullptr);CHECK(restored.Get()==native.Get());
            D3D11_VIEWPORT after{};UINT count=1;ctx->RSGetViewports(&count,&after);CHECK(after.TopLeftX==4 && after.Height==22 && after.MinDepth==.2f);
            D3D11_PRIMITIVE_TOPOLOGY topology{};ctx->IAGetPrimitiveTopology(&topology);CHECK(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            ComPtr<ID3D11Buffer> old;ctx->PSGetConstantBuffers(0,1,&old);CHECK(old.Get()==cb.Get());
            UINT mask=0,ref=0;float factors[4]{};ctx->OMGetBlendState(nullptr,factors,&mask);ctx->OMGetDepthStencilState(nullptr,&ref);
            CHECK(mask==0x13579 && ref==37 && factors[2]==.3f);
            upload();CHECK(ApplySceneAA(ctx.Get(),target.Get(),false));CHECK(read()==source); // No accumulated/history blur.
            // A flat coloured image must keep its colour and opacity (also checks sRGB writes).
            for(size_t i=0;i<source.size();i+=4) {source[i]=67;source[i+1]=123;source[i+2]=191;source[i+3]=103;}
            upload();CHECK(ApplySceneAA(ctx.Get(),target.Get(),true));out=read();
            for(size_t i=0;i<out.size();++i) CHECK(std::abs(int(out[i])-int(source[i]))<=1);
        }
        ReleaseSceneAA(); // F11 off/on must build fresh private resources safely.
    }
    ReleaseSceneAA();printf("Scene AA failures=%d\n",failures);return failures?1:0;
}
