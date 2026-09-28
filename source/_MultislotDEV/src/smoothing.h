#pragma once
#include <cstdint>

#include "code.h"
#include "patches.h"

namespace multislot {

// How fast a remote player's drawn position catches up with the one the network reported.
//
// SoldierBase's per-frame sync (its vtable slot 55, EDF+596130) keeps a correction vector at
// `player+0x1920` and moves it toward the newly received value by a fixed fraction each time:
//
//     0059637C  movups xmm0, [rdi + 0x1920]    ; where it is now
//     00596383  subps  xmm6, xmm0              ; how far off the new value is
//     00596386  mulps  xmm6, [EDF+17A5A70]     ; x 0.05
//     0059638D  addps  xmm6, xmm0
//     005963A1  movups [rdi + 0x1920], xmm6
//
// The constant is 0.05 splatted four ways, so each update closes one twentieth of the gap. Packets
// go out every 90 ms (the packet Controller's timer, EDF+12CB5F0 writing 90.0 at +0x58), so half the
// error is still there after ~1.3 s and a tenth after ~4 s. That is what the players see: someone who
// has stopped moving still looks displaced, and shots miss because the game decides hits on the
// machine of whoever is being shot at - your aim is wrong by exactly this error. Enemies look right
// because they are not SoldierBase and do not come through here.
//
// EDF+17A5A70 is a shared 0.05 used by ten call sites, so it must not be rewritten. Instead the one
// instruction's rip-relative operand is pointed at a constant of our own, next to the call stubs.
// Nothing else changes: this is a local, display-side value, so it alters only how this machine draws
// other people. It sends nothing, changes no packet, and needs no agreement with anyone else in the
// room - a player without this mod is unaffected either way.
constexpr float kVanillaSmoothing = 0.05f;
constexpr std::uint32_t kSmoothingSite = 0x596386;

// Builds the patch that makes EDF+596386 read `factor` instead of the shared 0.05, emitting the
// constant into `thunks` (which must be allocated and not yet sealed). False when the factor is the
// stock one, when the site does not hold the expected instruction, or when there is no room: the
// caller then leaves the game alone. The patch carries the original bytes, so it is verified and
// applied with everything else, all or nothing.
bool SmoothingPatch(unsigned char* base, ThunkPage& thunks, float factor, Patch& out);

// Roughly how long a correction takes to shrink to a tenth at `factor`, for the log line, given the
// 90 ms the packet timer sends at. Returns 0 when the factor is not usable.
int SmoothingSettleMs(float factor);

}  // namespace multislot
