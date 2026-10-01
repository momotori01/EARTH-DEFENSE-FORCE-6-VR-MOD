// The other crew member of the Proteus's tandem cabin (crew_figures.h),
// drawn with the cabin: built off the render thread the first time a rider of
// that class and look sits there, then kept (the last two). Included into
// cockpit_draw.cpp's anonymous namespace, after Constants.
std::wstring crewFolder;
CrewDrawStats crewStats{};
ComPtr<ID3D11SamplerState> crewSampler;ComPtr<ID3D11ShaderResourceView> crewDefaultParam;
struct CrewCpu {
    CrewFigure figure;std::vector<std::array<float,8>> presets;
    std::vector<std::vector<unsigned char>> albedo,mask,param;   // DDS bytes, one a part
    std::string error;bool ok=false;
};
struct CrewGpu {
    unsigned key=~0u;std::uint64_t used=0;
    ComPtr<ID3D11Buffer> vb,ib;
    struct Part {UINT first=0,count=0;ComPtr<ID3D11ShaderResourceView> albedo,mask,param;float colour0[4]{},colour1[4]{};};
    std::vector<Part> parts;std::vector<std::array<float,8>> presets;
};
CrewGpu crewCache[2];
struct CrewJob {unsigned key=~0u;std::future<std::shared_ptr<CrewCpu>> future;} crewJob;
unsigned crewFailedKey=~0u;

bool ReadBytes(const std::wstring& path,std::vector<unsigned char>& out) {
    out.clear();HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    LARGE_INTEGER size{};bool ok=GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<64ll*1024*1024;
    if(ok){out.resize(static_cast<std::size_t>(size.QuadPart));DWORD read=0;ok=ReadFile(file,out.data(),static_cast<DWORD>(out.size()),&read,nullptr)&&read==out.size();}
    CloseHandle(file);if(!ok)out.clear();return ok;
}
// Block-compressed DDS (BC1-BC5, BC7; legacy or DX10 header) as a shader resource.
bool CreateDdsTexture(ID3D11Device* d,const std::vector<unsigned char>& bytes,bool srgb,ComPtr<ID3D11ShaderResourceView>& out) {
    out.Reset();
    if(bytes.size()<128||std::memcmp(bytes.data(),"DDS ",4))return false;
    auto u32=[&](std::size_t at){std::uint32_t v=0;std::memcpy(&v,bytes.data()+at,4);return v;};
    const UINT height=u32(12),width=u32(16),mips=std::max(1u,u32(28));
    std::size_t at=128;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    const auto fourcc=u32(84);
    auto code=[](const char* s){return static_cast<std::uint32_t>(s[0])|static_cast<std::uint32_t>(s[1])<<8|static_cast<std::uint32_t>(s[2])<<16|static_cast<std::uint32_t>(s[3])<<24;};
    if(fourcc==code("DX10")){if(bytes.size()<148)return false;format=static_cast<DXGI_FORMAT>(u32(128));at=148;}
    else if(fourcc==code("DXT1"))format=DXGI_FORMAT_BC1_UNORM;
    else if(fourcc==code("DXT3"))format=DXGI_FORMAT_BC2_UNORM;
    else if(fourcc==code("DXT5"))format=DXGI_FORMAT_BC3_UNORM;
    else if(fourcc==code("ATI1")||fourcc==code("BC4U"))format=DXGI_FORMAT_BC4_UNORM;
    else if(fourcc==code("ATI2")||fourcc==code("BC5U"))format=DXGI_FORMAT_BC5_UNORM;
    unsigned block=0;
    switch(format) {
        case DXGI_FORMAT_BC1_UNORM:case DXGI_FORMAT_BC1_UNORM_SRGB:case DXGI_FORMAT_BC4_UNORM:block=8;break;
        case DXGI_FORMAT_BC2_UNORM:case DXGI_FORMAT_BC2_UNORM_SRGB:case DXGI_FORMAT_BC3_UNORM:case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_UNORM:case DXGI_FORMAT_BC7_UNORM:case DXGI_FORMAT_BC7_UNORM_SRGB:block=16;break;
        default:return false;
    }
    if(srgb) {
        if(format==DXGI_FORMAT_BC1_UNORM)format=DXGI_FORMAT_BC1_UNORM_SRGB;
        else if(format==DXGI_FORMAT_BC2_UNORM)format=DXGI_FORMAT_BC2_UNORM_SRGB;
        else if(format==DXGI_FORMAT_BC3_UNORM)format=DXGI_FORMAT_BC3_UNORM_SRGB;
        else if(format==DXGI_FORMAT_BC7_UNORM)format=DXGI_FORMAT_BC7_UNORM_SRGB;
    }
    if(!width||!height||width>8192||height>8192||mips>16)return false;
    D3D11_SUBRESOURCE_DATA levels[16]{};UINT w=width,h=height;
    for(UINT level=0;level<mips;++level) {
        const UINT pitch=std::max(1u,(w+3)/4)*block,rows=std::max(1u,(h+3)/4);
        if(at+static_cast<std::size_t>(pitch)*rows>bytes.size())return false;
        levels[level]={bytes.data()+at,pitch,pitch*rows};at+=static_cast<std::size_t>(pitch)*rows;
        w=std::max(1u,w/2);h=std::max(1u,h/2);
    }
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=mips;desc.ArraySize=1;desc.Format=format;
    desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(d->CreateTexture2D(&desc,levels,&texture))&&SUCCEEDED(d->CreateShaderResourceView(texture.Get(),nullptr,&out));
}
unsigned CrewKey(const CockpitRig& rig,CrewSeat& seat,bool& rearBuild) {
    rearBuild=rig.kind==CockpitKind::ProteusMissile;seat=rearBuild?CrewSeat::Driver:CrewSeat::Rear;
    return rig.crewKind|rig.crewModel<<4|(rearBuild?1u:0u)<<8;
}
std::shared_ptr<CrewCpu> BuildCrewCpu(std::wstring folder,unsigned kind,unsigned model,CrewSeat seat,bool rearBuild) {
    auto cpu=std::make_shared<CrewCpu>();
    if(!BuildCrewFigure(folder,kind,model,seat,rearBuild,cpu->figure,cpu->error))return cpu;
    LoadCrewColours(folder,kind,cpu->presets);
    for(const auto& part:cpu->figure.parts) {
        std::vector<unsigned char> a,m,p;
        if(!part.albedo.empty())ReadBytes(folder+L"\\"+std::wstring(part.albedo.begin(),part.albedo.end()),a);
        if(!part.mask.empty())ReadBytes(folder+L"\\"+std::wstring(part.mask.begin(),part.mask.end()),m);
        if(!part.param.empty())ReadBytes(folder+L"\\"+std::wstring(part.param.begin(),part.param.end()),p);
        cpu->albedo.push_back(std::move(a));cpu->mask.push_back(std::move(m));cpu->param.push_back(std::move(p));
    }
    cpu->ok=true;return cpu;
}
// The figure for this pose, if one is ready; starts building it otherwise.
const CrewGpu* CrewFor(ID3D11Device* d,const CockpitRig& rig,std::uint64_t frame) {
    if(rig.kind!=CockpitKind::ProteusDriver&&rig.kind!=CockpitKind::ProteusMissile)return nullptr;
    if(!rig.crewKind||rig.crewKind>4||rig.crewModel>3||crewFolder.empty())return nullptr;
    CrewSeat seat{};bool rearBuild=false;const unsigned key=CrewKey(rig,seat,rearBuild);
    for(auto& entry:crewCache)if(entry.key==key){entry.used=frame;return &entry;}
    if(crewJob.future.valid()) {
        if(crewJob.future.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return nullptr;
        const auto cpu=crewJob.future.get();const unsigned built=crewJob.key;crewJob.key=~0u;
        if(!cpu||!cpu->ok) {
            crewFailedKey=built;++crewStats.failures;
            std::snprintf(crewStats.note,sizeof(crewStats.note),"%s",cpu?cpu->error.c_str():"no result");
            return nullptr;
        }
        auto& slot=crewCache[crewCache[0].used<=crewCache[1].used?0:1];slot=CrewGpu{};
        const auto& f=cpu->figure;
        D3D11_BUFFER_DESC vb{};vb.ByteWidth=static_cast<UINT>(f.vertices.size()*sizeof(CockpitVertex));vb.BindFlags=D3D11_BIND_VERTEX_BUFFER;vb.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA vd{f.vertices.data(),0,0};
        D3D11_BUFFER_DESC ib{};ib.ByteWidth=static_cast<UINT>(f.indices.size()*sizeof(std::uint32_t));ib.BindFlags=D3D11_BIND_INDEX_BUFFER;ib.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA id{f.indices.data(),0,0};
        if(FAILED(d->CreateBuffer(&vb,&vd,&slot.vb))||FAILED(d->CreateBuffer(&ib,&id,&slot.ib))) {
            slot=CrewGpu{};crewFailedKey=built;++crewStats.failures;std::snprintf(crewStats.note,sizeof(crewStats.note),"buffers");return nullptr;
        }
        for(std::size_t i=0;i<f.parts.size();++i) {
            CrewGpu::Part part;part.first=f.parts[i].first;part.count=f.parts[i].count;
            CreateDdsTexture(d,cpu->albedo[i],true,part.albedo);CreateDdsTexture(d,cpu->mask[i],false,part.mask);CreateDdsTexture(d,cpu->param[i],false,part.param);
            for(int k=0;k<4;++k){part.colour0[k]=f.parts[i].colour0[k];part.colour1[k]=f.parts[i].colour1[k];}
            if(part.albedo)slot.parts.push_back(part);
        }
        slot.presets=cpu->presets;slot.key=built;slot.used=frame;++crewStats.builds;
        std::snprintf(crewStats.note,sizeof(crewStats.note),"%s %s: %zu vertices, %zu parts",CrewModelName(built&15,(built>>4)&15),
            (built>>8)&1?"driver seat":"rear seat",f.vertices.size(),slot.parts.size());
        if(built==key)return &slot;
    }
    if(key==crewFailedKey)return nullptr;
    crewJob.key=key;crewJob.future=std::async(std::launch::async,BuildCrewCpu,crewFolder,static_cast<unsigned>(rig.crewKind),static_cast<unsigned>(rig.crewModel),seat,rearBuild);
    return nullptr;
}
// After the cabin's solid geometry, with its depth: the figure, a draw a part.
void DrawCrewFigure(ID3D11DeviceContext* ctx,ID3D11Device* d,const CockpitPose& pose,Constants data,std::uint64_t frame) {
    const auto* crew=CrewFor(d,pose.rig,frame);if(!crew||crew->parts.empty())return;
    if(!crewSampler) {
        D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_ANISOTROPIC;s.MaxAnisotropy=8;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;s.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(d->CreateSamplerState(&s,&crewSampler)))return;
    }
    const auto original=data;
    // The game's own bindings in the slots the cabin pass does not save.
    ID3D11ShaderResourceView* oldViews[3]{};ctx->PSGetShaderResources(9,3,oldViews);
    ComPtr<ID3D11SamplerState> oldSampler;ctx->PSGetSamplers(2,1,&oldSampler);
    ComPtr<ID3D11Buffer> oldIndex;DXGI_FORMAT oldFormat{};UINT oldOffset=0;ctx->IAGetIndexBuffer(&oldIndex,&oldFormat,&oldOffset);
    // The cabin's own vertex buffer: its seams and monitors are drawn from it
    // right after (left bound to the figure's, the monitors went blank).
    ComPtr<ID3D11Buffer> oldVertices;UINT oldStride=0,oldVertexOffset=0;ctx->IAGetVertexBuffers(0,1,&oldVertices,&oldStride,&oldVertexOffset);
    // A part with no roughness map (it should not happen since the generator
    // reads every param_r_m_occ_* slot) is matte rather than a mirror.
    if(!crewDefaultParam) {
        const UINT texel=0x00FF0099u;   // R roughness .6, G metallic 0, B occlusion 1
        D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=1;t.MipLevels=t.ArraySize=1;t.Format=DXGI_FORMAT_R8G8B8A8_UNORM;t.SampleDesc.Count=1;
        t.Usage=D3D11_USAGE_IMMUTABLE;t.BindFlags=D3D11_BIND_SHADER_RESOURCE;D3D11_SUBRESOURCE_DATA s{&texel,4,4};
        ComPtr<ID3D11Texture2D> texture;if(SUCCEEDED(d->CreateTexture2D(&t,&s,&texture)))d->CreateShaderResourceView(texture.Get(),nullptr,&crewDefaultParam);
    }
    const UINT stride=sizeof(CockpitVertex),zero=0;auto* vb=crew->vb.Get();
    ctx->IASetVertexBuffers(0,1,&vb,&stride,&zero);ctx->IASetIndexBuffer(crew->ib.Get(),DXGI_FORMAT_R32_UINT,0);
    auto* smp=crewSampler.Get();ctx->PSSetSamplers(2,1,&smp);
    // The rider's own colours when read, else a preset, else the model's own.
    const bool own=pose.rig.crewColours,preset=!own&&pose.rig.crewPreset<crew->presets.size();
    for(const auto& part:crew->parts) {
        for(int k=0;k<4;++k) {
            data.crew0[k]=own?pose.rig.crewColour[0][k]:preset?crew->presets[pose.rig.crewPreset][k]:part.colour0[k];
            data.crew1[k]=own?pose.rig.crewColour[1][k]:preset?crew->presets[pose.rig.crewPreset][4+k]:part.colour1[k];
        }
        ctx->UpdateSubresource(constants.Get(),0,nullptr,&data,0,0);
        ID3D11ShaderResourceView* views[]={part.albedo.Get(),part.mask.Get(),part.param?part.param.Get():crewDefaultParam.Get()};ctx->PSSetShaderResources(9,3,views);
        ctx->DrawIndexed(part.count,part.first,0);
    }
    ++crewStats.draws;
    ctx->PSSetShaderResources(9,3,oldViews);for(auto* view:oldViews)if(view)view->Release();
    smp=oldSampler.Get();ctx->PSSetSamplers(2,1,&smp);
    ctx->IASetIndexBuffer(oldIndex.Get(),oldFormat,oldOffset);
    auto* restored=oldVertices.Get();ctx->IASetVertexBuffers(0,1,&restored,&oldStride,&oldVertexOffset);
    ctx->UpdateSubresource(constants.Get(),0,nullptr,&original,0,0);
}
