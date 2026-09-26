#include "hand_draw.h"
#include "hand_model.h"
#include "weapon_stereo.h"
#include <wrl/client.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
thread_local bool active=false,wantLeft=false,wantRight=false,viaLayer=false;
SRWLOCK lock=SRWLOCK_INIT;
int leftBones[kHandBoneMax+1]{},rightBones[kHandBoneMax+1]{};
int collapseBones[2]={-1,-1};   // HandCollapseSlots over both hands and cuffs
unsigned leftCount=0,rightCount=0,nodeCount=0;
int cuffBones[2]{-1,-1};

enum class Stage { Copying, Ready, Empty, Unsupported };
struct Mesh {
    // Retain source identities: a rebuilt model must not reuse an old pointer
    // and accidentally receive a previously cached hand selection.
    ComPtr<ID3D11Buffer> vertexBuffer,indexBuffer;
    UINT count=0,start=0; INT base=0; UINT stride=0,vertexOffset=0,indexOffset=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    Stage stage=Stage::Copying;
    ComPtr<ID3D11Buffer> vertexStaging,indexStaging,hands,handVertices;
    unsigned left=0,right=0;   // indices per hand in `hands`; left first
    unsigned attempts=0;
};
std::vector<Mesh> meshes;
HandDrawStats stats{};

void Note(const char* text) noexcept { std::snprintf(stats.note,sizeof(stats.note),"%s",text); }

bool Stage_(ID3D11Device* device,ID3D11DeviceContext* ctx,ID3D11Buffer* source,ComPtr<ID3D11Buffer>& staging) noexcept {
    D3D11_BUFFER_DESC desc{}; source->GetDesc(&desc);
    if(!desc.ByteWidth || desc.ByteWidth>256u*1024*1024) return false;
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    if(FAILED(device->CreateBuffer(&desc,nullptr,&staging))) return false;
    ctx->CopyResource(staging.Get(),source);
    return true;
}

// The two copies are asked for without waiting; the GPU finishes them within
// a frame or two and the mesh is built on the first call that finds both ready.
void Build(ID3D11DeviceContext* ctx,Mesh& mesh) noexcept {
    ComPtr<ID3D11Device> device; ctx->GetDevice(&device);
    D3D11_MAPPED_SUBRESOURCE vertices{},indices{};
    if(FAILED(ctx->Map(mesh.vertexStaging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&vertices))) return;
    if(FAILED(ctx->Map(mesh.indexStaging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&indices))) {
        ctx->Unmap(mesh.vertexStaging.Get(),0); return;
    }
    D3D11_BUFFER_DESC vd{},id{}; mesh.vertexStaging->GetDesc(&vd); mesh.indexStaging->GetDesc(&id);
    const bool wide=mesh.format==DXGI_FORMAT_R32_UINT;
    const unsigned indexBytes=wide?4:2;
    const std::size_t indexEnd=static_cast<std::size_t>(mesh.indexOffset)+(static_cast<std::size_t>(mesh.start)+mesh.count)*indexBytes;
    // Every player model puts the four float weights twenty bytes from the end
    // of the vertex and the four byte bone indices in the last four.
    HandVertexLayout layout{mesh.stride,mesh.stride>=20?mesh.stride-20:0,mesh.stride>=4?mesh.stride-4:0};
    const auto* vertexBytes=static_cast<const unsigned char*>(vertices.pData)+mesh.vertexOffset;
    const std::size_t vertexSize=vd.ByteWidth>mesh.vertexOffset?vd.ByteWidth-mesh.vertexOffset:0;
    bool supported=mesh.stride>=24 && indexEnd<=id.ByteWidth && (mesh.format==DXGI_FORMAT_R16_UINT || wide) && vertexSize>=mesh.stride;
    if(supported) {
        // The layout is checked on the data itself: weights that sum to one and
        // bones that exist. A stride that puts them elsewhere fails this.
        const std::size_t vertexCount=vertexSize/mesh.stride;
        for(std::size_t v=0;v<vertexCount && v<256 && supported;v+=7) {
            float w[4]; std::memcpy(w,vertexBytes+v*mesh.stride+layout.weightOffset,16);
            float sum=0; for(float x:w) { if(!(x>=-0.001f && x<=1.001f)) supported=false; sum+=x; }
            if(!(sum>0.98f && sum<1.02f)) supported=false;
            const unsigned char* b=vertexBytes+v*mesh.stride+layout.indexOffset;
            for(int k=0;k<4;++k) if(w[k]>0.001f && b[k]>=nodeCount) supported=false;
        }
    }
    if(!supported) {
        mesh.stage=Stage::Unsupported; Note("vertex layout not recognised");
    } else {
        const auto* indexBytesAt=static_cast<const unsigned char*>(indices.pData)+mesh.indexOffset+static_cast<std::size_t>(mesh.start)*indexBytes;
        std::vector<std::uint32_t> left(mesh.count),right(mesh.count);
        HandBoneSet l{leftBones,leftCount},r{rightBones,rightCount};
        const auto picked=SelectHandTriangles(vertexBytes,vertexSize,layout,
            wide?nullptr:reinterpret_cast<const std::uint16_t*>(indexBytesAt),
            wide?reinterpret_cast<const std::uint32_t*>(indexBytesAt):nullptr,
            mesh.count,mesh.base,l,r,left.data(),right.data(),mesh.count,true);
        mesh.left=picked.left; mesh.right=picked.right;
        if(!picked.left && !picked.right) mesh.stage=Stage::Empty;
        else {
            std::vector<int> leftPosed(leftBones,leftBones+leftCount),rightPosed(rightBones,rightBones+rightCount);
            if(cuffBones[0]>=0)leftPosed.push_back(cuffBones[0]);
            if(cuffBones[1]>=0)rightPosed.push_back(cuffBones[1]);
            auto isolated=IsolateHandMesh(vertexBytes,vertexSize,layout,left.data(),picked.left,right.data(),picked.right,mesh.base,
                {leftPosed.data(),static_cast<unsigned>(leftPosed.size())},{rightPosed.data(),static_cast<unsigned>(rightPosed.size())},
                collapseBones[0],collapseBones[1],cuffBones[0],cuffBones[1]);
            mesh.left=isolated.leftIndices;mesh.right=isolated.rightIndices;
            D3D11_BUFFER_DESC desc{}; desc.ByteWidth=static_cast<UINT>(isolated.indices.size()*4); desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA data{isolated.indices.data(),0,0};
            bool ready=isolated.valid && device && SUCCEEDED(device->CreateBuffer(&desc,&data,&mesh.hands));
            if(ready) {
                desc.ByteWidth=static_cast<UINT>(isolated.vertices.size());desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
                data.pSysMem=isolated.vertices.data();ready=SUCCEEDED(device->CreateBuffer(&desc,&data,&mesh.handVertices));
            }
            if(ready) {
                mesh.stage=Stage::Ready; stats.trianglesLeft+=picked.left/3; stats.trianglesRight+=picked.right/3;
                stats.trianglesPartial+=picked.partial;stats.verticesReweighted+=isolated.reweighted;
                stats.verticesCollapsed+=isolated.collapsed; stats.verticesTapered+=isolated.tapered;
            } else { mesh.stage=Stage::Unsupported; Note(isolated.valid?"hand buffer allocation failed":"hand skin isolation rejected"); }
        }
    }
    ctx->Unmap(mesh.vertexStaging.Get(),0); ctx->Unmap(mesh.indexStaging.Get(),0);
    mesh.vertexStaging.Reset(); mesh.indexStaging.Reset();
}
}

HandDrawScope::HandDrawScope(bool left,bool right,bool layer) noexcept { active=true; wantLeft=left; wantRight=right; viaLayer=layer; }
HandDrawScope::~HandDrawScope() noexcept { active=false; wantLeft=wantRight=viaLayer=false; }

void SetHandBones(const int* left,unsigned lc,const int* right,unsigned rc,unsigned nodes,int leftCuff,int rightCuff) noexcept {
    AcquireSRWLockExclusive(&lock);
    leftCount=lc<=kHandBoneMax+1?lc:0; rightCount=rc<=kHandBoneMax+1?rc:0; nodeCount=nodes;
    cuffBones[0]=leftCuff>=0&&static_cast<unsigned>(leftCuff)<nodes?leftCuff:-1;
    cuffBones[1]=rightCuff>=0&&static_cast<unsigned>(rightCuff)<nodes?rightCuff:-1;
    for(unsigned i=0;i<leftCount;++i) leftBones[i]=left[i];
    for(unsigned i=0;i<rightCount;++i) rightBones[i]=right[i];
    {
        int used[2*(kHandBoneMax+1)+2]; unsigned n=0;
        for(unsigned i=0;i<leftCount;++i) used[n++]=leftBones[i];
        for(unsigned i=0;i<rightCount;++i) used[n++]=rightBones[i];
        used[n++]=cuffBones[0]; used[n++]=cuffBones[1];
        HandCollapseSlots(used,n,nodeCount,collapseBones);
    }
    meshes.clear(); stats.meshes=0; stats.trianglesLeft=stats.trianglesRight=stats.trianglesPartial=stats.verticesReweighted=0;
    ReleaseSRWLockExclusive(&lock);
}
void ResetHandMeshes() noexcept {
    AcquireSRWLockExclusive(&lock);
    meshes.clear(); stats.meshes=0; stats.trianglesLeft=stats.trianglesRight=stats.trianglesPartial=stats.verticesReweighted=0;
    ReleaseSRWLockExclusive(&lock);
}

bool HandDrawIntercept(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base) noexcept {
    if(!active || !ctx) return false;
    AcquireSRWLockExclusive(&lock);
    ComPtr<ID3D11Buffer> vb,ib; UINT stride=0,vOffset=0,iOffset=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    ctx->IAGetVertexBuffers(0,1,&vb,&stride,&vOffset);
    ctx->IAGetIndexBuffer(&ib,&format,&iOffset);
    Mesh* mesh=nullptr;
    for(auto& m:meshes)
        if(m.vertexBuffer.Get()==vb.Get() && m.indexBuffer.Get()==ib.Get() && m.count==count && m.start==start && m.base==base
           && m.stride==stride && m.vertexOffset==vOffset && m.indexOffset==iOffset && m.format==format) { mesh=&m; break; }
    if(!mesh) {
        // Buffers are recreated between missions; stale keys are let go together.
        if(meshes.size()>=8) { meshes.clear(); stats.meshes=0; stats.trianglesLeft=stats.trianglesRight=stats.trianglesPartial=stats.verticesReweighted=0; }
        if(vb && ib && count>=3) {
            Mesh m{}; m.vertexBuffer=vb.Get(); m.indexBuffer=ib.Get(); m.count=count; m.start=start; m.base=base;
            m.stride=stride; m.vertexOffset=vOffset; m.indexOffset=iOffset; m.format=format;
            ComPtr<ID3D11Device> device; ctx->GetDevice(&device);
            if(device && Stage_(device.Get(),ctx,vb.Get(),m.vertexStaging) && Stage_(device.Get(),ctx,ib.Get(),m.indexStaging)) {
                meshes.push_back(std::move(m)); mesh=&meshes.back(); ++stats.meshes;
            } else Note("staging copy failed");
        }
    }
    bool handled=true;
    if(mesh && mesh->stage==Stage::Copying) {
        // Until the copy is back, the whole body stays hidden: a frame or two
        // without hands rather than a frame with the body in the picture.
        if(++mesh->attempts<600) Build(ctx,*mesh); else { mesh->stage=Stage::Unsupported; Note("staging copy never completed"); }
        ++stats.pending;
    } else if(mesh && mesh->stage==Stage::Ready) {
        auto* handVertex=mesh->handVertices.Get();const UINT handOffset=0;
        ctx->IASetVertexBuffers(0,1,&handVertex,&stride,&handOffset);
        ctx->IASetIndexBuffer(mesh->hands.Get(),DXGI_FORMAT_R32_UINT,0);
        // Into the layer, or not at all: a native copy would sit in the world's
        // own camera, a frame behind the layer's, and show as a second hand.
        auto draw=[&](UINT n,UINT first) {
            if(viaLayer) { if(ReplayHandStereo(ctx,n,first,0)) ++stats.layerDraws; else ++stats.layerRejected; return; }
            ctx->DrawIndexed(n,first,0);
        };
        if(wantLeft && mesh->left) draw(mesh->left,0);
        if(wantRight && mesh->right) draw(mesh->right,mesh->left);
        ctx->IASetIndexBuffer(ib.Get(),format,iOffset);
        auto* originalVertex=vb.Get();ctx->IASetVertexBuffers(0,1,&originalVertex,&stride,&vOffset);
        ++stats.handDraws;
    } else ++stats.dropped;
    ReleaseSRWLockExclusive(&lock);
    return handled;
}

void ReadHandCollapseSlots(int out[2]) noexcept {
    AcquireSRWLockShared(&lock); out[0]=collapseBones[0]; out[1]=collapseBones[1]; ReleaseSRWLockShared(&lock);
}

HandDrawStats ReadHandDrawStats() noexcept {
    AcquireSRWLockShared(&lock); auto copy=stats; ReleaseSRWLockShared(&lock); return copy;
}
}
