#include "netlog.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <intrin.h>

#include "log.h"
#include "identity.h"
#include "joinlog.h"
#include "traffic.h"
#include "packetsize.h"

namespace multislot {
namespace {

// EOS SDK C types, only the fields read here. Layouts match what EDF.dll builds (see NOTES.md).
using EOS_EResult = std::int32_t;
struct CreateLobbyOptions {
    std::int32_t ApiVersion;
    void* LocalUserId;
    std::uint32_t MaxLobbyMembers;
    std::int32_t PermissionLevel;
    std::int32_t bPresenceEnabled;
    std::int32_t bAllowInvites;
    const char* BucketId;
    std::int32_t bDisableHostMigration;
    std::int32_t bEnableRTCRoom;
};
struct SetMaxMembersOptions {
    std::int32_t ApiVersion;
    std::uint32_t MaxMembers;
};
struct AttributeData {
    std::int32_t ApiVersion;
    const char* Key;
    union {
        std::int64_t AsInt64;
        double AsDouble;
        std::int32_t AsBool;
        const char* AsUtf8;
    } Value;
    std::int32_t ValueType;  // 0 bool, 1 int64, 2 double, 3 string
};
struct AddAttributeOptions {
    std::int32_t ApiVersion;
    const AttributeData* Attribute;
    std::int32_t Visibility;
};
struct SetParameterOptions {
    std::int32_t ApiVersion;
    const AttributeData* Parameter;
    std::int32_t ComparisonOp;
};
struct LogMessage {
    const char* Category;
    const char* Message;
    std::int32_t Level;
};
using LogCallback = void(*)(const LogMessage*);

using CreateLobbyFn = void(*)(void*, const CreateLobbyOptions*, void*, void*);
using SetMaxMembersFn = EOS_EResult(*)(void*, const SetMaxMembersOptions*);
using AddAttributeFn = EOS_EResult(*)(void*, const AddAttributeOptions*);
using SetParameterFn = EOS_EResult(*)(void*, const SetParameterOptions*);
using CountFn = std::uint32_t(*)(void*, const void*);
using SetLogCallbackFn = EOS_EResult(*)(LogCallback);
using SetLogLevelFn = EOS_EResult(*)(std::int32_t, std::int32_t);
using AcceptConnectionFn = EOS_EResult(*)(void*, const void*);
struct SocketId {
    std::int32_t ApiVersion;
    char Name[33];
};
struct CloseConnectionOptions {
    std::int32_t ApiVersion;
    const void* LocalUserId;
    const void* RemoteUserId;
    const SocketId* Socket;
};
struct SendPacketOptions {
    std::int32_t ApiVersion;
    const void* LocalUserId;
    const void* RemoteUserId;
    const SocketId* Socket;
    std::uint8_t Channel;
    std::uint32_t DataLengthBytes;
    const void* Data;
    std::int32_t AllowDelayedDelivery;
    std::int32_t Reliability;
    std::int32_t DisableAutoAccept;
};
static_assert(offsetof(SendPacketOptions, Data) == 0x28);
static_assert(offsetof(SendPacketOptions, AllowDelayedDelivery) == 0x30);
static_assert(offsetof(SendPacketOptions, Reliability) == 0x34);
using SendPacketFn = EOS_EResult(*)(void*, const SendPacketOptions*);
using ReceivePacketFn = EOS_EResult(*)(void*, const void*, void**, SocketId*, std::uint8_t*, void*, std::uint32_t*);
using CloseConnectionFn = EOS_EResult(*)(void*, const CloseConnectionOptions*);
SendPacketFn originalSendPacket = nullptr;
ReceivePacketFn originalReceivePacket = nullptr;
CloseConnectionFn originalCloseConnection = nullptr;
std::uintptr_t gameAddress = 0;
std::atomic<bool> reliableGameTraffic{false};
std::atomic<unsigned> handshakeLines{0};
bool packetDiagnostics = true;
bool handshakeRecovery = false;
using FinalHelloFn = void(*)(void*, const void*, const char*);
FinalHelloFn originalFinalHello = nullptr;

// Only lives on the game's final-hello call stack. No packet, token, user or room is retained.
struct HelloAttempt {
    void* manager;
    const void* peer;
    bool retry = false;
    unsigned sends = 0;
    EOS_EResult result = -1;
};
thread_local HelloAttempt* activeHello = nullptr;
struct HelloScope {
    HelloAttempt* previous;
    explicit HelloScope(HelloAttempt* attempt) : previous(activeHello) { activeHello = attempt; }
    ~HelloScope() { activeHello = previous; }
};

bool EligibleFinalHello(void* manager, const void* peer) {
    if (!manager || !peer) return false;
    __try {
        const auto* bytes = static_cast<const unsigned char*>(manager);
        const auto local = *reinterpret_cast<const void* const*>(bytes + 0x138);
        if (!local || local == peer) return false;
        const auto users = *reinterpret_cast<const void* const*>(bytes + 0x140);
        // Do not queue a reply from an old manager after leaving/replacing the active room.
        std::uintptr_t roomUsers = 0;
        const char* failure = nullptr;
        if (!ReadCurrentRoomUsers(gameAddress, roomUsers, failure) ||
            reinterpret_cast<const void*>(roomUsers) != users) return false;
        UserSlotsSnapshot snapshot{};
        if (!ReadUserSlots(users, snapshot) || snapshot.occupied <= 4) return false;
        bool localReady = false, peerReady = false;
        for (const auto& slot : snapshot.slots) {
            const auto id = reinterpret_cast<const void*>(slot.productId);
            if (slot.object && id == local && (slot.flags & 1)) localReady = true;
            // The peer must already have passed the game's validation on this machine.
            if (slot.object && id == peer && (slot.flags & 3) == 3) peerReady = true;
        }
        return localReady && peerReady;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MatchFinalHello(void* handle, const SendPacketOptions* options, std::uintptr_t caller) {
    if (!activeHello || caller != 0x12C90F2 || !options) return false;
    __try {
        const auto* manager = static_cast<const unsigned char*>(activeHello->manager);
        std::uint32_t type = ~0u;
        if (!options->Data || options->DataLengthBytes < sizeof(type)) return false;
        std::memcpy(&type, options->Data, sizeof(type));
        return type == 0 && options->ApiVersion == 3 && options->Channel == 0 &&
            options->Reliability == 1 && options->AllowDelayedDelivery == 0 && options->DisableAutoAccept == 1 &&
            options->RemoteUserId == activeHello->peer &&
            options->LocalUserId == *reinterpret_cast<const void* const*>(manager + 0x138) &&
            options->Socket == reinterpret_cast<const SocketId*>(manager + 0x20) &&
            handle == *reinterpret_cast<void* const*>(manager + 0x18);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool HandshakeBudget() {
    const auto line = handshakeLines.fetch_add(1);
    if (line == 6000) Log("HANDSHAKE packet/close diagnostic budget exhausted; further packet lines suppressed");
    return line < 6000;
}

std::uintptr_t GameRva(const void* address) {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= gameAddress && value - gameAddress < 0x22CE000 ? value - gameAddress : 0;
}

bool IsHello(const void* data, std::uint32_t length) {
    __try {
        std::uint32_t type = ~0u;
        if (!data || length < sizeof(type)) return false;
        std::memcpy(&type, data, sizeof(type));
        return type == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Keep the import wrapper itself small so its return address is always the real game caller.
EOS_EResult DispatchSendPacket(void* handle, const SendPacketOptions* options, std::uintptr_t caller) {
    const bool finalHello = MatchFinalHello(handle, options, caller);
    SendPacketOptions queued{};
    const auto* effective = options;
    if (finalHello && activeHello->retry && activeHello->sends == 0) {
        queued = *options;
        queued.AllowDelayedDelivery = 1;
        effective = &queued;
    }
    // Reading the options and, when asked, handing EOS a copy with the reliability raised. One guarded
    // block for both, because both touch memory the game owns; the summary line is written outside it.
    std::uint8_t meterChannel = 0;
    std::uint32_t meterBytes = 0;
    const void* meterPeer = nullptr;
    __try {
        if (options) {
            if (TrafficMeterOn()) {
                meterChannel = options->Channel;
                meterBytes = options->DataLengthBytes;
                meterPeer = options->RemoteUserId;
            }
            if (reliableGameTraffic.load(std::memory_order_relaxed) && effective == options &&
                options->ApiVersion >= 3 && options->Reliability == 0) {
                queued = *options;
                queued.Reliability = 1;  // EOS_PR_ReliableUnordered: delivered once, order not promised
                effective = &queued;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        meterBytes = 0;
    }
    if (meterBytes) RecordSent(meterPeer, meterChannel, meterBytes, GetTickCount64());
    const auto result = originalSendPacket(handle, effective);
    // A send EOS refused is a message lost for good: 12C8BC0 only reacts to EOS_NoConnection (1). The one
    // expected to show up is a packet over EOS's 1170 bytes (packetsize.h).
    std::uint32_t sentBytes = 0;
    std::uint8_t sentChannel = 0;
    std::int32_t sentReliability = 0;
    bool sentRead = false;
    __try {
        if (effective) {
            sentBytes = effective->DataLengthBytes;
            sentChannel = effective->Channel;
            sentReliability = effective->Reliability;
            sentRead = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (sentRead && (sentBytes > kEosMaxPacket || (result != 0 && result != 1)))
        NoteSendRefused(sentBytes, sentChannel, sentReliability, static_cast<int>(result), caller);
    if (finalHello) {
        ++activeHello->sends;
        activeHello->result = result;
    }
    __try {
        if (packetDiagnostics && caller == 0x12C90F2 && effective && HandshakeBudget()) {
            char local[40]{}, remote[40]{};
            ProductUserIdText(effective->LocalUserId, local, sizeof(local));
            ProductUserIdText(effective->RemoteUserId, remote, sizeof(remote));
            Log("HANDSHAKE SEND: %s -> %s bytes=%u channel=%u delayed=%d reliability=%d result=%d thread=%lu",
                local, remote, effective->DataLengthBytes, effective->Channel, effective->AllowDelayedDelivery,
                effective->Reliability, result, GetCurrentThreadId());
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return result;
}

EOS_EResult HookSendPacket(void* handle, const SendPacketOptions* options) {
    return DispatchSendPacket(handle, options, GameRva(_ReturnAddress()));
}

EOS_EResult HookReceivePacket(void* handle, const void* options, void** peer, SocketId* socket,
                             std::uint8_t* channel, void* data, std::uint32_t* size) {
    const auto caller = GameRva(_ReturnAddress());
    const auto result = originalReceivePacket(handle, options, peer, socket, channel, data, size);
    if (result == 0 && size && *size && TrafficMeterOn())
        RecordReceived(peer ? *peer : nullptr, channel ? *channel : 0, *size, GetTickCount64());
    __try {
        if (result == 0 && size && IsHello(data, *size) && HandshakeBudget()) {
            char remote[40]{};
            ProductUserIdText(peer ? *peer : nullptr, remote, sizeof(remote));
            Log("HANDSHAKE RECEIVE type=0: EOS %s bytes=%u channel=%u socket=%.33s caller=EDF+%llX thread=%lu",
                remote, *size, channel ? *channel : 255, socket ? socket->Name : "?",
                static_cast<unsigned long long>(caller), GetCurrentThreadId());
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return result;
}

EOS_EResult HookCloseConnection(void* handle, const CloseConnectionOptions* options) {
    const auto caller = GameRva(_ReturnAddress());
    __try {
        if (options && HandshakeBudget()) {
            char local[40]{}, remote[40]{};
            ProductUserIdText(options->LocalUserId, local, sizeof(local));
            ProductUserIdText(options->RemoteUserId, remote, sizeof(remote));
            void* stack[6]{};
            const auto count = CaptureStackBackTrace(0, 6, stack, nullptr);
            std::uintptr_t frames[6]{};
            for (USHORT i = 0; i < count; ++i) frames[i] = GameRva(stack[i]);
            Log("HANDSHAKE CLOSE: %s -> %s reason=%s caller=EDF+%llX thread=%lu stackEDF=%llX,%llX,%llX,%llX,%llX,%llX",
                local, remote, caller == 0x12C7C2C ? "initial-retry" : caller == 0x12C95A2 ? "Users-disconnect-notify" : "other",
                static_cast<unsigned long long>(caller), GetCurrentThreadId(),
                static_cast<unsigned long long>(frames[0]), static_cast<unsigned long long>(frames[1]),
                static_cast<unsigned long long>(frames[2]), static_cast<unsigned long long>(frames[3]),
                static_cast<unsigned long long>(frames[4]), static_cast<unsigned long long>(frames[5]));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return originalCloseConnection(handle, options);
}
struct SetLobbyIdOptions {
    std::int32_t ApiVersion;
    const char* LobbyId;
};
using SetLobbyIdFn = EOS_EResult(*)(void*, const SetLobbyIdOptions*);
struct FindCallbackInfo {
    EOS_EResult ResultCode;
    void* ClientData;
};
using FindCallback = void(*)(const FindCallbackInfo*);
using FindFn = void(*)(void*, const void*, void*, FindCallback);
struct JoinLobbyCallbackInfo {
    EOS_EResult ResultCode;
    void* ClientData;
    const char* LobbyId;
};
using JoinLobbyCallback = void(*)(const JoinLobbyCallbackInfo*);
using JoinLobbyFn = void(*)(void*, const void*, void*, JoinLobbyCallback);
using LeaveLobbyFn = void(*)(void*, const void*, void*, JoinLobbyCallback);
LeaveLobbyFn originalLeaveLobby = nullptr;

void HookLeaveLobby(void* handle, const void* options, void* clientData, JoinLobbyCallback completion) {
    const auto caller = GameRva(_ReturnAddress());
    void* stack[6]{};
    const auto count = CaptureStackBackTrace(0, 6, stack, nullptr);
    std::uintptr_t frames[6]{};
    for (USHORT i = 0; i < count; ++i) frames[i] = GameRva(stack[i]);
    Log("HANDSHAKE LEAVE LOBBY requested: caller=EDF+%llX thread=%lu stackEDF=%llX,%llX,%llX,%llX,%llX,%llX",
        static_cast<unsigned long long>(caller), GetCurrentThreadId(),
        static_cast<unsigned long long>(frames[0]), static_cast<unsigned long long>(frames[1]),
        static_cast<unsigned long long>(frames[2]), static_cast<unsigned long long>(frames[3]),
        static_cast<unsigned long long>(frames[4]), static_cast<unsigned long long>(frames[5]));
    originalLeaveLobby(handle, options, clientData, completion);
}

CreateLobbyFn originalCreateLobby = nullptr;
JoinLobbyFn originalJoinLobby = nullptr;
SetMaxMembersFn originalSetMaxMembers = nullptr;
AddAttributeFn originalAddAttribute = nullptr;
SetParameterFn originalSetParameter = nullptr;
CountFn originalSearchResultCount = nullptr;
CountFn originalMemberCount = nullptr;
SetLogCallbackFn originalSetLogCallback = nullptr;
AcceptConnectionFn originalAcceptConnection = nullptr;
SetLobbyIdFn originalSetLobbyId = nullptr;
FindFn originalFind = nullptr;
LogCallback gameLogCallback = nullptr;

std::atomic<std::uint32_t> lastMemberCount{0xFFFFFFFF};

// EOS strings come from the game or the SDK; keep log lines bounded and printable.
const char* Printable(const char* text, char* buffer, std::size_t size) {
    __try {
        if (!text) return "(null)";
        std::size_t i = 0;
        for (; i + 1 < size && text[i]; ++i) buffer[i] = (text[i] >= 0x20 && text[i] < 0x7F) ? text[i] : '?';
        buffer[i] = 0;
        return buffer;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "(unreadable)";
    }
}

void LogAttribute(const char* what, const AttributeData* data, std::int32_t extra) {
    char key[64], text[96];
    if (!data) {
        Log("EOS %s (null attribute) op/vis=%d", what, extra);
        return;
    }
    const char* name = Printable(data->Key, key, sizeof(key));
    switch (data->ValueType) {
    case 0: Log("EOS %s %s = bool %d  op/vis=%d", what, name, data->Value.AsBool, extra); break;
    case 1: Log("EOS %s %s = %lld (0x%llX)  op/vis=%d", what, name, data->Value.AsInt64, data->Value.AsInt64, extra); break;
    case 2: Log("EOS %s %s = %f  op/vis=%d", what, name, data->Value.AsDouble, extra); break;
    case 3: Log("EOS %s %s = \"%s\"  op/vis=%d", what, name, Printable(data->Value.AsUtf8, text, sizeof(text)), extra); break;
    default: Log("EOS %s %s = type %d  op/vis=%d", what, name, data->ValueType, extra); break;
    }
}

void HookCreateLobby(void* handle, const CreateLobbyOptions* options, void* clientData, void* completion) {
    char bucket[64];
    if (options)
        Log("EOS CreateLobby api=%d MaxLobbyMembers=%u permission=%d bucket=\"%s\" hostMigrationDisabled=%d rtc=%d",
            options->ApiVersion, options->MaxLobbyMembers, options->PermissionLevel,
            Printable(options->BucketId, bucket, sizeof(bucket)), options->bDisableHostMigration, options->bEnableRTCRoom);
    originalCreateLobby(handle, options, clientData, completion);
}

// Completion delegates are wrapped to log their result: EOS calls each exactly once.
struct JoinContext {
    void* clientData;
    JoinLobbyCallback callback;
};

void OnJoinLobby(const JoinLobbyCallbackInfo* info) {
    const auto* context = static_cast<JoinContext*>(info->ClientData);
    JoinLobbyCallbackInfo forwarded = *info;
    forwarded.ClientData = context->clientData;
    Log("EOS JoinLobby result %d", info->ResultCode);
    if (context->callback) context->callback(&forwarded);
    delete context;
}

void HookJoinLobby(void* handle, const void* options, void* clientData, JoinLobbyCallback completion) {
    Log("EOS JoinLobby requested");
    originalJoinLobby(handle, options, new JoinContext{clientData, completion}, &OnJoinLobby);
}

EOS_EResult HookSetLobbyId(void* handle, const SetLobbyIdOptions* options) {
    const EOS_EResult result = originalSetLobbyId(handle, options);
    char id[64];
    Log("EOS LobbySearch SetLobbyId %s -> result %d", options ? Printable(options->LobbyId, id, sizeof(id)) : "(null)", result);
    return result;
}

struct FindContext {
    void* clientData;
    FindCallback callback;
};

void OnFind(const FindCallbackInfo* info) {
    const auto* context = static_cast<FindContext*>(info->ClientData);
    FindCallbackInfo forwarded = *info;
    forwarded.ClientData = context->clientData;
    Log("EOS LobbySearch Find result %d", info->ResultCode);
    if (context->callback) context->callback(&forwarded);
    delete context;
}

void HookFind(void* handle, const void* options, void* clientData, FindCallback completion) {
    originalFind(handle, options, new FindContext{clientData, completion}, &OnFind);
}

EOS_EResult HookSetMaxMembers(void* handle, const SetMaxMembersOptions* options) {
    const EOS_EResult result = originalSetMaxMembers(handle, options);
    Log("EOS LobbyModification SetMaxMembers %u -> result %d", options ? options->MaxMembers : 0, result);
    return result;
}

EOS_EResult HookAddAttribute(void* handle, const AddAttributeOptions* options) {
    const EOS_EResult result = originalAddAttribute(handle, options);
    LogAttribute("LobbyModification AddAttribute", options ? options->Attribute : nullptr, options ? options->Visibility : -1);
    return result;
}

EOS_EResult HookSetParameter(void* handle, const SetParameterOptions* options) {
    const EOS_EResult result = originalSetParameter(handle, options);
    LogAttribute("LobbySearch SetParameter", options ? options->Parameter : nullptr, options ? options->ComparisonOp : -1);
    return result;
}

std::uint32_t HookSearchResultCount(void* handle, const void* options) {
    const std::uint32_t count = originalSearchResultCount(handle, options);
    Log("EOS LobbySearch results %u", count);
    return count;
}

std::uint32_t HookMemberCount(void* handle, const void* options) {
    const std::uint32_t count = originalMemberCount(handle, options);
    // Polled every frame by the room UI: log changes only.
    if (lastMemberCount.exchange(count) != count) Log("EOS LobbyDetails member count %u", count);
    return count;
}

EOS_EResult HookAcceptConnection(void* handle, const void* options) {
    const EOS_EResult result = originalAcceptConnection(handle, options);
    Log("EOS P2P AcceptConnection -> result %d", result);
    return result;
}

bool Wanted(const char* category) {
    return std::strstr(category, "Lobby") || std::strstr(category, "P2P") || std::strstr(category, "RTC") ||
           std::strstr(category, "Connect") || std::strstr(category, "Presence") || std::strstr(category, "CustomInvites");
}

void OnEosLog(const LogMessage* message) {
    __try {
        // No per-session line limit: the log file drops its oldest lines past 2 MB (log.h), so the lines
        // leading up to a late crash are kept.
        if (message && message->Category && message->Message && (message->Level <= 300 || Wanted(message->Category)))
            Log("EOSSDK %d %.40s: %.600s", message->Level, message->Category, message->Message);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (gameLogCallback) gameLogCallback(message);
}

EOS_EResult HookSetLogCallback(LogCallback callback) {
    gameLogCallback = callback;
    const EOS_EResult result = originalSetLogCallback(&OnEosLog);
    // The game never raises the SDK log level; Info shows lobby membership and P2P connections.
    const HMODULE sdk = GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll");
    const auto setLevel = sdk ? reinterpret_cast<SetLogLevelFn>(GetProcAddress(sdk, "EOS_Logging_SetLogLevel")) : nullptr;
    const EOS_EResult level = setLevel ? setLevel(0x7FFFFFFF, 400) : -1;
    Log("EOS logging routed to plugin log (SetCallback result %d, SetLogLevel(all, Info) result %d)", result, level);
    return result;
}

using TerminateProcessFn = BOOL(WINAPI*)(HANDLE, UINT);
TerminateProcessFn originalTerminate = nullptr;

BOOL WINAPI TerminateProcessHook(HANDLE process, UINT code) {
    // Only this process ending counts as the game shutting down.
    if (process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId())
        LogShutdown("the game exited");
    return originalTerminate ? originalTerminate(process, code) : FALSE;
}

bool RedirectImport(HMODULE module, const char* dll, const char* function, void* replacement, void** original) {
    const auto base = reinterpret_cast<unsigned char*>(module);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress) return false;
    for (auto entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); entry->Name; ++entry) {
        if (_stricmp(reinterpret_cast<const char*>(base + entry->Name), dll) != 0) continue;
        auto names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (entry->OriginalFirstThunk ? entry->OriginalFirstThunk : entry->FirstThunk));
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(byName->Name), function) != 0) continue;
            DWORD previous = 0;
            if (!VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), PAGE_READWRITE, &previous)) return false;
            *original = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = reinterpret_cast<ULONGLONG>(replacement);
            DWORD ignored = 0;
            VirtualProtect(&slots->u1.Function, sizeof(slots->u1.Function), previous, &ignored);
            return true;
        }
    }
    return false;
}

}  // namespace

bool InstallExitMarker(HMODULE game) {
    if (originalTerminate) return true;  // already wrapped
    void* previous = nullptr;
    const bool ok = RedirectImport(game, "KERNEL32.dll", "TerminateProcess",
                                   reinterpret_cast<void*>(&TerminateProcessHook), &previous) ||
                    RedirectImport(game, "kernel32.dll", "TerminateProcess",
                                   reinterpret_cast<void*>(&TerminateProcessHook), &previous);
    if (!ok) return false;
    originalTerminate = reinterpret_cast<TerminateProcessFn>(previous);
    // The game must always be able to exit. If the slot held something unusable - null, or our own hook
    // because something redirected it already - fall back to the real one rather than return FALSE from
    // the wrapper and leave the process unable to end.
    if (!originalTerminate || originalTerminate == &TerminateProcessHook) {
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        originalTerminate = kernel ? reinterpret_cast<TerminateProcessFn>(
                                         reinterpret_cast<void*>(GetProcAddress(kernel, "TerminateProcess")))
                                   : nullptr;
    }
    if (!originalTerminate) {  // could not find a way through: put the slot back and stay out of it
        void* restore = nullptr;
        RedirectImport(game, "KERNEL32.dll", "TerminateProcess", reinterpret_cast<void*>(previous), &restore);
        return false;
    }
    return true;
}

void SetReliableGameTraffic(bool on) { reliableGameTraffic.store(on, std::memory_order_relaxed); }

void FinalHelloHook(void* manager, const void* peer, const char* token) {
    HelloAttempt attempt{manager, peer};
    const bool eligible = handshakeRecovery && !activeHello && EligibleFinalHello(manager, peer);
    HelloScope scope(eligible ? &attempt : nullptr);
    originalFinalHello(manager, peer, token);
    // The unmodified sender has now run its own AcceptConnection after EOS_NoConnection (1).
    // Retry once only after that; do not fabricate success or bypass validation/teardown/timeouts.
    if (eligible && attempt.sends == 1 && attempt.result == 1 && EligibleFinalHello(manager, peer)) {
        attempt.retry = true;
        attempt.sends = 0;
        attempt.result = -1;
        originalFinalHello(manager, peer, token);
        char remote[40]{};
        ProductUserIdText(peer, remote, sizeof(remote));
        Log("HANDSHAKE RECOVERY: final hello EOS %s retry after NoConnection, delayed=1 sends=%u result=%d (queued is not confirmed)",
            remote, attempt.sends, attempt.result);
    }
}

// Only ever called from the traffic meter's own once-a-window report, never per packet.
void NamePeer(const void* peer, char* out, std::size_t size) { ProductUserIdText(peer, out, size); }

int InstallNetLog(HMODULE game, bool diagnostics, bool recovery) {
    SetPeerNameResolver(&NamePeer);
    gameAddress = reinterpret_cast<std::uintptr_t>(game);
    packetDiagnostics = diagnostics;
    handshakeRecovery = false;
    originalFinalHello = reinterpret_cast<FinalHelloFn>(gameAddress + 0x12C8F50);
    constexpr const char* sdk = "EOSSDK-Win64-Shipping.dll";
    struct Entry {
        const char* name;
        void* replacement;
        void** original;
    };
    const Entry entries[] = {
        {"EOS_Lobby_CreateLobby", reinterpret_cast<void*>(&HookCreateLobby), reinterpret_cast<void**>(&originalCreateLobby)},
        {"EOS_Lobby_JoinLobby", reinterpret_cast<void*>(&HookJoinLobby), reinterpret_cast<void**>(&originalJoinLobby)},
        {"EOS_Lobby_LeaveLobby", reinterpret_cast<void*>(&HookLeaveLobby), reinterpret_cast<void**>(&originalLeaveLobby)},
        {"EOS_LobbyModification_SetMaxMembers", reinterpret_cast<void*>(&HookSetMaxMembers), reinterpret_cast<void**>(&originalSetMaxMembers)},
        {"EOS_LobbyModification_AddAttribute", reinterpret_cast<void*>(&HookAddAttribute), reinterpret_cast<void**>(&originalAddAttribute)},
        {"EOS_LobbySearch_SetParameter", reinterpret_cast<void*>(&HookSetParameter), reinterpret_cast<void**>(&originalSetParameter)},
        {"EOS_LobbySearch_GetSearchResultCount", reinterpret_cast<void*>(&HookSearchResultCount), reinterpret_cast<void**>(&originalSearchResultCount)},
        {"EOS_LobbyDetails_GetMemberCount", reinterpret_cast<void*>(&HookMemberCount), reinterpret_cast<void**>(&originalMemberCount)},
        {"EOS_P2P_AcceptConnection", reinterpret_cast<void*>(&HookAcceptConnection), reinterpret_cast<void**>(&originalAcceptConnection)},
        {"EOS_P2P_CloseConnection", reinterpret_cast<void*>(&HookCloseConnection), reinterpret_cast<void**>(&originalCloseConnection)},
        {"EOS_P2P_SendPacket", reinterpret_cast<void*>(&HookSendPacket), reinterpret_cast<void**>(&originalSendPacket)},
        {"EOS_P2P_ReceivePacket", reinterpret_cast<void*>(&HookReceivePacket), reinterpret_cast<void**>(&originalReceivePacket)},
        {"EOS_Logging_SetCallback", reinterpret_cast<void*>(&HookSetLogCallback), reinterpret_cast<void**>(&originalSetLogCallback)},
        {"EOS_LobbySearch_SetLobbyId", reinterpret_cast<void*>(&HookSetLobbyId), reinterpret_cast<void**>(&originalSetLobbyId)},
        {"EOS_LobbySearch_Find", reinterpret_cast<void*>(&HookFind), reinterpret_cast<void**>(&originalFind)},
    };
    int redirected = 0;
    for (const auto& entry : entries) {
        const bool send = std::strcmp(entry.name, "EOS_P2P_SendPacket") == 0;
        const bool receive = std::strcmp(entry.name, "EOS_P2P_ReceivePacket") == 0;
        // The meter counts packets as they pass through these two wrappers, and the reliability upgrade
        // rewrites the send, so either one needs its import redirected even with the detailed log off.
        const bool metered = TrafficMeterOn() && (send || receive);
        const bool upgraded = reliableGameTraffic.load(std::memory_order_relaxed) && send;
        if (!diagnostics && !metered && !upgraded && !(recovery && send)) continue;
        if (RedirectImport(game, sdk, entry.name, entry.replacement, entry.original)) {
            ++redirected;
            if (send) handshakeRecovery = recovery;
        } else {
            Log("NETLOG: import %s not found; not logged", entry.name);
        }
    }
    Log("HandshakeRecovery=%d: %s", handshakeRecovery ? 1 : 0,
        handshakeRecovery ? "experimental final-hello NoConnection retry for 5+ occupied users" : "off");
    return redirected;
}

}  // namespace multislot
