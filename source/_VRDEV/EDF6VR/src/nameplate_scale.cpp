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
std::atomic<float> g_textScale{1.0f};
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
}
