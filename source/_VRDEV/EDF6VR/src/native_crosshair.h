#pragma once
#include "image_profile.h"
#include <cstdint>
namespace edf6vr {
// The game's own "is the crosshair shown" answer. Every weapon class reaches it:
// 25 vtables point straight at it and the four that override it call it first
// and leave the flag alone. Found from MoistGoat's NoCrosshair.txt in
// _VRDEV/References, whose two patch sites (B001EB2D85, 0FB6838D070000) are the
// two ways this function writes byte +0x18 of the state it fills.
//
// While the VR reticle is drawn, the game's own is switched off here instead of
// cutting the middle out of the HUD panel. The cut took everything else that
// lives in the middle with it: MISSION CLEAR, and the chat bubbles in online play.
inline constexpr std::uint32_t kCrosshairStateRva=0x692100;
inline constexpr std::size_t kCrosshairVisibleOffset=0x18;
inline constexpr std::uint32_t kCrosshairSlotRvas[]={
    0x17E2390, 0x17E26C0, 0x17E3400, 0x17E3668, 0x17E37B8,
    0x17E3908, 0x17E3BC8, 0x17E3E28, 0x17E3FF0, 0x17E4178,
    0x17E4378, 0x17E46C0, 0x17E4828, 0x17E49E0, 0x17E4B58,
    0x17E4EA0, 0x17E5088, 0x17E5278, 0x17E5518, 0x17E5A28,
    0x17E5C38, 0x17E5F18, 0x17E6080, 0x17E61F8, 0x17E63F8};
inline constexpr std::uint32_t kCrosshairCallRvas[]={0x681A7F, 0x683EA0, 0x685985, 0x6AEBB0};
bool InstallNativeCrosshair(const ImageProfile&,bool& changed) noexcept;
bool NativeCrosshairReady() noexcept;
// Keeps the game's crosshair off for the next second. Asked every VR frame the
// VR reticle is drawn, so it comes back on its own when VR stops or a vehicle
// takes over the reticle.
void HideNativeCrosshairFor(unsigned long long now) noexcept;
bool NativeCrosshairHidden(unsigned long long now) noexcept;
struct NativeCrosshairStats { std::uint64_t calls=0,hidden=0; };
NativeCrosshairStats ReadNativeCrosshairStats() noexcept;
}
