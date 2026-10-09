// Included by plugin.cpp (diagnostics only): a sampling profiler of the game's
// render thread, for the virtual-headset self-tests ([Test] SampleRenderThread=1).
//
// Every millisecond the render thread (the one that runs Umbra's
// processVisibility, noted by NoteRenderThread) is suspended for as long as it
// takes to read its registers, and its stack is unwound with the modules' own
// unwind data: the function it was in, and the first function of EDF.dll on the
// way up (who in the game asked for the driver's or Umbra's time). Every 15
// seconds the counts so far go to render_samples.txt beside the log, worst
// first, as module+function-start. Off unless asked for: suspending a thread a
// thousand times a second costs it time of its own.
// (std::unordered_map: included at the top of plugin.cpp)
std::atomic<DWORD> g_renderThreadId{0};
bool g_sampleRenderThread=false;
void NoteRenderThread() noexcept {
    if(g_sampleRenderThread && !g_renderThreadId.load(std::memory_order_relaxed))
        g_renderThreadId.store(GetCurrentThreadId(),std::memory_order_relaxed);
}
struct SampleKey { std::uint64_t leaf=0,game=0; };
// Function start for an address, by the module's unwind table; 0 when it has none.
std::uint64_t SampleFunctionStart(std::uint64_t pc,std::uint64_t& moduleBase) noexcept {
    DWORD64 base=0;
    auto* entry=RtlLookupFunctionEntry(pc,&base,nullptr);
    moduleBase=base;
    return entry?base+entry->BeginAddress:pc;
}
std::wstring SampleName(std::uint64_t address) noexcept {
    HMODULE module=nullptr;
    wchar_t name[MAX_PATH]{};
    if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(address),&module) && module
       && GetModuleFileNameW(module,name,MAX_PATH)) {
        const wchar_t* file=wcsrchr(name,L'\\'); file=file?file+1:name;
        wchar_t out[MAX_PATH+32]{};
        swprintf_s(out,L"%s+%llX",file,static_cast<unsigned long long>(address-reinterpret_cast<std::uint64_t>(module)));
        return out;
    }
    wchar_t out[32]{}; swprintf_s(out,L"?%llX",static_cast<unsigned long long>(address)); return out;
}
// The image's size from its own header.
std::uint64_t SampleImageSize() noexcept {
    __try {
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(g_image.base);
        return reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_image.base+dos->e_lfanew)->OptionalHeader.SizeOfImage;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// One suspended moment of the thread: its registers and up to 24 return
// addresses by the unwind data. The number of addresses, 0 when it could not.
int SampleStack(HANDLE thread,std::uint64_t (&pcs)[24]) noexcept {
    if(SuspendThread(thread)==static_cast<DWORD>(-1)) return -1;
    CONTEXT walk{}; walk.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
    int depth=0;
    if(GetThreadContext(thread,&walk)) {
        __try {
            for(;depth<24;) {
                pcs[depth++]=walk.Rip;
                DWORD64 base=0;
                auto* entry=RtlLookupFunctionEntry(walk.Rip,&base,nullptr);
                if(!entry) { walk.Rip=*reinterpret_cast<DWORD64*>(walk.Rsp); walk.Rsp+=8; }
                else {
                    void* handlerData=nullptr; DWORD64 frame=0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,walk.Rip,entry,&walk,&handlerData,&frame,nullptr);
                }
                if(!walk.Rip) break;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    ResumeThread(thread);
    return depth;
}
DWORD WINAPI RenderSampler(void*) noexcept {
    while(!g_renderThreadId.load(std::memory_order_relaxed)) Sleep(200);
    HANDLE thread=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,g_renderThreadId.load());
    if(!thread) { Log("SAMPLER could not open the render thread (%lu)",GetLastError()); return 0; }
    Log("SAMPLER render thread %lu, 1 ms",g_renderThreadId.load());
    const auto image=reinterpret_cast<std::uint64_t>(g_image.base);
    const std::uint64_t imageSize=SampleImageSize();
    std::unordered_map<std::uint64_t,unsigned> leaf,game;
    std::map<std::pair<std::uint64_t,std::uint64_t>,unsigned> pair,chain;
    unsigned long long samples=0;
    ULONGLONG nextDump=GetTickCount64()+15000;
    wchar_t path[MAX_PATH]{}; wcscpy_s(path,g_logPath);
    if(auto slash=wcsrchr(path,L'\\')) { slash[1]=0; wcscat_s(path,L"render_samples.txt"); }
    for(;;) {
        Sleep(1);
        std::uint64_t pcs[24]{};
        const int depth=SampleStack(thread,pcs);
        if(depth<0) break;
        if(!depth) continue;
        ++samples;
        std::uint64_t moduleBase=0;
        const auto top=SampleFunctionStart(pcs[0],moduleBase);
        ++leaf[top];
        std::uint64_t first=0;
        for(int i=0;i<depth;++i) {
            if(pcs[i]>=image && pcs[i]<image+imageSize) {
                std::uint64_t b=0; const auto fn=SampleFunctionStart(pcs[i],b);
                if(!first) {
                    first=fn; ++game[fn];
                    ++pair[{fn,top}]; // the game function and what it was in
                } else if(fn!=first) { ++chain[{fn,first}]; break; } // and who called it
            }
        }
        if(GetTickCount64()>=nextDump) {
            nextDump=GetTickCount64()+15000;
            FILE* f=nullptr;
            if(!_wfopen_s(&f,path,L"w") && f) {
                fwprintf(f,L"%llu samples of the render thread, 1 ms apart\n\n-- where it was (function start)\n",samples);
                std::vector<std::pair<unsigned,std::uint64_t>> v;
                for(auto& kv:leaf) v.push_back({kv.second,kv.first});
                std::sort(v.rbegin(),v.rend());
                for(size_t i=0;i<v.size() && i<60;++i) fwprintf(f,L"%6u %5.1f%%  %s\n",v[i].first,100.0*v[i].first/samples,SampleName(v[i].second).c_str());
                fwprintf(f,L"\n-- the first EDF.dll function on its stack\n");
                v.clear(); for(auto& kv:game) v.push_back({kv.second,kv.first});
                std::sort(v.rbegin(),v.rend());
                for(size_t i=0;i<v.size() && i<60;++i) fwprintf(f,L"%6u %5.1f%%  %s\n",v[i].first,100.0*v[i].first/samples,SampleName(v[i].second).c_str());
                for(int which=0;which<2;++which) {
                    auto& m=which?chain:pair;
                    fwprintf(f,which?L"\n-- caller > first EDF.dll function\n":L"\n-- first EDF.dll function < where it was\n");
                    std::vector<std::pair<unsigned,std::pair<std::uint64_t,std::uint64_t>>> w;
                    for(auto& kv:m) w.push_back({kv.second,kv.first});
                    std::sort(w.rbegin(),w.rend());
                    for(size_t i=0;i<w.size() && i<80;++i)
                        fwprintf(f,L"%6u %5.1f%%  %s %s %s\n",w[i].first,100.0*w[i].first/samples,
                                 SampleName(w[i].second.first).c_str(),which?L">":L"<",SampleName(w[i].second.second).c_str());
                }
                fclose(f);
            }
        }
    }
    CloseHandle(thread);
    return 0;
}
void StartRenderSampler() noexcept {
    if(!g_sampleRenderThread) return;
    if(HANDLE t=CreateThread(nullptr,0,&RenderSampler,nullptr,0,nullptr)) CloseHandle(t);
}
