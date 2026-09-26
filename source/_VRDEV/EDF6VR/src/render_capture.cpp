#include "render_capture.h"
#include "image_profile.h"
#include "frame_profile.h"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace edf6vr {
namespace {
// IDXGISwapChain vtable layout: IUnknown 0..2, IDXGIObject 3..6,
// IDXGIDeviceSubObject 7, then Present at 8. IDXGISwapChain1 adds Present1 at 22.
constexpr int kPresentSlot=8;
constexpr int kPresent1Slot=22;
// Reading the description waits until the game has settled instead of racing
// its very first frame, which is where 0.4.0 lost the process.
constexpr unsigned long long kDescribeFrame=600;

using PresentFn=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
using Present1Fn=HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain1*,UINT,UINT,const DXGI_PRESENT_PARAMETERS*);

PresentFn g_originalPresent=nullptr;
Present1Fn g_originalPresent1=nullptr;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_describe{true};
std::atomic<unsigned long long> g_presents{0}, g_present1Calls{0};
std::atomic<unsigned> g_syncInterval{0}, g_presentFlags{0};
std::atomic<unsigned> g_requestedSyncInterval{0};
std::atomic<ULONGLONG> g_uncapStamp{0};
UINT EffectiveSyncInterval(UINT requested,UINT flags) noexcept {
    const auto stamp=g_uncapStamp.load(std::memory_order_relaxed);
    return stamp && GetTickCount64()-stamp<500 && !(flags&DXGI_PRESENT_TEST)?0:requested;
}
// Whether the renderer runs inline on the game loop decides how hard a
// two-pass stereo render would be, so record who calls Present.
std::atomic<unsigned long> g_presentThread{0};
SRWLOCK g_lock=SRWLOCK_INIT;
FrameCapture g_capture{};
char g_status[256]="not installed";
CaptureLogger g_log=nullptr;
std::atomic<FrameCallback> g_frameCallback{nullptr};

// EDF.dll imports D3D11CreateDevice at this RVA; see research/render/find_d3d.py.
constexpr std::uint32_t kD3D11CreateDeviceImportRva=0x1756728;
using CreateDeviceFn=HRESULT (WINAPI*)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,
    const D3D_FEATURE_LEVEL*,UINT,UINT,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
CreateDeviceFn g_originalCreateDevice=nullptr;
std::atomic<bool> g_deviceInstalled{false};
SRWLOCK g_deviceLock=SRWLOCK_INIT;
ID3D11Device* g_gameDevice=nullptr;
ID3D11DeviceContext* g_gameContext=nullptr;

// Only ever called from the game thread while installing; the render thread
// must not log, because file IO inside Present is one of the suspects.
void SetStatus(const char* text) noexcept {
    strncpy_s(g_status,text,_TRUNCATE);
    if(g_log) g_log(g_status);
}

void SetStatusf(const char* format,...) noexcept {
    char text[256]{};
    va_list args; va_start(args,format); vsnprintf(text,sizeof(text),format,args); va_end(args);
    SetStatus(text);
}

bool InsideDxgi(const void* pointer) noexcept {
    __try {
        const auto base=reinterpret_cast<const unsigned char*>(GetModuleHandleW(L"dxgi.dll"));
        if(!base) return false;
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return false;
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE) return false;
        const auto at=reinterpret_cast<const unsigned char*>(pointer);
        return at>=base && static_cast<std::size_t>(at-base)<nt->OptionalHeader.SizeOfImage;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Reads what the game is presenting into our own struct. No logging and no
// allocation: the game thread prints this later from its own periodic report.
void Describe(IDXGISwapChain* chain) noexcept {
    DXGI_SWAP_CHAIN_DESC desc{};
    if(FAILED(chain->GetDesc(&desc))) return;
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* context=nullptr;
    if(SUCCEEDED(chain->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(&device))) && device)
        device->GetImmediateContext(&context);
    AcquireSRWLockExclusive(&g_lock);
    g_capture.swapChain=chain;
    g_capture.device=device;
    g_capture.context=context;
    g_capture.window=desc.OutputWindow;
    g_capture.width=desc.BufferDesc.Width;
    g_capture.height=desc.BufferDesc.Height;
    g_capture.format=static_cast<unsigned>(desc.BufferDesc.Format);
    g_capture.sampleCount=desc.SampleDesc.Count;
    g_capture.sampleQuality=desc.SampleDesc.Quality;
    g_capture.bufferCount=desc.BufferCount;
    g_capture.flags=desc.Flags;
    g_capture.windowed=desc.Windowed?1:0;
    g_capture.valid=true;
    ReleaseSRWLockExclusive(&g_lock);
    // The references are dropped immediately: this diagnostic must not change
    // the lifetime of anything the game owns.
    if(context) context->Release();
    if(device) device->Release();
}

// Hands the finished frame to whoever wants it, then leaves the swapchain
// exactly as it was. Runs on the render thread inside the game's present.
void Deliver(IDXGISwapChain* chain) noexcept {
    const auto callback=g_frameCallback.load(std::memory_order_relaxed);
    if(!callback) return;
    unsigned width=0,height=0,samples=0;
    AcquireSRWLockShared(&g_lock);
    bool known=g_capture.valid && g_capture.swapChain==chain;
    if(known) { width=g_capture.width; height=g_capture.height; samples=g_capture.sampleCount; }
    ReleaseSRWLockShared(&g_lock);
    if(!known) {
        // Someone wants frames, so the description cannot wait for frame 600.
        Describe(chain);
        AcquireSRWLockShared(&g_lock);
        known=g_capture.valid && g_capture.swapChain==chain;
        if(known) { width=g_capture.width; height=g_capture.height; samples=g_capture.sampleCount; }
        ReleaseSRWLockShared(&g_lock);
    }
    if(!known) return;
    ID3D11Texture2D* backBuffer=nullptr;
    if(FAILED(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&backBuffer))) || !backBuffer)
        return;
    // A menu change to resolution or anti-aliasing replaces the buffer without
    // telling us, so the cache is checked against the real thing every frame
    // rather than every six hundred.
    D3D11_TEXTURE2D_DESC live{};
    backBuffer->GetDesc(&live);
    if(live.Width!=width || live.Height!=height || live.SampleDesc.Count!=samples) {
        Describe(chain);
        AcquireSRWLockExclusive(&g_lock);
        g_capture.width=live.Width;
        g_capture.height=live.Height;
        g_capture.format=static_cast<unsigned>(live.Format);
        g_capture.sampleCount=live.SampleDesc.Count;
        g_capture.sampleQuality=live.SampleDesc.Quality;
        ReleaseSRWLockExclusive(&g_lock);
        width=live.Width;
        height=live.Height;
        samples=live.SampleDesc.Count;
    }
    callback(backBuffer,width,height,samples);
    backBuffer->Release();
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* chain,UINT interval,UINT flags) {
    g_requestedSyncInterval.store(interval,std::memory_order_relaxed);
    interval=EffectiveSyncInterval(interval,flags);
    g_syncInterval.store(interval,std::memory_order_relaxed);
    g_presentFlags.store(flags,std::memory_order_relaxed);
    g_presentThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    const auto start=PerfNow();
    static thread_local std::int64_t previous=0;
    PerfBatch perf{};
    if(previous) perf.Add(PerfStage::PresentInterval,start-previous);
    previous=start;
    const auto count=g_presents.fetch_add(1,std::memory_order_relaxed)+1;
    __try {
        if(count==kDescribeFrame && g_describe.load(std::memory_order_relaxed)) Describe(chain);
        Deliver(chain);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    const auto delivered=PerfNow();
    const HRESULT result=g_originalPresent(chain,interval,flags);
    const auto end=PerfNow();
    perf.Add(PerfStage::Deliver,delivered-start);
    perf.Add(PerfStage::NativePresent,end-delivered);
    perf.Add(PerfStage::PresentHook,end-start);
    SubmitPerf(perf);
    return result;
}

HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* chain,UINT interval,UINT flags,
                                       const DXGI_PRESENT_PARAMETERS* parameters) {
    g_requestedSyncInterval.store(interval,std::memory_order_relaxed);
    interval=EffectiveSyncInterval(interval,flags);
    g_syncInterval.store(interval,std::memory_order_relaxed);
    g_presentFlags.store(flags,std::memory_order_relaxed);
    g_presentThread.store(GetCurrentThreadId(),std::memory_order_relaxed);
    const auto start=PerfNow();
    static thread_local std::int64_t previous=0;
    PerfBatch perf{};
    if(previous) perf.Add(PerfStage::Present1Interval,start-previous);
    previous=start;
    const auto count=g_present1Calls.fetch_add(1,std::memory_order_relaxed)+1;
    __try {
        if(count==kDescribeFrame && g_describe.load(std::memory_order_relaxed)) Describe(chain);
        Deliver(chain);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    const auto delivered=PerfNow();
    const HRESULT result=g_originalPresent1(chain,interval,flags,parameters);
    const auto end=PerfNow();
    perf.Add(PerfStage::Deliver1,delivered-start);
    perf.Add(PerfStage::NativePresent1,end-delivered);
    perf.Add(PerfStage::Present1Hook,end-start);
    SubmitPerf(perf);
    return result;
}

// A throwaway device and swapchain, used only to read the DXGI vtable. DXGI
// gives every swapchain of the same class one shared vtable, so patching this
// one also covers the swapchain the game creates later.
struct Probe {
    HWND window=nullptr;
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* context=nullptr;
    IDXGISwapChain* chain=nullptr;
    ~Probe() {
        if(chain) { chain->SetFullscreenState(FALSE,nullptr); chain->Release(); }
        if(context) context->Release();
        if(device) device->Release();
        if(window) DestroyWindow(window);
    }
    bool Create() noexcept {
        window=CreateWindowExW(0,L"STATIC",L"EDF6VR",WS_OVERLAPPED,0,0,8,8,nullptr,nullptr,
                               GetModuleHandleW(nullptr),nullptr);
        if(!window) { SetStatusf("probe window failed (error %lu)",GetLastError()); return false; }
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount=1;
        desc.BufferDesc.Width=8;
        desc.BufferDesc.Height=8;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow=window;
        desc.SampleDesc.Count=1;
        desc.Windowed=TRUE;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1};
        D3D_FEATURE_LEVEL achieved{};
        const HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
            levels,2,D3D11_SDK_VERSION,&desc,&chain,&device,&achieved,&context);
        if(FAILED(hr) || !chain) {
            SetStatusf("probe swapchain failed (0x%08lX)",static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }
};
}

bool InstallPresentCapture(CaptureLogger log,bool describeSwapChain) noexcept {
    g_log=log;
    g_describe.store(describeSwapChain);
    if(g_installed.load()) return true;
    Probe probe;
    if(!probe.Create()) return false;
    auto table=*reinterpret_cast<void***>(probe.chain);
    if(!Readable(table,static_cast<std::size_t>(kPresent1Slot+1)*sizeof(void*))) {
        SetStatus("DXGI vtable is not readable");
        return false;
    }
    if(!InsideDxgi(table[kPresentSlot])) {
        SetStatus("IDXGISwapChain::Present does not point into dxgi.dll; refusing to patch");
        return false;
    }
    g_originalPresent=reinterpret_cast<PresentFn>(table[kPresentSlot]);
    bool changed=false;
    if(!ReplacePointer(&table[kPresentSlot],reinterpret_cast<void*>(g_originalPresent),
                       reinterpret_cast<void*>(&HookPresent),changed) && !changed) {
        SetStatus("Present slot replacement refused");
        return false;
    }
    g_installed.store(true);

    // Present1 exists only when the runtime hands out IDXGISwapChain1. Its slot
    // is optional: a game that never calls it simply never reaches the hook.
    IDXGISwapChain1* newer=nullptr;
    if(SUCCEEDED(probe.chain->QueryInterface(__uuidof(IDXGISwapChain1),reinterpret_cast<void**>(&newer))) && newer) {
        const bool sameObject=*reinterpret_cast<void***>(newer)==table;
        if(sameObject && InsideDxgi(table[kPresent1Slot])) {
            g_originalPresent1=reinterpret_cast<Present1Fn>(table[kPresent1Slot]);
            bool present1Changed=false;
            ReplacePointer(&table[kPresent1Slot],reinterpret_cast<void*>(g_originalPresent1),
                           reinterpret_cast<void*>(&HookPresent1),present1Changed);
            if(!present1Changed) g_originalPresent1=nullptr;
        }
        newer->Release();
    }
    SetStatusf("present capture installed: vtable=%p Present=%p Present1=%s describe=%d",
        static_cast<void*>(table),reinterpret_cast<void*>(g_originalPresent),
        g_originalPresent1?"hooked":"not hooked",describeSwapChain?1:0);
    return true;
}

HRESULT WINAPI HookCreateDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE driverType,HMODULE software,
    UINT flags,const D3D_FEATURE_LEVEL* featureLevels,UINT featureLevelCount,UINT sdkVersion,
    ID3D11Device** outDevice,D3D_FEATURE_LEVEL* outFeatureLevel,ID3D11DeviceContext** outContext) {
    const HRESULT hr=g_originalCreateDevice(adapter,driverType,software,flags,featureLevels,
        featureLevelCount,sdkVersion,outDevice,outFeatureLevel,outContext);
    if(SUCCEEDED(hr) && outDevice && *outDevice) {
        ID3D11Device* device=*outDevice;
        ID3D11DeviceContext* context=nullptr;
        device->GetImmediateContext(&context);
        device->AddRef();
        AcquireSRWLockExclusive(&g_deviceLock);
        ID3D11Device* previousDevice=g_gameDevice;
        ID3D11DeviceContext* previousContext=g_gameContext;
        g_gameDevice=device;
        g_gameContext=context;
        ReleaseSRWLockExclusive(&g_deviceLock);
        // A second creation replaces the first; the game only keeps one.
        if(previousContext) previousContext->Release();
        if(previousDevice) previousDevice->Release();
    }
    return hr;
}

bool InstallDeviceCapture(const ImageProfile& image,CaptureLogger log) noexcept {
    g_log=log;
    if(g_deviceInstalled.load()) return true;
    if(!image.base) { SetStatus("no image profile for the device capture"); return false; }
    auto slot=reinterpret_cast<void**>(image.base+kD3D11CreateDeviceImportRva);
    if(!Readable(slot,sizeof(void*))) { SetStatus("D3D11CreateDevice import is not readable"); return false; }
    const auto d3d11=GetModuleHandleW(L"d3d11.dll");
    if(!d3d11 || *slot!=reinterpret_cast<void*>(GetProcAddress(d3d11,"D3D11CreateDevice"))) {
        SetStatus("D3D11CreateDevice import does not hold the real d3d11.dll entry; refusing to patch");
        return false;
    }
    g_originalCreateDevice=reinterpret_cast<CreateDeviceFn>(*slot);
    bool changed=false;
    if(!ReplacePointer(slot,reinterpret_cast<void*>(g_originalCreateDevice),
                       reinterpret_cast<void*>(&HookCreateDevice),changed) && !changed) {
        SetStatus("D3D11CreateDevice import replacement refused");
        return false;
    }
    g_deviceInstalled.store(true);
    SetStatusf("device capture installed at EDF.dll+%X -> %p",
        kD3D11CreateDeviceImportRva,reinterpret_cast<void*>(g_originalCreateDevice));
    return true;
}

bool DeviceCaptureInstalled() noexcept { return g_deviceInstalled.load(); }

bool GameDevice(void*& device,void*& context) noexcept {
    AcquireSRWLockShared(&g_deviceLock);
    device=g_gameDevice;
    context=g_gameContext;
    ReleaseSRWLockShared(&g_deviceLock);
    return device!=nullptr && context!=nullptr;
}

void SetFrameCallback(FrameCallback callback) noexcept {
    g_frameCallback.store(callback,std::memory_order_relaxed);
}
void SetPresentUncapped(bool on) noexcept { g_uncapStamp.store(on?GetTickCount64():0,std::memory_order_relaxed); }
unsigned RequestedSyncInterval() noexcept { return g_requestedSyncInterval.load(std::memory_order_relaxed); }

unsigned long PresentThread() noexcept { return g_presentThread.load(std::memory_order_relaxed); }

bool PresentCaptureInstalled() noexcept { return g_installed.load(); }
const char* PresentCaptureStatus() noexcept { return g_status; }

bool ReadFrameCapture(FrameCapture& out) noexcept {
    AcquireSRWLockShared(&g_lock);
    out=g_capture;
    ReleaseSRWLockShared(&g_lock);
    out.presents=g_presents.load();
    out.present1Calls=g_present1Calls.load();
    out.syncInterval=g_syncInterval.load(std::memory_order_relaxed);
    out.presentFlags=g_presentFlags.load(std::memory_order_relaxed);
    return out.valid;
}
}
