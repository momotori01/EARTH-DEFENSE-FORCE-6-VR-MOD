#pragma once
#include <cmath>
namespace edf6vr {
// Coordinates follow XrToGame: right=(-cos(yaw),0,sin(yaw)). Left only.
inline bool InLeftShoulder(const float* head,const float* hand,float yaw,bool leaving,
    float sideCentre=.18f,float up=0,float forward=0,float enterRadius=.15f,
    float leaveRadius=.19f,float front=.05f,float frontLeave=.09f,bool mirrored=false) noexcept {
    if(!head || !hand || !std::isfinite(yaw)) return false;
    const float dx=hand[0]-head[0],dy=hand[1]-head[1],dz=hand[2]-head[2];
    // Mirrored (left-handed mode): the off hand is the right one, at the right temple.
    const float side=(dz*std::sin(yaw)-dx*std::cos(yaw))*(mirrored?-1.0f:1.0f);
    const float ahead=dx*std::sin(yaw)+dz*std::cos(yaw);
    // Holster-only margin: original 3 cm plus the requested additional 5 cm.
    constexpr float margin=.08f;
    const float radius=(leaving?leaveRadius:enterRadius)+margin;
    const float x=side+std::fabs(sideCentre),y=dy-up,z=ahead-forward;
    const bool temple=side<-.02f && x*x+y*y+z*z<radius*radius
        && ahead<(leaving?frontLeave:front)+margin;
    // And the back, shoulder width and down to the shoulder blades: reaching
    // over either shoulder to behind the body finds the holster wherever the
    // hand lands, while a hand swung out wide behind does not. Slightly wider
    // on the way out so it does not flicker at the edge.
    const float grow=leaving?.05f:0;
    const bool back=ahead<-.02f+grow && ahead>-.55f-grow
        && std::fabs(side)<.25f+grow
        && dy<.15f+grow && dy>-.50f-grow;
    return temple || back;
}
struct ShoulderStep { bool entered=false,toggle=false,consume=false; };
struct RangerHolster {
    bool inside=false,pressed=false,captured=false;
    ShoulderStep Step(bool eligible,bool zone,bool grip) noexcept {
        ShoulderStep out{};
        out.entered=eligible && zone && !inside;
        inside=eligible && zone;
        // A press that began outside is consumed on entry, but never toggles.
        if(inside && grip) captured=true;
        if(!grip) captured=false;
        out.toggle=inside && grip && !pressed;
        out.consume=inside || captured;
        pressed=grip;
        return out;
    }
};
}
