#include "cockpit.h"
#include <cstring>
#include <iterator>
namespace edf6vr {
namespace {
#include "cockpit_occlusion.inc"
#include "cockpit_crawler_occlusion.inc"
#include "cockpit_barga_occlusion.inc"
#include "cockpit_proteus_occlusion.inc"
#include "cockpit_proteus_driver_occlusion.inc"
#include "cockpit_proteus_missile_occlusion.inc"
#include "cockpit_tank_occlusion.inc"
#include "cockpit_titan_gunner_occlusion.inc"
#include "cockpit_combat_negling_occlusion.inc"
#include "cockpit_combat_grape_occlusion.inc"
#include "cockpit_combat_caliban_occlusion.inc"
#include "cockpit_heli_nereid_occlusion.inc"
#include "cockpit_heli_602_occlusion.inc"
#include "cockpit_heli_506_occlusion.inc"
#include "cockpit_heli_brute_occlusion.inc"
#include "cockpit_brute_gunner_occlusion.inc"
#include "cockpit_nix_chest_occlusion.inc"
#include "cockpit_truck_pickup_occlusion.inc"
struct Bake {const unsigned char* data;std::size_t size;std::uint64_t hash;};
// One entry per CockpitKind, in enum order. A stale or missing bake simply
// fails the hash and the cabin renders with flat visibility.
constexpr Bake bakes[]={{cockpitVisibility,sizeof(cockpitVisibility),cockpitVisibilityHash},
    {crawlerVisibility,sizeof(crawlerVisibility),crawlerVisibilityHash},
    {bargaVisibility,sizeof(bargaVisibility),bargaVisibilityHash},
    {proteusVisibility,sizeof(proteusVisibility),proteusVisibilityHash},
    {proteusDriverVisibility,sizeof(proteusDriverVisibility),proteusDriverVisibilityHash},
    {proteusMissileVisibility,sizeof(proteusMissileVisibility),proteusMissileVisibilityHash},
    {tankVisibility,sizeof(tankVisibility),tankVisibilityHash},
    {titanGunnerVisibility,sizeof(titanGunnerVisibility),titanGunnerVisibilityHash},
    {combatNeglingVisibility,sizeof(combatNeglingVisibility),combatNeglingVisibilityHash},
    {combatGrapeVisibility,sizeof(combatGrapeVisibility),combatGrapeVisibilityHash},
    {combatCalibanVisibility,sizeof(combatCalibanVisibility),combatCalibanVisibilityHash},
    {heliNereidVisibility,sizeof(heliNereidVisibility),heliNereidVisibilityHash},
    {heli602Visibility,sizeof(heli602Visibility),heli602VisibilityHash},
    {heli506Visibility,sizeof(heli506Visibility),heli506VisibilityHash},
    {heliBruteVisibility,sizeof(heliBruteVisibility),heliBruteVisibilityHash},
    {bruteGunnerVisibility,sizeof(bruteGunnerVisibility),bruteGunnerVisibilityHash},
    {nixChestVisibility,sizeof(nixChestVisibility),nixChestVisibilityHash},
    {truckPickupVisibility,sizeof(truckPickupVisibility),truckPickupVisibilityHash}};
static_assert(std::size(bakes)==kCockpitKinds);
}
bool ApplyCockpitOcclusion(std::vector<CockpitVertex>& mesh,CockpitKind kind) noexcept {
    const auto index=static_cast<unsigned>(kind);if(index>=kCockpitKinds)return false;
    const auto* visibility=bakes[index].data;
    const auto size=bakes[index].size;
    const auto expected=bakes[index].hash;
    if(mesh.size()*2!=size)return false;
    std::uint64_t hash=14695981039346656037ull;
    for(const auto& v:mesh) {
        unsigned char bytes[24];std::memcpy(bytes,v.position,12);std::memcpy(bytes+12,v.normal,12);
        for(auto b:bytes){hash^=b;hash*=1099511628211ull;}
    }
    if(hash!=expected)return false;
    for(std::size_t i=0;i<mesh.size();++i)for(unsigned s=0;s<2;++s)
        mesh[i].visibility[s]=visibility[i*2+s]/255.f;
    return true;
}
}
