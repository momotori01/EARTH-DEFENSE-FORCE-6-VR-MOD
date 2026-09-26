// Verifies the patch tables against the EDF.dll on disk: every original byte must be present, no
// two writes may overlap, and the SEARCH_TYPE and capacity values must follow the room rules.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

#include "../src/mission.h"
#include "../src/patches.h"
#include "../src/rooms.h"
#include "../src/joinlog.h"

using namespace multislot;

namespace {

int failures = 0;

void Check(bool condition, const char* what, std::uint32_t rva = 0) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s (EDF+%X)\n", what, rva);
    }
}

struct Image {
    std::vector<std::uint8_t> file;
    const IMAGE_NT_HEADERS64* nt = nullptr;

    const std::uint8_t* At(std::uint32_t rva, std::size_t size) const {
        auto section = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (rva >= section->VirtualAddress && rva + size <= section->VirtualAddress + section->SizeOfRawData) {
                const std::size_t offset = section->PointerToRawData + (rva - section->VirtualAddress);
                return offset + size <= file.size() ? file.data() + offset : nullptr;
            }
        }
        return nullptr;
    }
};

std::uint64_t Operand(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t size) {
    std::uint64_t value = 0;
    std::memcpy(&value, bytes.data() + offset, size);
    return value;
}

using Spans = std::set<std::pair<std::uint32_t, std::uint32_t>>;

Spans WriteSpans(const std::vector<Patch>& patches, const std::vector<CallSite>& calls, const std::vector<MidSite>& hooks) {
    Spans spans;
    for (const auto& patch : patches) spans.insert({patch.rva, patch.rva + static_cast<std::uint32_t>(patch.original.size())});
    for (const auto& call : calls) spans.insert({call.rva, call.rva + 5});
    for (const auto& hook : hooks) spans.insert({hook.rva, hook.rva + static_cast<std::uint32_t>(hook.original.size())});
    const PointerSlot slot = RoomViewSlot();
    spans.insert({slot.rva, slot.rva + 8});
    const PointerSlot frame = MainFrameSlot();
    spans.insert({frame.rva, frame.rva + 8});
    const PointerSlot lobby = LobbySlot();
    spans.insert({lobby.rva, lobby.rva + 8});
    for (const auto& missionSlot : MissionSlots()) spans.insert({missionSlot.rva, missionSlot.rva + 8});
    return spans;
}

void CheckNoOverlap(const Spans& spans) {
    std::uint32_t lastEnd = 0;
    for (const auto& span : spans) {
        Check(span.first >= lastEnd, "writes do not overlap", span.first);
        lastEnd = span.second;
    }
}

// Addresses EDF6VR patches (Mods/Plugins/EDF6VR.log, read-only). Each is assumed to cover 8 bytes.
// Every byte EDF6VR changes in EDF.dll, from Mods/Plugins/EDF6VR.patches.txt (read-only; refreshed
// 2026-09-19 after the VR mod was updated). MultiSlot may not write inside any of them.
struct VrSite {
    std::uint32_t rva;
    std::uint32_t size;
};
constexpr VrSite kVrSites[] = {
    {0x2BA9C0, 5}, {0x2BAD96, 5}, {0x2BAF60, 5}, {0x2BFC02, 5},
    {0x2BFC89, 5}, {0x2C0D32, 5}, {0x2C0DB9, 5}, {0x572FFB, 5},
    {0x681A7F, 5}, {0x683EA0, 5}, {0x684271, 5}, {0x685985, 5},
    {0x6888D9, 5}, {0x688FFD, 5}, {0x6891A5, 5}, {0x6904F7, 5},
    {0x690603, 5}, {0x690B3F, 5}, {0x690D54, 5}, {0x691B10, 5},
    {0x692A24, 5}, {0x69366A, 5}, {0x694894, 5}, {0x6A3D0E, 5},
    {0x6A3DFB, 5}, {0x6AEBB0, 5}, {0x6C06F2, 5}, {0x70573B, 5},
    {0x7ACEE0, 5}, {0x7ACF98, 5}, {0x7AEE60, 5}, {0x80200A, 42},
    {0x805657, 4}, {0x80565F, 4}, {0x805EFE, 4}, {0x805F09, 4},
    {0x806123, 4}, {0x80612E, 4}, {0x82B6DB, 5}, {0x100B07B, 5},
    {0x106C0B6, 5}, {0x10FF556, 6}, {0x10FF5C1, 6}, {0x1131198, 7},
    {0x1131233, 7}, {0x1183250, 16}, {0x1183260, 43}, {0x1183290, 16},
    {0x11832A0, 17}, {0x1183310, 17}, {0x1197A55, 5}, {0x1197F31, 5},
    {0x1197FBD, 5}, {0x119889B, 5}, {0x11A2F65, 6}, {0x11A2F74, 2},
    {0x1755578, 8}, {0x1756728, 8}, {0x1756868, 8}, {0x1756870, 8},
    {0x1756898, 8}, {0x17568A8, 8}, {0x17568E0, 8}, {0x17568E8, 8},
    {0x1768C28, 8}, {0x1768C30, 8}, {0x1768C58, 8}, {0x17A6D70, 8},
    {0x17C4038, 8}, {0x17E1CD8, 8}, {0x17E1F28, 8}, {0x17E2158, 8},
    {0x17E2390, 8}, {0x17E2468, 8}, {0x17E2470, 8}, {0x17E24A8, 8},
    {0x17E24B0, 8}, {0x17E26C0, 8}, {0x17E3350, 8}, {0x17E3400, 8},
    {0x17E35B8, 8}, {0x17E3668, 8}, {0x17E3708, 8}, {0x17E37B8, 8},
    {0x17E3858, 8}, {0x17E3908, 8}, {0x17E3B18, 8}, {0x17E3BC8, 8},
    {0x17E3D78, 8}, {0x17E3E28, 8}, {0x17E3F40, 8}, {0x17E3FF0, 8},
    {0x17E40C8, 8}, {0x17E4178, 8}, {0x17E42C8, 8}, {0x17E4378, 8},
    {0x17E4610, 8}, {0x17E46C0, 8}, {0x17E4778, 8}, {0x17E4828, 8},
    {0x17E4930, 8}, {0x17E49E0, 8}, {0x17E4AA8, 8}, {0x17E4B58, 8},
    {0x17E4DF0, 8}, {0x17E4EA0, 8}, {0x17E4FD8, 8}, {0x17E5088, 8},
    {0x17E51C8, 8}, {0x17E5278, 8}, {0x17E5468, 8}, {0x17E5518, 8},
    {0x17E5800, 8}, {0x17E5978, 8}, {0x17E5A28, 8}, {0x17E5B88, 8},
    {0x17E5C38, 8}, {0x17E5E68, 8}, {0x17E5F18, 8}, {0x17E5FD0, 8},
    {0x17E6080, 8}, {0x17E6148, 8}, {0x17E61F8, 8}, {0x17E6348, 8},
    {0x17E63F8, 8}, {0x17F6C20, 8}, {0x17F6CB0, 8}, {0x17F6E20, 8},
    {0x1AE5290, 8},
    // Sites EDF6VR reported in earlier builds but not in the current list: kept so MultiSlot stays clear of
    // anything the VR mod has ever touched. 16 bytes each, which is more than any of them was.
    {0x18428, 16}, {0x2CBDF0, 16}, {0x56D709, 16}, {0x56DB5B, 16},
    {0x570650, 16}, {0x576E60, 16}, {0x692100, 16}, {0x705B10, 16},
    {0x11001F0, 16}, {0x11883C2, 16}, {0x11883D7, 16}, {0x1194280, 16},
    {0x1197A3A, 16}, {0x2136FF0, 16},
};

void CheckClearOfVr(const Spans& spans) {
    for (const auto& span : spans)
        for (const VrSite& vr : kVrSites)
            Check(span.second <= vr.rva || span.first >= vr.rva + vr.size, "write stays clear of EDF6VR's patches", span.first);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: MultiSlotTests EDF.dll\n");
        return 2;
    }
    Image image;
    std::ifstream in(argv[1], std::ios::binary);
    image.file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (image.file.size() < 0x1000) {
        std::printf("FAIL: cannot read %s\n", argv[1]);
        return 1;
    }
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.file.data());
    image.nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.file.data() + dos->e_lfanew);
    Check(image.nt->FileHeader.TimeDateStamp == kImageTimeDateStamp, "EDF.dll TimeDateStamp is the supported build");
    Check(image.nt->OptionalHeader.SizeOfImage == kImageSize, "EDF.dll SizeOfImage is the supported build");

    const auto guest = GuestPatches();
    auto calls = GuestCalls();
    Check(calls.size() == 2, "two member count calls are redirected");
    const auto roomCalls = RoomViewCalls();
    Check(roomCalls.size() == 3, "two panel builder calls and the voice icon call are redirected");
    calls.insert(calls.end(), roomCalls.begin(), roomCalls.end());
    // Test harness: every call to the member list builder, so fake members reach all of its consumers.
    const auto fakeMemberCalls = FakeMemberCalls();
    Check(fakeMemberCalls.size() == 3, "all three member list calls are redirected");
    for (const auto& call : fakeMemberCalls) Check(call.target == 0x7468C0, "the member list builder is the target", call.rva);
    calls.insert(calls.end(), fakeMemberCalls.begin(), fakeMemberCalls.end());
    // The helpers the harness uses to grow that list: reserve (748E70) and append-copies (749040), as the
    // builder itself uses them.
    Check(CallTargets(image.At(0x746A30, 5), 0x746A30, 0x748E70) && CallTargets(image.At(0x746B49, 5), 0x746B49, 0x748E70),
          "the member list builder reserves through 748E70", 0x746A30);
    Check(CallTargets(image.At(0x748F6E, 5), 0x748F6E, 0x749040), "reserve clears the old buffer through 749040", 0x748F6E);
    const std::uint8_t reserveHead[] = {0x40, 0x55, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0xEA, 0x48, 0x8B,
                                        0xF9, 0x48, 0x3B, 0x51, 0x10};  // cmp rdx, [rcx+0x10]: capacity
    const std::uint8_t resizeHead[] = {0x48, 0x89, 0x74, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20,
                                       0x48, 0x8B, 0x41, 0x18};   // mov rax, [rcx+0x18]: size
    Check(std::memcmp(image.At(0x748E70, sizeof(reserveHead)), reserveHead, sizeof(reserveHead)) == 0,
          "748E70 takes the list and a capacity", 0x748E70);
    Check(std::memcmp(image.At(0x749040, sizeof(resizeHead)), resizeHead, sizeof(resizeHead)) == 0,
          "749040 takes the list, a size and an entry", 0x749040);
    for (const auto& call : calls) Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);
    const PointerSlot slot = RoomViewSlot();
    Check(SlotTargets(image.At(slot.rva, 8), image.nt->OptionalHeader.ImageBase, slot.target), slot.name, slot.rva);
    const PointerSlot frameSlot = MainFrameSlot();
    Check(SlotTargets(image.At(frameSlot.rva, 8), image.nt->OptionalHeader.ImageBase, frameSlot.target), frameSlot.name, frameSlot.rva);
    const PointerSlot lobbySlot = LobbySlot();
    Check(SlotTargets(image.At(lobbySlot.rva, 8), image.nt->OptionalHeader.ImageBase, lobbySlot.target), lobbySlot.name, lobbySlot.rva);

    // Constants roomview.cpp relies on, checked where the game itself uses them.
    const auto RipTarget = [&](std::uint32_t rva, std::size_t length) {
        const auto at = image.At(rva, length);
        std::int32_t disp = 0;
        std::memcpy(&disp, at + length - 4, 4);
        return static_cast<std::uint32_t>(static_cast<std::int64_t>(rva) + static_cast<std::int64_t>(length) + disp);
    };
    Check(RipTarget(0x901ADB, 7) == 0x180AC00, "member creation stores the MemberInfo vtable 180AC00", 0x901ADB);
    // What LobbyOnUpdateHook (hostmode.cpp) relies on. 8EDBC0 takes the lobby alone, shows "Lobby_Refreshing" and
    // starts the search at lobby+0x7E0 through 73A6B0, which marks +0x43 and clears +0x40; 73ABE0 sets +0x40 once the
    // results are in. The lobby's dialogs keep the callback they run on closing at lobby+0x118+0x38 (8F0082 clears it
    // and then searches again, as the other dialog callbacks do).
    const std::uint8_t lobbyThis[] = {0x48, 0x8B, 0xF1};                                 // mov rsi, rcx
    const std::uint8_t lobbySearch[] = {0x48, 0x8D, 0x8E, 0xE0, 0x07, 0x00, 0x00};         // lea rcx, [rsi+0x7E0]
    Check(std::memcmp(image.At(0x8EDBEE, 3), lobbyThis, 3) == 0 && RipTarget(0x8EDBF1, 7) == 0x180A2E8 &&
              std::memcmp(image.At(0x180A2E8, 34), L"Lobby_Refreshing", 34) == 0,
          "8EDBC0 takes the lobby and shows Lobby_Refreshing", 0x8EDBF1);
    Check(std::memcmp(image.At(0x8EDD28, 7), lobbySearch, 7) == 0 && CallTargets(image.At(0x8EDD2F, 5), 0x8EDD2F, 0x73A6B0),
          "8EDBC0 starts the search at lobby+0x7E0", 0x8EDD28);
    const std::uint8_t searchStarts[] = {0xC6, 0x43, 0x43, 0x01}, resultsCleared[] = {0xC6, 0x43, 0x40, 0x00},
                       resultsIn[] = {0xC6, 0x47, 0x40, 0x01};
    Check(std::memcmp(image.At(0x73A852, 4), searchStarts, 4) == 0 && std::memcmp(image.At(0x73A85F, 4), resultsCleared, 4) == 0 &&
              std::memcmp(image.At(0x73AEB7, 4), resultsIn, 4) == 0,
          "search flags: +0x43 set at start, +0x40 cleared then and set when the results are in", 0x73A852);
    const std::uint8_t dialogOwner[] = {0x48, 0x8B, 0x5F, 0x08}, dialogSlot[] = {0x48, 0x81, 0xC3, 0x18, 0x01, 0x00, 0x00},
                       dialogCallback[] = {0x48, 0x8B, 0x4B, 0x38};
    Check(std::memcmp(image.At(0x8F0087, 4), dialogOwner, 4) == 0 && std::memcmp(image.At(0x8F008B, 7), dialogSlot, 7) == 0 &&
              std::memcmp(image.At(0x8F0099, 4), dialogCallback, 4) == 0 && CallTargets(image.At(0x8F0105, 5), 0x8F0105, 0x8EDBC0),
          "a lobby dialog's close callback lives at lobby+0x118+0x38 and searches again", 0x8F0087);
    // MemberInfo's name, which roomview.cpp logs as the room roster. Creation (the shared_ptr block starts
    // 0x10 before the object) clears the class-ok byte at +0x08, writes the class at +0x0C and then builds
    // the name in place at +0x10; 8F1E80 is that constructor.
    const std::uint8_t memberHead[] = {0xC6, 0x43, 0x18, 0x00,              // mov byte [rbx+0x18], 0   -> +0x08
                                       0x44, 0x89, 0x73, 0x1C,              // mov [rbx+0x1C], r14d     -> +0x0C
                                       0x48, 0x8D, 0x4B, 0x20};             // lea rcx, [rbx+0x20]      -> +0x10
    Check(std::memcmp(image.At(0x901AE6, sizeof(memberHead)), memberHead, sizeof(memberHead)) == 0 &&
              RipTarget(0x901AF2, 5) == 0x8F1E80,
          "MemberInfo keeps the name at +0x10, built by 8F1E80", 0x901AE6);
    const std::uint8_t nameCapacity[] = {0x48, 0xC7, 0x41, 0x20, 0x07, 0x00, 0x00, 0x00};  // [rcx+0x20] = 7
    const std::uint8_t nameEmpty[] = {0x48, 0x89, 0x73, 0x10,   // [rcx+0x18] = 0     the size
                                      0x66, 0x89, 0x33,         // mov word [rcx+8], si  a wchar_t terminator
                                      0x40, 0x88, 0x31};        // mov byte [rcx], sil   the name-valid byte
    Check(std::memcmp(image.At(0x8F1EA0, sizeof(nameCapacity)), nameCapacity, sizeof(nameCapacity)) == 0 &&
              std::memcmp(image.At(0x8F1EB4, sizeof(nameEmpty)), nameEmpty, sizeof(nameEmpty)) == 0,
          "the name is a std::wstring at +0x18 with SSO capacity 7, behind a valid byte at +0x10", 0x8F1EA0);
    Check(RipTarget(0x1188315, 7) == 0x2136530, "pad poll uses the input slots at 2136530", 0x1188315);
    const std::uint8_t rsClick[] = {0x4A, 0x8B, 0x8C, 0x2B, 0xB8, 0x00, 0x00, 0x00};  // mov rcx, [rbx+r13+0xB8]
    Check(std::memcmp(image.At(0x1188C35, sizeof(rsClick)), rsClick, sizeof(rsClick)) == 0,
          "right stick click is the channel at slot+0xB8", 0x1188C35);
    const std::uint8_t pressed[] = {0xF3, 0x0F, 0x11, 0x49, 0x48, 0x44, 0x88, 0x41, 0x4C};  // value +0x48, pressed +0x4C
    Check(std::memcmp(image.At(0x1182A00, sizeof(pressed)), pressed, sizeof(pressed)) == 0,
          "input channel keeps the pressed state at +0x4C", 0x1182A00);
    const std::uint8_t templates[] = {0x48, 0x8D, 0x05};  // lea rax, [Button_Master] in the panel builder
    Check(std::memcmp(image.At(0x8F805B, 3), templates, 3) == 0 && RipTarget(0x8F805B, 7) == 0x180B428,
          "panel builder's template table starts with Button_Master", 0x8F805B);

    for (const auto& patch : guest) {
        Check(Matches(image.At(patch.rva, patch.original.size()), patch), patch.name, patch.rva);
        Check(patch.original.size() == patch.replacement.size(), "replacement has the original size", patch.rva);
    }

    // Room tables: each patch turns one operand 4 into 8 (or 4 records into 8 records) in the three constructors that
    // size a table per room member.
    const auto sessions = SessionPatches();
    Check(sessions.size() == 11, "session patch table size");
    for (const auto& patch : sessions) {
        Check(Matches(image.At(patch.rva, patch.original.size()), patch), patch.name, patch.rva);
        Check(patch.original.size() == patch.replacement.size() && patch.original != patch.replacement, "session patch changes bytes in place", patch.rva);
        if (patch.rva == 0x9606A9) {
            Check(Operand(patch.original, 1, 4) == 0x50 * kVanillaPlayers && Operand(patch.replacement, 1, 4) == 0x50 * kMaxPlayers,
                  "the voice chat HUD allocates eight 0x50-byte records", patch.rva);
            continue;
        }
        int changed = 0;
        bool fourToEight = true;
        for (std::size_t i = 0; fourToEight && i < patch.original.size(); ++i) {
            if (patch.original[i] == patch.replacement[i]) continue;
            ++changed;
            fourToEight = patch.original[i] == kVanillaPlayers && patch.replacement[i] == kMaxPlayers;
        }
        Check(fourToEight && changed == 1, "session patch changes a 4 into 8 and nothing else", patch.rva);
        Check((patch.rva > 0x12B77E0 && patch.rva < 0x12B79A3) || (patch.rva > 0x12CB5F0 && patch.rva < 0x12CBA54) ||
                  (patch.rva > 0x9605E0 && patch.rva < 0x960844),
              "session patch lies in the Users, packet Controller or UiVoiceChat_Notify constructor", patch.rva);
    }
    // UiVoiceChat_Notify: the constructor's count feeds the record resize; the update (vtable slot 1) writes records
    // 0x50 bytes apart from +0x140 for every room member.
    Check(CallTargets(image.At(0x9606AE, 5), 0x9606AE, 0x12D85B0) && CallTargets(image.At(0x960789, 5), 0x960789, 0x9621A0),
          "the voice chat HUD allocates and resizes its records in the constructor", 0x960789);
    Check(SlotTargets(image.At(0x1811998, 8), image.nt->OptionalHeader.ImageBase, 0x961140), "961140 is UiVoiceChat_Notify's update", 0x1811998);
    const std::uint8_t recordBase[] = {0x48, 0x8B, 0x98, 0x40, 0x01, 0x00, 0x00};  // mov rbx, [rax+0x140]
    const std::uint8_t recordStep[] = {0x49, 0x83, 0xC5, 0x50};                    // add r13, 0x50
    Check(std::memcmp(image.At(0x961560, 7), recordBase, 7) == 0 && std::memcmp(image.At(0x9616DD, 4), recordStep, 4) == 0,
          "the voice chat HUD update writes 0x50-byte records from +0x140", 0x961560);
    Check(CallTargets(image.At(0x12BD4D7, 5), 0x12BD4D7, 0x12B77E0), "the room constructor builds its Users with 12B77E0", 0x12BD4D7);
    Check(CallTargets(image.At(0x12B7967, 5), 0x12B7967, 0x732410), "Users grows its empty slot vector through 732410", 0x12B7967);
    Check(CallTargets(image.At(0x12D2678, 5), 0x12D2678, 0x12CB5F0), "ControllerImpl builds on the packet Controller 12CB5F0", 0x12D2678);
    Check(CallTargets(image.At(0x12CB97E, 5), 0x12CB97E, 0x12D2060) && CallTargets(image.At(0x12CBA0A, 5), 0x12CBA0A, 0x12CAA40),
          "the session count feeds the reserve and the resize", 0x12CB97E);
    const std::uint8_t slotSearch[] = {0x4D, 0x39, 0x34, 0xC1};                                                // cmp [r9+rax*8], r14
    const std::uint8_t slotStore[] = {0x49, 0x63, 0x4C, 0x24, 0x40, 0x48, 0xC1, 0xE1, 0x04, 0x49, 0x03, 0x4D, 0x00};  // slot = [user+0x40]
    Check(std::memcmp(image.At(0x12B8058, 4), slotSearch, 4) == 0 && std::memcmp(image.At(0x12B824A, 13), slotStore, 13) == 0,
          "Users::Add takes the first empty slot and stores the user at its index", 0x12B8058);
    const std::uint8_t sessionIndex[] = {0x4C, 0x63, 0x60, 0x40, 0x49, 0xC1, 0xE4, 0x04, 0x49, 0x8B, 0x45, 0x20};  // [ctrl+0x20][slot]
    Check(std::memcmp(image.At(0x12CDA8E, 12), sessionIndex, 12) == 0, "packet sessions are indexed by the user slot", 0x12CDA8E);

    // Mission phase tables.
    const auto missionPatches = MissionPatches();
    const auto missionHooks = MissionHooks();
    const auto missionCalls = MissionCalls();
    for (const auto& patch : missionPatches) {
        Check(Matches(image.At(patch.rva, patch.original.size()), patch), patch.name, patch.rva);
        Check(patch.original.size() == patch.replacement.size() && patch.original != patch.replacement, "mission patch changes bytes in place", patch.rva);
    }
    for (const auto& hook : missionHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
        Check(hook.original.size() >= 5 && hook.displacedOffset + hook.displacedSize <= hook.original.size(), "hook covers a jump", hook.rva);
    }
    for (const auto& call : missionCalls) Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);
    Check(missionPatches.size() == 27 && missionHooks.size() == 25 && missionCalls.size() == 5, "mission table sizes");
    // The ninth remote flag would land on the user vector CreatePlayers keeps at rsp+0x30 and re-reads
    // every pass of the loop that writes the flags (mission.cpp, RemoteFlagHandler).
    const std::uint8_t vectorBegin[] = {0x48, 0x8B, 0x7C, 0x24, 0x30};  // 1D98E9 mov rdi, [rsp+0x30]
    Check(std::memcmp(image.At(0x1D98E9, 5), vectorBegin, 5) == 0 && std::memcmp(image.At(0x1D985E, 5), vectorBegin, 5) == 0,
          "the flags at rsp+0x28 have eight bytes before the user vector at rsp+0x30", 0x1D98E9);
    const auto missionSlots = MissionSlots();
    Check(missionSlots.size() == 3, "mission slot table size");
    for (const auto& missionSlot : missionSlots)
        Check(SlotTargets(image.At(missionSlot.rva, 8), image.nt->OptionalHeader.ImageBase, missionSlot.target), missionSlot.name, missionSlot.rva);
    // The three item stores are the same leaf: movsxd r9, [rdx]; mov rax, [GameStatus]; mov rcx, [r8]; mov [rax+r9*8+0x14FD4], rcx; ret
    for (const auto& missionSlot : missionSlots) {
        const std::uint8_t* leaf = image.At(missionSlot.target, 22);
        const std::uint8_t head[] = {0x4C, 0x63, 0x0A, 0x48, 0x8B, 0x05};
        const std::uint8_t tail[] = {0x49, 0x8B, 0x08, 0x4A, 0x89, 0x8C, 0xC8, 0xD4, 0x4F, 0x01, 0x00, 0xC3};
        Check(leaf && std::memcmp(leaf, head, 6) == 0 && RipTarget(missionSlot.target + 3, 7) == kGameStatusPointer &&
                  std::memcmp(leaf + 10, tail, sizeof(tail)) == 0,
              "item store callback writes GameStatus+0x14FD4 + position*8", missionSlot.target);
    }
    // Constants mission.cpp relies on, where the game itself uses them.
    Check(RipTarget(0x790899, 7) == kGameStatusPointer, "MissionSync_Update loads the GameStatus pointer", 0x790899);
    const std::uint8_t recordsLea[] = {0x48, 0x8D, 0x88, 0x78, 0x4C, 0x01, 0x00};  // lea rcx, [rax+0x14C78]
    Check(std::memcmp(image.At(0x7908A5, 7), recordsLea, 7) == 0, "loadout records start at GameStatus+0x14C78", 0x7908A5);
    // The online HUD's colour tables (mission.cpp, PlayerTagIndexHandler): 7FFBD0 writes the player's
    // index to the local each caller indexes its table with, and every one of those tables is built for
    // four entries. If an update to the game widens them, these fail and the wrapping can go.
    const std::uint8_t statusIndexOut[] = {0x48, 0x8D, 0x54, 0x24, 0x64};  // 804EB6 lea rdx, [rsp+0x64]
    const std::uint8_t chatIndexOut[] = {0x48, 0x8D, 0x55, 0xB0};          // 801820 lea rdx, [rbp-0x50]
    const std::uint8_t radarIndexOut[] = {0x48, 0x8D, 0x54, 0x24, 0x44};   // 82A1FC lea rdx, [rsp+0x44]
    Check(std::memcmp(image.At(0x804EB6, 5), statusIndexOut, 5) == 0 && CallTargets(image.At(0x804EBF, 5), 0x804EBF, 0x7FFBD0),
          "the status HUD asks 7FFBD0 for the index at rsp+0x64", 0x804EBF);
    Check(std::memcmp(image.At(0x801820, 4), chatIndexOut, 4) == 0 && CallTargets(image.At(0x801828, 5), 0x801828, 0x7FFBD0),
          "the chat HUD asks 7FFBD0 for the index at rbp-0x50", 0x801828);
    Check(std::memcmp(image.At(0x82A1FC, 5), radarIndexOut, 5) == 0 && CallTargets(image.At(0x82A205, 5), 0x82A205, 0x7FFBD0),
          "the radar asks 7FFBD0 for the index at rsp+0x44", 0x82A205);
    const std::uint8_t statusLamp[] = {0x8B, 0x44, 0x24, 0x64, 0x48, 0x8D, 0x04, 0x40, 0x48, 0xC1,
                                       0xE0, 0x04, 0x48, 0x03, 0x87, 0xD8, 0x00, 0x00, 0x00};  // 8056F6 [rdi+0xD8] + index*0x30
    const std::uint8_t chatEntry[] = {0x4C, 0x63, 0x6D, 0xB0, 0x49, 0xC1, 0xE5, 0x05};  // 801D7F index*0x20
    const std::uint8_t radarColour[] = {0x48, 0x63, 0x44, 0x24, 0x44, 0x48, 0xC1, 0xE0,
                                        0x04, 0x4C, 0x8D, 0x8D, 0x50, 0x01, 0x00, 0x00};  // 82A6CD rbp+0x150 + index*0x10
    Check(std::memcmp(image.At(0x8056F6, sizeof(statusLamp)), statusLamp, sizeof(statusLamp)) == 0,
          "the status HUD takes the player lamp from table[index]", 0x8056F6);
    Check(std::memcmp(image.At(0x801D7F, sizeof(chatEntry)), chatEntry, sizeof(chatEntry)) == 0,
          "the chat HUD takes the balloon from table[index]", 0x801D7F);
    Check(std::memcmp(image.At(0x82A6CD, sizeof(radarColour)), radarColour, sizeof(radarColour)) == 0 &&
              std::memcmp(image.At(0x82A743, sizeof(radarColour)), radarColour, sizeof(radarColour)) == 0,
          "the radar takes the marker colour from table[index]", 0x82A6CD);
    const std::uint8_t fourBy30[] = {0xB9, 0xC0, 0x00, 0x00, 0x00};  // mov ecx, 4 * 0x30
    const std::uint8_t fourBy20[] = {0xB9, 0x80, 0x00, 0x00, 0x00};  // mov ecx, 4 * 0x20
    Check(std::memcmp(image.At(0x8071F7, 5), fourBy30, 5) == 0 && std::memcmp(image.At(0x807400, 5), fourBy30, 5) == 0,
          "HudPlayer_MultiPlayStatus builds its class and lamp tables for four", 0x807400);
    Check(std::memcmp(image.At(0x802A95, 5), fourBy20, 5) == 0, "HudPlayer_Chat builds its balloon table for four", 0x802A95);
    const std::uint8_t contextSize[] = {0xBA, 0xF8, 0x02, 0x00, 0x00};  // mov edx, 0x2F8 (sized delete)
    Check(std::memcmp(image.At(0x1D6F31, 5), contextSize, 5) == 0 && kMovedPlayerArray >= 0x2F8, "moved array starts after MissionContext", 0x1D6F31);
    const std::uint8_t vanillaArray[] = {0x48, 0x8D, 0x8B, 0xE8, 0x00, 0x00, 0x00};  // lea rcx, [rbx+0xE8], rbx = this+0x18
    Check(std::memcmp(image.At(0x1D628E, 7), vanillaArray, 7) == 0, "constructor builds the player array at +0x100", 0x1D628E);
    Check(RipTarget(0x1D6295, 7) == 0x1D6C50 && RipTarget(0x1D6ED5, 7) == 0x1D6C50, "constructor and destructor use the weak_ptr destructor", 0x1D6ED5);

    const auto spawnHooks = SpawnHooks();
    Check(spawnHooks.size() == 8, "spawn hook table size");
    for (const auto& hook : spawnHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
        Check(hook.original.size() >= 5 && hook.displacedOffset == 0 && hook.displacedSize == hook.original.size(),
              "spawn hooks run all of their instructions after the handler", hook.rva);
    }
    // The group loops read the count again from the parameter block each pass (`cmp r12d, [r14+0x68]` and
    // `cmp r12d, [r13+0x50]`), which is what lets the handler raise it after the first object.
    const std::uint8_t pointLoop[] = {0x45, 0x3B, 0x66, 0x68};  // 1D932B cmp r12d, [r14+0x68]
    const std::uint8_t areaLoop[] = {0x45, 0x3B, 0x65, 0x50};   // 1D879A cmp r12d, [r13+0x50]
    Check(std::memcmp(image.At(0x1D932B, 4), pointLoop, 4) == 0, "CreateObjectGroup loop compares with params+0x68", 0x1D932B);
    Check(std::memcmp(image.At(0x1D879A, 4), areaLoop, 4) == 0, "CreateAreaObject loop compares with params+0x50", 0x1D879A);
    // "copy armor" (armor.cpp): the reads of the armor pickup count that are raised, and the four writes of
    // it that must stay clear of them, or a raised count would reach the save. The read the room is shown
    // (D7BFA, inside D7BD0) is deliberately not raised, so nobody ever copies a copied number.
    // 1DC4FB picks the path: online every player comes from a loadout record (595A12), offline this machine's
    // own comes straight from the save (595CD9). Both have to be raised or the armor only reaches one of them.
    const std::uint8_t onlineBranch[] = {0x83, 0xF9, 0xFF};  // 1DC4FB cmp ecx, -1
    Check(std::memcmp(image.At(0x1DC4FB, 3), onlineBranch, 3) == 0 &&
              CallTargets(image.At(0x1DC525, 5), 0x1DC525, 0x591130) &&
              CallTargets(image.At(0x1DC539, 5), 0x1DC539, 0x591410),
          "CreatePlayers builds this machine's player from a record online and from the save offline", 0x1DC4FB);
    Check(CallTargets(image.At(0x591448, 5), 0x591448, 0x595C80), "the offline path reads the save at 595C80", 0x591448);
    const auto armorHooks = ArmorHooks();
    Check(armorHooks.size() == 3, "armor hook table size");
    for (const auto& hook : armorHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
        Check(hook.displacedSize == 0 && hook.original.size() >= 5, "the armor handlers replace the whole read", hook.rva);
    }
    const std::uint8_t armorStore[] = {0x89, 0x8C, 0x98, 0x88, 0x6F, 0x00, 0x00};  // AB311 mov [rax+rbx*4+0x6F88], ecx
    const std::uint8_t armorScreen[] = {0x44, 0x89, 0x84, 0x90, 0x88, 0x6F, 0x00, 0x00};  // 87AF8F ..., r8d
    Check(std::memcmp(image.At(0x0AB311, sizeof(armorStore)), armorStore, sizeof(armorStore)) == 0,
          "the save load writes the pickup count without reading it", 0x0AB311);
    Check(std::memcmp(image.At(0x87AF8F, sizeof(armorScreen)), armorScreen, sizeof(armorScreen)) == 0,
          "the armor screen writes the pickup count from its own state", 0x87AF8F);
    const std::uint8_t armorFromScreen[] = {0xF3, 0x4C, 0x0F, 0x2C, 0x81, 0xC0, 0x07, 0x00, 0x00};  // 87AF6F
    Check(std::memcmp(image.At(0x87AF6F, sizeof(armorFromScreen)), armorFromScreen, sizeof(armorFromScreen)) == 0,
          "and that state is a float of its own, not a read of the count", 0x87AF6F);
    // The rules the copied armor is worked out with: base and step per class, from the game data.
    Check(CallTargets(image.At(0x0D7C07, 5), 0x0D7C07, 0x0E3470) && CallTargets(image.At(0x0D7C14, 5), 0x0D7C14, 0x0E34A0),
          "armor = base(class) + step(class) * pickups", 0x0D7BD0);
    const std::uint8_t roomArmor[] = {0x8B, 0xBC, 0x91, 0x88, 0x6F, 0x00, 0x00};  // D7BFA, left as the game has it
    Check(std::memcmp(image.At(0x0D7BFA, sizeof(roomArmor)), roomArmor, sizeof(roomArmor)) == 0,
          "the armor the room is shown is still read straight from the save", 0x0D7BFA);
    for (const auto& hook : armorHooks) Check(hook.rva != 0x0D7BFA, "and is not one of the raised reads", hook.rva);

    for (const auto& hook : DiagnosticHooks()) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
    }
    for (const auto& call : DiagnosticCalls()) Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);
    for (const auto& call : RecoveryCalls()) Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);
    // These constructors overwrite eos::lobby::Room's base vtable with eos::RoomImpl.
    // Tie the runtime guard to the actual image, so a fake fixture cannot repeat a wrong assumption.
    for (const auto site : {0x741088u, 0x741557u}) {
        Check(RipTarget(site, 7) == kActiveRoomVtableRva &&
              std::memcmp(image.At(site + 7, 3), "\x48\x89\x03", 3) == 0,
              "both live RoomImpl constructors install the vtable accepted by diagnostics/recovery", site);
    }
    Check(SlotTargets(image.At(static_cast<std::uint32_t>(kActiveRoomVtableRva) + 8, 8),
                      image.nt->OptionalHeader.ImageBase, 0x12BD420),
          "RoomImpl inherits the audited GetUsers implementation", static_cast<std::uint32_t>(kActiveRoomVtableRva));
    for (const auto& hook : GhostHooks()) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
    }
    for (const auto& call : GhostCalls()) Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);

    std::vector<CallSite> allCalls = calls;
    allCalls.insert(allCalls.end(), missionCalls.begin(), missionCalls.end());
    const auto ghostCalls = GhostCalls();
    allCalls.insert(allCalls.end(), ghostCalls.begin(), ghostCalls.end());
    const auto diagnosticCalls = DiagnosticCalls();
    allCalls.insert(allCalls.end(), diagnosticCalls.begin(), diagnosticCalls.end());
    const auto recoveryCalls = RecoveryCalls();
    allCalls.insert(allCalls.end(), recoveryCalls.begin(), recoveryCalls.end());
    auto allHooks = missionHooks;
    allHooks.insert(allHooks.end(), spawnHooks.begin(), spawnHooks.end());
    const auto diagnosticHooks = DiagnosticHooks();
    allHooks.insert(allHooks.end(), diagnosticHooks.begin(), diagnosticHooks.end());
    allHooks.insert(allHooks.end(), armorHooks.begin(), armorHooks.end());
    const auto ghostHooks = GhostHooks();
    allHooks.insert(allHooks.end(), ghostHooks.begin(), ghostHooks.end());
    // 8Player MOD (hostmode.cpp): the sites it replaces, and the game functions it calls from the menu frame.
    const auto hostHooks = HostModeHooks();
    Check(hostHooks.size() == 11, "host mode hook table size");
    for (const auto& hook : hostHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
        Check(hook.displacedSize == 0 && hook.original.size() >= 5, "host mode handlers replace the whole instruction", hook.rva);
        // 74AC69/74/7F/8A: movabs rax, (high << 32) | 0x91, the vanilla range of one room kind.
        if (hook.original.size() == 10 && hook.original[0] == 0x48 && hook.original[1] == 0xB8) {
            const std::uint64_t range = Operand(hook.original, 2, 8);
            const auto high = static_cast<std::uint32_t>(range >> 32);
            Check(static_cast<std::uint32_t>(range) == 0x91 &&
                      high >= 0x91 && high <= 0x94,
                  "search range site is movabs rax, vanilla [0x91, high]", hook.rva);
        }
        if (hook.original[0] == 0xBB) {
            const auto vanilla = static_cast<std::uint64_t>(hook.original[1]);
            const std::uint64_t mirrored = 2 * kSearchTypeCenter - vanilla;
            Check(vanilla >= 0x91 && vanilla <= 0x94 && mirrored >= 2 * kSearchTypeCenter - 0x94 && mirrored <= 2 * kSearchTypeCenter - 0x91,
                  "published SEARCH_TYPE is the vanilla value mirrored around the centre", hook.rva);
            Check((mirrored & ~0xFull) != 0x90, "vanilla join check refuses the mirrored value", hook.rva);
            Check((mirrored & ~0x1Full) != 0x80 && (mirrored < 0x8C || mirrored > 0x94), "MultiSlot 0.2-0.4.1 neither searches nor accepts it", hook.rva);
            Check(mirrored < 0x7C || mirrored > 0x7F, "MultiSlot 0.4.2-0.4.3 (join check 0x7C..0x7F) refuses it", hook.rva);
            Check(mirrored < 0x74 || mirrored > 0x77, "MultiSlot 0.5.0-1.0.0 (join check 0x74..0x77) refuses it", hook.rva);
            Check(mirrored < 0x6C || mirrored > 0x6F, "MultiSlot 1.1.0-1.1.1 (join check 0x6C..0x6F) refuses it", hook.rva);
            Check(mirrored < 0x64 || mirrored > 0x67, "MultiSlot 1.2.0 (join check 0x64..0x67) refuses it", hook.rva);
            Check(mirrored < 0x5C || mirrored > 0x5F, "MultiSlot 1.2.1-1.2.5 (join check 0x5C..0x5F) refuses it", hook.rva);
            Check((mirrored < 0x5D || mirrored > 0x93) && (mirrored < 0x5E || mirrored > 0x92) && (mirrored < 0x5C || mirrored > 0x94) &&
                      (mirrored < 0x5F || mirrored > 0x91),
                  "MultiSlot 1.2.1-1.2.5 searches (and earlier ones) do not list it", hook.rva);
            Check(mirrored >= 2 * (kSearchTypeCenter - 4) - 0x94, "the published values stay inside the search range of the next family", hook.rva);
        }
    }
    Check(CallTargets(image.At(0x742B32, 5), 0x742B32, 0x12B3160), "the create function with the capacity site creates the lobby", 0x742B32);
    // The Steam lobby step: 7435F7's r8d is cMaxMembers of 7812B0, which calls ISteamMatchmaking::CreateLobby
    // (vtable +0x68) on the SteamMatchMaking009 interface (context 1F76000, initialiser 787B60).
    Check(CallTargets(image.At(0x74360C, 5), 0x74360C, 0x7812B0), "the Steam lobby capacity feeds 7812B0", 0x74360C);
    const std::uint8_t createLobby[] = {0xFF, 0x50, 0x68};  // call [rax+0x68]
    Check(RipTarget(0x78130D, 7) == 0x1F76000 && std::memcmp(image.At(0x781341, 3), createLobby, 3) == 0 &&
              std::memcmp(image.At(0x78133E, 3), "\x44\x8B\xC5", 3) == 0,  // mov r8d, ebp (ebp = the caller's r8d)
          "7812B0 passes r8d to ISteamMatchmaking::CreateLobby", 0x781341);
    Check(SlotTargets(image.At(0x1F76000, 8), image.nt->OptionalHeader.ImageBase, 0x787B60) && RipTarget(0x787B71, 7) == 0x17EE480 &&
              std::memcmp(image.At(0x17EE480, 20), "SteamMatchMaking009", 20) == 0,
          "context 1F76000 is SteamMatchMaking009", 0x1F76000);
    Check(CallTargets(image.At(0x749C9B, 5), 0x749C9B, 0x12BFE20), "the update capacity feeds SetMaxMembers", 0x749C9B);
    Check(CallTargets(image.At(0x8C12EE, 5), 0x8C12EE, 0x84B490), "HUiMainFrame::OnUpdate starts with HUiLayout::OnUpdate", 0x8C12EE);
    Check(CallTargets(image.At(0x8C0A98, 5), 0x8C0A98, 0x839600), "MainFrame looks components up by name with 839600", 0x8C0A98);
    Check(CallTargets(image.At(0x8C1EE2, 5), 0x8C1EE2, 0x8859C0) && CallTargets(image.At(0x8C1EF5, 5), 0x8C1EF5, 0x863690),
          "MainFrame sets component text with 8859C0 + 863690", 0x8C1EE2);
    const std::uint8_t isRoomHost[] = {0x48, 0x8B, 0x0D};  // mov rcx, [g_Online]; jmp IsRoomHost
    Check(std::memcmp(image.At(0x70EEB0, 3), isRoomHost, 3) == 0 && RipTarget(0x70EEB0, 7) == 0x20B2AC8 &&
              image.At(0x70EEB7, 1)[0] == 0xE9,
          "IsRoomHost native reaches 787370 with the online manager", 0x70EEB0);
    Check(RipTarget(0x7859AA, 7) == 0x20B2AC0, "the session check reads the holder at 20B2AC0", 0x7859AA);
    const std::uint8_t sessionField[] = {0x48, 0x83, 0xB8, 0xC0, 0x00, 0x00, 0x00, 0x00};  // cmp qword [rax+0xC0], 0
    Check(std::memcmp(image.At(0x7859D3, 8), sessionField, 8) == 0, "session present = [holder-0x98+0xC0] != 0", 0x7859D3);

    auto all = guest;
    all.insert(all.end(), sessions.begin(), sessions.end());
    all.insert(all.end(), missionPatches.begin(), missionPatches.end());
    allHooks.insert(allHooks.end(), hostHooks.begin(), hostHooks.end());
    const Spans spans = WriteSpans(all, allCalls, allHooks);
    CheckNoOverlap(spans);
    CheckClearOfVr(spans);

    Check(CapacityFromInfo(2, 3, 5) == 5, "capacity: consistent 5-player room");
    Check(CapacityFromInfo(4, 0, 4) == 4, "capacity: full vanilla room");
    Check(CapacityFromInfo(0, 8, 8) == 8, "capacity: empty 8-player room");
    Check(CapacityFromInfo(3, 3, 5) == 0, "capacity: members + available != max is rejected");
    Check(CapacityFromInfo(0, 0, 0) == 0, "capacity: zero max is rejected");
    Check(CapacityFromInfo(1, 70, 71) == 0, "capacity: more than EOS's 64 is rejected");

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("patch tables verified against %s\n", argv[1]);
    return 0;
}
