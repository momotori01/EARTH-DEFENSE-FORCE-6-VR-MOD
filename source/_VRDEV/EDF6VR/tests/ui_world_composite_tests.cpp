#include "ui_world_composite.h"
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

static ComPtr<ID3D11Texture2D> Make(ID3D11Device* d,DXGI_FORMAT format,UINT bind,const unsigned char rgba[4]) {
    D3D11_TEXTURE2D_DESC t{}; t.Width=t.Height=8; t.MipLevels=t.ArraySize=1; t.Format=format; t.SampleDesc.Count=1;
    t.Usage=D3D11_USAGE_DEFAULT; t.BindFlags=bind;
    unsigned char pixels[8*8*4]; for(int i=0;i<64;++i) std::memcpy(pixels+i*4,rgba,4);
    D3D11_SUBRESOURCE_DATA data{pixels,8*4,0};
    ComPtr<ID3D11Texture2D> out; d->CreateTexture2D(&t,&data,&out); return out;
}
static void Read(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* tex,unsigned char out[4]) {
    D3D11_TEXTURE2D_DESC t{}; tex->GetDesc(&t); t.Usage=D3D11_USAGE_STAGING; t.BindFlags=0; t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; d->CreateTexture2D(&t,nullptr,&staging); c->CopyResource(staging.Get(),tex);
    D3D11_MAPPED_SUBRESOURCE m{}; c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m);
    std::memcpy(out,static_cast<unsigned char*>(m.pData)+(4*m.RowPitch)+4*4,4); c->Unmap(staging.Get(),0);
}
static bool Near(int a,int b,int tolerance=2) { return a-b<=tolerance && b-a<=tolerance; }

int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) {
        printf("no WARP device\n"); return 1;
    }
    const UINT srvBind=D3D11_BIND_SHADER_RESOURCE, rtvBind=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    // Premultiplied half-covered white over black, both UNORM (the SteamVR typeless case reads as UNORM).
    {
        const unsigned char half[4]={128,128,128,128}, black[4]={0,0,0,255};
        auto overlay=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,srvBind,half);
        auto target=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,black);
        CHECK(overlay && target);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),overlay.Get(),target.Get()));
        unsigned char px[4]{}; Read(device.Get(),context.Get(),target.Get(),px);
        CHECK(Near(px[0],128) && Near(px[1],128) && px[3]==255);   // colour added, target alpha kept
    }
    // Fully covered colour replaces; empty overlay leaves the world untouched.
    {
        const unsigned char red[4]={255,0,0,255}, clear[4]={0,0,0,0}, world[4]={10,200,30,255};
        auto opaque=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,srvBind,red);
        auto empty=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,srvBind,clear);
        auto target=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,world);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),empty.Get(),target.Get()));
        unsigned char px[4]{}; Read(device.Get(),context.Get(),target.Get(),px);
        CHECK(px[0]==10 && px[1]==200 && px[2]==30);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),opaque.Get(),target.Get()));
        Read(device.Get(),context.Get(),target.Get(),px);
        CHECK(Near(px[0],255) && Near(px[1],0) && Near(px[2],0));
    }
    // An sRGB-typed capture into a raw target is encoded back to the same bits.
    {
        const unsigned char grey[4]={188,188,188,255}, black[4]={0,0,0,255};
        auto overlay=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,srvBind,grey);
        auto target=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,black);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),overlay.Get(),target.Get()));
        unsigned char px[4]{}; Read(device.Get(),context.Get(),target.Get(),px);
        CHECK(Near(px[0],188,3));
    }
    // A different size is stretched rather than refused; multisampled input is refused.
    {
        const unsigned char blue[4]={0,0,255,255}, black[4]={0,0,0,255};
        D3D11_TEXTURE2D_DESC t{}; t.Width=t.Height=4; t.MipLevels=t.ArraySize=1; t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc.Count=1; t.BindFlags=srvBind; unsigned char pixels[16*4]; for(int i=0;i<16;++i) std::memcpy(pixels+i*4,blue,4);
        D3D11_SUBRESOURCE_DATA data{pixels,4*4,0}; ComPtr<ID3D11Texture2D> tiny; device->CreateTexture2D(&t,&data,&tiny);
        auto target=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,black);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),tiny.Get(),target.Get()));
        unsigned char px[4]{}; Read(device.Get(),context.Get(),target.Get(),px);
        CHECK(Near(px[2],255));
        CHECK(!edf6vr::CompositeOverlayImage(device.Get(),context.Get(),nullptr,target.Get()));
    }
    // Placement: shrunk to half about the centre, the middle keeps the overlay
    // and the edge column now samples outside it and keeps the world; moved
    // down by half the image, the middle samples outside too.
    {
        const unsigned char red[4]={255,0,0,255}, black[4]={0,0,0,255};
        auto overlay=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,srvBind,red);
        auto target=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,black);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),overlay.Get(),target.Get(),0.5f,0.5f,0.0f));
        unsigned char px[4]{}; Read(device.Get(),context.Get(),target.Get(),px);
        CHECK(Near(px[0],255));
        D3D11_TEXTURE2D_DESC t{}; target->GetDesc(&t); t.Usage=D3D11_USAGE_STAGING; t.BindFlags=0; t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging; device->CreateTexture2D(&t,nullptr,&staging); context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE m{}; context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m);
        const int edge=static_cast<unsigned char*>(m.pData)[4*m.RowPitch];
        context->Unmap(staging.Get(),0);
        CHECK(edge==0);
        auto moved=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,black);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),overlay.Get(),moved.Get(),1.0f,1.0f,0.6f));
        Read(device.Get(),context.Get(),moved.Get(),px);
        CHECK(px[0]==0);
        CHECK(!edf6vr::CompositeOverlayImage(device.Get(),context.Get(),overlay.Get(),moved.Get(),0.0f,1.0f,0.0f));
        // The debug tint paints coverage magenta regardless of the overlay's colour.
        auto tinted=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,black);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),overlay.Get(),tinted.Get(),1.0f,1.0f,0.0f,true));
        Read(device.Get(),context.Get(),tinted.Get(),px);
        CHECK(Near(px[0],255) && px[1]==0 && Near(px[2],255));
    }
    // Pause boards also restore captured UI to the multisampled desktop.
    {
        const unsigned char menu[4]={255,80,20,255},black[4]={0,0,0,255};
        auto overlay=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,srvBind,menu);
        D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=8;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc.Count=4;t.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> msaa;CHECK(SUCCEEDED(device->CreateTexture2D(&t,nullptr,&msaa)));
        ComPtr<ID3D11RenderTargetView> view;CHECK(SUCCEEDED(device->CreateRenderTargetView(msaa.Get(),nullptr,&view)));
        const float clear[4]={0,0,0,1};context->ClearRenderTargetView(view.Get(),clear);
        CHECK(edf6vr::CompositeOverlayImage(device.Get(),context.Get(),overlay.Get(),msaa.Get()));
        auto resolved=Make(device.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,rtvBind,black);
        context->ResolveSubresource(resolved.Get(),0,msaa.Get(),0,t.Format);
        unsigned char px[4]{};Read(device.Get(),context.Get(),resolved.Get(),px);
        CHECK(Near(px[0],255)&&Near(px[1],80)&&Near(px[2],20));
    }
    edf6vr::ReleaseOverlayComposite();
    printf("World UI composite: %d failures\n",failures);
    return failures?1:0;
}
