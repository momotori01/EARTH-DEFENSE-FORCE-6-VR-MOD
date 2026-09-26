#pragma once
#include <cstdint>

#include "midhook.h"

namespace multislot {

// Enemy spawn counts in online missions with 5-8 players ([Mission] ExtraEnemies=1). Every machine runs
// the mission script and creates the same objects in the same order, so the result depends only on the
// script's own arguments, the created object's class and the mission's player count, which all players
// share. Four or fewer players, offline missions and other teams are never changed.

// count * (1 + 0.2 per player above four), rounded half up: 1.2x for 5 players ... 1.8x for 8.
int ScaledEnemyCount(int count, int players);

// Scene object classes (xgs_scene_object_class, which is also the C++ class name) whose spawns grow:
// mobile enemies. Spawners and fixed destructible objects (nests, anchors, ships, pods, eggs, webs,
// shield bearers) and set-piece bosses keep their count.
bool IsMultipliedEnemyClass(const char* className);

// Mid-function hook handlers for SpawnHooks() in patches.h, by site RVA.
MidHandler SpawnHookHandler(std::uint32_t rva);

}  // namespace multislot
