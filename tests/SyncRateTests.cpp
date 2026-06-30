#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "SyncRate.h"

using namespace lflow;
using Catch::Matchers::WithinAbs;

TEST_CASE ("base divisions map to quarter-note beats", "[syncrate]")
{
    REQUIRE_THAT (cycleBeats (Division::D1_1,  Rhythm::Straight), WithinAbs (4.0,   1e-9));
    REQUIRE_THAT (cycleBeats (Division::D1_4,  Rhythm::Straight), WithinAbs (1.0,   1e-9));
    REQUIRE_THAT (cycleBeats (Division::D1_8,  Rhythm::Straight), WithinAbs (0.5,   1e-9));
    REQUIRE_THAT (cycleBeats (Division::D1_32, Rhythm::Straight), WithinAbs (0.125, 1e-9));
}

TEST_CASE ("dotted multiplies length by 1.5, triplet by 2/3", "[syncrate]")
{
    REQUIRE_THAT (cycleBeats (Division::D1_4, Rhythm::Dotted),  WithinAbs (1.5,        1e-9));
    REQUIRE_THAT (cycleBeats (Division::D1_4, Rhythm::Triplet), WithinAbs (2.0 / 3.0,  1e-9));
}
