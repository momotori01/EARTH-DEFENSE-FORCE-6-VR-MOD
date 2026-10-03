#include "../src/ui_capture.cpp"
#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
using namespace edf6vr;
static int testFailures=0;
#define CHECK(x) do { if(!(x)) {printf("FAIL %d: %s\n",__LINE__,#x);++testFailures;} } while(false)
int main() {
    ID3D11Device* device=nullptr;ID3D11DeviceContext* ctx=nullptr;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&ctx)));if(!ctx) return 1;
    CHECK(InstallUiCapture(ctx));EnableUiCapture(true);
    const char shader[]=R"(
cbuffer Data : register(b0) {float4 tint;float4 z;}
float4 vs(uint id:SV_VertexID):SV_Position {
    float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),z.x,1);
}
float4 ps():SV_Target{return tint;}
)";
    ID3DBlob *v=nullptr,*p=nullptr,*err=nullptr;
    CHECK(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs","vs_4_0",0,0,&v,&err)));Release(err);
    CHECK(SUCCEEDED(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"ps","ps_4_0",0,0,&p,&err)));Release(err);
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;ID3D11Buffer* buffer=nullptr;
    CHECK(SUCCEEDED(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs)));
    CHECK(SUCCEEDED(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps)));
    Release(v);Release(p);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=32;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&buffer)));
    ctx->VSSetShader(vs,nullptr,0);ctx->PSSetShader(ps,nullptr,0);
    ctx->VSSetConstantBuffers(0,1,&buffer);ctx->PSSetConstantBuffers(0,1,&buffer);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DepthStencilState *flat=nullptr,*tested=nullptr;
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=false;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;ds.DepthFunc=D3D11_COMPARISON_ALWAYS;
    CHECK(SUCCEEDED(device->CreateDepthStencilState(&ds,&flat)));
    ds.DepthEnable=true;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS;
    CHECK(SUCCEEDED(device->CreateDepthStencilState(&ds,&tested)));
    ID3D11BlendState *straight=nullptr,*premultiplied=nullptr;
    D3D11_BLEND_DESC blend{};auto& b=blend.RenderTarget[0];b.BlendEnable=true;
    b.SrcBlend=D3D11_BLEND_SRC_ALPHA;b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;b.BlendOp=D3D11_BLEND_OP_ADD;
    b.SrcBlendAlpha=D3D11_BLEND_ONE;b.DestBlendAlpha=D3D11_BLEND_ZERO;b.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    CHECK(SUCCEEDED(device->CreateBlendState(&blend,&straight)));
    b.SrcBlend=D3D11_BLEND_ONE;CHECK(SUCCEEDED(device->CreateBlendState(&blend,&premultiplied)));
    auto paint=[&](float r,float g,float blue,float a,float depth) {
        const float data[8]={r,g,blue,a,depth,0,0,0};ctx->UpdateSubresource(buffer,0,nullptr,data,0,0);ctx->Draw(3,0);
    };
    auto pixel=[&](ID3D11Texture2D* image,float out[4]) {
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
        auto single=desc;single.SampleDesc={1,0};single.BindFlags=0;
        ID3D11Texture2D* resolved=nullptr;
        if(desc.SampleDesc.Count>1) {
            CHECK(SUCCEEDED(device->CreateTexture2D(&single,nullptr,&resolved)));
            ctx->ResolveSubresource(resolved,0,image,0,desc.Format);image=resolved;
        }
        single.Usage=D3D11_USAGE_STAGING;single.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging=nullptr;CHECK(SUCCEEDED(device->CreateTexture2D(&single,nullptr,&staging)));
        ctx->CopyResource(staging,image);D3D11_MAPPED_SUBRESOURCE map{};
        CHECK(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&map)));
        auto* bytes=static_cast<unsigned char*>(map.pData)+(desc.Height/2)*map.RowPitch+(desc.Width/2)*4;
        for(int i=0;i<4;++i) out[i]=bytes[i]/255.0f;
        ctx->Unmap(staging,0);Release(staging);Release(resolved);
    };
    // All cases keep a REAL depth view bound, the condition that disabled the
    // old capture. Resize/MSAA/straight vs premultiplied native RGB are covered.
    for(unsigned samples:{1u,4u,1u}) for(bool premult:{false,true}) {
        const UINT size=samples==4?32u:16u;
        ID3D11Texture2D *colour=nullptr,*depth=nullptr;ID3D11RenderTargetView* target=nullptr;ID3D11DepthStencilView* dsv=nullptr;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc={samples,0};desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&colour)));
        CHECK(SUCCEEDED(device->CreateRenderTargetView(colour,nullptr,&target)));
        desc.Format=DXGI_FORMAT_D32_FLOAT;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&depth)));
        CHECK(SUCCEEDED(device->CreateDepthStencilView(depth,nullptr,&dsv)));
        auto* originalBlend=premult?premultiplied:straight;
        const FLOAT factors[4]={.1f,.2f,.3f,.4f};
        ctx->OMSetBlendState(originalBlend,factors,~0u);ctx->OMSetDepthStencilState(flat,17);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(size),static_cast<float>(size),0,1};ctx->RSSetViewports(1,&viewport);
        const FLOAT blue[4]={0,0,1,1};ctx->ClearRenderTargetView(target,blue);ctx->ClearDepthStencilView(dsv,D3D11_CLEAR_DEPTH,.2f,0);
        ID3D11RenderTargetView* slots[8]={target};
        const UINT slotCount=premult?8u:1u;
        ctx->OMSetRenderTargets(slotCount,slots,dsv);UiCaptureTargets(ctx,slotCount,slots,dsv,true);
        CHECK(!routed); // no prior-frame binding prediction may steal the world
        UiCaptureGate();CHECK(routed);
        paint(0,premult?.5f:1,0,.5f,0);
        paint(premult?.5f:1,0,0,.5f,0);
        ID3D11DepthStencilView* retained=nullptr;ID3D11RenderTargetView* bound=nullptr;
        ctx->OMGetRenderTargets(1,&bound,&retained);CHECK(bound==drawTarget && retained==dsv);Release(bound);Release(retained);
        // Scene-depth-tested markers remain in the world and are OCCLUDED.
        ctx->OMSetDepthStencilState(tested,23);CHECK(!routed);
        paint(1,1,1,1,.8f);float rgba[4]{};pixel(colour,rgba);CHECK(rgba[2]>.99f && rgba[0]<.01f);
        paint(1,1,1,1,.1f);pixel(colour,rgba);CHECK(rgba[0]>.99f && rgba[1]>.99f);
        ctx->OMSetDepthStencilState(flat,17);CHECK(routed);
        // A native blend change is tracked and replaced only on the private UI.
        ctx->OMSetBlendState(originalBlend,factors,~0u);
        FinishUiCapture();CHECK(CapturedUi()!=nullptr);
        pixel(CapturedUi(),rgba);
        CHECK(std::fabs(rgba[0]-.5f)<.015f && std::fabs(rgba[1]-.25f)<.015f);
        CHECK(rgba[2]<.01f && std::fabs(rgba[3]-.75f)<.015f); // no blue world baked into translucent UI
        ctx->OMGetRenderTargets(1,&bound,&retained);CHECK(bound==target && retained==dsv);Release(bound);Release(retained);
        ID3D11BlendState* restored=nullptr;FLOAT f[4]{};UINT mask=0;ctx->OMGetBlendState(&restored,f,&mask);
        CHECK(restored==originalBlend && mask==~0u && !std::memcmp(f,factors,sizeof f));Release(restored);
        ID3D11DepthStencilState* restoredDepth=nullptr;UINT ref=0;ctx->OMGetDepthStencilState(&restoredDepth,&ref);
        CHECK(restoredDepth==flat && ref==17);Release(restoredDepth);
        CHECK(!nativeTarget && !nativeDepth && !nativeBlend); // no swapchain references over frame boundary
        {
            PauseUiCapture compositor;
            ctx->OMSetRenderTargets(1,&target,dsv);UiCaptureTargets(ctx,1,&target,dsv,true);
            UiCaptureGate();ctx->OMSetDepthStencilState(flat,17);ctx->OMSetBlendState(originalBlend,factors,~0u);
            CHECK(!nativeTarget && !routed && CapturedUi()!=nullptr);
        }
        FinishUiCapture();CHECK(CapturedUi()==nullptr); // blank/menu frame cannot reuse previous HUD
        EnableUiCapture(false);UiCaptureTargets(ctx,1,&target,dsv,true);UiCaptureGate();CHECK(!routed);
        FinishUiCapture();CHECK(CapturedUi()==nullptr);EnableUiCapture(true);
        // Offscreen passes and genuine MRT passes must never be redirected.
        ctx->OMSetRenderTargets(1,&target,dsv);UiCaptureTargets(ctx,1,&target,dsv,false);
        UiCaptureGate();CHECK(!routed);FinishUiCapture();CHECK(CapturedUi()==nullptr);
        ID3D11RenderTargetView* mrt[2]={target,drawTarget};
        ctx->OMSetRenderTargets(2,mrt,dsv);UiCaptureTargets(ctx,2,mrt,dsv,true);
        UiCaptureGate();CHECK(!routed);FinishUiCapture();CHECK(CapturedUi()==nullptr);
        ID3D11RenderTargetView* actual[2]{};ctx->OMGetRenderTargets(2,actual,nullptr);
        CHECK(actual[0]==target && actual[1]==drawTarget);Release(actual[0]);Release(actual[1]);
        ctx->OMSetRenderTargets(0,nullptr,nullptr);
        Release(target);Release(dsv);Release(colour);Release(depth);
    }
    // World scope: a nameplate whose own state manager binds another target (an
    // offscreen one, then the back buffer) partway through still lands in the
    // world image, and the game gets its own binding back on exit.
    {
        const UINT size=16;
        ID3D11Texture2D *back=nullptr,*offscreen=nullptr;ID3D11RenderTargetView *backView=nullptr,*offView=nullptr;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=size;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc={1,0};desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&back)));CHECK(SUCCEEDED(device->CreateRenderTargetView(back,nullptr,&backView)));
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&offscreen)));CHECK(SUCCEEDED(device->CreateRenderTargetView(offscreen,nullptr,&offView)));
        const FLOAT none[4]{},blueWorld[4]={0,0,1,1};
        ctx->ClearRenderTargetView(backView,blueWorld);ctx->ClearRenderTargetView(offView,none);
        const FLOAT factors[4]={1,1,1,1};
        ctx->OMSetBlendState(premultiplied,factors,~0u);ctx->OMSetDepthStencilState(flat,0);
        D3D11_VIEWPORT viewport{0,0,float(size),float(size),0,1};ctx->RSSetViewports(1,&viewport);
        ctx->OMSetRenderTargets(1,&backView,nullptr);UiCaptureTargets(ctx,1,&backView,nullptr,true);
        UiCaptureGate();CHECK(routed);
        paint(0,.5f,0,.5f,0); // a screen-fixed HUD element
        UiCaptureWorldScope(true);
        ID3D11RenderTargetView* bound=nullptr;
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound && bound==worldTarget);Release(bound);
        ctx->OMSetRenderTargets(1,&offView,nullptr);UiCaptureTargets(ctx,1,&offView,nullptr,false);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound && bound==worldTarget);Release(bound);
        ctx->OMSetBlendState(straight,factors,~0u);
        ctx->OMSetRenderTargets(1,&backView,nullptr);UiCaptureTargets(ctx,1,&backView,nullptr,true);
        // The game's HUD gate is evaluated inside the plate draw; it must not
        // pull the binding back to the panel (this is what lost a class per frame).
        UiCaptureGate();ctx->OMSetDepthStencilState(flat,0);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound && bound==worldTarget);Release(bound);
        paint(1,0,0,1,0); // the nameplate
        UiCaptureWorldScope(false);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound==drawTarget);Release(bound); // back to the panel
        CHECK(routed);
        ID3D11BlendState* restored=nullptr;FLOAT f[4]{};UINT mask=0;ctx->OMGetBlendState(&restored,f,&mask);
        CHECK(restored!=straight);Release(restored); // the panel's alpha copy of the game's last blend
        float rgba[4]{};
        pixel(offscreen,rgba);CHECK(rgba[0]<.01f && rgba[3]<.01f);
        pixel(back,rgba);CHECK(rgba[2]>.99f && rgba[0]<.01f);
        FinishUiCapture();
        CHECK(CapturedWorldUi()!=nullptr && CapturedUi()!=nullptr);
        if(CapturedWorldUi()) { pixel(CapturedWorldUi(),rgba);CHECK(rgba[0]>.99f && rgba[3]>.99f && rgba[1]<.01f); }
        if(CapturedUi()) { pixel(CapturedUi(),rgba);CHECK(rgba[0]<.01f && std::fabs(rgba[1]-.5f)<.015f); }
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound==backView);Release(bound);
        // A nameplate that leaves the game on another target: the game keeps it
        // and the panel capture lets go instead of drawing that target's draws.
        ctx->OMSetRenderTargets(1,&backView,nullptr);UiCaptureTargets(ctx,1,&backView,nullptr,true);
        UiCaptureGate();CHECK(routed);
        UiCaptureWorldScope(true);
        ctx->OMSetRenderTargets(1,&offView,nullptr);UiCaptureTargets(ctx,1,&offView,nullptr,false);
        UiCaptureWorldScope(false);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound==offView);Release(bound);
        CHECK(!routed && !nativeTarget);
        FinishUiCapture();
        // Switched off, a scope leaves the binding alone.
        EnableWorldUi(false);
        ctx->OMSetRenderTargets(1,&backView,nullptr);UiCaptureTargets(ctx,1,&backView,nullptr,true);
        UiCaptureGate();UiCaptureWorldScope(true);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound==drawTarget);Release(bound);
        UiCaptureWorldScope(false);FinishUiCapture();CHECK(CapturedWorldUi()==nullptr);
        EnableWorldUi(true);
        // Subtitle scope: left on the panel until a cockpit marks it; then its
        // draws land in their own picture, not on the panel nor in the world's.
        ctx->OMSetBlendState(premultiplied,factors,~0u);ctx->OMSetDepthStencilState(flat,0);
        ctx->OMSetRenderTargets(1,&backView,nullptr);UiCaptureTargets(ctx,1,&backView,nullptr,true);
        UiCaptureGate();CHECK(routed);
        subtitleMarkedAt.store(0);
        UiCaptureSubtitleScope(true);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound==drawTarget);Release(bound);
        UiCaptureSubtitleScope(false);
        MarkSubtitleUi();CHECK(SubtitleUiActive());
        UiCaptureSubtitleScope(true);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound && bound==scopeTargets[1].target);Release(bound);
        paint(0,0,1,1,0); // a subtitle line
        UiCaptureSubtitleScope(false);
        ctx->OMGetRenderTargets(1,&bound,nullptr);CHECK(bound==drawTarget);Release(bound);
        FinishUiCapture();
        CHECK(CapturedSubtitleUi()!=nullptr && CapturedWorldUi()==nullptr);
        if(CapturedSubtitleUi()) { pixel(CapturedSubtitleUi(),rgba);CHECK(rgba[2]>.99f && rgba[3]>.99f && rgba[0]<.01f); }
        if(CapturedUi()) { pixel(CapturedUi(),rgba);CHECK(rgba[2]<.01f); }
        CHECK(ReadSubtitleUiStats().frames>0);
        subtitleMarkedAt.store(0);CHECK(!SubtitleUiActive());
        ctx->OMSetRenderTargets(0,nullptr,nullptr);
        Release(backView);Release(offView);Release(back);Release(offscreen);
    }
    CHECK(failures==0);
    // Unhook before releasing our private context. The production context lives
    // for the game session; these pointers must never target freed test memory.
    bool changed=false;
    ReplacePointer(&dispatch[35],reinterpret_cast<void*>(&BlendHook),reinterpret_cast<void*>(blendOriginal),changed);
    ReplacePointer(&dispatch[36],reinterpret_cast<void*>(&DepthHook),reinterpret_cast<void*>(depthOriginal),changed);
    ctx->ClearState();Release(drawTarget);Release(drawTexture);Release(resolvedTexture);
    Release(worldTarget);Release(worldTexture);Release(worldResolved);Release(worldProbe);
    for(auto& entry:blendCopies) {Release(entry.source);Release(entry.result);}
    Release(buffer);Release(vs);Release(ps);Release(flat);Release(tested);Release(straight);Release(premultiplied);
    Release(ctx);Release(device);
    printf("UI capture alpha/depth/MSAA/restore: %d failures\n",testFailures);
    return testFailures?1:0;
}
