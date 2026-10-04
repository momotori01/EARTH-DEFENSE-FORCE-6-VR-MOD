// The seat log (ridelog.h): the lines it writes for a stand-in vehicle laid out as the game's (RTTI name,
// seat count at +0x618), the verdict it gives a seat request, that a repeat writes nothing, that the three
// handlers read the registers the hooks see, and that a launch writes at most kRideLogLines lines.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../src/log.h"
#include "../src/ridelog.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// A polymorphic object as MSVC x64 lays it out: vtable[-1] -> complete object locator {1, 0, 0, type
// descriptor RVA, 0, locator RVA}, the RVAs counted from `module`, the decorated name 16 bytes into the
// type descriptor.
struct FakeModule {
    alignas(16) unsigned char bytes[0x200]{};
};
FakeModule module;
std::uint64_t vtable[2];
alignas(16) unsigned char vehicle[0x700]{};
alignas(16) unsigned char soldier[0x2000]{};

void BuildVehicle(std::uint64_t seats) {
    std::memcpy(module.bytes + 0x40 + 16, ".?AVVehicle507_Rescuetank@@", 28);
    auto* col = reinterpret_cast<std::uint32_t*>(module.bytes + 0x100);
    col[0] = 1;
    col[3] = 0x40;
    col[5] = 0x100;
    vtable[0] = reinterpret_cast<std::uint64_t>(col);
    vtable[1] = 0;
    const std::uint64_t vt = reinterpret_cast<std::uint64_t>(&vtable[1]);
    std::memcpy(vehicle, &vt, 8);
    std::memcpy(vehicle + kVehicleSeatCount, &seats, 8);
}

std::string Line(RideEvent event, const void* who, int seat, std::uint32_t counter) {
    char text[320]{};
    const std::size_t n = FormatRideLine(event, who, vehicle, seat, counter, text, sizeof(text));
    return std::string(text, n);
}

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

std::string ReadAll(const std::wstring& path) {
    std::string text;
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return text;
    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read) text.append(buffer, read);
    CloseHandle(handle);
    return text;
}

std::size_t Count(const std::string& text, const char* what) {
    std::size_t n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) ++n;
    return n;
}

}  // namespace

int main() {
    ResetRideLogForTest();
    BuildVehicle(5);

    // Our seat, then the same again (nothing), then none.
    const std::string ours = Line(RideEvent::Ours, soldier, 3, 0);
    Check(Has(ours, "RIDE ours: seat 3 of 5 in Vehicle507_Rescuetank"), "our seat names the vehicle and its seat count");
    Check(Line(RideEvent::Ours, soldier, 3, 0).empty(), "the same seat again writes nothing");
    Check(Has(Line(RideEvent::Ours, soldier, -1, 0), "RIDE ours: no seat now"), "leaving says so");
    Check(Has(Line(RideEvent::Ours, soldier, 3, 0), "seat 3 of 5"), "and taking it again is a new line");

    // Another player's seat; with no room to name them, the soldier itself.
    const std::string theirs = Line(RideEvent::Theirs, soldier, 4, 0);
    Check(Has(theirs, "RIDE theirs: soldier ") && Has(theirs, "takes seat 4 of 5 in Vehicle507_Rescuetank"),
          "their seat names who and where");

    // A seat request and its verdict: inside the seats and newer, outside them, older than the soldier's.
    std::uint32_t mine = 7;
    std::memcpy(soldier + kSoldierRideCounter, &mine, 4);
    Check(Has(Line(RideEvent::Request, soldier, 2, 7), "asks for seat 2 of 5") &&
              Has(Line(RideEvent::Request, soldier, 2, 8), ": taken"),
          "a request inside the seats and not older is taken");
    const std::string pastLast = Line(RideEvent::Request, soldier, 5, 9);
    Check(Has(pastLast, "seat 5 of 5") && Has(pastLast, "outside the seats - refused") &&
              Has(Line(RideEvent::Request, soldier, 6, 9), "outside the seats - refused") &&
              Has(Line(RideEvent::Request, soldier, -1, 9), "outside the seats - refused"),
          "a seat past the last one, or negative, is refused");
    Check(Has(Line(RideEvent::Request, soldier, 1, 6), "older than their change 7 - dropped"),
          "a request older than the soldier's last change is dropped");
    Check(Line(RideEvent::Request, soldier, 1, 6).empty(), "and the same request again writes nothing");

    // A vehicle that is not readable at all still gives a line.
    char text[320]{};
    Check(FormatRideLine(RideEvent::Theirs, soldier, nullptr, 1, 0, text, sizeof(text)) > 0 &&
              Has(text, "takes seat 1 of 0 in ?"),
          "no vehicle: seat count 0 and an unknown class");

    // A vehicle is noted when first seen and again only after kVehicleGoneMs without it.
    {
        char seen[200]{};
        Check(NoteVehicle(vehicle, 10000, seen, sizeof(seen)) > 0 &&
                  Has(seen, "RIDE vehicle here: Vehicle507_Rescuetank") && Has(seen, ", 5 seats"),
              "a vehicle seen for the first time is noted with its class and seats");
        Check(NoteVehicle(vehicle, 10016, seen, sizeof(seen)) == 0 &&
                  NoteVehicle(vehicle, 10016 + kVehicleGoneMs - 1, seen, sizeof(seen)) == 0,
              "and not again while it keeps running its update");
        Check(NoteVehicle(vehicle, 10016 + 2 * kVehicleGoneMs, seen, sizeof(seen)) > 0,
              "but again after it was gone (its address may now be another vehicle)");
        Check(NoteVehicle(nullptr, 20000, seen, sizeof(seen)) == 0, "no vehicle, no line");
    }

    // The handlers, through the registers the hooks see.
    const std::wstring log = L"ridelog-test.log";
    DeleteFileW(log.c_str());
    LogOpen(log.c_str());
    ResetRideLogForTest();
    Check(RideLogHookHandler(kRideSend) && RideLogHookHandler(kRideReceive) && RideLogHookHandler(kRideRequest) &&
              RideLogHookHandler(kVehicleUpdate) && !RideLogHookHandler(0x123456),
          "a handler for each site");
    CpuContext send{};
    send.rdi = reinterpret_cast<std::uintptr_t>(soldier);
    send.rbp = reinterpret_cast<std::uintptr_t>(vehicle);
    send.rax = 0xFFFFFFFF00000002ull;  // eax is the index; the upper half is whatever was there
    RideLogHookHandler(kRideSend)(&send);
    // The receive handler reads the vehicle from the game's stack: [rsp+0x70] at the site, where rsp is just
    // past the saved registers.
    struct {
        CpuContext context;
        unsigned char stack[0x100];
    } frame{};
    const std::uint64_t vehicleAddress = reinterpret_cast<std::uint64_t>(vehicle);
    std::memcpy(reinterpret_cast<unsigned char*>(SiteRsp(&frame.context)) + 0x70, &vehicleAddress, 8);
    frame.context.rsi = reinterpret_cast<std::uintptr_t>(soldier) + 0x120;
    frame.context.r12 = 3;
    RideLogHookHandler(kRideReceive)(&frame.context);
    CpuContext request{};
    request.rsi = reinterpret_cast<std::uintptr_t>(soldier);
    request.rbx = reinterpret_cast<std::uintptr_t>(vehicle) + 0x120;
    request.r14 = 6;
    request.r15 = 9;
    RideLogHookHandler(kRideRequest)(&request);
    CpuContext update{};
    update.rcx = reinterpret_cast<std::uintptr_t>(vehicle);
    RideLogHookHandler(kVehicleUpdate)(&update);
    RideLogHookHandler(kVehicleUpdate)(&update);
    std::string written = ReadAll(log);
    Check(Count(written, "RIDE vehicle here: Vehicle507_Rescuetank") == 1, "the update hook: rcx, once while it runs");
    Check(Has(written, "RIDE ours: seat 2 of 5 in Vehicle507_Rescuetank"), "the send hook: rdi, rbp and eax");
    Check(Has(written, "takes seat 3 of 5 in Vehicle507_Rescuetank"), "the receive hook: rsi-0x120, r12d and [rsp+0x70]");
    Check(Has(written, "asks for seat 6 of 5 in Vehicle507_Rescuetank") && Has(written, "(change 9): outside the seats - refused"),
          "the request hook: rsi, rbx-0x120, r14d and r15d");

    // At most kRideLogLines lines a launch, and one saying the rest are not written.
    for (int i = 0; i < kRideLogLines + 50; ++i) {
        request.r15 = 100 + static_cast<std::uint32_t>(i);
        RideLogHookHandler(kRideRequest)(&request);
    }
    written = ReadAll(log);
    Check(Count(written, "RIDE request:") + Count(written, "RIDE ours:") + Count(written, "RIDE theirs:") +
                  Count(written, "RIDE vehicle here:") ==
              static_cast<std::size_t>(kRideLogLines),
          "the line budget holds");
    Check(Count(written, "the rest of this launch's seat changes are not logged") == 1, "and says so once");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    DeleteFileW(log.c_str());
    std::printf("seat log verified\n");
    return 0;
}
