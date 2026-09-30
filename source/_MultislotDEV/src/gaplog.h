#pragma once

#include <cstdint>
#include <vector>

#include "midhook.h"
#include "patches.h"

namespace multislot {

// How far a remote player really is from where this machine is drawing them, measured.
//
// Every class's per-frame sync (vtable slot 55; PaleWing's 5803C0 is a jmp to SoldierBase's 596130, so
// they all land there) keeps a smoothed copy of the player at +0x1920 and moves it 5% of the way to the
// value that came over the network, on every call:
//
//     59637C  movups xmm0, [rdi+0x1920]   ; where the game currently has them
//     596383  subps  xmm6, xmm0           ; the gap: where they really are, minus that
//     596386  mulps  xmm6, [0.05 x4]      ; five percent of it
//     5963A1  movups [rdi+0x1920], xmm6
//
// 5% per call is about a second to close, which is what "they land, then slide into place" looks like.
//
// The gap sits in xmm6, which a mid-function thunk does not save - and does not need to. The step the
// game takes is 5% of the gap, so reading +0x1920 once per call and differencing recovers the gap
// exactly, times twenty. The hook is at 59637C, before the load, where +0x1920 still holds the previous
// call's result; its seven bytes address off rdi, so they copy into the thunk unchanged.
//
// Not every object on this path is being interpolated. 596130 starts by clearing +0x1900 and, unless
// bit 0 of +0x128 is set, jumps straight to the smoothing tail with xmm6 left holding the constant at
// 1765B30, which is (0, 0, 0, 1) - so the field is dragged towards zero and the difference means
// nothing. The real branch writes 1 to +0x1900 immediately before the tail:
//
//     596194  mov byte [rdi+0x1900], 0    ; every call starts by clearing it
//     59619D  test byte [rdi+0x128], 1
//     5961A4  je 596374                   ; not interpolated: xmm6 stays the constant
//     ...
//     59636A  mov byte [rdi+0x1900], 1    ; the target is real
//     596371  movaps xmm6, xmm2
//
// The hook is past that write, so +0x1900 says which kind of call this is, for free and exactly when
// it is needed. A step only counts when the call that produced it was a real one, and an object that
// never has a real call is reported as idle instead of as a gap of zero. An offline mission tracks four
// such objects, which is how this was found.
//
// This is the measurement every earlier attempt was missing. The desync has been argued about from
// mechanisms - packet loss, the game's own bandwidth budget, prediction going wrong - and never once
// measured, so none of them could be ruled in or out. Read-only: nothing is written back to the game.
// EDF.dll's base, to report a class as an RVA rather than a bare address, and the fraction of the gap
// the game closes per call - 0.05 unless [Smoothing] RemotePlayerPercent changed it. The gap is
// recovered by dividing the step by that fraction, so it has to be the one actually installed.
void InitDesyncMeter(unsigned char* gameBase, float smoothingFraction);

std::vector<MidSite> DesyncHooks();
MidHandler DesyncHookHandler(std::uint32_t rva);

// [Sync] DesyncMeter. Off by default: this is an investigation, and a shipped build should not hook a
// per-frame function or add a line a second to anyone's log for nothing.
void SetDesyncMeter(bool on);
bool DesyncMeterOn();

// [Sync] ClearSyncLatch. EXPERIMENT, and the only thing here that writes to the game: when the sync
// checker has latched itself off (its +0x20 byte), put that byte back to zero so the next call corrects
// again. Needs the meter on, since it rides the same hook.
void SetClearSyncLatch(bool on);

// [Sync] SyncTolerance. EXPERIMENT. How far out of sync EDF6 will still smooth rather than give up; the
// stock value is 20 and the checker reads it every call, so writing a larger one makes it keep
// correcting through errors it currently refuses. 0 leaves the game's own value alone.
void SetSyncTolerance(float units);

// Summarises whatever has been collected but not yet reported.
void FlushDesync();

}  // namespace multislot
