#pragma once
// The rows of the in-game "VR setting" menu (vr_menu.h): the setting each one
// changes, the values a press steps through, and what the row shows for each,
// in English and in Japanese. No game and no files in here.
//
// Only settings the mod reads again while the game runs are listed (the user,
// 2026-10-12: "exe内でゲーム中でも変更できる項目は、スタートメニューにVRsettingの項目を
// 追加してゲーム中に切り替えられるようにできる？"); the rest stay in EDF6 VR setting.exe:
// VR mode, picture size, HD textures, crew figures, gun hand, VR cockpit,
// lighter Kurul shots. The order is the order of ROWS in tools/make_vr_menu.py,
// which writes the rows' names into the layouts.
#include <cmath>
#include <cwchar>

namespace edf6vr {

enum class VrMenuKind {
    OnOff,    // the mod reads it as "not 0"
    Choice,   // whole numbers, each with a name
    Number,   // steps of a number; a value between two steps is shown as it is
};
struct VrMenuState {
    const wchar_t* value;    // written under the row's key
    const wchar_t* value2;   // written under its second key; null leaves that key alone and matches any
    const wchar_t* en;       // shown for it; null shows the value
    const wchar_t* ja;
};
struct VrMenuItem {
    VrMenuKind kind;
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* fallback;     // what the mod takes when the key is missing
    const wchar_t* key2;         // the compact HUD has two keys
    const wchar_t* fallback2;
    int cycle;                   // a press goes through the first so many states
    int count;                   // states that can be shown (a hand-set one past cycle)
    VrMenuState states[8];
};

#define EDF6VR_MENU_ONOFF(section,key,fallback) \
    {VrMenuKind::OnOff,section,key,fallback,nullptr,nullptr,2,2,{{L"1",nullptr,L"On",L"オン"},{L"0",nullptr,L"Off",L"オフ"}}}
#define EDF6VR_MENU_LINE(key,fallback) \
    {VrMenuKind::Number,L"Render",key,fallback,nullptr,nullptr,8,8,{{L"0.1"},{L"0.2"},{L"0.3"},{L"0.5"},{L"0.75"}, \
        {L"1",nullptr,L"1 (the game's own)",L"1（ゲーム本来の太さ）"},{L"1.5"},{L"2"}}}

constexpr VrMenuItem kVrMenuItems[]={
    {VrMenuKind::Choice,L"VR",L"ScopeMode",L"1",nullptr,nullptr,3,3,{
        {L"1",nullptr,L"Native (sharp)",L"ネイティブ（高画質）"},
        {L"2",nullptr,L"Digital (no FPS cost)",L"デジタル（FPSに影響なし）"},
        {L"0",nullptr,L"Off (the whole view zooms)",L"オフ（視界全体がズーム）"}}},
    // 2, the level horizon, is set by hand in the INI: shown, and a press leaves it for 2D.
    {VrMenuKind::Choice,L"VR",L"AirStrikeCamera",L"0",nullptr,nullptr,2,3,{
        {L"0",nullptr,L"2D (light)",L"2D表示（軽い）"},
        {L"1",nullptr,L"VR (lower FPS)",L"VR表示（FPSが低下）"},
        {L"2",nullptr,L"VR, level horizon (lower FPS)",L"VR表示・水平固定（FPSが低下）"}}},
    // On trial (umbra_properties.h): on unless switched off, here, in the settings program or in the INI.
    {VrMenuKind::OnOff,L"Render",L"OcclusionLeafOnly",L"1",nullptr,nullptr,2,2,{
        {L"1",nullptr,L"On (lighter)",L"オン（軽い）"},{L"0",nullptr,L"Off (the game's own)",L"オフ（ゲーム本来）"}}},
    EDF6VR_MENU_ONOFF(L"VR",L"RightStickButtons",L"1"),
    EDF6VR_MENU_ONOFF(L"VR",L"RecoilKick",L"1"),
    {VrMenuKind::Number,L"VR",L"ShotBuzz",L"1",nullptr,nullptr,5,5,{
        {L"0",nullptr,L"0 (off)",L"0（オフ）"},{L"0.25"},{L"0.5"},{L"0.75"},
        {L"1",nullptr,L"1 (normal)",L"1（標準）"}}},
    {VrMenuKind::Number,L"Render",L"ReticleScale",L"0.5",nullptr,nullptr,6,6,{
        {L"0.25"},{L"0.5",nullptr,L"0.5 (normal)",L"0.5（標準）"},{L"0.75"},{L"1"},{L"1.5"},{L"2"}}},
    {VrMenuKind::Choice,L"Render",L"UiCluster",L"1",L"UiClusterPlace",L"0",4,4,{
        {L"1",L"0",L"On: corner of your view",L"オン：視界の隅"},
        {L"1",L"1",L"On: right wrist",L"オン：右手首"},
        {L"1",L"2",L"On: left wrist",L"オン：左手首"},
        {L"0",nullptr,L"Off (the game's full-size HUD)",L"オフ（ゲーム本来のHUD）"}}},
    EDF6VR_MENU_ONOFF(L"Render",L"DesktopMirror",L"1"),
    EDF6VR_MENU_LINE(L"LaserSightWidth",L"0.1"),
    EDF6VR_MENU_LINE(L"ThrowGuideWidth",L"0.2"),
    EDF6VR_MENU_LINE(L"VehicleAimLineWidth",L"0.3"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimNix",L"1"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimDepth",L"0"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimBarga",L"0"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimTank",L"0"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimCombat",L"0"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimHeli",L"0"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimGunner",L"0"),
    EDF6VR_MENU_ONOFF(L"VR",L"VehicleHandAimBruteGunner",L"0"),
};
#undef EDF6VR_MENU_ONOFF
#undef EDF6VR_MENU_LINE
constexpr int kVrMenuItemCount=static_cast<int>(sizeof(kVrMenuItems)/sizeof(kVrMenuItems[0]));

inline bool VrMenuNumber(const wchar_t* text,double& out) noexcept {
    if(!text) return false;
    wchar_t* end=nullptr;
    out=std::wcstod(text,&end);
    return end!=text && std::isfinite(out);
}
// The state the INI's values are in, or -1 when they are none of the row's
// (typed by hand, or a number between two steps).
inline int VrMenuStateOf(const VrMenuItem& item,const wchar_t* value,const wchar_t* value2) noexcept {
    if(item.kind==VrMenuKind::Number) {
        double now=0;
        if(!VrMenuNumber(value,now)) return -1;
        for(int i=0;i<item.count;++i) {
            double step=0;
            if(VrMenuNumber(item.states[i].value,step) && std::fabs(now-step)<0.0005) return i;
        }
        return -1;
    }
    const long first=value?std::wcstol(value,nullptr,10):0,second=value2?std::wcstol(value2,nullptr,10):0;
    if(item.kind==VrMenuKind::OnOff) return first!=0?0:1;
    for(int i=0;i<item.count;++i) {
        const auto& state=item.states[i];
        if(std::wcstol(state.value,nullptr,10)!=first) continue;
        if(state.value2 && std::wcstol(state.value2,nullptr,10)!=second) continue;
        return i;
    }
    return -1;
}
// The state a press goes to: the next one round the cycle; from a number
// between two steps, the step above it; from anything else unknown, the first.
inline int VrMenuNext(const VrMenuItem& item,int state,const wchar_t* value) noexcept {
    if(state>=0) return state+1<item.cycle?state+1:0;
    double now=0;
    if(item.kind==VrMenuKind::Number && VrMenuNumber(value,now))
        for(int i=0;i<item.cycle;++i) {
            double step=0;
            if(VrMenuNumber(item.states[i].value,step) && step>now) return i;
        }
    return 0;
}
// What the row shows: the state's name, or the value as it stands in the INI.
inline const wchar_t* VrMenuLabel(const VrMenuItem& item,int state,bool japanese,const wchar_t* raw) noexcept {
    if(state<0 || state>=item.count) return raw && raw[0]?raw:L"-";
    const auto& shown=item.states[state];
    const wchar_t* text=japanese?shown.ja:shown.en;
    return text?text:shown.value;
}

}  // namespace edf6vr
