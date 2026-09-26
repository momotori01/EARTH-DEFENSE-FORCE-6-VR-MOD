#pragma once
#include <d3d11.h>
#include <cstdint>

// The body's own DrawIndexed calls, reduced to the hands.
//
// While a HandDrawScope is alive on the render thread, every DrawIndexed the
// game issues (through the two material-loop sites weapon stereo already hooks)
// belongs to the player's body model. The first time a mesh is seen its vertex
// and index buffers are copied back to the CPU, the hand triangles are picked
// out with SelectHandTriangles, and from then on only those are submitted, from
// private index/vertex buffers with only posed hand/cuff bone influences.
// Meshes with no hand in them -- the head, the hair
// -- draw nothing. Nothing of the game's is written.
namespace edf6vr {
// layer: draw into the weapon's live stereo layer (ReplayHandStereo).
// If the layer refuses, omit the hand to avoid a duplicate in the native view.
struct HandDrawScope {
    HandDrawScope(bool left,bool right,bool layer) noexcept;
    ~HandDrawScope() noexcept;
    HandDrawScope(const HandDrawScope&)=delete;
    HandDrawScope& operator=(const HandDrawScope&)=delete;
};
// The palette indices of each hand's bones, and how many nodes the skeleton
// has (a blend index at or past it means the vertex layout was misread).
void SetHandBones(const int* left,unsigned leftCount,const int* right,unsigned rightCount,unsigned nodeCount,
                  int leftCuff=-1,int rightCuff=-1) noexcept;
// Forget every mesh: the soldier or the model changed.
void ResetHandMeshes() noexcept;
// True when the call was the body's and has been dealt with (drawn as hands, or
// dropped); the caller must not forward it.
bool HandDrawIntercept(ID3D11DeviceContext*,UINT count,UINT start,INT base) noexcept;
struct HandDrawStats {
    unsigned long long handDraws=0,layerDraws=0,layerRejected=0,dropped=0,pending=0;
    unsigned meshes=0,trianglesLeft=0,trianglesRight=0;
    // Selected source triangles with mixed weights (before skin isolation).
    unsigned trianglesPartial=0;
    unsigned verticesReweighted=0;
    // Vertices past the cuff folded onto the wrist (IsolateHandMesh's collapse).
    unsigned verticesCollapsed=0;
    unsigned verticesTapered=0;
    char note[96]{};
};
HandDrawStats ReadHandDrawStats() noexcept;
// The palette slots SetHandBones set aside, one per hand, for CollapseTo(wrist).
void ReadHandCollapseSlots(int out[2]) noexcept;
}
