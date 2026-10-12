#include "../src/plugin.cpp"
#include <limits>
static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL %d: %s\n",__LINE__,#x); ++failures; } } while(false)
template<class T> static T& Field(void* p,std::size_t offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+offset); }
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    auto mapped=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    char reason[256]{};
    if(!mapped || !edf6vr::CheckImage(mapped,g_image,reason,sizeof(reason))) return 2;
    alignas(16) unsigned char camera[0xC00]{},soldier[0x2100]{},entries[3*0x150]{},weapon[0x1600]{},transform[0x100]{},bullet[0x1100]{};
    void* wrapper=weapon;
    Field<void*>(soldier,0)=g_image.base+0x17CDF28;
    Field<void*>(camera,edf6vr::kSourceOffset)=soldier; Field<void*>(camera,edf6vr::kSoldierOffset)=soldier;
    Field<void*>(soldier,0x1970)=entries; Field<unsigned>(soldier,0x1980)=1;
    Field<void*>(entries,0x40)=&wrapper; Field<void*>(weapon,0x1D0)=transform;
    const edf6vr::Matrix matrix={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{10,20,30,1}}};
    Field<edf6vr::Matrix>(soldier,0x60)=matrix;
    Field<edf6vr::Matrix>(transform,0x50)=matrix; Field<float>(weapon,0x358)=1;
    g_lastCamera=camera; g_watchBullets=false; g_shotFromMuzzle=false; g_fireOriginLift=2;
    CHECK(OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon)+0x800,true)==reinterpret_cast<DWORD64>(weapon));
    CHECK(OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon)+0x9E0,true)==reinterpret_cast<DWORD64>(weapon));
    CHECK(!OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon)+0x810,true));
    DWORD64 returnAddress=reinterpret_cast<DWORD64>(g_image.base)+0x28A9CA;
    CONTEXT c{}; c.Rcx=reinterpret_cast<DWORD64>(bullet); c.Rdx=reinterpret_cast<DWORD64>(weapon)+0x800; c.Rsp=reinterpret_cast<DWORD64>(&returnAddress);
    CHECK(HandleShotSite(0x22E9C0,&c));
    // Emulate base world construction followed by the proven +140 component call.
    Field<edf6vr::Matrix>(bullet,0x60)=matrix;
    c.Rcx=reinterpret_cast<DWORD64>(bullet)+0x140; c.Rdx=reinterpret_cast<DWORD64>(bullet)+0x60;
    returnAddress=reinterpret_cast<DWORD64>(g_image.base)+0x22EA4A;
    CHECK(HandleShotSite(0x231CC0,&c));
    CHECK(Field<float>(bullet,0x94)==22);
    CHECK(!std::memcmp(bullet+0x60,&matrix,48)); CHECK(Field<float>(bullet,0x9C)==1);
    CHECK(!HandleShotSite(0x231CC0,&c)); CHECK(Field<float>(bullet,0x94)==22); // once only
    // Native 231D97..231DBC copies source matrix into component+B50 (bullet+C90).
    Field<edf6vr::Matrix>(bullet,0xC90)=Field<edf6vr::Matrix>(bullet,0x60);
    c.Rsi=reinterpret_cast<DWORD64>(bullet);
    CHECK(HandleShotSite(0x22EA4A,&c)); CHECK(g_shotReport.simulation[1]==22 && g_shotReport.world[1]==22);
    CHECK(!FindFreshShot(c.Rsi));
    // Reject nonplayer constructor even immediately after a player's shot.
    c.Rcx=reinterpret_cast<DWORD64>(bullet); c.Rdx=reinterpret_cast<DWORD64>(weapon)+0x880;
    CHECK(!HandleShotSite(0x22E9C0,&c));
    c.Rdx=reinterpret_cast<DWORD64>(weapon)+0x800; CHECK(HandleShotSite(0x22E9C0,&c));
    c.Rcx=reinterpret_cast<DWORD64>(bullet)+0x140; c.Rdx=reinterpret_cast<DWORD64>(transform)+0x50;
    CHECK(!HandleShotSite(0x231CC0,&c)); CHECK(Field<float>(transform,0x84)==20);
    c.Rdx=reinterpret_cast<DWORD64>(bullet)+0x60; returnAddress=0;
    CHECK(!HandleShotSite(0x231CC0,&c));
    returnAddress=reinterpret_cast<DWORD64>(g_image.base)+0x22EA4A;
    auto shot=FindFreshShot(reinterpret_cast<DWORD64>(bullet)); CHECK(shot!=nullptr);
    if(shot) shot->born=GetTickCount64()-1001;
    CHECK(!HandleShotSite(0x231CC0,&c));
    // INI slot order must not reinterpret an arbitrary candidate as a CC0 write.
    g_fireSite[0]=0x28BB5C; c.Dr6=1; c.Rbx=reinterpret_cast<DWORD64>(bullet);
    const auto before=Field<edf6vr::Matrix>(bullet,0xC90);
    CHECK(ServeFireSite(&c)); CHECK(!std::memcmp(&before,bullet+0xC90,64));
    // Exercise the actual muzzle branch separately from the diagnostic lift.
    // A stale/foreign weapon, distant muzzle or invalid value must leave the
    // native source intact, even with a nonzero diagnostic lift configured.
    g_shotFromMuzzle=true; g_muzzleWantValid=true; g_weaponObject=weapon;
    for(int sample=0;sample<5;++sample) {
        Field<edf6vr::Matrix>(bullet,0x60)=matrix;
        g_weaponObject=sample==1?nullptr:weapon;
        g_muzzleWantValid=sample!=4;
        g_muzzleWant[0]=sample==2?1000.0f:11.0f;
        g_muzzleWant[1]=sample==3?std::numeric_limits<float>::quiet_NaN():21.0f;
        g_muzzleWant[2]=31.0f;
        PublishMuzzleFrame(weapon,soldier,0,g_muzzleWant,matrix.m[3],GetTickCount64());
        c.Rcx=reinterpret_cast<DWORD64>(bullet); c.Rdx=reinterpret_cast<DWORD64>(weapon)+0x800;
        CHECK(HandleShotSite(0x22E9C0,&c));
        c.Rcx=reinterpret_cast<DWORD64>(bullet)+0x140; c.Rdx=reinterpret_cast<DWORD64>(bullet)+0x60;
        CHECK(HandleShotSite(0x231CC0,&c));
        CHECK(Field<float>(bullet,0x90)==(sample==0?11.0f:10.0f));
        CHECK(Field<float>(bullet,0x94)==(sample==0?21.0f:20.0f));
        CHECK(Field<float>(bullet,0x98)==(sample==0?31.0f:30.0f));
        CHECK(!std::memcmp(bullet+0x60,&matrix,48)); CHECK(Field<float>(bullet,0x9C)==1);
        Field<edf6vr::Matrix>(bullet,0xC90)=Field<edf6vr::Matrix>(bullet,0x60);
        c.Rsi=reinterpret_cast<DWORD64>(bullet);
        CHECK(HandleShotSite(0x22EA4A,&c)); // finish lifecycle before recycling fixture
    }
    // Projectile initialization must use current player translation, even when
    // the camera's diagnostic muzzle still belongs to a previous render.
    g_weaponObject=weapon; g_muzzleWantValid=true;
    const float oldMuzzle[3]={11,21,31};
    PublishMuzzleFrame(weapon,soldier,0,oldMuzzle,matrix.m[3],GetTickCount64());
    Field<edf6vr::Matrix>(soldier,0x60)=matrix;
    Field<float>(soldier,0x94)+=4;
    Field<edf6vr::Matrix>(bullet,0x60)=matrix; Field<float>(bullet,0x94)+=4;
    c.Rcx=reinterpret_cast<DWORD64>(bullet); c.Rdx=reinterpret_cast<DWORD64>(weapon)+0x800;
    CHECK(HandleShotSite(0x22E9C0,&c));
    c.Rcx=reinterpret_cast<DWORD64>(bullet)+0x140; c.Rdx=reinterpret_cast<DWORD64>(bullet)+0x60;
    CHECK(HandleShotSite(0x231CC0,&c)); CHECK(Field<float>(bullet,0x94)==25);
    Field<edf6vr::Matrix>(bullet,0xC90)=Field<edf6vr::Matrix>(bullet,0x60);
    c.Rsi=reinterpret_cast<DWORD64>(bullet); CHECK(HandleShotSite(0x22EA4A,&c));
    Field<edf6vr::Matrix>(soldier,0x60)=matrix;
    // Air Raider's third slot must enter the same shot lifecycle, with owner and
    // array bounds unchanged. Use different class/slot pairs, not extra writes.
    g_shotFromMuzzle=false; g_fireOriginLift=2;
    for(const auto table:{0x17CDF28u,0x17D0FF8u,0x17CF100u}) {
        Field<void*>(soldier,0)=g_image.base+table;
        for(unsigned slot=0;slot<(table==0x17CF100u?3u:2u);++slot) {
            memset(entries,0,sizeof(entries));
            Field<unsigned>(soldier,0x1980)=slot+1;
            Field<void*>(entries,slot*0x150+0x40)=&wrapper;
            CHECK(edf6vr::WeaponSlotCount(soldier)==slot+1);
            CHECK(OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon),false)==reinterpret_cast<DWORD64>(weapon));
            CHECK(!OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon)+16,false));
            c.Rcx=reinterpret_cast<DWORD64>(bullet); c.Rdx=reinterpret_cast<DWORD64>(weapon)+0x800;
            CHECK(HandleShotSite(0x22E9C0,&c));
            Field<edf6vr::Matrix>(bullet,0x60)=matrix;
            c.Rcx=reinterpret_cast<DWORD64>(bullet)+0x140; c.Rdx=reinterpret_cast<DWORD64>(bullet)+0x60;
            CHECK(HandleShotSite(0x231CC0,&c)); CHECK(Field<float>(bullet,0x94)==22);
            Field<edf6vr::Matrix>(bullet,0xC90)=Field<edf6vr::Matrix>(bullet,0x60);
            c.Rsi=reinterpret_cast<DWORD64>(bullet); CHECK(HandleShotSite(0x22EA4A,&c));
        }
    }
    Field<unsigned>(soldier,0x1980)=9;
    CHECK(edf6vr::WeaponSlotCount(soldier)==0 && !OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon),false));
    Field<unsigned>(soldier,0x1980)=3;
    Field<void*>(camera,edf6vr::kSourceOffset)=weapon;
    CHECK(!OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon),false));
    Field<void*>(camera,edf6vr::kSourceOffset)=soldier;
    Field<void*>(soldier,0)=g_image.base+0x17CF5B8;
    CHECK(OwnedShotWeapon(reinterpret_cast<DWORD64>(weapon),false)==reinterpret_cast<DWORD64>(weapon));
    // The Fencer trial observes every owned constructor, but never shifts its
    // origins, even with a leftover muzzle or diagnostic lift enabled.
    for(bool fromMuzzle:{false,true}) {
        g_shotFromMuzzle=fromMuzzle; g_muzzleWantValid=true; g_weaponObject=weapon; g_fireOriginLift=2;
        const float muzzle[3]={11,21,31};
        PublishMuzzleFrame(weapon,soldier,0,muzzle,matrix.m[3],GetTickCount64());
        c.Rcx=reinterpret_cast<DWORD64>(bullet); c.Rdx=reinterpret_cast<DWORD64>(weapon)+0x800;
        CHECK(HandleShotSite(0x22E9C0,&c));
        Field<edf6vr::Matrix>(bullet,0x60)=matrix;
        c.Rcx=reinterpret_cast<DWORD64>(bullet)+0x140; c.Rdx=reinterpret_cast<DWORD64>(bullet)+0x60;
        CHECK(HandleShotSite(0x231CC0,&c)); CHECK(!memcmp(bullet+0x60,&matrix,64));
        Field<edf6vr::Matrix>(bullet,0xC90)=matrix;
        c.Rsi=reinterpret_cast<DWORD64>(bullet); CHECK(HandleShotSite(0x22EA4A,&c));
    }
    CHECK(g_shotReport.fencer[0].count==2);
    // A blade's wave and the katana's thrust, told by where the bullet's
    // constructor returns to: in the real image each is the instruction after
    // a call of that constructor (22E9C0).
    {
        const auto base=reinterpret_cast<DWORD64>(g_image.base);
        for(const DWORD64 born:{kSlashWaveBorn,kEfsBulletBorn}) {
            const auto* at=g_image.base+born-5;
            CHECK(at[0]==0xE8 && g_image.base+born+*reinterpret_cast<const std::int32_t*>(at+1)==g_image.base+0x22E9C0);
            CHECK(ShotFliesFromSwing(base+born));
        }
        CHECK(!ShotFliesFromSwing(base+0x28A9CA) && !ShotFliesFromSwing(0));
        // The way a muzzle fires: its rows by the weapon's fire vector.
        float along[3]{};
        const edf6vr::Matrix turned={{{0,0,-1,0},{0,1,0,0},{1,0,0,0},{10,20,30,1}}};   // its Z along +X
        Field<edf6vr::Matrix>(transform,0x50)=turned;
        Field<float>(weapon,0x350)=0; Field<float>(weapon,0x354)=0; Field<float>(weapon,0x358)=1;
        CHECK(ShotMuzzleDirection(weapon,along) && std::fabs(along[0]-1)<1e-6f && std::fabs(along[1])<1e-6f && std::fabs(along[2])<1e-6f);
        Field<float>(weapon,0x358)=0; CHECK(!ShotMuzzleDirection(weapon,along));   // no fire vector, no direction
        Field<float>(weapon,0x358)=1;
        Field<void*>(weapon,0x1D0)=nullptr; CHECK(!ShotMuzzleDirection(weapon,along));
        Field<void*>(weapon,0x1D0)=transform; Field<edf6vr::Matrix>(transform,0x50)=matrix;
    }
    // The soldier's root motion (fencer_dual.h, FencerTurnLunge): the call the
    // hook takes, the routine it calls, and the body's rows the step is laid
    // along, as they are in the real image.
    {
        const auto* base=g_image.base;
        CHECK(base[0x5758C0]==0xE8 && base+0x5758C5+*reinterpret_cast<const std::int32_t*>(base+0x5758C1)==base+0x1166ED0);
        static const unsigned char take[]={0x8B,0xC2,0x48,0x69,0xD0,0x60,0x01,0x00,0x00,0x48,0x8B,0x41,0x60};   // layer * 0x160 of [controller+0x60]
        CHECK(!std::memcmp(base+0x1166ED0,take,sizeof(take)));
        static const unsigned char controller[]={0x48,0x8D,0x8E,0x90,0x0B,0x00,0x00};      // lea rcx,[rsi+0xB90]
        CHECK(!std::memcmp(base+0x5758A9,controller,sizeof(controller)));
        static const unsigned char rowX[]={0x0F,0x59,0x96,0xE0,0x05,0x00,0x00};            // mulps xmm2,[rsi+0x5E0]
        static const unsigned char rowY[]={0x0F,0x10,0x9E,0xF0,0x05,0x00,0x00};            // movups xmm3,[rsi+0x5F0]
        static const unsigned char rowZ[]={0x0F,0x10,0x8E,0x00,0x06,0x00,0x00};            // movups xmm1,[rsi+0x600]
        CHECK(!std::memcmp(base+0x5758CF,rowX,sizeof(rowX)) && !std::memcmp(base+0x5758E0,rowY,sizeof(rowY)) && !std::memcmp(base+0x5758F4,rowZ,sizeof(rowZ)));
        // The hook on a made-up soldier: only the player's Fencer, only while the
        // left weapon's arm animation plays, and then the step leaves along the left aim.
        alignas(16) unsigned char body[0x1500]{},leftWeapon[0x1000]{},rightWeapon[0x1000]{};
        const float rows[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};      // facing yaw 0
        std::memcpy(body+0x5E0,rows,sizeof(rows));
        struct Taken { float step[4]; };
        static Taken given{};
        g_soldierMoveOriginal=[](void*,unsigned,float* step,float*) { std::memcpy(step,given.step,16); };
        const bool vrWas=g_vrEnabled; g_vrEnabled=true;
        g_fencerActive.store(true); g_fencerWeapons[0].store(leftWeapon); g_fencerWeapons[1].store(rightWeapon);
        g_fencerShadow=FencerShadow{}; g_fencerShadow.smooth[1]=1.5707963f; g_fencerShadow.valid=true;   // the left hand aims 90 degrees to the left
        g_fencerMoveOwner=edf6vr::FencerMoveOwner{}; g_fencerLungeYawValid=false;
        float step[4]{},turn[4]{};
        given={{0,0,0.5f,1}};
        // Not the player's soldier: untouched.
        g_fencerMoveSoldier.store(nullptr); leftWeapon[0xEC4]=1;
        HookSoldierMove(body+0xB90,0,step,turn); CHECK(step[2]==0.5f && step[0]==0);
        // The player's, the left weapon swinging: along the left aim (+X is the world's yaw 90).
        g_fencerMoveSoldier.store(body);
        HookSoldierMove(body+0xB90,0,step,turn);
        CHECK(std::fabs(step[0]-0.5f)<1e-5f && std::fabs(step[2])<1e-5f && step[3]==1);
        // The left aim moves on during the lunge: the lunge keeps its line.
        g_fencerShadow.smooth[1]=0.3f;
        HookSoldierMove(body+0xB90,0,step,turn); CHECK(std::fabs(step[0]-0.5f)<1e-5f && std::fabs(step[2])<1e-5f);
        // The charge (layer 1) is no attack; the right weapon's own lunge is the game's.
        *reinterpret_cast<int*>(leftWeapon+0xEC8)=1;
        HookSoldierMove(body+0xB90,0,step,turn); CHECK(step[2]==0.5f && step[0]==0);
        *reinterpret_cast<int*>(leftWeapon+0xEC8)=0; leftWeapon[0xEC4]=0; rightWeapon[0xEC4]=1;
        HookSoldierMove(body+0xB90,0,step,turn); CHECK(step[2]==0.5f && step[0]==0);
        // Standing still: nothing to turn.
        rightWeapon[0xEC4]=0; leftWeapon[0xEC4]=1; given={{0,0,0,1}};
        HookSoldierMove(body+0xB90,0,step,turn); CHECK(step[0]==0 && step[2]==0);
        g_fencerActive.store(false); g_fencerWeapons[0].store(nullptr); g_fencerWeapons[1].store(nullptr);
        g_fencerMoveSoldier.store(nullptr); g_fencerShadow=FencerShadow{}; g_vrEnabled=vrWas;
    }
    FreeLibrary(mapped);
    printf("Shot initialization: %d failures; owner identity, ABI, lifetime, single application, no late writes\n",failures);
    return failures?1:0;
}
