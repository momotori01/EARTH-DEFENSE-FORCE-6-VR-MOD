#pragma once
#include <Windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;

namespace edf6vr {
// Builds the eye the game did not render, from the eye it did and the scene
// depth, as one full-screen pass.
//
// The game keeps drawing once per frame, so the frame rate does not move, and
// both eyes come from the same instant, so there is no time difference between
// them at all. That is the whole point: rendering the world twice would have
// halved the frame rate and left each eye exactly where it already was.
//
// What it cannot do is see round things. Where the second eye should look past
// an edge there is nothing to show, and the nearest pixel is stretched instead.
struct WarpParams {
    float eyeSeparation=0.065f;      // metres between the drawn eye and the built one
    float verticalFovRadians=0;      // what the game was told to render
    float nearPlane=0.1f;            // EDF6: camera+0x28
    float farPlane=1000.0f;          // EDF6: camera+0x2C
    bool buildRightOfDrawn=true;     // whether the built eye sits right of the drawn one
    float steps=48;                  // samples along the search
    float nearestMetres=0.35f;       // nothing is treated as closer than this
    // Nearer than nearKneeMetres the parallax keeps only nearScale of itself,
    // at a constant slope. Beyond it nothing changes. See Ease in the shader for
    // why the slope is constant rather than a curve.
    float nearKneeMetres=1.5f;
    float nearScale=1.0f;
        // The colour handed in is the HUD on its own, drawn straight into a texture
    // of ours, so the panel pass copies it and decides nothing. Panel only.
    bool uiDirect=false;
    // Half the reticle square as a fraction of the source image, x and y, cut
    // out of the panel because a quad of its own carries it. Zero leaves the
    // panel whole. With reticleOnly the target is filled with that square
    // instead, which is how the reticle image is made: it has to be a copy,
    // since the panel pass clears those pixels.
    float reticleHalf[2]{};
    bool reticleOnly=false;
    // Independent VR glyph; does not require the native HUD to contain a sight.
    bool drawReticle=false;
    bool lockReticle=false; // native weapon requests lock-on HUD
    // Remove residual source-eye reticle pixels using the exact pre-HUD image,
    // including differences below the UI threshold. Never use the curve fallback.
    bool eraseReticle=false;
    bool useHudless=true;            // keep the UI where it is instead of shifting it
    // The picture handed over as hudless is the presented image from just before
    // the HUD went on: same eight bits, same tone mapping, same anti aliasing, so
    // every pixel the HUD did not touch is identical and any difference at all is
    // the HUD. Nothing has to be measured or swept, and thin bright things stop
    // leaking through, which no threshold on the older comparison could stop.
    bool exact=false;
    // Only for the older comparison, between the linear scene target and the
    // tone mapped frame. Settled by sweeping real frames, not chosen: below 48 of
    // 255 the wires and pylon lattice come back, and at five neighbours the
    // reticle is eroded away.
    float uiThreshold=48.0f/255.0f;
    float neighbours=4;
    float debug=0;                   // 1 paints the depth, 2 paints the shift
    // Keep only what the UI added and make the rest transparent, so the HUD
    // can hang on a panel of its own instead of being stretched with the world.
    bool uiOnly=false;
    // The drawn eye does not go through the warp, so it needs its own pass to
    // take the UI out, or the HUD would be lifted out of one eye only.
    bool eraseOnly=false;
    // 0 ignore the UI, 1 pass it through where it is, 2 take it out because it
    // is being shown on its own panel.
    float uiMode=1;
};

// Colour and depth are read, target is written. All three belong to the game's
// device. Returns false and leaves the target untouched if anything is missing,
// so the caller can fall back to showing the drawn eye in both.
// hudless is the same image taken before the UI was composited, or null. Where
// the two differ the finished pixel is passed through unshifted, which is what
// keeps text readable: shifting it by the depth behind it tears it apart.
// Works out which pixels are HUD, once a frame, into a half size mask the passes
// below then read. The two images are not in the same space, so the tone curve
// between them is measured from the frames first; see tools/ui_mask.py, where
// all of this was settled against real pixels before any of it was written.
bool BuildUiMask(ID3D11Device*,ID3D11DeviceContext*,
                 ID3D11Texture2D* colour,ID3D11Texture2D* hudless,const WarpParams&) noexcept;
// The measured curve, for the log.
const char* MaskStatus() noexcept;

bool WarpEye(ID3D11Device*,ID3D11DeviceContext*,
             ID3D11Texture2D* colour,ID3D11Texture2D* depth,ID3D11Texture2D* target,
             ID3D11Texture2D* hudless,const WarpParams&) noexcept;
// Why the last WarpEye failed, or how the shaders were built.
const char* WarpStatus() noexcept;
void ReleaseWarp() noexcept;
}
