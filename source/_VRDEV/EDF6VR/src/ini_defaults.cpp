#include "ini_defaults.h"
#include <Windows.h>
#include <cstring>
#include <string>
#include <vector>

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
// A file as lines, each without its line end (kept beside it, so a rewrite
// leaves every line as it was), and without a UTF-8 BOM (remembered).
struct Lines { std::vector<std::string> text,ends; bool bom=false; std::string eol="\r\n"; };
Lines Split(const char* data,std::size_t length) {
    Lines out;
    std::size_t at=0;
    if(length>=3 && !std::memcmp(data,"\xEF\xBB\xBF",3)) { out.bom=true; at=3; }
    const char* newline=static_cast<const char*>(std::memchr(data+at,'\n',length-at));
    if(newline && !(newline>data+at && newline[-1]=='\r')) out.eol="\n";
    while(at<length) {
        std::size_t end=at;
        while(end<length && data[end]!='\n') ++end;
        std::size_t stop=end;
        if(stop>at && data[stop-1]=='\r') --stop;
        out.text.emplace_back(data+at,stop-at);
        out.ends.emplace_back(data+stop,(end<length?end+1:end)-stop);
        at=end+1;
    }
    return out;
}
std::string SectionOf(const std::string& line) {
    const auto t=Trim(line);
    if(t.size()<3 || t.front()!='[') return {};
    const auto close=t.find(']');
    return close==std::string::npos?std::string():t.substr(1,close-1);
}
bool IsComment(const std::string& line) {
    const auto t=Trim(line);
    return !t.empty() && (t[0]==';' || t[0]=='#');
}
bool IsKey(const std::string& line) {
    const auto t=Trim(line);
    const auto equals=t.find('=');
    return !t.empty() && !IsComment(t) && t.front()!='[' && equals!=std::string::npos && equals>0;
}

// A section the file lacks altogether goes in whole, placed where the defaults
// have it: the blank lines and comments leading into it, its keys, and the blank
// lines after them, above the comments that lead into the section following it
// there (as many of those lines as the file shares), or at the end of the file
// when it has no such section. Key by key (MergeIniDefaults) a new section would
// land at the bottom with no comments, where nobody finds it -- [LeftHanded] is
// meant to be seen as soon as the file is opened.
void InsertMissingSections(const char* defaults,std::size_t length,const wchar_t* iniPath,IniMergeResult& result) {
    std::string raw;
    {
        HANDLE file=CreateFileW(iniPath,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) { result.failed=true; return; }
        LARGE_INTEGER size{};
        bool ok=GetFileSizeEx(file,&size) && size.QuadPart<(64ll<<20);
        if(ok) {
            raw.resize(static_cast<std::size_t>(size.QuadPart));
            DWORD read=0;
            ok=raw.empty() || (ReadFile(file,raw.data(),static_cast<DWORD>(raw.size()),&read,nullptr) && read==raw.size());
        }
        CloseHandle(file);
        if(!ok) { result.failed=true; return; }
    }
    Lines file=Split(raw.data(),raw.size());
    const Lines shipped=Split(defaults,length);
    const auto& d=shipped.text;
    auto& f=file.text;
    auto find=[&f](const std::string& name) {
        for(std::size_t i=0;i<f.size();++i) if(!_stricmp(SectionOf(f[i]).c_str(),name.c_str())) return i;
        return std::string::npos;
    };
    unsigned sections=0,keys=0;
    for(std::size_t h=0;h<d.size();++h) {
        const auto name=SectionOf(d[h]);
        if(name.empty() || find(name)!=std::string::npos) continue;
        std::size_t first=h;                                   // the comments leading in...
        while(first>0 && IsComment(d[first-1])) --first;
        while(first>0 && Trim(d[first-1]).empty()) --first;    // ...and the blank lines above them
        std::size_t next=h+1;                                  // the section that follows
        while(next<d.size() && SectionOf(d[next]).empty()) ++next;
        std::size_t end=next;                                  // this block ends where its lead-in starts
        if(next<d.size()) while(end>h+1 && IsComment(d[end-1])) --end;
        std::size_t at=f.size();
        const auto there=next<d.size()?find(SectionOf(d[next])):std::string::npos;
        if(there!=std::string::npos) {
            at=there;
            for(std::size_t k=next;k>end && at>0 && Trim(f[at-1])==Trim(d[k-1]);--k) --at;
        }
        if(at==f.size() && at>0 && file.ends[at-1].empty()) file.ends[at-1]=file.eol;   // a last line without an end
        f.insert(f.begin()+static_cast<std::ptrdiff_t>(at),d.begin()+static_cast<std::ptrdiff_t>(first),
                 d.begin()+static_cast<std::ptrdiff_t>(end));
        file.ends.insert(file.ends.begin()+static_cast<std::ptrdiff_t>(at),end-first,file.eol);
        ++sections;
        for(std::size_t i=h+1;i<end;++i) if(IsKey(d[i])) ++keys;
    }
    if(!sections) return;
    std::string out=file.bom?"\xEF\xBB\xBF":"";
    for(std::size_t i=0;i<f.size();++i) { out+=f[i]; out+=file.ends[i]; }
    // Written beside it and moved over it, so a failed write leaves the old file.
    const std::wstring temporary=std::wstring(iniPath)+L".new";
    HANDLE file2=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file2==INVALID_HANDLE_VALUE) { result.failed=true; return; }
    DWORD written=0;
    const bool ok=WriteFile(file2,out.data(),static_cast<DWORD>(out.size()),&written,nullptr) && written==out.size();
    CloseHandle(file2);
    if(!ok || !MoveFileExW(temporary.c_str(),iniPath,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        result.failed=true; return;
    }
    result.sections=sections;
    result.added+=keys;
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
        InsertMissingSections(defaults,length,iniPath,result);
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
