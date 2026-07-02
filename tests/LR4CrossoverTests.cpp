#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "LR4Crossover.h"
#include <vector>
#include <cmath>

using namespace lflow;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi = 3.14159265358979323846;

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

TEST_CASE ("LR4Crossover flat sum: low+high RMS equals input RMS within +/-0.2dB at 100/1k/8k through a 1kHz split",
           "[lr4]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr);

    for (double freq : { 100.0, 1000.0, 8000.0 })
    {
        LR4Crossover xo;
        xo.prepare (sr);
        xo.setFrequency (1000.0);

        auto in = makeSine (freq, sr, n);
        std::vector<float> sum (in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            float low = 0.0f, high = 0.0f;
            xo.split (in[i], low, high);
            sum[i] = low + high;
        }

        INFO ("freq = " << freq);
        REQUIRE_THAT (dBRatio (rmsTailHalf (sum), rmsTailHalf (in)), WithinAbs (0.0, 0.2));
    }
}

TEST_CASE ("LR4Crossover band isolation: 100Hz stays in low band, 8kHz mirror in high band", "[lr4]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr);

    {
        LR4Crossover xo;
        xo.prepare (sr);
        xo.setFrequency (1000.0);

        auto in = makeSine (100.0, sr, n);
        std::vector<float> low (in.size()), high (in.size());
        for (size_t i = 0; i < in.size(); ++i)
            xo.split (in[i], low[i], high[i]);

        const double inRms = rmsTailHalf (in);
        REQUIRE_THAT (dBRatio (rmsTailHalf (low), inRms), WithinAbs (0.0, 0.5));
        REQUIRE (dBRatio (rmsTailHalf (high), inRms) < -35.0);
    }

    {
        LR4Crossover xo;
        xo.prepare (sr);
        xo.setFrequency (1000.0);

        auto in = makeSine (8000.0, sr, n);
        std::vector<float> low (in.size()), high (in.size());
        for (size_t i = 0; i < in.size(); ++i)
            xo.split (in[i], low[i], high[i]);

        const double inRms = rmsTailHalf (in);
        REQUIRE_THAT (dBRatio (rmsTailHalf (high), inRms), WithinAbs (0.0, 0.5));
        REQUIRE (dBRatio (rmsTailHalf (low), inRms) < -35.0);
    }
}

TEST_CASE ("LR4Crossover reset clears state (impulse, reset, silence in -> silence out)", "[lr4]")
{
    LR4Crossover xo;
    xo.prepare (48000.0);
    xo.setFrequency (1000.0);

    float low = 0.0f, high = 0.0f;
    xo.split (1.0f, low, high);
    for (int i = 0; i < 25; ++i)
        xo.split (0.0f, low, high);

    xo.reset();

    for (int i = 0; i < 25; ++i)
    {
        xo.split (0.0f, low, high);
        REQUIRE_THAT (low, WithinAbs (0.0, 1e-9));
        REQUIRE_THAT (high, WithinAbs (0.0, 1e-9));
    }
}
