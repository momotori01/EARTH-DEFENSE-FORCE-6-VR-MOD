#include <cmath>
#include "ui_cluster.h"
#include <wrl/client.h>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

static ComPtr<ID3D11Texture2D> Make(ID3D11Device* d,UINT size,DXGI_FORMAT format,UINT bind,const unsigned char* pixels) {
    D3D11_TEXTURE2D_DESC t{}; t.Width=t.Height=size; t.MipLevels=t.ArraySize=1; t.Format=format; t.SampleDesc.Count=1;
    t.Usage=D3D11_USAGE_DEFAULT; t.BindFlags=bind;
    D3D11_SUBRESOURCE_DATA data{pixels,size*4,0};
    ComPtr<ID3D11Texture2D> out; d->CreateTexture2D(&t,pixels?&data:nullptr,&out); return out;
}
static void ReadAt(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* tex,UINT x,UINT y,unsigned char out[4]) {
    D3D11_TEXTURE2D_DESC t{}; tex->GetDesc(&t); t.Usage=D3D11_USAGE_STAGING; t.BindFlags=0; t.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging; d->CreateTexture2D(&t,nullptr,&staging); c->CopyResource(staging.Get(),tex);
    D3D11_MAPPED_SUBRESOURCE m{}; c->Map(staging.Get(),0,D3D11_MAP_READ,0,&m);
    std::memcpy(out,static_cast<unsigned char*>(m.pData)+y*m.RowPitch+x*4,4); c->Unmap(staging.Get(),0);
}
static bool Near(int a,int b,int tolerance=2) { return a-b<=tolerance && b-a<=tolerance; }

int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) {
        printf("no WARP device\n"); return 1;
    }
    const UINT srvBind=D3D11_BIND_SHADER_RESOURCE, rtvBind=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    // A 16x16 HUD: blue everywhere, a red block in the third quarter (u,v 0.5..0.75).
    unsigned char hud[16*16*4];
    for(UINT y=0;y<16;++y) for(UINT x=0;x<16;++x) {
        auto* p=hud+(y*16+x)*4; const bool red=x>=8 && x<12 && y>=8 && y<12;
        p[0]=red?255:0; p[1]=0; p[2]=red?0:255; p[3]=red?255:128;
    }
    auto source=Make(device.Get(),16,DXGI_FORMAT_R8G8B8A8_UNORM,srvBind,hud);
    // One item: the red block, doubled, fills a canvas half the HUD's size.
    {
        edf6vr::UiClusterLayout layout; layout.canvasWidth=0.5f; layout.canvasHeight=0.5f;
        layout.items[0]={{0.5f,0.5f,0.75f,0.75f},0,0,2.0f}; layout.count=1;
        auto target=Make(device.Get(),8,DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,nullptr);
        CHECK(edf6vr::ComposeUiCluster(device.Get(),context.Get(),source.Get(),target.Get(),layout));
        unsigned char px[4]{};
        ReadAt(device.Get(),context.Get(),target.Get(),1,1,px); CHECK(Near(px[0],255) && px[2]==0 && px[3]==255);
        ReadAt(device.Get(),context.Get(),target.Get(),6,6,px); CHECK(Near(px[0],255) && px[3]==255);
    }
    // Two items side by side at half size; what no item covers stays clear;
    // the blue's half alpha is copied, not blended.
    {
        edf6vr::UiClusterLayout layout; layout.canvasWidth=0.5f; layout.canvasHeight=0.5f;
        layout.items[0]={{0.5f,0.5f,0.75f,0.75f},0,0,1.0f};          // red, quarter of the canvas
        layout.items[1]={{0.0f,0.0f,0.25f,0.25f},0.5f,0.5f,1.0f};    // blue, bottom-right quarter
        layout.count=2;
        auto target=Make(device.Get(),8,DXGI_FORMAT_R8G8B8A8_UNORM,rtvBind,nullptr);
        CHECK(edf6vr::ComposeUiCluster(device.Get(),context.Get(),source.Get(),target.Get(),layout));
        unsigned char px[4]{};
        ReadAt(device.Get(),context.Get(),target.Get(),1,1,px); CHECK(Near(px[0],255) && px[3]==255);
        ReadAt(device.Get(),context.Get(),target.Get(),6,6,px); CHECK(px[0]==0 && Near(px[2],255) && Near(px[3],128));
        ReadAt(device.Get(),context.Get(),target.Get(),6,1,px); CHECK(px[3]==0);
        ReadAt(device.Get(),context.Get(),target.Get(),1,6,px); CHECK(px[3]==0);
    }
    // Overlapping pieces: the transparent part of a later piece leaves what an
    // earlier piece put underneath it.
    {
        unsigned char pic[16*16*4]{};
        for(UINT y=0;y<16;++y) for(UINT x=0;x<16;++x) {
            auto* p=pic+(y*16+x)*4;
            if(x<8) { p[1]=255; p[3]=255; }                      // left half solid green
            else if(x<12 && y<4) { p[0]=255; p[3]=255; }         // a red block at the top of the right half
        }
        auto tex=Make(device.Get(),16,DXGI_FORMAT_R8G8B8A8_UNORM,srvBind,pic);
        edf6vr::UiClusterLayout over; over.canvasWidth=1; over.canvasHeight=1;
        over.items[0]={{0.5f,0,0.75f,0.25f},0.75f,0.5f,1.0f};   // the red block, moved to the right middle
        over.items[1]={{0,0,1,1},0,0,1.0f};                       // then the whole picture over everything
        over.count=2;
        auto target=Make(device.Get(),8,DXGI_FORMAT_R8G8B8A8_UNORM,rtvBind,nullptr);
        CHECK(edf6vr::ComposeUiCluster(device.Get(),context.Get(),tex.Get(),target.Get(),over));
        unsigned char px[4]{};
        ReadAt(device.Get(),context.Get(),target.Get(),6,5,px); CHECK(Near(px[0],255) && px[3]==255);   // survived
        ReadAt(device.Get(),context.Get(),target.Get(),1,4,px); CHECK(Near(px[1],255) && px[3]==255);
        ReadAt(device.Get(),context.Get(),target.Get(),5,1,px); CHECK(Near(px[0],255) && px[3]==255);
        ReadAt(device.Get(),context.Get(),target.Get(),7,7,px); CHECK(px[3]==0);
    }
    // sRGB-typed source into a raw target keeps the bytes.
    {
        unsigned char grey[16*16*4]; for(int i=0;i<256;++i) { grey[i*4]=grey[i*4+1]=grey[i*4+2]=188; grey[i*4+3]=255; }
        auto srgb=Make(device.Get(),16,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,srvBind,grey);
        edf6vr::UiClusterLayout layout; layout.canvasWidth=1; layout.canvasHeight=1;
        layout.items[0]={{0,0,1,1},0,0,1.0f}; layout.count=1;
        auto target=Make(device.Get(),8,DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,nullptr);
        CHECK(edf6vr::ComposeUiCluster(device.Get(),context.Get(),srgb.Get(),target.Get(),layout));
        unsigned char px[4]{}; ReadAt(device.Get(),context.Get(),target.Get(),4,4,px); CHECK(Near(px[1],188,3));
    }
    // Cutting: the left half becomes transparent, the right half is untouched.
    {
        auto panel=Make(device.Get(),16,DXGI_FORMAT_R8G8B8A8_TYPELESS,rtvBind,hud);
        const edf6vr::UiRect left{0,0,0.5f,1};
        CHECK(edf6vr::CutUiRects(device.Get(),context.Get(),panel.Get(),&left,1));
        unsigned char px[4]{};
        ReadAt(device.Get(),context.Get(),panel.Get(),2,2,px); CHECK(px[0]==0 && px[2]==0 && px[3]==0);
        ReadAt(device.Get(),context.Get(),panel.Get(),9,9,px); CHECK(Near(px[0],255) && px[3]==255);
        ReadAt(device.Get(),context.Get(),panel.Get(),13,2,px); CHECK(Near(px[2],255) && Near(px[3],128));
        CHECK(!edf6vr::CutUiRects(device.Get(),context.Get(),panel.Get(),nullptr,1));
    }
    // Refusals: no items, a multisampled target.
    {
        edf6vr::UiClusterLayout empty;
        auto target=Make(device.Get(),8,DXGI_FORMAT_R8G8B8A8_UNORM,rtvBind,nullptr);
        CHECK(!edf6vr::ComposeUiCluster(device.Get(),context.Get(),source.Get(),target.Get(),empty));
    }
    edf6vr::ReleaseUiCluster();
    printf("UI cluster compose/cut: %d failures\n",failures);
    return failures?1:0;
}
