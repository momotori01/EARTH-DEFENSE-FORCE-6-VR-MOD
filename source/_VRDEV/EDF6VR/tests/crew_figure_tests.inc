// The crew figures built from the generated data on this PC (Mods/Plugins/
// EDF6VRCrew, tools/edf6/crew_figures.py): every class and look, on either
// seat of the tandem cabin and seen from either station. Seated on the
// cushion, head over the hips, wrists on the controls, nothing non-finite.
void CrewFigureTest(const wchar_t* folder) {
    unsigned built=0;
    for(unsigned kind=1;kind<=4;++kind) {
        std::vector<std::array<float,8>> presets;CHECK(LoadCrewColours(folder,kind,presets)&&presets.size()>=2);
        for(unsigned model=0;model<4;++model)for(const auto seat:{CrewSeat::Driver,CrewSeat::Rear})for(const bool rearBuild:{false,true}) {
            CrewFigure figure;std::string error;
            const bool ok=BuildCrewFigure(folder,kind,model,seat,rearBuild,figure,error);
            if(!ok)std::printf("crew %s %s: %s\n",CrewModelName(kind,model),seat==CrewSeat::Rear?"rear":"driver",error.c_str());
            CHECK(ok);if(!ok)continue;
            // Seat frame: undo the build shift.
            std::array<float,3> offset{0,0,0};
            if(!rearBuild&&seat==CrewSeat::Rear)offset=kProteusCabinRearSeat;
            if(rearBuild&&seat==CrewSeat::Driver)for(int k=0;k<3;++k)offset[k]=-kProteusCabinRearSeat[k];
            float hips[3],head[3];for(int k=0;k<3;++k){hips[k]=figure.hips[k]-offset[k];head[k]=figure.head[k]-offset[k];}
            float reach=0,feet=0;
            for(int side=0;side<2;++side) {
                float d=0,f=0;for(int k=0;k<3;++k){const float e=figure.wrist[side][k]-figure.target[side][k],g=figure.ankle[side][k]-figure.footTarget[side][k];d+=e*e;f+=g*g;}
                reach=std::max(reach,std::sqrt(d));feet=std::max(feet,std::sqrt(f));
            }
            if(!rearBuild&&seat==CrewSeat::Rear||rearBuild&&seat==CrewSeat::Driver)
                std::printf("crew %-22s %-6s hips=(%.2f,%.2f,%.2f) head=(%.2f,%.2f,%.2f) grip miss=%.3f feet miss=%.3f m lean %.0f deg clearance %.3f/%.3f (%s/%s), %zu vertices\n",CrewModelName(kind,model),
                    seat==CrewSeat::Rear?"rear":"driver",hips[0],hips[1],hips[2],head[0],head[1],head[2],reach,feet,figure.lean*57.29578f,figure.gripClearance[0],figure.gripClearance[1],figure.gripWorst[0].c_str(),figure.gripWorst[1].c_str(),figure.vertices.size());
            CHECK(std::fabs(hips[0])<.02f&&hips[1]>-.62f&&hips[1]<-.35f&&hips[2]>-.35f&&hips[2]<.20f);
            CHECK(head[1]>hips[1]+.40f&&head[1]<hips[1]+.90f);
            CHECK(reach<.03f&&feet<.03f&&figure.gripClearance[0]>-.006f&&figure.gripClearance[1]>-.006f);
            if(model==0&&seat==CrewSeat::Rear&&!rearBuild)std::printf("   L wrist (%.3f,%.3f,%.3f) target (%.3f,%.3f,%.3f) ankle (%.3f,%.3f,%.3f)\n",figure.wrist[0][0],figure.wrist[0][1],figure.wrist[0][2],figure.target[0][0],figure.target[0][1],figure.target[0][2],figure.ankle[0][0],figure.ankle[0][1],figure.ankle[0][2]);
            CHECK(!figure.parts.empty()&&!figure.indices.empty());
            ++built;
        }
    }
    std::printf("Crew figures built: %u\n",built);CHECK(built==64);
}
// Research views: the driver's cabin with a figure on each seat, as OBJ
// (metres, cabin frame: +X right, +Y up, +Z forward, origin the driver's eye).
void CrewFigureExport(const wchar_t* folder,const std::filesystem::path& out,unsigned kind,unsigned model) {
    std::filesystem::create_directories(out);
    auto write=[&](const std::filesystem::path& path,const std::vector<CockpitVertex>& v,const std::vector<std::uint32_t>* indices) {
        std::ofstream f(path);
        for(const auto& p:v)f<<"v "<<p.position[0]<<' '<<p.position[1]<<' '<<p.position[2]<<'\n';
        if(indices)for(std::size_t i=0;i+2<indices->size();i+=3)f<<"f "<<(*indices)[i]+1<<' '<<(*indices)[i+1]+1<<' '<<(*indices)[i+2]+1<<'\n';
        else for(std::size_t i=0;i+2<v.size();i+=3)f<<"f "<<i+1<<' '<<i+2<<' '<<i+3<<'\n';
    };
    write(out/L"cabin_driver.obj",BuildCockpit(CockpitKind::ProteusDriver),nullptr);
    {
        std::ofstream f(out/L"grip_right.txt");
        for(const auto seat:{CrewSeat::Driver,CrewSeat::Rear}) {
            CrewFigure figure;std::string error;CHECK(BuildCrewFigure(folder,kind,model,seat,false,figure,error));
            std::printf("%s fingers L %.2f %.2f %.2f %.2f %.2f R %.2f %.2f %.2f %.2f %.2f roll %.0f/%.0f deg\n",seat==CrewSeat::Rear?"rear":"driver",
                figure.fingerClose[0][0],figure.fingerClose[0][1],figure.fingerClose[0][2],figure.fingerClose[0][3],figure.fingerClose[0][4],
                figure.fingerClose[1][0],figure.fingerClose[1][1],figure.fingerClose[1][2],figure.fingerClose[1][3],figure.fingerClose[1][4],
                figure.wristRoll[0]*57.29578f,figure.wristRoll[1]*57.29578f);
            f<<figure.target[1][0]<<' '<<figure.target[1][1]<<' '<<figure.target[1][2]<<'\n';
        }
    }
    for(const auto seat:{CrewSeat::Driver,CrewSeat::Rear}) {
        CrewFigure figure;std::string error;
        CHECK(BuildCrewFigure(folder,kind,model,seat,false,figure,error));
        write(out/(seat==CrewSeat::Rear?L"figure_rear.obj":L"figure_driver.obj"),figure.vertices,&figure.indices);
    }
}
// The cabin pass draws the figure: rendered on WARP from each station looking
// at the other seat, the picture changes once the other seat is taken (the
// figure is built in the background, so the first frames show nothing).
void CrewDrawTest(const wchar_t* folder,const wchar_t* pictures=nullptr) {
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> ctx;CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,nullptr,&ctx)));if(!d)return;
    constexpr UINT width=480,height=360;D3D11_TEXTURE2D_DESC td{};td.Width=width;td.Height=height;td.ArraySize=td.MipLevels=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> texture;ComPtr<ID3D11RenderTargetView> rtv;CHECK(SUCCEEDED(d->CreateTexture2D(&td,nullptr,&texture)));CHECK(SUCCEEDED(d->CreateRenderTargetView(texture.Get(),nullptr,&rtv)));
    Matrix projection{};projection.m[0][0]=.90f/(float(width)/height);projection.m[1][1]=.90f;projection.m[2][3]=-1;
    projection.m[2][2]=1000.f/(.1f-1000.f);projection.m[3][2]=.1f*1000.f/(.1f-1000.f);
    ConfigureCrewFigures(folder);
    const float blue[]={.18f,.42f,.68f,1};
    struct Case {CockpitKind kind;float yaw;Point eye;unsigned crew;};
    for(const Case c:{Case{CockpitKind::ProteusDriver,180,{0,.10f,-.10f},1},Case{CockpitKind::ProteusMissile,0,{0,.10f,-.10f},4}}) {
        CockpitPose pose{};pose.cabin=Identity();pose.rig=Rig();pose.rig.kind=c.kind;
        Matrix camera{};CHECK(RotateCamera(Identity(),c.yaw,-10,camera));for(int k=0;k<3;++k)camera.m[3][k]=c.eye[k];
        ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose));
        const auto empty=ReadPixels(d.Get(),ctx.Get(),texture.Get());
        pose.rig.crewKind=static_cast<unsigned char>(c.crew);
        const auto before=ReadCrewDrawStats().draws;const auto deadline=GetTickCount64()+30000;
        std::vector<unsigned char> taken;
        for(std::uint64_t frame=1;GetTickCount64()<deadline;++frame) {
            ctx->ClearRenderTargetView(rtv.Get(),blue);CHECK(DrawCockpit(ctx.Get(),texture.Get(),Inverse(camera),projection,pose,nullptr,frame));
            if(ReadCrewDrawStats().draws>before){taken=ReadPixels(d.Get(),ctx.Get(),texture.Get());break;}
            Sleep(20);
        }
        const auto stats=ReadCrewDrawStats();
        std::printf("Crew draw %s: builds=%u failures=%u draws=%llu %s\n",c.kind==CockpitKind::ProteusDriver?"driver cabin, rear seat":"missile cabin, driver seat",
            stats.builds,stats.failures,stats.draws,stats.note);
        CHECK(!taken.empty()&&stats.failures==0);if(taken.empty())continue;
        unsigned changed=0;for(std::size_t i=0;i<taken.size();i+=4)if(std::abs(int(taken[i])-empty[i])+std::abs(int(taken[i+1])-empty[i+1])+std::abs(int(taken[i+2])-empty[i+2])>24)++changed;
        std::printf("   pixels changed by the figure: %u of %u\n",changed,width*height);
        if(pictures) {
            std::ofstream out(std::filesystem::path(pictures)/(c.kind==CockpitKind::ProteusDriver?L"crew_driver_cabin.ppm":L"crew_missile_cabin.ppm"),std::ios::binary);
            out<<"P6\n"<<width<<' '<<height<<"\n255\n";for(std::size_t i=0;i<taken.size();i+=4)out.write(reinterpret_cast<const char*>(&taken[i]),3);
        }
        CHECK(changed>2000);
    }
    ReleaseCockpitDraw();ConfigureCrewFigures(L"");
}
