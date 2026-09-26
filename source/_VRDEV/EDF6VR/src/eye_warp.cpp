#include "eye_warp.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace edf6vr {
namespace {
// Searching backwards is what makes occlusion come out right. For an output
// pixel, walk the source line from the largest disparity down; the first
// surface that reaches this far is the one in front, because a nearer surface
// shifts further. Marching the other way would let a distant wall win over the
// weapon in front of it.
const char kShader[]=R"(
cbuffer Warp : register(b0) {
    float2 texel;        // 1/width, 1/height
    float  maxShift;     // pixels; the search never looks further than this
    float  shiftScale;   // eye separation times focal length, in pixel metres
    float  nearPlane;
    float  farPlane;
    float  dirSign;      // which way source pixels have to be looked for
    float  steps;
    float  nearestZ;     // nothing is treated as closer than this
    float  uiThreshold;  // how far the finished image may differ before it is UI
    float  uiMode;       // 0 ignore the UI, 1 pass it through, 2 take it out
    float  debug;        // 0 build the eye, 1 show the depth, 2 show the shift
    float2 maskTexel;    // 1/width, 1/height of the mask, which is half size
    float  neighbours;   // how many of the nine have to agree for a pixel to keep
    float  exact;        // the hudless picture is the frame itself, less the HUD
    float  nearKnee;     // parallax up to here is left alone, in pixels
    float  nearScale;    // and past it, how much of it is kept
    float  uiDirect;     // the colour handed in is the HUD itself, with alpha
    float  uiPad;        // the slot HLSL skips rather than straddle the next float2
    float2 reticleHalf;  // half the reticle square in the source image, in uv
    float  reticleOnly;  // 1 extract source square, 2 draw independent VR glyph
    float  eraseReticle;
};
Texture2D<float4> Colour  : register(t0);
Texture2D<float>  Depth   : register(t1);
Texture2D<float4> Hudless : register(t2);
// The tone curve the game applies between the two, measured from the frames
// themselves and handed over as 256 steps. Without it the difference between a
// linear scene target and a tone mapped frame swamps the HUD everywhere.
Texture2D<float4> Curve   : register(t3);
// What came out of the mask passes: one means UI.
Texture2D<float>  Mask    : register(t4);
SamplerState Point : register(s0);
// The mask is half size, so reading it with point sampling quantises every HUD
// edge to two pixel steps, which is what made the text look chewed. Read it
// smoothly instead and settle the edge itself at full resolution below.
SamplerState Smooth : register(s1);

struct VsOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VsOut VsMain(uint id : SV_VertexID) {
    VsOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
    return o;
}

float LinearZ(float d) {
    float z = nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
    // A first person weapon can be drawn with its own squashed depth range, and
    // read against the world's planes that lands absurdly close and throws the
    // shift off the end of the search. Nothing is allowed nearer than this.
    return max(z, nearestZ);
}

// Measured on a real frame: 97.2 per cent of the picture asks for under thirty
// pixels of parallax, while the weapon asks for 68 to 173. The strip between the
// two was never drawn by the game and can only be invented, so the honest fix is
// not to ask for so much.
//
// Straight, not curved. A curve bending towards a ceiling took the weapon's own
// 105 pixels of depth down to 1.5: a flat plate, while everything else about the
// picture, the foreshortening and the perspective, still said a barrel pointing
// away. The eye reads that as the weapon being under a different lens from the
// rest of the scene, and the disagreement changes as the weapon tilts, which is
// exactly how it was described. Keeping a constant slope instead is the near
// field seen with a smaller eye separation, which is a thing eyes are used to.
// At a tenth it leaves the weapon 10.5 pixels of its own depth and a narrower
// strip than the curve did, so it is better on both counts.
float Ease(float shift) {
    return shift <= nearKnee ? shift : nearKnee + (shift - nearKnee) * nearScale;
}

float ShiftAt(float2 uv, float k) {
    float2 s = uv + float2(dirSign * k * texel.x, 0);
    return Ease(shiftScale / LinearZ(Depth.SampleLevel(Point, saturate(s), 0)));
}

float3 LinearToSrgb(float3 v) {
    v = saturate(v);
    return v <= 0.0031308 ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

// What the finished frame would hold here if the UI had not been drawn: the
// pre-UI colour put through the measured curve. Anything much away from that is
// something the UI added.
float3 Expected(float2 uv) {
    // Nothing to correct for: the two pictures are the same frame at two moments
    // and everything the HUD did not touch matches bit for bit.
    if (exact > 0.5) return Hudless.SampleLevel(Point, uv, 0).rgb;
    float3 before = LinearToSrgb(Hudless.SampleLevel(Point, uv, 0).rgb);
    float3 expected;
    expected.r = Curve.SampleLevel(Point, float2(before.r, 0.5), 0).r;
    expected.g = Curve.SampleLevel(Point, float2(before.g, 0.5), 0).g;
    expected.b = Curve.SampleLevel(Point, float2(before.b, 0.5), 0).b;
    return expected;
}

float RawDiff(float2 uv) {
    float3 d = abs(Colour.SampleLevel(Point, uv, 0).rgb - Expected(uv));
    return max(d.r, max(d.g, d.b));
}

bool RawUi(float2 uv) { return RawDiff(uv) > uiThreshold; }

// Thin bright edges, wires and pylon lattice, come out of the raw test as lines
// and single pixels because a per pixel curve is not quite the whole story. The
// HUD is solid. Requiring the neighbours to agree keeps one and drops the other.
float4 PsMaskRaw(VsOut i) : SV_Target { return RawUi(i.uv) ? 1 : 0; }

float MaskAround(float2 uv) {
    float total = 0;
    [unroll] for (int dy = -1; dy <= 1; ++dy)
        [unroll] for (int dx = -1; dx <= 1; ++dx)
            total += Mask.SampleLevel(Point, uv + float2(dx, dy) * maskTexel, 0);
    return total;
}

float4 PsMaskErode(VsOut i) : SV_Target {
    return (Mask.SampleLevel(Point, i.uv, 0) > 0.5 && MaskAround(i.uv) >= neighbours) ? 1 : 0;
}

// Erosion eats the edges of the solid parts as well, so they are put back.
float4 PsMaskGrow(VsOut i) : SV_Target {
    return (Mask.SampleLevel(Point, i.uv, 0) > 0.5 || MaskAround(i.uv) >= 2) ? 1 : 0;
}

bool IsUi(float2 uv) {
    if (uiMode < 0.5) return false;
    // With the exact picture there is nothing to decide: a pixel the HUD did not
    // touch is identical, so the threshold only has to clear rounding.
    if (exact > 0.5) return RawDiff(uv) > uiThreshold;
    // The mask says which regions are HUD, and it is trusted where it is sure.
    // Only the band it is unsure about is decided again per pixel, which is what
    // puts the true edge of the text back without letting the raw test loose
    // over the whole picture, where it fires on wires and lattice.
    float region = Mask.SampleLevel(Smooth, uv, 0);
    if (region <= 0.05) return false;
    if (region >= 0.95) return true;
    // An anti aliased glyph edge is a faint difference, so it is held to a
    // gentler test here than the one that decided the region in the first place.
    return RawDiff(uv) > uiThreshold * 0.4;
}

// The scene as it stands, with the HUD taken out where it has been lifted onto
// its own panel. The pre-UI target is the same picture before the HUD was drawn,
// so its pixel through the measured curve is what belongs underneath.
float3 Scene(float2 uv) {
    bool clearReticle = eraseReticle > 0.5 && exact > 0.5
        && abs(uv.x - 0.5) <= reticleHalf.x
        && abs(uv.y - 0.5) <= reticleHalf.y;
    // Read between the pixels, not at them. The search settles the shift to a
    // fraction of a pixel, and taking the nearest pixel throws all of that away:
    // at this focal length one whole pixel of disparity is the difference
    // between eight and twelve metres at ten, and between fourteen and
    // thirty two at twenty. Rounded, the ground arrives as a handful of flat
    // plates at set distances, which is what reads as terracing. Sampling
    // between them gives back the depth the search already worked out.
    //
    // Where the read is exactly on a pixel, which is every one to one copy the
    // other passes make, this returns precisely what point sampling did.
    // One return: with two, fxc reports the value as possibly unset.
    float3 shown = Colour.SampleLevel(Smooth, uv, 0).rgb;
    float3 clean = (uiMode > 1.5 && IsUi(uv)) ? Expected(uv) : shown;
    return clearReticle ? Hudless.SampleLevel(Smooth, uv, 0).rgb : clean;
}

)"
R"(
// Everything the UI added, kept opaque, and nothing else. That is the HUD on its
// own, which can then hang on a panel of its own size in front of the eyes
// instead of being stretched across the whole field of view with the world.
float4 PsUi(VsOut i) : SV_Target {
    if (reticleOnly > 1.5) {
        // Coordinates in equivalent 1080p pixels: ReticlePixels controls the
        // canvas only. Scale and distance remain the existing XR quad settings.
        float2 p = (i.uv - 0.5) * reticleHalf * 2 / texel * (1080 * texel.y);
        float2 a = abs(p);
        float radial = abs(length(p) - 9) - 0.75;
        float ticks = max(min(a.x, a.y) - 0.75, max(14 - max(a.x, a.y), max(a.x, a.y) - 23));
        float shape = min(min(radial, ticks), length(p) - 1.2);
        if(reticleOnly>2.5) shape=min(max(abs(max(a.x,a.y)-18)-.8,10-min(a.x,a.y)),length(p)-1.2);
        float aa = max(fwidth(shape), 0.5);
        float ink = 1 - smoothstep(-aa, aa, shape);
        float coverage = 1 - smoothstep(-aa, aa, shape - 1);
        // Premultiplied red with a thin dark outline, transparent everywhere else.
        return float4(float3(1, 0.03, 0.03) * ink, coverage);
    }
    // Filling the reticle image: the middle of the HUD, stretched over the whole
    // of a small target of its own. It has to be a copy rather than the same
    // pixels, because the panel pass clears them.
    if (reticleOnly > 0.5) {
        float2 middle = 0.5 + (i.uv - 0.5) * reticleHalf * 2;
        // The same rules as the panel, at a moved reading point. Handing the
        // colour straight over is only right when it is already the HUD on its
        // own; against the finished frame it copies the world, which is what
        // put a little window of the scene where the reticle should be.
        if (uiDirect > 0.5) return Colour.SampleLevel(Smooth, middle, 0);
        if (!IsUi(middle)) return float4(0, 0, 0, 0);
        return float4(Colour.SampleLevel(Point, middle, 0).rgb, 1);
    }
    // The middle belongs to the reticle, which hangs on a quad of its own turned
    // to the weapon. Left in, it would appear twice: once where the weapon
    // points and once in the middle of where the player is looking.
    if (reticleHalf.y > 0
        && abs(i.uv.x - 0.5) < reticleHalf.x
        && abs(i.uv.y - 0.5) < reticleHalf.y) return float4(0, 0, 0, 0);
    // The HUD was drawn into a texture of its own, alpha and all. Nothing to
    // work out: hand it over as it stands.
    if (uiDirect > 0.5) return Colour.SampleLevel(Point, i.uv, 0);
    if (!IsUi(i.uv)) return float4(0, 0, 0, 0);
    return float4(Colour.SampleLevel(Point, i.uv, 0).rgb, 1);
}

// The drawn eye, with the UI taken out of it. It does not go through the warp,
// so without this the HUD would stay in one eye and be lifted out of the other.
//
// What goes in its place is not a neighbouring pixel dragged sideways: a
// subtitle bar is four hundred pixels wide and that smears one pixel across all
// of it. The pre-UI target is the same picture with the HUD not yet drawn, so
// the honest fill is that pixel through the measured curve. Verified off the
// game first; see tools/ui_split.py.
float4 PsErase(VsOut i) : SV_Target { return float4(Scene(i.uv), 1); }

float4 PsMain(VsOut i) : SV_Target {
    // Whether a thing is in this depth buffer at all is not something to reason
    // about. Mode 1 paints the distance it holds, near white and far black, so a
    // weapon that never writes depth is simply not there to see.
    if (debug > 0.5) {
        float z = LinearZ(Depth.SampleLevel(Point, i.uv, 0));
        // Mode 3 lays the two over each other in one eye, so whether the depth
        // sits on the object or beside it can be judged without comparing across
        // eyes, where the eye separation confuses the question.
        if (debug > 2.5) {
            float3 c = Colour.SampleLevel(Point, i.uv, 0).rgb;
            return float4(lerp(c, float3(1, 0, 0), saturate(1 - z / 3) * 0.65), 1);
        }
        if (debug > 1.5) return float4(saturate((shiftScale / z) / maxShift).xxx, 1);
        return float4(saturate(1 - z / 20).xxx, 1);
    }
    // The UI is composited after the scene, so its pixels sit on whatever depth
    // happens to be behind them, and shifting them by that tears text apart
    // because neighbouring letters ride different distances. With uiMode 1 the
    // finished pixel is used as it stands, which leaves the UI in the same place
    // in both eyes. With uiMode 2 it has been lifted onto its own panel and has
    // to come out of here, which Scene does wherever the colour is read.
    float4 final = float4(Scene(i.uv), 1);
    if (uiMode > 0.5 && uiMode < 1.5 && IsUi(i.uv)) return final;

    // Where a near thing has moved aside it leaves behind a strip of background
    // that was never drawn. The walk below takes the largest shift that reaches
    // this pixel, which is right when two surfaces genuinely overlap, because
    // the nearer one is in front. Over a hole it is wrong: the only thing that
    // reaches is the edge of the near object itself, and taking it smears that
    // object across the gap, so it ends up occupying its old place and its new
    // one at once and reads as bigger than it is. That is what near objects
    // looking enlarged was.
    //
    // The two cases are told apart by how well the shift agrees with the
    // distance walked. A real answer agrees closely; an edge overshoots wildly,
    // because the shift jumps from the object's value to the background's with
    // nothing in between. An overshoot is remembered and the walk carries on
    // down to the background, which is what should fill a hole.
    // Include zero exactly. Repeated float subtraction sometimes skipped the
    // final interval, treating a distant flat surface as a disocclusion hole
    // and doubling its disparity below. Integer indexing removes that jump.
    int intervals = max(4, (int)ceil(steps));
    float step = maxShift / intervals;
    float tolerance = max(step * 2, 4);
    float found = -1, lastFail = -1, previous = maxShift;
    [loop] for (int sampleIndex = 0; sampleIndex <= intervals; ++sampleIndex) {
        float k = maxShift * (float)(intervals - sampleIndex) / intervals;
        float shift = ShiftAt(i.uv, k);
        if (shift >= k) {
            if (shift - k <= tolerance) { found = k; break; }
        } else {
            previous = k;
            lastFail = k;   // the shortest walk that does not reach here
        }
    }

    // In the middle of a hole, nothing to the right reaches this pixel except
    // the object that made the hole, and every one of those overshoots. The
    // shortest walk that failed lands just past that object, on the background,
    // which is what belongs in the gap. Taking the object's edge instead is what
    // painted it across the gap and made it look bigger than it is.
    float low, high;
    if (found >= 0) { low = found; high = previous; }
    else if (lastFail >= 0) { low = max(0, lastFail - step); high = lastFail; }
    else return final;

    // The coarse walk lands within one step, which at these sizes is a few
    // pixels: on an object it shows as a jagged edge, and over a hole it makes
    // the fill hop about and come out hatched rather than streaked.
    [unroll] for (int r = 0; r < 8; ++r) {
        float mid = (low + high) * 0.5;
        if (ShiftAt(i.uv, mid) >= mid) low = mid; else high = mid;
    }
    float pick = found >= 0 ? low : high;
    // Over a hole, pick lands on the background pixel immediately past the object
    // that made it, and every pixel of the hole lands on the same one, so the
    // whole strip comes out as that one pixel held across it. That is the streak
    // beside the weapon, and with only one eye warped it has nothing to agree
    // with in the other, which is what reads as the gun bending.
    //
    // Doubling the distance reflects the background back across that edge
    // instead. The strip fills with real texture at the right scale, and the
    // seam falls exactly on the edge, where there is already a discontinuity and
    // nothing is added by another. What is behind the weapon cannot be known;
    // this is the least wrong thing that can be put there.
    if (found < 0) pick *= 2;
    float2 src = i.uv + float2(dirSign * pick * texel.x, 0);
    return float4(Scene(saturate(src)), 1);
}
)";

struct Constants {
    float texel[2];
    float maxShift;
    float shiftScale;
    float nearPlane;
    float farPlane;
    float dirSign;
    float steps;
    float nearestZ;
    float uiThreshold;
    float uiMode;
    float debug;
    float maskTexel[2];
    float neighbours;
    float exact;
    float nearKnee;
    float nearScale;
    float uiDirect;
    // HLSL will not straddle a float2 across a sixteen byte register, so it
    // skips the last slot of this one and starts reticleHalf in the next. The
    // gap has to be here too or every field after it reads from the wrong place.
    float uiPad;
    float reticleHalf[2];
    float reticleOnly;
    float eraseReticle;
};
// A constant buffer has to be a whole number of sixteen byte registers, and
// CreateBuffer simply refuses when it is not. Refusing there took the whole warp
// down and left no stereo at all, so the size is checked here where a mistake
// costs a build rather than a session.
static_assert(sizeof(Constants)%16==0,"pad Constants to a multiple of 16 bytes");

using D3DCompileFn=HRESULT (WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,
                                     LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);

ID3D11VertexShader* g_vs=nullptr;
ID3D11PixelShader* g_ps=nullptr;
ID3D11PixelShader* g_psUi=nullptr;
ID3D11PixelShader* g_psErase=nullptr;
ID3D11PixelShader* g_psMaskRaw=nullptr;
ID3D11PixelShader* g_psMaskErode=nullptr;
ID3D11PixelShader* g_psMaskGrow=nullptr;
// The mask is built once a frame at half size and read by the passes that
// follow, rather than each of them working it out again per pixel. At four K
// the difference between the two is the difference between affordable and not.
ID3D11Texture2D* g_mask[2]{};
ID3D11RenderTargetView* g_maskTarget[2]{};
ID3D11ShaderResourceView* g_maskView[2]{};
unsigned g_maskWidth=0, g_maskHeight=0;
int g_maskReady=-1;
// The tone curve between the pre-UI colour and the finished frame, measured from
// the frames and handed to the shader as 256 steps.
ID3D11Texture2D* g_curve=nullptr;
ID3D11ShaderResourceView* g_curveView=nullptr;
ID3D11Texture2D* g_stripFinal=nullptr;
ID3D11Texture2D* g_stripBefore=nullptr;
ULONGLONG g_curveTaken=0;
bool g_curveReady=false;
unsigned short* g_counts=nullptr;   // [channel][before][after]
ID3D11Texture2D* g_stripMask=nullptr;
ULONGLONG g_coverTaken=0;
float g_coverage=-1;               // per cent of the frame called UI, or -1
char g_curveNote[96]="no curve yet";
ID3D11Buffer* g_constants=nullptr;
ID3D11SamplerState* g_sampler=nullptr;
ID3D11SamplerState* g_smooth=nullptr;
ID3D11BlendState* g_blend=nullptr;
ID3D11DepthStencilState* g_depthState=nullptr;
ID3D11RasterizerState* g_raster=nullptr;
ID3D11Device* g_owner=nullptr;
char g_status[192]="not built";

// The three textures change rarely: colour and depth are the same objects every
// frame and the swapchain cycles a handful of images. One slot each, rebuilt
// when the texture behind it changes, is enough and keeps this allocation-free
// in the ordinary case.
struct ViewSlot { void* resource=nullptr; ID3D11ShaderResourceView* srv=nullptr; };
ViewSlot g_colourView{}, g_depthView{}, g_hudlessView{};
struct TargetSlot { void* resource=nullptr; ID3D11RenderTargetView* rtv=nullptr; };
// Both eye swapchains and the UI one, three images each, plus room to spare. At
// eight this filled and then refused to make any more, so passes started failing
// silently part way through a frame and the two eyes drifted apart.
constexpr int kMaxTargets=32;
TargetSlot g_targetViews[kMaxTargets]{};
int g_nextTarget=0;

void SetStatusf(const char* format,...) noexcept {
    va_list args; va_start(args,format);
    std::vsnprintf(g_status,sizeof(g_status),format,args);
    va_end(args);
}

bool CompileShaders(ID3D11Device* device) noexcept {
    static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");
    if(!compiler) { SetStatusf("d3dcompiler_47.dll did not load"); return false; }
    auto compile=reinterpret_cast<D3DCompileFn>(
        reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    if(!compile) { SetStatusf("D3DCompile is missing"); return false; }
    ID3DBlob* code=nullptr; ID3DBlob* errors=nullptr;
    const UINT flags=D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if(FAILED(compile(kShader,sizeof(kShader)-1,"eye_warp",nullptr,nullptr,"VsMain","vs_5_0",
                      flags,0,&code,&errors)) || !code) {
        SetStatusf("vertex shader failed: %s",errors?static_cast<const char*>(errors->GetBufferPointer()):"");
        if(errors) errors->Release();
        if(code) code->Release();
        return false;
    }
    const bool madeVs=SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),
                                                           nullptr,&g_vs));
    code->Release();
    if(errors) { errors->Release(); errors=nullptr; }
    if(!madeVs) { SetStatusf("CreateVertexShader failed"); return false; }
    code=nullptr;
    if(FAILED(compile(kShader,sizeof(kShader)-1,"eye_warp",nullptr,nullptr,"PsMain","ps_5_0",
                      flags,0,&code,&errors)) || !code) {
        SetStatusf("pixel shader failed: %s",errors?static_cast<const char*>(errors->GetBufferPointer()):"");
        if(errors) errors->Release();
        if(code) code->Release();
        return false;
    }
    const bool madePs=SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),
                                                          nullptr,&g_ps));
    code->Release();
    if(errors) errors->Release();
    if(!madePs) { SetStatusf("CreatePixelShader failed"); return false; }
    code=nullptr;
    if(FAILED(compile(kShader,sizeof(kShader)-1,"eye_warp",nullptr,nullptr,"PsUi","ps_5_0",
                      flags,0,&code,&errors)) || !code) {
        SetStatusf("UI shader failed: %s",errors?static_cast<const char*>(errors->GetBufferPointer()):"");
        if(errors) errors->Release();
        if(code) code->Release();
        return false;
    }
    const bool madeUi=SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),
                                                          nullptr,&g_psUi));
    code->Release();
    if(errors) errors->Release();
    if(!madeUi) { SetStatusf("CreatePixelShader failed for the UI pass"); return false; }
    code=nullptr;
    if(FAILED(compile(kShader,sizeof(kShader)-1,"eye_warp",nullptr,nullptr,"PsErase","ps_5_0",
                      flags,0,&code,&errors)) || !code) {
        SetStatusf("erase shader failed: %s",errors?static_cast<const char*>(errors->GetBufferPointer()):"");
        if(errors) errors->Release();
        if(code) code->Release();
        return false;
    }
    const bool madeErase=SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),
                                                             nullptr,&g_psErase));
    code->Release();
    if(errors) errors->Release();
    if(!madeErase) { SetStatusf("CreatePixelShader failed for the erase pass"); return false; }
    struct Extra { const char* entry; ID3D11PixelShader** shader; };
    const Extra extras[]={{"PsMaskRaw",&g_psMaskRaw},{"PsMaskErode",&g_psMaskErode},
                          {"PsMaskGrow",&g_psMaskGrow}};
    for(const auto& extra:extras) {
        code=nullptr;
        if(FAILED(compile(kShader,sizeof(kShader)-1,"eye_warp",nullptr,nullptr,extra.entry,"ps_5_0",
                          flags,0,&code,&errors)) || !code) {
            SetStatusf("%s failed: %s",extra.entry,
                       errors?static_cast<const char*>(errors->GetBufferPointer()):"");
            if(errors) errors->Release();
            if(code) code->Release();
            return false;
        }
        const bool made=SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),
                                                            code->GetBufferSize(),nullptr,extra.shader));
        code->Release();
        if(errors) { errors->Release(); errors=nullptr; }
        if(!made) { SetStatusf("CreatePixelShader failed for %s",extra.entry); return false; }
    }
    return true;
}

ULONGLONG g_buildFailedAt=0;

bool Build(ID3D11Device* device) noexcept {
    if(g_owner==device && g_vs && g_ps) return true;
    // A failed build used to be retried every frame, and the retry recompiles
    // every shader: measured at ninety milliseconds a frame, five frames a
    // second, on top of the warp already being dead. The failure is what needs
    // fixing, but it must not also bring the game down while it is happening.
    const ULONGLONG now=GetTickCount64();
    if(g_buildFailedAt && now-g_buildFailedAt<2000) return false;
    ReleaseWarp();
    if(!CompileShaders(device)) { g_buildFailedAt=GetTickCount64(); return false; }
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth=sizeof(Constants);
    buffer.Usage=D3D11_USAGE_DYNAMIC;
    buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    const HRESULT madeBuffer=device->CreateBuffer(&buffer,nullptr,&g_constants);
    if(FAILED(madeBuffer)) {
        // Says which size and which error. This failing takes the whole warp
        // down, so it must not be a bare sentence when it happens again.
        SetStatusf("constant buffer failed hr=0x%08X size=%u",
                   static_cast<unsigned>(madeBuffer),
                   static_cast<unsigned>(sizeof(Constants)));
        return false;
    }
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    sampler.MaxLOD=D3D11_FLOAT32_MAX;
    if(FAILED(device->CreateSamplerState(&sampler,&g_sampler))) {
        SetStatusf("sampler failed"); g_buildFailedAt=GetTickCount64(); return false;
    }
    sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if(FAILED(device->CreateSamplerState(&sampler,&g_smooth))) {
        SetStatusf("smooth sampler failed"); g_buildFailedAt=GetTickCount64(); return false;
    }
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    if(FAILED(device->CreateBlendState(&blend,&g_blend))) { SetStatusf("blend state failed"); g_buildFailedAt=GetTickCount64(); return false; }
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable=FALSE;
    depth.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
    if(FAILED(device->CreateDepthStencilState(&depth,&g_depthState))) {
        SetStatusf("depth state failed"); g_buildFailedAt=GetTickCount64(); return false;
    }
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode=D3D11_FILL_SOLID;
    raster.CullMode=D3D11_CULL_NONE;
    raster.DepthClipEnable=TRUE;
    if(FAILED(device->CreateRasterizerState(&raster,&g_raster))) {
        SetStatusf("rasteriser state failed"); g_buildFailedAt=GetTickCount64(); return false;
    }
    g_owner=device;
    SetStatusf("built");
    g_buildFailedAt=0;
    return true;
}

// A typeless image can take an RTV of either kind. Matching what the plain copy
// path does, which moves bits and converts nothing, means writing through a
// non-sRGB view; when the runtime hands back an sRGB image instead, the source
// is read as sRGB too so the round trip still lands on the same bits.
DXGI_FORMAT RenderFormat(DXGI_FORMAT format,bool& srgb) noexcept {
    srgb=false;
    switch(format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: srgb=true; return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: srgb=true; return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT ColourFormat(DXGI_FORMAT format,bool srgb) noexcept {
    switch(format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return srgb?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return srgb?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

DXGI_FORMAT DepthFormat(DXGI_FORMAT format) noexcept {
    switch(format) {
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

bool EnsureView(ID3D11Device* device,ViewSlot& slot,ID3D11Texture2D* texture,DXGI_FORMAT format) noexcept {
    if(slot.resource==texture && slot.srv) return true;
    if(slot.srv) { slot.srv->Release(); slot.srv=nullptr; }
    slot.resource=nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format=format;
    desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels=1;
    if(FAILED(device->CreateShaderResourceView(texture,&desc,&slot.srv)) || !slot.srv) return false;
    slot.resource=texture;
    return true;
}

ID3D11RenderTargetView* EnsureTarget(ID3D11Device* device,ID3D11Texture2D* texture,DXGI_FORMAT format) noexcept {
    for(auto& slot:g_targetViews) if(slot.resource==texture && slot.rtv) return slot.rtv;
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format=format;
    desc.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    for(auto& slot:g_targetViews) {
        if(slot.rtv) continue;
        if(FAILED(device->CreateRenderTargetView(texture,&desc,&slot.rtv)) || !slot.rtv) return nullptr;
        slot.resource=texture;
        return slot.rtv;
    }
    // Full. Rather than refuse and leave a pass undone, take the oldest slot:
    // the swapchains cycle through a fixed handful of images, so a view that has
    // gone unused the longest is the safest one to give up.
    auto& slot=g_targetViews[g_nextTarget];
    g_nextTarget=(g_nextTarget+1)%kMaxTargets;
    if(slot.rtv) { slot.rtv->Release(); slot={}; }
    if(FAILED(device->CreateRenderTargetView(texture,&desc,&slot.rtv)) || !slot.rtv) return nullptr;
    slot.resource=texture;
    return slot.rtv;
}

// The game is mid-frame from D3D11's point of view, so everything touched here
// has to go back exactly as it was.
struct SavedState {
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ID3D11DepthStencilView* depth=nullptr;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewportCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11VertexShader* vs=nullptr;
    ID3D11PixelShader* ps=nullptr;
    ID3D11InputLayout* layout=nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11ShaderResourceView* resources[5]{};
    ID3D11SamplerState* samplers[2]{};
    ID3D11Buffer* constants=nullptr;
    ID3D11BlendState* blend=nullptr;
    FLOAT blendFactor[4]{};
    UINT sampleMask=0xFFFFFFFF;
    ID3D11DepthStencilState* depthState=nullptr;
    UINT stencilRef=0;
    ID3D11RasterizerState* raster=nullptr;

    void Save(ID3D11DeviceContext* context) noexcept {
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,&depth);
        context->RSGetViewports(&viewportCount,viewports);
        context->VSGetShader(&vs,nullptr,nullptr);
        context->PSGetShader(&ps,nullptr,nullptr);
        context->IAGetInputLayout(&layout);
        context->IAGetPrimitiveTopology(&topology);
        context->PSGetShaderResources(0,5,resources);
        context->PSGetSamplers(0,2,samplers);
        context->PSGetConstantBuffers(0,1,&constants);
        context->OMGetBlendState(&blend,blendFactor,&sampleMask);
        context->OMGetDepthStencilState(&depthState,&stencilRef);
        context->RSGetState(&raster);
    }
    void Restore(ID3D11DeviceContext* context) noexcept {
        context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,depth);
        context->RSSetViewports(viewportCount,viewports);
        context->VSSetShader(vs,nullptr,0);
        context->PSSetShader(ps,nullptr,0);
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(topology);
        context->PSSetShaderResources(0,5,resources);
        context->PSSetSamplers(0,2,samplers);
        context->PSSetConstantBuffers(0,1,&constants);
        context->OMSetBlendState(blend,blendFactor,sampleMask);
        context->OMSetDepthStencilState(depthState,stencilRef);
        context->RSSetState(raster);
        for(auto& target:targets) if(target) target->Release();
        if(depth) depth->Release();
        if(vs) vs->Release();
        if(ps) ps->Release();
        if(layout) layout->Release();
        for(auto& resource:resources) if(resource) resource->Release();
        for(auto& one:samplers) if(one) one->Release();
        if(constants) constants->Release();
        if(blend) blend->Release();
        if(depthState) depthState->Release();
        if(raster) raster->Release();
    }
};
}

namespace {
// Rows read out of each image to measure the curve. Eight was not enough: on a
// real frame the bin for 192 held three to ten pixels out of the whole picture,
// so its median was whatever those few happened to be, and the curve came back
// with 192 mapping below 128. See tools/ui_split.py, where the same count was
// checked against the dumps.
constexpr int kStrips=24;
constexpr unsigned kLeastSamples=8;   // below this a bin is guesswork, not a median
constexpr unsigned kCurveSteps=256;

float HalfToFloat(unsigned short value) noexcept {
    const unsigned sign=(value>>15)&1, exponent=(value>>10)&0x1F, mantissa=value&0x3FF;
    unsigned bits=0;
    if(!exponent) {
        if(!mantissa) bits=sign<<31;
        else {
            unsigned e=exponent, m=mantissa;
            do { m<<=1; --e; } while(!(m&0x400));
            bits=(sign<<31)|((e+127-15+1)<<23)|((m&0x3FF)<<13);
        }
    } else if(exponent==0x1F) bits=(sign<<31)|0x7F800000u|(mantissa<<13);
    else bits=(sign<<31)|((exponent+127-15)<<23)|(mantissa<<13);
    float out=0;
    std::memcpy(&out,&bits,4);
    return out;
}

int LinearToIndex(float value) noexcept {
    if(!(value>0)) return 0;
    const float encoded=value<=0.0031308f?value*12.92f
                                         :1.055f*std::pow(value,1.0f/2.4f)-0.055f;
    const int index=static_cast<int>(encoded*255.0f+0.5f);
    return index<0?0:(index>255?255:index);
}

ID3D11Texture2D* MakeStrips(ID3D11Device* device,const D3D11_TEXTURE2D_DESC& source) noexcept {
    D3D11_TEXTURE2D_DESC desc=source;
    desc.Height=kStrips;
    desc.MipLevels=1;
    desc.ArraySize=1;
    desc.SampleDesc.Count=1;
    desc.SampleDesc.Quality=0;
    desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=0;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    desc.MiscFlags=0;
    ID3D11Texture2D* strips=nullptr;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&strips))) return nullptr;
    return strips;
}

bool ReadStrips(ID3D11DeviceContext* context,ID3D11Texture2D* source,ID3D11Texture2D* strips,
                unsigned width,unsigned height) noexcept {
    for(int i=0;i<kStrips;++i) {
        D3D11_BOX box{};
        box.left=0; box.right=width;
        box.top=(height*(2*i+1))/(2*kStrips);
        box.bottom=box.top+1;
        box.front=0; box.back=1;
        if(box.bottom>height) return false;
        context->CopySubresourceRegion(strips,0,0,static_cast<UINT>(i),0,source,0,&box);
    }
    return true;
}

// For each pre-UI value, the value the finished frame usually holds. The HUD is
// far too small a minority to move a median, so what comes out is the game's
// tone curve with the HUD ignored, which is exactly what the mask needs to
// subtract before anything can be called UI.
bool UpdateCurve(ID3D11Device* device,ID3D11DeviceContext* context,
                 ID3D11Texture2D* colour,ID3D11Texture2D* hudless) noexcept {
    const auto now=GetTickCount64();
    if(g_curveReady && now-g_curveTaken<1000) return true;
    D3D11_TEXTURE2D_DESC finalDesc{}, beforeDesc{};
    colour->GetDesc(&finalDesc);
    hudless->GetDesc(&beforeDesc);
    if(finalDesc.Width!=beforeDesc.Width || finalDesc.Height!=beforeDesc.Height) return false;
    if(beforeDesc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT) {
        std::snprintf(g_curveNote,sizeof(g_curveNote),"pre-UI format %u not handled",beforeDesc.Format);
        return false;
    }
    if(!g_stripFinal) g_stripFinal=MakeStrips(device,finalDesc);
    if(!g_stripBefore) g_stripBefore=MakeStrips(device,beforeDesc);
    if(!g_stripFinal || !g_stripBefore) return false;
    if(!g_counts) {
        g_counts=new(std::nothrow) unsigned short[3*kCurveSteps*kCurveSteps];
        if(!g_counts) return false;
    }
    if(!g_curve) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=kCurveSteps; desc.Height=1; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&g_curve)) || !g_curve) return false;
        if(FAILED(device->CreateShaderResourceView(g_curve,nullptr,&g_curveView)) || !g_curveView)
            return false;
    }
    g_curveTaken=now;
    if(!ReadStrips(context,colour,g_stripFinal,finalDesc.Width,finalDesc.Height)) return false;
    if(!ReadStrips(context,hudless,g_stripBefore,beforeDesc.Width,beforeDesc.Height)) return false;
    D3D11_MAPPED_SUBRESOURCE finalMap{}, beforeMap{};
    if(FAILED(context->Map(g_stripFinal,0,D3D11_MAP_READ,0,&finalMap)) || !finalMap.pData) return false;
    if(FAILED(context->Map(g_stripBefore,0,D3D11_MAP_READ,0,&beforeMap)) || !beforeMap.pData) {
        context->Unmap(g_stripFinal,0);
        return false;
    }
    std::memset(g_counts,0,sizeof(unsigned short)*3*kCurveSteps*kCurveSteps);
    const bool blueFirst=finalDesc.Format==DXGI_FORMAT_B8G8R8A8_UNORM
                      || finalDesc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    for(int row=0;row<kStrips;++row) {
        auto after=static_cast<const unsigned char*>(finalMap.pData)
                   +static_cast<size_t>(row)*finalMap.RowPitch;
        auto before=reinterpret_cast<const unsigned short*>(
            static_cast<const unsigned char*>(beforeMap.pData)
            +static_cast<size_t>(row)*beforeMap.RowPitch);
        for(unsigned x=0;x<finalDesc.Width;++x) {
            for(int c=0;c<3;++c) {
                const int source=c==0&&blueFirst?2:(c==2&&blueFirst?0:c);
                const int from=LinearToIndex(HalfToFloat(before[x*4+c]));
                const int to=after[x*4+source];
                auto& count=g_counts[(c*kCurveSteps+from)*kCurveSteps+to];
                if(count<0xFFFF) ++count;
            }
        }
    }
    context->Unmap(g_stripBefore,0);
    context->Unmap(g_stripFinal,0);

    unsigned char table[kCurveSteps*4]{};
    for(int c=0;c<3;++c) {
        int last=0;
        for(unsigned from=0;from<kCurveSteps;++from) {
            unsigned total=0;
            for(unsigned to=0;to<kCurveSteps;++to) total+=g_counts[(c*kCurveSteps+from)*kCurveSteps+to];
            if(total>=kLeastSamples) {
                unsigned run=0;
                for(unsigned to=0;to<kCurveSteps;++to) {
                    run+=g_counts[(c*kCurveSteps+from)*kCurveSteps+to];
                    if(run*2>=total) {
                        // A tone curve does not go down. Holding it to that
                        // throws away the bins that had barely any pixels in
                        // them without having to decide which those were.
                        const int median=static_cast<int>(to);
                        if(median>last) last=median;
                        break;
                    }
                }
            }
            // A value nothing in the frame happens to hold, or hardly anything,
            // keeps the last one rather than dropping to zero.
            table[from*4+c]=static_cast<unsigned char>(last);
        }
    }
    for(unsigned i=0;i<kCurveSteps;++i) table[i*4+3]=255;
    D3D11_MAPPED_SUBRESOURCE curveMap{};
    if(FAILED(context->Map(g_curve,0,D3D11_MAP_WRITE_DISCARD,0,&curveMap)) || !curveMap.pData)
        return false;
    std::memcpy(curveMap.pData,table,sizeof(table));
    context->Unmap(g_curve,0);
    g_curveReady=true;
    std::snprintf(g_curveNote,sizeof(g_curveNote),"curve 0->%u 128->%u 192->%u",
                  table[0],table[128*4],table[192*4]);
    return true;
}

bool EnsureMask(ID3D11Device* device,unsigned width,unsigned height) noexcept {
    if(g_maskWidth==width && g_maskHeight==height && g_mask[0] && g_mask[1]) return true;
    for(int i=0;i<2;++i) {
        if(g_maskView[i]) { g_maskView[i]->Release(); g_maskView[i]=nullptr; }
        if(g_maskTarget[i]) { g_maskTarget[i]->Release(); g_maskTarget[i]=nullptr; }
        if(g_mask[i]) { g_mask[i]->Release(); g_mask[i]=nullptr; }
    }
    g_maskWidth=g_maskHeight=0;
    g_maskReady=-1;
    if(g_stripMask) { g_stripMask->Release(); g_stripMask=nullptr; }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    for(int i=0;i<2;++i) {
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&g_mask[i])) || !g_mask[i]) return false;
        if(FAILED(device->CreateRenderTargetView(g_mask[i],nullptr,&g_maskTarget[i]))) return false;
        if(FAILED(device->CreateShaderResourceView(g_mask[i],nullptr,&g_maskView[i]))) return false;
    }
    g_maskWidth=width; g_maskHeight=height;
    return true;
}
}

bool WarpEye(ID3D11Device* device,ID3D11DeviceContext* context,
             ID3D11Texture2D* colour,ID3D11Texture2D* depth,ID3D11Texture2D* target,
             ID3D11Texture2D* hudless,const WarpParams& params) noexcept {
    if(!device || !context || !colour || !target) { SetStatusf("missing input"); return false; }
    if(!depth && !params.uiOnly && !params.eraseOnly) { SetStatusf("missing depth"); return false; }
    if(params.uiDirect && !params.uiOnly) { SetStatusf("uiDirect is for the panel only"); return false; }
    D3D11_TEXTURE2D_DESC colourDesc{}, depthDesc{}, targetDesc{};
    colour->GetDesc(&colourDesc);
    if(depth) depth->GetDesc(&depthDesc);
    target->GetDesc(&targetDesc);
    if(colourDesc.SampleDesc.Count!=1 || (depth && depthDesc.SampleDesc.Count!=1)) {
        SetStatusf("multisampled input"); return false;
    }
    // The reticle pass is the one place that deliberately writes a different
    // size: it lifts a square out of the middle of the HUD onto a small image of
    // its own. Everywhere else a mismatch means something is wrong.
    if(!params.reticleOnly
       && (colourDesc.Width!=targetDesc.Width || colourDesc.Height!=targetDesc.Height)) {
        SetStatusf("colour %ux%u does not match target %ux%u",
                   colourDesc.Width,colourDesc.Height,targetDesc.Width,targetDesc.Height);
        return false;
    }
    if(depth && (depthDesc.Width!=colourDesc.Width || depthDesc.Height!=colourDesc.Height)) {
        SetStatusf("depth %ux%u does not match colour %ux%u",
                   depthDesc.Width,depthDesc.Height,colourDesc.Width,colourDesc.Height);
        return false;
    }
    if(!params.uiOnly && !params.eraseOnly
       && (params.verticalFovRadians<=0 || params.farPlane<=params.nearPlane)) {
        SetStatusf("projection not known yet"); return false;
    }
    bool srgb=false;
    const auto renderFormat=RenderFormat(targetDesc.Format,srgb);
    const auto colourFormat=ColourFormat(colourDesc.Format,srgb);
    const auto depthFormat=depth?DepthFormat(depthDesc.Format):DXGI_FORMAT_R32_FLOAT;
    if(renderFormat==DXGI_FORMAT_UNKNOWN || colourFormat==DXGI_FORMAT_UNKNOWN
       || depthFormat==DXGI_FORMAT_UNKNOWN) {
        SetStatusf("format target=%u colour=%u depth=%u not handled",
                   targetDesc.Format,colourDesc.Format,depthDesc.Format);
        return false;
    }
    if(!Build(device)) return false;
    if(!EnsureView(device,g_colourView,colour,colourFormat)) { SetStatusf("colour view failed"); return false; }
    if(depth && !EnsureView(device,g_depthView,depth,depthFormat)) { SetStatusf("depth view failed"); return false; }
    auto rtv=EnsureTarget(device,target,renderFormat);
    if(!rtv) { SetStatusf("render target view failed"); return false; }
    bool haveHudless=false;
    D3D11_TEXTURE2D_DESC hudlessDesc{};
    if(hudless && params.useHudless) {
        hudless->GetDesc(&hudlessDesc);
        haveHudless=hudlessDesc.Width==colourDesc.Width && hudlessDesc.Height==colourDesc.Height
                    && hudlessDesc.SampleDesc.Count==1
                    && EnsureView(device,g_hudlessView,hudless,ColourFormat(hudlessDesc.Format,srgb));
    }

    // Square pixels, so the focal length in pixels comes from the vertical field
    // of view the game was told to draw, and the sideways shift of a point at z
    // metres is the eye separation times that, over z.
    const float focal=(params.uiOnly||params.eraseOnly)?1.0f
        :static_cast<float>(targetDesc.Height)*0.5f/std::tan(params.verticalFovRadians*0.5f);
    const float shiftScale=params.eyeSeparation*focal;
    const float nearest=params.nearestMetres>0.01f?params.nearestMetres:0.01f;
    // The knee in pixels, from the distance it is set in, so the setting means
    // the same thing at every resolution.
    const float nearFrom=params.nearKneeMetres>0.05f?params.nearKneeMetres:0.05f;
    const float nearKnee=shiftScale/nearFrom;
    const float nearScale=params.nearScale>0?(params.nearScale<1?params.nearScale:1.0f):0.0f;
    // Nothing is treated as nearer than nearestMetres, so that is the largest
    // parallax anything can ask for, and after the compression the largest it
    // can be granted. The walk never has to look past it, and the same number of
    // steps over a shorter range lands finer.
    const float rawMax=shiftScale/nearest;
    float maxShift=rawMax<=nearKnee?rawMax:nearKnee+(rawMax-nearKnee)*nearScale;
    if(maxShift>static_cast<float>(targetDesc.Width)*0.25f)
        maxShift=static_cast<float>(targetDesc.Width)*0.25f;

    Constants values{};
    // texel is used to step across the source, so it belongs to the source,
    // which is the target everywhere except the reticle pass.
    values.texel[0]=1.0f/static_cast<float>(colourDesc.Width);
    values.texel[1]=1.0f/static_cast<float>(colourDesc.Height);
    values.maxShift=maxShift;
    values.shiftScale=shiftScale;
    values.nearPlane=params.nearPlane;
    values.farPlane=params.farPlane;
    // A point seen from an eye further right sits further left in the image, so
    // the pixel that lands here came from further right in what was drawn.
    values.dirSign=params.buildRightOfDrawn?1.0f:-1.0f;
    values.steps=params.steps>=4?params.steps:4;
    values.nearestZ=nearest;
    values.uiDirect=params.uiDirect?1.0f:0.0f;
    values.reticleHalf[0]=(params.uiOnly||params.eraseReticle)?params.reticleHalf[0]:0.0f;
    values.reticleHalf[1]=(params.uiOnly||params.eraseReticle)?params.reticleHalf[1]:0.0f;
    values.reticleOnly=params.reticleOnly?(params.drawReticle?(params.lockReticle?3.0f:2.0f):1.0f):0.0f;
    values.eraseReticle=(params.eraseReticle && params.exact && haveHudless)?1.0f:0.0f;
    values.nearKnee=nearKnee;
    values.nearScale=nearScale;
    values.exact=params.exact?1.0f:0.0f;
    // One step of eight bits either way, so rounding in a copy cannot read as HUD.
    values.uiThreshold=params.exact?(1.5f/255.0f):params.uiThreshold;
    // Without either the exact picture or a finished mask, nothing can be called
    // UI, whatever the mode asked for.
    values.uiMode=(haveHudless && (params.exact || g_maskReady>=0))?params.uiMode:0.0f;
    values.maskTexel[0]=g_maskWidth?1.0f/static_cast<float>(g_maskWidth):0.0f;
    values.maskTexel[1]=g_maskHeight?1.0f/static_cast<float>(g_maskHeight):0.0f;
    values.neighbours=params.neighbours;
    values.debug=params.debug;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(g_constants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)) || !mapped.pData) {
        SetStatusf("constant map failed"); return false;
    }
    std::memcpy(mapped.pData,&values,sizeof(values));
    context->Unmap(g_constants,0);

    SavedState saved{};
    saved.Save(context);
    auto pixelShader=params.uiOnly?g_psUi:(params.eraseOnly?g_psErase:g_ps);

    ID3D11RenderTargetView* targets[]={rtv};
    context->OMSetRenderTargets(1,targets,nullptr);
    D3D11_VIEWPORT viewport{};
    viewport.Width=static_cast<float>(targetDesc.Width);
    viewport.Height=static_cast<float>(targetDesc.Height);
    viewport.MaxDepth=1.0f;
    context->RSSetViewports(1,&viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* noBuffers[]={nullptr};
    const UINT zero[]={0};
    context->IASetVertexBuffers(0,1,noBuffers,zero,zero);
    context->VSSetShader(g_vs,nullptr,0);
    context->PSSetShader(pixelShader,nullptr,0);
    if(params.uiOnly) {
        const FLOAT clear[4]={0,0,0,0};
        context->ClearRenderTargetView(rtv,clear);
    }
    ID3D11ShaderResourceView* views[]={g_colourView.srv,depth?g_depthView.srv:nullptr,
                                       haveHudless?g_hudlessView.srv:nullptr,g_curveView,
                                       g_maskReady>=0?g_maskView[g_maskReady]:nullptr};
    context->PSSetShaderResources(0,5,views);
    ID3D11SamplerState* both[]={g_sampler,g_smooth};
    context->PSSetSamplers(0,2,both);
    context->PSSetConstantBuffers(0,1,&g_constants);
    const FLOAT factor[4]={0,0,0,0};
    context->OMSetBlendState(g_blend,factor,0xFFFFFFFF);
    context->OMSetDepthStencilState(g_depthState,0);
    context->RSSetState(g_raster);
    context->Draw(3,0);

    ID3D11ShaderResourceView* none[]={nullptr,nullptr,nullptr,nullptr,nullptr};
    context->PSSetShaderResources(0,5,none);
    saved.Restore(context);
    // A bare zero for hudless leaves nowhere to look, and it turned itself off at
    // four K without saying why, so the shapes go in the line.
    if(!params.uiOnly && !params.eraseOnly)
        SetStatusf("ok shift=%.1f max=%.0f near=%.0fpx x%.2f focal=%.0f hudless=%d colour=%ux%u ui=%ux%u/%u",
                   shiftScale,maxShift,nearKnee,nearScale,focal,haveHudless?1:0,
                   colourDesc.Width,colourDesc.Height,
                   hudlessDesc.Width,hudlessDesc.Height,hudlessDesc.SampleDesc.Count);
    return true;
}

bool BuildUiMask(ID3D11Device* device,ID3D11DeviceContext* context,
                 ID3D11Texture2D* colour,ID3D11Texture2D* hudless,const WarpParams& params) noexcept {
    g_maskReady=-1;
    if(!device || !context || !colour || !hudless) return false;
    if(!Build(device)) return false;
    D3D11_TEXTURE2D_DESC colourDesc{}, hudlessDesc{};
    colour->GetDesc(&colourDesc);
    hudless->GetDesc(&hudlessDesc);
    if(colourDesc.Width!=hudlessDesc.Width || colourDesc.Height!=hudlessDesc.Height) return false;
    if(!UpdateCurve(device,context,colour,hudless)) return false;
    bool srgb=false;
    if(!EnsureView(device,g_colourView,colour,ColourFormat(colourDesc.Format,srgb))) return false;
    if(!EnsureView(device,g_hudlessView,hudless,hudlessDesc.Format)) return false;
    // Half size: it is what the numbers were settled at, and it makes the three
    // passes cost almost nothing even at four K.
    if(!EnsureMask(device,colourDesc.Width/2,colourDesc.Height/2)) return false;

    Constants values{};
    values.texel[0]=1.0f/static_cast<float>(colourDesc.Width);
    values.texel[1]=1.0f/static_cast<float>(colourDesc.Height);
    values.uiThreshold=params.uiThreshold;
    values.uiMode=2;
    values.maskTexel[0]=1.0f/static_cast<float>(g_maskWidth);
    values.maskTexel[1]=1.0f/static_cast<float>(g_maskHeight);
    values.neighbours=params.neighbours;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(g_constants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)) || !mapped.pData)
        return false;
    std::memcpy(mapped.pData,&values,sizeof(values));
    context->Unmap(g_constants,0);

    SavedState saved{};
    saved.Save(context);
    D3D11_VIEWPORT viewport{};
    viewport.Width=static_cast<float>(g_maskWidth);
    viewport.Height=static_cast<float>(g_maskHeight);
    viewport.MaxDepth=1.0f;
    context->RSSetViewports(1,&viewport);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* noBuffers[]={nullptr};
    const UINT zero[]={0};
    context->IASetVertexBuffers(0,1,noBuffers,zero,zero);
    context->VSSetShader(g_vs,nullptr,0);
    ID3D11SamplerState* both[]={g_sampler,g_smooth};
    context->PSSetSamplers(0,2,both);
    context->PSSetConstantBuffers(0,1,&g_constants);
    const FLOAT factor[4]={0,0,0,0};
    context->OMSetBlendState(g_blend,factor,0xFFFFFFFF);
    context->OMSetDepthStencilState(g_depthState,0);
    context->RSSetState(g_raster);

    // Raw test, then drop what the neighbours do not agree with, then put the
    // edges of what survived back. Ping-pong between the two mask textures,
    // since each pass reads the one the last wrote.
    struct Pass { ID3D11PixelShader* shader; int target; int source; };
    const Pass passes[]={{g_psMaskRaw,0,-1},{g_psMaskErode,1,0},{g_psMaskGrow,0,1}};
    for(const auto& pass:passes) {
        ID3D11ShaderResourceView* inputs[]={g_colourView.srv,nullptr,g_hudlessView.srv,g_curveView,
                                            pass.source>=0?g_maskView[pass.source]:nullptr};
        ID3D11RenderTargetView* targets[]={g_maskTarget[pass.target]};
        context->OMSetRenderTargets(1,targets,nullptr);
        context->PSSetShaderResources(0,5,inputs);
        context->PSSetShader(pass.shader,nullptr,0);
        context->Draw(3,0);
        ID3D11ShaderResourceView* none[]={nullptr,nullptr,nullptr,nullptr,nullptr};
        context->PSSetShaderResources(0,5,none);
    }
    saved.Restore(context);
    g_maskReady=0;

    // How much of the picture ended up called UI. A single number, but it is the
    // difference between knowing the mask found the HUD and having to ask for a
    // frame dump to find out.
    const auto now=GetTickCount64();
    if(now-g_coverTaken>=1000) {
        g_coverTaken=now;
        D3D11_TEXTURE2D_DESC maskDesc{};
        g_mask[0]->GetDesc(&maskDesc);
        if(!g_stripMask) g_stripMask=MakeStrips(device,maskDesc);
        D3D11_MAPPED_SUBRESOURCE rows{};
        if(g_stripMask && ReadStrips(context,g_mask[0],g_stripMask,g_maskWidth,g_maskHeight)
           && SUCCEEDED(context->Map(g_stripMask,0,D3D11_MAP_READ,0,&rows)) && rows.pData) {
            unsigned long long lit=0;
            for(int row=0;row<kStrips;++row) {
                auto line=static_cast<const unsigned char*>(rows.pData)
                          +static_cast<size_t>(row)*rows.RowPitch;
                for(unsigned x=0;x<g_maskWidth;++x) if(line[x]>127) ++lit;
            }
            context->Unmap(g_stripMask,0);
            g_coverage=static_cast<float>(lit)*100.0f
                       /static_cast<float>(kStrips*static_cast<int>(g_maskWidth));
        }
    }
    return true;
}

const char* MaskStatus() noexcept {
    static char note[160];
    if(g_coverage>=0) std::snprintf(note,sizeof(note),"%s ui=%.1f%%",g_curveNote,g_coverage);
    else std::snprintf(note,sizeof(note),"%s",g_curveNote);
    return note;
}

const char* WarpStatus() noexcept { return g_status; }

void ReleaseWarp() noexcept {
    if(g_colourView.srv) { g_colourView.srv->Release(); g_colourView={}; }
    if(g_depthView.srv) { g_depthView.srv->Release(); g_depthView={}; }
    if(g_hudlessView.srv) { g_hudlessView.srv->Release(); g_hudlessView={}; }
    for(auto& slot:g_targetViews) if(slot.rtv) { slot.rtv->Release(); slot={}; }
    if(g_vs) { g_vs->Release(); g_vs=nullptr; }
    if(g_ps) { g_ps->Release(); g_ps=nullptr; }
    if(g_psUi) { g_psUi->Release(); g_psUi=nullptr; }
    if(g_psErase) { g_psErase->Release(); g_psErase=nullptr; }
    if(g_psMaskRaw) { g_psMaskRaw->Release(); g_psMaskRaw=nullptr; }
    if(g_psMaskErode) { g_psMaskErode->Release(); g_psMaskErode=nullptr; }
    if(g_psMaskGrow) { g_psMaskGrow->Release(); g_psMaskGrow=nullptr; }
    for(int i=0;i<2;++i) {
        if(g_maskView[i]) { g_maskView[i]->Release(); g_maskView[i]=nullptr; }
        if(g_maskTarget[i]) { g_maskTarget[i]->Release(); g_maskTarget[i]=nullptr; }
        if(g_mask[i]) { g_mask[i]->Release(); g_mask[i]=nullptr; }
    }
    g_maskWidth=g_maskHeight=0;
    g_maskReady=-1;
    if(g_curveView) { g_curveView->Release(); g_curveView=nullptr; }
    if(g_curve) { g_curve->Release(); g_curve=nullptr; }
    if(g_stripFinal) { g_stripFinal->Release(); g_stripFinal=nullptr; }
    if(g_stripMask) { g_stripMask->Release(); g_stripMask=nullptr; }
    g_coverage=-1;
    if(g_stripBefore) { g_stripBefore->Release(); g_stripBefore=nullptr; }
    delete[] g_counts; g_counts=nullptr;
    g_curveReady=false;
    if(g_constants) { g_constants->Release(); g_constants=nullptr; }
    if(g_sampler) { g_sampler->Release(); g_sampler=nullptr; }
    if(g_smooth) { g_smooth->Release(); g_smooth=nullptr; }
    if(g_blend) { g_blend->Release(); g_blend=nullptr; }
    if(g_depthState) { g_depthState->Release(); g_depthState=nullptr; }
    if(g_raster) { g_raster->Release(); g_raster=nullptr; }
    g_owner=nullptr;
}
}
