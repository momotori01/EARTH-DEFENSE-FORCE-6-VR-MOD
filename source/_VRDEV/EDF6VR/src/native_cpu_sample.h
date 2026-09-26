// Sample OS-accounted CPU time separately from renderer wall time. No GPU
// flush/readback and no scheduling changes. CPU includes any busy polling;
// coarse Windows accounting requires aggregating several reporting windows.
struct NativeCpuTotals { unsigned long long samples=0,cpu100ns=0; std::int64_t wallTicks=0; };
SRWLOCK g_nativeCpuLock=SRWLOCK_INIT;
NativeCpuTotals g_nativeCpuTotals{};
bool ReadCurrentThreadCpu(unsigned long long& value) noexcept {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))return false;
    value=(static_cast<unsigned long long>(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime
        +(static_cast<unsigned long long>(user.dwHighDateTime)<<32)+user.dwLowDateTime;
    return true;
}
struct NativeCpuSample {
    unsigned long long before=0;
    bool active=false;
    explicit NativeCpuSample(bool main) noexcept {
        thread_local unsigned sequence=0;
        if(main && (++sequence%16)==0)active=ReadCurrentThreadCpu(before);
    }
    void Finish(std::int64_t elapsed) noexcept {
        unsigned long long after=0;
        if(!active || !ReadCurrentThreadCpu(after) || after<before || elapsed<0)return;
        if(!TryAcquireSRWLockExclusive(&g_nativeCpuLock))return;
        ++g_nativeCpuTotals.samples;g_nativeCpuTotals.cpu100ns+=after-before;
        g_nativeCpuTotals.wallTicks+=elapsed;
        ReleaseSRWLockExclusive(&g_nativeCpuLock);
    }
};
NativeCpuTotals DrainNativeCpu() noexcept {
    AcquireSRWLockExclusive(&g_nativeCpuLock);
    const auto result=g_nativeCpuTotals;g_nativeCpuTotals={};
    ReleaseSRWLockExclusive(&g_nativeCpuLock);return result;
}
