// EDF6VR.log: lines logged from several threads at once all arrive whole, and
// the start-of-run trim keeps the last runs. Runs on a temporary file.
#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)

namespace {
wchar_t g_logPath[MAX_PATH]{};
#include "../src/log_file.h"
}

static std::string ReadAll() {
    std::string text;
    FILE* file=nullptr;
    if(_wfopen_s(&file,g_logPath,L"rb") || !file) return text;
    char buffer[65536]; std::size_t n=0;
    while((n=std::fread(buffer,1,sizeof(buffer),file))>0) text.append(buffer,n);
    fclose(file);
    return text;
}
// Every line starts with a time stamp and ends with a line end.
static bool WholeLines(const std::string& text) {
    std::size_t at=0;
    while(at<text.size()) {
        const auto end=text.find('\n',at);
        if(end==std::string::npos) return false;
        if(text[at]!='[' || text.find(']',at)>end) return false;
        at=end+1;
    }
    return true;
}

int main() {
    wchar_t dir[MAX_PATH]{}; GetTempPathW(MAX_PATH,dir);
    swprintf(g_logPath,MAX_PATH,L"%sedf6vr_log_test_%lu.log",dir,GetCurrentProcessId());
    DeleteFileW(g_logPath);

    // Four threads at once: every line arrives, whole, in each thread's order.
    std::vector<std::thread> threads;
    for(int t=0;t<4;++t) threads.emplace_back([t] { for(int i=0;i<3000;++i) Log("thread %d line %05d %s",t,i,"padding padding padding"); });
    for(auto& thread:threads) thread.join();
    {
        const auto text=ReadAll();
        CHECK(WholeLines(text));
        int next[4]={0,0,0,0}; bool order=true;
        for(std::size_t at=0;at<text.size();) {
            const auto end=text.find('\n',at);
            int t=-1,i=-1;
            const auto found=text.find("] thread ",at);
            if(found<end && std::sscanf(text.c_str()+found,"] thread %d line %d",&t,&i)==2 && t>=0 && t<4) {
                if(i!=next[t]) order=false;
                next[t]=i+1;
            }
            at=end+1;
        }
        CHECK(order);
        for(int t=0;t<4;++t) CHECK(next[t]==3000);
    }

    // A line longer than the stack buffer.
    DeleteFileW(g_logPath);
    const std::string longText(5000,'x');
    Log("long %s end",longText.c_str());
    {
        const auto text=ReadAll();
        CHECK(WholeLines(text));
        CHECK(text.find("long "+longText+" end\n")!=std::string::npos);
    }

    // TrimLog keeps the last runs at a start.
    DeleteFileW(g_logPath);
    for(int run=0;run<5;++run) { TrimLog(10); Log("%s",kSessionMark); Log("run %d",run); }
    TrimLog(3); Log("%s",kSessionMark);
    {
        const auto text=ReadAll();
        CHECK(text.find("run 2")==std::string::npos);
        CHECK(text.find("run 3")!=std::string::npos && text.find("run 4")!=std::string::npos);
    }

    DeleteFileW(g_logPath);
    if(failures) { printf("%d failure(s)\n",failures); return 1; }
    printf("log file tests passed\n");
    return 0;
}
