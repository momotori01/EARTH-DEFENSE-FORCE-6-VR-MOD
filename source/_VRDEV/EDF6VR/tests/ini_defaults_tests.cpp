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

int wmain() {
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
    printf("EDF6VR INI defaults tests: %d failures\n",failures);
    return failures?1:0;
}
