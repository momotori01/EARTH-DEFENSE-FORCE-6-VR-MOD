#include "hand_model.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>

namespace edf6vr {
namespace {
constexpr float kPi=3.14159265f;
// How far each joint bends at a full fist, knuckle to tip. The thumb has less
// travel and most of it is at the base.
constexpr float kFingerBend[3]={75*kPi/180,95*kPi/180,60*kPi/180};
// The thumb sweeps a little across the palm at its base and flexes a little at
// each joint after: from resting on the controller to resting on the stick is
// a small move, and a full sweep reached the palm.
// The joint nearest the tip does most of the visible bending: with it straight
// the thumb read as a stiff rod however far the base moved.
constexpr float kThumbBend[3]={8*kPi/180,14*kPi/180,30*kPi/180};
constexpr float kThumbSweep=15*kPi/180;

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
void Cross(const float* a,const float* b,float* out) noexcept {
    out[0]=a[1]*b[2]-a[2]*b[1]; out[1]=a[2]*b[0]-a[0]*b[2]; out[2]=a[0]*b[1]-a[1]*b[0];
}
bool Normalize(float* v) noexcept {
    const float length=std::sqrt(Dot(v,v));
    if(!(length>1e-6f) || !std::isfinite(length)) return false;
    for(int i=0;i<3;++i) v[i]/=length;
    return true;
}
Matrix Identity() noexcept { Matrix m{}; for(int i=0;i<4;++i) m.m[i][i]=1; return m; }
bool Finite(const Matrix& m) noexcept {
    for(auto& row:m.m) for(float v:row) if(!std::isfinite(v)) return false;
    return true;
}
bool Equal(const char* a,const char* b) noexcept { return a && b && !std::strcmp(a,b); }
int Find(const SkeletonView& skeleton,const char* name) noexcept {
    for(unsigned i=0;i<skeleton.count;++i) if(Equal(skeleton.names[i],name)) return static_cast<int>(i);
    return -1;
}
// A vector through a matrix's rotation only (rows 0..2).
void Turn(const float* v,const Matrix& m,float* out) noexcept {
    for(int j=0;j<3;++j) out[j]=v[0]*m.m[0][j]+v[1]*m.m[1][j]+v[2]*m.m[2][j];
}
// The same vector expressed in the matrix's own frame: components along its rows.
void IntoFrame(const float* v,const Matrix& m,float* out) noexcept {
    for(int i=0;i<3;++i) out[i]=Dot(v,m.m[i]);
}
void RotateAxes(float axes[3][3],const float* about,float radians) noexcept {
    const Matrix r=RotationAbout(about,radians);
    for(int i=0;i<3;++i) { float turned[3]; Turn(axes[i],r,turned); std::memcpy(axes[i],turned,sizeof(turned)); }
}
}

Matrix MatrixMultiply(const Matrix& a,const Matrix& b) noexcept {
    Matrix out{};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {
        float sum=0;
        for(int k=0;k<4;++k) sum+=a.m[i][k]*b.m[k][j];
        out.m[i][j]=sum;
    }
    return out;
}

Matrix RotationAbout(const float axis[3],float radians) noexcept {
    // Row i is the image of the basis vector i, so v*R rotates v about the axis.
    float a[3]={axis[0],axis[1],axis[2]};
    Matrix r=Identity();
    if(!Normalize(a)) return r;
    const float c=std::cos(radians),s=std::sin(radians);
    for(int i=0;i<3;++i) {
        float e[3]={0,0,0}; e[i]=1;
        float cross[3]; Cross(a,e,cross);
        const float along=Dot(a,e);
        for(int j=0;j<3;++j) r.m[i][j]=e[j]*c+cross[j]*s+a[j]*along*(1-c);
    }
    return r;
}

Matrix RigidInverse(const Matrix& m) noexcept {
    Matrix out=Identity();
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) out.m[i][j]=m.m[j][i];
    for(int j=0;j<3;++j) out.m[3][j]=-(m.m[3][0]*out.m[0][j]+m.m[3][1]*out.m[1][j]+m.m[3][2]*out.m[2][j]);
    return out;
}

bool BuildHandRig(const SkeletonView& skeleton,bool left,HandRig& rig) noexcept {
    rig=HandRig{};
    rig.left=left;
    if(!skeleton.names || !skeleton.parents || !skeleton.restLocal || !skeleton.count) return false;
    const char side=left?'l':'r';
    struct Want { const char* stem; Finger finger; unsigned char joint; bool required; };
    const Want wanted[]={
        {"te",Finger::None,0,true},{"fing0",Finger::None,0,false},{"fing1",Finger::None,0,false},
        {"index0",Finger::Index,0,true},{"index1",Finger::Index,1,true},{"index2",Finger::Index,2,true},
        {"middle0",Finger::Middle,0,true},{"middle1",Finger::Middle,1,true},{"middle2",Finger::Middle,2,true},
        {"ring0",Finger::Ring,0,true},{"ring1",Finger::Ring,1,true},{"ring2",Finger::Ring,2,true},
        {"little0",Finger::Little,0,true},{"little1",Finger::Little,1,true},{"little2",Finger::Little,2,true},
        {"thumb0",Finger::Thumb,0,true},{"thumb1",Finger::Thumb,1,true},{"thumb2",Finger::Thumb,2,true},
    };
    HandBone found[kHandBoneMax]{}; unsigned foundCount=0;
    for(const auto& w:wanted) {
        char name[32]{}; std::snprintf(name,sizeof(name),"%s_%c",w.stem,side);
        const int node=Find(skeleton,name);
        if(node<0) { if(w.required) return false; continue; }
        if(!Finite(skeleton.restLocal[node])) return false;
        HandBone bone{}; bone.node=node; bone.finger=w.finger; bone.joint=w.joint; bone.restLocal=skeleton.restLocal[node];
        found[foundCount++]=bone;
    }
    { char name[32]{}; std::snprintf(name,sizeof(name),"kawan2_%c",side); rig.kawan2=Find(skeleton,name); }
    rig.kawan2Rest=Identity();
    if(rig.kawan2>=0 && skeleton.parents[rig.kawan2]==skeleton.parents[found[0].node] && Finite(skeleton.restLocal[rig.kawan2]))
        rig.kawan2Rest=MatrixMultiply(skeleton.restLocal[rig.kawan2],RigidInverse(found[0].restLocal));
    // Parents before children. The wrist is the root of the rig; every other
    // bone's parent must be in the rig, or the chain to the controller is broken.
    rig.bones[0]=found[0]; rig.count=1;
    bool placed[kHandBoneMax]{}; placed[0]=true;
    for(unsigned pass=0;pass<foundCount;++pass) {
        for(unsigned i=1;i<foundCount;++i) {
            if(placed[i]) continue;
            const int parentNode=skeleton.parents[found[i].node];
            for(unsigned slot=0;slot<rig.count;++slot) {
                if(rig.bones[slot].node!=parentNode) continue;
                found[i].parentSlot=static_cast<int>(slot);
                rig.bones[rig.count++]=found[i]; placed[i]=true;
                break;
            }
        }
    }
    if(rig.count!=foundCount) return false;
    // Rest pose in the wrist's own frame.
    Matrix rest[kHandBoneMax]{}; rest[0]=Identity();
    for(unsigned b=1;b<rig.count;++b) rest[b]=MatrixMultiply(rig.bones[b].restLocal,rest[rig.bones[b].parentSlot]);
    auto slotOf=[&](Finger finger,unsigned char joint) {
        for(unsigned b=0;b<rig.count;++b) if(rig.bones[b].finger==finger && rig.bones[b].joint==joint) return static_cast<int>(b);
        return -1;
    };
    const int middle0=slotOf(Finger::Middle,0),index0=slotOf(Finger::Index,0),little0=slotOf(Finger::Little,0),thumb2=slotOf(Finger::Thumb,2);
    if(middle0<0 || index0<0 || little0<0 || thumb2<0) return false;
    float fingers[3],across[3],palmOut[3];
    for(int j=0;j<3;++j) { fingers[j]=rest[middle0].m[3][j]; across[j]=rest[little0].m[3][j]-rest[index0].m[3][j]; }
    rig.palmLength=std::sqrt(Dot(fingers,fingers));
    if(!Normalize(fingers) || !Normalize(across)) return false;
    Cross(fingers,across,palmOut);
    if(!Normalize(palmOut)) return false;
    // The thumb tip lies on the palm side of the hand in the bind pose.
    if(Dot(palmOut,rest[thumb2].m[3])<0) for(float& v:palmOut) v=-v;
    std::memcpy(rig.fingers,fingers,sizeof(fingers));
    std::memcpy(rig.palmOut,palmOut,sizeof(palmOut));
    // A finger closes about one axis, perpendicular to its first segment and to
    // the palm normal, and every joint of it shares that axis: this is what
    // makes the three joints fold together instead of each finding its own.
    for(unsigned b=1;b<rig.count;++b) {
        auto& bone=rig.bones[b];
        if(bone.finger==Finger::None) continue;
        const int knuckle=slotOf(bone.finger,0),second=slotOf(bone.finger,1);
        if(knuckle<0 || second<0) return false;
        float segment[3]{};
        for(int j=0;j<3;++j) segment[j]=rest[second].m[3][j]-rest[knuckle].m[3][j];
        float axis[3]; Cross(segment,palmOut,axis);
        if(!Normalize(axis)) return false;
        IntoFrame(axis,rest[b],bone.curlAxis);
        if(bone.finger==Finger::Thumb && bone.joint==0) {
            // About the hand's long axis, in whichever sense carries the thumb
            // tip towards the palm side.
            float sweep[3]={fingers[0],fingers[1],fingers[2]},moved[3];
            Cross(sweep,segment,moved);
            if(Dot(moved,palmOut)<0) for(float& v:sweep) v=-v;
            IntoFrame(sweep,rest[b],bone.sweepAxis);
        }
    }
    return rig.palmLength>0.02f && rig.palmLength<0.3f;
}

void InferParents(const Matrix* local,const Matrix* world,unsigned count,int* parents) noexcept {
    for(unsigned i=0;i<count;++i) {
        parents[i]=-1; float best=1e30f;
        for(unsigned p=0;p<count;++p) {
            if(p==i) continue;
            const Matrix made=MatrixMultiply(local[i],world[p]);
            float error=0;
            for(int r=0;r<4;++r) for(int c=0;c<3;++c) error+=std::fabs(made.m[r][c]-world[i].m[r][c]);
            if(error<best) { best=error; parents[i]=static_cast<int>(p); }
        }
        if(!(best<0.02f)) parents[i]=-1;
    }
}

FingerCurl CurlFromControls(float trigger,float squeeze,float thumbOnStick,float relaxed) noexcept {
    auto clamp01=[](float v) { return v<0?0.0f:(v>1?1.0f:(std::isfinite(v)?v:0.0f)); };
    auto past=[&](float v,float dead) { v=clamp01(v); return v<=dead?0.0f:(v-dead)/(1-dead); };
    trigger=past(trigger,kTriggerDeadZone); squeeze=past(squeeze,kSqueezeDeadZone); thumbOnStick=clamp01(thumbOnStick); relaxed=clamp01(relaxed);
    // A finger is closed well before the control is: the buttons sit under
    // the fingers, so the hand has to look closed by the time one is pressed,
    // and a third of the travel is a finger around the grip.
    auto from=[&](float control,float fullAt) { const float steep=control>fullAt?1.0f:control/fullAt; return relaxed+(1-relaxed)*steep; };
    FingerCurl curl{};
    curl.index=from(trigger,kCurlFullAt);
    curl.middle=curl.ring=curl.little=from(squeeze,kSqueezeFullAt);
    curl.thumb=relaxed+(1-relaxed)*thumbOnStick;
    return curl;
}

bool WristFromController(const HandRig& rig,const float axes[3][3],const float palm[3],const HandTrim& trim,Matrix& wrist) noexcept {
    float frame[3][3]; std::memcpy(frame,axes,sizeof(frame));
    for(auto& row:frame) if(!Normalize(row)) return false;
    for(int j=0;j<3;++j) if(!std::isfinite(palm[j])) return false;
    // Trims turn the controller frame before the hand is laid on it. They are
    // found on the right hand; the left hand is its mirror, so everything that
    // tells left from right changes sign.
    const float mirror=rig.left?-1.0f:1.0f;
    RotateAxes(frame,frame[1],mirror*trim.yawDegrees*kPi/180);
    RotateAxes(frame,frame[0],trim.pitchDegrees*kPi/180);
    RotateAxes(frame,frame[2],mirror*trim.rollDegrees*kPi/180);
    const float* right=frame[0]; const float* up=frame[1]; const float* ahead=frame[2];
    // Fingers along the controller, palm facing the other hand.
    float fingersWorld[3]={ahead[0],ahead[1],ahead[2]};
    const float palmSign=(rig.left?1.0f:-1.0f)*(trim.palmInvert?-1.0f:1.0f);
    float palmWorld[3]={right[0]*palmSign,right[1]*palmSign,right[2]*palmSign};
    float thirdWorld[3]; Cross(fingersWorld,palmWorld,thirdWorld);
    float thirdLocal[3]; Cross(rig.fingers,rig.palmOut,thirdLocal);
    if(!Normalize(thirdWorld) || !Normalize(thirdLocal)) return false;
    // R = L^T W: a wrist-local vector's components along (fingers, palmOut,
    // third) become the same components along the world versions.
    const float* local[3]={rig.fingers,rig.palmOut,thirdLocal};
    const float* world[3]={fingersWorld,palmWorld,thirdWorld};
    wrist=Identity();
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
        float sum=0;
        for(int k=0;k<3;++k) sum+=local[k][i]*world[k][j];
        wrist.m[i][j]=sum;
    }
    // The grip pose is the middle of the palm; the wrist is half a palm back.
    for(int j=0;j<3;++j)
        wrist.m[3][j]=palm[j]-fingersWorld[j]*rig.palmLength*0.5f
                      +right[j]*trim.right*mirror+up[j]*trim.up+ahead[j]*trim.ahead;
    return Finite(wrist);
}

void PoseHand(const HandRig& rig,const Matrix& wrist,const FingerCurl& curl,bool curlInvert,Matrix* palette,std::size_t paletteCount) noexcept {
    if(!rig.count || !palette) return;
    Matrix world[kHandBoneMax]{}; world[0]=wrist;
    auto amount=[&](Finger finger) {
        switch(finger) {
        case Finger::Index: return curl.index; case Finger::Middle: return curl.middle;
        case Finger::Ring: return curl.ring; case Finger::Little: return curl.little;
        case Finger::Thumb: return curl.thumb; default: return 0.0f;
        }
    };
    for(unsigned b=1;b<rig.count;++b) {
        const auto& bone=rig.bones[b];
        Matrix base=MatrixMultiply(bone.restLocal,world[bone.parentSlot]);
        if(bone.finger!=Finger::None) {
            const float* bend=bone.finger==Finger::Thumb?kThumbBend:kFingerBend;
            float angle=amount(bone.finger)*bend[bone.joint<3?bone.joint:2];
            if(curlInvert) angle=-angle;
            // In the bone's own frame, about its own origin: the translation row
            // of `base` survives because the rotation has none.
            if(bone.finger==Finger::Thumb && bone.joint==0)
                base=MatrixMultiply(RotationAbout(bone.sweepAxis,amount(bone.finger)*kThumbSweep*(curlInvert?-1.0f:1.0f)),base);
            base=MatrixMultiply(RotationAbout(bone.curlAxis,angle),base);
        }
        world[b]=base;
    }
    for(unsigned b=0;b<rig.count;++b) {
        const auto node=rig.bones[b].node;
        if(node>=0 && static_cast<std::size_t>(node)<paletteCount) palette[node]=world[b];
    }
    if(rig.kawan2>=0 && static_cast<std::size_t>(rig.kawan2)<paletteCount) palette[rig.kawan2]=MatrixMultiply(rig.kawan2Rest,world[0]);
}

HandTriangles SelectHandTriangles(const unsigned char* vertices,std::size_t vertexBytes,const HandVertexLayout& layout,
                                  const std::uint16_t* indices16,const std::uint32_t* indices32,
                                  unsigned indexCount,int baseVertex,
                                  const HandBoneSet& left,const HandBoneSet& right,
                                  std::uint32_t* outLeft,std::uint32_t* outRight,unsigned capacity,
                                  bool touching) noexcept {
    HandTriangles result{};
    if(!vertices || !layout.stride || (!indices16 && !indices32) || !outLeft || !outRight) return result;
    if(layout.weightOffset+16>layout.stride || layout.indexOffset+4>layout.stride) return result;
    const std::size_t vertexCount=vertexBytes/layout.stride;
    auto inSet=[](const HandBoneSet& set,unsigned node) {
        for(unsigned i=0;i<set.count;++i) if(set.nodes[i]==static_cast<int>(node)) return true;
        return false;
    };
    // 0 neither, 1 left, 2 right, 3 both (which no real vertex is).
    //
    // Retain the proven triangle selection. Rejecting every mixed vertex
    // removed real gloves on hardware. IsolateHandMesh removes unposed body
    // influences from a private copy instead; `partial` describes the source.
    auto side=[&](std::uint32_t index,bool* outside) -> unsigned {
        const std::int64_t vertex=static_cast<std::int64_t>(index)+baseVertex;
        if(vertex<0 || static_cast<std::size_t>(vertex)>=vertexCount) return 0;
        const unsigned char* v=vertices+static_cast<std::size_t>(vertex)*layout.stride;
        float weights[4]; std::memcpy(weights,v+layout.weightOffset,sizeof(weights));
        const unsigned char* bones=v+layout.indexOffset;
        unsigned mask=0;
        for(int k=0;k<4;++k) {
            if(!(weights[k]>0.001f)) continue;
            const bool l=inSet(left,bones[k]),r=inSet(right,bones[k]);
            if(l) mask|=1;
            if(r) mask|=2;
            if(!l && !r && outside) *outside=true;
        }
        return mask;
    };
    auto index=[&](unsigned i) { return indices32?indices32[i]:static_cast<std::uint32_t>(indices16[i]); };
    for(unsigned t=0;t+3<=indexCount;t+=3) {
        const std::uint32_t tri[3]={index(t),index(t+1),index(t+2)};
        bool outside=false;
        const unsigned a=side(tri[0],&outside),b=side(tri[1],&outside),c=side(tri[2],&outside);
        // Touching: the band past the cuff too, which IsolateHandMesh folds onto the wrist.
        const unsigned mask=touching?(a|b|c):(a&b&c);
        if(mask==1 && result.left+3<=capacity) { for(int k=0;k<3;++k) outLeft[result.left++]=tri[k]; }
        else if(mask==2 && result.right+3<=capacity) { for(int k=0;k<3;++k) outRight[result.right++]=tri[k]; }
        else continue;
        if(outside) ++result.partial;
    }
    return result;
}

IsolatedHandMesh IsolateHandMesh(const unsigned char* vertices,std::size_t vertexBytes,const HandVertexLayout& layout,
    const std::uint32_t* left,unsigned leftCount,const std::uint32_t* right,unsigned rightCount,int baseVertex,
    const HandBoneSet& leftPosed,const HandBoneSet& rightPosed,int collapseLeft,int collapseRight,
    int cuffLeft,int cuffRight) {
    IsolatedHandMesh result{};
    if(!vertices || layout.stride<20 || layout.weightOffset>layout.stride-16 || layout.indexOffset>layout.stride-4
        || (leftCount&&!left) || (rightCount&&!right) || leftCount%3 || rightCount%3)return result;
    const auto vertexCount=vertexBytes/layout.stride;
    const HandBoneSet sets[]={leftPosed,rightPosed};const std::uint32_t* selected[]={left,right};
    const unsigned counts[]={leftCount,rightCount};
    for(unsigned h=0;h<2;++h) {
        const auto& set=sets[h];if(counts[h]&&(!set.nodes || !set.count))return {};
        auto allowed=[&](unsigned bone){for(unsigned j=0;j<set.count;++j)if(set.nodes[j]==static_cast<int>(bone))return true;return false;};
        std::unordered_map<std::uint32_t,std::uint32_t> remap;
        for(unsigned i=0;i<counts[h];++i) {
            const auto index=selected[h][i];const auto found=remap.find(index);
            if(found!=remap.end()){result.indices.push_back(found->second);continue;}
            const auto source=static_cast<std::int64_t>(index)+baseVertex;
            if(source<0 || static_cast<std::size_t>(source)>=vertexCount)return {};
            const auto* v=vertices+static_cast<std::size_t>(source)*layout.stride;
            float weights[4];unsigned char bones[4];std::memcpy(weights,v+layout.weightOffset,16);std::memcpy(bones,v+layout.indexOffset,4);
            float total=0,kept=0;int fallback=-1;bool changed=false;
            for(int k=0;k<4;++k) {
                if(!std::isfinite(weights[k]) || weights[k]<-.001f || weights[k]>1.001f)return {};
                total+=weights[k];weights[k]=std::clamp(weights[k],0.f,1.f);
                if(allowed(bones[k])){kept+=weights[k];if(weights[k]>0)fallback=k;}
                else {changed|=weights[k]>0;weights[k]=0;}
            }
            if(total<.98f || total>1.02f)return {};
            // Past the cuff: no weight on the hand itself (the cuff bone alone, or the
            // arm), so all of it goes to the wrist. The cuff bone does not count: the
            // ring just past the cuff carries it too, and kept, the opening would only
            // move one ring out.
            const int collapse=h?collapseRight:collapseLeft,cuff=h?cuffRight:cuffLeft;
            float onHand=0;
            for(int k=0;k<4;++k) if(allowed(bones[k]) && bones[k]!=cuff) onHand+=weights[k];
            if(collapse>=0 && onHand<=.001f) {
                if(collapse>255)return {};
                for(int k=0;k<4;++k) { weights[k]=k?0.f:1.f; bones[k]=static_cast<unsigned char>(collapse); }
                changed=true; ++result.collapsed;
            } else if(collapse>=0 && collapse<=255 && onHand<.999f) {
                // Partly on the hand: everything that is not the hand's own -- the
                // cuff bone and the arm alike -- goes to the wrist point, with its
                // weight. Kept on the cuff bone, which is posed rigidly with the
                // hand, a sleeve vertex with a few percent on the hand rode the
                // wrist from where it sits up the forearm, and its triangle to the
                // folded ring stuck out of the cuff (hardware, 2026-09-26: Ranger
                // and Air Raider). Shared with the wrist point, it is drawn in by as
                // much as it is not hand, and the sleeve tapers shut -- checked
                // offline on all sixteen player models (research/hands/wrist_sim.py).
                std::memcpy(weights,v+layout.weightOffset,16);
                for(int k=0;k<4;++k) {
                    weights[k]=std::clamp(weights[k],0.f,1.f);
                    if(!allowed(bones[k]) || bones[k]==cuff) bones[k]=static_cast<unsigned char>(collapse);
                }
                changed=true; ++result.tapered;
            } else if(kept<=.001f || fallback<0) {
                return {};
            } else {
                const auto safeBone=bones[fallback];
                for(int k=0;k<4;++k) {
                    weights[k]/=kept;
                    // Shaders may fetch all four matrices even at zero weight.
                    // Do not leave stale/NaN ragdoll bones in those zero slots.
                    if(!allowed(bones[k]))bones[k]=safeBone;
                }
            }
            const auto destination=static_cast<std::uint32_t>(result.vertices.size()/layout.stride);
            const auto offset=result.vertices.size();result.vertices.insert(result.vertices.end(),v,v+layout.stride);
            std::memcpy(result.vertices.data()+offset+layout.weightOffset,weights,16);
            std::memcpy(result.vertices.data()+offset+layout.indexOffset,bones,4);
            if(changed)++result.reweighted;
            remap.emplace(index,destination);result.indices.push_back(destination);
        }
    }
    result.leftIndices=leftCount;result.rightIndices=rightCount;
    result.valid=true;return result;
}

Matrix CollapseTo(const Matrix& wrist,float scale) noexcept {
    Matrix m{};
    for(int k=0;k<3;++k) {
        const float n=std::sqrt(wrist.m[k][0]*wrist.m[k][0]+wrist.m[k][1]*wrist.m[k][1]+wrist.m[k][2]*wrist.m[k][2]);
        const bool usable=std::isfinite(n) && n>1e-6f;
        for(int j=0;j<3;++j) m.m[k][j]=(usable?wrist.m[k][j]/n:(j==k?1.f:0.f))*scale;
    }
    for(int j=0;j<3;++j) m.m[3][j]=wrist.m[3][j];
    m.m[3][3]=1;
    return m;
}

void HandCollapseSlots(const int* used,unsigned usedCount,unsigned nodeCount,int out[2]) noexcept {
    out[0]=out[1]=-1;
    unsigned found=0;
    for(unsigned node=0;node<nodeCount && node<256 && found<2;++node) {
        bool taken=false;
        for(unsigned i=0;i<usedCount && used;++i) if(used[i]==static_cast<int>(node)) { taken=true; break; }
        if(!taken) out[found++]=static_cast<int>(node);
    }
}

}
