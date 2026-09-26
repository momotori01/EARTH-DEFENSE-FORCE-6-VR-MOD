#include "desktop_mirror.h"
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
using Microsoft::WRL::ComPtr;
using namespace edf6vr;
namespace {
int failures=0;
#define CHECK(x) do { if(!(x)) {printf("FAIL %d: %s\n",__LINE__,#x);++failures;} } while(false)
}
int main() {
    // The geometry below is checked at the headset's own size; the desktop's
    // larger HUD is checked on its own further down.
    SetDesktopMirrorUiScale(1.0f);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx)));
    if(!ctx) return 1;
    for(unsigned samples:{1u,4u,8u,1u}) {
        const unsigned side=samples==4?128u:64u;
        D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=side;d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> input,output,single,readback;ComPtr<ID3D11RenderTargetView> view;
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&input)));
        CHECK(SUCCEEDED(device->CreateRenderTargetView(input.Get(),nullptr,&view)));
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&single)));
        d.SampleDesc.Count=samples;CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&output)));
        d.SampleDesc.Count=1;d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&readback)));
        auto snapshot=[&](MirrorImage slot,float r,float g,float b,float a,DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM) {
            const FLOAT c[4]={r,g,b,a};ctx->ClearRenderTargetView(view.Get(),c);
            CHECK(SnapshotMirror(ctx.Get(),input.Get(),slot,format));
        };
        auto read=[&](unsigned x,unsigned y) {
            if(samples>1) ctx->ResolveSubresource(single.Get(),0,output.Get(),0,DXGI_FORMAT_R8G8B8A8_UNORM);
            else ctx->CopyResource(single.Get(),output.Get());
            ctx->CopyResource(readback.Get(),single.Get());
            D3D11_MAPPED_SUBRESOURCE map{};std::array<float,4> pixel{};
            if(FAILED(ctx->Map(readback.Get(),0,D3D11_MAP_READ,0,&map))) {++failures;return pixel;}
            const auto* p=static_cast<unsigned char*>(map.pData)+y*map.RowPitch+x*4;
            for(unsigned i=0;i<4;++i) pixel[i]=p[i]/255.f;
            ctx->Unmap(readback.Get(),0);return pixel;
        };
        XrPosef eye{{0,0,0,1},{0,0,0}},head=eye;
        XrFovf fov{-.78539816f,.78539816f,.78539816f,-.78539816f};
        XrCompositionLayerQuad ui{XR_TYPE_COMPOSITION_LAYER_QUAD};ui.pose={{0,0,0,1},{0,0,-2}};ui.size={2,2};
        XrCompositionLayerQuad reticle=ui;reticle.pose.position={1,0,-2};reticle.size={.3f,.3f};
        snapshot(MirrorImage::Left,1,0,0,1);snapshot(MirrorImage::Right,0,0,1,1);
        snapshot(MirrorImage::Ui,0,.5f,0,.5f);snapshot(MirrorImage::Reticle,1,1,1,1);
        // Source can change immediately afterwards (e.g. runtime reuses it).
        const FLOAT poison[4]={0,0,0,1};ctx->ClearRenderTargetView(view.Get(),poison);
        // Check restoration of native output, viewport, input layout/topology,
        // shader resource, constant buffer, sampler, depth, raster and blend.
        auto* native=view.Get();ctx->OMSetRenderTargets(1,&native,nullptr);
        D3D11_VIEWPORT vp{3,4,16,20,.1f,.9f};ctx->RSSetViewports(1,&vp);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        ComPtr<ID3D11Buffer> cb;D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&cb)));auto* rawCb=cb.Get();ctx->PSSetConstantBuffers(0,1,&rawCb);
        const FLOAT factors[4]={.1f,.2f,.3f,.4f};ctx->OMSetBlendState(nullptr,factors,0xabcdef12);
        ctx->OMSetDepthStencilState(nullptr,27);
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,&ui,&reticle));
        auto c=read(side/2,side/2);CHECK(c[0]<.01f && std::fabs(c[1]-.5f)<.015f && std::fabs(c[2]-.5f)<.015f);
        c=read(side*3/4,side/2);CHECK(c[0]>.99f && c[1]>.99f && c[2]>.99f); // aim quad is at its projected position
        c=read(2,2);CHECK(c[0]<.01f && c[1]<.01f && c[2]>.99f); // right world, not left or source poison
        ComPtr<ID3D11RenderTargetView> restored;ctx->OMGetRenderTargets(1,&restored,nullptr);CHECK(restored.Get()==native);
        D3D11_VIEWPORT after{};UINT count=1;ctx->RSGetViewports(&count,&after);CHECK(after.TopLeftX==3 && after.Width==16 && after.MaxDepth==.9f);
        D3D11_PRIMITIVE_TOPOLOGY topology{};ctx->IAGetPrimitiveTopology(&topology);CHECK(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        ComPtr<ID3D11Buffer> restoredCb;ctx->PSGetConstantBuffers(0,1,&restoredCb);CHECK(restoredCb.Get()==cb.Get());
        ComPtr<ID3D11BlendState> restoredBlend;FLOAT bf[4]{};UINT mask=0;ctx->OMGetBlendState(&restoredBlend,bf,&mask);
        CHECK(!restoredBlend && mask==0xabcdef12 && bf[2]==factors[2]);
        ComPtr<ID3D11DepthStencilState> restoredDepth;UINT ref=0;ctx->OMGetDepthStencilState(&restoredDepth,&ref);CHECK(!restoredDepth && ref==27);
        // Reference-space reticle behind the eye is clipped, not mirrored forward.
        reticle.pose.position.z=2;
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,nullptr,&reticle));
        c=read(side*3/4,side/2);CHECK(c[0]<.01f && c[2]>.99f);
        // Right-eye offset shifts the view-space panel projection (no fixed 2D paste).
        eye.position.x=.5f;reticle.pose.position.z=-2;
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,&ui,nullptr));
        c=read(side*3/10,side/2);CHECK(c[1]>.49f);
        c=read(side*7/10,side/2);CHECK(c[1]<.01f);
        // Larger on the desktop: the same panel at 1.35 reaches past where it ended.
        SetDesktopMirrorUiScale(1.35f);
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,&ui,nullptr));
        c=read(side*7/10,side/2);CHECK(c[1]>.49f);
        SetDesktopMirrorUiScale(1.0f);
        // Shared yaw/roll/position of head and eye leaves head-locked UI centred.
        eye={{0,0,.70710678f,.70710678f},{10,3,-4}};head=eye;
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,&ui,nullptr));
        c=read(side/2,side/2);CHECK(c[1]>.49f);
        // Current-frame invalidation prevents stale HUD and aim after a blank frame.
        InvalidateMirrorImage(MirrorImage::Ui);InvalidateMirrorImage(MirrorImage::Reticle);
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,&ui,&reticle));
        c=read(side/2,side/2);CHECK(c[1]<.01f && c[2]>.99f);
        // Native-eye fallback explicitly chooses left, never a previous right image.
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Left,eye,fov,head,nullptr,nullptr));
        c=read(side/2,side/2);CHECK(c[0]>.99f && c[2]<.01f);
        // sRGB sources blend in linear space; desktop UNORM gets encoded once.
        snapshot(MirrorImage::Right,0,0,1,1,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        snapshot(MirrorImage::Ui,0,.735357f,0,.5f,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,&ui,nullptr));
        c=read(side/2,side/2);CHECK(std::fabs(c[1]-.735357f)<.015f && std::fabs(c[2]-.735357f)<.015f);
        // Narrow spectator FOV samples the central source rays, while the aim
        // plane uses the SAME new rays (not its old full-FOV screen position).
        eye={{0,0,0,1},{0,0,0}};head=eye;
        XrFovf wide{-std::atan(2.f),std::atan(2.f),std::atan(2.f),-std::atan(2.f)};
        std::vector<unsigned> gradient(side*side);
        for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) gradient[y*side+x]=0xff000000u+(x*255u/(side-1));
        ctx->UpdateSubresource(input.Get(),0,nullptr,gradient.data(),side*4,0);
        CHECK(SnapshotMirror(ctx.Get(),input.Get(),MirrorImage::Right,DXGI_FORMAT_R8G8B8A8_UNORM));
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,wide,head,nullptr,nullptr,90));
        c=read(side/4,side/2);CHECK(std::fabs(c[0]-.375f)<.02f);
        c=read(side*3/4,side/2);CHECK(std::fabs(c[0]-.625f)<.02f);
        snapshot(MirrorImage::Reticle,1,1,1,1);
        reticle.pose={{0,0,0,1},{.5f,0,-2}};reticle.size={.15f,.15f};
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,wide,head,nullptr,&reticle,90));
        c=read(side*5/8,side/2);CHECK(c[0]>.99f && c[1]>.99f);
        c=read(side*9/16,side/2);CHECK(c[1]<.01f);
        CHECK(RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,wide,head,nullptr,nullptr,0));
        c=read(side/4,side/2);CHECK(std::fabs(c[0]-.25f)<.02f); // 0 restores full source
        // Session/resize release invalidates all private eyes. No output on failure.
        ReleaseDesktopMirror();CHECK(!RenderDesktopMirror(ctx.Get(),output.Get(),MirrorImage::Right,eye,fov,head,&ui,&reticle));
        ctx->ClearState();
    }
    printf("Desktop right-eye mirror pixels/quad projection/MSAA/ownership: %d failures\n",failures);
    return failures?1:0;
}
