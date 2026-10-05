#include "ini_defaults.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

static std::string ReadAll(const wchar_t* path) {
    FILE* f=nullptr; std::string out;
    if(_wfopen_s(&f,path,L"rb") || !f) return out;
    char buffer[4096]; size_t n;
    while((n=fread(buffer,1,sizeof(buffer),f))>0) out.append(buffer,n);
    fclose(f); return out;
}
static std::wstring Get(const wchar_t* section,const wchar_t* key,const wchar_t* path) {
    wchar_t value[128]{}; GetPrivateProfileStringW(section,key,L"<none>",value,128,path); return value;
}

// The INI this build ships (packaging/EDF6VR.ini): a 2.1.1 file, which has no
// [LeftHanded], comes out with it placed exactly as in a fresh one.
static void ShippedIniGainsLeftHanded(const wchar_t* shippedPath,const wchar_t* path) {
    const std::string shipped=ReadAll(shippedPath);
    const auto section=shipped.find("[LeftHanded]");
    CHECK(section!=std::string::npos);
    if(section==std::string::npos) return;
    CHECK(section<1000);                                             // in view as soon as the file is opened
    const auto blank=shipped.rfind("\r\n\r\n\r\n",section);
    const auto end=shipped.find("; Which generation",section);
    CHECK(blank!=std::string::npos && end!=std::string::npos);
    if(blank==std::string::npos || end==std::string::npos) return;
    const std::string older=shipped.substr(0,blank+2)+shipped.substr(end);
    DeleteFileW(path);
    FILE* file=nullptr; _wfopen_s(&file,path,L"wb"); fwrite(older.data(),1,older.size(),file); fclose(file);
    const auto r=edf6vr::MergeIniDefaults(shipped.data(),shipped.size(),path);
    CHECK(!r.failed && r.sections==1 && r.added==2);
    CHECK(ReadAll(path)==shipped);
    CHECK(Get(L"LeftHanded",L"LeftHanded",path)==L"0");               // right-handed unless asked
    CHECK(Get(L"LeftHanded",L"LeftHandedSticks",path)==L"1");
    DeleteFileW(path);
}

int wmain(int argc,wchar_t** argv) {
    const char defaults[]=
        "; shipped defaults\r\n"
        "[VR]\r\n"
        "; comment for A\r\n"
        "A=1\r\n"
        "B = 2.5 \r\n"
        "Empty=\r\n"
        "\r\n"
        "[Hand.index]\r\n"
        "HandRightMetres=0.0639\r\n"
        "[Render]\r\n"
        "BoardWorldLocked=1\r\n";
    wchar_t dir[MAX_PATH]{}; GetTempPathW(MAX_PATH,dir);
    wchar_t path[MAX_PATH]{}; swprintf_s(path,L"%sedf6vr_ini_defaults_%lu.ini",dir,GetCurrentProcessId());
    DeleteFileW(path);

    // No INI: written out whole, comments included.
    auto r=edf6vr::MergeIniDefaults(defaults,sizeof(defaults)-1,path);
    CHECK(r.created && !r.failed && r.added==0);
    CHECK(ReadAll(path)==std::string(defaults,sizeof(defaults)-1));
    // Second start: nothing to add, file untouched.
    r=edf6vr::MergeIniDefaults(defaults,sizeof(defaults)-1,path);
    CHECK(!r.created && !r.failed && r.added==0);
    CHECK(ReadAll(path)==std::string(defaults,sizeof(defaults)-1));

    // An older INI with the player's own values and some keys missing.
    DeleteFileW(path);
    const char older[]="[VR]\r\nA=7\r\nEmpty=\r\n[Render]\r\nOther=3\r\n";
    FILE* f=nullptr; _wfopen_s(&f,path,L"wb"); fwrite(older,1,sizeof(older)-1,f); fclose(f);
    r=edf6vr::MergeIniDefaults(defaults,sizeof(defaults)-1,path);
    CHECK(!r.created && !r.failed && r.added==3);          // B, HandRightMetres, BoardWorldLocked
    CHECK(r.sections==1);                                  // [Hand.index], whole, above [Render]
    CHECK(ReadAll(path).find("\r\n[Hand.index]\r\nHandRightMetres=0.0639\r\n[Render]\r\n")!=std::string::npos);
    CHECK(Get(L"VR",L"A",path)==L"7");                     // the player's value stays
    CHECK(Get(L"VR",L"B",path)==L"2.5");                   // added, trimmed
    CHECK(Get(L"VR",L"Empty",path)==L"");                  // present but empty: not overwritten
    CHECK(Get(L"Hand.index",L"HandRightMetres",path)==L"0.0639");
    CHECK(Get(L"Render",L"BoardWorldLocked",path)==L"1");
    CHECK(Get(L"Render",L"Other",path)==L"3");             // unknown keys are kept
    r=edf6vr::MergeIniDefaults(defaults,sizeof(defaults)-1,path);
    CHECK(r.added==0);
    DeleteFileW(path);

    CHECK(edf6vr::MergeIniDefaults(nullptr,0,path).failed);

    // A new section goes in whole where the shipped file has it: blank lines,
    // comments and keys, above the comments leading into the next section.
    {
        const char shipped[]=
            "; header\r\n; more header\r\n\r\n\r\n"
            "; new section, explained\r\n[New]\r\nOn=0\r\nSticks=1\r\n\r\n\r\n"
            "; about Settings\r\n[Settings]\r\nRevision=2\r\n\r\n"
            "; last\r\n[Tail]\r\nT=1\r\n";
        const char before[]=
            "; header\r\n; more header\r\n"
            "; about Settings\r\n[Settings]\r\nRevision=2\r\n\r\n"
            "; last\r\n[Tail]\r\nT=5\r\n";
        DeleteFileW(path);
        FILE* file=nullptr; _wfopen_s(&file,path,L"wb"); fwrite(before,1,sizeof(before)-1,file); fclose(file);
        r=edf6vr::MergeIniDefaults(shipped,sizeof(shipped)-1,path);
        CHECK(!r.failed && r.sections==1 && r.added==2);
        std::string expected(shipped,sizeof(shipped)-1);
        expected.replace(expected.find("T=1"),3,"T=5");                 // the player's value stays
        CHECK(ReadAll(path)==expected);
        r=edf6vr::MergeIniDefaults(shipped,sizeof(shipped)-1,path);
        CHECK(!r.failed && r.sections==0 && r.added==0 && ReadAll(path)==expected);
        // An LF file gets LF lines; a file without the next section gets the block at the end.
        const char lf[]="[Settings]\nRevision=2\n";
        DeleteFileW(path);
        _wfopen_s(&file,path,L"wb"); fwrite(lf,1,sizeof(lf)-1,file); fclose(file);
        const char tailOnly[]="[Settings]\r\nRevision=2\r\n\r\n; last\r\n[Tail]\r\nT=1\r\n";
        r=edf6vr::MergeIniDefaults(tailOnly,sizeof(tailOnly)-1,path);
        CHECK(!r.failed && r.sections==1 && r.added==1);
        CHECK(ReadAll(path)=="[Settings]\nRevision=2\n\n; last\n[Tail]\nT=1\n");
        DeleteFileW(path);
    }

    // A file from an older generation is replaced whole, once, with a backup; the resolution carries over.
    {
        const char next[]=
            "[Settings]\r\nRevision=2\r\n"
            "[VR]\r\nA=1\r\n"
            "[Render]\r\nForceWidth=3840\r\nForceHeight=2160\r\nBoard=2.6\r\n";
        const char previous[]="[VR]\r\nA=7\r\nOld=1\r\n[Render]\r\nForceWidth=4800\r\nForceHeight=2700\r\nBoard=2.2\r\n";
        DeleteFileW(path);
        FILE* file=nullptr; _wfopen_s(&file,path,L"wb"); fwrite(previous,1,sizeof(previous)-1,file); fclose(file);
        std::wstring backup=std::wstring(path)+L".v1.bak";
        DeleteFileW(backup.c_str());
        auto reset=edf6vr::ResetOlderIni(next,sizeof(next)-1,path,2);
        CHECK(reset.reset && !reset.failed && reset.from==0 && reset.keptResolution);
        CHECK(ReadAll(backup.c_str())==previous);                    // the player's file, byte for byte
        CHECK(Get(L"VR",L"A",path)==L"1");                         // the new default, not the player's 7
        CHECK(Get(L"VR",L"Old",path)==L"<none>");                  // nothing of the old file but the resolution
        CHECK(Get(L"Render",L"Board",path)==L"2.6");
        CHECK(Get(L"Render",L"ForceWidth",path)==L"4800" && Get(L"Render",L"ForceHeight",path)==L"2700");
        CHECK(Get(L"Settings",L"Revision",path)==L"2");
        // Once only: the player's later changes stay.
        WritePrivateProfileStringW(L"VR",L"A",L"9",path);
        reset=edf6vr::ResetOlderIni(next,sizeof(next)-1,path,2);
        CHECK(!reset.reset && !reset.failed && reset.from==2 && Get(L"VR",L"A",path)==L"9");
        // No file: nothing to replace (the merge writes it).
        DeleteFileW(path);
        reset=edf6vr::ResetOlderIni(next,sizeof(next)-1,path,2);
        CHECK(!reset.reset && !reset.failed && GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
        DeleteFileW(backup.c_str());
    }
    if(argc>1) ShippedIniGainsLeftHanded(argv[1],path);
    if(argc>1) {
        // Vehicle hand aim ships on in the Nix only, and without the marker the
        // mod writes once it has turned an older INI's seats off.
        CHECK(Get(L"VR",L"VehicleHandAimNix",argv[1])==L"1");
        for(const wchar_t* key:{L"VehicleHandAimDepth",L"VehicleHandAimBarga",L"VehicleHandAimTank",L"VehicleHandAimCombat",
                                L"VehicleHandAimHeli",L"VehicleHandAimGunner",L"VehicleHandAimBruteGunner"})
            CHECK(Get(L"VR",key,argv[1])==L"0");
        CHECK(Get(L"VR",L"VehicleHandAimDefaults",argv[1])==L"<none>");
    }
    else { printf("FAIL: the shipped INI's path is the first argument\n"); ++failures; }
    printf("EDF6VR INI defaults tests: %d failures\n",failures);
    return failures?1:0;
}
