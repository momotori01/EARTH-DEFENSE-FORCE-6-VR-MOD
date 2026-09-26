// Included in plugin.cpp's private namespace. Read-only, bounded firing trace.
// AfterUpdate already owns g_lock; never acquire it again here.
struct PresentationTraceWindow {
    ULONGLONG until=0,next=0;
    unsigned shots=0,lines=0;
    bool trigger=false;
    bool Tick(ULONGLONG now,bool active,bool pressed,unsigned initialized) noexcept {
        const bool event=(pressed && !trigger) || initialized!=shots;
        trigger=pressed;shots=initialized;
        if(!active) {until=0;return false;}
        if(event) until=now+6000;
        if(!until || now>=until || now<next || lines>=600) return false;
        next=now+100;++lines;return true;
    }
};
PresentationTraceWindow g_presentationTrace;
std::atomic<ULONGLONG> g_heldDrawStamp{0},g_heldPreparedStamp{0};
std::atomic<unsigned long long> g_heldDrawTraceCount{0},g_heldRejectTraceCount{0};
std::atomic<unsigned> g_heldPassTraceMask{0};

struct PresentationModelState {
    int flags=-1,enabled=-1,activity=-1;
};
PresentationModelState ReadPresentationModel(void* model) noexcept {
    PresentationModelState out{};
    __try {
        if(!edf6vr::Readable(model,0x4D2)
           || !edf6vr::HasType(g_image,model,".?AVAnimationModel@@")) return out;
        const auto bytes=static_cast<const unsigned char*>(model);
        out.flags=bytes[0x31C];out.enabled=bytes[0x480];out.activity=bytes[0x4D1];
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
    return out;
}
void TraceFiringPresentation(void* camera,void* soldier,bool fpsApplied,bool nativeLive,
                             const edf6vr::Matrix& before,const edf6vr::Matrix& after) noexcept {
    if(!g_firingPresentationTrace) return;
    unsigned shots=0;
    AcquireSRWLockShared(&g_shotReportLock);shots=g_shotReport.initialized;ReleaseSRWLockShared(&g_shotReportLock);
    const auto now=GetTickCount64();
    if(!g_presentationTrace.Tick(now,g_vrEnabled && !g_faulted,g_lastTrigger>.5f,shots)) return;
    auto age=[now](ULONGLONG stamp){return stamp && stamp<=now?now-stamp:~0ull;};
    const auto body=ReadPresentationModel(g_bodyTarget.pose.body);
    const auto weapon=ReadPresentationModel(g_holdCommand.model);
    int state=0,result=0,layers=0,kind=0,fov=0,images=0;
    unsigned long long failures=0,notRendering=0;
    edf6vr::g_openxr.ReadSubmission(state,result,failures,notRendering,layers,kind,fov,images);
    Log("FIREVIEW n=%u shots=%u trigger=%.2f fps=%d native=%d tactical=%d vehicle=%d "
        "sceneAge=%llu inputAge=%llu bodyAge=%llu heldAge=%llu preparedAge=%llu "
        "draws=%llu rejected=%llu passes=%X xrKind=%d layers=%d images=%d "
        "camera=%p soldier=%p held=%p selected=%p candidate=%p slot=%u debounce=%d "
        "commandAge=%llu bodyFlags=%X/%d/%d weaponFlags=%X/%d/%d "
        "nativeEye=(%.2f,%.2f,%.2f) vrEye=(%.2f,%.2f,%.2f)",
        g_presentationTrace.lines,shots,g_lastTrigger,fpsApplied,nativeLive,g_nativeTactical.load(),g_vehicleMounted.load(),
        edf6vr::g_openxr.SceneAgeMs(),age(g_soldierInput),age(g_bodyDraw),
        age(g_heldDrawStamp.load()),age(g_heldPreparedStamp.load()),
        g_heldDrawTraceCount.load(),g_heldRejectTraceCount.load(),g_heldPassTraceMask.exchange(0),kind,layers,images,
        camera,soldier,g_holdCommand.model,g_weaponModelSeen,g_nearestCandidate,g_heldSlot,g_nearestHeld,
        age(g_holdCommand.refreshed),body.flags,body.enabled,body.activity,weapon.flags,weapon.enabled,weapon.activity,
        before.m[3][0],before.m[3][1],before.m[3][2],after.m[3][0],after.m[3][1],after.m[3][2]);
    // Compare every carried slot with the hand-distance selector. A special
    // firing pose may make a holstered weapon nearer than the active one.
    __try {
        if(!edf6vr::IsSupportedSoldier(g_image,soldier)) return;
        for(unsigned slot=0;slot<edf6vr::WeaponSlotCount(soldier);++slot) {
            edf6vr::WeaponPose held{};
            if(!edf6vr::ReadWeaponPose(soldier,slot,held)) continue;
            const auto model=ReadPresentationModel(held.model);
            int visibility=-1;
            if(edf6vr::Readable(held.weapon,0x78D)) visibility=static_cast<unsigned char*>(held.weapon)[0x78C];
            Log("FIREVIEW_SLOT n=%u slot=%u weapon=%p type=%s model=%p visibility78C=%d flags=%X/%d/%d pos=(%.2f,%.2f,%.2f)",
                g_presentationTrace.lines,slot,held.weapon,edf6vr::TypeName(g_image,held.weapon),held.model,
                visibility,model.flags,model.enabled,model.activity,held.world.m[3][0],held.world.m[3][1],held.world.m[3][2]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
