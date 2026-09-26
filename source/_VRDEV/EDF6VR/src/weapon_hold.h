#pragma once
#include "camera_math.h"

namespace edf6vr {
// No weapon state is part of this command. The update thread publishes only
// the tracked destination; the draw thread samples the bones it is about to
// draw.
struct WeaponHoldFrame { float axes[3][3]{}; float palm[3]{}; };
bool WeaponLocalPoint(const Matrix& root,const float* world,float* local) noexcept;
bool PlaceWeaponLocalPoint(const WeaponHoldFrame& hand,const float* local,float* world) noexcept;
// Turns rows and a point about the vertical line through `pivot`, the way
// RotateY turns a vector: a hand placed with one yaw offset moved onto another.
void TurnAboutVertical(float axes[3][3],float point[3],const float pivot[3],float radians) noexcept;
// `arm` is the frame the weapon is currently in. It must come from the same
// snapshot as `original`.
//
// It used to be the animation node arms_r, read live on the draw thread, while
// the bones came from the palette the renderer consumes -- and the distance
// between the two, which cannot change on a weapon held rigidly in an arm,
// wandered over 40cm. They are two copies of the same skeleton taken at
// different moments, and the gap between those moments is widest while walking,
// which is exactly when the weapon was reported to blur. Taking the source from
// the palette itself leaves nothing to be out of step with.
// `pointIn`/`pointOut`, when given, carry one extra world point through the same
// move. The muzzle flash is drawn from a transform of the game's own, so putting
// that transform's point through this puts the flash exactly where the game had
// it relative to the weapon -- no distance to guess and nothing to tune.
bool CarryWeaponBones(const Matrix& arm,const WeaponHoldFrame& hand,
                      const Matrix* original,std::size_t count,Matrix* wanted,
                      float& sourceLever,float& wantedLever,
                      const float* pointIn=nullptr,float* pointOut=nullptr) noexcept;
}
