#pragma once
#include "image_profile.h"
#include <cstdint>

namespace edf6vr {
// A pair repeats only the native viewport submission loop, not preparation,
// game simulation, the render-thread loop, or a consumed visibility result.
struct NativeWorldQueueCallbacks {
    bool (*allowPair)() noexcept=nullptr;
    void (*begin)(std::uint64_t frame,unsigned eye,void* renderContext) noexcept=nullptr;
    void (*end)(std::uint64_t frame,unsigned eye,void* renderContext) noexcept=nullptr;
    // Producer-side snapshot latch, before an eye's marker or queued commands.
    // Left visibility may already have been resolved by native preparation.
    void (*producerBegin)(std::uint64_t frame,unsigned eye) noexcept=nullptr;
    // Called before the eye's end marker; retain any native final output here.
    // scene is the original11978D0 scene (native nonvolatile RDI), not a queue.
    void (*producerEnd)(std::uint64_t frame,unsigned eye,void* scene) noexcept=nullptr;
};
using NativeWorldQueueLog=void(*)(const char*,...);
bool InstallNativeWorldQueue(const ImageProfile&,const NativeWorldQueueCallbacks&,
                             NativeWorldQueueLog=nullptr) noexcept;
void SetNativeWorldQueueEnabled(bool enabled) noexcept;
// Producer thread only. A pair is latched until both native loops finish.
int NativeWorldProducerEye() noexcept;
std::uint64_t NativeWorldProducerFrame() noexcept;
// Consumers always receive balanced begin/end markers, including cancellation.
// Check this before using images; still clear an obsolete matching scope at end.
bool NativeWorldFrameValid(std::uint64_t frame) noexcept;
struct NativeWorldQueueStats {
    std::uint64_t pairs=0,loops=0,begins=0,ends=0,invalidMarkers=0;
};
NativeWorldQueueStats NativeWorldQueueStatistics() noexcept;
}
