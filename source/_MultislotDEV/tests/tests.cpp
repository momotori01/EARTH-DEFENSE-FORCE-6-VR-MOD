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
#include "../src/gaplog.h"
#include "../src/packetsize.h"
#include "../src/smoothing.h"
#include "../src/rooms.h"
#include "../src/joinlog.h"
#include "../src/handaim.h"
#include "../src/versionmsg.h"
#include "../src/ridelog.h"

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

// A multiset: two tables writing the very same site must show up as an overlap, not collapse into one span.
using Spans = std::multiset<std::pair<std::uint32_t, std::uint32_t>>;

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
    // Added by EDF6VR on 2026-10-01 (133 sites: the vehicle hand aim / crew work).
    {0x59576A, 5}, {0x5957BA, 5}, {0x598459, 5}, {0x5996A9, 5},
    // Added by EDF6VR by 2026-10-04 (147 sites: chat bubbles, HUD and more vtable slots).
    {0x17E6138, 8}, {0x17E6338, 8}, {0x17F6A70, 8}, {0x17F42A8, 8},
    {0x17FFAD0, 8}, {0x17FF068, 8}, {0x17FFB30, 8}, {0x17FF1D8, 8},
    {0x6B34C7, 5}, {0x801977, 5}, {0x802059, 5}, {0x801CFA, 5},
    {0x801CB8, 4}, {0x801CB0, 4},
    // EDF6VR's hand aim send (2026-10-04, told by its session before its patches.txt was rewritten): the call to
    // 694910 at 690D3B.
    {0x690D3B, 5},
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
    // The remote-player position correction (smoothing.h). SoldierBase's per-frame sync steps its
    // correction vector toward the received one by a fixed fraction; the fraction is a shared 0.05, so
    // only this one instruction's operand may be retargeted. If a game update moves either, the factor
    // must not be written at all.
    const std::uint8_t smoothing[] = {0x0F, 0x59, 0x35, 0xE3, 0xF6, 0x20, 0x01};  // mulps xmm6, [rip+...]
    Check(std::memcmp(image.At(kSmoothingSite, sizeof(smoothing)), smoothing, sizeof(smoothing)) == 0 &&
              RipTarget(kSmoothingSite, sizeof(smoothing)) == 0x17A5A70,
          "the remote player correction still multiplies by the constant at 17A5A70", kSmoothingSite);
    const std::uint8_t splat[16] = {0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D,
                                    0xCD, 0xCC, 0x4C, 0x3D, 0xCD, 0xCC, 0x4C, 0x3D};
    Check(std::memcmp(image.At(0x17A5A70, sizeof(splat)), splat, sizeof(splat)) == 0,
          "and that constant is still 0.05 four ways", 0x17A5A70);
    // The neighbours the site is identified by: the read before it and the write after.
    const std::uint8_t around[] = {0x0F, 0x10, 0x87, 0x20, 0x19, 0x00, 0x00};  // movups xmm0, [rdi+0x1920]
    Check(std::memcmp(image.At(0x59637C, sizeof(around)), around, sizeof(around)) == 0,
          "the correction vector is still read from player+0x1920", 0x59637C);
    // How long a correction takes to shrink to a tenth, at 90 ms a step.
    Check(SmoothingSettleMs(kVanillaSmoothing) > 3800 && SmoothingSettleMs(kVanillaSmoothing) < 4200,
          "the stock 0.05 needs about four seconds", 0);
    Check(SmoothingSettleMs(0.5f) > 250 && SmoothingSettleMs(0.5f) < 350, "half closes it in about 0.3 s", 0);
    Check(SmoothingSettleMs(0.0f) == 0 && SmoothingSettleMs(-1.0f) == 0 && SmoothingSettleMs(2.0f) == 0,
          "a factor outside (0, 1] has no settle time", 0);
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
    // The version message (versionmsg.h): 709A30 is the only call of the join check, fed the room's SEARCH_TYPE
    // (12AE670); 951BC1 is the dialog builder's one text lookup, and both "could not join" dialogs (8EFF37 after a
    // join, 92650D) build OnlineError_RoomError with it.
    const auto versionCalls = VersionMessageCalls();
    for (const auto& call : versionCalls) Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);
    Check(CallTargets(image.At(0x709A28, 5), 0x709A28, 0x12AE670) && std::memcmp(image.At(0x709A2D, 3), "\x48\x8B\xC8", 3) == 0,
          "the join check is handed the room's SEARCH_TYPE", 0x709A28);
    Check(versionCalls.size() == 3, "three version message calls");
    for (const auto& call : versionCalls) Check(VersionMessageCallHandler(call.rva) != nullptr, "and a handler for each", call.rva);
    const auto versionHooks = VersionMessageHooks();
    Check(versionHooks.size() == 2, "two version message hooks");
    for (const auto& hook : versionHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify) && VersionMessageHookHandler(hook.rva) != nullptr,
              hook.name, hook.rva);
    }
    // 73B230: `mov ecx, ebx; call 74AC20` decodes the entry's SEARCH_TYPE, then the kind goes to RoomInfo+0x168.
    Check(std::memcmp(image.At(0x73C5A5, 2), "\x8B\xCB", 2) == 0 && CallTargets(image.At(0x73C5A7, 5), 0x73C5A7, 0x74AC20),
          "the room list entry is decoded from ebx right before the hook", 0x73C5A5);
    // The join button: 8EC476 cleared? -> 8EC62E; else difficulty [r14+0x18] <= 2 -> 8EC62E; else the refusal dialog
    // with Lobby_Join_Impossible (8EC4EE/8EC4F5), which ends at 8EC629 `jmp 8EC878` - r14 and rcx are not read
    // in between, so pointing them at stand-ins only steers the two compares.
    Check(std::memcmp(image.At(0x8EC47D, 6), "\x0F\x85\xAB\x01\x00\x00", 6) == 0 &&
              std::memcmp(image.At(0x8EC483, 11), "\x41\x83\x7E\x18\x02\x0F\x8E\xA0\x01\x00\x00", 11) == 0,
          "the gate: jne 8EC62E, cmp dword [r14+0x18], 2, jle 8EC62E", 0x8EC47D);
    Check(RipTarget(0x8EC4EE, 7) == 0x180A540 && std::memcmp(image.At(0x180A540, 44), L"Lobby_Join_Impossible", 44) == 0 &&
              std::memcmp(image.At(0x8EC629, 5), "\xE9\x4A\x02\x00\x00", 5) == 0,
          "the refusal path shows Lobby_Join_Impossible and leaves", 0x8EC4EE);
    Check(std::memcmp(image.At(0x957EC0, 3), "\x48\x8B\xD1", 3) == 0 && RipTarget(0x957EC3, 7) == 0x20B29A8 &&
              std::memcmp(image.At(0x957ECA, 5), "\xE9\x31\xB6\xE4\xFF", 5) == 0,
          "957EC0 is the text lookup with the global table", 0x957EC0);
    Check(RipTarget(0x8EFF26, 7) == 0x180A4B8 && CallTargets(image.At(0x8EFF37, 5), 0x8EFF37, 0x951AB0) &&
              RipTarget(0x926501, 7) == 0x180A4B8 && CallTargets(image.At(0x92650D, 5), 0x92650D, 0x951AB0) &&
              std::memcmp(image.At(0x180A4B8, 44), L"OnlineError_RoomError", 44) == 0,
          "both OnlineError_RoomError dialogs go through 951AB0", 0x8EFF26);
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
    // The desync meter samples the field the 5% smoothing writes. Its seven bytes end exactly where the
    // subtract begins, so it must not reach the RIP-relative multiply three bytes later - that operand
    // could not be copied into a thunk, and it is the one [Smoothing] retargets.
    const auto desyncHooks = DesyncHooks();
    Check(desyncHooks.size() == 5, "five desync hooks");
    for (const auto& hook : desyncHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
        Check(DesyncHookHandler(hook.rva) != nullptr, "and a handler for it", hook.rva);
        if (hook.rva == 0x59637C) {
            Check(hook.displacedSize == 7 && hook.rva + hook.displacedSize <= kSmoothingSite,
                  "it stops before the multiply it must not displace", hook.rva);
            // Displaced bytes are copied verbatim, so nothing RIP-relative may be among them.
            Check(hook.original[0] == 0x0F && hook.original[1] == 0x10 && hook.original[2] == 0x87,
                  "and addresses the player off rdi, not off rip", hook.rva);
        } else {
            // 77AC60 writes the arriving position into the checker and re-arms it; 77AB40 is the one
            // that takes this machine's own position instead, and is what arrivals are counted against.
            Check(hook.rva == 0x77AC60 || hook.rva == 0x77AB40 || hook.rva == 0x77AB00 ||
                      hook.rva == 0x592180,
                  "the others trace where a position comes from", hook.rva);
            if (hook.rva == 0x592180) {
                // The gate that decides whether a packet carries a position at all.
                const std::uint8_t gate[] = {0x41, 0xF6, 0xC0, 0x01, 0x74, 0x0C};  // 5921A7 test r8b,1 / je
                Check(std::memcmp(image.At(0x5921A7, sizeof(gate)), gate, sizeof(gate)) == 0,
                      "the position bit is still what admits one", 0x5921A7);
            }
            if (hook.rva == 0x77AB00) {
                const std::uint8_t store[] = {0x0F, 0x11, 0x43, 0x10};  // 77AB30 movups [rbx+0x10], xmm0
                Check(std::memcmp(image.At(0x77AB30, sizeof(store)), store, sizeof(store)) == 0,
                      "and it still lands at the checker's +0x10", 0x77AB30);
            }
            if (hook.rva == 0x77AC60) {
                const std::uint8_t arm[] = {0x66, 0xC7, 0x43, 0x21, 0x01, 0x01};  // 77AC78
                Check(hook.displacedSize == 6 &&
                          std::memcmp(image.At(0x77AC78, sizeof(arm)), arm, sizeof(arm)) == 0,
                      "and it still re-arms the checker", 0x77AC78);
            } else if (hook.rva == 0x77AB40) {
                const std::uint8_t adopt[] = {0x0F, 0x10, 0x8F, 0x90, 0x00, 0x00, 0x00};  // 77AB64
                Check(hook.displacedSize == 5 &&
                          std::memcmp(image.At(0x77AB64, sizeof(adopt)), adopt, sizeof(adopt)) == 0,
                      "and it still adopts the player's own position", 0x77AB64);
            }
        }
    }
    // The subtract between them is what makes the sampled field recoverable: it is still the game's own
    // "where they really are, minus where we draw them".
    const std::uint8_t subps[] = {0x0F, 0x5C, 0xF0};  // subps xmm6, xmm0 at 596383
    Check(std::memcmp(image.At(0x596383, sizeof(subps)), subps, sizeof(subps)) == 0,
          "the gap is still computed where the meter assumes", 0x596383);
    // Which calls are worth measuring is read from +0x1900: cleared at the top of every call, set to 1
    // only on the branch that puts the incoming position in xmm6. Without it the four objects an offline
    // mission carries report a gap of zero, because the early exit leaves xmm6 holding a constant.
    const std::uint8_t clearFlag[] = {0xC6, 0x87, 0x00, 0x19, 0x00, 0x00, 0x00};  // 596194
    const std::uint8_t setFlag[] = {0xC6, 0x87, 0x00, 0x19, 0x00, 0x00, 0x01};    // 59636A
    Check(std::memcmp(image.At(0x596194, sizeof(clearFlag)), clearFlag, sizeof(clearFlag)) == 0,
          "every call still starts by clearing the interpolated flag", 0x596194);
    Check(std::memcmp(image.At(0x59636A, sizeof(setFlag)), setFlag, sizeof(setFlag)) == 0,
          "and only the real branch still sets it, before the hook reads it", 0x59636A);
    // The early exit lands between the flag write and the hook, so the hook sees both paths.
    const std::uint8_t earlyExit[] = {0xF6, 0x87, 0x28, 0x01, 0x00, 0x00, 0x01};  // test byte [rdi+0x128], 1
    Check(std::memcmp(image.At(0x59619D, sizeof(earlyExit)), earlyExit, sizeof(earlyExit)) == 0,
          "the early exit that makes the flag necessary is still there", 0x59619D);
    // The gate diagnostic reads four things at the hook; each offset comes from one instruction, so each
    // instruction is checked. 596130 is SoldierBase::NetworkUpdate - named by the RTTI of the checkers it
    // builds at 17D2E58 (PlayerCtrlSyncChecker) and 17D2E70 (AICtrlSyncChecker).
    const std::uint8_t zeroFlags[] = {0x40, 0x88, 0x75, 0xF0};  // 59624B mov byte [rbp-0x10], sil
    Check(std::memcmp(image.At(0x59624B, sizeof(zeroFlags)), zeroFlags, sizeof(zeroFlags)) == 0,
          "the checker's output flags are still zeroed at [rbp-0x10] before the call", 0x59624B);
    const std::uint8_t checkerAt[] = {0x48, 0x8D, 0x8F, 0x30, 0x18, 0x00, 0x00};  // 59621F lea rcx,[rdi+0x1830]
    Check(std::memcmp(image.At(0x59621F, sizeof(checkerAt)), checkerAt, sizeof(checkerAt)) == 0,
          "the sync checker still lives at +0x1830 on the player", 0x59621F);
    const std::uint8_t bit1[] = {0x80, 0xE1, 0x02};  // 596328 and cl, 2
    Check(std::memcmp(image.At(0x596328, sizeof(bit1)), bit1, sizeof(bit1)) == 0,
          "the correction is still flag bit 1", 0x596328);
    const std::uint8_t tailGate[] = {0x84, 0xC9, 0x74, 0x0A};  // 596366 test cl,cl / je 596374
    Check(std::memcmp(image.At(0x596366, sizeof(tailGate)), tailGate, sizeof(tailGate)) == 0,
          "and that bit is still what decides whether the target is real", 0x596366);
    // Inside the checker runner: the two bail-outs whose inputs the diagnostic names.
    const std::uint8_t offCheck[] = {0x80, 0x39, 0x00};  // 77AD84 cmp byte [rcx], 0
    Check(std::memcmp(image.At(0x77AD84, sizeof(offCheck)), offCheck, sizeof(offCheck)) == 0,
          "the checker still bails when its own byte 0 is zero", 0x77AD84);
    const std::uint8_t stateCheck[] = {0x80, 0x79, 0x20, 0x00};  // 77ADA1 cmp byte [rcx+0x20], 0
    Check(std::memcmp(image.At(0x77ADA1, sizeof(stateCheck)), stateCheck, sizeof(stateCheck)) == 0,
          "and when its +0x20 is set", 0x77ADA1);

    // And it lands on the tail, ahead of the hook, so both paths reach the sample point.
    std::int32_t exitTo = 0;
    std::memcpy(&exitTo, image.At(0x5961A4 + 2, 4), 4);
    Check(image.At(0x5961A4, 2)[0] == 0x0F && image.At(0x5961A4, 2)[1] == 0x84 &&
              static_cast<std::uint32_t>(0x5961A4 + 6 + exitTo) == 0x596374,
          "the early exit still jumps into the smoothing tail", 0x5961A4);
    for (const auto& call : GhostCalls()) Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);

    // [Sync] PositionEveryPacket: two `sete bl` in SoldierBase slot 92, the function that picks which
    // fields a player's packet carries. Each is the whole of bit 0 (the position), so each must still sit
    // between the `cmp` of the six-frame cycle and the `add` that lays the always-sent fields on top.
    const auto positionPatches = PositionPatches();
    Check(positionPatches.size() == 2, "two position patches");
    for (const auto& patch : positionPatches) {
        Check(Matches(image.At(patch.rva, patch.original.size()), patch), patch.name, patch.rva);
        const std::uint8_t sete[] = {0x0F, 0x94, 0xC3};  // sete bl
        const std::uint8_t mov[] = {0xB3, 0x01, 0x90};   // mov bl, 1; nop - leaves the flags alone
        Check(patch.original.size() == 3 && std::memcmp(patch.original.data(), sete, 3) == 0 &&
                  std::memcmp(patch.replacement.data(), mov, 3) == 0,
              "sete bl becomes mov bl, 1", patch.rva);
    }
    const std::uint8_t cycleMain[] = {0x83, 0xFA, 0x03};         // 59FB7B cmp edx, 3  (counter % 6)
    const std::uint8_t fieldsMain[] = {0x66, 0x83, 0xC3, 0x58};  // 59FB81 add bx, 0x58
    const std::uint8_t cycleIdle[] = {0x83, 0xF9, 0x03};         // 59FBD6 cmp ecx, 3
    const std::uint8_t fieldsIdle[] = {0x66, 0x83, 0xC3, 0x48};  // 59FBDC add bx, 0x48
    Check(std::memcmp(image.At(0x59FB7B, 3), cycleMain, 3) == 0 && std::memcmp(image.At(0x59FB81, 4), fieldsMain, 4) == 0,
          "the moving player's position bit is still `counter % 6 == 3` under the other fields", 0x59FB7E);
    Check(std::memcmp(image.At(0x59FBD6, 3), cycleIdle, 3) == 0 && std::memcmp(image.At(0x59FBDC, 4), fieldsIdle, 4) == 0,
          "and so is the one that sends no movement", 0x59FBD9);
    // bl is a whole byte only because ebx starts at zero, and both branches are the player's side of the
    // vtable+0x108 test (550890: +0x340 or +0x1ED0 set); the AI branch at 59FC3A is left as it is.
    const std::uint8_t start[] = {0x33, 0xDB, 0xFF, 0x90, 0x08, 0x01, 0x00, 0x00};  // xor ebx,ebx; call [rax+0x108]
    Check(std::memcmp(image.At(0x59FB20, sizeof(start)), start, sizeof(start)) == 0,
          "59FB10 starts from ebx = 0 and asks slot +0x108 which side it is on", 0x59FB20);
    for (const std::uint32_t vtable : {0x17D24D8u, 0x17CDF28u, 0x17CF100u, 0x17CF5B8u})
        Check(SlotTargets(image.At(vtable + 0x2E0, 8), image.nt->OptionalHeader.ImageBase, 0x59FB10) &&
                  SlotTargets(image.At(vtable + 0x108, 8), image.nt->OptionalHeader.ImageBase, 0x550890),
              "SoldierBase, Ranger, Air Raider and Fencer pick their fields with 59FB10", vtable);
    // Wing Diver adds her own field on top of the same function.
    Check(SlotTargets(image.At(0x17D0FF8 + 0x2E0, 8), image.nt->OptionalHeader.ImageBase, 0x582DF0) &&
              CallTargets(image.At(0x582DFD, 5), 0x582DFD, 0x59FB10),
          "Wing Diver's slot 92 calls 59FB10 first", 0x582DFD);
    // Where those fields go: 59FA80 counts the build (5781B0: inc [NetworkObject+0x1700] = player+0x1820),
    // asks slot 92, writes the flags and calls slot 52, whose bit 0 writes the player's +0x90.
    Check(CallTargets(image.At(0x59FA95, 5), 0x59FA95, 0x5781B0) &&
              std::memcmp(image.At(0x5781B0, 6), "\xFF\x81\x00\x17\x00\x00", 6) == 0,
          "each packet build counts itself at player+0x1820", 0x59FA95);
    Check(std::memcmp(image.At(0x59FAAC, 6), "\xFF\x90\xE0\x02\x00\x00", 6) == 0 &&
              std::memcmp(image.At(0x59FADA, 7), "\x41\xFF\x91\xA0\x01\x00\x00", 7) == 0,
          "and asks slot 92 for the fields, then slot 52 to write them", 0x59FAAC);
    Check(std::memcmp(image.At(0x59FDEA, 6), "\x41\xF6\xC0\x01\x74\x0F", 6) == 0 &&
              CallTargets(image.At(0x59FDFA, 5), 0x59FDFA, 0x77AD00) &&
              std::memcmp(image.At(0x77AD03, 7), "\x49\x8D\x90\x90\x00\x00\x00", 7) == 0,
          "bit 0 is what writes the player's position (+0x90) into the packet", 0x59FDEA);

    // [Sync] FacingEveryPacket: bit 1 (the angles) is put in by `or ax, 2` and taken back out by the
    // `cmovne ax, bx` that follows the `counter % 6` (or % 2) division; that cmov becomes a nop.
    const auto facingPatches = FacingPatches();
    Check(facingPatches.size() == 1, "one facing patch");
    for (const auto& patch : facingPatches) {
        Check(Matches(image.At(patch.rva, patch.original.size()), patch), patch.name, patch.rva);
        const std::uint8_t cmov[] = {0x66, 0x0F, 0x45, 0xC3};  // cmovne ax, bx
        const std::uint8_t nop4[] = {0x0F, 0x1F, 0x40, 0x00};  // nop dword [rax+0]
        Check(patch.original.size() == 4 && std::memcmp(patch.original.data(), cmov, 4) == 0 &&
                  std::memcmp(patch.replacement.data(), nop4, 4) == 0,
              "cmovne ax, bx becomes a four-byte nop", patch.rva);
    }
    const std::uint8_t anglesIn[] = {0x0F, 0xB7, 0xC3, 0x66, 0x83, 0xC8, 0x02, 0x85, 0xD2};  // 59FB91
    const std::uint8_t anglesKept[] = {0x0F, 0xB7, 0xD8};                                    // 59FB9E movzx ebx, ax
    Check(std::memcmp(image.At(0x59FB91, sizeof(anglesIn)), anglesIn, sizeof(anglesIn)) == 0 &&
              std::memcmp(image.At(0x59FB9E, sizeof(anglesKept)), anglesKept, sizeof(anglesKept)) == 0,
          "movzx eax,bx; or ax,2; test edx,edx still surround it, and ebx takes ax after", 0x59FB9A);
    Check(std::memcmp(image.At(0x59FB8F, 2), "\xF7\xF9", 2) == 0,
          "and edx is the remainder of the idiv just before", 0x59FB8F);

    // [MultiSlot] JoinRetrySeconds: the hello count Link::OnInitial compares before it restarts a link.
    const auto joinRetryPatches = JoinRetryPatches(10);
    Check(joinRetryPatches.size() == 1 && JoinRetryPatches(5).empty() && JoinRetryPatches(4).empty() &&
              JoinRetryPatches(61).empty() && JoinRetryPatches(60).size() == 1,
          "a join retry patch only for 6..60 s");
    for (const auto& patch : joinRetryPatches) {
        Check(Matches(image.At(patch.rva, patch.original.size()), patch), patch.name, patch.rva);
        Check(patch.original[6] == 10 && patch.replacement[6] == 20 && patch.original.size() == 9 &&
                  std::memcmp(patch.original.data(), patch.replacement.data(), 6) == 0 &&
                  std::memcmp(patch.original.data() + 7, patch.replacement.data() + 7, 2) == 0,
              "only the immediate of cmp [rdi+0x90], 10 changes, to 20", patch.rva);
    }
    Check(JoinRetryPatches(60)[0].replacement[6] == 120, "60 s is 120 hellos, still a positive imm8");
    // Around it: the count goes up by one per hello (12D5CEC), jle skips the restart (to 12D5D1D, the send),
    // the restart is Close then Accept, every 500 ms (the movss at 12D5CD2 reloads 500.0 from 1765A7C).
    Check(std::memcmp(image.At(0x12D5CEC, 6), "\xFF\x87\x90\x00\x00\x00", 6) == 0, "inc dword [rdi+0x90] just before", 0x12D5CEC);
    Check(CallTargets(image.At(0x12D5D0B, 5), 0x12D5D0B, 0x12C7BE0) && CallTargets(image.At(0x12D5D18, 5), 0x12D5D18, 0x12C7180) &&
              CallTargets(image.At(0x12D5D4B, 5), 0x12D5D4B, 0x12C8F50),
          "the restart closes (12C7BE0) and accepts (12C7180); the jle lands before the hello send (12C8F50)", 0x12D5D0B);
    Check(RipTarget(0x12D5CD2, 8) == 0x1765A7C, "the hello interval is reloaded from 1765A7C", 0x12D5CD2);
    {
        float interval = 0.0f;
        std::memcpy(&interval, image.At(0x1765A7C, 4), 4);
        Check(interval == 500.0f, "and it is 500 ms", 0x1765A7C);
    }

    // Hand aim (handaim.h) rests on the game dropping a packet that starts 00 00 'M' 'S' unread, and on these:
    // 1. The receive loops ask EOS for every channel (RequestedChannel = null) and hand each packet to the
    //    subscribers at manager+0x1F0 (12C71F0) without looking at the channel.
    Check(std::memcmp(image.At(0x12C8D1D, 13), "\xC7\x44\x24\x60\x00\x10\x00\x00\x48\x89\x74\x24\x68", 13) == 0 &&
              std::memcmp(image.At(0x12C8CED, 2), "\x33\xF6", 2) == 0 && RipTarget(0x12C8D84, 6) == 0x1755058 &&
              CallTargets(image.At(0x12C8DB5, 5), 0x12C8DB5, 0x12C71F0),
          "12C8CC0 receives with MaxDataSizeBytes 0x1000 and RequestedChannel null (rsi = 0)", 0x12C8D1D);
    Check(std::memcmp(image.At(0x12C9CFD, 6), "\x33\xFF\x48\x89\x7D\xDF", 6) == 0 && RipTarget(0x12C9D5A, 6) == 0x1755058 &&
              CallTargets(image.At(0x12C9D91, 5), 0x12C9D91, 0x12C71F0),
          "12C9CA0 likewise (RequestedChannel = rdi = 0)", 0x12C9CFD);
    // 2. Only two subscribers are ever added (12D2FD0): packet::Controller (12D27E0) and the join handshake
    //    (12D5EBE), whose std::function calls land in 12D2540 and 12D5570.
    Check(CallTargets(image.At(0x12D27E0, 5), 0x12D27E0, 0x12D2FD0) && CallTargets(image.At(0x12D5EBE, 5), 0x12D5EBE, 0x12D2FD0),
          "the two subscriptions", 0x12D27E0);
    Check(std::memcmp(image.At(0x12D32A0, 9), "\x48\x83\xC1\x08\xE9\x97\xF2\xFF\xFF", 9) == 0 &&
              std::memcmp(image.At(0x12D6050, 9), "\x48\x83\xC1\x08\xE9\x17\xF5\xFF\xFF", 9) == 0,
          "their calls jump to 12D2540 and 12D5570", 0x12D32A0);
    // 3. packet::Controller skips a packet under 8 bytes or whose bytes 0 and 1 are both zero.
    Check(std::memcmp(image.At(0x12D2564, 10), "\x83\x7A\x10\x08\x0F\x82\xA8\x00\x00\x00", 10) == 0 &&
              std::memcmp(image.At(0x12D25A2, 11), "\x80\x3F\x00\x75\x09\x80\x7F\x01\x00\x74\x2C", 11) == 0,
          "12D2540: size < 8 or bytes 0 and 1 zero -> 12D25D9 (nothing)", 0x12D25A2);
    // 4. The join handshake reads only a packet from its own peer whose first dword is zero.
    Check(std::memcmp(image.At(0x12D5606, 9), "\x83\x3A\x00\x0F\x85\x62\x01\x00\x00", 9) == 0,
          "12D5570: first dword not zero -> 12D5771 (nothing)", 0x12D5606);
    // 5. The game's data goes out through 12C8BC0, whose SendPacket returns to 12C8C5A (the hook's cue), on
    //    channel 0 - the hand aim channel is not one the game uses.
    Check(RipTarget(0x12C8C54, 6) == 0x1755050 && std::memcmp(image.At(0x12C8C3B, 5), "\xC6\x44\x24\x40\x00", 5) == 0 &&
              kHandAimChannel != 0,
          "12C8BC0 sends on channel 0 and returns to 12C8C5A", 0x12C8C54);
    // 6. A player's soldier holds its room user: CreateOnlinePlayer stores the shared_ptr<eos::User> it found
    //    in the room's slot vector (12B9E80) at soldier+0x1ED0, and User+0x18 is the ProductUserId.
    Check(CallTargets(image.At(0x5911D3, 5), 0x5911D3, 0x12B9E80) &&
              std::memcmp(image.At(0x591254, 7), "\x49\x89\x87\xD0\x1E\x00\x00", 7) == 0,
          "591130 stores the user at soldier+0x1ED0", 0x591254);

    // 7. Without EDF6VR the plugin turns another player's rapid-fire catch-up shot (690420) itself: hooks on the
    //    instruction before and after each `call 696FD0`, never on the call EDF6VR redirects.
    const auto catchUpHooks = HandAimCatchUpHooks();
    Check(catchUpHooks.size() == 8, "six catch-up hooks and the per-shot receive's two");
    // The paced replay: 691EAB (weapon tick 6934F0 -> 691DC0) sends a remote count-synced weapon to 6947E0 with dl = 0.
    Check(CallTargets(image.At(0x6943DD, 5), 0x6943DD, 0x691DC0) &&
              std::memcmp(image.At(0x691F81, 5), "\xE9\x5A\x28\x00\x00", 5) == 0 &&
              std::memcmp(image.At(0x691F8D, 5), "\xE9\x4E\x28\x00\x00", 5) == 0,
          "the weapon tick reaches the paced replay 6947E0", 0x691F81);
    // The per-shot receive (692540): rsi holds the weapon from 69255F to the call at 692A24 (`mov rcx, rsi` at 692A21).
    Check(std::memcmp(image.At(0x69255F, 3), "\x48\x8B\xF1", 3) == 0 && std::memcmp(image.At(0x692A21, 3), "\x48\x8B\xCE", 3) == 0 &&
              CallTargets(image.At(0x692A24, 5), 0x692A24, kCatchUpFire) && kPerShotReceive == 0x692A12,
          "the per-shot receive's weapon is rsi at 692A12", 0x692A12);
    // Its muzzle is rdi: the index modulo +0x1E0 (692582 div, 692589 `mov rdi, rdx`), kept at [rbp-0x21] across the
    // loop that reuses rdi (6929D5 puts it back), and the call's edx (692A1F `mov edx, edi`). 698500 rebuilds that
    // muzzle (6925C0); 7606A0 then writes the message's muzzle over it only in mode 0 or 1 (6925D6..6925F0), so a
    // weapon in any other mode fires from the rebuilt muzzle along this machine's aim - the one the plugin turns.
    Check(std::memcmp(image.At(0x692582, 10), "\x48\xF7\xB6\xE0\x01\x00\x00\x48\x8B\xFA", 10) == 0 &&
              std::memcmp(image.At(0x6929D5, 4), "\x48\x8B\x7D\xDF", 4) == 0 &&
              std::memcmp(image.At(0x692A1F, 2), "\x8B\xD7", 2) == 0 &&
              CallTargets(image.At(0x6925C0, 5), 0x6925C0, 0x698500) &&
              std::memcmp(image.At(0x6925D6, 15), "\x8B\x96\x40\x15\x00\x00\x85\xD2\x74\x07\x83\xFA\x01\x75\x10", 15) == 0 &&
              CallTargets(image.At(0x6925F0, 5), 0x6925F0, 0x7606A0),
          "the per-shot receive fires muzzle rdi, from the message only in mode 0 or 1", 0x6925D6);
    // Between the turn and the call only the call's arguments, and the put-back is right after it.
    Check(std::memcmp(image.At(kPerShotReceive, 0x12), "\xC6\x44\x24\x20\x01\x4C\x8D\x4D\x7F\x4C\x8B\x45\xC7\x8B\xD7\x48\x8B\xCE", 0x12) == 0 &&
              kPerShotCall == kPerShotReceive + 0x12 && kPerShotAfter == kPerShotCall + 5 &&
              std::memcmp(image.At(0x692A32, 2), "\x7D\x06", 2) == 0,
          "after the per-shot turn only the call's arguments; the put-back's cmp feeds the jge at 692A32", kPerShotCall);
    for (const auto& hook : catchUpHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify) && HandAimCatchUpHandler(hook.rva) != nullptr,
              hook.name, hook.rva);
    }
    // 690420's sites put the turn on the 7-byte `lea r9, [rbx+0xBD0]` 0x14 before the call; the paced replay's on the
    // 5-byte `mov byte [rsp+0x20], 1` 0xD before it. The put-back is the instruction right after each call.
    const auto kAfterOk = [&](int i) {
        const std::uint32_t distance = kCatchUpCalls[i] - kCatchUpBefore[i];
        return (distance == 0x14 || (i == 2 && distance == 0xD)) && kCatchUpAfter[i] == kCatchUpCalls[i] + 5;
    };
    for (int i = 0; i < kCatchUpSites; ++i) {
        Check(CallTargets(image.At(kCatchUpCalls[i], 5), kCatchUpCalls[i], kCatchUpFire), "the catch-up loop calls 696FD0",
              kCatchUpCalls[i]);
        Check(kAfterOk(i), "the hooks sit right around the call", kCatchUpCalls[i]);
        // 6969A0 rebuilds the muzzle 0x14 bytes before the turn; between the turn and the call only the call's
        // arguments - `mov byte [rsp+0x20], 1; xor r8d, r8d; xor edx, edx; mov rcx, rbx` - so the shot is muzzle 0
        // of the weapon in rbx and nothing rebuilds the muzzle after the turn.
        Check(CallTargets(image.At(kCatchUpBefore[i] - 0x14 + 0x03, 5), kCatchUpBefore[i] - 0x14 + 0x03, 0x6969A0) ||
                  CallTargets(image.At(kCatchUpBefore[i] - 0x11, 5), kCatchUpBefore[i] - 0x11, 0x6969A0) ||
                  CallTargets(image.At(kCatchUpBefore[i] - 0x14, 5), kCatchUpBefore[i] - 0x14, 0x6969A0),
              "6969A0 rebuilds the muzzle before the turn", kCatchUpBefore[i]);
        const unsigned char* arguments = image.At(kCatchUpCalls[i] - 13, 13);
        Check(std::memcmp(arguments, "\xC6\x44\x24\x20\x01\x45\x33\xC0\x33\xD2\x48\x8B\xCB", 13) == 0,
              "after the turn only the call's arguments", kCatchUpCalls[i]);
    }
    Check(std::memcmp(image.At(0x59B428, 14), "\x48\x8B\xBB\x50\x19\x00\x00\x48\x8B\x83\x60\x19\x00\x00", 14) == 0 &&
              kOwnedWeapons == 0x1950 && kOwnedWeaponCount == 0x1960,
          "the soldier's weapon loop reads its list at +0x1950, count +0x1960", 0x59B428);
    Check(std::memcmp(image.At(0x69054C, 7), "\x48\x8B\x8B\x20\x01\x00\x00", 7) == 0 && kWeaponOwner == 0x120,
          "690420 itself reads the weapon's owner at +0x120", 0x69054C);
    Check(std::memcmp(image.At(0x68CCFA, 7), "\x0F\x11\x86\x50\x03\x00\x00", 7) == 0 &&
              std::memcmp(image.At(0x691943, 7), "\x0F\x10\x87\x50\x03\x00\x00", 7) == 0 &&
              CallTargets(image.At(0x6976CA, 5), 0x6976CA, 0x691560) && kWeaponFireVector == 0x350,
          "+0x350 is FireVector (68CCFA), read by the shot's setup 691560 that 696FD0 calls", 0x68CCFA);

    // Seat log (ridelog.h): three read-only hooks, and what their handlers take from the registers.
    const auto rideHooks = RideLogHooks();
    Check(rideHooks.size() == 4, "four seat log hooks");
    // 6314A0, the seat bookkeeping every vehicle runs in its update (62EEC0 tail-jumps there at 62F1F2): rcx is the vehicle
    // (rdi = rcx at 6314B7; its seat count [rdi+0x618] at 6314DC), and the hook is its very first instruction.
    Check(std::memcmp(image.At(0x6314B7, 3), "\x48\x8B\xF9", 3) == 0 &&
              std::memcmp(image.At(0x6314DC, 7), "\x48\x39\xB7\x18\x06\x00\x00", 7) == 0 &&
              std::memcmp(image.At(0x62F1F2, 5), "\xE9\xA9\x22\x00\x00", 5) == 0 &&  // jmp 6314A0
              kVehicleUpdate == 0x6314A0,
          "a vehicle's update: rcx the vehicle", kVehicleUpdate);
    for (const auto& hook : rideHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify) && RideLogHookHandler(hook.rva) != nullptr,
              hook.name, hook.rva);
    }
    // 5763E0: rdi = the soldier (576407), rbp = its vehicle [+0x1548] (576459), eax = 62D810's index of its seat
    // [+0x1540] among the vehicle's (+0x608, 0x340 each, +0x618 of them), -1 for none; then the broadcast.
    Check(std::memcmp(image.At(0x576407, 3), "\x48\x8B\xF9", 3) == 0 &&
              std::memcmp(image.At(0x576459, 7), "\x48\x8B\xAF\x48\x15\x00\x00", 7) == 0 &&
              std::memcmp(image.At(0x5764A1, 7), "\x48\x8B\x97\x40\x15\x00\x00", 7) == 0 &&
              CallTargets(image.At(0x5764AB, 5), 0x5764AB, 0x62D810) &&
              std::memcmp(image.At(0x62D810, 18), "\x4C\x69\x89\x18\x06\x00\x00\x40\x03\x00\x00\x48\x8B\x81\x08\x06\x00\x00", 18) == 0 &&
              kRideSend == 0x5764B0 && kVehicleSeats == 0x608 && kVehicleSeatCount == 0x618,
          "our seat: rdi soldier, rbp vehicle, eax its seat index (62D810)", kRideSend);
    // 5774C0's ride case: r12d = the seat byte (5775CE), the vehicle's shared_ptr at [rsp+0x70] (57764C), and
    // rsi - 0x120 the soldier 5765E0 is called for (57765A, 577661).
    Check(std::memcmp(image.At(0x5775CE, 4), "\x44\x0F\xBE\xE0", 4) == 0 &&
              std::memcmp(image.At(0x57764C, 6), "\x66\x0F\x7F\x44\x24\x70", 6) == 0 &&
              std::memcmp(image.At(0x57765A, 7), "\x48\x8D\x8E\xE0\xFE\xFF\xFF", 7) == 0 &&
              CallTargets(image.At(0x577661, 5), 0x577661, 0x5765E0),
          "their seat: r12d seat, [rsp+0x70] vehicle, rsi-0x120 soldier", kRideReceive);
    // 6325B0's case 4: rbx = this (vehicle+0x120, 6325C6), r14d = the seat byte (632651), r15d = the counter
    // (632671), rsi = the soldier (632690); then the counter, the sign and the count [rbx+0x4F8] are checked.
    Check(std::memcmp(image.At(0x6325C6, 3), "\x48\x8B\xD9", 3) == 0 &&
              std::memcmp(image.At(0x632651, 4), "\x44\x0F\xBE\xF0", 4) == 0 &&
              std::memcmp(image.At(0x632671, 3), "\x44\x8B\xF8", 3) == 0 &&
              std::memcmp(image.At(0x632690, 4), "\x48\x8B\x75\xC0", 4) == 0 &&
              std::memcmp(image.At(0x6326A0, 14), "\x7F\x75\x45\x85\xF6\x78\x70\x44\x3B\xB3\xF8\x04\x00\x00", 14) == 0 &&
              std::memcmp(image.At(0x63262B, 7), "\x48\x8D\x8B\xE0\xFE\xFF\xFF", 7) == 0 &&
              kSoldierRideCounter == 0x1824,
          "a seat request: rbx vehicle+0x120, r14d seat, r15d counter, rsi soldier, refused outside 0..[+0x618]", kRideRequest);

    // Packet sizes (packetsize.h): the hook at 12CFFD0's entry, and the three facts the log lines state.
    const auto packetHooks = PacketSizeHooks();
    Check(packetHooks.size() == 1, "one packet size hook");
    for (const auto& hook : packetHooks) {
        const Patch verify{hook.name, hook.rva, hook.original, hook.original};
        Check(Matches(image.At(hook.rva, hook.original.size()), verify), hook.name, hook.rva);
        Check(PacketSizeHookHandler(hook.rva) != nullptr, "and a handler for it", hook.rva);
    }
    // 1. The game closes a packet at 1100 bytes and flushes before a message that would pass it - but the
    //    message itself is copied whole whatever its length (no split).
    Check(std::memcmp(image.At(0x12D0017, 7), "\x48\x81\xFA\x4C\x04\x00\x00", 7) == 0 &&
              CallTargets(image.At(0x12D0020, 5), 0x12D0020, 0x12CEA10),
          "12CFFD0 flushes at 1100 bytes (cmp rdx, 0x44C; call 12CEA10)", 0x12D0017);
    // 2. The only data send reacts to EOS_NoConnection alone; every other result is ignored.
    Check(RipTarget(0x12C8C54, 6) == 0x1755050 && std::memcmp(image.At(0x12C8C5A, 3), "\x83\xF8\x01", 3) == 0,
          "12C8BC0 calls EOS_P2P_SendPacket and checks only for result 1", 0x12C8C54);
    // 3. MissionSync_Res writes every member's loadout record into the message it sends.
    Check(CallTargets(image.At(0x78D6FA, 5), 0x78D6FA, 0x773840) && InSyncFunctions(0x78D6FF) &&
              !InSyncFunctions(0x78830B),
          "MissionSync_Res (inside the sync range) writes records with 773840; the per-frame object loop is outside",
          0x78D6FA);
    // The stack walk's call test, on real return addresses and on ones that are not.
    for (const std::uint32_t after : {0x12D0025u, 0x12C8C5Au, 0x78830Bu, 0x59FAE1u, 0x78D6FFu})
        Check(FollowsCall(image.At(after - 7, 7)), "a real return address is recognised", after);
    for (const std::uint32_t after : {0x12CFFDBu, 0x12D0017u, 0x12C8C5Du, 0x59FB7Eu})
        Check(!FollowsCall(image.At(after - 7, 7)), "an address after no call is not", after);
    // The start sync's records (1.5.34): four calls into the record writer/reader, and the two position fields
    // the wrappers read before and after.
    const auto sortieCalls = SortieRecordCalls();
    Check(sortieCalls.size() == 4, "four start sync record calls");
    for (const auto& call : sortieCalls) {
        Check(CallTargets(image.At(call.rva, 5), call.rva, call.target), call.name, call.rva);
        Check(SortieRecordCallHandler(call.rva) != nullptr, "and a wrapper for it", call.rva);
        Check(InSyncFunctions(call.rva), "inside the sync functions", call.rva);
    }
    Check(std::memcmp(image.At(0x12B5404, 7), "\x49\x8B\x80\xF0\x05\x00\x00", 7) == 0,
          "the packet writer keeps its position at +0x5F0 (mov rax, [r8+0x5F0])", 0x12B5404);
    Check(std::memcmp(image.At(0x12B49DF, 4), "\x4C\x8B\x59\x08", 4) == 0 &&
              std::memcmp(image.At(0x12B49EC, 6), "\x45\x0F\xB6\x54\x0B\x10", 6) == 0,
          "the packet reader keeps its position at +8 and its bytes from +0x10", 0x12B49DF);
    Check(std::memcmp(image.At(0x773826, 2), "\xB0\x01", 2) == 0, "the record reader returns true", 0x773826);
    Check(ClassifyMessage(1092) == MessageFit::Shares && ClassifyMessage(1093) == MessageFit::OwnPacket &&
              ClassifyMessage(1162) == MessageFit::OwnPacket && ClassifyMessage(1163) == MessageFit::Dropped,
          "message size classes: 1092 shares a packet, 1093-1162 need their own, 1163 and up are dropped");

    std::vector<CallSite> allCalls = calls;
    allCalls.insert(allCalls.end(), missionCalls.begin(), missionCalls.end());
    const auto ghostCalls = GhostCalls();
    allCalls.insert(allCalls.end(), ghostCalls.begin(), ghostCalls.end());
    const auto diagnosticCalls = DiagnosticCalls();
    allCalls.insert(allCalls.end(), diagnosticCalls.begin(), diagnosticCalls.end());
    const auto recoveryCalls = RecoveryCalls();
    allCalls.insert(allCalls.end(), recoveryCalls.begin(), recoveryCalls.end());
    allCalls.insert(allCalls.end(), versionCalls.begin(), versionCalls.end());
    allCalls.insert(allCalls.end(), sortieCalls.begin(), sortieCalls.end());
    auto allHooks = missionHooks;
    allHooks.insert(allHooks.end(), spawnHooks.begin(), spawnHooks.end());
    const auto diagnosticHooks = DiagnosticHooks();
    allHooks.insert(allHooks.end(), diagnosticHooks.begin(), diagnosticHooks.end());
    allHooks.insert(allHooks.end(), versionHooks.begin(), versionHooks.end());
    allHooks.insert(allHooks.end(), armorHooks.begin(), armorHooks.end());
    const auto ghostHooks = GhostHooks();
    allHooks.insert(allHooks.end(), ghostHooks.begin(), ghostHooks.end());
    allHooks.insert(allHooks.end(), desyncHooks.begin(), desyncHooks.end());
    allHooks.insert(allHooks.end(), packetHooks.begin(), packetHooks.end());
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
            // 1.6.0: enemies grow stronger past four players, which 1.2.6-1.5.38 do not do.
            Check(mirrored < 0x54 || mirrored > 0x57, "MultiSlot 1.2.6-1.5.38 (join check 0x54..0x57) refuses it", hook.rva);
            Check(mirrored < 0x54, "MultiSlot 1.2.6-1.5.38 searches ([0x54, 0x94] at most) do not list it", hook.rva);
            Check(mirrored < 0x4C || mirrored > 0x4F, "the offline ten-player experiment (centre 0x70) is not this family", hook.rva);
            // 1.6.6: HARDEST and INFERNO enemies hit harder past four players, which 1.6.0-1.6.5 do not do.
            Check(mirrored < 0x44 || mirrored > 0x47, "MultiSlot 1.6.0-1.6.5 (join check 0x44..0x47) refuses it", hook.rva);
            Check(mirrored < 0x44, "MultiSlot 1.6.0-1.6.5 searches ([0x44, 0x94] at most) do not list it", hook.rva);
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
    all.insert(all.end(), positionPatches.begin(), positionPatches.end());
    all.insert(all.end(), facingPatches.begin(), facingPatches.end());
    all.insert(all.end(), joinRetryPatches.begin(), joinRetryPatches.end());
    allHooks.insert(allHooks.end(), hostHooks.begin(), hostHooks.end());
    allHooks.insert(allHooks.end(), catchUpHooks.begin(), catchUpHooks.end());
    allHooks.insert(allHooks.end(), rideHooks.begin(), rideHooks.end());
    auto spans = WriteSpans(all, allCalls, allHooks);
    // The remote-player correction factor is built at load (its operand depends on where the constant
    // lands), so it is not in the tables above - but it is still a write into EDF.dll and has to keep
    // clear of EDF6VR like every other one. The package ships both mods together.
    spans.insert({kSmoothingSite, kSmoothingSite + 7});
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
