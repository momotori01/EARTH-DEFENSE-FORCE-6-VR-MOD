// Production frame submission, fake XR validation, real D3D11 WARP images.
// Reproduce Quad -> stereo without depth, then resize with both eyes alive.
#include "../src/openxr_session.cpp"
#include "../src/ui_capture.h"
#include <cstdio>
#include <vector>
#include <wrl/client.h>
namespace {
using namespace edf6vr;
int failures=0, created=0, projections=0;
bool expectPair=false;
bool validHeadLocation=false,expectRecordedPose=false;
bool expectNativeWorld=false;
XrPosef expectedNativePoses[2]{};
XrFovf expectedNativeFov{};
XrStructureType lastWorldLayer=static_cast<XrStructureType>(0);
uint32_t lastLayerCount=0;
XrSwapchain failNextAcquire=XR_NULL_HANDLE,failNextRelease=XR_NULL_HANDLE;
unsigned transferFailures=0;
bool failNextRightCreate=false;
unsigned createFailures=0;
int waits=0,begins=0,ends=0;
bool frameBegun=false,renderRequested=true,sendReady=false,sendStopping=false;
XrResult waitResult=XR_SUCCESS,beginResult=XR_SUCCESS;
XrTime prediction=1000;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
struct Chain { ID3D11Texture2D* texture=nullptr; bool acquired=false, waited=false, released=false; unsigned width=0,height=0; } chains[20];
Chain* Find(XrSwapchain h) {
    const auto id=reinterpret_cast<std::uintptr_t>(h);
    if(!id || id>=20 || !chains[id].texture) { CHECK(false); return nullptr; }
    return &chains[id];
}
XrResult XRAPI_PTR Formats(XrSession,uint32_t capacity,uint32_t* count,int64_t* formats) {
    *count=1; if(capacity) formats[0]=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; return XR_SUCCESS;
}
XrResult XRAPI_PTR Create(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    if(failNextRightCreate && out==&g_swapchainRight) {
        CHECK(g_swapchain!=XR_NULL_HANDLE && g_swapchainRight==XR_NULL_HANDLE);
        failNextRightCreate=false;++createFailures;*out=XR_NULL_HANDLE;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    CHECK(created<19); if(created>=19) return XR_ERROR_LIMIT_REACHED;
    CHECK((info->usageFlags&XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT)!=0);
    auto& c=chains[++created]; c.width=info->width; c.height=info->height;
    D3D11_TEXTURE2D_DESC d{}; d.Width=c.width; d.Height=c.height;
    d.MipLevels=1; d.ArraySize=1; d.Format=static_cast<DXGI_FORMAT>(info->format);
    d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&d,nullptr,&c.texture)));
    *out=reinterpret_cast<XrSwapchain>(static_cast<std::uintptr_t>(created)); return XR_SUCCESS;
}
XrResult XRAPI_PTR Images(XrSwapchain h,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    auto* c=Find(h); if(!c) return XR_ERROR_HANDLE_INVALID;
    *count=1; if(capacity) reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)->texture=c->texture;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR Destroy(XrSwapchain h) {
    auto* c=Find(h); if(!c) return XR_ERROR_HANDLE_INVALID;
    CHECK(!c->acquired); c->texture->Release(); c->texture=nullptr; return XR_SUCCESS;
}
XrResult XRAPI_PTR Space(XrSession,const XrReferenceSpaceCreateInfo*,XrSpace* out) {
    *out=reinterpret_cast<XrSpace>(1); return XR_SUCCESS;
}
XrResult XRAPI_PTR Poll(XrInstance,XrEventDataBuffer* event) {
    if(!sendReady && !sendStopping) return XR_EVENT_UNAVAILABLE;
    auto* changed=reinterpret_cast<XrEventDataSessionStateChanged*>(event);
    *changed={XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    changed->state=sendReady?XR_SESSION_STATE_READY:XR_SESSION_STATE_STOPPING;
    sendReady=sendStopping=false;
    return XR_SUCCESS;
}
XrResult XRAPI_PTR BeginSession(XrSession,const XrSessionBeginInfo*) { return XR_SUCCESS; }
XrResult XRAPI_PTR EndSession(XrSession) { CHECK(!frameBegun); return XR_SUCCESS; }
XrResult XRAPI_PTR Wait(XrSession,const XrFrameWaitInfo*,XrFrameState* frame) {
    CHECK(!frameBegun); ++waits;
    frame->shouldRender=renderRequested?XR_TRUE:XR_FALSE;
    frame->predictedDisplayTime=++prediction; frame->predictedDisplayPeriod=13333333;
    return waitResult;
}
XrResult XRAPI_PTR Begin(XrSession,const XrFrameBeginInfo*) {
    CHECK(!frameBegun); ++begins;
    if(XR_SUCCEEDED(beginResult)) frameBegun=true;
    return beginResult;
}
XrResult XRAPI_PTR Locate(XrSpace,XrSpace,XrTime,XrSpaceLocation* location) {
    if(!validHeadLocation) return XR_ERROR_TIME_INVALID;
    location->locationFlags=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
    location->pose={{0,0,0,1},{0,0,0}};return XR_SUCCESS;
}
XrResult XRAPI_PTR Views(XrSession,const XrViewLocateInfo*,XrViewState* state,uint32_t,uint32_t* count,XrView* views) {
    *count=2; state->viewStateFlags=XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if(validHeadLocation) state->viewStateFlags|=XR_VIEW_STATE_POSITION_VALID_BIT;
    for(int i=0;i<2;++i) { views[i].pose.orientation.w=1; views[i].pose.position.x=i?0.032f:-0.032f; views[i].fov={-0.8f,0.8f,0.8f,-0.8f}; }
    return XR_SUCCESS;
}
XrResult XRAPI_PTR Acquire(XrSwapchain h,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    auto* c=Find(h); if(!c) return XR_ERROR_HANDLE_INVALID;
    if(h==failNextAcquire) {
        CHECK(!c->acquired);failNextAcquire=XR_NULL_HANDLE;++transferFailures;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    CHECK(!c->acquired); c->acquired=true; c->waited=false; *index=0; return XR_SUCCESS;
}
XrResult XRAPI_PTR WaitImage(XrSwapchain h,const XrSwapchainImageWaitInfo*) {
    auto* c=Find(h); if(!c) return XR_ERROR_HANDLE_INVALID;
    CHECK(c->acquired); c->waited=true; return XR_SUCCESS;
}
XrResult XRAPI_PTR Release(XrSwapchain h,const XrSwapchainImageReleaseInfo*) {
    auto* c=Find(h); if(!c) return XR_ERROR_HANDLE_INVALID;
    if(h==failNextRelease) {
        CHECK(c->acquired && c->waited);failNextRelease=XR_NULL_HANDLE;++transferFailures;
        // Recoverable runtime fault: no successful publication of this image.
        // Preserve any previous release, exercising stale g_haveImage=true.
        c->acquired=false;c->waited=false;return XR_ERROR_RUNTIME_FAILURE;
    }
    CHECK(c->acquired && c->waited); c->released=true; c->acquired=false; return XR_SUCCESS;
}
void Validate(const XrSwapchainSubImage& image) {
    auto* c=Find(image.swapchain); if(!c) return;
    CHECK(c->released && !c->acquired);
    CHECK(image.imageRect.extent.width==static_cast<int32_t>(c->width));
    CHECK(image.imageRect.extent.height==static_cast<int32_t>(c->height));
}
XrResult XRAPI_PTR End(XrSession,const XrFrameEndInfo* info) {
    CHECK(frameBegun); frameBegun=false; ++ends;
    CHECK(info->displayTime==prediction);
    lastLayerCount=info->layerCount;
    lastWorldLayer=info->layerCount?info->layers[0]->type:static_cast<XrStructureType>(0);
    if(!renderRequested) CHECK(info->layerCount==0);
    for(uint32_t i=0;i<info->layerCount;++i) {
        if(info->layers[i]->type==XR_TYPE_COMPOSITION_LAYER_QUAD)
            Validate(reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[i])->subImage);
        else if(info->layers[i]->type==XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            ++projections;
            const auto* p=reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[i]);
            CHECK(p->viewCount==2);
            if(expectRecordedPose) for(unsigned v=0;v<2;++v) {
                CHECK(std::fabs(p->views[v].pose.orientation.y-std::sin(.25f))<1e-5f);
                CHECK(std::fabs(p->views[v].pose.orientation.w-std::cos(.25f))<1e-5f);
            }
            if(expectNativeWorld) for(unsigned v=0;v<2;++v) {
                const auto& actual=p->views[v];const auto& expected=expectedNativePoses[v];
                CHECK(std::fabs(actual.pose.orientation.x-expected.orientation.x)<1e-5f);
                CHECK(std::fabs(actual.pose.orientation.y-expected.orientation.y)<1e-5f);
                CHECK(std::fabs(actual.pose.orientation.z-expected.orientation.z)<1e-5f);
                CHECK(std::fabs(actual.pose.orientation.w-expected.orientation.w)<1e-5f);
                CHECK(std::fabs(actual.pose.position.x-expected.position.x)<1e-5f);
                CHECK(std::fabs(actual.pose.position.y-expected.position.y)<1e-5f);
                CHECK(std::fabs(actual.pose.position.z-expected.position.z)<1e-5f);
                CHECK(std::fabs(actual.fov.angleLeft-expectedNativeFov.angleLeft)<1e-6f);
                CHECK(std::fabs(actual.fov.angleRight-expectedNativeFov.angleRight)<1e-6f);
                CHECK(std::fabs(actual.fov.angleUp-expectedNativeFov.angleUp)<1e-6f);
                CHECK(std::fabs(actual.fov.angleDown-expectedNativeFov.angleDown)<1e-6f);
            }
            for(uint32_t v=0;v<p->viewCount;++v) Validate(p->views[v].subImage);
            CHECK((p->views[0].subImage.swapchain!=p->views[1].subImage.swapchain)==expectPair);
            if(g_warpEnabled.load() && !expectNativeWorld) {
                // Both chains may contain old images; depth is absent in this
                // fixture. Use the fresh eye even when it is the RIGHT one.
                const auto fresh=g_frameEye.load()==1?g_swapchainRight:g_swapchain;
                CHECK(p->views[0].subImage.swapchain==fresh && p->views[1].subImage.swapchain==fresh);
            }
        }
    }
    return XR_SUCCESS;
}
}
// Actual RunDisplayFrame consumer: a rendered native pair, matched historical
// head pose, and direct RGBA HUD must reach XR without depth warp or erasure.
void TestNativeWorldSubmission(ID3D11Texture2D* desktop,ID3D11RenderTargetView* desktopView) {
    using namespace edf6vr;
    using Microsoft::WRL::ComPtr;
    constexpr unsigned side=64;
    constexpr float geometryIpd=.080f,physicalIpd=.072f;
    // Match the XR fixture's sRGB format so direct HUD views have a legal cast.
    D3D11_TEXTURE2D_DESC fixture{};desktop->GetDesc(&fixture);
    fixture.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    ComPtr<ID3D11Texture2D> nativeDesktop;ComPtr<ID3D11RenderTargetView> nativeTarget;
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&fixture,nullptr,&nativeDesktop)));
    CHECK(SUCCEEDED(g_device->CreateRenderTargetView(nativeDesktop.Get(),nullptr,&nativeTarget)));
    desktop=nativeDesktop.Get();desktopView=nativeTarget.Get();
    std::vector<unsigned> left(side*side,0xFF0000FF),right(side*side,0xFF00FF00),poison(side*side,0xFFFF0000);
    for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) {
        if(x>=29 && x<=34)left[y*side+x]=0xFF55AA11;
        if((x+y)%9==0)right[y*side+x]=0xFF7744CC;
    }
    auto read=[&](ID3D11Texture2D* image) {
        std::vector<unsigned> pixels;CHECK(image!=nullptr);if(!image)return pixels;
        D3D11_TEXTURE2D_DESC d{};image->GetDesc(&d);CHECK(d.SampleDesc.Count==1);
        pixels.resize(d.Width*d.Height);
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(g_device->CreateTexture2D(&d,nullptr,&staging)));
        if(!staging)return pixels;g_context->CopyResource(staging.Get(),image);D3D11_MAPPED_SUBRESOURCE mapped{};
        const auto hr=g_context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);CHECK(SUCCEEDED(hr));
        if(SUCCEEDED(hr)) {
            for(unsigned y=0;y<d.Height;++y)std::memcpy(pixels.data()+y*d.Width,
                static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,d.Width*4);
            g_context->Unmap(staging.Get(),0);
        }
        return pixels;
    };
    auto inverse=[](const Matrix& m) {
        Matrix out{};out.m[3][3]=1;
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out.m[i][j]=m.m[j][i];
        for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)out.m[3][j]-=m.m[3][k]*out.m[k][j];
        return out;
    };
    ConfigureNativeWorld(true);ConfigureRenderedPose(true);validHeadLocation=true;
    g_displayForce=2;g_displayMode=XrDisplayMode::ProjectionStereo;g_frameEye=0;
    g_warpEnabled=true;g_fovScale=1;g_binocularZoom=1;expectPair=true;
    g_adviceFov=1.6f;g_adviceReady=true;g_reticlePixels=512;
    g_uiLayer=true;g_reticleFollows=true;g_aimKnown=true;
    g_refSpace=reinterpret_cast<XrSpace>(1);
    EnableSceneAA(false);g_openxr.SetDesktopMirror(false);
    SetLegacyPreUiCapture(false);NoteBackBuffer(desktop);
    CHECK(InstallUiCapture(g_context));EnableUiCapture(true);
    expectedNativeFov={-.8f,.8f,.8f,-.8f};
    const auto warpedBefore=g_openxr.WarpedEyes();
    std::uint64_t frame=900;
    bool nativeGlyph=true;
    auto produce=[&](bool bothEyes,bool observePose,float yaw) {
        RefreshNativeWorld(true,geometryIpd,physicalIpd);
        Matrix camera{};for(unsigned i=0;i<4;++i)camera.m[i][i]=1;
        camera.m[0][0]=camera.m[2][2]=std::cos(yaw);
        camera.m[0][2]=-std::sin(yaw);camera.m[2][0]=std::sin(yaw);
        camera.m[3][0]=static_cast<float>(frame-899);camera.m[3][1]=2;camera.m[3][2]=-4;
        HmdSample head{};head.orientationValid=head.positionValid=true;head.frame=frame;
        head.orientation={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};head.position={3,1.7f,-2};
        RecordRenderedCamera(camera,head);
        Matrix newer=camera;newer.m[3][0]+=10;HmdSample latest=head;
        latest.frame=frame+1;latest.orientation={0,0,0,1};latest.position={7,8,9};
        RecordRenderedCamera(newer,latest); // This newer logical camera was not rendered.
        for(unsigned eye=0;eye<2;++eye) {
            const float sign=eye?1.f:-1.f;
            expectedNativePoses[eye]={{0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)},
                {head.position.x+sign*physicalIpd*.5f*std::cos(yaw),head.position.y,
                 head.position.z-sign*physicalIpd*.5f*std::sin(yaw)}};
        }
        BeginNativeRenderPose();if(observePose)ObserveNativeRenderView(inverse(camera));
        Matrix projection{};projection.m[0][0]=projection.m[1][1]=1.f/std::tan(.8f);
        projection.m[2][2]=1000.f/999.9f;projection.m[2][3]=1;projection.m[3][2]=-100.f/999.9f;
        for(unsigned eye=0;eye<(bothEyes?2u:1u);++eye) {
            auto shifted=camera;for(unsigned j=0;j<3;++j)shifted.m[3][j]+=camera.m[0][j]*(eye?-1.f:1.f)*geometryIpd*.5f;
            const auto& pixels=eye?right:left;g_context->UpdateSubresource(desktop,0,nullptr,pixels.data(),side*4,0);
            BeginNativeWorldEye(frame,eye);BeginNativeWorldView(0,true);
            ObserveNativeWorldView(inverse(shifted));ObserveNativeWorldProjection(projection);EndNativeWorldView();
            EndNativeWorldEye(frame,eye,g_context,desktop);
        }
        ++frame;
        // Deliberately destroy the native source. XR must use the two private
        // copies, including the late green bar crossing the old reticle square.
        g_context->UpdateSubresource(desktop,0,nullptr,poison.data(),side*4,0);
        g_context->OMSetRenderTargets(1,&desktopView,nullptr);
        UiCaptureTargets(g_context,1,&desktopView,nullptr,true);UiCaptureGate();FinishUiCapture();
        CHECK(UiTexture()!=nullptr && !PreUiColour());
        // A real centre glyph, not just an allocated but transparent HUD. Check
        // that the native-world path still extracts its alpha into the aim quad.
        std::vector<unsigned> hud(side*side,0);
        if(nativeGlyph)for(unsigned y=30;y<34;++y)for(unsigned x=30;x<34;++x)hud[y*side+x]=0xFF0000FF;
        hud[2*side+2]=0x80008000;
        g_context->UpdateSubresource(UiTexture(),0,nullptr,hud.data(),side*4,0);
    };
    auto submit=[&](bool projectionExpected,bool nothingExpected=false,bool boardOrNothing=false) {
        const bool captured=UiTexture()!=nullptr;
        const auto boardBefore=read(desktop);
        const int waited=waits,begun=begins,ended=ends;
        const int projected=projections;
        g_openxr.PrepareSceneFrame();g_openxr.PrepareSceneFrame();
        CHECK(waits==waited+1 && begins==begun+1 && g_displayFramePrepared);
        expectNativeWorld=projectionExpected;
        RunDisplayFrame(desktop,side,side,1);
        CHECK(waits==waited+1 && begins==begun+1 && ends==ended+1 && !g_displayFramePrepared);
        if(boardOrNothing) {
            CHECK(!projectionExpected && (lastWorldLayer==XR_TYPE_COMPOSITION_LAYER_QUAD
                || lastWorldLayer==static_cast<XrStructureType>(0)));
            nothingExpected=lastWorldLayer==static_cast<XrStructureType>(0);
        }
        CHECK(lastWorldLayer==(nothingExpected?static_cast<XrStructureType>(0):
            (projectionExpected?XR_TYPE_COMPOSITION_LAYER_PROJECTION:XR_TYPE_COMPOSITION_LAYER_QUAD)));
        if(nothingExpected) CHECK(lastLayerCount==0 && g_lastLayerCount.load()==0);
        if(!projectionExpected) CHECK(projections==projected);
        CHECK(g_openxr.WarpedEyes()==warpedBefore);
        if(projectionExpected) {
            CHECK(read(Find(g_swapchain)->texture)==left);
            CHECK(read(Find(g_swapchainRight)->texture)==right);
            CHECK(g_writtenPoseValid[0] && g_writtenPoseValid[1]);
            CHECK(lastLayerCount==3 && g_swapchainReticle!=XR_NULL_HANDLE);
            const auto hud=read(Find(g_swapchainUi)->texture);
            CHECK(hud[32*side+32]==0 && hud[2*side+2]==0x80008000);
            if(g_swapchainReticle) {
                const auto marker=read(Find(g_swapchainReticle)->texture);
                const auto crop=Find(g_swapchainReticle)->width;
                if(nativeGlyph)CHECK((marker[(crop/2)*crop+crop/2]&0xFF0000FF)==0xFF0000FF);
                else {
                    bool visible=false;for(auto p:marker)if((p>>24) && (p&255))visible=true;
                    CHECK(visible && marker[0]==0 && read(UiTexture())[32*side+32]==0);
                }
            }
        } else if(!nothingExpected) {
            // The direct capture removed this UI from the back buffer. Board
            // fallback must restore it whole: especially the central pause menu.
            const auto board=read(Find(g_swapchain)->texture),desktopPixels=read(desktop);
            CHECK(board[0]==boardBefore[0]);
            if(captured) {
                if(nativeGlyph)CHECK((board[32*side+32]&0x00FFFFFF)==0x000000FF);
                CHECK((board[2*side+2]&0x0000FF00)!=0);
            } else CHECK(board==boardBefore);
            CHECK(desktopPixels[32*side+32]==board[32*side+32]);
        }
        CHECK(!TakeNativeWorldImages(side,side).attempted);
        FinishUiCapture();
    };
    produce(true,true,.5f);submit(true);
    const float forward[3]={0,0,-1};
    g_openxr.SetAimDirection(forward,true);CHECK(g_aimFromHand.load());
    nativeGlyph=false;produce(true,true,.55f);submit(true);
    g_openxr.SetAimDirection(forward);CHECK(!g_aimFromHand.load()); // boarding restores native glyph
    nativeGlyph=true;
    produce(false,true,.6f);submit(false); // No right eye: board, never old right/depth fallback.
    produce(true,false,.7f);submit(false); // Images without matched pose also cannot be called native.
    produce(true,true,.8f);submit(true);   // Failure is recoverable on the next complete frame.
    for(unsigned failure=0;failure<4;++failure) {
        // Both XR chains have valid old images and poses. Reject the ENTIRE new
        // projection if either transfer fails, especially a left release after
        // right release succeeded. Never publish a new pose for an old image.
        const XrPosef previousPoses[2]={g_writtenPose[0],g_writtenPose[1]};
        const bool previousValid[2]={g_writtenPoseValid[0],g_writtenPoseValid[1]};
        const unsigned errors=transferFailures;
        const auto failingEye=(failure%2)?g_swapchainRight:g_swapchain;
        if(failure<2) failNextRelease=failingEye;
        else failNextAcquire=failingEye;
        produce(true,true,.95f+.1f*failure);submit(false,true);
        CHECK(transferFailures==errors+1 && failNextRelease==XR_NULL_HANDLE && failNextAcquire==XR_NULL_HANDLE);
        CHECK(std::memcmp(previousPoses,g_writtenPose,sizeof(previousPoses))==0);
        CHECK(g_writtenPoseValid[0]==previousValid[0] && g_writtenPoseValid[1]==previousValid[1]);
        // An extra display callback has no producer pair. Neither cached images
        // nor legacy warp may be used to invent another native projection.
        submit(false);
        CHECK(std::memcmp(previousPoses,g_writtenPose,sizeof(previousPoses))==0);
        produce(true,true,1.5f+.1f*failure);submit(true);
    }
    // A complete native GPU pair is not enough if the runtime cannot create
    // its right chain. Recreate through the real setup path and fail only that
    // allocation: no mono/depth fallback may be called native projection.
    DestroyDisplaySwapchain();
    const unsigned allocationErrors=createFailures;
    failNextRightCreate=true;
    produce(true,true,2.1f);submit(false,false,true);
    CHECK(createFailures==allocationErrors+1 && !failNextRightCreate);
    CHECK(g_swapchainRight==XR_NULL_HANDLE && !g_haveImage[1]);
    produce(true,true,2.2f);submit(true);
    CHECK(g_swapchainRight!=XR_NULL_HANDLE && createFailures==allocationErrors+1);
    produce(true,true,.9f);RefreshNativeWorld(false,geometryIpd,physicalIpd);
    CHECK(!NativeWorldLive());g_displayForce=1;submit(false); // VR OFF invalidates queued native images.
    g_displayForce=2;submit(false); // Configured native mode cannot fall into old warp while not live.
    expectNativeWorld=false;expectPair=false;validHeadLocation=false;
    ConfigureNativeWorld(false);ClearNativeWorldImages();ConfigureRenderedPose(false);
    EnableUiCapture(false);FinishUiCapture();g_uiLayer=false;
    SetLegacyPreUiCapture(true);NoteBackBuffer(nullptr);g_context->ClearState();
}
// Regression for the old reticle square and "UI" pixels frozen in the right
// eye after direct HUD capture. Use a late world bar absent from the pre-HUD
// snapshot and the real capture-ready signal, not a mocked boolean.
void TestDirectHudWorldIsolation() {
    using namespace edf6vr;
    using Microsoft::WRL::ComPtr;
    constexpr UINT side=128;
    std::vector<unsigned> scene(side*side,0xFF202020),early(side*side,0xFF202020);
    for(UINT y=0;y<side;++y) for(UINT x=60;x<68;++x) scene[y*side+x]=0xFF00FF00;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=desc.Height=side;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA data{scene.data(),side*4,0};
    CHECK(!g_resolved);if(g_resolved) return;
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc,&data,&g_resolved)));
    ComPtr<ID3D11Texture2D> before,target,staging,depth;
    data.pSysMem=early.data();CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc,&data,&before)));
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc,nullptr,&target)));
    desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc,nullptr,&staging)));
    std::vector<float> z(side*side,.90009f); // planar world about one metre away
    desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.Format=DXGI_FORMAT_R32_FLOAT;
    data.pSysMem=z.data();CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc,&data,&depth)));
    if(!g_resolved || !before || !target || !staging || !depth) return;
    auto render=[&](const WarpParams& params,ID3D11Texture2D* underlay) {
        CHECK(WarpEye(g_device,g_context,g_resolved,params.eraseOnly?nullptr:depth.Get(),target.Get(),underlay,params));
        g_context->CopyResource(staging.Get(),target.Get());
        std::vector<unsigned> pixels(side*side);
        D3D11_MAPPED_SUBRESOURCE map{};
        if(SUCCEEDED(g_context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) {
            for(UINT y=0;y<side;++y) std::memcpy(pixels.data()+y*side,static_cast<const unsigned char*>(map.pData)+y*map.RowPitch,side*4);
            g_context->Unmap(staging.Get(),0);
        } else CHECK(false);
        return pixels;
    };
    g_uiLayer=true;g_reticleFollows=true;g_aimKnown=true;g_reticlePixels=208;g_warpHudless=true;
    EnableUiCapture(false);
    WarpParams legacy{};legacy.eraseOnly=true;ConfigureWorldUi(legacy,true);
    CHECK(legacy.eraseReticle && legacy.uiMode==2 && legacy.useHudless);
    const auto scar=render(legacy,before.Get());
    CHECK(scar[64*side+64]!=scene[64*side+64]); // old centre is erased to earlier world

    ComPtr<ID3D11RenderTargetView> worldTarget;
    CHECK(SUCCEEDED(g_device->CreateRenderTargetView(g_resolved,nullptr,&worldTarget)));
    CHECK(InstallUiCapture(g_context));EnableUiCapture(true);
    auto* rtv=worldTarget.Get();g_context->OMSetRenderTargets(1,&rtv,nullptr);
    UiCaptureTargets(g_context,1,&rtv,nullptr,true);UiCaptureGate();FinishUiCapture();
    CHECK(UiTexture()!=nullptr);
    WarpParams clean{};clean.eraseOnly=true;ConfigureWorldUi(clean,true);
    CHECK(!clean.eraseReticle && clean.uiMode==0 && !clean.useHudless);
    CHECK(!SetReticleErase(clean,true));
    CHECK(render(clean,before.Get())==scene); // native eye keeps every final world pixel
    WarpParams expected{};expected.eyeSeparation=.064f;expected.verticalFovRadians=1.5f;
    expected.nearPlane=.1f;expected.farPlane=1000;expected.uiMode=0;
    const auto reference=render(expected,nullptr); // final world-only reference
    clean=expected;ConfigureWorldUi(clean,true);
    CHECK(render(clean,before.Get())==reference); // right eye doesn't freeze late world
    // Isolate the second old bug without the reticle rectangle: mode1 pins any
    // final/pre-HUD difference at its unshifted source UV, despite world depth.
    auto frozen=expected;frozen.exact=true;frozen.useHudless=true;frozen.uiMode=1;
    CHECK(render(frozen,before.Get())!=reference);
    // Capture not ready on the next frame: keep the legacy fallback available.
    FinishUiCapture();CHECK(!UiTexture());ConfigureWorldUi(clean,true);
    CHECK(clean.eraseReticle && clean.uiMode==2 && clean.useHudless);
    EnableUiCapture(false);g_context->ClearState();ReleaseWarp();
    g_resolved->Release();g_resolved=nullptr;
}

int main() {
    using namespace edf6vr;
    g_reticlePixels=96;
    CHECK(ReticleSideForHeight(1080)==96);
    CHECK(ReticleSideForHeight(2160)==192);
    CHECK(ReticleSideForHeight(1800)==160);
    CHECK(ReticleSideForHeight(0)==0);
    const float shot[3]={0.6f,0,-0.8f};
    XrPosef head{{0,0,0,1},{1,1.6f,2}}, reticle{};
    CHECK(AimReticlePose(shot,head,15,reticle));
    CHECK(std::fabs(reticle.position.x-10)<0.0001f);
    CHECK(std::fabs(reticle.position.z+10)<0.0001f);
    // The runtime may turn the head after submission. Reference-space aim is
    // unchanged; no head orientation is baked into a VIEW-space quad.
    head.orientation={0,0.70710678f,0,0.70710678f};
    XrPosef turned{}; CHECK(AimReticlePose(shot,head,15,turned));
    CHECK(std::memcmp(&reticle,&turned,sizeof(reticle))==0);
    const float q[4]={turned.orientation.x,turned.orientation.y,
                      turned.orientation.z,turned.orientation.w};
    const float forward[3]={0,0,-1}; float direction[3]{};
    RotateByQuat(q,forward,direction);
    CHECK(std::fabs(direction[0]-shot[0])<0.0001f);
    CHECK(std::fabs(direction[2]-shot[2])<0.0001f);
    const float otherShot[3]={-0.6f,0,-0.8f};
    CHECK(AimReticlePose(otherShot,head,15,turned));
    CHECK(std::fabs(turned.position.x+8)<0.0001f);
    const float invalid[3]={0,0,0}; CHECK(!AimReticlePose(invalid,head,15,turned));
    // Compare marker projection with the actual magnified scene projection,
    // including head yaw, pitch and roll. A simple angle*zoom fails this test.
    for(const XrQuaternionf camera:{XrQuaternionf{0,0,0,1},
        XrQuaternionf{0,0.70710678f,0,0.70710678f},
        XrQuaternionf{0.38268343f,0,0,0.92387953f},
        XrQuaternionf{0,0,0.38268343f,0.92387953f}}) {
        const float rotation[4]={camera.x,camera.y,camera.z,camera.w};
        const float inverse[4]={-camera.x,-camera.y,-camera.z,camera.w};
        for(float x:{-0.12f,0.0f,0.17f}) for(float zoom:{1.0f,2.0f,6.0f,8.0f,40.0f}) {
            const float localAim[3]={x,0.04f,-1}; float reference[3]{},shown[3]{},projected[3]{};
            RotateByQuat(rotation,localAim,reference);
            CHECK(ZoomReticleDirection(reference,camera,zoom,shown));
            RotateByQuat(inverse,shown,projected);
            CHECK(std::fabs(projected[0]/-projected[2]-x*zoom)<0.0001f);
            CHECK(std::fabs(projected[1]/-projected[2]-0.04f*zoom)<0.0001f);
            if(zoom==1) CHECK(std::memcmp(reference,shown,sizeof(reference))==0);
        }
    }
    float adjusted[3]{};
    const XrQuaternionf identity{0,0,0,1};
    const float centre[3]={0,0,-1},rear[3]={0,0,1};
    CHECK(ZoomReticleDirection(centre,identity,6,adjusted));
    CHECK(adjusted[0]==0 && adjusted[1]==0 && adjusted[2]==-1);
    CHECK(!ZoomReticleDirection(rear,identity,6,adjusted));
    CHECK(!ZoomReticleDirection(centre,XrQuaternionf{0,0,0,0},6,adjusted));
    g_adviceFov=1.8f; g_adviceIpd=0.065f; g_adviceReady=true;
    const auto unzoomed=RenderedFov(3840,2160);
    for(float zoom:{1.0f,2.0f,8.0f,20.0f,40.0f}) {
        g_openxr.SetBinocularZoom(zoom); float render=0,ipd=0;
        CHECK(g_openxr.StereoAdvice(render,ipd));
        CHECK(std::fabs(std::tan(0.9f)/std::tan(render*0.5f)-zoom)<0.0001f);
        CHECK(render>=0.015f && ipd==0.065f);
        // Both XR eyes still display the full field; they don't undo the zoom.
        const auto display=RenderedFov(3840,2160);
        CHECK(std::memcmp(&unzoomed,&display,sizeof(display))==0);
    }
    g_openxr.SetBinocularZoom(0); float restored=0,ipd=0;
    CHECK(g_openxr.StereoAdvice(restored,ipd) && restored==1.8f);
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&g_device,nullptr,&g_context)));
    if(!g_device || !g_context) return 1;
    // Use the same pre-tracking warmup as CreateSession. Entering either cabin
    // later must not compile/upload resources on the first displayed pair.
    g_mode=XrMode::HeadsetDisplay;ConfigureCockpit(true);
    PrepareSessionCockpit();const auto warm=ReadCockpitDrawStats().initializations;
    CHECK(warm==1);PrepareSessionCockpit();CHECK(ReadCockpitDrawStats().initializations==warm);
    for(auto kind:{CockpitKind::Nix,CockpitKind::Crawler}) {
        auto desired=kCockpitSeatedEye;desired[0]+=.2f;
        const auto cabinHead=ClampCockpitHead(desired,.10f,kind);CHECK(cabinHead[0]>.03f);
    }
    ConfigureCockpit(false);
    g_api={}; g_api.enumerateFormats=&Formats; g_api.createSwapchain=&Create;
    g_api.enumerateImages=&Images; g_api.destroySwapchain=&Destroy; g_api.createSpace=&Space;
    g_api.pollEvent=&Poll; g_api.waitFrame=&Wait; g_api.beginFrame=&Begin;
    g_api.beginSession=&BeginSession; g_api.endSession=&EndSession;
    g_api.locateSpace=&Locate; g_api.locateViews=&Views; g_api.acquireImage=&Acquire;
    g_api.waitImage=&WaitImage; g_api.releaseImage=&Release; g_api.endFrame=&End;
    g_session=reinterpret_cast<XrSession>(1); g_sessionRunning=true; g_stop=false;
    g_running=true; g_mode=XrMode::HeadsetDisplay;
    g_uiLayer=false; g_warpEnabled=true; // no scene depth, as in the real failing log
    g_displayMode=XrDisplayMode::ProjectionStereo;
    g_uiLayer=true;g_displayForce=2;
    CHECK(g_openxr.UiCaptureAvailable());
    g_displayForce=1;CHECK(!g_openxr.UiCaptureAvailable());
    g_displayForce=2;g_displayMode=XrDisplayMode::Quad;CHECK(!g_openxr.UiCaptureAvailable());
    g_displayMode=XrDisplayMode::ProjectionStereo;g_sessionRunning=false;CHECK(!g_openxr.UiCaptureAvailable());
    g_sessionRunning=true;g_uiLayer=false;CHECK(!g_openxr.UiCaptureAvailable());
    int round=0;
    for(unsigned side:{64u,128u,64u}) {
        g_warpEnabled=++round!=3; // also preserve native alternating stereo
        D3D11_TEXTURE2D_DESC d{}; d.Width=side; d.Height=side; d.MipLevels=1; d.ArraySize=1;
        d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D* source=nullptr; CHECK(SUCCEEDED(g_device->CreateTexture2D(&d,nullptr,&source)));
        if(!source) return 1;
        g_displayForce=1; expectPair=false; g_openxr.RenderFrame(source,side,side,1);
        CHECK(g_haveImage[0] && !g_haveImage[1]); // Quad did not populate right chain
        g_displayForce=2; g_frameEye=0;
        const int before=projections, waitBefore=waits;
        g_openxr.PrepareSceneFrame(); g_openxr.PrepareSceneFrame();
        CHECK(waits==waitBefore+1 && g_displayFramePrepared);
        g_openxr.RenderFrame(source,side,side,1);
        CHECK(waits==waitBefore+1 && !g_displayFramePrepared);
        CHECK(projections==before+1 && !g_haveImage[1]); // valid native-eye fallback
        g_frameEye=1; expectPair=!g_warpEnabled.load(); RunDisplayFrame(source,side,side,1);
        CHECK(g_haveImage[0] && g_haveImage[1]);
        source->Release();
    }
    CHECK(waits==9 && begins==9 && ends==9 && !g_displayFramePrepared);
    g_displayOwnerThread=0; // simulate a fresh display session
    // Runtime ownership is learned only at Present. Before it, wrong threads,
    // not-ready sessions and tracking-only mode may not start an early frame.
    g_running=true; g_mode=XrMode::HeadsetDisplay;
    g_openxr.PrepareSceneFrame(); CHECK(waits==9);
    g_displayOwnerThread=GetCurrentThreadId();
    std::thread wrongThread([] { g_openxr.PrepareSceneFrame(); }); wrongThread.join();
    CHECK(waits==9);
    g_mode=XrMode::TrackingOnly; g_openxr.PrepareSceneFrame(); CHECK(waits==9);
    g_mode=XrMode::HeadsetDisplay; g_sessionRunning=false;
    g_openxr.PrepareSceneFrame(); CHECK(waits==9);
    // READY, duplicate scene passes and shouldRender=false still produce one cycle.
    sendReady=true; renderRequested=false;
    g_openxr.PrepareSceneFrame(); g_openxr.PrepareSceneFrame();
    CHECK(waits==10 && begins==10 && ends==9 && g_displayFramePrepared);
    g_openxr.RenderFrame(nullptr,0,0,0);
    CHECK(waits==10 && begins==10 && ends==10 && !g_displayFramePrepared);
    // A menu without an early hook uses the original fallback.
    g_openxr.RenderFrame(nullptr,0,0,0);
    CHECK(waits==11 && begins==11 && ends==11);
    // Failed wait/begin never fabricate a prepared frame or submit an end.
    waitResult=XR_ERROR_RUNTIME_FAILURE;
    g_openxr.PrepareSceneFrame(); CHECK(waits==12 && begins==11 && !g_displayFramePrepared);
    waitResult=XR_SUCCESS; beginResult=XR_ERROR_RUNTIME_FAILURE;
    g_openxr.PrepareSceneFrame(); CHECK(waits==13 && begins==12 && !g_displayFramePrepared);
    beginResult=XR_SUCCESS;
    g_openxr.PrepareSceneFrame(); CHECK(waits==14 && begins==13 && ends==11);
    // Runtime-driven STOPPING between early begin and Present drains before endSession.
    sendStopping=true;
    g_openxr.RenderFrame(nullptr,0,0,0);
    CHECK(!g_sessionRunning && !g_displayFramePrepared && ends==12 && !frameBegun);
    g_openxr.RenderFrame(nullptr,0,0,0); CHECK(waits==14 && ends==12);
    // With unwarped alternating stereo, desktop must keep RIGHT when the game
    // draws a new LEFT image; also prove composition never alters XR sources.
    g_sessionRunning=true;g_displayForce=2;g_warpEnabled=false;expectPair=true;renderRequested=true;
    D3D11_TEXTURE2D_DESC md{};md.Width=md.Height=64;md.MipLevels=md.ArraySize=md.SampleDesc.Count=1;
    md.Format=DXGI_FORMAT_R8G8B8A8_UNORM;md.BindFlags=D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* desktop=nullptr;ID3D11RenderTargetView* desktopView=nullptr;
    CHECK(SUCCEEDED(g_device->CreateTexture2D(&md,nullptr,&desktop)));
    CHECK(SUCCEEDED(g_device->CreateRenderTargetView(desktop,nullptr,&desktopView)));
    const FLOAT blue[4]={0,0,1,1},green[4]={0,1,0,1};
    g_context->ClearRenderTargetView(desktopView,blue);g_frameEye=1;RunDisplayFrame(desktop,64,64,1);
    g_context->ClearRenderTargetView(desktopView,green);g_frameEye=0;RunDisplayFrame(desktop,64,64,1);
    auto readPixel=[&](ID3D11Texture2D* image) {
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
        desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging=nullptr;CHECK(SUCCEEDED(g_device->CreateTexture2D(&desc,nullptr,&staging)));
        g_context->CopyResource(staging,image);D3D11_MAPPED_SUBRESOURCE map{};
        CHECK(SUCCEEDED(g_context->Map(staging,0,D3D11_MAP_READ,0,&map)));
        const unsigned pixel=*static_cast<unsigned*>(map.pData);
        g_context->Unmap(staging,0);staging->Release();return pixel;
    };
    CHECK((readPixel(desktop)&0xffffff)==0xff0000); // right remains blue
    CHECK((readPixel(Find(g_swapchain)->texture)&0xffffff)==0x00ff00); // HMD left remains new green
    CHECK((readPixel(Find(g_swapchainRight)->texture)&0xffffff)==0xff0000); // HMD right remains blue
    g_openxr.SetDesktopMirror(false);
    g_context->ClearRenderTargetView(desktopView,green);g_frameEye=0;RunDisplayFrame(desktop,64,64,1);
    CHECK((readPixel(desktop)&0xffffff)==0x00ff00); // disabled => native desktop
    g_openxr.SetDesktopMirror(true);g_displayForce=1;
    g_context->ClearRenderTargetView(desktopView,green);RunDisplayFrame(desktop,64,64,1);
    CHECK((readPixel(desktop)&0xffffff)==0x00ff00); // board/menu => native desktop
    // End-to-end: old queued native view, newer logic camera, newest XR locate.
    // Both projection submissions must keep the pose belonging to the image.
    ConfigureRenderedPose(true);validHeadLocation=true;expectRecordedPose=true;
    g_displayForce=2;g_warpEnabled=true;g_frameEye=0;expectPair=false;
    Matrix oldCamera{};for(int i=0;i<4;++i) oldCamera.m[i][i]=1;
    HmdSample oldHead{};oldHead.orientationValid=oldHead.positionValid=true;
    oldHead.orientation={0,std::sin(.25f),0,std::cos(.25f)};oldHead.frame=300;
    RecordRenderedCamera(oldCamera,oldHead);
    Matrix nextCamera=oldCamera;nextCamera.m[3][0]=10;HmdSample nextHead=oldHead;nextHead.orientation={0,0,0,1};nextHead.frame=301;
    RecordRenderedCamera(nextCamera,nextHead);
    g_openxr.PrepareSceneFrame();BeginNativeRenderPose();ObserveNativeRenderView(oldCamera);
    RunDisplayFrame(desktop,64,64,1);
    CHECK(std::fabs(g_writtenPose[0].orientation.y-std::sin(.25f))<1e-5f);
    expectRecordedPose=false; // no native observation next frame => old fallback, never reuse selection
    RunDisplayFrame(desktop,64,64,1);CHECK(std::fabs(g_writtenPose[0].orientation.y)<1e-5f);
    ConfigureRenderedPose(false);validHeadLocation=false;
    TestNativeWorldSubmission(desktop,desktopView);
    desktopView->Release();desktop->Release();
    DestroyDisplaySwapchain();
    TestDirectHudWorldIsolation();
    ReleaseCockpitDraw();
    g_context->Release(); g_device->Release(); g_context=nullptr; g_device=nullptr;
    printf("XR resize and missing depth/eye: %d failures, %d projections\n",failures,projections);
    return failures?1:0;
}
