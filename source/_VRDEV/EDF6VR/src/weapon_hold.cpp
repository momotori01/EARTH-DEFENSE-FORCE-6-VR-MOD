#include "weapon_hold.h"
#include <cmath>

namespace edf6vr {
namespace {
// Positive when rows 0 x 1 point the same way as row 2.
float Handedness(const float axes[3][3]) noexcept {
    const float cross[3]={axes[0][1]*axes[1][2]-axes[0][2]*axes[1][1],
                          axes[0][2]*axes[1][0]-axes[0][0]*axes[1][2],
                          axes[0][0]*axes[1][1]-axes[0][1]*axes[1][0]};
    float along=0;
    for(int j=0;j<3;++j) along+=cross[j]*axes[2][j];
    return along;
}
}

bool WeaponLocalPoint(const Matrix& root,const float* world,float* local) noexcept {
    if(!world || !local)return false;
    float reach=0;
    for(int j=0;j<3;++j) {
        const float d=world[j]-root.m[3][j];reach+=d*d;
    }
    if(!std::isfinite(reach) || reach>=25)return false;
    const WeaponHoldFrame origin{{{1,0,0},{0,1,0},{0,0,1}},{0,0,0}};
    Matrix unused{};float before=0,after=0;
    return CarryWeaponBones(root,origin,&root,1,&unused,before,after,world,local);
}
void TurnAboutVertical(float axes[3][3],float point[3],const float pivot[3],float radians) noexcept {
    const float c=std::cos(radians),s=std::sin(radians);
    for(int k=0;k<3;++k) {
        const float x=axes[k][0],z=axes[k][2];
        axes[k][0]=x*c+z*s; axes[k][2]=-x*s+z*c;
    }
    const float x=point[0]-pivot[0],z=point[2]-pivot[2];
    point[0]=pivot[0]+x*c+z*s; point[2]=pivot[2]-x*s+z*c;
}
bool PlaceWeaponLocalPoint(const WeaponHoldFrame& hand,const float* local,float* world) noexcept {
    if(!local || !world)return false;
    float reach=0;for(int j=0;j<3;++j)reach+=local[j]*local[j];
    if(!std::isfinite(reach) || reach>=25)return false;
    const Matrix origin={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    Matrix unused{};float before=0,after=0;
    return CarryWeaponBones(origin,hand,&origin,1,&unused,before,after,local,world);
}
bool CarryWeaponBones(const Matrix& arm,const WeaponHoldFrame& hand,
                      const Matrix* original,std::size_t count,Matrix* wanted,
                      float& sourceLever,float& wantedLever,
                      const float* pointIn,float* pointOut) noexcept {
    if(!original || !wanted || !count || count>16) return false;
    float from[3][3]{};
    for(int k=0;k<3;++k) {
        float norm=0, toNorm=0;
        for(int j=0;j<3;++j) {
            if(!std::isfinite(arm.m[k][j]) || !std::isfinite(hand.axes[k][j])
               || !std::isfinite(arm.m[3][j]) || !std::isfinite(hand.palm[j])) return false;
            norm+=arm.m[k][j]*arm.m[k][j];
            toNorm+=hand.axes[k][j]*hand.axes[k][j];
        }
        if(!std::isfinite(norm) || norm<1e-8f || std::fabs(toNorm-1)>0.02f) return false;
        for(int j=0;j<3;++j) from[k][j]=arm.m[k][j]/std::sqrt(norm);
    }
    for(int a=0;a<3;++a) for(int b=a+1;b<3;++b) {
        float sourceDot=0,targetDot=0;
        for(int j=0;j<3;++j) { sourceDot+=from[a][j]*from[b][j]; targetDot+=hand.axes[a][j]*hand.axes[b][j]; }
        if(std::fabs(sourceDot)>0.02f || std::fabs(targetDot)>0.02f) return false;
    }
    // Both frames must be wound the same way round.
    //
    // Orthonormal was not enough. A destination of the opposite handedness is
    // orthonormal too, and it passed every test above while making this a
    // reflection instead of a turn -- so the weapon was handed to the renderer
    // inside out, its triangles reversed, and culling removed the half facing
    // the player. Refusing it leaves the weapon on the body, which is visible
    // and countable, instead of drawing it wrongly.
    if(Handedness(from)*Handedness(hand.axes)<0) return false;
    for(std::size_t b=0;b<count;++b) {
        for(const auto& row:original[b].m) for(float value:row) if(!std::isfinite(value)) return false;
        wanted[b]=original[b]; // Preserve W lanes and each bone's original scale.
        for(int row=0;row<4;++row) {
            float local[3]{};
            for(int k=0;k<3;++k) for(int j=0;j<3;++j)
                local[k]+=(original[b].m[row][j]-(row==3?arm.m[3][j]:0))*from[k][j];
            for(int j=0;j<3;++j) {
                float value=row==3?hand.palm[j]:0;
                for(int k=0;k<3;++k) value+=local[k]*hand.axes[k][j];
                if(!std::isfinite(value)) return false;
                wanted[b].m[row][j]=value;
            }
        }
    }
    if(pointIn && pointOut) {
        float local[3]{};
        for(int k=0;k<3;++k) for(int j=0;j<3;++j)
            local[k]+=(pointIn[j]-arm.m[3][j])*from[k][j];
        for(int j=0;j<3;++j) {
            float value=hand.palm[j];
            for(int k=0;k<3;++k) value+=local[k]*hand.axes[k][j];
            pointOut[j]=std::isfinite(value)?value:hand.palm[j];
        }
    }
    float before=0,after=0;
    for(int j=0;j<3;++j) {
        const float a=original[0].m[3][j]-arm.m[3][j];
        const float b=wanted[0].m[3][j]-hand.palm[j];
        before+=a*a; after+=b*b;
    }
    sourceLever=std::sqrt(before); wantedLever=std::sqrt(after);
    return std::isfinite(sourceLever) && std::isfinite(wantedLever);
}
}
