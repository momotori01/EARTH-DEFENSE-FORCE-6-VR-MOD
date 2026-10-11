#pragma once
// "VR setting" in the Start (pause) menu: the settings that the mod reads again
// while the game runs, changed in the game (the user, 2026-10-12: "exe内でゲーム中
// でも変更できる項目は、スタートメニューにVRsettingの項目を追加してゲーム中に切り替え
// られるようにできる？", and "言語選択で日本語とそれ以外（英語）で表示言語別けてね").
//
// The game's menus are data: a layout (SGO) lists components, and a Button
// names the callback its window runs when it is pressed. So nothing of the
// game's menu code is changed; two layouts are offered to it through
// EDFModLoader's Mods folder, and one callback name is added to its windows.
//
//   Mods\UI\LYT_PAUSEMENU.SGO        the game's pause layout with one more Button,
//                                    ButtonVrSetting, whose callback is the game's
//                                    own CallWindow on the layout below
//   Mods\UI\LYT_EDF6VR_SETTING.SGO   a plain HUiLayout: a Button a setting (VrBtnNN,
//                                    callback cbVrSet with item=NN) and a TextField
//                                    a setting (VrValNN, the value now)
//
// Both are made by tools/make_vr_menu.py from the game's own layouts, in English
// and in Japanese, and are embedded here (menu_assets.rc). The pair of the
// game's language (0 = ja in its language number, read through its own
// GetLanguage; every other language gets English) is written while the mod
// runs, each beside a "<name>.edf6vr" marker; a file of that name without the
// marker is another mod's and is left alone (then there is no item). EDF6 VR
// setting.exe removes the marked files when it switches to Normal, and a press
// without this DLL would do nothing anyway: the game gives an unknown callback
// name its "Default" callback (850450: "指定されたコールバックが見つからないので
// デフォルトを割り当てます").
//
// cbVrSet: a window registers its named callbacks in 847990, called once from
// its set-up (83EB20), before its components are made and look their callback
// up by name. That call is redirected here: the game's registration runs, then
// one more entry goes into the same map (window+4E0) through the game's own
// insert (850810), in the game's own shape -- the 0x38-byte block 847C82 builds:
// a shared count (17FE3D8), the agent (17FDE70: weak pointer to the window,
// member function pointer, this adjustment) -- allocated with the game's
// operator new, so the game frees it with the window. Only the function pointer
// is ours; the agent calls it as (window, arguments) (840630). Plain HUiLayout
// windows only: ours is one.
//
// A press: the item number is read out of the callback's arguments (a
// std::map<wstring, wstring> at context+8, as ResultCallback 84F780 reads its
// own), the setting steps to its next value (vr_menu_items.h), that is written
// to EDF6VR.ini and ReloadTunables takes it at once, and the row's text field
// is set through the game's own lookups (839600 name -> index, 8859C0 index ->
// HUiTextField, 863690 set text; the three EDF6MultiSlot's menu label uses).
// The values are first shown when the window is first updated (OnUpdate, slot
// 1 of HUiLayout's table), which also keeps the board up while it is open
// (menu_board.h does the same for the pause menu itself).
// (vr_menu_items.h, the rows themselves, is included at the top of plugin.cpp: it has a namespace of its own.)
#include "vr_menu_rows.inc"

static_assert(edf6vr::kVrMenuItemCount==EDF6VR_MENU_ROWS,"kVrMenuItems and ROWS in tools/make_vr_menu.py must be the same list");

void ReloadTunables() noexcept;

constexpr std::uintptr_t kVrMenuRegisterCall=0x83EB20,kVrMenuRegister=0x847990,kVrMenuAddCallback=0x850810,
    kVrMenuNew=0x12D85B0,kVrMenuIndex=0x839600,kVrMenuTextField=0x8859C0,kVrMenuSetText=0x863690,
    kVrMenuLayoutTable=0x17F3E10,kVrMenuLayoutUpdate=0x84B490,kVrMenuAgentCount=0x17FE3D8,kVrMenuAgentTable=0x17FDE70,
    kVrMenuLanguage=0xE4000;
struct VrMenuBytes { std::uintptr_t rva; unsigned char bytes[12]; unsigned count; };
constexpr VrMenuBytes kVrMenuBytes[]={
    {kVrMenuRegister,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89},12},
    {kVrMenuAddCallback,{0x40,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xC0},12},
    {kVrMenuNew,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xEB,0x0F,0x48},12},
    {kVrMenuIndex,{0x40,0x53,0x48,0x83,0xEC,0x40,0x4C,0x8B,0xC2,0x48,0x8D,0x54},12},
    {kVrMenuTextField,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48},12},
    {kVrMenuSetText,{0x48,0x83,0xB9,0x28,0x02,0x00,0x00,0x00,0x74,0x11,0x48,0x85},12},
    {kVrMenuLanguage,{0x8B,0x05,0x2A,0xEB,0xFC,0x01,0xC3},7},     // mov eax,[language]; ret
};
// MSVC's std::wstring and std::shared_ptr as the game's functions take them.
struct VrMenuString {
    union { wchar_t text[8]; const wchar_t* far_; };
    std::uint64_t size,capacity;
    const wchar_t* Data() const noexcept { return capacity>=8?far_:text; }
};
struct VrMenuCount { void (**table)(VrMenuCount*); volatile long uses,weaks; };
struct VrMenuShared { void* object; VrMenuCount* count; };
using VrMenuUpdateFn=void(*)(void*,void*,void*,void*);

bool g_vrMenuWanted=true;            // [VR] PauseMenuVrSetting
bool g_vrMenuReady=false;            // the hooks are in and every address checked
VrMenuUpdateFn g_vrMenuUpdateOriginal=nullptr;
struct VrMenuFresh { void* window; int tries; };
VrMenuFresh g_vrMenuFresh[4]{};      // plain layouts set up and not yet seen with their components
void* g_vrMenuWindow=nullptr;        // ours while it is open; compared only, never read through
std::atomic<unsigned long long> g_vrMenuRegistered{0},g_vrMenuOpened{0},g_vrMenuPresses{0},g_vrMenuUpdates{0};
std::atomic<int> g_vrMenuFilesLanguage{-1};   // the pair in Mods\UI: 0 English, 1 Japanese, -1 none, -2 not ours to write

bool VrMenuJapanese() noexcept {
    return g_vrMenuReady && reinterpret_cast<int(*)()>(g_image.base+kVrMenuLanguage)()==0;
}
void VrMenuName(VrMenuString& out,const wchar_t* prefix,int index) noexcept {
    out={};
    std::swprintf(out.text,8,L"%ls%02d",prefix,index);   // "VrVal07": seven characters, held in place
    out.size=std::wcslen(out.text); out.capacity=7;
}
// The row's value as the INI has it now, into its text field. False when the
// window has no such field (it is not ours).
bool VrMenuShow(void* window,int index,bool japanese) noexcept {
    const auto& item=edf6vr::kVrMenuItems[index];
    wchar_t value[40]{},value2[40]{};
    GetPrivateProfileStringW(item.section,item.key,item.fallback,value,40,g_iniPath);
    if(item.key2) GetPrivateProfileStringW(item.section,item.key2,item.fallback2,value2,40,g_iniPath);
    const wchar_t* text=edf6vr::VrMenuLabel(item,edf6vr::VrMenuStateOf(item,value,value2),japanese,value);
    __try {
        auto* base=g_image.base;
        VrMenuString name; VrMenuName(name,L"VrVal",index);
        const int component=reinterpret_cast<int(*)(void*,const VrMenuString*)>(base+kVrMenuIndex)(window,&name);
        if(component<0) return false;
        VrMenuShared field{};
        reinterpret_cast<VrMenuShared*(*)(void*,VrMenuShared*,int)>(base+kVrMenuTextField)(window,&field,component);
        if(field.object) reinterpret_cast<void(*)(void*,const wchar_t*)>(base+kVrMenuSetText)(field.object,text);
        if(field.count && InterlockedDecrement(&field.count->uses)==0) {
            field.count->table[0](field.count);
            if(InterlockedDecrement(&field.count->weaks)==0) field.count->table[1](field.count);
        }
        return field.object!=nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool VrMenuShowAll(void* window) noexcept {
    const bool japanese=VrMenuJapanese();
    if(!VrMenuShow(window,0,japanese)) return false;
    for(int i=1;i<edf6vr::kVrMenuItemCount;++i) VrMenuShow(window,i,japanese);
    return true;
}
// "item" out of a callback's arguments: std::map<std::wstring, std::wstring>,
// MSVC's tree (head; +08 root; node +00 left, +10 right, +19 nil, +20 key, +40 value).
bool VrMenuArgument(const unsigned char* map,const wchar_t* key,int& out) noexcept {
    __try {
        const auto* head=*reinterpret_cast<const unsigned char* const*>(map);
        if(!head) return false;
        const std::size_t length=std::wcslen(key);
        const unsigned char* stack[32]{}; int depth=0,visited=0;
        const auto* node=*reinterpret_cast<const unsigned char* const*>(head+8);
        while((node && !node[0x19]) || depth) {
            while(node && !node[0x19]) {
                if(depth>=32) return false;
                stack[depth++]=node; node=*reinterpret_cast<const unsigned char* const*>(node);
            }
            node=stack[--depth];
            if(++visited>64) return false;
            const auto* name=reinterpret_cast<const VrMenuString*>(node+0x20);
            if(name->size==length && !std::wcsncmp(name->Data(),key,length)) {
                const auto* value=reinterpret_cast<const VrMenuString*>(node+0x40);
                if(!value->size || value->size>16) return false;
                wchar_t* end=nullptr;
                out=static_cast<int>(std::wcstol(value->Data(),&end,10));
                return end!=value->Data();
            }
            node=*reinterpret_cast<const unsigned char* const*>(node+0x10);
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The press of a row (the game calls this as the window's cbVrSet).
void OnVrMenuSet(void* window,void* arguments) noexcept {
    int index=-1;
    __try {
        if(!window || !arguments) return;
        auto* context=(*reinterpret_cast<unsigned char*(***)(void*)>(arguments))[0](arguments);
        if(!context || !VrMenuArgument(context+8,L"item",index)) return;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if(index<0 || index>=edf6vr::kVrMenuItemCount || !g_iniPath[0]) return;
    const auto& item=edf6vr::kVrMenuItems[index];
    wchar_t value[40]{},value2[40]{};
    GetPrivateProfileStringW(item.section,item.key,item.fallback,value,40,g_iniPath);
    if(item.key2) GetPrivateProfileStringW(item.section,item.key2,item.fallback2,value2,40,g_iniPath);
    const int now=edf6vr::VrMenuStateOf(item,value,value2),next=edf6vr::VrMenuNext(item,now,value);
    const auto& state=item.states[next];
    WritePrivateProfileStringW(item.section,item.key,state.value,g_iniPath);
    if(item.key2 && state.value2) WritePrivateProfileStringW(item.section,item.key2,state.value2,g_iniPath);
    ReloadTunables();
    g_vrMenuWindow=window;
    const bool shown=VrMenuShow(window,index,VrMenuJapanese());
    g_vrMenuPresses.fetch_add(1,std::memory_order_relaxed);
    Log("VRMENU item=%d [%ls] %ls: %ls -> %ls%ls%ls shown=%d",index,item.section,item.key,value,state.value,
        item.key2 && state.value2?L", second key ":L"",item.key2 && state.value2?state.value2:L"",shown?1:0);
}
// One more named callback on a plain HUiLayout window, after the game's own.
bool VrMenuAddCallback(void* window) noexcept {
    __try {
        auto* base=g_image.base;
        auto** fields=reinterpret_cast<void**>(window);
        if(fields[0]!=base+kVrMenuLayoutTable) return false;
        void* object=fields[1];
        auto* owner=static_cast<VrMenuCount*>(fields[2]);      // the window's own shared count (+10)
        if(!object || !owner || !owner->uses) return false;
        auto* block=static_cast<unsigned char*>(reinterpret_cast<void*(*)(std::size_t)>(base+kVrMenuNew)(0x38));
        if(!block) return false;
        std::memset(block,0,0x38);
        *reinterpret_cast<void**>(block)=base+kVrMenuAgentCount;
        *reinterpret_cast<long*>(block+0x08)=1;
        *reinterpret_cast<long*>(block+0x0C)=1;
        *reinterpret_cast<void**>(block+0x10)=base+kVrMenuAgentTable;
        *reinterpret_cast<void**>(block+0x18)=object;
        *reinterpret_cast<void**>(block+0x20)=owner;
        InterlockedIncrement(&owner->weaks);                   // the agent's weak pointer to the window
        *reinterpret_cast<void**>(block+0x28)=reinterpret_cast<void*>(&OnVrMenuSet);
        VrMenuString name{};
        std::memcpy(name.text,L"cbVrSet",8*sizeof(wchar_t)); name.size=7; name.capacity=7;
        VrMenuShared agent{block+0x10,reinterpret_cast<VrMenuCount*>(block)};
        // By value in the game's source: the callee keeps a copy in its map and releases this one.
        reinterpret_cast<void(*)(void*,const VrMenuString*,VrMenuShared*)>(base+kVrMenuAddCallback)(window,&name,&agent);
        // To be looked at when it is first updated; the oldest note gives way.
        static unsigned turn=0;
        VrMenuFresh* slot=&g_vrMenuFresh[turn++%4];
        for(auto& fresh:g_vrMenuFresh) if(fresh.window==window || !fresh.window) { slot=&fresh; break; }
        *slot={window,4};
        g_vrMenuRegistered.fetch_add(1,std::memory_order_relaxed);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void HookVrMenuRegister(void* window) {
    reinterpret_cast<void(*)(void*)>(g_image.base+kVrMenuRegister)(window);
    if(g_vrMenuWanted) VrMenuAddCallback(window);
}
void HookVrMenuUpdate(void* self,void* context,void* a,void* b) noexcept {
    for(auto& fresh:g_vrMenuFresh) {
        if(fresh.window!=self) continue;
        if(VrMenuShowAll(self)) {
            fresh={};
            g_vrMenuWindow=self;
            g_vrMenuOpened.fetch_add(1,std::memory_order_relaxed);
            Log("VRMENU window open: %d rows shown in %s",edf6vr::kVrMenuItemCount,VrMenuJapanese()?"Japanese":"English");
        } else if(--fresh.tries<=0) {
            fresh={};
            if(g_vrMenuWindow==self) g_vrMenuWindow=nullptr;   // the address went to another window
        }
        break;
    }
    if(self==g_vrMenuWindow) {
        edf6vr::g_openxr.MarkMenuOpen();
        g_vrMenuUpdates.fetch_add(1,std::memory_order_relaxed);
    }
    g_vrMenuUpdateOriginal(self,context,a,b);
}

// ---- the two layouts in Mods\UI -----------------------------------------------
struct VrMenuFile { const wchar_t* name; int english,japanese; };   // RCDATA in menu_assets.rc
constexpr VrMenuFile kVrMenuFiles[2]={{L"LYT_PAUSEMENU.SGO",301,302},{L"LYT_EDF6VR_SETTING.SGO",303,304}};
constexpr char kVrMenuMarker[]="Written by EDF6VR (the in-game VR setting menu). EDF6VR replaces or removes the file beside this one.\r\n";
bool VrMenuFolder(std::wstring& folder) {
    wchar_t host[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,host,MAX_PATH);
    const auto slash=length && length<MAX_PATH?wcsrchr(host,L'\\'):nullptr;
    if(!slash) return false;
    folder.assign(host,slash+1); folder+=L"Mods\\UI\\";
    return true;
}
bool VrMenuExists(const std::wstring& path) noexcept { return GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES; }
bool VrMenuSame(const std::wstring& path,const void* data,DWORD size) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    std::vector<unsigned char> now(size);
    DWORD read=0;
    const bool same=GetFileSize(file,nullptr)==size && ReadFile(file,now.data(),size,&read,nullptr) && read==size
        && !std::memcmp(now.data(),data,size);
    CloseHandle(file);
    return same;
}
bool VrMenuWrite(const std::wstring& path,const void* data,DWORD size) noexcept {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD written=0;
    const bool ok=WriteFile(file,data,size,&written,nullptr) && written==size;
    CloseHandle(file);
    return ok;
}
// Takes our two files out again (marker and all); another mod's are not touched.
void VrMenuRemoveFiles() noexcept {
    try {
        std::wstring folder;
        if(!VrMenuFolder(folder)) return;
        for(const auto& f:kVrMenuFiles) {
            const std::wstring path=folder+f.name,marker=path+L".edf6vr";
            if(!VrMenuExists(marker)) continue;
            DeleteFileW(path.c_str());
            DeleteFileW(marker.c_str());
        }
    } catch(...) {}
}
// The pair of the game's language in Mods\UI. Called on the watchdog's beat:
// nothing is touched unless the language is not the one the files are in.
void VrMenuSyncFiles() noexcept {
    if(!g_vrMenuReady || !g_vrMenuWanted) return;
    const int language=VrMenuJapanese()?1:0;
    const int have=g_vrMenuFilesLanguage.load();
    if(have==language || have==-2) return;
    try {
        std::wstring folder;
        if(!VrMenuFolder(folder)) return;
        for(const auto& f:kVrMenuFiles)
            if(VrMenuExists(folder+f.name) && !VrMenuExists(folder+f.name+L".edf6vr")) {
                g_vrMenuFilesLanguage=-2;
                Log("VRMENU off: Mods\\UI\\%ls is another mod's (no .edf6vr marker beside it); the Start menu gets no VR setting item",f.name);
                return;
            }
        const std::wstring mods(folder,0,folder.size()-3);     // ...\Mods\ ; UI\ under it
        CreateDirectoryW(mods.c_str(),nullptr);
        CreateDirectoryW(folder.c_str(),nullptr);
        bool ok=true;
        for(const auto& f:kVrMenuFiles) {
            const auto resource=FindResourceW(g_module,MAKEINTRESOURCEW(language?f.japanese:f.english),MAKEINTRESOURCEW(10));   // RT_RCDATA
            const auto loaded=resource?LoadResource(g_module,resource):nullptr;
            const void* data=loaded?LockResource(loaded):nullptr;
            const DWORD size=resource?SizeofResource(g_module,resource):0;
            if(!data || !size) { ok=false; break; }
            const std::wstring path=folder+f.name;
            if(!VrMenuWrite(path+L".edf6vr",kVrMenuMarker,sizeof(kVrMenuMarker)-1)) { ok=false; break; }
            if(!VrMenuSame(path,data,size) && !VrMenuWrite(path,data,size)) { ok=false; break; }
        }
        g_vrMenuFilesLanguage=ok?language:-1;
        static int said=-3;
        if(said!=(ok?language:-1)) {
            said=ok?language:-1;
            Log("VRMENU files %s: %lsLYT_PAUSEMENU.SGO and LYT_EDF6VR_SETTING.SGO in %s (the game's language number is %s)",
                ok?"written":"NOT written",folder.c_str(),language?"Japanese":"English",language?"0, ja":"not 0");
        }
    } catch(...) {}
}

bool InstallVrMenu(bool& changed) noexcept {
    changed=false;
    g_vrMenuWanted=GetPrivateProfileIntW(L"VR",L"PauseMenuVrSetting",1,g_iniPath)!=0;
    if(!g_vrMenuWanted) { VrMenuRemoveFiles(); return true; }
    __try {
        auto* base=g_image.base;
        if(!base) return false;
        for(const auto& f:kVrMenuBytes) if(std::memcmp(base+f.rva,f.bytes,f.count)) return false;
        void* table=base+kVrMenuLayoutTable;
        if(!edf6vr::HasType(g_image,&table,".?AVHUiLayout@ui@@")) return false;
        auto** count=reinterpret_cast<void**>(base+kVrMenuAgentCount);
        auto** agent=reinterpret_cast<void**>(base+kVrMenuAgentTable);
        if(count[0]!=base+0x84E4D0 || count[1]!=base+0x84E3A0 || agent[0]!=base+0x83FD80 || agent[1]!=base+0x840630) return false;
        g_vrMenuUpdateOriginal=reinterpret_cast<VrMenuUpdateFn>(base+kVrMenuLayoutUpdate);
        bool one=false;
        if(!edf6vr::ReplacePointer(reinterpret_cast<void**>(base+kVrMenuLayoutTable+8),reinterpret_cast<void*>(g_vrMenuUpdateOriginal),
            reinterpret_cast<void*>(&HookVrMenuUpdate),one)) return false;
        changed|=one;
        if(!edf6vr::RedirectCall(base+kVrMenuRegisterCall,base+kVrMenuRegister,reinterpret_cast<void*>(&HookVrMenuRegister),one)) return false;
        changed|=one;
        g_vrMenuReady=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    VrMenuSyncFiles();
    return true;
}
