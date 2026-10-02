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
// Size of the quick chat's speech bubbles (HudPlayer_Chat). The per-bubble
// draw (0x801420) builds the bubble's transform once -- identity plus the
// projected point (0x7FC380), applied by 0x7FC760 -- and draws the frame
// relative to it, so its identity's x and y immediates scale the frame about
// that point. The text is not under it (hardware, 2026-10-02: the frames shrank,
// the text did not): it goes through the text-block draw 0x1171EB0 (called at
// 0x802059) with a matrix of its own at [rbp+0x80] -- rows from shared
// constants, its translation the same point (x [rsp+0x50], y [rsp+0x54], the
// latter after the y fix at kChatTextScaleRva). That one call is redirected and
// the matrix's x and y rows scaled by the same amount, so frame and text shrink
// together about one point; the text is still measured at full size, which is
// what the frame was sized from before both were scaled.
//
// The draw places each run of text (each line) at the matrix's translation plus
// the run's offset, and its last argument (a byte) says whether that offset goes
// through the matrix first (1171F3E). The bubble passes 0 (dil, the frame's
// nine-piece loop counter run down to 0 at 801F89), so with only the rows
// scaled the glyphs would shrink but the lines keep their full-size places --
// what scattered the nameplate's text when only its glyphs were scaled. While
// scaling, the byte is set to 1; with the shipped identity rows the two paths
// give the same place, so at 1.0 nothing changes.
constexpr unsigned kChatScaleXRva=0x801CB4;   // 48 C7 45 30 <imm32>  mov qword [rbp+0x30], 1.0f
constexpr unsigned kChatScaleYRva=0x801CAD;   // C7 45 44 <imm32>     mov dword [rbp+0x44], 1.0f
constexpr unsigned kChatTextDrawCallRva=0x802059;   // E8 -> 0x1171EB0
constexpr unsigned kChatTextDrawRva=0x1171EB0;
// ...and both about the speaker, not about the bubble's corner. The game puts
// the bubble beside the speaker with a full-size gap (801949-801A05: the head
// projected by 7FF870, then the bubble to its right, or to its left on the right
// half of the screen, and below by a line), and the frame's transform and the
// text's matrix both have the bubble's corner as their point -- so shrunk about
// that corner, a small bubble hung a full-size gap away ("かなり遠くに出てる").
// The speaker's point is the head: feet + 1.9 m (0x17F6B50), projected by
// 7FF870 at 801977 into [rbp+0xC0] -- next to where a nameplate hangs (feet +
// 2.0 m, 8051FA). The tail's target at [rbp-0x40] (801F8F, handed to 800C60) is
// not always that point. A bubble's kind is set when it is added (802E37: 1 if
// the speaker is not the HUD's owner): another player's (1) points at the head,
// the local player's own (0) 140 px below it (801A43), and either points at the
// feet when the speaker's byte +0x2E8 is set (801A29) -- so shrunk about the
// tail's target (the build before this one), those slid down the body. Bubbles
// only come from quick chat: the chat window's send (867A44 -> 799240 -> 722860,
// which returns before 7236C0 when there is no room, 7228EE) and messages from
// EOS (a lambda in eos::Core -> 722FB0); both reach 7236C0, which queues the
// event the HUD's listener 800C00 takes (802C00 adds). No NPC line does. The head is
// caught as the game projects it (the call at 801977 redirected; its output is
// what the game reads back), and the corner handed to 7FC380 for the frame
// (801CFA, point at [rbp+0x10], 801CEC) and the text matrix's translation (at
// [rbp+0x80], 802041) are each pulled to head + scale x (corner - head): the
// whole bubble, gap and tail included, is the full-size one shrunk about the
// head, as a nameplate is about its own point, and the tail -- drawn under the
// frame's transform at its full-size offset -- points the same way, shorter.
// A speaker that shares the HUD owner's object at +0x1548 (8024D0; the 801853
// path) projects no head; it keeps the tail's target. Frame offsets are read
// through the pointers the calls are given (their lea instructions checked).
constexpr unsigned kChatAnchorCallRva=0x801CFA;    // E8 -> 0x7FC380, after lea r8,[rbp+0x10] at 0x801CEC
constexpr unsigned kChatAnchorRva=0x7FC380;
constexpr unsigned kChatAnchorPointLeaRva=0x801CEC;
constexpr unsigned kChatTextMatrixLeaRva=0x802041;
constexpr unsigned kChatTailTargetLeaRva=0x801F8F;
constexpr unsigned kChatHeadCallRva=0x801977;      // E8 -> 0x7FF870, after lea rcx,[rbp+0xC0] at 0x801970
constexpr unsigned kChatProjectRva=0x7FF870;
constexpr unsigned kChatHeadLeaRva=0x801970;
// The three hooks, and the originals they call -- swappable so the test can run
// them over a stand-in for the bubble draw's frame. Swap returns the previous.
void* ChatProjectHook(void* out,const void* in,void* camera,void* state) noexcept;
void* ChatAnchorHook(void* self,void* out,const float* corner,void* state) noexcept;
void* ChatTextDrawHook(void* layout,void* state,void* style,const float* matrix,std::uintptr_t flag) noexcept;
struct ChatOriginals { void* project=nullptr; void* anchor=nullptr; void* textDraw=nullptr; float scale=1.0f; };
ChatOriginals SwapChatOriginalsForTest(const ChatOriginals& with) noexcept;
// What the anchoring did, for the log: bubbles shrunk about the head and about
// the tail's target, and the last one's centre and corner before and after.
struct ChatAnchorNote {
    unsigned long long aboutHead=0,aboutTail=0;
    float centre[2]{},corner[2]{},moved[2]{};
};
ChatAnchorNote ReadChatAnchorNote() noexcept;
// corner pulled toward point by scale: point + scale*(corner - point).
void ScaleAbout(const float point[2],float scale,float corner[2]) noexcept;
bool ApplyChatBubbleScale(const ImageProfile& image,float scale,char* reason,std::size_t capacity) noexcept;
// The text matrix with its x and y rows scaled, and the draw's last argument
// with its byte set so the lines' offsets go through it (exposed for the test).
void ScaleChatTextMatrix(const float in[16],float scale,float out[16]) noexcept;
std::uintptr_t ChatTextRunsThroughMatrix(std::uintptr_t flag) noexcept;
// The replacement setter (exposed for the test, which calls it through the
// redirected site's thunk).
void NameplateTextSizeHook(void* style,float x,float y) noexcept;
}
