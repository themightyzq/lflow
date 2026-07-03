#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "MultiLaneEngine.h"
#include "ModDelay.h"
#include "ShapeModel.h"
#include <vector>
#include <cmath>
#include <limits>

using namespace lflow;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Dest enum: Volume/Pan MUST keep indices 0/1 (Phase 1/2 session compat); Low/Mid/High
// are an append for Phase 3 band-level destinations.
static_assert (static_cast<int> (Dest::Volume) == 0, "Dest::Volume must stay index 0");
static_assert (static_cast<int> (Dest::Pan)    == 1, "Dest::Pan must stay index 1");
static_assert (static_cast<int> (Dest::Low)    == 2, "Dest::Low appended after Pan");
static_assert (static_cast<int> (Dest::Mid)    == 3, "Dest::Mid appended after Low");
static_assert (static_cast<int> (Dest::High)   == 4, "Dest::High appended after Mid");
static_assert (static_cast<int> (Dest::Pitch)  == 5, "Dest::Pitch appended after High (Phase 5)");

namespace {

constexpr double kPi = 3.14159265358979323846;

// Local test-only helpers, mirroring BiquadTests/LR4CrossoverTests conventions.
std::vector<float> makeSine (double freqHz, double sampleRate, int numSamples)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] =
            static_cast<float> (std::sin (2.0 * kPi * freqHz * static_cast<double> (i) / sampleRate));
    return out;
}

// Like makeSine, but with an explicit starting phase (radians) -- used to build
// decorrelated L/R stereo test content (Phase 4/5 folded hardening item).
std::vector<float> makeSinePhase (double freqHz, double sampleRate, int numSamples, double phaseRad)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] =
            static_cast<float> (std::sin (2.0 * kPi * freqHz * static_cast<double> (i) / sampleRate + phaseRad));
    return out;
}

// Rising zero-crossing frequency estimate over samples[startIdx, endIdx), using linear
// interpolation between samples for sub-sample crossing times, then dividing the total
// span between the first and last crossing by the number of periods it spans. Averaging
// over every crossing in the window (rather than a single period) suppresses per-crossing
// quantization noise, which matters here since we're measuring a moving-target
// instantaneous frequency over a short (20 ms) window.
double zeroCrossingFreq (const std::vector<float>& buf, int startIdx, int endIdx, double sampleRate)
{
    std::vector<double> crossTimes;
    for (int i = startIdx + 1; i < endIdx && i < static_cast<int> (buf.size()); ++i)
    {
        const float prev = buf[static_cast<size_t> (i - 1)];
        const float curr = buf[static_cast<size_t> (i)];
        if (prev < 0.0f && curr >= 0.0f)
        {
            const double frac = static_cast<double> (-prev) / static_cast<double> (curr - prev);
            const double samplePos = static_cast<double> (i - 1) + frac;
            crossTimes.push_back (samplePos / sampleRate);
        }
    }
    if (crossTimes.size() < 2)
        return 0.0;
    const double span = crossTimes.back() - crossTimes.front();
    const int numPeriods = static_cast<int> (crossTimes.size()) - 1;
    return static_cast<double> (numPeriods) / span;
}

double rmsRange (const std::vector<float>& v, size_t start, size_t end)
{
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = start; i < end && i < v.size(); ++i)
    {
        sum += static_cast<double> (v[i]) * static_cast<double> (v[i]);
        ++n;
    }
    return std::sqrt (sum / static_cast<double> (n));
}

double rmsTailHalf (const std::vector<float>& v)
{
    return rmsRange (v, v.size() / 2, v.size());
}

double dBRatio (double outRms, double inRms)
{
    return 20.0 * std::log10 (outRms / inRms);
}

} // namespace

// Deterministic square-wave volume lane, mirroring Phase 1's baseParams().
static LaneParams squareVolumeLane()
{
    LaneParams p;
    p.waveform = Waveform::Square;
    p.sync = false;
    p.rateHz = 1.0;
    p.cycleBeats = 1.0;
    p.depth = 1.0f;
    p.dest = Dest::Volume;
    p.phaseOffset = 0.0f;
    return p;
}

TEST_CASE ("phase1 parity: single volume lane gates a square wave to zero on the high half", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    e.setLaneParams (0, squareVolumeLane());
    e.setLaneParams (1, LaneParams{});  // depth 0 -> no-op
    e.setLaneParams (2, LaneParams{});  // depth 0 -> no-op
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    // 1 Hz at 1000 Hz sr: sample 0 -> phase 0 (square high -> gain 0),
    // sample 600 -> phase 0.6 (square low -> gain 1).
    std::vector<float> ch (1000, 1.0f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    REQUIRE_THAT (ch[0],   WithinAbs (0.0, 1e-5));
    REQUIRE_THAT (ch[600], WithinAbs (1.0, 1e-5));
}

TEST_CASE ("pan lane drives L and R in antiphase", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    auto p = squareVolumeLane(); p.dest = Dest::Pan;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> l (1000, 1.0f), r (1000, 1.0f);
    float* chans[2] = { l.data(), r.data() };
    e.process (chans, 2, 1000);

    // sample 0: phase 0 -> L square high -> gainL 0 -> L=0; R uses phase+0.5 -> low -> R=1
    REQUIRE_THAT (l[0], WithinAbs (0.0, 1e-5));
    REQUIRE_THAT (r[0], WithinAbs (1.0, 1e-5));
}

TEST_CASE ("two volume lanes at half depth multiply gains", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    LaneParams p0; p0.waveform = Waveform::Square; p0.depth = 0.5f; p0.dest = Dest::Volume;
    LaneParams p1 = p0;
    e.setLaneParams (0, p0);
    e.setLaneParams (1, p1);
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> ch (1000, 1.0f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    // sample 0: phase 0 -> square high -> mod 1 -> each lane gain (1 - 0.5*1) = 0.5;
    // combined gain = 0.5 * 0.5 = 0.25.
    REQUIRE_THAT (ch[0], WithinAbs (0.25, 1e-5));
}

TEST_CASE ("phase offset shifts the gating point", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    auto p = squareVolumeLane(); p.phaseOffset = 0.5f;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> ch (1000, 1.0f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    // Inverted vs. the parity test: offset by half a cycle flips which half is gated.
    REQUIRE_THAT (ch[0],   WithinAbs (1.0, 1e-5));
    REQUIRE_THAT (ch[600], WithinAbs (0.0, 1e-5));
}

TEST_CASE ("all lanes at depth 0 leave the buffer bit-identical regardless of mix", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    e.setLaneParams (0, LaneParams{});
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 0.37f; g.smooth = 0.15f;  // deliberately not the default: mix is irrelevant here
    e.setGlobalParams (g);

    std::vector<float> ch (1000);
    for (int i = 0; i < 1000; ++i)
        ch[static_cast<size_t> (i)] = std::sin (0.01f * static_cast<float> (i)) * 0.73f + 0.02f;
    const std::vector<float> original = ch;

    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    for (size_t i = 0; i < ch.size(); ++i)
        REQUIRE (ch[i] == original[i]);  // bit-exact no-op
}

TEST_CASE ("mix=0 is pure dry passthrough with an active lane", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    e.setLaneParams (0, squareVolumeLane());
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 0.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> ch (1000, 0.7f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    for (float v : ch) REQUIRE_THAT (v, WithinAbs (0.7, 1e-6));
}

TEST_CASE ("lane phase/value getters are coherent after a block", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    LaneParams p;
    p.waveform = Waveform::Sine;
    p.sync = false;
    p.rateHz = 1.0;
    p.depth = 1.0f;
    p.dest = Dest::Volume;
    p.phaseOffset = 0.25f;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    constexpr int N = 250;
    std::vector<float> ch (static_cast<size_t> (N), 1.0f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, N);

    const double raw = 249.0 / 1000.0 + 0.25;
    const float expectedPhase = static_cast<float> (raw - std::floor (raw));

    REQUIRE_THAT (e.getLanePhase (0), WithinAbs (expectedPhase, 1e-6));

    LfoCore reference;
    reference.setWaveform (Waveform::Sine);
    const float expectedValue = reference.valueAt (e.getLanePhase (0));

    REQUIRE_THAT (e.getLaneValue (0), WithinAbs (expectedValue, 1e-4));
}

TEST_CASE ("sample & hold in a pan lane steps independently per channel", "[multilane]")
{
    MultiLaneEngine e; e.prepare (1000.0, 1024); e.reset();
    LaneParams p;
    p.waveform = Waveform::SampleHold;
    p.dest = Dest::Pan;
    p.depth = 1.0f;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> l (1000, 1.0f), r (1000, 1.0f);
    float* chans[2] = { l.data(), r.data() };
    e.process (chans, 2, 1000);

    // L's phase runs 0..1 across the block (wrap only at sample 0): one held
    // S&H value for the whole cycle, so every L sample shares one gain.
    for (int n = 1; n < 1000; ++n)
        REQUIRE_THAT (l[n], WithinAbs (l[0], 1e-6));

    // R's phase is offset half a cycle, so it wraps once at sample 500:
    // constant within [0,499] and within [500,999].
    for (int n = 1; n < 500; ++n)
        REQUIRE_THAT (r[n], WithinAbs (r[0], 1e-6));
    for (int n = 501; n < 1000; ++n)
        REQUIRE_THAT (r[n], WithinAbs (r[500], 1e-6));
}

// ---------------------------------------------------------------------------
// Phase 3 Task 2: band-level lane destinations + Link lockstep fix.
// ---------------------------------------------------------------------------

TEST_CASE ("setCrossovers enforces effectiveHigh >= low*1.25", "[multilane][band]")
{
    MultiLaneEngine e; e.prepare (48000.0, 512); e.reset();

    // Defaults (250/2500) leave effectiveHigh untouched.
    REQUIRE_THAT (e.getEffectiveHighHz(), WithinAbs (2500.0, 1e-9));

    // Requested high (1000) is below low*1.25 (2500) -> clamps to 2500.
    e.setCrossovers (2000.0, 1000.0);
    REQUIRE_THAT (e.getEffectiveHighHz(), WithinAbs (2500.0, 1e-9));
}

TEST_CASE ("lane->Low destination gates a 100Hz sine and barely touches an 8kHz sine", "[multilane][band]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr);

    LaneParams lowLane;
    lowLane.waveform = Waveform::Square;
    lowLane.sync = false;
    lowLane.rateHz = 5.0;
    lowLane.depth = 1.0f;
    lowLane.dest = Dest::Low;
    lowLane.phaseOffset = 0.0f;

    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;

    // 100 Hz sine lives almost entirely in the low band, so gating that band gates
    // the tone. 5 Hz square @48kHz sr: period 9600 samples, gated half = [0,4800),
    // open half = [4800,9600). Measure well inside each half, clear of both the
    // gate edge and the crossover's own startup transient.
    {
        MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
        e.setLaneParams (0, lowLane);
        e.setLaneParams (1, LaneParams{});
        e.setLaneParams (2, LaneParams{});
        e.setGlobalParams (g);

        std::vector<float> buf = makeSine (100.0, sr, n);
        float* chans[1] = { buf.data() };
        e.process (chans, 1, n);

        const double gatedRms = rmsRange (buf, 1200, 3600);
        const double openRms  = rmsRange (buf, 6000, 8400);

        REQUIRE (gatedRms < 0.10 * openRms);
    }

    // 8 kHz sine carries negligible energy in the low band, so gating the low band
    // changes the summed output by well under 0.5 dB.
    {
        MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
        e.setLaneParams (0, lowLane);
        e.setLaneParams (1, LaneParams{});
        e.setLaneParams (2, LaneParams{});
        e.setGlobalParams (g);

        auto in = makeSine (8000.0, sr, n);
        std::vector<float> buf = in;
        float* chans[1] = { buf.data() };
        e.process (chans, 1, n);

        REQUIRE_THAT (dBRatio (rmsTailHalf (buf), rmsTailHalf (in)), WithinAbs (0.0, 0.5));
    }
}

TEST_CASE ("lane->High destination mirrors lane->Low: gates 8kHz, barely touches 100Hz", "[multilane][band]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr);

    LaneParams highLane;
    highLane.waveform = Waveform::Square;
    highLane.sync = false;
    highLane.rateHz = 5.0;
    highLane.depth = 1.0f;
    highLane.dest = Dest::High;
    highLane.phaseOffset = 0.0f;

    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;

    // 8 kHz sine lives almost entirely in the high band.
    {
        MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
        e.setLaneParams (0, highLane);
        e.setLaneParams (1, LaneParams{});
        e.setLaneParams (2, LaneParams{});
        e.setGlobalParams (g);

        std::vector<float> buf = makeSine (8000.0, sr, n);
        float* chans[1] = { buf.data() };
        e.process (chans, 1, n);

        const double gatedRms = rmsRange (buf, 1200, 3600);
        const double openRms  = rmsRange (buf, 6000, 8400);

        REQUIRE (gatedRms < 0.10 * openRms);
    }

    // 100 Hz sine carries negligible energy in the high band.
    {
        MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
        e.setLaneParams (0, highLane);
        e.setLaneParams (1, LaneParams{});
        e.setLaneParams (2, LaneParams{});
        e.setGlobalParams (g);

        auto in = makeSine (100.0, sr, n);
        std::vector<float> buf = in;
        float* chans[1] = { buf.data() };
        e.process (chans, 1, n);

        REQUIRE_THAT (dBRatio (rmsTailHalf (buf), rmsTailHalf (in)), WithinAbs (0.0, 0.5));
    }
}

TEST_CASE ("Link lockstep fix: a lane reactivated after sitting at depth 0 resumes at the phase it would have had",
           "[multilane]")
{
    auto square1Hz = []
    {
        LaneParams p;
        p.waveform = Waveform::Square;
        p.sync = false;
        p.rateHz = 1.0;
        p.cycleBeats = 1.0;
        p.dest = Dest::Volume;
        p.phaseOffset = 0.0f;
        return p;
    };

    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;

    // Reference: a single square Volume lane, active for the full 1000 samples
    // (two 1 Hz cycles @1kHz sr).
    MultiLaneEngine ref; ref.prepare (1000.0, 1024); ref.reset();
    auto refLane0 = square1Hz(); refLane0.depth = 1.0f;
    ref.setLaneParams (0, refLane0);
    ref.setLaneParams (1, LaneParams{});
    ref.setLaneParams (2, LaneParams{});
    ref.setGlobalParams (g);

    std::vector<float> refBuf (1000, 1.0f);
    float* refChans[1] = { refBuf.data() };
    ref.process (refChans, 1, 1000);

    // Under test: lane0 depth 1 from the start; lane1 (identical Square/1Hz/offset0)
    // starts at depth 0, then flips to depth 1 after 500 samples. With the lockstep
    // fix, lane1's clock keeps advancing at its own rate while inactive, so once
    // reactivated it is phase-aligned with lane0 -- their product reproduces exactly
    // what lane0 alone would have produced (0*0 == 0, 1*1 == 1 at every sample).
    MultiLaneEngine e; e.prepare (1000.0, 1024); e.reset();
    auto lane0 = square1Hz(); lane0.depth = 1.0f;
    auto lane1 = square1Hz(); lane1.depth = 0.0f;
    e.setLaneParams (0, lane0);
    e.setLaneParams (1, lane1);
    e.setLaneParams (2, LaneParams{});
    e.setGlobalParams (g);

    std::vector<float> buf (1000, 1.0f);
    float* chans[1] = { buf.data() };
    e.process (chans, 1, 500); // first half: lane1 still at depth 0

    lane1.depth = 1.0f;
    e.setLaneParams (1, lane1);
    float* chansTail[1] = { buf.data() + 500 };
    e.process (chansTail, 1, 500); // second half: lane1 now active

    for (int nIdx = 500; nIdx < 1000; ++nIdx)
        REQUIRE_THAT (buf[static_cast<size_t> (nIdx)], WithinAbs (refBuf[static_cast<size_t> (nIdx)], 1e-5));
}

// ---------------------------------------------------------------------------
// Phase 4 Task 1: Custom waveform (shape DSP) + folded Phase 3 stereo band-split test.
// ---------------------------------------------------------------------------

TEST_CASE ("stereo band-split: a Low-dest lane gates both channels identically (mono-gain band)",
           "[multilane][band][stereo]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr);

    LaneParams lowLane;
    lowLane.waveform = Waveform::Square;
    lowLane.sync = false;
    lowLane.rateHz = 5.0;
    lowLane.depth = 1.0f;
    lowLane.dest = Dest::Low;
    lowLane.phaseOffset = 0.0f;

    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;

    MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
    e.setLaneParams (0, lowLane);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    e.setGlobalParams (g);

    auto sine = makeSine (100.0, sr, n);
    std::vector<float> l = sine, r = sine; // identical stereo content in
    float* chans[2] = { l.data(), r.data() };
    e.process (chans, 2, n);

    // Band gain (bandGain[]) is computed once per sample and applied to each
    // channel's own crossover chain identically -- it isn't a per-channel (Pan-style)
    // gain. With identical L/R input and identically-prepared per-channel crossovers,
    // the two channels must gate in lockstep, sample for sample.
    for (int i = 0; i < n; ++i)
        REQUIRE_THAT (l[static_cast<size_t> (i)], WithinAbs (r[static_cast<size_t> (i)], 1e-6));
}

TEST_CASE ("engine: a Custom lane with a drawn near-square shape gates audio", "[multilane][custom]")
{
    // Near-square: high across [0,0.499], a fast transition around x=0.5 (steep
    // curve exponents bend the short seg toward/away from its endpoint quickly),
    // low across most of [0.501,1.0], with a sharp rise back to 1 right at the
    // wrap (x=1 meets x=0, both 1 -> continuous, no click at the loop point).
    ShapeNode nodes[4] = {
        { 0.0f,   1.0f,  0.0f },
        { 0.499f, 1.0f, -1.0f },
        { 0.501f, 0.0f,  1.0f },
        { 1.0f,   1.0f,  0.0f }
    };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 4, table, kShapeTableSize);

    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    e.setCustomTable (0, table, kShapeTableSize);

    LaneParams p;
    p.waveform = Waveform::Custom;
    p.sync = false;
    p.rateHz = 1.0;
    p.depth = 1.0f;
    p.dest = Dest::Volume;
    p.phaseOffset = 0.0f;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> ch (1000, 1.0f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    // 1 Hz @ 1kHz sr: sample 0 -> phase 0 -> shape ~1 -> gain ~0 (gated closed).
    // sample 700 -> phase 0.7 -> shape ~0 -> gain ~1 (fully open).
    REQUIRE_THAT (ch[0],   WithinAbs (0.0, 0.02));
    REQUIRE_THAT (ch[700], WithinAbs (1.0, 0.02));
}

TEST_CASE ("engine: a follower lane fed lane0's table via setCustomTable reproduces lane0's values",
           "[multilane][custom]")
{
    ShapeNode nodes[3] = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.5f }, { 1.0f, 0.0f, 0.0f } };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 3, table, kShapeTableSize);

    LaneParams p;
    p.waveform = Waveform::Custom;
    p.sync = false;
    p.rateHz = 1.0;
    p.depth = 1.0f;
    p.dest = Dest::Volume;
    p.phaseOffset = 0.0f;

    // Reference: lane0 alone, given the table.
    MultiLaneEngine ref; ref.prepare (1000.0, 512); ref.reset();
    ref.setCustomTable (0, table, kShapeTableSize);
    ref.setLaneParams (0, p);
    ref.setLaneParams (1, LaneParams{});
    ref.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    ref.setGlobalParams (g);

    std::vector<float> refBuf (1000, 1.0f);
    float* refChans[1] = { refBuf.data() };
    ref.process (refChans, 1, 1000);

    // Under test: mirrors how the processor hands a linked follower lane1's table
    // (here: lane0's) when its resolved waveform is Custom -- both lanes get the
    // SAME table pointer and identical params.
    MultiLaneEngine e; e.prepare (1000.0, 512); e.reset();
    e.setCustomTable (0, table, kShapeTableSize);
    e.setCustomTable (1, table, kShapeTableSize); // follower fed lane0's table
    e.setLaneParams (0, p);
    e.setLaneParams (1, p);
    e.setLaneParams (2, LaneParams{});
    e.setGlobalParams (g);

    std::vector<float> buf (1000, 1.0f);
    float* chans[1] = { buf.data() };
    e.process (chans, 1, 1000);

    // Per-sample modulator values line up exactly: the follower reproduces lane0's
    // values, and lane0 itself is unaffected by having a sibling on the same table.
    REQUIRE_THAT (e.getLaneValue (1), WithinAbs (e.getLaneValue (0), 1e-6));
    REQUIRE_THAT (e.getLaneValue (0), WithinAbs (ref.getLaneValue (0), 1e-6));
}

// ---------------------------------------------------------------------------
// Phase 5 Task 2: Pitch lane destination (LFO-modulated delay / vibrato) + folded
// Phase 4 review hardening (decorrelated-L/R stereo band test; ShapeManager/engine
// lane-count parity static_assert, added in source/shapes/ShapeManager.cpp since it's
// the file that sees both constants).
// ---------------------------------------------------------------------------

TEST_CASE ("Pitch lane: vibrato deviation matches the analytic Doppler ratio at LFO extremes",
           "[multilane][pitch]")
{
    const double sr = 48000.0;
    const int n = static_cast<int> (sr); // 1.0 s

    LaneParams pitchLane;
    pitchLane.waveform = Waveform::Sine;
    pitchLane.sync = false;
    pitchLane.rateHz = 5.0;
    pitchLane.depth = 1.0f;
    pitchLane.dest = Dest::Pitch;
    pitchLane.phaseOffset = 0.0f;

    MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
    e.setLaneParams (0, pitchLane);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    const double carrierHz = 440.0;
    std::vector<float> buf = makeSine (carrierHz, sr, n);
    float* chans[1] = { buf.data() };
    e.process (chans, 1, n);

    // --- Analytic ground truth ---
    // Engine mapping (one Pitch lane, depth=1): mod(phase) = 0.5 + 0.5*sin(2*pi*phase)
    // (LfoCore::valueAt, Sine, unipolar). swingMs = (mod-0.5)*2*depth*kMaxSwingMs
    //         = (0.5*sin(2*pi*phase)) * 2 * 1 * 10 = 10*sin(2*pi*phase).
    // Lane rateHz=5, phaseOffset=0 -> phase(t) = frac(5t), so (periodic in phase)
    //         swingMs(t) = 10*sin(2*pi*5*t) = 10*sin(theta), theta = 2*pi*5*t.
    // delay(t) [seconds] = (kCenterMs + swingMs(t)) / 1000 = (12 + 10*sin(theta)) / 1000.
    // d(delay)/dt = (10/1000) * (2*pi*5) * cos(theta) = A*omega*cos(theta),
    //   A = kMaxSwingMs/1000 = 0.01 s, omega = 2*pi*5 = 31.415927 rad/s
    //   max |d(delay)/dt| = A*omega = 0.01 * 31.415927 = 0.31415927  (matches spec's ~0.314)
    // cos(theta) = +1 at theta = 0, 2*pi, ... -> t = 0, 0.2, 0.4, ... (down-shift instant)
    // cos(theta) = -1 at theta = pi, 3*pi, ... -> t = 0.1, 0.3, 0.5, ... (up-shift instant)
    // Doppler-style instantaneous frequency: output(t) = carrier(t - delay(t)), so the
    // local "read time" read(t) = t - delay(t) advances at rate d(read)/dt = 1 - delay'(t);
    // measured frequency f' = f * d(read)/dt = f * (1 - d(delay)/dt).
    //   up-shift   (t=0.1s, delay'=-0.31415927): f' = 440 * (1 - (-0.31415927)) = 440 * 1.31415927 = 578.23 Hz
    //   down-shift (t=0.2s, delay'=+0.31415927): f' = 440 * (1 -  0.31415927)  = 440 * 0.68584073 = 301.77 Hz
    // (matches the spec's "~0.69*440 .. ~1.31*440" window.)
    const double A = 10.0 / 1000.0;
    const double omega = 2.0 * kPi * 5.0;
    const double maxSlope = A * omega; // ~0.3141593
    const double fUp   = carrierHz * (1.0 + maxSlope);
    const double fDown = carrierHz * (1.0 - maxSlope);

    const double windowSec = 0.020; // 20 ms
    const int windowSamples = static_cast<int> (windowSec * sr);

    // Up-shift window, centered at t=0.1s (theta=pi).
    {
        const int center = static_cast<int> (0.1 * sr);
        const int start = center - windowSamples / 2;
        const int end   = center + windowSamples / 2;
        const double measured = zeroCrossingFreq (buf, start, end, sr);
        REQUIRE (measured > 0.0);
        REQUIRE_THAT (measured, WithinRel (fUp, 0.10));
    }

    // Down-shift window, centered at t=0.2s (theta=2*pi).
    {
        const int center = static_cast<int> (0.2 * sr);
        const int start = center - windowSamples / 2;
        const int end   = center + windowSamples / 2;
        const double measured = zeroCrossingFreq (buf, start, end, sr);
        REQUIRE (measured > 0.0);
        REQUIRE_THAT (measured, WithinRel (fDown, 0.10));
    }
}

TEST_CASE ("Pitch lane: a static (held) mod produces a constant delay, matching a bare ModDelay",
           "[multilane][pitch]")
{
    // Square, rateHz=0.1 (period 10s): held HIGH (mod==1.0 exactly) for phase in [0,0.5),
    // i.e. for the first 5 seconds. depth=1 -> swingMs = (1.0-0.5)*2*1*10 = 10 (constant,
    // exact float arithmetic, no drift) for as long as the mod is held -- our comparison
    // window (well inside that half-cycle) sees an EXACTLY constant delaySamples the whole
    // time, so the mapping is time-invariant (no pitch bend) whenever the mod itself is.
    const double sr = 48000.0;
    const int n = static_cast<int> (sr); // 1.0 s -- comfortably inside the first 5s half-cycle

    LaneParams pitchLane;
    pitchLane.waveform = Waveform::Square;
    pitchLane.sync = false;
    pitchLane.rateHz = 0.1;
    pitchLane.depth = 1.0f;
    pitchLane.dest = Dest::Pitch;
    pitchLane.phaseOffset = 0.0f;

    MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
    e.setLaneParams (0, pitchLane);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> buf = makeSine (300.0, sr, n);
    const std::vector<float> input = buf; // keep a copy to feed the reference ModDelay
    float* chans[1] = { buf.data() };
    e.process (chans, 1, n);

    // Reference: the SAME input, sample-for-sample, through a bare ModDelay at the
    // expected constant delay. ModDelay::process() writes `in` unconditionally before
    // reading, so its internal ring-buffer CONTENT/writePos trajectory depends only on
    // the input sequence -- never on the delaySamples argument used on any call. That
    // means a fresh ModDelay fed the identical input from sample 0 has bit-identical
    // buffer content to the engine's internal pitchDelay at every sample n, so the two
    // outputs match wherever the delaySamples used at that n also match (true here, for
    // the whole buffer, since the mod is held constant throughout).
    const double expectedSwingMs = (1.0 - 0.5) * 2.0 * 1.0 * MultiLaneEngine::kMaxSwingMs;
    const double expectedDelaySamples = (MultiLaneEngine::kCenterMs + expectedSwingMs) * sr / 1000.0;

    ModDelay ref; ref.prepare (sr, 0.064);
    std::vector<float> refOut (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        refOut[static_cast<size_t> (i)] = ref.process (input[static_cast<size_t> (i)],
                                                         static_cast<float> (expectedDelaySamples));

    // Compare only from well past the delay-line's own fill-in (the first
    // ~expectedDelaySamples samples read mostly zero history in BOTH the engine and the
    // reference -- they still match each other there, but we skip them anyway to keep
    // this test's intent -- "matches a constant-delay reference" -- unambiguous).
    const size_t skip = static_cast<size_t> (expectedDelaySamples) + 8;
    for (size_t i = skip; i < static_cast<size_t> (n); ++i)
        REQUIRE_THAT (buf[i], WithinAbs (refOut[i], 1e-5));
}

TEST_CASE ("Pitch lane: two full-depth same-phase lanes sum then clamp at +/-kMaxSwingMs (not 2x)",
           "[multilane][pitch]")
{
    // Two identical Sine/1Hz/depth1/Pitch lanes, same phase: unclamped swing would be
    // 2*10*sin(theta) = 20*sin(theta), which exceeds +/-kMaxSwingMs (10) whenever
    // |sin(theta)| > 0.5 -- true for theta in [pi/6, 5pi/6] (and the mirror negative
    // lobe). At rateHz=1, that's t in [1/12, 5/12] = [0.0833s, 0.4167s] for the clamped
    // TOP (+10). We pick a window well inside that flat region -- [0.15s, 0.25s] -- where
    // the clamped analytic swing is EXACTLY +kMaxSwingMs the whole time (not just at an
    // instant), so (as in the static-mod test above) the engine's output there must match
    // a bare ModDelay at the CLAMPED constant delay, not the (unreachable) unclamped one.
    const double sr = 48000.0;
    const int n = static_cast<int> (sr); // 1.0 s

    LaneParams lane;
    lane.waveform = Waveform::Sine;
    lane.sync = false;
    lane.rateHz = 1.0;
    lane.depth = 1.0f;
    lane.dest = Dest::Pitch;
    lane.phaseOffset = 0.0f;

    MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
    e.setLaneParams (0, lane);
    e.setLaneParams (1, lane); // identical second Pitch lane, same phase -> sums, then clamps
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    std::vector<float> buf = makeSine (300.0, sr, n);
    const std::vector<float> input = buf;
    float* chans[1] = { buf.data() };
    e.process (chans, 1, n);

    const double clampedDelaySamples = (MultiLaneEngine::kCenterMs + MultiLaneEngine::kMaxSwingMs) * sr / 1000.0;
    // Sanity: confirm the UNCLAMPED sum really would exceed the clamp at our window (i.e.
    // this test is actually exercising the clamp, not a no-op).
    const double unclampedSwingAtWindow = 20.0 * std::sin (2.0 * kPi * 1.0 * 0.20);
    REQUIRE (unclampedSwingAtWindow > MultiLaneEngine::kMaxSwingMs);

    ModDelay ref; ref.prepare (sr, 0.064);
    std::vector<float> refOut (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        refOut[static_cast<size_t> (i)] = ref.process (input[static_cast<size_t> (i)],
                                                         static_cast<float> (clampedDelaySamples));

    const int windowStart = static_cast<int> (0.15 * sr);
    const int windowEnd   = static_cast<int> (0.25 * sr);
    for (int i = windowStart; i < windowEnd; ++i)
        REQUIRE_THAT (buf[static_cast<size_t> (i)], WithinAbs (refOut[static_cast<size_t> (i)], 1e-5));
}

TEST_CASE ("stereo band-split: decorrelated L/R (different-phase sines) still gate each channel independently correct",
           "[multilane][band][stereo]")
{
    // Folded Phase 4 review hardening item: the earlier stereo band-split test used
    // IDENTICAL L/R content, which can't distinguish "each channel gated independently"
    // from "some accidental cross-channel coupling that happens to look right on matched
    // input." Here L and R carry the SAME 100 Hz tone but at DIFFERENT phases, so a bug
    // that leaked L's crossover state into R's (or vice versa) would show up as differing
    // gated/open behavior between the two channels' own expectations.
    const double sr = 48000.0;
    const int n = static_cast<int> (sr);

    LaneParams lowLane;
    lowLane.waveform = Waveform::Square;
    lowLane.sync = false;
    lowLane.rateHz = 5.0;
    lowLane.depth = 1.0f;
    lowLane.dest = Dest::Low;
    lowLane.phaseOffset = 0.0f;

    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;

    MultiLaneEngine e; e.prepare (sr, 4096); e.reset();
    e.setLaneParams (0, lowLane);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    e.setGlobalParams (g);

    // L at phase 0, R at phase pi/3 -- decorrelated, same 100 Hz frequency.
    std::vector<float> l = makeSinePhase (100.0, sr, n, 0.0);
    std::vector<float> r = makeSinePhase (100.0, sr, n, kPi / 3.0);
    float* chans[2] = { l.data(), r.data() };
    e.process (chans, 2, n);

    // Same 5 Hz square gate (rateHz=5, sr=48000): period 9600 samples, gated half
    // [0,4800), open half [4800,9600) -- identical windows used by the mono Low test.
    // Each channel is checked against ITS OWN dB ratio (not cross-compared to the other),
    // proving independent per-channel gating rather than coincidental symmetry.
    const double gatedRmsL = rmsRange (l, 1200, 3600);
    const double openRmsL  = rmsRange (l, 6000, 8400);
    REQUIRE (gatedRmsL < 0.10 * openRmsL);

    const double gatedRmsR = rmsRange (r, 1200, 3600);
    const double openRmsR  = rmsRange (r, 6000, 8400);
    REQUIRE (gatedRmsR < 0.10 * openRmsR);
}

// ---------------------------------------------------------------------------
// Phase 7 Task 1: DSP NaN/robustness hardening (QA C1, H1, L1, L3). Repro
// recipes ported from the adversarial QA sweep's throwaway probes
// (probe.cpp/probe2.cpp, archived at /tmp/qa-probes/ at review time) into
// permanent regression tests. See docs/superpowers/specs/
// 2026-07-03-lflow-phase7-hardening-tablestakes-design.md section A.
// ---------------------------------------------------------------------------

namespace {
bool allFinite (const float* buf, int n)
{
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (buf[i]))
            return false;
    return true;
}
} // namespace

TEST_CASE ("C1: a NaN block into a Low-band lane does not permanently poison the band filters",
           "[multilane][hardening][C1]")
{
    MultiLaneEngine e; e.prepare (48000.0, 512); e.reset();
    LaneParams p; p.dest = Dest::Low; p.depth = 0.5f; p.waveform = Waveform::Sine; p.rateHz = 2.0;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    const int blockSize = 64;
    std::vector<float> L (static_cast<size_t> (blockSize)), R (static_cast<size_t> (blockSize));
    float* chans[2] = { L.data(), R.data() };

    // One block of NaN (probe.cpp PROBE 1 recipe).
    for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = std::nanf (""); R[static_cast<size_t> (i)] = std::nanf (""); }
    e.process (chans, 2, blockSize);

    // 10 clean sine blocks -- old code: permanently NaN forever after. New code
    // must be finite from the 2nd clean block on (allowing the block that
    // absorbs the bad input its own transient).
    for (int blk = 0; blk < 10; ++blk)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = 0.1f * std::sin (0.1f * static_cast<float> (blk * blockSize + i));
            L[static_cast<size_t> (i)] = v; R[static_cast<size_t> (i)] = v;
        }
        e.process (chans, 2, blockSize);

        if (blk >= 1)
        {
            REQUIRE (allFinite (L.data(), blockSize));
            REQUIRE (allFinite (R.data(), blockSize));
        }
    }
}

TEST_CASE ("C1: a single Inf sample into a Mid-band lane does not permanently poison the band filters",
           "[multilane][hardening][C1]")
{
    MultiLaneEngine e; e.prepare (48000.0, 512); e.reset();
    LaneParams p; p.dest = Dest::Mid; p.depth = 0.5f; p.waveform = Waveform::Sine; p.rateHz = 2.0;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    const int blockSize = 64;
    std::vector<float> L (static_cast<size_t> (blockSize), 0.1f), R (static_cast<size_t> (blockSize), 0.1f);
    L[10] = std::numeric_limits<float>::infinity();
    float* chans[2] = { L.data(), R.data() };
    e.process (chans, 2, blockSize);

    for (int blk = 0; blk < 10; ++blk)
    {
        for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = 0.1f; R[static_cast<size_t> (i)] = 0.1f; }
        e.process (chans, 2, blockSize);

        if (blk >= 1)
        {
            REQUIRE (allFinite (L.data(), blockSize));
            REQUIRE (allFinite (R.data(), blockSize));
        }
    }
}

TEST_CASE ("C1: NaN fed while every lane is at depth 0 does not stop a later-activated band lane from being finite",
           "[multilane][hardening][C1]")
{
    // The engine still "runs" (advances lane clocks) even when every lane is at
    // depth 0 -- the reachability note in the QA finding ("poisons even while
    // bypassed"). Verifies that path stays inert with respect to filter state.
    MultiLaneEngine e; e.prepare (48000.0, 512); e.reset();
    e.setLaneParams (0, LaneParams{});
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    const int blockSize = 64;
    std::vector<float> L (static_cast<size_t> (blockSize)), R (static_cast<size_t> (blockSize));
    float* chans[2] = { L.data(), R.data() };
    for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = std::nanf (""); R[static_cast<size_t> (i)] = std::nanf (""); }
    e.process (chans, 2, blockSize);

    LaneParams p; p.dest = Dest::Low; p.depth = 0.7f; p.waveform = Waveform::Sine; p.rateHz = 2.0;
    e.setLaneParams (0, p);

    for (int blk = 0; blk < 5; ++blk)
    {
        for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = 0.2f; R[static_cast<size_t> (i)] = 0.2f; }
        e.process (chans, 2, blockSize);
        REQUIRE (allFinite (L.data(), blockSize));
        REQUIRE (allFinite (R.data(), blockSize));
    }
}

TEST_CASE ("H1: a NaN custom-table sample does not permanently latch the onePole smoother",
           "[multilane][hardening][H1]")
{
    MultiLaneEngine e; e.prepare (48000.0, 64); e.reset();
    LaneParams p;
    p.dest = Dest::Volume;
    p.depth = 0.5f;
    p.waveform = Waveform::Custom;
    p.rateHz = 20.0; // fast: sweeps many cycles per block, guarantees hitting the NaN sample
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.5f; // smoothing ON (H1 requires Smooth>0)
    e.setGlobalParams (g);

    static float tbl[kShapeTableSize];
    for (float& v : tbl) v = 0.5f;
    tbl[128] = std::nanf ("");
    e.setCustomTable (0, tbl, kShapeTableSize);

    const int blockSize = 64;
    std::vector<float> L (static_cast<size_t> (blockSize), 0.2f), R (static_cast<size_t> (blockSize), 0.2f);
    float* chans[2] = { L.data(), R.data() };

    // Sweep across the NaN sample repeatedly (probe2.cpp PROBE 2 recipe).
    for (int blk = 0; blk < 100; ++blk)
    {
        for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = 0.2f; R[static_cast<size_t> (i)] = 0.2f; }
        e.process (chans, 2, blockSize);
    }

    // Swap to an all-finite table.
    for (float& v : tbl) v = 0.5f;
    e.setCustomTable (0, tbl, kShapeTableSize);

    for (int blk = 0; blk < 2; ++blk)
    {
        for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = 0.2f; R[static_cast<size_t> (i)] = 0.2f; }
        e.process (chans, 2, blockSize);
    }
    // Old code: still non-finite here (state permanently latched). New code:
    // recovered within these 2 post-swap blocks.
    REQUIRE (allFinite (L.data(), blockSize));
    REQUIRE (allFinite (R.data(), blockSize));

    // And it stays recovered -- not a one-block fluke.
    for (int blk = 0; blk < 20; ++blk)
    {
        for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = 0.2f; R[static_cast<size_t> (i)] = 0.2f; }
        e.process (chans, 2, blockSize);
        REQUIRE (allFinite (L.data(), blockSize));
        REQUIRE (allFinite (R.data(), blockSize));
    }
}

TEST_CASE ("L3: a single NaN input sample into a Pitch lane recovers within 2 blocks (old code: ~8-9 blocks)",
           "[multilane][hardening][L3]")
{
    MultiLaneEngine e; e.prepare (48000.0, 64); e.reset();
    LaneParams p; p.dest = Dest::Pitch; p.depth = 0.5f; p.waveform = Waveform::Sine; p.rateHz = 2.0;
    e.setLaneParams (0, p);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    GlobalParams g; g.mix = 1.0f; g.smooth = 0.0f;
    e.setGlobalParams (g);

    const int blockSize = 64;
    std::vector<float> L (static_cast<size_t> (blockSize), 0.1f), R (static_cast<size_t> (blockSize), 0.1f);
    L[0] = std::nanf ("");
    float* chans[2] = { L.data(), R.data() };
    e.process (chans, 2, blockSize);

    for (int blk = 0; blk < 2; ++blk)
    {
        for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = 0.1f; R[static_cast<size_t> (i)] = 0.1f; }
        e.process (chans, 2, blockSize);
    }
    REQUIRE (allFinite (L.data(), blockSize));
    REQUIRE (allFinite (R.data(), blockSize));

    // Stays clean afterward (bounded, self-healed permanently -- not permanent
    // poisoning and not a relapse).
    for (int blk = 0; blk < 20; ++blk)
    {
        for (int i = 0; i < blockSize; ++i) { L[static_cast<size_t> (i)] = 0.1f; R[static_cast<size_t> (i)] = 0.1f; }
        e.process (chans, 2, blockSize);
        REQUIRE (allFinite (L.data(), blockSize));
        REQUIRE (allFinite (R.data(), blockSize));
    }
}

TEST_CASE ("L1: reactivating a lane after a stale-state inactive stretch snaps straight to the target "
           "(matches a fresh engine at the identical phase, no glide from stale state)",
           "[multilane][hardening][L1]")
{
    // rateHz=1 @ sr=1000: 500 elapsed samples (200 primed-active + 300 inactive)
    // land the clock's phase at EXACTLY 0.5 (500/1000), so a from-scratch "fresh"
    // engine given phaseOffset=0.5 evaluates the identical target on its very
    // first (also inactive->active edge) sample -- a clean, phase-matched oracle
    // for "the target," with no history-replay needed.
    const double sr = 1000.0;

    LaneParams active;
    active.waveform = Waveform::Sine;
    active.dest = Dest::Volume;
    active.rateHz = 1.0;
    active.depth = 1.0f;
    active.phaseOffset = 0.0f;

    GlobalParams g; g.mix = 1.0f; g.smooth = 0.5f; // Smooth>0, per the L1 repro

    // --- "test" engine: primed active (builds a stale, non-zero smoothL via the
    // one-pole glide), then depth 0 for 300 samples (smoother frozen while the
    // clock keeps advancing -- the lockstep fix), then reactivated.
    MultiLaneEngine e; e.prepare (sr, 2048); e.reset();
    e.setGlobalParams (g);
    e.setLaneParams (0, active);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});

    {
        std::vector<float> buf (200, 1.0f);
        float* chans[1] = { buf.data() };
        e.process (chans, 1, 200); // prime: smoothL glides toward a HIGH-ish target
    }

    auto inactive = active; inactive.depth = 0.0f;
    e.setLaneParams (0, inactive);
    {
        std::vector<float> buf (300, 1.0f);
        float* chans[1] = { buf.data() };
        e.process (chans, 1, 300); // depth 0: smoothL frozen stale, clock keeps ticking
    }

    e.setLaneParams (0, active); // reactivate
    std::vector<float> testBuf (1, 1.0f);
    float* testChans[1] = { testBuf.data() };
    e.process (testChans, 1, 1);
    const float testFirstValue = e.getLaneValue (0);

    // --- "fresh" reference engine: never previously run (its own inactive->
    // active edge is the implicit one from construction -- wasActive starts
    // false), activated from sample 0 with a phaseOffset that lands it on the
    // SAME phase the test engine reactivates at.
    MultiLaneEngine fresh; fresh.prepare (sr, 2048); fresh.reset();
    fresh.setGlobalParams (g);
    auto freshActive = active; freshActive.phaseOffset = 0.5f;
    fresh.setLaneParams (0, freshActive);
    fresh.setLaneParams (1, LaneParams{});
    fresh.setLaneParams (2, LaneParams{});

    std::vector<float> freshBuf (1, 1.0f);
    float* freshChans[1] = { freshBuf.data() };
    fresh.process (freshChans, 1, 1);
    const float freshFirstValue = fresh.getLaneValue (0);

    // Old code: the test engine glides from its stale (primed) state, the fresh
    // engine glides from its zero-initialized state -- different starting
    // points, so they diverge (genuine RED). New code: both snap straight to
    // the (identical) target on their inactive->active edge, so they match.
    REQUIRE_THAT (testFirstValue, WithinAbs (freshFirstValue, 1e-6));
}
