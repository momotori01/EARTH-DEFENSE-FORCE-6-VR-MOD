#include "joinlog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <atomic>
#include <cstring>

#include "identity.h"
#include "log.h"
#include "patches.h"

namespace multislot {
namespace {

unsigned char* game = nullptr;

constexpr std::uint32_t kMissionContentOwned = 0x0D92B0;  // (GameStatus*, mission) -> bool
constexpr std::uint32_t kLobbyJoinStart = 0x8E7060;       // HUiLobby join action
constexpr std::uint32_t kSearchTypeCompatible = 0x749AC0; // SEARCH_TYPE -> bool

// Per session. Each join logs a handful of lines plus one per member slot, so this covers a long evening.
constexpr int kLineLimit = 5000;
std::atomic<int> lines{0};
bool Budget() { return lines.fetch_add(1) < kLineLimit; }
UserSlotsSnapshot lastSlots{};
std::uintptr_t lastUsers = 0;
std::size_t lastRosterCount = ~std::size_t{0};
ULONGLONG lastPoll = 0, lastSlotReport = 0;
const char* lastSlotFailure = nullptr;

template <typename T>
bool Read(std::uint64_t address, T& value) {
    __try {
        value = *reinterpret_cast<const T*>(static_cast<std::uintptr_t>(address));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// HUiLobby join button (8EC476, `cmp byte [rcx+0x300], 0`): rcx = GameStatus, r14 = selected RoomInfo.
// The game refuses HARDEST/INFERNO rooms (difficulty > 2) unless the final mission is cleared (+0x300).
void JoinPressedHandler(CpuContext* context) {
    std::int32_t mission = -1, difficulty = -1, members = -1, capacity = -1, kind = -1;
    std::uint8_t cleared = 0xFF;
    Read(context->r14 + 0x08, mission);
    Read(context->r14 + 0x18, difficulty);
    Read(context->r14 + 0x78, members);
    Read(context->r14 + 0x7C, capacity);
    Read(context->r14 + 0x168, kind);
    Read(context->rcx + 0x300, cleared);
    if (Budget())
        Log("LOBBY join pressed: mission %d difficulty %d members %d/%d kind %d, final mission cleared %d%s", mission,
            difficulty, members, capacity, kind, cleared,
            difficulty > 2 && cleared == 0 ? " -> the game refuses (Lobby_Join_Impossible)" : "");
}

using ContentOwnedFn = bool(__fastcall*)(void*, std::int32_t);
bool __fastcall ContentOwnedHook(void* status, std::int32_t mission) {
    const bool owned = reinterpret_cast<ContentOwnedFn>(game + kMissionContentOwned)(status, mission);
    if (Budget())
        Log("LOBBY join: mission %d content owned %d%s", mission, owned ? 1 : 0,
            owned ? "" : " -> the game asks first (Lobby_WarnWeaponEquip)");
    return owned;
}

using JoinStartFn = void*(__fastcall*)(void*);
void* __fastcall JoinStartHook(void* request) {
    if (Budget()) Log("LOBBY join: request started");
    return reinterpret_cast<JoinStartFn>(game + kLobbyJoinStart)(request);
}

// OnRequest_Join completion (8EFEC0): rdx = eos::RoomCreate::CompleteResult, code at +4.
void JoinFinishedHandler(CpuContext* context) {
    std::int32_t code = -1;
    Read(context->rdx + 4, code);
    if (Budget())
        Log("LOBBY join finished: code %d (%s)", code,
            code == 0 ? "joined" : code == 3 ? "room full, OnlineError_RoomFull" : "could not join, OnlineError_RoomError");
}

// Room list page / refresh completions (8F01D0 / 8F02E0): rdx = bool ok, r8 = EOS_EResult.
template <int Which>
void RoomListHandler(CpuContext* context) {
    std::uint8_t ok = 0xFF;
    std::int32_t result = -1;
    Read(context->rdx, ok);
    Read(context->r8, result);
    if (Budget()) Log("LOBBY room list %s: ok %d, EOS result %d", Which ? "refresh" : "next page", ok, result);
}

// Steam LobbyEnter_t callback (743490): rdx = {uint64 lobby, uint32 permissions, bool locked,
// uint32 EChatRoomEnterResponse at +0x10}. The game joins the host's Steam lobby before the EOS lobby;
// anything but 1 ends the join as "could not join" without reaching EOS.
const char* EnterResponseName(std::uint32_t response) {
    switch (response) {
        case 1: return "Success";
        case 2: return "DoesntExist";
        case 3: return "NotAllowed";
        case 4: return "Full";
        case 5: return "Error";
        case 6: return "Banned";
        case 7: return "Limited";
        case 8: return "ClanDisabled";
        case 9: return "CommunityBan";
        case 10: return "MemberBlockedYou";
        case 11: return "YouBlockedMember";
        case 15: return "RatelimitExceeded";
        default: return "unknown";
    }
}

void SteamLobbyEnterHandler(CpuContext* context) {
    std::uint32_t response = 0xFFFFFFFF;
    std::uint8_t locked = 0xFF;
    Read(context->rdx + 0x10, response);
    Read(context->rdx + 0x0C, locked);
    if (Budget())
        Log("STEAM lobby enter: response %u (%s), locked %u", response, EnterResponseName(response), locked);
}

// Users::Add (12B7F50), after its search for an empty user slot: ecx = slot, r8 = number of slots,
// rbx = {EOS_ProductUserId, bool remote}. Every member of a room (you included) needs a slot for a P2P link.
// The id is logged in full: the EOS SDK's own lines abbreviate it to "[000...044]", and that abbreviation is
// all another machine's log has to identify the same person by.
void UserSlotTakenHandler(CpuContext* context) {
    std::uint8_t remote = 0xFF;
    Read(context->rbx + 8, remote);
    const void* user = nullptr;
    Read(context->rbx, user);
    char id[40]{};
    ProductUserIdText(user, id, sizeof(id));
    if (Budget())
        Log("ROOM user slot %d of %llu taken by %s%s%s", static_cast<std::int32_t>(context->rcx),
            static_cast<unsigned long long>(context->r8), remote == 0 ? "you" : remote == 1 ? "a member" : "?",
            id[0] ? ", EOS " : "", id);
}

const char* MemberStatusName(std::uint32_t status) {
    switch (status) {
    case 0: return "JOINED";
    case 1: return "LEFT";
    case 2: return "DISCONNECTED";
    case 3: return "KICKED";
    case 4: return "PROMOTED";
    case 5: return "CLOSED";
    default: return "unknown";
    }
}

// EOS lobby member status (12BEF00): rdx = the callback info, +0x10 the member, +0x18 the code.
// LEFT, DISCONNECTED and KICKED all end up removing the user and closing their connection, so a code of
// 2 here is the thing to look for when someone will not finish their handshake.
void MemberStatusHandler(CpuContext* context) {
    const void* user = nullptr;
    std::uint32_t status = 0xFFFFFFFF;
    Read(context->rdx + 0x10, user);
    Read(context->rdx + 0x18, status);
    char id[40]{};
    ProductUserIdText(user, id, sizeof(id));
    if (Budget())
        Log("LOBBY member status: %s -> %u (%s)%s", id[0] ? id : "?", status, MemberStatusName(status),
            status == 2 ? "  <- treated the same as LEFT: the user is removed and their connection closed"
                        : "");
}

void UserSlotsFullHandler(CpuContext* context) {
    if (Budget())
        Log("ROOM all %llu user slots are taken: the new member gets no P2P link", static_cast<unsigned long long>(context->r8));
}

void NoteHandshakeUser(const char* event, const void* user) {
    const auto address = reinterpret_cast<std::uintptr_t>(user);
    const void* id = nullptr;
    std::uint32_t flags = 0;
    std::int32_t slot = -1;
    Read(address + 0x18, id);
    Read(address + 0x10, flags);
    Read(address + 0x40, slot);
    char text[40]{};
    ProductUserIdText(id, text, sizeof(text));
    Log("HANDSHAKE %s: EOS %s slot=%d flags=0x%X ready=%u thread=%lu", event,
        text[0] ? text : "?", slot, flags, flags & 1, GetCurrentThreadId());
}

// Link::OnInitial reached its deadline. Observe only; displaced instruction sets Link+0xA8.
void HandshakeTimeoutHandler(CpuContext* context) {
    // OnInitial already owns a strong reference in rbx on this branch.
    const void* user = reinterpret_cast<const void*>(context->rbx);
    float deadline = 0;
    Read(context->rdi + 0xA0, deadline);
    if (Budget()) {
        NoteHandshakeUser("TIMEOUT", user);
        Log("HANDSHAKE deadline=%.0f ms link=%p", deadline, reinterpret_cast<void*>(context->rdi));
    }
}

using ConfirmUserFn = void(__fastcall*)(void*, void*, void*);
void __fastcall ConfirmUserHook(void* manager, void* sharedUser, void* profile) {
    const void* user = nullptr;
    Read(reinterpret_cast<std::uintptr_t>(sharedUser), user);
    if (Budget()) NoteHandshakeUser("VALIDATED, promoting user", user);
    reinterpret_cast<ConfirmUserFn>(game + 0x12C7C40)(manager, sharedUser, profile);
}

using RejectUserFn = void(__fastcall*)(void*, void*);
void __fastcall RejectUserHook(void* manager, void* sharedUser) {
    const void* user = nullptr;
    Read(reinterpret_cast<std::uintptr_t>(sharedUser), user);
    if (Budget()) NoteHandshakeUser("VALIDATION FAILED", user);
    reinterpret_cast<RejectUserFn>(game + 0x12C8820)(manager, sharedUser);
}

using CompatibleFn = bool(__fastcall*)(std::int64_t);
// Invitation::Search (709A30): the SEARCH_TYPE check for invites and session joins.
bool __fastcall SearchTypeCompatibleHook(std::int64_t value) {
    const bool compatible = reinterpret_cast<CompatibleFn>(game + kSearchTypeCompatible)(value);
    if (Budget()) Log("JOIN invite/session: SEARCH_TYPE 0x%llX compatible %d", static_cast<unsigned long long>(value), compatible ? 1 : 0);
    return compatible;
}

}  // namespace

void InitJoinLog(unsigned char* gameBase) { game = gameBase; }

MidHandler JoinLogHookHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x8EC476: return &JoinPressedHandler;
        case 0x8EFEC0: return &JoinFinishedHandler;
        case 0x8F01D0: return &RoomListHandler<0>;
        case 0x8F02E0: return &RoomListHandler<1>;
        case 0x743496: return &SteamLobbyEnterHandler;
        case 0x12B80CB: return &UserSlotTakenHandler;
        case 0x12BEF00: return &MemberStatusHandler;
        case 0x12B8076: return &UserSlotsFullHandler;
        case 0x12D5C90: return &HandshakeTimeoutHandler;
        default: return nullptr;
    }
}

bool ReadUserSlots(const void* users, UserSlotsSnapshot& out) {
    out = {};
    if (!users) return false;
    __try {
        const auto* vector = static_cast<const std::uintptr_t*>(users);
        const auto begin = vector[0], end = vector[1], allocatedEnd = vector[2];
        if (!begin || end < begin || allocatedEnd < end || (end - begin) % 16 ||
            (end - begin) / 16 > static_cast<std::uintptr_t>(kMaxPlayers)) return false;
        UserSlotsSnapshot snapshot{};
        snapshot.capacity = (end - begin) / 16;
        for (std::size_t i = 0; i < snapshot.capacity; ++i) {
            const auto object = *reinterpret_cast<const std::uintptr_t*>(begin + 16 * i);
            if (!object) continue;
            auto& slot = snapshot.slots[i];
            slot.object = object;
            slot.productId = *reinterpret_cast<const std::uintptr_t*>(object + 0x18);
            slot.flags = *reinterpret_cast<const std::uint32_t*>(object + 0x10);
            slot.index = *reinterpret_cast<const std::int32_t*>(object + 0x40);
            ++snapshot.occupied;
            if (slot.flags & 1) ++snapshot.ready;
        }
        out = snapshot;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadCurrentRoomUsers(std::uintptr_t gameBase, std::uintptr_t& users, const char*& failure) {
    users = 0;
    failure = "no active session";
    std::uintptr_t session = 0, room = 0, vtable = 0;
    if (!gameBase || !Read(gameBase + 0x20B2AC0, session) || !session) return false;
    failure = "no active room";
    if (!Read(session + 0x28, room) || !room) return false;
    failure = "unexpected or unreadable room type";
    if (!Read(room, vtable) || vtable != gameBase + kActiveRoomVtableRva) return false;
    failure = "no readable Users object";
    if (!Read(room + 0x160, users) || !users) return false;
    failure = nullptr;
    return true;
}

void NoteUserSlots(std::size_t rosterCount) {
    if (!game || !DetailLog()) return;
    const auto now = GetTickCount64();
    if (now - lastPoll < 500) return;
    lastPoll = now;
    std::uintptr_t users = 0;
    const char* failure = nullptr;
    UserSlotsSnapshot snapshot{};
    if (ReadCurrentRoomUsers(reinterpret_cast<std::uintptr_t>(game), users, failure) &&
        !ReadUserSlots(reinterpret_cast<const void*>(users), snapshot)) failure = "invalid or unreadable slot vector";
    if (failure) {
        if ((!lastSlotFailure || std::strcmp(failure, lastSlotFailure) != 0) && Budget())
            Log("ROOM USERS unavailable: %s; roster=%zu (not evidence that counts agree)", failure, rosterCount);
        lastSlotFailure = failure;
        return;
    }
    bool changed = lastSlotFailure || users != lastUsers || rosterCount != lastRosterCount || snapshot.capacity != lastSlots.capacity;
    lastSlotFailure = nullptr;
    for (std::size_t i = 0; i < 8; ++i) {
        const auto& a = snapshot.slots[i];
        const auto& b = lastSlots.slots[i];
        changed = changed || a.object != b.object || a.productId != b.productId || a.flags != b.flags || a.index != b.index;
    }
    const auto pending = snapshot.occupied - snapshot.ready;
    if (!changed && (!pending || now - lastSlotReport < 5000)) return;
    lastUsers = users;
    lastSlots = snapshot;
    lastRosterCount = rosterCount;
    lastSlotReport = now;
    if (!Budget()) return;
    Log("ROOM USERS: occupied=%zu ready=%zu pending=%zu roster=%zu slots=%zu (pending can be normal during join)",
        snapshot.occupied, snapshot.ready, pending, rosterCount, snapshot.capacity);
    for (std::size_t i = 0; i < snapshot.capacity; ++i) {
        const auto& slot = snapshot.slots[i];
        if (!slot.object) continue;
        char id[40]{};
        ProductUserIdText(reinterpret_cast<const void*>(slot.productId), id, sizeof(id));
        Log("ROOM USER: slot=%zu key=%d EOS %s flags=0x%X ready=%u", i, slot.index,
            id[0] ? id : "?", slot.flags, slot.flags & 1);
    }
}

void ForgetUserSlots() {
    lastSlotFailure = nullptr;
    lastUsers = 0;
    lastRosterCount = ~std::size_t{0};
    lastPoll = lastSlotReport = 0;
    lastSlots = {};
}

void* JoinLogCallHandler(std::uint32_t rva) {
    switch (rva) {
        case 0x8EC659: return reinterpret_cast<void*>(&ContentOwnedHook);
        case 0x8EC837: return reinterpret_cast<void*>(&JoinStartHook);
        case 0x709A30: return reinterpret_cast<void*>(&SearchTypeCompatibleHook);
        case 0x12D56CE: return reinterpret_cast<void*>(&ConfirmUserHook);
        case 0x12D56A4: return reinterpret_cast<void*>(&RejectUserHook);
        default: return nullptr;
    }
}

}  // namespace multislot
