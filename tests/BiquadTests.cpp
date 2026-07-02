#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Biquad.h"
#include <vector>
#include <cmath>

using namespace lflow;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kQButterworth = 0.70710678118654752440; // 1/sqrt(2)

// Local test-only helpers: full-amplitude sine + steady-state RMS over the tail half.
std::vector<float> makeSine (double freqHz, double sampleRate, int numSamples)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] =
            static_cast<float> (std::sin (2.0 * kPi * freqHz * static_cast<double> (i) / sampleRate));
    return out;
}

double rmsTailHalf (const std::vector<float>& v)
{
    const size_t start = v.size() / 2;
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = start; i < v.size(); ++i)
    {
        sum += static_cast<double> (v[i]) * static_cast<double> (v[i]);
        ++n;
    }
    return std::sqrt (sum / static_cast<double> (n));
}

double dBRatio (double outRms, double inRms)
{
    return 20.0 * std::log10 (outRms / inRms);
}

} // namespace

TEST_CASE ("Biquad lowpass: passband near unity, stopband attenuated >20dB", "[biquad]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr); // ~1s

    Biquad lp100;
    lp100.setLowpass (1000.0, sr, kQButterworth);
    auto in100 = makeSine (100.0, sr, n);
    std::vector<float> out100 (in100.size());
    for (size_t i = 0; i < in100.size(); ++i)
        out100[i] = lp100.process (in100[i]);

    REQUIRE_THAT (dBRatio (rmsTailHalf (out100), rmsTailHalf (in100)), WithinAbs (0.0, 0.5));

    Biquad lp10k;
    lp10k.setLowpass (1000.0, sr, kQButterworth);
    auto in10k = makeSine (10000.0, sr, n);
    std::vector<float> out10k (in10k.size());
    for (size_t i = 0; i < in10k.size(); ++i)
        out10k[i] = lp10k.process (in10k[i]);

    REQUIRE (dBRatio (rmsTailHalf (out10k), rmsTailHalf (in10k)) < -20.0);
}

TEST_CASE ("Biquad highpass: mirror of lowpass", "[biquad]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr);

    Biquad hp10k;
    hp10k.setHighpass (1000.0, sr, kQButterworth);
    auto in10k = makeSine (10000.0, sr, n);
    std::vector<float> out10k (in10k.size());
    for (size_t i = 0; i < in10k.size(); ++i)
        out10k[i] = hp10k.process (in10k[i]);

    REQUIRE_THAT (dBRatio (rmsTailHalf (out10k), rmsTailHalf (in10k)), WithinAbs (0.0, 0.5));

    Biquad hp100;
    hp100.setHighpass (1000.0, sr, kQButterworth);
    auto in100 = makeSine (100.0, sr, n);
    std::vector<float> out100 (in100.size());
    for (size_t i = 0; i < in100.size(); ++i)
        out100[i] = hp100.process (in100[i]);

    REQUIRE (dBRatio (rmsTailHalf (out100), rmsTailHalf (in100)) < -20.0);
}

TEST_CASE ("Biquad reset clears filter state (impulse, reset, silence in -> silence out)", "[biquad]")
{
    Biquad lp;
    lp.setLowpass (1000.0, 48000.0, kQButterworth);

    // Excite the filter with an impulse plus some settling samples.
    (void) lp.process (1.0f);
    for (int i = 0; i < 25; ++i)
        (void) lp.process (0.0f);

    lp.reset();

    for (int i = 0; i < 25; ++i)
        REQUIRE_THAT (lp.process (0.0f), WithinAbs (0.0, 1e-9));
}
