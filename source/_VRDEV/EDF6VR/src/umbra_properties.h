#pragma once
// [Render] OcclusionLeafOnly: Umbra's camera property 4 on the two eyes' main
// views -- occlusion queries for the leaves of its hierarchy only, none for
// the inner nodes. Found by measuring every property bit the game leaves off
// (the user, 2026-10-12, after the CPU measurement showed the drawing thread
// spinning 40% of the time for occlusion-query answers: "グラボの返事待ちを減らす
// 方法を他にも調べて"). Without it Umbra tests a node, waits for the card's answer,
// then tests its children, level by level; with it the leaves are asked
// together and the levels' round trips go. What is drawn is the same: a leaf
// inside a hidden node is hidden itself (268xx: with internal 0x10000 a node
// that has children goes straight to the traversal, 286F0, instead of to a
// query). Virtual headset, the save's mission, 3488x3240 an eye: 20.67 ms a
// frame without (8 runs, 20.3-21.1), 18.06 with (5 runs, 17.9-18.1): -12.6%;
// waits 7.9 -> 5.5 ms a frame; draws the same (300 against 304 a frame).
// On hardware (the user's A/B in the Deroy fight the same day, switching the
// in-game row; SteamVR motion smoothing off): the work of a frame 43.3 -> 36.9
// ms (-15%), occlusion waits 6.2 -> 2.1 ms a frame; shown FPS 22.9 -> 25.5
// there, 28 -> 37 in a lighter stretch (the runtime holds frames to whole
// multiples of 13.3 ms); "見た目は問題なかったので規定オンにして". On by default
// since 3.2.1, still called a trial: read again while the game runs, a row of
// the in-game "VR setting" menu and of the settings program to turn it off.
//
// [Test] UmbraCameraProperties / UmbraCameraPropertiesRight: any other bits,
// for the same kind of measurement. What they did (one or two runs each):
//   10  nothing measurable (20.9)
//   20  a query for every object: 2750 waits a frame, 36-41 ms
//   40  no queries at all, the state of the pass before is used as it stands:
//       on both eyes nothing is ever tested again (16.95 ms, but what was
//       hidden stays hidden); on the RIGHT eye only it reuses the left eye's
//       result of the same frame: 18.8 ms alone, 17.45 with property 4 (-0.6
//       on top of it) -- not taken: an object only the right eye can see past
//       the edge of something near would be missing in that eye
//   80  no queries for objects, nodes only (sets 4 too): 18.6-19.8, more drawn
//
// umbra_sandlot.dll is Umbra Occlusion Booster (2006-2009) built from source
// by the game's maker. Camera::setProperties takes eight public bits and turns
// them into the camera's internal flags (2B4B8..2B51A):
//   01 -> 0x800     view frustum culling        (the game sets it)
//   02 -> 0x400     occlusion culling           (the game sets it)
//   08 -> 0x1000    the depth pass, command 3   (the game sets it, less on some condition)
//   04 -> 0x10000   ?  an object that already has a query is reported at once (269B5)
//   10 -> 0x100000  ?
//   20 -> 0x80000   ?  with 0x400000 clear: objects are reported without a query of their own (269E6)
//   40 -> 0x200000  ?  (26A1C)
//   80 -> 0x400000  ?
// The game sets the properties before every resolveVisibility (11B6800,
// 11B6B14, 11B6FF9, 11B71C4; 0xB at creation, 11D4526), so the bits are added
// where the call lands: EDF.dll's import slot for Camera::setProperties (the
// thunk at 12D611C). Only the cameras seen running a main view (mode 0) of an
// eye get them; the shadow and far cameras are left as the game set them.
int g_umbraExtraProperties=0;
// (g_occlusionLeafOnly, [Render] OcclusionLeafOnly, is declared at the top of plugin.cpp: ReloadTunables reads it.)
// [Test] UmbraCameraPropertiesRight: more bits for the right eye's pass only.
// Both eyes' main views run on ONE Umbra camera, its matrix moved between the
// two processVisibility calls of a frame (one "main view camera noted" in the
// log), so to Umbra the right eye is the next frame of the same camera. The
// game sets the properties and calls resolveVisibility, which starts the
// traversal, on its loop thread while it prepares an eye (the producer's eye,
// NativeWorldProducerEye; the drawing thread's eye is not current then: 5773
// of 5778 calls), twice a frame for the main camera. Bits set later, around
// processVisibility on the drawing thread, change nothing (tried: the waits
// stayed as they were).
int g_umbraExtraRight=0;
std::atomic<unsigned long long> g_umbraPropertyByEye[3]{};   // calls on the main camera while eye 0, eye 1, neither is prepared
using UmbraSetProperties=void(__fastcall*)(void*,unsigned);
UmbraSetProperties g_umbraSetProperties=nullptr;
std::atomic<void*> g_umbraMainCameras[4]{};
std::atomic<unsigned long long> g_umbraPropertyCalls{0},g_umbraPropertyChanged{0};
std::atomic<unsigned> g_umbraPropertySeen{0};      // every value the game passed, OR-ed
std::atomic<unsigned> g_umbraPropertyMainSeen{0};  // the same, for the main cameras only
void NoteUmbraMainCamera(void* camera) noexcept {
    for(auto& slot:g_umbraMainCameras) {
        void* have=slot.load(std::memory_order_relaxed);
        if(have==camera) return;
        if(!have) { slot.store(camera,std::memory_order_relaxed); Log("UMBRAPROPS main view camera %p noted",camera); return; }
    }
}
void __fastcall HookUmbraSetProperties(void* camera,unsigned properties) {
    g_umbraPropertyCalls.fetch_add(1,std::memory_order_relaxed);
    g_umbraPropertySeen.fetch_or(properties,std::memory_order_relaxed);
    for(auto& slot:g_umbraMainCameras)
        if(slot.load(std::memory_order_relaxed)==camera) {
            g_umbraPropertyMainSeen.fetch_or(properties,std::memory_order_relaxed);
            const int eye=edf6vr::NativeWorldProducerEye();
            g_umbraPropertyByEye[eye==0?0:eye==1?1:2].fetch_add(1,std::memory_order_relaxed);
            properties|=static_cast<unsigned>(g_umbraExtraProperties);
            if(g_occlusionLeafOnly.load(std::memory_order_relaxed)) properties|=4u;
            if(eye==1) properties|=static_cast<unsigned>(g_umbraExtraRight);
            g_umbraPropertyChanged.fetch_add(1,std::memory_order_relaxed);
            break;
        }
    g_umbraSetProperties(camera,properties);
    static std::atomic<ULONGLONG> said{0};
    const ULONGLONG now=GetTickCount64();
    ULONGLONG last=said.load(std::memory_order_relaxed);
    if(now-last>5000 && said.compare_exchange_strong(last,now))
        Log("UMBRAPROPS leafOnly=%d extra=0x%X right=0x%X calls=%llu onMainCameras=%llu (eye0 %llu, eye1 %llu, neither %llu) gameGave=0x%X (main cameras 0x%X)",
            g_occlusionLeafOnly.load()?1:0,static_cast<unsigned>(g_umbraExtraProperties),static_cast<unsigned>(g_umbraExtraRight),
            g_umbraPropertyCalls.load(),g_umbraPropertyChanged.load(),g_umbraPropertyByEye[0].load(),g_umbraPropertyByEye[1].load(),
            g_umbraPropertyByEye[2].load(),g_umbraPropertySeen.load(),g_umbraPropertyMainSeen.load());
}
bool InstallUmbraProperties() noexcept {
    __try {
        auto* thunk=g_image.base+0x12D611C;                  // jmp [Camera::setProperties]
        if(thunk[0]!=0xFF || thunk[1]!=0x25) return false;
        std::int32_t offset=0; std::memcpy(&offset,thunk+2,4);
        auto** slot=reinterpret_cast<void**>(thunk+6+offset);
        g_umbraSetProperties=reinterpret_cast<UmbraSetProperties>(*slot);
        if(!g_umbraSetProperties) return false;
        bool changed=false;
        return edf6vr::ReplacePointer(slot,reinterpret_cast<void*>(g_umbraSetProperties),reinterpret_cast<void*>(&HookUmbraSetProperties),changed);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
