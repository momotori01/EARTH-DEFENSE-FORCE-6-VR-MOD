#include "camera_math.h"
#include <cmath>
#include <cstring>

namespace edf6vr {

bool AnchorHead(HeadAnchor& state,const Matrix& root,const float rawEye[3],float out[3]) noexcept {
    if(!ValidCamera(root)) return false;
    for(int j=0;j<3;++j) if(!std::isfinite(rawEye[j])) return false;
    if(!state.primed) {
        // If tracking starts while knocked down, wait for a standing head before
        // learning the persistent on-foot offset. The caller keeps the raw pose
        // until then. This does not limit physical HMD crouching (applied later).
        if(rawEye[1]-root.m[3][1]<1.0f) return false;
        for(int j=0;j<3;++j) state.offset[j]=rawEye[j]-root.m[3][j];
        state.primed=true;
    }
    for(int j=0;j<3;++j) out[j]=root.m[3][j]+state.offset[j];
    return true;
}

float SteadyHead(HeadSteady& state,const Matrix& root,const float rawEye[3],
                 float aimPitchRadians,float damping,float limitMetres,float out[3]) noexcept {
    bool finite=ValidCamera(root);
    for(int i=0;i<3;++i) if(!std::isfinite(rawEye[i])) finite=false;
    if(!finite) { for(int i=0;i<3;++i) out[i]=rawEye[i]; return 0; }

    // Into the soldier's frame. His rows are his own axes, so projecting onto
    // them is the whole of the transform; his travel drops out here, which is
    // why none of it can be lagged by what follows.
    float local[3]{};
    for(int i=0;i<3;++i) {
        float along=0;
        for(int j=0;j<3;++j) along+=(rawEye[j]-root.m[3][j])*root.m[i][j];
        local[i]=along;
    }
    if(!state.primed) {
        for(int i=0;i<3;++i) state.offset[i]=local[i];
        state.primed=true;
        for(int i=0;i<3;++i) out[i]=rawEye[i];
        return 0;
    }
    // The reference only learns while the weapon is near level.
    //
    // The bob is a wobble and any slow filter averages it away, but the lean is
    // not: raise the weapon and the head stays leant back for as long as it is
    // held there. A filter free to follow that will follow it, however slow it
    // is made - modelled at a fiftieth per tick, five eighths of an eight
    // centimetre lean was still reaching the view immediately and all of it
    // within a few seconds. Holding the reference while the weapon is raised is
    // what cancels the lean, and it costs nothing, because a stance that really
    // has changed is picked up again the moment the weapon comes back down.
    constexpr float kGain=0.02f;
    constexpr float kLevelRadians=0.21f;   // about twelve degrees
    if(std::isfinite(aimPitchRadians) && std::fabs(aimPitchRadians)<kLevelRadians)
        for(int i=0;i<3;++i) state.offset[i]+=(local[i]-state.offset[i])*kGain;

    const float mix=damping<0?0:(damping>1?1:damping);
    const float limit=limitMetres>0.005f?limitMetres:0.005f;
    float moved=0;
    for(int i=0;i<3;++i) out[i]=rawEye[i];
    for(int i=0;i<3;++i) {
        float difference=state.offset[i]-local[i];
        if(difference>limit) difference=limit;
        if(difference<-limit) difference=-limit;
        const float applied=difference*mix;
        moved+=applied*applied;
        for(int j=0;j<3;++j) out[j]+=root.m[i][j]*applied;
    }
    return std::sqrt(moved);
}

bool ValidCamera(const Matrix& a) noexcept {
    for (const auto& row : a.m)
        for (float v : row) if (!std::isfinite(v)) return false;
    for (int i = 0; i < 3; ++i) {
        float norm = 0;
        for (int k = 0; k < 3; ++k) norm += a.m[i][k] * a.m[i][k];
        if (std::fabs(norm - 1.0f) > 0.03f || std::fabs(a.m[i][3]) > 0.001f) return false;
        for (int j = i + 1; j < 3; ++j) {
            float dot = 0;
            for (int k = 0; k < 3; ++k) dot += a.m[i][k] * a.m[j][k];
            if (std::fabs(dot) > 0.03f) return false;
        }
    }
    const float det = a.m[0][0]*(a.m[1][1]*a.m[2][2]-a.m[1][2]*a.m[2][1])
                    -a.m[0][1]*(a.m[1][0]*a.m[2][2]-a.m[1][2]*a.m[2][0])
                    +a.m[0][2]*(a.m[1][0]*a.m[2][1]-a.m[1][1]*a.m[2][0]);
    return std::fabs(det-1.0f) < 0.05f && std::fabs(a.m[3][3]-1.0f) < 0.001f;
}
bool InvertCamera(const Matrix& input,Matrix& output) noexcept {
    if(!ValidCamera(input))return false;
    double a[3][3]{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i][j]=input.m[i][j];
    const double det=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])
        +a[0][1]*(a[1][2]*a[2][0]-a[1][0]*a[2][2])
        +a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if(!std::isfinite(det)||std::fabs(det)<.9)return false;
    Matrix result{};result.m[3][3]=1;
    double inverse[3][3]{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
        const int p=(j+1)%3,q=(j+2)%3,r=(i+1)%3,s=(i+2)%3;
        inverse[i][j]=(a[p][r]*a[q][s]-a[p][s]*a[q][r])/det;
        result.m[i][j]=static_cast<float>(inverse[i][j]);
    }
    for(int j=0;j<3;++j) {
        double t=0;for(int k=0;k<3;++k)t-=double(input.m[3][k])*inverse[k][j];
        result.m[3][j]=static_cast<float>(t);
    }
    if(!ValidCamera(result))return false;
    output=result;return true;
}
bool RotateCamera(const Matrix& input, float yawDegrees, float pitchDegrees, Matrix& output) noexcept {
    if (!ValidCamera(input) || !std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)
        || std::fabs(yawDegrees)>180 || std::fabs(pitchDegrees)>85) return false;
    output=input;
    if (yawDegrees==0 && pitchDegrees==0) return true;
    constexpr float radians=0.01745329251994329577f;
    const float cy=std::cos(yawDegrees*radians), sy=std::sin(yawDegrees*radians);
    const float cp=std::cos(pitchDegrees*radians), sp=std::sin(pitchDegrees*radians);
    const float rotation[3][3]={{cy,0,-sy},{-sp*sy,cp,-sp*cy},{cp*sy,sp,cp*cy}};
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        output.m[i][j]=0;
        for (int k=0;k<3;++k) output.m[i][j]+=rotation[i][k]*input.m[k][j];
    }
    return ValidCamera(output);
}
bool LookAlong(const Matrix& like,const float forwardWorld[3],Matrix& out) noexcept {
    if(!ValidCamera(like)) return false;
    for(int i=0;i<3;++i) if(!std::isfinite(forwardWorld[i])) return false;
    auto cross=[](const float a[3],const float b[3],float r[3]) {
        r[0]=a[1]*b[2]-a[2]*b[1];
        r[1]=a[2]*b[0]-a[0]*b[2];
        r[2]=a[0]*b[1]-a[1]*b[0];
    };
    auto normalise=[](float v[3]) {
        const float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(!(n>1e-6f)) return false;
        for(int i=0;i<3;++i) v[i]/=n;
        return true;
    };
    float forward[3]={forwardWorld[0],forwardWorld[1],forwardWorld[2]};
    if(!normalise(forward)) return false;

    // Which way the basis is handed, read off the camera the game itself built.
    float test[3]{};
    cross(like.m[1],like.m[2],test);
    const float agree=test[0]*like.m[0][0]+test[1]*like.m[0][1]+test[2]*like.m[0][2];
    const bool rightIsUpCrossForward=agree>0;

    const float worldUp[3]={0,1,0};
    float right[3]{};
    if(rightIsUpCrossForward) cross(worldUp,forward,right); else cross(forward,worldUp,right);
    if(!normalise(right)) {
        // Straight up or straight down leaves the sideways axis undefined, so the
        // one the camera already had is kept and made square to the new forward.
        for(int i=0;i<3;++i) right[i]=like.m[0][i];
        float along=0;
        for(int i=0;i<3;++i) along+=right[i]*forward[i];
        for(int i=0;i<3;++i) right[i]-=forward[i]*along;
        if(!normalise(right)) return false;
    }
    float up[3]{};
    if(rightIsUpCrossForward) cross(forward,right,up); else cross(right,forward,up);
    if(!normalise(up)) return false;

    out=like;
    for(int i=0;i<3;++i) { out.m[0][i]=right[i]; out.m[1][i]=up[i]; out.m[2][i]=forward[i]; }
    return ValidCamera(out);
}

bool SavedCamera::Restore(Matrix& current) noexcept {
    const bool restore=pending && std::memcmp(&current,&applied,sizeof(Matrix))==0;
    if (restore) current=original;
    pending=false;
    return restore;
}
bool SavedCamera::Apply(Matrix& current,float yaw,float pitch) noexcept {
    Matrix rotated{};
    if (!RotateCamera(current,yaw,pitch,rotated)) return false;
    original=current; applied=rotated; current=rotated; pending=true;
    return true;
}
}
