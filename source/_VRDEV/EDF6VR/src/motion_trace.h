#pragma once
#include "camera_math.h"
#include <openxr/openxr.h>
#include <cstdint>
namespace edf6vr {
struct HmdSample;
using MotionLog=void(*)(const char*);
// Bounded, read-only trace. Render/input callbacks only copy POD to RAM.
// No GPU readbacks, file writes, waits or changes to rendering/pose selection.
void ConfigureMotionTrace(bool,const wchar_t* directory,MotionLog) noexcept;
bool MotionTraceEnabled() noexcept;
void TraceCamera(const Matrix&,const HmdSample&,float yawOffset,unsigned kind) noexcept;
void TraceFrameBegin(XrTime predicted,XrDuration period) noexcept;
void TraceNativeMatrix(const float* matrix,bool projection,int handedness) noexcept;
void TracePresent() noexcept;
void TraceFrameEnd(const XrCompositionLayerProjectionView* views,unsigned count,
                   const HmdSample& latest,XrResult result,const XrView* located=nullptr) noexcept;
void FinishMotionTrace() noexcept;
// Background watchdog only. Writes completed immutable trace; never calls D3D/XR.
void FlushMotionTrace() noexcept;
}
