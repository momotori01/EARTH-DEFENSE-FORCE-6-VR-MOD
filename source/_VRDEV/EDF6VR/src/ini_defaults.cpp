#include "ini_defaults.h"
#include <Windows.h>
#include <cstring>
#include <string>

namespace edf6vr {
namespace {
std::wstring Widen(const char* text,std::size_t length) {
    if(!length) return {};
    const int size=MultiByteToWideChar(CP_UTF8,0,text,static_cast<int>(length),nullptr,0);
    std::wstring out(static_cast<std::size_t>(size>0?size:0),L'\0');
    if(size>0) MultiByteToWideChar(CP_UTF8,0,text,static_cast<int>(length),out.data(),size);
    return out;
}
std::string Trim(const std::string& s) {
    const auto first=s.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return {};
    const auto last=s.find_last_not_of(" \t\r\n");
    return s.substr(first,last-first+1);
}
}

IniResetResult ResetOlderIni(const char* defaults,std::size_t length,const wchar_t* iniPath,int revision) noexcept {
    IniResetResult result{};
    if(!defaults || !length || !iniPath || !iniPath[0]) { result.failed=true; return result; }
    try {
        if(GetFileAttributesW(iniPath)==INVALID_FILE_ATTRIBUTES) return result;   // a new file is simply written
        result.from=static_cast<int>(GetPrivateProfileIntW(L"Settings",L"Revision",0,iniPath));
        if(result.from>=revision) return result;
        wchar_t width[32]{},height[32]{};
        GetPrivateProfileStringW(L"Render",L"ForceWidth",L"",width,32,iniPath);
        GetPrivateProfileStringW(L"Render",L"ForceHeight",L"",height,32,iniPath);
        std::wstring backup=iniPath;
        backup+=L".v"+std::to_wstring(result.from<1?1:result.from)+L".bak";
        if(!CopyFileW(iniPath,backup.c_str(),FALSE)) { result.failed=true; return result; }
        HANDLE file=CreateFileW(iniPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) { result.failed=true; return result; }
        DWORD written=0;
        const bool ok=WriteFile(file,defaults,static_cast<DWORD>(length),&written,nullptr) && written==length;
        CloseHandle(file);
        if(!ok) { result.failed=true; return result; }
        result.reset=true;
        if(width[0] && height[0]) {
            result.keptResolution=WritePrivateProfileStringW(L"Render",L"ForceWidth",width,iniPath)
                && WritePrivateProfileStringW(L"Render",L"ForceHeight",height,iniPath);
        }
    } catch(...) { result.failed=true; }
    return result;
}

IniMergeResult MergeIniDefaults(const char* defaults,std::size_t length,const wchar_t* iniPath) noexcept {
    IniMergeResult result{};
    if(!defaults || !length || !iniPath || !iniPath[0]) { result.failed=true; return result; }
    try {
        if(GetFileAttributesW(iniPath)==INVALID_FILE_ATTRIBUTES) {
            HANDLE file=CreateFileW(iniPath,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE) { result.failed=true; return result; }
            DWORD written=0;
            const bool ok=WriteFile(file,defaults,static_cast<DWORD>(length),&written,nullptr) && written==length;
            CloseHandle(file);
            result.created=ok; result.failed=!ok;
            return result;
        }
        constexpr wchar_t kMissing[]=L"\x01missing\x01";
        std::string section;
        std::size_t at=0;
        while(at<length) {
            std::size_t end=at;
            while(end<length && defaults[end]!='\n') ++end;
            const std::string line=Trim(std::string(defaults+at,end-at));
            at=end+1;
            if(line.empty() || line[0]==';' || line[0]=='#') continue;
            if(line.front()=='[') {
                const auto close=line.find(']');
                section=close==std::string::npos?std::string():line.substr(1,close-1);
                continue;
            }
            const auto equals=line.find('=');
            if(section.empty() || equals==std::string::npos || equals==0) continue;
            const auto key=Trim(line.substr(0,equals)), value=Trim(line.substr(equals+1));
            const auto wSection=Widen(section.data(),section.size()), wKey=Widen(key.data(),key.size());
            wchar_t current[64]{};
            GetPrivateProfileStringW(wSection.c_str(),wKey.c_str(),kMissing,current,64,iniPath);
            if(std::wcscmp(current,kMissing)) continue;   // the player's value stays
            const auto wValue=Widen(value.data(),value.size());
            if(WritePrivateProfileStringW(wSection.c_str(),wKey.c_str(),wValue.c_str(),iniPath)) ++result.added;
            else result.failed=true;
        }
    } catch(...) { result.failed=true; }
    return result;
}
}
