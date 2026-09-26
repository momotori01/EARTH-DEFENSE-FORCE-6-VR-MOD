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
    FreeLibrary(mapped);
    printf("Shot initialization: %d failures; owner identity, ABI, lifetime, single application, no late writes\n",failures);
    return failures?1:0;
}
