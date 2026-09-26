#pragma once
#include "image_profile.h"
#include <cstddef>
namespace edf6vr {
// Size of the other-player nameplate (HudPlayer_MultiPlayStatus).
//
// The per-plate draw (0x804C90) builds the plate's transform three times --
// identity plus the projected anchor, applied to the render state by 0x7FC760
// -- once for the icons and bars, again (0x805F87) before the text lines and
// again (0x806171) for the class letter. Everything is drawn relative to the
// transform current at the time, so rewriting the identity's x and y
// immediates (1.0f) in all three builds scales the whole plate, offsets
// included, about its anchor. Hardware-confirmed in steps: the first build
// alone shrank icons and bars only; scaling glyphs alone scattered the text.
// The text glyph size can still be scaled on top through the one style-size
// setter (0x113A3F0: [rcx+4]=xmm1, [rcx+8]=xmm2) all six call sites reach.
constexpr unsigned kPlateScaleXRva=0x80565B;   // 48 C7 45 00 <imm32>  mov qword [rbp], 1.0f
constexpr unsigned kPlateScaleYRva=0x805654;   // C7 45 14 <imm32>     mov dword [rbp+0x14], 1.0f
constexpr unsigned kPlateTextScaleXRva=0x805F02;   // 48 C7 85 C0 00 00 00 <imm32>
constexpr unsigned kPlateTextScaleYRva=0x805EF8;   // C7 85 D4 00 00 00 <imm32>
constexpr unsigned kPlateLetterScaleXRva=0x806127; // 48 C7 85 00 01 00 00 <imm32>
constexpr unsigned kPlateLetterScaleYRva=0x80611D; // C7 85 14 01 00 00 <imm32>
constexpr unsigned kPlateTextSizeSetterRva=0x113A3F0;
constexpr unsigned kPlateTextSizeCallRvas[]={0x80503E,0x805081,0x806244,0x806354,0x806416,0x806450};
// Writes the scales (1.0 = as shipped). The immediates are checked against the
// shipped bytes before the first write and the call sites before the first
// redirect; later calls only change the values, so the INI can be edited while
// the game runs. Refuses out-of-range scales.
bool ApplyNameplateScale(const ImageProfile& image,float plateScale,float textScale,char* reason,std::size_t capacity) noexcept;
// The replacement setter (exposed for the test, which calls it through the
// redirected site's thunk).
void NameplateTextSizeHook(void* style,float x,float y) noexcept;
}
