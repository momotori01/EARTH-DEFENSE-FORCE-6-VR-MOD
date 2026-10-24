#include "../src/plugin.cpp"
#include <limits>
static int errors=0,draws=0,queries=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++errors; } } while(false)
template<class T> static T& Field(void* p,std::size_t off) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+off); }
static float seenStart[4]{},seenEnd[4]{};
static void* seenContext=nullptr;
static bool seenActive=false; static int seenMode=0;
static void __fastcall FakeBooster(void*) { ++draws; }
// The Booster update as the game's: it reads the matrix +3D8 points at.
static int updates=0; static const float* seenLive=nullptr; static float seenMatrix[16]{};
static void __fastcall FakeBoosterUpdate(void* effect,void*) {
    ++updates; seenLive=*reinterpret_cast<const float* const*>(static_cast<unsigned char*>(effect)+0x3D8);
    if(seenLive) for(int k=0;k<16;++k) seenMatrix[k]=seenLive[k];   // the Wing Diver's test effect points nowhere
}
alignas(16) static unsigned char boneNodes[0x200]{};
// The streamer emit as the game's: what place and way it was handed.
static int emits=0; static float seenPlace[4]{},seenWay[4]{};
static void __fastcall FakeStreamerEmit(void*,const float* place,const float* way,const float*) {
    ++emits; for(int j=0;j<4;++j) { seenPlace[j]=place[j]; seenWay[j]=way[j]; }
}
static void* __fastcall FakeBoneLookup(void*,const void* name) {
    return !wcscmp(static_cast<const wchar_t*>(name),L"mune")?boneNodes:nullptr;
}
static void __fastcall FakeMarker(void*,void* context,const float* a,const float* b,bool active,int mode) {
    ++queries; memcpy(seenStart,a,16); memcpy(seenEnd,b,16);
    seenContext=context; seenActive=active; seenMode=mode;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    auto image=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[256]{};
    if(!image || !edf6vr::CheckImage(image,g_image,reason,sizeof(reason))) return 2;
    CHECK(InstallClassEffects());
    g_originalBooster=FakeBooster; g_originalMarker=FakeMarker;
    alignas(16) unsigned char soldier[0x2100]{},npc[0x2100]{},effect[0x3F0]{},weapon[0x2000]{};
    Field<void*>(soldier,0)=g_image.base+0x17D0FF8; Field<unsigned>(soldier,0x314)=42;
    Field<void*>(effect,0)=g_image.base+0x17A6D58; Field<void*>(effect,0x3D0)=soldier;
    Field<void*>(npc,0)=g_image.base+0x17D0FF8;
    const auto now=GetTickCount64();
    PublishBoosterOwner(soldier,42,now);
    auto booster=*reinterpret_cast<BoosterDraw*>(g_image.base+0x17A6D70);
    CHECK(*reinterpret_cast<void**>(g_image.base+0x17A6D80)==reinterpret_cast<void*>(&HookBoosterUpdate)); // the update: the Fencer's held on his back
    CHECK(*reinterpret_cast<void**>(g_image.base+0x17A8D20)==g_image.base+0x2FE5D0); // unused old class untouched
    Field<void*>(effect,0)=g_image.base+0x17A8D08;
    CHECK(!HideOwnBooster(effect,now));
    Field<void*>(effect,0)=g_image.base+0x17A6D58;
    booster(effect); CHECK(draws==0 && g_boosterHidden==1);
    Field<void*>(effect,0x3D0)=npc; booster(effect); CHECK(draws==1);
    Field<void*>(effect,0x3D0)=soldier; Field<unsigned>(soldier,0x314)=43;
    booster(effect); CHECK(draws==2); Field<unsigned>(soldier,0x314)=42;
    CHECK(!HideOwnBooster(effect,now+251));
    ClearClassPresentation(); booster(effect); CHECK(draws==3);

    // The Fencer's boosters: never hidden, held on the standing chest by the update (HookBoosterUpdate).
    {
        g_originalBoosterUpdate=FakeBoosterUpdate; g_nodeLookup=FakeBoneLookup;
        alignas(16) unsigned char heavy[0x2100]{},fencerEffect[0x3F0]{};
        Field<void*>(heavy,0)=g_image.base+0x17CF5B8; Field<unsigned>(heavy,0x314)=7;
        Field<void*>(fencerEffect,0)=g_image.base+0x17A6D58; Field<void*>(fencerEffect,0x3D0)=heavy;
        // The chest tipped well forward by a dash, and the attach point it carries 0.66 m out behind it.
        const float t=1.2f,ct=std::cos(t),st=std::sin(t);
        float chest[3][3]{},chestAt[3]={3,11,20};
        for(int k=0;k<3;++k) {
            const float* r=edf6vr::kFencerMuneRows[k];
            chest[k][0]=r[0]; chest[k][1]=r[1]*ct-r[2]*st; chest[k][2]=r[1]*st+r[2]*ct;
        }
        auto* bone=reinterpret_cast<float*>(boneNodes+0xB0);
        for(int k=0;k<3;++k) { for(int j=0;j<3;++j) bone[k*4+j]=chest[k][j]; bone[k*4+3]=0; }
        for(int j=0;j<3;++j) bone[12+j]=chestAt[j]; bone[15]=1;
        alignas(16) float attach[16]{};
        for(int k=0;k<3;++k) for(int j=0;j<3;++j) attach[k*4+j]=chest[k][j]*0.5f;
        for(int j=0;j<3;++j) attach[12+j]=chestAt[j]+0.08f*chest[0][j]+0.661f*chest[1][j]-0.158f*chest[2][j];
        attach[15]=1;
        Field<const float*>(fencerEffect,0x3D8)=attach;
        auto update=*reinterpret_cast<BoosterUpdate*>(g_image.base+0x17A6D80);
        // No frame published (third person, VR off): as the game has it.
        update(fencerEffect,nullptr);
        CHECK(updates==1 && seenLive==attach && Field<const float*>(fencerEffect,0x3D8)==attach && g_boosterHeld==0);
        const float body[3][3]={{1,0,0},{0,1,0},{0,0,1}},eye[3]={3,11.4f,20.03f};
        PublishFencerBoosterFrame(heavy,7,body,eye,GetTickCount64());
        update(fencerEffect,nullptr);
        CHECK(updates==2 && seenLive!=attach && g_boosterHeld==1);
        CHECK(Field<const float*>(fencerEffect,0x3D8)==attach);                 // put back after the call
        float live[4][4]{},expected[4][4]{};
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) live[i][j]=attach[i*4+j];
        CHECK(edf6vr::FencerBoosterHeld(live,chest,chestAt,body,eye,expected));
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(std::fabs(seenMatrix[i*4+j]-expected[i][j])<1e-5f);
        CHECK(seenMatrix[13]<eye[1] && seenMatrix[14]<eye[2]-0.5f && attach[13]>eye[1]);   // held behind and below; the game's above the eye
        // Another soldier's booster, a stale id, or the Wing Diver's: the game's.
        Field<unsigned>(heavy,0x314)=8; update(fencerEffect,nullptr);
        CHECK(updates==3 && seenLive==attach && g_boosterHeld==1);
        Field<unsigned>(heavy,0x314)=7;
        update(effect,nullptr); CHECK(updates==4 && g_boosterHeld==1);
        PublishFencerBoosterFrame(nullptr,0,body,eye,GetTickCount64());
        update(fencerEffect,nullptr); CHECK(updates==5 && seenLive==attach && g_boosterHeld==1);
        g_nodeLookup=nullptr;
    }

    // The Wing Diver's streamer sources held on her standing chest (HookStreamerEmit at 57FB24).
    {
        CHECK(*reinterpret_cast<void**>(g_image.base+0x17A8F20)==g_image.base+0x300B60);   // the draw untouched
        const auto site=g_image.base+0x57FB24;
        CHECK(site[0]==0xE8 && site+5+*reinterpret_cast<const std::int32_t*>(site+1)!=g_image.base+0x300780);   // redirected
        g_originalStreamerEmit=FakeStreamerEmit; g_nodeLookup=FakeBoneLookup;
        alignas(16) unsigned char diver[0x2100]{},streamer[0x400]{};
        Field<void*>(diver,0)=g_image.base+0x17D0FF8; Field<unsigned>(diver,0x314)=9;
        Field<void*>(streamer,0)=g_image.base+0x17A8F08; Field<void*>(streamer,0x3D0)=diver;
        // Her chest leant 60 degrees forward in flight; a source 0.3 m behind it, thrown up and ahead.
        const float t=1.05f,ct=std::cos(t),st=std::sin(t);
        float chest[3][3]{},chestAt[3]={4,12,30};
        for(int k=0;k<3;++k) {
            const float* r=edf6vr::kDiverMuneRows[k];
            chest[k][0]=r[0]; chest[k][1]=r[1]*ct-r[2]*st; chest[k][2]=r[1]*st+r[2]*ct;
        }
        auto* bone=reinterpret_cast<float*>(boneNodes+0xB0);
        for(int k=0;k<3;++k) { for(int j=0;j<3;++j) bone[k*4+j]=chest[k][j]; bone[k*4+3]=0; }
        for(int j=0;j<3;++j) bone[12+j]=chestAt[j]; bone[15]=1;
        alignas(16) float place[4]{},way[4]{0,0,-1,0};
        for(int j=0;j<3;++j) place[j]=chestAt[j]+0.25f*chest[0][j]+0.30f*chest[1][j]+0.3f*chest[2][j];
        place[3]=1;
        const float velocity[4]{0,0,8,0};
        const float body[3][3]={{1,0,0},{0,1,0},{0,0,1}},eye[3]={4,12.35f,29.96f};
        // No frame: as the game hands it over.
        PublishFencerBoosterFrame(nullptr,0,body,eye,GetTickCount64());
        HookStreamerEmit(streamer,place,way,velocity);
        CHECK(emits==1 && seenPlace[2]==place[2] && g_streamerHeld==0);
        PublishFencerBoosterFrame(diver,9,body,eye,GetTickCount64());
        HookStreamerEmit(streamer,place,way,velocity);
        float expectedPlace[4]{},expectedWay[4]{};
        CHECK(edf6vr::DiverStreamerHeld(place,way,chest,chestAt,body,eye,expectedPlace,expectedWay));
        CHECK(emits==2 && g_streamerHeld==1);
        for(int j=0;j<4;++j) CHECK(std::fabs(seenPlace[j]-expectedPlace[j])<1e-5f && std::fabs(seenWay[j]-expectedWay[j])<1e-5f);
        CHECK(place[2]>eye[2] && seenPlace[2]<eye[2]-0.1f);                   // the game's ahead of the eye; held behind it
        CHECK(seenPlace[3]==1 && std::fabs(seenWay[0]*seenWay[0]+seenWay[1]*seenWay[1]+seenWay[2]*seenWay[2]-1)<1e-4f);
        // Another soldier's streamer, a stale id, or the Fencer published: as the game hands it over.
        Field<unsigned>(diver,0x314)=10; HookStreamerEmit(streamer,place,way,velocity);
        CHECK(emits==3 && seenPlace[2]==place[2] && g_streamerHeld==1);
        Field<unsigned>(diver,0x314)=9;
        alignas(16) unsigned char heavy[0x2100]{};
        Field<void*>(heavy,0)=g_image.base+0x17CF5B8; Field<unsigned>(heavy,0x314)=9;
        PublishFencerBoosterFrame(heavy,9,body,eye,GetTickCount64());
        HookStreamerEmit(streamer,place,way,velocity); CHECK(emits==4 && g_streamerHeld==1);
        PublishFencerBoosterFrame(nullptr,0,body,eye,GetTickCount64());
        g_nodeLookup=nullptr;
    }

    // Translation compensation across multiple missing renders, including flight.
    const float reference[3]={100,200,300},muzzle[3]={101,201,301};
    float current[3]{};
    PublishMuzzleFrame(weapon,soldier,42,muzzle,reference,now);
    for(int frame=0;frame<20;++frame) {
        const float delta[3]={frame*.1f,frame*.3f,frame*-.2f};
        for(int j=0;j<3;++j) Field<float>(soldier,0x90+j*4)=reference[j]+delta[j];
        CHECK(ResolveTrackedMuzzle(weapon,current,now+frame*5));
        for(int j=0;j<3;++j) CHECK(std::fabs(current[j]-(muzzle[j]+delta[j]))<.0001f);
    }
    CHECK(!ResolveTrackedMuzzle(weapon+8,current,now));
    CHECK(!ResolveTrackedMuzzle(weapon,current,now+251));
    Field<unsigned>(soldier,0x314)=43; CHECK(!ResolveTrackedMuzzle(weapon,current,now));
    Field<unsigned>(soldier,0x314)=42;
    Field<float>(soldier,0x90)=1000; CHECK(!ResolveTrackedMuzzle(weapon,current,now));
    for(int j=0;j<3;++j) Field<float>(soldier,0x90+j*4)=reference[j];
    auto common=weapon+0x1570; Field<void*>(common,0)=g_image.base+0x17E45C8;
    alignas(16) float start[4]={100,200,300,1},end[4]={100,200,400,1};
    const auto originalStart=std::array<float,4>{100,200,300,1};
    // Execute both installed near bridges against fake native callees; verifies
    // all 6 x64 arguments including stack bool/mode. Never executes game code.
    for(const auto site:{0x6A3D0Eu,0x6A3DFBu}) {
        PublishMuzzleFrame(weapon,soldier,42,muzzle,reference,GetTickCount64());
        auto bridge=reinterpret_cast<MarkerUpdate>(g_image.base+site+5+*reinterpret_cast<std::int32_t*>(g_image.base+site+1));
        bridge(common,effect,start,end,true,7);
        CHECK(seenContext==effect && seenActive && seenMode==7);
        CHECK(seenStart[0]==101 && seenStart[1]==201 && seenEnd[2]==401);
        CHECK(seenEnd[2]-seenStart[2]==100 && seenStart[3]==1 && seenEnd[3]==1);
        CHECK(!memcmp(start,originalStart.data(),16));
        g_nativeTactical=true; bridge(common,effect,start,end,false,2); g_nativeTactical=false;
        CHECK(seenStart[0]==100 && !seenActive && seenMode==2);
    }
    // Drone's Common is separately allocated; owner cannot be common-1570.
    alignas(16) unsigned char droneCommon[0x400]{},foreignCommon[0x400]{};
    Field<void*>(droneCommon,0)=Field<void*>(foreignCommon,0)=g_image.base+0x17E45C8;
    Field<void*>(weapon,0)=g_image.base+0x17E1F00;
    CHECK(edf6vr::HasType(g_image,weapon,".?AVWeapon_Drone_LaserMarker@@"));
    Field<void*>(weapon,0x1570)=droneCommon;
    auto droneBridge=reinterpret_cast<MarkerUpdate>(g_image.base+0x684276+*reinterpret_cast<std::int32_t*>(g_image.base+0x684272));
    PublishMuzzleFrame(weapon,soldier,42,muzzle,reference,GetTickCount64());
    droneBridge(droneCommon,effect,start,end,true,3);
    CHECK(seenStart[0]==101 && seenStart[1]==201 && seenEnd[2]==401);
    CHECK(seenContext==effect && seenActive && seenMode==3 && seenStart[3]==1);
    CHECK(!memcmp(start,originalStart.data(),16));
    droneBridge(foreignCommon,effect,start,end,false,4);
    CHECK(seenStart[0]==100 && seenEnd[2]==400 && !seenActive && seenMode==4);
    Field<void*>(weapon,0x1570)=nullptr;droneBridge(droneCommon,effect,start,end,true,0);
    CHECK(seenStart[0]==100);
    Field<void*>(weapon,0x1570)=droneCommon;
    Field<void*>(weapon,0)=g_image.base+0x17E4FB0; // a different weapon type must not use pointer semantics
    droneBridge(droneCommon,effect,start,end,true,0);CHECK(seenStart[0]==100);
    Field<void*>(weapon,0)=g_image.base+0x17E1F00;
    PublishMuzzleFrame(weapon,soldier,42,muzzle,reference,GetTickCount64()-251);
    droneBridge(droneCommon,effect,start,end,true,0);CHECK(seenStart[0]==100);
    ClearClassPresentation();droneBridge(droneCommon,effect,start,end,true,0);CHECK(seenStart[0]==100);
    // The soldier's root is at (100,200,300); a body model to measure against
    // is part of being on your feet, and an ant's mouth takes it away.
    Field<void*>(soldier,edf6vr::kBodyModelOffset)=g_image.base+edf6vr::kModelVtableRva;
    edf6vr::Matrix camera={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{100,240,300,1}}};
    CHECK(!IsNativeTacticalView(soldier,camera,false)); // flying Diver never becomes tactical
    Field<void*>(soldier,0)=g_image.base+0x17CF100;
    CHECK(IsNativeTacticalView(soldier,camera,false));
    camera.m[3][1]=208; CHECK(!IsNativeTacticalView(soldier,camera,false)); CHECK(IsNativeTacticalView(soldier,camera,true));
    camera.m[3][1]=202; CHECK(!IsNativeTacticalView(soldier,camera,true));
    // Carried off and thrown by an ant: far enough away to pass the distance
    // on its own, but the camera trails level with him or below rather than
    // looking down from above. Both of these came off hardware.
    camera.m[3][1]=201.7f; camera.m[3][2]=340;
    CHECK(!IsNativeTacticalView(soldier,camera,false));
    camera.m[3][1]=195; CHECK(!IsNativeTacticalView(soldier,camera,false));
    // Height only decides going in, so a targeting view that dips on its way
    // out still ends on distance alone.
    CHECK(IsNativeTacticalView(soldier,camera,true));
    float distance=0,height=0;
    IsNativeTacticalView(soldier,camera,true,&distance,&height);
    CHECK(std::fabs(distance-std::sqrt(1625.0f))<.01f && std::fabs(height+5)<.01f);
    // Held in the mouth the body model goes away. Keep whatever the view was
    // rather than decide from a soldier that cannot be measured.
    Field<void*>(soldier,edf6vr::kBodyModelOffset)=nullptr;
    camera.m[3][1]=240; camera.m[3][2]=300;
    CHECK(!IsNativeTacticalView(soldier,camera,false));
    CHECK(IsNativeTacticalView(soldier,camera,true));
    Field<void*>(soldier,edf6vr::kBodyModelOffset)=g_image.base+edf6vr::kModelVtableRva;

    // The Fencer's weapon is laid on the hand's own frame, the way the
    // Ranger's is. Its up is the controller's, carried onto the aim.
    {
        // The hand's up at the aim is the controller's, wherever it points:
        // level, straight up with its top toward the back, and on over.
        g_yawOffset=0;
        float up[3]{};
        const float level[3]={0,0,1};
        CHECK(FencerHandUp(edf6vr::Quat{},level,up));
        CHECK(std::fabs(up[1]-1)<1e-4f);
        const float half=std::sqrt(0.5f);
        const float sky[3]={0,1,0};
        CHECK(FencerHandUp(edf6vr::Quat{half,0,0,half},sky,up));
        CHECK(std::fabs(up[2]+1)<1e-4f);
        float last[3]{}; bool have=false;
        for(float pitch=1.3f;pitch<1.85f;pitch+=0.01f) {
            const edf6vr::Quat q{std::sin(pitch*0.5f),0,0,std::cos(pitch*0.5f)};
            const float aim[3]={0,std::sin(pitch),std::cos(pitch)};
            CHECK(FencerHandUp(q,aim,up));
            if(have) CHECK(std::fabs(up[0]-last[0])+std::fabs(up[1]-last[1])+std::fabs(up[2]-last[2])<0.05f);
            for(int j=0;j<3;++j) last[j]=up[j];
            have=true;
        }
    }
    // A vehicle part's kick: turned about its pivot, the muzzle end up and the
    // whole back along the shot. The pivot bone itself only goes back.
    {
        const float pivot[3]={1,6,0},aim[3]={0,0,1};
        edf6vr::Matrix at{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{1,6,0,1}}};
        VehicleRecoilTurn(at,pivot,aim,0.1f,0.2f);
        CHECK(std::fabs(at.m[3][0]-1)<1e-5f && std::fabs(at.m[3][1]-6)<1e-5f && std::fabs(at.m[3][2]+0.2f)<1e-5f);
        edf6vr::Matrix hand{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{1,6,2.5f,1}}};
        VehicleRecoilTurn(hand,pivot,aim,0.1f,0.0f);
        CHECK(hand.m[3][1]>6.2f && hand.m[3][2]<2.5f);               // tipped up about the shoulder
        CHECK(std::fabs(hand.m[2][1]-std::sin(0.1f))<1e-4f);         // its forward row turned with it
        const float lz=hand.m[2][0]*hand.m[2][0]+hand.m[2][1]*hand.m[2][1]+hand.m[2][2]*hand.m[2][2];
        CHECK(std::fabs(lz-1)<1e-4f);
        edf6vr::Matrix up{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{1,8,0,1}}};
        const float sky[3]={0,1,0};
        const edf6vr::Matrix before=up;
        VehicleRecoilTurn(up,pivot,sky,0.1f,0.2f);                   // no up to tip towards: untouched
        CHECK(!std::memcmp(&up,&before,sizeof(up)));
    }
    ClearClassPresentation(); CHECK(!ResolveTrackedMuzzle(weapon,current,GetTickCount64()));
    FreeLibrary(image);
    printf("Class effects/flight/tactical: %d failures, queries=%d; synthetic objects, no game code\n",errors,queries);
    return errors?1:0;
}
