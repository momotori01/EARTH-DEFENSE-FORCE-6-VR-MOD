// Helicopter cockpits (cockpit_heli_shells.h). The cabin: sane, the canopy's
// glass last in the mesh (the translucent pass), the displays let in, the view
// ahead through the windscreen and down through the foot monitor open, the
// head kept under the hood. The lining, from the research survey's hull when
// it is there (research/cockpit/helis, never shipped): inside the region, never
// on the glass, and every pane still open from the eye.
void HeliLayoutTest(bool baked=true) {
    for(const auto& shell:kHeliShells) {
        const auto kind=shell.kind;
        auto mesh=BuildCockpit(kind);
        std::printf("Heli cabin %u: %zu triangles\n",static_cast<unsigned>(kind),mesh.size()/3);
        CheckMeshSanity(mesh);
        if(baked)CHECK(ApplyCockpitOcclusion(mesh,kind));
        // A rebuilt machine (kHeliCockpits): its own shell, windows and
        // monitors; its screens hang in the view, see-through.
        bool rebuilt=false;for(const auto& c:kHeliCockpits)rebuilt=rebuilt||c.kind==kind;
        unsigned glass=0;for(std::size_t i=mesh.size();i>0&&(mesh[i-1].surface==21||mesh[i-1].surface==24);--i)++glass;
        CHECK(glass>=(rebuilt?3u:3*shell.paneCount));
        const auto displays=BuildCockpitDisplays(kind);CHECK(displays.size()==18);
        for(std::size_t i=0;i<displays.size();++i) {
            const bool clear=rebuilt&&!HeliScreenOpaque(kind,unsigned(i/6));
            CHECK(clear?displays[i].visibility[1]>.5f:displays[i].visibility[1]<.5f);
        }
        const auto eye=kCockpitSeatedEye;
        auto open=[&](Point dir,float reach){return !MeshBlocked(mesh,eye,{eye[0]+dir[0]*reach,eye[1]+dir[1]*reach,eye[2]+dir[2]*reach});};
        // Ahead, over the panel through the windscreen (a rebuilt one's beside
        // its centre frame); a tandem pilot looks over the front seat.
        if(rebuilt) {   // somewhere between level and 9 degrees down, near the middle
            bool ahead=false;
            for(float x:{0.f,.06f,.12f})for(float down:{-.15f,-.10f,-.07f,-.05f,0.f})ahead=ahead||open({x,down,1},3);
            CHECK(ahead);
        } else CHECK(open({0,shell.frontSeat>0?-.02f:-.15f,1},3));
        if(rebuilt) {
            // Down through its floor monitors (the zones that take in the
            // floor): past the legs' place, the keel, the stick and the
            // footrest, a sixth of it at least (the Euros's is only its
            // footwell, under a dashboard; the Heron's a narrow footwell, most
            // of it under the footrest, its look down ahead through the nose).
            const HeliCockpit* cockpit=nullptr;for(const auto& h:kHeliCockpits)if(h.kind==kind)cockpit=&h;
            unsigned seen=0,total=0;
            for(unsigned zi=0;zi<cockpit->zoneCount;++zi) {
                const auto& zone=cockpit->zones[zi];if(zone.xMin>0)continue;
                const float z1=std::min(zone.z1,cockpit->front);   // a zone may run on past the shell
                for(float x:{-.35f,-.25f,-.15f,.15f,.25f,.35f})for(int k=0;k<5;++k) {
                    const float z=zone.z0+.10f+(z1-zone.z0-.15f)*float(k)/4;
                    ++total;if(!MeshBlocked(mesh,eye,{x,cockpit->floorY-.20f,z}))++seen;
                }
            }
            std::printf("heli %u rebuilt: %u of %u points under the floor in view\n",static_cast<unsigned>(kind),seen,total);
            CHECK(seen*6>=total);
        } else {
            const float floorY=shell.floor-(shell.eye[1]-kCockpitSeatedEye[1]);unsigned seen=0,total=0;
            const float nose=shell.stations[shell.stationCount-1][0];
            auto wf=[&](float z){for(unsigned i=0;i+1<shell.stationCount;++i)if(z<=shell.stations[i+1][0]) {
                const float t=(z-shell.stations[i][0])/(shell.stations[i+1][0]-shell.stations[i][0]);
                return shell.stations[i][1]+(shell.stations[i+1][1]-shell.stations[i][1])*t;}return shell.stations[shell.stationCount-1][1];};
            const float front=std::min(.85f,shell.pilotNose-.08f);
            const float half=shell.sideBySide>0?std::min(.30f,shell.eye[0]-.10f):std::min(.44f,std::min(wf(.44f),wf(front))-.06f);
            for(float x:{-half+.04f,-half+.10f,-.04f,.04f,half-.10f,half-.04f})for(float z=.47f;z<=front-.03f;z+=(front-.50f)/4) {
                ++total;if(!MeshBlocked(mesh,eye,{x,floorY-.10f,z}))++seen;
            }
            std::printf("heli %u foot monitor: %u of %u in view\n",static_cast<unsigned>(kind),seen,total);
            // A narrow machine's pedals cover most of its small monitor.
            CHECK(half<.30f?seen>=1:seen*4>=total);
        }
        CHECK(!open({0,-1,0},1.6f));                 // the seat under the pilot
        CHECK(!open({0,.05f,-1},1.2f));              // the bulkhead behind
        // Every pane can be seen from the eye: its centre is not behind cabin furniture.
        unsigned hidden=0;
        for(unsigned i=0;i<shell.paneCount;++i) {
            const auto& pane=shell.panes[i];float c[3]{};
            for(unsigned k=0;k<pane.count;++k)for(int j=0;j<3;++j)c[j]+=pane.p[k][j]/pane.count;
            const auto p=HeliCabinPoint(shell,c);
            // Just short of the pane: its own frame and glass are in the mesh.
            const Point to{eye[0]+(p[0]-eye[0])*.9f,eye[1]+(p[1]-eye[1])*.9f,eye[2]+(p[2]-eye[2])*.9f};
            if(MeshBlocked(mesh,eye,to)&&++hidden<4)std::printf("heli %u pane %u hidden: %.3f %.3f %.3f\n",static_cast<unsigned>(kind),i,p[0],p[1],p[2]);
        }
        CHECK(hidden<=shell.paneCount/4);   // a few small quarter lights behind the shoulders may be
        for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
            auto desired=eye;desired[axis]+=sign*3;
            const auto stopped=ClampCockpitHead(desired,.10f,kind);
            std::printf("heli %u head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",static_cast<unsigned>(kind),axis,sign,stopped[0],stopped[1],stopped[2]);
            CHECK(sign*(stopped[axis]-eye[axis])>.02f);
            CHECK(!MeshBlocked(mesh,eye,stopped));
        }
        if(kind==CockpitKind::TruckPickup) {
            // The pickups' cab: out through both front door windows, and back
            // through the rear window between the headrests (to the gun: the
            // head turned round stands 15 cm left of the eye, past its own
            // headrest); each screen's middle in view (the gauges over the
            // wheel's rim).
            CHECK(open({1,.05f,.10f},3));CHECK(open({-1,0,.10f},3));
            CHECK(!MeshBlocked(mesh,{-.15f,.10f,-.10f},{-.45f,.10f,-2.f}));
            for(std::size_t i=0;i+6<=displays.size();i+=6) {
                Point c{};for(std::size_t k=0;k<6;++k)for(int j=0;j<3;++j)c[j]+=displays[i+k].position[j]/6;
                const Point to{eye[0]+(c[0]-eye[0])*.97f,eye[1]+(c[1]-eye[1])*.97f,eye[2]+(c[2]-eye[2])*.97f};
                CHECK(!MeshBlocked(mesh,eye,to));
            }
        }
    }
}
// The Brute's door gunner: a sane booth, its screens see-through on their
// arms, out of the door to the gun and the ground, walled fore and aft, the
// hatch shut behind, and the head free to lean out of the doorway but not
// through a wall.
void BruteGunnerTest(bool baked=true) {
    const auto kind=CockpitKind::HeliBruteGunner;
    auto mesh=BuildCockpit(kind);
    std::printf("Brute door gunner: %zu triangles\n",mesh.size()/3);
    CheckMeshSanity(mesh);
    if(baked)CHECK(ApplyCockpitOcclusion(mesh,kind));
    const auto displays=BuildCockpitDisplays(kind);CHECK(displays.size()==18);
    for(const auto& v:displays)CHECK(v.visibility[1]>.5f);
    const auto eye=kCockpitSeatedEye;
    auto open=[&](Point to){return !MeshBlocked(mesh,eye,to);};
    CHECK(open({0,.10f,3}));                 // straight out of the door
    CHECK(open({.30f,-1.50f,1.80f}));        // down to the gun on its stub wing
    CHECK(open({-.30f,-1.20f,1.80f}));
    CHECK(!open({0,.10f,-2}));               // the hatch behind
    for(float s:{-1.f,1.f})CHECK(!open({s*2,.10f,-.30f}));   // the walls fore and aft
    CHECK(!open({0,2,-.30f}));               // the ceiling
    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}) {
        auto desired=eye;desired[axis]+=sign*3;
        const auto stopped=ClampCockpitHead(desired,.10f,kind);
        std::printf("brute gunner head axis=%u sign=%.0f -> %.3f %.3f %.3f\n",axis,sign,stopped[0],stopped[1],stopped[2]);
        CHECK(sign*(stopped[axis]-eye[axis])>.02f);
        CHECK(!MeshBlocked(mesh,eye,stopped));
    }
    // A lean out of the door takes the head past the fuselage's line.
    CHECK(ClampCockpitHead({0,.10f,.60f},.10f,kind)[2]>.30f);
}
void HeliLiningTest(const std::filesystem::path& survey={},const std::filesystem::path& exportTo={}) {
    const char* stems[]={"vehicle409_heli","v602_heli","v506_heli","vehicle410_heli","v610_ptruck_gun","v610_ptruck_gun"};
    static_assert(std::size(stems)==std::size(kHeliShells));
    // The pickups' survey sits beside the helicopters' (research/cockpit/trucks).
    auto folder=[&](unsigned s){return kHeliShells[s].kind==CockpitKind::TruckPickup?survey.parent_path()/"trucks":survey;};
    for(unsigned s=0;s<std::size(kHeliShells);++s) {
        const auto& shell=kHeliShells[s];
        std::vector<float> hull;
        {
            std::string bodyName;for(const wchar_t* ch=shell.body;*ch;++ch)bodyName.push_back(static_cast<char>(*ch));
            std::ifstream f;if(!survey.empty())f.open(folder(s)/(std::string(stems[s])+"__"+bodyName+".obj"));
            std::string line;
            while(f&&std::getline(f,line))if(line.size()>2&&line[0]=='v'&&line[1]==' ') {
                std::istringstream in(line.substr(2));float x=0,y=0,z=0;
                if(in>>x>>y>>z){hull.push_back(x);hull.push_back(y);hull.push_back(z);}
            }
        }
        const bool real=hull.size()>=900&&hull.size()%9==0;
        if(!survey.empty())CHECK(real);
        if(!real) {   // a box round the canopy region
            const float x0=-shell.regionHalfWidth,x1=shell.regionHalfWidth,y0=shell.floor-.2f,y1=shell.eye[1]+.6f,z0=shell.regionRear-.3f,z1=shell.regionFront+.3f;
            const float c[8][3]={{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}};
            const int q[6][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{3,7,6,2},{0,4,7,3},{1,2,6,5}};   // outward
            for(const auto& f:q){const int tris[2][3]={{f[0],f[1],f[2]},{f[0],f[2],f[3]}};
                for(const auto& t:tris)for(int k=0;k<3;++k)for(int j=0;j<3;++j)hull.push_back(c[t[k]][j]);}
        }
        const auto lining=BuildHeliLining(shell.kind,hull);
        std::printf("Heli lining %u (%s hull, %zu triangles): %zu triangles\n",static_cast<unsigned>(shell.kind),real?"real":"box",hull.size()/9,lining.size()/3);
        const float o[3]={shell.bodyOrigin[0],shell.bodyOrigin[1],shell.bodyOrigin[2]};
        // For the preview only, beside the survey (never shipped).
        auto exportPreview=[&] {
            if(!real||exportTo.empty())return;
            std::ofstream f(exportTo/("lid_"+std::string(stems[s])+".obj")),k(exportTo/("keep_"+std::string(stems[s])+".obj"));
            f<<"# EDF6VR helicopter canopy lining from the research survey hull, hull bone local (+X model left); research only\n";
            for(const auto& v:lining)f<<"v "<<v.position[0]<<' '<<v.position[1]<<' '<<v.position[2]<<'\n';
            f<<"usemtl m0\n";
            for(std::size_t i=0;i<lining.size();i+=3)f<<"f "<<i+1<<' '<<i+2<<' '<<i+3<<'\n';
            k<<"# EDF6VR helicopter hull the game still draws (all but the glass and the monitor windows), hull bone local; research only\n";
            std::size_t n=0;
            for(std::size_t i=0;i+9<=hull.size();i+=9) {
                if(HeliGlass(shell,&hull[i])||HeliRebuiltGlass(shell,&hull[i])||HeliMonitorHull(shell,&hull[i])||HeliRebuiltHidden(shell,&hull[i]))continue;
                for(unsigned j=0;j<3;++j)k<<"v "<<hull[i+j*3]-o[0]<<' '<<hull[i+j*3+1]-o[1]<<' '<<hull[i+j*3+2]-o[2]<<'\n';
                k<<"f "<<n+1<<' '<<n+2<<' '<<n+3<<'\n';n+=3;
            }
            CHECK(bool(f)&&bool(k));
        };
        // A rebuilt machine is not lined: its own shell closes the cabin. On the
        // real hull, the one that asks for it has its own triangles under its
        // monitors hidden (the Euros's cockpit tub); the others none.
        bool rebuilt=false;float hideWidth=0;
        for(const auto& c:kHeliCockpits)if(c.kind==shell.kind){rebuilt=true;hideWidth=c.hideHalfWidth;}
        if(real) {
            unsigned hidden=0;for(std::size_t i=0;i+9<=hull.size();i+=9)if(HeliRebuiltHidden(shell,&hull[i]))++hidden;
            std::printf("heli %u: %u hull triangles hidden under its monitors\n",static_cast<unsigned>(shell.kind),hidden);
            CHECK(hideWidth>0?hidden>=20:hidden==0);
        }
        if(rebuilt){CHECK(lining.empty());exportPreview();continue;}
        CHECK(lining.size()>=6&&lining.size()%3==0);
        CHECK(BuildCombatLid(shell.kind,hull).size()==lining.size());   // the renderer's call gives the same
        unsigned outside=0,onGlass=0,facing=0;
        for(std::size_t i=0;i<lining.size();i+=3) {
            float t[9];for(int k=0;k<3;++k)for(int j=0;j<3;++j)t[k*3+j]=lining[i+k].position[j]+o[j];
            const float c[3]={(t[0]+t[3]+t[6])/3,(t[1]+t[4]+t[7])/3,(t[2]+t[5]+t[8])/3};
            float grown[3]={c[0]*.97f,c[1]-.02f,c[2]+(c[2]<shell.regionRear+.02f?.02f:c[2]>shell.regionFront-.02f?-.02f:0.f)};   // the 12 mm inset toward the seat
            // A piece cut at a monitor window's edge is part of a lined
            // triangle whose centre was checked; its own centre can fall anywhere.
            bool cut=false;
            for(unsigned m=0;m<shell.monitorCount;++m)for(unsigned e=0;e<shell.monitors[m].count;++e) {
                float n[3];HeliMonitorPlane(shell.monitors[m],e,n);
                for(int k=0;k<3;++k)cut=cut||std::fabs(n[0]*(t[k*3]-shell.eye[0])+n[1]*(t[k*3+1]-shell.eye[1])+n[2]*(t[k*3+2]-shell.eye[2]))<1e-3f;
            }
            if(!cut&&!HeliRegion(shell,c)&&!HeliRegion(shell,grown)&&++outside<4)std::printf("heli lining outside the region: %.3f %.3f %.3f\n",c[0],c[1],c[2]);
            if(!cut&&HeliGlass(shell,t))++onGlass;
            // Turned to the eye.
            const float e[3]={shell.eye[0]-c[0],shell.eye[1]-c[1],shell.eye[2]-c[2]};
            const auto& n=lining[i].normal;if(n[0]*e[0]+n[1]*e[1]+n[2]*e[2]<0)++facing;
        }
        CHECK(outside==0);CHECK(onGlass==0);CHECK(facing==0);
        // Inside a monitor window nothing is lined: the armour there is a
        // monitor onto the world.
        unsigned shown=0;
        for(const auto& v:lining){const float p[3]={v.position[0]+o[0],v.position[1]+o[1],v.position[2]+o[2]};if(HeliInMonitor(shell,p,1e-3f))++shown;}
        CHECK(shown==0);
        // From the eye every pane stays open through the lining: of its
        // centre and four points half way to its corners, two at least (a
        // real frame bar across a pane is lined like the rest of the hull).
        if(real) {
            const Point eye{shell.eye[0]-o[0],shell.eye[1]-o[1],shell.eye[2]-o[2]};
            unsigned closed=0;
            for(unsigned i=0;i<shell.paneCount;++i) {
                const auto& pane=shell.panes[i];float c[3]{};
                for(unsigned k=0;k<pane.count;++k)for(int j=0;j<3;++j)c[j]+=pane.p[k][j]/pane.count;
                unsigned open=0;
                for(unsigned k=0;k<5;++k) {
                    Point to{c[0]-o[0],c[1]-o[1],c[2]-o[2]};
                    if(k)for(int j=0;j<3;++j)to[j]=(c[j]+pane.p[(k-1)*pane.count/4][j])*.5f-o[j];
                    if(!MeshBlocked(lining,eye,to))++open;
                }
                if(open<2&&++closed<4)std::printf("heli pane %u closed by the lining\n",i);
            }
            CHECK(closed==0);
        }
        exportPreview();
    }
}
