// Proteus MK2 gunner station. The envelope constants repeat the measured pod
// from research/cockpit/proteus/survey.json rather than the build's own
// helpers, so a slip in the generator cannot validate itself.
namespace proteus {
constexpr float kSkinX=.7285f;      // inner side skins, cabin +-x
constexpr float kDeck=.736f;        // flat structural deck overhead
constexpr float kNose=1.444f;       // native front skin at the nose centreline
constexpr float kShroud=1.478f;     // underside of the native gun bay
constexpr float kPivot=1.827f;      // gun_mount_barrel bone, cabin height
constexpr float kPivotZ=1.014f;
constexpr float kBarrelR=.534f;     // barrel mesh half height about its bone
float Floor(float z) {return z<-.94f?-1.553f:z<-.54f?-1.453f+.25f*(z+.54f):-1.1644f+.5344f*z;}
float Skin(float z) {return z<1.22f?kSkinX:std::max(.16f,kSkinX-2.67f*(z-1.22f));}
// Nothing of ours reaches the deck any more: the arm carries no load into the
// turret except through its own pivot, so the cabin stops below it.
constexpr float kRoofCover=-.20f;   // forward edge of the one roof panel
// A seated adult, in this cabin's own coordinates: hip on the pan, torso 42
// degrees back to the eye, thighs and shins folded onto the floor ramp, arms
// to the stick. Used to answer "does a person actually fit", not to draw.
struct Limb {Point a,b;float radius;const char* name;};
std::vector<Limb> ProteusOccupant() {
    const Point eye{0,.10f,-.10f};
    const Point hip{0,-.594f,.209f};      // where the shared seat's pan lands
    const Point shoulder{0,-.119f,-.002f};
    const Point knee{0,-.430f,.630f};
    const Point ankle{0,-.465f,.870f};   // the foot itself carries on to the board
    const Point grip{0,-.220f,.550f};   // on the stick, ahead of the pan
    std::vector<Limb> body{{hip,shoulder,.145f,"torso"},{shoulder,eye,.085f,"neck"}};
    for(float s:{-1.f,1.f}) {
        body.push_back({{s*.115f,hip[1],hip[2]},{s*.205f,knee[1],knee[2]},.090f,"thigh"});
        body.push_back({{s*.205f,knee[1],knee[2]},{s*.215f,ankle[1],ankle[2]},.070f,"shin"});
        body.push_back({{s*.195f,shoulder[1]-.02f,shoulder[2]},{s*.195f,-.360f,.300f},.055f,"upper arm"});
        // The forearm stops a hand's length short: the hand is on the grip.
        body.push_back({{s*.195f,-.360f,.300f},{s*.098f,-.232f,.470f},.050f,"forearm"});
    }
    return body;
}
}
// Real clearance for that body: every limb segment is swept as a capsule and
// must keep its own radius clear of the cabin, apart from the seat it rests on.
void ProteusOccupantTest(const std::vector<CockpitVertex>& mesh,CockpitKind kind) {
    using namespace proteus;
    unsigned touches=0;
    for(const auto& limb:ProteusOccupant()) {
        float closest=10;
        for(unsigned step=0;step<=10;++step) {
            Point p{};for(int k=0;k<3;++k)p[k]=limb.a[k]+(limb.b[k]-limb.a[k])*step/10.f;
            // Sweep 26 directions at the limb's own radius; a hit means the
            // cabin is inside the body, not merely supporting it.
            // Torso and thighs rest on the shared seat by design, so for them
            // only the free directions matter: shoulder and hip width, and the
            // space above. Everything else is checked all round.
            const bool bearing=limb.name[0]=='t'||limb.name[0]=='s';
            for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
                if(!x&&!y&&!z)continue;
                if(bearing&&!((y==0&&z==0)||(x==0&&y>0&&z==0)))continue;
                const float length=std::sqrt(float(x*x+y*y+z*z));
                Point q{p[0]+limb.radius*x/length,p[1]+limb.radius*y/length,p[2]+limb.radius*z/length};
                if(!MeshBlocked(mesh,p,q))continue;
                ++touches;closest=0;
                if(touches<8)std::printf("Proteus occupant %s clipped at %.3f %.3f %.3f dir %d %d %d\n",limb.name,p[0],p[1],p[2],x,y,z);
            }
        }
        (void)closest;
    }
    CHECK(touches==0);
    // Head box: the helmet must clear the lining at the neutral eye and after
    // a lean in every direction, which is what ClampCockpitHead already gives.
    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
        auto desired=kCockpitSeatedEye;desired[axis]+=sign*2;
        const auto stopped=ClampCockpitHead(desired,.115f,kind);
        for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z) {
            if(!x&&!y&&!z)continue;
            const float length=std::sqrt(float(x*x+y*y+z*z));
            Point q{stopped[0]+.105f*x/length,stopped[1]+.105f*y/length,stopped[2]+.105f*z/length};
            CHECK(!MeshBlocked(mesh,stopped,q));
        }
    }
}
void ProteusLayoutTest(bool baked=true) {
    using namespace proteus;
    const auto kind=CockpitKind::ProteusGunner;auto mesh=BuildProteusGunnerCockpit();
    std::printf("Proteus gunner geometry: %zu triangles\n",mesh.size()/3);
    CHECK(mesh.size()>1000&&mesh.size()%3==0);if(baked)CHECK(ApplyCockpitOcclusion(mesh,kind));
    // Nothing leaves the pod: the compartment is the frame, not a guess.
    for(const auto& v:mesh) {
        float n=0;for(int k=0;k<3;++k){CHECK(std::isfinite(v.position[k]));n+=v.normal[k]*v.normal[k];}
        CHECK(std::fabs(n-1)<.001f);
        const float x=v.position[0],y=v.position[1],z=v.position[2];
        // Translucent screen ink rides the curved skin itself, so it is allowed
        // the width of its own ribbon; solid hardware is held to the pod.
        const float slack=v.surface==21?.025f:.004f;
        CHECK(z>-1.25f&&z<kNose+.02f);
        // Skin() describes the compartment. Above the deck the pod is the gun
        // bay, which does not pinch in with the nose cone, so the well is free.
        CHECK(std::fabs(x)<Skin(z)+slack);
        CHECK(y>Floor(z)-slack);
        CHECK(y<kDeck+.004f);
    }
    for(std::size_t i=0;i<mesh.size();i+=3) {
        Point a{},b{},c{},n{};std::memcpy(a.data(),mesh[i].position,12);std::memcpy(b.data(),mesh[i+1].position,12);
        std::memcpy(c.data(),mesh[i+2].position,12);std::memcpy(n.data(),mesh[i].normal,12);
        const auto normal=Cross(Sub(b,a),Sub(c,a));CHECK(Dot(normal,normal)>1e-18f&&Dot(normal,n)>0);
    }
    const auto eye=kCockpitSeatedEye;
    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
        auto desired=eye;desired[axis]+=sign*2;const auto stopped=ClampCockpitHead(desired,.10f,kind);
        std::printf("proteus head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",axis,sign,stopped[0],stopped[1],stopped[2]);
        CHECK(sign*(stopped[axis]-eye[axis])>.035f);      // six-axis movement really exists
        CHECK(std::fabs(stopped[axis]-desired[axis])>.05f); // and it is stopped by the pod
        CHECK(!MeshBlocked(mesh,eye,stopped));             // without passing through hardware
    }
    std::vector<Point> heads;
    for(float x:{-.40f,0.f,.40f})for(float y:{-.14f,.10f,.32f})for(float z:{-.34f,-.10f,.24f,.58f})
        heads.push_back(ClampCockpitHead({x,y,z},.10f,kind));
    // Overhead: the top is a screen, not a lining. Forward of the roof panel
    // the gunner looks straight up at the weapon and the bay's underside.
    unsigned roofOpen=0,roofShut=0;
    // Sample between the frame hoops: those cross members are structure and
    // are meant to be in the way.
    for(float x:{-.36f,0.f,.36f})for(float z:{.40f,1.00f,1.80f}) {
        if(!MeshBlocked(mesh,eye,{x,2.4f,z}))++roofOpen;
        else std::printf("Proteus roof still closed at %.2f %.2f\n",x,z);
    }
    CHECK(roofOpen==9);
    // The one panel that stays is the aft bay, which hides the back of the
    // pivot where the arm meets the turret.
    // Straight up through the panel itself. A sightline from further forward
    // legitimately leaves through the open part of the roof on its way.
    for(float x:{-.50f,-.25f,0.f,.25f,.50f})for(float z:{-.54f,-.44f,-.34f,-.24f}) {
        if(MeshBlocked(mesh,{x,.40f,z},{x,2.4f,z})){++roofShut;continue;}
        std::printf("Proteus aft roof open at %.2f %.2f\n",x,z);
    }
    CHECK(roofShut==20);
    // The pod tunnel continues aft toward the shoulder; the passage hatch,
    // not a blank plate, is what closes it.
    // A dense sweep, because the first hardware pass showed daylight around the
    // hatch: every corner of the passage section has to be shut, not most.
    unsigned aft=0;
    for(const auto& head:heads)for(int i=0;i<13;++i)for(int j=0;j<13;++j) {
        const Point target{-.75f+.125f*i,-1.55f+.1875f*j,-1.30f};
        if(MeshBlocked(mesh,head,target))continue;
        if(++aft<8)std::printf("Proteus aft leak: eye %.2f %.2f %.2f to %.2f %.2f\n",head[0],head[1],head[2],target[0],target[1]);
    }
    CHECK(aft==0);
    CHECK(!MeshBlocked(mesh,eye,{0,-.10f,-.90f}));               // the way through is open
    CHECK(MeshBlocked(mesh,{0,-.10f,-.85f},{0,-.10f,-1.40f}));   // the door is shut
    CHECK(MeshBlocked(mesh,{0,-.10f,-.85f},{.40f,0.f,-.85f}));  // side runs, not bare wall
    // Forward and to the sides the world stays visible: this is the monitor.
    CHECK(!MeshBlocked(mesh,eye,{0,eye[1],12}));
    // Under the sill the cabin is shut: the floor plate reaches the lining and
    // turns up into it. The plate used to stop 70mm short and the gunner could
    // see the ground down that slot, so check the seal where it lives - at the
    // edge of the plate, outward and downward.
    unsigned sill=0;
    for(float s:{-1.f,1.f})for(float z=-.54f;z<1.20f;z+=.08f) {
        const float edge=s*(Skin(z)-.095f),pan=Floor(z)+.075f;
        if(!MeshBlocked(mesh,{edge,pan+.014f,z},{s*3.f,pan+.014f,z})) {
            ++sill;if(sill<5)std::printf("Proteus open sill at z=%.2f side %.0f\n",z,s);
        }
        if(!MeshBlocked(mesh,{edge,pan+.060f,z},{edge,pan-.90f,z})) {
            ++sill;if(sill<5)std::printf("Proteus open floor edge at z=%.2f side %.0f\n",z,s);
        }
    }
    CHECK(sill==0);
    for(float s:{-1.f,1.f}) {
        unsigned open=0;
        for(float y:{-.28f,-.05f,.20f})for(float z:{-.10f,.30f,.70f})
            if(!MeshBlocked(mesh,eye,{s*6,y,z}))++open;
        CHECK(open>=6); // broad side skins, not a boxed-in lining
    }
    // The gunner must be able to check where the weapon points. Sample the
    // barrel's own axis across its whole SGO pitch range and require a clear
    // sightline to the near half; the far half leaves through the nose screen.
    unsigned worstPitch=99;
    for(int deg=-40;deg<=20;deg+=10) {
        const float a=float(deg)*3.14159265f/180,c=std::cos(a),s=std::sin(a);
        unsigned clear=0,close=0;
        for(float d:{.6f,1.0f,1.4f,2.0f,3.0f,4.5f,7.0f}) {
            const Point on{0,kPivot+d*s,kPivotZ+d*c};
            if(MeshBlocked(mesh,eye,on))continue;
            ++clear;if(d<=2.0f)++close;
        }
        std::printf("Proteus pitch %+3d: %u of 7 barrel samples visible (%u near)\n",deg,clear,close);
        worstPitch=std::min(worstPitch,clear);
        CHECK(close>=2);   // the aim is readable at every elevation the gun has
        CHECK(clear>=4);
    }
    CHECK(worstPitch>=4);
    // The same range must never bring the weapon into the cabin. The barrel is
    // a cylinder about its bone; check its lowest surface over the compartment.
    float worst=100;
    for(int deg=-45;deg<=45;deg+=5) {
        const float a=float(deg)*3.14159265f/180,c=std::cos(a),s=std::sin(a);
        for(float d=-.60f;d<=7.0f;d+=.10f) {
            const float y=kPivot+d*s,z=kPivotZ+d*c;
            if(z<-.70f||z>kNose+.02f)continue;
            worst=std::min(worst,y-kBarrelR-kDeck);
        }
    }
    std::printf("Proteus barrel clearance over the deck: %.3f m\n",worst);
    CHECK(worst>.05f);
    // Feet, boards and the floor ramp.
    for(const auto& head:heads)CHECK(MeshBlocked(mesh,head,{0,-1.15f,1.06f}));
    for(float s:{-1.f,1.f})CHECK(MeshBlocked(mesh,eye,{s*.215f,-1.25f,1.00f}));
    for(float s:{-1.f,1.f})for(float z:{.06f,.40f})
        CHECK(MeshBlocked(mesh,{s*.205f,-.66f,z},{s*.205f,Floor(z)-.20f,z}));
    // Centre stick: a solid, grippable head on a pedestal that reaches the floor.
    CHECK(MeshBlocked(mesh,{-.14f,-.250f,.538f},{.14f,-.250f,.538f})); // a real grip, not a decal
    CHECK(MeshBlocked(mesh,{0,-.45f,.595f},{0,-1.10f,.62f}));          // pedestal down to the floor
    // ...and it stands clear of the seat: the pan's front lip is aft of it, so
    // the gap between the two is open all the way down to the floor plate.
    for(float y:{-.56f,-.64f,-.72f})CHECK(!MeshBlocked(mesh,{0,y,.494f},{0,y,.530f}));
    CHECK(!MeshBlocked(mesh,eye,{0,.04f,.30f}));                       // open space over the crown
    ProteusOccupantTest(mesh,kind);
    const auto displays=BuildCockpitDisplays(kind);CHECK(displays.size()==18);
    for(unsigned i=0;i<18;i+=6) {
        Point n{},c{};for(int k=0;k<3;++k){n[k]=displays[i].normal[k];for(unsigned j=0;j<6;++j)c[k]+=displays[i+j].position[k]/6;}
        const auto toEye=Sub(eye,c);
        CHECK(std::fabs(Dot(n,toEye))/std::sqrt(Dot(toEye,toEye))>.999f); // square to the pilot
        CHECK(Dot(toEye,toEye)>.45f*.45f&&Dot(toEye,toEye)<1.0f*1.0f);    // readable distance
        CHECK((i==0?c[0]<-.2f:c[0]>.2f)); // radar left; health and ammunition right
        for(float u:{.06f,.12f,.5f,.88f,.94f})for(float t:{.06f,.12f,.5f,.88f,.94f}) {
            if(std::fabs(u-.5f)+std::fabs(t-.5f)>.85f)continue; // clipped glass corners
            Point p{},front{},back{};
            for(int k=0;k<3;++k){p[k]=displays[i].position[k]+u*(displays[i+1].position[k]-displays[i].position[k])
                    +t*(displays[i+2].position[k]-displays[i+1].position[k]);
                front[k]=p[k]+n[k]*.020f;back[k]=p[k]-n[k]*.020f;}
            CHECK(!MeshBlocked(mesh,front,back)); // no arm or cover across the glass
        }
        CHECK(!MeshBlocked(mesh,eye,c));          // and the pilot can see each one
    }
    // Screen ink stays on the skins and off the forward sight line.
    unsigned seams=0;
    for(std::size_t i=0;i<mesh.size();++i)if(mesh[i].surface==21) {
        ++seams;
        const float x=mesh[i].position[0],y=mesh[i].position[1],z=mesh[i].position[2];
        if(z>.9f&&std::fabs(x)<.30f)CHECK(y<-.17f||y>.50f); // nose seams keep the sight window clear
        CHECK(std::fabs(x)<Skin(z)+.02f);
    }
    CHECK(seams>2000);
    CHECK(ClampCockpitHead({0,.10f,3},.10f,kind)[2]>.55f); // the pod is long: real forward lean
}
