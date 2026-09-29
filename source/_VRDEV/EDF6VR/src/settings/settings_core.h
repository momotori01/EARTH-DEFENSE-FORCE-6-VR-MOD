#pragma once
// The parts of "EDF6 VR setting.exe" that need no window, kept here so they
// can be tested: EDF6VR.ini edited line by line (every comment and every other
// line stays byte for byte, as set_resolution.py does), version numbers, the
// picture size, and the HD texture builder's progress line.
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace edf6vr::settings {

inline std::string Trim(const std::string& s) {
    const auto first=s.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return {};
    const auto last=s.find_last_not_of(" \t\r\n");
    return s.substr(first,last-first+1);
}
inline bool SameText(const std::string& a,const std::string& b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) {
        char x=a[i],y=b[i];
        if(x>='A' && x<='Z') x=static_cast<char>(x-'A'+'a');
        if(y>='A' && y<='Z') y=static_cast<char>(y-'A'+'a');
        if(x!=y) return false;
    }
    return true;
}

// ---- EDF6VR.ini, line by line -------------------------------------------
struct IniText {
    std::string bom;                   // a UTF-8 BOM the file started with, kept
    std::vector<std::string> lines;    // without their ends
    std::vector<std::string> ends;     // each line's own end: "\r\n", "\n" or "" (last)
    std::string eol="\r\n";            // for lines added
};
inline IniText ParseIni(const std::string& bytes) {
    IniText ini;
    size_t at=0;
    if(bytes.size()>=3 && bytes.compare(0,3,"\xEF\xBB\xBF")==0) { ini.bom=bytes.substr(0,3); at=3; }
    const auto newline=bytes.find('\n',at);
    if(newline!=std::string::npos && !(newline>at && bytes[newline-1]=='\r')) ini.eol="\n";
    while(at<bytes.size()) {
        size_t end=bytes.find('\n',at);
        const size_t stop=end==std::string::npos?bytes.size():end;
        size_t text=stop;
        if(text>at && bytes[text-1]=='\r') --text;
        ini.lines.push_back(bytes.substr(at,text-at));
        ini.ends.push_back(end==std::string::npos?std::string():bytes.substr(text,end+1-text));
        at=end==std::string::npos?bytes.size():end+1;
    }
    return ini;
}
inline std::string WriteIni(const IniText& ini) {
    std::string out=ini.bom;
    for(size_t i=0;i<ini.lines.size();++i) { out+=ini.lines[i]; out+=ini.ends[i]; }
    return out;
}
inline std::string SectionName(const std::string& line) {
    const auto t=Trim(line);
    if(t.size()<3 || t.front()!='[') return {};
    const auto close=t.find(']');
    return close==std::string::npos?std::string():t.substr(1,close-1);
}
inline bool IsIniComment(const std::string& line) {
    const auto t=Trim(line);
    return !t.empty() && (t[0]==';' || t[0]=='#');
}
// The key of a "key=value" line, or "" for anything else.
inline std::string KeyName(const std::string& line) {
    if(IsIniComment(line) || !SectionName(line).empty()) return {};
    const auto equals=line.find('=');
    return equals==std::string::npos?std::string():Trim(line.substr(0,equals));
}
// Where a section starts (its header line) and ends (the next header), as the
// game's GetPrivateProfile* reads it: the first section of that name.
inline bool FindSection(const IniText& ini,const std::string& section,size_t& header,size_t& end) {
    for(size_t i=0;i<ini.lines.size();++i) {
        if(!SameText(SectionName(ini.lines[i]),section)) continue;
        header=i; end=i+1;
        while(end<ini.lines.size() && SectionName(ini.lines[end]).empty()) ++end;
        return true;
    }
    return false;
}
inline std::optional<std::string> IniGet(const IniText& ini,const std::string& section,const std::string& key) {
    size_t header=0,end=0;
    if(!FindSection(ini,section,header,end)) return std::nullopt;
    for(size_t i=header+1;i<end;++i) {
        if(!SameText(KeyName(ini.lines[i]),key)) continue;
        return Trim(ini.lines[i].substr(ini.lines[i].find('=')+1));
    }
    return std::nullopt;
}
// Replaces the key's line; a missing key goes after the section's last key, a
// missing section at the end of the file.
inline void IniSet(IniText& ini,const std::string& section,const std::string& key,const std::string& value) {
    const std::string line=key+"="+value;
    size_t header=0,end=0;
    if(!FindSection(ini,section,header,end)) {
        if(!ini.lines.empty() && ini.ends.back().empty()) ini.ends.back()=ini.eol;
        if(!ini.lines.empty() && !Trim(ini.lines.back()).empty()) { ini.lines.push_back(""); ini.ends.push_back(ini.eol); }
        ini.lines.push_back("["+section+"]"); ini.ends.push_back(ini.eol);
        ini.lines.push_back(line); ini.ends.push_back(ini.eol);
        return;
    }
    size_t lastKey=header;
    for(size_t i=header+1;i<end;++i) {
        const auto name=KeyName(ini.lines[i]);
        if(SameText(name,key)) { ini.lines[i]=line; return; }
        if(!name.empty()) lastKey=i;
    }
    if(ini.ends[lastKey].empty()) ini.ends[lastKey]=ini.eol;
    ini.lines.insert(ini.lines.begin()+static_cast<std::ptrdiff_t>(lastKey)+1,line);
    ini.ends.insert(ini.ends.begin()+static_cast<std::ptrdiff_t>(lastKey)+1,ini.eol);
}

// ---- versions ------------------------------------------------------------
struct Version { int major=-1,minor=0,patch=0; bool Valid() const { return major>=0; } };
inline bool operator<(const Version& a,const Version& b) {
    if(a.major!=b.major) return a.major<b.major;
    if(a.minor!=b.minor) return a.minor<b.minor;
    return a.patch<b.patch;
}
inline std::string ToString(const Version& v) {
    return v.Valid()?std::to_string(v.major)+"."+std::to_string(v.minor)+"."+std::to_string(v.patch):std::string();
}
// "x.y.z" at `at`, digits only; `used` is how many characters it took.
inline Version ParseVersionAt(const std::string& s,size_t at,size_t* used=nullptr) {
    Version v; int parts[3]{}; size_t i=at;
    for(int p=0;p<3;++p) {
        const size_t start=i;
        while(i<s.size() && s[i]>='0' && s[i]<='9' && i-start<6) ++i;
        if(i==start) return {};
        parts[p]=std::atoi(s.substr(start,i-start).c_str());
        if(p<2) { if(i>=s.size() || s[i]!='.') return {}; ++i; }
    }
    v.major=parts[0]; v.minor=parts[1]; v.patch=parts[2];
    if(used) *used=i-at;
    return v;
}
inline Version ParseVersion(const std::string& s) {
    size_t used=0;
    const auto v=ParseVersionAt(Trim(s),0,&used);
    return used==Trim(s).size()?v:Version{};
}
// The value of "name": "..." in a JSON text (enough for the two fields read here).
inline std::string JsonString(const std::string& json,const std::string& name) {
    const auto key=json.find("\""+name+"\"");
    if(key==std::string::npos) return {};
    auto colon=json.find(':',key+name.size()+2);
    if(colon==std::string::npos) return {};
    const auto open=json.find('"',colon+1);
    if(open==std::string::npos || Trim(json.substr(colon+1,open-colon-1))!="") return {};
    const auto close=json.find('"',open+1);
    return close==std::string::npos?std::string():json.substr(open+1,close-open-1);
}
// EDF6VR\PACKAGE_MANIFEST.json's "version".
inline Version ManifestVersion(const std::string& json) { return ParseVersion(JsonString(json,"version")); }
// The newest release, from GitHub's releases/latest answer ("tag_name": "EDF6VR-x.y.z").
inline Version LatestReleaseVersion(const std::string& json) {
    const auto tag=JsonString(json,"tag_name");
    return tag.rfind("EDF6VR-",0)==0?ParseVersion(tag.substr(7)):Version{};
}
// The version in the DLL's loading line, "EDF6VR x.y.z cockpit loading".
inline Version DllVersion(const std::string& bytes) {
    const std::string head="EDF6VR ",tail=" cockpit loading";
    for(size_t at=bytes.find(head);at!=std::string::npos;at=bytes.find(head,at+1)) {
        size_t used=0;
        const auto v=ParseVersionAt(bytes,at+head.size(),&used);
        if(v.Valid() && bytes.compare(at+head.size()+used,tail.size(),tail)==0) return v;
    }
    return {};
}

// ---- picture size (as set_resolution.py) ---------------------------------
constexpr int kBaseWidth=3840, kBaseHeight=2160;
constexpr double kLeastScale=0.5, kMostScale=2.0;
// Both sides move together, rounded like Python's round() (half to even).
inline bool SizeForScale(double scale,int& width,int& height) {
    if(!(scale>=kLeastScale-1e-9 && scale<=kMostScale+1e-9)) return false;
    width=static_cast<int>(std::nearbyint(kBaseWidth*scale/8.0))*8;
    height=static_cast<int>(std::nearbyint(width*9.0/16.0));
    height+=height&1;
    return true;
}
inline double ScaleForWidth(int width) { return static_cast<double>(width)/kBaseWidth; }
// A number typed by a player: "1.25", " 0,9 " (a decimal comma is taken too).
inline bool ParseNumber(std::string text,double& out) {
    text=Trim(text);
    for(auto& c:text) if(c==',') c='.';
    if(text.empty()) return false;
    char* end=nullptr;
    out=std::strtod(text.c_str(),&end);
    return end && *end==0 && std::isfinite(out);
}

// ---- the HD texture builder's progress line ------------------------------
// "  [#########.............]  42%  upscale 120 of 300    about 20 min left"
inline bool ParseProgress(const std::string& line,int& percent,std::string& detail) {
    const auto open=line.find('['), close=line.find(']');
    if(open==std::string::npos || close==std::string::npos || close<open) return false;
    size_t i=close+1;
    while(i<line.size() && line[i]==' ') ++i;
    const size_t start=i;
    while(i<line.size() && line[i]>='0' && line[i]<='9') ++i;
    if(i==start || i>=line.size() || line[i]!='%') return false;
    percent=std::atoi(line.substr(start,i-start).c_str());
    std::string rest=Trim(line.substr(i+1)), squeezed;
    for(char c:rest) if(!(c==' ' && !squeezed.empty() && squeezed.back()==' ')) squeezed+=c;
    detail=squeezed;
    return true;
}
}
