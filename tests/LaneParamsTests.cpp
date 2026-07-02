#include <catch2/catch_test_macros.hpp>
#include "LaneParams.h"

using namespace lflow;

TEST_CASE ("resolveLinkedLanes copies motion fields onto lanes 2-3 when linked", "[laneparams]")
{
    LaneParams lanes[3];

    lanes[0].waveform    = Waveform::Square;
    lanes[0].sync        = true;
    lanes[0].rateHz      = 3.5;
    lanes[0].cycleBeats  = 2.0;
    lanes[0].depth       = 0.5f;
    lanes[0].dest        = Dest::Pan;
    lanes[0].phaseOffset = 0.25f;

    lanes[1].waveform    = Waveform::Sine;
    lanes[1].sync        = false;
    lanes[1].rateHz      = 1.0;
    lanes[1].cycleBeats  = 1.0;
    lanes[1].depth       = 0.7f;
    lanes[1].dest        = Dest::Volume;
    lanes[1].phaseOffset = 0.9f;

    lanes[2].waveform    = Waveform::Triangle;
    lanes[2].sync        = true;
    lanes[2].rateHz      = 9.0;
    lanes[2].cycleBeats  = 4.0;
    lanes[2].depth       = 0.0f;
    lanes[2].dest        = Dest::Pan;
    lanes[2].phaseOffset = 0.1f;

    resolveLinkedLanes (lanes, 3, true);

    for (int i = 1; i < 3; ++i)
    {
        REQUIRE (lanes[i].waveform   == lanes[0].waveform);
        REQUIRE (lanes[i].sync       == lanes[0].sync);
        REQUIRE (lanes[i].rateHz     == lanes[0].rateHz);
        REQUIRE (lanes[i].cycleBeats == lanes[0].cycleBeats);
    }

    // Identity fields (depth/dest/phaseOffset) are untouched by linking.
    REQUIRE (lanes[1].depth       == 0.7f);
    REQUIRE (lanes[1].dest        == Dest::Volume);
    REQUIRE (lanes[1].phaseOffset == 0.9f);

    REQUIRE (lanes[2].depth       == 0.0f);
    REQUIRE (lanes[2].dest        == Dest::Pan);
    REQUIRE (lanes[2].phaseOffset == 0.1f);
}

TEST_CASE ("resolveLinkedLanes is a no-op when link is false", "[laneparams]")
{
    LaneParams lanes[3];

    lanes[0].waveform   = Waveform::Square;
    lanes[0].sync       = true;
    lanes[0].rateHz     = 3.5;
    lanes[0].cycleBeats = 2.0;

    lanes[1].waveform   = Waveform::Sine;
    lanes[1].sync       = false;
    lanes[1].rateHz     = 1.0;
    lanes[1].cycleBeats = 1.0;
    lanes[1].depth      = 0.2f;

    lanes[2].waveform   = Waveform::SawUp;
    lanes[2].sync       = false;
    lanes[2].rateHz     = 4.0;
    lanes[2].cycleBeats = 1.0;

    const LaneParams before1 = lanes[1];
    const LaneParams before2 = lanes[2];

    resolveLinkedLanes (lanes, 3, false);

    REQUIRE (lanes[1].waveform   == before1.waveform);
    REQUIRE (lanes[1].sync       == before1.sync);
    REQUIRE (lanes[1].rateHz     == before1.rateHz);
    REQUIRE (lanes[1].cycleBeats == before1.cycleBeats);
    REQUIRE (lanes[1].depth      == before1.depth);

    REQUIRE (lanes[2].waveform   == before2.waveform);
    REQUIRE (lanes[2].sync       == before2.sync);
    REQUIRE (lanes[2].rateHz     == before2.rateHz);
    REQUIRE (lanes[2].cycleBeats == before2.cycleBeats);
}
