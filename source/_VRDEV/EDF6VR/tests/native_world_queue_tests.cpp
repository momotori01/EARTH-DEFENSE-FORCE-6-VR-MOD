#include "../src/native_world_queue.cpp"
#include <cstdio>
#include <vector>
#include <thread>
#include <array>

using namespace edf6vr;
static unsigned failures=0;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
struct QueueFake {
    alignas(16) unsigned char owner[0x3000]{};
    alignas(16) unsigned char bytes[16*128]{};
    int read=0,write=0,available=0,pending=0,reserved=0,claimed=0;
    unsigned flushes=0,setups=0,reserves=0,commits=0,peeks=0,pops=0;
} queue;
struct Event {std::uint64_t frame;unsigned eye,kind;bool valid;};
static std::vector<Event> events;
static std::vector<unsigned> producerEvents;
static std::vector<void*> producerScenes;
static bool allow=true;
static void* expectedContext=reinterpret_cast<void*>(0x1234000);
static const unsigned char flagsValue=3;
static bool Allow() noexcept{return allow;}
static void __fastcall FakeSetup(void* context,void* owner,const void* flags,bool set) {
    CHECK(context==expectedContext);CHECK(owner==queue.owner);CHECK(flags==&flagsValue);CHECK(set);++queue.setups;
}
static void __fastcall FakeFlush(void* owner) {CHECK(owner==queue.owner);++queue.flushes;queue.available+=queue.pending;queue.pending=0;}
static void* __fastcall FakeReserve(void* q,int units) {
    CHECK(q==queue.owner+0x28A8);CHECK(units==1 || units==2);CHECK(!queue.reserved);
    queue.reserved=units;++queue.reserves;return queue.bytes+queue.write*16;
}
static void __fastcall FakeCommit(void* q,int units) {
    CHECK(q==queue.owner+0x28A8);CHECK(queue.reserved==units);queue.reserved=0;
    queue.write+=units;if(queue.write>=128)queue.write=0;queue.pending+=units;++queue.commits;
}
static void* __fastcall FakePeek(void* q,int units) {
    CHECK(q==&queue);CHECK(queue.available>=units);CHECK(!queue.claimed);
    queue.claimed=units;queue.available-=units;++queue.peeks;return queue.bytes+queue.read*16;
}
static void __fastcall FakePop(void* q,int units) {
    CHECK(q==&queue);CHECK(queue.claimed==units);queue.claimed=0;
    queue.read+=units;if(queue.read>=128)queue.read=0;++queue.pops;
}
static void Begin(std::uint64_t frame,unsigned eye,void* context) noexcept {
    CHECK(context==expectedContext);events.push_back({frame,eye,0,NativeWorldFrameValid(frame)});
}
static void End(std::uint64_t frame,unsigned eye,void* context) noexcept {
    CHECK(context==expectedContext);events.push_back({frame,eye,1,NativeWorldFrameValid(frame)});
}
static void ProducerBegin(std::uint64_t frame,unsigned eye) noexcept {
    CHECK(frame==NativeWorldProducerFrame() && static_cast<int>(eye)==NativeWorldProducerEye());
    // Each prior marker uses two reserve calls. Producer latch precedes this
    // eye's begin marker, including when original left resolve was skipped.
    CHECK(queue.reserves==(eye?4u:0u) || producerEvents.size()>=2);
    producerEvents.push_back(eye);
}
static void ProducerEnd(std::uint64_t frame,unsigned eye,void* scene) noexcept {
    CHECK(frame==NativeWorldProducerFrame() && static_cast<int>(eye)==NativeWorldProducerEye());
    // The corresponding end marker has not yet been reserved or published.
    CHECK(queue.reserves==2u+producerScenes.size()*4u);
    producerScenes.push_back(scene);
}
static void Drain() {
    while(queue.available) {
        const auto fn=*static_cast<void(__fastcall**)(void*,void*)>(FakePeek(&queue,1));
        FakePop(&queue,1);fn(expectedContext,&queue);
    }
    CHECK(!queue.claimed && !queue.reserved);
}
static void ResetFake() {
    queue={};events.clear();producerEvents.clear();producerScenes.clear();producer={};allow=true;
    calls={FakeSetup,FakeFlush,FakeReserve,FakePeek,FakeCommit,FakePop};
    callbacks={Allow,Begin,End,ProducerBegin,ProducerEnd};
    SetNativeWorldQueueEnabled(false);
}
static void SetupAt(std::uintptr_t stack,bool exact=true) {StartSetup(expectedContext,queue.owner,&flagsValue,true,stack,exact);}
static void TestOrdering() {
    ResetFake();SetupAt(0x1000);CHECK(NativeWorldProducerEye()==-1);CHECK(!FinishLoop(queue.owner,0x1000));
    CHECK(queue.setups==1 && queue.flushes==1 && queue.reserves==0);
    SetNativeWorldQueueEnabled(true);allow=false;SetupAt(0x1000);CHECK(!FinishLoop(queue.owner,0x1000));CHECK(queue.reserves==0);
    allow=true;SetupAt(0x1000,false);CHECK(!FinishLoop(queue.owner,0x1000));CHECK(queue.reserves==0);
    SetupAt(0x1000);const auto frame=NativeWorldProducerFrame();CHECK(frame && NativeWorldFrameValid(frame));CHECK(NativeWorldProducerEye()==0);
    SetupAt(0x2000);CHECK(NativeWorldProducerFrame()==frame && NativeWorldProducerEye()==0);
    CHECK(!FinishLoop(queue.owner,0x2000));CHECK(NativeWorldProducerEye()==0); // nested scene is never replayed
    CHECK(FinishLoop(queue.owner,0x1000));CHECK(NativeWorldProducerEye()==-1);Drain();
    CHECK(events.size()==2 && events[0].kind==0 && events[1].kind==1 && events[0].eye==0 && events[1].eye==0);
    SetupAt(0x1000);CHECK(NativeWorldProducerFrame()==frame && NativeWorldProducerEye()==1);
    CHECK(!FinishLoop(queue.owner,0x1000));CHECK(NativeWorldProducerEye()==-1 && !NativeWorldProducerFrame());Drain();
    CHECK(events.size()==4);
    for(unsigned i=0;i<4 && i<events.size();++i){CHECK(events[i].frame==frame && events[i].eye==i/2 && events[i].kind==i%2 && events[i].valid);}
    CHECK(queue.reserves==8 && queue.commits==8 && queue.peeks==8 && queue.pops==8);
    CHECK(queue.setups==6 && queue.flushes==6);
    CHECK(producerEvents.size()==2 && producerEvents[0]==0 && producerEvents[1]==1);
    CHECK(producerScenes.size()==2 && !producerScenes[0] && !producerScenes[1]);
}
static void TestCancellationWrapAndThreads() {
    ResetFake();SetNativeWorldQueueEnabled(true);
    queue.write=queue.read=127; // handler wraps; its separate two-unit payload is at0
    SetupAt(0x5000);const auto frame=NativeWorldProducerFrame();CHECK(NativeWorldProducerEye()==0);
    bool threadClean=false;std::thread worker([&]{threadClean=NativeWorldProducerEye()==-1 && !NativeWorldProducerFrame();});worker.join();CHECK(threadClean);
    SetNativeWorldQueueEnabled(false);CHECK(!NativeWorldFrameValid(frame));
    CHECK(FinishLoop(queue.owner,0x5000));SetupAt(0x5000);CHECK(NativeWorldProducerEye()==1);
    CHECK(!FinishLoop(queue.owner,0x5000));Drain();
    CHECK(events.size()==4);for(const auto& event:events)CHECK(!event.valid && event.frame==frame);
    SetNativeWorldQueueEnabled(true);CHECK(!NativeWorldFrameValid(frame));
    SetupAt(0x5000);const auto next=NativeWorldProducerFrame();CHECK(next!=frame && NativeWorldFrameValid(next));
    CHECK(FinishLoop(queue.owner,0x5000));SetupAt(0x5000);CHECK(!FinishLoop(queue.owner,0x5000));Drain();
    CHECK(events.size()==8);
    for(unsigned i=4;i<8 && i<events.size();++i)CHECK(events[i].valid && events[i].frame==next);
    ResetFake();
}

struct alignas(16) RegisterState {std::uint64_t gpr[16]{},flags=0,pad=0;unsigned char xmm[16][16]{};};
static void EmitStore(Emitter& e,unsigned reg,void* target) {
    e.Byte(static_cast<unsigned char>(0x48|(reg>=8?4:0)));e.Byte(0x89);e.Byte(static_cast<unsigned char>(5+((reg&7)<<3)));
    const auto next=reinterpret_cast<std::intptr_t>(e.memory+e.pos+4);
    e.U32(static_cast<std::uint32_t>(reinterpret_cast<std::intptr_t>(target)-next));
}
static void EmitLoad(Emitter& e,unsigned reg,const void* source) {
    e.Byte(static_cast<unsigned char>(0x48|(reg>=8?4:0)));e.Byte(0x8B);e.Byte(static_cast<unsigned char>(5+((reg&7)<<3)));
    const auto next=reinterpret_cast<std::intptr_t>(e.memory+e.pos+4);
    e.U32(static_cast<std::uint32_t>(reinterpret_cast<std::intptr_t>(source)-next));
}
static void EmitXmm(Emitter& e,unsigned xmm,void* target,bool store) {
    e.Byte(0xF3);if(xmm>=8)e.Byte(0x44);e.Byte(0x0F);e.Byte(store?0x7F:0x6F);e.Byte(static_cast<unsigned char>(5+((xmm&7)<<3)));
    const auto next=reinterpret_cast<std::intptr_t>(e.memory+e.pos+4);
    e.U32(static_cast<std::uint32_t>(reinterpret_cast<std::intptr_t>(target)-next));
}
static void EmitState(Emitter& e,RegisterState* state) {
    for(unsigned reg=0;reg<16;++reg)EmitStore(e,reg,&state->gpr[reg]);
    for(unsigned xmm=0;xmm<16;++xmm)EmitXmm(e,xmm,state->xmm[xmm],true);
    e.Byte(0x9C);e.Byte(0x58);EmitStore(e,0,&state->flags);EmitLoad(e,0,&state->gpr[0]);
}
static void TestBridgeRegisters(bool repeat) {
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    CHECK(block);if(!block)return;
    auto* before=reinterpret_cast<RegisterState*>(block+4096);
    auto* after=reinterpret_cast<RegisterState*>(block+4608);
    auto* input=reinterpret_cast<RegisterState*>(block+5120);
    auto* helper=reinterpret_cast<std::uint64_t*>(block+5632);
    auto* branch=reinterpret_cast<unsigned*>(block+5680);
    for(unsigned reg=0;reg<16;++reg)input->gpr[reg]=0xA000000000000000ull+reg*0x111111111111ull;
    for(unsigned xmm=0;xmm<16;++xmm)for(unsigned b=0;b<16;++b)input->xmm[xmm][b]=static_cast<unsigned char>(0x30+xmm*17+b);
    // A native-like caller preserving its own nonvolatiles, with RSP%16==0
    // at our JMP site. The helper intentionally destroys volatile registers.
    Emitter harness{block+1024};
    const unsigned char save[]={0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xC8,0,0,0};harness.Bytes(save);
    for(unsigned xmm=6;xmm<16;++xmm) {
        harness.Byte(0xF3);if(xmm>=8)harness.Byte(0x44);harness.Byte(0x0F);harness.Byte(0x7F);harness.Byte(static_cast<unsigned char>(0x84+((xmm&7)<<3)));harness.Byte(0x24);harness.U32(0x20+(xmm-6)*16);
    }
    for(unsigned reg=0;reg<16;++reg)if(reg!=4)EmitLoad(harness,reg,&input->gpr[reg]);
    for(unsigned xmm=0;xmm<16;++xmm)EmitXmm(harness,xmm,input->xmm[xmm],false);
    harness.Byte(0x68);harness.U32(0x246);harness.Byte(0x9D);
    EmitState(harness,before);harness.Jump(block);
    Emitter finish{block+1792};EmitStore(finish,1,&helper[0]);EmitStore(finish,2,&helper[1]);EmitStore(finish,4,&helper[2]);EmitStore(finish,8,&helper[3]);
    const unsigned char shadow[]={0x48,0xC7,0x44,0x24,0x08,0xEF,0xBE,0x00,0x00};finish.Bytes(shadow);
    for(unsigned xmm=0;xmm<6;++xmm){finish.Byte(0x66);finish.Byte(0x0F);finish.Byte(0xEF);finish.Byte(static_cast<unsigned char>(0xC0+xmm*9));}
    for(unsigned reg:{1u,2u,8u,9u,10u,11u}){finish.Byte(reg<8?0x48:0x49);finish.Byte(static_cast<unsigned char>(0xB8+(reg&7)));finish.U64(reg*0x5555);}
    finish.Byte(0xB8);finish.U32(repeat?1:0);finish.Byte(0xF9);finish.Byte(0xC3);
    Emitter continuation{block+2304};EmitState(continuation,after);
    for(unsigned xmm=6;xmm<16;++xmm) {
        continuation.Byte(0xF3);if(xmm>=8)continuation.Byte(0x44);continuation.Byte(0x0F);continuation.Byte(0x6F);continuation.Byte(static_cast<unsigned char>(0x84+((xmm&7)<<3)));continuation.Byte(0x24);continuation.U32(0x20+(xmm-6)*16);
    }
    const unsigned char restore[]={0x48,0x81,0xC4,0xC8,0,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D,0x5B,0xC3};continuation.Bytes(restore);
    for(unsigned which=0;which<2;++which) {
        Emitter choice{block+2048+which*64};choice.Byte(0xC7);choice.Byte(0x05);
        const auto next=reinterpret_cast<std::intptr_t>(choice.memory+choice.pos+8);
        choice.U32(static_cast<std::uint32_t>(reinterpret_cast<std::intptr_t>(branch)-next));choice.U32(which);
        choice.Jump(block+2304);
    }
    const RUNTIME_FUNCTION unused{};const auto layout=EmitBridge(block,block+1792,block+2112,block+2048,unused);
    CHECK(layout.codeSize<512 && layout.prologueLength==0);
    FlushInstructionCache(GetCurrentProcess(),block,4096);
    reinterpret_cast<void(*)()>(block+1024)();
    CHECK(*branch==(repeat?1u:0u));
    for(unsigned reg:{3u,4u,5u,6u,7u,12u,13u,14u,15u})CHECK(before->gpr[reg]==after->gpr[reg]);
    for(unsigned xmm=6;xmm<16;++xmm)CHECK(!std::memcmp(before->xmm[xmm],after->xmm[xmm],16));
    CHECK(helper[0]==input->gpr[1] && helper[1]==before->gpr[4]);
    CHECK(helper[3]==input->gpr[7]); // native scene RDI is forwarded as third argument
    CHECK((helper[2]&15)==8); // Win64 callee entry alignment,32-byte shadow space exercised
    VirtualFree(block,0,MEM_RELEASE);
}
static unsigned preparedCount=0,worldCount=0;
static bool cancelInBody=false;
static std::vector<int> bodyEyes;
static void __fastcall FakeWorldBody() {
    ++worldCount;bodyEyes.push_back(NativeWorldProducerEye());
    if(cancelInBody && worldCount==1)SetNativeWorldQueueEnabled(false);
}
static void TestActualStackLoop(bool active,bool cancel) {
    ResetFake();preparedCount=worldCount=0;bodyEyes.clear();cancelInBody=cancel;
    SetNativeWorldQueueEnabled(active);
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    CHECK(block);if(!block)return;
    Emitter function{block+512};
    const unsigned char prologue[]={0x57,0x48,0x83,0xEC,0x20};function.Bytes(prologue);
    auto* const scene=reinterpret_cast<void*>(std::uintptr_t{0x98765400});
    function.Byte(0x48);function.Byte(0xBF);function.U64(reinterpret_cast<std::uint64_t>(scene));
    function.Byte(0x48);function.Byte(0xB8);function.U64(reinterpret_cast<std::uint64_t>(&preparedCount));function.Byte(0xFF);function.Byte(0x00);
    auto* restart=function.memory+function.pos;
    function.Byte(0x48);function.Byte(0xB9);function.U64(reinterpret_cast<std::uint64_t>(expectedContext));
    function.Byte(0x48);function.Byte(0xBA);function.U64(reinterpret_cast<std::uint64_t>(queue.owner));
    function.Byte(0x49);function.Byte(0xB8);function.U64(reinterpret_cast<std::uint64_t>(&flagsValue));
    function.Byte(0x41);function.Byte(0xB9);function.U32(1);
    function.Byte(0x48);function.Byte(0xB8);function.U64(reinterpret_cast<std::uint64_t>(&HookSetup));function.Byte(0xFF);function.Byte(0xD0);
    expectedSetupReturn=function.memory+function.pos;
    function.Byte(0x48);function.Byte(0xB8);function.U64(reinterpret_cast<std::uint64_t>(&FakeWorldBody));function.Byte(0xFF);function.Byte(0xD0);
    function.Byte(0x48);function.Byte(0xB9);function.U64(reinterpret_cast<std::uint64_t>(queue.owner));function.Jump(block);
    auto* continuation=function.memory+function.pos;
    const unsigned char epilogue[]={0x48,0x83,0xC4,0x20,0x5F,0xC3};function.Bytes(epilogue);
    const RUNTIME_FUNCTION unused{};EmitBridge(block,reinterpret_cast<void*>(&FinishLoop),restart,continuation,unused);
    FlushInstructionCache(GetCurrentProcess(),block,4096);
    reinterpret_cast<void(*)()>(block+512)();Drain();
    CHECK(preparedCount==1 && worldCount==(active?2u:1u));
    CHECK(queue.setups==(active?2u:1u) && queue.flushes==(active?2u:1u));
    CHECK(events.size()==(active?4u:0u));
    CHECK(bodyEyes[0]==(active?0:-1));if(active)CHECK(bodyEyes[1]==1);
    for(const auto& event:events)CHECK(event.valid==!cancel);
    CHECK(producerScenes.size()==(active?2u:0u));
    for(const auto* capturedScene:producerScenes)CHECK(capturedScene==scene);
    CHECK(NativeWorldProducerEye()==-1 && !NativeWorldProducerFrame());
    expectedSetupReturn=nullptr;VirtualFree(block,0,MEM_RELEASE);ResetFake();
}
static void TestProfileAndUnwind(const wchar_t* file) {
    const auto mapped=LoadLibraryExW(file,nullptr,DONT_RESOLVE_DLL_REFERENCES);CHECK(mapped);if(!mapped)return;
    ImageProfile image{};char reason[256]{};CHECK(CheckImage(mapped,image,reason,sizeof(reason)));if(!image.base)return;
    RUNTIME_FUNCTION parent{};CHECK(ValidateImage(image,parent));
    DWORD old=0;CHECK(VirtualProtect(image.base+kRestart,16,PAGE_EXECUTE_READWRITE,&old)!=0);
    image.base[kRestart+4]^=1;CHECK(!ValidateImage(image,parent));image.base[kRestart+4]^=1;
    DWORD ignored=0;VirtualProtect(image.base+kRestart,16,old,&ignored);CHECK(ValidateImage(image,parent));
    const NativeWorldQueueCallbacks callbacksForTest{Allow,Begin,End};
    CHECK(InstallNativeWorldQueue(image,callbacksForTest));CHECK(!InstallNativeWorldQueue(image,callbacksForTest));
    CHECK(image.base[kSetupSite]==0xE8 && image.base[kEndSite]==0xE9);
    DWORD64 lookupBase=0;const auto* found=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(codePage+15),&lookupBase,nullptr);
    CHECK(found && lookupBase==reinterpret_cast<DWORD64>(image.base));
    // Reconstruct the original11978D0 frame, then unwind from the bridge's
    // completed prologue. Chaining must recover that function's caller, not
    // mistake its local stack for a synthetic return address.
    alignas(16) unsigned char stack[4096]{};
    auto* entry=stack+0xC08;auto* nativeStack=entry-0x4A8;
    auto write=[&](std::ptrdiff_t offset,std::uint64_t value){std::memcpy(entry+offset,&value,8);};
    write(0,0x1234567890ABCDEF);write(-8,0xB0);write(-16,0xC0);write(-24,0xD0);write(-32,0xE0);write(-40,0xF0);
    write(16,0x30);write(24,0x60);write(32,0x70);
    CONTEXT context{};context.ContextFlags=CONTEXT_FULL;context.Rip=reinterpret_cast<DWORD64>(codePage+15);context.Rsp=reinterpret_cast<DWORD64>(nativeStack);
    PVOID handlerData=nullptr;DWORD64 establisher=0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER,reinterpret_cast<DWORD64>(image.base),context.Rip,&bridgeFunction,&context,&handlerData,&establisher,nullptr);
    CHECK(context.Rip==0x1234567890ABCDEF && context.Rsp==reinterpret_cast<DWORD64>(entry+8));
    if(context.Rbx!=0x30 || context.Rsi!=0x60 || context.Rdi!=0x70 || context.Rbp!=0xB0)
        std::printf("Unwind registers rbx=%llx rsi=%llx rdi=%llx rbp=%llx\n",context.Rbx,context.Rsi,context.Rdi,context.Rbp);
    CHECK(context.Rbx==0x30 && context.Rsi==0x60 && context.Rdi==0x70 && context.Rbp==0xB0);
    CHECK(context.R12==0xC0 && context.R13==0xD0 && context.R14==0xE0 && context.R15==0xF0);
    // Do not execute any mapped EDF/Umbra code. Both hooks are tested against
    // synthetic native calls above. The mapped image/table stay resident until
    // process exit, matching the production process-lifetime hook ownership.
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    TestOrdering();TestCancellationWrapAndThreads();TestBridgeRegisters(false);TestBridgeRegisters(true);
    TestActualStackLoop(false,false);TestActualStackLoop(true,false);TestActualStackLoop(true,true);TestProfileAndUnwind(argv[1]);
    std::printf("Native world queue: %u failures; native queue/simulation not executed\n",failures);return failures?1:0;
}
