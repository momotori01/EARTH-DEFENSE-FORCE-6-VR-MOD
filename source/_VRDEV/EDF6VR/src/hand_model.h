#pragma once
#include "camera_math.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// The player's own hands, drawn from the wrist forward at the controllers.
//
// The soldier's body is one skinned mesh and is withheld whole while VR is on.
// Its glove is not a mesh of its own. Some cuff vertices also depend on body
// bones, particularly on the Air Raider's multiplayer model. The hand is drawn by
// submitting only the triangles whose vertices all touch hand bones (see
// SelectHandTriangles), with a render palette in which kawan2 is given the
// hand's own matrix so the cuff closes on the wrist. A private vertex buffer
// removes the remaining body influences without discarding glove triangles.
// Ranger, Wing Diver and
// Air Raider all carry the same finger rig: te (wrist), fing0/fing1 (palm),
// and three joints each for index, middle, ring, little and thumb.
namespace edf6vr {

inline constexpr std::size_t kHandBoneMax=24;
enum class Finger : unsigned char { None, Index, Middle, Ring, Little, Thumb };

struct HandBone {
    int node=-1;              // index in the skeleton, and in the render palette
    int parentSlot=-1;        // index into HandRig::bones, -1 for the wrist
    Finger finger=Finger::None;
    unsigned char joint=0;    // 0 knuckle, 1 middle, 2 tip
    Matrix restLocal{};       // bone in its parent's frame, from the model file
    float curlAxis[3]{};      // in this bone's own frame; the finger closes about it
    // Thumb base only: the axis it sweeps across the palm about, before it
    // flexes. A thumb that only flexed dived straight into the palm.
    float sweepAxis[3]{};
};

struct HandRig {
    bool left=false;
    unsigned count=0;         // bones[0] is always the wrist
    HandBone bones[kHandBoneMax]{};
    int kawan2=-1;            // forearm twist bone, which the cuff is weighted to
    // Where kawan2 sits against the wrist in the bind pose. Skinning applies each
    // bone's own inverse bind, so the cuff closes on the wrist only when kawan2 is
    // placed where it was relative to te, not on top of it.
    Matrix kawan2Rest{};
    // The wrist's own frame, in rest: where the fingers point and which way the
    // palm faces, so the wrist can be turned onto a controller.
    float fingers[3]{};       // unit, wrist-local
    float palmOut[3]{};       // unit, wrist-local, out of the palm
    float palmLength=0;       // wrist to middle knuckle, metres
};

// How the skeleton describes itself. Names are ASCII, node indices are palette
// indices. restLocal is the model-file bind pose in the parent's frame, rows
// as the game stores them (translation in row 3, child = local * parent).
struct SkeletonView {
    unsigned count=0;
    const char* const* names=nullptr;
    const int* parents=nullptr;
    const Matrix* restLocal=nullptr;
};

// False when a bone of the rig is missing or the pose is degenerate.
bool BuildHandRig(const SkeletonView&,bool left,HandRig&) noexcept;

// Parents recovered from a posed skeleton, for a skeleton that does not say:
// the parent of a node is the one whose world, multiplied by the node's local,
// gives the node's world. -1 where nothing fits within tolerance.
void InferParents(const Matrix* local,const Matrix* world,unsigned count,int* parents) noexcept;

// How far each finger is closed, 0 open to 1 a fist.
struct FingerCurl { float index=0,middle=0,ring=0,little=0,thumb=0; };
// Control travel, past the dead zone, at which a finger is fully closed. The
// trigger finger closes on a touch. The grip follows the hand: the capacitive
// grip reads a little for fingers resting on the controller, more as they
// tighten, and the drawn fingers close in step, so a hand that is holding the
// controller lightly is drawn holding it lightly.
inline constexpr float kCurlFullAt=0.2f,kSqueezeFullAt=0.08f;
// What a resting trigger finger reads: a little above zero.
inline constexpr float kTriggerDeadZone=0.12f,kSqueezeDeadZone=0.0f;
// The relaxed hand is not flat. Grip closes the three outer fingers, trigger
// closes the index finger, and the thumb comes down onto the stick when it is
// touched; each from the relaxed value up.
FingerCurl CurlFromControls(float trigger,float squeeze,float thumbOnStick,float relaxed) noexcept;

// Trims on where the wrist sits against the grip pose, in the controller's own
// frame: metres along its right, up and ahead, and degrees turned about them.
// Defaults are the right hand as placed on a PSVR2 Sense grip in the 2026-09-15
// session; the left hand mirrors them (right, yaw and roll change sign).
struct HandTrim { float right=0.0639f,up=0.0127f,ahead=-0.0305f; float yawDegrees=4.47f,pitchDegrees=-59.27f,rollDegrees=3.61f; bool palmInvert=false; };

// The wrist's world matrix for a controller whose grip pose has these axes and
// palm position in the world (right, up, ahead rows). The rig's finger direction
// goes along ahead and its palm faces away from the thumb side, so a right hand's
// palm looks left and a left hand's looks right -- which is how a grip is held.
bool WristFromController(const HandRig&,const float axes[3][3],const float palm[3],const HandTrim&,Matrix& wrist) noexcept;

// Every bone of the rig in the world, from the wrist matrix: written to
// palette[bone.node] for the rig's bones and to palette[kawan2]. Nothing else
// in the palette is touched. curlInvert flips the bend for a rig whose palm
// side was read the wrong way round.
void PoseHand(const HandRig&,const Matrix& wrist,const FingerCurl&,bool curlInvert,Matrix* palette,std::size_t paletteCount) noexcept;

// Which triangles of the body mesh are a hand. A vertex is a hand vertex when
// it carries any weight on that side's hand bones; the cuff ring is weighted
// half to kawan2 and is included because PoseHand moves kawan2 with the wrist.
// A triangle is a hand triangle when all three of its vertices are.
struct HandVertexLayout {
    unsigned stride=0;
    unsigned weightOffset=0;   // float4
    unsigned indexOffset=0;    // ubyte4
};
struct HandBoneSet { const int* nodes=nullptr; unsigned count=0; };
// Writes up to `capacity` indices for each hand into outLeft/outRight and
// returns the counts; indices are the source's own values (relative to the
// same base vertex). indices32 null means 16-bit indices.
// partial: of the triangles taken, how many keep weight on a bone the hands
// are not given -- the ones skinning drags back toward the body.
struct HandTriangles { unsigned left=0,right=0,partial=0; };
HandTriangles SelectHandTriangles(const unsigned char* vertices,std::size_t vertexBytes,const HandVertexLayout&,
                                  const std::uint16_t* indices16,const std::uint32_t* indices32,
                                  unsigned indexCount,int baseVertex,
                                  const HandBoneSet& left,const HandBoneSet& right,
                                  std::uint32_t* outLeft,std::uint32_t* outRight,unsigned capacity,
                                  bool touching=false) noexcept;

struct IsolatedHandMesh {
    std::vector<unsigned char> vertices;
    std::vector<std::uint32_t> indices; // left, then right; compact vertices, base 0
    unsigned leftIndices=0,rightIndices=0;   // how many of `indices` are each hand's
    unsigned reweighted=0;
    unsigned collapsed=0;                     // vertices handed to the collapse slot
    unsigned tapered=0;                       // vertices sharing their arm weight with it
    bool valid=false;
};
// Keep all selected hand triangles and their non-skin attributes. Renormalize
// each vertex onto bones actually posed for that hand (including its cuff).
// Never modify the source mesh; sharing a source vertex across hands is safe.
IsolatedHandMesh IsolateHandMesh(const unsigned char* vertices,std::size_t vertexBytes,const HandVertexLayout&,
    const std::uint32_t* left,unsigned leftCount,const std::uint32_t* right,unsigned rightCount,int baseVertex,
    const HandBoneSet& leftPosed,const HandBoneSet& rightPosed,int collapseLeft=-1,int collapseRight=-1,
    int cuffLeft=-1,int cuffRight=-1);

// The wrist is closed by collapsing what lies past it, not by cutting. The
// selection takes every triangle that touches a hand vertex (touching=true),
// so the band just past the cuff comes too; its vertices with no weight on the
// hand's posed bones are handed to one spare palette slot per hand
// (IsolateHandMesh's collapse), and that slot holds CollapseTo(wrist): the
// wrist's position, and its turn shrunk to almost nothing. Each such vertex
// lands on the wrist and the cuff closes into it, instead of ending in an open
// ring -- whatever the model's bones and seams, as nothing about positions is
// read. (Capping the ring from positions, tried first, found hundreds of
// rings: the vertex is not laid out the way that assumed.)
//
// Shrunk, not zeroed. The first version had no turn at all, and the folded
// vertices' normals and tangents went through it to zero, which the shader
// normalises into nothing: the cap drew black on every class but the Fencer,
// whose dark gauntlet hid it (hardware, 2026-09-26). At kHandCollapseScale the
// vertices still land within a few millimetres of the wrist, and their normals
// keep a direction and turn with the hand.
constexpr float kHandCollapseScale=0.002f;
Matrix CollapseTo(const Matrix& wrist,float scale=kHandCollapseScale) noexcept;
// The first two palette slots in [0,nodeCount) that none of `used` names, one
// per hand; -1 where there are not enough.
void HandCollapseSlots(const int* used,unsigned usedCount,unsigned nodeCount,int out[2]) noexcept;

// Row-vector helpers shared with the tests.
Matrix MatrixMultiply(const Matrix& a,const Matrix& b) noexcept;   // a then b
Matrix RotationAbout(const float axis[3],float radians) noexcept;
Matrix RigidInverse(const Matrix&) noexcept;   // rotation and translation only
}
