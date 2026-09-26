#include "ui_capture.h"
#include "image_profile.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace edf6vr {
namespace {
using BlendFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11BlendState*,const FLOAT*,UINT);
using DepthFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11DepthStencilState*,UINT);
BlendFn blendOriginal=nullptr;
DepthFn depthOriginal=nullptr;
void** dispatch=nullptr;
ID3D11DeviceContext* captureContext=nullptr; // owned by the game
std::atomic<bool> enabled{false};
thread_local bool internal=false;
thread_local bool paused=false;
DWORD renderThread=0;
bool hudPhase=false,routed=false,touched=false,ready=false;
ID3D11RenderTargetView* nativeTarget=nullptr;
ID3D11DepthStencilView* nativeDepth=nullptr;
ID3D11BlendState* nativeBlend=nullptr;
FLOAT nativeFactors[4]{};
UINT nativeMask=~0u;
ID3D11Texture2D *drawTexture=nullptr,*resolvedTexture=nullptr;
ID3D11RenderTargetView* drawTarget=nullptr;
// The world-anchored HUD (see ui_capture.h).
//
// The nameplate draws go through the game's render-state manager, which binds
// its own render target (not necessarily the back buffer) partway through the
// draw. Binding once on entry is therefore not enough: while a scope is active
// every target the game binds is remembered and replaced by the world target,
// and on exit the game's own binding is put back so its state cache stays true.
thread_local int worldScope=0;
std::atomic<bool> worldEnabled{true};
bool worldActive=false,worldTouched=false,worldReady=false;
ID3D11Texture2D *worldTexture=nullptr,*worldResolved=nullptr,*worldProbe=nullptr;
ID3D11RenderTargetView* worldTarget=nullptr;
D3D11_TEXTURE2D_DESC worldDesc{};
DXGI_FORMAT worldFormat=DXGI_FORMAT_UNKNOWN;
ID3D11RenderTargetView* gameTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
ID3D11DepthStencilView* gameDepth=nullptr;
ID3D11BlendState* gameBlend=nullptr;
FLOAT gameFactors[4]{};
UINT gameMask=~0u;
std::atomic<unsigned long long> worldScopes{0},worldScopesOffThread{0},worldBinds{0},worldFrames{0};
std::atomic<unsigned long long> worldBailPaused{0},worldBailNoContext{0},worldBailDisabled{0},worldNoTarget{0},
    worldTextureFailures{0},worldGameBinds{0},worldBlends{0},worldExitsKept{0},worldExitsLost{0};
std::atomic<bool> worldProbeOn{false};
char worldTargetNote[160]="none";
char worldProbeNote[160]="off";
ULONGLONG worldProbeAt=0;
D3D11_TEXTURE2D_DESC drawDesc{};
DXGI_FORMAT drawFormat=DXGI_FORMAT_UNKNOWN;
unsigned long long directFrames=0,depthKept=0,failures=0,rebinds=0;
char status[640]="off";
SRWLOCK statusLock=SRWLOCK_INIT;
struct BlendCopy { ID3D11BlendState* source=nullptr; ID3D11BlendState* result=nullptr; bool used=false; };
BlendCopy blendCopies[32]{};unsigned nextBlend=0;
// Which depth states kept a HUD-phase draw on the native target. A HUD element
// that goes missing in the headset while the rest of the HUD arrives is either
// drawn with one of these or before the gate; this tells the two apart.
struct KeptState { bool used; UINT depth,func,write,stencil; unsigned long long count; };
KeptState keptStates[6]{};
// The viewports HUD-phase draws are issued with. A HUD element that lands in
// the wrong place while the rest of the HUD is right is either laid out into a
// different viewport (this shows it) or positioned by other arithmetic (this
// rules it out). Routed draws and depth-kept draws are counted apart.
struct SeenViewport { bool used; bool routed; float x,y,w,h; unsigned long long count; };
SeenViewport viewports[8]{};
void NoteViewport(bool routedDraw) noexcept {
    UINT n=1; D3D11_VIEWPORT vp{}; captureContext->RSGetViewports(&n,&vp);
    if(!n) return;
    for(auto& v:viewports) {
        if(v.used && (v.routed!=routedDraw || v.x!=vp.TopLeftX || v.y!=vp.TopLeftY || v.w!=vp.Width || v.h!=vp.Height)) continue;
        v={true,routedDraw,vp.TopLeftX,vp.TopLeftY,vp.Width,vp.Height,v.count+1};
        return;
    }
}
template<class T> void Release(T*& p) noexcept { if(p) { p->Release();p=nullptr; } }
struct Internal { bool prior=internal; Internal() {internal=true;} ~Internal() {internal=prior;} };

bool RuntimePointer(void* pointer) noexcept {
    HMODULE owner=nullptr;
    return pointer && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        static_cast<LPCWSTR>(pointer),&owner) && owner==GetModuleHandleW(L"d3d11.dll");
}
void STDMETHODCALLTYPE BlendHook(ID3D11DeviceContext*,ID3D11BlendState*,const FLOAT*,UINT);
void STDMETHODCALLTYPE DepthHook(ID3D11DeviceContext*,ID3D11DepthStencilState*,UINT);
bool EnsureHooks() noexcept {
    if(!dispatch) return false;
    auto blend=dispatch[35],depth=dispatch[36];
    if(blend!=reinterpret_cast<void*>(&BlendHook)) {
        if(!RuntimePointer(blend)) return false;
        blendOriginal=reinterpret_cast<BlendFn>(blend);
        bool changed=false;ReplacePointer(&dispatch[35],blend,reinterpret_cast<void*>(&BlendHook),changed);
        if(!changed) return false;
        ++rebinds;
    }
    if(depth!=reinterpret_cast<void*>(&DepthHook)) {
        if(!RuntimePointer(depth)) return false;
        depthOriginal=reinterpret_cast<DepthFn>(depth);
        bool changed=false;ReplacePointer(&dispatch[36],depth,reinterpret_cast<void*>(&DepthHook),changed);
        if(!changed) return false;
        ++rebinds;
    }
    return true;
}
void SaveBlend(ID3D11BlendState* blend,const FLOAT* factors,UINT mask) noexcept {
    if(blend) blend->AddRef();Release(nativeBlend);nativeBlend=blend;
    for(int i=0;i<4;++i) nativeFactors[i]=factors?factors[i]:1.0f;
    nativeMask=mask;
}
ID3D11BlendState* AlphaBlendFor(ID3D11BlendState* source) noexcept {
    for(auto& entry:blendCopies) if(entry.used && entry.source==source) return entry.result;
    D3D11_BLEND_DESC desc{};
    if(source) source->GetDesc(&desc);
    else {
        for(auto& rt:desc.RenderTarget) {
            rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_ONE;
            rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_ZERO;
            rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        }
    }
    auto& rt=desc.RenderTarget[0];
    // UEVR's imgui_impl_dx11 is the reference: accumulate coverage with
    // Aout=As+Ad*(1-As). Preserve native RGB blending (straight or premultiplied).
    if(rt.BlendEnable) {
        rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;
    }
    if(rt.RenderTargetWriteMask) rt.RenderTargetWriteMask|=D3D11_COLOR_WRITE_ENABLE_ALPHA;
    ID3D11Device* device=nullptr;captureContext->GetDevice(&device);
    ID3D11BlendState* result=nullptr;
    const auto hr=device->CreateBlendState(&desc,&result);device->Release();
    if(FAILED(hr)) return nullptr;
    auto& entry=blendCopies[nextBlend++%32];Release(entry.source);Release(entry.result);
    entry.used=true;entry.source=source;if(source) source->AddRef();entry.result=result;
    return result;
}
ID3D11BlendState* AlphaBlend() noexcept { return AlphaBlendFor(nativeBlend); }
void RestoreTarget(bool restoreTarget) noexcept {
    if(routed && captureContext) {
        Internal guard;
        if(restoreTarget) captureContext->OMSetRenderTargets(1,&nativeTarget,nativeDepth);
        blendOriginal(captureContext,nativeBlend,nativeFactors,nativeMask);
    }
    routed=false;
}
void DropBinding(bool restoreTarget) noexcept {
    RestoreTarget(restoreTarget);
    Release(nativeTarget);Release(nativeDepth);Release(nativeBlend);
}
bool EnsureTextures() noexcept {
    ID3D11Resource* resource=nullptr;nativeTarget->GetResource(&resource);
    ID3D11Texture2D* source=nullptr;
    const auto hr=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&source));
    resource->Release();if(FAILED(hr)||!source) return false;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);source->Release();
    D3D11_RENDER_TARGET_VIEW_DESC view{};nativeTarget->GetDesc(&view);
    if(desc.ArraySize!=1 || (view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D
        && view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS)) return false;
    if(nativeDepth) {
        nativeDepth->GetResource(&resource);
        source=nullptr;resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&source));resource->Release();
        if(!source) return false;
        D3D11_TEXTURE2D_DESC depth{};source->GetDesc(&depth);source->Release();
        if(depth.Width!=desc.Width || depth.Height!=desc.Height || depth.SampleDesc.Count!=desc.SampleDesc.Count
            || depth.SampleDesc.Quality!=desc.SampleDesc.Quality) return false;
    }
    if(drawTexture && (desc.Width!=drawDesc.Width || desc.Height!=drawDesc.Height
        || desc.SampleDesc.Count!=drawDesc.SampleDesc.Count || desc.SampleDesc.Quality!=drawDesc.SampleDesc.Quality
        || view.Format!=drawFormat)) {
        // Do not replace a partially populated HUD during the same frame.
        if(touched) return false;
        Release(drawTarget);Release(drawTexture);Release(resolvedTexture);
    }
    if(!drawTexture) {
        ID3D11Device* device=nullptr;captureContext->GetDevice(&device);
        desc.MipLevels=1;desc.ArraySize=1;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.Format=view.Format;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags=desc.MiscFlags=0;
        HRESULT made=device->CreateTexture2D(&desc,nullptr,&drawTexture);
        if(SUCCEEDED(made)) made=device->CreateRenderTargetView(drawTexture,nullptr,&drawTarget);
        auto single=desc;single.SampleDesc={1,0};single.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(SUCCEEDED(made)) made=device->CreateTexture2D(&single,nullptr,&resolvedTexture);
        device->Release();
        if(FAILED(made)) {Release(drawTarget);Release(drawTexture);Release(resolvedTexture);return false;}
        drawDesc=desc;drawFormat=view.Format;
    }
    return true;
}
bool FlatHudDepth(ID3D11DepthStencilState* state) noexcept {
    if(!nativeDepth) return true;
    if(!state) return false; // D3D11 default: depth enabled, LESS, full writes
    D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);
    return !desc.DepthEnable || desc.DepthFunc==D3D11_COMPARISON_ALWAYS;
}
void Route(ID3D11DepthStencilState* state) noexcept {
    if(worldActive) return;
    if(!nativeTarget || !hudPhase || !enabled.load()) { RestoreTarget(true);return; }
    if(!FlatHudDepth(state)) {
        if(routed) ++depthKept;
        NoteViewport(false);
        D3D11_DEPTH_STENCIL_DESC desc{};
        if(state) state->GetDesc(&desc); else { desc.DepthEnable=TRUE;desc.DepthFunc=D3D11_COMPARISON_LESS;desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; }
        for(auto& k:keptStates) {
            if(k.used && (k.depth!=UINT(desc.DepthEnable) || k.func!=UINT(desc.DepthFunc)
                || k.write!=UINT(desc.DepthWriteMask) || k.stencil!=UINT(desc.StencilEnable))) continue;
            k={true,UINT(desc.DepthEnable),UINT(desc.DepthFunc),UINT(desc.DepthWriteMask),UINT(desc.StencilEnable),k.count+1};
            break;
        }
        RestoreTarget(true);return;
    }
    if(!EnsureTextures()) {++failures;RestoreTarget(true);return;}
    auto* blend=AlphaBlend();if(!blend) {++failures;RestoreTarget(true);return;}
    Internal guard;
    if(!touched) { const FLOAT clear[4]{};captureContext->ClearRenderTargetView(drawTarget,clear);touched=true; }
    if(!routed) captureContext->OMSetRenderTargets(1,&drawTarget,nativeDepth);
    blendOriginal(captureContext,blend,nativeFactors,nativeMask);
    if(!routed) NoteViewport(true);
    routed=true;
}
// ---- world scope ----
ID3D11RenderTargetView* entryTarget=nullptr; // what was bound when the outermost scope opened
// Two views of the same texture are the same target to the game.
bool SameTarget(ID3D11RenderTargetView* a,ID3D11RenderTargetView* b) noexcept {
    if(a==b) return true;
    if(!a || !b) return false;
    ID3D11Resource* ra=nullptr,*rb=nullptr;a->GetResource(&ra);b->GetResource(&rb);
    const bool same=ra && ra==rb;
    Release(ra);Release(rb);
    return same;
}
bool worldBound=false;
void SetNote(char* note,const char* text) noexcept {
    AcquireSRWLockExclusive(&statusLock);
    std::snprintf(note,160,"%s",text);
    ReleaseSRWLockExclusive(&statusLock);
}
DXGI_FORMAT WorldFormatFor(DXGI_FORMAT view) noexcept {
    switch(view) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R10G10B10A2_UNORM: return view;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM; // no alpha to accumulate coverage in
    }
}
// The world image takes the size and sample count of whatever the game is
// drawing the nameplates into, so its viewport and positions carry over as-is.
bool EnsureWorldTexture(ID3D11RenderTargetView* game) noexcept {
    if(!game) return false;
    ID3D11Resource* resource=nullptr;game->GetResource(&resource);
    ID3D11Texture2D* source=nullptr;
    const auto hr=resource?resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&source)):E_FAIL;
    if(resource) resource->Release();
    if(FAILED(hr)||!source) return false;
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);source->Release();
    D3D11_RENDER_TARGET_VIEW_DESC view{};game->GetDesc(&view);
    const auto format=WorldFormatFor(view.Format);
    const UINT gameSamples=desc.SampleDesc.Count;
    // Single-sampled whatever the game uses: plates are textured quads, and an
    // 8x copy of a 2912x2700 target is a quarter of a gigabyte for nothing.
    desc.SampleDesc={1,0};
    {
        char note[sizeof(worldTargetNote)]{};
        std::snprintf(note,sizeof(note),"%ux%u fmt=%u view=%u samples=%u entry=%d",
            desc.Width,desc.Height,unsigned(desc.Format),unsigned(view.Format),gameSamples,SameTarget(game,entryTarget));
        SetNote(worldTargetNote,note);
    }
    if(game==worldTarget) return true;
    if(worldTexture && (desc.Width!=worldDesc.Width || desc.Height!=worldDesc.Height
        || desc.SampleDesc.Count!=worldDesc.SampleDesc.Count || format!=worldFormat)) {
        if(worldTouched) return false; // keep this frame's plates; resize next frame
        Release(worldTarget);Release(worldTexture);Release(worldResolved);Release(worldProbe);
    }
    if(worldTexture) return true;
    ID3D11Device* device=nullptr;captureContext->GetDevice(&device);
    desc.MipLevels=1;desc.ArraySize=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.Format=format;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=desc.MiscFlags=0;
    HRESULT made=device->CreateTexture2D(&desc,nullptr,&worldTexture);
    if(SUCCEEDED(made)) made=device->CreateRenderTargetView(worldTexture,nullptr,&worldTarget);
    auto single=desc;single.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    if(SUCCEEDED(made)) made=device->CreateTexture2D(&single,nullptr,&worldResolved);
    device->Release();
    if(FAILED(made)) { Release(worldTarget);Release(worldTexture);Release(worldResolved);return false; }
    worldDesc=desc;worldFormat=format;
    return true;
}
void ForgetGameBinding() noexcept {
    for(auto*& t:gameTargets) Release(t);
    Release(gameDepth);Release(gameBlend);
}
// The game has just bound these (or they were bound when the scope opened):
// remember them for the exit and put the world image in their place. No depth
// buffer, so nothing in front of a nameplate hides it.
void RedirectToWorld() noexcept {
    ForgetGameBinding();
    captureContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,gameTargets,&gameDepth);
    captureContext->OMGetBlendState(&gameBlend,gameFactors,&gameMask);
    Internal guard;
    worldBound=false;
    if(!gameTargets[0]) { ++worldNoTarget;return; }
    if(!EnsureWorldTexture(gameTargets[0])) { ++worldTextureFailures;return; }
    auto* blend=AlphaBlendFor(gameBlend);
    if(!blend) { ++worldTextureFailures;return; }
    if(!worldTouched) { const FLOAT clear[4]{};captureContext->ClearRenderTargetView(worldTarget,clear);worldTouched=true; }
    captureContext->OMSetRenderTargets(1,&worldTarget,nullptr);
    blendOriginal(captureContext,blend,gameFactors,gameMask);
    worldBound=true;
    ++worldBinds;
}
void ProbeWorld() noexcept {
    if(!worldProbeOn.load(std::memory_order_relaxed) || !worldResolved) return;
    const auto now=GetTickCount64();
    if(now-worldProbeAt<5000) return;
    worldProbeAt=now;
    const auto format=worldDesc.Format;
    if(format!=DXGI_FORMAT_R8G8B8A8_UNORM && format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
       && format!=DXGI_FORMAT_B8G8R8A8_UNORM && format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        char note[sizeof(worldProbeNote)]{};
        std::snprintf(note,sizeof(note),"format %u not read",unsigned(format));SetNote(worldProbeNote,note);return;
    }
    if(!worldProbe) {
        ID3D11Device* device=nullptr;captureContext->GetDevice(&device);
        auto staging=worldDesc;staging.SampleDesc={1,0};staging.Usage=D3D11_USAGE_STAGING;
        staging.BindFlags=0;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        device->CreateTexture2D(&staging,nullptr,&worldProbe);device->Release();
        if(!worldProbe) return;
    }
    captureContext->CopyResource(worldProbe,worldResolved);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(captureContext->Map(worldProbe,0,D3D11_MAP_READ,0,&mapped))) return;
    unsigned long long covered=0;
    UINT left=worldDesc.Width,top=worldDesc.Height,right=0,bottom=0;
    const auto* row=static_cast<const unsigned char*>(mapped.pData);
    for(UINT y=0;y<worldDesc.Height;y+=2,row+=2*mapped.RowPitch)
        for(UINT x=0;x<worldDesc.Width;x+=2) {
            if(row[x*4+3]<8) continue;
            ++covered;
            if(x<left) left=x; if(x>right) right=x; if(y<top) top=y; if(y>bottom) bottom=y;
        }
    captureContext->Unmap(worldProbe,0);
    char note[sizeof(worldProbeNote)]{};
    if(covered) std::snprintf(note,sizeof(note),"covered=%llu box=%u,%u-%u,%u of %ux%u",
                              covered*4,left,top,right,bottom,worldDesc.Width,worldDesc.Height);
    else std::snprintf(note,sizeof(note),"empty %ux%u",worldDesc.Width,worldDesc.Height);
    SetNote(worldProbeNote,note);
}
void RestoreGameBinding() noexcept {
    {
        ID3D11RenderTargetView* bound=nullptr;ID3D11DepthStencilView* depth=nullptr;
        captureContext->OMGetRenderTargets(1,&bound,&depth);
        if(bound && bound==worldTarget) ++worldExitsKept; else ++worldExitsLost;
        Release(bound);Release(depth);
    }
    Internal guard;
    UINT count=0;
    for(UINT i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i) if(gameTargets[i]) count=i+1;
    captureContext->OMSetRenderTargets(count,gameTargets,gameDepth);
    blendOriginal(captureContext,gameBlend,gameFactors,gameMask);
    ForgetGameBinding();
}
void RouteCurrent() noexcept {
    if(!hudPhase || !enabled.load()) { RestoreTarget(true);return; }
    ID3D11DepthStencilState* state=nullptr;UINT ref=0;
    captureContext->OMGetDepthStencilState(&state,&ref);Route(state);Release(state);
}
void STDMETHODCALLTYPE BlendHook(ID3D11DeviceContext* ctx,ID3D11BlendState* blend,const FLOAT* factors,UINT mask) {
    if(worldActive && !internal && ctx==captureContext && GetCurrentThreadId()==renderThread) {
        // Inside a nameplate: keep the game's choice for the exit, draw with
        // coverage accumulated so the world composite knows where plates are.
        if(blend) blend->AddRef();Release(gameBlend);gameBlend=blend;
        for(int i=0;i<4;++i) gameFactors[i]=factors?factors[i]:1.0f;
        gameMask=mask;
        auto* alpha=worldBound?AlphaBlendFor(blend):nullptr;
        blendOriginal(ctx,alpha?alpha:blend,factors,mask);
        ++worldBlends;
        return;
    }
    if(internal || paused || ctx!=captureContext || GetCurrentThreadId()!=renderThread || !nativeTarget) {
        blendOriginal(ctx,blend,factors,mask);return;
    }
    SaveBlend(blend,factors,mask);
    blendOriginal(ctx,blend,factors,mask);
    RouteCurrent();
}
void STDMETHODCALLTYPE DepthHook(ID3D11DeviceContext* ctx,ID3D11DepthStencilState* depth,UINT ref) {
    depthOriginal(ctx,depth,ref);
    if(worldActive) return;
    if(!internal && !paused && ctx==captureContext && GetCurrentThreadId()==renderThread && nativeTarget) Route(depth);
}
}
bool InsideUiCapture() noexcept { return internal; }
PauseUiCapture::PauseUiCapture() noexcept:previous(paused) { paused=true; }
PauseUiCapture::~PauseUiCapture() noexcept { paused=previous; }
bool InstallUiCapture(ID3D11DeviceContext* ctx) noexcept {
    if(!ctx) return false;
    if(captureContext && ctx!=captureContext) return false;
    captureContext=ctx;dispatch=*reinterpret_cast<void***>(ctx);
    return EnsureHooks();
}
void EnableUiCapture(bool on) noexcept { enabled.store(on); }
void EnableWorldUi(bool on) noexcept { worldEnabled.store(on); }
void UiCaptureWorldScope(bool enter) noexcept {
    if(enter) {
        if(worldScope++>0) return;
        ++worldScopes;
        if(!captureContext) { ++worldBailNoContext;return; }
        if(GetCurrentThreadId()!=renderThread) { ++worldScopesOffThread;return; }
        if(internal || paused) { ++worldBailPaused;return; }
        if(!enabled.load() || !worldEnabled.load()) { ++worldBailDisabled;return; }
        // The panel capture lets go of its binding; the game's own is what
        // gets remembered and put back.
        RestoreTarget(true);
        Release(entryTarget);
        captureContext->OMGetRenderTargets(1,&entryTarget,nullptr);
        worldActive=true;
        RedirectToWorld();
        return;
    }
    if(worldScope<=0) return;
    if(--worldScope>0 || !worldActive) return;
    worldActive=false;
    const bool same=SameTarget(gameTargets[0],entryTarget);
    RestoreGameBinding();
    routed=false; // the panel target is rebound below, whatever happened inside
    Release(entryTarget);
    // Back where the panel capture left off only if the game is too; if the
    // nameplate moved the game to another target, the capture waits for the
    // game's next back-buffer bind rather than routing that target's draws.
    if(same && hudPhase && nativeTarget) RouteCurrent();
    else if(!same) DropBinding(false);
}
ID3D11DeviceContext* UiCaptureContext() noexcept { return captureContext; }
void EnableWorldUiProbe(bool on) noexcept { worldProbeOn.store(on); }
void ReadWorldUiNotes(char* out,size_t size) noexcept {
    if(!out || !size) return;
    AcquireSRWLockShared(&statusLock);
    const int used=std::snprintf(out,size,"bail paused/context/disabled=%llu/%llu/%llu noTarget=%llu textureFail=%llu gameBinds=%llu blends=%llu exitKept/lost=%llu/%llu target=[%s] probe=[%s]",
        worldBailPaused.load(),worldBailNoContext.load(),worldBailDisabled.load(),worldNoTarget.load(),worldTextureFailures.load(),
        worldGameBinds.load(),worldBlends.load(),worldExitsKept.load(),worldExitsLost.load(),worldTargetNote,worldProbeNote);
    ReleaseSRWLockShared(&statusLock);
    if(used<0) out[0]=0;
}
ID3D11Texture2D* CapturedWorldUi() noexcept {
    return enabled.load() && worldEnabled.load() && worldReady?worldResolved:nullptr;
}
WorldUiStats ReadWorldUiStats() noexcept {
    return {worldScopes.load(),worldScopesOffThread.load(),worldBinds.load(),worldFrames.load(),worldExitsLost.load()};
}
void UiCaptureTargets(ID3D11DeviceContext* ctx,UINT count,ID3D11RenderTargetView* const* targets,
    ID3D11DepthStencilView* depth,bool backBuffer) noexcept {
    if(internal || paused || ctx!=captureContext) return;
    if(worldActive && GetCurrentThreadId()==renderThread) {
        // A nameplate's own state manager binding its target: take it over too.
        ++worldGameBinds;
        RedirectToWorld();
        return;
    }
    DropBinding(false); // the game has already bound its NEXT render target
    renderThread=GetCurrentThreadId();
    if(!enabled.load() || !count || count>D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT
        || !targets || !targets[0] || !backBuffer || !EnsureHooks()) return;
    // Some callers submit eight slots with seven nulls. That is still one RTV.
    for(UINT i=1;i<count;++i) if(targets[i]) return;
    nativeTarget=targets[0];nativeTarget->AddRef();nativeDepth=depth;if(depth) depth->AddRef();
    ctx->OMGetBlendState(&nativeBlend,nativeFactors,&nativeMask);
    RouteCurrent();
}
void UiCaptureGate() noexcept {
    if(paused || !captureContext || !enabled.load() || GetCurrentThreadId()!=renderThread || !nativeTarget || !EnsureHooks()) return;
    hudPhase=true;if(!worldActive) RouteCurrent();
}
void FinishUiCapture() noexcept {
    if(!captureContext) return;
    DropBinding(true);ready=false;worldReady=false;
    if(touched && enabled.load() && resolvedTexture) {
        Internal guard;
        if(drawDesc.SampleDesc.Count>1) captureContext->ResolveSubresource(resolvedTexture,0,drawTexture,0,drawFormat);
        else captureContext->CopyResource(resolvedTexture,drawTexture);
        ready=true;++directFrames;
    }
    if(worldActive) { worldActive=false;RestoreGameBinding();Release(entryTarget); } // a scope left open by a fault
    if(worldTouched && enabled.load() && worldResolved) {
        Internal guard;
        if(worldDesc.SampleDesc.Count>1) captureContext->ResolveSubresource(worldResolved,0,worldTexture,0,worldFormat);
        else captureContext->CopyResource(worldResolved,worldTexture);
        worldReady=true;++worldFrames;
        ProbeWorld();
    }
    AcquireSRWLockExclusive(&statusLock);
    int used=std::snprintf(status,sizeof(status),"%s frames=%llu samples=%u kept=%llu failures=%llu rebinds=%llu worldFrames=%llu worldBinds=%llu keptStates=",
        !enabled.load()?"off":(ready?"direct":"waiting-gate"),directFrames,drawDesc.SampleDesc.Count,depthKept,failures,rebinds,
        worldFrames.load(),worldBinds.load());
    for(const auto& k:keptStates) {
        if(!k.used || used<0 || used>=int(sizeof(status))) break;
        used+=std::snprintf(status+used,sizeof(status)-used,"[d%u f%u w%u s%u x%llu]",k.depth,k.func,k.write,k.stencil,k.count);
    }
    if(used>0 && used<int(sizeof(status))) used+=std::snprintf(status+used,sizeof(status)-used," viewports=");
    for(const auto& v:viewports) {
        if(!v.used || used<0 || used>=int(sizeof(status))) break;
        used+=std::snprintf(status+used,sizeof(status)-used,"[%s %.0f,%.0f %.0fx%.0f x%llu]",v.routed?"hud":"kept",v.x,v.y,v.w,v.h,v.count);
    }
    ReleaseSRWLockExclusive(&statusLock);
    hudPhase=touched=worldTouched=false;
}
ID3D11Texture2D* CapturedUi() noexcept { return enabled.load() && ready?resolvedTexture:nullptr; }
const char* UiCaptureStatus() noexcept {
    thread_local char snapshot[sizeof(status)]{};
    AcquireSRWLockShared(&statusLock);std::memcpy(snapshot,status,sizeof status);ReleaseSRWLockShared(&statusLock);
    return snapshot;
}
}
