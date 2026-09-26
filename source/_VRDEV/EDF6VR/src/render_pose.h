#pragma once
#include "openxr_session.h"
#include <openxr/openxr.h>
#include <array>
#include <limits>
namespace edf6vr {
// CPU camera history only. No game memory, rendering, input or XR calls.
// yawOffset: the stick turn the camera was built with, NaN when not known.
struct RenderCameraRecord { Matrix camera{}; HmdSample head{}; std::int64_t at=0;
                            float yawOffset=std::numeric_limits<float>::quiet_NaN(); };
class RenderCameraHistory {
public:
    void Record(const Matrix&,const HmdSample&,std::int64_t now,
                float yawOffset=std::numeric_limits<float>::quiet_NaN()) noexcept;
    bool Match(const Matrix& nativeView,std::int64_t now,std::int64_t maxAge,
               HmdSample& out,float* yawOffset=nullptr) const noexcept;
private:
    std::array<RenderCameraRecord,128> entries{};
    unsigned next=0,count=0;
};
// Both location and views must be from the same xrLocate prediction/reference
// space. Transport their rigid head-to-eye relation onto the rendered sample.
bool RenderedEyePoses(const HmdSample&,const XrSpaceLocation&,const XrView*,
                     XrViewStateFlags,unsigned count,XrPosef out[2]) noexcept;
// Native world pairs use parallel cameras with one identical projection. Their
// submitted poses must describe those actual cameras, not the runtime's canted
// optical views. physicalIpd is the unscaled distance in XR metres; any game
// world-scale setting belongs in the game camera offset, not this metadata.
// The caller must have matched this rendered head to the baseline image, and
// applied both eye offsets along the final (including roll) camera right axis.
// The existing RenderedEyePoses path remains unchanged for legacy rendering.
bool ParallelRenderedEyePoses(const HmdSample&,float physicalIpd,XrPosef out[2]) noexcept;
void ConfigureRenderedPose(bool enabled) noexcept;
void ResetRenderedPoseHistory() noexcept;
void RecordRenderedCamera(const Matrix&,const HmdSample&,
                          float yawOffset=std::numeric_limits<float>::quiet_NaN()) noexcept;
// Publish matching metadata BEFORE exposing a new camera to the native producer.
void PublishRenderedCamera(Matrix& destination,const Matrix&,const HmdSample&,
                           float yawOffset=std::numeric_limits<float>::quiet_NaN()) noexcept;
void BeginNativeRenderPose() noexcept;
void ObserveNativeRenderView(const Matrix&) noexcept;
bool ConsumeNativeRenderPose(HmdSample&) noexcept;
// The stick turn of the camera this thread is drawing now: the yaw offset
// recorded with the camera ObserveNativeRenderView matched, until the frame is
// consumed. False without a match or when that camera was recorded without one.
bool NativeRenderYaw(float& yawOffset) noexcept;
bool RenderedPoseEnabled() noexcept;
}
