#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "midhook.h"
#include "patches.h"

namespace multislot {

// [Sync] HandAim: which way a VR player's other hand fired, carried to the other players who have the mod.
//
// In VR a Ranger fires the gun in each hand, and a Fencer each hand's weapon, where that hand points. EDF6
// has one aim per player, so everyone else sees both shots go the same way: a weapon synced shot by shot
// (WeaponBulletSyncTable, 767150) sends its muzzle matrix from 694910 before EDF6VR turns it onto the hand
// (696FD0, after the send), and a rapid-fire one (flag 0x10) sends only a shot count, which the receiver
// fires along its own copy of that player's single aim (690420 -> 696FD0).
//
// EDF6VR tells this plugin each frame which way each of its own weapons is fired (MultiSlot_SetHandAim). The
// plugin sends that, a few dozen bytes, to everyone in the room right after the game's own packet to them,
// and keeps what the others send. EDF6VR asks for it when it fires another player's weapon
// (MultiSlot_GetHandAim) and turns that shot. Nothing in the game's own traffic changes.
//
// The packets start 00 00 'M' 'S' on a channel the game never uses (it sends everything on 0). EDF6 hands
// every received packet to two readers and both pass this one by: packet::Controller (12D2540) skips any
// packet whose first two bytes are zero, and the join handshake (12D5570) any whose first dword is not.
// So a player without the mod, or with an older build, receives them and the game drops them unread; this
// build takes them out before the game sees them at all.

constexpr std::uint8_t kHandAimChannel = 0x4D;  // 'M'
constexpr int kHandAimSlots = 8;                 // weapon slots, numbered by EDF6VR; the same on both ends
constexpr std::uint64_t kHandAimLocalLifeMs = 300;   // a slot EDF6VR stops refreshing is no longer sent
constexpr std::uint64_t kHandAimRemoteLifeMs = 500;  // and one that stops arriving is no longer applied
constexpr std::uint64_t kHandAimSendGapMs = 40;      // at most 25 a second to one peer; the game itself sends ~11
constexpr std::size_t kHandAimMaxPacket = 9 + kHandAimSlots * 7;
constexpr int kHandAimApiVersion = 1;

void SetHandAimEnabled(bool on);
bool HandAimEnabled();

// The wire format, for the packet hooks (netlog.cpp) and the tests.
bool IsMultiSlotPacket(const void* data, std::uint32_t size);
// Builds this machine's packet from the slots EDF6VR refreshed in the last kHandAimLocalLifeMs.
// Returns its size, or 0 when there is nothing to send.
std::size_t BuildHandAimPacket(std::uint8_t* out, std::size_t size, std::uint64_t nowMs);
// Stores what `peer` (a ProductUserId as text) sent. False when it is not a hand aim packet this build reads.
bool ReceiveHandAimPacket(const char* peer, const void* data, std::uint32_t size, std::uint64_t nowMs);
// Once per kHandAimSendGapMs per peer: true means send now (and counts it as sent).
bool HandAimDue(const void* peer, std::uint64_t nowMs);

// The two halves of the exports, with the clock and the peer passed in so the tests can drive them.
void SetLocalHandAim(int slot, const float dir[3], std::uint64_t nowMs);
bool RemoteHandAim(const char* peer, int slot, float dir[3], std::uint64_t nowMs);
// Which room member a soldier belongs to: soldier+0x1ED0 is the shared_ptr<eos::User> CreateOnlinePlayer
// (591254) gives it, the same object as in the room's user slots, whose +0x18 is the ProductUserId.
bool SoldierPeer(std::uintptr_t gameBase, const void* soldier, char* peer, std::size_t size);
void InitHandAim(unsigned char* gameBase);

// The rapid-fire catch-up shot without EDF6VR. A rapid-fire weapon (WeaponBulletSyncTable flag 0x10) sends
// only a shot count, and every other machine fires the shots it is behind on in 690420: rebuild the muzzle
// from the weapon's attachment (6969A0), then `call 696FD0` - at 6904F7 in one loop, 690603 in the other,
// both with the weapon in rbx and muzzle 0. The plugin hooks the instruction just before each call (turn the
// muzzle onto the owner's hand) and just after it (put it back), whether or not EDF6VR is loaded: 1.5.37 left
// the turn to EDF6VR when it had redirected the call, and 1.5.38 takes it alone, as agreed with the EDF6VR
// session (one place that does it; EDF6VR's HookDualFire now hands another player's weapon straight on to
// 696FD0, and its setup tool's "Normal (no VR)" does not load it at all). The hooks never touch the call's own
// bytes, which EDF6VR redirects and checks; they only need it to still be a call.
constexpr std::uint32_t kCatchUpFire = 0x696FD0;
// 690420's two loops (a count event that arrives behind), and 6947E0's paced replay at 694894: the weapon tick
// (6934F0 -> 691DC0 -> 691EAB) fires another player's count-synced weapon one shot per fire interval until it
// reaches their count. The second real test (2026-10-04 11:44) showed a Fencer's Weapon_Gatling fired only there
// on the guest - neither 690420 nor the per-shot receive ran once - so 1.6.4 hooks it as well.
constexpr int kCatchUpSites = 3;
constexpr std::uint32_t kCatchUpCalls[kCatchUpSites] = {0x6904F7, 0x690603, 0x694894};
constexpr std::uint32_t kCatchUpBefore[kCatchUpSites] = {0x6904E3, 0x6905EF, 0x694887};
constexpr std::uint32_t kCatchUpAfter[kCatchUpSites] = {0x6904FC, 0x690608, 0x694899};
// The per-shot receive (692540) turns the shot too when the message carried no muzzle (mode not 0 or 1, 1.6.5).
constexpr std::uint32_t kPerShotReceive = 0x692A12;  // before 692540's call
constexpr std::uint32_t kPerShotCall = 0x692A24;
constexpr std::uint32_t kPerShotAfter = 0x692A29;    // after it
constexpr std::size_t kWeaponOwner = 0x120;        // the soldier that owns a weapon
constexpr std::size_t kWeaponFireVector = 0x350;   // the barrel in the muzzle's own frame
constexpr std::size_t kOwnedWeapons = 0x1950;      // a soldier's weapon list (59B428 reads it this way)
constexpr std::size_t kOwnedWeaponCount = 0x1960;
constexpr std::size_t kMuzzleEntrySize = 0xF0;     // *(weapon+0x1D0), count weapon+0x1E0: two matrices +0x50, +0x90
// The six catch-up hooks and the per-shot receive's two (before and after each call).
std::vector<MidSite> HandAimCatchUpHooks();
MidHandler HandAimCatchUpHandler(std::uint32_t rva);
// The pieces, for the tests: a weapon's place in its owner's list (EDF6VR's slot), the way a muzzle matrix
// fires, and a matrix's rows turned by the shortest arc (EDF6VR's FencerTurnRows; false from a half turn).
int OwnedWeaponSlot(const void* soldier, const void* weapon);
bool MuzzleFireDirection(const void* weapon, const float matrix[4][4], float out[3]);
bool TurnRows(float rows[4][4], const float from[3], const float to[3]);

// A ProductUserId as text: ProductUserIdText (the EOS SDK), or a stand-in the tests install, since the SDK
// makes no ids before EOS_Initialize.
using PeerNamer = const char* (*)(const void* id, char* out, std::size_t size);
void SetHandAimPeerNamer(PeerNamer namer);
const char* HandAimPeerText(const void* id, char* out, std::size_t size);

void ResetHandAimForTest();

}  // namespace multislot

// What EDF6VR calls (GetProcAddress on EDF6MultiSlot.dll, so neither DLL needs the other to load).
extern "C" {
__declspec(dllexport) int MultiSlot_HandAimVersion();
// slot 0..7: one of this machine's own weapons; dir: the world-space direction it fires (need not be unit
// length). Call every frame while it applies; any thread.
__declspec(dllexport) void MultiSlot_SetHandAim(int slot, const float* dir);
// soldier: another player's soldier object. 1 and a unit vector in dir when its owner sent one for that
// slot in the last kHandAimRemoteLifeMs, otherwise 0 (yourself, an NPC, a player without the mod, stale).
__declspec(dllexport) int MultiSlot_GetHandAim(const void* soldier, int slot, float* dir);
}
