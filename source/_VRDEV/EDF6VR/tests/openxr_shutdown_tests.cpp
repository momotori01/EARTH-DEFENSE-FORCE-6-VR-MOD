// Production shutdown with fake OpenXR dispatch and an actual WARP D3D11
// context/fence. No headset/runtime/game is used. Including the implementation
// lets us inject dispatch without adding test-only entry points to the DLL.
#include "../src/openxr_session.cpp"
#include <cstdio>

namespace {
int failures=0, exitCalls=0, endCalls=0, destroyCalls=0;
int hapticCalls=0,inputSpaces=0,inputSets=0,emptyEnds=0;
int hapticStops=0;
float hapticAmplitude=0;XrDuration hapticDuration=0;
bool allowStopping=false, eventSent=false;
ID3D11Texture2D* runtimeTexture=nullptr;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
XrResult XRAPI_PTR RequestExit(XrSession) {
    CHECK(emptyEnds==1 && !edf6vr::g_displayFramePrepared);
    ++exitCalls;
    CHECK(destroyCalls==0);
    return XR_SUCCESS;
}
XrResult XRAPI_PTR PollEvent(XrInstance,XrEventDataBuffer* event) {
    if(!allowStopping || eventSent) return XR_EVENT_UNAVAILABLE;
    auto* changed=reinterpret_cast<XrEventDataSessionStateChanged*>(event);
    *changed={XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    changed->state=XR_SESSION_STATE_STOPPING;
    eventSent=true;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR EndFrame(XrSession,const XrFrameEndInfo* info) {
    CHECK(exitCalls==0 && endCalls==0 && destroyCalls==0);
    CHECK(info->layerCount==0 && info->layers==nullptr && info->displayTime==12345);
    ++emptyEnds; return XR_SUCCESS;
}
XrResult XRAPI_PTR EndSession(XrSession) {
    CHECK(emptyEnds==1);
    CHECK(eventSent && allowStopping);
    CHECK(edf6vr::g_stopFence==nullptr); // fence must follow endSession
    ++endCalls;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR DestroySession(XrSession) {
    CHECK(!edf6vr::g_inputReady && edf6vr::g_actionSet==XR_NULL_HANDLE);
    CHECK(endCalls==1);
    CHECK(edf6vr::g_stopFence==nullptr); // released only after query reports completion
    ++destroyCalls;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR Haptic(XrSession,const XrHapticActionInfo*,const XrHapticBaseHeader* header) {
    auto buzz=reinterpret_cast<const XrHapticVibration*>(header);
    hapticAmplitude=buzz->amplitude;hapticDuration=buzz->duration;++hapticCalls;return XR_SUCCESS;
}
XrResult XRAPI_PTR StopHaptic(XrSession,const XrHapticActionInfo*) {++hapticStops;return XR_SUCCESS;}
XrResult XRAPI_PTR DestroyInputSpace(XrSpace) { ++inputSpaces; return XR_SUCCESS; }
XrResult XRAPI_PTR DestroyInputSet(XrActionSet) { ++inputSets; return XR_SUCCESS; }
XrResult XRAPI_PTR DestroySwapchain(XrSwapchain) {
    CHECK(runtimeTexture!=nullptr);
    if(runtimeTexture) {
        runtimeTexture->AddRef();
        CHECK(runtimeTexture->Release()==1); // WarpEye's cached RTV must be gone
    }
    return XR_SUCCESS;
}
}

int main() {
    using namespace edf6vr;
    ID3D11Device* device=nullptr;
    ID3D11DeviceContext* context=nullptr;
    D3D_FEATURE_LEVEL level{};
    const auto hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
                                   D3D11_SDK_VERSION,&device,&level,&context);
    CHECK(SUCCEEDED(hr) && device && context);
    if(!device || !context) return 1;
    // Run repeated lifecycles, catching stale pending flags and use after stop.
    for(int cycle=0;cycle<5;++cycle) {
        exitCalls=endCalls=destroyCalls=emptyEnds=0;
        allowStopping=eventSent=false;
        g_api={};
        g_api.requestExitSession=&RequestExit;
        g_api.pollEvent=&PollEvent;
        g_api.endSession=&EndSession;
        g_api.endFrame=&EndFrame;
        g_api.destroySession=&DestroySession;
        g_api.destroySwapchain=&DestroySwapchain;
        g_api.destroySpace=&DestroyInputSpace;
        g_api.destroyActionSet=&DestroyInputSet;
        g_api.applyHaptic=&Haptic;
        g_api.stopHaptic=&StopHaptic;
        g_session=reinterpret_cast<XrSession>(static_cast<std::uintptr_t>(1));
        g_device=device;
        g_context=context;
        g_ownDevice=false;
        g_mode=XrMode::HeadsetDisplay;
        g_sessionRunning=true;
        g_running=true;
        g_stop=false;
        g_inputReady=true;
        g_actionSet=reinterpret_cast<XrActionSet>(std::uintptr_t(11));
        g_hapticAction=reinterpret_cast<XrAction>(std::uintptr_t(12));
        for(int i=0;i<2;++i) {
            g_handPath[i]=static_cast<XrPath>(i+1);
            g_aimSpace[i]=reinterpret_cast<XrSpace>(std::uintptr_t(20+i));
            g_gripSpace[i]=reinterpret_cast<XrSpace>(std::uintptr_t(30+i));
            g_hand[i].tracked=g_grip[i].tracked=true;
        }
        g_controls.stick[0][0]=.8f;
        g_openxr.Buzz(0,.24f,.65f);
        FlushHaptics(XR_SUCCESS,g_hapticPending[0].until-240);CHECK(hapticCalls==cycle+1);
        CHECK(hapticAmplitude==.65f && hapticDuration>239000000 && hapticDuration<=240000000);
        g_openxr.StopBuzz(0);FlushHaptics(XR_SUCCESS,GetTickCount64());CHECK(hapticStops==cycle+1);
        AcquireSRWLockExclusive(&g_frameLock);
        g_openxr.Buzz(0,.08f,.6f); // game thread cannot wait on frame teardown
        g_openxr.StopBuzz(0); // cancellation must not deadlock with render teardown either
        ReleaseSRWLockExclusive(&g_frameLock); CHECK(hapticCalls==cycle+1);
        CHECK(hapticStops==cycle+1);
        CHECK(g_hapticPending[0].pending && g_hapticPending[0].stop);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=64; desc.Height=64; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        ID3D11Texture2D* source=nullptr;
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&source)));
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&g_resolved)));
        if(!source || !g_resolved) return 1;
        context->CopyResource(g_resolved,source);
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&runtimeTexture)));
        WarpParams erase{}; erase.eraseOnly=true; erase.uiMode=0;
        CHECK(WarpEye(device,context,g_resolved,nullptr,runtimeTexture,nullptr,erase));
        g_swapchain=reinterpret_cast<XrSwapchain>(static_cast<std::uintptr_t>(2));
        source->Release();
        CHECK(!g_displayFramePrepared && g_displayOwnerThread==0);
        g_preparedFrame={XR_TYPE_FRAME_STATE};
        g_preparedFrame.predictedDisplayTime=12345;
        g_displayFramePrepared=true;
        g_displayOwnerThread=GetCurrentThreadId();
        g_openxr.Stop();
        CHECK(!g_hapticPending[0].pending);
        g_openxr.PrepareSceneFrame(); // input-thread stop prohibits any new beginning
        CHECK(emptyEnds==0); // Stop itself must not call the runtime
        CHECK(!g_openxr.Running() && g_displayStopPending && destroyCalls==0);
        CHECK(!g_openxr.Start(nullptr,XrMode::HeadsetDisplay,device,context));
        g_openxr.RenderFrame(nullptr,0,0,0);
        CHECK(exitCalls==1 && endCalls==0 && destroyCalls==0);
        // Repeated pending frames must not request exit again or release anything.
        g_openxr.RenderFrame(nullptr,0,0,0);
        CHECK(exitCalls==1 && destroyCalls==0 && g_stopFence==nullptr);
        CHECK(g_resolved!=nullptr);
        allowStopping=true;
        const auto deadline=GetTickCount64()+2000;
        while(g_displayStopPending && GetTickCount64()<deadline) {
            g_openxr.RenderFrame(nullptr,0,0,0);
            SwitchToThread();
        }
        CHECK(!g_displayStopPending && endCalls==1 && destroyCalls==1);
        CHECK(g_resolved==nullptr);
        CHECK(emptyEnds==1 && !g_displayFramePrepared && g_displayOwnerThread==0);
        if(runtimeTexture) { runtimeTexture->Release(); runtimeTexture=nullptr; }
        CHECK(g_openxr.Mode()==XrMode::TrackingOnly && g_context==nullptr && g_device==nullptr);
        CHECK(!g_inputReady && !g_hapticAction && !g_aimSpace[0] && !g_gripSpace[1]);
        CHECK(inputSpaces==4*(cycle+1) && inputSets==cycle+1);
        ControllerState controls{}; CHECK(!g_openxr.Controls(controls) && controls.stick[0][0]==0);
        CHECK(!g_hand[1].tracked && !g_grip[1].tracked);
        // Exactly the crash window: new instance exists, before READY has
        // recreated controller APIs. StartVr requests an immediate vibration.
        g_running=true; g_stop=false;
        g_session=reinterpret_cast<XrSession>(std::uintptr_t(40));
        g_openxr.Buzz(1,.08f,.6f); CHECK(hapticCalls==cycle+1);
        g_openxr.StopBuzz(1);CHECK(hapticStops==cycle+1);
        CHECK(!CreateInput()); // no fake runtime: must attempt initialization, not return stale-ready
        g_session=XR_NULL_HANDLE; g_running=false;
        // A previously captured callback arriving after teardown must be harmless.
        g_openxr.RenderFrame(nullptr,0,0,0);
        g_openxr.Stop();
        CHECK(destroyCalls==1);
    }
    context->Release();
    device->Release();
    printf("OpenXR shutdown: %d failures (fake XR, real WARP GPU fence, 5 cycles)\n",failures);
    return failures?1:0;
}
