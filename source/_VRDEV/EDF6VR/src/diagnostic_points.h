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
unsigned OwnedWeaponCount(void* soldier) noexcept;
void* OwnedWeaponAt(void* soldier,unsigned index) noexcept;
// One diagnostic allowance against initial point-reload debt, once per owner/
// mission. Native reload completion grants readiness. Never refills a used call,
// never writes ammo, and never modifies the shared cumulative credit ledger.
int GrantInitialRequestPoints(const ImageProfile&,void* localSoldier,int initialBudget,
    RequestPointBudget&,bool& allocated) noexcept;
}
