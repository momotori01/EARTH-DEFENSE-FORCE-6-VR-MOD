// Native visibility integration: one consumer process per producer resolve.
// Only the independently validated native queue loop may repeat submission.
using UmbraResolve=void(__fastcall*)(void*);
using UmbraProcess=void(__fastcall*)(void*,void*);
using UmbraSet=void(__fastcall*)(void*,const void*);
UmbraResolve g_umbraResolve=nullptr;
UmbraProcess g_umbraProcess=nullptr;
UmbraSet g_umbraMatrix=nullptr,g_umbraFrustum=nullptr;
using UmbraCommand=void(__fastcall*)(void*,unsigned);
UmbraCommand g_umbraCommand=nullptr;
thread_local edf6vr::PerfBatch* g_umbraCommandBatch=nullptr;
thread_local bool g_umbraTraceMain=false;
struct UmbraTraceScope {
    bool previous=g_umbraTraceMain;
    explicit UmbraTraceScope(bool main) { g_umbraTraceMain=main; }
    ~UmbraTraceScope() { g_umbraTraceMain=previous; }
};
struct UmbraBatchScope {
    edf6vr::PerfBatch* previous=g_umbraCommandBatch;
    explicit UmbraBatchScope(edf6vr::PerfBatch& batch) { g_umbraCommandBatch=&batch; }
    ~UmbraBatchScope() { g_umbraCommandBatch=previous; }
};
SRWLOCK g_umbraProbeLock=SRWLOCK_INIT;
struct UmbraProbeCamera { void* camera=nullptr; unsigned seen=0; };
UmbraProbeCamera g_umbraCameras[16]{};

bool ClaimUmbraSample(void* camera,unsigned bit) noexcept {
    if(!camera || !g_nativeStereoProbeActive.load(std::memory_order_relaxed)) return false;
    bool sample=false;
    if(!TryAcquireSRWLockExclusive(&g_umbraProbeLock)) return false;
    for(auto& entry:g_umbraCameras) {
        if(entry.camera && entry.camera!=camera) continue;
        entry.camera=camera;
        sample=(entry.seen&bit)==0; entry.seen|=bit; break;
    }
    ReleaseSRWLockExclusive(&g_umbraProbeLock);
    return sample;
}
bool ReadUmbraProbeData(const void* source,void* output,std::size_t bytes) noexcept {
    __try {
        if(!edf6vr::Readable(source,bytes)) return false;
        std::memcpy(output,source,bytes); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The last camera matrix and frustum each Umbra camera was given (118DFF0 sets
// both inside every loop, before its resolves), for the scope's view to replace
// and put back. Producer thread, but locked: the setters are not ours to time.
struct UmbraFrustumRaw { float f[6]; unsigned tag; };
static_assert(sizeof(UmbraFrustumRaw)==28);
struct UmbraLatest { void* camera=nullptr; edf6vr::Matrix matrix{}; UmbraFrustumRaw frustum{}; bool haveMatrix=false,haveFrustum=false; };
SRWLOCK g_umbraLatestLock=SRWLOCK_INIT;
UmbraLatest g_umbraLatest[16]{};
unsigned g_umbraLatestNext=0;
void NoteUmbraLatest(void* camera,const edf6vr::Matrix* matrix,const UmbraFrustumRaw* frustum) noexcept {
    if(!camera) return;
    AcquireSRWLockExclusive(&g_umbraLatestLock);
    UmbraLatest* entry=nullptr;
    for(auto& e:g_umbraLatest) if(e.camera==camera) {entry=&e;break;}
    if(!entry) {entry=&g_umbraLatest[g_umbraLatestNext];g_umbraLatestNext=(g_umbraLatestNext+1)%16;*entry={};entry->camera=camera;}
    if(matrix) {entry->matrix=*matrix;entry->haveMatrix=true;}
    if(frustum) {entry->frustum=*frustum;entry->haveFrustum=true;}
    ReleaseSRWLockExclusive(&g_umbraLatestLock);
}
bool ReadUmbraLatest(void* camera,UmbraLatest& out) noexcept {
    AcquireSRWLockShared(&g_umbraLatestLock);
    bool found=false;
    for(const auto& e:g_umbraLatest) if(e.camera==camera) {out=e;found=true;break;}
    ReleaseSRWLockShared(&g_umbraLatestLock);
    return found && out.haveMatrix && out.haveFrustum;
}
// The scope's view (eye 2): its camera on the firing line and its narrow field,
// put on Main and Far for their resolves and taken off again -- the same
// pattern as the right eye's shift, plus the frustum. Culling, the projection
// the GPU is given and the distance scale all follow the resolve.
bool ScopeResolve(void* camera) noexcept {
    UmbraLatest latest{};
    const auto view=edf6vr::ReadScopeView();
    alignas(16) edf6vr::Matrix scope{};UmbraFrustumRaw narrow{};
    const bool haveLatest=view.active && view.mode==1 && ReadUmbraLatest(camera,latest);
    // Measured only: this frame's camera against the eye the origin was placed
    // against. On hardware (2026-10-01) it read a constant 0.033 m standing and
    // walking alike -- an offset of the camera this loop starts from, not a frame
    // lag -- so the origin is used as published.
    // On the weapon, the camera goes where the laser starts (ScopeLaserCarry).
    float origin[3]={view.origin[0],view.origin[1],view.origin[2]};
    if(view.kind!=edf6vr::ScopeHoloPanel) {
        float carry[3]{};
        if(ScopeLaserCarry(carry)) for(int j=0;j<3;++j) origin[j]+=carry[j];
        g_scopeWalked.store(std::sqrt(carry[0]*carry[0]+carry[1]*carry[1]+carry[2]*carry[2]),std::memory_order_relaxed);
    }
    if(haveLatest && view.eyeValid) {
        float skew=0;
        for(int j=0;j<3;++j) {const float d=latest.matrix.m[3][j]-view.eye[j];skew+=d*d;}
        skew=std::sqrt(skew);
        if(std::isfinite(skew) && skew<2.f) {
            g_scopeSkewLast.store(skew,std::memory_order_relaxed);
            if(skew>g_scopeSkewPeak.load(std::memory_order_relaxed)) g_scopeSkewPeak.store(skew,std::memory_order_relaxed);
            g_scopeFramed.fetch_add(1,std::memory_order_relaxed);
        } else g_scopeUnframed.fetch_add(1,std::memory_order_relaxed);
    }
    if(!haveLatest
       || !edf6vr::ScopeCamera(latest.matrix,origin,view.forward,scope)
       || !edf6vr::ScopeFrustum(latest.frustum.f,view.tanHalf,view.aspect,narrow.f)) {
        g_scopeResolveRefused.fetch_add(1,std::memory_order_relaxed);
        g_umbraResolve(camera);
        return false;
    }
    narrow.tag=latest.frustum.tag;
    g_umbraMatrix(camera,&scope);g_umbraFrustum(camera,&narrow);  // bypass the observers
    g_umbraResolve(camera);
    g_umbraFrustum(camera,&latest.frustum);g_umbraMatrix(camera,&latest.matrix); // restore, WITHOUT another resolve
    g_scopeResolves.fetch_add(1,std::memory_order_relaxed);
    return true;
}
void __fastcall ProbeUmbraResolve(void* camera) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-reinterpret_cast<std::uintptr_t>(g_image.base);
    const int eye=edf6vr::NativeWorldProducerEye();
    if(eye==2 && edf6vr::ClassifyNativeWorldResolve(caller)!=edf6vr::NativeWorldViewKind::Other) {ScopeResolve(camera);return;}
    alignas(16) edf6vr::Matrix original{},shifted{};
    const bool eyeView=eye>=0 && edf6vr::ClassifyNativeWorldResolve(caller)!=edf6vr::NativeWorldViewKind::Other
        && edf6vr::PrepareNativeWorldEye(camera,edf6vr::NativeWorldProducerFrame(),static_cast<unsigned>(eye),
            g_nativeProducerIpd,original,shifted);
    if(eyeView) g_umbraMatrix(camera,&shifted); // bypass original-matrix observer
    const bool enabled=g_nativeStereoProbeActive.load(std::memory_order_relaxed);
    if(ClaimUmbraSample(camera,1)) Log("UMBRA resolve thread=%lu camera=%p",GetCurrentThreadId(),camera);
    const auto start=edf6vr::PerfNow();
    g_umbraResolve(camera);
    if(eyeView) g_umbraMatrix(camera,&original); // restore, WITHOUT another resolve
    if(!enabled) return;
    edf6vr::PerfBatch batch{};
    batch.Add(edf6vr::PerfStage::NativeResolve,edf6vr::PerfNow()-start);
    edf6vr::SubmitPerf(batch);
}
void __fastcall ProbeUmbraProcess(void* camera,void* commander) {
    NoteRenderThread();
    const bool enabled=g_nativeStereoProbeActive.load(std::memory_order_relaxed);
    const int nativeEye=edf6vr::NativeWorldRenderEye();
    if(ClaimUmbraSample(camera,2)) {
        unsigned fields[2]{};
        const bool valid=commander && ReadUmbraProbeData(static_cast<unsigned char*>(commander)+0xE0,fields,sizeof(fields));
        Log("UMBRA process thread=%lu camera=%p commander=%p fieldsValid=%d E0=%u E4=%u",
            GetCurrentThreadId(),camera,commander,valid,fields[0],fields[1]);
    }
    if(!enabled && nativeEye<0) { g_umbraProcess(camera,commander); return; }
    unsigned mode=~0u;
    if(commander) ReadUmbraProbeData(static_cast<unsigned char*>(commander)+0xE4,&mode,sizeof(mode));
    // The order the four viewports of a loop arrive in, once. Whether the
    // shadows are drawn before or after the eye's own pass decides what may be
    // shared between the two loops, and guessing it from the totals cannot.
    static std::atomic<unsigned> sequence{0};
    if(nativeEye>=0) {
        const auto step=sequence.fetch_add(1,std::memory_order_relaxed);
        if(step<8) Log("UMBRAORDER step=%u eye=%d mode=%u",step,nativeEye,mode);
    }
    if(mode==0 && g_earlyXrFrame) edf6vr::g_openxr.PrepareSceneFrame();
    if(mode==0 && nativeEye<=0) edf6vr::BeginNativeRenderPose();
    // Match the baseline once; the right eye uses this exact HMD sample.
    UmbraTraceScope traceScope(mode==0 && nativeEye<=0);
    unsigned char refresh=0;
    if(commander) ReadUmbraProbeData(static_cast<unsigned char*>(commander)+0x183,&refresh,1);
    edf6vr::BeginNativeWorldView(mode,refresh!=0);
    edf6vr::PerfBatch batch{};
    UmbraBatchScope scope(batch);
    NativeCpuSample cpuSample(enabled && mode==0);
    // Never skip this call.
    //
    // 1.1.1-dev7 dropped it for the second eye's shadow viewports, whose result
    // is a provable repeat of the first eye's, and the game froze on the mission
    // loading screen: the update thread sat in 111CC20, the message pump and
    // Sleep(0) loop it waits in, and never came out. processVisibility is not an
    // optional producer of a picture -- it is what issues cmd0.begin and
    // cmd1.end for that viewport, so a viewport it is not called for never
    // completes and whatever waits on it waits forever. A repeat can only be
    // dropped where the whole viewport is, which is the renderer's loop, not
    // here. UMBRAORDER shows that loop as shadow, shadow, far, main.
    const auto start=edf6vr::PerfNow();
    g_umbraViewMode=mode<3?mode:3u;
    viewport_gpu::Begin(commander,nativeEye,g_umbraViewMode);
    g_shadowRepeat=g_shareShadows && nativeEye==1 && mode==2;
    g_umbraProcess(camera,commander);
    g_shadowRepeat=false;
    viewport_gpu::End(nativeEye,g_umbraViewMode);
    g_umbraViewMode=3;
    edf6vr::EndNativeWorldView();
    const auto elapsed=edf6vr::PerfNow()-start;
    cpuSample.Finish(elapsed);
    batch.Add(edf6vr::PerfStage::NativeProcess,elapsed);
    const auto group=mode==0?edf6vr::PerfStage::NativeMain:mode==1?edf6vr::PerfStage::NativeFar:
        mode==2?edf6vr::PerfStage::NativeShadow:edf6vr::PerfStage::NativeOther;
    batch.Add(group,elapsed); // subset of NativeProcess, not additional time
    edf6vr::SubmitPerf(batch);
}
void __fastcall ProbeUmbraCommand(void* commander,unsigned command) {
    // The right eye's shadow viewport (ShareEyeShadows): its object batches would
    // draw the left eye's maps again from the same camera.
    if(g_shadowRepeat && command==4) { g_shadowSkippedBatches.fetch_add(1,std::memory_order_relaxed); return; }
    if(!g_nativeStereoProbeActive.load(std::memory_order_relaxed)) { g_umbraCommand(commander,command); return; }
    const auto start=edf6vr::PerfNow();
    g_umbraCommand(commander,command);
    const auto stage=command<=9?static_cast<edf6vr::PerfStage>(
        static_cast<unsigned>(edf6vr::PerfStage::NativeCommand0)+command):edf6vr::PerfStage::NativeCommandOther;
    const auto elapsed=edf6vr::PerfNow()-start;
    if(g_umbraCommandBatch) g_umbraCommandBatch->Add(stage,elapsed);
    else {
        edf6vr::PerfBatch batch{}; batch.Add(stage,elapsed); edf6vr::SubmitPerf(batch);
    } // Nested subset of process; don't sum with its parent. One submit per process.
}
bool InstallUmbraCommandProbe() noexcept {
    // Runtime model stack and the current image's RTTI identify this exact callback.
    constexpr unsigned char bytes[]={0x83,0xFA,0x09,0x0F,0x87,0x24,0x22,0,0};
    void* table=g_image.base+0x1AE5288;
    if(!edf6vr::HasType(g_image,&table,".?AVCommander@umbra@xgs@@")
        || std::memcmp(g_image.base+0x11E2120,bytes,sizeof(bytes))) return false;
    auto slot=reinterpret_cast<void**>(g_image.base+0x1AE5290);
    if(*slot!=g_image.base+0x11E2120) return false;
    g_umbraCommand=reinterpret_cast<UmbraCommand>(*slot);
    bool changed=false;
    return edf6vr::ReplacePointer(slot,reinterpret_cast<void*>(g_umbraCommand),reinterpret_cast<void*>(&ProbeUmbraCommand),changed);
}
// Occlusion query results (Umbra's QUERY_RESULTS, command 6, 11E41F5..). For
// each query of the viewport 1631790(query, object, context, &pixels, wait)
// reads it with ID3D11DeviceContext::GetData -- no DONOTFLUSH flag -- and when
// `wait` (12D6194, Umbra's own word for that query) is set it spins on that
// call, under a time limit, until the GPU has the answer. In VR that is where
// the CPU stands waiting for the GPU: 8.5 ms of a 29.8 ms frame over about 630
// calls on the virtual headset (2026-10-09), all of it in the two main views.
// Measured here by kind and view, read only. Not waiting (objects vanish,
// 16 ms) or answering "visible" when not ready (48 ms) were tried and dropped
// (research/render/perf_experiments_20261009).
using QueryResult=bool(__fastcall*)(void*,void*,void*,void*,unsigned char);
QueryResult g_queryResult=nullptr;
// (the counters are declared at the top of plugin.cpp: the report reads them)
bool __fastcall HookQueryResult(void* query,void* object,void* context,void* pixels,unsigned char wait) {
    const auto start=edf6vr::PerfNow();
    const bool ready=g_queryResult(query,object,context,pixels,wait);
    const unsigned kind=wait?1u:0u;
    g_queryCalls[kind].fetch_add(1,std::memory_order_relaxed);
    if(ready) g_queryReady[kind].fetch_add(1,std::memory_order_relaxed);
    const auto spent=edf6vr::PerfNow()-start;
    g_queryTicks[kind].fetch_add(spent,std::memory_order_relaxed);
    const int eye=edf6vr::NativeWorldRenderEye();
    const unsigned e=eye==0?0u:eye==1?1u:2u, m=g_umbraViewMode<4?g_umbraViewMode:3u;
    g_queryViewTicks[e][m].fetch_add(spent,std::memory_order_relaxed);
    g_queryViewCalls[e][m].fetch_add(1,std::memory_order_relaxed);
    return ready;
}
// [Render] ShareEyeShadows (2026-10-09, default 1). Each eye's loop of the renderer (11978D0)
// is shadow, shadow, far, main, and ClassifyNativeWorldResolve moves only far
// and main per eye: the right eye's two shadow viewports draw the left eye's
// maps again, from the same camera, into the same maps -- ~1.5 ms of the render
// thread each and ~2.9 ms of the GPU a frame on the virtual headset
// (VIEWPORTGPU). Leaving the viewports out does not work: processVisibility
// issues cmd0/cmd1, which others wait on (1.1.1-dev7 froze), and leaving them
// out of the loop stopped that loop's far and main (2026-10-01). So they still
// run; only their depth clear (cmd0, 11E2215 -> 113D390 with DEPTH|STENCIL)
// and their object batches (cmd4) are left out, and the maps keep the left
// eye's picture for the right eye's lighting.
using ViewportClear=void(__fastcall*)(void*,unsigned,const void*,float,unsigned,unsigned);
ViewportClear g_viewportClear=nullptr;
void __fastcall HookViewportClear(void* context,unsigned colourIndex,const void* colour,float depth,unsigned stencil,unsigned flags) {
    if(g_shadowRepeat) { g_shadowSkippedClears.fetch_add(1,std::memory_order_relaxed); return; }
    g_viewportClear(context,colourIndex,colour,depth,stencil,flags);
}
bool InstallShadowShare() noexcept {
    g_viewportClear=reinterpret_cast<ViewportClear>(g_image.base+0x113D390);
    bool changed=false;
    return edf6vr::RedirectCall(g_image.base+0x11E2215,reinterpret_cast<void*>(g_viewportClear),reinterpret_cast<void*>(&HookViewportClear),changed);
}
bool InstallQueryResultProbe() noexcept {
    auto* site=g_image.base+0x11E4244;
    if(site[0]!=0xE8) return false;
    std::int32_t relative=0; std::memcpy(&relative,site+1,4);
    if(site+5+relative!=g_image.base+0x1631790) return false;
    g_queryResult=reinterpret_cast<QueryResult>(g_image.base+0x1631790);
    bool changed=false;
    return edf6vr::RedirectCall(site,reinterpret_cast<void*>(g_queryResult),reinterpret_cast<void*>(&HookQueryResult),changed);
}
void __fastcall ProbeUmbraMatrix(void* camera,const void* source) {
    edf6vr::Matrix original{};
    if(edf6vr::NativeWorldEnabled() && ReadUmbraProbeData(source,&original,sizeof(original))) {
        edf6vr::RecordNativeWorldCamera(camera,original);
        NoteUmbraLatest(camera,&original,nullptr);
        // 118DFF0 publishes camera matrices inside the first viewport loop.
        // Main/Far may then use an already queued resolve (+74==0), so latch
        // here as well. Never latch last frame's matrices at loop entry.
        if(edf6vr::NativeWorldProducerEye()==0) {
            edf6vr::Matrix baseline{},left{};
            edf6vr::PrepareNativeWorldEye(camera,edf6vr::NativeWorldProducerFrame(),0,
                g_nativeProducerIpd,baseline,left);
        }
    }
    if(ClaimUmbraSample(camera,4)) {
        float m[16]{};
        if(ReadUmbraProbeData(source,m,sizeof(m)))
            Log("UMBRA matrix thread=%lu camera=%p rows=[%.5f %.5f %.5f %.5f][%.5f %.5f %.5f %.5f][%.5f %.5f %.5f %.5f][%.5f %.5f %.5f %.5f]",
                GetCurrentThreadId(),camera,m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]);
    }
    g_umbraMatrix(camera,source);
}
void __fastcall ProbeUmbraFrustum(void* camera,const void* source) {
    {
        UmbraFrustumRaw raw{};
        if(edf6vr::NativeWorldEnabled() && ReadUmbraProbeData(source,&raw,sizeof(raw))) NoteUmbraLatest(camera,nullptr,&raw);
    }
    if(ClaimUmbraSample(camera,8)) {
        // EDF setter11D46B0 copies six floats and one 32-bit value (28 bytes).
        struct Frustum { float f[6]; unsigned tag; } frustum{};
        static_assert(sizeof(frustum)==28);
        if(ReadUmbraProbeData(source,&frustum,sizeof(frustum)))
            Log("UMBRA frustum thread=%lu camera=%p raw=[%.6f %.6f %.6f %.6f %.6f %.6f] tag=%u",
                GetCurrentThreadId(),camera,frustum.f[0],frustum.f[1],frustum.f[2],frustum.f[3],frustum.f[4],frustum.f[5],frustum.tag);
    }
    g_umbraFrustum(camera,source);
}
constexpr unsigned kUmbraProbeSlots[]={0x1756868,0x1756870,0x1756898,0x17568A8};
bool PatchUmbraProbe(void* const (&expected)[4]) noexcept {
    void* replacements[]={reinterpret_cast<void*>(&ProbeUmbraResolve),reinterpret_cast<void*>(&ProbeUmbraProcess),
        reinterpret_cast<void*>(&ProbeUmbraMatrix),reinterpret_cast<void*>(&ProbeUmbraFrustum)};
    // Preflight the whole set and refuse another plugin's IAT changes.
    for(unsigned i=0;i<4;++i)
        if(!expected[i] || *reinterpret_cast<void**>(g_image.base+kUmbraProbeSlots[i])!=expected[i]) return false;
    g_umbraResolve=reinterpret_cast<UmbraResolve>(expected[0]);
    g_umbraProcess=reinterpret_cast<UmbraProcess>(expected[1]);
    g_umbraMatrix=reinterpret_cast<UmbraSet>(expected[2]);
    g_umbraFrustum=reinterpret_cast<UmbraSet>(expected[3]);
    for(unsigned i=0;i<4;++i) {
        bool changed=false;
        if(edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+kUmbraProbeSlots[i]),expected[i],replacements[i],changed)) continue;
        for(unsigned j=0;j<i;++j) {
            bool restored=false;
            edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+kUmbraProbeSlots[j]),replacements[j],expected[j],restored);
        }
        return false;
    }
    return true;
}
bool InstallUmbraProbe() noexcept {
    const auto module=GetModuleHandleW(L"umbra_sandlot.dll");
    if(!module) return false;
    const char* names[]={"?resolveVisibility@Camera@Umbra@@QEBAXXZ",
        "?processVisibility@Camera@Umbra@@QEAAXPEAVCommander@2@@Z",
        "?setCameraToCellMatrix@Camera@Umbra@@QEAAXAEBVMatrix4x4@2@@Z",
        "?setFrustum@Camera@Umbra@@QEAAXAEBUFrustum@2@@Z"};
    void* expected[4]{};
    for(unsigned i=0;i<4;++i) expected[i]=reinterpret_cast<void*>(GetProcAddress(module,names[i]));
    return PatchUmbraProbe(expected);
}

// Read the matrices that the native VIEW_CHANGED command actually consumes,
// rather than assuming the latest logic-camera update is the rendered frame.
// Exact current EDF imports; wrappers forward once and only read returned data.
using UmbraGetView=void(__fastcall*)(const void*,void*);
using UmbraGetProjection=void(__fastcall*)(const void*,void*,int);
UmbraGetView g_umbraGetView=nullptr;
UmbraGetProjection g_umbraGetProjection=nullptr;
constexpr unsigned kUmbraTraceSlots[]={0x17568E8,0x17568E0};
void __fastcall TraceUmbraView(const void* viewer,void* output) {
    g_umbraGetView(viewer,output);
    edf6vr::Matrix matrix{};
    if(edf6vr::NativeWorldRenderEye()>=0 && ReadUmbraProbeData(output,&matrix,sizeof(matrix)))
        edf6vr::ObserveNativeWorldView(matrix);
    if(g_umbraTraceMain && (edf6vr::MotionTraceEnabled() || edf6vr::RenderedPoseEnabled())
       && ReadUmbraProbeData(output,&matrix,sizeof(matrix))) {
        edf6vr::TraceNativeMatrix(&matrix.m[0][0],false,0);
        edf6vr::ObserveNativeRenderView(matrix);
    }
}
void __fastcall TraceUmbraProjection(const void* viewer,void* output,int handedness) {
    g_umbraGetProjection(viewer,output,handedness);
    edf6vr::Matrix projection{};
    if(edf6vr::NativeWorldRenderEye()>=0 && ReadUmbraProbeData(output,&projection,sizeof(projection)))
        edf6vr::ObserveNativeWorldProjection(projection);
    float matrix[16]{};
    if(g_umbraTraceMain && edf6vr::MotionTraceEnabled() && ReadUmbraProbeData(output,matrix,sizeof(matrix)))
        edf6vr::TraceNativeMatrix(matrix,true,handedness);
}
bool PatchUmbraTrace(void* view,void* projection) noexcept {
    void* expected[]={view,projection};
    void* replacements[]={reinterpret_cast<void*>(&TraceUmbraView),reinterpret_cast<void*>(&TraceUmbraProjection)};
    for(unsigned i=0;i<2;++i)
        if(!expected[i] || *reinterpret_cast<void**>(g_image.base+kUmbraTraceSlots[i])!=expected[i]) return false;
    g_umbraGetView=reinterpret_cast<UmbraGetView>(view);
    g_umbraGetProjection=reinterpret_cast<UmbraGetProjection>(projection);
    for(unsigned i=0;i<2;++i) {
        bool changed=false;
        if(edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+kUmbraTraceSlots[i]),expected[i],replacements[i],changed)) continue;
        for(unsigned j=0;j<i;++j) {bool restored=false;edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+kUmbraTraceSlots[j]),replacements[j],expected[j],restored);}
        return false;
    }
    return true;
}
bool InstallUmbraTrace() noexcept {
    const auto module=GetModuleHandleW(L"umbra_sandlot.dll");if(!module) return false;
    return PatchUmbraTrace(
        reinterpret_cast<void*>(GetProcAddress(module,"?getCellToCameraMatrix@Viewer@Commander@Umbra@@QEBAXAEAVMatrix4x4@3@@Z")),
        reinterpret_cast<void*>(GetProcAddress(module,"?getProjectionMatrix@Viewer@Commander@Umbra@@QEBAXAEAVMatrix4x4@3@W4Handedness@123@@Z")));
}
