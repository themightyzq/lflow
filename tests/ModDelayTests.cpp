#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ModDelay.h"
#include <vector>
#include <cmath>
#include <random>

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

// Feeds `preRollZeros` zero samples, then one impulse (1.0f), then `postZeros`
// zero samples, all at a fixed delaySamples. Returns the impulse call's output
// followed by the post-impulse outputs, so index N of the returned vector is
// the output N samples after the impulse was written.
std::vector<float> runImpulseAndCapture (ModDelay& d, float delaySamples, int preRollZeros, int postZeros)
{
    for (int i = 0; i < preRollZeros; ++i)
        (void) d.process (0.0f, delaySamples);

    std::vector<float> out;
    out.reserve (static_cast<size_t> (postZeros) + 1);
    out.push_back (d.process (1.0f, delaySamples));
    for (int i = 0; i < postZeros; ++i)
        out.push_back (d.process (0.0f, delaySamples));
    return out;
}

} // namespace

TEST_CASE ("ModDelay integer delay: impulse reappears exactly N samples later (no wrap)", "[moddelay]")
{
    ModDelay d;
    d.prepare (48000.0, 100.0 / 48000.0); // capacity == 100

    const int N = 64;
    auto out = runImpulseAndCapture (d, static_cast<float> (N), 10, N + 5);

    for (size_t i = 0; i < out.size(); ++i)
    {
        if (static_cast<int> (i) == N)
            REQUIRE_THAT (out[i], WithinAbs (1.0, 1e-5));
        else
            REQUIRE_THAT (out[i], WithinAbs (0.0, 1e-5));
    }
}

TEST_CASE ("ModDelay integer delay: exact alignment across the ring wrap boundary", "[moddelay]")
{
    ModDelay d;
    d.prepare (48000.0, 100.0 / 48000.0); // capacity == 100

    const int N = 64;
    // Pre-roll several full traversals of the ring, chosen so the impulse
    // lands in the LAST physical slot (index capacity-1). When it is read
    // back N samples later the 4 Hermite taps sit at physical indices
    // {capacity-2, capacity-1, 0, 1} — the read window straddles the wrap
    // boundary exactly at the verified sample (the classic wrap bug site).
    const int cap = static_cast<int> (d.getMaxDelaySamples());
    const int preRoll = cap * 4 + (cap - 1);
    auto out = runImpulseAndCapture (d, static_cast<float> (N), preRoll, N + 5);

    for (size_t i = 0; i < out.size(); ++i)
    {
        if (static_cast<int> (i) == N)
            REQUIRE_THAT (out[i], WithinAbs (1.0, 1e-5));
        else
            REQUIRE_THAT (out[i], WithinAbs (0.0, 1e-5));
    }
}

TEST_CASE ("ModDelay fractional delay: steady sine amplitude preserved within 0.1 dB", "[moddelay]")
{
    ModDelay d;
    d.prepare (48000.0, 0.01); // capacity == 480

    const double sr = 48000.0;
    const int n = static_cast<int> (sr); // ~1s
    auto in = makeSine (440.0, sr, n);

    std::vector<float> out (in.size());
    for (size_t i = 0; i < in.size(); ++i)
        out[i] = d.process (in[i], 100.5f);

    REQUIRE_THAT (dBRatio (rmsTailHalf (out), rmsTailHalf (in)), WithinAbs (0.0, 0.1));
}

TEST_CASE ("ModDelay reset clears the line (impulse, reset, silence in -> silence out)", "[moddelay]")
{
    ModDelay d;
    d.prepare (48000.0, 0.01); // capacity == 480

    (void) d.process (1.0f, 50.3f);
    for (int i = 0; i < 100; ++i)
        (void) d.process (0.25f, 50.3f);

    d.reset();

    for (int i = 0; i < 500; ++i)
        REQUIRE_THAT (d.process (0.0f, 50.3f), WithinAbs (0.0, 1e-6));
}

TEST_CASE ("ModDelay clamps out-of-range delaySamples safely (finite output under random sweep)", "[moddelay]")
{
    ModDelay d;
    d.prepare (48000.0, 0.005); // capacity == 240

    const double capacity = d.getMaxDelaySamples();

    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> delayDist (0.0f, static_cast<float> (2.0 * capacity));
    std::uniform_real_distribution<float> inputDist (-1.0f, 1.0f);

    for (int i = 0; i < 5000; ++i)
    {
        const float delaySamples = delayDist (rng);
        const float input = inputDist (rng);
        const float out = d.process (input, delaySamples);
        REQUIRE (std::isfinite (out));
    }

    // Also verify below-minimum-margin and negative delays are handled safely.
    REQUIRE (std::isfinite (d.process (0.5f, 0.0f)));
    REQUIRE (std::isfinite (d.process (0.5f, -10.0f)));
}

TEST_CASE ("ModDelay getMaxDelaySamples matches prepare's allocated capacity", "[moddelay]")
{
    ModDelay d1;
    d1.prepare (48000.0, 0.01); // 480 samples exactly
    REQUIRE_THAT (d1.getMaxDelaySamples(), WithinAbs (480.0, 1e-9));

    ModDelay d2;
    d2.prepare (44100.0, 100.0 / 48000.0); // ~91.875 -> ceil -> 92
    REQUIRE_THAT (d2.getMaxDelaySamples(), WithinAbs (92.0, 1e-9));

    // Degenerate request (near-zero max delay) still yields a usable capacity
    // (minimum 8 samples so the [4, capacity-4] clamp range is non-empty).
    ModDelay d3;
    d3.prepare (48000.0, 0.0);
    REQUIRE (d3.getMaxDelaySamples() >= 8.0);

    // Negative maxDelaySeconds is treated like zero (minimum capacity).
    ModDelay d4;
    d4.prepare (48000.0, -1.0);
    REQUIRE (d4.getMaxDelaySamples() >= 8.0);
}

TEST_CASE ("ModDelay is safe-by-default: process before prepare produces finite output", "[moddelay]")
{
    // A default-constructed (never-prepared) ModDelay must not read or write
    // out of bounds; it degrades gracefully (minimum-capacity line).
    ModDelay d;
    for (int i = 0; i < 64; ++i)
        REQUIRE (std::isfinite (d.process (0.5f, 100.0f)));
}

TEST_CASE ("ModDelay minimum capacity: clamp range collapses to a point and stays exact", "[moddelay]")
{
    ModDelay d;
    d.prepare (48000.0, 0.0); // capacity == 8 -> clamp range [4, 4]
    REQUIRE_THAT (d.getMaxDelaySamples(), WithinAbs (8.0, 1e-9));

    // Every request clamps to exactly 4 samples; an impulse must come back
    // exactly 4 samples later regardless of the requested delay.
    auto out = runImpulseAndCapture (d, 100.0f, 20, 12); // 100 clamps to 4
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (static_cast<int> (i) == 4)
            REQUIRE_THAT (out[i], WithinAbs (1.0, 1e-5));
        else
            REQUIRE_THAT (out[i], WithinAbs (0.0, 1e-5));
    }
}

TEST_CASE ("ModDelay integer delay: exact alignment at the clamp boundaries (4 and capacity-4)", "[moddelay]")
{
    const double sr = 48000.0;

    // Lower clamp boundary: delaySamples == 4 exactly.
    {
        ModDelay d;
        d.prepare (sr, 100.0 / sr); // capacity == 100
        auto out = runImpulseAndCapture (d, 4.0f, 10, 12);
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (static_cast<int> (i) == 4)
                REQUIRE_THAT (out[i], WithinAbs (1.0, 1e-5));
            else
                REQUIRE_THAT (out[i], WithinAbs (0.0, 1e-5));
        }
    }

    // Upper clamp boundary: delaySamples == capacity - 4 exactly.
    {
        ModDelay d;
        d.prepare (sr, 100.0 / sr); // capacity == 100
        const int N = static_cast<int> (d.getMaxDelaySamples()) - 4; // 96
        auto out = runImpulseAndCapture (d, static_cast<float> (N), 10, N + 5);
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (static_cast<int> (i) == N)
                REQUIRE_THAT (out[i], WithinAbs (1.0, 1e-5));
            else
                REQUIRE_THAT (out[i], WithinAbs (0.0, 1e-5));
        }
    }
}

TEST_CASE ("ModDelay re-prepare: resizes capacity and clears all prior state", "[moddelay]")
{
    ModDelay d;
    d.prepare (48000.0, 0.01); // capacity == 480
    REQUIRE_THAT (d.getMaxDelaySamples(), WithinAbs (480.0, 1e-9));

    // Load the line with signal.
    for (int i = 0; i < 300; ++i)
        (void) d.process (0.7f, 200.0f);

    // Re-prepare at a new size (host sample-rate/config change).
    d.prepare (96000.0, 0.001); // capacity == 96
    REQUIRE_THAT (d.getMaxDelaySamples(), WithinAbs (96.0, 1e-9));

    // No residue from the previous life may survive the re-prepare.
    for (int i = 0; i < 200; ++i)
        REQUIRE_THAT (d.process (0.0f, 50.0f), WithinAbs (0.0, 1e-6));

    // And the resized line still delays exactly.
    auto out = runImpulseAndCapture (d, 32.0f, 10, 40);
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (static_cast<int> (i) == 32)
            REQUIRE_THAT (out[i], WithinAbs (1.0, 1e-5));
        else
            REQUIRE_THAT (out[i], WithinAbs (0.0, 1e-5));
    }
}
