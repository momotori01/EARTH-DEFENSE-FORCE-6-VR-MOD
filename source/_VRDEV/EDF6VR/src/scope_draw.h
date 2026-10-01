#pragma once
#include "scope.h"
#include <d3d11.h>

namespace edf6vr {
// One eye's scope picture on its surface: the eyepiece of a scope, a screen, or a
// holographic monitor (scope.h ScopeKind), round or rectangular.
// - Lens style: a dark rim (GoldenEye VR: 0.6 dark over the outer tenth) and a
//   duplex reticle drawn into the scope only.
// - Holo styles: the picture slightly see-through, faint scan lines and a glowing
//   pale blue (weapons without a scope) or pale green (the Fencer's panel) edge.
struct ScopeDrawInput {
    ScopeLensFrame lens{};             // centre, normal, up, half height (radius), half width, shape
    Matrix view{},projection{};        // this eye's camera for the surface
    ID3D11Texture2D* source=nullptr;   // mode 1: the scope's view; mode 2: this eye's world picture
    int mode=0;
    int style=ScopeStyleLens;
    float tanHalf=0;                   // the picture's vertical half field
    float aspect=1;                    // the surface's width / height
    // How the picture stands: along the aim, its right and up (unit, in the world).
    float forward[3]{},right[3]{},up[3]{},origin[3]{};
    // Mode 1: the scope view's own camera axes and frustum edges (tangents).
    float cameraRight[3]{},cameraUp[3]{},cameraForward[3]{};
    float tanLeft=0,tanRight=0,tanTop=0,tanBottom=0;
    // Mode 2: world -> clip of this eye, to look the magnified ray up in its picture.
    Matrix sourceClip{};
};
bool DrawScopeLens(ID3D11DeviceContext*,ID3D11Texture2D* target,const ScopeDrawInput&) noexcept;
// Refusals: 1-6 inside DrawScopeLens; 10 no scope picture this frame, 11 the
// scope picture was not drawn with the narrow field (its projection's top
// tangent is fieldTan, the field asked for was wantTan).
struct ScopeDrawStats { unsigned long long drawn=0,refused=0,failed=0; unsigned lastRefusal=0; float fieldTan=0,wantTan=0; };
ScopeDrawStats ReadScopeDrawStats() noexcept;
void NoteScopeSkip(unsigned why,float fieldTan=0,float wantTan=0) noexcept;
}
