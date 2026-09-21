#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "LfoClock.h"

using lflow::LfoClock;
using Catch::Matchers::WithinAbs;

TEST_CASE ("syncedHz converts bpm + cycle-beats to cycles per second", "[lfoclock]")
{
    REQUIRE_THAT (LfoClock::syncedHz (120.0, 1.0), WithinAbs (2.0, 1e-9)); // 1/4 @120 = 2 Hz
    REQUIRE_THAT (LfoClock::syncedHz (120.0, 0.5), WithinAbs (4.0, 1e-9)); // 1/8 @120 = 4 Hz
    REQUIRE_THAT (LfoClock::syncedHz (120.0, 4.0), WithinAbs (0.5, 1e-9)); // whole @120 = 0.5 Hz
}

TEST_CASE ("advance accumulates and wraps into the half-open range 0 to 1", "[lfoclock]")
{
    LfoClock c; c.prepare (100.0); c.reset (0.0);
    REQUIRE_THAT (c.advance (10.0), WithinAbs (0.1, 1e-9)); // 10 Hz / 100 sr = 0.1 / sample
    for (int i = 0; i < 9; ++i) c.advance (10.0);          // total 10 steps -> wrap
    REQUIRE (c.getPhase() < 1e-9);
}

TEST_CASE ("setPhaseFromPpq aligns phase to the beat", "[lfoclock]")
{
    LfoClock c; c.prepare (48000.0);
    c.setPhaseFromPpq (1.0, 1.0);   REQUIRE_THAT (c.getPhase(), WithinAbs (0.0, 1e-9));
    c.setPhaseFromPpq (0.5, 1.0);   REQUIRE_THAT (c.getPhase(), WithinAbs (0.5, 1e-9));
    c.setPhaseFromPpq (2.5, 1.0);   REQUIRE_THAT (c.getPhase(), WithinAbs (0.5, 1e-9));
}
