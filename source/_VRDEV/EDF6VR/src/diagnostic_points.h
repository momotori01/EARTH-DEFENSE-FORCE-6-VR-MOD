#pragma once
#include "image_profile.h"
namespace edf6vr {
struct RequestPointBudget {
    void* owner=nullptr;
    void* ledger=nullptr;
    unsigned ownerId=0;
    int remaining=0;
    float lastTotal=0;
    void* weapons=nullptr;
    unsigned ownedCount=0;
    unsigned requests=0, alreadyReady=0, credited=0;
    bool applied=false;
};
bool CheckRequestPointProfile(const ImageProfile&) noexcept;
// The weapon tick's `sub [rsi+0xE68],eax; movss [rsi+0xE80],xmm6` (693F18), as
// the game has it or with its first instruction hooked the way EDF6MultiSlot
// 1.6.9 hooks it (the Air Raider's credit with five or more players): a jump
// to a thunk, NOP-padded to the instruction's six bytes, which runs the
// instruction itself. Byte for byte the hooked form was refused, and with it
// the allowance, on every install with the bundled MultiSlot.
bool CreditConsumeSite(const unsigned char* at) noexcept;
unsigned OwnedWeaponCount(void* soldier) noexcept;
void* OwnedWeaponAt(void* soldier,unsigned index) noexcept;
// One diagnostic allowance against initial point-reload debt, once per owner/
// mission. Native reload completion grants readiness. Never refills a used call,
// never writes ammo, and never modifies the shared cumulative credit ledger.
int GrantInitialRequestPoints(const ImageProfile&,void* localSoldier,int initialBudget,
    RequestPointBudget&,bool& allocated) noexcept;
}
