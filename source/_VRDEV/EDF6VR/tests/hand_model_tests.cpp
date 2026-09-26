#include "hand_model.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <limits>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
using namespace edf6vr;

static Matrix Translation(float x,float y,float z) {
    Matrix m{}; for(int i=0;i<4;++i) m.m[i][i]=1; m.m[3][0]=x; m.m[3][1]=y; m.m[3][2]=z; return m;
}
static bool Near(float a,float b,float tolerance=1e-4f) { return std::fabs(a-b)<tolerance; }

// A right hand in its wrist's frame: fingers along +X, index finger on the +Z
// side, palm facing -Y. Every bone's local frame is a pure translation.
struct Synthetic {
    std::vector<const char*> names; std::vector<int> parents; std::vector<Matrix> local; std::vector<Matrix> world;
    int index(const char* name) const { for(size_t i=0;i<names.size();++i) if(!std::strcmp(names[i],name)) return int(i); return -1; }
    void add(const char* name,const char* parent,float x,float y,float z) {
        const int p=parent?index(parent):-1;
        names.push_back(name); parents.push_back(p);
        // Locals are offsets from the parent, so subtract the parent's rest position.
        float px=0,py=0,pz=0; if(p>=0) { px=world[p].m[3][0]; py=world[p].m[3][1]; pz=world[p].m[3][2]; }
        local.push_back(Translation(x-px,y-py,z-pz)); world.push_back(Translation(x,y,z));
    }
    SkeletonView view() const { return SkeletonView{unsigned(names.size()),names.data(),parents.data(),local.data()}; }
};
static Synthetic MakeRightHand() {
    Synthetic s;
    s.add("root",nullptr,-0.5f,0,0);
    s.add("kawan0_r","root",-0.20f,0,0);
    s.add("kawan2_r","kawan0_r",-0.10f,0,0);
    s.add("te_r","kawan0_r",0,0,0);
    const char* fingers[4]={"index","middle","ring","little"}; const float z[4]={0.03f,0.0f,-0.01f,-0.03f};
    for(int f=0;f<4;++f) {
        static char storage[4][3][16];
        for(int j=0;j<3;++j) std::snprintf(storage[f][j],16,"%s%d_r",fingers[f],j);
        s.add(storage[f][0],"te_r",0.08f,0,z[f]); s.add(storage[f][1],storage[f][0],0.11f,0,z[f]); s.add(storage[f][2],storage[f][1],0.14f,0,z[f]);
    }
    s.add("thumb0_r","te_r",0.03f,-0.01f,0.045f); s.add("thumb1_r","thumb0_r",0.06f,-0.015f,0.06f); s.add("thumb2_r","thumb1_r",0.08f,-0.02f,0.07f);
    s.add("head","root",-0.5f,0.5f,0);
    return s;
}

int main() {
    // Rotation: row vectors, right-handed.
    {
        const float z[3]={0,0,1}; const Matrix r=RotationAbout(z,3.14159265f/2);
        float v[3]={1,0,0}, out[3]{};
        for(int j=0;j<3;++j) out[j]=v[0]*r.m[0][j]+v[1]*r.m[1][j]+v[2]*r.m[2][j];
        CHECK(Near(out[0],0) && Near(out[1],1) && Near(out[2],0));
        const Matrix ab=MatrixMultiply(Translation(1,0,0),Translation(0,2,0));
        CHECK(Near(ab.m[3][0],1) && Near(ab.m[3][1],2));
    }
    Synthetic hand=MakeRightHand();
    HandRig rig{};
    CHECK(BuildHandRig(hand.view(),false,rig));
    CHECK(rig.count==16);   // wrist, four fingers of three, thumb of three; no palm bones here
    CHECK(rig.bones[0].node==hand.index("te_r"));
    CHECK(rig.kawan2==hand.index("kawan2_r"));
    CHECK(Near(rig.palmLength,0.08f));
    CHECK(Near(rig.fingers[0],1) && Near(rig.fingers[1],0) && Near(rig.fingers[2],0));
    CHECK(Near(rig.palmOut[1],-1));   // the thumb tip is below the finger plane
    // A left hand is missing here, so it must be refused rather than guessed.
    HandRig leftRig{}; CHECK(!BuildHandRig(hand.view(),true,leftRig));
    // Every joint's curl axis is perpendicular to its segment (+X) and the palm (-Y): +-Z.
    for(unsigned b=1;b<rig.count;++b) if(rig.bones[b].finger!=Finger::None && rig.bones[b].finger!=Finger::Thumb)
        CHECK(Near(std::fabs(rig.bones[b].curlAxis[2]),1));

    // Parents recovered from the posed skeleton match the ones given.
    {
        std::vector<int> inferred(hand.names.size());
        InferParents(hand.local.data(),hand.world.data(),unsigned(hand.names.size()),inferred.data());
        for(size_t i=0;i<hand.names.size();++i) {
            if(hand.parents[i]<0) continue;
            CHECK(inferred[i]==hand.parents[i]);
        }
    }

    // Open hand on an identity wrist: rest positions come back untouched.
    std::vector<Matrix> palette(hand.names.size());
    Matrix wrist=Translation(0,0,0);
    PoseHand(rig,wrist,FingerCurl{},false,palette.data(),palette.size());
    const int index2=hand.index("index2_r"), middle0=hand.index("middle0_r");
    CHECK(Near(palette[index2].m[3][0],0.14f) && Near(palette[index2].m[3][1],0) && Near(palette[index2].m[3][2],0.03f));
    CHECK(Near(palette[middle0].m[3][0],0.08f));
    CHECK(Near(palette[rig.kawan2].m[3][0],-0.1f));   // the cuff bone keeps its bind offset from the wrist
    {
        const Matrix inv=RigidInverse(hand.local[hand.index("te_r")]);
        CHECK(Near(inv.m[3][0],-0.2f));
        const float z[3]={0,0,1}; const Matrix r=RotationAbout(z,0.7f), back=MatrixMultiply(r,RigidInverse(r));
        for(int i=0;i<4;++i) for(int j=0;j<4;++j) CHECK(Near(back.m[i][j],i==j?1.0f:0.0f));
    }
    // Curling the index finger takes its tip towards the palm and back towards the wrist.
    FingerCurl curl{}; curl.index=1;
    PoseHand(rig,wrist,curl,false,palette.data(),palette.size());
    CHECK(palette[index2].m[3][1]<-0.03f);
    CHECK(palette[index2].m[3][0]<0.10f);
    CHECK(Near(palette[middle0].m[3][0],0.08f));   // other fingers untouched
    // The thumb sweeps across the palm (towards the little finger, -Z here) and
    // down to the palm side (-Y), instead of only folding in.
    {
        const int thumb2=hand.index("thumb2_r");
        FingerCurl thumbOnly{}; thumbOnly.thumb=1;
        PoseHand(rig,wrist,thumbOnly,false,palette.data(),palette.size());
        CHECK(palette[thumb2].m[3][2]<0.069f);  // rest z was 0.07: a small sweep, not a fold
        CHECK(palette[thumb2].m[3][1]<-0.022f); // rest y was -0.02
        CHECK(palette[thumb2].m[3][1]>-0.05f);  // and it stays well clear of the palm
        CHECK(Near(palette[index2].m[3][0],0.14f));   // fingers untouched
    }
    // Inverted, it bends the other way.
    PoseHand(rig,wrist,curl,true,palette.data(),palette.size());
    CHECK(palette[index2].m[3][1]>0.03f);
    // A fist closes the outer three; the trigger alone only the index.
    const FingerCurl grip=CurlFromControls(0,1,0,0.15f), pull=CurlFromControls(1,0,0,0.15f), rest=CurlFromControls(0,0,0,0.15f);
    CHECK(Near(grip.little,1) && Near(grip.index,0.15f) && Near(pull.index,1) && Near(pull.ring,0.15f) && Near(rest.middle,0.15f));
    CHECK(Near(grip.thumb,0.15f) && Near(CurlFromControls(0,0,1,0.15f).thumb,1));   // the thumb follows the stick, not the grip
    CHECK(Near(CurlFromControls(2,-1,0,0.15f).index,1));
    CHECK(Near(CurlFromControls(0.5f,0,0,0.15f).index,1));
    CHECK(Near(CurlFromControls(0.12f,0,0,0.15f).index,0.15f));      // a resting trigger is not a pull
    CHECK(Near(CurlFromControls(0,0.016f,0,0.15f).ring,0.15f+0.85f*0.2f,1e-3f));  // resting on the grip: held lightly
    CHECK(Near(CurlFromControls(0,0.08f,0,0.15f).ring,1) && CurlFromControls(0,0.04f,0,0.15f).ring<0.7f);
    CHECK(Near(CurlFromControls(0.35f,0,0,0.15f).index,1));
    CHECK(Near(CurlFromControls(0.208f,0,0,0.15f).index,0.15f+0.85f*0.5f,2e-3f));   // (0.208-0.12)/0.88 = half of 0.2

    // A right controller pointing +Z with up +Y: fingers go along +Z, palm faces -X.
    {
        const float axes[3][3]={{1,0,0},{0,1,0},{0,0,1}}; const float palm[3]={1,2,3};
        HandTrim trim{}; trim=HandTrim{}; trim.right=trim.up=trim.ahead=0; trim.yawDegrees=trim.pitchDegrees=trim.rollDegrees=0;
        const HandTrim zero=trim;
        CHECK(WristFromController(rig,axes,palm,trim,wrist));
        float fingersWorld[3]{},palmWorld[3]{};
        for(int j=0;j<3;++j) { fingersWorld[j]=rig.fingers[0]*wrist.m[0][j]+rig.fingers[1]*wrist.m[1][j]+rig.fingers[2]*wrist.m[2][j];
                               palmWorld[j]=rig.palmOut[0]*wrist.m[0][j]+rig.palmOut[1]*wrist.m[1][j]+rig.palmOut[2]*wrist.m[2][j]; }
        CHECK(Near(fingersWorld[2],1) && Near(palmWorld[0],-1));
        // Proper rotation, not a mirror.
        const float det=wrist.m[0][0]*(wrist.m[1][1]*wrist.m[2][2]-wrist.m[1][2]*wrist.m[2][1])
                       -wrist.m[0][1]*(wrist.m[1][0]*wrist.m[2][2]-wrist.m[1][2]*wrist.m[2][0])
                       +wrist.m[0][2]*(wrist.m[1][0]*wrist.m[2][1]-wrist.m[1][1]*wrist.m[2][0]);
        CHECK(Near(det,1));
        // The wrist sits half a palm behind the grip.
        CHECK(Near(wrist.m[3][0],1) && Near(wrist.m[3][1],2) && Near(wrist.m[3][2],3-0.04f));
        trim=zero; trim.palmInvert=true; CHECK(WristFromController(rig,axes,palm,trim,wrist));
        for(int j=0;j<3;++j) palmWorld[j]=rig.palmOut[0]*wrist.m[0][j]+rig.palmOut[1]*wrist.m[1][j]+rig.palmOut[2]*wrist.m[2][j];
        CHECK(Near(palmWorld[0],1));
        trim=zero; trim.ahead=0.04f; trim.up=0.01f; trim.right=0.02f; CHECK(WristFromController(rig,axes,palm,trim,wrist));
        CHECK(Near(wrist.m[3][2],3) && Near(wrist.m[3][1],2.01f) && Near(wrist.m[3][0],1.02f));
        // The left rig faces the other way, and its trims are the mirror image.
        HandRig left=rig; left.left=true;
        CHECK(WristFromController(left,axes,palm,trim,wrist));
        CHECK(Near(wrist.m[3][0],0.98f) && Near(wrist.m[3][1],2.01f));
        trim=zero; trim.yawDegrees=30;
        Matrix rightYawed{},leftYawed{};
        CHECK(WristFromController(rig,axes,palm,trim,rightYawed) && WristFromController(left,axes,palm,trim,leftYawed));
        float fr[3]{},fl[3]{};
        for(int j=0;j<3;++j) { fr[j]=rig.fingers[0]*rightYawed.m[0][j]+rig.fingers[1]*rightYawed.m[1][j]+rig.fingers[2]*rightYawed.m[2][j];
                               fl[j]=rig.fingers[0]*leftYawed.m[0][j]+rig.fingers[1]*leftYawed.m[1][j]+rig.fingers[2]*leftYawed.m[2][j]; }
        CHECK(Near(fr[0],-fl[0]) && Near(fr[2],fl[2]) && !Near(fr[0],0));
        trim=zero;
        CHECK(WristFromController(left,axes,palm,trim,wrist));
        for(int j=0;j<3;++j) palmWorld[j]=rig.palmOut[0]*wrist.m[0][j]+rig.palmOut[1]*wrist.m[1][j]+rig.palmOut[2]*wrist.m[2][j];
        CHECK(Near(palmWorld[0],1));
        const float bad[3][3]={{0,0,0},{0,1,0},{0,0,1}}; CHECK(!WristFromController(rig,bad,palm,trim,wrist));
    }

    // Hand triangles out of a body mesh: hand-only and cuff vertices count, the
    // forearm and the other hand do not. `partial` counts the ones taken that
    // keep weight on a bone outside the set -- the cuff is exactly that, and
    // it is what a sleeve stretched to the arm would show up as.
    {
        const HandVertexLayout layout{60,40,56};
        const int leftBones[]={5,6}, rightBones[]={20};
        auto vertex=[&](std::vector<unsigned char>& bytes,const float w[4],const unsigned char b[4]) {
            unsigned char v[60]{}; std::memcpy(v+40,w,16); std::memcpy(v+56,b,4); bytes.insert(bytes.end(),v,v+60);
        };
        std::vector<unsigned char> bytes;
        const float one[4]={1,0,0,0}, half[4]={0.5f,0.5f,0,0};
        const unsigned char hand5[4]={5,0,0,0}, cuff[4]={5,7,0,0}, forearm[4]={7,0,0,0}, other[4]={20,0,0,0};
        vertex(bytes,one,hand5); vertex(bytes,one,hand5); vertex(bytes,one,hand5);   // 0,1,2
        vertex(bytes,half,cuff); vertex(bytes,one,forearm); vertex(bytes,one,other); // 3,4,5
        vertex(bytes,one,other); vertex(bytes,one,other);                             // 6,7
        const std::uint16_t indices[]={0,1,2, 1,2,3, 2,3,4, 0,1,5, 5,6,7};
        std::uint32_t outLeft[15]{},outRight[15]{};
        const auto picked=SelectHandTriangles(bytes.data(),bytes.size(),layout,indices,nullptr,15,0,
                                              HandBoneSet{leftBones,2},HandBoneSet{rightBones,1},outLeft,outRight,15);
        CHECK(picked.left==6 && picked.right==3);
        CHECK(outLeft[3]==1 && outLeft[5]==3 && outRight[0]==5);
        // Of the three taken, the one holding the cuff vertex keeps weight on
        // bone 7, which nobody moves.
        CHECK(picked.partial==1);
        // With a base vertex the same indices name different vertices.
        const std::uint16_t shifted[]={0,1,2};
        const auto moved=SelectHandTriangles(bytes.data(),bytes.size(),layout,shifted,nullptr,3,5,
                                             HandBoneSet{leftBones,2},HandBoneSet{rightBones,1},outLeft,outRight,15);
        CHECK(moved.left==0 && moved.right==3);
        const std::uint32_t wide[]={0,1,2};
        CHECK(SelectHandTriangles(bytes.data(),bytes.size(),layout,nullptr,wide,3,0,HandBoneSet{leftBones,2},HandBoneSet{rightBones,1},outLeft,outRight,15).left==3);
        CHECK(SelectHandTriangles(bytes.data(),bytes.size(),HandVertexLayout{60,50,56},indices,nullptr,15,0,HandBoneSet{leftBones,2},HandBoneSet{rightBones,1},outLeft,outRight,15).left==0);
    }
    // Mixed cuff/body influences survive selection, but must not drag the
    // glove when a multiplayer revival moves the unposed body palette.
    {
        const HandVertexLayout layout{60,40,56};
        std::vector<unsigned char> source(7*60,0);
        auto vertex=[&](unsigned i,const float (&weights)[4],const unsigned char (&bones)[4]) {
            for(unsigned k=0;k<40;++k)source[i*60+k]=static_cast<unsigned char>(i*9+k);
            std::memcpy(source.data()+i*60+40,weights,16);std::memcpy(source.data()+i*60+56,bones,4);
        };
        vertex(2,{.2f,.3f,.5f,0},{5,7,1,255}); // wrist, posed cuff, body, unused invalid bone
        vertex(3,{.0011f,.9989f,0,0},{6,1,255,255}); // keep a real glove even with little hand weight
        vertex(4,{.5f,.5f,0,0},{5,20,0,0}); // shared source vertex, isolated separately per hand
        vertex(5,{.3f,.2f,.5f,0},{20,21,1,255});vertex(6,{1,0,0,0},{20,255,255,255});
        const auto original=source;
        const int leftNodes[]={5,6,7},rightNodes[]={20,21};
        const HandBoneSet l{leftNodes,3},r{rightNodes,2};
        const std::uint32_t left[]={0,1,2,2,1,0},right[]={2,3,4};
        const auto isolated=IsolateHandMesh(source.data(),source.size(),layout,left,6,right,3,2,l,r);
        CHECK(isolated.valid && isolated.indices.size()==9 && isolated.vertices.size()==6*60);
        CHECK(isolated.reweighted==5);CHECK(source==original);
        CHECK(isolated.indices[2]!=isolated.indices[6]); // no cross-hand skin weights through a shared vertex
        for(unsigned h=0;h<2;++h)for(unsigned i=h?6:0;i<(h?9u:6u);++i) {
            const auto destination=isolated.indices[i];const auto* v=isolated.vertices.data()+destination*60;
            const auto sourceIndex=(h?right[i-6]:left[i])+2;
            CHECK(std::memcmp(v,source.data()+sourceIndex*60,40)==0); // positions, normals, UVs untouched
            float weights[4];std::memcpy(weights,v+40,16);float sum=0,originalBodyWeight=0;
            for(int k=0;k<4;++k) {
                const auto bone=v[56+k];CHECK(h?(bone==20 || bone==21):(bone==5 || bone==6 || bone==7));
                sum+=weights[k];if(bone==1)originalBodyWeight+=weights[k];
            }
            CHECK(Near(sum,1));CHECK(originalBodyWeight==0);
            // Skinning under arbitrary body/ragdoll movement is unchanged.
            auto skin=[&](float body) {float x=0;for(int k=0;k<4;++k)x+=weights[k]*(v[56+k]==1?body:2.f);return x;};
            CHECK(Near(skin(-200),skin(500)) && Near(skin(500),2));
        }
        float cuffWeights[4];std::memcpy(cuffWeights,isolated.vertices.data()+40,16);
        CHECK(Near(cuffWeights[0],.4f) && Near(cuffWeights[1],.6f)); // preserve the cuff's rest offset
        CHECK(IsolateHandMesh(source.data(),source.size(),layout,right,3,nullptr,0,-2,r,l).valid==false);
        CHECK(!IsolateHandMesh(source.data(),source.size(),layout,left,6,right,3,20,l,r).valid);
        CHECK(!IsolateHandMesh(source.data(),source.size(),layout,left,5,right,3,2,l,r).valid);
        float bad=std::numeric_limits<float>::quiet_NaN();std::memcpy(source.data()+2*60+40,&bad,4);
        CHECK(!IsolateHandMesh(source.data(),source.size(),layout,left,6,right,3,2,l,r).valid);
    }
    printf("EDF6VR hand model tests: %d failures\n",failures);
    // Past the cuff folds onto the wrist: the touching selection brings the band, the
    // collapse slot takes what has no posed weight, and that slot is a point.
    {
        int slots[2];
        const int used[]={0,1,2,5,-1};
        HandCollapseSlots(used,5,8,slots);CHECK(slots[0]==3 && slots[1]==4);
        HandCollapseSlots(nullptr,0,8,slots);CHECK(slots[0]==0 && slots[1]==1);
        HandCollapseSlots(used,5,4,slots);CHECK(slots[0]==3 && slots[1]==-1);
        const float wristAt[3]={1.5f,-2,3};
        // A wrist turned a quarter about Z: its x along world y, its y along -x.
        Matrix turnedWrist{};
        turnedWrist.m[0][1]=1; turnedWrist.m[1][0]=-1; turnedWrist.m[2][2]=1;
        for(int j=0;j<3;++j) turnedWrist.m[3][j]=wristAt[j];
        turnedWrist.m[3][3]=1;
        const Matrix point=CollapseTo(turnedWrist);
        const float anywhere[4]={.3f,7,-4,1};
        for(int j=0;j<3;++j) {
            float x=0;for(int i=0;i<4;++i)x+=anywhere[i]*point.m[i][j];
            CHECK(std::fabs(x-wristAt[j])<0.02f);   // within millimetres of the wrist
        }
        // A normal through it keeps a direction, the wrist's: not zero (black).
        const float normal[3]={1,0,0};
        float turned[3]{},length=0;
        for(int j=0;j<3;++j) { for(int i=0;i<3;++i) turned[j]+=normal[i]*point.m[i][j]; length+=turned[j]*turned[j]; }
        length=std::sqrt(length);
        CHECK(length>1e-4f && Near(turned[1]/length,1));
        const HandVertexLayout layout{60,40,56};
        std::vector<unsigned char> bytes(4*60,0);
        auto set=[&](unsigned i,const float (&w)[4],const unsigned char (&b)[4]) {
            std::memcpy(bytes.data()+i*60+40,w,16);std::memcpy(bytes.data()+i*60+56,b,4);
        };
        set(0,{.5f,.5f,0,0},{5,7,0,0});   // cuff: wrist and cuff bone
        set(1,{1,0,0,0},{5,0,0,0});       // hand
        set(2,{.6f,.4f,0,0},{7,11,0,0});  // past the cuff: cuff bone and the arm
        set(3,{1,0,0,0},{11,0,0,0});      // the arm
        const std::uint16_t indices[]={0,1,2, 2,3,0, 3,2,3};
        const int handNodes[]={5},none[]={30};
        std::uint32_t outLeft[9],outRight[9];
        const auto strict=SelectHandTriangles(bytes.data(),bytes.size(),layout,indices,nullptr,9,0,
            HandBoneSet{handNodes,1},HandBoneSet{none,1},outLeft,outRight,9);
        CHECK(strict.left==0);                                   // no triangle is all hand
        const auto band=SelectHandTriangles(bytes.data(),bytes.size(),layout,indices,nullptr,9,0,
            HandBoneSet{handNodes,1},HandBoneSet{none,1},outLeft,outRight,9,true);
        CHECK(band.left==6);                                     // the two that touch it, not the arm's own
        const int posed[]={5,7},posedRight[]={30};
        CHECK(!IsolateHandMesh(bytes.data(),bytes.size(),layout,outLeft,band.left,nullptr,0,0,
            HandBoneSet{posed,2},HandBoneSet{posedRight,1}).valid);      // no slot: refused, as before
        const auto folded=IsolateHandMesh(bytes.data(),bytes.size(),layout,outLeft,band.left,nullptr,0,0,
            HandBoneSet{posed,2},HandBoneSet{posedRight,1},3,4,7,-1);
        CHECK(folded.valid && folded.collapsed==2 && folded.leftIndices==6);   // the cuff-and-arm vertex too
        for(std::size_t v=0;v<folded.vertices.size()/60;++v) {
            const auto* p=folded.vertices.data()+v*60;
            float w[4];std::memcpy(w,p+40,16);
            if(p[56]==3) CHECK(p[57]==3 && p[58]==3 && p[59]==3 && w[0]==1 && w[1]==0);
            else for(int k=0;k<4;++k) CHECK(p[56+k]==5 || p[56+k]==3);   // the cuff's share goes to the slot too
        }
        CHECK(folded.tapered==1);                                   // the cuff vertex, half hand
        // Partly hand, partly arm: the arm's share goes to the wrist point, the
        // hand's stays -- it tapers into the cuff instead of riding the hand rigidly.
        set(0,{.3f,.7f,0,0},{5,11,0,0});
        const std::uint32_t one[]={0,1,1};
        const auto tapered=IsolateHandMesh(bytes.data(),bytes.size(),layout,one,3,nullptr,0,0,
            HandBoneSet{posed,2},HandBoneSet{posedRight,1},3,4,7,-1);
        CHECK(tapered.valid && tapered.tapered==1 && tapered.collapsed==0);
        {
            const auto* p=tapered.vertices.data();
            float w[4];std::memcpy(w,p+40,16);
            CHECK(p[56]==5 && Near(w[0],.3f) && p[57]==3 && Near(w[1],.7f));
        }
    }
    return failures?1:0;
}
