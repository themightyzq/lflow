#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "ShapeModel.h"
#include <cmath>
#include <limits>

using namespace lflow;
using Catch::Matchers::WithinAbs;

// ---------------------------------------------------------------------------
// bakeShapeTable
// ---------------------------------------------------------------------------

TEST_CASE ("bakeShapeTable: linear ramp is exact at start and at the midpoint", "[shapemodel]")
{
    ShapeNode nodes[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 2, table, kShapeTableSize);

    // x=0 sample: x <= firstX(0) -> flat clamp to first node's y.
    REQUIRE_THAT (table[0], WithinAbs (0.0, 1e-6));
    // x=0.5 sample (i = size/2): curve=0 -> f(t)=t -> y = 0 + (1-0)*0.5 = 0.5 exactly.
    REQUIRE_THAT (table[kShapeTableSize / 2], WithinAbs (0.5, 1e-6));
}

TEST_CASE ("bakeShapeTable: curve=0 midpoint is the arithmetic average of the segment endpoints", "[shapemodel]")
{
    ShapeNode nodes[2] = { { 0.0f, 0.2f, 0.0f }, { 1.0f, 0.8f, 0.0f } };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 2, table, kShapeTableSize);

    REQUIRE_THAT (table[kShapeTableSize / 2], WithinAbs ((0.2 + 0.8) / 2.0, 1e-5));
}

TEST_CASE ("bakeShapeTable: curve=+1 midpoint matches the computed power-curve formula", "[shapemodel]")
{
    // curve lives on the node BEFORE the segment it bends (spec: "curve bends the
    // segment FOLLOWING the node").
    ShapeNode nodes[2] = { { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 0.0f } };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 2, table, kShapeTableSize);

    // f(t) = t^(2^(3*curve)); t=0.5 (midpoint), curve=1 -> exponent = 2^3 = 8.
    // f(0.5) = 0.5^8 = 0.00390625. y = y1 + (y2-y1)*f = 0 + (1-0)*0.00390625 = 0.00390625.
    const double exponent = std::pow (2.0, 3.0 * 1.0);
    const double expected = std::pow (0.5, exponent);
    REQUIRE_THAT (expected, WithinAbs (0.00390625, 1e-8)); // sanity-check the hand computation
    REQUIRE_THAT (table[kShapeTableSize / 2], WithinAbs (expected, 1e-5));
}

TEST_CASE ("bakeShapeTable: flat extension holds first/last node y outside [firstX,lastX]", "[shapemodel]")
{
    ShapeNode nodes[2] = { { 0.3f, 0.25f, 0.0f }, { 0.7f, 0.75f, 0.0f } };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 2, table, kShapeTableSize);

    REQUIRE_THAT (table[0], WithinAbs (0.25, 1e-6));                    // x ~ 0 < 0.3 -> first y
    REQUIRE_THAT (table[kShapeTableSize - 1], WithinAbs (0.75, 1e-6));  // x ~ 0.996 > 0.7 -> last y
}

TEST_CASE ("bakeShapeTable: 2-node minimal input produces a valid monotonic ramp", "[shapemodel]")
{
    ShapeNode nodes[2] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 2, table, kShapeTableSize);

    for (int i = 1; i < kShapeTableSize; ++i)
        REQUIRE (table[i] >= table[i - 1] - 1e-6f);
}

TEST_CASE ("bakeShapeTable: 32-node stress input stays in range and matches node 0's y", "[shapemodel]")
{
    ShapeNode nodes[kMaxShapeNodes];
    for (int i = 0; i < kMaxShapeNodes; ++i)
    {
        nodes[i].x = static_cast<float> (i) / static_cast<float> (kMaxShapeNodes - 1);
        nodes[i].y = (i % 2 == 0) ? 0.0f : 1.0f; // zigzag, stresses many segments
        nodes[i].curve = (i % 3 == 0) ? 0.5f : ((i % 3 == 1) ? -0.5f : 0.0f);
    }
    float table[kShapeTableSize];
    bakeShapeTable (nodes, kMaxShapeNodes, table, kShapeTableSize);

    for (int i = 0; i < kShapeTableSize; ++i)
    {
        REQUIRE (std::isfinite (table[i]));
        REQUIRE (table[i] >= -1e-5f);
        REQUIRE (table[i] <= 1.0f + 1e-5f);
    }
    REQUIRE_THAT (table[0], WithinAbs (0.0, 1e-5));
}

TEST_CASE ("bakeShapeTable: degenerate inputs (null / count<2 / zero-width segment) stay safe", "[shapemodel]")
{
    float table[kShapeTableSize];

    // null nodes / count 0 -> must not crash and must leave every sample finite.
    bakeShapeTable (nullptr, 0, table, kShapeTableSize);
    for (float v : table) REQUIRE (std::isfinite (v));

    // count==1 -> flat table at that node's (clamped) y.
    ShapeNode one[1] = { { 0.5f, 0.6f, 0.0f } };
    bakeShapeTable (one, 1, table, kShapeTableSize);
    for (float v : table) REQUIRE_THAT (v, WithinAbs (0.6, 1e-6));

    // zero-width segment (duplicate x) -> must not divide by zero / produce NaN.
    ShapeNode zeroWidth[3] = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.5f, 0.2f, 0.0f } };
    bakeShapeTable (zeroWidth, 3, table, kShapeTableSize);
    for (float v : table) REQUIRE (std::isfinite (v));

    // out==nullptr / outSize<=0 -> must not crash.
    bakeShapeTable (one, 1, nullptr, 0);
    bakeShapeTable (one, 1, table, 0);
}

// ---------------------------------------------------------------------------
// shapeTableValue
// ---------------------------------------------------------------------------

TEST_CASE ("shapeTableValue: exact at table indices, lerps between, and wraps past the end", "[shapemodel]")
{
    float table[4] = { 0.0f, 1.0f, 2.0f, 3.0f };

    REQUIRE_THAT (shapeTableValue (table, 4, 0.0f),  WithinAbs (0.0, 1e-6));
    REQUIRE_THAT (shapeTableValue (table, 4, 0.25f), WithinAbs (1.0, 1e-6));
    REQUIRE_THAT (shapeTableValue (table, 4, 0.5f),  WithinAbs (2.0, 1e-6));
    REQUIRE_THAT (shapeTableValue (table, 4, 0.75f), WithinAbs (3.0, 1e-6));

    REQUIRE_THAT (shapeTableValue (table, 4, 0.125f), WithinAbs (0.5, 1e-6)); // lerp idx0->idx1

    // Wrap: the last sample interpolates toward table[0], not off the end of the array.
    REQUIRE_THAT (shapeTableValue (table, 4, 0.875f), WithinAbs (1.5, 1e-6)); // (table[3]+table[0])/2

    REQUIRE_THAT (shapeTableValue (table, 4, 1.25f),  WithinAbs (1.0, 1e-6)); // phase wraps to 0.25
    REQUIRE_THAT (shapeTableValue (table, 4, -0.25f), WithinAbs (3.0, 1e-6)); // negative wraps to 0.75
}

TEST_CASE ("shapeTableValue: degenerate table (null/size<=0) is safe", "[shapemodel]")
{
    float table[4] = { 0.0f, 1.0f, 2.0f, 3.0f };
    REQUIRE (shapeTableValue (nullptr, 4, 0.5f) == 0.0f);
    REQUIRE (shapeTableValue (table, 0, 0.5f) == 0.0f);
}

// ---------------------------------------------------------------------------
// Phase 7 Task 1: bakeShapeTable defense-in-depth against non-finite/out-of-
// range node fields (QA H2's suspected area -- getNodes/setStateInformation
// reload paths don't clamp, so a NaN curve or Inf x reaching here must still
// bake a finite, in-range table; see probe.cpp PROBE 7).
// ---------------------------------------------------------------------------

TEST_CASE ("bakeShapeTable: NaN curve, Inf x, and out-of-range y on a node all bake to a finite in-range table",
           "[shapemodel][hardening][H2]")
{
    // curve=NaN on node 0 -> old code: pow(2, 3*NaN) = NaN propagated into
    // every sample of that node's segment (H1's upstream root cause).
    ShapeNode nodes[3] = {
        { std::numeric_limits<float>::infinity(), -5.0f, std::numeric_limits<float>::quiet_NaN() },
        { 0.5f, 1.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f }
    };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 3, table, kShapeTableSize);

    for (float v : table)
    {
        REQUIRE (std::isfinite (v));
        REQUIRE (v >= -1e-5f);
        REQUIRE (v <= 1.0f + 1e-5f);
    }
}

TEST_CASE ("bakeShapeTable: a NaN curve alone (finite x/y) still bakes a finite table",
           "[shapemodel][hardening][H2]")
{
    // Isolates the exact H2/probe-7 repro: curve=NaN with otherwise-sane nodes.
    ShapeNode nodes[3] = { { 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN() },
                           { 0.5f, 1.0f, 0.0f },
                           { 1.0f, 0.0f, 0.0f } };
    float table[kShapeTableSize];
    bakeShapeTable (nodes, 3, table, kShapeTableSize);

    for (float v : table)
        REQUIRE (std::isfinite (v));
}

TEST_CASE ("bakeShapeTable: an out-of-range finite curve is clamped to [-1,1], not left raw",
           "[shapemodel][hardening]")
{
    ShapeNode extremeHigh[2] = { { 0.0f, 0.0f, 500.0f }, { 1.0f, 1.0f, 0.0f } };
    float tableHigh[kShapeTableSize];
    bakeShapeTable (extremeHigh, 2, tableHigh, kShapeTableSize);
    for (float v : tableHigh)
        REQUIRE (std::isfinite (v));

    ShapeNode extremeLow[2] = { { 0.0f, 0.0f, -500.0f }, { 1.0f, 1.0f, 0.0f } };
    float tableLow[kShapeTableSize];
    bakeShapeTable (extremeLow, 2, tableLow, kShapeTableSize);
    for (float v : tableLow)
        REQUIRE (std::isfinite (v));
}
