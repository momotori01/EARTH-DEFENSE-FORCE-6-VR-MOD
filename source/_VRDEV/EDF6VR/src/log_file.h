// EDF6VR.log: the file every Log line goes to, and how it is kept bounded.
// Included inside plugin.cpp's private namespace after g_logPath is declared;
// tests/log_file_tests.cpp includes it the same way on a temporary file.
// Each run writes this first, so the log can be cut back to whole runs.
const char kSessionMark[]="SESSION START";
// The log is appended to for the life of the install. A week of testing had put
// it past a hundred megabytes, and nobody reads back further than the last few
// starts, so the file is cut to the most recent runs before this one begins.
// A mod should not quietly grow a file that size in somebody's game folder.
void TrimLog(int keep) noexcept {
    if(!g_logPath[0] || keep<1) return;
    // One run that logs without restraint must not defeat this either, so the
    // tail is bounded before the runs inside it are counted.
    constexpr long long kMostBytes=32ll*1024*1024;
    FILE* file=nullptr;
    if(_wfopen_s(&file,g_logPath,L"rb") || !file) return;
    long long size=0;
    if(!_fseeki64(file,0,SEEK_END)) size=_ftelli64(file);
    if(size<=0) { fclose(file); return; }
    const long long from=size>kMostBytes?size-kMostBytes:0;
    const auto length=static_cast<std::size_t>(size-from);
    auto text=static_cast<char*>(std::malloc(length+1));
    if(!text) { fclose(file); return; }
    _fseeki64(file,from,SEEK_SET);
    const auto read=std::fread(text,1,length,file);
    fclose(file);
    text[read]=0;
    // The last `keep - 1` run starts, held in a ring so that a log with
    // hundreds of runs in it costs no more than one with ten. The run about to
    // begin makes up the last of them.
    constexpr int kMostKept=32;
    if(keep>kMostKept) keep=kMostKept;
    const int wanted=keep-1;
    std::size_t recent[kMostKept]{};
    int seen=0;
    const auto mark=std::strlen(kSessionMark);
    for(std::size_t i=0;i+mark<read;++i) {
        if(i && text[i-1]!='\n') continue;
        const auto close=std::memchr(text+i,']',read-i>80?80:read-i);
        if(!close) continue;
        const auto at=static_cast<std::size_t>(static_cast<const char*>(close)-text)+2;
        if(at+mark<=read && !std::memcmp(text+at,kSessionMark,mark)) {
            if(wanted>0) recent[seen%wanted]=i;
            ++seen;
        }
    }
    // The oldest of the ones being kept is where the file now starts. With no
    // marks at all -- a log written before this existed -- only the byte cap
    // applies, and that lands mid-line, so it moves on to the next one.
    std::size_t cut=0;
    if(wanted>0 && seen>wanted) cut=recent[seen%wanted];
    else if(from>0) {
        const auto line=std::memchr(text,'\n',read);
        cut=line?static_cast<std::size_t>(static_cast<const char*>(line)-text)+1:0;
    }
    if(cut>0 || from>0) {
        FILE* out=nullptr;
        if(!_wfopen_s(&out,g_logPath,L"wb") && out) {
            std::fwrite(text+cut,1,read-cut,out);
            fclose(out);
        }
    }
    std::free(text);
}
// Within a run the file only grows, about 10 MB an hour; TrimLog cuts it back at
// the next start. The user judged that fine (2026-09-28): a day left running
// stays far under a gigabyte.
//
// The whole line goes into one buffer and out in one append that the system
// makes atomic (FILE_APPEND_DATA). The CRT's "a" seeks to the end and then
// writes, two steps, so two threads logging at once could land on the same
// place and one line overwrote the other (log_file_tests caught it).
void Log(const char* format,...) noexcept {
    if(!g_logPath[0]) return;
    char stack[2048];
    char* line=stack;
    SYSTEMTIME time{}; GetLocalTime(&time);
    const int used=std::snprintf(stack,sizeof(stack),"[%04u-%02u-%02u %02u:%02u:%02u] ",
                                 time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
    va_list args; va_start(args,format);
    va_list again; va_copy(again,args);
    const int body=std::vsnprintf(nullptr,0,format,args);
    va_end(args);
    if(used>0 && body>=0) {
        const std::size_t total=static_cast<std::size_t>(used)+static_cast<std::size_t>(body)+1;
        if(total+1>sizeof(stack)) {
            line=static_cast<char*>(std::malloc(total+1));
            if(line) std::memcpy(line,stack,static_cast<std::size_t>(used));
        }
        if(line) {
            std::vsnprintf(line+used,static_cast<std::size_t>(body)+1,format,again);
            line[total-1]='\n';
            HANDLE file=CreateFileW(g_logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                    nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE) {
                DWORD wrote=0;
                WriteFile(file,line,static_cast<DWORD>(total),&wrote,nullptr);
                CloseHandle(file);
            }
            if(line!=stack) std::free(line);
        }
    }
    va_end(again);
}
