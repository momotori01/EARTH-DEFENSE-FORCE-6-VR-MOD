// Included by plugin.cpp (diagnostics only, [Test] ViewportGpu=1): where the
// GPU's frame goes, by Umbra viewport. A timestamp at each present and at the
// start and end of every processVisibility (two eyes x shadow, shadow, far,
// main), read back a few frames later without flushing. When the GPU is the
// busy side -- the virtual headset at full size (2026-10-09) -- consecutive
// stamps bound the GPU work submitted between them: the viewport's own draws,
// the gap after it (whatever the renderer does between viewports: lighting,
// post-processing), and the tail from the last viewport to the present.
// Everything on the render thread, on the game's immediate context.
namespace viewport_gpu {
constexpr unsigned kSlots=6,kMarks=24;   // 8 viewports x 2 + present + spare
struct Mark { int eye=-1; unsigned mode=3; bool end=false; };
struct Slot {
    ID3D11Query* disjoint=nullptr;
    ID3D11Query* stamps[kMarks]{};
    Mark marks[kMarks]{};
    unsigned used=0;
    bool open=false,pending=false;
};
Slot slots[kSlots]; Slot* current=nullptr;
ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
bool enabled=false;
// Averages since the last report: per [eye 0,1,other][mode] the viewport's
// own span and the gap after it, the head (present to first viewport) and the
// tail (last viewport to present), frames counted.
struct Totals {
    double own[3][4]{},gap[3][4]{};unsigned long long n[3][4]{};
    double head=0,tail=0,frame=0;unsigned long long frames=0,lost=0;
} totals;
SRWLOCK lock=SRWLOCK_INIT;
bool Create(Slot& s) noexcept {
    if(s.disjoint) return true;
    D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
    if(FAILED(device->CreateQuery(&desc,&s.disjoint))) return false;
    desc.Query=D3D11_QUERY_TIMESTAMP;
    for(auto*& q:s.stamps) if(FAILED(device->CreateQuery(&desc,&q))) return false;
    return true;
}
void Stamp(int eye,unsigned mode,bool end) noexcept {
    if(!current || current->used>=kMarks) return;
    auto& s=*current;
    context->End(s.stamps[s.used]);
    s.marks[s.used]={eye,mode,end};
    ++s.used;
}
void Collect() noexcept {
    for(auto& s:slots) {
        if(!s.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
        if(context->GetData(s.disjoint,&clock,sizeof(clock),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
        UINT64 t[kMarks]{}; bool ready=true;
        for(unsigned i=0;i<s.used && ready;++i)
            ready=context->GetData(s.stamps[i],&t[i],sizeof(t[i]),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK;
        if(!ready) continue;
        s.pending=false;
        AcquireSRWLockExclusive(&lock);
        // marks: [0] the frame's start (previous present), then begin/end pairs, last the present.
        if(clock.Disjoint || !clock.Frequency || s.used<4) { ++totals.lost; ReleaseSRWLockExclusive(&lock); continue; }
        const double ms=1000.0/static_cast<double>(clock.Frequency);
        auto span=[&](unsigned a,unsigned b) { return t[b]>=t[a]?static_cast<double>(t[b]-t[a])*ms:0.0; };
        const unsigned last=s.used-1;
        totals.frame+=span(0,last); ++totals.frames;
        bool first=true;
        for(unsigned i=1;i<last;++i) {
            if(s.marks[i].end) continue;
            // begin at i; its end is the next mark flagged end
            unsigned e=i+1; while(e<last && !s.marks[e].end) ++e;
            if(e>=last) break;
            const unsigned eye=s.marks[i].eye==0?0u:s.marks[i].eye==1?1u:2u, mode=s.marks[i].mode<4?s.marks[i].mode:3u;
            if(first) { totals.head+=span(0,i); first=false; }
            totals.own[eye][mode]+=span(i,e);
            const unsigned next=e+1<=last?e+1:last;
            if(next==last) totals.tail+=span(e,last); else totals.gap[eye][mode]+=span(e,next);
            ++totals.n[eye][mode];
        }
        ReleaseSRWLockExclusive(&lock);
    }
}
// processVisibility of one viewport, around the call.
void Begin(void* commander,int eye,unsigned mode) noexcept {
    if(!enabled || !current || !commander) return;
    Stamp(eye,mode,false);
}
void End(int eye,unsigned mode) noexcept { if(enabled && current) Stamp(eye,mode,true); }
// At each present on the render thread: close this frame, open the next.
void Present(ID3D11Device* d,ID3D11DeviceContext* c) noexcept {
    if(!enabled || !d || !c) return;
    if(d!=device || c!=context) {
        for(auto& s:slots) {
            if(s.disjoint) s.disjoint->Release();
            for(auto*& q:s.stamps) if(q) { q->Release(); q=nullptr; }
            s=Slot{};
        }
        current=nullptr; device=d; context=c;
    }
    if(current) {
        Stamp(-1,3,true);
        context->End(current->disjoint);
        current->open=false; current->pending=true; current=nullptr;
    }
    Collect();
    for(auto& s:slots) if(!s.pending && !s.open) {
        if(!Create(s)) return;
        s.used=0; s.open=true; current=&s;
        context->Begin(s.disjoint);
        Stamp(-1,3,false);
        return;
    }
}
void Report() noexcept {
    AcquireSRWLockExclusive(&lock); const auto t=totals; totals={}; ReleaseSRWLockExclusive(&lock);
    if(!t.frames) return;
    const double f=static_cast<double>(t.frames);
    auto own=[&](int e,int m) { return t.own[e][m]/f; };
    auto gap=[&](int e,int m) { return t.gap[e][m]/f; };
    Log("VIEWPORTGPU frames=%llu lost=%llu gpuFrameMs=%.2f head=%.2f tail=%.2f | eye0 shadow=%.2f(+%.2f) far=%.2f(+%.2f) main=%.2f(+%.2f) | eye1 shadow=%.2f(+%.2f) far=%.2f(+%.2f) main=%.2f(+%.2f) | other=%.2f(+%.2f) per frame, (+gap after)",
        t.frames,t.lost,t.frame/f,t.head/f,t.tail/f,
        own(0,2),gap(0,2),own(0,1),gap(0,1),own(0,0),gap(0,0),
        own(1,2),gap(1,2),own(1,1),gap(1,1),own(1,0),gap(1,0),
        own(2,0)+own(2,1)+own(2,2)+own(2,3),gap(2,0)+gap(2,1)+gap(2,2)+gap(2,3));
}
}
