#include "cockpit_lighting.h"
namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
CockpitLighting cached;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
std::uint64_t capturedFrame=0;
bool CopyConstants(ID3D11DeviceContext* ctx,ID3D11Buffer* source,ComPtr<ID3D11Buffer>& out) {
    D3D11_BUFFER_DESC d{},old{};source->GetDesc(&d);if(out)out->GetDesc(&old);
    if(!out||old.ByteWidth!=d.ByteWidth) {
        out.Reset();d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags=d.MiscFlags=d.StructureByteStride=0;
        if(FAILED(device->CreateBuffer(&d,nullptr,&out)))return false;
    }
    ctx->CopyResource(out.Get(),source);return true;
}
}
void DiscardCockpitLighting() noexcept {cached.cubes.Reset();cached.grid.Reset();context.Reset();capturedFrame=0;}
void ReleaseCockpitLighting() noexcept {DiscardCockpitLighting();cached={};device.Reset();}
bool CaptureCockpitLighting(ID3D11DeviceContext* ctx,std::uint64_t frame,UINT x,UINT y,UINT z) noexcept {
    if(!ctx||!frame||z!=1||ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
    ComPtr<ID3D11Device> d;ctx->GetDevice(&d);
    if(d!=device){ReleaseCockpitLighting();device=d;}
    ComPtr<ID3D11Buffer> system,extra;ctx->CSGetConstantBuffers(0,1,&system);ctx->CSGetConstantBuffers(2,1,&extra);
    D3D11_BUFFER_DESC sd{},ed{};if(system)system->GetDesc(&sd);if(extra)extra->GetDesc(&ed);
    ComPtr<ID3D11UnorderedAccessView> output;ctx->CSGetUnorderedAccessViews(0,1,&output);
    ComPtr<ID3D11Resource> resource;ComPtr<ID3D11Texture2D> texture;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};D3D11_TEXTURE2D_DESC td{};
    if(output){output->GetDesc(&ud);output->GetResource(&resource);if(SUCCEEDED(resource.As(&texture)))texture->GetDesc(&td);}
    if(sd.ByteWidth!=608||ed.ByteWidth<400||ed.ByteWidth>4096||
       ud.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||ud.ViewDimension!=D3D11_UAV_DIMENSION_TEXTURE2D||
       td.SampleDesc.Count!=1||td.ArraySize!=1||x!=(td.Width+15)/16||y!=(td.Height+15)/16)return false;
    capturedFrame=0;
    if(!CopyConstants(ctx,system.Get(),cached.system)||!CopyConstants(ctx,extra.Get(),cached.extra))return false;
    cached.cubes.Reset();cached.grid.Reset();
    ComPtr<ID3D11ShaderResourceView> cubes,grid;ComPtr<ID3D11Buffer> environment;
    ctx->CSGetShaderResources(8,1,&cubes);ctx->CSGetShaderResources(10,1,&grid);ctx->CSGetConstantBuffers(10,1,&environment);
    D3D11_SHADER_RESOURCE_VIEW_DESC cd{},gd{};D3D11_BUFFER_DESC envd{},gridDesc{};
    ComPtr<ID3D11Resource> gridResource;ComPtr<ID3D11Buffer> gridBuffer;
    if(cubes)cubes->GetDesc(&cd);
    if(grid){grid->GetDesc(&gd);grid->GetResource(&gridResource);if(SUCCEEDED(gridResource.As(&gridBuffer)))gridBuffer->GetDesc(&gridDesc);}
    if(environment)environment->GetDesc(&envd);
    // Confirm the actual native layout: cube array and 80-byte LocalEnvGrid.
    if(cd.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBEARRAY&&cd.TextureCubeArray.NumCubes>0&&
       gd.ViewDimension==D3D11_SRV_DIMENSION_BUFFER&&gridDesc.StructureByteStride==80&&
       gd.Buffer.NumElements>0&&envd.ByteWidth>=96&&envd.ByteWidth<=1024&&CopyConstants(ctx,environment.Get(),cached.environment)) {
        cached.cubes=cubes;cached.grid=grid;
    }
    context=ctx;capturedFrame=frame;return true;
}
CockpitLighting GetCockpitLighting(ID3D11DeviceContext* ctx,std::uint64_t frame) noexcept {
    return frame&&frame==capturedFrame&&ctx==context.Get()?cached:CockpitLighting{};
}
}
