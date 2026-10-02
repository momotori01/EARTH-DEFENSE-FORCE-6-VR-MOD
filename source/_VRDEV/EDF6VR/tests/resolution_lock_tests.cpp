#include "resolution_lock.h"
#include "nameplate_scale.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <cmath>
// The bubble draw (0x801420) acted out over a stand-in for its stack frame, in
// the order the game calls the three hooked sites: the head projected into
// [rbp+0xC0] (801977), the corner [rbp+0x10] handed to the frame's transform
// (801CFA), the text matrix [rbp+0x80] handed to the text draw (802059); the
// tail's target sits at [rbp-0x40]. Stand-in originals record what the game's
// functions would have been given.
namespace chatrun {
float head[2], seenCorner[2], seenText[2], seenRow;
std::uintptr_t seenFlag;
void* Project(void* out,const void*,void*,void*) { std::memcpy(out,head,sizeof(head)); return out; }
void* Anchor(void*,void* out,const float* corner,void*) { seenCorner[0]=corner[0]; seenCorner[1]=corner[1]; return out; }
void* Text(void* layout,void*,void*,const float* matrix,std::uintptr_t flag) { seenText[0]=matrix[12]; seenText[1]=matrix[13]; seenRow=matrix[0]; seenFlag=flag; return layout; }
}
template<class Check> void ChatFrameRun(Check& check) {
    using namespace chatrun;
    const auto previous=edf6vr::SwapChatOriginalsForTest({reinterpret_cast<void*>(&Project),reinterpret_cast<void*>(&Anchor),reinterpret_cast<void*>(&Text),0.3f});
    alignas(16) unsigned char stack[0x400]{};
    unsigned char* rbp=stack+0x200;
    auto at=[&](int offset) { return reinterpret_cast<float*>(rbp+offset); };
    auto same=[](float a,float b) { return std::fabs(a-b)<1e-3f; };
    // The local player's own bubble (kind 0, tail 140 px below the head): head at
    // (700,300), corner placed by the game 200 right and 91 below, text 18
    // further in; the tail's target 140 below the head.
    head[0]=700; head[1]=300;
    at(-0x40)[0]=700; at(-0x40)[1]=440;
    at(0x10)[0]=900; at(0x10)[1]=391; at(0x10)[2]=-600; at(0x10)[3]=1;
    float* matrix=at(0x80);
    const float rows[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 918,409,0,1};
    std::memcpy(matrix,rows,sizeof(rows));
    edf6vr::ChatProjectHook(rbp+0xC0,nullptr,nullptr,nullptr);
    edf6vr::ChatAnchorHook(nullptr,nullptr,at(0x10),nullptr);
    edf6vr::ChatTextDrawHook(nullptr,nullptr,nullptr,matrix,0);
    // Both shrunk about the head: 0.3 of the way out from it.
    check(same(seenCorner[0],760) && same(seenCorner[1],327.3f));
    check(same(seenText[0],765.4f) && same(seenText[1],332.7f) && same(seenRow,0.3f) && seenFlag==1);
    // The game's own corner and matrix are left as they were.
    check(at(0x10)[0]==900 && at(0x10)[1]==391 && matrix[12]==918 && matrix[0]==1);
    // The tail, drawn at its full-size offset under the 0.3 transform, points
    // the same way from the head: 0.3 of 140 down.
    const float tipX=seenCorner[0]+0.3f*(at(-0x40)[0]-at(0x10)[0]), tipY=seenCorner[1]+0.3f*(at(-0x40)[1]-at(0x10)[1]);
    check(same(tipX,700) && same(tipY,342));
    auto note=edf6vr::ReadChatAnchorNote();
    check(note.aboutHead==1 && note.aboutTail==0 && same(note.centre[1],300) && same(note.moved[0],760));
    // The next bubble projects no head (the 801853 path): it keeps the tail's
    // target, not the head before it.
    at(-0x40)[0]=100; at(-0x40)[1]=900;
    at(0x10)[0]=300; at(0x10)[1]=900;
    edf6vr::ChatAnchorHook(nullptr,nullptr,at(0x10),nullptr);
    check(same(seenCorner[0],160) && same(seenCorner[1],900));
    note=edf6vr::ReadChatAnchorNote();
    check(note.aboutHead==1 && note.aboutTail==1);
    // A head caught in another frame (another draw) is not this bubble's.
    edf6vr::ChatProjectHook(rbp+0x100,nullptr,nullptr,nullptr);
    edf6vr::ChatAnchorHook(nullptr,nullptr,at(0x10),nullptr);
    check(same(seenCorner[0],160));
    edf6vr::SwapChatOriginalsForTest(previous);
}

int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    const auto module=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    edf6vr::ImageProfile image{}; char reason[256]{};
    if(!module || !edf6vr::CheckImage(module,image,reason,sizeof(reason))) return 2;
    int failures=0;
    auto check=[&](bool ok) { if(!ok) { ++failures; std::printf("FAIL: %s\n",reason); } };
    unsigned char original[17]{};
    auto getter=image.base+edf6vr::kResolutionGetterRva;
    std::memcpy(original,getter,sizeof(original));
    check(edf6vr::InstallResolutionLock(image,0,0,reason,sizeof(reason)));
    check(!std::memcmp(original,getter,sizeof(original)));
    check(!edf6vr::InstallResolutionLock(image,3840,0,reason,sizeof(reason)));
    check(!std::memcmp(original,getter,sizeof(original)));
    check(edf6vr::InstallResolutionLock(image,3840,2160,reason,sizeof(reason)));
    // Only execute the replacement leaf instructions. No original game code,
    // imports, entry point, renderer or configuration loader is executed.
    using Getter=unsigned (__fastcall*)(unsigned*,unsigned*);
    for(unsigned rva:{0x1183310u,0x1183260u,0x11832A0u}) {
        for(unsigned initial:{0u,1920u,1280u,3200u}) {
            // Real globals used by the old buffer and letterbox getters. Model
            // both letterbox modes and a subsequent window resolution change.
            auto* globals=reinterpret_cast<unsigned*>(image.base+0x21360C4);
            globals[0]=initial; globals[1]=initial/2;
            globals[4]=initial; globals[5]=initial/2;
            for(int flag:{0,1}) {
                *(image.base+0x1FEEE10)=static_cast<unsigned char>(flag);
                unsigned width=0,height=0;
                const auto fn=reinterpret_cast<Getter>(image.base+rva);
                check(fn(&width,&height)==2160 && width==3840 && height==2160);
            }
        }
    }
    using Offset=unsigned (__fastcall*)();
    check(reinterpret_cast<Offset>(image.base+0x1183250)()==0);
    check(reinterpret_cast<Offset>(image.base+0x1183290)()==0);
    check(edf6vr::InstallResolutionLock(image,3840,2160,reason,sizeof(reason)));
    check(!edf6vr::InstallResolutionLock(image,1920,1080,reason,sizeof(reason))); // refuse conflict
    // The chat text's own y scaling: present in the shipped build, replaced by a
    // plain load of the bubble's y, idempotent, and the instruction after it
    // (the store into the matrix) untouched.
    auto* chat=image.base+edf6vr::kChatTextScaleRva;
    const unsigned char storeAfter[8]={0xF3,0x0F,0x11,0x8D,0xB4,0x00,0x00,0x00};
    check(!std::memcmp(chat+42,storeAfter,sizeof(storeAfter)));
    check(chat[0]==0x8B && chat[36]==0xF3 && chat[41]==0x54);
    check(edf6vr::InstallChatTextScaleFix(image,reason,sizeof(reason)));
    const unsigned char load[6]={0xF3,0x0F,0x10,0x4C,0x24,0x54};
    check(!std::memcmp(chat,load,sizeof(load)));
    for(int i=6;i<42;++i) check(chat[i]==0x90);
    check(!std::memcmp(chat+42,storeAfter,sizeof(storeAfter)));
    check(edf6vr::InstallChatTextScaleFix(image,reason,sizeof(reason)));
    check(!std::strcmp(reason,"chat text scaling already neutral"));
    // Nameplate size: the two identity immediates and the two glyph constants
    // are what the shipped build has, take a scale, take another, and refuse
    // nonsense without touching anything.
    {
        auto readFloat=[&](unsigned rva,std::size_t skip) { float v=0; std::memcpy(&v,image.base+rva+skip,sizeof v); return v; };
        check(readFloat(edf6vr::kPlateScaleXRva,4)==1.0f && readFloat(edf6vr::kPlateScaleYRva,3)==1.0f);
        check(readFloat(edf6vr::kPlateTextScaleXRva,7)==1.0f && readFloat(edf6vr::kPlateTextScaleYRva,6)==1.0f);
        check(readFloat(edf6vr::kPlateLetterScaleXRva,7)==1.0f && readFloat(edf6vr::kPlateLetterScaleYRva,6)==1.0f);
        // Text: the six style-size calls all reach the shipped setter, and a
        // scale of 1.0 leaves them alone.
        auto callTarget=[&](unsigned rva) { std::int32_t rel=0; std::memcpy(&rel,image.base+rva+1,4); return image.base+rva+5+rel; };
        for(unsigned rva:edf6vr::kPlateTextSizeCallRvas) check(image.base[rva]==0xE8 && callTarget(rva)==image.base+edf6vr::kPlateTextSizeSetterRva);
        check(edf6vr::ApplyNameplateScale(image,0.25f,1.0f,reason,sizeof(reason)));
        for(unsigned rva:edf6vr::kPlateTextSizeCallRvas) check(callTarget(rva)==image.base+edf6vr::kPlateTextSizeSetterRva);
        check(edf6vr::ApplyNameplateScale(image,0.25f,0.5f,reason,sizeof(reason)));
        check(readFloat(edf6vr::kPlateScaleXRva,4)==0.25f && readFloat(edf6vr::kPlateScaleYRva,3)==0.25f);
        check(readFloat(edf6vr::kPlateTextScaleXRva,7)==0.25f && readFloat(edf6vr::kPlateTextScaleYRva,6)==0.25f);
        check(readFloat(edf6vr::kPlateLetterScaleXRva,7)==0.25f && readFloat(edf6vr::kPlateLetterScaleYRva,6)==0.25f);
        // The instruction after each rewritten immediate is untouched.
        check(image.base[edf6vr::kPlateTextScaleXRva+11]==0xC7 && image.base[edf6vr::kPlateLetterScaleXRva+11]==0xC7);
        // Every site now goes elsewhere, and what it reaches stores the scaled size.
        using Setter=void(*)(void*,float,float);
        for(unsigned rva:edf6vr::kPlateTextSizeCallRvas) {
            check(callTarget(rva)!=image.base+edf6vr::kPlateTextSizeSetterRva);
            float style[4]={9,9,9,9};
            reinterpret_cast<Setter>(callTarget(rva))(style,0.5f,0.4f);
            check(style[0]==9 && style[1]==0.25f && style[2]==0.2f && style[3]==9);
        }
        const unsigned char scaleXPrefix[4]={0x48,0xC7,0x45,0x00}, scaleYPrefix[3]={0xC7,0x45,0x14};
        check(!std::memcmp(image.base+edf6vr::kPlateScaleXRva,scaleXPrefix,4) && !std::memcmp(image.base+edf6vr::kPlateScaleYRva,scaleYPrefix,3));
        check(edf6vr::ApplyNameplateScale(image,1.0f,1.0f,reason,sizeof(reason)));
        check(readFloat(edf6vr::kPlateScaleXRva,4)==1.0f);
        { float style[4]={9,9,9,9}; reinterpret_cast<Setter>(callTarget(edf6vr::kPlateTextSizeCallRvas[0]))(style,0.5f,0.4f); check(style[1]==0.5f && style[2]==0.4f); }
        check(!edf6vr::ApplyNameplateScale(image,0.0f,1.0f,reason,sizeof(reason)));
        check(readFloat(edf6vr::kPlateScaleXRva,4)==1.0f);
        // The chat bubble's identity: shipped 1.0 behind the expected opcodes,
        // scaled and set back, nonsense refused, the text fix's bytes and the
        // instructions around the immediates untouched.
        const unsigned char chatXPrefix[4]={0x48,0xC7,0x45,0x30}, chatYPrefix[3]={0xC7,0x45,0x44};
        check(!std::memcmp(image.base+edf6vr::kChatScaleXRva,chatXPrefix,4) && !std::memcmp(image.base+edf6vr::kChatScaleYRva,chatYPrefix,3));
        check(readFloat(edf6vr::kChatScaleXRva,4)==1.0f && readFloat(edf6vr::kChatScaleYRva,3)==1.0f);
        auto* textCall=image.base+edf6vr::kChatTextDrawCallRva;
        check(textCall[0]==0xE8 && callTarget(edf6vr::kChatTextDrawCallRva)==image.base+edf6vr::kChatTextDrawRva);
        check(image.base[edf6vr::kChatAnchorCallRva]==0xE8 && callTarget(edf6vr::kChatAnchorCallRva)==image.base+edf6vr::kChatAnchorRva);
        check(image.base[edf6vr::kChatHeadCallRva]==0xE8 && callTarget(edf6vr::kChatHeadCallRva)==image.base+edf6vr::kChatProjectRva);
        check(edf6vr::ApplyChatBubbleScale(image,0.2f,reason,sizeof(reason)));
        check(readFloat(edf6vr::kChatScaleXRva,4)==0.2f && readFloat(edf6vr::kChatScaleYRva,3)==0.2f);
        check(image.base[edf6vr::kChatScaleYRva+7]==0x48 && image.base[edf6vr::kChatScaleXRva+8]==0xC7);
        check(!std::memcmp(chat,load,sizeof(load)));
        check(edf6vr::ApplyChatBubbleScale(image,1.0f,reason,sizeof(reason)));
        check(readFloat(edf6vr::kChatScaleXRva,4)==1.0f && readFloat(edf6vr::kChatScaleYRva,3)==1.0f);
        check(!edf6vr::ApplyChatBubbleScale(image,0.0f,reason,sizeof(reason)));
        check(readFloat(edf6vr::kChatScaleXRva,4)==1.0f);
        // The text: its one call to the text-block draw (shipped target checked
        // above) is redirected once a scale other than 1.0 is asked for, and the
        // matrix's x and y rows (not its translation) are what is scaled.
        check(edf6vr::ApplyChatBubbleScale(image,0.3f,reason,sizeof(reason)));
        check(textCall[0]==0xE8 && callTarget(edf6vr::kChatTextDrawCallRva)!=image.base+edf6vr::kChatTextDrawRva);
        check(readFloat(edf6vr::kChatScaleXRva,4)==0.3f);
        {
            float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 640,360,0,1}, s[16]{};
            edf6vr::ScaleChatTextMatrix(m,0.3f,s);
            check(s[0]==0.3f && s[5]==0.3f && s[10]==1.0f && s[12]==640.0f && s[13]==360.0f && s[15]==1.0f);
            // A line 40 px right of and 30 px under the origin, placed the way
            // the draw does with its byte set (offset through rows 0 and 1, then
            // the translation): 0.3 of the way out, about the same point.
            const float runX=40,runY=30;
            const float placedX=runX*s[0]+runY*s[4]+s[12], placedY=runX*s[1]+runY*s[5]+s[13];
            check(placedX==640.0f+12.0f && placedY==360.0f+9.0f);
            // The unscaled rows give the byte-0 place (offset added as it is).
            check(runX*m[0]+runY*m[4]+m[12]==640.0f+runX && runX*m[1]+runY*m[5]+m[13]==360.0f+runY);
            check(edf6vr::ChatTextRunsThroughMatrix(0)==1 && edf6vr::ChatTextRunsThroughMatrix(0xABCD00)==0xABCD01
                  && edf6vr::ChatTextRunsThroughMatrix(0x11)==1);
            // About the speaker: a corner 100 right and 40 below the speaker
            // comes to 30 and 12 at 0.3; and the tail, drawn at its full-size
            // offset from the corner under the 0.3 transform, ends on the speaker.
            const float speaker[2]={500,300}; float corner[2]={600,340};
            edf6vr::ScaleAbout(speaker,0.3f,corner);
            check(std::fabs(corner[0]-530.0f)<1e-3f && std::fabs(corner[1]-312.0f)<1e-3f);
            const float tipX=corner[0]+0.3f*(500.0f-600.0f), tipY=corner[1]+0.3f*(300.0f-340.0f);
            check(std::fabs(tipX-500.0f)<1e-3f && std::fabs(tipY-300.0f)<1e-3f);
        }
        // The call to the corner's conversion is redirected along with the text,
        // and the head's projection.
        check(image.base[edf6vr::kChatAnchorCallRva]==0xE8 && callTarget(edf6vr::kChatAnchorCallRva)!=image.base+edf6vr::kChatAnchorRva);
        check(image.base[edf6vr::kChatHeadCallRva]==0xE8 && callTarget(edf6vr::kChatHeadCallRva)!=image.base+edf6vr::kChatProjectRva);
        ChatFrameRun(check);
        check(edf6vr::ApplyChatBubbleScale(image,1.0f,reason,sizeof(reason)));
        check(readFloat(edf6vr::kChatScaleXRva,4)==1.0f);
    }
    FreeLibrary(module);
    std::printf("Native render-size lock: %d failures\n",failures);
    return failures?1:0;
}
