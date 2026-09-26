#include "scene_aa.h"
#include "depth_probe.h"
#include "aim_hud.h"
#include "image_profile.h"
#include "weapon_layer.h"
#include "ui_capture.h"
#include "desktop_mirror.h"

#include <d3d11.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace edf6vr {
namespace {
// A D3D11 context keeps its dispatch table inside the object, not in the module,
// and the runtime rewrites the entries as its own state changes. The survey
// proved it: DrawIndexed took the patch and was found holding the original
// again a few seconds later, DrawIndexedInstanced would not take at all, and
// only OMSetRenderTargets ever stayed and fired. So draws are not counted here
// any more. What does hold is the render target hook, and EDF6's own model draw
// is already hooked elsewhere in the plugin, which gives draw attribution
// without asking D3D11 for anything.
//
// The earlier build also patched a deferred context of its own making and then
// released it, leaving the report reading freed memory that had since become a
// string. That is gone with the rest.
constexpr int kOMSetRenderTargetsSlot=33;
constexpr int kHighestSlot=kOMSetRenderTargetsSlot;

using OMSetRenderTargetsFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,
                                                     ID3D11RenderTargetView* const*,ID3D11DepthStencilView*);

void** g_table=nullptr;
OMSetRenderTargetsFn g_original=nullptr;
std::atomic<bool> g_installed{false};
std::atomic<unsigned long long> g_binds{0}, g_rebinds{0}, g_modelDraws{0};

// One row per depth texture. Keying on the view instead let the shadow cascades,
// which are many views onto one texture, fill the table.
//
// Textures have an owned reference until inactive; views are never cached.
// DSV address recycling cannot redirect attribution to a prior mission.
struct Target {
    void* texture=nullptr;      // ID3D11Texture2D*, key only
    unsigned width=0, height=0;
    unsigned format=0;          // DXGI_FORMAT of the texture
    unsigned viewFormat=0;      // DXGI_FORMAT the view reads it as
    unsigned samples=0;
    unsigned bindFlags=0;
    unsigned views=0;
    unsigned long long binds=0;
    unsigned long long modelDraws=0;      // EDF6's own model draws while bound
    unsigned long long frameDraws=0;     // ONLY the frame currently being rendered
    unsigned long long lastBoundFrame=0;
    ID3D11Texture2D* held=nullptr;     // kept alive so it can be read after the pass
    float centre=-1;                   // last depth read from the middle of the image
    float nearest=-1, farthest=-1;
    float corner=-1;                   // one pixel away from the middle, for scale
    float weapon[2]{-1,-1};            // where a first person weapon sits
    unsigned long long reads=0;
};
constexpr int kMaxTargets=32;
Target g_targets[kMaxTargets]{};
int g_targetCount=0;
unsigned long long g_targetOverflow=0;
unsigned long long g_depthFrame=1, g_completedDepthFrame=0;
// No DSV-address cache: the game releases and recreates views between missions.
// GetResource identifies the actual texture even if the DSV address is reused.
std::atomic<int> g_current{-1};
SRWLOCK g_lock=SRWLOCK_INIT;
std::atomic<unsigned long> g_bindThread{0};
char g_status[224]="not installed";

// Legacy log row only; no synchronous GPU readback in the render path.
int g_sampleRow=0;
int g_previous=-1;

// The UI goes on after the scene, so the colour bound alongside the scene depth
// holds the world without it. Kept so the warp can leave text where it is
// instead of dragging it along with whatever depth happens to be behind it.
std::atomic<int> g_sceneRow{-1};
unsigned long long g_sceneSwitches=0, g_sceneFrameDraws=0;
unsigned long long g_hudlessFrame=0;
int g_hudlessRow=-1;
ID3D11Texture2D* g_sceneColour=nullptr;   // what was bound, held
ID3D11Texture2D* g_hudless=nullptr;       // our copy, single sample, sampleable
unsigned long long g_hudlessCopies=0;
char g_hudlessNote[96]="not taken";

// The swapchain images, however many the runtime rotates through, and a copy of
// whichever one was last bound to be drawn into. Everything the game draws to
// the screen has already happened by then except the pass that is starting, and
// the last such pass is the UI.
// Recognised by address and not held.
//
// These used to be kept with a reference each, and a reference on a swapchain
// image is exactly what ResizeBuffers refuses to work around: the game asks for
// a new resolution, DXGI declines because someone else still has the old
// buffers, and the picture stops for the rest of the session. Turning
// anti-aliasing off did the same thing, which is the tell -- it changes the
// buffers without changing their size, so it is the buffers and not the size.
// Nothing here needs to keep them alive; the list only answers "is this the one
// being presented?", and a released texture's address is enough for that. The
// list is emptied whenever an unfamiliar image turns up with no room left, so a
// recycled address cannot go on answering for a buffer that is gone.
constexpr int kMaxBackBuffers=4;
ID3D11Texture2D* g_backBuffers[kMaxBackBuffers]{};
int g_backBufferCount=0;
SRWLOCK g_backLock=SRWLOCK_INIT;
// The presented image is bound several times a frame, once for each pass that
// draws into it, and the HUD is spread over more than one of them: taking only
// the last gave the subtitle bar and nothing else. So every bind is kept and the
// right one is chosen by looking at them.
constexpr int kMaxPreUi=8;
std::atomic<bool> g_legacyPreUiRequested{true};
std::atomic<unsigned> g_legacyPreUiGeneration{0};
bool g_legacyPreUiActive=true;        // render thread; changes at frame boundaries
unsigned g_legacyPreUiFrameGeneration=0;
ID3D11Texture2D* g_preUiRing[kMaxPreUi]{};
int g_preUiTaken=0;               // snapshots taken during the frame being drawn
int g_preUiHeld=0;                // snapshots from the frame before it, readable
int g_preUiPick=-1;               // which of those is the picture before the HUD
char g_preUiNote[128]="no snapshots yet";
ULONGLONG g_preUiChosen=0;
unsigned g_preUiBinds=0;          // actual backbuffer binds, independent of copies
unsigned g_preUiBindsLast=0;      // during the one before it
// Where the game itself said the HUD begins, rather than where comparing the
// pictures suggests it does.
// Where the HUD goes when it is taken out of the presented image.
int g_gateAt=-1, g_gateAtLast=-1;
int g_gateCalls=0, g_gateCallsLast=0;
int g_gateYes=0, g_gateYesLast=0;
DWORD g_gateThread=0;
char g_gateNote[96]="gate not hooked";

bool LegacyPreUiEnabled() noexcept {
    return g_legacyPreUiActive && g_legacyPreUiRequested.load(std::memory_order_acquire)
        && g_legacyPreUiFrameGeneration==g_legacyPreUiGeneration.load(std::memory_order_acquire);
}

bool IsBackBuffer(ID3D11Texture2D* image) noexcept {
    bool found=false;
    AcquireSRWLockShared(&g_backLock);
    for(int i=0;i<g_backBufferCount;++i) if(g_backBuffers[i]==image) { found=true; break; }
    ReleaseSRWLockShared(&g_backLock);
    return found;
}

// Copied before the pass that is being bound draws anything, so what lands here
// is the picture as the pass found it. Overwritten at every such bind, so after
// the last one it holds the frame without the UI.
void TakePreUi(ID3D11DeviceContext* context,ID3D11Texture2D* image) noexcept {
    if(!LegacyPreUiEnabled() || g_preUiTaken>=kMaxPreUi) return;
    __try {
        D3D11_TEXTURE2D_DESC source{};
        image->GetDesc(&source);
        ID3D11Device* device=nullptr;
        context->GetDevice(&device);
        if(!device) return;
        ID3D11Texture2D*& slot=g_preUiRing[g_preUiTaken];
        if(slot) {
            D3D11_TEXTURE2D_DESC existing{};
            slot->GetDesc(&existing);
            if(existing.Width!=source.Width || existing.Height!=source.Height
               || existing.Format!=source.Format) { slot->Release(); slot=nullptr; }
        }
        if(!slot) {
            D3D11_TEXTURE2D_DESC desc=source;
            desc.SampleDesc.Count=1;
            desc.SampleDesc.Quality=0;
            desc.MipLevels=1;
            desc.Usage=D3D11_USAGE_DEFAULT;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags=0;
            desc.MiscFlags=0;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&slot))) slot=nullptr;
        }
        if(slot) {
            if(source.SampleDesc.Count>1) context->ResolveSubresource(slot,0,image,0,source.Format);
            else context->CopyResource(slot,image);
            ++g_preUiTaken;
        }
        device->Release();
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Which snapshot is the picture as it stood after everything except the HUD.
//
// The passes before the HUD rewrite the whole picture, so their snapshots share
// almost nothing with the finished frame. From the first HUD pass onwards each
// snapshot is the finished frame less whatever HUD is still to come, which is a
// few per cent of it. The separation is not close, so the earliest snapshot that
// mostly agrees with the finished frame is the one wanted.
//
// Measured, not assumed, and measured again every second: how many passes there
// are and where the HUD starts among them is not going to be the same in a menu,
// in a vehicle, or for another class.
void Note(const char* format,...) noexcept {
    va_list args; va_start(args,format);
    std::vsnprintf(g_preUiNote,sizeof(g_preUiNote),format,args);
    va_end(args);
}

constexpr int kPickRows=16;
ID3D11Texture2D* g_pickStrip=nullptr;
ID3D11Texture2D* g_pickFinal=nullptr;
ID3D11Texture2D* g_pickFlat=nullptr;   // the presented image resolved, when it is multisampled

void ReleaseLegacyPreUi() noexcept {
    for(auto& image:g_preUiRing) if(image) { image->Release(); image=nullptr; }
    ID3D11Texture2D** images[]={&g_pickStrip,&g_pickFinal,&g_pickFlat,&g_hudless};
    for(auto** image:images)
        if(*image) { (*image)->Release(); *image=nullptr; }
    g_preUiHeld=0;g_preUiPick=-1;g_preUiChosen=0;
    g_hudlessFrame=0;g_hudlessRow=-1;
}

ID3D11Texture2D* MakePickStrip(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& source) noexcept {
    D3D11_TEXTURE2D_DESC desc=source;
    desc.Height=kPickRows;
    desc.MipLevels=1;
    desc.ArraySize=1;
    desc.SampleDesc.Count=1;
    desc.SampleDesc.Quality=0;
    desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=0;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    desc.MiscFlags=0;
    ID3D11Texture2D* strip=nullptr;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&strip))) return nullptr;
    return strip;
}

bool ReadPickRows(ID3D11DeviceContext* context,ID3D11Texture2D* source,ID3D11Texture2D* strip,
                  unsigned width,unsigned height) noexcept {
    for(int i=0;i<kPickRows;++i) {
        D3D11_BOX box{};
        box.left=0; box.right=width;
        box.top=(height*(2*i+1))/(2*kPickRows);
        box.bottom=box.top+1;
        box.front=0; box.back=1;
        if(box.bottom>height) return false;
        context->CopySubresourceRegion(strip,0,0,static_cast<UINT>(i),0,source,0,&box);
    }
    return true;
}

int RedirectFrom() noexcept;

void ChoosePreUi(ID3D11DeviceContext* context,ID3D11Texture2D* presented) noexcept {
    if(!LegacyPreUiEnabled()) return;
    if(!context || !presented) { Note("no context or nothing presented"); return; }
    if(g_preUiHeld<=0) { Note("no snapshots held"); return; }
    const auto now=GetTickCount64();
    if(g_preUiPick>=0 && now-g_preUiChosen<1000) return;
    __try {
        D3D11_TEXTURE2D_DESC desc{};
        presented->GetDesc(&desc);
        ID3D11Device* device=nullptr;
        context->GetDevice(&device);
        if(!device) { Note("no device"); return; }
        // EDF6 presents a multisampled image, which cannot be copied row by row
        // into a staging texture: it has to be flattened first, the same way the
        // snapshots themselves already are, or the two would not compare.
        ID3D11Texture2D* flat=presented;
        if(desc.SampleDesc.Count>1) {
            desc.SampleDesc.Count=1;
            desc.SampleDesc.Quality=0;
            if(!g_pickFlat) {
                D3D11_TEXTURE2D_DESC copy=desc;
                copy.MipLevels=1;
                copy.ArraySize=1;
                copy.Usage=D3D11_USAGE_DEFAULT;
                copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                copy.CPUAccessFlags=0;
                copy.MiscFlags=0;
                if(FAILED(device->CreateTexture2D(&copy,nullptr,&g_pickFlat))) g_pickFlat=nullptr;
            }
            if(!g_pickFlat) { device->Release(); Note("resolve target failed"); return; }
            context->ResolveSubresource(g_pickFlat,0,presented,0,desc.Format);
            flat=g_pickFlat;
        }
        if(!g_pickStrip) g_pickStrip=MakePickStrip(device,desc);
        if(!g_pickFinal) g_pickFinal=MakePickStrip(device,desc);
        device->Release();
        if(!g_pickStrip || !g_pickFinal) { Note("staging rows failed fmt=%u",desc.Format); return; }
        if(!ReadPickRows(context,flat,g_pickFinal,desc.Width,desc.Height)) {
            Note("presented rows unreadable"); return;
        }
        D3D11_MAPPED_SUBRESOURCE finalMap{};
        if(FAILED(context->Map(g_pickFinal,0,D3D11_MAP_READ,0,&finalMap)) || !finalMap.pData) {
            Note("presented rows would not map"); return;
        }
        g_preUiChosen=now;
        int best=-1;
        int shares[kMaxPreUi]{};
        for(int i=0;i<g_preUiHeld;++i) {
            shares[i]=-1;
            if(!g_preUiRing[i]) continue;
            if(!ReadPickRows(context,g_preUiRing[i],g_pickStrip,desc.Width,desc.Height)) continue;
            D3D11_MAPPED_SUBRESOURCE map{};
            if(FAILED(context->Map(g_pickStrip,0,D3D11_MAP_READ,0,&map)) || !map.pData) continue;
            unsigned long long same=0, total=0;
            for(int row=0;row<kPickRows;++row) {
                auto a=static_cast<const unsigned char*>(finalMap.pData)
                       +static_cast<size_t>(row)*finalMap.RowPitch;
                auto b=static_cast<const unsigned char*>(map.pData)
                       +static_cast<size_t>(row)*map.RowPitch;
                for(unsigned x=0;x<desc.Width;++x) {
                    const size_t at=static_cast<size_t>(x)*4;
                    if(a[at]==b[at] && a[at+1]==b[at+1] && a[at+2]==b[at+2]) ++same;
                    ++total;
                }
            }
            context->Unmap(g_pickStrip,0);
            shares[i]=total?static_cast<int>(same*100/total):-1;
            if(best<0 && shares[i]>=80) best=i;
        }
        context->Unmap(g_pickFinal,0);
        // Snapshot zero is the presented image from several frames ago. If even
        // that agrees with the finished frame then nothing moved and the test
        // has nothing to go on, so it keeps the last answer instead of taking
        // the first index that happens to match.
        if(shares[0]>=95) {
            std::snprintf(g_preUiNote,sizeof(g_preUiNote),"kept=%d, frame too still to judge",
                          g_preUiPick);
            return;
        }
        g_preUiPick=best;
        int used=std::snprintf(g_preUiNote,sizeof(g_preUiNote),"pick=%d from=%d of %d shares",
                               best,RedirectFrom(),g_preUiHeld);
        for(int i=0;i<g_preUiHeld && used>0 && used<static_cast<int>(sizeof(g_preUiNote));++i)
            used+=std::snprintf(g_preUiNote+used,sizeof(g_preUiNote)-used," %d",shares[i]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void KeepSceneColour(ID3D11RenderTargetView* view) noexcept {
    if(!LegacyPreUiEnabled() || !view) return;
    __try {
        ID3D11Resource* resource=nullptr;
        view->GetResource(&resource);
        if(!resource) return;
        ID3D11Texture2D* image=nullptr;
        if(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&image)))
           && image) {
            if(g_sceneColour!=image) {
                if(g_sceneColour) g_sceneColour->Release();
                image->AddRef();
                g_sceneColour=image;
            }
            image->Release();
        }
        resource->Release();
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void TakeHudless(ID3D11DeviceContext* context) noexcept {
    if(!LegacyPreUiEnabled() || !context || !g_sceneColour) return;
    __try {
        D3D11_TEXTURE2D_DESC source{};
        g_sceneColour->GetDesc(&source);
        ID3D11Device* device=nullptr;
        context->GetDevice(&device);
        if(!device) return;
        if(g_hudless) {
            D3D11_TEXTURE2D_DESC existing{};
            g_hudless->GetDesc(&existing);
            if(existing.Width!=source.Width || existing.Height!=source.Height
               || existing.Format!=source.Format) { g_hudless->Release(); g_hudless=nullptr; }
        }
        if(!g_hudless) {
            D3D11_TEXTURE2D_DESC desc=source;
            desc.SampleDesc.Count=1;
            desc.SampleDesc.Quality=0;
            desc.MipLevels=1;
            desc.ArraySize=1;
            desc.Usage=D3D11_USAGE_DEFAULT;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags=0;
            desc.MiscFlags=0;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&g_hudless)) || !g_hudless) {
                std::snprintf(g_hudlessNote,sizeof(g_hudlessNote),"texture %ux%u fmt=%u refused",
                              source.Width,source.Height,source.Format);
                device->Release();
                return;
            }
        }
        if(source.SampleDesc.Count>1) context->ResolveSubresource(g_hudless,0,g_sceneColour,0,source.Format);
        else context->CopyResource(g_hudless,g_sceneColour);
        ++g_hudlessCopies;
        g_hudlessFrame=g_depthFrame;
        g_hudlessRow=g_sceneRow.load(std::memory_order_relaxed);
        std::snprintf(g_hudlessNote,sizeof(g_hudlessNote),"%ux%u fmt=%u samples=%u",
                      source.Width,source.Height,source.Format,source.SampleDesc.Count);
        device->Release();
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Called with the lock held. Returns the row for the view's texture.
int Resolve(ID3D11DepthStencilView* view) noexcept {
    ID3D11Resource* resource=nullptr;
    view->GetResource(&resource);
    if(!resource) return -1;
    // All registered depth textures are held, so neither their keys nor the
    // dimensions associated with those keys can silently change identity.
    for(int i=0;i<g_targetCount;++i) if(g_targets[i].texture==resource) {
        resource->Release();
        g_targets[i].lastBoundFrame=g_depthFrame;
        return i;
    }
    ID3D11Texture2D* image=nullptr;
    const HRESULT hr=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&image));
    resource->Release();
    if(FAILED(hr) || !image) return -1;
    int index=-1;
    for(int i=0;i<g_targetCount;++i) if(!g_targets[i].held) { index=i; break; }
    if(index<0 && g_targetCount<kMaxTargets) index=g_targetCount++;
    if(index<0) { ++g_targetOverflow; image->Release(); return -1; }
    D3D11_TEXTURE2D_DESC desc{}; image->GetDesc(&desc);
    D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{}; view->GetDesc(&viewDesc);
    Target row{};
    row.texture=image; row.held=image; row.width=desc.Width; row.height=desc.Height;
    row.format=desc.Format; row.viewFormat=viewDesc.Format;
    row.samples=desc.SampleDesc.Count; row.bindFlags=desc.BindFlags;
    row.views=1; row.lastBoundFrame=g_depthFrame;
    g_targets[index]=row;
    return index;
}

// Called at Present before OpenXR consumes this frame. ReShade generic_depth's
// reset_on_present / last_used_in_frame is the reference: lifetime draw totals
// must not select a depth texture from a previous sortie.
void FinishDepthFrame(unsigned width,unsigned height) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    int best=-1;
    unsigned long long most=0;
    for(int i=0;i<g_targetCount;++i) {
        const auto& row=g_targets[i];
        if(row.held && row.samples==1 && row.format==DXGI_FORMAT_R32_TYPELESS
            && row.width==width && row.height==height && row.frameDraws>most) {
            most=row.frameDraws; best=i;
        }
    }
    if(best!=g_sceneRow.load()) ++g_sceneSwitches;
    g_sceneRow.store(best); g_sceneFrameDraws=most;
    g_completedDepthFrame=g_depthFrame;
    // Reclaim unused resources on the render thread, after their frame has
    // finished. Diagnostic reports only copy metadata while holding this lock.
    for(int i=0;i<g_targetCount;++i) {
        auto& row=g_targets[i]; row.frameDraws=0;
        if(row.held && i!=best && row.lastBoundFrame+2<g_depthFrame) {
            row.held->Release(); row={};
            if(g_current.load()==i) g_current.store(-1);
            if(g_previous==i) g_previous=-1;
        }
    }
    ++g_depthFrame;
    ReleaseSRWLockExclusive(&g_lock);
}

// Legacy snapshot position is diagnostic only; routing uses the current HUD gate.
int RedirectFrom() noexcept {
    if(g_gateAtLast<3) return -1;     // never early enough to take the world with it
    return g_gateAtLast-1;
}

void STDMETHODCALLTYPE HookOMSetRenderTargets(ID3D11DeviceContext* context,UINT count,
                                              ID3D11RenderTargetView* const* targets,
                                              ID3D11DepthStencilView* depth) {
    if(InWeaponLayerCapture() || InsideUiCapture() || InsideAimHudDraw() || (InsideDesktopMirror() || InsideSceneAA())) { g_original(context,count,targets,depth); return; }
    g_binds.fetch_add(1,std::memory_order_relaxed);
    int index=-1;
    if(!depth) g_current.store(-1,std::memory_order_relaxed);
    else {
        g_bindThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
        AcquireSRWLockExclusive(&g_lock);
        index=Resolve(depth);
        if(index>=0) ++g_targets[index].binds;
        ReleaseSRWLockExclusive(&g_lock);
        g_current.store(index,std::memory_order_relaxed);
    }
    if(index>=0 && index==g_sceneRow && count && targets) KeepSceneColour(targets[0]);
    bool backBuffer=false;
    if(count && targets && targets[0]) {
        ID3D11Resource* resource=nullptr;targets[0]->GetResource(&resource);
        ID3D11Texture2D* image=nullptr;
        if(resource) {
            resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&image));
            resource->Release();
        }
        if(image) {
            backBuffer=IsBackBuffer(image);
            if(backBuffer) {
                ++g_preUiBinds;
                TakePreUi(context,image);
            }
            image->Release();
        }
    }
    // The candidate has just been left for something else, so what it holds is
    // the finished result of the pass that was drawing into it.
    const int leaving=g_previous;
    g_previous=index;
    if(leaving>=0 && leaving!=index) {
        // The scene pass has just ended, so the colour it was drawing into is
        // the world with no UI on it yet.
        if(leaving==g_sceneRow && LegacyPreUiEnabled()) TakeHudless(context);
    }
    g_original(context,count,targets,depth);
    UiCaptureTargets(context,count,targets,depth,backBuffer);
}

void SetStatusf(const char* format,...) noexcept {
    va_list args; va_start(args,format);
    std::vsnprintf(g_status,sizeof(g_status),format,args);
    va_end(args);
}

bool InD3d11(void* address) noexcept {
    static const auto d3d11=GetModuleHandleW(L"d3d11.dll");
    if(!d3d11 || !address) return false;
    HMODULE owner=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(address),&owner)!=FALSE && owner==d3d11;
}
}

bool InstallDepthProbe(CaptureLogger log) noexcept {
    if(g_installed.load()) return true;
    void* device=nullptr; void* rawContext=nullptr;
    if(!GameDevice(device,rawContext) || !rawContext) {
        SetStatusf("no device yet");
        if(log) log(g_status);
        return false;
    }
    auto context=static_cast<ID3D11DeviceContext*>(rawContext);
    auto table=*reinterpret_cast<void***>(context);
    if(!Readable(table,(kHighestSlot+1)*sizeof(void*))) {
        SetStatusf("context dispatch table is not readable");
        if(log) log(g_status);
        return false;
    }
    auto original=table[kOMSetRenderTargetsSlot];
    if(!InD3d11(original)) {
        SetStatusf("OMSetRenderTargets does not point into d3d11.dll; nothing patched");
        if(log) log(g_status);
        return false;
    }
    g_table=table;
    g_original=reinterpret_cast<OMSetRenderTargetsFn>(original);
    bool changed=false;
    ReplacePointer(&table[kOMSetRenderTargetsSlot],original,
                   reinterpret_cast<void*>(&HookOMSetRenderTargets),changed);
    if(!changed) {
        SetStatusf("OMSetRenderTargets slot would not take the hook");
        if(log) log(g_status);
        return false;
    }
    SetStatusf("depth probe on OMSetRenderTargets only; draws come from the game's own hook");
    InstallUiCapture(context);
    g_installed.store(true);
    if(log) log(g_status);
    return true;
}

bool DepthProbeInstalled() noexcept { return g_installed.load(); }

void NoteModelDraw() noexcept {
    if(!g_installed.load(std::memory_order_relaxed)) return;
    g_modelDraws.fetch_add(1,std::memory_order_relaxed);
    const int index=g_current.load(std::memory_order_relaxed);
    if(index<0) return;
    AcquireSRWLockExclusive(&g_lock);
    if(index<g_targetCount) { ++g_targets[index].modelDraws; ++g_targets[index].frameDraws; }
    ReleaseSRWLockExclusive(&g_lock);
}

ID3D11Texture2D* SceneColour() noexcept {
    return LegacyPreUiEnabled() && g_hudlessFrame==g_completedDepthFrame && g_hudlessRow==g_sceneRow.load()
        && g_hudlessRow>=0?g_hudless:nullptr;
}

void SetLegacyPreUiCapture(bool enabled) noexcept {
    if(g_legacyPreUiRequested.exchange(enabled,std::memory_order_acq_rel)!=enabled)
        g_legacyPreUiGeneration.fetch_add(1,std::memory_order_release);
}

ID3D11Texture2D* UiTexture() noexcept { return CapturedUi(); }
const char* UiRedirectNote() noexcept { return UiCaptureStatus(); }
void SetUiRedirect(bool on) noexcept { EnableUiCapture(on); }

ID3D11Texture2D* PreUiColour() noexcept {
    return LegacyPreUiEnabled() && (g_preUiPick>=0 && g_preUiPick<g_preUiHeld)?g_preUiRing[g_preUiPick]:nullptr;
}

unsigned PreUiBinds() noexcept { return g_preUiBindsLast; }

int PreUiHeld() noexcept { return LegacyPreUiEnabled()?g_preUiHeld:0; }

ID3D11Texture2D* PreUiSnapshot(int index) noexcept {
    return LegacyPreUiEnabled() && (index>=0 && index<g_preUiHeld)?g_preUiRing[index]:nullptr;
}

void NoteUiGate(bool drawing) noexcept {
    ++g_gateCalls;
    g_gateThread=GetCurrentThreadId();
    if(!drawing) return;
    ++g_gateYes;
    // The first yes of the frame is the one that matters: everything the HUD
    // draws comes after it, so the snapshot standing at that moment is the
    // picture without any of it.
    if(g_gateAt<0) g_gateAt=static_cast<int>(g_preUiBinds);
    // Gate is evaluated inside the native HUD pass, before its draws.
    // Counting copied snapshots made direct UI depend on obsolete image copies.
    if(g_preUiBinds>=3) UiCaptureGate();
}

const char* UiGateNote() noexcept { return g_gateNote; }

const char* PreUiNote() noexcept { return g_preUiNote; }

void NoteBackBuffer(ID3D11Texture2D* image) noexcept {
    FinishUiCapture();
    D3D11_TEXTURE2D_DESC desc{};
    if(image) image->GetDesc(&desc);
    FinishDepthFrame(desc.Width,desc.Height);
    // The bound scene target is often the presented buffer itself, so it is let
    // go here, once the frame that needed it is over. Holding it across the gap
    // between frames is holding it exactly when the game wants to resize.
    if(g_sceneColour) { g_sceneColour->Release(); g_sceneColour=nullptr; }
    // Once a frame, from the present path: the snapshots belong to the frame
    // that has just finished and stay readable until the next one starts
    // overwriting them, which is after everything downstream has had its turn.
    g_preUiBindsLast=g_preUiBinds;
    g_preUiBinds=0;
    const bool completedLegacy=LegacyPreUiEnabled();
    g_preUiHeld=completedLegacy?g_preUiTaken:0;
    g_preUiTaken=0;
    if(!completedLegacy) ReleaseLegacyPreUi();
    // Settings may arrive from the worker thread. Never expose an old snapshot
    // when re-enabling fallback, nor publish a partially collected switch frame.
    g_legacyPreUiFrameGeneration=g_legacyPreUiGeneration.load(std::memory_order_acquire);
    g_legacyPreUiActive=g_legacyPreUiRequested.load(std::memory_order_acquire);
    if(!g_legacyPreUiActive) Note("legacy pre-UI capture off; HUD gate uses bind count");
    else if(!completedLegacy) Note("legacy pre-UI capture resumed; awaiting fresh frame");
    g_gateAtLast=g_gateAt; g_gateAt=-1;
    g_gateCallsLast=g_gateCalls; g_gateCalls=0;
    g_gateYesLast=g_gateYes; g_gateYes=0;
    if(g_gateCallsLast)
        std::snprintf(g_gateNote,sizeof(g_gateNote),"calls=%d yes=%d at=%d thread=%lu render=%lu",
                      g_gateCallsLast,g_gateYesLast,g_gateAtLast,g_gateThread,
                      g_bindThread.load(std::memory_order_relaxed));
    if(!image) return;
    if(g_preUiHeld>0) {
        ID3D11DeviceContext* context=nullptr;
        ID3D11Device* device=nullptr;
        image->GetDevice(&device);
        if(device) {
            device->GetImmediateContext(&context);
            device->Release();
        }
        if(context) {
            ChoosePreUi(context,image);
            context->Release();
        }
    }
    if(IsBackBuffer(image)) return;
    AcquireSRWLockExclusive(&g_backLock);
    bool known=false;
    for(int i=0;i<g_backBufferCount;++i) if(g_backBuffers[i]==image) { known=true; break; }
    if(!known) {
        // A new set of buffers: the old addresses are stale and in the way.
        if(g_backBufferCount>=kMaxBackBuffers) {
            for(auto& slot:g_backBuffers) slot=nullptr;
            g_backBufferCount=0;
        }
        g_backBuffers[g_backBufferCount++]=image;
    }
    ReleaseSRWLockExclusive(&g_backLock);
}

ID3D11Texture2D* SceneDepth() noexcept {
    AcquireSRWLockShared(&g_lock);
    const int row=g_sceneRow.load(std::memory_order_relaxed);
    auto* image=row>=0 && row<g_targetCount?g_targets[row].held:nullptr;
    ReleaseSRWLockShared(&g_lock);
    return image;
}

void ReportDepthProbe(CaptureLogger log) noexcept {
    if(!log) return;
    char line[400]{};
    if(!g_installed.load()) {
        std::snprintf(line,sizeof(line),"DEPTH %s",g_status);
        log(line);
        return;
    }
    // The runtime rewrites its own dispatch entries, so the hook has to be put
    // back whenever it is found gone rather than assumed to still be there.
    bool bound=false;
    if(g_table && Readable(g_table,(kHighestSlot+1)*sizeof(void*))) {
        bound=g_table[kOMSetRenderTargetsSlot]==reinterpret_cast<void*>(&HookOMSetRenderTargets);
        if(!bound) {
            auto current=g_table[kOMSetRenderTargetsSlot];
            if(InD3d11(current)) {
                g_original=reinterpret_cast<OMSetRenderTargetsFn>(current);
                bool changed=false;
                ReplacePointer(&g_table[kOMSetRenderTargetsSlot],current,
                               reinterpret_cast<void*>(&HookOMSetRenderTargets),changed);
                if(changed) g_rebinds.fetch_add(1,std::memory_order_relaxed);
            }
        }
    }
    Target copy[kMaxTargets]{};
    int count=0;
    unsigned long long overflow=0,frameNumber=0,sceneDraws=0,switches=0;
    int selected=-1;
    AcquireSRWLockExclusive(&g_lock);
    count=g_targetCount;
    overflow=g_targetOverflow;
    frameNumber=g_completedDepthFrame; sceneDraws=g_sceneFrameDraws; switches=g_sceneSwitches;
    selected=g_sceneRow.load();
    std::memcpy(copy,g_targets,sizeof(Target)*static_cast<size_t>(count));
    for(int i=0;i<count;++i) { g_targets[i].binds=0; g_targets[i].modelDraws=0; }
    // Walk the candidates so each single-sample one gets read in turn.
    for(int step=0;step<kMaxTargets;++step) {
        g_sampleRow=(g_sampleRow+1)%(count>0?count:1);
        if(g_sampleRow<count && g_targets[g_sampleRow].samples==1
           && g_targets[g_sampleRow].format==DXGI_FORMAT_R32_TYPELESS) break;
    }
    ReleaseSRWLockExclusive(&g_lock);

    std::snprintf(line,sizeof(line),"DEPTH bound=%d rebinds=%llu binds=%llu modelDraws=%llu thread=%lu targets=%d overflow=%llu frame=%llu sampling=%d sceneRow=%d frameDraws=%llu switches=%llu hudless=%llu %s",
                  bound?1:0,g_rebinds.load(),g_binds.exchange(0,std::memory_order_relaxed),
                  g_modelDraws.exchange(0,std::memory_order_relaxed),g_bindThread.load(),count,overflow,
                  frameNumber,g_sampleRow,selected,sceneDraws,switches,g_hudlessCopies,g_hudlessNote);
    log(line);
    // The scene is the backbuffer-shaped target EDF6's own model draws land in.
    // Shape has to come first: a shadow atlas redraws the same models once per
    // cascade and wins on raw counts.
    FrameCapture frame{};
    const bool haveFrame=ReadFrameCapture(frame) && frame.width;
    int best=-1;
    for(int i=0;i<count;++i) {
        if(!copy[i].modelDraws) continue;
        if(haveFrame && (copy[i].width!=frame.width || copy[i].height!=frame.height)) continue;
        if(best<0 || copy[i].modelDraws>copy[best].modelDraws) best=i;
    }
    for(int i=0;i<count;++i) {
        const auto& row=copy[i];
        if(!row.held) continue;
        // srv means the texture can be sampled as it stands; depth is the middle
        // of the image, which has to move with the view if this is the scene.
        std::snprintf(line,sizeof(line),
                      "DEPTH %s row=%d tex=%p %ux%u fmt=%u viewFmt=%u samples=%u bind=0x%X srv=%d views=%u "
                      "binds=%llu modelDraws=%llu reads=%llu centre=%.6f min=%.6f max=%.6f lower=%.6f gun=%.6f,%.6f",
                      i==selected?"MAIN":(i==best?"BUSY":"    "),i,row.texture,row.width,row.height,row.format,row.viewFormat,
                      row.samples,row.bindFlags,(row.bindFlags&D3D11_BIND_SHADER_RESOURCE)?1:0,row.views,
                      row.binds,row.modelDraws,row.reads,row.centre,row.nearest,row.farthest,row.corner,
                      row.weapon[0],row.weapon[1]);
        log(line);
    }
}
}
