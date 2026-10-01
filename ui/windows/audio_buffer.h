#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

// Stereo float 44.1 kHz between WASAPI's two independent shared-mode clients.
// Fixed capacity; gentle occupancy-driven resampling compensates clock drift.
// One worker owns this object. No allocations or logging in Push/Render.
class WindowsAudioBuffer {
public:
    static constexpr unsigned Capacity = 8192, Target = 1323;
    uint64_t dropped = 0, missing = 0, rendered = 0;
    void Reset() { head_ = size_ = 0; phase_ = 0; primed_ = false; step_ = 1; }
    unsigned Size() const { return size_; }
    double Step() const { return step_; }
    void Push(const float* data, unsigned frames, bool silent) {
        for (unsigned i = 0; i < frames; ++i) {
            if (size_ == Capacity) { head_ = (head_ + 1) % Capacity; --size_; ++dropped; }
            const unsigned at = (head_ + size_++) % Capacity;
            for (unsigned c = 0; c < 2; ++c)
                samples_[at * 2 + c] = silent ? 0.f : data[i * 2 + c];
        }
    }
    void Render(float* output, unsigned frames) {
        if (!primed_ && size_ >= Target) primed_ = true;
        // Account for the packet about to be consumed when centering occupancy.
        const double error = (double(size_) - double(frames) / 2 - Target) / Target;
        const double desired = 1 + std::clamp(error * .002, -.003, .003);
        step_ += .02 * (desired - step_);
        for (unsigned i = 0; i < frames; ++i) {
            if (!primed_ || size_ < 2) {
                output[i * 2] = output[i * 2 + 1] = 0;
                ++missing; primed_ = false; phase_ = 0; continue;
            }
            for (unsigned c = 0; c < 2; ++c) {
                const float a = samples_[head_ * 2 + c];
                const float b = samples_[((head_ + 1) % Capacity) * 2 + c];
                output[i * 2 + c] = a + (b - a) * float(phase_);
            }
            ++rendered;
            phase_ += step_;
            const unsigned consumed = static_cast<unsigned>(phase_);
            phase_ -= consumed;
            head_ = (head_ + consumed) % Capacity; size_ -= consumed;
        }
    }
private:
    std::array<float, Capacity * 2> samples_{};
    unsigned head_ = 0, size_ = 0;
    double phase_ = 0, step_ = 1;
    bool primed_ = false;
};
