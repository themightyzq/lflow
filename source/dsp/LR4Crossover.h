#pragma once
#include "Biquad.h"

namespace lflow {

// One split point, one channel. LP path = 2 cascaded Butterworth lowpass biquads
// (q = 1/sqrt(2)); HP path = 2 cascaded highpass biquads at the same cutoff.
// This is the standard Linkwitz-Riley 4th-order (LR4) construction: both paths
// sum allpass-flat in magnitude with NO polarity flip (unlike LR2, which needs
// one). Pure C++ — no JUCE, no allocation, RT-safe.
class LR4Crossover
{
public:
    void prepare (double sampleRateHz) noexcept
    {
        sampleRate = (sampleRateHz > 0.0) ? sampleRateHz : 48000.0;
        setFrequency (frequency);
    }

    // Recompute coefficients (arithmetic only, no allocation).
    void setFrequency (double fcHz) noexcept
    {
        frequency = fcHz;
        constexpr double q = 0.70710678118654752440; // 1/sqrt(2)
        lp1.setLowpass (fcHz, sampleRate, q);
        lp2.setLowpass (fcHz, sampleRate, q);
        hp1.setHighpass (fcHz, sampleRate, q);
        hp2.setHighpass (fcHz, sampleRate, q);
    }

    void split (float in, float& low, float& high) noexcept
    {
        low = lp2.process (lp1.process (in));
        high = hp2.process (hp1.process (in));
    }

    void reset() noexcept
    {
        lp1.reset();
        lp2.reset();
        hp1.reset();
        hp2.reset();
    }

private:
    double sampleRate { 48000.0 };
    double frequency { 1000.0 };
    Biquad lp1, lp2, hp1, hp2;
};

} // namespace lflow
