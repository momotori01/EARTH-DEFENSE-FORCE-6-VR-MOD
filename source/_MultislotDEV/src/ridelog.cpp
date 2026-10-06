#include "ridelog.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "handaim.h"
#include "log.h"

namespace multislot {
namespace {

std::uintptr_t game = 0;
std::atomic<int> linesLeft{kRideLogLines};

// The last event written from each place, so a vehicle that repeats itself every frame writes one line.
struct Last {
    const void* soldier = nullptr;
    const void* vehicle = nullptr;
    int seat = -2;
    std::uint32_t counter = 0;
};
Last last[3];
// A vehicle repeats every seat request it holds every few seconds (2026-10-05: 256 of one log's 400 lines), so
// requests are kept in a ring and each one is written once, whatever came between.
constexpr int kRecentRequests = 32;
Last recentRequests[kRecentRequests];
int nextRequest = 0;

// Vehicles seen recently: a vehicle runs its update every frame, so one missing for kVehicleGoneMs is gone
// (and its address may come back as another).
struct Seen {
    const void* vehicle = nullptr;
    std::uint64_t at = 0;
};
constexpr int kSeenVehicles = 64;
Seen seen[kSeenVehicles];

std::uint64_t ReadU64(const void* object, std::size_t offset) {
    std::uint64_t value = 0;
    __try {
        std::memcpy(&value, static_cast<const unsigned char*>(object) + offset, sizeof(value));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return value;
}

// MSVC x64 RTTI, as spawn.cpp reads it: vtable[-1] is the complete object locator {signature 1, ...,
// type descriptor RVA at +12, locator RVA at +20}; the type descriptor's name starts 16 bytes in.
void ClassName(const void* object, char* out, std::size_t size) {
    std::snprintf(out, size, "?");
    if (!object) return;
    __try {
        const auto vtable = *static_cast<const std::uint64_t*>(object);
        const auto locator = *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(vtable - 8));
        const auto* col = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(locator));
        if (col[0] != 1) return;
        const auto* name = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(locator - col[5] + col[3] + 16));
        if (std::strncmp(name, ".?AV", 4) == 0) name += 4;
        std::size_t i = 0;
        for (; i + 1 < size && name[i] && name[i] != '@'; ++i) out[i] = name[i];
        out[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(out, size, "?");
    }
}

void Who(const void* soldier, char* out, std::size_t size) {
    char peer[40]{};
    if (soldier && game && SoldierPeer(game, soldier, peer, sizeof(peer)))
        std::snprintf(out, size, "EOS %s", peer);
    else
        std::snprintf(out, size, "soldier %p", soldier);
}

void Emit(const char* line) {
    const int left = linesLeft.fetch_sub(1);
    if (left > 0) Log("%s", line);
    if (left == 1) Log("RIDE: %d seat lines written; the rest of this launch's seat changes are not logged", kRideLogLines);
}

void Write(RideEvent event, const void* soldier, const void* vehicle, int seat, std::uint32_t counter) {
    char line[320];
    if (FormatRideLine(event, soldier, vehicle, seat, counter, line, sizeof(line))) Emit(line);
}

void OnSend(CpuContext* context) {
    Write(RideEvent::Ours, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rdi)),
          reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rbp)),
          static_cast<int>(static_cast<std::uint32_t>(context->rax)), 0);
}

void OnReceive(CpuContext* context) {
    const auto vehicle = ReadU64(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(SiteRsp(context))), 0x70);
    Write(RideEvent::Theirs, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rsi - 0x120)),
          reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vehicle)),
          static_cast<int>(static_cast<std::uint32_t>(context->r12)), 0);
}

void OnVehicleUpdate(CpuContext* context) {
    char line[200];
    if (NoteVehicle(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rcx)), GetTickCount64(), line,
                    sizeof(line)))
        Emit(line);
}

void OnRequest(CpuContext* context) {
    Write(RideEvent::Request, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rsi)),
          reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->rbx - 0x120)),
          static_cast<int>(static_cast<std::uint32_t>(context->r14)), static_cast<std::uint32_t>(context->r15));
}

}  // namespace

std::size_t FormatRideLine(RideEvent event, const void* soldier, const void* vehicle, int seat,
                           std::uint32_t counter, char* out, std::size_t size) {
    const Last now{soldier, vehicle, seat, counter};
    const auto same = [&](const Last& other) {
        return other.soldier == soldier && other.vehicle == vehicle && other.seat == seat && other.counter == counter;
    };
    if (event == RideEvent::Request) {
        for (const auto& recent : recentRequests)
            if (same(recent)) return 0;
        recentRequests[nextRequest] = now;
        nextRequest = (nextRequest + 1) % kRecentRequests;
    } else {
        Last& previous = last[static_cast<int>(event)];
        if (same(previous)) return 0;
        previous = now;
    }
    char name[96], who[64];
    ClassName(vehicle, name, sizeof(name));
    Who(soldier, who, sizeof(who));
    const auto seats = static_cast<unsigned long long>(vehicle ? ReadU64(vehicle, kVehicleSeatCount) : 0);
    int n = 0;
    switch (event) {
    case RideEvent::Ours:
        n = seat < 0 ? std::snprintf(out, size, "RIDE ours: no seat now (%s %p)", name, vehicle)
                     : std::snprintf(out, size, "RIDE ours: seat %d of %llu in %s %p", seat, seats, name, vehicle);
        break;
    case RideEvent::Theirs:
        n = std::snprintf(out, size, "RIDE theirs: %s takes seat %d of %llu in %s %p", who, seat, seats, name, vehicle);
        break;
    case RideEvent::Request: {
        const auto theirs = static_cast<std::uint32_t>(ReadU64(soldier, kSoldierRideCounter));
        char verdict[64];
        if (seat < 0 || static_cast<unsigned long long>(seat) >= seats)
            std::snprintf(verdict, sizeof(verdict), "outside the seats - refused");
        else if (static_cast<std::int32_t>(theirs) > static_cast<std::int32_t>(counter))
            std::snprintf(verdict, sizeof(verdict), "older than their change %u - dropped", theirs);
        else
            std::snprintf(verdict, sizeof(verdict), "taken");
        n = std::snprintf(out, size, "RIDE request: %s asks for seat %d of %llu in %s %p (change %u): %s", who, seat,
                          seats, name, vehicle, counter, verdict);
        break;
    }
    }
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

std::size_t NoteVehicle(const void* vehicle, std::uint64_t now, char* out, std::size_t size) {
    if (!vehicle) return 0;
    Seen* slot = nullptr;
    Seen* oldest = &seen[0];
    for (auto& entry : seen) {
        if (entry.vehicle == vehicle) {
            slot = &entry;
            break;
        }
        if (entry.at < oldest->at) oldest = &entry;
    }
    const bool fresh = slot && now - slot->at < kVehicleGoneMs;
    if (!slot) slot = oldest;
    *slot = {vehicle, now};
    if (fresh) return 0;
    char name[96];
    ClassName(vehicle, name, sizeof(name));
    const int n = std::snprintf(out, size, "RIDE vehicle here: %s %p, %llu seats", name, vehicle,
                                static_cast<unsigned long long>(ReadU64(vehicle, kVehicleSeatCount)));
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}

void ResetRideLogForTest() {
    for (auto& entry : seen) entry = Seen{};
    for (auto& entry : last) entry = Last{};
    for (auto& entry : recentRequests) entry = Last{};
    nextRequest = 0;
    linesLeft.store(kRideLogLines);
    game = 0;
}

void InitRideLog(unsigned char* gameBase) { game = reinterpret_cast<std::uintptr_t>(gameBase); }

std::vector<MidSite> RideLogHooks() {
    // Each site's instructions are position independent and none is a branch target; the cmp at 632699 is
    // replayed after the handler, so the jg after it still reads its flags.
    return {{"seat log: our seat goes out", kRideSend, {0x8B, 0xD8, 0x0F, 0x57, 0xC0, 0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x28}, 0, 11},
            {"seat log: another player's seat arrives", kRideReceive, {0x45, 0x8B, 0xC4, 0x48, 0x8D, 0x54, 0x24, 0x70}, 0, 8},
            {"seat log: a vehicle is asked for a seat", kRideRequest, {0x44, 0x39, 0xBE, 0x24, 0x18, 0x00, 0x00}, 0, 7},
            // The update's first instruction, `mov [rsp+0x20], rbx` (its home slot), before anything is pushed.
            {"seat log: a vehicle is here", kVehicleUpdate, {0x48, 0x89, 0x5C, 0x24, 0x20}, 0, 5}};
}

MidHandler RideLogHookHandler(std::uint32_t rva) {
    if (rva == kRideSend) return &OnSend;
    if (rva == kRideReceive) return &OnReceive;
    if (rva == kRideRequest) return &OnRequest;
    if (rva == kVehicleUpdate) return &OnVehicleUpdate;
    return nullptr;
}

}  // namespace multislot
