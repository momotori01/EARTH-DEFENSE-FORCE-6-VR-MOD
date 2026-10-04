// Exercise the actual import wrappers against in-process fakes: no EOS service or game launch.
#include "../src/netlog.cpp"
#include <cstdio>
#include <thread>
#include <stdexcept>
#include <vector>

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

// Hand aim (handaim.h): rides on the game's own data sends and is taken out of its receives.
std::vector<SendPacketOptions> aimSends;
std::vector<std::vector<std::uint8_t>> aimData;
EOS_EResult aimResult = 0;
EOS_EResult FakeAimSend(void* handle, const SendPacketOptions* options) {
    Check(handle == expectedHandle, "hand aim keeps the EOS handle");
    aimSends.push_back(*options);
    const auto* bytes = static_cast<const std::uint8_t*>(options->Data);
    aimData.emplace_back(bytes, bytes + options->DataLengthBytes);
    return aimSends.size() == 1 ? aimResult : 0;
}
const char* AddressNamer(const void* id, char* out, std::size_t size) {
    std::snprintf(out, size, "peer-%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(id)));
    return out;
}
struct Queued {
    const void* peer;
    std::uint8_t channel;
    std::vector<std::uint8_t> bytes;
};
std::vector<Queued> inbox;
std::size_t inboxNext = 0;
EOS_EResult FakeQueueReceive(void*, const void*, void** peer, SocketId*, std::uint8_t* channel, void* data, std::uint32_t* size) {
    ++receiveCalls;
    if (inboxNext >= inbox.size()) return 13;  // EOS_NotFound: nothing waiting
    const auto& q = inbox[inboxNext++];
    *peer = const_cast<void*>(q.peer);
    *channel = q.channel;
    std::memcpy(data, q.bytes.data(), q.bytes.size());
    *size = static_cast<std::uint32_t>(q.bytes.size());
    return 0;
}
void HandAimTests() {
    ResetHandAimForTest();
    SetHandAimPeerNamer(&AddressNamer);
    originalSendPacket = &FakeAimSend;
    const SocketId socket{1, "game-socket"};
    std::uint8_t payload[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    SendPacketOptions game{};
    game.ApiVersion = 3;
    game.LocalUserId = Peer(0);
    game.RemoteUserId = Peer(5);
    game.Socket = &socket;
    game.Channel = 0;
    game.DataLengthBytes = sizeof(payload);
    game.Data = payload;
    game.Reliability = 0;
    game.DisableAutoAccept = 1;
    const SendPacketOptions before = game;
    const auto send = [&](std::uintptr_t caller) {
        aimSends.clear();
        aimData.clear();
        return DispatchSendPacket(expectedHandle, &game, caller);
    };
    send(0x12C8C5A);
    Check(aimSends.size() == 1, "nothing extra before EDF6VR gives a direction");
    const float dir[3] = {0.0f, 1.0f, 0.0f};
    SetLocalHandAim(0, dir, GetTickCount64());
    send(0x12C90F2);
    Check(aimSends.size() == 1, "never after a hello (12C8F50): that peer may not be connected yet");
    send(0x12C8C5A);
    Check(aimSends.size() == 2, "after the game's data to a peer, one hand aim packet to the same peer");
    if (aimSends.size() == 2) {
        const auto& ours = aimSends[1];
        Check(ours.ApiVersion == 3 && ours.LocalUserId == Peer(0) && ours.RemoteUserId == Peer(5) &&
                  ours.Socket == &socket && ours.DisableAutoAccept == 1,
              "on the game's own socket, from and to the same users");
        Check(ours.Channel == kHandAimChannel && ours.Reliability == 0 && ours.AllowDelayedDelivery == 0 &&
                  IsMultiSlotPacket(aimData[1].data(), static_cast<std::uint32_t>(aimData[1].size())) &&
                  aimData[1].size() == 9 + 7,
              "on its own channel, unreliable, carrying one slot");
        Check(aimSends[0].Data == payload && aimData[0].size() == sizeof(payload) && aimSends[0].Channel == 0,
              "the game's packet went first, untouched");
    }
    Check(std::memcmp(&game, &before, sizeof(game)) == 0, "the game's options are not written to");
    send(0x12C8C5A);
    Check(aimSends.size() == 1, "not again within kHandAimSendGapMs");
    game.RemoteUserId = Peer(6);
    send(0x12C8C5A);
    Check(aimSends.size() == 2, "each peer on its own clock");
    game.RemoteUserId = Peer(7);
    aimResult = 1;
    send(0x12C8C5A);
    Check(aimSends.size() == 1, "nothing when the game's own send failed (no connection)");
    aimResult = 0;
    SetHandAimEnabled(false);
    game.RemoteUserId = Peer(8);
    send(0x12C8C5A);
    Check(aimSends.size() == 1, "HandAim=0 sends nothing extra");
    SetHandAimEnabled(true);

    // Receive: ours are taken out and the next packet is handed to the game in their place.
    std::uint8_t built[kHandAimMaxPacket]{};
    const auto builtSize = BuildHandAimPacket(built, sizeof(built), GetTickCount64());
    const std::vector<std::uint8_t> aim(built, built + builtSize);
    const std::vector<std::uint8_t> gamePacket = {1, 0, 0, 0, 0x44, 0x33, 0x22, 0x11};
    originalReceivePacket = &FakeQueueReceive;
    inbox = {{Peer(3), kHandAimChannel, aim}, {Peer(3), kHandAimChannel, aim}, {Peer(4), 0, gamePacket}};
    inboxNext = 0;
    receiveCalls = 0;
    void* from = nullptr;
    SocketId fromSocket{};
    std::uint8_t channel = 0;
    static std::uint8_t buffer[0x1000]{};
    std::uint32_t size = 0;
    Check(HookReceivePacket(expectedHandle, nullptr, &from, &fromSocket, &channel, buffer, &size) == 0 &&
              receiveCalls == 3 && from == Peer(4) && channel == 0 && size == gamePacket.size() &&
              std::memcmp(buffer, gamePacket.data(), gamePacket.size()) == 0,
          "the game gets its own packet, after the two of ours");
    char text[40]{};
    AddressNamer(Peer(3), text, sizeof(text));
    float got[3]{};
    Check(RemoteHandAim(text, 0, got, GetTickCount64()) && got[1] > 0.99f, "and ours were kept for EDF6VR");
    inbox = {{Peer(3), kHandAimChannel, aim}};
    inboxNext = 0;
    receiveCalls = 0;
    Check(HookReceivePacket(expectedHandle, nullptr, &from, &fromSocket, &channel, buffer, &size) == 13 && receiveCalls == 2,
          "when only ours were waiting, the game hears that nothing is waiting");
    inbox.assign(100, Queued{Peer(3), kHandAimChannel, aim});
    inboxNext = 0;
    receiveCalls = 0;
    Check(HookReceivePacket(expectedHandle, nullptr, &from, &fromSocket, &channel, buffer, &size) == 0 && receiveCalls == 65,
          "a flood is cut off after 64 (the 65th reaches the game, which drops it unread)");
    originalReceivePacket = &FakeReceive;
    SetHandAimPeerNamer(nullptr);
    ResetHandAimForTest();
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
    HandAimTests();
    std::printf("network wrapper forwarding: %d failures\n", failures);
    return failures ? 1 : 0;
}
