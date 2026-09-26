#include "eye_warp.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <array>
#include <vector>
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,&level,&context))) return 2;
    constexpr unsigned side=64;
    std::array<unsigned,side*side> pixels{},clean{};
    clean.fill(0xff000000); pixels=clean;
    pixels[32*side+32]=0xff000001; // one-code faint red residual, below UI threshold
    pixels[2*side+2]=0xff00ff00; // unrelated scene/UI outside the cut must survive
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=desc.Height=side; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> colour,hudless,target,readback,depth;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),side*4,0};
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,&data,&colour)));
    data.pSysMem=clean.data(); CHECK(SUCCEEDED(device->CreateTexture2D(&desc,&data,&hudless)));
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    std::array<float,side*side> depths{}; depths.fill(0.5f);
    desc.Format=DXGI_FORMAT_R32_FLOAT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.CPUAccessFlags=0;
    data.pSysMem=depths.data(); CHECK(SUCCEEDED(device->CreateTexture2D(&desc,&data,&depth)));
    if(failures) return 1;
    auto read=[&](unsigned x,unsigned y) {
        context->CopyResource(readback.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if(FAILED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped))) { ++failures; return 0u; }
        const auto value=*reinterpret_cast<unsigned*>(static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4);
        context->Unmap(readback.Get(),0); return value;
    };
    edf6vr::WarpParams p{}; p.exact=true; p.eraseOnly=true; p.uiMode=1;
    p.reticleHalf[0]=p.reticleHalf[1]=0.125f;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),hudless.Get(),p));
    CHECK((read(32,32)&255)==1); // reproduce old pass retaining faint pixel
    p.eraseReticle=true;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),hudless.Get(),p));
    CHECK((read(32,32)&0xffffff)==0); CHECK((read(2,2)&0xffffff)==0xff00);
    // Exercise the other-eye shader too, with zero disparity to check exact pixels.
    p.eraseOnly=false; p.eyeSeparation=0; p.verticalFovRadians=1.5f;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),depth.Get(),target.Get(),hudless.Get(),p));
    CHECK((read(32,32)&0xffffff)==0); CHECK((read(2,2)&0xffffff)==0xff00);
    // No exact underlay: do not erase the world using an approximate tone curve.
    p.eraseOnly=true;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK((read(32,32)&255)==1);
    p.exact=false;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),hudless.Get(),p));
    CHECK((read(32,32)&255)==1);
    // A 1080p 96px reticle becomes 192px at 4K. The old fixed 96px cut
    // strands the outer marker on the HUD. Exercise actual direct-UI shaders.
    constexpr unsigned w=3840,h=2160;
    std::vector<unsigned> hud(w*h,0);
    hud[(h/2)*w+w/2+72]=0xff0000ff;
    hud[(h/2)*w+w/2+170]=0xff0000ff; // larger Sight13-style outer arm
    hud[(h/2)*w+w/2+250]=0xff00ff00; // neighbouring HUD outside enlarged cut
    hud[2*w+2]=0xff00ff00;
    colour.Reset(); target.Reset(); readback.Reset();
    desc={}; desc.Width=w; desc.Height=h;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    data={hud.data(),w*4,0};
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,&data,&colour)));
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    p={}; p.uiOnly=true; p.uiDirect=true;
    p.reticleHalf[0]=48.0f/w; p.reticleHalf[1]=48.0f/h;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK(read(w/2+72,h/2)==0xff0000ff); // reproduce old stranded outer reticle
    p.reticleHalf[0]=96.0f/w; p.reticleHalf[1]=96.0f/h;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK(read(w/2+72,h/2)==0); CHECK(read(2,2)==0xff00ff00);
    CHECK(read(w/2+170,h/2)==0xff0000ff); // 192px still strands the larger outer arm
    // Those removed pixels must still exist on the separate reticle image.
    target.Reset(); readback.Reset(); desc.Width=desc.Height=192;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.CPUAccessFlags=0;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    p.reticleOnly=true;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK((read(168,96)&255)>240);
    // 208 at 1080p -> 416 at 4K. Keep the outer arm in the aim layer only.
    target.Reset(); readback.Reset(); desc.Width=w; desc.Height=h;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.CPUAccessFlags=0;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    p.reticleOnly=false;p.reticleHalf[0]=208.0f/w;p.reticleHalf[1]=208.0f/h;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK(read(w/2+170,h/2)==0 && read(w/2+250,h/2)==0xff00ff00);
    CHECK(read(2,2)==0xff00ff00);
    target.Reset(); readback.Reset(); desc.Width=desc.Height=416;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.CPUAccessFlags=0;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    p.reticleOnly=true;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK((read(378,208)&255)>240 && (read(280,208)&255)>240);
    // Actual 0.114.4 captures have an entirely zero RGBA centre. Reproduce the
    // missing glyph, then exercise the native VR sight without restoring world warp.
    std::fill(hud.begin(),hud.end(),0);
    context->UpdateSubresource(colour.Get(),0,nullptr,hud.data(),w*4,0);
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK(read(208,208)==0 && read(244,208)==0);
    p.drawReticle=true;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK((read(208,208)&255)>100 && (read(244,208)>>24)>100);
    CHECK(read(0,0)==0 && read(378,208)==0); // no old glyph/world pixels or opaque canvas
    p.lockReticle=true;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK((read(244,244)>>24)>100 && read(244,208)==0); // lock corner replaces the ordinary cross arm
    CHECK((read(208,208)&255)>100); // aiming centre never moves
    p.lockReticle=false;
    p.drawReticle=false;
    CHECK(edf6vr::WarpEye(device.Get(),context.Get(),colour.Get(),nullptr,target.Get(),nullptr,p));
    CHECK(read(208,208)==0); // vehicle/legacy path still follows the original image
    edf6vr::ReleaseWarp(); context->ClearState();
    std::printf("Reticle residual GPU tests: %d failures\n",failures);
    return failures?1:0;
}
