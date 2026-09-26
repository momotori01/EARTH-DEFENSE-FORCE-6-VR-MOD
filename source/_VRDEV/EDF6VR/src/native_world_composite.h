#pragma once
#include "image_profile.h"

namespace edf6vr {
using NativeWorldCompositeLog=void(*)(const char*,...);
// Latches the native Application only at its original world-to-screen call.
bool InstallNativeWorldComposite(const ImageProfile&,NativeWorldCompositeLog=nullptr) noexcept;
// Producer thread only; rejects missing, stale, changed, or cross-thread state.
bool NativeWorldCompositeAvailable() noexcept;
// Enqueues the original HDR-world-to-backbuffer conversion, without HUD,
// gameplay, Present, or a second consumption of any native render packet.
// Call before the native eye-end marker on the same producer thread.
bool EnqueueNativeWorldComposite(void* scene) noexcept;
}
