#include "native_world.h"
#include "native_world_view.h"
#include "aim_hud.h"
#include "cockpit_draw.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
std::atomic<bool> enabled{false},live{false};
std::atomic<ULONGLONG> liveAt{0};
std::atomic<float> separation{0};
std::atomic<float> physicalSeparation{0};
std::atomic<unsigned> generation{0};
NativeWorldLog logger=nullptr;
struct View {Matrix view{},projection{};unsigned calls=0;bool sawView=false,sawProjection=false;};
struct State {
    ComPtr<ID3D11Texture2D> image[3];   // left, right, the scope's mono view
    View views[3][2]{}; // Main/Far; shadows retain their original view
    AimHudSnapshot aim{};
    CockpitPose cockpit{};
    bool cockpitMatched=false;
    float aimScale=.5f;
    std::uint64_t frame=0;
    ULONGLONG at=0;
    unsigned generation=0,width=0,height=0;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    float ipd=0,physicalIpd=0;
    int eye=-1,mode=-1;
    bool bad=false,attempted=false,copied[2]{};
    // Eye 2, the scope: kept apart, so a scope that fails never costs the pair.
    bool scopeCopied=false,scopeBad=false;
} state;
std::uint64_t pairs=0,rejected=0,scopes=0;
ULONGLONG nextReport=0;
Matrix InverseRigid(const Matrix& m) {
    Matrix out{};out.m[3][3]=1;
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) out.m[i][j]=m.m[j][i];
    for(int j=0;j<3;++j) for(int k=0;k<3;++k) out.m[3][j]-=m.m[3][k]*out.m[k][j];
    return out;
}
bool MatchingViews(float& measured) {
    measured=0;
    for(unsigned mode=0;mode<2;++mode) {
        const auto& l=state.views[0][mode];const auto& r=state.views[1][mode];
        if(mode==1 && !l.calls && !r.calls) continue;
        if(l.calls!=1 || r.calls!=1 || !l.sawView || !r.sawView || !l.sawProjection || !r.sawProjection) return false;
        const auto left=InverseRigid(l.view),right=InverseRigid(r.view);
        float displacement=0;
        for(int i=0;i<3;++i) {
            const float delta=right.m[3][i]-left.m[3][i];
            displacement+=delta*delta;
            if(std::fabs(delta+left.m[0][i]*state.ipd)>.003f) return false;
            for(int j=0;j<3;++j) if(std::fabs(left.m[i][j]-right.m[i][j])>.0002f) return false;
        }
        if(!mode) measured=std::sqrt(displacement);
        for(int i=0;i<4;++i) for(int j=0;j<4;++j)
            if(!std::isfinite(l.projection.m[i][j]) || !std::isfinite(r.projection.m[i][j]) ||
               std::fabs(l.projection.m[i][j]-r.projection.m[i][j])>.0001f) return false;
    }
    return true;
}
bool Capture(unsigned eye,ID3D11DeviceContext* ctx,ID3D11Texture2D* source) {
    if(!ctx || !source || ctx->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
    ComPtr<ID3D11Device> device,sourceDevice;ctx->GetDevice(&device);source->GetDevice(&sourceDevice);
    if(device.Get()!=sourceDevice.Get()) return false;
    if(eye>=1 && state.image[0]) {
        ComPtr<ID3D11Device> leftDevice;state.image[0]->GetDevice(&leftDevice);
        if(device.Get()!=leftDevice.Get()) return false;
    }
    D3D11_TEXTURE2D_DESC d{};source->GetDesc(&d);
    if(!d.Width || !d.Height || d.ArraySize!=1 || d.MipLevels!=1 || !d.SampleDesc.Count) return false;
    if(eye==0) {state.width=d.Width;state.height=d.Height;state.format=d.Format;}
    else if(state.width!=d.Width || state.height!=d.Height || state.format!=d.Format) return false;
    D3D11_TEXTURE2D_DESC old{};if(state.image[eye]) state.image[eye]->GetDesc(&old);
    if(old.Width!=d.Width || old.Height!=d.Height || old.Format!=d.Format) {
        state.image[eye].Reset();auto target=d;target.SampleDesc={1,0};target.Usage=D3D11_USAGE_DEFAULT;
        target.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;target.CPUAccessFlags=0;target.MiscFlags=0;
        if(FAILED(device->CreateTexture2D(&target,nullptr,&state.image[eye]))) return false;
    } else {ComPtr<ID3D11Device> oldDevice;state.image[eye]->GetDevice(&oldDevice);if(oldDevice.Get()!=device.Get()) return false;}
    if(d.SampleDesc.Count>1) ctx->ResolveSubresource(state.image[eye].Get(),0,source,0,d.Format);
    else ctx->CopyResource(state.image[eye].Get(),source);
    return true;
}
}
void ConfigureNativeWorld(bool on,NativeWorldLog log) noexcept {logger=log;enabled=on;live=false;++generation;}
bool NativeWorldEnabled() noexcept {return enabled.load();}
void RefreshNativeWorld(bool on,float ipd,float physicalIpd) noexcept {
    const float physical=physicalIpd==0?ipd:physicalIpd;
    on=on && enabled && std::isfinite(ipd) && ipd>.025f && ipd<.15f
        && std::isfinite(physical) && physical>.025f && physical<.15f;
    if(!on) {if(live.exchange(false)) ++generation;return;}
    separation=ipd;physicalSeparation=physical;liveAt=GetTickCount64();live=true;
}
bool NativeWorldLive() noexcept {return enabled && live && GetTickCount64()-liveAt.load()<250;}
float NativeWorldSeparation() noexcept {return separation.load();}
int NativeWorldRenderEye() noexcept {return state.eye;}
const CockpitPose* NativeWorldCockpit() noexcept {
    // Restrict by the verified viewport, not guessed material pass numbers.
    // Nix is within the main near range; shadow/far views keep full geometry.
    return state.eye>=0&&state.mode==0&&state.cockpitMatched&&CockpitEnabled()&&NativeWorldLive()?&state.cockpit:nullptr;
}
std::uint64_t NativeWorldRenderFrame() noexcept {return state.eye>=0?state.frame:0;}
void BeginNativeWorldEye(std::uint64_t frame,unsigned eye) noexcept {
    if(eye>2 || !frame) return;
    if(eye==2) {
        // After a complete pair only; it never marks the pair bad.
        std::memset(state.views[2],0,sizeof(state.views[2]));
        state.scopeCopied=false;
        state.scopeBad=state.frame!=frame || !state.copied[1] || state.eye>=0;
        state.eye=2;state.mode=-1;
        return;
    }
    if(!eye) {
        state.scopeCopied=false;state.scopeBad=false;
        const bool unfinished=state.eye>=0;
        std::memset(state.views,0,sizeof(state.views));state.copied[0]=state.copied[1]=false;
        state.frame=frame;state.at=GetTickCount64();state.generation=generation.load();
        state.aim=ReadAimHud();state.aimScale=AimHudScale(); // same immutable targets in BOTH eyes
        state.cockpit={};state.cockpitMatched=false;
        state.ipd=separation.load();state.physicalIpd=physicalSeparation.load();state.bad=unfinished;state.attempted=true;
    } else if(state.frame!=frame || !state.copied[0]) state.bad=true;
    state.eye=static_cast<int>(eye);state.mode=-1;
    if(!NativeWorldLive() || state.generation!=generation.load()) state.bad=true;
}
void BeginNativeWorldView(unsigned mode,bool refresh) noexcept {
    state.mode=-1;
    if(state.eye<0 || mode>1) return;
    state.mode=static_cast<int>(mode);++state.views[state.eye][mode].calls;
    if(!refresh) {if(state.eye==2) state.scopeBad=true; else state.bad=true;}
}
void ObserveNativeWorldView(const Matrix& m) noexcept {
    if(state.eye<0 || state.mode<0) return;
    auto& v=state.views[state.eye][state.mode];v.view=m;v.sawView=ValidNativeWorldCamera(m);
    if(state.eye==0&&state.mode==0) state.cockpitMatched=MatchCockpit(m,state.cockpit);
}
void ObserveNativeWorldProjection(const Matrix& m) noexcept {
    if(state.eye<0 || state.mode<0) return;
    auto& v=state.views[state.eye][state.mode];v.projection=m;v.sawProjection=true;
}
void EndNativeWorldView() noexcept {state.mode=-1;}
void EndNativeWorldEye(std::uint64_t frame,unsigned eye,ID3D11DeviceContext* ctx,ID3D11Texture2D* source) noexcept {
    if(eye==2) {
        // The scope's view: copied as it is, nothing drawn into it.
        state.scopeCopied=frame==state.frame && state.eye==2 && !state.scopeBad && Capture(2,ctx,source)
            && state.views[2][0].sawView && state.views[2][0].sawProjection;
        state.eye=-1;state.mode=-1;
        return;
    }
    if(frame!=state.frame || eye>1 || state.eye!=static_cast<int>(eye)) {state.bad=true;return;}
    state.copied[eye]=!state.bad && Capture(eye,ctx,source);
    if(state.copied[eye] && state.views[eye][0].sawView && state.views[eye][0].sawProjection) {
        if(state.cockpitMatched&&state.cockpit.rig.capCount) {
            // The native depth still belongs to THIS eye here. Joint lids
            // respect moving fists/world occluders without retaining/copying
            // a full-size stereo depth pair until Present.
            DrawNativeBargaCaps(ctx,state.image[eye].Get(),state.views[eye][0].view,state.views[eye][0].projection,state.cockpit,frame,eye);
        }
        // Cabin + instrument screens are drawn at Present, after the game's
        // direct HUD capture is complete, using these same latched matrices.
        if(!DrawAimHud(ctx,state.image[eye].Get(),state.views[eye][0].view,state.views[eye][0].projection,state.aim,state.aimScale)
           && logger) logger("AIMHUD draw failed; native world preserved");
    }
    if(!state.copied[eye]) state.bad=true;
    state.eye=-1;state.mode=-1;
}
void CancelNativeWorldEye(std::uint64_t frame) noexcept {
    if(state.frame==frame) {state.bad=true;state.eye=-1;state.mode=-1;}
}
NativeWorldImages TakeNativeWorldImages(unsigned width,unsigned height) noexcept {
    NativeWorldImages out{};out.attempted=state.attempted;out.frame=state.frame;out.physicalIpd=state.physicalIpd;
    float measured=0;
    out.ready=out.attempted && !state.bad && state.eye<0 && state.copied[0] && state.copied[1] &&
        NativeWorldLive() && state.generation==generation.load() && GetTickCount64()-state.at<500 &&
        width==state.width && height==state.height && MatchingViews(measured);
    if(out.ready) {
        out.eye[0]=state.image[0];out.eye[1]=state.image[1];++pairs;
        if(state.scopeCopied && state.image[2]) {
            out.scope=state.image[2];out.scopeView=state.views[2][0].view;out.scopeProjection=state.views[2][0].projection;
            out.scopeReady=true;++scopes;
        }
        out.cockpitMatched=state.cockpitMatched&&CockpitEnabled();out.cockpit=state.cockpit;
        for(unsigned i=0;i<2;++i) {out.view[i]=state.views[i][0].view;out.projection[i]=state.views[i][0].projection;}
    }
    else if(out.attempted) ++rejected;
    if(out.attempted && logger && GetTickCount64()>=nextReport) {
        nextReport=GetTickCount64()+5000;char line[512]{};
        std::snprintf(line,sizeof(line),"NATIVEWORLD frame=%llu ready=%d pairs=%llu rejected=%llu copied=%d/%d main=%u/%u far=%u/%u separation=%.5f/%.5fm size=%ux%u bad=%d path=%s scopes=%llu",
            out.frame,out.ready,pairs,rejected,state.copied[0],state.copied[1],state.views[0][0].calls,state.views[1][0].calls,
            state.views[0][1].calls,state.views[1][1].calls,measured,state.ipd,width,height,state.bad,out.ready?"two-native-eyes":"board-fallback",scopes);logger(line);
        if(CockpitEnabled()) {
            const auto s=ReadCockpitDrawStats();
            std::snprintf(line,sizeof(line),"COCKPIT matched=%d interiors=%llu instruments=%llu textured=%d ao=%d lights=%d environment=%d filtered=%llu pending=%llu unsupported=%llu meshes=%u kept=%u removed=%u init=%llu prepareMs=%llu ownDepthCaps=%llu lid=%u lidDraws=%llu",
                state.cockpitMatched,s.interiors,s.instrumentFrames,s.textured,s.occlusion,s.nativeLights,s.environment,s.filtered,s.pending,s.unsupported,s.meshes,s.keptTriangles,s.removedTriangles,s.initializations,s.prepareMs,
                s.ownDepthCaps,s.lidTriangles,s.lidDraws);logger(line);
        }
    }
    state.attempted=false;return out;
}
void ClearNativeWorldImages() noexcept {state={};DiscardCockpitFrame();}
}
