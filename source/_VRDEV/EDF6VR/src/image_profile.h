#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstddef>

namespace edf6vr {
// Profile for the user's current EDF.dll; every gate must pass before patching.
inline constexpr std::uint32_t kTimestamp=0x678CCB46;
inline constexpr std::uint32_t kImageSize=0x22CE000;
inline constexpr std::uint32_t kUpdateRva=0xF86A0;
inline constexpr std::uint32_t kVtableRva=0x1768C10;
inline constexpr std::uint32_t kUpdateSlotRva=kVtableRva+0x20;
inline constexpr std::size_t kCameraMatrixOffset=0x220;
inline constexpr std::size_t kSourceOffset=0x350;
inline constexpr std::size_t kSoldierOffset=0x360;
// The game answers for itself whether the HUD should be drawn, and it does so in
// one place: a bool virtual on this same camera vtable. Found from EDFModLoader
// patch DisableUI.txt in _VRDEV/References, whose byte pattern 80B9F003000000753C
// is this function opening with cmp byte ptr [rcx+3F0h],0 / jne to xor al,al ret.
// It has no direct callers anywhere in the image; it is only ever called
// virtually, so the slot is the way in. Knowing when it is asked is knowing when
// the HUD is about to be drawn, which no amount of comparing pictures can tell.
inline constexpr std::uint32_t kUiGateRva=0xF7500;
inline constexpr std::uint32_t kUiGateSlotRva=kVtableRva+0x48;
// EDF6 resolves XInput through GetProcAddress rather than importing it, so
// GetProcAddress is the way in, and it is a normal import: one aligned pointer.
// Found by parsing the import directory of the shipped EDF.dll offline.
inline constexpr std::uint32_t kGetProcAddressSlotRva=0x1755578;
struct ImageProfile { unsigned char* base=nullptr; void** updateSlot=nullptr; void* original=nullptr; };
bool CheckImage(HMODULE module, ImageProfile& result, char* reason, std::size_t reasonSize) noexcept;
// The slot still points at the function it should, and that function still opens
// the way the patch pattern says. Both, or nothing is touched.
bool CheckUiGate(const ImageProfile& image) noexcept;
bool HasType(const ImageProfile& image, const void* object, const char* name) noexcept;
// The decorated RTTI name of an object, pointing into the image, or null.
const char* TypeName(const ImageProfile& image, const void* object) noexcept;
bool Readable(const void* ptr, std::size_t size, bool writable=false) noexcept;
// Patch a single aligned pointer, restoring its page protection. Refuse conflicts.
bool ReplacePointer(void** slot, void* expected, void* replacement, bool& changed) noexcept;
// Executable 16-byte thunk (mov rax, imm64; jmp rax) placed within reach of a
// rel32 from `anchor`, so a call site can be redirected without moving code.
void* AllocateNearThunk(const void* anchor, void* target) noexcept;
// Rewrite one `call rel32`. Refuses unless the instruction and its current
// target are exactly what the caller expects.
bool RedirectCall(unsigned char* callSite, void* expectedTarget, void* replacement, bool& changed) noexcept;
// The same for a `jmp rel32` (a tail call): the replacement returns to the
// jumping function's caller, as the original target would have.
bool RedirectJump(unsigned char* jumpSite, void* expectedTarget, void* replacement, bool& changed) noexcept;
// Every byte this plugin changes in another module's memory, so two mods can
// be checked for overlap without reading either one's code. ReplacePointer and
// RedirectCall record themselves; hand-written patches call RecordPatch.
// WritePatchList writes "<module>+0x<rva> <bytes> <purpose>" per line.
void RecordPatch(const void* address, std::size_t size, const char* purpose) noexcept;
bool WritePatchList(const wchar_t* path) noexcept;
unsigned PatchCount() noexcept;
}
