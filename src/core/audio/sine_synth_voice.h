#pragma once
#include "core/note.h"
#include <cmath>
#include <cstdint>

namespace BroTracker {
inline double SynthNoteFrequency(unsigned note) noexcept {
    return IsPitchedPatternNote(note) ? 440.0 * std::exp2((double(note)-69.0)/12.0) : 0.0;
}
inline double SmoothEndpoint(double x) noexcept { return x*x*(3.0-2.0*x); }

// Phase and tuning only; independent of envelope and channel orchestration.
class SineOscillator {
public:
    void Start(double frequency, std::uint32_t rate) noexcept { phase_=0; step_=frequency/double(rate); }
    float Next() noexcept {
        const float value=static_cast<float>(std::sin(phase_*6.2831853071795864769));
        phase_+=step_; if(phase_>=1.0) phase_-=1.0;
        return value;
    }
private:
    double phase_=0,step_=0;
};

// Sample-counted one-shot: endpoint samples are exactly 0/1/0. No sustain.
class SynthEnvelope {
public:
    void Configure(std::uint32_t rate) noexcept {
        total_=rate; attack_=static_cast<std::uint32_t>((std::uint64_t(rate)*15+500)/1000);
        if(attack_<2) attack_=2;
        Reset();
    }
    void Reset() noexcept { age_=total_; release_age_=attack_; releasing_=false; level_=0; }
    void Trigger() noexcept { age_=0; releasing_=false; level_=0; }
    void Release() noexcept {
        if(!Active() || releasing_) return;
        release_level_=level_; release_age_=0; releasing_=true;
    }
    float Next() noexcept {
        if(releasing_) {
            if(release_age_>=attack_) { level_=0; age_=total_; return 0; }
            level_=release_level_*(1.0f-SmoothEndpoint(double(release_age_++)/double(attack_-1)));
        } else if(age_<total_) {
            if(age_<attack_) level_=SmoothEndpoint(double(age_)/double(attack_-1));
            else level_=1.0f-SmoothEndpoint(double(age_-(attack_-1))/double(total_-attack_));
            ++age_;
        } else level_=0;
        return level_;
    }
    bool Active() const noexcept { return releasing_ ? release_age_<attack_ : age_<total_; }
private:
    std::uint32_t total_=0,attack_=2,age_=0,release_age_=0;
    float level_=0,release_level_=0;
    bool releasing_=false;
};

class SineSynthVoice {
public:
    bool Configure(std::uint32_t rate) noexcept {
        // All supported notes must stay below Nyquist (G8 = 12543.85 Hz).
        if(rate<32000 || rate>192000) return false;
        rate_=rate; envelope_.Configure(rate); return true;
    }
    bool Trigger(unsigned note) noexcept {
        if(!rate_ || !IsPitchedPatternNote(note)) return false;
        oscillator_.Start(SynthNoteFrequency(note),rate_); envelope_.Trigger(); return true;
    }
    void Release() noexcept { envelope_.Release(); }
    void Reset() noexcept { envelope_.Reset(); }
    bool Active() const noexcept { return envelope_.Active(); }
    float Next() noexcept {
        if(!envelope_.Active()) return 0;
        return oscillator_.Next()*envelope_.Next()*2048.0f;
    }
private:
    SineOscillator oscillator_;
    SynthEnvelope envelope_;
    std::uint32_t rate_=0;
};
}
