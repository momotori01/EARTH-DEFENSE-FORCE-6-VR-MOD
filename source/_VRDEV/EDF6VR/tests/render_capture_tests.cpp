// Exercises the DXGI present hook end to end without the game: it installs the
// hook, then drives a swapchain of its own and checks the hook saw the frames
// and read the backbuffer description back correctly.
//
// A machine with no usable D3D11 hardware reports SKIP and passes, because the
// hook cannot be exercised there. A machine that can create a swapchain but
// never reaches the hook is a real failure.
#include "../src/render_capture.cpp"
#include "frame_profile.h"
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

static void Report(const char* text) { printf("[render] %s\n",text); }

int main() {
    edf6vr::SetPresentUncapped(false);
    CHECK(edf6vr::EffectiveSyncInterval(1,0)==1);
    edf6vr::SetPresentUncapped(true);
    CHECK(edf6vr::EffectiveSyncInterval(1,0)==0);
    CHECK(edf6vr::EffectiveSyncInterval(2,DXGI_PRESENT_DO_NOT_WAIT)==0);
    CHECK(edf6vr::EffectiveSyncInterval(1,DXGI_PRESENT_TEST)==1);
    edf6vr::g_uncapStamp=GetTickCount64()-501; // mission callbacks stopped: restore desktop sync
    CHECK(edf6vr::EffectiveSyncInterval(1,0)==1);
    edf6vr::SetPresentUncapped(false);
    const bool installed=edf6vr::InstallPresentCapture(&Report);
    if(!installed) {
        printf("SKIP: %s\n",edf6vr::PresentCaptureStatus());
        return 0;
    }
    CHECK(edf6vr::PresentCaptureInstalled());
    // Installing twice must be harmless: the plugin can be reloaded.
    CHECK(edf6vr::InstallPresentCapture(&Report));

    edf6vr::FrameCapture before{};
    edf6vr::ReadFrameCapture(before);

    HWND window=CreateWindowExW(0,L"STATIC",L"EDF6VR test",WS_OVERLAPPED,0,0,64,48,
                                nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CHECK(window!=nullptr);
    if(!window) return 1;
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount=2;
    desc.BufferDesc.Width=64;
    desc.BufferDesc.Height=48;
    desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow=window;
    desc.SampleDesc.Count=1;
    desc.Windowed=TRUE;
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* context=nullptr;
    IDXGISwapChain* chain=nullptr;
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1};
    D3D_FEATURE_LEVEL achieved{};
    const HRESULT hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
        levels,2,D3D11_SDK_VERSION,&desc,&chain,&device,&achieved,&context);
    if(FAILED(hr) || !chain) {
        printf("SKIP: no usable swapchain (0x%08lX)\n",static_cast<unsigned long>(hr));
        DestroyWindow(window);
        return failures?1:0;
    }

    // The hook deliberately leaves the early frames alone and reads the
    // description on frame 600, so drive it past that point. DXGI_PRESENT_TEST
    // costs almost nothing because nothing is actually shown.
    CHECK(!before.valid);
    for(int i=0;i<5;++i) chain->Present(0,DXGI_PRESENT_TEST);
    edf6vr::FrameCapture early{};
    edf6vr::ReadFrameCapture(early);
    CHECK(early.presents>=before.presents+5);
    // Nothing may be read out of the swapchain during those first frames.
    CHECK(!early.valid);
    for(int i=0;i<700;++i) chain->Present(0,DXGI_PRESENT_TEST);

    edf6vr::FrameCapture after{};
    CHECK(edf6vr::ReadFrameCapture(after));
    CHECK(after.presents>=before.presents+705);
    CHECK(after.swapChain==chain);
    CHECK(after.device==device);
    CHECK(after.window==window);
    CHECK(after.width==64 && after.height==48);
    CHECK(after.format==DXGI_FORMAT_R8G8B8A8_UNORM);
    CHECK(after.sampleCount==1);
    CHECK(after.bufferCount==2);
    CHECK(after.windowed==1);
    CHECK(after.syncInterval==0 && after.presentFlags==DXGI_PRESENT_TEST);
    const auto perf=edf6vr::DrainPerf();
    const auto& native=perf.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::NativePresent)];
    const auto& hook=perf.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::PresentHook)];
    CHECK(native.count>=705 && native.count==hook.count);
    CHECK(hook.ticks>=native.ticks);
    CHECK(perf.droppedBatches==0);
    // The hook must not have kept a reference to anything the caller owns.
    chain->AddRef();
    CHECK(chain->Release()==1);

    chain->Release();
    if(context) context->Release();
    if(device) device->Release();
    DestroyWindow(window);
    printf(failures?"FAILURES %d\n":"present capture OK\n",failures);
    return failures?1:0;
}
