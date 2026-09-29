#include "eye_check.h"
#include <cstdio>
#include <cstring>
#include <memory>

using namespace edf6vr;

static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

// A textured picture: a fixed pseudo-random value per pixel, shifted per frame.
static float Picture(int x,int y,int frame) {
    unsigned h=static_cast<unsigned>(x+frame*7)*73856093u ^ static_cast<unsigned>(y)*19349663u;
    h^=h>>13; h*=0x5bd1e995u; h^=h>>15;
    return static_cast<float>(h&1023)/1023.0f;
}
// Both eyes of one frame. The right eye sees what the left sees at x+dx, y+dy
// (dx > 0: the right way round, nearer content further right in the left eye).
static void Fill(EyeStrips& e,unsigned width,unsigned height,int dx,int dy,int frame,bool flat=false,int rightFrame=-1) {
    if(rightFrame<0) rightFrame=frame;
    for(unsigned s=0;s<kEyeStrips;++s) {
        unsigned x=0,y=0;
        e.valid[s]=EyeStripOrigin(s,width,height,x,y);
        if(!e.valid[s]) continue;
        for(unsigned j=0;j<kEyeStripH;++j) for(unsigned i=0;i<kEyeStripW;++i)
            e.left[s][j][i]=flat?.5f:Picture(static_cast<int>(x+i),static_cast<int>(y+j),frame);
        for(unsigned j=0;j<kEyeCellH;++j) for(unsigned i=0;i<kEyeCellW;++i) {
            const int px=static_cast<int>(x+i)-kEyeSearchX+dx, py=static_cast<int>(y+j)-kEyeSearchY+dy;
            e.right[s][j][i]=flat?.5f:Picture(px,py,rightFrame);
        }
    }
}

int main() {
    auto previous=std::make_unique<EyeStrips>(), now=std::make_unique<EyeStrips>();
    const unsigned w=2368,h=2160;

    // Strips fit even the smallest resolution a player picked (1184x1080), not a tiny one.
    unsigned x=0,y=0;
    for(unsigned s=0;s<kEyeStrips;++s) CHECK(EyeStripOrigin(s,1184,1080,x,y));
    CHECK(!EyeStripOrigin(0,200,100,x,y));

    // A sound pair: every strip shifts 6 px the right way, none vertically.
    Fill(*previous,w,h,6,0,0); Fill(*now,w,h,6,0,1);
    auto r=AnalyzeEyes(*previous,*now);
    CHECK(r.strips==9 && r.detailed==9 && r.positive==9 && r.negative==0 && r.vertical==0);
    CHECK(r.dx[4]==6 && r.dy[4]==0);
    CHECK(r.lr>.5f && r.motionLeft>.5f);
    CHECK(!std::strcmp(r.verdict,"stereo looks right"));

    // Both eyes got one picture.
    Fill(*previous,w,h,0,0,0); Fill(*now,w,h,0,0,1);
    r=AnalyzeEyes(*previous,*now);
    CHECK(r.lr==0.0f);
    CHECK(!std::strncmp(r.verdict,"IDENTICAL",9));

    // Swapped eyes: the shift runs the other way.
    Fill(*previous,w,h,-6,0,0); Fill(*now,w,h,-6,0,1);
    r=AnalyzeEyes(*previous,*now);
    CHECK(r.negative==9 && r.positive==0 && r.dx[0]==-6);
    CHECK(!std::strncmp(r.verdict,"REVERSED",8));

    // Offset up/down as well as sideways.
    Fill(*previous,w,h,4,3,0); Fill(*now,w,h,4,3,1);
    r=AnalyzeEyes(*previous,*now);
    CHECK(r.dx[2]==4 && r.dy[2]==3 && r.vertical==9);
    CHECK(!std::strncmp(r.verdict,"VERTICAL",8));
    Fill(*previous,w,h,6,1,0); Fill(*now,w,h,6,1,1);   // one pixel up/down alone is not called vertical
    r=AnalyzeEyes(*previous,*now);
    CHECK(r.dy[4]==1 && r.vertical==0 && !std::strcmp(r.verdict,"stereo looks right"));

    // The left eye shows the previous frame's right picture.
    Fill(*previous,w,h,6,0,0);
    Fill(*now,w,h,0,0,0,false,1);           // right eye moved on, left eye kept...
    for(unsigned s=0;s<kEyeStrips;++s)      // ...exactly the previous right picture
        for(unsigned j=0;j<kEyeStripH;++j) for(unsigned i=0;i<kEyeStripW;++i)
            now->left[s][j][i]=previous->right[s][j+kEyeSearchY][i+kEyeSearchX];
    r=AnalyzeEyes(*previous,*now);
    CHECK(r.lrPrevious==0.0f && r.lr>.5f);
    CHECK(!std::strncmp(r.verdict,"STALE",5));

    // Nothing to judge on a flat picture (sky, fade to black); still not called identical when it differs.
    Fill(*previous,w,h,0,0,0,true); Fill(*now,w,h,0,0,1,true);
    for(unsigned s=0;s<kEyeStrips;++s) now->right[s][kEyeSearchY][kEyeSearchX]=.6f;
    r=AnalyzeEyes(*previous,*now);
    CHECK(r.positive+r.negative+r.level==0 && r.detailed==0);
    CHECK(!std::strncmp(r.verdict,"no detail",9));
    Fill(*now,w,h,0,0,1,true);              // and a flat picture the same in both eyes is not "identical"
    r=AnalyzeEyes(*previous,*now);
    CHECK(!std::strncmp(r.verdict,"no detail",9));

    // Pixel formats: green channel of RGBA8, R10G10B10A2, R11G11B10 float, RGBA16 float.
    const unsigned char rgba[4]={10,255,20,255};
    CHECK(EyePixelOf(29)==EyePixel::Rgba8 && EyePixelOf(87)==EyePixel::Rgba8 && EyeGreen(EyePixel::Rgba8,rgba)==1.0f);
    const unsigned packed10=1023u<<10;
    unsigned char b10[4]; std::memcpy(b10,&packed10,4);
    CHECK(EyePixelOf(24)==EyePixel::Rgb10a2 && EyeGreen(EyePixel::Rgb10a2,b10)==1.0f);
    const unsigned packed11=(15u<<6)<<11;   // exponent 15, mantissa 0: 1.0
    unsigned char b11[4]; std::memcpy(b11,&packed11,4);
    CHECK(EyePixelOf(26)==EyePixel::Rg11b10f && std::fabs(EyeGreen(EyePixel::Rg11b10f,b11)-1.0f)<1e-6f);
    const unsigned char half[8]={0,0,0,0x3C,0,0,0,0};   // G = 0x3C00 = 1.0
    CHECK(EyePixelOf(10)==EyePixel::Rgba16f && EyePixelBytes(EyePixel::Rgba16f)==8 && EyeGreen(EyePixel::Rgba16f,half)==1.0f);
    CHECK(EyePixelOf(71)==EyePixel::Unknown && EyePixelBytes(EyePixel::Unknown)==0);

    std::printf("EDF6VR eye check tests: %d failures\n",failures);
    return failures?1:0;
}
