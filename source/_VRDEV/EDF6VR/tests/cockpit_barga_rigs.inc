std::vector<std::wstring> bargaBoneNames;
void* __fastcall BargaLookup(void*,const void* name) {
    const auto* bytes=static_cast<const unsigned char*>(name);const auto n=*reinterpret_cast<const std::uint64_t*>(bytes+16);
    const auto* text=n<=7?reinterpret_cast<const wchar_t*>(bytes):*reinterpret_cast<const wchar_t* const*>(bytes);
    for(unsigned i=0;i<bargaBoneNames.size();++i)if(bargaBoneNames[i]==text)return nodes+i*0x110;
    return nullptr;
}
void BargaRigTest(const wchar_t* dll,const std::filesystem::path& folder) {
    auto module=LoadLibraryExW(dll,nullptr,DONT_RESOLVE_DLL_REFERENCES);ImageProfile image{};char reason[256]{};
    CHECK(module&&CheckImage(module,image,reason,sizeof(reason)));if(!image.base)return;
    unsigned models=0;
    for(const auto& file:std::filesystem::directory_iterator(folder)) {
        if(file.path().extension()!=L".rigfixture")continue;
        std::ifstream f(file.path(),std::ios::binary);char magic[4];std::uint32_t count=0,variant=0;
        f.read(magic,4);f.read(reinterpret_cast<char*>(&count),4);f.read(reinterpret_cast<char*>(&variant),4);
        CHECK(!std::memcmp(magic,"BRG1",4)&&count>=59&&count<=71&&variant<=1);if(count>80)continue;
        alignas(16) unsigned char vehicle[0x1500]{},boneBytes[80*0x110]{},resource[16]{},seats[2*0x340]{};
        nodes=boneBytes;At<void*>(vehicle,0xE40)=image.base+kModelVtableRva;At<void*>(vehicle,0xEE0)=resource;
        At<void*>(vehicle,0xEF0)=nodes;At<std::uint64_t>(vehicle,0xF00)=count;
        At<void*>(vehicle,0x608)=seats;At<std::uint64_t>(vehicle,0x618)=2; // seat array and its count
        bargaBoneNames.clear();std::vector<unsigned char> kept;
        for(unsigned i=0;i<count;++i) {
            std::uint32_t length=0;f.read(reinterpret_cast<char*>(&length),4);CHECK(length<64);if(length>=64)return;
            std::wstring name(length,L' ');f.read(reinterpret_cast<char*>(name.data()),length*2);bargaBoneNames.push_back(name);
            f.read(reinterpret_cast<char*>(&At<Matrix>(nodes+i*0x110,0xB0)),sizeof(Matrix));unsigned char keep=0;f.read(reinterpret_cast<char*>(&keep),1);kept.push_back(keep);
        }
        CHECK(bool(f));VehicleSeat seat{};seat.vehicle=vehicle;seat.cameraOwner=vehicle;seat.seat=seats;
        CockpitRig rig{};Matrix cabin{};CHECK(PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));
        CHECK(rig.kind==CockpitKind::Barga&&rig.capCount==5&&rig.capVariant==variant);
        CHECK(std::fabs(cabin.m[3][1]-40.901319f)<.001f&&std::fabs(cabin.m[3][2]-.10f)<.001f);
        const auto eye=CockpitSeatedCamera(cabin);CHECK(std::fabs(eye.m[3][1]-41.001319f)<.001f&&std::fabs(eye.m[3][2])<.001f);
        for(unsigned i=0;i<count;++i)CHECK(rig.limb[i]==kept[i]);
        for(unsigned i=0;i<5;++i)CHECK(bargaBoneNames[rig.capBones[i]]==kBargaCapBones[i]);
        // Native opening samples measured from the MDB, independent of the
        // generated dish profile. A rigidly attached seal must block rays
        // from every side, including behind the rim during a punching turn.
        unsigned sealRays=0,sealLeaks=0;
        for(unsigned part=0;part<3;++part) {
            const auto cap=BuildBargaJointCap(variant,part);const auto inverse=Inverse(rig.capFrames[part]);
            auto local=[&](Point p){Point q{};for(unsigned j=0;j<3;++j){q[j]=inverse.m[3][j];for(unsigned k=0;k<3;++k)q[j]+=p[k]*inverse.m[k][j];}return q;};
            for(float a:{-1.f,0.f,1.f})for(float b:{-1.f,0.f,1.f}) {
                // Include the full opening rectangle, not just its centre.
                const Point world=part<2?Point{part==0?4.0625f:-4.0625f,41.53125f+a*1.375f,b*1.3837891f}:
                    Point{a*1.4755859f,33.34435f+b*.03125f,-.0546875f+b*4.3789063f};
                const auto target=local(world);
                for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
                    if(!x&&!y&&!z)continue;
                    auto origin=target;origin[0]+=12.f*x;origin[1]+=12.f*y;origin[2]+=12.f*z;
                    // Cardinal rays can land exactly on shared triangle
                    // edges; tolerate float barycentric roundoff there.
                    ++sealRays;if(!MeshBlocked(cap,origin,target,.00001f)){++sealLeaks;if(!models)std::printf("Unsealed part=%u sample=%.0f %.0f direction=%d %d %d\n",part,a,b,x,y,z);}
                }
            }
        }
        std::printf("Barga variant %u closed joint rays=%u leaks=%u\n",variant,sealRays,sealLeaks);CHECK(sealLeaks==0);
        const auto old=rig;const auto oldCabin=cabin;
        // Punching chest/shoulder motion must not rotate or displace the cabin:
        // the chest twisting on its spine or leaning sideways (world rotations
        // of its bind pose about a pivot, whatever the bone's own axes).
        const auto chest=static_cast<unsigned>(std::find(bargaBoneNames.begin(),bargaBoneNames.end(),L"mune")-bargaBoneNames.begin());
        auto& chestPose=At<Matrix>(nodes+chest*0x110,0xB0);const auto chestRest=chestPose;
        auto turned=[](const Matrix& rest,auto rotate,Point pivot,Point shift) {
            Matrix m=rest;
            for(int i=0;i<3;++i){const auto r=rotate(Point{rest.m[i][0],rest.m[i][1],rest.m[i][2]});for(int j=0;j<3;++j)m.m[i][j]=r[j];}
            const auto r=rotate(Point{rest.m[3][0]-pivot[0],rest.m[3][1]-pivot[1],rest.m[3][2]-pivot[2]});
            for(int j=0;j<3;++j)m.m[3][j]=pivot[j]+r[j]+shift[j];
            return m;
        };
        const Point chestPivot{chestRest.m[3][0],chestRest.m[3][1],chestRest.m[3][2]};
        auto twist=[](Point p){return Point{p[2],p[1],-p[0]};};
        auto sideways=[](Point p){const float c=std::cos(.35f),s=std::sin(.35f);return Point{p[0]*c-p[1]*s,p[0]*s+p[1]*c,p[2]};};
        for(const auto& pose:{turned(chestRest,twist,chestPivot,{}),turned(chestRest,sideways,chestPivot,{})}) {
            chestPose=pose;CHECK(PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));CHECK(!std::memcmp(&cabin,&oldCabin,sizeof(cabin)));
        }
        // Only its lean fore and aft moves the cabin, along the waist's forward,
        // by the travel of a spine point 4.40 m over the chest's pivot (at the
        // cabin's height); the waist keeps its orientation and height.
        for(const float angle:{.5236f,-.5236f}) {
            const float c=std::cos(angle),s=std::sin(angle);
            chestPose=turned(chestRest,[&](Point p){return Point{p[0],p[1]*c-p[2]*s,p[1]*s+p[2]*c};},chestPivot,{});
            CHECK(PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));
            CHECK(std::fabs(cabin.m[3][2]-(.10f+4.40f*s))<.001f&&std::fabs(cabin.m[3][1]-oldCabin.m[3][1])<.001f&&std::fabs(cabin.m[3][0]-oldCabin.m[3][0])<.001f);
            CHECK(!std::memcmp(&cabin.m[0],&oldCabin.m[0],sizeof(float)*12));
            CHECK(std::fabs(BargaChestLean()-4.40f*s)<.001f);
        }
        chestPose=turned(chestRest,twist,chestPivot,{});
        CHECK(PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));CHECK(!std::memcmp(&cabin,&oldCabin,sizeof(cabin)));
        At<Matrix>(nodes+rig.capBones[0]*0x110,0xB0).m[3][2]+=2;
        CHECK(PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));
        CHECK(std::fabs(rig.capFrames[0].m[3][2]-old.capFrames[0].m[3][2]-2)<.001f);
        CockpitHistory history;CockpitPose pose{};pose.camera=eye;pose.cabin=oldCabin;pose.rig=old;pose.at=1000;history.Record(pose);
        pose.rig=rig;pose.camera.m[3][2]+=.10f;pose.at=1001;history.Record(pose);CockpitPose found{};
        CHECK(history.Match(Inverse(eye),1002,found));CHECK(found.rig.capFrames[0].m[3][2]==old.capFrames[0].m[3][2]);
        // A whole-body turn and translation still carry the cabin (the chest,
        // its child, turned with the pelvis).
        auto& pelvis=At<Matrix>(nodes+rig.capBones[2]*0x110,0xB0);const auto pelvisRest=pelvis;
        for(int i=0;i<4;++i){const float x=pelvis.m[i][0],z=pelvis.m[i][2];pelvis.m[i][0]=-z;pelvis.m[i][2]=x;}
        pelvis.m[3][0]+=3;pelvis.m[3][2]+=5;
        chestPose=turned(chestRest,[](Point p){return Point{-p[2],p[1],p[0]};},Point{0,0,0},Point{3,0,5});
        CHECK(PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));
        CHECK(std::fabs(cabin.m[3][0]-2.9f)<.001f&&std::fabs(cabin.m[3][1]-40.901319f)<.001f&&std::fabs(cabin.m[3][2]-5)<.001f);
        CHECK(std::fabs(cabin.m[0][2]-1)<.001f&&std::fabs(cabin.m[2][0]+1)<.001f);
        pelvis=pelvisRest;chestPose=chestRest;
        seat.seat=seats+0x340;CHECK(!PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));seat.seat=seats;
        const auto good=rig;At<Matrix>(nodes+rig.capBones[0]*0x110,0xB0).m[0][0]=std::numeric_limits<float>::quiet_NaN();
        CHECK(!PlaceVehicleCockpit(image,seat,BargaLookup,cabin,rig));CHECK(!std::memcmp(&rig,&good,sizeof(rig)));
        ++models;
    }
    CHECK(models==5);std::printf("Barga animated rig variants=%u: bind placement, all retained bones, immutable cap poses\n",models);FreeLibrary(module);
}
