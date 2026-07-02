#pragma once
#include <cmath>

namespace lflow {

// Transposed Direct Form II biquad. RBJ cookbook lowpass/highpass coefficient
// formulas. Coefficients are computed in double precision; the sample path runs
// in float (house norm). Pure C++ — no JUCE, no allocation, RT-safe.
class Biquad
{
public:
    void setLowpass (double fcHz, double sampleRate, double q) noexcept
    {
        setCoeffs (fcHz, sampleRate, q, true);
    }

    void setHighpass (double fcHz, double sampleRate, double q) noexcept
    {
        setCoeffs (fcHz, sampleRate, q, false);
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

private:
    void setCoeffs (double fcHz, double sampleRate, double q, bool lowpass) noexcept
    {
        const double sr = (sampleRate > 0.0) ? sampleRate : 48000.0;
        const double nyquistGuard = 0.49 * sr;

        double fc = fcHz;
        if (fc < 10.0) fc = 10.0;
        if (fc > nyquistGuard) fc = nyquistGuard;

        const double qq = (q > 1.0e-6) ? q : 1.0e-6;

        const double w0 = 2.0 * kPi * fc / sr;
        const double cosw0 = std::cos (w0);
        const double sinw0 = std::sin (w0);
        const double alpha = sinw0 / (2.0 * qq);

        double nb0, nb1, nb2;
        if (lowpass)
        {
            nb0 = (1.0 - cosw0) / 2.0;
            nb1 = 1.0 - cosw0;
            nb2 = (1.0 - cosw0) / 2.0;
        }
        else
        {
            nb0 = (1.0 + cosw0) / 2.0;
            nb1 = -(1.0 + cosw0);
            nb2 = (1.0 + cosw0) / 2.0;
        }

        const double na0 = 1.0 + alpha;
        const double na1 = -2.0 * cosw0;
        const double na2 = 1.0 - alpha;

        const double invA0 = 1.0 / na0;
        b0 = static_cast<float> (nb0 * invA0);
        b1 = static_cast<float> (nb1 * invA0);
        b2 = static_cast<float> (nb2 * invA0);
        a1 = static_cast<float> (na1 * invA0);
        a2 = static_cast<float> (na2 * invA0);
    }

    static constexpr double kPi = 3.14159265358979323846;

    float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f };
    float a1 { 0.0f }, a2 { 0.0f };
    float z1 { 0.0f }, z2 { 0.0f };
};

} // namespace lflow
