// ScanVehicleCrew on a hand-built vehicle of four seats: you (a Ranger) in
// seat 0, held one pointer away (RideInfo+0x100 -> a block whose +8 is you, as
// a weak reference would be); a Fencer in seat 3, held directly at
// RideInfo+0xE0; a Wing Diver the vehicle points at who sits in another one.
void VehicleCrewTest(const wchar_t* dll) {
    auto module=LoadLibraryExW(dll,nullptr,DONT_RESOLVE_DLL_REFERENCES);ImageProfile image{};char reason[256]{};
    CHECK(module&&CheckImage(module,image,reason,sizeof(reason)));if(!image.base)return;
    alignas(16) static unsigned char vehicle[0x1000],seats[4*0x340],you[0x1600],fencer[0x1600],diver[0x1600],other[0x1000],block[0x40];
    Matrix root=Identity();root.m[3][0]=10;At<Matrix>(vehicle,0x60)=root;
    At<void*>(vehicle,0x608)=seats;At<std::uint64_t>(vehicle,0x618)=4;At<void*>(vehicle,0x700)=diver;
    for(unsigned i=0;i<4;++i)At<void*>(seats+i*0x340,8)=vehicle;
    auto soldier=[&](unsigned char* s,std::uint32_t table,void* ride,void* owner,float x,float y,float z) {
        At<void*>(s,0)=image.base+table;At<void*>(s,0x1540)=ride;At<void*>(s,0x1548)=owner;
        At<float>(s,0x90)=x;At<float>(s,0x94)=y;At<float>(s,0x98)=z;
    };
    soldier(you,0x17CDF28,seats,vehicle,11,2,3);
    soldier(fencer,0x17CF5B8,seats+3*0x340,vehicle,10,4,-1);
    soldier(diver,0x17D0FF8,other+0x10,other,0,0,0);
    At<void*>(block,8)=you;At<void*>(seats,0x100)=block;
    At<void*>(seats+3*0x340,0xE0)=fencer;
    VehicleSeat seat{};seat.vehicle=vehicle;seat.seat=seats;seat.cameraOwner=vehicle;
    CrewScan crew{};CHECK(ScanVehicleCrew(image,seat,you,crew));
    CHECK(crew.seatCount==4&&crew.soldierCount==3);
    CHECK(crew.rider[0]==you&&crew.riderKind[0]==1&&crew.rider[3]==fencer&&crew.riderKind[3]==4);
    CHECK(!crew.rider[1]&&!crew.rider[2]&&crew.cameraOwner[3]==vehicle);
    auto has=[&](unsigned s,unsigned offset,unsigned inner,const void* who) {
        for(unsigned i=0;i<crew.refCount;++i) {
            const auto& r=crew.refs[i];
            if(r.seat==s&&r.offset==offset&&r.inner==inner&&r.soldier==who)return true;
        }
        return false;
    };
    CHECK(has(0,0x100,8,you)&&has(3,0xE0,~0u,fencer)&&has(~0u,0x700,~0u,diver)&&crew.refCount==3);
    for(unsigned i=0;i<crew.soldierCount;++i) {
        const auto& c=crew.soldiers[i];
        if(c.soldier==you)CHECK(c.self&&c.seat==0&&c.kind==1&&c.placed&&std::fabs(c.local[0]-1)<1e-5f&&std::fabs(c.local[1]-2)<1e-5f&&std::fabs(c.local[2]-3)<1e-5f);
        if(c.soldier==fencer)CHECK(!c.self&&c.seat==3&&c.kind==4&&std::fabs(c.local[1]-4)<1e-5f);
        if(c.soldier==diver)CHECK(c.seat==~0u&&c.kind==2&&c.vehicle==other);
    }
    CHECK(SoldierClassOf(image,other)==0&&SoldierClassOf(image,nullptr)==0);
    // Shallow: only RideInfo+0x260 (where the game keeps a seat's rider) and yourself.
    At<void*>(seats+3*0x340,kRideInfoRider)=fencer;
    CHECK(ScanVehicleCrew(image,seat,you,crew,false));
    CHECK(crew.rider[0]==you&&crew.rider[3]==fencer&&crew.riderKind[3]==4&&crew.soldierCount==2&&crew.refCount==1);
    CHECK(crew.refs[0].seat==3&&crew.refs[0].offset==kRideInfoRider&&crew.refs[0].inner==~0u);
    std::printf("Vehicle crew scan: riders by seat, weak and direct references, a soldier seated elsewhere\n");
    FreeLibrary(module);
}
