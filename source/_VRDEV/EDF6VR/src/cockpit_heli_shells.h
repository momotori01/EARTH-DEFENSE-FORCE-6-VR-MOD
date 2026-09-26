#pragma once
// Helicopter cockpits (Nereid first): the pilot sits under the real canopy and
// looks out through its glass. The numbers come from each model, measured by
// research/cockpit/heli_shells.py (the canopy's panes found from the model's
// own albedo by research/cockpit/heli_canopy.py); no mesh is shipped.
#include "cockpit.h"
#include <cmath>
namespace edf6vr {
#include "cockpit_heli_shells.inc"
// The rebuilt machines (research/cockpit/heli_cockpit.py): windows and hull sections.
#include "cockpit_heli_cockpits.inc"
inline const HeliShell* FindHeliShell(CockpitKind kind) noexcept {
    for(const auto& s:kHeliShells)if(s.kind==kind)return &s;
    return nullptr;
}
inline bool IsHeliKind(CockpitKind kind) noexcept {return FindHeliShell(kind)!=nullptr;}
// A rebuilt machine's screen let into its body (not in the view) is opaque;
// the rest of a rebuilt machine's screens hang in the view, see-through.
inline bool HeliScreenOpaque(CockpitKind kind,unsigned panel) noexcept {
    // The Euros's radar, in its dashboard's face; the pickups' three, in theirs.
    return (kind==CockpitKind::Heli506&&panel==0)||kind==CockpitKind::TruckPickup;
}
// The region round the canopy whose hull the cockpit lines where the pilot
// would see it from behind (model coordinates).
inline bool HeliRegion(const HeliShell& s,const float* p) noexcept {
    return p[1]>=s.regionFloor&&p[2]>=s.regionRear&&p[2]<=s.regionFront&&std::fabs(p[0])<=s.regionHalfWidth;
}
// Does a triangle (nine floats, model coordinates) lie on the canopy's glass:
// its centre within 2 cm of a pane's plane and inside its outline, grown 5 mm?
// Those the game no longer draws, so the windows are clear.
inline bool HeliGlass(const HeliShell& s,const float* t) noexcept {
    const float c[3]={(t[0]+t[3]+t[6])/3,(t[1]+t[4]+t[7])/3,(t[2]+t[5]+t[8])/3};
    for(unsigned i=0;i<s.paneCount;++i) {
        const auto& pane=s.panes[i];if(pane.count<3)continue;
        const float* a=pane.p[0];const float* b=pane.p[1];const float* d=pane.p[2];
        float n[3]={(b[1]-a[1])*(d[2]-a[2])-(b[2]-a[2])*(d[1]-a[1]),(b[2]-a[2])*(d[0]-a[0])-(b[0]-a[0])*(d[2]-a[2]),
            (b[0]-a[0])*(d[1]-a[1])-(b[1]-a[1])*(d[0]-a[0])};
        float l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(l<1e-5f) {   // the first three corners nearly in line: the normal from every edge
            n[0]=n[1]=n[2]=0;
            for(unsigned k=0;k<pane.count;++k){const float* p=pane.p[k];const float* q=pane.p[(k+1)%pane.count];
                n[0]+=p[1]*q[2]-p[2]*q[1];n[1]+=p[2]*q[0]-p[0]*q[2];n[2]+=p[0]*q[1]-p[1]*q[0];}
            l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        }
        if(l<1e-8f)continue;
        for(auto& x:n)x/=l;
        if(std::fabs(n[0]*(c[0]-a[0])+n[1]*(c[1]-a[1])+n[2]*(c[2]-a[2]))>.02f)continue;
        // The outline runs one way round n (it is sorted so): n x edge points in.
        bool inside=true;
        for(unsigned k=0;k<pane.count&&inside;++k) {
            const float* p=pane.p[k];const float* q=pane.p[(k+1)%pane.count];
            const float e[3]={q[0]-p[0],q[1]-p[1],q[2]-p[2]};
            const float m[3]={n[1]*e[2]-n[2]*e[1],n[2]*e[0]-n[0]*e[2],n[0]*e[1]-n[1]*e[0]};
            const float ml=std::sqrt(m[0]*m[0]+m[1]*m[1]+m[2]*m[2]);if(ml<1e-8f)continue;
            inside=(m[0]*(c[0]-p[0])+m[1]*(c[1]-p[1])+m[2]*(c[2]-p[2]))/ml>=-.005f;
        }
        if(inside)return true;
    }
    return false;
}
// Monitor windows (the user: where the armour blocks the view ahead, it is a
// monitor onto the world; the rest of the armour and the frames stay). Each
// is a cone of directions from the seated eye (HeliShell::monitors, chosen per
// machine in research/cockpit/heli_shells.py); inside it the canopy's armour
// is not lined, so the pilot sees through it. The plane of the cone's edge
// from d[i] to d[i+1], turned so that inside is n.(p - eye) <= 0.
inline void HeliMonitorPlane(const HeliMonitorWindow& w,unsigned i,float* n) noexcept {
    const float* a=w.d[i];const float* b=w.d[(i+1)%w.count];
    n[0]=a[1]*b[2]-a[2]*b[1];n[1]=a[2]*b[0]-a[0]*b[2];n[2]=a[0]*b[1]-a[1]*b[0];
    float c[3]{};for(unsigned k=0;k<w.count;++k)for(int j=0;j<3;++j)c[j]+=w.d[k][j];
    const float l=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(l<1e-9f){n[0]=n[1]=n[2]=0;return;}
    const float sign=(n[0]*c[0]+n[1]*c[1]+n[2]*c[2])>0?-1.f:1.f;
    for(int j=0;j<3;++j)n[j]*=sign/l;
}
// Is a point (model coordinates) inside a monitor window's cone, by more than `by`?
inline bool HeliInMonitor(const HeliShell& s,const float* p,float by=0) noexcept {
    for(unsigned m=0;m<s.monitorCount;++m) {
        bool inside=s.monitors[m].count>=3;
        for(unsigned i=0;i<s.monitors[m].count&&inside;++i) {
            float n[3];HeliMonitorPlane(s.monitors[m],i,n);
            inside=n[0]*(p[0]-s.eye[0])+n[1]*(p[1]-s.eye[1])+n[2]*(p[2]-s.eye[2])<-by;
        }
        if(inside)return true;
    }
    return false;
}
// A hull triangle (nine floats) of the canopy region with its centre in a
// monitor window: the game no longer draws it, whichever way it faces, so the
// window shows the world (and the lining leaves it open).
inline bool HeliMonitorHull(const HeliShell& s,const float* t) noexcept {
    const float c[3]={(t[0]+t[3]+t[6])/3,(t[1]+t[4]+t[7])/3,(t[2]+t[5]+t[8])/3};
    return s.monitorCount&&HeliRegion(s,c)&&HeliInMonitor(s,c);
}
// The shell in cabin coordinates: the cabin origin is the eye less the seated
// eye offset, and cabin +X is model -X (the pilot's right).
inline std::array<float,3> HeliCabinPoint(const HeliShell& s,const float* m) noexcept {
    return {-(m[0]-s.eye[0]),m[1]-(s.eye[1]-kCockpitSeatedEye[1]),m[2]-(s.eye[2]-kCockpitSeatedEye[2])};
}
// A glass triangle (nine floats, model coordinates) on a rebuilt machine's own
// windows: its centre on one of a window's fan triangles grown 1.5 cm, within
// 3 cm of it. The game's panes (HeliGlass) are convex and miss the notched
// parts of a window (the Euros's): all of its glass goes, or the game's glass
// stays in the view behind the cockpit's, flickering.
inline bool HeliRebuiltGlass(const HeliShell& s,const float* t) noexcept {
    const HeliCockpit* c=nullptr;for(const auto& h:kHeliCockpits)if(h.kind==s.kind)c=&h;
    if(!c)return false;
    const float m[3]={(t[0]+t[3]+t[6])/3,(t[1]+t[4]+t[7])/3,(t[2]+t[5]+t[8])/3};
    const auto p=HeliCabinPoint(s,m);
    auto sub=[](const float* a,const float* b,float* o){for(int k=0;k<3;++k)o[k]=a[k]-b[k];};
    auto cross=[](const float* a,const float* b,float* o){o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];};
    auto dot=[](const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    for(unsigned w=0;w<c->windowCount;++w) {
        const auto& win=c->windows[w];if(win.count<3)continue;
        float q[3]{};for(unsigned i=0;i<win.count;++i)for(int k=0;k<3;++k)q[k]+=win.p[i][k]/float(win.count);
        for(unsigned i=0;i<win.count;++i) {
            const float* tri[3]={q,win.p[i],win.p[(i+1)%win.count]};
            float e1[3],e2[3],n[3];sub(tri[1],tri[0],e1);sub(tri[2],tri[0],e2);cross(e1,e2,n);
            const float l=std::sqrt(dot(n,n));if(l<1e-9f)continue;
            for(auto& x:n)x/=l;
            float d[3];sub(p.data(),q,d);if(std::fabs(dot(n,d))>.03f)continue;
            bool inside=true;
            for(int k=0;k<3&&inside;++k) {
                float e[3],mm[3],r[3],o[3];sub(tri[(k+1)%3],tri[k],e);cross(n,e,mm);
                const float ml=std::sqrt(dot(mm,mm));if(ml<1e-9f)continue;
                sub(tri[(k+2)%3],tri[k],o);if(dot(mm,o)<0)for(auto& x:mm)x=-x;   // inward
                sub(p.data(),tri[k],r);inside=dot(mm,r)/ml>=-.015f;
            }
            if(inside)return true;
        }
    }
    return false;
}
// A hull triangle (nine floats, model coordinates) of a rebuilt machine inside
// its fuselage under one of its monitors (HeliCockpit::hideHalfWidth): the
// Euros's cockpit tub, modelled to be seen through its glass from outside.
// The monitor shows the world there, so the game no longer draws it. The
// zones grown a little (10 cm along, 12 cm up, 5 cm in), so a large triangle
// centred just past a zone's edge is not seen through it (the tub's walls);
// past the edge the shell covers them anyway.
inline bool HeliRebuiltHidden(const HeliShell& s,const float* t) noexcept {
    const HeliCockpit* c=nullptr;for(const auto& h:kHeliCockpits)if(h.kind==s.kind)c=&h;
    if(!c||c->hideHalfWidth<=0)return false;
    // Its centre or any corner (a large triangle reaches into a zone from
    // outside it).
    const float centre[3]={(t[0]+t[3]+t[6])/3,(t[1]+t[4]+t[7])/3,(t[2]+t[5]+t[8])/3};
    const float* points[4]={centre,t,t+3,t+6};
    for(const float* m:points) {
        const auto p=HeliCabinPoint(s,m);
        if(std::fabs(p[0]-c->centreX)>=c->hideHalfWidth||p[1]<c->floorY-.40f)continue;
        for(unsigned i=0;i<c->zoneCount;++i) {
            const auto& z=c->zones[i];
            const float f=(p[2]-z.z0)/(z.z1-z.z0),top=z.top0+(z.top1-z.top0)*(f<0?0:f>1?1:f);
            if(p[2]>z.z0-.10f&&p[2]<z.z1+.10f&&p[1]<top+.12f&&std::fabs(p[0]-c->centreX)>=z.xMin-.05f)return true;
        }
    }
    return false;
}
}
