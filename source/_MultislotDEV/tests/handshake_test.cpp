#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include "../src/joinlog.h"
#include "../src/log.h"

using namespace multislot;
namespace {
int failures = 0;
void Check(bool ok, const char* label) {
    if (!ok) { std::printf("FAIL: %s\n", label); ++failures; }
}
template <typename T> void Put(unsigned char* data, std::size_t offset, T value) {
    std::memcpy(data + offset, &value, sizeof(value));
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    // Real MSVC vector layout; shared_ptr entries have an object and a control-block pointer.
    std::array<std::array<unsigned char, 0xA0>, 8> users{};
    std::uintptr_t slots[16]{};
    for (std::size_t i = 0; i < 8; ++i) {
        slots[2 * i] = reinterpret_cast<std::uintptr_t>(users[i].data());
        Put(users[i].data(), 0x18, std::uintptr_t{0x1000} + i);
        Put(users[i].data(), 0x40, static_cast<std::int32_t>(i));
        Put(users[i].data(), 0x10, std::uint32_t{3});
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(slots);
    std::uintptr_t vector[] = {begin, begin + sizeof(slots), begin + sizeof(slots)};
    UserSlotsSnapshot snapshot{};
    Check(ReadUserSlots(vector, snapshot) && snapshot.occupied == 8 && snapshot.ready == 8, "eight ready users");
    slots[2] = 0;
    Check(ReadUserSlots(vector, snapshot) && snapshot.occupied == 7 && snapshot.ready == 7,
          "a hole below the highest slot is not counted as an eighth member");
    Put(users[7].data(), 0x10, std::uint32_t{2});
    Check(ReadUserSlots(vector, snapshot) && snapshot.occupied == 7 && snapshot.ready == 6 &&
          snapshot.slots[7].productId == 0x1007 && snapshot.slots[7].index == 7,
          "allocated but unconfirmed user is identified separately");
    Put(users[7].data(), 0x10, std::uint32_t{3});
    Check(ReadUserSlots(vector, snapshot) && snapshot.ready == 7, "confirmation without a slot-count change");
    slots[14] = 0;
    Check(ReadUserSlots(vector, snapshot) && snapshot.occupied == 6 && snapshot.slots[7].object == 0,
          "removal discards the previous observation");
    slots[2] = reinterpret_cast<std::uintptr_t>(users[1].data());
    Put(users[1].data(), 0x10, std::uint32_t{2});
    Check(ReadUserSlots(vector, snapshot) && snapshot.occupied == 7 && snapshot.ready == 6,
          "slot reuse can start a new pending handshake");
    vector[1] = begin + 9 * 16;
    vector[2] = vector[1];
    Check(!ReadUserSlots(vector, snapshot) && snapshot.occupied == 0, "oversized vector rejected");
    vector[1] = begin + 17;
    Check(!ReadUserSlots(vector, snapshot), "partial shared_ptr rejected");
    vector[1] = begin + 16;
    vector[2] = begin;
    Check(!ReadUserSlots(vector, snapshot), "end beyond allocation rejected");
    Check(!ReadUserSlots(nullptr, snapshot), "null users rejected");
    auto* inaccessible = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    Check(inaccessible && !ReadUserSlots(inaccessible, snapshot), "unreadable users rejected without crashing");
    if (inaccessible) VirtualFree(inaccessible, 0, MEM_RELEASE);
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x22CE000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(image != nullptr, "fake image allocated");
    if (image) {
        const auto base = reinterpret_cast<std::uintptr_t>(image);
        unsigned char session[0x38]{}, room[0x170]{};
        std::uintptr_t currentUsers = 0;
        const char* reason = nullptr;
        Check(!ReadCurrentRoomUsers(base, currentUsers, reason) && reason && currentUsers == 0, "missing session has a diagnostic reason");
        Put(image, 0x20B2AC0, reinterpret_cast<std::uintptr_t>(session));
        Put(session, 0x28, reinterpret_cast<std::uintptr_t>(room));
        Put(room, 0, base + 0x17ECA00);
        Put(room, 0x160, reinterpret_cast<std::uintptr_t>(vector));
        Check(ReadCurrentRoomUsers(base, currentUsers, reason) && !reason &&
              currentUsers == reinterpret_cast<std::uintptr_t>(vector), "RoomImpl resolves the active Users object");
        vector[1] = vector[2] = begin + sizeof(slots);
        DeleteFileW(argv[1]);
        LogOpen(argv[1]);
        SetDetailLog(true);
        InitJoinLog(image);
        ForgetUserSlots();
        NoteUserSlots(6);
        Put(room, 0, base + 0x17EC968);
        Check(!ReadCurrentRoomUsers(base, currentUsers, reason) && reason && currentUsers == 0,
              "base Room constructor/destructor state rejected");
        ForgetUserSlots();
        NoteUserSlots(6);
        Put(room, 0, base + 0x17ECA00);
        ForgetUserSlots();
        NoteUserSlots(6);
        std::ifstream log(argv[1], std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
        const std::string success = "ROOM USERS: occupied=7 ready=6 pending=1 roster=6 slots=8";
        const auto first = text.find(success);
        Check(first != std::string::npos && text.find(success, first + success.size()) != std::string::npos,
              "real diagnostic path emits counts before and after an unavailable room");
        Check(text.find("ROOM USERS unavailable: unexpected or unreadable room type") != std::string::npos,
              "failed room guard is visible instead of silently dropping diagnostics");
        Put(room, 0x160, std::uintptr_t{0});
        Check(!ReadCurrentRoomUsers(base, currentUsers, reason) && reason, "missing Users has a diagnostic reason");
        VirtualFree(image, 0, MEM_RELEASE);
        InitJoinLog(nullptr);
    }
    std::printf("handshake slot snapshots: %d failures\n", failures);
    return failures ? 1 : 0;
}
