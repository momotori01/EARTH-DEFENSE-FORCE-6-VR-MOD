#include "../src/native_world_composite.cpp"
#include <array>
#include <cstdio>
#include <thread>
#include <vector>

using namespace edf6vr;
static unsigned failures=0;
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(false)
alignas(16) static std::array<unsigned char,0x90> app{};
static std::array<unsigned char,16> table{};
static std::array<unsigned char,16> state{},owner{},scene{};
static void* sceneSlot=scene.data();
static ULONGLONG now=1000;
static ULONGLONG WINAPI FakeClock(){return now;}
static std::vector<unsigned> commands;
static unsigned nativeCalls=0;
static void* nativeApplication=nullptr;
static void __fastcall FakeComposite(void* application) {
    ++nativeCalls;nativeApplication=application;commands.push_back(1);
    // Engine calls must never run under the state lock.
    const auto acquired=TryAcquireSRWLockExclusive(&applicationLock);
    CHECK(acquired!=0);if(acquired)ReleaseSRWLockExclusive(&applicationLock);
    if(enqueueingComposite) {
        CHECK(!NativeWorldCompositeAvailable());
        CHECK(!EnqueueNativeWorldComposite(scene.data()));
    }
}
static void __fastcall FakeSetup(void* renderState,void* queue,const void* flags,bool set) {
    CHECK(renderState==state.data() && queue==owner.data());
    CHECK(flags && *static_cast<const unsigned char*>(flags)==3 && !set);
    commands.push_back(0);
}
static void Reset() {
    app={};*reinterpret_cast<void**>(app.data())=table.data();sceneSlot=scene.data();
    compositeCalls={FakeComposite,FakeSetup,state.data(),owner.data(),&sceneSlot,table.data()};
    applicationSnapshot={};compositeClock=FakeClock;compositeThread=GetCurrentThreadId;
    compositeInstalled=true;compositeImage=nullptr;enqueueingComposite=false;
    now=1000;commands.clear();nativeCalls=0;nativeApplication=nullptr;
}
static void TestForwardAndOrder() {
    Reset();CHECK(!NativeWorldCompositeAvailable());CHECK(!EnqueueNativeWorldComposite(scene.data()));
    // Execute the same generated Win64 jump thunk used by the call redirect.
    auto* thunk=AllocateNearThunk(reinterpret_cast<void*>(&FakeComposite),reinterpret_cast<void*>(&HookWorldComposite));
    CHECK(thunk);if(!thunk)return;
    reinterpret_cast<Composite>(thunk)(app.data());
    CHECK(nativeCalls==1 && nativeApplication==app.data() && NativeWorldCompositeAvailable());
    VirtualFree(thunk,0,MEM_RELEASE);
    commands.clear();CHECK(EnqueueNativeWorldComposite(scene.data()));commands.push_back(2); // caller's subsequent eye-end marker
    CHECK(commands.size()==3 && commands[0]==0 && commands[1]==1 && commands[2]==2);
    CHECK(nativeCalls==2 && NativeWorldCompositeAvailable());
    // Normal original calls still forward once after paired composition.
    HookWorldComposite(app.data());CHECK(nativeCalls==3);
}
static void TestRejections() {
    Reset();HookWorldComposite(app.data());commands.clear();const auto before=nativeCalls;
    CHECK(!EnqueueNativeWorldComposite(nullptr));CHECK(!EnqueueNativeWorldComposite(owner.data()));
    void* originalScene=sceneSlot;sceneSlot=nullptr;CHECK(!EnqueueNativeWorldComposite(originalScene));sceneSlot=originalScene;
    bool rejected=false;std::thread other([&]{rejected=!NativeWorldCompositeAvailable() && !EnqueueNativeWorldComposite(scene.data());});other.join();CHECK(rejected);
    now=1251;CHECK(!NativeWorldCompositeAvailable() && !EnqueueNativeWorldComposite(scene.data()));
    now=999;CHECK(!NativeWorldCompositeAvailable() && !EnqueueNativeWorldComposite(scene.data()));
    now=1000;*reinterpret_cast<void**>(app.data())=owner.data();CHECK(!NativeWorldCompositeAvailable() && !EnqueueNativeWorldComposite(scene.data()));
    *reinterpret_cast<void**>(app.data())=table.data();compositeInstalled=false;CHECK(!NativeWorldCompositeAvailable() && !EnqueueNativeWorldComposite(scene.data()));
    CHECK(commands.empty() && nativeCalls==before);
    compositeInstalled=true;HookWorldComposite(nullptr);CHECK(nativeCalls==before+1 && !nativeApplication && !NativeWorldCompositeAvailable());
    // Reject unreadable Application memory without invoking a native helper.
    applicationSnapshot={reinterpret_cast<void*>(std::uintptr_t{1}),GetCurrentThreadId(),now};
    CHECK(!NativeWorldCompositeAvailable() && !EnqueueNativeWorldComposite(scene.data()));
    Reset();HookWorldComposite(app.data());now=1250;CHECK(NativeWorldCompositeAvailable()); // inclusive250ms contract
}
static void TestActualProfile(const wchar_t* file) {
    Reset();compositeCalls={};compositeInstalled=false;applicationSnapshot={};
    const auto module=LoadLibraryExW(file,nullptr,DONT_RESOLVE_DLL_REFERENCES);CHECK(module);if(!module)return;
    ImageProfile image{};char reason[256]{};CHECK(CheckImage(module,image,reason,sizeof(reason)));if(!image.base)return;
    CHECK(ValidateCompositeImage(image));
    DWORD previous=0;CHECK(VirtualProtect(image.base+kCompositeSite,5,PAGE_EXECUTE_READWRITE,&previous)!=0);
    image.base[kCompositeSite+1]^=1;CHECK(!ValidateCompositeImage(image));image.base[kCompositeSite+1]^=1;
    DWORD ignored=0;VirtualProtect(image.base+kCompositeSite,5,previous,&ignored);
    CHECK(InstallNativeWorldComposite(image));CHECK(!InstallNativeWorldComposite(image));CHECK(!NativeWorldCompositeAvailable());
    CHECK(image.base[kCompositeSite]==0xE8);
    std::int32_t displacement=0;std::memcpy(&displacement,image.base+kCompositeSite+1,4);
    const auto* thunk=image.base+kCompositeSite+5+displacement;
    CHECK(Readable(thunk,12));CHECK(thunk[0]==0x48 && thunk[1]==0xB8 && thunk[10]==0xFF && thunk[11]==0xE0);
    void* target=nullptr;std::memcpy(&target,thunk+2,8);CHECK(target==reinterpret_cast<void*>(&HookWorldComposite));
    // Do not execute any EDF-native function from the mapped image. Retain the
    // process-lifetime code/mapping just as production's patch ownership does.
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    TestForwardAndOrder();TestRejections();TestActualProfile(argv[1]);
    std::printf("Native world composite: %u failures; native EDF/GPU code not executed\n",failures);
    return failures?1:0;
}
