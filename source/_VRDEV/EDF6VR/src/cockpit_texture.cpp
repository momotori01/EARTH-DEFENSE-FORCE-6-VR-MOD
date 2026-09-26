#include "cockpit_texture.h"
#include <wincodec.h>
#include <vector>
namespace edf6vr {
bool LoadCockpitTexture(ID3D11Device* device,unsigned id,bool srgb,Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& result) noexcept {
    using Microsoft::WRL::ComPtr;
    HMODULE module=nullptr;
    if(!device||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LoadCockpitTexture),&module))return false;
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)return false;
    auto* bytes=static_cast<BYTE*>(LockResource(LoadResource(module,resource)));const auto size=SizeofResource(module,resource);
    if(!bytes||!size)return false;
    const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Apartment {HRESULT hr;~Apartment(){if(SUCCEEDED(hr))CoUninitialize();}} apartment{initialized};
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||
       FAILED(factory->CreateStream(&stream))||FAILED(stream->InitializeFromMemory(bytes,size))||
       FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder))||
       FAILED(decoder->GetFrame(0,&frame))||FAILED(factory->CreateFormatConverter(&converter))||
       FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return false;
    UINT w=0,h=0;if(FAILED(converter->GetSize(&w,&h))||!w||!h||w>4096||h>4096)return false;
    std::vector<BYTE> pixels(static_cast<size_t>(w)*h*4);
    if(FAILED(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data())))return false;
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.ArraySize=d.SampleDesc.Count=1;
    d.Format=srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
    d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;d.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ComPtr<ID3D11Texture2D> texture;
    if(FAILED(device->CreateTexture2D(&d,nullptr,&texture))||FAILED(device->CreateShaderResourceView(texture.Get(),nullptr,&result)))return false;
    ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
    context->UpdateSubresource(texture.Get(),0,nullptr,pixels.data(),w*4,0);context->GenerateMips(result.Get());return true;
}
}
