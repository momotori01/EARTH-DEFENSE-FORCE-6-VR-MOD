// downsync.h: the pure parts, and every table checked against the EDF.dll on disk.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "../src/downsync.h"
#include "../src/patches.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what, std::uint32_t rva = 0) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s (EDF+%X)\n", what, rva);
    }
}

struct Image {
    std::vector<std::uint8_t> file;
    const IMAGE_NT_HEADERS64* nt = nullptr;
    const std::uint8_t* At(std::uint32_t rva, std::size_t size) const {
        auto section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (rva >= section->VirtualAddress && rva + size <= section->VirtualAddress + section->SizeOfRawData) {
                const std::size_t offset = section->PointerToRawData + (rva - section->VirtualAddress);
                return offset + size <= file.size() ? file.data() + offset : nullptr;
            }
        }
        return nullptr;
    }
};

void PureParts() {
    // A window: alive and in the air, or alive in the blow reaction on the ground.
    Check(DownWindow(false, false, false), "in the air is a window");
    Check(DownWindow(false, true, true), "the blow on the ground is a window");
    Check(!DownWindow(false, true, false), "standing is not");
    Check(!DownWindow(true, false, true), "dead never is");

    ForceInput in{true, true, true, true, true, 6, 6, 0};
    Check(ShouldForceSend(in), "a host's flying enemy, 6 frames on: send");
    in.framesSince = 5;
    Check(!ShouldForceSend(in), "not before the interval");
    in.framesSince = 6;
    in.flags = 5;
    Check(!ShouldForceSend(in), "not when a send is already flagged");
    in.flags = 4;
    Check(ShouldForceSend(in), "a stray bit 4 alone does not stop it");
    for (int field = 0; field < 5; ++field) {
        ForceInput off{true, true, true, true, true, 6, 6, 0};
        bool* const flags[] = {&off.on, &off.host, &off.alive, &off.active, &off.window};
        *flags[field] = false;
        Check(!ShouldForceSend(off), "off, a guest's, dead, inactive or out of a window: no send");
    }
    ForceInput none{true, true, true, true, true, 100, 0, 0};
    Check(!ShouldForceSend(none), "no interval, no send");

    // 120 a second: a burst of 30, then one every ~8.3 ms.
    SendBudget budget;
    budget.Reset(120, 1000);
    int taken = 0;
    for (int i = 0; i < 100; ++i) taken += budget.Take(1000) ? 1 : 0;
    Check(taken == 30, "the burst is a quarter second's worth");
    Check(budget.refused() == 70, "the rest are refused and counted");
    Check(!budget.Take(1005) && budget.Take(1009), "refilled at 120 a second");
    while (budget.Take(2000)) {
    }  // the burst gone first
    taken = 0;
    for (std::uint64_t t = 2001; t <= 3000; ++t) taken += budget.Take(t) ? 1 : 0;
    Check(taken >= 118 && taken <= 122, "about 120 in a second of steady asking");

    Spread spread;
    for (int i = 1; i <= 10; ++i) spread.Add(static_cast<float>(i));
    spread.Add(std::nanf(""));
    Check(spread.count() == 10, "ten kept, NaN ignored");
    Check(std::fabs(spread.Mean() - 5.5f) < 1e-5f && spread.Max() == 10.0f, "mean and max");
    Check(spread.Percentile(0.5f) == 6.0f && spread.Percentile(0.9f) == 9.0f && spread.Percentile(1.0f) == 10.0f,
          "median and 90th percentile of the samples");
    spread.Clear();
    Check(spread.count() == 0 && spread.Percentile(0.5f) == 0.0f, "cleared");

    // The room's agreement. t = 10 s; a member is one the game sent to in the last 3 s.
    const std::uint64_t t = 10000;
    std::vector<DownPeer> room;
    Check(!HostSideActive(true, room, t), "alone: the host side stays off");
    room.push_back({t - 100, t - 500, 0});
    room.push_back({t - 50, t - 900, 0});
    Check(HostSideActive(true, room, t), "every member said it accepts: on");
    Check(!HostSideActive(false, room, t), "this machine's switch off: off");
    room.push_back({t - 20, 0, 0});
    Check(!HostSideActive(true, room, t), "a member that never said it (vanilla, older build): off");
    room.back().switchAt = t - 4000;
    Check(!HostSideActive(true, room, t), "a member whose word is older than 3.5 s (switched off): off");
    room.back().sentAt = t - 5000;
    Check(HostSideActive(true, room, t), "a member the game no longer sends to has left: on again");
    std::vector<DownPeer> guests{{t - 100, t - 100, 0}, {t - 100, t - 100, 0}};
    Check(!GuestSideActive(true, guests, t), "no member keeps them (vanilla host, or its switch off): off");
    guests[0].keepingAt = t - 900;
    Check(GuestSideActive(true, guests, t), "the host says it keeps them: on");
    Check(!GuestSideActive(false, guests, t), "but not with this machine's own switch off");
    guests[0].keepingAt = t - 3600;
    Check(!GuestSideActive(true, guests, t), "the host's word went stale: off");
    guests[0].keepingAt = t + 50;
    Check(!GuestSideActive(true, guests, t), "a clock that went backwards does not count");

    // The word: 00 00 'M' 'S', version 1, type 2, flags. The hand aim reader (type 1) passes it by.
    std::uint8_t word[16]{};
    Check(BuildDownSyncPacket(word, 4, 3) == 0, "no room, no word");
    Check(BuildDownSyncPacket(word, sizeof(word), kDownSyncSwitchOn | kDownSyncKeeping) == kDownSyncPacketSize, "eight bytes");
    const std::uint8_t expected[8] = {0, 0, 'M', 'S', 1, 2, 3, 0};
    Check(std::memcmp(word, expected, 8) == 0, "the bytes on the wire");
    Check(ReadDownSyncPacket(word, kDownSyncPacketSize) == 3, "read back");
    Check(ReadDownSyncPacket(word, 7) == -1, "too short");
    std::uint8_t handAim[16] = {0, 0, 'M', 'S', 1, 1, 0, 0, 0};
    Check(ReadDownSyncPacket(handAim, 9) == -1, "a hand aim packet is not this");
    std::uint8_t later[8] = {0, 0, 'M', 'S', 2, 2, 3, 0};
    Check(ReadDownSyncPacket(later, 8) == -1, "a later version is left alone");

    // The newest of five queued entries, as 77D610 finds it.
    Check(NewestEntry(1) == 0 && NewestEntry(0) == 4 && NewestEntry(4) == 3, "newest = (write - 1) mod 5");
}

void Tables(const Image& image) {
    const std::uint64_t base = image.nt->OptionalHeader.ImageBase;
    // The gates: each run as read, the flipped byte inside it, and the flip a one-byte change.
    const auto gates = DownSyncGates();
    Check(gates.size() == 5, "five gates (ant host and guest, spider, Dango, robot)");
    unsigned guestGates = 0;
    for (const auto& g : gates) guestGates += g.guestSide ? 1u : 0u;
    Check(guestGates == 1 && gates[1].guestSide && gates[1].rva == 0x409CE0, "one guest gate: the ant's blow reaction");
    for (const auto& g : gates) {
        const auto* at = image.At(g.rva, g.original.size());
        Check(at && std::memcmp(at, g.original.data(), g.original.size()) == 0, g.name, g.rva);
        Check(g.flip < g.original.size() && g.original[g.flip] != g.patched, "the flip changes one byte", g.rva);
        // The byte is either a short jump's displacement to 0 (je +0: the branch never skips) or the jump's
        // opcode to an unconditional jmp with the same displacement.
        const bool displacement = g.patched == 0x00 && (g.original[g.flip - 1] == 0x74 || g.original[g.flip - 1] == 0x75);
        const bool opcode = g.patched == 0xEB && (g.original[g.flip] == 0x74 || g.original[g.flip] == 0x75);
        Check(displacement || opcode, "the flip is a short branch made never / always to the on side", g.rva);
    }
    // The class update slots and the sync object's two slots point where the tables say.
    const auto slots = DownSyncSlots();
    Check(slots.size() == DownSyncClasses().size() && slots.size() == 11, "eleven classes watched");
    for (const auto& slot : slots) {
        const auto* at = image.At(slot.rva, 8);
        Check(at && SlotTargets(at, base, slot.target), slot.name, slot.rva);
        Check(DownSyncSlotHandler(slot.rva) != nullptr, "each slot has an observer", slot.rva);
    }
    for (const auto& slot : DownSyncSyncSlots()) {
        const auto* at = image.At(slot.rva, 8);
        Check(at && SlotTargets(at, base, slot.target), slot.name, slot.rva);
        Check(DownSyncSyncSlotHandler(slot.rva) != nullptr, "each sync slot has a handler", slot.rva);
    }
    for (const auto& call : DownSyncSnapCalls()) {
        const auto* at = image.At(call.rva, 5);
        Check(at && CallTargets(at, call.rva, call.target), call.name, call.rva);
        Check(DownSyncSnapHandler(call.rva) != nullptr, "each jump call has a handler", call.rva);
    }
    // The vtables are the classes named: their RTTI type names.
    for (const auto& c : DownSyncClasses()) {
        const auto* col = image.At(c.vtable - 8, 8);
        bool named = false;
        if (col) {
            std::uint64_t colVa = 0;
            std::memcpy(&colVa, col, 8);
            const auto* colData = image.At(static_cast<std::uint32_t>(colVa - base), 24);
            if (colData) {
                std::uint32_t td = 0;
                std::memcpy(&td, colData + 12, 4);
                const auto* name = reinterpret_cast<const char*>(image.At(td + 16, 48));
                named = name && std::strstr(name, c.name) != nullptr;
            }
        }
        Check(named, c.name, c.vtable);
    }
    // The sync object's vtable is net::WalkCharacterSyncControl.
    const auto* sync = image.At(0x17ABA10 - 8, 8);
    bool syncNamed = false;
    if (sync) {
        std::uint64_t colVa = 0;
        std::memcpy(&colVa, sync, 8);
        const auto* colData = image.At(static_cast<std::uint32_t>(colVa - base), 24);
        if (colData) {
            std::uint32_t td = 0;
            std::memcpy(&td, colData + 12, 4);
            const auto* name = reinterpret_cast<const char*>(image.At(td + 16, 48));
            syncNamed = name && std::strcmp(name, ".?AVWalkCharacterSyncControl@net@@") == 0;
        }
    }
    Check(syncNamed, "17ABA10 is net::WalkCharacterSyncControl", 0x17ABA10);
}

}  // namespace

int main(int argc, char** argv) {
    PureParts();
    if (argc < 2) {
        std::printf("usage: DownSyncTests EDF.dll\n");
        return 2;
    }
    Image image;
    std::ifstream in(argv[1], std::ios::binary);
    image.file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (image.file.size() < 0x1000) {
        std::printf("FAIL: cannot read %s\n", argv[1]);
        return 1;
    }
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.file.data());
    image.nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.file.data() + dos->e_lfanew);
    Check(image.nt->FileHeader.TimeDateStamp == kImageTimeDateStamp, "EDF.dll is the supported build");
    Tables(image);
    std::printf("Downed enemy sync: %d failures\n", failures);
    return failures ? 1 : 0;
}
