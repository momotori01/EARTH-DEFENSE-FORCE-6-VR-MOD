// Production hooks + synthetic instances. Mapped EDF is data only; no game code runs.
#include "../src/plugin.cpp"
#include <limits>

static int failures=0, gameUpdates=0, forwardedDraws=0;
#define VERIFY(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
alignas(16) static unsigned char nodes[84*0x110]{};
static bool findHead=true;
static int armsLookups=0;
static void* __fastcall FakeLookup(void*,const void* name) {
    const auto text=static_cast<const wchar_t*>(name);
    // The weapon hangs on a bone the game's own weapon data names: every Ranger
    // weapon carries ModelConstraint = "arms_r", one carries "arms". Both are
    // asked for here, so both are answered; anything else is a mistake.
    if(!wcscmp(text,L"arms_r")) { ++armsLookups; return nodes+40*0x110; }
    if(!wcscmp(text,L"arms")) { ++armsLookups; return nullptr; }
    VERIFY(wcscmp(text,L"head")==0);
    return findHead?nodes+63*0x110:nullptr;
}
static const wchar_t* cabinModel=L"v506_heli";
static void* cabinNode=nullptr;
static const wchar_t* cabinPositionBone=L"";
static void* cabinPoseNode=nullptr;
static int cabinLookups=0;
static void* __fastcall FakeCabinLookup(void*,const void* name) {
    ++cabinLookups;
    const auto capacity=*reinterpret_cast<const std::uint64_t*>(static_cast<const unsigned char*>(name)+24);
    const auto text=capacity<=7?static_cast<const wchar_t*>(name):*static_cast<const wchar_t* const*>(name);
    if(!wcscmp(text,cabinModel)) return cabinNode;
    return !wcscmp(text,cabinPositionBone)?cabinPoseNode:nullptr;
}
static void __fastcall FakeUpdate(void*,const void*) { ++gameUpdates; }
static void __fastcall FakeDraw(void*,void*,int,void*) { ++forwardedDraws; }
template<typename T> static T& At(void* p,std::size_t off) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+off);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    if(!mapped) return 2;
    char reason[256]{};
    if(!edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason))) { puts(reason); return 2; }
    VERIFY(edf6vr::CheckFirstPersonProfile(g_image));
    alignas(16) unsigned char camera[0xC00]{},soldier[0x2100]{},resource[0x100]{},otherModel[0x500]{};
    At<void*>(soldier,0)=g_image.base+0x17CDF28;
    At<void*>(soldier,0x340)=resource;
    At<std::uint32_t>(soldier,0x314)=17;
    At<void*>(soldier,0x860)=g_image.base+edf6vr::kModelVtableRva;
    At<void*>(soldier,0x900)=resource;
    At<void*>(soldier,0x910)=nodes;
    At<std::uint64_t>(soldier,0x920)=84;
    At<void*>(otherModel,0)=g_image.base+edf6vr::kModelVtableRva;
    At<void*>(camera,0)=g_image.base+edf6vr::kVtableRva;
    At<void*>(camera,0x350)=soldier; At<void*>(camera,0x360)=soldier;
    const edf6vr::Matrix root{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{10,20,30,1}}};
    At<edf6vr::Matrix>(soldier,0x5E0)=root;
    At<edf6vr::Matrix>(soldier,0x60)=root;
    auto head=root; head.m[3][1]+=1.53f;
    At<edf6vr::Matrix>(nodes+63*0x110,0xB0)=head;
    auto thirdPerson=root; thirdPerson.m[3][1]+=1.8f; thirdPerson.m[3][2]-=3.5f;
    At<edf6vr::Matrix>(camera,0x220)=thirdPerson;
    g_original=FakeUpdate; g_modelOriginal=FakeDraw; g_nodeLookup=FakeLookup;
    g_fpsReady=true; g_fpsEnabled=true; g_enabled=false;
    for(int i=0;i<1000;++i) {
        HookUpdate(camera,nullptr);
        HookModelDraw(soldier+0x860,nullptr,2,nullptr);
        HookModelDraw(otherModel,nullptr,2,nullptr); // Same model class, different instance: weapon/NPC.
    }
    const auto& first=At<edf6vr::Matrix>(camera,0x220);
    VERIFY(gameUpdates==1000 && g_fpsApplied==1000 && !g_faulted);
    VERIFY(std::fabs(first.m[3][1]-21.68f)<0.0001f);
    VERIFY(first.m[3][0]==10 && first.m[3][2]==30);
    VERIFY(!std::memcmp(first.m,thirdPerson.m,48)); // Stock aim basis unchanged, no accumulating yaw.
    VERIFY(g_bodyTarget.pose.headFound && g_bodyTarget.pose.headIndex==63);
    VERIFY(g_bodySkipped==1000 && forwardedDraws==1000);
    VERIFY(At<unsigned char>(soldier+0x860,0x4D0)==1);
    VERIFY(At<unsigned char>(otherModel,0x4D0)==0); // Only live owner's activity bookkeeping touched.
    // Diagnostic body toggle, stale lease, and reused memory must all forward drawing.
    g_hideBody=false; HookModelDraw(soldier+0x860,nullptr,2,nullptr); VERIFY(forwardedDraws==1001);
    g_hideBody=true; g_bodyTarget.refreshed=0; HookModelDraw(soldier+0x860,nullptr,2,nullptr); VERIFY(forwardedDraws==1002);
    HookUpdate(camera,nullptr);
    At<std::uint32_t>(soldier,0x314)=18;
    HookModelDraw(soldier+0x860,nullptr,2,nullptr); VERIFY(forwardedDraws==1003);
    // Fallback and invalid head position guard.
    edf6vr::PlayerPose pose{};
    findHead=false;
    VERIFY(edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    VERIFY(!pose.headFound && std::fabs(pose.eye[1]-21.70f)<0.0001f);
    findHead=true; At<edf6vr::Matrix>(nodes+63*0x110,0xB0).m[3][1]=std::numeric_limits<float>::quiet_NaN();
    VERIFY(!edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    At<edf6vr::Matrix>(nodes+63*0x110,0xB0)=head;
    // Vehicle, missing controller, invalid skeleton, and wrong owner class are rejected.
    unsigned int vehicle[3]={0,0,1}; At<void*>(soldier,0x1550)=vehicle;
    VERIFY(!edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    At<void*>(soldier,0x1550)=nullptr; At<void*>(soldier,0x340)=nullptr;
    VERIFY(!edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    At<void*>(soldier,0x340)=resource; At<std::uint64_t>(soldier,0x920)=2000;
    VERIFY(!edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    At<std::uint64_t>(soldier,0x920)=84;
    At<void*>(soldier,0)=g_image.base+edf6vr::kVtableRva;
    VERIFY(!edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    At<void*>(soldier,0)=g_image.base+0x17CDF28;
    // Turning FPS off restores camera position and drawing next callback.
    g_fpsEnabled=false; HookUpdate(camera,nullptr);
    VERIFY(!std::memcmp(&At<edf6vr::Matrix>(camera,0x220),&thirdPerson,64));
    HookModelDraw(soldier+0x860,nullptr,2,nullptr); VERIFY(forwardedDraws==1004);
    // Real RTTI + shared production hooks for both new classes. Reuse the same
    // address and object ID deliberately: a vtable/class change must reset state.
    for(const auto table:{0x17D0FF8u,0x17CF100u}) {
        At<void*>(soldier,0)=g_image.base+table;
        g_moveBasis.locked=true; g_moveBasis.scale=99;
        g_nearestCandidate=otherModel; g_weaponModelSeen=otherModel;
        g_fpsEnabled=true;
        const auto skipped=g_bodySkipped.load(); const auto draws=forwardedDraws;
        for(int i=0;i<30;++i) {
            HookUpdate(camera,nullptr);
            HookModelDraw(soldier+0x860,nullptr,2,nullptr);
            HookModelDraw(otherModel,nullptr,2,nullptr);
        }
        VERIFY(edf6vr::IsSupportedSoldier(g_image,soldier));
        VERIFY(g_bodyTarget.pose.headFound && !g_faulted);
        VERIFY(g_bodySkipped==skipped+30 && forwardedDraws==draws+30);
        VERIFY(!g_moveBasis.locked && g_moveBasis.scale!=99);
        VERIFY(g_nearestCandidate==nullptr && g_weaponModelSeen==nullptr);
        VERIFY(std::fabs(At<edf6vr::Matrix>(camera,0x220).m[3][1]-21.68f)<0.0001f);
        // A flying root translates the eye by the same amount; never clamp to ground.
        At<edf6vr::Matrix>(soldier,0x5E0).m[3][1]+=10;
        At<edf6vr::Matrix>(soldier,0x60).m[3][1]+=10;
        At<edf6vr::Matrix>(nodes+63*0x110,0xB0).m[3][1]+=10;
        HookUpdate(camera,nullptr);
        VERIFY(std::fabs(At<edf6vr::Matrix>(camera,0x220).m[3][1]-31.68f)<0.0001f);
        At<edf6vr::Matrix>(soldier,0x5E0)=root;
        At<edf6vr::Matrix>(soldier,0x60)=root;
        At<edf6vr::Matrix>(nodes+63*0x110,0xB0)=head;
        At<void*>(soldier,0x1550)=vehicle;
        VERIFY(!edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
        At<void*>(soldier,0x1550)=nullptr;
        g_fpsEnabled=false; HookUpdate(camera,nullptr);
        VERIFY(!std::memcmp(&At<edf6vr::Matrix>(camera,0x220),&thirdPerson,64));
        HookModelDraw(soldier+0x860,nullptr,2,nullptr); VERIFY(forwardedDraws==draws+31);
    }
    // Preserve native aerial camera, then restore FPS after release.
    auto aerial=thirdPerson; aerial.m[3][1]=60;
    At<edf6vr::Matrix>(camera,0x220)=aerial; g_fpsEnabled=true;
    const auto drawsBeforeTactical=forwardedDraws;
    HookUpdate(camera,nullptr); HookModelDraw(soldier+0x860,nullptr,2,nullptr);
    VERIFY(g_nativeTactical && !std::memcmp(&At<edf6vr::Matrix>(camera,0x220),&aerial,64));
    VERIFY(forwardedDraws==drawsBeforeTactical+1);
    At<edf6vr::Matrix>(camera,0x220)=thirdPerson;
    HookUpdate(camera,nullptr);
    VERIFY(!g_nativeTactical && g_bodyTarget.pose.headFound);
    g_fpsEnabled=false; HookUpdate(camera,nullptr);
    // HeavyArmor: FPS/body ownership works, but tracked aim must NEVER write
    // native integrated/smoothed angles, inertia, recoil or weapon transforms.
    At<void*>(soldier,0)=g_image.base+0x17CF5B8;
    VERIFY(edf6vr::IsSupportedSoldier(g_image,soldier));
    VERIFY(edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    g_fpsEnabled=true; const auto applied=g_fpsApplied; HookUpdate(camera,nullptr);
    VERIFY(g_fpsApplied==applied+1);
    At<float>(soldier,0x1260)=0.015f;
    At<float>(soldier,0x1230)=0.1f; At<float>(soldier,0x1234)=6.2f;
    At<float>(soldier,0x1240)=0.08f; At<float>(soldier,0x1244)=6.0f;
    unsigned char savedAim[0x40]{}; memcpy(savedAim,soldier+0x1230,sizeof(savedAim));
    VERIFY(!WriteTrackedAim(soldier,-0.5f,0.3f,true));
    VERIFY(!memcmp(savedAim,soldier+0x1230,sizeof(savedAim)));
    VERIFY(g_fencerPad.Fresh(GetTickCount64()) && g_fencerPad.x<0 && g_fencerPad.y>0);
    VERIFY(!WriteTrackedAim(soldier,-0.5f,0.3f,false));
    VERIFY(!g_fencerPad.valid);
    const auto pursuit=edf6vr::FencerPursuit(0,6.2f,-0.5f,0.3f,100);
    VERIFY(pursuit.x<0 && pursuit.y>0 && pursuit.Fresh(199) && !pursuit.Fresh(200) && !pursuit.Fresh(99));
    VERIFY(edf6vr::FencerPursuitAxis(0)==0 && edf6vr::FencerPursuitAxis(100)==1);
    VERIFY(edf6vr::FencerPursuitAxis(-100)==-1);
    VERIFY(!edf6vr::FencerPursuit(0,0,0,std::numeric_limits<float>::infinity(),100).valid);
    VERIFY(std::fabs(edf6vr::FencerYawError(-3.13f,3.13f))<0.03f);
    VERIFY(std::fabs(edf6vr::FencerYawError(3.13f,-3.13f))<0.03f);
    // Fencer stereo lease accepts both native weapons, rejects NPC/stale/swap.
    alignas(16) unsigned char weapons[2][0x1500]{}, transforms[2][0x100]{}, entries[2*0x150]{};
    void* wrappers[2]={weapons[0],weapons[1]};
    At<void*>(soldier,0x1970)=entries; At<unsigned>(soldier,0x1980)=2;
    for(unsigned i=0;i<2;++i) {
        At<void*>(entries,i*0x150+0x40)=wrappers+i;
        At<void*>(weapons[i],0x1D0)=transforms[i];
        At<edf6vr::Matrix>(transforms[i],0x50)=root; At<float>(weapons[i],0x358)=1;
        At<void*>(weapons[i],edf6vr::kWeaponModelOffset)=g_image.base+edf6vr::kModelVtableRva;
    }
    PublishFencerStereo(soldier,true); const auto lease=g_fencerStereo;
    VERIFY(ValidateFencerStereo(lease,weapons[0]+edf6vr::kWeaponModelOffset));
    VERIFY(ValidateFencerStereo(lease,weapons[1]+edf6vr::kWeaponModelOffset));
    VERIFY(!ValidateFencerStereo(lease,otherModel));
    ++At<unsigned>(soldier,0x314); VERIFY(!ValidateFencerStereo(lease,weapons[0]+edf6vr::kWeaponModelOffset));
    --At<unsigned>(soldier,0x314);
    wrappers[1]=otherModel; VERIFY(!ValidateFencerStereo(lease,weapons[1]+edf6vr::kWeaponModelOffset)); wrappers[1]=weapons[1];
    auto expired=lease; expired.refreshed=GetTickCount64()-251;
    VERIFY(!ValidateFencerStereo(expired,weapons[0]+edf6vr::kWeaponModelOffset));
    PublishFencerStereo(soldier,false); VERIFY(!g_fencerStereo.soldier);
    PublishFencerStereo(soldier,true);
    g_fpsEnabled=false; HookUpdate(camera,nullptr); VERIFY(!g_fencerPad.valid && !g_fencerStereo.soldier);
    float badEye[3]={0,std::numeric_limits<float>::infinity(),0}; edf6vr::Matrix output{};
    VERIFY(!edf6vr::PlaceAtEye(thirdPerson,badEye,output));
    VERIFY(*g_image.updateSlot==g_image.original); // Mapped game never patched.
    // Vehicle mount identity, stale controlblock and native input isolation.
    VERIFY(edf6vr::CheckVehicleProfile(g_image));
    alignas(16) unsigned char bike[0x400]{},seatData[0x280]{},control[0x10]{};
    At<void*>(bike,0)=g_image.base+0x17DA508;At<unsigned>(bike,0x314)=100;
    At<void*>(seatData,0)=g_image.base+0x17D9868;At<void*>(seatData,8)=bike;
    At<unsigned>(control,8)=1;
    At<void*>(soldier,0x1540)=seatData;At<void*>(soldier,0x1548)=bike;At<void*>(soldier,0x1550)=control;
    edf6vr::VehicleSeat mount{};
    VERIFY(edf6vr::ReadVehicleSeat(g_image,soldier,mount) && mount.vehicle==bike && mount.riderHead);
    VERIFY(edf6vr::HasMountReference(soldier));
    VERIFY(!edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose));
    VERIFY(edf6vr::ReadPlayerPose(g_image,soldier,FakeLookup,g_eyeSettings,pose,true) && pose.headFound);
    At<unsigned>(control,8)=0;VERIFY(!edf6vr::ReadVehicleSeat(g_image,soldier,mount));
    VERIFY(!edf6vr::HasMountReference(soldier));At<unsigned>(control,8)=1;
    At<void*>(seatData,0)=g_image.base+edf6vr::kModelVtableRva;
    VERIFY(!edf6vr::ReadVehicleSeat(g_image,soldier,mount));
    At<void*>(seatData,0)=g_image.base+0x17D9868;
    g_vrEnabled=true;g_vrSoldier=soldier;g_vrSoldierSeen=GetTickCount64();
    g_vrSoldierId=At<unsigned>(soldier,0x314);g_vrSoldierTable=At<void*>(soldier,0);
    unsigned char playerCopy[sizeof soldier];memcpy(playerCopy,soldier,sizeof soldier);
    ApplyVrInput(soldier);VERIFY(!memcmp(playerCopy,soldier,sizeof soldier));
    VERIFY(!g_handAiming && !g_fencerPad.valid);
    // Forced bike dismount: retain standing calibration through the downed pose,
    // recover the normal eye/body draw heartbeat, and do not stay near the floor.
    At<void*>(soldier,0x1540)=nullptr;At<void*>(soldier,0x1548)=nullptr;At<void*>(soldier,0x1550)=nullptr;
    g_fpsEnabled=true;g_headRootLock=true;g_vehicleMounted=true;
    g_headAnchor={};g_headAnchorOwner=soldier;g_headAnchorId=At<unsigned>(soldier,0x314);
    float standingEye[3]={10,21.68f,30},calibrated[3]{};
    VERIFY(edf6vr::AnchorHead(g_headAnchor,root,standingEye,calibrated));
    const auto savedHeight=g_headAnchor.offset[1];
    At<edf6vr::Matrix>(nodes+63*0x110,0xB0).m[3][1]=20.05f;
    At<edf6vr::Matrix>(camera,0x220)=thirdPerson;
    HookUpdate(camera,nullptr);
    VERIFY(!g_vehicleMounted && g_headAnchor.primed && g_headAnchor.offset[1]==savedHeight);
    VERIFY(std::fabs(At<edf6vr::Matrix>(camera,0x220).m[3][1]-21.68f)<.001f);
    At<edf6vr::Matrix>(nodes+63*0x110,0xB0)=head;
    HookUpdate(camera,nullptr);
    VERIFY(std::fabs(At<edf6vr::Matrix>(camera,0x220).m[3][1]-21.68f)<.001f);
    const auto hiddenBefore=g_bodySkipped.load();
    HookModelDraw(soldier+0x860,nullptr,2,nullptr);
    VERIFY(g_bodySkipped==hiddenBefore+1 && g_bodyDraw!=0);
    // Field log: a Nix dismount briefly reuses its high, distant native view.
    // That matrix is not an Engineer targeting command. Restore FPS first;
    // a genuine targeting view on a later update must still enter tactical.
    At<void*>(soldier,0)=g_image.base+0x17CF100;
    VERIFY(edf6vr::HasType(g_image,soldier,".?AVEngineer@@"));
    auto departing=thirdPerson;departing.m[3][1]+=8;departing.m[3][2]-=14;
    g_vehicleMounted=true;g_nativeTactical=false;
    At<edf6vr::Matrix>(camera,0x220)=departing;HookUpdate(camera,nullptr);
    VERIFY(!g_nativeTactical && !g_vehicleMounted);
    VERIFY(std::fabs(At<edf6vr::Matrix>(camera,0x220).m[3][1]-21.68f)<.001f);
    At<edf6vr::Matrix>(camera,0x220)=departing;HookUpdate(camera,nullptr);
    VERIFY(g_nativeTactical);
    At<edf6vr::Matrix>(camera,0x220)=thirdPerson;HookUpdate(camera,nullptr);VERIFY(!g_nativeTactical);
    g_vrEnabled=false;
    // Cabins: exact model signature, primary seat only, vehicle rotation used
    // for position, native camera basis retained; no vehicle/seat/bone writes.
    alignas(16) unsigned char cabinVehicle[0x1500]{},cabinSeats[5*0x340]{},cabinBones[2*0x110]{};
    At<void*>(cabinVehicle,0)=g_image.base+0x17DB238; // Vehicle506_Helicopter
    At<void*>(cabinVehicle,0xE40)=g_image.base+edf6vr::kModelVtableRva;
    At<void*>(cabinVehicle,0xEE0)=resource;At<void*>(cabinVehicle,0xEF0)=cabinBones;
    At<std::uint64_t>(cabinVehicle,0xF00)=1;
    At<void*>(cabinVehicle,0x608)=cabinSeats;At<std::uint64_t>(cabinVehicle,0x618)=2;
    At<edf6vr::Matrix>(cabinVehicle,0x60)=root;cabinNode=cabinBones;
    edf6vr::VehicleSeat cabin{};cabin.vehicle=cabinVehicle;cabin.seat=cabinSeats;cabin.cameraOwner=cabinVehicle;
    {
        // Reproduce boarding with different on-foot camera/head headings.
        // All must face the chassis, preserving the already selected eye point.
        unsigned char saved[sizeof cabinVehicle];memcpy(saved,cabinVehicle,sizeof saved);
        for(float entryYaw:{-110.f,0.f,75.f}) {
            edf6vr::Matrix inherited{},faced{};
            VERIFY(edf6vr::RotateCamera(root,entryYaw,-25,inherited));
            inherited.m[3][1]+=2.8f;faced=inherited;
            VERIFY(edf6vr::FaceVehicleForward(cabin,faced));
            VERIFY(!memcmp(faced.m,root.m,48));
            VERIFY(!memcmp(faced.m[3],inherited.m[3],16));
            const float a=entryYaw*.01745329252f;
            const edf6vr::Quat h{0,std::sin(a*.5f),0,std::cos(a*.5f)};
            edf6vr::Quat reference{};edf6vr::Matrix view{};float localYaw=1;
            VERIFY(edf6vr::VehicleHeadingReference(h,reference));
            VERIFY(edf6vr::ComposeVehicleCamera(faced,reference,h,{},view,localYaw));
            for(int i=0;i<3;++i)for(int j=0;j<3;++j)VERIFY(std::fabs(view.m[i][j]-root.m[i][j])<.0001f);
            VERIFY(std::fabs(localYaw)<.0001f);
        }
        VERIFY(!memcmp(saved,cabinVehicle,sizeof saved));
        // Turn, pitch and bank after boarding: read the live chassis each frame.
        edf6vr::Matrix turned{},bankedChassis{};float roll=0;
        VERIFY(edf6vr::RotateCamera(root,55,17,turned));
        VERIFY(edf6vr::RollCameraToUp(turned,{-.4f,.916515f,0},bankedChassis,roll));
        At<edf6vr::Matrix>(cabinVehicle,0x60)=bankedChassis;
        auto faced=thirdPerson;
        VERIFY(edf6vr::FaceVehicleForward(cabin,faced));
        VERIFY(!memcmp(faced.m,bankedChassis.m,48));
        VERIFY(!memcmp(faced.m[3],thirdPerson.m[3],16));
        // Unknown models and passengers still use the chassis; no skeleton
        // lookup, turret rotation, or seat-specific mutable reference is needed.
        cabin.riderHead=true;VERIFY(edf6vr::FaceVehicleForward(cabin,faced));cabin.riderHead=false;
        cabin.seat=cabinSeats+0x340;VERIFY(edf6vr::FaceVehicleForward(cabin,faced));cabin.seat=cabinSeats;
        const auto good=faced;
        At<edf6vr::Matrix>(cabinVehicle,0x60)={};
        VERIFY(!edf6vr::FaceVehicleForward(cabin,faced));
        VERIFY(!memcmp(&good,&faced,sizeof faced));
        auto invalid=cabin;invalid.vehicle=nullptr;
        VERIFY(!edf6vr::FaceVehicleForward(invalid,faced));
        VERIFY(!memcmp(&good,&faced,sizeof faced));
        memcpy(cabinVehicle,saved,sizeof saved);
    }
    const char* cabinLabel=nullptr;int cabinIndex=-1;
    auto cabinCamera=thirdPerson;
    unsigned char vehicleBefore[sizeof cabinVehicle];memcpy(vehicleBefore,cabinVehicle,sizeof cabinVehicle);
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(cabinIndex==0 && !strcmp(cabinLabel,"euros-canopy"));
    VERIFY(std::fabs(cabinCamera.m[3][1]-22.05f)<.001f && std::fabs(cabinCamera.m[3][2]-32.55f)<.001f);
    VERIFY(!memcmp(cabinCamera.m,thirdPerson.m,48));
    VERIFY(!memcmp(vehicleBefore,cabinVehicle,sizeof cabinVehicle));
    cabin.seat=cabinSeats+0x340;cabinCamera=thirdPerson;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(cabinIndex==1 && !memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    cabin.seat=cabinSeats+1;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    cabin.seat=cabinSeats;cabinModel=L"unknown";
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    cabinModel=L"v512_keiTruck";
    edf6vr::Matrix banked{};VERIFY(edf6vr::RotateCamera(root,65,25,banked));
    At<edf6vr::Matrix>(cabinVehicle,0x60)=banked;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    for(int j=0;j<3;++j) {
        const float expected=banked.m[3][j]-.44f*banked.m[0][j]+1.62f*banked.m[1][j]+1.06f*banked.m[2][j];
        VERIFY(std::fabs(cabinCamera.m[3][j]-expected)<.001f);
    }
    VERIFY(!strcmp(cabinLabel,"kei-driver"));
    cabinModel=L"v505_tank";
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!strcmp(cabinLabel,"505-hatch"));
    cabin.cameraOwner=otherModel;const auto unchanged=cabinCamera;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!memcmp(&unchanged,&cabinCamera,sizeof cabinCamera));
    cabin.cameraOwner=cabinVehicle;At<std::uint64_t>(cabinVehicle,0xF00)=2049;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    // Animated cockpit follows a read-only bone world pose, not the static root.
    At<std::uint64_t>(cabinVehicle,0xF00)=2;
    At<std::uint64_t>(cabinVehicle,0x618)=4;
    cabinModel=L"v504_begaruta";cabinPositionBone=L"head";cabinPoseNode=cabinBones+0x110;
    auto headPose=root;headPose.m[3][1]+=7.73212f;headPose.m[3][2]+=.37005f;
    At<edf6vr::Matrix>(cabinPoseNode,0xB0)=headPose;
    unsigned char bonesBefore[sizeof cabinBones];memcpy(bonesBefore,cabinBones,sizeof cabinBones);
    memcpy(vehicleBefore,cabinVehicle,sizeof cabinVehicle);
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(std::fabs(cabinCamera.m[3][1]-27.95212f)<.001f && std::fabs(cabinCamera.m[3][2]-31.3f)<.001f);
    VERIFY(!memcmp(cabinCamera.m,thirdPerson.m,48));
    VERIFY(!memcmp(vehicleBefore,cabinVehicle,sizeof cabinVehicle));
    VERIFY(!memcmp(bonesBefore,cabinBones,sizeof cabinBones));
    const auto standingCabin=cabinCamera;
    At<edf6vr::Matrix>(cabinPoseNode,0xB0).m[3][1]-=2;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(std::fabs(cabinCamera.m[3][1]-(standingCabin.m[3][1]-2))<.001f);
    // Missing/invalid animation pose must not fall back to root + bone-local offset.
    cabinPoseNode=nullptr;cabinCamera=thirdPerson;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    cabinPoseNode=cabinBones+0x110;At<edf6vr::Matrix>(cabinPoseNode,0xB0)={};
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    // V612 has the same head location as V504, but different bind axes.
    auto alternateHead=headPose;
    const float alternateAxes[3][4]={{0,1,0,0},{0,0,-1,0},{-1,0,0,0}};
    memcpy(alternateHead.m,alternateAxes,sizeof alternateAxes);
    cabinModel=L"v612_nix";At<edf6vr::Matrix>(cabinPoseNode,0xB0)=alternateHead;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    for(int j=0;j<3;++j) VERIFY(std::fabs(cabinCamera.m[3][j]-standingCabin.m[3][j])<.001f);
    // Giant front deck still receives 30 cm of HMD motion as 30 cm, not model scale.
    cabinModel=L"v605_barga_cannon";cabinPositionBone=L"mune";
    alternateHead.m[3][1]=36.50132f;alternateHead.m[3][2]=0;
    At<edf6vr::Matrix>(cabinPoseNode,0xB0)=alternateHead;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(std::fabs(cabinCamera.m[3][1]-47.0875f)<.001f && std::fabs(cabinCamera.m[3][2]-7.0f)<.001f);
    edf6vr::Matrix giantMoved{};float giantYaw=0;
    VERIFY(edf6vr::ComposeVehicleCamera(cabinCamera,{0,0,0,1},{0,0,0,1},{0,.3f,0},giantMoved,giantYaw));
    float giantDistance2=0;
    for(int j=0;j<3;++j) { const float d=giantMoved.m[3][j]-cabinCamera.m[3][j];giantDistance2+=d*d; }
    VERIFY(std::fabs(giantDistance2-.09f)<.0001f);
    // A model marker alone cannot enable gun seats on a non-Proteus vehicle.
    // Non-Proteus passenger seats retain their complete native camera.
    for(const auto model:{L"Vehicle407_bigbegaruta",L"v614_proteus_mk2"}) {
        cabinModel=model;cabinPositionBone=L"neck";
        At<edf6vr::Matrix>(cabinPoseNode,0xB0)=headPose;
        cabin.seat=cabinSeats;cabinCamera=thirdPerson;
        VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
        VERIFY(cabinIndex==0 && strstr(cabinLabel,"proteus"));
        for(int index=1;index<4;++index) {
            cabin.seat=cabinSeats+index*0x340;cabinCamera=thirdPerson;
            const int before=cabinLookups;
            VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            VERIFY(cabinIndex==index && !strcmp(cabinLabel,"native-passenger-seat"));
            VERIFY(cabinLookups==before && !memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
        }
    }
    // Actual Proteus type + four seats + matching model: each gun seat requests
    // only its own moving bone. Missing bones never become driver fallbacks.
    At<void*>(cabinVehicle,0)=g_image.base+0x17DEC40;
    VERIFY(edf6vr::HasType(g_image,cabinVehicle,".?AVVehicleBigBegaruta@@"));
    for(const auto model:{L"Vehicle407_bigbegaruta",L"v614_proteus_mk2"}) {
        cabinModel=model;const bool mk2=!wcscmp(model,L"v614_proteus_mk2");
        for(int index=1;index<4;++index) {
            cabin.seat=cabinSeats+index*0x340;cabinCamera=thirdPerson;
            cabinPositionBone=index==1?(mk2?L"gun_mount_l":L"gun_mount_barrel_l"):
                index==2?(mk2?L"gun_mount_r":L"gun_mount_barrel_r"):
                (mk2?L"missile_launcher":L"rocket_launcher");
            cabinPoseNode=cabinBones+0x110;
            At<edf6vr::Matrix>(cabinPoseNode,0xB0)=banked;
            memcpy(bonesBefore,cabinBones,sizeof cabinBones);
            memcpy(vehicleBefore,cabinVehicle,sizeof cabinVehicle);
            unsigned char seatsBefore[sizeof cabinSeats];memcpy(seatsBefore,cabinSeats,sizeof cabinSeats);
            VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            VERIFY(cabinIndex==index && strstr(cabinLabel,index==1?"left-sensor":index==2?"right-sensor":"missile-top"));
            VERIFY(!memcmp(cabinCamera.m,thirdPerson.m,48));
            VERIFY(!memcmp(bonesBefore,cabinBones,sizeof cabinBones));
            VERIFY(!memcmp(vehicleBefore,cabinVehicle,sizeof cabinVehicle));
            VERIFY(!memcmp(seatsBefore,cabinSeats,sizeof cabinSeats));
            const auto gunCamera=cabinCamera;
            At<edf6vr::Matrix>(cabinPoseNode,0xB0).m[3][0]+=.5f;
            VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            VERIFY(std::fabs(cabinCamera.m[3][0]-gunCamera.m[3][0]-.5f)<.001f);
            // Wrong seat bone (driver neck) is deliberately present instead.
            cabinPositionBone=L"neck";cabinCamera=thirdPerson;
            VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
        }
    }
    cabinModel=L"unknown-proteus-variant";
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    cabinModel=L"v614_proteus_mk2";cabinPositionBone=L"missile_launcher";
    // Do not assume this seat order for modified two-seat vehicles, or other owners.
    At<std::uint64_t>(cabinVehicle,0x618)=2;cabin.seat=cabinSeats+0x340;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    At<std::uint64_t>(cabinVehicle,0x618)=4;cabin.seat=cabinSeats+3*0x340;cabin.cameraOwner=otherModel;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    // Brute has three seats, not Proteus' four. Explicit gunCam bones belong
    // to this vehicle only; pilot retains the existing canopy profile.
    At<void*>(cabinVehicle,0)=g_image.base+0x17DF338;
    VERIFY(edf6vr::HasType(g_image,cabinVehicle,".?AVVehicleHelicopter410@@"));
    At<std::uint64_t>(cabinVehicle,0x618)=3;cabin.cameraOwner=cabinVehicle;
    cabinModel=L"Vehicle410_heli";cabinPoseNode=cabinBones+0x110;
    for(int index=1;index<=2;++index) {
        cabin.seat=cabinSeats+index*0x340;cabinPositionBone=index==1?L"gunCam_l":L"gunCam_r";
        auto gunPose=banked;gunPose.m[3][0]=index==1?3.01751f:-3.01751f;gunPose.m[3][1]=1.62756f;gunPose.m[3][2]=.57195f;
        At<edf6vr::Matrix>(cabinPoseNode,0xB0)=gunPose;cabinCamera=thirdPerson;
        memcpy(vehicleBefore,cabinVehicle,sizeof cabinVehicle);memcpy(bonesBefore,cabinBones,sizeof cabinBones);
        VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
        VERIFY(!strcmp(cabinLabel,index==1?"brute-left-guncam":"brute-right-guncam"));
        for(int j=0;j<3;++j) VERIFY(std::fabs(cabinCamera.m[3][j]-gunPose.m[3][j])<.001f);
        VERIFY(!memcmp(cabinCamera.m,thirdPerson.m,48));
        VERIFY(!memcmp(vehicleBefore,cabinVehicle,sizeof cabinVehicle) && !memcmp(bonesBefore,cabinBones,sizeof cabinBones));
        At<edf6vr::Matrix>(cabinPoseNode,0xB0).m[3][2]+=1.2f;
        VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
        VERIFY(std::fabs(cabinCamera.m[3][2]-gunPose.m[3][2]-1.2f)<.001f);
        cabinPositionBone=index==1?L"gunCam_r":L"gunCam_l";cabinCamera=thirdPerson;
        VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
        VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    }
    cabin.seat=cabinSeats;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!strcmp(cabinLabel,"brute-canopy"));
    cabin.seat=cabinSeats+0x340;cabinPositionBone=L"gunCam_l";cabinCamera=thirdPerson;
    At<std::uint64_t>(cabinVehicle,0x618)=4;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    At<std::uint64_t>(cabinVehicle,0x618)=3;At<void*>(cabinVehicle,0)=g_image.base+0x17DB238;
    VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    // EMC uses animated body, not the moving cannon or the static vehicle root.
    cabin.seat=cabinSeats;At<std::uint64_t>(cabinVehicle,0x618)=1;
    At<void*>(cabinVehicle,0)=g_image.base+0x17DB9D8;cabinModel=L"v510_maser";cabinPositionBone=L"body";
    At<edf6vr::Matrix>(cabinPoseNode,0xB0)=banked;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!strcmp(cabinLabel,"emc-front-deck"));
    const auto emcCamera=cabinCamera;
    At<edf6vr::Matrix>(cabinPoseNode,0xB0).m[3][1]-=.7f;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(std::fabs(cabinCamera.m[3][1]-emcCamera.m[3][1]+.7f)<.001f);
    // Kevlar's fixed hatch position rotates with the chassis and keeps native view basis.
    At<void*>(cabinVehicle,0)=g_image.base+0x17DC620;cabinModel=L"v603_flak";
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!strcmp(cabinLabel,"kevlar-hatch"));
    for(int j=0;j<3;++j) VERIFY(std::fabs(cabinCamera.m[3][j]-(banked.m[3][j]+.55f*banked.m[0][j]+3.15f*banked.m[1][j]+1.25f*banked.m[2][j]))<.001f);
    VERIFY(!memcmp(cabinCamera.m,thirdPerson.m,48));
    // Remaining mapped tank gunner seats use their own turret, keeping model
    // memory and native camera orientation untouched. Wrong count/bone/owner
    // continues to use the original passenger view.
    for(int kind=0;kind<2;++kind) {
        At<void*>(cabinVehicle,0)=g_image.base+(kind?0x17D9458:0x17D8FA0);
        VERIFY(edf6vr::HasType(g_image,cabinVehicle,kind?".?AVVehicle404_Tank@@":".?AVVehicle403_Tank@@"));
        cabinModel=kind?L"Vehicle404_bigtank":L"Vehicle403_tank";
        At<std::uint64_t>(cabinVehicle,0x618)=3;cabin.cameraOwner=cabinVehicle;
        for(int index=1;index<=2;++index) {
            cabin.seat=cabinSeats+index*0x340;
            cabinPositionBone=kind?(index==1?L"subCannon_A_roll":L"subCannon_B_roll")
                :(index==1?L"MachineGun_A_roll":L"MachineGun_B_roll");
            At<edf6vr::Matrix>(cabinPoseNode,0xB0)=banked;cabinCamera=thirdPerson;
            memcpy(vehicleBefore,cabinVehicle,sizeof cabinVehicle);memcpy(bonesBefore,cabinBones,sizeof cabinBones);
            VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            for(int j=0;j<3;++j) VERIFY(std::fabs(cabinCamera.m[3][j]-(banked.m[3][j]
                +(kind?1.8f:.9f)*banked.m[1][j]-(kind?.8f:.4f)*banked.m[2][j]))<.001f);
            VERIFY(!memcmp(cabinCamera.m,thirdPerson.m,48));
            VERIFY(!memcmp(vehicleBefore,cabinVehicle,sizeof cabinVehicle) && !memcmp(bonesBefore,cabinBones,sizeof cabinBones));
            const auto priorCamera=cabinCamera;
            At<edf6vr::Matrix>(cabinPoseNode,0xB0).m[3][0]+=2;
            VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            VERIFY(std::fabs(cabinCamera.m[3][0]-priorCamera.m[3][0]-2)<.001f);
            const auto bone=cabinPositionBone;cabinPositionBone=L"missing";cabinCamera=thirdPerson;
            VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));cabinPositionBone=bone;
            At<std::uint64_t>(cabinVehicle,0x618)=4;
            VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            At<std::uint64_t>(cabinVehicle,0x618)=3;cabin.cameraOwner=soldier;
            VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
            cabin.cameraOwner=cabinVehicle;
        }
    }
    // Special BGP kei truck: driver alias only. Four rear seats remain native.
    cabinModel=L"v512_keiTruck_bgp";At<std::uint64_t>(cabinVehicle,0x618)=5;
    cabin.seat=cabinSeats;cabin.cameraOwner=cabinVehicle;
    VERIFY(edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
    VERIFY(!strcmp(cabinLabel,"kei-driver"));
    for(int index=1;index<=4;++index) {
        cabin.seat=cabinSeats+index*0x340;cabinCamera=thirdPerson;
        VERIFY(!edf6vr::PlaceVehicleAnchor(g_image,cabin,FakeCabinLookup,cabinCamera,cabinLabel,cabinIndex));
        VERIFY(!memcmp(&cabinCamera,&thirdPerson,sizeof cabinCamera));
    }
    // Mission start allowance: all owned weapons, including inactive requests.
    // Only initial reload debt is reduced; native completion remains responsible
    // for ammo. Originally-ready calls and subsequent reloads receive no gift.
    VERIFY(edf6vr::CheckRequestPointProfile(g_image));
    alignas(16) unsigned char manager[0x40]{},scores[7*0x38]{};
    auto global=reinterpret_cast<void**>(g_image.base+0x20B2978);void* prior=*global;*global=manager;
    At<void*>(manager,0x38)=scores;At<unsigned>(soldier,0x314)=0;
    void* owned[2]={weapons[0],weapons[1]};
    At<void*>(soldier,0x1950)=owned;At<std::uint64_t>(soldier,0x1960)=2;
    // Deliberately exclude the high-cost request from the active-slot array.
    At<unsigned>(soldier,0x1980)=1;
    for(unsigned i=0;i<2;++i) {
        At<void*>(weapons[i],0)=g_image.base+0x17E4FB0; // Weapon_RadioContact
        At<void*>(weapons[i],0x120)=soldier;At<int>(weapons[i],0x208)=2;
        At<int>(weapons[i],0x248)=1;At<int>(weapons[i],0x20C)=50000;
        At<int>(weapons[i],0xE68)=50000;At<float>(weapons[i],0xE80)=0;
    }
    At<int>(weapons[0],0xBE8)=1; // Freeger-style initially ready call.
    edf6vr::RequestPointBudget budget{};bool allocated=false;
    unsigned char weaponBefore[sizeof weapons];memcpy(weaponBefore,weapons,sizeof weapons);
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,100000,budget,allocated)==50000);
    VERIFY(allocated && budget.applied && budget.remaining==50000 && budget.credited==1 && budget.alreadyReady==1);
    VERIFY(At<int>(weapons[1],0xE68)==0 && At<int>(weapons[1],0xBE8)==0); // Completion not emulated by MOD.
    At<int>(weaponBefore+sizeof weapons[0],0xE68)=0;
    VERIFY(!memcmp(weaponBefore,weapons,sizeof weapons)); // Exactly the pending-debt field changed.
    for(unsigned i=0;i<sizeof scores;++i) VERIFY(scores[i]==0); // ALL players' ledgers unchanged.
    // Simulate native completion, use, and reload. Neither initial ready nor
    // newly credited request may be refilled, including after weapon switching.
    At<int>(weapons[0],0xBE8)=0;At<int>(weapons[1],0xE68)=50000;
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,100000,budget,allocated)==0 && !allocated);
    VERIFY(At<int>(weapons[0],0xE68)==50000 && At<int>(weapons[1],0xE68)==50000);
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,0,budget,allocated)==0);
    At<unsigned>(soldier,0x314)=7;
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,100000,budget,allocated)==0);
    At<unsigned>(soldier,0x314)=0;
    // Normal credits dropping on mission restart re-arm the initial allowance.
    At<float>(scores,0x30)=10;edf6vr::GrantInitialRequestPoints(g_image,soldier,100000,budget,allocated);
    At<float>(scores,0x30)=0;
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,100000,budget,allocated)==100000 && allocated);
    VERIFY(budget.remaining==0);
    // Budget cap, wrong-owner exclusion, malformed list, and default-off.
    budget={};At<int>(weapons[0],0xE68)=50000;At<int>(weapons[1],0xE68)=50000;
    At<void*>(weapons[0],0x120)=otherModel;
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,0,budget,allocated)==0 && !budget.applied);
    At<std::uint64_t>(soldier,0x1960)=33;
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,100000,budget,allocated)==0 && !budget.applied);
    At<std::uint64_t>(soldier,0x1960)=2;
    VERIFY(edf6vr::GrantInitialRequestPoints(g_image,soldier,1000,budget,allocated)==1000 && budget.remaining==0);
    VERIFY(At<int>(weapons[0],0xE68)==50000 && At<int>(weapons[1],0xE68)==49000);
    // A camera slot must outlive its camera only as long as the game keeps
    // asking for it. The mission end destroys the camera without ever coming
    // back for its restore, so with one camera per mission the eight slots ran
    // out and the ninth mission of a run got none: first person was simply
    // never applied again.
    {
        for(auto& entry:g_states) entry={};
        void* cameras[9]{};
        for(int i=0;i<9;++i) cameras[i]=reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x20000+i*0x100));
        for(int i=0;i<8;++i) {
            auto slot=State(cameras[i]);
            VERIFY(slot && slot->camera==cameras[i]);
            if(slot) slot->saved.pending=true;   // written this update, not yet put back
        }
        // All eight were updated this instant, so none of them is dead and the
        // ninth camera must not be given one of their slots.
        VERIFY(!State(cameras[8]));
        // However full the table is, a camera still finds its own entry.
        VERIFY(State(cameras[3])==&g_states[3]);
        // Age them past the threshold: the game has stopped updating them.
        const auto now=GetTickCount64();
        for(auto& entry:g_states) entry.seen=now-kCameraSlotStaleMs-1;
        g_states[5].seen=now-kCameraSlotStaleMs-9000;   // unused the longest
        auto ninth=State(cameras[8]);
        VERIFY(ninth==&g_states[5]);
        // A fresh entry, not the dead camera's leftovers.
        VERIFY(ninth && ninth->camera==cameras[8] && !ninth->saved.pending && ninth->seen>=now);
        for(auto& entry:g_states) entry={};
    }
    *global=prior;
    FreeLibrary(mapped);
    printf("FPS production hooks: %d failures. 1000 cycles, owner isolation, bone position, restoration; NO EDF CODE EXECUTED.\n",failures);
    return failures?1:0;
}
