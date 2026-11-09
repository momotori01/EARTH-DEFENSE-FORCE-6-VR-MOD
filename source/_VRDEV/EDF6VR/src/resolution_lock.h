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
// The game's boot (0x7004F0) writes the screen's sample count, 8, with quality
// D3D11_STANDARD_MULTISAMPLE_PATTERN, before it creates the device. It is the only
// literal sample count in EDF.dll. With anti-aliasing on, the swapchain (0x111B220)
// takes the highest count up to it that the D24S8 depth supports. The world is drawn
// without MSAA, so the samples only carry the final screen, its depth and the HUD
// copy (about 1 GB at 8 on a 3184x3240 screen). 4 or 2 rewrite the 8; 8 leaves it.
inline constexpr std::uint32_t kGameMsaaCountRva=0x7006BE;
bool InstallGameMsaa(const ImageProfile&,unsigned samples,char* reason,std::size_t capacity) noexcept;
}
