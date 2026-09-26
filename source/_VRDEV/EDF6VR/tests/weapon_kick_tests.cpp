#include "weapon_kick.h"
#include <cmath>
#include <cstdio>

static int failures=0;
#define CHECK(x) do { if(!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
static bool Near(float a,float b,float tolerance=1e-4f) { return std::fabs(a-b)<tolerance; }
using namespace edf6vr;

int main() {
    const float decay=0.08f;
    RecoilKick kick{};
    CHECK(Near(kick.Level(10.0,decay),0));
    kick.Shot(10.0,decay);
    CHECK(Near(kick.Level(10.0,decay),1));
    CHECK(Near(kick.Level(10.08,decay),std::exp(-1.0f),1e-3f));
    CHECK(kick.Level(10.5,decay)<0.01f);
    // Pellets of the same shot count once.
    kick.Shot(10.001,decay); kick.Shot(10.004,decay);
    CHECK(kick.Level(10.004,decay)<1.0f && kick.Level(10.004,decay)>0.9f);   // still the one shot, 4 ms on
    // Sustained fire builds up but is capped.
    for(int i=0;i<40;++i) kick.Shot(11.0+i*0.02,decay);
    CHECK(kick.Level(11.0+39*0.02,decay)>1.5f && kick.Level(11.0+39*0.02,decay)<=RecoilKick::kMax);
    // Time cannot run backwards into a bigger level, and nonsense is ignored.
    CHECK(Near(kick.Level(5.0,decay),kick.level));
    RecoilKick untouched{}; untouched.Shot(std::nan(""),decay); CHECK(Near(untouched.Level(1.0,decay),0));

    // The kick tilts the muzzle up about the palm and moves the hand back.
    float axes[3][3]={{1,0,0},{0,1,0},{0,0,1}}; float palm[3]={0,0,0};
    RecoilKickShape shape{3.14159265f/2,0.01f};
    ApplyRecoilKick(1.0f,shape,axes,palm);
    CHECK(Near(axes[2][1],1) && Near(axes[2][2],0));      // ahead became up
    CHECK(Near(axes[1][2],-1) && Near(axes[1][1],0));     // up became -ahead
    CHECK(Near(axes[0][0],1));                            // right untouched
    CHECK(Near(palm[2],-0.01f));                          // back along the old ahead
    float same[3][3]={{1,0,0},{0,1,0},{0,0,1}}; float still[3]={1,2,3};
    ApplyRecoilKick(0,shape,same,still);
    CHECK(Near(same[2][2],1) && Near(still[2],3));
    // Half the level, half the angle.
    float half[3][3]={{1,0,0},{0,1,0},{0,0,1}}; float palmHalf[3]={0,0,0};
    ApplyRecoilKick(0.5f,shape,half,palmHalf);
    CHECK(Near(half[2][1],std::sin(3.14159265f/4)) && Near(palmHalf[2],-0.005f));

    // Carried onto the frame itself, the kick is the same kick.
    {
        const float s=std::sqrt(0.5f);
        const float reference[3][3]={{s,0,-s},{0,1,0},{s,0,s}}; const float referencePalm[3]={0.1f,1.2f,0.3f};
        float own[3][3],ownPalm[3],carried[3][3],carriedPalm[3];
        for(int k=0;k<3;++k) for(int j=0;j<3;++j) own[k][j]=carried[k][j]=reference[k][j];
        for(int j=0;j<3;++j) ownPalm[j]=carriedPalm[j]=referencePalm[j];
        const RecoilKickShape small{0.07f,0.05f};
        ApplyRecoilKick(1.3f,small,own,ownPalm);
        ApplyRecoilKickWith(1.3f,small,reference,referencePalm,carried,carriedPalm);
        bool match=true;
        for(int k=0;k<3;++k) for(int j=0;j<3;++j) match=match && Near(own[k][j],carried[k][j]);
        for(int j=0;j<3;++j) match=match && Near(ownPalm[j],carriedPalm[j]);
        CHECK(match);
    }
    // A support hand 0.3 m ahead of the firing palm rises with the muzzle and
    // comes back with the weapon; its hand frame turns the same way.
    {
        const float reference[3][3]={{1,0,0},{0,1,0},{0,0,1}}; const float referencePalm[3]={0,0,0};
        float support[3][3]={{0,0,1},{0,1,0},{-1,0,0}}; float supportPalm[3]={0,0,0.3f};
        const RecoilKickShape tilt{0.1f,0.02f};
        ApplyRecoilKickWith(1.0f,tilt,reference,referencePalm,support,supportPalm);
        CHECK(Near(supportPalm[1],0.3f*std::sin(0.1f)));
        CHECK(Near(supportPalm[2],0.3f*std::cos(0.1f)-0.02f));
        CHECK(Near(supportPalm[0],0));
        CHECK(Near(support[0][1],std::sin(0.1f)) && Near(support[0][2],std::cos(0.1f)));   // its right follows ahead
        CHECK(Near(support[2][0],-1));                                                     // across the turn axis: untouched
        // Distance to the firing palm is kept: the two hands move as one.
        CHECK(Near(std::sqrt(supportPalm[0]*supportPalm[0]+supportPalm[1]*supportPalm[1]
                             +(supportPalm[2]+0.02f)*(supportPalm[2]+0.02f)),0.3f));
        float idle[3][3]={{0,0,1},{0,1,0},{-1,0,0}}; float idlePalm[3]={0,0,0.3f};
        ApplyRecoilKickWith(0,tilt,reference,referencePalm,idle,idlePalm);
        CHECK(Near(idlePalm[2],0.3f) && Near(idle[0][2],1));
    }
    printf("EDF6VR weapon kick tests: %d failures\n",failures);
    return failures?1:0;
}
