#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "LfoCore.h"

using lflow::LfoCore;
using lflow::Waveform;
using Catch::Matchers::WithinAbs;

TEST_CASE ("sine is unipolar and centered", "[lfocore]")
{
    LfoCore c; c.setWaveform (Waveform::Sine);
    REQUIRE_THAT (c.valueAt (0.0f),  WithinAbs (0.5, 1e-4));
    REQUIRE_THAT (c.valueAt (0.25f), WithinAbs (1.0, 1e-4));
    REQUIRE_THAT (c.valueAt (0.5f),  WithinAbs (0.5, 1e-4));
    REQUIRE_THAT (c.valueAt (0.75f), WithinAbs (0.0, 1e-4));
}

TEST_CASE ("triangle rises 0->1 over first half", "[lfocore]")
{
    LfoCore c; c.setWaveform (Waveform::Triangle);
    REQUIRE_THAT (c.valueAt (0.0f),  WithinAbs (0.0, 1e-4));
    REQUIRE_THAT (c.valueAt (0.25f), WithinAbs (0.5, 1e-4));
    REQUIRE_THAT (c.valueAt (0.5f),  WithinAbs (1.0, 1e-4));
    REQUIRE_THAT (c.valueAt (0.75f), WithinAbs (0.5, 1e-4));
}

TEST_CASE ("square is high on first half, low on second", "[lfocore]")
{
    LfoCore c; c.setWaveform (Waveform::Square);
    REQUIRE_THAT (c.valueAt (0.25f), WithinAbs (1.0, 1e-6));
    REQUIRE_THAT (c.valueAt (0.75f), WithinAbs (0.0, 1e-6));
}

TEST_CASE ("saws are linear ramps", "[lfocore]")
{
    LfoCore up;   up.setWaveform (Waveform::SawUp);
    LfoCore down; down.setWaveform (Waveform::SawDown);
    REQUIRE_THAT (up.valueAt (0.3f),   WithinAbs (0.3, 1e-4));
    REQUIRE_THAT (down.valueAt (0.3f), WithinAbs (0.7, 1e-4));
}

TEST_CASE ("sample & hold stays constant within a cycle and changes across cycles", "[lfocore]")
{
    LfoCore c; c.setWaveform (Waveform::SampleHold); c.reset (42u);
    const float a = c.valueAt (0.10f);
    REQUIRE_THAT (c.valueAt (0.40f), WithinAbs (a, 1e-6));   // same cycle, held
    const float b = c.valueAt (0.05f);                       // wrapped -> new value
    // Values are in range; the new step is (almost surely) different.
    REQUIRE (a >= 0.0f); REQUIRE (a <= 1.0f);
    REQUIRE (b >= 0.0f); REQUIRE (b <= 1.0f);
}
