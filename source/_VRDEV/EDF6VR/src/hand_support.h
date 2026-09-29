// Included inside plugin.cpp's private namespace after casing_origin.h.
//
// The player's hands at the controllers, cut from the soldier's own body mesh.
// The body draw is normally withheld whole; with hands on, the passes named by
// HandPassMask run the game's own draw with the hand bones of the render
// palette placed on the controllers and, through HandDrawScope, only the hand
// triangles submitted. The palette is put back as soon as the draw returns.
std::atomic<unsigned long long> g_handBodyDraws{0},g_handLayerDraws{0},g_handRefused{0};
std::atomic<unsigned> g_handPassSeen{0};

constexpr unsigned kHandNodeMax=160;
struct HandRigState {
    void* soldier=nullptr; void* nodes=nullptr; void* resource=nullptr;
    bool ready=false,tried=false;
    unsigned nodeCount=0;
    // A name lookup that fails on the first frames of a new soldier -- the Air
    // Raider did, once -- is asked again a little later rather than written off.
    ULONGLONG retryAt=0; unsigned attempts=0;
    edf6vr::HandRig rig[2]{};
    char source[24]{};
};
HandRigState g_handRigState{};   // draw thread only

void ReadHandSettings() noexcept {
    if(!g_iniPath[0]) return;
    const bool on=GetPrivateProfileIntW(L"VR",L"HandModels",g_handModels,g_iniPath)!=0;
    if(on!=g_handModels) Log("HANDMODEL %s (live INI)",on?"on":"off");
    g_handModels=on;
    g_handPassMask=static_cast<unsigned>(GetPrivateProfileIntW(L"VR",L"HandPassMask",static_cast<int>(g_handPassMask),g_iniPath));
    g_handRelaxed=ReadFloat(g_iniPath,L"HandRelaxed",g_handRelaxed,0.0f,0.9f,L"VR");
    g_handCurlInvert=GetPrivateProfileIntW(L"VR",L"HandCurlInvert",g_handCurlInvert,g_iniPath)!=0;
    // While the adjust gesture is moving the hand, the file still holds the old
    // numbers; reading them back would undo every nudge within seconds.
    if(g_handAdjust && g_adjust==Adjusting::Active) return;
    // [VR] first, then the section for the controller in use on top of it.
    const edf6vr::HandTrim defaults{};
    edf6vr::HandTrim trim{};
    wchar_t section[48]=L"VR";
    const bool known=HandSection(section,48);
    auto read=[&](const wchar_t* key,float fallback,float low,float high) {
        const float base=ReadFloat(g_iniPath,key,fallback,low,high,L"VR");
        return known?ReadFloat(g_iniPath,key,base,low,high,section):base;
    };
    trim.right=read(L"HandRightMetres",defaults.right,-0.3f,0.3f);
    trim.up=read(L"HandUpMetres",defaults.up,-0.3f,0.3f);
    trim.ahead=read(L"HandAheadMetres",defaults.ahead,-0.3f,0.3f);
    trim.yawDegrees=read(L"HandYawDegrees",defaults.yawDegrees,-180.0f,180.0f);
    trim.pitchDegrees=read(L"HandPitchDegrees",defaults.pitchDegrees,-180.0f,180.0f);
    trim.rollDegrees=read(L"HandRollDegrees",defaults.rollDegrees,-180.0f,180.0f);
    trim.palmInvert=GetPrivateProfileIntW(L"VR",L"HandPalmInvert",g_handTrim.palmInvert,g_iniPath)!=0;
    if(std::memcmp(&trim,&g_handTrim,sizeof(trim)))
        Log("HANDMODEL trim [%ls] right=%.3f up=%.3f ahead=%.3f yaw=%.1f pitch=%.1f roll=%.1f palmInvert=%d relaxed=%.2f curlInvert=%d",section,
            trim.right,trim.up,trim.ahead,trim.yawDegrees,trim.pitchDegrees,trim.rollDegrees,trim.palmInvert,g_handRelaxed,g_handCurlInvert);
    g_handTrim=trim;
}

// The skeleton as the model file describes it. The registry's first pointer is
// the loaded resource; when it is the MDB0 image itself, the bind pose, parents
// and names are all there. Otherwise the parents are recovered from the posed
// nodes and the game's current local matrices stand in for the bind pose.
struct SkeletonScratch {
    char names[kHandNodeMax][32]{}; const char* namePointers[kHandNodeMax]{};
    int parents[kHandNodeMax]{}; edf6vr::Matrix rest[kHandNodeMax]{};
    edf6vr::Matrix local[kHandNodeMax]{},world[kHandNodeMax]{};
};
SkeletonScratch g_handScratch{};

bool ReadSkeletonFromMdb(const unsigned char* resource,unsigned nodeCount,SkeletonScratch& out) noexcept {
    __try {
        if(!edf6vr::Readable(resource,48) || std::memcmp(resource,"MDB0",4)) return false;
        const auto nameCount=*reinterpret_cast<const std::uint32_t*>(resource+8);
        const auto nameOffset=*reinterpret_cast<const std::uint32_t*>(resource+12);
        const auto boneCount=*reinterpret_cast<const std::uint32_t*>(resource+16);
        const auto boneOffset=*reinterpret_cast<const std::uint32_t*>(resource+20);
        if(boneCount!=nodeCount || boneCount>kHandNodeMax || nameCount>4096) return false;
        if(!edf6vr::Readable(resource+boneOffset,static_cast<std::size_t>(boneCount)*192)) return false;
        if(!edf6vr::Readable(resource+nameOffset,static_cast<std::size_t>(nameCount)*4)) return false;
        for(unsigned i=0;i<boneCount;++i) {
            const auto record=resource+boneOffset+i*192;
            const auto parent=*reinterpret_cast<const std::int32_t*>(record+4);
            const auto nameIndex=*reinterpret_cast<const std::uint32_t*>(record+16);
            if(parent<-1 || parent>=static_cast<int>(boneCount) || nameIndex>=nameCount) return false;
            out.parents[i]=parent;
            const auto slot=resource+nameOffset+nameIndex*4;
            const auto text=reinterpret_cast<const wchar_t*>(slot+*reinterpret_cast<const std::uint32_t*>(slot));
            if(!edf6vr::Readable(text,64)) return false;
            unsigned n=0;
            for(;n<31 && text[n];++n) out.names[i][n]=text[n]<128?static_cast<char>(text[n]):'?';
            out.names[i][n]=0;
            out.namePointers[i]=out.names[i];
            std::memcpy(&out.rest[i],record+32,sizeof(edf6vr::Matrix));
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadSkeletonFromNodes(unsigned char* soldier,unsigned char* nodes,unsigned nodeCount,SkeletonScratch& out) noexcept {
    __try {
        if(nodeCount>kHandNodeMax || !edf6vr::Readable(nodes,static_cast<std::size_t>(nodeCount)*0x110)) return false;
        for(unsigned i=0;i<nodeCount;++i) {
            std::memcpy(&out.local[i],nodes+i*0x110+0x70,sizeof(edf6vr::Matrix));
            std::memcpy(&out.world[i],nodes+i*0x110+0xB0,sizeof(edf6vr::Matrix));
            // The whole name, not just its first byte: this scratch is reused for
            // every soldier, and a longer name left by the previous one showed
            // through the end of a shorter one written here.
            out.rest[i]=out.local[i]; std::memset(out.names[i],0,sizeof(out.names[i])); out.namePointers[i]=out.names[i];
        }
        edf6vr::InferParents(out.local,out.world,nodeCount,out.parents);
        const wchar_t* stems[]={L"te",L"kawan0",L"kawan2",L"fing0",L"fing1",L"index0",L"index1",L"index2",L"middle0",L"middle1",L"middle2",
                                L"ring0",L"ring1",L"ring2",L"little0",L"little1",L"little2",L"thumb0",L"thumb1",L"thumb2"};
        for(const wchar_t* side:{L"l",L"r"}) for(const auto* stem:stems) {
            wchar_t name[32]{}; std::swprintf(name,32,L"%ls_%ls",stem,side);
            auto node=static_cast<unsigned char*>(edf6vr::FindNamedBone(soldier,g_nodeLookup,name));
            if(!node || node<nodes || (node-nodes)%0x110) continue;
            const auto index=static_cast<unsigned>((node-nodes)/0x110);
            if(index>=nodeCount) continue;
            unsigned n=0;
            for(;n<31 && name[n];++n) out.names[index][n]=static_cast<char>(name[n]);
            out.names[index][n]=0;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The bind pose of the hand chain, from the shipped models. The game's nodes
// carry only the animated pose, in which the fingers already grip a weapon, so
// the finger directions and the palm read from them were wrong. A bone's local
// translation does not animate, so it says which class's table this skeleton
// is: the table whose translations match is applied, parents and all.
const edf6vr::HandRestTable* ApplyHandRestTable(SkeletonScratch& scratch,unsigned nodeCount,float& fit) noexcept {
    auto indexOf=[&](const char* name) {
        for(unsigned i=0;i<nodeCount;++i) if(scratch.names[i][0] && !std::strcmp(scratch.names[i],name)) return static_cast<int>(i);
        return -1;
    };
    const edf6vr::HandRestTable* best=nullptr; float bestError=1e30f;
    for(const auto& table:edf6vr::kHandRestTables) {
        float error=0; unsigned matched=0;
        for(unsigned b=0;b<table.count;++b) {
            const int node=indexOf(table.bones[b].name);
            if(node<0) { matched=0; break; }
            for(int j=0;j<3;++j) error+=std::fabs(scratch.local[node].m[3][j]-table.bones[b].m[12+j]);
            ++matched;
        }
        if(matched==table.count && error/matched<bestError) { bestError=error/matched; best=&table; }
    }
    fit=bestError;
    if(!best || bestError>0.004f) return nullptr;
    for(unsigned b=0;b<best->count;++b) {
        const int node=indexOf(best->bones[b].name),parent=indexOf(best->bones[b].parent);
        if(node<0 || parent<0) return nullptr;
        std::memcpy(&scratch.rest[node],best->bones[b].m,sizeof(edf6vr::Matrix));
        scratch.parents[node]=parent;
    }
    return best;
}

bool EnsureHandRig(unsigned char* soldier) noexcept {
    auto& state=g_handRigState;
    unsigned char* nodes=nullptr; unsigned count=0; unsigned char* resource=nullptr;
    __try {
        const auto registry=soldier+edf6vr::kSkeletonOffset;
        if(!edf6vr::Readable(registry,0x38)) return false;
        resource=*reinterpret_cast<unsigned char**>(registry);
        nodes=*reinterpret_cast<unsigned char**>(registry+0x10);
        const auto n=*reinterpret_cast<std::int64_t*>(registry+0x20);
        if(!nodes || n<=0 || n>static_cast<std::int64_t>(kHandNodeMax)) return false;
        count=static_cast<unsigned>(n);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    const auto now=GetTickCount64();
    if(state.soldier==soldier && state.nodes==nodes && state.resource==resource && state.nodeCount==count) {
        if(state.ready || state.attempts>=10 || now<state.retryAt) return state.ready;
    } else state=HandRigState{};
    state.soldier=soldier; state.nodes=nodes; state.resource=resource; state.nodeCount=count; state.tried=true;
    ++state.attempts; state.retryAt=now+2000;
    auto& scratch=g_handScratch;
    bool have=false;
    if(ReadSkeletonFromMdb(resource,count,scratch)) { have=true; std::snprintf(state.source,sizeof(state.source),"mdb0"); }
    else if(ReadSkeletonFromNodes(soldier,nodes,count,scratch)) {
        have=true; float fit=0;
        const auto* table=ApplyHandRestTable(scratch,count,fit);
        std::snprintf(state.source,sizeof(state.source),table?"table:%s":"nodes",table?table->label:"");
        Log("HANDMODEL bind pose %s (translation fit %.1f mm)",table?table->label:"not matched; animated pose used",fit*1000);
    }
    if(!have) { Log("HANDMODEL skeleton unreadable (%u nodes); hands off for this soldier",count); return false; }
    const edf6vr::SkeletonView view{count,scratch.namePointers,scratch.parents,scratch.rest};
    const bool left=edf6vr::BuildHandRig(view,true,state.rig[0]);
    const bool right=edf6vr::BuildHandRig(view,false,state.rig[1]);
    if(!left || !right) {
        // Which names the skeleton did not answer to, so the next run can say
        // whether this class names its bones differently.
        char missing[512]{}; std::size_t used=0;
        const char* stems[]={"te","kawan0","kawan2","fing0","fing1","index0","index1","index2","middle0","middle1","middle2",
                             "ring0","ring1","ring2","little0","little1","little2","thumb0","thumb1","thumb2"};
        for(const char* side:{"l","r"}) for(const auto* stem:stems) {
            char name[32]{}; std::snprintf(name,sizeof(name),"%s_%s",stem,side);
            bool found=false;
            for(unsigned i=0;i<count && !found;++i) found=scratch.names[i][0] && !std::strcmp(scratch.names[i],name);
            if(!found && used<sizeof(missing)-40) used+=std::snprintf(missing+used,sizeof(missing)-used,"%s ",name);
        }
        Log("HANDMODEL rig incomplete (source=%s nodes=%u left=%d right=%d) missing: %s; hands off for this soldier",
            state.source,count,left,right,missing[0]?missing:"none (parent chain broken)");
        return false;
    }
    // The triangles picked are those that touch these. kawan2 is deliberately
    // not among them: the cuff ring touches it, but so does the whole forearm,
    // whose other bones stay where the game has the arm -- with kawan2 in the
    // set the sleeve was drawn stretched from the hand to there. The band just
    // past the cuff that a touching triangle brings is folded onto the wrist
    // instead (CollapseTo), which closes the cuff.
    // Selection and posing are separate: the cuff remains posed, and private
    // hand vertices discard all remaining body weights after selection.
    int bones[2][edf6vr::kHandBoneMax+1]{}; unsigned boneCount[2]={0,0};
    for(int h=0;h<2;++h)
        for(unsigned b=0;b<state.rig[h].count;++b) bones[h][boneCount[h]++]=state.rig[h].bones[b].node;
    edf6vr::SetHandBones(bones[0],boneCount[0],bones[1],boneCount[1],count,state.rig[0].kawan2,state.rig[1].kawan2);
    state.ready=true;
    const auto& r=state.rig[1];
    Log("HANDMODEL rig ready source=%s nodes=%u bones/hand=%u wrist=%d kawan2=%d palm=%.3fm fingers=(%.2f,%.2f,%.2f) palmOut=(%.2f,%.2f,%.2f)",
        state.source,count,r.count,r.bones[0].node,r.kawan2,r.palmLength,r.fingers[0],r.fingers[1],r.fingers[2],r.palmOut[0],r.palmOut[1],r.palmOut[2]);
    return true;
}

// ResolveHoldPalette's reading of the model, without the weapon's bone cap.
bool ResolveBodyPalette(void* model,int pass,edf6vr::Matrix*& palette,std::size_t& count) noexcept {
    __try {
        auto bytes=static_cast<unsigned char*>(model);
        if(pass<0 || pass>5 || pass==4 || !edf6vr::Readable(bytes,0x483)) return false;
        auto registry=bytes+0xA0;
        if(pass!=2 || !bytes[0x482]) {
            auto descriptor=bytes+0x160;
            const auto lodCount=*reinterpret_cast<std::uint64_t*>(bytes+0x260);
            if(lodCount>64) return false;
            auto lods=*reinterpret_cast<unsigned char**>(bytes+0x250);
            if(lodCount) {
                const auto index=bytes[0x45];
                if(index>7 || !edf6vr::Readable(lods,static_cast<std::size_t>(lodCount)*0xF0)) return false;
                const float distance=*reinterpret_cast<float*>(bytes+0x24+index*4);
                if(!std::isfinite(distance)) return false;
                for(std::size_t i=0;i<lodCount;++i) {
                    const float threshold=*reinterpret_cast<float*>(lods+i*0xF0+0xE8);
                    if(!std::isfinite(threshold)) return false;
                    if(threshold>distance*distance) break;
                    descriptor=lods+i*0xF0;
                }
            }
            registry=*reinterpret_cast<unsigned char**>(descriptor+0x58);
        }
        if(!edf6vr::Readable(registry,0x38)) return false;
        const auto n=*reinterpret_cast<std::int32_t*>(registry+0x20);
        if(n<=0 || n>static_cast<int>(kHandNodeMax)) return false;
        palette=*reinterpret_cast<edf6vr::Matrix**>(registry+0x30);
        count=static_cast<std::size_t>(n);
        return edf6vr::Readable(palette,count*sizeof(*palette),true);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The controller's grip pose in the soldier's frame, measured against the same
// head sample and eye the weapon is, so hand and weapon agree.
bool HandControllerFrame(int hand,float axes[3][3],float palm[3]) noexcept {
    float pos[3]{},rot[4]{};
    if(!edf6vr::g_openxr.GripPosePhysical(hand,pos,rot)) return false;   // the hand model on its own controller
    float eye[3]{},headRoom[3]{},yaw=0;
    AcquireSRWLockShared(&g_lock);
    const auto now=GetTickCount64();
    if(g_holdCommand.tracked && now>=g_holdCommand.refreshed && now-g_holdCommand.refreshed<250) {
        std::memcpy(eye,g_holdCommand.eyeWorld,sizeof(eye)); std::memcpy(headRoom,g_holdCommand.headRoom,sizeof(headRoom)); yaw=g_holdCommand.yawOffset;
    } else {
        std::memcpy(eye,g_eyeWorld,sizeof(eye)); headRoom[0]=g_lastHeadXr.x; headRoom[1]=g_lastHeadXr.y; headRoom[2]=g_lastHeadXr.z; yaw=g_yawOffset;
    }
    ReleaseSRWLockShared(&g_lock);
    // The stick turn of the camera being drawn, as for the weapon (RetimeHoldYaw).
    float rendered=0;
    if(edf6vr::NativeRenderYaw(rendered)) {
        const float delta=std::remainder(rendered-yaw,6.28318530718f);
        if(std::isfinite(delta) && std::fabs(delta)<=0.8f) yaw=rendered;
    }
    const edf6vr::Quat q{rot[0],rot[1],rot[2],rot[3]};
    if(!edf6vr::NormalizedQuat(q)) return false;
    auto intoSoldier=[&](const edf6vr::Vec3& v) { return edf6vr::RotateY(edf6vr::XrToGame(edf6vr::QuatRotate(q,v)),yaw); };
    const edf6vr::Vec3 right=intoSoldier({1,0,0}),up=intoSoldier({0,1,0}),ahead=intoSoldier({0,0,-1});
    const edf6vr::Vec3 room=edf6vr::XrToGame({pos[0],pos[1],pos[2]});
    const edf6vr::Vec3 moved=edf6vr::RotateY({room.x-headRoom[0],room.y-headRoom[1],room.z-headRoom[2]},yaw);
    const float rows[3][3]={{right.x,right.y,right.z},{up.x,up.y,up.z},{ahead.x,ahead.y,ahead.z}};
    std::memcpy(axes,rows,sizeof(rows));
    palm[0]=eye[0]+moved.x; palm[1]=eye[1]+moved.y; palm[2]=eye[2]+moved.z;
    for(int i=0;i<3;++i) { for(float v:axes[i]) if(!std::isfinite(v)) return false; if(!std::isfinite(palm[i])) return false; }
    // The kick is added by KickHands, once both hands are known.
    return true;
}

// The hand bones of the palette placed on the controllers; what they held is
// kept so it can be put back. No destructors here: this is the guarded part.
bool PoseHandsIntoPalette(edf6vr::Matrix* palette,std::size_t count,const bool have[2],const float axes[2][3][3],
                          const float palm[2][3],const edf6vr::ControllerState& controls,
                          edf6vr::Matrix kept[2][edf6vr::kHandBoneMax+2],unsigned touched[2],bool drawn[2]) noexcept {
    bool posed=false;
    int collapse[2]={-1,-1};
    edf6vr::ReadHandCollapseSlots(collapse);
    __try {
        for(int h=0;h<2;++h) {
            drawn[h]=false;
            if(!have[h]) continue;
            const auto& rig=g_handRigState.rig[h];
            for(unsigned b=0;b<rig.count;++b) if(static_cast<std::size_t>(rig.bones[b].node)<count) kept[h][touched[h]++]=palette[rig.bones[b].node];
            if(rig.kawan2>=0 && static_cast<std::size_t>(rig.kawan2)<count) kept[h][touched[h]++]=palette[rig.kawan2];
            if(collapse[h]>=0 && static_cast<std::size_t>(collapse[h])<count) kept[h][touched[h]++]=palette[collapse[h]];
            edf6vr::Matrix wrist{};
            if(!edf6vr::WristFromController(rig,axes[h],palm[h],g_handTrim,wrist)) continue;
            // Capacitive touch where the controller reports it; a stick that is
            // pushed or pressed has a thumb on it either way.
            const float stickMoved=std::fabs(controls.stick[h][0])+std::fabs(controls.stick[h][1]);
            const float thumb=(controls.stickTouch[h] || controls.stickClick[h] || stickMoved>0.15f)?1.0f:0.0f;
            const auto curl=edf6vr::CurlFromControls(controls.trigger[h],controls.squeeze[h],thumb,g_handRelaxed);
            edf6vr::PoseHand(rig,wrist,curl,g_handCurlInvert,palette,count);
            // What lies past the cuff folds onto the wrist (hand_model.h, CollapseTo).
            if(collapse[h]>=0 && static_cast<std::size_t>(collapse[h])<count) palette[collapse[h]]=edf6vr::CollapseTo(wrist);
            drawn[h]=true; posed=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { posed=false; }
    return posed;
}
void RestoreHandPalette(edf6vr::Matrix* palette,std::size_t count,const edf6vr::Matrix kept[2][edf6vr::kHandBoneMax+2],const unsigned touched[2]) noexcept {
    int collapse[2]={-1,-1};
    edf6vr::ReadHandCollapseSlots(collapse);
    __try {
        for(int h=0;h<2;++h) {
            const auto& rig=g_handRigState.rig[h]; unsigned k=0;
            for(unsigned b=0;b<rig.count && k<touched[h];++b) if(static_cast<std::size_t>(rig.bones[b].node)<count) palette[rig.bones[b].node]=kept[h][k++];
            if(rig.kawan2>=0 && static_cast<std::size_t>(rig.kawan2)<count && k<touched[h]) palette[rig.kawan2]=kept[h][k++];
            if(collapse[h]>=0 && static_cast<std::size_t>(collapse[h])<count && k<touched[h]) palette[collapse[h]]=kept[h][k++];
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
bool CallBodyDrawGuarded(void* model,void* renderContext,int pass,void* view) noexcept {
    __try { g_modelOriginal(model,renderContext,pass,view); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Runs the body's own draw with the hands in it. False leaves the body withheld
// as before. Called in place of the skip, on the render thread.
bool DrawBodyHands(void* model,void* renderContext,int pass,void* view) noexcept {
    if(pass>=0 && pass<32) g_handPassSeen.fetch_or(1u<<pass,std::memory_order_relaxed);
    if(!g_handModels || !g_modelOriginal || !g_vrEnabled || g_faulted || g_nativeTactical || pass<0 || pass>=32) return false;
    // With the weapon's live layer up, the hands go into it from the G-buffer
    // pass alone, both eyes at once, and nothing of them is drawn natively.
    // Without it they are drawn in the passes named by HandPassMask.
    ID3D11DeviceContext* drawContext=nullptr;
    if(renderContext && edf6vr::Readable(renderContext,16)) drawContext=*reinterpret_cast<ID3D11DeviceContext**>(static_cast<unsigned char*>(renderContext)+8);
    // Once the live layer exists at all, the hands go there or nowhere: a
    // native copy drawn in a pass the layer could not take was a second,
    // lagging pair of hands.
    const bool live=edf6vr::WeaponStereoLive();
    const bool layer=live && drawContext && edf6vr::HandStereoAvailable(drawContext);
    if(live) {
        if(!layer || pass!=1) return true;
        if(edf6vr::NativeWorldRenderEye()==1 && edf6vr::HandStereoDrawnThisFrame()) return true;
    } else if(!((1u<<pass)&g_handPassMask)) return false;
    auto soldier=static_cast<unsigned char*>(model)-edf6vr::kBodyModelOffset;
    if(!EnsureHandRig(soldier)) { ++g_handRefused; return false; }
    float axes[2][3][3]{},palm[2][3]{}; bool have[2]={false,false};
    for(int h=0;h<2;++h) have[h]=HandControllerFrame(h,axes[h],palm[h]);
    if(!have[0] && !have[1]) { ++g_handRefused; return false; }
    // The hands kick with the weapons they hold; the support hand of a
    // two-handed weapon with the firing hand.
    KickHands(have,axes,palm);
    edf6vr::ControllerState controls{};
    edf6vr::g_openxr.ControlsPhysical(controls);   // each hand's fingers from its own controller
    if(layer) {
        // The same eye separation and origin the weapon hands the layer, so the
        // two are drawn against one camera and stay together when the head moves.
        AcquireSRWLockShared(&g_lock);
        const bool liveCamera=g_weaponStereoLive && g_stereoMode>=4 && !g_swapEyes && g_lastIpd>.03f && g_lastIpd<.09f;
        const float eyeHalf=g_lastIpd*g_ipdScale*.5f,sourceOffset=g_lastEyeOffset;
        float eye[3]; std::memcpy(eye,g_holdCommand.tracked?g_holdCommand.eyeWorld:g_eyeWorld,sizeof(eye));
        ReleaseSRWLockShared(&g_lock);
        if(liveCamera) { edf6vr::SetWeaponStereoEyes(-eyeHalf,eyeHalf); edf6vr::SetWeaponStereoOrigin(eye,sourceOffset); }
    }
    edf6vr::Matrix* palette=nullptr; std::size_t count=0;
    if(!ResolveBodyPalette(model,pass,palette,count)) { ++g_handRefused; return false; }
    // Everything written is put back, whatever the draw does.
    static thread_local edf6vr::Matrix kept[2][edf6vr::kHandBoneMax+2];
    unsigned touched[2]={0,0}; bool drawn[2]={false,false};
    bool posed=PoseHandsIntoPalette(palette,count,have,axes,palm,controls,kept,touched,drawn);
    if(posed) {
        edf6vr::HandDrawScope scope(drawn[0],drawn[1],layer);
        if(!CallBodyDrawGuarded(model,renderContext,pass,view)) {
            posed=false; g_handModels=false; Log("HANDMODEL fault while drawing; hands off");
        }
    }
    RestoreHandPalette(palette,count,kept,touched);
    if(posed) { ++g_handBodyDraws; if(layer && edf6vr::HandStereoDrawnThisFrame()) ++g_handLayerDraws; } else ++g_handRefused;
    return posed;
}
