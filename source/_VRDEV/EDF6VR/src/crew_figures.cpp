#include "crew_figures.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

namespace edf6vr {
namespace {
constexpr const char* kFamilies[]={"","RANGER","WINGDIVER","AIRRAIDER","FENCER"};
// A class's four looks: the EDF6 suit first (the default), then EDF5's, then
// the two prototype suits.
constexpr const char* kModels[5][4]={{"","","",""},
    {"P605_RANGER","P505_RANGER","P601_PROTO_RANGER","P501_PROTO_RANGER"},
    {"P606_WINGDIVER","P506_WINGDIVER","P602_PROTO_WINGDIVER","P502_PROTO_WINGDIVER"},
    {"P608_AIRRADER","P508_AIRRADER","P604_PROTO_AIRRADER","P504_PROTO_AIRRADER"},
    {"P607_FENCER","P507_FENCER","P603_PROTO_FENCER","P503_PROTO_FENCER"}};

// Row-vector 4x4 (v' = v*M), as the game and the MDB store them.
struct M4 { float m[4][4]; };
using V3=std::array<float,3>;
M4 Identity4() {M4 r{};for(int i=0;i<4;++i)r.m[i][i]=1;return r;}
M4 Mul(const M4& a,const M4& b) {
    M4 r{};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j){float s=0;for(int k=0;k<4;++k)s+=a.m[i][k]*b.m[k][j];r.m[i][j]=s;}
    return r;
}
V3 Point(const V3& p,const M4& m) {
    V3 r{};for(int j=0;j<3;++j)r[j]=p[0]*m.m[0][j]+p[1]*m.m[1][j]+p[2]*m.m[2][j]+m.m[3][j];return r;
}
V3 Direction(const V3& p,const M4& m) {
    V3 r{};for(int j=0;j<3;++j)r[j]=p[0]*m.m[0][j]+p[1]*m.m[1][j]+p[2]*m.m[2][j];return r;
}
V3 Sub(V3 a,V3 b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
V3 Add(V3 a,V3 b) {return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
V3 Scale(V3 a,float s) {return {a[0]*s,a[1]*s,a[2]*s};}
float Dot(V3 a,V3 b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
V3 Cross(V3 a,V3 b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float Length(V3 a) {return std::sqrt(Dot(a,a));}
V3 Unit(V3 a) {const float l=Length(a);return l>1e-8f?Scale(a,1/l):V3{0,0,0};}
V3 Origin(const M4& m) {return {m.m[3][0],m.m[3][1],m.m[3][2]};}

bool ReadAll(const std::wstring& path,std::vector<unsigned char>& out) {
    std::ifstream file(path,std::ios::binary);if(!file)return false;
    file.seekg(0,std::ios::end);const auto size=file.tellg();if(size<=0||size>512ll*1024*1024)return false;
    out.resize(static_cast<std::size_t>(size));file.seekg(0);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()),size));
}
std::wstring Widen(const char* text) {std::wstring w;for(const char* c=text;*c;++c)w+=static_cast<wchar_t>(*c);return w;}
struct Reader {
    const std::vector<unsigned char>& data;std::size_t at=0;bool ok=true;
    template<class T> T Get() {T v{};if(at+sizeof(T)>data.size()){ok=false;return v;}std::memcpy(&v,data.data()+at,sizeof(T));at+=sizeof(T);return v;}
    std::string Text(std::size_t size) {
        if(at+size>data.size()){ok=false;return {};}
        const char* p=reinterpret_cast<const char*>(data.data()+at);at+=size;
        return std::string(p,strnlen(p,size));
    }
    void Floats(float* out,std::size_t n) {for(std::size_t i=0;i<n;++i)out[i]=Get<float>();}
};
struct Bone { std::string name; int parent=-1; M4 local{},inverse{}; };
struct Material { std::string albedo,normal,param,mask; float colour[8]{}; };
struct Mesh { int material=0; std::vector<float> vertex; std::vector<std::uint32_t> indices; unsigned count=0; };
constexpr std::size_t kVertexFloats=13;   // position 3, normal 3, uv 2, bones 1 (4 bytes packed), weights 4
struct PoseEntry { unsigned flags=0; float t[3]{},r[9]{},s[3]{}; };

bool LoadModel(const std::wstring& path,std::vector<Bone>& bones,std::vector<Material>& materials,std::vector<Mesh>& meshes,std::string& error) {
    std::vector<unsigned char> data;if(!ReadAll(path,data)){error="cannot read model";return false;}
    Reader r{data};
    if(r.Text(4)!="CRW1"){error="not a crew model";return false;}
    r.Get<std::uint32_t>();
    const auto boneCount=r.Get<std::uint32_t>(),materialCount=r.Get<std::uint32_t>(),meshCount=r.Get<std::uint32_t>();
    if(!r.ok||boneCount>512||materialCount>64||meshCount>64){error="bad model header";return false;}
    bones.resize(boneCount);
    for(auto& b:bones){b.name=r.Text(32);b.parent=r.Get<std::int32_t>();r.Floats(&b.local.m[0][0],16);r.Floats(&b.inverse.m[0][0],16);}
    materials.resize(materialCount);
    for(auto& m:materials){r.Text(64);r.Text(64);m.albedo=r.Text(64);m.normal=r.Text(64);m.param=r.Text(64);m.mask=r.Text(64);r.Floats(m.colour,8);}
    meshes.resize(meshCount);
    for(auto& m:meshes) {
        m.material=r.Get<std::int32_t>();m.count=r.Get<std::uint32_t>();const auto indexCount=r.Get<std::uint32_t>();
        if(!r.ok||m.count>2000000||indexCount>6000000){error="bad mesh";return false;}
        m.vertex.resize(static_cast<std::size_t>(m.count)*kVertexFloats);
        for(unsigned v=0;v<m.count;++v) {
            float* f=m.vertex.data()+v*kVertexFloats;r.Floats(f,8);
            std::uint32_t packed=r.Get<std::uint32_t>();std::memcpy(f+8,&packed,4);
            r.Floats(f+9,4);
        }
        m.indices.resize(indexCount);
        for(auto& i:m.indices)i=r.Get<std::uint32_t>();
    }
    if(!r.ok){error="truncated model";return false;}
    for(std::size_t i=0;i<bones.size();++i)if(bones[i].parent>=static_cast<int>(bones.size())){error="bad bone parent";return false;}
    return true;
}
bool LoadPose(const std::wstring& path,std::vector<std::pair<std::string,PoseEntry>>& pose) {
    std::vector<unsigned char> data;if(!ReadAll(path,data))return false;
    Reader r{data};if(r.Text(4)!="CRP1")return false;r.Get<std::uint32_t>();
    const auto count=r.Get<std::uint32_t>();if(!r.ok||count>512)return false;
    pose.resize(count);
    for(auto& [name,e]:pose){name=r.Text(32);e.flags=r.Get<std::uint32_t>();r.Floats(e.t,3);r.Floats(e.r,9);r.Floats(e.s,3);}
    return r.ok;
}
// Rotation about `pivot` taking direction `from` onto `to`, applied to a bone
// matrix (its axes and origin, row-vector convention).
struct Turn { float r[3][3]; V3 pivot; };
Turn Between(V3 from,V3 to,V3 pivot) {
    Turn t{};t.pivot=pivot;
    from=Unit(from);to=Unit(to);
    const V3 axis=Cross(from,to);const float s=Length(axis),c=Dot(from,to);
    // Column-vector Rodrigues, then transposed for v' = v*R.
    float col[3][3]{{1,0,0},{0,1,0},{0,0,1}};
    if(s>1e-7f) {
        const V3 k=Scale(axis,1/s);const float v=1-c;
        col[0][0]=c+k[0]*k[0]*v;      col[0][1]=k[0]*k[1]*v-k[2]*s; col[0][2]=k[0]*k[2]*v+k[1]*s;
        col[1][0]=k[1]*k[0]*v+k[2]*s; col[1][1]=c+k[1]*k[1]*v;      col[1][2]=k[1]*k[2]*v-k[0]*s;
        col[2][0]=k[2]*k[0]*v-k[1]*s; col[2][1]=k[2]*k[1]*v+k[0]*s; col[2][2]=c+k[2]*k[2]*v;
    }
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)t.r[i][j]=col[j][i];
    return t;
}
V3 Rotate(const Turn& t,V3 v) {return {v[0]*t.r[0][0]+v[1]*t.r[1][0]+v[2]*t.r[2][0],v[0]*t.r[0][1]+v[1]*t.r[1][1]+v[2]*t.r[2][1],v[0]*t.r[0][2]+v[1]*t.r[1][2]+v[2]*t.r[2][2]};}
void Apply(const Turn& t,M4& m) {
    for(int i=0;i<3;++i){const auto row=Rotate(t,{m.m[i][0],m.m[i][1],m.m[i][2]});for(int j=0;j<3;++j)m.m[i][j]=row[j];}
    const auto o=Add(Rotate(t,Sub(Origin(m),t.pivot)),t.pivot);for(int j=0;j<3;++j)m.m[3][j]=o[j];
}
void ApplySubtree(const Turn& t,std::vector<M4>& world,const std::vector<std::vector<int>>& children,int bone) {
    Apply(t,world[bone]);for(const int c:children[bone])ApplySubtree(t,world,children,c);
}
int Find(const std::vector<Bone>& bones,const char* name) {
    for(std::size_t i=0;i<bones.size();++i)if(bones[i].name==name)return static_cast<int>(i);
    return -1;
}
// Two-bone reach: the upper arm turns about the shoulder so the elbow lands
// where upper and forearm meet on the way to `target` (the bend kept on the
// side the pose had it), then the forearm about the new elbow onto the target.
// pole: which way the middle joint should point (zero: keep the pose's own bend).
void Reach(std::vector<M4>& world,const std::vector<std::vector<int>>& children,int upper,int lower,int hand,V3 target,V3& reached,V3 pole={0,0,0}) {
    const V3 s=Origin(world[upper]),e=Origin(world[lower]),w=Origin(world[hand]);
    const float a=Length(Sub(e,s)),b=Length(Sub(w,e));
    if(a<1e-4f||b<1e-4f){reached=w;return;}
    V3 toTarget=Sub(target,s);float d=Length(toTarget);
    d=std::clamp(d,std::fabs(a-b)+1e-3f,a+b-1e-3f);
    const V3 u=Unit(toTarget);
    V3 bend=Length(pole)>0?pole:Sub(Sub(e,s),Scale(u,Dot(Sub(e,s),u)));
    if(Length(Sub(bend,Scale(u,Dot(bend,u))))<1e-4f)bend={0,-1,0};
    bend=Unit(Sub(bend,Scale(u,Dot(bend,u))));
    const float cosA=std::clamp((a*a+d*d-b*b)/(2*a*d),-1.f,1.f),sinA=std::sqrt(std::max(0.f,1-cosA*cosA));
    const V3 elbow=Add(s,Add(Scale(u,a*cosA),Scale(bend,a*sinA)));
    ApplySubtree(Between(Sub(e,s),Sub(elbow,s),s),world,children,upper);
    const V3 wrist=Origin(world[hand]),goal=Add(s,Scale(u,d));
    ApplySubtree(Between(Sub(wrist,elbow),Sub(goal,elbow),elbow),world,children,lower);
    reached=Origin(world[hand]);
}
// The tandem cabin's controls and footrests, in a seat's own frame (its eye
// reference; the rear seat is the same furniture moved by kProteusCabinRearSeat):
// the driver's two side sticks (cockpit_proteus_cabin.inc CabinConsole /
// CabinStick: the stick body's grip, 13 cm up its inward-leaning axis, the
// trigger on its front), the operator's two palm levers (cockpit_geometry.cpp
// PalmLever: a horizontal bar, radius 2 cm), and the two sloping treads of the
// foot rest (CabinFootStrut).
// A grip: its axis through `centre`, its radius, which way along the axis the
// knuckles run (little -> index), and whether the index finger rests on a
// trigger (curled less than the others). With `placeIndex` the hand sits so
// the index finger lies at `index` on the axis (the centre is then moved so,
// by the hand's own knuckle spacing).
struct Grip { V3 centre,axis;float radius;V3 knuckles;bool trigger=false,placeIndex=false;V3 index{}; };
// The driver's side stick: held where the index finger lies on the trigger
// (16-19 cm up the axis, on its front; the user), the middle finger at 15 cm
// where the shaft has flared to about 2.4 cm radius, the index at the top.
Grip DriverGrip(float s) {
    const V3 axis{-s*.7071068f,.6644630f,-.2418448f};
    const V3 plate=Add(V3{s*.52f,-.34f,.33f},Scale(axis,.105f));
    return {Add(plate,Scale(axis,.150f)),axis,.024f,axis,true};
}
// The operator's lever (cockpit_geometry.cpp PalmLever): a horizontal bar (1.7
// by 1.6 cm radii and a knurled sleeve) out of an outboard hinge, its carriage
// at the front of the travel (11.5 cm ahead of where it first stood). The index
// finger lies on the trigger paddle hung below the neck at the inboard end
// (6.7 cm in from the sleeve's middle), the rest of the hand on the sleeve.
Grip RearGrip(float s) {
    const V3 axis{-.2706f,.2945f,0};   // the bar's height and depth
    return {{s*.332f,axis[0],axis[1]},{1,0,0},.018f,{-s,0,0},true,true,{s*.265f,axis[0],axis[1]}};
}
// The elbow on its circle (upper arm and forearm lengths fixed, the wrist at
// `wrist`) that keeps the forearm square to the grip's axis -- the wrist then
// stays straight -- without lifting it over the shoulder; `natural` (down at
// the side) breaks the ties. Returns the direction to hand to Reach as its pole.
V3 ElbowPole(V3 shoulder,V3 elbowNow,V3 wristNow,V3 wrist,V3 gripAxis,V3 natural) {
    const float a=Length(Sub(elbowNow,shoulder)),b=Length(Sub(wristNow,elbowNow));
    V3 u=Sub(wrist,shoulder);float d=Length(u);if(d<1e-4f||a<1e-4f||b<1e-4f)return natural;
    u=Scale(u,1/d);d=std::clamp(d,std::fabs(a-b)+1e-3f,a+b-1e-3f);
    const float cosA=std::clamp((a*a+d*d-b*b)/(2*a*d),-1.f,1.f),sinA=std::sqrt(std::max(0.f,1-cosA*cosA));
    const V3 c=Add(shoulder,Scale(u,a*cosA)),end=Add(shoulder,Scale(u,d));
    V3 p=Sub(natural,Scale(u,Dot(natural,u)));if(Length(p)<1e-4f)p=Cross(u,V3{1,0,0});p=Unit(p);const V3 q=Cross(u,p);
    float best=1e9f;V3 chosen=p;
    for(int k=0;k<72;++k) {
        const float t=k*6.2831853f/72;const V3 dir=Add(Scale(p,std::cos(t)),Scale(q,std::sin(t)));
        const V3 elbow=Add(c,Scale(dir,a*sinA)),fore=Unit(Sub(end,elbow));
        const float score=std::fabs(Dot(fore,Unit(gripAxis)))+4*std::max(0.f,elbow[1]-shoulder[1]+.05f)+.35f*(1-std::cos(t));
        if(score<best){best=score;chosen=dir;}
    }
    return chosen;
}
// A percentile of a set of distances (the hands' armour has stray bits).
float Percentile(std::vector<float> values,float fraction) {
    if(values.empty())return 0;
    const auto at=static_cast<std::size_t>(fraction*static_cast<float>(values.size()-1));
    std::nth_element(values.begin(),values.begin()+static_cast<std::ptrdiff_t>(at),values.end());
    return values[at];
}
float SegmentDistance(V3 p,V3 a,V3 b) {
    const V3 ab=Sub(b,a);const float l=Dot(ab,ab);
    const float t=l>1e-10f?std::clamp(Dot(Sub(p,a),ab)/l,0.f,1.f):0;
    return Length(Sub(p,Add(a,Scale(ab,t))));
}
struct Tread { V3 surface,up,normal; };      // up: along the tread toward the toes; normal: off it, up and back
Tread FootTread(float s) {
    const V3 up{0,.5446f,.8387f},normal{0,.8387f,-.5446f};
    return {Add(V3{s*.1825f,-.94f,.58f},Scale(normal,.035f)),up,normal};
}
// Affine inverse of a bone matrix (rows: axes, then origin).
M4 Inverse4(const M4& m) {
    const float a=m.m[0][0],b=m.m[0][1],c=m.m[0][2],d=m.m[1][0],e=m.m[1][1],f=m.m[1][2],g=m.m[2][0],h=m.m[2][1],i=m.m[2][2];
    const float A=e*i-f*h,B=f*g-d*i,C=d*h-e*g,det=a*A+b*B+c*C;
    M4 r=Identity4();if(std::fabs(det)<1e-12f)return r;
    const float k=1/det;
    r.m[0][0]=A*k;r.m[0][1]=(c*h-b*i)*k;r.m[0][2]=(b*f-c*e)*k;
    r.m[1][0]=B*k;r.m[1][1]=(a*i-c*g)*k;r.m[1][2]=(c*d-a*f)*k;
    r.m[2][0]=C*k;r.m[2][1]=(b*g-a*h)*k;r.m[2][2]=(a*e-b*d)*k;
    for(int j=0;j<3;++j)r.m[3][j]=-(m.m[3][0]*r.m[0][j]+m.m[3][1]*r.m[1][j]+m.m[3][2]*r.m[2][j]);
    return r;
}
void Carry(std::vector<M4>& world,const std::vector<std::vector<int>>& children,int bone,const M4& delta) {
    for(const int c:children[bone]){world[c]=Mul(world[c],delta);Carry(world,children,c,delta);}
}
// A bone's new world matrix, its descendants carried along.
void SetWorld(std::vector<M4>& world,const std::vector<std::vector<int>>& children,int bone,const M4& next) {
    const M4 delta=Mul(Inverse4(world[bone]),next);world[bone]=next;Carry(world,children,bone,delta);
}
// A turn in the bone's own frame, about its local Z: the finger joints bend
// about it (their rest curl in the MDB is one).
void BendLocal(std::vector<M4>& world,const std::vector<std::vector<int>>& children,int bone,float angle) {
    M4 r=Identity4();const float c=std::cos(angle),s=std::sin(angle);
    r.m[0][0]=c;r.m[0][1]=s;r.m[1][0]=-s;r.m[1][1]=c;
    SetWorld(world,children,bone,Mul(r,world[bone]));
}
// The turn about `pivot` taking the frame (a, b) onto (c, d): a onto c, b as
// near d as it can go.
Turn Align(V3 a,V3 b,V3 c,V3 d,V3 pivot) {
    a=Unit(a);b=Unit(Sub(b,Scale(a,Dot(b,a))));c=Unit(c);d=Unit(Sub(d,Scale(c,Dot(d,c))));
    const V3 a3=Cross(a,b),c3=Cross(c,d);
    const V3 from[3]={a,b,a3},to[3]={c,d,c3};
    Turn t{};t.pivot=pivot;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){float v=0;for(int k=0;k<3;++k)v+=from[k][i]*to[k][j];t.r[i][j]=v;}
    return t;
}
// The tandem cabin's seat, from its own eye reference (cockpit_proteus_cabin.inc):
// the cushion's top, and the backrest's front (18 degrees back) as a line.
constexpr float kSeatTop=-.645f;
// The hip joint's place fore and aft: the backrest's front at the lower back
// is at z=-.15; a pelvis is about 12 cm deep behind the joint.
constexpr float kHipsForward=-.04f;
}

const char* CrewModelName(unsigned kind,unsigned model) noexcept {return kind>=1&&kind<=4&&model<4?kModels[kind][model]:"";}
const char* CrewFamilyName(unsigned kind) noexcept {return kind>=1&&kind<=4?kFamilies[kind]:"";}

bool LoadCrewColours(const std::wstring& folder,unsigned kind,std::vector<std::array<float,8>>& presets) {
    presets.clear();if(kind<1||kind>4)return false;
    std::vector<unsigned char> data;if(!ReadAll(folder+L"\\"+Widen(kFamilies[kind])+L".colors",data))return false;
    Reader r{data};if(r.Text(4)!="CRC1")return false;r.Get<std::uint32_t>();
    const auto count=r.Get<std::uint32_t>();if(!r.ok||count>256)return false;
    presets.resize(count);for(auto& p:presets)r.Floats(p.data(),8);
    return r.ok;
}

bool BuildCrewFigure(const std::wstring& folder,unsigned kind,unsigned model,CrewSeat seat,bool rearBuild,
                     CrewFigure& out,std::string& error) {
    out={};
    if(kind<1||kind>4||model>3){error="no such class or look";return false;}
    std::vector<Bone> bones;std::vector<Material> materials;std::vector<Mesh> meshes;
    if(!LoadModel(folder+L"\\"+Widen(kModels[kind][model])+L".crew",bones,materials,meshes,error))return false;
    std::vector<std::pair<std::string,PoseEntry>> pose;
    if(!LoadPose(folder+L"\\"+Widen(kFamilies[kind])+L".pose",pose)){error="cannot read pose";return false;}
    // The seated skeleton: each posed bone's local transform replaces its bind
    // one (translation, rotation, scale as the clip has them), the rest stay.
    std::vector<M4> world(bones.size());std::vector<std::vector<int>> children(bones.size());
    for(std::size_t i=0;i<bones.size();++i) {
        M4 local=bones[i].local;
        for(const auto& [name,e]:pose) {
            if(name!=bones[i].name)continue;
            float length[3];for(int k=0;k<3;++k)length[k]=std::sqrt(local.m[k][0]*local.m[k][0]+local.m[k][1]*local.m[k][1]+local.m[k][2]*local.m[k][2]);
            if(e.flags&2){for(int k=0;k<3;++k){const float s=(e.flags&4)?e.s[k]:length[k];for(int j=0;j<3;++j)local.m[k][j]=e.r[k*3+j]*s;}}
            else if(e.flags&4){for(int k=0;k<3;++k)if(length[k]>1e-8f)for(int j=0;j<3;++j)local.m[k][j]*=e.s[k]/length[k];}
            if(e.flags&1)for(int j=0;j<3;++j)local.m[3][j]=e.t[j];
            break;
        }
        const int parent=bones[i].parent;
        if(parent>=static_cast<int>(i)){error="bones out of order";return false;}
        world[i]=parent<0?local:Mul(local,world[parent]);
        if(parent>=0)children[parent].push_back(static_cast<int>(i));
    }
    // Into the cabin's handedness (the model's +X is its left, the cabin's
    // +X the pilot's right): mirror X.
    M4 mirror=Identity4();mirror.m[0][0]=-1;
    for(auto& w:world)w=Mul(w,mirror);
    // Skinning matrices (bind inverse, then the posed bone) for the current pose.
    std::vector<M4> skinning(bones.size());
    auto prepare=[&]{for(std::size_t i=0;i<bones.size();++i)skinning[i]=Mul(bones[i].inverse,world[i]);};
    auto skin=[&](const Mesh& mesh,unsigned v,V3& p,V3& n) {
        const float* f=mesh.vertex.data()+v*kVertexFloats;unsigned char index[4];std::memcpy(index,f+8,4);
        const float* w=f+9;float total=0;p={0,0,0};n={0,0,0};
        for(int k=0;k<4;++k) {
            if(w[k]<=0||index[k]>=bones.size())continue;
            const auto& m=skinning[index[k]];
            p=Add(p,Scale(Point({f[0],f[1],f[2]},m),w[k]));n=Add(n,Scale(Direction({f[3],f[4],f[5]},m),w[k]));total+=w[k];
        }
        if(total>1e-6f)p=Scale(p,1/total);
        n=Unit(n);
    };
    const int hips=Find(bones,"koshi");
    if(hips<0){error="no koshi bone";return false;}
    // Seat it: the lowest point of the seat (the vertices the hips and thighs
    // carry most, behind the knees) on the cushion; the hips a fixed step in
    // front of the backrest -- a back unit (the Fencer's, the Wing Diver's)
    // may sink into it rather than push the figure off the seat; centred.
    {
        const V3 k=Origin(world[hips]);float bottom=1e9f;prepare();
        const int thighs[2]={Find(bones,"momo_l"),Find(bones,"momo_r")};
        for(const auto& mesh:meshes)for(unsigned v=0;v<mesh.count;++v) {
            const float* f=mesh.vertex.data()+v*kVertexFloats;unsigned char index[4];std::memcpy(index,f+8,4);
            int top=0;for(int q=1;q<4;++q)if(f[9+q]>f[9+top])top=q;
            const int carrier=index[top];
            if(carrier!=hips&&carrier!=thighs[0]&&carrier!=thighs[1])continue;
            V3 p,n;skin(mesh,v,p,n);
            if(std::fabs(p[0]-k[0])<.25f&&p[2]>k[2]-.25f&&p[2]<k[2]+.12f)bottom=std::min(bottom,p[1]);
        }
        if(bottom>1e8f){error="no seat contact";return false;}
        M4 move=Identity4();move.m[3][0]=-k[0];move.m[3][1]=kSeatTop-bottom;move.m[3][2]=kHipsForward-k[2];
        for(auto& w:world)w=Mul(w,move);
    }
    // Hands on the controls, the way a pilot's fist holds them (the user's
    // references): the wrist straight -- the back of the hand continuing the
    // forearm, which the elbow keeps square to the grip -- the knuckles along
    // the grip (index at the top of a stick, at the inboard end of a bar), the
    // palm lying on it, and each finger joint bent until the next one comes to
    // rest on the grip's surface, so the fingers wrap it instead of sinking in.
    const char* arms[2][3]={{"jowan0_l","kawan0_l","te_l"},{"jowan0_r","kawan0_r","te_r"}};
    const char* fingers[5]={"index","middle","ring","little","thumb"};
    // palm: how far the palm's surface lies in front of the middle knuckle;
    // finger: each finger segment's thickness (radius) -- both measured off the
    // model, the gloves being armoured and thick.
    struct Arm { int upper=-1,lower=-1,hand=-1,index0=-1,little0=-1,chain[5][3]{}; float sign=1,palm=.024f,finger[5][3]{}; Grip grip{}; bool ok=false; };
    Arm arm[2];
    for(int side=0;side<2;++side) {
        auto& a=arm[side];const float s=side?1.f:-1.f;a.grip=seat==CrewSeat::Driver?DriverGrip(s):RearGrip(s);
        out.target[side]=a.grip.centre;
        const char* suffix=side?"_r":"_l";
        auto bone=[&](const char* stem,int joint){std::string name=stem;if(joint>=0)name+=std::to_string(joint);name+=suffix;return Find(bones,name.c_str());};
        a.upper=Find(bones,arms[side][0]);a.lower=Find(bones,arms[side][1]);a.hand=Find(bones,arms[side][2]);
        for(int finger=0;finger<5;++finger)for(int joint=0;joint<3;++joint)a.chain[finger][joint]=bone(fingers[finger],joint);
        a.index0=a.chain[0][0];a.little0=a.chain[3][0];
        a.ok=a.upper>=0&&a.lower>=0&&a.hand>=0&&a.index0>=0&&a.little0>=0&&a.chain[1][0]>=0&&a.chain[1][1]>=0;
        // Which way the fingers curl: the sign of the middle joints' rest bend.
        float rest=0;
        for(int finger=0;finger<4;++finger)if(const int b=a.chain[finger][1];b>=0)rest+=std::atan2(bones[b].local.m[0][1],bones[b].local.m[0][0]);
        a.sign=rest<0?-1.f:1.f;
        // Each finger segment's radius: the vertices it carries most, from its
        // bone's line in the bind pose (the next joint, or a tip for the last).
        for(int finger=0;finger<5;++finger)for(int joint=0;joint<3;++joint) {
            a.finger[finger][joint]=.011f;
            const int b=a.chain[finger][joint];if(b<0)continue;
            const M4 bind=Inverse4(bones[b].inverse);const V3 from=Origin(bind);V3 to;
            if(joint<2&&a.chain[finger][joint+1]>=0)to=Origin(Inverse4(bones[a.chain[finger][joint+1]].inverse));
            else {
                const float length=joint>0&&a.chain[finger][joint-1]>=0?.8f*Length(Sub(from,Origin(Inverse4(bones[a.chain[finger][joint-1]].inverse)))):.03f;
                to=Add(from,Scale(Unit({bind.m[0][0],bind.m[0][1],bind.m[0][2]}),length));
            }
            std::vector<float> distances;
            for(const auto& mesh:meshes)for(unsigned v=0;v<mesh.count;++v) {
                const float* f=mesh.vertex.data()+v*kVertexFloats;unsigned char index[4];std::memcpy(index,f+8,4);
                int top=0;for(int q=1;q<4;++q)if(f[9+q]>f[9+top])top=q;
                if(index[top]==b)distances.push_back(SegmentDistance({f[0],f[1],f[2]},from,to));
            }
            if(distances.size()>=8)a.finger[finger][joint]=std::clamp(Percentile(distances,.9f),.005f,.030f);
        }
    }
    // The fingers start open (their bind pose, a flat hand), so they can wrap
    // the grip from outside: the seated clip leaves them curled on the lap.
    for(const auto& a:arm)for(int finger=0;finger<5;++finger)for(int joint=0;joint<3;++joint)
        if(const int b=a.chain[finger][joint];b>=0&&bones[b].parent>=0)SetWorld(world,children,b,Mul(bones[b].local,world[bones[b].parent]));
    auto knuckle=[&](const Arm& a){return Origin(world[a.chain[1][0]]);};
    // From the back of the hand to the palm: the side the middle finger curls to.
    auto palmNormal=[&](const Arm& a) {
        const V3 k=Unit(Sub(Origin(world[a.index0]),Origin(world[a.little0]))),b=Unit(Sub(knuckle(a),Origin(world[a.hand])));
        const V3 n=Unit(Cross(k,b));
        auto trial=world;BendLocal(trial,children,a.chain[1][0],a.sign*.4f);
        return Dot(Sub(Origin(trial[a.chain[1][1]]),Origin(world[a.chain[1][1]])),n)<0?Scale(n,-1):n;
    };
    auto align=[&](const Arm& a,V3 forearm) {
        const V3 knuckles=Sub(Origin(world[a.index0]),Origin(world[a.little0])),back=Sub(knuckle(a),Origin(world[a.hand]));
        ApplySubtree(Align(knuckles,back,a.grip.knuckles,forearm,Origin(world[a.hand])),world,children,a.hand);
    };
    auto forearmOf=[&](const Arm& a){return Unit(Sub(Origin(world[a.hand]),Origin(world[a.lower])));};
    // Where the wrist goes for the palm to lie on the grip: the axis a grip's
    // radius and a palm's thickness in from the middle knuckle.
    auto wristTarget=[&](const Arm& a) {
        const V3 onKnuckle=Sub(a.grip.centre,Scale(palmNormal(a),a.grip.radius+a.palm));
        return Sub(onKnuckle,Sub(knuckle(a),Origin(world[a.hand])));
    };
    // The palm's surface in front of the middle knuckle: the vertices the hand
    // bone carries most, between the wrist and the knuckles, along the palm's normal.
    auto measurePalm=[&](Arm& a) {
        prepare();
        const V3 k=knuckle(a),n=palmNormal(a),back=Unit(Sub(k,Origin(world[a.hand])));
        const float span=Length(Sub(k,Origin(world[a.hand])));
        std::vector<float> depths;
        for(const auto& mesh:meshes)for(unsigned v=0;v<mesh.count;++v) {
            const float* f=mesh.vertex.data()+v*kVertexFloats;unsigned char index[4];std::memcpy(index,f+8,4);
            int top=0;for(int q=1;q<4;++q)if(f[9+q]>f[9+top])top=q;
            if(index[top]!=a.hand)continue;
            V3 p,normal;skin(mesh,v,p,normal);
            const float along=Dot(Sub(p,k),back);
            if(along>-span&&along<.01f)depths.push_back(Dot(Sub(p,k),n));
        }
        if(depths.size()>=8)a.palm=std::clamp(Percentile(depths,.95f)+.002f,.012f,.06f);
    };
    V3 wristFor[2]{};float armLength[2]{};
    for(int side=0;side<2;++side)if(arm[side].ok) {
        auto& a=arm[side];align(a,Unit(Sub(a.grip.centre,Origin(world[a.upper]))));
        if(a.grip.placeIndex) {
            const V3 along=Unit(a.grip.knuckles);
            a.grip.centre=Sub(a.grip.index,Scale(along,Dot(Sub(Origin(world[a.index0]),knuckle(a)),along)));
            out.target[side]=a.grip.centre;
        }
        measurePalm(a);wristFor[side]=wristTarget(a);
        armLength[side]=Length(Sub(Origin(world[a.lower]),Origin(world[a.upper])))+Length(Sub(Origin(world[a.hand]),Origin(world[a.lower])));
    }
    auto worst=[&](float shift,float lean,float fraction) {
        const V3 pivot=Origin(world[hips]);float over=-1e9f;
        for(int side=0;side<2;++side) {
            const auto& a=arm[side];if(!a.ok)continue;
            const V3 shoulder=Sub(Origin(world[a.upper]),pivot);
            const V3 placed=Add(pivot,V3{shoulder[0],shoulder[1]*std::cos(lean)-shoulder[2]*std::sin(lean),shoulder[1]*std::sin(lean)+shoulder[2]*std::cos(lean)+shift});
            over=std::max(over,Length(Sub(wristFor[side],placed))-fraction*armLength[side]);
        }
        return over;
    };
    // Deep in the seat and leaning a little forward, face up and eyes ahead:
    // the robot pilot's posture (the user). At least 8 degrees; more, up to 20,
    // until the elbows bend to about 130 degrees on the controls; beyond that
    // only as far as the arms need to reach at all (25 at most).
    float lean=8*.017453293f;
    while(lean<20*.017453293f&&worst(0,lean,.92f)>0)lean+=.017453293f;
    while(lean<25*.017453293f&&worst(0,lean,1.f)>-.005f)lean+=.017453293f;
    out.shift=0;out.lean=lean;
    int torso=Find(bones,"hara0");if(torso<0)torso=Find(bones,"mune");
    if(lean>0&&torso>=0)ApplySubtree(Between({0,1,0},{0,std::cos(lean),std::sin(lean)},Origin(world[hips])),world,children,torso);
    // Eyes ahead: the neck raises the head to face straight forward, level.
    if(const int head=Find(bones,"head");head>=0) {
        int neck=Find(bones,"kubi");if(neck<0)neck=head;
        auto carried=[&](V3 model){return Unit(Direction(Direction(model,bones[head].inverse),world[head]));};
        ApplySubtree(Align(carried({0,0,1}),carried({0,1,0}),Unit(V3{0,.02f,1}),{0,1,0},Origin(world[neck])),world,children,neck);
    }
    // Feet on the footrest: each ankle where the ball of the foot rests on its
    // tread (the foot's own bind proportions: the ankle's height over the sole,
    // its reach behind the ball), then the foot turned so its sole lies along
    // the tread.
    const char* legs[2][4]={{"momo_l","sune_l","ashi_l","toe_l"},{"momo_r","sune_r","ashi_r","toe_r"}};
    for(int side=0;side<2;++side) {
        const int thigh=Find(bones,legs[side][0]),shin=Find(bones,legs[side][1]),foot=Find(bones,legs[side][2]),toe=Find(bones,legs[side][3]);
        const float s=side?1.f:-1.f;const auto tread=FootTread(s);
        if(thigh<0||shin<0||foot<0){out.ankle[side]=out.footTarget[side]=tread.surface;continue;}
        // The Wing Diver and the Fencer have no toe bone: a 10 cm ball reach.
        const M4 bindFoot=Inverse4(bones[foot].inverse),bindToe=toe>=0?Inverse4(bones[toe].inverse):bindFoot;
        float sole=1e9f;
        for(const auto& mesh:meshes)for(unsigned v=0;v<mesh.count;++v) {
            const float* f=mesh.vertex.data()+v*kVertexFloats;unsigned char index[4];std::memcpy(index,f+8,4);
            int top=0;for(int q=1;q<4;++q)if(f[9+q]>f[9+top])top=q;
            if(index[top]==foot||index[top]==toe)sole=std::min(sole,f[1]);
        }
        const float height=sole<1e8f?bindFoot.m[3][1]-sole:.10f,reach=toe>=0?std::max(0.f,bindToe.m[3][2]-bindFoot.m[3][2]):.10f;
        const V3 ankle=Add(tread.surface,Add(Scale(tread.normal,height),Scale(tread.up,-reach)));
        out.footTarget[side]=ankle;
        // The knee up and a touch out, the thigh along the cushion.
        Reach(world,children,thigh,shin,foot,ankle,out.ankle[side],{s*.15f,1,.40f});
        // The foot's forward and up (the model's +Z and +Y at bind) as it is now.
        const M4& now=world[foot];const M4& inverse=bones[foot].inverse;
        auto carried=[&](V3 model){const V3 local=Direction(model,inverse);return Unit(Direction(local,now));};
        ApplySubtree(Align(carried({0,0,1}),carried({0,1,0}),tread.up,tread.normal,Origin(now)),world,children,foot);
    }
    for(int side=0;side<2;++side) {
        auto& a=arm[side];const float s=side?1.f:-1.f;const V3 natural{s*.45f,-1,-.35f};
        if(!a.ok) {
            if(a.upper>=0&&a.lower>=0&&a.hand>=0)Reach(world,children,a.upper,a.lower,a.hand,a.grip.centre,out.wrist[side],natural);
            else out.wrist[side]=a.grip.centre;
            continue;
        }
        // How the hand holds the grip: where the grip's axis lies against the
        // hand (its depth in front of the middle knuckle, its offset toward the
        // fingers) so the four fingers close round it furthest without the palm
        // or any finger going into it. Found on the hand alone -- turned onto
        // the grip, its fingers open -- with each finger's vertices carried
        // rigidly by their strongest bone; the arm then brings the hand there.
        align(a,Unit(Sub(a.grip.centre,Origin(world[a.upper]))));
        const V3 axis=Unit(a.grip.axis);
        const float full[5][3]={{1.48f,1.92f,1.22f},{1.48f,1.92f,1.22f},{1.48f,1.92f,1.22f},{1.48f,1.92f,1.22f},{.52f,1.05f,1.05f}};
        struct Carried { int bone; V3 local; };
        std::vector<Carried> carried[6];   // the five fingers, then the palm
        for(const auto& mesh:meshes)for(unsigned v=0;v<mesh.count;++v) {
            const float* f=mesh.vertex.data()+v*kVertexFloats;unsigned char index[4];std::memcpy(index,f+8,4);
            int top=0;for(int q=1;q<4;++q)if(f[9+q]>f[9+top])top=q;
            const int b=index[top];
            // The palm: the hand bone and the metacarpal helpers the ring and
            // little fingers hang from (fing0/fing1, which never bend here).
            int owner=b==a.hand||(bones[b].parent==a.hand&&bones[b].name.rfind("fing",0)==0)?5:-1;
            for(int finger=0;finger<5&&owner<0;++finger)for(int joint=0;joint<3;++joint)if(a.chain[finger][joint]==b)owner=finger;
            if(owner>=0)carried[owner].push_back({b,Point({f[0],f[1],f[2]},bones[b].inverse)});
        }
        auto inside=[&](V3 p,V3 centre){const V3 r=Sub(p,centre);const float along=Dot(r,axis);
            return std::fabs(along)<.06f&&Length(Sub(r,Scale(axis,along)))<a.grip.radius-.002f;};
        // A finger's joints closed by t along its curl (direction: the thumb's way).
        auto fingerAt=[&](int finger,float t,float direction,M4 joints[3]) {
            M4 parent{};
            for(int joint=0;joint<3;++joint) {
                const int b=a.chain[finger][joint];if(b<0){joints[joint]=parent;continue;}
                M4 r=Identity4();const float angle=direction*a.sign*t*full[finger][joint],c=std::cos(angle),sn=std::sin(angle);
                r.m[0][0]=c;r.m[0][1]=sn;r.m[1][0]=-sn;r.m[1][1]=c;
                const M4 rest=joint&&a.chain[finger][joint-1]>=0?Mul(Mul(world[b],Inverse4(world[a.chain[finger][joint-1]])),parent):world[b];
                joints[joint]=Mul(r,rest);parent=joints[joint];
            }
        };
        auto fingerClear=[&](int finger,float t,float direction,V3 centre) {
            M4 joints[3];fingerAt(finger,t,direction,joints);
            for(const auto& c:carried[finger])for(int joint=0;joint<3;++joint)
                if(a.chain[finger][joint]==c.bone){if(inside(Point(c.local,joints[joint]),centre))return false;break;}
            return true;
        };
        // How far a finger closes round a grip at `centre` before it touches it,
        // which way (the thumb), and whether it touches at all.
        auto closing=[&](int finger,V3 centre,float& direction,bool& touched) {
            float best=0;direction=1;touched=false;
            if(a.chain[finger][0]<0)return 0.f;
            if(!fingerClear(finger,0,1,centre)) {   // inside even open: open it further
                for(int step=1;step<=20;++step)for(const float way:{1.f,-1.f})if(fingerClear(finger,-step/20.f,way,centre)){direction=way;return -step/20.f;}
                return 0.f;
            }
            // The trigger finger stops a third of the way closed, on the trigger.
            const float most=finger==0&&a.grip.trigger?.35f:1.f;
            for(const float way:finger==4?std::initializer_list<float>{1.f,-1.f}:std::initializer_list<float>{1.f}) {
                float kept=0;bool met=false;
                for(int step=1;step<=40&&step/40.f<=most+1e-4f;++step){if(!fingerClear(finger,step/40.f,way,centre)){met=true;break;}kept=step/40.f;}
                if(met&&(!touched||kept>best)){best=kept;direction=way;touched=true;}
                else if(!touched&&kept>best){best=kept;direction=way;}
            }
            return best;
        };
        float depth=a.grip.radius+a.palm,offset=0,closure[5]{},direction[5]{1,1,1,1,1};
        {
            const V3 k=knuckle(a),n=palmNormal(a),back=Unit(Sub(k,Origin(world[a.hand])));
            float bestScore=-1e9f;
            // The palm lies against the grip (the user: it must touch): for each
            // offset toward the fingers, the least depth that keeps it outside.
            for(float e=-.03f;e<=.05f;e+=.01f) {
                float d=a.grip.radius;V3 centre{};
                for(;d<=a.grip.radius+.08f;d+=.002f) {
                    centre=Add(k,Add(Scale(n,d),Scale(back,e)));bool clear=true;
                    for(const auto& c:carried[5])if(inside(Point(c.local,world[c.bone]),centre)){clear=false;break;}
                    if(clear)break;
                }
                if(d>a.grip.radius+.08f)continue;
                // A hold: the middle, ring and little fingers each close onto
                // the grip -- touching it on the way, which counts most -- the
                // deeper they wrap first and the nearer the palm, the better.
                // Some hold is always taken, never an open hand.
                float t[5],way[5],score=0;
                for(int finger=0;finger<5;++finger) {
                    bool touched=false;t[finger]=closing(finger,centre,way[finger],touched);
                    if(finger>=1&&finger<=3)score+=touched?10+t[finger]:0.f;
                    else score+=finger==0?(touched?.5f*t[finger]:0.f):(touched?.3f*std::fabs(t[finger]):0.f);
                }
                if(score>bestScore){bestScore=score;depth=d;offset=e;for(int f=0;f<5;++f){closure[f]=t[f];direction[f]=way[f];}}
            }
        }
        auto holdTarget=[&] {
            const V3 k=knuckle(a),n=palmNormal(a),back=Unit(Sub(k,Origin(world[a.hand])));
            return Sub(Sub(a.grip.centre,Add(Scale(n,depth),Scale(back,offset))),Sub(k,Origin(world[a.hand])));
        };
        V3 pole=natural;
        for(int round=0;round<10;++round) {
            align(a,round?forearmOf(a):Unit(Sub(a.grip.centre,Origin(world[a.upper]))));
            const V3 target=holdTarget();
            pole=ElbowPole(Origin(world[a.upper]),Origin(world[a.lower]),Origin(world[a.hand]),target,a.grip.axis,natural);
            V3 reached{};Reach(world,children,a.upper,a.lower,a.hand,target,reached,pole);
        }
        // Last: the hand square to the forearm, then the arm once more so the
        // hold lands exactly (the hand turns with the forearm only slightly).
        align(a,forearmOf(a));
        {V3 reached{};Reach(world,children,a.upper,a.lower,a.hand,holdTarget(),reached,pole);}
        {
            const V3 k=knuckle(a),n=palmNormal(a),back=Unit(Sub(k,Origin(world[a.hand])));
            out.wrist[side]=Add(k,Add(Scale(n,depth),Scale(back,offset)));
        }
        for(int finger=0;finger<5;++finger) {
            for(int joint=0;joint<3;++joint)if(const int b=a.chain[finger][joint];b>=0)BendLocal(world,children,b,direction[finger]*a.sign*closure[finger]*full[finger][joint]);
            out.fingerClose[side][finger]=direction[finger]*closure[finger];
        }
        // The hand's roll against the forearm (about the forearm's axis, from
        // the way the hand sits on it at bind) shared out along the forearm's
        // twist bones, a third and two thirds, so the wrist does not wring
        // (the user: the wrist looked twisted).
        if(bones[a.hand].parent==a.lower) {
            const V3 axisF=Unit(Sub(Origin(world[a.hand]),Origin(world[a.lower])));
            const M4 expected=Mul(bones[a.hand].local,world[a.lower]);
            auto across=[&](const M4& m){V3 r{m.m[1][0],m.m[1][1],m.m[1][2]};return Unit(Sub(r,Scale(axisF,Dot(r,axisF))));};
            const V3 from=across(expected),to=across(world[a.hand]);
            const float roll=std::atan2(Dot(Cross(from,to),axisF),Dot(from,to));
            const char* suffix=side?"_r":"_l";
            for(const auto& [stem,share]:{std::pair<const char*,float>{"kawan1",1/3.f},{"kawan2",2/3.f}}) {
                const int b=Find(bones,(std::string(stem)+suffix).c_str());
                if(b<0||b==a.hand)continue;
                const float angle=roll*share,c=std::cos(angle),sn=std::sin(angle);
                // Rodrigues about the forearm's axis, row-vector form, about the bone's own origin.
                Turn t{};t.pivot=Origin(world[b]);const V3 k=axisF;const float v=1-c;
                const float col[3][3]={{c+k[0]*k[0]*v,k[0]*k[1]*v-k[2]*sn,k[0]*k[2]*v+k[1]*sn},
                                       {k[1]*k[0]*v+k[2]*sn,c+k[1]*k[1]*v,k[1]*k[2]*v-k[0]*sn},
                                       {k[2]*k[0]*v-k[1]*sn,k[2]*k[1]*v+k[0]*sn,c+k[2]*k[2]*v}};
                for(int i=0;i<3;++i)for(int j=0;j<3;++j)t.r[i][j]=col[j][i];
                Apply(t,world[b]);
            }
            out.wristRoll[side]=roll;
        }
    }
    // Into the build the cabin is drawn from.
    V3 offset{0,0,0};
    if(!rearBuild&&seat==CrewSeat::Rear)offset={kProteusCabinRearSeat[0],kProteusCabinRearSeat[1],kProteusCabinRearSeat[2]};
    if(rearBuild&&seat==CrewSeat::Driver)offset={-kProteusCabinRearSeat[0],-kProteusCabinRearSeat[1],-kProteusCabinRearSeat[2]};
    M4 shift=Identity4();for(int j=0;j<3;++j)shift.m[3][j]=offset[j];
    for(auto& w:world)w=Mul(w,shift);
    for(int side=0;side<2;++side) {
        out.wrist[side]=Add(out.wrist[side],offset);out.target[side]=Add(out.target[side],offset);
        out.ankle[side]=Add(out.ankle[side],offset);out.footTarget[side]=Add(out.footTarget[side],offset);
    }
    out.hips=Origin(world[hips]);
    // Which hand each bone belongs to, for the grip clearance below.
    std::vector<int> handOf(bones.size(),-1);
    for(int side=0;side<2;++side)if(arm[side].hand>=0) {
        std::vector<int> stack{arm[side].hand};
        while(!stack.empty()){const int b=stack.back();stack.pop_back();handOf[b]=side;for(const int c:children[b])stack.push_back(c);}
    }
    for(int side=0;side<2;++side)out.gripClearance[side]=1;
    if(const int head=Find(bones,"head");head>=0)out.head=Origin(world[head]);
    // The figure's vertices, one index range a mesh.
    prepare();
    for(const auto& mesh:meshes) {
        CrewFigurePart part;
        if(mesh.material>=0&&mesh.material<static_cast<int>(materials.size())) {
            const auto& m=materials[mesh.material];
            part.albedo=m.albedo;part.normal=m.normal;part.param=m.param;part.mask=m.mask;
            for(int k=0;k<4;++k){part.colour0[k]=m.colour[k];part.colour1[k]=m.colour[4+k];}
        }
        const auto base=static_cast<std::uint32_t>(out.vertices.size());
        for(unsigned v=0;v<mesh.count;++v) {
            V3 p,n;skin(mesh,v,p,n);
            const float* f=mesh.vertex.data()+v*kVertexFloats;
            {
                unsigned char index[4];std::memcpy(index,f+8,4);int top=0;for(int q=1;q<4;++q)if(f[9+q]>f[9+top])top=q;
                const int side=index[top]<handOf.size()?handOf[index[top]]:-1;
                if(side>=0&&arm[side].ok) {
                    const auto& g=arm[side].grip;const V3 axis=Unit(g.axis),r=Sub(p,Add(g.centre,offset));const float along=Dot(r,axis);
                    if(std::fabs(along)<.05f){const float clear=Length(Sub(r,Scale(axis,along)))-g.radius;if(clear<out.gripClearance[side]){out.gripClearance[side]=clear;out.gripWorst[side]=bones[index[top]].name;}}
                }
            }
            CockpitVertex c{};
            for(int k=0;k<3;++k){c.position[k]=p[k];c.normal[k]=n[k];c.colour[k]=1;}
            c.uv[0]=f[6];c.uv[1]=f[7];c.surface=25;
            out.vertices.push_back(c);
        }
        part.first=static_cast<unsigned>(out.indices.size());
        for(const auto i:mesh.indices)if(i<mesh.count)out.indices.push_back(base+i);
        part.count=static_cast<unsigned>(out.indices.size())-part.first;
        part.count-=part.count%3;
        out.parts.push_back(part);
    }
    for(const auto& v:out.vertices)for(int k=0;k<3;++k)if(!std::isfinite(v.position[k])){error="non-finite vertex";out={};return false;}
    return !out.vertices.empty();
}
}
