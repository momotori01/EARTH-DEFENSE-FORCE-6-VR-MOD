// Exercise the actual import wrappers against in-process fakes: no EOS service or game launch.
#include "../src/netlog.cpp"
#include <cstdio>
#include <thread>
#include <stdexcept>

using namespace multislot;
namespace {
int failures = 0, sendCalls = 0, receiveCalls = 0, closeCalls = 0, leaveCalls = 0;
const void* expectedOptions = nullptr;
void* expectedHandle = reinterpret_cast<void*>(0x1234);
void Check(bool ok, const char* label) {
    if (!ok) { std::printf("FAIL: %s\n", label); ++failures; }
}
EOS_EResult FakeSend(void* handle, const SendPacketOptions* options) {
    ++sendCalls;
    Check(handle == expectedHandle && options == expectedOptions, "send forwards original arguments");
    return 7;
}
EOS_EResult FakeReceive(void* handle, const void* options, void** peer, SocketId* socket,
                       std::uint8_t* channel, void* data, std::uint32_t* size) {
    ++receiveCalls;
    Check(handle == expectedHandle && options == expectedOptions, "receive forwards original arguments");
    *peer = nullptr;
    *socket = SocketId{1, "test-socket"};
    *channel = 3;
    const std::uint32_t payload[] = {0, 0x12345678};
    std::memcpy(data, payload, sizeof(payload));
    *size = sizeof(payload);
    return 0;
}
EOS_EResult FakeClose(void* handle, const CloseConnectionOptions* options) {
    ++closeCalls;
    Check(handle == expectedHandle && options == expectedOptions, "close forwards original arguments");
    return 9;
}
void Completion(const JoinLobbyCallbackInfo*) {}
void FakeLeave(void* handle, const void* options, void* clientData, JoinLobbyCallback completion) {
    ++leaveCalls;
    Check(handle == expectedHandle && options == expectedOptions && clientData == expectedHandle && completion == &Completion,
          "leave forwards options, client data and callback unchanged");
}

alignas(8) unsigned char manager[0x150]{}, users[8][0x50]{};
alignas(8) unsigned char session[0x38]{}, room[0x170]{};
std::uintptr_t slots[16]{}, slotVector[3]{};
std::uint32_t helloPayload[2]{0, 0x12345678};
int helloCalls = 0, recoverySends = 0, firstResult = 1, secondResult = 0;
bool accepted = false, invalidateAfterFirst = false;
std::uintptr_t helloCaller = 0x12C90F2;
std::uint8_t helloChannel = 0;
std::int32_t helloApi = 3, helloReliability = 1;
SendPacketOptions recorded[2]{};
const SendPacketOptions* gameOptions = nullptr;
const char token[] = "original-token";
template <typename T> void Put(unsigned char* memory, std::size_t offset, T value) {
    std::memcpy(memory + offset, &value, sizeof(value));
}
const void* Peer(int index) { return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x8000 + index)); }
void ResetRecovery(int count = 8) {
    std::memset(manager, 0, sizeof(manager));
    std::memset(users, 0, sizeof(users));
    std::memset(slots, 0, sizeof(slots));
    slotVector[0] = reinterpret_cast<std::uintptr_t>(slots);
    slotVector[1] = slotVector[2] = slotVector[0] + sizeof(slots);
    for (int i = 0; i < count; ++i) {
        slots[i * 2] = reinterpret_cast<std::uintptr_t>(users[i]);
        Put(users[i], 0x10, std::uint32_t(i ? 3 : 1));
        Put(users[i], 0x18, Peer(i));
        Put(users[i], 0x40, std::int32_t(i));
    }
    Put(manager, 0x18, expectedHandle);
    const SocketId socket{1, "test-session"};
    Put(manager, 0x20, socket);
    Put(manager, 0x138, Peer(0));
    Put(manager, 0x140, static_cast<const void*>(slotVector));
    Put(reinterpret_cast<unsigned char*>(gameAddress), 0x20B2AC0, static_cast<const void*>(session));
    Put(session, 0x28, static_cast<const void*>(room));
    Put(room, 0, gameAddress + 0x17ECA00);  // Actual RoomImpl, verified against EDF constructors in tests.cpp.
    Put(room, 0x160, static_cast<const void*>(slotVector));
    handshakeRecovery = true;
    packetDiagnostics = false;
    helloCalls = recoverySends = 0;
    firstResult = 1;
    secondResult = 0;
    accepted = invalidateAfterFirst = false;
    helloPayload[0] = 0;
    helloCaller = 0x12C90F2;
    helloChannel = 0;
    helloApi = 3;
    helloReliability = 1;
}
EOS_EResult FakeRecoverySend(void* handle, const SendPacketOptions* options) {
    Check(handle == expectedHandle, "recovery preserves the EOS handle");
    Check(recoverySends < 2, "recovery never loops");
    if (recoverySends >= 2) return 1;
    recorded[recoverySends] = *options;
    if (!recoverySends) {
        Check(options == gameOptions && options->AllowDelayedDelivery == 0, "first send is unchanged");
    } else {
        Check(accepted, "vanilla AcceptConnection ran before retry");
        Check(options != gameOptions && gameOptions->AllowDelayedDelivery == 0,
              "retry copies options without mutating game memory");
        Check(options->AllowDelayedDelivery == 1 && options->DisableAutoAccept == 1 && options->Reliability == 1,
              "only delayed delivery changes");
        Check(options->Data == helloPayload && options->DataLengthBytes == sizeof(helloPayload) &&
              options->RemoteUserId == Peer(1) && options->LocalUserId == Peer(0) &&
              options->Socket == reinterpret_cast<const SocketId*>(manager + 0x20) && options->Channel == 0,
              "retry preserves payload, peer, local user, socket and channel");
    }
    ++recoverySends;
    return recoverySends == 1 ? firstResult : secondResult;
}
void FakeFinalHello(void* context, const void* peer, const char* suppliedToken) {
    ++helloCalls;
    Check(context == manager && suppliedToken == token, "sender preserves manager and authentication token");
    SendPacketOptions options{};
    options.ApiVersion = helloApi;
    options.LocalUserId = Peer(0);
    options.RemoteUserId = peer;
    options.Socket = reinterpret_cast<const SocketId*>(manager + 0x20);
    options.Channel = helloChannel;
    options.DataLengthBytes = sizeof(helloPayload);
    options.Data = helloPayload;
    options.Reliability = helloReliability;
    options.DisableAutoAccept = 1;
    gameOptions = &options;
    const auto result = DispatchSendPacket(expectedHandle, &options, helloCaller);
    Check(result == (helloCalls == 1 ? firstResult : secondResult), "original EOS result is never fabricated");
    if (result == 1) accepted = true;  // Mirrors the original EDF sender's branch after SendPacket returns.
    if (invalidateAfterFirst) Put(users[1], 0x10, std::uint32_t(2));
}
void RunRecovery(int expectedCalls, const char* label) {
    originalSendPacket = &FakeRecoverySend;
    originalFinalHello = &FakeFinalHello;
    FinalHelloHook(manager, Peer(1), token);
    Check(helloCalls == expectedCalls && recoverySends == expectedCalls && !activeHello, label);
}
void ThrowingHello(void*, const void*, const char*) { throw std::runtime_error("test unwinding"); }
void RecoveryTests() {
    auto* mappedGame = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x22CE000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(mappedGame != nullptr, "fake image allocated");
    if (!mappedGame) return;
    gameAddress = reinterpret_cast<std::uintptr_t>(mappedGame);
    ResetRecovery(); RunRecovery(2, "NoConnection: one queued retry");
    ResetRecovery(); secondResult = 1; RunRecovery(2, "retry also fails: no third attempt");
    ResetRecovery(); firstResult = 0; RunRecovery(1, "successful 8-player handshake stays unchanged");
    ResetRecovery(); firstResult = 7; RunRecovery(1, "other EOS errors are not retried");
    ResetRecovery(4); RunRecovery(1, "four-player room stays unchanged");
    ResetRecovery(1); RunRecovery(1, "single player stays unchanged");
    ResetRecovery(5); RunRecovery(2, "five occupied slots qualify");
    ResetRecovery(5); slots[14] = slots[8]; slots[8] = 0; RunRecovery(2, "sparse slots count occupancy");
    ResetRecovery(4); slots[14] = slots[6]; slots[6] = 0; RunRecovery(1, "high slot index alone does not qualify");
    ResetRecovery(); handshakeRecovery = false; RunRecovery(1, "feature disabled preserves original behavior");
    ResetRecovery(); Put(users[1], 0x10, std::uint32_t(2)); RunRecovery(1, "unvalidated peer is never promoted or retried");
    ResetRecovery(); Put(users[0], 0x10, std::uint32_t(0)); RunRecovery(1, "local user must be validated");
    ResetRecovery(); slots[2] = 0; RunRecovery(1, "absent peer is not retried");
    ResetRecovery(); invalidateAfterFirst = true; RunRecovery(1, "peer validation is rechecked after original send");
    ResetRecovery(); slotVector[1] = slotVector[0] + 17; RunRecovery(1, "malformed vector fails closed");
    ResetRecovery(); helloPayload[0] = 1; RunRecovery(1, "non-hello payload is untouched");
    ResetRecovery(); helloChannel = 1; RunRecovery(1, "other channels are untouched");
    ResetRecovery(); helloCaller = 0x123456; RunRecovery(1, "other send callers are untouched");
    ResetRecovery(); helloApi = 2; RunRecovery(1, "unexpected API version is untouched");
    ResetRecovery(); helloReliability = 0; RunRecovery(1, "unexpected reliability is untouched");
    ResetRecovery(); Put(session, 0x28, static_cast<const void*>(nullptr)); RunRecovery(1, "room teardown is untouched");
    ResetRecovery(); Put(room, 0x160, static_cast<const void*>(nullptr)); RunRecovery(1, "old manager after room replacement is untouched");
    ResetRecovery(); Put(room, 0, std::uintptr_t(0)); RunRecovery(1, "unexpected room implementation is untouched");
    ResetRecovery(); Put(room, 0, gameAddress + 0x17EC968); RunRecovery(1, "base room during teardown is not treated as active");
    ResetRecovery();
    Check(!EligibleFinalHello(nullptr, Peer(1)) && !EligibleFinalHello(manager, nullptr) &&
          !EligibleFinalHello(manager, Peer(0)), "null and self peers rejected");
    {
        HelloAttempt outer{manager, Peer(1)};
        HelloScope scope(&outer);
        bool isolated = false;
        std::thread thread([&] { isolated = activeHello == nullptr; });
        thread.join();
        Check(isolated, "another thread cannot inherit recovery scope");
        originalFinalHello = &ThrowingHello;
        try { FinalHelloHook(manager, Peer(1), token); } catch (const std::runtime_error&) {}
        Check(activeHello == &outer, "nested exception restores outer thread scope");
    }
    Check(!activeHello, "recovery retains no scope after returning");
    VirtualFree(mappedGame, 0, MEM_RELEASE);
    gameAddress = 0;
}
}
int main() {
    originalSendPacket = &FakeSend;
    originalReceivePacket = &FakeReceive;
    originalCloseConnection = &FakeClose;
    originalLeaveLobby = &FakeLeave;
    SendPacketOptions send{};
    expectedOptions = &send;
    Check(HookSendPacket(expectedHandle, &send) == 7 && sendCalls == 1, "send called once and result preserved");
    void* peer = reinterpret_cast<void*>(0x1234);
    SocketId socket{};
    std::uint8_t channel = 0;
    std::uint32_t payload[2]{1, 2}, size = 0;
    Check(HookReceivePacket(expectedHandle, expectedOptions, &peer, &socket, &channel, payload, &size) == 0 &&
          receiveCalls == 1, "receive called once and result preserved");
    Check(!peer && channel == 3 && size == 8 && payload[0] == 0 && payload[1] == 0x12345678 &&
          std::strcmp(socket.Name, "test-socket") == 0, "receive output is byte-for-byte preserved");
    CloseConnectionOptions close{};
    expectedOptions = &close;
    Check(HookCloseConnection(expectedHandle, &close) == 9 && closeCalls == 1, "close called once and result preserved");
    HookLeaveLobby(expectedHandle, expectedOptions, expectedHandle, &Completion);
    Check(leaveCalls == 1, "leave called once");
    Check(IsHello(payload, 8) && !IsHello(payload, 3) && !IsHello(nullptr, 8), "hello probe checks minimum length");
    RecoveryTests();
    std::printf("network wrapper forwarding: %d failures\n", failures);
    return failures ? 1 : 0;
}
