#include "eye_crop.h"
#include <cstdio>
#include <cstdlib>

using namespace edf6vr;

static int failures=0;
#define CHECK(x) do { if(!(x)) { std::printf("FAIL line %d: %s\n",__LINE__,#x); ++failures; } } while(false)
static float Rad(float degrees) { return degrees*3.14159265f/180.0f; }

int main() {
    // The picture's own frustum is the whole picture.
    auto c=CropForEye(Rad(-53.1f),Rad(53.1f),Rad(50.5f),Rad(-50.5f),Rad(-53.1f),Rad(53.1f),Rad(50.5f),Rad(-50.5f),1184,1080);
    CHECK(c.x==0 && c.y==0 && c.width==1184 && c.height==1080);

    // Quest 3S (hardware log 2026-09-30): picture 1184x1080 over +-53.1 x +-50.5 degrees,
    // left eye -52/+45 across, +48/-50 up/down; the right eye is its mirror.
    const float tl=Rad(-53.1f),tr=Rad(53.1f),tu=Rad(50.5f),td=Rad(-50.5f);
    const auto left=CropForEye(tl,tr,tu,td,Rad(-52.f),Rad(45.f),Rad(48.f),Rad(-50.f),1184,1080);
    const auto right=CropForEye(tl,tr,tu,td,Rad(-45.f),Rad(52.f),Rad(48.f),Rad(-50.f),1184,1080);
    // The left eye sees further left than right: its rectangle sits left of centre.
    CHECK(left.x<right.x && left.x+left.width<1184 && right.x>0);
    // Mirror images across the middle, and the same height.
    CHECK(std::abs(left.x-(1184-(right.x+right.width)))<=1 && std::abs(left.width-right.width)<=1);
    CHECK(left.y==right.y && left.height==right.height && left.y>0 && left.y+left.height<=1080);
    // Pixel positions from the tangents: x = (tan a - tan L)/(tan R - tan L) * width.
    const double tanL=std::tan(tl),tanR=std::tan(tr);
    const int expectLeftEdge=static_cast<int>(std::lround((std::tan(Rad(-52.f))-tanL)/(tanR-tanL)*1184));
    const int expectRightEdge=static_cast<int>(std::lround((std::tan(Rad(45.f))-tanL)/(tanR-tanL)*1184));
    CHECK(left.x==expectLeftEdge && left.x+left.width==expectRightEdge);
    // Up is the top of the picture: the 48 degree edge is lower than the 50.5 one.
    const int expectTop=static_cast<int>(std::lround((std::tan(tu)-std::tan(Rad(48.f)))/(std::tan(tu)-std::tan(td))*1080));
    CHECK(left.y==expectTop);

    // An eye reaching past the picture is held to its edge.
    c=CropForEye(tl,tr,tu,td,Rad(-60.f),Rad(45.f),Rad(55.f),Rad(-50.f),1184,1080);
    CHECK(c.x==0 && c.y==0);
    // Nonsense in, the whole picture out.
    c=CropForEye(tl,tr,tu,td,0,0,0,0,1184,1080);
    CHECK(c.x==0 && c.y==0 && c.width==1184 && c.height==1080);
    c=CropForEye(tl,tr,tu,td,Rad(10.f),Rad(-10.f),Rad(20.f),Rad(-20.f),1184,1080);
    CHECK(c.width==1184);

    // The drawn frustum from a projection matrix: right-handed (w = -z), 107.94 degrees
    // tall, 2912x2700 wide, as this PC renders it.
    {
        const float half=Rad(107.94f)*.5f, aspect=2912.f/2700.f;
        float m[4][4]{};
        m[1][1]=1.f/std::tan(half); m[0][0]=m[1][1]/aspect; m[2][3]=-1.f; m[2][2]=-1.f; m[3][2]=-.1f;
        float l=0,r=0,u=0,d=0;
        CHECK(FrustumFromProjection(m,l,r,u,d));
        CHECK(std::fabs(u-half)<1e-5f && std::fabs(d+half)<1e-5f);
        const float halfH=std::atan(std::tan(half)*aspect);
        CHECK(std::fabs(r-halfH)<1e-5f && std::fabs(l+halfH)<1e-5f);
        // Left-handed (w = +z): the same frustum.
        m[2][3]=1.f; m[2][2]=1.f;
        float l2=0,r2=0,u2=0,d2=0;
        CHECK(FrustumFromProjection(m,l2,r2,u2,d2) && std::fabs(l2-l)<1e-6f && std::fabs(u2-u)<1e-6f);
        // Off-centre: shifted to the right by 0.1 in tangent (w = -z: tan = (ndc + m20) / m00).
        m[2][3]=-1.f; m[2][0]=.1f*m[0][0];
        CHECK(FrustumFromProjection(m,l,r,u,d));
        CHECK(std::fabs(std::tan(r)-(std::tan(halfH)+.1f))<1e-4f && std::fabs(std::tan(l)-(-std::tan(halfH)+.1f))<1e-4f);
        // Nothing usable.
        volatile float zero=0.f;         // not a constant the compiler can fold into a division
        float z[4][4]{};
        for(auto& row:z) for(auto& v:row) v=zero;
        CHECK(!FrustumFromProjection(z,l,r,u,d));
    }

    std::printf("EDF6VR eye crop tests: %d failures\n",failures);
    return failures?1:0;
}
