#pragma once
#include "cockpit.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The other crew member of the Proteus's tandem cabin, as that player's own
// soldier, seated (the user, 2026-10-01): the class's body model, posed with
// the game's own seated pose (the Grape's rear seat, `vehicle_striker_rear`,
// frame 0), set on the other seat and its arms bent so the hands are on that
// seat's controls, then painted with the player's two colours. The data are
// generated from the player's own Root.cpk by tools/edf6/crew_figures.py into
// Mods/Plugins/EDF6VRCrew; nothing of the game ships with the mod.
namespace edf6vr {
// A class's four looks, in kCrewModels order; kind 1 Ranger, 2 Wing Diver,
// 3 Air Raider, 4 Fencer (SoldierClassOf).
const char* CrewModelName(unsigned kind,unsigned model) noexcept;
const char* CrewFamilyName(unsigned kind) noexcept;
// The figure's two seats in the tandem cabin: the driver's (front) and the
// missile operator's (rear, kProteusCabinRearSeat from the driver's). A
// driver's cabin shows the rear seat's figure, and the other way round.
enum class CrewSeat : unsigned { Driver, Rear };
struct CrewFigurePart {
    std::string albedo,normal,param,mask;   // texture files in the folder ("" when missing)
    std::array<float,4> colour0{1,1,1,1},colour1{1,1,1,1};   // the material's own change colours
    unsigned first=0,count=0;               // index range
};
struct CrewFigure {
    std::vector<CockpitVertex> vertices;    // cabin frame of the build it is drawn in, surface 25
    std::vector<std::uint32_t> indices;
    std::vector<CrewFigurePart> parts;
    // Where the posing put things (cabin frame), for tests and the log: the
    // hands' grip (the hole the curled fingers make) against the grip's
    // centre, the feet's ankles against where the footrest wants them.
    std::array<float,3> hips{},head{},wrist[2]{},target[2]{},ankle[2]{},footTarget[2]{};
    float lean=0;   // the upper body's forward lean to reach the controls (radians)
    float shift=0;  // how far forward on the seat it sits to reach them (metres)
    float gripClearance[2]{};   // the hand's nearest approach to each grip's surface (negative: inside it)
    std::string gripWorst[2];   // the bone carrying that nearest vertex
    float wristRoll[2]{};       // the hand's roll against the forearm, shared out along its twist bones
    float fingerClose[2][5]{};  // how far each finger closed (1: a full curl; negative: opened; the thumb signed by its way)
};
// seat: where the figure sits; rearBuild: the cabin it is drawn in is the
// missile operator's (its origin at the rear seat), else the driver's.
bool BuildCrewFigure(const std::wstring& folder,unsigned kind,unsigned model,CrewSeat seat,bool rearBuild,
                     CrewFigure& out,std::string& error);
// The class's colour presets: main then sub, RGBA each.
bool LoadCrewColours(const std::wstring& folder,unsigned kind,std::vector<std::array<float,8>>& presets);
}
