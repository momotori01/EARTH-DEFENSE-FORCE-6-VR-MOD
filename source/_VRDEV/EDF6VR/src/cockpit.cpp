#include "cockpit.h"
#include "cockpit_combat_shells.h"
#include "cockpit_heli_shells.h"
#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace edf6vr {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
CockpitHistory history;
std::atomic<bool> enabled{false};
#include "cockpit_crawler_base.inc"
#include "cockpit_proteus_shroud.inc"
std::uint64_t PositionKey(const unsigned char* position) noexcept {
    std::uint64_t key=14695981039346656037ull;
    for(unsigned i=0;i<6;++i)key=(key^position[i])*1099511628211ull;
    return key;
}
bool CrawlerBasePosition(const unsigned char* vertex) noexcept {
    return std::binary_search(std::begin(crawlerBasePositions),std::end(crawlerBasePositions),PositionKey(vertex));
}
// Proteus pods carry the gun bay and the gunner's compartment on one bone.
// Only the compartment goes; the bay above stays so the weapon still grows
// out of real structure when the rider looks up the well. POSITION sits at
// +24 in this model's 60 byte vertex.
bool ProteusShroudPosition(const unsigned char* vertex) noexcept {
    return std::binary_search(std::begin(proteusShroudPositions),std::end(proteusShroudPositions),PositionKey(vertex+24));
}
float Half(const unsigned char* at) noexcept {
    std::uint16_t h=0;std::memcpy(&h,at,2);
    const std::uint32_t sign=(h>>15)&1,exponent=(h>>10)&31,mantissa=h&1023;
    float value=exponent==0?std::ldexp(float(mantissa),-24):exponent==31?1e30f:std::ldexp(float(mantissa|1024),int(exponent)-25);
    return sign?-value:value;
}
// Tank barrels keep only what lies ahead of their cut plane: the part inside
// the turret would sit in the cabin, and a dish covers the cut.
bool BarrelCut(const unsigned char* vertex,unsigned stride,const float* w,const unsigned char* b,const CockpitRig& rig) noexcept {
    if(!rig.cutCount)return false;
    const unsigned offset=stride==60?rig.cutPosition60:stride==68?rig.cutPosition68:255;
    if(offset>=stride)return false;
    const float z=Half(vertex+offset+4);
    float cut=0;
    for(unsigned j=0;j<4;++j)if(w[j]>.001f)for(unsigned i=0;i<rig.cutCount&&i<rig.cutBones.size();++i)
        if(b[j]==rig.cutBones[i]&&z<rig.cutZ[i]){cut+=w[j];break;}
    return cut>=.5f;
}
}
void CockpitHistory::Reset() noexcept {next=count=0;}
void CockpitHistory::Record(const CockpitPose& pose) noexcept {
    if(!pose.at || !pose.rig.model || !pose.rig.nodes || !pose.rig.resource || !pose.rig.nodeCount || pose.rig.nodeCount>256 ||
       !ValidCamera(pose.camera) || !ValidCamera(pose.cabin)) return;
    entries[next]=pose;next=(next+1)%unsigned(entries.size());if(count<entries.size())++count;
}
bool CockpitHistory::Match(const Matrix& view,std::uint64_t now,CockpitPose& out) const noexcept {
    Matrix camera{};if(!InvertCamera(view,camera))return false;
    float best=1e30f;bool found=false;
    for(unsigned n=0;n<count;++n) {
        const auto& p=entries[(next+unsigned(entries.size())-1-n)%unsigned(entries.size())];
        if(now<p.at || now-p.at>250) continue;
        float r=0,t=0;
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) {const float d=p.camera.m[i][j]-camera.m[i][j];r+=d*d;}
        for(int j=0;j<3;++j) {const float d=p.camera.m[3][j]-camera.m[3][j];t+=d*d;}
        if(r>.002f*.002f || t>.02f*.02f) continue;
        const float score=4*std::sqrt(r)+std::sqrt(t);
        if(score<best) {best=score;out=p;found=true;}
    }
    return found;
}
void ConfigureCockpit(bool on) noexcept {if(enabled.exchange(on)!=on)ClearCockpitPose();}
bool CockpitEnabled() noexcept {return enabled.load();}
void ClearCockpitPose() noexcept {AcquireSRWLockExclusive(&lock);history.Reset();ReleaseSRWLockExclusive(&lock);}
void PublishCockpit(const CockpitPose& p) noexcept {if(!enabled)return;AcquireSRWLockExclusive(&lock);history.Record(p);ReleaseSRWLockExclusive(&lock);}
bool MatchCockpit(const Matrix& view,CockpitPose& out) noexcept {
    if(!enabled)return false;AcquireSRWLockShared(&lock);const bool ok=history.Match(view,GetTickCount64(),out);ReleaseSRWLockShared(&lock);return ok;
}
Matrix CockpitLocalToWorld(const Matrix& cabin,bool mirrored) noexcept {
    Matrix m=cabin;if(!mirrored)for(int j=0;j<3;++j)m.m[0][j]=-m.m[0][j];return m;
}
Matrix CockpitSeatedCamera(const Matrix& cabin) noexcept {
    auto eye=cabin;const auto basis=CockpitLocalToWorld(cabin);
    for(int j=0;j<3;++j)for(int k=0;k<3;++k)eye.m[3][j]+=kCockpitSeatedEye[k]*basis.m[k][j];
    return eye;
}
CockpitSelection SelectCockpitLimbs(const unsigned char* vertices,std::size_t bytes,unsigned stride,
    const void* indices,unsigned count,bool wide,int base,const CockpitRig& rig,std::vector<std::uint32_t>& out) noexcept {
    CockpitSelection result{};out.clear();
    if(!vertices||!indices||stride<24||stride>256||bytes<stride||!count||count%3||count>3000000||!rig.nodeCount||rig.nodeCount>256)return result;
    out.reserve(count);
    const std::size_t vertexCount=bytes/stride;
    auto index=[&](unsigned i){std::uint32_t v=0;if(wide)std::memcpy(&v,static_cast<const unsigned char*>(indices)+i*4,4);
        else {std::uint16_t s=0;std::memcpy(&s,static_cast<const unsigned char*>(indices)+i*2,2);v=s;}return v;};
    // Combat vehicles: the hull above the sill ahead of the cockpit's rear is
    // cut away (cockpit_combat_shells.h); its triangles meeting the cut are the
    // lid's to draw. POSITION is three halves at byte 0 in their 60/68 byte vertices.
    const auto* combat=(stride==60||stride==68)?FindCombatShell(rig.kind):nullptr;
    // Helicopters: the canopy's glass is left out, so the windows are clear,
    // and so is the hull in a monitor window (HeliMonitorHull) and a rebuilt
    // machine's own triangles inside its fuselage under a monitor (HeliRebuiltHidden).
    const auto* heli=(stride==60||stride==68)?FindHeliShell(rig.kind):nullptr;
    // The Brute's door gunner: the fuselage round his doorway goes (left door,
    // capVariant 0, is model +X), his own walls and hatch take its place.
    const bool doorGunner=(stride==60||stride==68)&&rig.kind==CockpitKind::HeliBruteGunner;
    for(unsigned t=0;t<count;t+=3) {
        bool keep=true,hullOnly=combat!=nullptr||heli!=nullptr||doorGunner;std::uint32_t tri[3]{};float at[9]{};
        for(unsigned k=0;k<3;++k) {
            tri[k]=index(t+k);const auto vi=static_cast<std::int64_t>(tri[k])+base;
            if(vi<0||static_cast<std::uint64_t>(vi)>=vertexCount) {out.clear();return {};}
            const auto* v=vertices+static_cast<std::size_t>(vi)*stride;
            float w[4]{};std::memcpy(w,v+stride-20,16);const auto* b=v+stride-4;
            float sum=0,limb=0,body=0;
            for(unsigned j=0;j<4;++j) {
                if(!std::isfinite(w[j])||w[j]<-.001f||w[j]>1.001f||(w[j]>.001f&&b[j]>=rig.nodeCount)) {out.clear();return {};}
                sum+=w[j];if(w[j]>.001f&&rig.limb[b[j]])limb+=w[j];
                if(b[j]==rig.bodyBone)body+=w[j];
            }
            if(sum<.98f||sum>1.02f) {out.clear();return {};}
            // Groundrobo's original rotating base, bearings and crossmembers
            // share 'body' with the outer hull. Keep complete surveyed parts,
            // not a height-clipped hull. This runs once per cached draw mesh;
            // native skinning, textures and animation remain untouched.
            const bool basePart=rig.kind==CockpitKind::Crawler&&stride==68&&body>=.5f&&CrawlerBasePosition(v);
            const bool gunBay=rig.kind==CockpitKind::ProteusGunner&&stride==60&&body>=.5f&&ProteusShroudPosition(v);
            keep=keep&&(limb>=.5f||basePart||gunBay)&&!BarrelCut(v,stride,w,b,rig);
            if(combat||heli||doorGunner){hullOnly=hullOnly&&body>=.5f;for(unsigned c=0;c<3;++c)at[k*3+c]=Half(v+2*c);}
        }
        if(keep&&hullOnly&&combat&&CombatCutMeets(*combat,at))keep=false;
        if(keep&&hullOnly&&heli&&(HeliGlass(*heli,at)||HeliRebuiltGlass(*heli,at)||HeliMonitorHull(*heli,at)||HeliRebuiltHidden(*heli,at)))keep=false;
        if(keep&&hullOnly&&doorGunner) {
            // The model's own recessed door panel on his side (z .74-1.78, y
            // 1.58-2.70), every corner inside it; nothing else, so the hull
            // round the doorway and both wings stay (cockpit_brute_gunner.inc).
            const float side=rig.capVariant?-1.f:1.f;bool door=true;
            for(unsigned k=0;k<3;++k){const float x=side*at[k*3],y=at[k*3+1],z=at[k*3+2];
                door=door&&x>.95f&&x<1.56f&&y>1.57f&&y<2.72f&&z>.73f&&z<1.79f;}
            if(door)keep=false;
        }
        if(keep) {out.insert(out.end(),tri,tri+3);result.kept+=3;}else result.removed+=3;
    }
    result.valid=true;return result;
}
void CollectCockpitHull(const unsigned char* vertices,std::size_t bytes,unsigned stride,
    const void* indices,unsigned count,bool wide,int base,const CockpitRig& rig,std::vector<float>& hull) noexcept {
    if(!vertices||!indices||(stride!=60&&stride!=68)||bytes<stride||!count||count%3||count>3000000||rig.bodyBone>=rig.nodeCount)return;
    const std::size_t vertexCount=bytes/stride;
    auto index=[&](unsigned i){std::uint32_t v=0;if(wide)std::memcpy(&v,static_cast<const unsigned char*>(indices)+i*4,4);
        else {std::uint16_t s=0;std::memcpy(&s,static_cast<const unsigned char*>(indices)+i*2,2);v=s;}return v;};
    for(unsigned t=0;t<count;t+=3) {
        float p[9]{};bool body=true;
        for(unsigned k=0;k<3&&body;++k) {
            const auto vi=static_cast<std::int64_t>(index(t+k))+base;
            if(vi<0||static_cast<std::uint64_t>(vi)>=vertexCount)return;
            const auto* v=vertices+static_cast<std::size_t>(vi)*stride;
            float w[4]{};std::memcpy(w,v+stride-20,16);const auto* b=v+stride-4;
            float on=0;for(unsigned j=0;j<4;++j)if(w[j]>.001f&&b[j]==rig.bodyBone)on+=w[j];
            body=on>=.5f;
            for(unsigned c=0;c<3;++c)p[k*3+c]=Half(v+2*c);
        }
        bool finite=true;for(float x:p)finite=finite&&std::isfinite(x)&&std::fabs(x)<100;
        if(body&&finite&&hull.size()<9*60000)hull.insert(hull.end(),p,p+9);
    }
}
}
