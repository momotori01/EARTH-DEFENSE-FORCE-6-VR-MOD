#pragma once
#include "image_profile.h"
#include <cstdint>
namespace edf6vr {
inline constexpr unsigned kRadarAngleCall=0x82B6DB;
inline constexpr unsigned kDirectionAngles=0x4E100;
struct RadarHeadingStats {
    std::uint64_t calls=0, applied=0;
    float nativeYaw=0, headYaw=0;
};
// Independent, short lock; never acquires the camera/update lock.
void PublishRadarHeading(const void* soldier,float yaw,ULONGLONG now) noexcept;
void ClearRadarHeading() noexcept;
bool ApplyRadarHeading(float* angles,const void* soldier,ULONGLONG now) noexcept;
RadarHeadingStats ReadRadarHeadingStats() noexcept;
bool InstallRadarHeading(const ImageProfile& image,bool& changed) noexcept;
}
