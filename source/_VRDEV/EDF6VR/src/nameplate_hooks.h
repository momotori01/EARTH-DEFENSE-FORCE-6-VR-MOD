// Included inside plugin.cpp's private namespace after hand_support.h.
//
// World-anchored HUD. Three HUD classes draw things that belong to a place in
// the world rather than to the screen: other players' nameplates and status
// (HudPlayer_MultiPlayStatus), NPC squad health (HudPlayer_FollowerDurability)
// and the rescue prompt over a downed ally (HudPlayer_RescueMessage). They were
// captured onto the HUD panel like everything else, where their screen position
// no longer matched the world behind it. Their draw (vtable +0x18, found from
// RTTI like HudPlayer_Chat's) is wrapped in a ui_capture world scope, which
// sends those draws to a target composited into the world eye images instead.
struct NameplateClass { const char* name; unsigned vtable; unsigned draw; };
constexpr NameplateClass kNameplateClasses[]={
    {".?AVHudPlayer_MultiPlayStatus@@",0x17F6C98,0x8077B0},
    {".?AVHudPlayer_FollowerDurability@@",0x17F6C08,0x8040E0},
    {".?AVHudPlayer_RescueMessage@@",0x17F6E08,0x808410},
};
using NameplateDraw=void*(__fastcall*)(void*,void*,void*,void*);
NameplateDraw g_nameplateOriginal[3]{};
std::atomic<unsigned long long> g_nameplateCalls[3]{};
// Diagnostics: who calls each draw (RVA of the return address, first seen) and
// whether the render-state object it is handed (+8) drives the context the
// capture watches. A different context would mean the draws are recorded
// somewhere the capture cannot redirect.
std::atomic<unsigned> g_nameplateCaller[3]{};
std::atomic<unsigned long long> g_nameplateSameContext{0},g_nameplateOtherContext{0};
// Scopes of each class that ended with the world target no longer bound.
std::atomic<unsigned long long> g_nameplateLost[3]{};
bool g_worldNameplateDebug=false;
float g_worldNameplateScaleX=1.0f,g_worldNameplateScaleY=1.0f,g_worldNameplateOffsetY=0.0f;
float g_nameplateSize=0.25f,g_nameplateTextSize=1.0f;
char g_nameplateSizeNote[128]="not applied";
bool g_worldNameplates=true;
// Which of the three go to the world: bit 0 player status, 1 squad health,
// 2 rescue prompt. A class that turns out to also draw a screen-fixed list can
// be sent back to the panel without losing the others.
unsigned g_worldNameplateMask=7;

void NoteNameplateCaller(unsigned index,void* caller,void* state) noexcept {
    if(!g_nameplateCaller[index].load(std::memory_order_relaxed) && caller && g_image.base
       && static_cast<unsigned char*>(caller)>g_image.base)
        g_nameplateCaller[index].store(static_cast<unsigned>(static_cast<unsigned char*>(caller)-g_image.base));
    __try {
        const auto context=state?*static_cast<void**>(static_cast<void*>(static_cast<unsigned char*>(state)+8)):nullptr;
        if(context && context==static_cast<void*>(edf6vr::UiCaptureContext())) g_nameplateSameContext.fetch_add(1,std::memory_order_relaxed);
        else g_nameplateOtherContext.fetch_add(1,std::memory_order_relaxed);
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_nameplateOtherContext.fetch_add(1,std::memory_order_relaxed); }
}
void* NameplateDrawScoped(unsigned index,void* caller,void* self,void* a,void* b,void* c) {
    g_nameplateCalls[index].fetch_add(1,std::memory_order_relaxed);
    NoteNameplateCaller(index,caller,a);
    if(!g_worldNameplates || !g_vrEnabled || !(g_worldNameplateMask&(1u<<index))) return g_nameplateOriginal[index](self,a,b,c);
    const auto lostBefore=edf6vr::ReadWorldUiStats().exitsLost;
    edf6vr::UiCaptureWorldScope(true);
    void* result=nullptr;
    __try { result=g_nameplateOriginal[index](self,a,b,c); }
    __finally { edf6vr::UiCaptureWorldScope(false); }
    if(edf6vr::ReadWorldUiStats().exitsLost!=lostBefore) g_nameplateLost[index].fetch_add(1,std::memory_order_relaxed);
    return result;
}
void* __fastcall NameplateDraw0(void* s,void* a,void* b,void* c) { return NameplateDrawScoped(0,_ReturnAddress(),s,a,b,c); }
void* __fastcall NameplateDraw1(void* s,void* a,void* b,void* c) { return NameplateDrawScoped(1,_ReturnAddress(),s,a,b,c); }
void* __fastcall NameplateDraw2(void* s,void* a,void* b,void* c) { return NameplateDrawScoped(2,_ReturnAddress(),s,a,b,c); }

bool InstallNameplateHooks(bool& changed) noexcept {
    changed=false;
    if(!g_image.base) return false;
    constexpr NameplateDraw hooks[3]={&NameplateDraw0,&NameplateDraw1,&NameplateDraw2};
    __try {
        for(const auto& c:kNameplateClasses) {
            void* table=g_image.base+c.vtable;
            if(!edf6vr::HasType(g_image,&table,c.name)) return false;
            if(*reinterpret_cast<void**>(g_image.base+c.vtable+0x18)!=g_image.base+c.draw) return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    bool all=true;
    for(unsigned i=0;i<3;++i) {
        const auto& c=kNameplateClasses[i];
        g_nameplateOriginal[i]=reinterpret_cast<NameplateDraw>(g_image.base+c.draw);
        bool one=false;
        all=edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+c.vtable+0x18),g_image.base+c.draw,
                                   reinterpret_cast<void*>(hooks[i]),one) && all;
        changed=changed||one;
    }
    return all;
}
