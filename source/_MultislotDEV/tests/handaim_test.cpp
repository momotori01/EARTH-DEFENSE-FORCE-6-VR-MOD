// Hand aim (handaim.h): the wire format, a round trip, what is refused, the packet the game's two readers pass
// by, and who a soldier belongs to - through a stand-in room laid out as the game's.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../src/handaim.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

bool Near(const float d[3], float x, float y, float z) {
    return std::fabs(d[0] - x) < 1e-3f && std::fabs(d[1] - y) < 1e-3f && std::fabs(d[2] - z) < 1e-3f;
}

// The EOS SDK makes no ids before EOS_Initialize, so ids are named by address here.
const char* FakeNamer(const void* id, char* out, std::size_t size) {
    if (!out || !size) return "";
    out[0] = 0;
    if (id) std::snprintf(out, size, "0002%028llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(id)));
    return out;
}

template <typename T> void Put(unsigned char* memory, std::size_t offset, T value) { std::memcpy(memory + offset, &value, sizeof(value)); }

}  // namespace

int main() {
    ResetHandAimForTest();
    SetHandAimPeerNamer(&FakeNamer);
    std::uint8_t packet[kHandAimMaxPacket]{};
    float dir[3]{};

    Check(BuildHandAimPacket(packet, sizeof(packet), 1000) == 0, "nothing goes out before EDF6VR gives a direction");
    const float up[3] = {0.0f, 0.0f, 2.0f};  // not unit length: normalised on the way in
    const float diagonal[3] = {1.0f, 1.0f, 0.0f};
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const float nan[3] = {std::nanf(""), 0.0f, 0.0f};
    SetLocalHandAim(1, up, 1000);
    SetLocalHandAim(3, diagonal, 1000);
    SetLocalHandAim(kHandAimSlots, up, 1000);
    SetLocalHandAim(-1, up, 1000);
    SetLocalHandAim(2, zero, 1000);
    SetLocalHandAim(4, nan, 1000);
    SetLocalHandAim(5, nullptr, 1000);
    const std::size_t size = BuildHandAimPacket(packet, sizeof(packet), 1100);
    Check(size == 9 + 2 * 7, "two slots go out: out of range, zero, NaN and null are refused");
    Check(BuildHandAimPacket(packet, 8, 1100) == 0, "a buffer smaller than the largest packet is refused");

    // What makes the game drop it unread: packet::Controller (12D2540) skips a packet whose bytes 0 and 1
    // are both zero (12D25A2/12D25A7), the join handshake (12D5570) any whose first dword is not zero (12D5606).
    std::uint32_t firstDword = 0;
    std::memcpy(&firstDword, packet, sizeof(firstDword));
    Check(packet[0] == 0 && packet[1] == 0 && firstDword != 0, "first two bytes zero, first dword not");
    Check(IsMultiSlotPacket(packet, static_cast<std::uint32_t>(size)) && !IsMultiSlotPacket(packet, 3) &&
              !IsMultiSlotPacket(nullptr, 9),
          "the probe needs the four magic bytes");
    const std::uint32_t hello[2] = {0, 0x12345678};
    Check(!IsMultiSlotPacket(hello, sizeof(hello)), "a game hello is not ours");
    const std::uint8_t gameData[8] = {1, 0, 'M', 'S', 1, 1, 0, 0};
    Check(!IsMultiSlotPacket(gameData, sizeof(gameData)), "nor is a game packet that happens to carry the letters");

    const auto n = static_cast<std::uint32_t>(size);
    Check(ReceiveHandAimPacket("peerA", packet, n, 2000), "a packet is taken");
    Check(RemoteHandAim("peerA", 1, dir, 2000) && Near(dir, 0.0f, 0.0f, 1.0f), "slot 1 arrives as (0,0,1)");
    Check(RemoteHandAim("peerA", 3, dir, 2000) && Near(dir, 0.70711f, 0.70711f, 0.0f), "slot 3 arrives normalised");
    Check(!RemoteHandAim("peerA", 2, dir, 2000) && !RemoteHandAim("peerA", 0, dir, 2000), "slots never sent are empty");
    Check(!RemoteHandAim("peerB", 1, dir, 2000), "another member has nothing");
    Check(RemoteHandAim("peerA", 1, dir, 2000 + kHandAimRemoteLifeMs) &&
              !RemoteHandAim("peerA", 1, dir, 2000 + kHandAimRemoteLifeMs + 1),
          "a direction is used for kHandAimRemoteLifeMs, then dropped");
    Check(BuildHandAimPacket(packet, sizeof(packet), 1000 + kHandAimLocalLifeMs) != 0 &&
              BuildHandAimPacket(packet, sizeof(packet), 1000 + kHandAimLocalLifeMs + 1) == 0,
          "a slot EDF6VR stops refreshing stops going out");

    // Unordered delivery: an older packet than the newest one kept is dropped.
    std::uint8_t older[kHandAimMaxPacket]{}, newer[kHandAimMaxPacket]{};
    const float east[3] = {1.0f, 0.0f, 0.0f}, north[3] = {0.0f, 1.0f, 0.0f};
    SetLocalHandAim(1, east, 3000);
    const auto olderSize = static_cast<std::uint32_t>(BuildHandAimPacket(older, sizeof(older), 3000));
    SetLocalHandAim(1, north, 3010);
    const auto newerSize = static_cast<std::uint32_t>(BuildHandAimPacket(newer, sizeof(newer), 3010));
    ReceiveHandAimPacket("peerA", newer, newerSize, 3020);
    ReceiveHandAimPacket("peerA", older, olderSize, 3030);
    Check(RemoteHandAim("peerA", 1, dir, 3040) && Near(dir, 0.0f, 1.0f, 0.0f), "a late older packet does not win");
    ReceiveHandAimPacket("peerA", older, olderSize, 3020 + 2001);
    Check(RemoteHandAim("peerA", 1, dir, 3020 + 2002) && Near(dir, 1.0f, 0.0f, 0.0f),
          "after two quiet seconds any number is taken (the sender may have restarted)");

    // Refused: a body shorter than its count, another version or type, a short header, no peer.
    std::uint8_t bad[kHandAimMaxPacket]{};
    std::memcpy(bad, newer, newerSize);
    bad[8] = 5;
    Check(!ReceiveHandAimPacket("peerC", bad, newerSize, 5000), "count beyond the bytes received");
    std::memcpy(bad, newer, newerSize);
    bad[8] = kHandAimSlots + 1;
    Check(!ReceiveHandAimPacket("peerC", bad, sizeof(bad), 5000), "count beyond the slots");
    std::memcpy(bad, newer, newerSize);
    bad[4] = 2;
    Check(!ReceiveHandAimPacket("peerC", bad, newerSize, 5000), "a later version is left alone");
    std::memcpy(bad, newer, newerSize);
    bad[5] = 2;
    Check(!ReceiveHandAimPacket("peerC", bad, newerSize, 5000), "an unknown type is left alone");
    Check(!ReceiveHandAimPacket("peerC", newer, 8, 5000) && !ReceiveHandAimPacket("", newer, newerSize, 5000) &&
              !ReceiveHandAimPacket(nullptr, newer, newerSize, 5000),
          "a short header or no peer");
    Check(!RemoteHandAim("peerC", 1, dir, 5000), "and none of them stored anything");

    // Rate: once per kHandAimSendGapMs per peer, each peer on its own clock.
    const void* p1 = reinterpret_cast<const void*>(0x51);
    const void* p2 = reinterpret_cast<const void*>(0x52);
    Check(HandAimDue(p1, 10000) && !HandAimDue(p1, 10000 + kHandAimSendGapMs - 1) && HandAimDue(p2, 10001) &&
              HandAimDue(p1, 10000 + kHandAimSendGapMs) && !HandAimDue(nullptr, 10000),
          "at most one packet per kHandAimSendGapMs to a peer");

    // Who a soldier belongs to: soldier+0x1ED0 -> the eos::User the room's slots hold -> its ProductUserId.
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x22CE000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "stand-in image");
    if (!image) return 1;
    const auto base = reinterpret_cast<std::uintptr_t>(image);
    alignas(8) static unsigned char session[0x38]{}, room[0x170]{}, users[3][0x50]{}, soldier[0x2000]{}, stranger[0x50]{};
    static std::uintptr_t slots[16]{}, slotVector[3]{};
    slotVector[0] = reinterpret_cast<std::uintptr_t>(slots);
    slotVector[1] = slotVector[2] = slotVector[0] + sizeof(slots);
    for (int i = 0; i < 3; ++i) {
        slots[i * 2] = reinterpret_cast<std::uintptr_t>(users[i]);
        Put(users[i], 0x10, std::uint32_t(i ? 3 : 1));
        Put(users[i], 0x18, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x9000 + i)));
        Put(users[i], 0x40, std::int32_t(i));
    }
    Put(image, 0x20B2AC0, static_cast<const void*>(session));
    Put(session, 0x28, static_cast<const void*>(room));
    Put(room, 0, base + 0x17ECA00);
    Put(room, 0x160, static_cast<const void*>(slotVector));
    char peer[40]{}, expected[40]{};
    FakeNamer(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x9002)), expected, sizeof(expected));
    Put(soldier, 0x1ED0, static_cast<const void*>(users[2]));
    Check(SoldierPeer(base, soldier, peer, sizeof(peer)) && std::strcmp(peer, expected) == 0,
          "a player's soldier is named by its room user's ProductUserId");
    Put(soldier, 0x1ED0, static_cast<const void*>(stranger));
    Check(!SoldierPeer(base, soldier, peer, sizeof(peer)), "a user that is not in the room is not named");
    Put(soldier, 0x1ED0, static_cast<const void*>(nullptr));
    Check(!SoldierPeer(base, soldier, peer, sizeof(peer)) && !SoldierPeer(base, nullptr, peer, sizeof(peer)),
          "an NPC (no user) or no soldier");
    Put(soldier, 0x1ED0, static_cast<const void*>(users[2]));
    Put(room, 0, std::uintptr_t(0));  // the room going away: the cached owner still answers
    Check(SoldierPeer(base, soldier, peer, sizeof(peer)) && std::strcmp(peer, expected) == 0,
          "owners are cached once found");

    // The exports, end to end on the real clock.
    InitHandAim(image);
    const float west[3] = {-3.0f, 0.0f, 0.0f};
    SetLocalHandAim(2, west, GetTickCount64());
    std::uint8_t theirs[kHandAimMaxPacket]{};
    const auto theirsSize = static_cast<std::uint32_t>(BuildHandAimPacket(theirs, sizeof(theirs), GetTickCount64()));
    Check(ReceiveHandAimPacket(expected, theirs, theirsSize, GetTickCount64()), "their packet arrives");
    Check(MultiSlot_HandAimVersion() == 1, "export version 1");
    Check(MultiSlot_GetHandAim(soldier, 2, dir) == 1 && Near(dir, -1.0f, 0.0f, 0.0f),
          "MultiSlot_GetHandAim gives their direction for their soldier");
    Check(MultiSlot_GetHandAim(soldier, 1, dir) == 0 && MultiSlot_GetHandAim(nullptr, 2, dir) == 0 &&
              MultiSlot_GetHandAim(soldier, 2, nullptr) == 0,
          "and nothing for another slot, no soldier or no output");
    SetHandAimEnabled(false);
    Check(MultiSlot_GetHandAim(soldier, 2, dir) == 0 && BuildHandAimPacket(theirs, sizeof(theirs), GetTickCount64()) == 0,
          "HandAim=0: nothing is used and nothing goes out");
    SetHandAimEnabled(true);

    // Without EDF6VR: another player's rapid-fire catch-up shot (690420), through the four hook handlers, on a
    // stand-in weapon laid out as the game's. The two `call 696FD0` are written into the stand-in image as the
    // game has them; EDF6VR redirecting one is played by pointing it elsewhere.
    const auto putCall = [&](std::uint32_t at, std::uint32_t target) {
        image[at] = 0xE8;
        Put(image, at + 1, static_cast<std::int32_t>(static_cast<std::int64_t>(target) - (at + 5)));
    };
    for (const auto call : kCatchUpCalls) putCall(call, kCatchUpFire);
    alignas(16) static unsigned char weapons[2][0x1600]{}, muzzles[0xF0 * 2]{};  // up to +0x1540 (the sync mode)
    static void* owned[2] = {weapons[0], weapons[1]};
    Put(soldier, kOwnedWeapons, static_cast<void*>(owned));
    Put(soldier, kOwnedWeaponCount, std::uint64_t(2));
    for (auto& weapon : weapons) {
        Put(weapon, kWeaponOwner, static_cast<void*>(soldier));
        const float forward[4] = {0.0f, 0.0f, 1.0f, 0.0f};  // FireVector: the muzzle's +z
        std::memcpy(weapon + kWeaponFireVector, forward, sizeof(forward));
    }
    Put(weapons[1], 0x1D0, static_cast<void*>(muzzles));
    Put(weapons[1], 0x1E0, std::uint64_t(1));
    const float firstMatrix[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {10, 20, 30, 1}};
    const float secondMatrix[4][4] = {{0, 1, 0, 0}, {-1, 0, 0, 0}, {0, 0, 1, 0}, {1, 2, 3, 1}};
    const auto resetMuzzle = [&] {
        std::memset(muzzles, 0x5A, sizeof(muzzles));
        std::memcpy(muzzles + 0x50, firstMatrix, sizeof(firstMatrix));
        std::memcpy(muzzles + 0x90, secondMatrix, sizeof(secondMatrix));
    };
    resetMuzzle();
    unsigned char pristine[0xF0];
    std::memcpy(pristine, muzzles, sizeof(pristine));
    // Their machine says weapon 1 (the second in the list) fires along +x.
    Put(room, 0, base + 0x17ECA00);  // the room is back (the owner cache is emptied below)
    ResetHandAimForTest();
    SetHandAimPeerNamer(&FakeNamer);
    InitHandAim(image);
    const float plusX[3] = {1.0f, 0.0f, 0.0f};
    SetLocalHandAim(1, plusX, GetTickCount64());
    std::uint8_t aimPacket[kHandAimMaxPacket]{};
    const auto aimSize = static_cast<std::uint32_t>(BuildHandAimPacket(aimPacket, sizeof(aimPacket), GetTickCount64()));
    ReceiveHandAimPacket(expected, aimPacket, aimSize, GetTickCount64());
    MidHandler before[kCatchUpSites]{}, after[kCatchUpSites]{};
    bool handlers = !HandAimCatchUpHandler(0x123456);
    for (int i = 0; i < kCatchUpSites; ++i) {
        before[i] = HandAimCatchUpHandler(kCatchUpBefore[i]);
        after[i] = HandAimCatchUpHandler(kCatchUpAfter[i]);
        handlers = handlers && before[i] && after[i];
    }
    Check(handlers && HandAimCatchUpHandler(kPerShotReceive) != nullptr, "a handler before and after each call");
    if (!handlers) {
        std::printf("%d check(s) failed (no point running the catch-up shots without their handlers)\n", failures);
        return 1;
    }
    CpuContext context{};
    context.rbx = reinterpret_cast<std::uintptr_t>(weapons[1]);
    const auto fired = [&](float out[3]) {
        float m[4][4];
        std::memcpy(m, muzzles + 0x50, sizeof(m));
        return MuzzleFireDirection(weapons[1], m, out);
    };
    for (int loop = 0; loop < kCatchUpSites; ++loop) {
        resetMuzzle();
        before[loop](&context);
        float shot[3]{};
        float m0[4][4], m1[4][4];
        std::memcpy(m0, muzzles + 0x50, sizeof(m0));
        std::memcpy(m1, muzzles + 0x90, sizeof(m1));
        Check(fired(shot) && Near(shot, 1.0f, 0.0f, 0.0f), "the catch-up shot leaves along their hand");
        Check(m0[3][0] == 10 && m0[3][1] == 20 && m0[3][2] == 30 && m1[3][0] == 1 && m1[3][1] == 2 && m1[3][2] == 3,
              "from the same point");
        float second[3]{};
        const float secondForward[3] = {m1[2][0], m1[2][1], m1[2][2]};
        for (int j = 0; j < 3; ++j) second[j] = secondForward[j];
        Check(Near(second, 1.0f, 0.0f, 0.0f), "the second matrix is turned the same way");
        bool orthonormal = true;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                const float dot = m0[r][0] * m0[c][0] + m0[r][1] * m0[c][1] + m0[r][2] * m0[c][2];
                orthonormal = orthonormal && std::fabs(dot - (r == c ? 1.0f : 0.0f)) < 1e-4f;
            }
        Check(orthonormal, "and stays a rotation");
        after[loop](&context);
        Check(std::memcmp(muzzles, pristine, sizeof(pristine)) == 0, "after the shot the muzzle is the game's again, byte for byte");
    }
    // Left alone: EDF6VR has the call, the weapon has no direction, the owner is no room member, HandAim=0,
    // a weapon not in its owner's list, an "after" with no "before", another weapon's "after".
    const auto untouched = [&](const char* what) {
        before[0](&context);
        after[0](&context);
        Check(std::memcmp(muzzles, pristine, sizeof(pristine)) == 0, what);
        before[0](&context);
        Check(std::memcmp(muzzles, pristine, sizeof(pristine)) == 0, what);
        after[0](&context);
    };
    resetMuzzle();
    // EDF6VR redirects the call to its HookDualFire, which passes another player's weapon straight on: the
    // plugin still turns the shot (1.5.38, one place that does it).
    putCall(kCatchUpCalls[0], 0x123450);
    before[0](&context);
    {
        float shot[3]{};
        Check(fired(shot) && Near(shot, 1.0f, 0.0f, 0.0f), "a call EDF6VR redirected is turned all the same");
    }
    after[0](&context);
    Check(std::memcmp(muzzles, pristine, sizeof(pristine)) == 0, "and put back after it");
    // Something that is no longer a call there would never come back to the "after": left alone.
    image[kCatchUpCalls[0]] = 0x90;
    untouched("a site that is no longer a call");
    putCall(kCatchUpCalls[0], kCatchUpFire);
    Put(soldier, kOwnedWeapons, static_cast<void*>(&owned[1]));  // the weapon is now slot 0, which has nothing
    untouched("no direction for that slot");
    Put(soldier, kOwnedWeapons, static_cast<void*>(owned));
    Put(soldier, 0x1ED0, static_cast<const void*>(nullptr));
    untouched("an owner with no room user (an NPC)");
    Put(soldier, 0x1ED0, static_cast<const void*>(users[2]));
    SetHandAimEnabled(false);
    untouched("HandAim=0");
    SetHandAimEnabled(true);
    Put(soldier, kOwnedWeaponCount, std::uint64_t(1));
    untouched("a weapon its owner does not list");
    Put(soldier, kOwnedWeaponCount, std::uint64_t(2));
    resetMuzzle();
    after[1](&context);
    Check(std::memcmp(muzzles, pristine, sizeof(pristine)) == 0, "an after with no before changes nothing");
    before[0](&context);
    CpuContext other{};
    other.rbx = reinterpret_cast<std::uintptr_t>(weapons[0]);
    after[0](&other);
    Check(std::memcmp(muzzles, pristine, sizeof(pristine)) != 0, "another weapon's after does not put this one back");
    before[0](&context);  // a new shot: the stale turn is dropped, this one starts from the turned muzzle
    after[0](&context);
    resetMuzzle();

    // The per-shot receive (692540): rsi the weapon, rdi the muzzle it fires. A weapon whose message carried no
    // muzzle (mode -1: a Fencer's hand cannon, Weapon_HeavyShoot, in the third real test) is turned onto the hand
    // and put back after the call; mode 0 and 1 carried the sender's own muzzle and are left as they are.
    putCall(kPerShotCall, kCatchUpFire);
    const MidHandler perShotBefore = HandAimCatchUpHandler(kPerShotReceive);
    const MidHandler perShotAfter = HandAimCatchUpHandler(kPerShotAfter);
    Check(perShotBefore && perShotAfter && perShotBefore != perShotAfter, "the per-shot receive's two handlers");
    if (perShotBefore && perShotAfter) {
        Put(weapons[1], 0x1E0, std::uint64_t(2));
        unsigned char* const fireEntry = muzzles + 0xF0;
        const auto resetBoth = [&] {
            resetMuzzle();
            std::memset(fireEntry, 0x3C, 0xF0);
            std::memcpy(fireEntry + 0x50, firstMatrix, sizeof(firstMatrix));
            std::memcpy(fireEntry + 0x90, secondMatrix, sizeof(secondMatrix));
        };
        resetBoth();
        unsigned char both[0xF0 * 2];
        std::memcpy(both, muzzles, sizeof(both));
        CpuContext shot{};
        shot.rsi = reinterpret_cast<std::uintptr_t>(weapons[1]);
        shot.rdi = 1;
        shot.rbx = reinterpret_cast<std::uintptr_t>(weapons[0]);  // 692540's rbx holds the +0x153C flags, not a weapon
        const auto firedFrom = [&](float out[3]) {
            float m[4][4];
            std::memcpy(m, fireEntry + 0x50, sizeof(m));
            return MuzzleFireDirection(weapons[1], m, out);
        };
        for (const std::uint32_t mode : {0xFFFFFFFFu, 2u}) {
            Put(weapons[1], 0x1540, mode);
            perShotBefore(&shot);
            float out[3]{};
            Check(firedFrom(out) && Near(out, 1.0f, 0.0f, 0.0f), "a per-shot shot with no muzzle in its message is turned");
            Check(std::memcmp(muzzles, both, 0xF0) == 0, "and only the muzzle it fires");
            perShotAfter(&shot);
            Check(std::memcmp(muzzles, both, sizeof(both)) == 0, "after the call the muzzle is the game's again");
        }
        for (const std::uint32_t mode : {0u, 1u}) {
            Put(weapons[1], 0x1540, mode);
            perShotBefore(&shot);
            Check(std::memcmp(muzzles, both, sizeof(both)) == 0, "mode 0 and 1 carried the muzzle: left alone");
            perShotAfter(&shot);
            Check(std::memcmp(muzzles, both, sizeof(both)) == 0, "and nothing to put back");
        }
        Put(weapons[1], 0x1540, 0xFFFFFFFFu);
        putCall(kPerShotCall, 0x123450);  // EDF6VR's redirect passes another player's weapon straight on
        perShotBefore(&shot);
        {
            float out[3]{};
            Check(firedFrom(out) && Near(out, 1.0f, 0.0f, 0.0f), "a per-shot call EDF6VR redirected is turned all the same");
        }
        perShotAfter(&shot);
        Check(std::memcmp(muzzles, both, sizeof(both)) == 0, "and put back after it");
        image[kPerShotCall] = 0x90;
        perShotBefore(&shot);
        Check(std::memcmp(muzzles, both, sizeof(both)) == 0, "a per-shot site that is no longer a call is left alone");
        perShotAfter(&shot);
        putCall(kPerShotCall, kCatchUpFire);
        SetHandAimEnabled(false);
        perShotBefore(&shot);
        Check(std::memcmp(muzzles, both, sizeof(both)) == 0, "HandAim=0 leaves the per-shot shot alone");
        perShotAfter(&shot);
        SetHandAimEnabled(true);
        perShotBefore(&shot);
        CpuContext otherShot = shot;
        otherShot.rsi = reinterpret_cast<std::uintptr_t>(weapons[0]);
        perShotAfter(&otherShot);
        Check(std::memcmp(muzzles, both, sizeof(both)) != 0, "another weapon's per-shot after does not put this one back");
        Put(weapons[1], 0x1540, 0u);
        Put(weapons[1], 0x1E0, std::uint64_t(1));
        resetMuzzle();
    }

    // The arc itself: up to just short of a half turn, and refused from a half turn away.
    float rows[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const float from[3] = {0, 0, 1};
    const float to170[3] = {std::sin(170.0f * 3.14159265f / 180.0f), 0.0f, std::cos(170.0f * 3.14159265f / 180.0f)};
    Check(TurnRows(rows, from, to170) && std::fabs(rows[2][0] - to170[0]) < 1e-4f && std::fabs(rows[2][2] - to170[2]) < 1e-4f,
          "170 degrees turns exactly");
    float flip[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    const float back[3] = {0, 0, -1};
    Check(!TurnRows(flip, from, back) && flip[2][2] == 1.0f, "a half turn is refused and leaves the rows");
    float same[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    Check(TurnRows(same, from, from) && same[0][0] == 1.0f && same[2][2] == 1.0f, "no turn when already along it");
    Check(OwnedWeaponSlot(soldier, weapons[1]) == 1 && OwnedWeaponSlot(soldier, weapons[0]) == 0 &&
              OwnedWeaponSlot(soldier, stranger) == -1 && OwnedWeaponSlot(nullptr, weapons[0]) == -1,
          "a weapon's slot is its place in the owner's list");

    VirtualFree(image, 0, MEM_RELEASE);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("hand aim packets verified\n");
    return 0;
}
