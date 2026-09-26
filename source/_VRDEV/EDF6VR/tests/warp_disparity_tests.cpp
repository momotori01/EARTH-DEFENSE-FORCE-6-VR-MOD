// Production warp shader on WARP: flat surfaces must move by baseline*focal/z.
#include "eye_warp.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cmath>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> d; ComPtr<ID3D11DeviceContext> c;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&c))) return 2;
    constexpr unsigned w=1024,h=32,x0=512,y0=16;
    std::vector<unsigned> pixels(w*h);
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x)
        pixels[y*w+x]=0xff000000u | static_cast<unsigned>((128+(static_cast<int>(x)-static_cast<int>(x0))*8)&255);
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> colour,depth,target,readback;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),w*4,0};
    if(FAILED(d->CreateTexture2D(&desc,&data,&colour)) || FAILED(d->CreateTexture2D(&desc,nullptr,&target))) return 2;
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(d->CreateTexture2D(&desc,nullptr,&readback))) return 2;
    desc.Format=DXGI_FORMAT_R32_FLOAT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.CPUAccessFlags=0;
    if(FAILED(d->CreateTexture2D(&desc,nullptr,&depth))) return 2;
    edf6vr::WarpParams p{}; p.uiMode=0; p.useHudless=false; p.nearestMetres=.15f;
    p.steps=64; p.verticalFovRadians=2*std::atan(static_cast<float>(h)/512.0f);
    std::vector<float> depths(w*h);
    int failures=0;
    std::vector<float> distances={4.0f,5.0f,8.0f,15.0f,20.0f,30.0f,100.0f};
    // Small movement across the last coarse interval must not double disparity.
    for(int i=0;i<=20;++i) distances.push_back(9.2f+static_cast<float>(i)*0.04f);
    for(float focal:{256.0f,300.0f,800.0f}) {
      p.verticalFovRadians=2*std::atan(static_cast<float>(h)/(2*focal));
      for(float z:distances) {
        const float nativeDepth=(p.farPlane-p.nearPlane*p.farPlane/z)/(p.farPlane-p.nearPlane);
        for(auto& v:depths) v=nativeDepth;
        c->UpdateSubresource(depth.Get(),0,nullptr,depths.data(),w*4,0);
        for(bool right:{false,true}) {
          p.buildRightOfDrawn=right;
          if(!edf6vr::WarpEye(d.Get(),c.Get(),colour.Get(),depth.Get(),target.Get(),nullptr,p)) return 2;
          c->CopyResource(readback.Get(),target.Get());
          D3D11_MAPPED_SUBRESOURCE m{};
          if(FAILED(c->Map(readback.Get(),0,D3D11_MAP_READ,0,&m))) return 2;
          const auto value=*reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(m.pData)+y0*m.RowPitch+x0*4);
          c->Unmap(readback.Get(),0);
          const float actual=(static_cast<float>(value&255)-128)/8;
          const float expected=(right?1:-1)*p.eyeSeparation*focal/z;
          if(std::fabs(actual-expected)>.15f) {
            ++failures; std::printf("FAIL focal=%.0f z=%.2f right=%d expected=%.3f actual=%.3f\n",focal,z,right,expected,actual);
          }
        }
      }
    }
    edf6vr::ReleaseWarp(); c->ClearState();
    std::printf("Warp flat-surface disparity: %d failures\n",failures);
    return failures?1:0;
}
