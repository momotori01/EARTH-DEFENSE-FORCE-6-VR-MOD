#include "../src/plugin.cpp"
#include <limits>
static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
static unsigned calls[4]{};
static void* expectedCamera=nullptr;
static void* expectedCommander=nullptr;
static const void* expectedMatrix=nullptr;
static const void* expectedFrustum=nullptr;
static bool nestedCommands=false;
static unsigned getViewCalls=0,getProjectionCalls=0;
static void __fastcall GetView(const void* v,void* output) {
    CHECK(v==expectedCamera);++getViewCalls;auto m=static_cast<float*>(output);
    for(unsigned i=0;i<16;++i) m[i]=float(i)+.25f;
}
static void __fastcall GetProjection(const void* v,void* output,int handedness) {
    CHECK(v==expectedCamera);CHECK(handedness==7);++getProjectionCalls;auto m=static_cast<float*>(output);
    for(unsigned i=0;i<16;++i) m[i]=float(i)+.75f;
}
static unsigned commandCalls=0,lastCommand=0;
static void __fastcall Command(void* c,unsigned command) {
    CHECK(c==expectedCommander); ++commandCalls; lastCommand=command;
    // A native side effect must survive the observing wrapper.
    *reinterpret_cast<unsigned*>(static_cast<unsigned char*>(c)+0x80)=command;
}
static void __fastcall Resolve(void* c) { CHECK(c==expectedCamera); ++calls[0]; }
static void __fastcall Process(void* c,void* v) {
    CHECK(c==expectedCamera && v==expectedCommander); ++calls[1];
    if(nestedCommands) {
        const auto callback=*reinterpret_cast<UmbraCommand*>(g_image.base+0x1AE5290);
        for(unsigned cmd=0;cmd<10;++cmd) callback(v,cmd);
        callback(v,~0u);
    }
}
static void __fastcall Matrix(void* c,const void* m) { CHECK(c==expectedCamera && m==expectedMatrix); ++calls[2]; }
static void __fastcall Frustum(void* c,const void* f) { CHECK(c==expectedCamera && f==expectedFrustum); ++calls[3]; }
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    const auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[256]{};
    if(!mapped || !edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason))) return 2;
    // Execute the actual isolated EDF getter with a synthetic camera. Its only
    // external call is tanf: resolve that import locally (no EDF game startup).
    {
        auto slot=reinterpret_cast<void**>(g_image.base+0x1756448);bool changed=false;
        auto old=*slot;
        CHECK(edf6vr::ReplacePointer(slot,old,reinterpret_cast<void*>(static_cast<float(*)(float)>(&std::tan)),changed));
        alignas(16) unsigned char view[0x40]{};
        auto ref=reinterpret_cast<float*>(view+0x20),fov=reinterpret_cast<float*>(view+0x24);
        *ref=45.f/57.29578f;*fov=106.94f/57.29578f;view[0x39]=1;
        auto getter=reinterpret_cast<WorldDistance>(g_image.base+0x118DCD0);
        const float ratio=getter(view);
        CHECK(std::fabs(ratio-std::tan(*fov/2)/std::tan(*ref/2))<.0001f && ratio>3.f);
        CHECK(PreserveWorldDistance(ratio,true)==1.f);
        CHECK(PreserveWorldDistance(ratio,false)==ratio);
        *fov=20.f/57.29578f;const float zoom=getter(view);
        CHECK(zoom>0 && zoom<1 && PreserveWorldDistance(zoom,true)==zoom);
        view[0x39]=0;CHECK(getter(view)==1.f);
        CHECK(std::isnan(PreserveWorldDistance(std::numeric_limits<float>::quiet_NaN(),true)));
        CHECK(PreserveWorldDistance(-2.f,true)==-2.f);
        CHECK(InstallWorldDistance());CHECK(!InstallWorldDistance());
        // Both call sites go through a valid rel32 thunk. Outside the native
        // stereo loop they forward the getter, even after installing the fix.
        view[0x39]=1;*fov=106.94f/57.29578f;
        for(auto site:kWorldDistanceSites) {
            std::int32_t delta=0;std::memcpy(&delta,g_image.base+site+1,4);
            auto hook=reinterpret_cast<WorldDistance>(g_image.base+site+5+delta);
            CHECK(hook(view)==getter(view));
        }
        CHECK(edf6vr::ReplacePointer(slot,*slot,old,changed));
    }
    {
        alignas(16) unsigned char weapon[0x1740]{};
        *reinterpret_cast<void**>(weapon)=g_image.base+0x17E3D50;
        *reinterpret_cast<int*>(weapon+0x1574)=90;
        *reinterpret_cast<int*>(weapon+0x1578)=100;
        *reinterpret_cast<void**>(weapon+0x1668)=reinterpret_cast<void*>(0x1234);
        unsigned char before[sizeof(weapon)];std::memcpy(before,weapon,sizeof(weapon));
        for(int n=0;n<100;++n)ObserveGatlingSound(0,weapon);
        CHECK(!std::memcmp(before,weapon,sizeof(weapon)));
        CHECK(g_gatlingContinuity[0].active==100 && g_gatlingContinuity[0].changed==0);
        *reinterpret_cast<void**>(weapon+0x1668)=nullptr;ObserveGatlingSound(0,weapon);
        CHECK(g_gatlingContinuity[0].missing==1 && g_gatlingContinuity[0].changed==1);
        *reinterpret_cast<int*>(weapon+0x1574)=0;ObserveGatlingSound(0,weapon);
        CHECK(g_gatlingContinuity[0].spinLost==1);
        ObserveGatlingSound(0,nullptr);CHECK(!g_gatlingContinuity[0].samples);
    }
    void* originals[]={reinterpret_cast<void*>(&Resolve),reinterpret_cast<void*>(&Process),
        reinterpret_cast<void*>(&Matrix),reinterpret_cast<void*>(&Frustum)};
    for(unsigned i=0;i<4;++i) {
        auto slot=reinterpret_cast<void**>(g_image.base+kUmbraProbeSlots[i]); bool changed=false;
        CHECK(edf6vr::ReplacePointer(slot,*slot,originals[i],changed));
    }
    void* conflict[]={originals[0],originals[0],originals[2],originals[3]};
    CHECK(!PatchUmbraProbe(conflict));
    for(unsigned i=0;i<4;++i) CHECK(*reinterpret_cast<void**>(g_image.base+kUmbraProbeSlots[i])==originals[i]);
    CHECK(PatchUmbraProbe(originals));
    unsigned char camera[16]{},commander[0xE8]{};
    alignas(16) float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,1};
    // At the end of a readable page: diagnostic must not assume Frustum is 32/64 bytes.
    auto page=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!page) return 2;
    DWORD old=0; CHECK(VirtualProtect(page+4096,4096,PAGE_NOACCESS,&old)!=0);
    auto frustum=page+4096-28; unsigned char kept[28]{};
    for(unsigned i=0;i<28;++i) kept[i]=frustum[i]=static_cast<unsigned char>(i);
    expectedCamera=camera; expectedCommander=commander; expectedMatrix=matrix; expectedFrustum=frustum;
    float matrixBefore[16]; memcpy(matrixBefore,matrix,sizeof(matrix));
    for(const bool active:{false,true}) {
        g_nativeStereoProbeActive=active;
        (*reinterpret_cast<UmbraResolve*>(g_image.base+kUmbraProbeSlots[0]))(camera);
        (*reinterpret_cast<UmbraProcess*>(g_image.base+kUmbraProbeSlots[1]))(camera,commander);
        (*reinterpret_cast<UmbraSet*>(g_image.base+kUmbraProbeSlots[2]))(camera,matrix);
        (*reinterpret_cast<UmbraSet*>(g_image.base+kUmbraProbeSlots[3]))(camera,frustum);
    }
    for(const auto count:calls) CHECK(count==2); // exactly once, no replay even when enabled
    CHECK(!memcmp(matrixBefore,matrix,sizeof(matrix)) && !memcmp(kept,frustum,28));
    const auto stats=edf6vr::DrainPerf();
    CHECK(stats.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::NativeResolve)].count==1);
    CHECK(stats.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::NativeProcess)].count==1);
    CHECK(InstallUmbraCommandProbe());
    CHECK(!InstallUmbraCommandProbe()); // conflict refusal retains the installed observer
    g_umbraCommand=Command; nestedCommands=true;
    for(unsigned mode=0;mode<3;++mode) {
        *reinterpret_cast<unsigned*>(commander+0xE4)=mode;
        (*reinterpret_cast<UmbraProcess*>(g_image.base+kUmbraProbeSlots[1]))(camera,commander);
        CHECK(!g_umbraCommandBatch && lastCommand==~0u);
        CHECK(*reinterpret_cast<unsigned*>(commander+0x80)==~0u);
    }
    g_nativeStereoProbeActive=false;
    (*reinterpret_cast<UmbraProcess*>(g_image.base+kUmbraProbeSlots[1]))(camera,commander);
    CHECK(commandCalls==44); // 11 commands per call, also exactly once when probe OFF
    const auto nested=edf6vr::DrainPerf();
    for(const auto stage:{edf6vr::PerfStage::NativeMain,edf6vr::PerfStage::NativeFar,edf6vr::PerfStage::NativeShadow})
        CHECK(nested.batch.stages[static_cast<std::size_t>(stage)].count==1);
    for(unsigned cmd=0;cmd<11;++cmd)
        CHECK(nested.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::NativeCommand0)+cmd].count==3);
    CHECK(nested.batch.stages[static_cast<std::size_t>(edf6vr::PerfStage::NativeProcess)].count==3);
    void* getterOriginals[]={reinterpret_cast<void*>(&GetView),reinterpret_cast<void*>(&GetProjection)};
    for(unsigned i=0;i<2;++i) {auto slot=reinterpret_cast<void**>(g_image.base+kUmbraTraceSlots[i]);bool changed=false;CHECK(edf6vr::ReplacePointer(slot,*slot,getterOriginals[i],changed));}
    CHECK(!PatchUmbraTrace(getterOriginals[1],getterOriginals[0]));
    CHECK(PatchUmbraTrace(getterOriginals[0],getterOriginals[1]));
    CHECK(!PatchUmbraTrace(getterOriginals[0],getterOriginals[1]));
    // Exact-sized output ending at an unreadable page. Preserve native output and call count.
    auto output=reinterpret_cast<float*>(page+4096-64);
    edf6vr::ConfigureRenderedPose(true);
    for(bool main:{false,true}) {
        edf6vr::BeginNativeRenderPose();
        UmbraTraceScope scope(main);
        (*reinterpret_cast<UmbraGetView*>(g_image.base+kUmbraTraceSlots[0]))(camera,output);
        for(unsigned i=0;i<16;++i) CHECK(output[i]==float(i)+.25f);
        (*reinterpret_cast<UmbraGetProjection*>(g_image.base+kUmbraTraceSlots[1]))(camera,output,7);
        for(unsigned i=0;i<16;++i) CHECK(output[i]==float(i)+.75f);
    }
    CHECK(getViewCalls==2 && getProjectionCalls==2 && !g_umbraTraceMain);
    edf6vr::HmdSample ignored{};CHECK(!edf6vr::ConsumeNativeRenderPose(ignored)); // invalid native matrix, fail closed
    edf6vr::ConfigureRenderedPose(false);
    VirtualFree(page,0,MEM_RELEASE); FreeLibrary(mapped);
    printf("Native renderer probe: %d failures, synthetic callees, no EDF/Umbra execution\n",failures);
    return failures?1:0;
}
