// Proteus MK2 tandem cockpit: the driver in front, the missile operator a step
// higher behind. The frame lines repeat here rather than being imported from
// the generator, so a slip in the cabin cannot validate itself.
namespace cabin {
constexpr float kRearUp=.56f,kRearBack=-1.18f;
constexpr float kFloor=-1.22f,kStep=-.66f,kRiser=-.62f;
constexpr float kTail=-1.92f,kNose=1.42f,kChin=1.16f;
// Screen lines: no frame inside them any more, and narrower than the body.
float Half(float z) {return z<.20f?.860f:std::max(.560f,.860f-.250f*(z-.20f));}
float Roof(float z) {return z<-.20f?1.040f:std::max(.560f,1.040f-.260f*(z+.20f));}
float Deck(float z) {return z<kRiser?kStep:kFloor;}
// The riser between the decks hangs from the upper one, so the lower deck's
// floor is what bounds that short band.
float Lowest(float z) {return z<kRiser-.08f?kStep:kFloor;}
}
void CabinLayoutTest(bool baked=true) {
    using namespace cabin;
    for(unsigned station=0;station<2;++station) {
        const bool rear=station==1;
        const auto kind=rear?CockpitKind::ProteusMissile:CockpitKind::ProteusDriver;
        const float up=rear?-kRearUp:0.f,back=rear?-kRearBack:0.f;   // build shift
        auto mesh=BuildCockpit(kind);
        std::printf("Proteus %s station: %zu triangles\n",rear?"missile":"driver",mesh.size()/3);
        CHECK(mesh.size()>1000&&mesh.size()%3==0);
        if(baked)CHECK(ApplyCockpitOcclusion(mesh,kind));
        for(const auto& v:mesh) {
            float n=0;for(int k=0;k<3;++k){CHECK(std::isfinite(v.position[k]));n+=v.normal[k]*v.normal[k];}
            CHECK(std::fabs(n-1)<.001f);
            // Undo the station's own shift and hold the cabin to its frame.
            const float x=v.position[0],y=v.position[1]-up,z=v.position[2]-back;
            // The frame members have their own radius and the screen ink rides
            // just outside the line, so the envelope is the line plus that.
            const float slack=v.surface==21?.080f:.055f;
            CHECK(z>kTail-.70f&&z<kNose+.06f);
            CHECK(std::fabs(x)<Half(z)+slack);
            CHECK(y<Roof(z)+slack);
            CHECK(y>Lowest(z)-.30f);
        }
        for(std::size_t i=0;i<mesh.size();i+=3) {
            Point a{},b{},c{},n{};std::memcpy(a.data(),mesh[i].position,12);std::memcpy(b.data(),mesh[i+1].position,12);
            std::memcpy(c.data(),mesh[i+2].position,12);std::memcpy(n.data(),mesh[i].normal,12);
            const auto normal=Cross(Sub(b,a),Sub(c,a));CHECK(Dot(normal,normal)>1e-18f&&Dot(normal,n)>0);
        }
        const auto eye=kCockpitSeatedEye;
        // Six-axis head movement, stopped by the cabin, never through hardware.
        for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
            // Four metres, so even a free run down the cabin ends at a wall.
            auto desired=eye;desired[axis]+=sign*4;
            const auto stopped=ClampCockpitHead(desired,.10f,kind);
            std::printf("%s head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",rear?"missile":"driver",axis,sign,stopped[0],stopped[1],stopped[2]);
            CHECK(sign*(stopped[axis]-eye[axis])>.035f);
            CHECK(std::fabs(stopped[axis]-desired[axis])>.05f);
            CHECK(!MeshBlocked(mesh,eye,stopped));
        }
        std::vector<Point> heads;
        for(float x:{-.36f,.36f})for(float y:{-.14f,.34f})for(float z:{-.28f,.40f})
            heads.push_back(ClampCockpitHead({x,y,z},.10f,kind));
        // The cabin is screen ahead, to the sides and overhead, and shut behind.
        CHECK(!MeshBlocked(mesh,eye,{0,eye[1],12}));
        // The sides are screen, but the frame and its bracing are meant to be
        // in the way of some of it.
        unsigned flank=0;
        for(float s:{-1.f,1.f})for(float z:{-1.f,0.f,2.f,5.f})if(!MeshBlocked(mesh,eye,{s*8,eye[1]+.20f,z}))++flank;
        std::printf("%s flank open %u of 8\n",rear?"missile":"driver",flank);
        CHECK(flank>=7);
        // Overhead is screen, though the frame's own cross members are meant
        // to be in the way: most of the sky, not all of it.
        unsigned sky=0;
        for(float z:{-.70f,-.30f,.20f,.70f,1.30f})if(!MeshBlocked(mesh,eye,{0,8,z}))++sky;
        CHECK(sky>=5);
        unsigned aft=0;
        for(const auto& head:heads)for(int i=0;i<9;++i)for(int j=0;j<9;++j) {
            // Inside the tail section: rays leaving through the side or roof screens are legal.
            const Point target{-.80f+.20f*i,-.90f+.235f*j+up,kTail-.80f+back};
            if(MeshBlocked(mesh,head,target))continue;
            if(++aft<6)std::printf("Cabin aft leak (%s): eye %.2f %.2f %.2f to %.2f %.2f\n",
                rear?"missile":"driver",head[0],head[1],head[2],target[0],target[1]);
        }
        CHECK(aft==0);
        // The head room itself is empty: nothing solid anywhere in the box the
        // head is allowed to reach, whichever station.
        // Below the chin is the body and its controls; behind the head below
        // the crown is the headrest. Neither is a thing the head walks into.
        for(float x=-.42f;x<=.421f;x+=.14f)for(float y=-.12f;y<=.521f;y+=.16f)for(float z=-.36f;z<=.561f;z+=.15f) {
            if(z<-.20f&&y<.36f)continue;
            const Point at{x,y,z};
            if(MeshBlocked(mesh,eye,at))std::printf("Cabin (%s) head room blocked toward %.2f %.2f %.2f\n",rear?"missile":"driver",x,y,z);
            CHECK(!MeshBlocked(mesh,eye,at));
        }
        // Both decks are real floor, and the step between them is closed.
        CHECK(MeshBlocked(mesh,{0,eye[1],back+.20f},{0,kFloor-.30f+up,back+.20f}));
        CHECK(MeshBlocked(mesh,{0,eye[1],back-1.10f},{0,kStep-.30f+up,back-1.10f}));
        CHECK(MeshBlocked(mesh,{0,kFloor+.20f+up,back-.50f},{0,kFloor+.20f+up,back-.90f}));
        // The chin opening exists: from the driver's eye the machine's own legs
        // are in view below the sight line.
        // Either side of the centre stick, which is of course in the way of a
        // sightline straight down the middle.
        // The driver looks down between the console's risers, past the keel,
        // to the deck's end. The operator's own view of the legs is straight down
        // the sides beside the seat, behind the lever consoles and between the
        // lockers, into the lower band of the side screen.
        for(float s:{-1.f,1.f}) {
            // On the operator's left the equipment bank stands in the low band;
            // that side is checked above the bank's lids.
            // Cabin coordinates plus the build shift, from that station's own eye.
            const Point from=rear?Point{0,.10f+kRearUp+up,-.10f+kRearBack+back}:Point{0,.10f,-.10f};
            const Point chin=rear?Point{s*.86f,(s>0?-.40f:.10f)+up,-1.30f+back}:Point{s*.20f,-1.05f,1.24f};
            if(MeshBlocked(mesh,from,chin))std::printf("Cabin (%s) leg sightline s=%.0f blocked\n",rear?"missile":"driver",s);
            CHECK(!MeshBlocked(mesh,from,chin));
        }
        // The stations: a stick each side at the front, leaning inward, a lever
        // each side at the back, and a seat under both.
        for(float s:{-1.f,1.f})
            CHECK(MeshBlocked(mesh,{s*.15f,-.240f+up,.284f+back},{s*.55f,-.240f+up,.284f+back}));
        for(float s:{-1.f,1.f})
            CHECK(MeshBlocked(mesh,{s*.20f,.27f+up,-1.00f+back},{s*.62f,.27f+up,-1.00f+back}));
        for(float z:{0.f,kRearBack}) {
            const float deck=z<kRiser?kStep:kFloor;
            CHECK(MeshBlocked(mesh,{0,-.40f+(z<kRiser?kRearUp:0.f)+up,z+.05f+back},{0,deck-.20f+up,z+.05f+back}));
        }
        // Screens: three joined per station; the centre square to its pilot,
        // the wings bent toward the pilot off the centre's edges.
        const auto displays=BuildCockpitDisplays(kind);CHECK(displays.size()==36);
        for(unsigned i=0;i<36;i+=6) {
            Point n{},c{};for(int k=0;k<3;++k){n[k]=displays[i].normal[k];for(unsigned j=0;j<6;++j)c[k]+=displays[i+j].position[k]/6;}
            const Point seat=i<18?Point{0,.10f+up,-.10f+back}:Point{0,.10f+kRearUp+up,-.10f+kRearBack+back};
            const auto toSeat=Sub(seat,c);
            const bool centre=(i/6)%3==0;
            // The driver's centre screen is the block's own face, 6 degrees off square.
            CHECK(std::fabs(Dot(n,toSeat))/std::sqrt(Dot(toSeat,toSeat))>(centre?.97f:.90f));
            CHECK(Dot(toSeat,toSeat)>.40f*.40f&&Dot(toSeat,toSeat)<1.20f*1.20f);
        }
    }
}
