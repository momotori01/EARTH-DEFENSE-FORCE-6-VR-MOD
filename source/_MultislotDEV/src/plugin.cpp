#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4201)
#include "PluginAPI.h"
#pragma warning(pop)

#include "code.h"
#include "armor.h"
#include "crashlog.h"
#include "default_ini.h"
#include "fakemembers.h"
#include "hostmode.h"
#include "joinlog.h"
#include "log.h"
#include "loaderproxy.h"
#include "menulayout.h"
#include "midhook.h"
#include "mission.h"
#include "netlog.h"
#include "patches.h"
#include "roomview.h"
#include "rooms.h"
#include "spawn.h"

namespace multislot {
namespace {

constexpr const char* kVersion = "1.5.7";
HMODULE self = nullptr;

// out: MAX_PATH characters. Refuses paths too long to also hold the rotated log name (log.cpp), instead of
// letting a *_s string function end the game over it.
bool SiblingPath(wchar_t* out, const wchar_t* extension) {
    const DWORD length = GetModuleFileNameW(self, out, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    wchar_t* dot = wcsrchr(out, L'.');
    if (!dot) return false;
    const std::size_t stem = static_cast<std::size_t>(dot - out), added = wcslen(extension);
    if (stem + added + 8 >= MAX_PATH) return false;
    wmemcpy(dot, extension, added + 1);
    return true;
}

// The package ships no INI, so an update never overwrites someone's settings; the first run
// writes the documented defaults for them to edit.
void WriteDefaultIni(const wchar_t* path) {
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;
    std::string text;
    for (const char* c = kDefaultIni; *c; ++c) {
        if (*c == '\n' && (text.empty() || text.back() != '\r')) text.push_back('\r');
        text.push_back(*c);
    }
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    Log("Wrote default settings to EDF6MultiSlot.ini");
}

std::wstring IniText(const wchar_t* ini, const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
    wchar_t value[64]{};
    GetPrivateProfileStringW(section, key, fallback, value, 64, ini);
    return value;
}

RoomViewSettings ReadRoomView(const wchar_t* ini) {
    RoomViewSettings settings;
    const auto button = IniText(ini, L"RoomScreen", L"PageButton", L"RightStick");
    const int offset = PadButtonOffset(button.c_str());
    if (offset < 0) Log("PageButton=%ls is not a known button; using RightStick", button.c_str());
    settings.padButton = static_cast<std::uint32_t>(offset < 0 ? 0xB8 : offset);
    const auto key = [&](const wchar_t* name, const wchar_t* fallback, int fallbackKey) {
        const auto text = IniText(ini, L"RoomScreen", name, fallback);
        const int vk = VirtualKey(text.c_str());
        if (vk < 0) Log("%ls=%ls is not a known key; using %ls", name, text.c_str(), fallback);
        return vk < 0 ? fallbackKey : vk;
    };
    // 0.6.0: F2 became the 8Player MOD key, so the page key moved (PageKey=F2 was the old default).
    if (IniText(ini, L"RoomScreen", L"PageKey", L"").size())
        Log("[RoomScreen] PageKey is no longer used: PageKeys (default F3,Tab) switches member pages");
    const auto pageText = IniText(ini, L"RoomScreen", L"PageKeys", L"F3,Tab");
    wchar_t skipped[64]{};
    int pageKeys[kMaxPageKeys]{};
    ParseKeyList(pageText.c_str(), pageKeys, kMaxPageKeys, skipped, 64);
    if (skipped[0]) Log("PageKeys: %ls left out (not a known key, or F2, which is the 8Player MOD key)", skipped);
    std::memcpy(settings.pageKeys, pageKeys, sizeof(pageKeys));
    settings.dummies = GetPrivateProfileIntW(L"RoomScreen", L"DummyMembers", 0, ini) != 0;
    settings.dummyAddKey = key(L"DummyAddKey", L"F6", VK_F6);
    settings.dummyRemoveKey = key(L"DummyRemoveKey", L"F7", VK_F7);
    if (ResolveDummyKeys(settings) && settings.dummies)
        Log("Dummy keys may not be F2 or a page key: using add %d, remove %d (virtual keys, 0 = none)",
            settings.dummyAddKey, settings.dummyRemoveKey);
    BuildPageHint(settings);
    return settings;
}

bool SupportedImage(const unsigned char* base) {
    __try {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return false;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
               nt->FileHeader.TimeDateStamp == kImageTimeDateStamp && nt->OptionalHeader.SizeOfImage == kImageSize;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct Redirect {
    CallSite site;
    void* handler;
};

struct Hook {
    MidSite site;
    MidHandler handler;
};

struct SlotWrite {
    PointerSlot slot;
    void* handler;
};

// All or nothing: a half-applied set could publish a 5-slot room that unmodded players can join,
// read a capacity from a call that was never redirected, or page a member list the builder never sees.
bool Apply(unsigned char* base, bool dummies, bool mission, bool spawns, int ghosts, bool diagnostics, bool armor, bool recovery,
           ThunkPage& thunks) {
    auto patches = GuestPatches();
    const auto sessionPatches = SessionPatches();
    patches.insert(patches.end(), sessionPatches.begin(), sessionPatches.end());
    std::vector<Hook> hooks;
    for (const auto& site : HostModeHooks()) hooks.push_back({site, HostModeHookHandler(site.rva)});
    if (armor)
        for (const auto& site : ArmorHooks()) hooks.push_back({site, ArmorHookHandler(site.rva)});
    if (diagnostics)
        for (const auto& site : DiagnosticHooks()) hooks.push_back({site, JoinLogHookHandler(site.rva)});
    if (mission) {
        const auto missionPatches = MissionPatches();
        patches.insert(patches.end(), missionPatches.begin(), missionPatches.end());
        for (const auto& site : MissionHooks()) hooks.push_back({site, MissionHookHandler(site.rva)});
        if (spawns)
            for (const auto& site : SpawnHooks()) hooks.push_back({site, SpawnHookHandler(site.rva)});
        if (ghosts > 0)
            for (const auto& site : GhostHooks()) hooks.push_back({site, GhostHookHandler(site.rva)});
    }
    std::vector<Redirect> redirects;
    const auto guest = GuestCalls();
    redirects.push_back({guest[0], reinterpret_cast<void*>(&RoomCountAndCapacity)});
    redirects.push_back({guest[1], reinterpret_cast<void*>(&RoomFullCount)});
    const auto room = RoomViewCalls();
    redirects.push_back({room[0], reinterpret_cast<void*>(&BuildPanelsHook)});
    redirects.push_back({room[1], reinterpret_cast<void*>(&BuildPanelsHook)});
    redirects.push_back({room[2], reinterpret_cast<void*>(&UpdateVoiceIconsHook)});
    if (dummies)
        for (const auto& call : FakeMemberCalls()) redirects.push_back({call, FakeMemberCallHandler(call.rva)});
    if (diagnostics)
        for (const auto& call : DiagnosticCalls()) redirects.push_back({call, JoinLogCallHandler(call.rva)});
    if (recovery)
        for (const auto& call : RecoveryCalls()) redirects.push_back({call, reinterpret_cast<void*>(&FinalHelloHook)});
    if (mission) {
        for (const auto& call : MissionCalls()) redirects.push_back({call, MissionCallHandler(call.rva)});
        if (ghosts > 0)
            for (const auto& call : GhostCalls()) redirects.push_back({call, GhostCallHandler(call.rva)});
    }
    std::vector<SlotWrite> slots{{RoomViewSlot(), reinterpret_cast<void*>(&RoomOnUpdateHook)},
                                 {MainFrameSlot(), reinterpret_cast<void*>(&MainFrameOnUpdateHook)},
                                 {LobbySlot(), reinterpret_cast<void*>(&LobbyOnUpdateHook)}};
    if (mission)
        for (const auto& missionSlot : MissionSlots()) slots.push_back({missionSlot, MissionSlotHandler(missionSlot.rva)});

    for (const auto& patch : patches) {
        if (patch.rva + patch.original.size() > kImageSize || !Matches(base + patch.rva, patch)) {
            Log("REFUSED: EDF+%X (%s) is not the expected code; another mod or a game update? Nothing was changed",
                patch.rva, patch.name);
            return false;
        }
    }
    for (const auto& hook : hooks) {
        const auto& site = hook.site;
        const Patch verify{site.name, site.rva, site.original, site.original};
        if (!hook.handler || site.original.size() < 5 || site.displacedOffset + site.displacedSize > site.original.size() ||
            site.rva + site.original.size() > kImageSize || !Matches(base + site.rva, verify)) {
            Log("REFUSED: EDF+%X (%s) is not the expected code; nothing was changed", site.rva, site.name);
            return false;
        }
    }
    for (const auto& redirect : redirects) {
        if (!redirect.handler || !CallTargets(base + redirect.site.rva, redirect.site.rva, redirect.site.target)) {
            Log("REFUSED: EDF+%X (%s) does not call EDF+%X; nothing was changed", redirect.site.rva, redirect.site.name,
                redirect.site.target);
            return false;
        }
    }
    for (const auto& write : slots) {
        const auto& slot = write.slot;
        if (!write.handler || !SlotTargets(base + slot.rva, reinterpret_cast<std::uint64_t>(base), slot.target)) {
            Log("REFUSED: EDF+%X (%s) does not point at EDF+%X; nothing was changed", slot.rva, slot.name, slot.target);
            return false;
        }
    }
    if (!thunks.Allocate(base)) {
        Log("REFUSED: no memory for call stubs near EDF.dll; nothing was changed");
        return false;
    }
    // Redirections are written first: the room info rewrite reads the value they return.
    std::vector<Patch> writes;
    for (const auto& redirect : redirects) {
        const unsigned char* stub = thunks.Add(redirect.handler);
        auto bytes = stub ? CallBytes(base + redirect.site.rva, stub) : std::vector<std::uint8_t>{};
        if (bytes.empty()) {
            Log("REFUSED: call stub for %s out of reach; nothing was changed", redirect.site.name);
            thunks.Release();
            return false;
        }
        writes.push_back({redirect.site.name, redirect.site.rva, {base + redirect.site.rva, base + redirect.site.rva + 5}, std::move(bytes)});
    }
    for (const auto& hook : hooks) {
        const auto& site = hook.site;
        const unsigned char* at = base + site.rva;
        const auto resume = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(at + site.original.size()));
        const unsigned char* thunk = EmitMidThunk(thunks, hook.handler, at + site.displacedOffset, site.displacedSize, resume);
        auto bytes = thunk ? JumpBytes(at, thunk, site.original.size()) : std::vector<std::uint8_t>{};
        if (bytes.empty()) {
            Log("REFUSED: hook thunk for %s out of reach; nothing was changed", site.name);
            thunks.Release();
            return false;
        }
        writes.push_back({site.name, site.rva, site.original, std::move(bytes)});
    }
    if (!thunks.Seal()) {
        Log("REFUSED: could not make call stubs executable (error %lu); nothing was changed", GetLastError());
        thunks.Release();
        return false;
    }
    writes.insert(writes.end(), patches.begin(), patches.end());
    for (const auto& write : slots) {
        const auto& slot = write.slot;
        std::vector<std::uint8_t> handler(sizeof(std::uint64_t));
        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(write.handler));
        std::memcpy(handler.data(), &address, sizeof(address));
        writes.push_back({slot.name, slot.rva, {base + slot.rva, base + slot.rva + sizeof(std::uint64_t)}, std::move(handler)});
    }

    for (std::size_t i = 0; i < writes.size(); ++i) {
        const auto& write = writes[i];
        if (!WriteCode(base + write.rva, write.replacement.data(), write.replacement.size())) {
            Log("ERROR: could not write EDF+%X (%s), error %lu; restoring %zu earlier writes", write.rva, write.name,
                GetLastError(), i);
            for (std::size_t j = i; j-- > 0;)
                WriteCode(base + writes[j].rva, writes[j].original.data(), writes[j].original.size());
            thunks.Release();
            return false;
        }
    }
    // One line instead of one per site: the sites are the same on every start, a refused or failed one is logged
    // by name above, and the stub address places a crash inside a stub or hook thunk.
    Log("Patched EDF.dll: %zu sites (%zu redirected calls, %zu hooks, %zu code patches, %zu vtable slots); stubs and hook "
        "thunks at %p (%zu bytes)", writes.size(), redirects.size(), hooks.size(), patches.size(), slots.size(),
        static_cast<const void*>(thunks.Base()), thunks.Used());
    return true;
}

// Mods\UI\LYT_MAINFRAME.SGO exists only while the plugin is active (menulayout.h): otherwise the game
// gets its own menu layout back.
void KeepMenuLayout(bool active) {
    wchar_t plugin[MAX_PATH]{};
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(self, plugin, MAX_PATH);
    if (!length || length >= MAX_PATH || !MenuLayoutPath(plugin, path, MAX_PATH)) {
        if (active) Log("Menu: the plugin is not in Mods\\Plugins, so the 8Player MOD label is not shown (F2 still works)");
        return;
    }
    if (!active) {
        const LayoutRemoval removal = RemoveMenuLayout(path);
        if (removal == LayoutRemoval::Removed)
            Log("Menu: removed Mods\\UI\\LYT_MAINFRAME.SGO; the game's own menu layout is used");
        else if (removal == LayoutRemoval::Failed)
            Log("Menu: could not remove Mods\\UI\\LYT_MAINFRAME.SGO (error %lu)", GetLastError());
        return;
    }
    switch (InstallMenuLayout(path)) {
        case LayoutInstall::Written:
            Log("Menu: wrote Mods\\UI\\LYT_MAINFRAME.SGO (the menu layout plus the 8Player MOD label)");
            break;
        case LayoutInstall::Updated: Log("Menu: updated Mods\\UI\\LYT_MAINFRAME.SGO"); break;
        case LayoutInstall::Current: break;
        case LayoutInstall::Foreign:
            Log("Menu: Mods\\UI\\LYT_MAINFRAME.SGO belongs to another mod and was left alone; the 8Player MOD label is "
                "not shown (F2 still works)");
            break;
        case LayoutInstall::Failed:
            Log("Menu: could not write Mods\\UI\\LYT_MAINFRAME.SGO (error %lu); the 8Player MOD label is not shown (F2 "
                "still works)", GetLastError());
            break;
    }
}

ThunkPage thunks;

}  // namespace
}  // namespace multislot

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    using namespace multislot;
    if (!info) return false;
    wchar_t iniPath[MAX_PATH]{};
    wchar_t logPath[MAX_PATH]{};
    if (!SiblingPath(iniPath, L".ini") || !SiblingPath(logPath, L".log")) return false;
    LogOpen(logPath);
    info->infoVersion = PluginInfo::MaxInfoVer;
    info->name = "EDF6 MultiSlot";
    info->version = PLUG_VER(1, 5, 5, 0);

    Log("==== EDF6MultiSlot %s ====", kVersion);
    const auto loader = GetModuleHandleW(L"winmm.dll");
    for (const auto name : {"timeBeginPeriod", "timeEndPeriod", "PlaySoundW"})
        Log("LOADER %s proxy=%s", name, LoaderProxyStyle(loader ? reinterpret_cast<const void*>(GetProcAddress(loader, name)) : nullptr));
    // Whether the game that wrote the lines above this one was closed or died. Without it a log that simply
    // stops says nothing, which is where two of the 2026-09-19 reports ran out of evidence.
    if (PreviousRun() == LastRun::Cut)
        Log("PREVIOUS RUN ended without a shutdown line: the game was killed, hung, or died without reaching "
            "the crash handler. Lines above this one are its last");
    WriteDefaultIni(iniPath);
    const bool enabled = GetPrivateProfileIntW(L"MultiSlot", L"Enabled", 1, iniPath) != 0;
    const bool eightPlayers = GetPrivateProfileIntW(L"MultiSlot", L"EightPlayerRooms", 0, iniPath) != 0;
    const bool crashLog = GetPrivateProfileIntW(L"MultiSlot", L"CrashLog", 1, iniPath) != 0;
    // A minidump of the first access violation inside EDF.dll, one file overwritten each launch. The
    // 2026-09-27 host crash is a null whose source is a stack local, which no log line can show.
    // Off unless the INI asks for it: a dump carries process memory, which can include the room name and
    // chat text, and that is not something to switch on for everyone who installs the package.
    const bool crashDump = GetPrivateProfileIntW(L"MultiSlot", L"CrashDump", 0, iniPath) != 0;
    // On by default since 1.2.2, so reports from real rooms come with the lobby and P2P lines (the log caps itself).
    const bool netLog = GetPrivateProfileIntW(L"MultiSlot", L"NetLog", 1, iniPath) != 0;
    const bool recovery = GetPrivateProfileIntW(L"MultiSlot", L"HandshakeRecovery", 1, iniPath) != 0;
    SetDetailLog(netLog);
    if (!enabled) {
        Log("Enabled=0: game left untouched, plugin unloaded");
        KeepMenuLayout(false);
        return false;
    }
    if (IniText(iniPath, L"MultiSlot", L"MaxPlayers", L"").size())
        Log("[MultiSlot] MaxPlayers is no longer used: the %dPlayer MOD setting is switched on a menu screen "
            "outside a room ([MultiSlot] Key and PadButton)", kModRoomCapacity);
    RoomViewSettings roomView = ReadRoomView(iniPath);
    const bool mission = GetPrivateProfileIntW(L"Mission", L"Extend", 1, iniPath) != 0;
    int ghosts = static_cast<int>(GetPrivateProfileIntW(L"Test", L"GhostPlayers", 0, iniPath));
    if (ghosts < 0 || ghosts > kMaxPlayers - 1) {
        Log("GhostPlayers=%d is outside 0..%d; using %d", ghosts, kMaxPlayers - 1, ghosts < 0 ? 0 : kMaxPlayers - 1);
        ghosts = ghosts < 0 ? 0 : kMaxPlayers - 1;
    }
    if (!mission) ghosts = 0;
    const bool spawns = mission && GetPrivateProfileIntW(L"Mission", L"ExtraEnemies", 1, iniPath) != 0;
    // 8Player MOD: switched outside a room only, because inside one the same inputs page through the members.
    const auto hostKeyText = IniText(iniPath, L"MultiSlot", L"Key", L"F2");
    int hostModeKey = VirtualKey(hostKeyText.c_str());
    if (hostModeKey < 0) {
        Log("[MultiSlot] Key=%ls is not a known key; using F2", hostKeyText.c_str());
        hostModeKey = VK_F2;
    }
    // The left stick is free outside a room (the page button is the right one, and copy armor only works
    // inside a room), so it can switch the setting there without taking anything away.
    const auto hostPadText = IniText(iniPath, L"MultiSlot", L"PadButton", L"LeftStick");
    int hostPadOffset = PadButtonOffset(hostPadText.c_str());
    if (hostPadOffset < 0) {
        Log("[MultiSlot] PadButton=%ls is not a known button; using LeftStick", hostPadText.c_str());
        hostPadOffset = 0xB0;
    }
    if (hostPadOffset && static_cast<std::uint32_t>(hostPadOffset) == roomView.padButton) {
        Log("[MultiSlot] PadButton may not be the page button; using none");
        hostPadOffset = 0;
    }
    const auto hostModePad = static_cast<std::uint32_t>(hostPadOffset);
    wchar_t hostModeHint[24]{};
    {
        wchar_t keyName[8]{};
        if (hostModeKey) KeyName(hostModeKey, keyName, 8);
        const wchar_t* padName = PadButtonShortName(hostModePad);
        _snwprintf_s(hostModeHint, _TRUNCATE, L"%ls%ls%ls", keyName, keyName[0] && padName[0] ? L"/" : L"", padName);
    }

    // "copy armor": with neither a key nor a pad button the three armor sites are left alone entirely.
    // It works inside a room, where the page inputs also work, so it may share neither of them.
    int copyArmorKey = 0;
    std::uint32_t copyArmorPad = 0;
    int copyArmorIgnore = 0;
    int copyArmorCaps[kSoldierTypeCount]{};
    wchar_t copyArmorHint[24]{};
    if (GetPrivateProfileIntW(L"CopyArmor", L"Enabled", 1, iniPath) != 0) {
        const auto text = IniText(iniPath, L"CopyArmor", L"Key", L"F4");
        const int vk = VirtualKey(text.c_str());
        if (vk < 0) Log("[CopyArmor] Key=%ls is not a known key; using F4", text.c_str());
        copyArmorKey = vk < 0 ? VK_F4 : vk;
        if (copyArmorKey == hostModeKey) {
            Log("[CopyArmor] Key may not be the %dPlayer MOD key; using F4", kModRoomCapacity);
            copyArmorKey = copyArmorKey == VK_F4 ? 0 : VK_F4;
        }
        for (const int page : roomView.pageKeys)
            if (page && page == copyArmorKey) {
                Log("[CopyArmor] Key may not be a page key; using F4");
                copyArmorKey = copyArmorKey == VK_F4 ? 0 : VK_F4;
            }
        const auto padText = IniText(iniPath, L"CopyArmor", L"PadButton", L"LeftStick");
        const int padOffset = PadButtonOffset(padText.c_str());
        if (padOffset < 0) Log("[CopyArmor] PadButton=%ls is not a known button; using LeftStick", padText.c_str());
        copyArmorPad = static_cast<std::uint32_t>(padOffset < 0 ? 0xB0 : padOffset);
        if (copyArmorPad && copyArmorPad == roomView.padButton) {
            Log("[CopyArmor] PadButton may not be the page button; using none");
            copyArmorPad = 0;
        }
        // Someone barely different from you is no help to copy, so they are passed over for the next one up.
        copyArmorIgnore = static_cast<int>(GetPrivateProfileIntW(L"CopyArmor", L"IgnoreWithin", 300, iniPath));
        if (copyArmorIgnore < 0) {
            Log("[CopyArmor] IgnoreWithin=%d is below zero; using 0", copyArmorIgnore);
            copyArmorIgnore = 0;
        }
        // The most this may give, per class: a rescue for someone behind, not a way to someone else's armor.
        const struct {
            const wchar_t* key;
            int fallback;
        } caps[kSoldierTypeCount] = {{L"MaxRanger", 4000}, {L"MaxWingDiver", 2500}, {L"MaxAirRaider", 3000},
                                     {L"MaxFencer", 4000}};
        for (int i = 0; i < kSoldierTypeCount; ++i) {
            copyArmorCaps[i] = static_cast<int>(GetPrivateProfileIntW(L"CopyArmor", caps[i].key, caps[i].fallback, iniPath));
            if (copyArmorCaps[i] >= 0) continue;
            Log("[CopyArmor] %ls=%d is below zero; using no ceiling", caps[i].key, copyArmorCaps[i]);
            copyArmorCaps[i] = 0;
        }
        if (copyArmorPad && copyArmorPad == roomView.padButton) {
            Log("[CopyArmor] PadButton may not be the page button; using none");
            copyArmorPad = 0;
        }
        wchar_t keyName[8]{};
        if (copyArmorKey) KeyName(copyArmorKey, keyName, 8);
        const wchar_t* padName = PadButtonShortName(copyArmorPad);
        _snwprintf_s(copyArmorHint, _TRUNCATE, L"%ls%ls%ls", keyName, keyName[0] && padName[0] ? L"/" : L"",
                     padName);
    }
    // 0.4.x let the INI multiply the 4-player factor; 0.5.0 follows fixed rules every player shares.
    for (int players = kVanillaPlayers + 1; players <= kMaxPlayers; ++players) {
        wchar_t key[16];
        _snwprintf_s(key, _TRUNCATE, L"Scale%d", players);
        if (IniText(iniPath, L"Mission", key, L"").size()) {
            Log("[Mission] Scale5..Scale%d are no longer used: 5+ players follow fixed rules (see README)", kMaxPlayers);
            break;
        }
    }

    const HMODULE game = GetModuleHandleW(L"EDF.dll");
    const auto base = reinterpret_cast<unsigned char*>(game);
    if (!game || !SupportedImage(base)) {
        Log("REFUSED: EDF.dll is not the supported build (TimeDateStamp %08X, SizeOfImage %X); nothing was changed",
            kImageTimeDateStamp, kImageSize);
        KeepMenuLayout(false);
        return false;
    }
    if (roomView.dummies && copyArmorKey) {
        const auto clash = [&](int& key, const char* which) {
            if (key != copyArmorKey) return;
            Log("[RoomScreen] Dummy%s key is the copy armor key; leaving it unbound", which);
            key = 0;
        };
        clash(roomView.dummyAddKey, "Add");
        clash(roomView.dummyRemoveKey, "Remove");
    }
    InitRooms(base);
    InitRoomView(base, roomView);
    InitFakeMembers(base);
    InitMission(base, ghosts);
    InitJoinLog(base);
    InitArmor(base, copyArmorKey, copyArmorPad, copyArmorHint, copyArmorIgnore, copyArmorCaps);
    InitHostMode(base, iniPath, eightPlayers, hostModeKey, hostModePad, hostModeHint);
    if (!Apply(base, roomView.dummies, mission, spawns, ghosts, netLog, copyArmorKey || copyArmorPad, recovery, thunks)) {
        KeepMenuLayout(false);
        return false;
    }
    KeepMenuLayout(true);
    Log("Joining: normal rooms and MultiSlot rooms are both listed and joinable");
    Log("Rooms: %d user slots, packet sessions and voice chat HUD records (P2P links to every member of a %d-player "
        "room; 4 or fewer: the extra ones stay empty)", kMaxPlayers, kMaxPlayers);
    Log("Hosting: %dPlayer MOD %s (%ls on a menu screen outside a room; inside one those page through the "
        "members instead): OFF = normal 4-player rooms anyone can join, ON = MultiSlot rooms for %d players, "
        "hidden from players without this mod",
        kModRoomCapacity, eightPlayers ? "ON" : "OFF", hostModeHint, kModRoomCapacity);
    if (copyArmorKey || copyArmorPad)
        Log("Copy armor: %ls in a room follows the lowest armor in it that is more than %d from yours (your own "
            "class if anyone else plays it, otherwise the room), up to %d/%d/%d/%d for Ranger/Wing Diver/Air "
            "Raider/Fencer; it never lowers, never reaches the room's own display and never saves",
            copyArmorHint, copyArmorIgnore, copyArmorCaps[0], copyArmorCaps[1], copyArmorCaps[2], copyArmorCaps[3]);
    else
        Log("Copy armor: off, the armor sites are untouched");
    Log("Room screen: 4 member panels per page, switched with %ls; fake members %s", roomView.pageHint[0] ? roomView.pageHint : L"(nothing)",
        roomView.dummies ? "ON (test mode: F6 adds one to the room's member list, F7 removes one)" : "off");
    if (mission) {
        Log("Mission: players 5-%d get loadout sidecars, player slots 5-%d and spawn points (4 or fewer: unchanged)",
            kMaxPlayers, kMaxPlayers);
        Log("Mission: 5+ players online - enemy durability, damage and speed stay at the 4-player values");
        if (spawns) {
            // The factor is (players + 1) / 5 (spawn.cpp), written out so the log says what every machine does.
            char factors[160]{};
            int used = 0;
            for (int players = kVanillaPlayers + 1; players <= kMaxPlayers && used >= 0; ++players) {
                const int written = _snprintf_s(factors + used, sizeof(factors) - used, _TRUNCATE, "%sx%d.%d (%d)",
                                                used ? " " : "", (players + 1) / 5, (players + 1) * 2 % 10, players);
                used = written < 0 ? -1 : used + written;
            }
            Log("Mission: 5+ players online - enemy counts %s, rounded; nests, anchors, ships and other fixed "
                "objects unchanged", factors);
        } else
            Log("Mission: ExtraEnemies=0, enemy counts unchanged (every player in a room needs the same setting)");
    } else
        Log("Mission: Extend=0, mission code untouched (only rooms of up to four players can start safely)");
    if (ghosts > 0)
        Log("Test: GhostPlayers=%d - a mission started alone online gets %d idle copies of you as extra players", ghosts, ghosts);
    if (netLog || recovery) {
        const int imports = InstallNetLog(game, netLog, recovery);
        if (netLog)
            Log("Net log: %d EOS imports redirected (NetLog=0 turns the detailed log off; the log file keeps its newest 2 MB)", imports);
        else
            Log("Recovery transport: %d EOS import redirected; detailed network logging off", imports);
    } else {
        Log("HandshakeRecovery=0: off");
    }
    if (crashLog) {
        wchar_t dumpPath[MAX_PATH]{};
        const bool wantDump = crashDump && SiblingPath(dumpPath, L"-crash.dmp");
        InstallCrashLog(game, wantDump ? dumpPath : nullptr);
        if (CrashDumpArmed())
            Log("Crash log armed; CrashDump=1, so the first access violation in EDF.dll also writes %ls "
                "(one per launch, overwritten each time). It holds process memory, so only send it on "
                "purpose", dumpPath);
        else if (crashDump)
            Log("Crash log armed; CrashDump=1 but no dump can be written (DbgHelp.dll or the path was "
                "not usable)");
        else
            Log("Crash log armed; no crash dump (CrashDump is off by default because a dump holds "
                "process memory; set CrashDump=1 in the INI to help chase a crash)");
    }
    return true;  // stays loaded: call stubs, import wrappers and the exception handler point here
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        multislot::self = instance;
        DisableThreadLibraryCalls(instance);
    }
    // The last thing the log gets from a game that was closed properly. LogShutdown is raw Win32 with no
    // heap and no CRT, which is what makes it safe this late in a process that is already tearing down.
    if (reason == DLL_PROCESS_DETACH) multislot::LogShutdown("the game exited");
    return TRUE;
}
