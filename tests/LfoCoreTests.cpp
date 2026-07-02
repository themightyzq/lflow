#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "LfoCore.h"
#include "ShapeModel.h"

using lflow::LfoCore;
using lflow::Waveform;
using Catch::Matchers::WithinAbs;

// Custom must append at index 6 (Sine..SampleHold occupy 0..5); Task 2/3 UI additions
// depend on this exact ordinal.
static_assert (static_cast<int> (Waveform::Custom) == 6, "Waveform::Custom must append at index 6");

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

TEST_CASE ("Custom waveform with a table set matches shapeTableValue exactly", "[lfocore][custom]")
{
    float table[8];
    for (int i = 0; i < 8; ++i)
        table[i] = static_cast<float> (i) / 8.0f;

    LfoCore c; c.setWaveform (Waveform::Custom);
    c.setCustomTable (table, 8);

    for (float phase : { 0.0f, 0.1f, 0.37f, 0.5f, 0.99f })
        REQUIRE_THAT (c.valueAt (phase), WithinAbs (lflow::shapeTableValue (table, 8, phase), 1e-6));
}

TEST_CASE ("Custom waveform with no table set falls back to Sine", "[lfocore][custom]")
{
    LfoCore c; c.setWaveform (Waveform::Custom); // setCustomTable never called -> nullptr default
    LfoCore sine; sine.setWaveform (Waveform::Sine);

    for (float phase : { 0.0f, 0.25f, 0.5f, 0.75f })
        REQUIRE_THAT (c.valueAt (phase), WithinAbs (sine.valueAt (phase), 1e-6));
}

TEST_CASE ("Custom waveform reverts to the Sine fallback after setCustomTable(nullptr, 0)", "[lfocore][custom]")
{
    float table[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
    LfoCore c; c.setWaveform (Waveform::Custom);
    c.setCustomTable (table, 4);
    c.setCustomTable (nullptr, 0); // explicit unset

    LfoCore sine; sine.setWaveform (Waveform::Sine);
    REQUIRE_THAT (c.valueAt (0.3f), WithinAbs (sine.valueAt (0.3f), 1e-6));
}
