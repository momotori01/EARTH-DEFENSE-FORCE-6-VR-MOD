#pragma once
#include "../src/xinput_bridge.h"
namespace edf6vr {
inline PadState capturedTestPad{};
inline void CaptureTestPad(const PadState& pad) noexcept {capturedTestPad=pad;SetPadState(pad);}
}
