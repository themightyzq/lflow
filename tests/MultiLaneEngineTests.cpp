#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "MultiLaneEngine.h"
#include <vector>
#include <cmath>

using namespace lflow;
using Catch::Matchers::WithinAbs;

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
