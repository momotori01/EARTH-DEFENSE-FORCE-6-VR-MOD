#pragma once
// The pause menu and the mission-failed screens on the board, online too.
//
// The board is chosen when the player's scene goes stale (MarkSceneLive): the
// offline pause stops the game, so the body stops being drawn and the view
// drops to the board with the menu whole. Online nothing stops, so the menu
// stayed in the world, caught by the HUD capture and cut by the compact HUD's
// rectangles and the reticle square (the user, 2026-10-04: "オンラインミッション中に
// スタートボタンを押したり全滅したりした時、再出撃とかの選択肢がUI切り抜きで虫食いみたいに
// なってる。オンラインだと板にならないから"). The menus themselves are watched instead:
// every ui object's vtable has OnUpdate in slot 1 (+0x08; HUiMissionFailedClient's
// 8DDE20 is the function that builds its OnUpdate lambda 18094F0) and the deleting
// destructor in slot 5 (+0x28, as chat_probe.h hooks). While one of these menus
// is being updated the display is the board and the HUD is not captured
// (OpenXrRuntime::MarkMenuOpen); a menu that is gone is no longer updated, so
// the board ends by itself 0.3 s later and no count can be left behind.
//   HUiPause / HUiPauseBG                 the Start menu and its backdrop
//   HUiMissionFailed                      the MISSION FAILED banner
//   HUiMissionFailedDialog / Client       the retry choices (host, other players)
//   HUiMissionFailedResult                the failed result
//   HUiDialogBox                          a confirmation over any of them
using MenuUpdate=void(*)(void*,void*,void*,void*);
struct MenuClass { std::uintptr_t table,update; const char* name; };
constexpr MenuClass kMenuClasses[]={
    {0x180F4F8,0x935210,".?AVHUiPause@ui@@"},
    {0x180F808,0x936230,".?AVHUiPauseBG@ui@@"},
    {0x18090F8,0x84B490,".?AVHUiMissionFailed@ui@@"},
    {0x18091A8,0x8DE260,".?AVHUiMissionFailedDialog@ui@@"},
    {0x1809260,0x8DDE20,".?AVHUiMissionFailedClient@ui@@"},
    {0x1809608,0x8DECC0,".?AVHUiMissionFailedResult@ui@@"},
    {0x17F7260,0x810D10,".?AVHUiDialogBox@ui@@"},
};
constexpr unsigned kMenuClassCount=sizeof(kMenuClasses)/sizeof(kMenuClasses[0]);
MenuUpdate g_menuUpdateOriginal[kMenuClassCount]{};
std::atomic<unsigned long long> g_menuUpdates[kMenuClassCount]{};
template<unsigned I> void HookMenuUpdate(void* self,void* context,void* a,void* b) noexcept {
    edf6vr::g_openxr.MarkMenuOpen();
    g_menuUpdates[I].fetch_add(1,std::memory_order_relaxed);
    g_menuUpdateOriginal[I](self,context,a,b);
}
constexpr MenuUpdate kMenuHooks[kMenuClassCount]={&HookMenuUpdate<0>,&HookMenuUpdate<1>,&HookMenuUpdate<2>,&HookMenuUpdate<3>,
    &HookMenuUpdate<4>,&HookMenuUpdate<5>,&HookMenuUpdate<6>};
bool InstallMenuBoard(bool& changed) noexcept {
    changed=false;
    __try {
        auto* base=g_image.base;
        if(!base) return false;
        for(const auto& c:kMenuClasses) {
            void* table=base+c.table;
            if(!edf6vr::HasType(g_image,&table,c.name) || *reinterpret_cast<void**>(base+c.table+8)!=base+c.update) return false;
        }
        for(unsigned i=0;i<kMenuClassCount;++i) {
            g_menuUpdateOriginal[i]=reinterpret_cast<MenuUpdate>(base+kMenuClasses[i].update);
            bool one=false;
            if(!edf6vr::ReplacePointer(reinterpret_cast<void**>(base+kMenuClasses[i].table+8),reinterpret_cast<void*>(g_menuUpdateOriginal[i]),
                reinterpret_cast<void*>(kMenuHooks[i]),one)) return false;
            changed|=one;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// For the log: updates seen per menu, "pause/bg/failed/dialog/client/result/box".
void MenuBoardCounts(unsigned long long out[kMenuClassCount]) noexcept {
    for(unsigned i=0;i<kMenuClassCount;++i) out[i]=g_menuUpdates[i].load(std::memory_order_relaxed);
}
