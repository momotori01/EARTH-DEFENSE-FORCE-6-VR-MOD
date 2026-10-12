#pragma once
// THREADCPU: how much processor the game's threads use, in logical cores
// (1.00 = one logical processor busy the whole time), every five seconds from
// the watchdog's thread. Read only: thread times and start addresses.
//
// For the question "is this game using the CPU fully, and is there a setting
// that lets it use more cores, as Skyrim VR's ini has" (the user, 2026-10-12:
// "このゲームってCPUフルに使えてる？ ... 調べてみて"). What EDF.dll does, read from
// the file: it makes its worker pools with one thread a logical processor less
// one, so there is no count to raise --
//   116F430  pool "CASMtCalculate"           GetSystemInfo().dwNumberOfProcessors - 1
//   1191360  pool "xgs::sysmte::functional"  std::thread::hardware_concurrency() - 1
//   10E8B0   pool "FieldRender::Initialize"  the same, for the length of a map's set-up
//   (65B90 makes a pool, 65E90 hands every worker a job, 65F10 waits for them:
//    fork and join, from the game loop's thread: 1198DA0, called by 705530)
// -- and this line says how much of those threads a frame really uses, beside
// the two threads a frame cannot do without: the one that draws (Present's)
// and the game loop's. Threads are grouped by where they start
// (NtQueryInformationThread, ThreadQuerySetWin32StartAddress), since the pools'
// threads all start at one address.
namespace thread_cpu {
struct Thread { DWORD id; ULONGLONG created,cpu; void* start; bool alive; };
struct Group { void* start; unsigned threads; double cores,busiest; };
constexpr unsigned kMaxThreads=768,kMaxGroups=128,kShown=8;
Thread g_threads[kMaxThreads]{};
unsigned g_count=0;
LARGE_INTEGER g_at{};
ULONGLONG g_systemIdle=0,g_systemAll=0;
using QueryThread=LONG(NTAPI*)(HANDLE,int,void*,ULONG,ULONG*);
// Which logical processors the two threads a frame waits for really run on:
// noted by the threads themselves, once a frame (the game loop's in the camera
// hook) or a few times (the drawing thread's in the Umbra hook). On a
// processor with fast and slow cores (EfficiencyClass: the higher is the
// faster kind) it matters which kind they get.
std::atomic<unsigned> g_ranOn[2][64]{};
inline void Ran(int which) noexcept { g_ranOn[which][GetCurrentProcessorNumber()&63].fetch_add(1,std::memory_order_relaxed); }
unsigned char g_class[64]{};
unsigned char g_fastClass=0;
bool g_classesRead=false;
inline void ReadClasses() noexcept {
    g_classesRead=true;
    DWORD size=0;
    GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&size);
    if(!size || size>65536) return;
    static unsigned char buffer[65536];
    if(!GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer),&size)) return;
    unsigned long long fast=0,slow=0;
    for(DWORD at=0;at<size;) {
        const auto* info=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer+at);
        if(!info->Size) break;
        if(info->Relationship==RelationProcessorCore && info->Processor.GroupCount) {
            const KAFFINITY mask=info->Processor.GroupMask[0].Mask;
            for(unsigned i=0;i<64;++i) if(mask&(1ull<<i)) g_class[i]=info->Processor.EfficiencyClass;
            if(info->Processor.EfficiencyClass>g_fastClass) g_fastClass=info->Processor.EfficiencyClass;
        }
        at+=info->Size;
    }
    for(unsigned i=0;i<64;++i) (g_class[i]==g_fastClass?fast:slow)|=1ull<<i;
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    const unsigned long long present=system.dwNumberOfProcessors>=64?~0ull:((1ull<<system.dwNumberOfProcessors)-1);
    Log("THREADCPU processors: %lu logical; fast kind (class %u) mask %016llX, slower kind mask %016llX",
        system.dwNumberOfProcessors,g_fastClass,fast&present,g_fastClass?slow&present:0ull);
}
// "fast 97% (cpu4 61%, cpu6 30%)": where one of the two threads ran since the last report.
inline int Where(char* out,std::size_t size,int which) noexcept {
    unsigned counts[64]{},total=0,fast=0;
    for(unsigned i=0;i<64;++i) { counts[i]=g_ranOn[which][i].exchange(0,std::memory_order_relaxed); total+=counts[i]; if(g_class[i]==g_fastClass) fast+=counts[i]; }
    if(!total) return std::snprintf(out,size,"not seen");
    int used=std::snprintf(out,size,"fast cores %.0f%% (",100.0*fast/total);
    for(int shown=0;shown<3 && used>0 && used<static_cast<int>(size)-24;++shown) {
        unsigned best=0;
        for(unsigned i=1;i<64;++i) if(counts[i]>counts[best]) best=i;
        if(!counts[best]) break;
        used+=std::snprintf(out+used,size-static_cast<std::size_t>(used),"%scpu%u %.0f%%",shown?", ":"",best,100.0*counts[best]/total);
        counts[best]=0;
    }
    return used+std::snprintf(out+used,size-static_cast<std::size_t>(used),")");
}

inline ULONGLONG Ticks(const FILETIME& t) noexcept { return (static_cast<ULONGLONG>(t.dwHighDateTime)<<32)|t.dwLowDateTime; }

inline void Report(DWORD drawThread,DWORD loopThread) noexcept {
    static ULONGLONG last=0;
    const ULONGLONG tick=GetTickCount64();
    if(tick-last<5000) return;
    last=tick;
    LARGE_INTEGER now{},frequency{};
    QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
    const double seconds=g_at.QuadPart && frequency.QuadPart?static_cast<double>(now.QuadPart-g_at.QuadPart)/static_cast<double>(frequency.QuadPart):0;
    g_at=now;
    static const auto query=reinterpret_cast<QueryThread>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationThread")));
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE) return;
    for(unsigned i=0;i<g_count;++i) g_threads[i].alive=false;
    static Group groups[kMaxGroups];
    unsigned groupCount=0,alive=0;
    double total=0,draw=0,loop=0;
    const DWORD process=GetCurrentProcessId();
    THREADENTRY32 entry{}; entry.dwSize=sizeof(entry);
    for(BOOL more=Thread32First(snapshot,&entry);more;more=Thread32Next(snapshot,&entry)) {
        if(entry.th32OwnerProcessID!=process) continue;
        HANDLE handle=OpenThread(THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
        if(!handle) continue;
        FILETIME created{},exited{},kernel{},user{};
        if(GetThreadTimes(handle,&created,&exited,&kernel,&user)) {
            const ULONGLONG cpu=Ticks(kernel)+Ticks(user),born=Ticks(created);
            Thread* slot=nullptr;
            for(unsigned i=0;i<g_count;++i) if(g_threads[i].id==entry.th32ThreadID) { slot=&g_threads[i]; break; }
            double cores=0;
            if(slot && slot->created==born) {
                if(seconds>0 && cpu>=slot->cpu) cores=static_cast<double>(cpu-slot->cpu)/1e7/seconds;
            } else {
                if(!slot && g_count<kMaxThreads) slot=&g_threads[g_count++];
                if(slot) {
                    *slot={entry.th32ThreadID,born,cpu,nullptr,true};      // a new thread (or a number used again)
                    if(query) query(handle,9,&slot->start,sizeof(slot->start),nullptr);
                }
            }
            if(slot) {
                slot->cpu=cpu; slot->alive=true; ++alive;
                total+=cores;
                if(entry.th32ThreadID==drawThread) draw=cores;
                if(entry.th32ThreadID==loopThread) loop=cores;
                Group* group=nullptr;
                for(unsigned i=0;i<groupCount;++i) if(groups[i].start==slot->start) { group=&groups[i]; break; }
                if(!group && groupCount<kMaxGroups) { group=&groups[groupCount++]; *group={slot->start,0,0,0}; }
                if(group) { ++group->threads; group->cores+=cores; if(cores>group->busiest) group->busiest=cores; }
            }
        }
        CloseHandle(handle);
    }
    CloseHandle(snapshot);
    unsigned kept=0;
    for(unsigned i=0;i<g_count;++i) if(g_threads[i].alive) g_threads[kept++]=g_threads[i];
    g_count=kept;
    FILETIME idle{},kernelAll{},userAll{};
    double pcBusy=-1;
    if(GetSystemTimes(&idle,&kernelAll,&userAll)) {
        const ULONGLONG idleNow=Ticks(idle),allNow=Ticks(kernelAll)+Ticks(userAll);   // kernel time includes idle
        if(g_systemAll && allNow>g_systemAll) pcBusy=100.0*(1.0-static_cast<double>(idleNow-g_systemIdle)/static_cast<double>(allNow-g_systemAll));
        g_systemIdle=idleNow; g_systemAll=allNow;
    }
    if(!g_classesRead) ReadClasses();
    char loopOn[96]{},drawOn[96]{};
    Where(loopOn,sizeof(loopOn),0); Where(drawOn,sizeof(drawOn),1);
    if(seconds<=0) return;
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    char line[1400]{};
    int used=std::snprintf(line,sizeof(line),"THREADCPU %.1fs: the game used %.2f of %lu logical cores over %u threads; drawing thread tid=%lu %.2f, game loop tid=%lu %.2f; the whole PC %.0f%% busy. By start:",
        seconds,total,system.dwNumberOfProcessors,alive,drawThread,draw,loopThread,loop,pcBusy);
    for(unsigned shown=0;shown<kShown && used>0 && used<static_cast<int>(sizeof(line))-96;++shown) {
        Group* top=nullptr;
        for(unsigned i=0;i<groupCount;++i) if(groups[i].threads && (!top || groups[i].cores>top->cores)) top=&groups[i];
        if(!top || top->cores<0.005) break;
        wchar_t path[MAX_PATH]{}; const wchar_t* name=L"?"; unsigned long long offset=0;
        HMODULE module=nullptr;
        if(top->start && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           static_cast<LPCWSTR>(top->start),&module) && module && GetModuleFileNameW(module,path,MAX_PATH)) {
            const wchar_t* slash=wcsrchr(path,L'\\');
            name=slash?slash+1:path;
            offset=static_cast<unsigned long long>(static_cast<unsigned char*>(top->start)-reinterpret_cast<unsigned char*>(module));
        }
        used+=std::snprintf(line+used,sizeof(line)-static_cast<std::size_t>(used)," %ls+%llX x%u %.2f (busiest %.2f);",name,offset,top->threads,top->cores,top->busiest);
        top->threads=0;   // taken
    }
    Log("%s",line);
    Log("THREADCPU ran on: game loop %s; drawing thread %s",loopOn,drawOn);
}
}  // namespace thread_cpu
