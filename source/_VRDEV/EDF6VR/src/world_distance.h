// Included by plugin.cpp: keep EDF's reference-FOV draw distance in native VR.
// 118DCD0 returns tan(camera+24 / 2) / tan(camera+20 / 2), or 1 if +39 is off.
// 1197F31 (Far) / 1197FBD (Main) store it in submission+18. The consumer
// 11E3EFA -> 11B28C0 scales distances before model+60's squared-distance cutoff.
// Correct the packet producer, before both Umbra resolve and native LOD/culling.
// No camera, model, far-clip, shadow or visibility-result mutation is needed.
using WorldDistance= float(__fastcall*)(void*);
WorldDistance g_worldDistance=nullptr;
bool g_worldDistanceReady=false;
constexpr unsigned kWorldDistanceSites[]={0x1197F31,0x1197FBD};
float PreserveWorldDistance(float native,bool stereo) noexcept {
    // Keep native zoom (<1), invalid values and non-VR paths unchanged.
    return stereo && std::isfinite(native) && native>1.f ? 1.f : native;
}
// The weapon scope's loop (eye 2): the scale the game itself gives a zoomed
// view, tan(field / 2) / tan(reference / 2), from the scope's own half field --
// what lets far enemies be drawn, and drawn in detail, when you zoom.
float ScopeWorldDistance(void* camera,float native) noexcept {
    const auto view=edf6vr::ReadScopeView();
    float reference=0;
    if(!view.active || view.mode!=1 || !(view.tanHalf>0) || !edf6vr::Readable(static_cast<unsigned char*>(camera)+0x20,4)) return native;
    std::memcpy(&reference,static_cast<unsigned char*>(camera)+0x20,4);
    const float base=std::tan(reference*.5f);
    if(!std::isfinite(base) || !(base>.01f)) return native;
    const float scale=view.tanHalf/base;
    return std::isfinite(scale) && scale>0 ? std::min(scale,1.f) : native;
}
float __fastcall HookWorldDistance(void* camera) {
    const float native=g_worldDistance(camera);
    const bool stereo=g_worldDistanceReady && edf6vr::NativeWorldProducerEye()>=0;
    const float result=edf6vr::NativeWorldProducerEye()==2?ScopeWorldDistance(camera,native):PreserveWorldDistance(native,stereo);
    if(stereo) {
        static thread_local ULONGLONG next=0;
        const auto now=GetTickCount64();
        if(now>=next) {
            next=now+60000;
            float fov[2]{};
            if(edf6vr::Readable(static_cast<unsigned char*>(camera)+0x20,sizeof(fov)))
                std::memcpy(fov,static_cast<unsigned char*>(camera)+0x20,sizeof(fov));
            Log("WORLDDIST native=%.5f applied=%.5f referenceFov=%.2f currentFov=%.2f eye=%d",
                native,result,fov[0]*57.29578f,fov[1]*57.29578f,edf6vr::NativeWorldProducerEye());
        }
    }
    return result;
}
bool InstallWorldDistance() noexcept {
    if(g_worldDistanceReady || !g_image.base) return false;
    // Exact supported executable and complete native getter contract.
    constexpr unsigned char getter[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x80,0x79,0x39,0x00};
    if(!edf6vr::Readable(g_image.base+0x118DCD0,sizeof(getter)) ||
       std::memcmp(g_image.base+0x118DCD0,getter,sizeof(getter))) return false;
    for(const auto site:kWorldDistanceSites) {
        if(!edf6vr::Readable(g_image.base+site,5) || g_image.base[site]!=0xE8) return false;
        std::int32_t relative=0;std::memcpy(&relative,g_image.base+site+1,4);
        if(g_image.base+site+5+relative!=g_image.base+0x118DCD0) return false;
    }
    g_worldDistance=reinterpret_cast<WorldDistance>(g_image.base+0x118DCD0);
    // If either installation fails, installed wrappers forward without changing
    // the result. Never enable a one-eye or Main-only distance correction.
    for(const auto site:kWorldDistanceSites) {
        bool changed=false;
        if(!edf6vr::RedirectCall(g_image.base+site,reinterpret_cast<void*>(g_worldDistance),
                               reinterpret_cast<void*>(&HookWorldDistance),changed)) return false;
    }
    g_worldDistanceReady=true;
    return true;
}
