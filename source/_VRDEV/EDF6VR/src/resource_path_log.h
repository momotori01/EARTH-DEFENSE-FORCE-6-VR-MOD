#pragma once
#include "image_profile.h"
#include <Windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace edf6vr {
// What EDFModLoader is actually willing to replace, observed instead of assumed.
//
// The loader has exactly one replacement mechanism (EDFModLoader dllmain.cpp,
// crifsio_hook): a game path beginning "/cri_bind/" is rewritten to
// "./Mods/<rest>" whenever that file is present on disk. It decides by calling
// FileExistsW, which is GetFileAttributesW, from its own module. So every name
// a mod could ever replace passes through that one import, and reading it
// patches no game code and changes no answer: the original result is handed
// straight back.
//
// The question this exists to settle is whether the game ever asks for a single
// texture inside an archive. EDF.dll contains the string
// APP:/EFFECT/BASIC.RAB@SPARK_01.DDS, which reads like per-entry addressing; if
// that spelling reaches the file layer then a texture pack is a few hundred
// megabytes of loose DDS files. If only whole .RAB names arrive, the same pack
// means rebuilding half-gigabyte archives. Nothing else about the design can be
// decided before this is known, so it is measured rather than guessed.
namespace {
using GetFileAttributesFn=DWORD (WINAPI*)(LPCWSTR);
GetFileAttributesFn g_pathOriginal=nullptr;
SRWLOCK g_pathLock=SRWLOCK_INIT;
std::set<std::wstring>* g_pathSeen=nullptr;
std::atomic<unsigned long long> g_pathAsks{0}, g_pathHits{0};
std::atomic<bool> g_pathDirty{false};
wchar_t g_pathFile[MAX_PATH]{};
constexpr std::size_t kPathLimit=400000;

// The loader always builds its candidate with this exact prefix and forward
// slashes, so anything else through this import belongs to somebody else.
constexpr wchar_t kModsPrefix[]=L"./Mods/";
constexpr std::size_t kModsPrefixLength=7;

DWORD WINAPI HookGetFileAttributes(LPCWSTR path) noexcept {
    const DWORD result=g_pathOriginal?g_pathOriginal(path):INVALID_FILE_ATTRIBUTES;
    if(!path || wcsncmp(path,kModsPrefix,kModsPrefixLength)) return result;
    g_pathAsks.fetch_add(1,std::memory_order_relaxed);
    if(result!=INVALID_FILE_ATTRIBUTES) g_pathHits.fetch_add(1,std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_pathLock);
    if(g_pathSeen && g_pathSeen->size()<kPathLimit
       && g_pathSeen->insert(path+kModsPrefixLength).second)
        g_pathDirty.store(true,std::memory_order_release);
    ReleaseSRWLockExclusive(&g_pathLock);
    return result;
}

// One named import of one module. Returns the slot rather than patching it, so
// the caller can go through ReplacePointer and keep the page protection rules
// this plugin uses everywhere else.
void** FindImportSlot(HMODULE module,const char* symbol) noexcept {
    auto base=reinterpret_cast<unsigned char*>(module);
    if(!base || !symbol) return nullptr;
    __try {
        auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return nullptr;
        auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE) return nullptr;
        const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if(!directory.VirtualAddress || !directory.Size) return nullptr;
        auto entry=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress);
        for(;entry->Name;++entry) {
            if(!entry->OriginalFirstThunk || !entry->FirstThunk) continue;
            auto names=reinterpret_cast<const IMAGE_THUNK_DATA*>(base+entry->OriginalFirstThunk);
            auto slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+entry->FirstThunk);
            for(;names->u1.AddressOfData;++names,++slots) {
                if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
                auto named=reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
                if(!std::strcmp(reinterpret_cast<const char*>(named->Name),symbol))
                    return reinterpret_cast<void**>(&slots->u1.Function);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}
}

// Every path written so far, newest run appended over the old file. Cheap
// enough to call on the existing five-second reporting path and a no-op when
// nothing new has been asked for.
inline void FlushResourcePathLog(bool force=false) noexcept {
    if(!g_pathFile[0]) return;
    if(!force && !g_pathDirty.exchange(false,std::memory_order_acq_rel)) return;
    g_pathDirty.store(false,std::memory_order_release);
    std::set<std::wstring> copy;
    AcquireSRWLockShared(&g_pathLock);
    if(g_pathSeen) copy=*g_pathSeen;
    ReleaseSRWLockShared(&g_pathLock);
    FILE* file=nullptr;
    if(_wfopen_s(&file,g_pathFile,L"w") || !file) return;
    std::fprintf(file,"# EDF6VR resource path log\n"
                      "# Every path EDFModLoader offered to the Mods folder, written as Mods/<path>.\n"
                      "# asks=%llu hitsOnDisk=%llu distinct=%zu\n",
                 g_pathAsks.load(std::memory_order_relaxed),
                 g_pathHits.load(std::memory_order_relaxed),copy.size());
    char utf8[1024];
    for(const auto& path:copy) {
        if(WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,utf8,sizeof(utf8),nullptr,nullptr)>0)
            std::fprintf(file,"%s\n",utf8);
    }
    std::fclose(file);
}

// directory ends in a backslash. False when the loader's import could not be
// found, which is the honest answer when it has not been told to redirect.
inline bool InstallResourcePathLog(const wchar_t* directory) noexcept {
    if(g_pathOriginal) return true;
    if(directory) {
        std::swprintf(g_pathFile,MAX_PATH,L"%sEDF6VR.paths.txt",directory);
    }
    static std::set<std::wstring> storage;
    g_pathSeen=&storage;
    auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
    if(snapshot==INVALID_HANDLE_VALUE) return false;
    bool installed=false;
    MODULEENTRY32W entry{};
    entry.dwSize=sizeof(entry);
    for(BOOL more=Module32FirstW(snapshot,&entry);more && !installed;more=Module32NextW(snapshot,&entry)) {
        if(_wcsicmp(entry.szModule,L"winmm.dll")) continue;
        auto slot=FindImportSlot(entry.hModule,"GetFileAttributesW");
        if(!slot || !*slot) continue;
        // The system's own winmm is in this process too; it has no reason to
        // ask about ./Mods and patching it would tell us nothing. Take the one
        // that is not under the Windows directory, which is the proxy.
        wchar_t path[MAX_PATH]{}, system[MAX_PATH]{};
        if(!GetModuleFileNameW(entry.hModule,path,MAX_PATH)) continue;
        if(GetSystemDirectoryW(system,MAX_PATH) && !_wcsnicmp(path,system,wcslen(system))) continue;
        void* current=*slot;
        bool changed=false;
        if(ReplacePointer(slot,current,reinterpret_cast<void*>(&HookGetFileAttributes),changed) && changed) {
            g_pathOriginal=reinterpret_cast<GetFileAttributesFn>(current);
            installed=true;
        }
    }
    CloseHandle(snapshot);
    return installed;
}

inline void ReadResourcePathCounts(unsigned long long& asks,unsigned long long& hits,
                                   std::size_t& distinct) noexcept {
    asks=g_pathAsks.load(std::memory_order_relaxed);
    hits=g_pathHits.load(std::memory_order_relaxed);
    AcquireSRWLockShared(&g_pathLock);
    distinct=g_pathSeen?g_pathSeen->size():0;
    ReleaseSRWLockShared(&g_pathLock);
}
}
