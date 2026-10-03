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
//
// The quick chat's speech bubbles over the players (HudPlayer_Chat) join them
// (2026-10-02, the user: "固定チャットの吹き出しもプレイヤーの位置にちゃんと
// 表示されるように"). Its draw (17F6A58+0x18 -> 802F10) is built the same way as
// the status one: the same set-up (7FFB60, 7FFEF0), then one call per bubble
// (801420, which projects the speaker and draws the bubble and its text there),
// and nothing queued for later -- the 119D1B0 call after the loop hands the
// bubbles' screen rectangles to listeners (message 0x17) and draws nothing.
// The text's height fix (InstallChatTextScaleFix, a patch inside 801420 at
// 80200A) is untouched: only the outer draw is wrapped, and bubble and text
// still land on one target at one size.
struct NameplateClass { const char* name; unsigned vtable; unsigned draw; };
constexpr NameplateClass kNameplateClasses[]={
    {".?AVHudPlayer_MultiPlayStatus@@",0x17F6C98,0x8077B0},
    {".?AVHudPlayer_FollowerDurability@@",0x17F6C08,0x8040E0},
    {".?AVHudPlayer_RescueMessage@@",0x17F6E08,0x808410},
    {".?AVHudPlayer_Chat@@",0x17F6A58,0x802F10},
};
constexpr unsigned kNameplateClassCount=sizeof(kNameplateClasses)/sizeof(kNameplateClasses[0]);
using NameplateDraw=void*(__fastcall*)(void*,void*,void*,void*);
NameplateDraw g_nameplateOriginal[kNameplateClassCount]{};
std::atomic<unsigned long long> g_nameplateCalls[kNameplateClassCount]{};
// Diagnostics: who calls each draw (RVA of the return address, first seen) and
// whether the render-state object it is handed (+8) drives the context the
// capture watches. A different context would mean the draws are recorded
// somewhere the capture cannot redirect.
std::atomic<unsigned> g_nameplateCaller[kNameplateClassCount]{};
std::atomic<unsigned long long> g_nameplateSameContext{0},g_nameplateOtherContext{0};
// Scopes of each class that ended with the world target no longer bound.
std::atomic<unsigned long long> g_nameplateLost[kNameplateClassCount]{};
bool g_worldNameplateDebug=false;
float g_worldNameplateScaleX=1.0f,g_worldNameplateScaleY=1.0f,g_worldNameplateOffsetY=0.0f;
float g_nameplateSize=0.25f,g_nameplateTextSize=1.0f;
char g_nameplateSizeNote[128]="not applied";
// The chat bubbles' size in VR ([Render] WorldChatBubbleSize), a little larger
// than the nameplates ("ネームプレートよりは気持ち大きくて良い").
float g_chatBubbleSize=0.3f;
char g_chatBubbleSizeNote[128]="not applied";
bool g_worldNameplates=true;
// Which go to the world: bit 0 player status, 1 squad health, 2 rescue prompt
// ([Render] WorldNameplateClasses), 3 chat bubbles (WorldChatBubbles). A class
// that turns out to also draw a screen-fixed list can be sent back to the panel
// without losing the others.
unsigned g_worldNameplateMask=15;

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
void* __fastcall NameplateDraw3(void* s,void* a,void* b,void* c) { return NameplateDrawScoped(3,_ReturnAddress(),s,a,b,c); }

// Each class is checked and hooked on its own, so one whose table does not
// match leaves the others working; `installed` gets a bit per class hooked.
// The radio subtitles (the user, 2026-10-03: the Nix's subtitle screen showed
// the weapon gauges' ends -- "字幕の分離って、兵士のモデルでこないだやったやつの
// 応用ではダメなの？"). Each line is a UiDebugMessage: the talk code (5DD580)
// takes the line's text, skips "***no_subtitle***", wraps it in 「 」 and news a
// UiDebugMessage (0x100 bytes, ctor 7D7C20) with it. Its draw is the class's own
// override of xgs::ui::Object's draw slot (vtable 17F4298+0x10 -> 7D8940, the
// base's is pure): it scales its layout from the screen size to 1920x1080 and
// draws the rounded box and the text. Wrapped in a subtitle scope while a
// cockpit with a subtitle screen is in use (MarkSubtitleUi), its draws go to a
// picture of their own -- the same means as the chat bubbles' world scope.
constexpr unsigned kSubtitleVtable=0x17F4298,kSubtitleDraw=0x7D8940;
using SubtitleDraw=void*(__fastcall*)(void*,void*,void*,void*);
SubtitleDraw g_subtitleOriginal=nullptr;
std::atomic<unsigned long long> g_subtitleCalls{0},g_subtitleScoped{0};
void* __fastcall SubtitleDrawHook(void* self,void* a,void* b,void* c) {
    g_subtitleCalls.fetch_add(1,std::memory_order_relaxed);
    if(!g_vrEnabled || !edf6vr::SubtitleUiActive()) return g_subtitleOriginal(self,a,b,c);
    g_subtitleScoped.fetch_add(1,std::memory_order_relaxed);
    edf6vr::UiCaptureSubtitleScope(true);
    void* result=nullptr;
    __try { result=g_subtitleOriginal(self,a,b,c); }
    __finally { edf6vr::UiCaptureSubtitleScope(false); }
    return result;
}
bool InstallSubtitleHook(bool& changed) noexcept {
    changed=false;
    if(!g_image.base) return false;
    bool match=false;
    __try {
        void* table=g_image.base+kSubtitleVtable;
        match=edf6vr::HasType(g_image,&table,".?AVUiDebugMessage@@")
            && *reinterpret_cast<void**>(g_image.base+kSubtitleVtable+0x10)==g_image.base+kSubtitleDraw;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    if(!match) return false;
    g_subtitleOriginal=reinterpret_cast<SubtitleDraw>(g_image.base+kSubtitleDraw);
    return edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+kSubtitleVtable+0x10),g_image.base+kSubtitleDraw,
                                  reinterpret_cast<void*>(&SubtitleDrawHook),changed);
}
bool InstallNameplateHooks(bool& changed,unsigned* installed=nullptr) noexcept {
    changed=false;
    if(installed) *installed=0;
    if(!g_image.base) return false;
    constexpr NameplateDraw hooks[kNameplateClassCount]={&NameplateDraw0,&NameplateDraw1,&NameplateDraw2,&NameplateDraw3};
    bool matches[kNameplateClassCount]{};
    __try {
        for(unsigned i=0;i<kNameplateClassCount;++i) {
            const auto& c=kNameplateClasses[i];
            void* table=g_image.base+c.vtable;
            matches[i]=edf6vr::HasType(g_image,&table,c.name)
                && *reinterpret_cast<void**>(g_image.base+c.vtable+0x18)==g_image.base+c.draw;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    bool all=true;
    for(unsigned i=0;i<kNameplateClassCount;++i) {
        if(!matches[i]) { all=false; continue; }
        const auto& c=kNameplateClasses[i];
        g_nameplateOriginal[i]=reinterpret_cast<NameplateDraw>(g_image.base+c.draw);
        bool one=false;
        const bool ok=edf6vr::ReplacePointer(reinterpret_cast<void**>(g_image.base+c.vtable+0x18),g_image.base+c.draw,
                                             reinterpret_cast<void*>(hooks[i]),one);
        all=ok && all;
        if(ok && installed) *installed|=1u<<i;
        changed=changed||one;
    }
    return all;
}
