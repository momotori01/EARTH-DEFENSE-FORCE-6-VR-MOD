#pragma once
// Some runtimes do not let a projection view declare a field of view of its own:
// XrViewConfigurationProperties::fovMutable is false, and they show each eye
// picture as if it covered exactly that eye's frustum from xrLocateViews. Meta's
// PC runtime is one ("the FOV is not mutable on Oculus"; SteamVR's is). The game
// draws a symmetric frustum wider than either eye, so there each picture was
// squeezed into the eye's narrower, lopsided frustum -- shifted one way in the
// left eye and the other way in the right, which the eyes cannot fuse: double
// vision with both open, fine with one shut (Quest 3S on Quest Link, 2026-09-30;
// the pictures themselves were a sound pair, EYECHECK).
//
// Such a runtime gets the eye's own frustum back, with just the part of the
// picture that covers it: the same pixels in the same directions. The picture is
// a flat perspective image, so a direction maps to a point linearly in its
// tangent, and the crop is a plain rectangle.
#include <cmath>

namespace edf6vr {

struct EyeCrop { int x=0,y=0,width=0,height=0; };

// Angles in radians, OpenXR style: left and down negative. The rectangle of a
// width x height picture drawn over `rendered*` that the eye frustum `eye*`
// covers, to the nearest pixel (y downward). Where the eye reaches past the
// picture, the picture's edge is used. A frustum that is not usable gives the
// whole picture.
inline EyeCrop CropForEye(float renderedLeft,float renderedRight,float renderedUp,float renderedDown,
                          float eyeLeft,float eyeRight,float eyeUp,float eyeDown,int width,int height) noexcept {
    EyeCrop whole{0,0,width,height};
    const double tl=std::tan(renderedLeft),tr=std::tan(renderedRight),tu=std::tan(renderedUp),td=std::tan(renderedDown);
    const double el=std::tan(eyeLeft),er=std::tan(eyeRight),eu=std::tan(eyeUp),ed=std::tan(eyeDown);
    if(!(tr>tl) || !(tu>td) || !(er>el) || !(eu>ed) || width<=0 || height<=0
       || !std::isfinite(el+er+eu+ed) || !std::isfinite(tl+tr+tu+td)) return whole;
    auto x=[&](double t) { const double v=(t-tl)/(tr-tl)*width; return v<0?0.0:v>width?double(width):v; };
    auto y=[&](double t) { const double v=(tu-t)/(tu-td)*height; return v<0?0.0:v>height?double(height):v; };
    EyeCrop c{};
    c.x=static_cast<int>(std::lround(x(el)));
    c.y=static_cast<int>(std::lround(y(eu)));
    c.width=static_cast<int>(std::lround(x(er)))-c.x;
    c.height=static_cast<int>(std::lround(y(ed)))-c.y;
    if(c.width<1 || c.height<1) return whole;
    return c;
}

// The frustum a game projection matrix really draws, in radians (left and down
// negative). The crop is only as exact as this: an error in the assumed field of
// view scales both eyes alike with the whole picture, but once each eye is cut
// from it, the same error moves the two cuts opposite ways (hardware 2026-09-30:
// "slightly doubled" with the crop from the assumed field). Row vectors, as EDF's
// native views (aim_hud.cpp): clip = v * m, w = z * m23. Either handedness and
// sign convention: the forward z is the one that makes w positive, and the edges
// are sorted, so a mirrored matrix still gives left < right.
inline bool FrustumFromProjection(const float m[4][4],float& left,float& right,float& up,float& down) noexcept {
    if(!std::isfinite(m[0][0]+m[1][1]+m[2][0]+m[2][1]+m[2][3]) || std::fabs(m[0][0])<1e-4f || std::fabs(m[1][1])<1e-4f
       || std::fabs(m[2][3])<.5f) return false;
    const double w=std::fabs(m[2][3]), s=m[2][3]>0?1.0:-1.0;     // forward z = s gives w = |m23|
    // ndc = (tan * m00 + s * m20) / w  ->  tan = (ndc * w - s * m20) / m00
    const double a=(-w-s*m[2][0])/m[0][0], b=(w-s*m[2][0])/m[0][0];
    const double c=(w-s*m[2][1])/m[1][1], d=(-w-s*m[2][1])/m[1][1];
    left=static_cast<float>(std::atan(a<b?a:b)); right=static_cast<float>(std::atan(a<b?b:a));
    up=static_cast<float>(std::atan(c>d?c:d)); down=static_cast<float>(std::atan(c>d?d:c));
    return right>left && up>down;
}
}
