#include "nameplate_scale.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <windows.h>
namespace edf6vr {
namespace {
struct FloatSite {
    unsigned rva; const unsigned char* prefix; std::size_t prefixSize; float shipped; const char* purpose;
    bool verified=false;
};
const unsigned char kScaleXPrefix[]={0x48,0xC7,0x45,0x00};
const unsigned char kScaleYPrefix[]={0xC7,0x45,0x14};
const unsigned char kTextXPrefix[]={0x48,0xC7,0x85,0xC0,0x00,0x00,0x00};
const unsigned char kTextYPrefix[]={0xC7,0x85,0xD4,0x00,0x00,0x00};
const unsigned char kLetterXPrefix[]={0x48,0xC7,0x85,0x00,0x01,0x00,0x00};
const unsigned char kLetterYPrefix[]={0xC7,0x85,0x14,0x01,0x00,0x00};
FloatSite g_sites[]={
    {kPlateScaleXRva,kScaleXPrefix,sizeof(kScaleXPrefix),1.0f,"nameplate scale x"},
    {kPlateScaleYRva,kScaleYPrefix,sizeof(kScaleYPrefix),1.0f,"nameplate scale y"},
    {kPlateTextScaleXRva,kTextXPrefix,sizeof(kTextXPrefix),1.0f,"nameplate text block scale x"},
    {kPlateTextScaleYRva,kTextYPrefix,sizeof(kTextYPrefix),1.0f,"nameplate text block scale y"},
    {kPlateLetterScaleXRva,kLetterXPrefix,sizeof(kLetterXPrefix),1.0f,"nameplate letter block scale x"},
    {kPlateLetterScaleYRva,kLetterYPrefix,sizeof(kLetterYPrefix),1.0f,"nameplate letter block scale y"},
};
const unsigned char kChatXPrefix[]={0x48,0xC7,0x45,0x30};
const unsigned char kChatYPrefix[]={0xC7,0x45,0x44};
FloatSite g_chatSites[]={
    {kChatScaleXRva,kChatXPrefix,sizeof(kChatXPrefix),1.0f,"chat bubble scale x"},
    {kChatScaleYRva,kChatYPrefix,sizeof(kChatYPrefix),1.0f,"chat bubble scale y"},
};
std::atomic<float> g_textScale{1.0f};
using ChatTextDraw=void*(*)(void*,void*,void*,const float*,std::uintptr_t);
using ChatAnchor=void*(*)(void*,void*,const float*,void*);
using ChatProject=void*(*)(void*,const void*,void*,void*);
ChatTextDraw g_chatTextDraw=nullptr;
ChatAnchor g_chatAnchor=nullptr;
ChatProject g_chatProject=nullptr;
std::atomic<float> g_chatTextScale{1.0f};
bool g_chatTextHooked=false;
// The head point of the bubble being drawn, with the frame (rbp) it was
// projected in. Cleared once that bubble's text is drawn -- the last thing the
// head-projecting path does (nothing between 801977 and 802059 leaves the
// draw) -- so a bubble that projects no head does not inherit the last one's.
struct ChatHead { const unsigned char* frame=nullptr; float point[2]{}; };
thread_local ChatHead t_chatHead;
std::atomic<unsigned long long> g_chatAboutHead{0},g_chatAboutTail{0};
ChatAnchorNote g_chatLast;   // render thread writes, the log reads; a torn read is only a log line
}
// Declared in the header (the test runs them over a stand-in frame).
void* ChatProjectHook(void* out,const void* in,void* camera,void* state) noexcept {
    void* result=g_chatProject(out,in,camera,state);
    if(out && result) {
        t_chatHead.frame=static_cast<const unsigned char*>(out)-0xC0;
        std::memcpy(t_chatHead.point,result,sizeof(t_chatHead.point));
    }
    return result;
}
// The point to shrink about, from a pointer into the bubble draw's frame
// (rbp+offset): the head when this bubble projected one, else the tail's
// target at [rbp-0x40].
static const float* ChatCentre(const void* fromRbp,std::ptrdiff_t offset,bool* head=nullptr) noexcept {
    const auto* rbp=static_cast<const unsigned char*>(fromRbp)-offset;
    const bool own=t_chatHead.frame==rbp;
    if(head) *head=own;
    return own?t_chatHead.point:reinterpret_cast<const float*>(rbp-0x40);
}
void* ChatTextDrawHook(void* layout,void* state,void* style,const float* matrix,std::uintptr_t flag) noexcept {
    const float scale=g_chatTextScale.load(std::memory_order_relaxed);
    float centre[2]{};
    if(matrix) std::memcpy(centre,ChatCentre(matrix,0x80),sizeof(centre));
    t_chatHead.frame=nullptr;   // this bubble is done
    if(scale==1.0f || !matrix) return g_chatTextDraw(layout,state,style,matrix,flag);
    float scaled[16];
    ScaleChatTextMatrix(matrix,scale,scaled);   // the draw copies it on entry
    ScaleAbout(centre,scale,scaled+12);
    // Each line's offset through the scaled rows too (see the header).
    return g_chatTextDraw(layout,state,style,scaled,ChatTextRunsThroughMatrix(flag));
}
void* ChatAnchorHook(void* self,void* out,const float* corner,void* state) noexcept {
    const float scale=g_chatTextScale.load(std::memory_order_relaxed);
    if(scale==1.0f || !corner) return g_chatAnchor(self,out,corner,state);
    bool head=false;
    const float* centre=ChatCentre(corner,0x10,&head);
    float moved[4]={corner[0],corner[1],corner[2],corner[3]};
    ScaleAbout(centre,scale,moved);
    (head?g_chatAboutHead:g_chatAboutTail).fetch_add(1,std::memory_order_relaxed);
    g_chatLast.centre[0]=centre[0]; g_chatLast.centre[1]=centre[1];
    g_chatLast.corner[0]=corner[0]; g_chatLast.corner[1]=corner[1];
    g_chatLast.moved[0]=moved[0]; g_chatLast.moved[1]=moved[1];
    return g_chatAnchor(self,out,moved,state);
}
ChatOriginals SwapChatOriginalsForTest(const ChatOriginals& with) noexcept {
    const ChatOriginals previous{reinterpret_cast<void*>(g_chatProject),reinterpret_cast<void*>(g_chatAnchor),
                                 reinterpret_cast<void*>(g_chatTextDraw),g_chatTextScale.load()};
    g_chatProject=reinterpret_cast<ChatProject>(with.project);
    g_chatAnchor=reinterpret_cast<ChatAnchor>(with.anchor);
    g_chatTextDraw=reinterpret_cast<ChatTextDraw>(with.textDraw);
    g_chatTextScale.store(with.scale);
    t_chatHead=ChatHead{};
    return previous;
}
ChatAnchorNote ReadChatAnchorNote() noexcept {
    ChatAnchorNote note=g_chatLast;
    note.aboutHead=g_chatAboutHead.load(); note.aboutTail=g_chatAboutTail.load();
    return note;
}
namespace {
bool g_textHooked=false;
bool WriteFloat(const ImageProfile& image,FloatSite& site,float value,char* reason,std::size_t capacity) noexcept {
    auto* start=image.base+site.rva;
    auto* target=start+site.prefixSize;
    if(!Readable(start,site.prefixSize+sizeof(float))) { std::snprintf(reason,capacity,"%s unreadable",site.purpose); return false; }
    if(!site.verified) {
        float current=0; std::memcpy(&current,target,sizeof(current));
        if(std::memcmp(start,site.prefix,site.prefixSize) || current!=site.shipped) {
            std::snprintf(reason,capacity,"%s signature differs; left alone",site.purpose); return false;
        }
        site.verified=true;
    }
    float current=0; std::memcpy(&current,target,sizeof(current));
    if(current==value) return true;
    DWORD old=0;
    if(!VirtualProtect(target,sizeof(float),PAGE_EXECUTE_READWRITE,&old)) { std::snprintf(reason,capacity,"%s protect failed",site.purpose); return false; }
    std::memcpy(target,&value,sizeof(value));
    RecordPatch(target,sizeof(float),site.purpose);
    FlushInstructionCache(GetCurrentProcess(),target,sizeof(float));
    DWORD ignored=0; VirtualProtect(target,sizeof(float),old,&ignored);
    return true;
}
bool HookTextSize(const ImageProfile& image,char* reason,std::size_t capacity) noexcept {
    if(g_textHooked) return true;
    // Every site still calls the shipped setter, or none is touched.
    for(const auto rva:kPlateTextSizeCallRvas) {
        auto* site=image.base+rva;
        if(!Readable(site,5) || site[0]!=0xE8) { std::snprintf(reason,capacity,"text size call %X differs",rva); return false; }
        std::int32_t relative=0; std::memcpy(&relative,site+1,sizeof(relative));
        if(site+5+relative!=image.base+kPlateTextSizeSetterRva) { std::snprintf(reason,capacity,"text size call %X target differs",rva); return false; }
    }
    for(const auto rva:kPlateTextSizeCallRvas) {
        bool changed=false;
        if(!RedirectCall(image.base+rva,image.base+kPlateTextSizeSetterRva,reinterpret_cast<void*>(&NameplateTextSizeHook),changed)) {
            std::snprintf(reason,capacity,"text size call %X would not redirect",rva); return false;
        }
    }
    g_textHooked=true;
    return true;
}
}
void NameplateTextSizeHook(void* style,float x,float y) noexcept {
    const float scale=g_textScale.load(std::memory_order_relaxed);
    auto* floats=static_cast<float*>(style);
    floats[1]=x*scale; floats[2]=y*scale;
}
bool ApplyNameplateScale(const ImageProfile& image,float plateScale,float textScale,char* reason,std::size_t capacity) noexcept {
    if(!reason || !capacity) return false;
    reason[0]=0;
    if(!image.base) { std::snprintf(reason,capacity,"no image"); return false; }
    if(!(plateScale>=0.05f && plateScale<=4.0f) || !(textScale>=0.05f && textScale<=4.0f)) {
        std::snprintf(reason,capacity,"scale out of range"); return false;
    }
    bool all=true;
    for(auto& site:g_sites) {
        char why[96]{};
        if(!WriteFloat(image,site,plateScale,why,sizeof(why))) { all=false; if(!reason[0]) std::snprintf(reason,capacity,"%s",why); }
    }
    // The setter is left alone at 1.0 until something else is asked for, so
    // a player who keeps the game's size has no redirected call at all.
    if(textScale!=1.0f || g_textHooked) {
        char why[96]{};
        if(HookTextSize(image,why,sizeof(why))) g_textScale.store(textScale);
        else { all=false; if(!reason[0]) std::snprintf(reason,capacity,"%s",why); }
    }
    if(all) std::snprintf(reason,capacity,"plate %.3f text %.3f%s",plateScale,textScale,g_textHooked?" (text hooked)":"");
    return all;
}
void ScaleAbout(const float point[2],float scale,float corner[2]) noexcept {
    corner[0]=point[0]+scale*(corner[0]-point[0]);
    corner[1]=point[1]+scale*(corner[1]-point[1]);
}
std::uintptr_t ChatTextRunsThroughMatrix(std::uintptr_t flag) noexcept {
    return (flag&~static_cast<std::uintptr_t>(0xFF))|1u;   // only the low byte is the argument
}
void ScaleChatTextMatrix(const float in[16],float scale,float out[16]) noexcept {
    for(int i=0;i<16;++i) out[i]=in[i];
    for(int i=0;i<4;++i) { out[i]*=scale; out[4+i]*=scale; }
}
bool ApplyChatBubbleScale(const ImageProfile& image,float scale,char* reason,std::size_t capacity) noexcept {
    if(!reason || !capacity) return false;
    reason[0]=0;
    if(!image.base) { std::snprintf(reason,capacity,"no image"); return false; }
    if(!(scale>=0.05f && scale<=4.0f)) { std::snprintf(reason,capacity,"scale out of range"); return false; }
    // The text first: a frame scaled without its text is what this replaces.
    if(scale!=1.0f || g_chatTextHooked) {
        if(!g_chatTextHooked) {
            auto* site=image.base+kChatTextDrawCallRva;
            auto* anchor=image.base+kChatAnchorCallRva;
            auto* head=image.base+kChatHeadCallRva;
            auto target=[&](unsigned char* call) { std::int32_t relative=0; std::memcpy(&relative,call+1,sizeof(relative)); return call+5+relative; };
            // The frame offsets the hooks read through: lea r8,[rbp+0x10] for the
            // corner, lea r9,[rbp+0x80] for the text matrix, lea rax,[rbp-0x40]
            // for the speaker the tail points at, lea rcx,[rbp+0xC0] for the head.
            const unsigned char pointLea[]={0x4C,0x8D,0x45,0x10}, matrixLea[]={0x4C,0x8D,0x8D,0x80,0x00,0x00,0x00}, tailLea[]={0x48,0x8D,0x45,0xC0},
                                headLea[]={0x48,0x8D,0x8D,0xC0,0x00,0x00,0x00};
            if(!Readable(site,5) || site[0]!=0xE8 || !Readable(anchor,5) || anchor[0]!=0xE8 || !Readable(head,5) || head[0]!=0xE8) { std::snprintf(reason,capacity,"chat bubble calls differ"); return false; }
            if(target(site)!=image.base+kChatTextDrawRva || target(anchor)!=image.base+kChatAnchorRva || target(head)!=image.base+kChatProjectRva) {
                std::snprintf(reason,capacity,"chat bubble call targets differ"); return false;
            }
            if(!Readable(image.base+kChatAnchorPointLeaRva,4) || std::memcmp(image.base+kChatAnchorPointLeaRva,pointLea,sizeof(pointLea))
               || !Readable(image.base+kChatTextMatrixLeaRva,7) || std::memcmp(image.base+kChatTextMatrixLeaRva,matrixLea,sizeof(matrixLea))
               || !Readable(image.base+kChatTailTargetLeaRva,4) || std::memcmp(image.base+kChatTailTargetLeaRva,tailLea,sizeof(tailLea))
               || !Readable(image.base+kChatHeadLeaRva,7) || std::memcmp(image.base+kChatHeadLeaRva,headLea,sizeof(headLea))) {
                std::snprintf(reason,capacity,"chat bubble frame offsets differ"); return false;
            }
            g_chatTextDraw=reinterpret_cast<ChatTextDraw>(image.base+kChatTextDrawRva);
            g_chatAnchor=reinterpret_cast<ChatAnchor>(image.base+kChatAnchorRva);
            g_chatProject=reinterpret_cast<ChatProject>(image.base+kChatProjectRva);
            bool changed=false;
            // The head first: until the corner and text hooks are in, it only
            // remembers a point.
            if(!RedirectCall(head,image.base+kChatProjectRva,reinterpret_cast<void*>(&ChatProjectHook),changed)) {
                std::snprintf(reason,capacity,"chat bubble head call would not redirect"); return false;
            }
            if(!RedirectCall(site,image.base+kChatTextDrawRva,reinterpret_cast<void*>(&ChatTextDrawHook),changed)) {
                std::snprintf(reason,capacity,"chat text draw call would not redirect"); return false;
            }
            if(!RedirectCall(anchor,image.base+kChatAnchorRva,reinterpret_cast<void*>(&ChatAnchorHook),changed)) {
                std::snprintf(reason,capacity,"chat bubble corner call would not redirect"); return false;
            }
            g_chatTextHooked=true;
        }
        g_chatTextScale.store(scale);
    }
    bool all=true;
    for(auto& site:g_chatSites) {
        char why[96]{};
        if(!WriteFloat(image,site,scale,why,sizeof(why))) { all=false; if(!reason[0]) std::snprintf(reason,capacity,"%s",why); }
    }
    if(all) std::snprintf(reason,capacity,"bubble and text %.3f%s",scale,g_chatTextHooked?" (text hooked)":"");
    return all;
}
}
