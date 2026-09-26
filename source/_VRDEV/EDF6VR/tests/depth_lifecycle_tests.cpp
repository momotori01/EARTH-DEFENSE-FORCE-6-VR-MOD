// Exercise the production selector and resource ownership with real D3D11
// textures. No game, headset, or runtime hooks are needed.
#include "../src/depth_probe.cpp"
#include <cstdio>
using namespace edf6vr;
static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)

// Model a COM view address recycled by the game for a new resource. Only the
// two methods used by Resolve are needed; all other methods are ordinary COM.
struct ReusedView final : ID3D11DepthStencilView {
    ID3D11DepthStencilView* backing=nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** d) override { backing->GetDevice(d); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g,UINT* n,void* d) override { return backing->GetPrivateData(g,n,d); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g,UINT n,const void* d) override { return backing->SetPrivateData(g,n,d); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g,const IUnknown* d) override { return backing->SetPrivateDataInterface(g,d); }
    void STDMETHODCALLTYPE GetResource(ID3D11Resource** r) override { backing->GetResource(r); }
    void STDMETHODCALLTYPE GetDesc(D3D11_DEPTH_STENCIL_VIEW_DESC* d) override { backing->GetDesc(d); }
};

void TestDirectUiWithoutLegacyCopies(ID3D11Device* device,ID3D11DeviceContext* ctx) {
    ID3D11Texture2D* colour=nullptr;
    ID3D11RenderTargetView* target=nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=64;desc.Height=48;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&colour)));
    CHECK(SUCCEEDED(device->CreateRenderTargetView(colour,nullptr,&target)));
    if(!colour || !target) { if(target)target->Release();if(colour)colour->Release();return; }
    auto** table=*reinterpret_cast<void***>(ctx);
    g_original=reinterpret_cast<OMSetRenderTargetsFn>(table[kOMSetRenderTargetsSlot]);
    CHECK(InstallUiCapture(ctx));
    EnableUiCapture(false);
    SetLegacyPreUiCapture(true);
    NoteBackBuffer(colour); // Register the real presented image, start fresh.
    auto bind=[&]() { HookOMSetRenderTargets(ctx,1,&target,nullptr); };
    auto expectNative=[&](bool native) {
        ID3D11RenderTargetView* bound=nullptr;ctx->OMGetRenderTargets(1,&bound,nullptr);
        CHECK((bound==target)==native);if(bound)bound->Release();
    };
    auto legacyFrame=[&](float channel) {
        const float blue[]={0,0,channel,1},green[]={0,channel,0,1},red[]={channel,0,0,1};
        ctx->ClearRenderTargetView(target,blue);bind();
        ctx->ClearRenderTargetView(target,green);bind();
        ctx->ClearRenderTargetView(target,red);bind();
        NoteBackBuffer(colour);
    };
    legacyFrame(1);
    CHECK(PreUiHeld()==3);CHECK(PreUiBinds()==3);CHECK(PreUiColour()!=nullptr);
    CHECK(g_pickFinal!=nullptr && g_pickStrip!=nullptr);
    CHECK(g_preUiChosen!=0); // Actual legacy classifier ran on the real WARP GPU.

    SetLegacyPreUiCapture(false);
    CHECK(PreUiColour()==nullptr && PreUiSnapshot(0)==nullptr && PreUiHeld()==0);
    NoteBackBuffer(colour); // Applies the request and releases old private images.
    CHECK(!g_legacyPreUiActive);
    for(auto* image:g_preUiRing) CHECK(image==nullptr);
    CHECK(g_pickFinal==nullptr && g_pickStrip==nullptr && g_pickFlat==nullptr);
    EnableUiCapture(true);
    bind();NoteUiGate(true);expectNative(true);  // Before third bind: not the HUD.
    bind();NoteUiGate(true);expectNative(true);
    bind();NoteUiGate(true);expectNative(false); // Same native HUD gate still arms.
    CHECK(g_preUiBinds==3 && g_preUiTaken==0);
    const auto copies=g_hudlessCopies;
    KeepSceneColour(target);TakeHudless(ctx);
    CHECK(g_sceneColour==nullptr && g_hudlessCopies==copies);
    NoteBackBuffer(colour);
    CHECK(UiTexture()!=nullptr);expectNative(true);
    CHECK(PreUiBinds()==3 && PreUiHeld()==0 && PreUiColour()==nullptr);
    CHECK(g_pickFinal==nullptr && g_pickStrip==nullptr && g_preUiChosen==0);
    CHECK(g_preUiTaken==0); // No legacy copy or classifier was required for HUD.
    NoteBackBuffer(colour); // No HUD draws: never expose the previous HUD image.
    CHECK(UiTexture()==nullptr && PreUiColour()==nullptr);

    EnableUiCapture(false);
    SetLegacyPreUiCapture(true);
    CHECK(PreUiColour()==nullptr && PreUiHeld()==0); // Not an old pre-disable image.
    NoteBackBuffer(colour);
    CHECK(g_legacyPreUiActive && PreUiHeld()==0);
    legacyFrame(.5f);
    CHECK(PreUiHeld()==3 && PreUiBinds()==3 && PreUiColour()!=nullptr);
    CHECK(g_preUiPick==2); // Newly captured red final pass, not a cached old pick.
    ID3D11Texture2D* staging=nullptr;
    auto readDesc=desc;readDesc.BindFlags=0;readDesc.Usage=D3D11_USAGE_STAGING;
    readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    CHECK(SUCCEEDED(device->CreateTexture2D(&readDesc,nullptr,&staging)));
    if(staging && PreUiColour()) {
        ctx->CopyResource(staging,PreUiColour());D3D11_MAPPED_SUBRESOURCE mapped{};
        CHECK(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&mapped)));
        if(mapped.pData) {
            const auto* pixel=static_cast<const unsigned char*>(mapped.pData);
            CHECK(pixel[0]>=127 && pixel[0]<=128 && pixel[1]==0 && pixel[2]==0);
            ctx->Unmap(staging,0);
        }
    }
    if(staging)staging->Release();
    // Rapid requests discard any partially collected frame as well.
    bind();SetLegacyPreUiCapture(false);SetLegacyPreUiCapture(true);
    CHECK(PreUiHeld()==0 && PreUiColour()==nullptr);
    NoteBackBuffer(colour);CHECK(PreUiHeld()==0);
    SetLegacyPreUiCapture(false);NoteBackBuffer(nullptr);
    ctx->OMSetRenderTargets(0,nullptr,nullptr);
    target->Release();colour->Release();
}
int main() {
    ID3D11Device* device=nullptr; ID3D11DeviceContext* ctx=nullptr;
    D3D_FEATURE_LEVEL level{};
    const HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,&level,&ctx);
    CHECK(SUCCEEDED(hr)); if(!device) return 1;
    auto make=[&](UINT width,UINT height,ID3D11Texture2D** image,ID3D11DepthStencilView** view) {
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=1;d.ArraySize=1;
        d.Format=DXGI_FORMAT_R32_TYPELESS;d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
        CHECK(SUCCEEDED(device->CreateTexture2D(&d,nullptr,image)));
        D3D11_DEPTH_STENCIL_VIEW_DESC v{};v.Format=DXGI_FORMAT_D32_FLOAT;
        v.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        CHECK(SUCCEEDED(device->CreateDepthStencilView(*image,&v,view)));
    };
    ID3D11Texture2D *old=nullptr,*fresh=nullptr,*shadow=nullptr;
    ID3D11DepthStencilView *oldView=nullptr,*freshView=nullptr,*shadowView=nullptr;
    make(64,48,&old,&oldView);make(64,48,&fresh,&freshView);make(32,32,&shadow,&shadowView);
    g_installed.store(true);
    auto draw=[&](ID3D11DepthStencilView* view,unsigned count) {
        g_current.store(Resolve(view));
        for(unsigned i=0;i<count;++i) NoteModelDraw();
    };
    // A long sortie must not outweigh even the FIRST frame of the next one.
    for(int f=0;f<20;++f) { draw(oldView,1000); NoteBackBuffer(old); CHECK(SceneDepth()==old); }
    draw(freshView,1);draw(shadowView,2000);NoteBackBuffer(fresh);
    CHECK(SceneDepth()==fresh);CHECK(g_sceneFrameDraws==1);
    // Same DSV address, different backing texture: no report/reset needed.
    ReusedView recycled;recycled.backing=oldView;const int a=Resolve(&recycled);
    recycled.backing=freshView;const int b=Resolve(&recycled);CHECK(a!=b);
    CHECK(g_targets[b].held==fresh);
    // A menu/loading frame never reuses a previous mission's depth or colour.
    g_hudless=old;old->AddRef();g_hudlessRow=a;g_hudlessFrame=g_completedDepthFrame-1;
    CHECK(SceneColour()==nullptr);
    NoteBackBuffer(fresh);CHECK(SceneDepth()==nullptr);CHECK(SceneColour()==nullptr);
    // A report must not choose a lifetime winner or alter the selected image.
    draw(freshView,2);NoteBackBuffer(fresh);
    ReportDepthProbe([](const char*) {});CHECK(SceneDepth()==fresh);
    oldView->Release();old->Release();freshView->Release();fresh->Release();
    shadowView->Release();shadow->Release();
    // More than 32 successive resource sets: inactive rows are reclaimed and
    // the scene never becomes permanently unavailable after many missions.
    for(int mission=0;mission<70;++mission) {
        ID3D11Texture2D* image=nullptr;ID3D11DepthStencilView* view=nullptr;
        make(64,48,&image,&view);draw(view,1);NoteBackBuffer(image);
        CHECK(SceneDepth()==image);view->Release();image->Release();
    }
    CHECK(g_targetOverflow==0);CHECK(g_targetCount<10);
    // Resize: a smaller old target with more draws must not win by count.
    make(64,48,&old,&oldView);make(128,96,&fresh,&freshView);
    draw(oldView,100);draw(freshView,1);NoteBackBuffer(fresh);CHECK(SceneDepth()==fresh);
    oldView->Release();old->Release();freshView->Release();fresh->Release();
    g_hudless->Release();g_hudless=nullptr;
    TestDirectUiWithoutLegacyCopies(device,ctx);
    for(auto& row:g_targets) if(row.held) { row.held->Release();row={}; }
    ctx->Release();device->Release();
    std::printf("Depth frame selection / recycled views / 70 sorties / direct HUD without legacy copies: %d failures\n",failures);
    return failures?1:0;
}
