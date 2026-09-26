#include "../src/plugin.cpp"
#include <limits>
static int failures=0;
#define CHECK(x) do {if(!(x)) {printf("FAIL %d: %s\n",__LINE__,#x);++failures;}} while(false)
template<class T> T& Field(void* p,std::size_t n) {return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+n);}
static edf6vr::Matrix seen{};
static const edf6vr::Matrix* received=nullptr;
static unsigned calls=0;
static unsigned boundsCalls=0;
static edf6vr::Matrix boundsSeen{};
static const edf6vr::Matrix* boundsSource=nullptr;
static void __fastcall Bounds(void*,const edf6vr::Matrix* matrix) {
    ++boundsCalls; boundsSource=matrix; boundsSeen=*matrix;
}
static unsigned efsCreateCalls=0;
static bool throwEfsCreate=false;
static edf6vr::Matrix efsCreated{};
static void* __fastcall EfsCreate(void* group,void* output,void* parameter) {
    ++efsCreateCalls;efsCreated=*static_cast<edf6vr::Matrix*>(parameter);
    CHECK(group && output && Field<unsigned>(parameter,64)==0x12345678);
    CHECK(TryAcquireSRWLockExclusive(&g_classLock));ReleaseSRWLockExclusive(&g_classLock);
    CHECK(TryAcquireSRWLockExclusive(&g_dualLock));ReleaseSRWLockExclusive(&g_dualLock);
    if(throwEfsCreate)RaiseException(0xE0423625,0,0,nullptr);
    *static_cast<unsigned*>(output)=123;
    return output;
}
static void CheckEfsException(void* group,void* parameter) {
    unsigned output=0;bool caught=false;throwEfsCreate=true;
    __try {HookEfsFlashCreate(group,&output,parameter);}
    __except(GetExceptionCode()==0xE0423625?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {caught=true;}
    throwEfsCreate=false;CHECK(caught && output==0);
}
static void* __fastcall EfsLocalOffset(void*,edf6vr::Matrix* output,const edf6vr::Matrix* source) {
    *output=*source;
    for(unsigned j=0;j<3;++j)output->m[3][j]+=.5f*source->m[0][j];
    return output;
}
static void __fastcall Draw(void* renderer,void* context,const edf6vr::Matrix* matrix,void* mesh,
                           unsigned flags,bool option,float value,void* extra) {
    ++calls; received=matrix; if(matrix) seen=*matrix;
    CHECK(renderer==reinterpret_cast<void*>(1) && context==reinterpret_cast<void*>(2));
    CHECK(mesh==reinterpret_cast<void*>(3) && flags==7 && option && value==0.25f && extra==reinterpret_cast<void*>(4));
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[256]{};
    if(!mapped || !edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason))) return 2;
    CHECK(CheckMuzzleFlashProfile());
    alignas(16) unsigned char effect[0x300]{},weapon[0x1600]{},soldier[0x2100]{};
    Field<void*>(soldier,0)=g_image.base+0x17CDF28;
    Field<unsigned>(soldier,0x314)=123;
    Field<void*>(weapon,0x120)=soldier;
    Field<void*>(effect,0)=g_image.base+0x17A6478;
    Field<void*>(effect,0x38)=weapon;
    const edf6vr::Matrix native={{{0,2,0,0},{-3,0,0,0},{0,0,4,0},{100,200,300,1}}};
    Field<edf6vr::Matrix>(effect,0x60)=native;
    auto source=reinterpret_cast<const edf6vr::Matrix*>(effect+0x60);
    g_originalFlashMesh=&Draw;
    const float p[3]={100,200,300};
    const auto now=GetTickCount64();
    PublishMuzzleFrame(weapon,soldier,123,p,p,now);
    auto draw=[&] {
        const auto before=calls;
        // Same lock nesting as live update: no re-entry of g_lock.
        AcquireSRWLockExclusive(&g_lock);
        HookFlashMesh(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),source,
                      reinterpret_cast<void*>(3),7,true,0.25f,reinterpret_cast<void*>(4));
        ReleaseSRWLockExclusive(&g_lock);
        CHECK(calls==before+1);
        CHECK(!std::memcmp(source,&native,sizeof(native)));
    };
    for(const auto vt:{0x17A6478u,0x17A6578u}) {
        Field<void*>(effect,0)=g_image.base+vt;
        for(unsigned n=0;n<30;++n) {
            draw();CHECK(received!=source);
            for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j)
                CHECK(std::fabs(seen.m[i][j]-native.m[i][j]*(i<3&&j<3?0.5f:1.0f))<0.0001f);
        }
    }
    // Exact native passthrough for NPC/unrelated effects and invalid/stale ownership.
    Field<void*>(effect,0x38)=soldier;draw();CHECK(received==source);
    Field<void*>(effect,0x38)=weapon;
    Field<unsigned>(soldier,0x314)=124;draw();CHECK(received==source);
    Field<unsigned>(soldier,0x314)=123;
    Field<void*>(weapon,0x120)=nullptr;draw();CHECK(received==source);
    Field<void*>(weapon,0x120)=soldier;
    // Wing Diver, Air Raider and Fencer are shrunk too. In VR the flash is
    // close to the eye whatever the stance, which is what the Fencer's
    // gatling showed after it had been left at native size.
    for(const auto vt:{0x17D0FF8u,0x17CF100u,0x17CF5B8u}) {
        Field<void*>(soldier,0)=g_image.base+vt;draw();CHECK(received!=source);
    }
    // Anything that is not a player class still passes through untouched.
    Field<void*>(soldier,0)=g_image.base+0x17A6478;draw();CHECK(received==source);
    Field<void*>(soldier,0)=g_image.base+0x17CDF28;
    Field<void*>(effect,0)=g_image.base+0x17CDF28;draw();CHECK(received==source);
    Field<void*>(effect,0)=g_image.base+0x17A6478;
    edf6vr::Matrix copy{};
    CHECK(!PrepareMuzzleFlash(source,copy,now+251));
    CHECK(!PrepareMuzzleFlash(source,copy,now-1));
    CHECK(!PrepareMuzzleFlash(reinterpret_cast<const edf6vr::Matrix*>(1),copy,now));
    for(float value:{1.0f,0.0f,2.0f,std::numeric_limits<float>::quiet_NaN()}) {
        g_muzzleFlashScale=value;draw();CHECK(received==source);
    }
    g_muzzleFlashScale=.7f;g_nativeTactical=true;draw();CHECK(received==source);g_nativeTactical=false;
    Field<float>(effect,0x60)=std::numeric_limits<float>::quiet_NaN();
    CHECK(!PrepareMuzzleFlash(source,copy,now));
    Field<edf6vr::Matrix>(effect,0x60)=native;
    ClearClassPresentation();draw();CHECK(received==source);
    // Exercise production live reload, including native-size comparison and bad input.
    wchar_t temp[MAX_PATH]{},ini[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH,temp)>0 && GetTempFileNameW(temp,L"efm",0,ini)!=0);
    wcscpy_s(g_iniPath,ini);
    CHECK(WritePrivateProfileStringW(L"Render",L"MuzzleFlashScale",L"1.0",ini));
    ReloadTunables();CHECK(g_muzzleFlashScale.load()==1.0f);
    CHECK(WritePrivateProfileStringW(L"Render",L"MuzzleFlashScale",L"0.7",ini));
    ReloadTunables();CHECK(g_muzzleFlashScale.load()==0.7f);
    CHECK(WritePrivateProfileStringW(L"Render",L"MuzzleFlashScale",L"nan",ini));
    ReloadTunables();CHECK(g_muzzleFlashScale.load()==0.7f);
    g_iniPath[0]=0;CHECK(DeleteFileW(ini));
    // Reproduce delayed flash preparation: firing's tracked attachment has
    // already been restored. Native copies it to +60 and submits its bounds.
    alignas(16) unsigned char right[0x1600]{},slots[0x150]{},nodes[0x110]{};
    void* owned[]={weapon,right};
    Field<void*>(soldier,0x1950)=owned;Field<unsigned>(soldier,0x1960)=2;
    Field<void*>(soldier,0x1970)=slots;Field<unsigned>(soldier,0x1980)=1;
    Field<void*>(slots,0x40)=owned+1;
    Field<void*>(right,0x120)=soldier;
    Field<void*>(weapon,edf6vr::kWeaponModelOffset+0xB0)=nodes;
    Field<std::uint64_t>(weapon,edf6vr::kWeaponModelOffset+0xC0)=1;
    const edf6vr::Matrix root={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{10,20,30,1}}};
    Field<edf6vr::Matrix>(soldier,0x60)=root;
    Field<edf6vr::Matrix>(nodes,0xB0)=root;
    alignas(16) edf6vr::Matrix attachment=root;
    attachment.m[3][0]+=.2f;attachment.m[3][1]+=.1f;attachment.m[3][2]+=.3f;
    attachment.m[0][0]=2;attachment.m[1][1]=3;attachment.m[2][2]=4;
    const auto savedAttachment=attachment;
    Field<const edf6vr::Matrix*>(effect,0x120)=&attachment;
    g_dualState={};g_dualState.soldier=soldier;g_dualState.id=123;
    g_dualState.weapons[0]=weapon;g_dualState.weapons[1]=right;
    g_dualWeapons[0]=weapon;g_dualWeapons[1]=right;g_dualActive=true;
    WeaponHoldCommand command{};
    command.handIndex=0;command.weapon=weapon;command.soldier=soldier;
    command.objectId=123;command.weaponNodes=nodes;
    command.hand={{{0,1,0},{-1,0,0},{0,0,1}},{9,21,30}};
    for(unsigned j=0;j<3;++j)command.rootWorld[j]=root.m[3][j];
    auto publish=[&] {command.refreshed=GetTickCount64();PublishCasingFrame(command);};
    publish();CHECK(CurrentRangerPair(g_dualState,soldier));
    g_originalFlashBounds=&Bounds;
    auto prepare=[&] {
        Field<edf6vr::Matrix>(effect,0x60)=attachment;
        const auto before=boundsCalls;
        AcquireSRWLockExclusive(&g_lock);
        HookFlashBounds(effect+0x130,&attachment);
        ReleaseSRWLockExclusive(&g_lock);
        CHECK(boundsCalls==before+1);
        CHECK(!std::memcmp(&attachment,&savedAttachment,64));
        CHECK(!std::memcmp(nodes+0xB0,&root,64));
        CHECK(Field<const edf6vr::Matrix*>(effect,0x120)==&attachment);
    };
    for(const auto vt:{0x17A6478u,0x17A6578u}) {
        Field<void*>(effect,0)=g_image.base+vt;
        for(float scale:{.5f,1.0f}) {
            g_muzzleFlashScale=scale;
            for(unsigned n=0;n<30;++n) {
                publish();prepare();CHECK(boundsSource!=&attachment);
                CHECK(std::fabs(boundsSeen.m[3][0]-8.9f)<.0001f);
                CHECK(std::fabs(boundsSeen.m[3][1]-21.2f)<.0001f);
                CHECK(std::fabs(boundsSeen.m[3][2]-30.3f)<.0001f);
                CHECK(boundsSeen.m[0][1]==2 && boundsSeen.m[1][0]==-3 && boundsSeen.m[2][2]==4);
                CHECK(!std::memcmp(effect+0x60,&boundsSeen,64));
                PublishMuzzleFrame(weapon,soldier,123,p,p,GetTickCount64(),0);
                // Both eyes consume the same prepared matrix; scaling neither
                // moves the muzzle again nor accumulates in effect storage.
                for(unsigned eye=0;eye<2;++eye) {
                    HookFlashMesh(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),source,
                                  reinterpret_cast<void*>(3),7,true,.25f,reinterpret_cast<void*>(4));
                    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j)
                        CHECK(std::fabs(seen.m[i][j]-boundsSeen.m[i][j]*(i<3&&j<3?scale:1))<.0001f);
                    CHECK(!std::memcmp(effect+0x60,&boundsSeen,64));
                }
            }
        }
    }
    Field<float>(soldier,0x94)+=4;publish();prepare();
    CHECK(std::fabs(boundsSeen.m[3][1]-25.2f)<.0001f);Field<float>(soldier,0x94)-=4;
    for(unsigned trial=0;trial<11;++trial) {
        publish();
        switch(trial) {
        case 0: g_dualActive=false;break;
        case 1: Field<void*>(effect,0x38)=right;break;
        case 2: Field<unsigned>(soldier,0x314)=124;break;
        case 3: Field<void*>(effect,0)=g_image.base+0x17A6068;break;
        case 4: g_nativeTactical=true;break;
        case 5: Field<void*>(weapon,edf6vr::kWeaponModelOffset+0xB0)=nullptr;break;
        case 6: command.refreshed=GetTickCount64()-1000;PublishCasingFrame(command);break;
        case 7: g_dualShot.active=true;g_dualTick.weapon=weapon;break;
        case 8: Field<void*>(slots,0x40)=owned;break;
        case 9: Field<float>(soldier,0x90)+=21;break;
        case 10: ClearClassPresentation();break;
        }
        prepare();CHECK(boundsSource==&attachment);CHECK(!std::memcmp(effect+0x60,&attachment,64));
        g_dualActive=true;Field<void*>(effect,0x38)=weapon;Field<unsigned>(soldier,0x314)=123;
        Field<void*>(effect,0)=g_image.base+0x17A6478;g_nativeTactical=false;
        Field<void*>(weapon,edf6vr::kWeaponModelOffset+0xB0)=nodes;g_dualShot={};g_dualTick={};
        Field<void*>(slots,0x40)=owned+1;Field<edf6vr::Matrix>(soldier,0x60)=root;
    }
    publish();
    CHECK(!PrepareLeftFlash(effect+0x130,&attachment,copy,command.refreshed-1));
    CHECK(!PrepareLeftFlash(reinterpret_cast<void*>(1),&attachment,copy,command.refreshed));
    attachment.m[0][0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!PrepareLeftFlash(effect+0x130,&attachment,copy,command.refreshed));
    attachment=savedAttachment;
    // EFS is a separate native child-effect route. Run its ACTUAL updater on
    // synthetic live references, including a native local-offset callback.
    Field<void*>(effect,0)=g_image.base+0x17A6068;
    alignas(16) unsigned char children[2][0xA0]{},entries[0xB0]{},references[2][0x10]{};
    void* offsetVtable[]={nullptr,nullptr,reinterpret_cast<void*>(&EfsLocalOffset)};
    void* offsetObject=offsetVtable;
    Field<void*>(effect,0x150)=entries;Field<std::uint64_t>(effect,0x160)=2;
    for(unsigned i=0;i<2;++i) {
        Field<void*>(entries,i*0x58)=children[i];Field<void*>(entries,i*0x58+8)=references[i];
        Field<unsigned>(references[i],8)=2;Field<unsigned>(references[i],12)=2;
    }
    Field<unsigned char>(entries,0x14)=1;
    Field<void*>(entries,0x58+0x50)=&offsetObject;
    g_originalEfsFlashUpdate=reinterpret_cast<FlashBounds>(g_image.base+kEfsFlashUpdateTarget);
    g_originalEfsFlashCreate=&EfsCreate;
    CHECK(!PrepareLeftFlash(effect+0x130,&attachment,copy,GetTickCount64())); // mesh route excludes EFS
    for(unsigned i=0;i<60;++i) {
        publish();g_muzzleFlashScale=i%2?.5f:1;
        AcquireSRWLockExclusive(&g_lock);
        HookEfsFlashUpdate(effect+0x130,&attachment);
        ReleaseSRWLockExclusive(&g_lock);
        const auto& child=Field<edf6vr::Matrix>(children[0],0x60);
        const auto& offset=Field<edf6vr::Matrix>(children[1],0x60);
        CHECK(std::fabs(child.m[3][0]-8.9f)<.0001f && std::fabs(child.m[3][1]-21.2f)<.0001f);
        CHECK(child.m[0][1]==2 && child.m[1][0]==-3 && child.m[3][3]==1);
        CHECK(std::fabs(offset.m[3][1]-22.2f)<.0001f); // native local offset retained
        CHECK(!std::memcmp(&attachment,&savedAttachment,64));
        CHECK(Field<const edf6vr::Matrix*>(effect,0x120)==&attachment);
        for(unsigned n=0;n<2;++n)CHECK(Field<unsigned>(references[n],8)==2);
    }
    alignas(16) unsigned char parameter[0xD0]{};
    Field<edf6vr::Matrix>(parameter,0)=attachment;Field<unsigned>(parameter,64)=0x12345678;
    unsigned output=0;
    publish();CHECK(HookEfsFlashCreate(effect+0x130,&output,parameter)==&output && output==123);
    CHECK(efsCreateCalls==1 && std::fabs(efsCreated.m[3][0]-8.9f)<.0001f);
    CHECK(!std::memcmp(parameter,&attachment,64) && Field<unsigned>(parameter,64)==0x12345678);
    CheckEfsException(effect+0x130,parameter);
    CHECK(efsCreateCalls==2 && !std::memcmp(parameter,&attachment,64));
    // While an effect is born inside an already-carried shot, pass through;
    // subsequent updates after restoration must move it left again.
    g_dualShot.active=true;g_dualTick.weapon=weapon;
    HookEfsFlashUpdate(effect+0x130,&attachment);
    CHECK(!std::memcmp(children[0]+0x60,&attachment,64));g_dualShot={};g_dualTick={};
    for(unsigned trial=0;trial<4;++trial) {
        publish();
        if(trial==0)Field<void*>(effect,0x38)=right;
        if(trial==1)g_dualActive=false;
        if(trial==2){command.refreshed=GetTickCount64()-1000;PublishCasingFrame(command);}
        if(trial==3)Field<unsigned>(soldier,0x314)=124;
        HookEfsFlashUpdate(effect+0x130,&attachment);
        CHECK(!std::memcmp(children[0]+0x60,&attachment,64));
        HookEfsFlashCreate(effect+0x130,&output,parameter);
        CHECK(!std::memcmp(&efsCreated,&attachment,64) && !std::memcmp(parameter,&attachment,64));
        Field<void*>(effect,0x38)=weapon;g_dualActive=true;Field<unsigned>(soldier,0x314)=123;
    }
    ResetRangerDual();ClearClassPresentation();
    // Verify real call-site patching and reject a second/competing installation.
    bool changed=false;
    CHECK(InstallMuzzleFlashScale(changed) && changed);
    CHECK(!CheckMuzzleFlashProfile());
    changed=false;CHECK(!InstallMuzzleFlashScale(changed) && !changed);
    printf("Muzzle flash: %d failures; mesh both-eye pose/bounds, EFS native child updates and offsets, creation/exception restoration, owner isolation and real profile verified.\n",failures);
    return failures?1:0;
}
