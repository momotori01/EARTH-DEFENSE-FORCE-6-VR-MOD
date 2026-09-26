#include "resolution_lock.h"
#include "nameplate_scale.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
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
    }
    FreeLibrary(module);
    std::printf("Native render-size lock: %d failures\n",failures);
    return failures?1:0;
}
