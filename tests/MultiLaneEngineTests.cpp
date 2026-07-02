#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "MultiLaneEngine.h"
#include <vector>
#include <cmath>

using namespace lflow;
using Catch::Matchers::WithinAbs;

// Dest enum: Volume/Pan MUST keep indices 0/1 (Phase 1/2 session compat); Low/Mid/High
// are an append for Phase 3 band-level destinations.
static_assert (static_cast<int> (Dest::Volume) == 0, "Dest::Volume must stay index 0");
static_assert (static_cast<int> (Dest::Pan)    == 1, "Dest::Pan must stay index 1");
static_assert (static_cast<int> (Dest::Low)    == 2, "Dest::Low appended after Pan");
static_assert (static_cast<int> (Dest::Mid)    == 3, "Dest::Mid appended after Low");
static_assert (static_cast<int> (Dest::High)   == 4, "Dest::High appended after Mid");

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
