// Loads the built plugin the way EDFModLoader does, against the real EDF.dll mapped without running
// its initialisation, then calls the patched SEARCH_TYPE functions (all leaf code) to check what
// they now return, compared with what the same functions returned before the plugin loaded.
// The plugin sits in <work-folder>\<mode>\Mods\Plugins, as in a game folder.
//   MultiSlotLoadTest EDF.dll EDF6MultiSlot.dll work-folder off|host4|host8|fresh|nomission
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include "PluginAPI.h"
#pragma warning(pop)

#include "../src/hostmode.h"
#include "../src/midhook.h"
#include "../src/patches.h"
#include "../src/gaplog.h"
#include "../src/packetsize.h"
#include "../src/smoothing.h"
#include "menu_layout.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::string ReadText(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

using Load = bool(EDFMLAPI*)(PluginInfo*);
using FamilyFn = bool(*)(int);        // EDF+749AC0 SEARCH_TYPE join check
using DecodeFn = int(*)(int);         // EDF+74AC20 SEARCH_TYPE -> room kind
using RangeFn = std::uint64_t(*)(int);  // EDF+74AC50 room kind -> searched range, (high << 32) | low
using MapFn = int(*)(int);            // EDF+74ACA0 room kind -> SEARCH_TYPE

struct Behaviour {
    std::vector<int> values;
    std::vector<bool> family;
    std::vector<int> decode;
    std::vector<std::uint64_t> range;
    std::vector<int> map;
};

Behaviour Observe(const unsigned char* base) {
    Behaviour b;
    for (int v = -0x20; v < 0x240; ++v) b.values.push_back(v);
    for (int v : {0x1590, 0x7FFFFFFF, INT_MIN, INT_MIN + 1}) b.values.push_back(v);
    const auto family = reinterpret_cast<FamilyFn>(base + 0x749AC0);
    const auto decode = reinterpret_cast<DecodeFn>(base + 0x74AC20);
    const auto range = reinterpret_cast<RangeFn>(base + 0x74AC50);
    const auto map = reinterpret_cast<MapFn>(base + 0x74ACA0);
    for (int v : b.values) {
        b.family.push_back(family(v));
        b.decode.push_back(decode(v));
    }
    for (int kind = 0; kind < 8; ++kind) {
        b.range.push_back(range(kind));
        b.map.push_back(map(kind));
    }
    return b;
}

// This version's MultiSlot rooms: the vanilla kinds mirrored around the centre.
bool IsMirror(int v) { return v >= 2 * static_cast<int>(kSearchTypeCenter) - 0x94 && v <= 2 * static_cast<int>(kSearchTypeCenter) - 0x91; }
// Rooms of MultiSlot 0.2-0.4.1 (0x8C..0x8F), 0.4.2-0.4.3 (0x7C..0x7F), 0.5.0-1.0.0 (0x74..0x77), 1.1.0-1.1.1 (0x6C..0x6F)
// and 1.2.0 (0x64..0x67).
bool IsOldMirror(int v) {
    return !IsMirror(v) && ((v >= 0x8C && v <= 0x8F) || (v >= 0x7C && v <= 0x7F) || (v >= 0x74 && v <= 0x77) ||
                            (v >= 0x6C && v <= 0x6F) || (v >= 0x64 && v <= 0x67));
}

// SoldierBase slot 92 (EDF+59FB10): which fields this frame's packet for a soldier carries; bit 0 is the
// position. Run on a zeroed stand-in whose slot +0x108 is the game's own 550890 ("player" when +0x340 or
// +0x1ED0 is set). The two helpers it calls (77B3E0, 77C1F0) only touch the object.
using FieldsFn = std::uint16_t (*)(void*);

struct Fields {
    std::vector<std::uint16_t> moving;  // a player
    std::vector<std::uint16_t> still;   // a player on the branch that sends no movement (+0x2E8 set)
    std::vector<std::uint16_t> ai;      // an AI soldier
};

constexpr int kFieldFrames = 120;  // the longest cycle in 59FB10

Fields ObserveFields(const unsigned char* base) {
    std::vector<const void*> vtable(0x80, nullptr);
    vtable[0x108 / 8] = base + 0x550890;
    const auto fields = reinterpret_cast<FieldsFn>(const_cast<unsigned char*>(base) + 0x59FB10);
    Fields f;
    alignas(16) static unsigned char object[0x2000];
    for (int kind = 0; kind < 3; ++kind)
        for (int counter = 0; counter < kFieldFrames; ++counter) {
            std::memset(object, 0, sizeof(object));
            const void* table = vtable.data();
            std::memcpy(object, &table, sizeof(table));
            if (kind != 2) object[0x340] = 1;
            if (kind == 1) object[0x2E8] = 1;
            std::memcpy(object + 0x1820, &counter, sizeof(counter));
            (kind == 0 ? f.moving : kind == 1 ? f.still : f.ai).push_back(fields(object));
        }
    return f;
}

bool InModule(std::uintptr_t address, HMODULE module) {
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(module)->e_lfanew);
    return address >= base && address < base + nt->OptionalHeader.SizeOfImage;
}

// `call rel32` at site -> stub `mov rax, imm64; jmp rax` -> address inside the plugin image.
bool RedirectedInto(const unsigned char* site, HMODULE plugin) {
    if (site[0] != 0xE8) return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, site + 1, 4);
    const unsigned char* stub = site + 5 + relative;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(stub, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
        return false;
    if (stub[0] != 0x48 || stub[1] != 0xB8 || stub[10] != 0xFF || stub[11] != 0xE0) return false;
    std::uintptr_t target = 0;
    std::memcpy(&target, stub + 2, 8);
    return InModule(target, plugin);
}

// `jmp rel32` at the site -> a thunk laid out like MidThunkCode (handler inside the plugin, the
// site's displaced bytes, resume right after the site).
bool HookedInto(const unsigned char* base, const MidSite& site, HMODULE plugin) {
    const unsigned char* at = base + site.rva;
    if (at[0] != 0xE9) return false;
    for (std::size_t i = 5; i < site.original.size(); ++i)
        if (at[i] != 0x90) return false;
    std::int32_t relative = 0;
    std::memcpy(&relative, at + 1, 4);
    const unsigned char* thunk = at + 5 + relative;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(thunk, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || mbi.Protect != PAGE_EXECUTE_READ) return false;
    const std::uint64_t marker = 0x1122334455667788ull;
    const auto reference = MidThunkCode(reinterpret_cast<MidHandler>(static_cast<std::uintptr_t>(marker)),
                                        site.original.data() + site.displacedOffset, site.displacedSize, marker);
    std::size_t handlerAt = 0;
    for (std::size_t i = 0; i + 8 <= reference.size(); ++i) {
        std::uint64_t value = 0;
        std::memcpy(&value, reference.data() + i, 8);
        if (value == marker) {
            handlerAt = i;
            break;
        }
    }
    for (std::size_t i = 0; i < reference.size(); ++i)
        if ((i < handlerAt || i >= handlerAt + 8) && i < reference.size() - 8 && thunk[i] != reference[i]) return false;
    std::uint64_t handler = 0, resume = 0;
    std::memcpy(&handler, thunk + handlerAt, 8);
    std::memcpy(&resume, thunk + reference.size() - 8, 8);
    return handlerAt && InModule(static_cast<std::uintptr_t>(handler), plugin) &&
           resume == static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(at + site.original.size()));
}

bool SiteUntouched(const unsigned char* base, const MidSite& site) {
    return std::memcmp(base + site.rva, site.original.data(), site.original.size()) == 0;
}

bool SlotInto(const unsigned char* base, const PointerSlot& slot, HMODULE plugin) {
    std::uintptr_t value = 0;
    std::memcpy(&value, base + slot.rva, sizeof(value));
    return InModule(value, plugin);
}

int Applied(const unsigned char* base, const std::vector<Patch>& patches) {
    int count = 0;
    for (const auto& patch : patches)
        if (std::memcmp(base + patch.rva, patch.replacement.data(), patch.replacement.size()) == 0) ++count;
    return count;
}

int Untouched(const unsigned char* base, const std::vector<Patch>& patches) {
    int count = 0;
    for (const auto& patch : patches)
        if (Matches(base + patch.rva, patch)) ++count;
    return count;
}

// Kept free of C++ objects: __try cannot share a function with destructors.
__declspec(noinline) void WriteToBadAddress() {
    __try {
        volatile std::uintptr_t bad = 0x10;
        *reinterpret_cast<int*>(bad) = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

__declspec(noinline) void ThrowAndCatch() {
    try {
        throw std::runtime_error("thrown and caught outside EDF.dll");
    } catch (const std::exception&) {
    }
}

void WriteBytes(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    std::ofstream(path, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(bytes.data()),
                                                                  static_cast<std::streamsize>(bytes.size()));
}

std::vector<unsigned char> ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 5) {
        std::printf("usage: MultiSlotLoadTest EDF.dll EDF6MultiSlot.dll work-folder off|host4|host8|fresh|nomission\n");
        return 2;
    }
    const std::wstring mode = argv[4];
    const HMODULE game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!game) {
        std::printf("FAIL: cannot map %ls (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    const auto base = reinterpret_cast<const unsigned char*>(game);
    const Behaviour before = Observe(base);
    const Fields fieldsBefore = ObserveFields(base);
    // What the send-side analysis rests on, read from the game's own code: a player's packet carries the
    // position one frame in six and a packet is built every frame; an AI soldier's is built one frame in
    // six and carries the position one frame in thirty.
    for (int c = 0; c < kFieldFrames; ++c) {
        const auto i = static_cast<std::size_t>(c);
        Check(((fieldsBefore.moving[i] & 1) != 0) == (c % 6 == 3) && ((fieldsBefore.still[i] & 1) != 0) == (c % 6 == 3),
              "vanilla: a player's position goes in when the frame counter is 3 mod 6");
        Check(fieldsBefore.moving[i] != 0 && fieldsBefore.still[i] != 0, "vanilla: a player has a packet every frame");
        Check((fieldsBefore.moving[i] & 0x10) != 0 && (fieldsBefore.still[i] & 0x10) == 0,
              "vanilla: only the moving branch sends movement");
        Check(((fieldsBefore.ai[i] & 1) != 0) == (c % 30 == 0), "vanilla: an AI soldier's position goes in one frame in 30");
        Check((fieldsBefore.ai[i] != 0) == (c % 6 == 0), "vanilla: an AI soldier has a packet one frame in six");
        // The angles (bit 1) run on the same cycle three frames apart, so the timer never lets both through.
        Check(((fieldsBefore.moving[i] & 2) != 0) == (c % 6 == 0) && (fieldsBefore.still[i] & 2) == 0,
              "vanilla: a player's angles go in when the counter is 0 mod 6, never on the branch without movement");
    }

    const std::wstring folder = std::wstring(argv[3]) + L"\\" + mode;
    const std::wstring mods = folder + L"\\Mods";
    const std::wstring plugins = mods + L"\\Plugins";
    const std::wstring uiFolder = mods + L"\\UI";
    const std::wstring layoutPath = uiFolder + L"\\LYT_MAINFRAME.SGO";
    for (const auto& path : {std::wstring(argv[3]), folder, mods, plugins}) CreateDirectoryW(path.c_str(), nullptr);
    const std::wstring dll = plugins + L"\\EDF6MultiSlot.dll";
    const std::wstring logPath = plugins + L"\\EDF6MultiSlot.log";
    CopyFileW(argv[2], dll.c_str(), FALSE);
    DeleteFileW(logPath.c_str());
    const std::wstring iniPath = plugins + L"\\EDF6MultiSlot.ini";
    DeleteFileW(iniPath.c_str());
    const bool fresh = mode == L"fresh";  // no INI at all: defaults are written and used
    if (!fresh) {
        const char* ini = mode == L"off"         ? "[MultiSlot]\r\nEnabled=0\r\nEightPlayerRooms=1\r\n"
                          : mode == L"recoveryoff" ? "[MultiSlot]\r\nEnabled=1\r\nHandshakeRecovery=0\r\n"
                          : mode == L"quiet"     ? "[MultiSlot]\r\nEnabled=1\r\nHandshakeRecovery=0\r\nNetLog=0\r\n"
                          : mode == L"host4"     ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=0\r\n[Mission]\r\nExtraEnemies=0\r\n"
                          : mode == L"nomission" ? "[MultiSlot]\r\nEnabled=1\r\nMaxPlayers=4\r\n[Mission]\r\nExtend=0\r\n"
                          : mode == L"smooth"    ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Smoothing]\r\nRemotePlayerPercent=30\r\n"
                          : mode == L"latch"     ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nDesyncMeter=1\r\nClearSyncLatch=1\r\n"
                          : mode == L"nolatch"   ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nDesyncMeter=0\r\nClearSyncLatch=1\r\n"
                          : mode == L"desync"    ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nDesyncMeter=1\r\n"
                          : mode == L"reliable"  ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nTrafficMeter=0\r\nReliableGameTraffic=1\r\n"
                          : mode == L"position"  ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nPositionEveryPacket=1\r\nFacingEveryPacket=0\r\n"
                          : mode == L"facing"    ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nPositionEveryPacket=0\r\nFacingEveryPacket=1\r\n"
                          : mode == L"sendall"   ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nPositionEveryPacket=1\r\nFacingEveryPacket=1\r\n"
                          : mode == L"sendoff"   ? "[MultiSlot]\r\nEnabled=1\r\nNetLog=1\r\n[Sync]\r\nPositionEveryPacket=0\r\nFacingEveryPacket=0\r\n"
                                                 : "[MultiSlot]\r\nEnabled=1\r\nEightPlayerRooms=1\r\nCrashLog=1\r\nNetLog=1\r\n"
                                                   "[RoomScreen]\r\nDummyMembers=1\r\nPageKey=F2\r\nDummyAddKey=F3\r\nDummyRemoveKey=F4\r\n"
                                                   "[Test]\r\nGhostPlayers=4\r\n[Mission]\r\nScale6=1.25\r\n";
        std::ofstream(iniPath, std::ios::binary | std::ios::trunc) << ini;
    }

    // Mods\UI\LYT_MAINFRAME.SGO before loading: ours (off, host8), another mod's (nomission), none (host4, fresh).
    const std::vector<unsigned char> ours(kMenuLayoutBytes, kMenuLayoutBytes + sizeof(kMenuLayoutBytes));
    auto foreign = ours;
    foreign[foreign.size() / 2] ^= 0x5A;
    DeleteFileW(layoutPath.c_str());
    RemoveDirectoryW(uiFolder.c_str());
    if (mode == L"off" || mode == L"host8" || mode == L"nomission") {
        CreateDirectoryW(uiFolder.c_str(), nullptr);
        WriteBytes(layoutPath, mode == L"nomission" ? foreign : ours);
    }

    const HMODULE plugin = LoadLibraryW(dll.c_str());
    const auto load = plugin ? reinterpret_cast<Load>(GetProcAddress(plugin, "EML6_Load")) : nullptr;
    Check(load != nullptr, "plugin exports EML6_Load");
    if (!load) return 1;
    PluginInfo info{};
    const bool loaded = load(&info);
    const Behaviour after = Observe(base);
    const Fields fieldsAfter = ObserveFields(base);
    const std::string log = ReadText(logPath);
    // [Sync] PositionEveryPacket and FacingEveryPacket: on unless the INI says 0 (1.5.32), each switched
    // by its own key, and each touching only its own bit of a player's packets (0: position, 1: angles).
    const bool positions = mode != L"off" && mode != L"facing" && mode != L"sendoff";
    const bool facing = mode != L"off" && mode != L"position" && mode != L"sendoff";
    const auto positionPatches = PositionPatches();
    const auto facingPatches = FacingPatches();
    Check(positions ? Applied(base, positionPatches) == static_cast<int>(positionPatches.size())
                    : Untouched(base, positionPatches) == static_cast<int>(positionPatches.size()),
          "the position sites are written exactly when PositionEveryPacket=1");
    Check(facing ? Applied(base, facingPatches) == static_cast<int>(facingPatches.size())
                 : Untouched(base, facingPatches) == static_cast<int>(facingPatches.size()),
          "the facing site is written exactly when FacingEveryPacket=1");
    if (mode != L"off") {
        Check(Contains(log, positions ? "Sync/PositionEveryPacket=1:" : "Sync/PositionEveryPacket=0:"), "the position setting is logged");
        Check(Contains(log, facing ? "Sync/FacingEveryPacket=1:" : "Sync/FacingEveryPacket=0:"), "the facing setting is logged");
    }
    const std::uint16_t forced = static_cast<std::uint16_t>((positions ? 1 : 0) | (facing ? 2 : 0));
    for (int c = 0; c < kFieldFrames; ++c) {
        const auto i = static_cast<std::size_t>(c);
        Check(fieldsAfter.moving[i] == (fieldsBefore.moving[i] | forced),
              "a moving player's packet gains exactly the forced bits, nothing else changes");
        // The branch without movement never sends the angles, with or without the patch.
        Check(fieldsAfter.still[i] == (fieldsBefore.still[i] | (forced & 1)),
              "the branch without movement gains only the position");
    }
    Check(fieldsAfter.ai == fieldsBefore.ai, "AI soldiers' packets are the game's own");
    const auto guest = GuestPatches();
    const auto sessions = SessionPatches();
    const auto hostHooks = HostModeHooks();
    const auto calls = GuestCalls();
    const auto missionPatches = MissionPatches();
    const auto missionHooks = MissionHooks();
    const auto missionCalls = MissionCalls();
    const auto missionUntouched = [&] {
        bool untouched = Untouched(base, missionPatches) == static_cast<int>(missionPatches.size());
        for (const auto& hook : missionHooks) untouched = untouched && SiteUntouched(base, hook);
        for (const auto& call : missionCalls) untouched = untouched && CallTargets(base + call.rva, call.rva, call.target);
        for (const auto& hook : SpawnHooks()) untouched = untouched && SiteUntouched(base, hook);
        for (const auto& slot : MissionSlots()) untouched = untouched && SlotTargets(base + slot.rva, reinterpret_cast<std::uint64_t>(base), slot.target);
        return untouched;
    };

    if (mode == L"off") {
        Check(!loaded, "Enabled=0 asks the loader to unload");
        Check(Untouched(base, guest) == static_cast<int>(guest.size()), "Enabled=0 leaves every guest patch site untouched");
        Check(Untouched(base, sessions) == static_cast<int>(sessions.size()), "Enabled=0 leaves the room tables at four");
        for (const auto& hook : hostHooks) Check(SiteUntouched(base, hook), "Enabled=0 leaves the host room sites untouched");
        const PointerSlot frame = MainFrameSlot();
        Check(SlotTargets(base + frame.rva, reinterpret_cast<std::uint64_t>(base), frame.target), "Enabled=0 leaves the menu frame vtable untouched");
        const PointerSlot lobbySlot = LobbySlot();
        Check(SlotTargets(base + lobbySlot.rva, reinterpret_cast<std::uint64_t>(base), lobbySlot.target), "Enabled=0 leaves the room list vtable untouched");
        for (const auto& call : calls) Check(CallTargets(base + call.rva, call.rva, call.target), "Enabled=0 leaves calls untouched");
        Check(after.family == before.family && after.decode == before.decode && after.range == before.range && after.map == before.map,
              "Enabled=0 leaves SEARCH_TYPE behaviour identical");
        Check(Contains(log, "Enabled=0"), "Enabled=0 is logged");
        Check(missionUntouched(), "Enabled=0 leaves mission code untouched");
        Check(GetFileAttributesW(layoutPath.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(uiFolder.c_str()) == INVALID_FILE_ATTRIBUTES,
              "Enabled=0 removes our menu layout (and the UI folder it was alone in)");
        Check(Contains(log, "Menu: removed Mods\\UI\\LYT_MAINFRAME.SGO"), "the removal is logged");
    } else {
        const bool eightPlayers = mode == L"host8";
        // NetLog defaults to on: only host4's INI turns it off.
        const bool netLog = mode != L"host4" && mode != L"quiet";
        const bool recovery = mode != L"recoveryoff" && mode != L"quiet";
        for (const auto& call : RecoveryCalls()) {
            Check(recovery ? RedirectedInto(base + call.rva, plugin) : CallTargets(base + call.rva, call.rva, call.target),
                  "recovery call is installed only when enabled");
        }
        Check(Contains(log, recovery ? "HandshakeRecovery=1:" : "HandshakeRecovery=0:"), "recovery mode is logged");
        if (!netLog && recovery)
            // Both P2P imports: the send for the recovery hello, the receive for the traffic meter, which
            // is on by default. Neither needs the detailed log.
            Check(Contains(log, "EOS imports redirected: 2"), "recovery and the meter work without NetLog");
        Check(loaded, "EML6_Load succeeds against the supported EDF.dll");
        Check(info.infoVersion == PluginInfo::MaxInfoVer && info.name && std::strcmp(info.name, "EDF6 MultiSlot") == 0,
              "PluginInfo is filled in");
        Check(Applied(base, guest) == static_cast<int>(guest.size()), "every guest patch is written");
        Check(Applied(base, sessions) == static_cast<int>(sessions.size()), "room user slots and packet sessions are sized for eight");
        Check(Contains(log, ("Rooms: " + std::to_string(kMaxPlayers) + " user slots, packet sessions and voice chat HUD records").c_str()),
              "the room table size is logged");
        for (const auto& call : calls) Check(RedirectedInto(base + call.rva, plugin), "member count call reaches the plugin through a stub");
        for (const auto& call : RoomViewCalls()) Check(RedirectedInto(base + call.rva, plugin), "room screen call reaches the plugin through a stub");
        Check(SlotInto(base, RoomViewSlot(), plugin), "HUiRoom OnUpdate vtable slot points into the plugin");
        // DummyMembers=1 redirects every call to the room member list builder, so fake members reach the
        // room screen, the voice chat HUD and anything else that asks for the members.
        for (const auto& call : FakeMemberCalls()) {
            if (mode == L"host8") Check(RedirectedInto(base + call.rva, plugin), call.name);
            else Check(CallTargets(base + call.rva, call.rva, call.target), "without dummy members the member list calls are the game's");
        }

        // 8Player MOD: the room sites and the menu frame are hooked whatever the setting; the setting is
        // read when a room is created (hostmode_test covers the handlers).
        for (const auto& hook : hostHooks) Check(HookedInto(base, hook, plugin), hook.name);
        Check(SlotInto(base, MainFrameSlot(), plugin), "HUiMainFrame OnUpdate vtable slot points into the plugin");
        Check(SlotInto(base, LobbySlot(), plugin), "HUiLobby OnUpdate vtable slot points into the plugin");
        Check(Contains(log, ("Hosting: " + std::to_string(kModRoomCapacity) + "Player MOD " + (eightPlayers ? "ON" : "OFF")).c_str()),
              "the host mode setting is logged");
        if (fresh) {
            const std::string written = ReadText(iniPath);
            Check(Contains(written, "[RoomScreen]") && Contains(written, "EightPlayerRooms=0\r\n") && Contains(written, "NetLog=1\r\n") &&
                      Contains(written, "DummyMembers=0") && Contains(written, "PageKeys=F3,Tab\r\n") &&
                      Contains(written, "[CopyArmor]") && Contains(written, "PadButton=LeftStick\r\n") &&
                      Contains(written, "DummyAddKey=F6\r\n") && !Contains(written, "MaxPlayers=") && !Contains(written, "PageKey="),
                  "missing INI is written with the documented defaults (CRLF)");
            Check(Contains(log, "Wrote default settings"), "writing the default INI is logged");
            // A dump copies the game's memory, so it must never be on for someone who only installed
            // the package. The default INI says 0 and the plugin must read 0 when the key is absent.
            Check(Contains(written, "CrashDump=0\r\n"), "the default INI leaves crash dumps off");
            // The sync fix only helps others see whoever has it, so a fresh install must have it on.
            Check(Contains(written, "PositionEveryPacket=1\r\n") && Contains(written, "FacingEveryPacket=1\r\n"),
                  "the default INI sends the position and the facing in every packet");
        }
        Check(Contains(log, "Crash log armed"), "the crash log is armed");
        // The off-path wording is what tells the player nothing is being written. (The same line also
        // mentions CrashDump=1 as the way to turn it on, so the wording is checked, not the substring.)
        Check(Contains(log, "no crash dump (CrashDump is off by default"), "the log says the dump is off");
        // [Sync]: the meter measures and is on, the reliability upgrade changes what is sent and is not.
        // Everything shipped has to be harmless with no INI at all, and only one of these two is.
        if (mode == L"latch") {
            Check(Contains(log, "Sync/ClearSyncLatch=1:"), "the latch experiment can be turned on");
            Check(Contains(log, "EXPERIMENT"), "and says plainly that it is one");
            for (const auto& site : DesyncHooks())
                Check(HookedInto(base, site, plugin), "it rides the desync hook");
        } else if (mode == L"nolatch") {
            // It writes to the game from inside the desync hook, so without that hook it must not claim
            // to be doing anything.
            Check(Contains(log, "ignored: it needs DesyncMeter=1"), "without the meter the latch fix is refused");
            for (const auto& site : DesyncHooks())
                Check(!HookedInto(base, site, plugin), "and no hook is installed");
        } else if (mode == L"desync") {
            Check(Contains(log, "Desync meter: on"), "the desync meter can be turned on");
            // Installed by replacing the load, and the multiply right after it must still read the
            // game's own shared 0.05 - the meter measures, it does not change the smoothing.
            for (const auto& site : DesyncHooks())
                Check(HookedInto(base, site, plugin), "the sample point is hooked into the plugin");
            const unsigned char* site = base + kSmoothingSite;
            Check(site[0] == 0x0F && site[1] == 0x59 && site[2] == 0x35, "the multiply itself is untouched");
            std::int32_t disp = 0;
            std::memcpy(&disp, site + 3, sizeof(disp));
            Check(site + 7 + disp == base + 0x17A5A70, "and still reads the shared 0.05");
        } else if (mode == L"reliable") {
            Check(Contains(log, "Sync/TrafficMeter=0"), "the meter can be turned off");
            Check(Contains(log, "Sync/ReliableGameTraffic=1"), "and the upgrade can be turned on");
            Check(Contains(log, "EXPERIMENT"), "the log says plainly that it is not a settled fix");
        } else {
            Check(Contains(log, "Traffic meter: on"), "measuring is the default");
            Check(Contains(log, "Sync/ReliableGameTraffic=0"), "changing what is sent is not");
        }
        if (mode == L"smooth") {
            // The one instruction is retargeted; the shared 0.05 at 17A5A70 must be left alone.
            const unsigned char* site = base + kSmoothingSite;
            Check(site[0] == 0x0F && site[1] == 0x59 && site[2] == 0x35, "the multiply itself is unchanged");
            std::int32_t disp = 0;
            std::memcpy(&disp, site + 3, sizeof(disp));
            const unsigned char* constant = site + 7 + disp;
            Check(constant != base + 0x17A5A70, "it no longer reads the shared 0.05");
            Check(reinterpret_cast<std::uintptr_t>(constant) % 16 == 0, "its operand is 16-byte aligned for mulps");
            float splat[4]{};
            std::memcpy(splat, constant, sizeof(splat));
            Check(splat[0] > 0.29f && splat[0] < 0.31f && splat[1] == splat[0] && splat[2] == splat[0] &&
                      splat[3] == splat[0],
                  "and points at 0.30 four ways");
            const std::uint8_t stock[16] = {0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D,
                                            0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D};
            Check(std::memcmp(base + 0x17A5A70, stock, sizeof(stock)) == 0,
                  "the shared constant the other nine sites use is untouched");
            Check(Contains(log, "closes 30% of the gap"), "the log says what it changed");
        } else {
            const unsigned char* site = base + kSmoothingSite;
            std::int32_t disp = 0;
            std::memcpy(&disp, site + 3, sizeof(disp));
            Check(site + 7 + disp == base + 0x17A5A70, "without the setting the correction is left alone");
            Check(Contains(log, "own position smoothing is untouched"), "and the log says so");
        }
        // EDF.dll ends the game with TerminateProcess; if that import cannot be wrapped the shutdown
        // marker never fires and every start misreports the last one as cut, which is what 1.5.2-1.5.8 did.
        Check(Contains(log, "Exit marker: a normal quit now writes SHUTDOWN"),
              "the TerminateProcess import is wrapped, so a clean exit can be recorded");
        Check(Contains(log, "switched with F3/Tab/RS"), "member pages switch with F3, Tab and the right stick");
        if (mode == L"host8") {
            // An INI from 0.5.0: PageKey=F2 and dummy keys F3/F4.
            Check(Contains(log, "PageKey is no longer used"), "the old page key is reported as unused");
            Check(Contains(log, "Dummy keys may not be F2 or a page key: using add 117, remove 115"),
                  "a dummy key on a page key moves to its default (F6), the other stays (F4)");
        }
        if (mode == L"nomission") Check(Contains(log, "MaxPlayers is no longer used"), "an old MaxPlayers setting is reported as unused");
        else Check(!Contains(log, "MaxPlayers is no longer used"), "no MaxPlayers report without the key");

        // The menu layout file follows the plugin; another mod's file is left alone.
        if (mode == L"nomission") {
            Check(ReadBytes(layoutPath) == foreign, "another mod's menu layout is left alone");
            Check(Contains(log, "belongs to another mod"), "the foreign menu layout is logged");
        } else {
            Check(ReadBytes(layoutPath) == ours, "our menu layout is in Mods\\UI");
            Check(Contains(log, "Menu: wrote Mods\\UI\\LYT_MAINFRAME.SGO") == (mode != L"host8"),
                  "writing the layout is logged only when it was written");
        }

        for (std::size_t i = 0; i < before.values.size(); ++i) {
            const int v = before.values[i];
            const bool accepted = (static_cast<unsigned>(v) & ~0xFu) == 0x90u || IsMirror(v);
            Check(after.family[i] == accepted, "join check accepts exactly this family's mirror values and 0x90..0x9F");
            if ((static_cast<unsigned>(v) & ~0xFu) == 0x90u) Check(after.family[i] == before.family[i], "vanilla rooms stay joinable");
            if (IsOldMirror(v)) Check(!after.family[i] && after.decode[i] == 0, "rooms of earlier MultiSlot versions are refused");
            if (IsMirror(v)) {
                const int vanilla = 2 * static_cast<int>(kSearchTypeCenter) - v;
                Check(after.decode[i] == before.decode[static_cast<std::size_t>(vanilla + 0x20)], "mirror value decodes to its vanilla kind");
            } else if (!IsOldMirror(v)) {
                Check(after.decode[i] == before.decode[i], "decode is unchanged outside the mirror values");
            }
        }
        for (int kind = 0; kind < 8; ++kind) {
            const std::uint64_t old = before.range[kind];
            const auto low = static_cast<std::uint32_t>(old), high = static_cast<std::uint32_t>(old >> 32);
            if (low == 0x91 && high >= 0x91 && high <= 0x94) {
                const std::uint64_t bottom = 2 * kSearchTypeCenter - high;
                const std::uint64_t top = eightPlayers ? 2 * kSearchTypeCenter - 0x91 : high;
                Check(after.range[kind] == ((top << 32) | bottom),
                      eightPlayers ? "8Player MOD ON: the search lists MultiSlot rooms only"
                                   : "the search lists vanilla and MultiSlot rooms of the kind");
            } else {
                Check(after.range[kind] == old, "unknown search kinds are unchanged");
            }
            // The published value is chosen per room in the update hook, so the kind map stays the game's.
            Check(after.map[kind] == before.map[kind], "kind map unchanged");
        }

        if (mode == L"nomission") {
            Check(missionUntouched(), "Extend=0 leaves every mission site untouched");
            Check(Contains(log, "Mission: Extend=0"), "Extend=0 is logged");
        } else {
            Check(Applied(base, missionPatches) == static_cast<int>(missionPatches.size()), "every mission patch is written");
            for (const auto& hook : missionHooks) Check(HookedInto(base, hook, plugin), hook.name);
            for (const auto& call : missionCalls) Check(RedirectedInto(base + call.rva, plugin), call.name);
            for (const auto& slot : MissionSlots()) Check(SlotInto(base, slot, plugin), slot.name);
            Check(Contains(log, ("Mission: players 5-" + std::to_string(kMaxPlayers)).c_str()) && !Contains(log, "Mission: Extend=0"),
                  "mission support is logged (and only that)");
            const bool spawns = mode != L"host4";
            for (const auto& hook : SpawnHooks()) Check(spawns ? HookedInto(base, hook, plugin) : SiteUntouched(base, hook), hook.name);
            Check(Contains(log, spawns ? "enemy counts x1.2" : "ExtraEnemies=0, enemy counts unchanged"), "enemy count setting is logged");
        }
        if (mode == L"host8") {
            for (const auto& hook : GhostHooks()) Check(HookedInto(base, hook, plugin), hook.name);
            for (const auto& call : GhostCalls()) Check(RedirectedInto(base + call.rva, plugin), call.name);
            Check(Contains(log, "Test: GhostPlayers=4"), "ghost harness is logged");
            Check(Contains(log, ("Scale5..Scale" + std::to_string(kMaxPlayers) + " are no longer used").c_str()),
                  "old per-player-count scale keys are reported as unused");
        } else {
            for (const auto& hook : GhostHooks()) Check(SiteUntouched(base, hook), "ghost sites untouched without GhostPlayers");
            for (const auto& call : GhostCalls()) Check(CallTargets(base + call.rva, call.rva, call.target), "ghost call untouched without GhostPlayers");
        }
        if (netLog) {
            Check(Contains(log, "Net log: 15 EOS imports redirected"), "NetLog=1 installs all EOS import wrappers");
            for (const auto& hook : PacketSizeHooks()) Check(HookedInto(base, hook, plugin), "the packet size hook is installed with NetLog");
            Check(Contains(log, "Packet sizes:"), "and says what it logs");
            for (const auto& hook : DiagnosticHooks()) Check(HookedInto(base, hook, plugin), hook.name);
            for (const auto& call : DiagnosticCalls()) Check(RedirectedInto(base + call.rva, plugin), call.name);
        } else {
            Check(!Contains(log, "Net log:"), "NetLog=0 leaves the net log off");
            for (const auto& hook : PacketSizeHooks()) Check(SiteUntouched(base, hook), "no packet size hook without NetLog");
            for (const auto& hook : DiagnosticHooks()) Check(SiteUntouched(base, hook), "diagnostic sites untouched without NetLog");
            for (const auto& call : DiagnosticCalls()) Check(CallTargets(base + call.rva, call.rva, call.target), "diagnostic calls untouched without NetLog");
        }
        Check(Contains(log, "Crash log armed"), "crash log is armed");
        Check(Contains(log, "Patched EDF.dll: ") && !Contains(log, "PATCH EDF+"), "the patched sites are summed up in one line");
        if (mode == L"host8") {
            ThrowAndCatch();
            WriteToBadAddress();
            const std::string crash = ReadText(logPath);
            Check(Contains(crash, "EXCEPTION C0000005"), "access violation is logged");
            Check(Contains(crash, "writing 0000000000000010"), "fault address and access kind are logged");
            Check(Contains(crash, "#01 MultiSlotLoadTest.exe+"), "call stack is unwound into the caller");
            Check(!Contains(crash, "E06D7363"), "a C++ exception thrown and caught outside EDF.dll is not reported");
        }
    }

    if (failures) {
        std::printf("%d check(s) failed\n----- log -----\n%s\n", failures, ReadText(logPath).c_str());
        return 1;
    }
    std::printf("mode %ls verified\n", mode.c_str());
    return 0;
}
