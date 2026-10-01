#include "packetsize.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstdio>

#include "log.h"

namespace multislot {
namespace {

constexpr std::uint32_t kAppendRva = 0x12CFFD0;  // queue one message for a peer: rcx session, rdx data, r8 length
constexpr std::uint32_t kSyncFirst = 0x78D0E0;   // MissionSync_Res ...
constexpr std::uint32_t kSyncEnd = 0x7938CE;     // ... to the end of UserSync_Begin's member list builder
constexpr std::uint32_t kBigMessage = 1000;      // logged from anywhere at this size and above
constexpr std::uint32_t kScanFloor = 160;        // below this nothing is logged, so the stack is not read
constexpr std::size_t kStackSlots = 320;         // 2.5 KB of stack: far enough up to reach the sync function
constexpr std::size_t kFrames = 8;
constexpr int kMaxLines = 300;                   // a mission start is a handful; this is several evenings

unsigned char* gameBase = nullptr;
std::uint32_t imageSize = 0;
std::atomic<int> lines{0};
std::atomic<int> refusedLines{0};
std::atomic<std::uint32_t> largest{0};

bool Budget(std::atomic<int>& counter, int limit, const char* what) {
    const int used = counter.fetch_add(1, std::memory_order_relaxed);
    if (used < limit) return true;
    if (used == limit) Log("PACKET %s: line budget spent, no more of these this run", what);
    return false;
}

// Return addresses into EDF.dll, nearest first, found by walking up the game's stack from the hooked entry.
// Frames without unwind data make a real unwind impossible from a thunk, so each slot that points into the
// image just after a call instruction is taken for one; a stale value can slip in, which is harmless here.
std::size_t ScanStack(std::uint64_t rsp, std::uint32_t* out, std::size_t max) {
    std::size_t found = 0;
    __try {
        const auto* slot = reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(rsp));
        const auto base = reinterpret_cast<std::uintptr_t>(gameBase);
        for (std::size_t i = 0; i < kStackSlots && found < max; ++i) {
            const std::uint64_t value = slot[i];
            if (value < base + 0x1000 + 7 || value >= base + imageSize) continue;
            if (!FollowsCall(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(value)) - 7)) continue;
            out[found++] = static_cast<std::uint32_t>(value - base);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return found;
}

void Describe(char* out, std::size_t size, const std::uint32_t* frames, std::size_t count) {
    std::size_t used = 0;
    out[0] = 0;
    for (std::size_t i = 0; i < count && used < size; ++i) {
        const int written = _snprintf_s(out + used, size - used, _TRUNCATE, "%sEDF+%X", i ? " < " : "", frames[i]);
        if (written < 0) break;
        used += static_cast<std::size_t>(written);
    }
}

void Head(char* out, std::size_t size, std::uint64_t data, std::uint32_t bytes) {
    out[0] = 0;
    std::size_t used = 0;
    __try {
        const auto* p = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(data));
        for (std::uint32_t i = 0; i < bytes && i < 16 && used + 3 < size; ++i) {
            _snprintf_s(out + used, size - used, _TRUNCATE, "%s%02X", i ? " " : "", p[i]);
            used += i ? 3 : 2;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        _snprintf_s(out, size, _TRUNCATE, "(unreadable)");
    }
}

const char* FitText(MessageFit fit) {
    switch (fit) {
        case MessageFit::Shares: return "fits";
        case MessageFit::OwnPacket: return "needs a packet of its own, still fits";
        case MessageFit::Dropped:
            return "OVER EOS's limit: EOS refuses the packet and EDF6 drops it without a word";
    }
    return "?";
}

// EDF+12CFFD0 entry: `push rbx` ... Nothing is changed.
void AppendHandler(CpuContext* context) {
    const auto bytes = static_cast<std::uint32_t>(context->r8);
    if (bytes < kScanFloor) return;
    std::uint32_t seen = largest.load(std::memory_order_relaxed);
    while (bytes > seen && !largest.compare_exchange_weak(seen, bytes, std::memory_order_relaxed)) {
    }
    std::uint32_t frames[kFrames]{};
    const std::size_t count = ScanStack(SiteRsp(context), frames, kFrames);
    bool sync = false;
    for (std::size_t i = 0; i < count; ++i) sync = sync || InSyncFunctions(frames[i]);
    const MessageFit fit = ClassifyMessage(bytes);
    if (!sync && bytes < kBigMessage && fit != MessageFit::Dropped) return;
    if (!Budget(lines, kMaxLines, "sizes")) return;
    char chain[160], head[64];
    Describe(chain, sizeof(chain), frames, count);
    Head(head, sizeof(head), context->rdx, bytes);
    Log("PACKET %s message %u B, packet %u of EOS's %u: %s [largest this run %u B] from %s head %s",
        sync ? "sync" : "big", bytes, bytes + kGamePacketHeader, kEosMaxPacket, FitText(fit),
        largest.load(std::memory_order_relaxed), chain, head);
}

}  // namespace

MessageFit ClassifyMessage(std::uint32_t bytes) {
    if (bytes + kGamePacketHeader > kEosMaxPacket) return MessageFit::Dropped;
    if (bytes + kGamePacketHeader > kGamePacketLimit) return MessageFit::OwnPacket;
    return MessageFit::Shares;
}

bool FollowsCall(const std::uint8_t* before) {
    // before[7] is the return address.
    if (before[2] == 0xE8) return true;                                              // call rel32
    if (before[1] == 0xFF && (before[2] & 0xF8) == 0x90) return true;               // call [reg+disp32]
    if (before[1] == 0xFF && before[2] == 0x15) return true;                         // call [rip+disp32]
    if (before[0] == 0x41 && before[1] == 0xFF && (before[2] & 0xF8) == 0x90) return true;  // r8-r15 +disp32
    if (before[4] == 0xFF && (before[5] & 0xF8) == 0x50) return true;               // call [reg+disp8]
    if (before[3] == 0x41 && before[4] == 0xFF && (before[5] & 0xF8) == 0x50) return true;
    if (before[5] == 0xFF && (before[6] & 0xF8) == 0xD0) return true;               // call reg
    if (before[4] == 0x41 && before[5] == 0xFF && (before[6] & 0xF8) == 0xD0) return true;
    if (before[5] == 0xFF && (before[6] & 0xF8) == 0x10) return true;               // call [reg]
    return false;
}

bool InSyncFunctions(std::uint32_t rva) { return rva >= kSyncFirst && rva < kSyncEnd; }

std::vector<MidSite> PacketSizeHooks() {
    // `push rbx` (REX form), `push rbp`, `push rsi`, `push rdi`, `push r12`: seven position-independent bytes.
    return {{"message queued for a peer", kAppendRva, {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54}, 0, 7}};
}

MidHandler PacketSizeHookHandler(std::uint32_t rva) { return rva == kAppendRva ? &AppendHandler : nullptr; }

void InitPacketSize(unsigned char* base) {
    gameBase = base;
    __try {
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        imageSize = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew)->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        imageSize = 0;
    }
}

void NoteSendRefused(std::uint32_t bytes, std::uint8_t channel, std::int32_t reliability, int result,
                     std::uintptr_t callerRva) {
    if (!Budget(refusedLines, kMaxLines, "refused sends")) return;
    Log("PACKET EOS refused a send: %u B on channel %u, reliability %d, result %d%s, from EDF+%llX", bytes, channel,
        reliability, result, bytes > kEosMaxPacket ? " (over EOS's 1170-byte limit)" : "",
        static_cast<unsigned long long>(callerRva));
}

}  // namespace multislot
