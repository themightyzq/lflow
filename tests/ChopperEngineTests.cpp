#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ChopperEngine.h"
#include <vector>

using namespace lflow;
using Catch::Matchers::WithinAbs;

static ChopperParams baseParams()
{
    ChopperParams p;
    p.waveform = Waveform::Square; // deterministic 1/0 gate
    p.sync = false;
    p.rateHz = 1.0;
    p.cycleBeats = 1.0;
    p.depth = 1.0f;
    p.mode = Mode::Tremolo;
    p.mix = 1.0f;
    p.smooth = 0.0f;               // no smoothing -> exact gates
    return p;
}

TEST_CASE ("tremolo at full depth+square gates the signal to zero on the low half", "[engine]")
{
    ChopperEngine e; e.prepare (1000.0, 512); e.reset();
    auto p = baseParams(); e.setParams (p);

    // 1 Hz at 1000 Hz sr: sample 0 -> phase 0 (square high -> gain 0),
    // sample 600 -> phase 0.6 (square low -> gain 1).
    std::vector<float> ch (1000, 1.0f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    REQUIRE_THAT (ch[0],   WithinAbs (0.0, 1e-5)); // gated out
    REQUIRE_THAT (ch[600], WithinAbs (1.0, 1e-5)); // passed
}

TEST_CASE ("mix=0 is pure dry passthrough", "[engine]")
{
    ChopperEngine e; e.prepare (1000.0, 512); e.reset();
    auto p = baseParams(); p.mix = 0.0f; e.setParams (p);

    std::vector<float> ch (1000, 0.7f);
    float* chans[1] = { ch.data() };
    e.process (chans, 1, 1000);

    for (float v : ch) REQUIRE_THAT (v, WithinAbs (0.7, 1e-6));
}

TEST_CASE ("pan mode drives L and R in antiphase", "[engine]")
{
    ChopperEngine e; e.prepare (1000.0, 512); e.reset();
    auto p = baseParams(); p.mode = Mode::Pan; e.setParams (p);

    std::vector<float> l (1000, 1.0f), r (1000, 1.0f);
    float* chans[2] = { l.data(), r.data() };
    e.process (chans, 2, 1000);

    // sample 0: phase 0 -> L square high -> gainL 0 -> L=0; R uses phase+0.5 -> low -> R=1
    REQUIRE_THAT (l[0], WithinAbs (0.0, 1e-5));
    REQUIRE_THAT (r[0], WithinAbs (1.0, 1e-5));
}

TEST_CASE ("sample & hold in pan mode steps independently per channel", "[engine]")
{
    ChopperEngine e; e.prepare (1000.0, 1024); e.reset();
    auto p = baseParams();
    p.waveform = Waveform::SampleHold;
    p.mode     = Mode::Pan;
    p.smooth   = 0.0f;
    e.setParams (p);

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
