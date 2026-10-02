#pragma once
// The quick chat windows (固定チャット): while one is open the compact HUD steps
// aside, and the panel shows the game's HUD as it is.
//
// The game draws the window on the left, down to about y 690 of 1080, over
// the Fencer's left charge gauge (x 35-190 from y 596). The compact HUD's cuts
// took the window's lower items off the panel and into the cluster (hardware,
// 2026-10-02: "固定チャットを開くと下の方が掛けてコンパクトUIの方に出ます").
// No layout file places the window (LYT_CHATTYPESELECT/LYT_CHAT give a full
// screen frame and 513x37 items; the code positions it), so the cut cannot be
// moved out of its way; the window's life can be seen instead.
//
// Each window is made by its factory, a new and the constructor and nothing
// else (Factory@HUiChat_TypeSelect 17FFAC0 slot 2 -> 871E40: new 0x9E0, ctor
// 872090; Factory@HUiChat 17FF058 slot 2 -> 865AF0: new 0xA90, ctor 865E90 --
// the only callers of either constructor), and goes through the class's
// deleting destructor (HUiChat_TypeSelect 17FFB08+0x28 -> 8725D0, HUiChat
// 17FF1B0+0x28 -> 867170). The category menu is the TypeSelect, the phrase list
// the HUiChat. Made minus destroyed is the windows open.
using ChatCreate=void*(*)(void*,void*,void*,void*);
using ChatDelete=void*(*)(void*,unsigned,void*,void*);
ChatCreate g_chatCreateOriginal[2]{};
ChatDelete g_chatDeleteOriginal[2]{};
std::atomic<int> g_chatAlive{0};
std::atomic<unsigned long long> g_chatOpened{0},g_chatClosed{0};
template<int I> void* HookChatCreate(void* factory,void* param,void* a,void* b) noexcept {
    void* window=g_chatCreateOriginal[I](factory,param,a,b);
    if(window) {
        g_chatOpened.fetch_add(1,std::memory_order_relaxed);
        edf6vr::g_openxr.SuspendUiCluster(g_chatAlive.fetch_add(1)+1>0);
    }
    return window;
}
template<int I> void* HookChatDelete(void* self,unsigned flags,void* a,void* b) noexcept {
    if(self) {
        g_chatClosed.fetch_add(1,std::memory_order_relaxed);
        int left=g_chatAlive.fetch_sub(1)-1;
        if(left<0) { g_chatAlive.store(0); left=0; }
        edf6vr::g_openxr.SuspendUiCluster(left>0);
    }
    return g_chatDeleteOriginal[I](self,flags,a,b);
}
bool InstallChatProbe(bool& changed) noexcept {
    changed=false;
    __try {
        auto* base=g_image.base;
        void** creates[2]={reinterpret_cast<void**>(base+0x17FFAC0+0x10),reinterpret_cast<void**>(base+0x17FF058+0x10)};
        void** deletes[2]={reinterpret_cast<void**>(base+0x17FFB08+0x28),reinterpret_cast<void**>(base+0x17FF1B0+0x28)};
        const std::uintptr_t createAt[2]={0x871E40,0x865AF0}, deleteAt[2]={0x8725D0,0x867170};
        for(int i=0;i<2;++i)
            if(*creates[i]!=base+createAt[i] || *deletes[i]!=base+deleteAt[i]) return false;
        for(int i=0;i<2;++i) {
            g_chatCreateOriginal[i]=reinterpret_cast<ChatCreate>(base+createAt[i]);
            g_chatDeleteOriginal[i]=reinterpret_cast<ChatDelete>(base+deleteAt[i]);
        }
        bool one=false;
        if(!edf6vr::ReplacePointer(creates[0],reinterpret_cast<void*>(g_chatCreateOriginal[0]),reinterpret_cast<void*>(&HookChatCreate<0>),one)) return false;
        changed|=one;
        if(!edf6vr::ReplacePointer(creates[1],reinterpret_cast<void*>(g_chatCreateOriginal[1]),reinterpret_cast<void*>(&HookChatCreate<1>),one)) return false;
        changed|=one;
        if(!edf6vr::ReplacePointer(deletes[0],reinterpret_cast<void*>(g_chatDeleteOriginal[0]),reinterpret_cast<void*>(&HookChatDelete<0>),one)) return false;
        changed|=one;
        if(!edf6vr::ReplacePointer(deletes[1],reinterpret_cast<void*>(g_chatDeleteOriginal[1]),reinterpret_cast<void*>(&HookChatDelete<1>),one)) return false;
        changed|=one;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
