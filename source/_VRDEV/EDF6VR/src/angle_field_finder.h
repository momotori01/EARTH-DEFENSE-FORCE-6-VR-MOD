#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace edf6vr {
// Finds where the game keeps an angle it does not tell us about, by watching
// memory while the angle is known to move. Each sample gives the change in the
// two known angles (yaw and pitch, radians) since the last sample and a region's
// floats; a float whose change matches one of them, as radians or degrees, with
// either sign, scores a hit for that angle. Only clean samples count: one
// angle moving, the other still. The offsets that hit (almost) every time are
// the angle's store.
class AngleFieldFinder {
public:
    enum Kind : unsigned { kRadians, kNegRadians, kDegrees, kNegDegrees, kKinds };
    struct Best { std::size_t offset=0; unsigned kind=0, hits=0, events=0; float value=0; bool valid=false; };
    void Reset(std::size_t floats) {
        last_.assign(floats,0);primed_=false;yawEvents_=pitchEvents_=0;
        for(auto& h:yaw_)h.assign(floats,0);
        for(auto& h:pitch_)h.assign(floats,0);
    }
    std::size_t Size() const noexcept { return last_.size(); }
    // current: the region's floats now; yawStep/pitchStep: the known angles'
    // change since the previous sample.
    void Sample(const float* current,float yawStep,float pitchStep) {
        const std::size_t n=last_.size();
        if(primed_) {
            const bool yawEvent=std::fabs(yawStep)>kMoved&&std::fabs(pitchStep)<kStill;
            const bool pitchEvent=std::fabs(pitchStep)>kMoved&&std::fabs(yawStep)<kStill;
            if(yawEvent)++yawEvents_;
            if(pitchEvent)++pitchEvents_;
            if(yawEvent||pitchEvent) {
                const float step=yawEvent?yawStep:pitchStep;
                auto& hits=yawEvent?yaw_:pitch_;
                const float expected[kKinds]={step,-step,step*kDegree,-step*kDegree};
                for(std::size_t i=0;i<n;++i) {
                    const float change=current[i]-last_[i];
                    if(!std::isfinite(change))continue;
                    for(unsigned k=0;k<kKinds;++k) {
                        const float tolerance=std::fabs(expected[k])*.15f+(k>=kDegrees?1e-3f:2e-5f);
                        if(std::fabs(change-expected[k])<tolerance&&hits[k][i]<65535)++hits[k][i];
                    }
                }
            }
        }
        for(std::size_t i=0;i<n;++i)last_[i]=current[i];
        primed_=true;
    }
    // The best-scoring float for yaw (pitch=false) or pitch, and the next ones
    // down, into out[0..count).
    unsigned Top(bool pitch,Best* out,unsigned count) const {
        const auto& hits=pitch?pitch_:yaw_;const unsigned events=pitch?pitchEvents_:yawEvents_;
        unsigned found=0;
        for(unsigned slot=0;slot<count;++slot) {
            Best best{};
            for(unsigned k=0;k<kKinds;++k)for(std::size_t i=0;i<last_.size();++i) {
                const unsigned h=hits[k][i];
                if(h==0||h<=best.hits)continue;
                bool taken=false;
                for(unsigned j=0;j<found;++j)if(out[j].offset==i*4&&out[j].kind==k)taken=true;
                if(taken)continue;
                best={i*4,k,h,events,last_[i],true};
            }
            if(!best.valid)break;
            out[found++]=best;
        }
        return found;
    }
    unsigned YawEvents() const noexcept { return yawEvents_; }
    unsigned PitchEvents() const noexcept { return pitchEvents_; }
private:
    static constexpr float kMoved=.01f,kStill=.003f,kDegree=57.2957795f;
    std::vector<float> last_;
    std::vector<std::uint16_t> yaw_[kKinds],pitch_[kKinds];
    unsigned yawEvents_=0,pitchEvents_=0;
    bool primed_=false;
};
}
