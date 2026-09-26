#pragma once
#include "image_profile.h"
namespace edf6vr {
inline constexpr std::uint32_t kResolutionGetterRva=0x1183310;
// Install at plugin load, before the game creates its renderer. Both zero disables.
bool InstallResolutionLock(const ImageProfile&,unsigned width,unsigned height,
                           char* reason,std::size_t capacity) noexcept;
// HudPlayer_Chat scales the text's vertical position by height/(width*9/16)
// before handing it to the font renderer; the bubble is not scaled. On a 16:9
// rectangle that is 1 and nothing shows. With a headset-fitted rectangle it is
// 1.6 and the text sits six lines under its bubble. Installed with the lock.
inline constexpr std::uint32_t kChatTextScaleRva=0x80200A;
bool InstallChatTextScaleFix(const ImageProfile&,char* reason,std::size_t capacity) noexcept;
}
