#pragma once
// Are the two eye pictures really two different eyes? A Quest 3S player saw both
// eyes show the right eye's picture on Meta Link, and double vision on Steam
// Link that went away with one eye shut (2026-09-29), while everything the log
// could say was fine: two native views 67.8 mm apart, both submitted. What the
// GPU made of them was never looked at. This looks.
//
// Every few seconds, thin strips are read back from the same places in both eye
// pictures on two frames in a row, and compared:
//  - left against right: nothing different means both eyes got one picture;
//  - left against the previous frame's right: the left eye showing a stale right;
//  - the sideways shift that best lines a left strip up with the right one (the
//    disparity). Nearer things sit further right in the left eye, so a sound pair
//    shifts one way only; the other way means the eyes are swapped. A vertical
//    shift, which parallel eyes never have, is looked for as well.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace edf6vr {

// Nine strips: three across (a quarter, half, three quarters of the width) on
// three rows (45%, 60% and 75% down, where the ground is near and shifts most).
constexpr unsigned kEyeStrips=9, kEyeStripW=128, kEyeStripH=16;
constexpr int kEyeSearchX=64, kEyeSearchY=4;                  // right strips are this much larger each side
constexpr unsigned kEyeCellW=kEyeStripW+2*kEyeSearchX, kEyeCellH=kEyeStripH+2*kEyeSearchY;

// The strip's top-left corner in an image; false when it does not fit with its search margin.
inline bool EyeStripOrigin(unsigned strip,unsigned width,unsigned height,unsigned& x,unsigned& y) noexcept {
    static constexpr float across[3]={.25f,.5f,.75f}, down[3]={.45f,.6f,.75f};
    const float cx=across[strip%3]*static_cast<float>(width), cy=down[strip/3%3]*static_cast<float>(height);
    const float left=cx-kEyeStripW*.5f, top=cy-kEyeStripH*.5f;
    if(left<kEyeSearchX || top<kEyeSearchY) return false;
    x=static_cast<unsigned>(left); y=static_cast<unsigned>(top);
    return x+kEyeStripW+kEyeSearchX<=width && y+kEyeStripH+kEyeSearchY<=height;
}

// Brightness (the green channel) of the pixel formats the game's picture comes in.
enum class EyePixel { Unknown, Rgba8, Rgb10a2, Rg11b10f, Rgba16f };
inline EyePixel EyePixelOf(unsigned dxgiFormat) noexcept {
    switch(dxgiFormat) {
    case 27: case 28: case 29: case 30: case 87: case 88: case 90: case 91: case 92: case 93: return EyePixel::Rgba8;
    case 23: case 24: case 25: return EyePixel::Rgb10a2;
    case 26: return EyePixel::Rg11b10f;
    case 9: case 10: return EyePixel::Rgba16f;
    default: return EyePixel::Unknown;
    }
}
inline unsigned EyePixelBytes(EyePixel p) noexcept { return p==EyePixel::Rgba16f?8u:p==EyePixel::Unknown?0u:4u; }
inline float EyeHalf(std::uint16_t h) noexcept {
    const int e=(h>>10)&31, m=h&1023;
    const float v=e==0?std::ldexp(static_cast<float>(m),-24):e==31?65504.0f:std::ldexp(static_cast<float>(m|1024),e-25);
    return (h&0x8000)?-v:v;
}
inline float EyeGreen(EyePixel p,const unsigned char* px) noexcept {
    std::uint32_t v=0;
    switch(p) {
    case EyePixel::Rgba8: return px[1]/255.0f;                       // G is the second byte in RGBA and BGRA
    case EyePixel::Rgb10a2: std::memcpy(&v,px,4); return static_cast<float>((v>>10)&1023)/1023.0f;
    case EyePixel::Rg11b10f: {
        std::memcpy(&v,px,4);
        const unsigned g=(v>>11)&0x7FF, e=g>>6, m=g&63;
        return e==0?std::ldexp(static_cast<float>(m),-20):std::ldexp(static_cast<float>(m|64),static_cast<int>(e)-21);
    }
    case EyePixel::Rgba16f: { std::uint16_t h=0; std::memcpy(&h,px+2,2); return EyeHalf(h); }
    default: return 0;
    }
}

// One frame's strips, as brightness. right[][][] is the larger cell around the
// same place, so right[s][y+kEyeSearchY][x+kEyeSearchX] is the pixel at left[s][y][x].
struct EyeStrips {
    bool valid[kEyeStrips]{};
    float left[kEyeStrips][kEyeStripH][kEyeStripW]{};
    float right[kEyeStrips][kEyeCellH][kEyeCellW]{};
};

struct EyeCheckResult {
    float lr=-1,lrPrevious=-1,motionLeft=-1,motionRight=-1;   // share of sampled pixels that differ
    int dx[kEyeStrips]{},dy[kEyeStrips]{};                    // disparity found (dx > 0: nearer is further right in the left eye)
    bool judged[kEyeStrips]{};                                // enough texture and a clear best match
    int strips=0,detailed=0,positive=0,negative=0,level=0,vertical=0;
    const char* verdict="";
};

namespace eye_check_detail {
// Share of pixels that differ, over the strips marked in `use`.
inline float Differs(const EyeStrips& a,bool aRight,const EyeStrips& b,bool bRight,const bool* use) noexcept {
    unsigned long long n=0,differ=0;
    for(unsigned s=0;s<kEyeStrips;++s) {
        if(!a.valid[s] || !b.valid[s] || !use[s]) continue;
        for(unsigned y=0;y<kEyeStripH;++y) for(unsigned x=0;x<kEyeStripW;++x) {
            const float p=aRight?a.right[s][y+kEyeSearchY][x+kEyeSearchX]:a.left[s][y][x];
            const float q=bRight?b.right[s][y+kEyeSearchY][x+kEyeSearchX]:b.left[s][y][x];
            ++n; if(std::fabs(p-q)>1e-6f) ++differ;
        }
    }
    return n?static_cast<float>(differ)/static_cast<float>(n):-1.0f;
}
// How much detail a left strip has (mean distance from its own average).
inline float Texture(const EyeStrips& e,unsigned s) noexcept {
    double mean=0;
    for(unsigned y=0;y<kEyeStripH;++y) for(unsigned x=0;x<kEyeStripW;++x) mean+=e.left[s][y][x];
    mean/=kEyeStripW*kEyeStripH;
    double texture=0;
    for(unsigned y=0;y<kEyeStripH;++y) for(unsigned x=0;x<kEyeStripW;++x) texture+=std::fabs(e.left[s][y][x]-mean);
    return static_cast<float>(texture/(kEyeStripW*kEyeStripH));
}
inline float Sad(const EyeStrips& e,unsigned s,int dx,int dy) noexcept {
    double sum=0;
    for(unsigned y=0;y<kEyeStripH;++y) {
        const float* r=e.right[s][static_cast<int>(y)+kEyeSearchY+dy]+kEyeSearchX+dx;
        for(unsigned x=0;x<kEyeStripW;++x) sum+=std::fabs(e.left[s][y][x]-r[x]);
    }
    return static_cast<float>(sum/(kEyeStripW*kEyeStripH));
}
}

inline EyeCheckResult AnalyzeEyes(const EyeStrips& previous,const EyeStrips& now) noexcept {
    using namespace eye_check_detail;
    EyeCheckResult r{};
    // Only strips with detail count: a clear sky is the same in both eyes anyway.
    bool detailed[kEyeStrips]{};
    for(unsigned s=0;s<kEyeStrips;++s) if(now.valid[s] && Texture(now,s)>.004f) { detailed[s]=true; ++r.detailed; }
    r.lr=Differs(now,false,now,true,detailed);
    r.lrPrevious=Differs(now,false,previous,true,detailed);
    r.motionLeft=Differs(now,false,previous,false,detailed);
    r.motionRight=Differs(now,true,previous,true,detailed);
    for(unsigned s=0;s<kEyeStrips;++s) {
        if(!now.valid[s]) continue;
        ++r.strips;
        if(!detailed[s]) continue;
        // Every shift, sideways and up/down (about 20M differences for all nine
        // strips: run off the render thread). Level wins a tie.
        float best=1e30f; double total=0; int bestX=0,bestY=0;
        for(int dy=0;dy<=2*kEyeSearchY;++dy) {
            const int sy=(dy&1)?(dy+1)/2:-(dy/2);            // 0, +1, -1, +2, -2, ...
            for(int dx=-kEyeSearchX;dx<=kEyeSearchX;++dx) {
                const float v=Sad(now,s,dx,sy); total+=v;
                if(v<best-1e-7f) { best=v; bestX=dx; bestY=sy; }
            }
        }
        const float average=static_cast<float>(total/((2*kEyeSearchX+1)*(2*kEyeSearchY+1)));
        // The match clearly better than a typical shift, and not pinned to the
        // edge of the search.
        r.judged[s]=best<average*.6f && std::abs(bestX)<kEyeSearchX;
        r.dx[s]=-bestX; r.dy[s]=-bestY;
        if(!r.judged[s]) continue;
        if(r.dx[s]>0) ++r.positive; else if(r.dx[s]<0) ++r.negative; else ++r.level;
        if(std::abs(r.dy[s])>=2) ++r.vertical;            // a pixel either way is texture noise (seen on a sound pair)
    }
    const int judged=r.positive+r.negative+r.level;
    if(!r.strips) r.verdict="no strips (picture too small)";
    else if(r.detailed<2) r.verdict="no detail to judge (sky, dark or menu)";
    else if(r.lr<.002f) r.verdict="IDENTICAL: both eyes got the same picture";
    else if(r.lrPrevious<.002f && r.motionLeft>.02f) r.verdict="STALE: the left eye shows the previous frame's right picture";
    else if(r.negative>=2 && r.negative>r.positive) r.verdict="REVERSED: the shift runs the wrong way (eyes swapped?)";
    else if(judged>=2 && r.vertical*2>judged) r.verdict="VERTICAL: the eyes are offset up/down";
    else if(!judged) r.verdict="no detail to judge (sky, dark or menu)";
    else r.verdict="stereo looks right";
    return r;
}
}
