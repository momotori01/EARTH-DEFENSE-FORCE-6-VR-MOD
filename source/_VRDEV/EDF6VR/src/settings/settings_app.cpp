// "EDF6 VR setting.exe": one window for what the .bat files did and for the
// settings players change outside the game. It sits next to EDF6.exe (the ZIP
// is extracted into the game folder as before) and only ever touches:
//  - Mods\Plugins\EDF6VR.ini, line by line (settings_core.h), after bringing it
//    up to date from EDF6VR\EDF6VR.defaults.ini the way the DLL does;
//  - Mods\Plugins\EDF6VR.dll <-> EDF6VR.dll.disabled (VR mode, as Switch-VR.ps1);
//  - what the shipped tools do when asked: EDF6VR\Update-EDF6VR.ps1 -Yes and
//    Mods\HDTexture\hd_textures.py, run hidden with their output shown here;
//  - a log ZIP it writes into its own folder.
// Everything a player reads is plain, simple English.
#include "settings_core.h"
#include "ini_defaults.h"
#include <Windows.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <TlHelp32.h>
#include <winhttp.h>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace edf6vr::settings;

namespace {
// ---- messages from the worker threads ------------------------------------
constexpr UINT WM_APP_LATEST=WM_APP+1;       // lParam: new std::string (version or "")
constexpr UINT WM_APP_UPDATE_LINE=WM_APP+2;  // lParam: new std::wstring
constexpr UINT WM_APP_UPDATE_DONE=WM_APP+3;  // wParam: exit code
constexpr UINT WM_APP_HD_PROGRESS=WM_APP+4;  // wParam: percent, lParam: new std::wstring
constexpr UINT WM_APP_HD_LINE=WM_APP+5;      // lParam: new std::wstring
constexpr UINT WM_APP_HD_DONE=WM_APP+6;      // wParam: exit code
constexpr UINT WM_APP_ZIP_DONE=WM_APP+7;     // wParam: exit code, lParam: new std::wstring (zip path)
constexpr UINT_PTR kGameTimer=1;

enum : int {
    ID_TABS=100, ID_UPDATE, ID_MODE_VR, ID_MODE_FLAT, ID_MODE_APPLY,
    ID_SIZE_LOW, ID_SIZE_NORMAL, ID_SIZE_HIGH, ID_SIZE_CUSTOM, ID_SIZE_VALUE, ID_SIZE_APPLY,
    ID_HD_MAKE, ID_HD_DELETE, ID_HAND_RIGHT, ID_HAND_LEFT, ID_HAND_APPLY, ID_LOGS,
    ID_COCKPIT_ON, ID_COCKPIT_APPLY, ID_HUD_ON, ID_HUD_CORNER, ID_HUD_RWRIST, ID_HUD_LWRIST, ID_HUD_APPLY,
    ID_RETICLE_APPLY, ID_RECOIL_ON, ID_RECOIL_APPLY, ID_BUZZ_APPLY, ID_MIRROR_ON, ID_MIRROR_APPLY, ID_RESET,
};

HINSTANCE g_instance=nullptr;
HWND g_window=nullptr,g_tabs=nullptr,g_footer=nullptr;
HFONT g_font=nullptr,g_bold=nullptr;
HBRUSH g_white=nullptr;
HANDLE g_job=nullptr;                      // the HD builder and its upscaler die with this program
int g_dpi=96;
std::wstring g_root,g_exe,g_ini,g_defaults,g_plugins;
bool g_gameRunning=false;
enum class Task { None, Update, Hd, Zip } g_task=Task::None;
bool g_renamedForUpdate=false;
std::wstring g_updateResult,g_updateError,g_hdLast;
Version g_installed,g_latest; bool g_latestChecked=false;

int S(int v) { return MulDiv(v,g_dpi,96); }

// ---- text and files --------------------------------------------------------
std::wstring Wide(const std::string& s,UINT codepage=CP_UTF8) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(codepage,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring out(static_cast<size_t>(n>0?n:0),L'\0');
    if(n>0) MultiByteToWideChar(codepage,0,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}
std::string Narrow(const std::wstring& s) {
    if(s.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<size_t>(n>0?n:0),'\0');
    if(n>0) WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
bool Exists(const std::wstring& path) {
    const DWORD a=GetFileAttributesW(path.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}
// Shared with a game that may be writing it, so a log in use still reads.
bool ReadBytes(const std::wstring& path,std::string& out) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok=GetFileSizeEx(file,&size) && size.QuadPart<(1ll<<31);
    if(ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read=0;
        ok=out.empty() || (ReadFile(file,out.data(),static_cast<DWORD>(out.size()),&read,nullptr) && read==out.size());
    }
    CloseHandle(file);
    return ok;
}
// Written beside it and moved over it: a failed write leaves the old file.
bool WriteBytes(const std::wstring& path,const std::string& bytes) {
    const std::wstring temporary=path+L".new";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    DWORD wrote=0;
    const bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr) && wrote==bytes.size();
    CloseHandle(file);
    if(!ok || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}
std::wstring Stamp() {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t text[32]{};
    swprintf_s(text,L"%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    return text;
}
std::wstring PowerShell() {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system,MAX_PATH);
    return std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
}

// ---- the game ----------------------------------------------------------------
bool GameRunning() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{sizeof(entry)};
    bool found=false;
    for(BOOL more=Process32FirstW(snapshot,&entry);more && !found;more=Process32NextW(snapshot,&entry))
        found=!_wcsicmp(entry.szExeFile,L"EDF6.exe") || !_wcsicmp(entry.szExeFile,L"LaunchGame.exe");
    CloseHandle(snapshot);
    return found;
}
Version InstalledVersion() {
    std::string bytes;
    if(ReadBytes(g_root+L"\\EDF6VR\\PACKAGE_MANIFEST.json",bytes)) {
        const auto v=ManifestVersion(bytes);
        if(v.Valid()) return v;
    }
    for(const wchar_t* name:{L"\\EDF6VR.dll",L"\\EDF6VR.dll.disabled"})
        if(ReadBytes(g_plugins+name,bytes)) { const auto v=DllVersion(bytes); if(v.Valid()) return v; }
    return {};
}
std::string LatestVersionFromGitHub() {
    std::string body;
    HINTERNET session=WinHttpOpen(L"EDF6VR-Settings",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!session) return {};
    WinHttpSetTimeouts(session,8000,8000,10000,10000);
    HINTERNET connection=WinHttpConnect(session,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",
        L"/repos/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD/releases/latest",nullptr,WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
    DWORD status=0,size=sizeof(status);
    if(request && WinHttpSendRequest(request,L"Accept: application/vnd.github+json\r\n",static_cast<DWORD>(-1),
                                     WINHTTP_NO_REQUEST_DATA,0,0,0)
       && WinHttpReceiveResponse(request,nullptr)
       && WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,
                              &status,&size,WINHTTP_NO_HEADER_INDEX) && status==200) {
        for(;;) {
            DWORD available=0;
            if(!WinHttpQueryDataAvailable(request,&available) || !available || body.size()>(4u<<20)) break;
            std::string chunk(available,'\0');
            DWORD read=0;
            if(!WinHttpReadData(request,chunk.data(),available,&read) || !read) break;
            body.append(chunk.data(),read);
        }
    }
    if(request) WinHttpCloseHandle(request);
    if(connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ToString(LatestReleaseVersion(body));
}

// ---- EDF6VR.ini ------------------------------------------------------------
// Before the first start of the game there is no INI yet: start from the
// shipped defaults. An older one is brought up to date exactly as the DLL
// would (new keys, new sections with their notes), so what is set here is not
// undone at the next start.
bool EnsureIni(std::wstring& error) {
    std::string defaults;
    if(!ReadBytes(g_defaults,defaults) || defaults.empty()) {
        if(Exists(g_ini)) return true;
        error=L"EDF6VR\\EDF6VR.defaults.ini is missing. Extract the mod again.";
        return false;
    }
    if(!Exists(g_ini)) {
        if(!WriteBytes(g_ini,defaults)) { error=L"Could not write Mods\\Plugins\\EDF6VR.ini."; return false; }
        return true;
    }
    const auto revision=IniGet(ParseIni(defaults),"Settings","Revision");
    edf6vr::ResetOlderIni(defaults.data(),defaults.size(),g_ini.c_str(),revision?std::atoi(revision->c_str()):2);
    edf6vr::MergeIniDefaults(defaults.data(),defaults.size(),g_ini.c_str());
    return true;
}
IniText LoadIni() {
    std::string bytes;
    if(!ReadBytes(g_ini,bytes)) ReadBytes(g_defaults,bytes);
    return ParseIni(bytes);
}
std::string IniValue(const IniText& ini,const char* section,const char* key,const char* fallback) {
    const auto v=IniGet(ini,section,key);
    return v?*v:fallback;
}
bool IniOn(const IniText& ini,const char* section,const char* key,bool fallback) {
    const auto v=IniGet(ini,section,key);
    return v?std::atoi(v->c_str())!=0:fallback;
}
struct Setting { const char* section; const char* key; std::string value; };
bool SaveSettings(const std::vector<Setting>& settings,std::wstring& error) {
    if(!EnsureIni(error)) return false;
    std::string bytes;
    if(!ReadBytes(g_ini,bytes)) { error=L"Could not read Mods\\Plugins\\EDF6VR.ini."; return false; }
    auto ini=ParseIni(bytes);
    for(const auto& s:settings) IniSet(ini,s.section,s.key,s.value);
    if(!WriteBytes(g_ini,WriteIni(ini))) { error=L"Could not write Mods\\Plugins\\EDF6VR.ini."; return false; }
    return true;
}
std::string Number(double v) {
    char text[32]{};
    std::snprintf(text,sizeof(text),"%.3f",v);
    std::string s=text;
    while(s.size()>1 && s.back()=='0') s.pop_back();
    if(s.back()=='.') s+='0';
    return s;
}

// ---- running the shipped tools ---------------------------------------------
// Runs hidden, output (and errors) to `segment` a piece at a time: split at
// "\n" and at "\r" too, which the HD builder uses to redraw its progress line.
DWORD RunCaptured(std::wstring command,const std::wstring& directory,HANDLE job,
                  const std::function<void(const std::string&)>& segment) {
    SECURITY_ATTRIBUTES inherit{sizeof(inherit),nullptr,TRUE};
    HANDLE readPipe=nullptr,writePipe=nullptr;
    if(!CreatePipe(&readPipe,&writePipe,&inherit,0)) return 0xFFFFFFFF;
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    HANDLE nothing=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,OPEN_EXISTING,0,nullptr);
    STARTUPINFOW start{sizeof(start)};
    start.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; start.wShowWindow=SW_HIDE;
    start.hStdInput=nothing; start.hStdOutput=writePipe; start.hStdError=writePipe;
    PROCESS_INFORMATION process{};
    const BOOL started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,
                                      nullptr,directory.c_str(),&start,&process);
    CloseHandle(writePipe);
    if(nothing!=INVALID_HANDLE_VALUE) CloseHandle(nothing);
    if(!started) { CloseHandle(readPipe); segment("ERROR: could not start the tool."); return 0xFFFFFFFF; }
    if(job) AssignProcessToJobObject(job,process.hProcess);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    char buffer[4096]; DWORD read=0; std::string pending;
    while(ReadFile(readPipe,buffer,sizeof(buffer),&read,nullptr) && read) {
        for(DWORD i=0;i<read;++i) {
            const char c=buffer[i];
            if(c=='\r' || c=='\n') { if(!pending.empty()) segment(pending); pending.clear(); }
            else pending+=c;
        }
    }
    if(!pending.empty()) segment(pending);
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=0xFFFFFFFF; GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess);
    return code;
}
void Post(UINT message,WPARAM w,std::wstring text) { PostMessageW(g_window,message,w,reinterpret_cast<LPARAM>(new std::wstring(std::move(text)))); }

// ---- the window's parts ------------------------------------------------------
struct Row { int tab; std::vector<HWND> parts; };
std::vector<Row> g_rows;
std::vector<std::pair<HWND,COLORREF>> g_colours;
std::vector<HWND> g_actions;               // disabled while the game runs or a task works
int g_y=0,g_tab=0,g_shownTab=0;
constexpr int kLeft=24,kRight=332,kApplyX=656,kRowHeight=78,kTop=46,kWidth=760;

HWND g_stUpdate,g_btUpdate,g_stMode,g_rbVr,g_rbFlat,g_stSize,g_rbLow,g_rbNormal,g_rbHigh,g_rbCustom,g_edSize,
     g_stHd,g_pbHd,g_btHdMake,g_btHdDelete,g_stHand,g_cbRight,g_cbLeft,g_stLogs,g_btLogs,
     g_stCockpit,g_cbCockpit,g_stHud,g_cbHud,g_rbCorner,g_rbRWrist,g_rbLWrist,g_stReticle,g_edReticle,
     g_stRecoil,g_cbRecoil,g_stBuzz,g_edBuzz,g_stMirror,g_cbMirror,g_stReset;

void Colour(HWND h,COLORREF c) {
    for(auto& entry:g_colours) if(entry.first==h) { entry.second=c; InvalidateRect(h,nullptr,TRUE); return; }
    g_colours.push_back({h,c}); InvalidateRect(h,nullptr,TRUE);
}
HWND Part(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0,HFONT font=nullptr,DWORD ex=0) {
    HWND hwnd=CreateWindowExW(ex,cls,text,WS_CHILD|style,S(x),S(y),S(w),S(h),g_window,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_instance,nullptr);
    SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font?font:g_font),TRUE);
    g_rows.back().parts.push_back(hwnd);
    return hwnd;
}
// A row: its name and a short note on the left (broken into lines by hand,
// one idea a line), what it is now on the right, and below that its controls,
// with the button that does it at the far right. A status of more than one
// line is narrower, to stay clear of that button.
HWND BeginRow(const wchar_t* title,const wchar_t* note,int statusLines=1,int statusWidth=404) {
    g_rows.push_back({g_tab,{}});
    Part(L"STATIC",title,SS_LEFT,kLeft,g_y,296,20,0,g_bold);
    Colour(Part(L"STATIC",note,SS_LEFT,kLeft,g_y+20,300,54),RGB(90,90,90));
    return Part(L"STATIC",L"",statusLines>1?SS_LEFT:SS_LEFTNOWORDWRAP|SS_ENDELLIPSIS,kRight,g_y,statusWidth,18*statusLines+2);
}
HWND Button(const wchar_t* text,int id,int x=kApplyX,int w=80) {
    HWND h=Part(L"BUTTON",text,BS_PUSHBUTTON|WS_TABSTOP,x,g_y+24,w,28,id);
    g_actions.push_back(h);
    return h;
}
HWND Radio(const wchar_t* text,int id,int x,int w) { return Part(L"BUTTON",text,BS_RADIOBUTTON|WS_TABSTOP,x,g_y+26,w,22,id); }
HWND Check(const wchar_t* text,int id,int x,int w,bool automatic) {
    return Part(L"BUTTON",text,(automatic?BS_AUTOCHECKBOX:BS_CHECKBOX)|WS_TABSTOP,x,g_y+26,w,22,id);
}
HWND Edit(int x,int w) { return Part(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,x,g_y+26,w,22,0,nullptr,WS_EX_CLIENTEDGE); }
void EndRow() { g_y+=kRowHeight; }

void SetCheck(HWND h,bool on) { SendMessageW(h,BM_SETCHECK,on?BST_CHECKED:BST_UNCHECKED,0); }
bool Checked(HWND h) { return SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED; }
void Pick(std::initializer_list<HWND> group,HWND chosen) { for(HWND h:group) SetCheck(h,h==chosen); }
std::wstring Text(HWND h) {
    std::wstring s(static_cast<size_t>(GetWindowTextLengthW(h))+1,L'\0');
    GetWindowTextW(h,s.data(),static_cast<int>(s.size()));
    s.resize(wcslen(s.c_str()));
    return s;
}
void Say(HWND status,const std::wstring& text,bool bad=false) {
    SetWindowTextW(status,text.c_str());
    Colour(status,bad?RGB(200,0,0):RGB(0,0,0));
}

void ShowTab(int tab) {
    g_shownTab=tab;
    for(const auto& row:g_rows) if(row.tab>=0) for(HWND h:row.parts) ShowWindow(h,row.tab==tab?SW_SHOW:SW_HIDE);
    if(g_task!=Task::Hd) ShowWindow(g_pbHd,SW_HIDE);
}
// Buttons work only while the game is closed and nothing else is running.
void RefreshButtons() {
    const bool free=!g_gameRunning && g_task==Task::None;
    for(HWND h:g_actions) EnableWindow(h,free);
    if(free && g_latestChecked && g_installed.Valid() && g_latest.Valid() && !(g_installed<g_latest)) EnableWindow(g_btUpdate,FALSE);
    SetWindowTextW(g_footer,g_gameRunning?L"The game is running. Close the game to change settings.   （ゲームを閉じてから変更してください）"
                                         :L"Changes are used the next time you start the game.   （次にゲームを起動した時から反映されます）");
    Colour(g_footer,g_gameRunning?RGB(200,0,0):RGB(90,90,90));
}

// ---- what each row shows ------------------------------------------------------
std::wstring ModeNow(bool& vr,bool& missing) {
    vr=Exists(g_plugins+L"\\EDF6VR.dll");
    missing=!vr && !Exists(g_plugins+L"\\EDF6VR.dll.disabled");
    return missing?L"EDF6VR.dll is missing. Extract the mod again.":vr?L"Now: VR":L"Now: Normal (no VR)";
}
// Three lines: what is installed, the newest release, and what the last update said.
std::wstring g_updateNote; bool g_updateNoteBad=false;
void ShowVersions() {
    std::wstring text=L"Installed: "+(g_installed.Valid()?Wide(ToString(g_installed)):std::wstring(L"unknown"));
    if(!g_latestChecked) text+=L"\nNewest: checking...";
    else if(!g_latest.Valid()) text+=L"\nNewest: could not check (no internet?)";
    else {
        text+=L"\nNewest: "+Wide(ToString(g_latest));
        if(g_installed.Valid() && !(g_installed<g_latest)) text+=L"  (you have it)";
    }
    if(!g_updateNote.empty()) text+=L"\n"+g_updateNote;
    Say(g_stUpdate,text,g_updateNoteBad);
}
void ShowHd() {
    if(g_task==Task::Hd) return;
    const std::wstring tools=g_root+L"\\Mods\\HDTexture";
    if(!Exists(tools+L"\\python\\python.exe") || !Exists(tools+L"\\hd_textures.py"))
        Say(g_stHd,L"The HD texture tools are missing.\nExtract the mod again.",true);
    else if(Exists(tools+L"\\complete.txt")) Say(g_stHd,L"Made.\nThey are used in the game.");
    // A pack from before complete.txt existed may be whole or not: say so plainly.
    else if(Exists(g_root+L"\\Mods\\HDTextureWork\\written.txt")) Say(g_stHd,L"Made, but not checked.\nPress Make to check and finish it.");
    else Say(g_stHd,L"Not made.");
}
void LoadAll() {
    const auto ini=LoadIni();
    g_installed=InstalledVersion();
    ShowVersions();
    bool vr=false,missing=false;
    Say(g_stMode,ModeNow(vr,missing),missing);
    Pick({g_rbVr,g_rbFlat},vr?g_rbVr:g_rbFlat);

    const int width=std::atoi(IniValue(ini,"Render","ForceWidth","3840").c_str());
    const int height=std::atoi(IniValue(ini,"Render","ForceHeight","2160").c_str());
    const double scale=ScaleForWidth(width>0?width:kBaseWidth);
    wchar_t text[96]{};
    swprintf_s(text,L"Now: %.2f   (%d x %d)",scale,width,height);
    Say(g_stSize,text);
    HWND preset=std::fabs(scale-0.8)<.005?g_rbLow:std::fabs(scale-1.0)<.005?g_rbNormal:std::fabs(scale-1.25)<.005?g_rbHigh:g_rbCustom;
    Pick({g_rbLow,g_rbNormal,g_rbHigh,g_rbCustom},preset);
    SetWindowTextW(g_edSize,Wide(Number(scale)).c_str());

    ShowHd();
    const bool left=IniOn(ini,"LeftHanded","LeftHanded",false);
    Say(g_stHand,left?L"Now: Left hand":L"Now: Right hand");
    Pick({g_cbRight,g_cbLeft},left?g_cbLeft:g_cbRight);

    const bool cockpit=IniOn(ini,"VR","VehicleCockpit",true);
    Say(g_stCockpit,cockpit?L"Now: On":L"Now: Off"); SetCheck(g_cbCockpit,cockpit);
    const bool hud=IniOn(ini,"Render","UiCluster",true);
    int place=std::atoi(IniValue(ini,"Render","UiClusterPlace","0").c_str());
    if(place<0 || place>2) place=0;
    static const wchar_t* places[3]={L"corner of your view",L"right wrist",L"left wrist"};
    Say(g_stHud,hud?(std::wstring(L"Now: On, ")+places[place]):std::wstring(L"Now: Off (the game's full-size HUD)"));
    SetCheck(g_cbHud,hud); Pick({g_rbCorner,g_rbRWrist,g_rbLWrist},place==1?g_rbRWrist:place==2?g_rbLWrist:g_rbCorner);
    const auto reticle=IniValue(ini,"Render","ReticleScale","0.5");
    Say(g_stReticle,L"Now: "+Wide(reticle)); SetWindowTextW(g_edReticle,Wide(reticle).c_str());
    const bool recoil=IniOn(ini,"VR","RecoilKick",true);
    Say(g_stRecoil,recoil?L"Now: On":L"Now: Off"); SetCheck(g_cbRecoil,recoil);
    const auto buzz=IniValue(ini,"VR","ShotBuzz","0.5");
    Say(g_stBuzz,L"Now: "+Wide(buzz)); SetWindowTextW(g_edBuzz,Wide(buzz).c_str());
    const bool mirror=IniOn(ini,"Render","DesktopMirror",true);
    Say(g_stMirror,mirror?L"Now: On":L"Now: Off"); SetCheck(g_cbMirror,mirror);
}

// ---- actions -------------------------------------------------------------------
bool Save(HWND status,const std::vector<Setting>& settings) {
    std::wstring error;
    if(g_gameRunning) { Say(status,L"Close the game first.",true); return false; }
    if(!SaveSettings(settings,error)) { Say(status,error,true); return false; }
    LoadAll();
    return true;
}
void ApplyMode() {
    if(GameRunning()) { Say(g_stMode,L"Close the game first.",true); return; }
    const std::wstring enabled=g_plugins+L"\\EDF6VR.dll",disabled=enabled+L".disabled";
    bool ok=true;
    if(!Exists(enabled) && !Exists(disabled)) { Say(g_stMode,L"EDF6VR.dll is missing. Extract the mod again.",true); return; }
    // As Switch-VR.ps1: keep the newest enabled DLL; an old disabled copy is kept aside.
    if(Checked(g_rbVr)) { if(!Exists(enabled)) ok=MoveFileW(disabled.c_str(),enabled.c_str())!=0; }
    else if(Exists(enabled)) {
        if(Exists(disabled)) ok=MoveFileW(disabled.c_str(),(disabled+L".previous-"+Stamp()).c_str())!=0;
        ok=ok && MoveFileW(enabled.c_str(),disabled.c_str())!=0;
    }
    if(!ok) { Say(g_stMode,L"Could not switch. Is the game still running?",true); return; }
    LoadAll();
}
void ApplySize() {
    double scale=1.0;
    if(Checked(g_rbLow)) scale=0.8; else if(Checked(g_rbHigh)) scale=1.25;
    else if(Checked(g_rbCustom) && !ParseNumber(Narrow(Text(g_edSize)),scale)) { Say(g_stSize,L"Type a number, like 1.1",true); return; }
    int width=0,height=0;
    if(!SizeForScale(scale,width,height)) { Say(g_stSize,L"The number must be from 0.5 to 2.0.",true); return; }
    Save(g_stSize,{{"Render","ForceWidth",std::to_string(width)},{"Render","ForceHeight",std::to_string(height)}});
}
void ApplyNumber(HWND status,HWND edit,const char* section,const char* key,double least,double most) {
    double v=0;
    if(!ParseNumber(Narrow(Text(edit)),v) || v<least || v>most) {
        Say(status,L"Type a number from "+Wide(Number(least))+L" to "+Wide(Number(most))+L".",true);
        return;
    }
    Save(status,{{section,key,Number(v)}});
}
void Reset() {
    if(MessageBoxW(g_window,L"Put all VR settings back to how they came?\n\nYour picture size and gun hand stay.\n"
                   L"The old settings file is kept as a backup.\n\n設定を初期値に戻しますか？（解像度と利き手はそのまま）",L"Reset settings",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
    if(GameRunning()) { Say(g_stReset,L"Close the game first.",true); return; }
    std::string defaults;
    if(!ReadBytes(g_defaults,defaults) || defaults.empty()) { Say(g_stReset,L"EDF6VR\\EDF6VR.defaults.ini is missing.",true); return; }
    const auto old=LoadIni();
    const std::wstring backup=g_ini+L".reset-"+Stamp()+L".bak";
    if(Exists(g_ini) && !CopyFileW(g_ini.c_str(),backup.c_str(),TRUE)) { Say(g_stReset,L"Could not make the backup. Nothing was changed.",true); return; }
    auto ini=ParseIni(defaults);
    for(const auto& keep:{Setting{"Render","ForceWidth",""},Setting{"Render","ForceHeight",""},
                          Setting{"LeftHanded","LeftHanded",""},Setting{"LeftHanded","LeftHandedSticks",""}}) {
        const auto v=IniGet(old,keep.section,keep.key);
        if(v) IniSet(ini,keep.section,keep.key,*v);
    }
    if(!WriteBytes(g_ini,WriteIni(ini))) { Say(g_stReset,L"Could not write Mods\\Plugins\\EDF6VR.ini.",true); return; }
    LoadAll();
    const auto slash=backup.find_last_of(L'\\');
    Say(g_stReset,L"Done. The old file is kept as\n"+backup.substr(slash+1));
}

void StartUpdate() {
    auto note=[](const wchar_t* text,bool bad) { g_updateNote=text; g_updateNoteBad=bad; ShowVersions(); };
    if(GameRunning()) { note(L"Close the game first.",true); return; }
    const std::wstring script=g_root+L"\\EDF6VR\\Update-EDF6VR.ps1";
    if(!Exists(script)) { note(L"EDF6VR\\Update-EDF6VR.ps1 is missing. Extract the mod again.",true); return; }
    // The update may bring a new copy of this program. A running program can be
    // renamed but not overwritten, so it steps aside first.
    const std::wstring old=g_exe+L".old";
    DeleteFileW(old.c_str());
    g_renamedForUpdate=MoveFileW(g_exe.c_str(),old.c_str())!=0;
    g_task=Task::Update; g_updateResult.clear(); g_updateError.clear();
    RefreshButtons();
    note(L"Updating... please wait.",false);
    const std::wstring command=L"\""+PowerShell()+L"\" -NoLogo -NoProfile -ExecutionPolicy Bypass -File \""+script+L"\" -Yes";
    std::thread([command]() {
        const DWORD code=RunCaptured(command,g_root,nullptr,[](const std::string& s) {
            if(!Trim(s).empty()) Post(WM_APP_UPDATE_LINE,0,Wide(Trim(s),CP_OEMCP));
        });
        PostMessageW(g_window,WM_APP_UPDATE_DONE,code,0);
    }).detach();
}
void UpdateDone(DWORD code) {
    g_task=Task::None;
    const std::wstring old=g_exe+L".old";
    const bool fresh=Exists(g_exe);
    if(g_renamedForUpdate && !fresh) MoveFileW(old.c_str(),g_exe.c_str());   // nothing new came: put it back
    if(code==0 && g_renamedForUpdate && fresh) {
        g_updateNote=L"Updated. Starting the new version..."; g_updateNoteBad=false; ShowVersions();
        ShellExecuteW(nullptr,L"open",g_exe.c_str(),nullptr,g_root.c_str(),SW_SHOWNORMAL);
        DestroyWindow(g_window);
        return;
    }
    g_renamedForUpdate=false;
    if(code==0) { g_updateNote=g_updateResult.empty()?L"Done.":g_updateResult; g_updateNoteBad=false; }
    else { g_updateNote=g_updateError.empty()?L"The update did not finish. Nothing was changed.":g_updateError; g_updateNoteBad=true; }
    LoadAll();       // the versions, and that note under them
    RefreshButtons();
}

void StartHd(bool remove) {
    if(GameRunning()) { Say(g_stHd,L"Close the game first.",true); return; }
    const std::wstring python=g_root+L"\\Mods\\HDTexture\\python\\python.exe",tool=g_root+L"\\Mods\\HDTexture\\hd_textures.py";
    if(!Exists(python) || !Exists(tool)) { ShowHd(); return; }
    if(remove) {
        if(MessageBoxW(g_window,L"Delete the HD texture files?\n\nThe game goes back to its normal pictures. "
                       L"Only the files this made are deleted.\n\n高画質テクスチャを削除しますか？",L"HD textures",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
    } else if(MessageBoxW(g_window,L"Make HD textures now?\n\nIt takes about 1 hour and needs about 40 GB of free disk space.\n"
                          L"Do not start the game until it is done. You can stop it and go on later.\n\n"
                          L"高画質テクスチャを作りますか？（約1時間。終わるまでゲームを起動しないでください）",
                          L"HD textures",MB_YESNO|MB_ICONQUESTION)!=IDYES) return;
    g_task=Task::Hd; g_hdLast.clear();
    RefreshButtons();
    SendMessageW(g_pbHd,PBM_SETPOS,0,0);
    ShowWindow(g_pbHd,remove?SW_HIDE:SW_SHOW);
    Say(g_stHd,remove?L"Deleting...":L"Starting... (reading the game files)");
    const std::wstring command=L"\""+python+L"\" \""+tool+(remove?L"\" --remove":L"\" --set all");
    std::thread([command]() {
        const DWORD code=RunCaptured(command,g_root,g_job,[](const std::string& s) {
            int percent=0; std::string detail;
            if(ParseProgress(s,percent,detail)) Post(WM_APP_HD_PROGRESS,static_cast<WPARAM>(percent),Wide(detail));
            else if(!Trim(s).empty()) Post(WM_APP_HD_LINE,0,Wide(Trim(s)));
        });
        PostMessageW(g_window,WM_APP_HD_DONE,code,0);
    }).detach();
}

// Every log and settings file a problem report needs, into one ZIP here.
void CopyInto(const std::wstring& from,const std::wstring& folder) {
    std::string bytes;
    if(!ReadBytes(from,bytes)) return;
    const auto slash=from.find_last_of(L'\\');
    WriteBytes(folder+L"\\"+from.substr(slash+1),bytes);
}
void StartZip() {
    const std::wstring stamp=Stamp();
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH,temp);
    const std::wstring folder=std::wstring(temp)+L"EDF6VR-logs-"+stamp;
    const std::wstring zip=g_root+L"\\EDF6VR-logs-"+stamp+L".zip";
    if(!CreateDirectoryW(folder.c_str(),nullptr)) { Say(g_stLogs,L"Could not make a work folder.",true); return; }
    std::string info="EDF6 VR setting - problem report\r\n";
    info+="Installed: EDF6VR "+(g_installed.Valid()?ToString(g_installed):std::string("unknown"))+"\r\n";
    bool vr=false,missing=false;
    info+="Mode: "+Narrow(ModeNow(vr,missing))+"\r\n";
    info+="Game running: "+std::string(GameRunning()?"yes":"no")+"\r\n";
    info+="HD textures: "+Narrow(Text(g_stHd))+"\r\n\r\nMods\\Plugins:\r\n";
    WIN32_FIND_DATAW found{};
    for(const wchar_t* pattern:{L"\\*"}) {
        HANDLE search=FindFirstFileW((g_plugins+pattern).c_str(),&found);
        if(search==INVALID_HANDLE_VALUE) continue;
        do {
            if(found.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
            SYSTEMTIME t{}; FileTimeToSystemTime(&found.ftLastWriteTime,&t);
            char line[512]{};
            std::snprintf(line,sizeof(line),"  %-48s %12llu  %04u-%02u-%02u %02u:%02u\r\n",Narrow(found.cFileName).c_str(),
                (static_cast<unsigned long long>(found.nFileSizeHigh)<<32)|found.nFileSizeLow,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute);
            info+=line;
            const std::wstring name=found.cFileName;
            const auto dot=name.find_last_of(L'.');
            const std::wstring ext=dot==std::wstring::npos?L"":name.substr(dot);
            if(!_wcsicmp(ext.c_str(),L".log") || !_wcsicmp(ext.c_str(),L".ini") || !_wcsicmp(ext.c_str(),L".fit"))
                CopyInto(g_plugins+L"\\"+name,folder);
        } while(FindNextFileW(search,&found));
        FindClose(search);
    }
    for(const wchar_t* name:{L"\\ModLoader.log",L"\\ModLoader.ini",L"\\Patcher.log",L"\\EDF6VR\\PACKAGE_MANIFEST.json"})
        CopyInto(g_root+name,folder);
    WriteBytes(folder+L"\\info.txt",info);
    g_task=Task::Zip;
    RefreshButtons();
    Say(g_stLogs,L"Making the zip...");
    auto quote=[](std::wstring s) { for(size_t i=s.find(L'\'');i!=std::wstring::npos;i=s.find(L'\'',i+2)) s.insert(i,1,L'\''); return L"'"+s+L"'"; };
    const std::wstring command=L"\""+PowerShell()+L"\" -NoLogo -NoProfile -ExecutionPolicy Bypass -Command \"Compress-Archive -Path "
        +quote(folder+L"\\*")+L" -DestinationPath "+quote(zip)+L" -Force\"";
    std::thread([command,folder,zip]() {
        const DWORD code=RunCaptured(command,g_root,nullptr,[](const std::string&) {});
        WIN32_FIND_DATAW f{};
        HANDLE search=FindFirstFileW((folder+L"\\*").c_str(),&f);
        if(search!=INVALID_HANDLE_VALUE) {
            do { if(!(f.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((folder+L"\\"+f.cFileName).c_str()); } while(FindNextFileW(search,&f));
            FindClose(search);
        }
        RemoveDirectoryW(folder.c_str());
        Post(WM_APP_ZIP_DONE,code,zip);
    }).detach();
}

// ---- the window ----------------------------------------------------------------
void Build() {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0);
    LOGFONTW face=metrics.lfMessageFont;
    face.lfHeight=-MulDiv(9,g_dpi,72);
    g_font=CreateFontIndirectW(&face);
    face.lfWeight=FW_BOLD;
    g_bold=CreateFontIndirectW(&face);

    g_tabs=CreateWindowExW(0,WC_TABCONTROLW,L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS|WS_TABSTOP,S(8),S(8),S(kWidth-16),S(kTop+7*kRowHeight),
                           g_window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABS)),g_instance,nullptr);
    SendMessageW(g_tabs,WM_SETFONT,reinterpret_cast<WPARAM>(g_font),TRUE);
    TCITEMW item{TCIF_TEXT};
    item.pszText=const_cast<wchar_t*>(L"Main"); SendMessageW(g_tabs,TCM_INSERTITEMW,0,reinterpret_cast<LPARAM>(&item));
    item.pszText=const_cast<wchar_t*>(L"Extra VR settings"); SendMessageW(g_tabs,TCM_INSERTITEMW,1,reinterpret_cast<LPARAM>(&item));

    // Main
    g_tab=0; g_y=kTop;
    g_stUpdate=BeginRow(L"Update",L"Gets the newest version of this mod.\nYour settings are kept.\n最新版に更新します（設定はそのまま）",4,290);
    g_btUpdate=Button(L"Update now",ID_UPDATE,kApplyX-20,100); EndRow();

    g_stMode=BeginRow(L"VR mode",L"VR: play in your VR headset.\nNormal: play on your monitor, without VR.\nVRで遊ぶか、普通にモニターで遊ぶか");
    g_rbVr=Radio(L"VR",ID_MODE_VR,kRight,60); g_rbFlat=Radio(L"Normal",ID_MODE_FLAT,kRight+64,90);
    Button(L"Apply",ID_MODE_APPLY); EndRow();

    g_stSize=BeginRow(L"Picture size",L"Bigger is sharper, but gives lower FPS.\nLow 0.8, Normal 1.0, High 1.25, Custom 0.5-2\n解像度。大きいほど綺麗で重くなります");
    g_rbLow=Radio(L"Low",ID_SIZE_LOW,kRight,52); g_rbNormal=Radio(L"Normal",ID_SIZE_NORMAL,kRight+54,68);
    g_rbHigh=Radio(L"High",ID_SIZE_HIGH,kRight+124,54); g_rbCustom=Radio(L"Custom",ID_SIZE_CUSTOM,kRight+180,70);
    g_edSize=Edit(kRight+252,52); Button(L"Apply",ID_SIZE_APPLY); EndRow();

    g_stHd=BeginRow(L"HD textures",L"Makes walls, weapons and enemies 2x sharper.\nTakes about 1 hour and 40 GB of disk.\n高画質化（約1時間・空き容量40GB）",2,226);
    g_pbHd=Part(PROGRESS_CLASSW,L"",0,kRight,g_y+44,226,14);
    g_btHdMake=Button(L"Make",ID_HD_MAKE,kApplyX-88,80); g_btHdDelete=Button(L"Delete",ID_HD_DELETE); EndRow();

    g_stHand=BeginRow(L"Gun hand",L"The hand that holds and fires the gun.\nRanger, Wing Diver, Air Raider. Not Fencer.\n銃を持つ手（フェンサー・乗り物は除く）");
    g_cbRight=Check(L"Right",ID_HAND_RIGHT,kRight,70,false); g_cbLeft=Check(L"Left",ID_HAND_LEFT,kRight+74,70,false);
    Button(L"Apply",ID_HAND_APPLY); EndRow();

    g_stLogs=BeginRow(L"Problem report",L"Puts the logs into one zip for a bug report.\nSaved in the game folder, next to this program.\n報告用zip。このexeと同じフォルダに出ます",2,300);
    g_btLogs=Button(L"Make zip",ID_LOGS); EndRow();

    // Extra VR settings
    g_tab=1; g_y=kTop;
    g_stCockpit=BeginRow(L"VR cockpit",L"Shows a cockpit around you\nwhen you ride a vehicle.\n乗り物にコクピットを表示");
    g_cbCockpit=Check(L"On",ID_COCKPIT_ON,kRight,60,true); Button(L"Apply",ID_COCKPIT_APPLY); EndRow();

    g_stHud=BeginRow(L"Compact HUD",L"Radar, armor and weapons in one small box,\nin the corner of your view or on a wrist.\n小さなHUD（視界の隅か手首に表示）");
    g_cbHud=Check(L"On",ID_HUD_ON,kRight,50,true); g_rbCorner=Radio(L"Corner",ID_HUD_CORNER,kRight+54,70);
    g_rbRWrist=Radio(L"Right wrist",ID_HUD_RWRIST,kRight+126,90); g_rbLWrist=Radio(L"Left wrist",ID_HUD_LWRIST,kRight+218,84);
    Button(L"Apply",ID_HUD_APPLY); EndRow();

    g_stReticle=BeginRow(L"Aim mark size",L"The size of the aim mark (reticle).\n0.5 is normal. Bigger number, bigger mark.\n照準の大きさ（0.5が標準）");
    g_edReticle=Edit(kRight,60); Button(L"Apply",ID_RETICLE_APPLY); EndRow();

    g_stRecoil=BeginRow(L"Recoil",L"The gun kicks back in your hand\nwhen you shoot.\n撃つと銃が反動で動きます");
    g_cbRecoil=Check(L"On",ID_RECOIL_ON,kRight,60,true); Button(L"Apply",ID_RECOIL_APPLY); EndRow();

    g_stBuzz=BeginRow(L"Shot vibration",L"How much the controller shakes when you shoot.\n0 = off, 0.5 = normal, 1 = strong\n射撃時の振動の強さ（0でオフ）");
    g_edBuzz=Edit(kRight,60); Button(L"Apply",ID_BUZZ_APPLY); EndRow();

    g_stMirror=BeginRow(L"Desktop mirror",L"Also shows the game on your monitor in VR.\nGood for recording and streaming.\nVR中もモニターに映します（配信向け）");
    g_cbMirror=Check(L"On",ID_MIRROR_ON,kRight,60,true); Button(L"Apply",ID_MIRROR_APPLY); EndRow();

    g_stReset=BeginRow(L"Reset settings",L"Puts all VR settings back to how they came.\nYour picture size and gun hand stay.\n設定を初期値に戻す（解像度と利き手は維持）",2,300);
    Button(L"Reset",ID_RESET); EndRow();

    g_rows.push_back({-1,{}});
    g_footer=Part(L"STATIC",L"",SS_LEFT|WS_VISIBLE,16,kTop+7*kRowHeight+16,kWidth-32,20);
    SetWindowPos(g_tabs,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}

void OnCommand(int id) {
    switch(id) {
    case ID_UPDATE: StartUpdate(); break;
    case ID_MODE_VR: case ID_MODE_FLAT: Pick({g_rbVr,g_rbFlat},GetDlgItem(g_window,id)); break;
    case ID_MODE_APPLY: ApplyMode(); break;
    case ID_SIZE_LOW: case ID_SIZE_NORMAL: case ID_SIZE_HIGH: case ID_SIZE_CUSTOM:
        Pick({g_rbLow,g_rbNormal,g_rbHigh,g_rbCustom},GetDlgItem(g_window,id)); break;
    case ID_SIZE_APPLY: ApplySize(); break;
    case ID_HD_MAKE: StartHd(false); break;
    case ID_HD_DELETE: StartHd(true); break;
    case ID_HAND_RIGHT: case ID_HAND_LEFT: Pick({g_cbRight,g_cbLeft},GetDlgItem(g_window,id)); break;
    case ID_HAND_APPLY: Save(g_stHand,{{"LeftHanded","LeftHanded",Checked(g_cbLeft)?"1":"0"}}); break;
    case ID_LOGS: StartZip(); break;
    case ID_COCKPIT_APPLY: Save(g_stCockpit,{{"VR","VehicleCockpit",Checked(g_cbCockpit)?"1":"0"}}); break;
    case ID_HUD_CORNER: case ID_HUD_RWRIST: case ID_HUD_LWRIST: Pick({g_rbCorner,g_rbRWrist,g_rbLWrist},GetDlgItem(g_window,id)); break;
    case ID_HUD_APPLY:
        Save(g_stHud,{{"Render","UiCluster",Checked(g_cbHud)?"1":"0"},
                      {"Render","UiClusterPlace",Checked(g_rbRWrist)?"1":Checked(g_rbLWrist)?"2":"0"}}); break;
    case ID_RETICLE_APPLY: ApplyNumber(g_stReticle,g_edReticle,"Render","ReticleScale",0.05,4.0); break;
    case ID_RECOIL_APPLY: Save(g_stRecoil,{{"VR","RecoilKick",Checked(g_cbRecoil)?"1":"0"}}); break;
    case ID_BUZZ_APPLY: ApplyNumber(g_stBuzz,g_edBuzz,"VR","ShotBuzz",0.0,1.0); break;
    case ID_MIRROR_APPLY: Save(g_stMirror,{{"Render","DesktopMirror",Checked(g_cbMirror)?"1":"0"}}); break;
    case ID_RESET: Reset(); break;
    default: break;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CREATE:
        g_window=hwnd;
        Build();
        LoadAll();
        g_gameRunning=GameRunning();
        ShowTab(0);
        RefreshButtons();
        SetTimer(hwnd,kGameTimer,1500,nullptr);
        std::thread([]() { auto* v=new std::string(LatestVersionFromGitHub()); PostMessageW(g_window,WM_APP_LATEST,0,reinterpret_cast<LPARAM>(v)); }).detach();
        return 0;
    case WM_TIMER:
        if(wParam==kGameTimer) {
            const bool running=GameRunning();
            if(running!=g_gameRunning) { g_gameRunning=running; RefreshButtons(); if(!running) LoadAll(); }
        }
        return 0;
    case WM_NOTIFY: {
        const auto* header=reinterpret_cast<const NMHDR*>(lParam);
        if(header->hwndFrom==g_tabs && header->code==TCN_SELCHANGE)
            ShowTab(static_cast<int>(SendMessageW(g_tabs,TCM_GETCURSEL,0,0)));
        return 0;
    }
    case WM_COMMAND:
        if(HIWORD(wParam)==BN_CLICKED) OnCommand(LOWORD(wParam));
        else if(HIWORD(wParam)==EN_SETFOCUS && reinterpret_cast<HWND>(lParam)==g_edSize) Pick({g_rbLow,g_rbNormal,g_rbHigh,g_rbCustom},g_rbCustom);
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc=reinterpret_cast<HDC>(wParam);
        COLORREF colour=GetSysColor(COLOR_WINDOWTEXT);
        for(const auto& entry:g_colours) if(entry.first==reinterpret_cast<HWND>(lParam)) colour=entry.second;
        SetTextColor(dc,colour);
        SetBkColor(dc,RGB(255,255,255));
        return reinterpret_cast<LRESULT>(g_white);
    }
    case WM_APP_LATEST: {
        std::unique_ptr<std::string> v(reinterpret_cast<std::string*>(lParam));
        g_latest=ParseVersion(*v); g_latestChecked=true;
        ShowVersions(); RefreshButtons();
        return 0;
    }
    case WM_APP_UPDATE_LINE: {
        std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lParam));
        // Each step as the updater says it. The release notes it prints at the
        // end are not shown: the result line stays.
        const bool bad=line->rfind(L"ERROR:",0)==0;
        if(bad) g_updateError=line->substr(line->find_first_not_of(L' ',6));
        const bool result=line->rfind(L"Updated to EDF6VR",0)==0 || line->rfind(L"Already up to date",0)==0;
        if(result) g_updateResult=*line;
        if(result || bad || g_updateResult.empty()) { g_updateNote=bad?g_updateError:*line; g_updateNoteBad=bad; ShowVersions(); }
        return 0;
    }
    case WM_APP_UPDATE_DONE: UpdateDone(static_cast<DWORD>(wParam)); return 0;
    case WM_APP_HD_PROGRESS: {
        std::unique_ptr<std::wstring> detail(reinterpret_cast<std::wstring*>(lParam));
        SendMessageW(g_pbHd,PBM_SETPOS,wParam,0);
        wchar_t text[64]{}; swprintf_s(text,L"Making: %d%%\n",static_cast<int>(wParam));
        Say(g_stHd,text+*detail);
        return 0;
    }
    case WM_APP_HD_LINE: {
        std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(lParam));
        g_hdLast=*line;
        return 0;
    }
    case WM_APP_HD_DONE:
        g_task=Task::None;
        ShowWindow(g_pbHd,SW_HIDE);
        RefreshButtons();
        ShowHd();
        if(wParam!=0 && !g_hdLast.empty()) Say(g_stHd,L"Stopped:\n"+g_hdLast,true);
        return 0;
    case WM_APP_ZIP_DONE: {
        std::unique_ptr<std::wstring> zip(reinterpret_cast<std::wstring*>(lParam));
        g_task=Task::None;
        RefreshButtons();
        if(wParam==0 && Exists(*zip)) {
            Say(g_stLogs,L"Saved in the game folder:\n"+zip->substr(zip->find_last_of(L'\\')+1));
            ShellExecuteW(nullptr,L"open",L"explorer.exe",(L"/select,\""+*zip+L"\"").c_str(),nullptr,SW_SHOWNORMAL);
        } else Say(g_stLogs,L"Could not make the zip.",true);
        return 0;
    }
    case WM_CLOSE:
        if(g_task==Task::Update) { MessageBoxW(hwnd,L"Please wait until the update is done.\n\n更新が終わるまでお待ちください。",L"EDF6 VR setting",MB_ICONINFORMATION); return 0; }
        if(g_task==Task::Hd && MessageBoxW(hwnd,L"HD textures are still being made.\n\nStop now? You can go on later from where it stopped.\n\n作成を中断しますか？（後で続きから再開できます）",
                                           L"EDF6 VR setting",MB_YESNO|MB_ICONQUESTION)!=IDYES) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd,kGameTimer);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd,message,wParam,lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    g_instance=instance;
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr,path,MAX_PATH);
    g_exe=path;
    g_root=g_exe.substr(0,g_exe.find_last_of(L'\\'));
    g_plugins=g_root+L"\\Mods\\Plugins";
    g_ini=g_plugins+L"\\EDF6VR.ini";
    g_defaults=g_root+L"\\EDF6VR\\EDF6VR.defaults.ini";
    if(!Exists(g_root+L"\\EDF6.exe")) {
        MessageBoxW(nullptr,L"Put this program in your EARTH DEFENSE FORCE 6 folder, next to EDF6.exe.\n\n"
                    L"(Extract everything in the mod's ZIP file into that folder.)\n\nEDF6.exe と同じフォルダに置いてください。",L"EDF6 VR setting",MB_ICONWARNING);
        return 1;
    }
    // Left behind by an update that replaced this program; and the .bat files it replaces.
    for(const wchar_t* name:{L".old"}) DeleteFileW((g_exe+name).c_str());
    for(const wchar_t* name:{L"\\VR_Play.bat",L"\\Set_Resolution.bat",L"\\HD_Texture_2x.bat"}) DeleteFileW((g_root+name).c_str());

    g_job=CreateJobObjectW(nullptr,nullptr);
    if(g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES|ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    HDC screen=GetDC(nullptr);
    g_dpi=GetDeviceCaps(screen,LOGPIXELSY);
    ReleaseDC(nullptr,screen);
    g_white=CreateSolidBrush(RGB(255,255,255));

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc=WindowProc;
    windowClass.hInstance=instance;
    windowClass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));
    windowClass.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(1),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0));
    windowClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    windowClass.hbrBackground=g_white;
    windowClass.lpszClassName=L"EDF6VRSettings";
    RegisterClassExW(&windowClass);
    const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT area{0,0,S(kWidth),S(kTop+7*kRowHeight+48)};
    AdjustWindowRectEx(&area,style,FALSE,WS_EX_CONTROLPARENT);
    HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,windowClass.lpszClassName,L"EDF6 VR setting",style,CW_USEDEFAULT,CW_USEDEFAULT,
                                area.right-area.left,area.bottom-area.top,nullptr,nullptr,instance,nullptr);
    if(!window) return 1;
    ShowWindow(window,show);
    UpdateWindow(window);
    MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) {
        if(IsDialogMessageW(window,&message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if(g_job) CloseHandle(g_job);   // stops an HD build still running
    return 0;
}
