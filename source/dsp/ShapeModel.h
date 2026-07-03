#pragma once
#include <cmath>

// Drawable custom-shape model (Phase 4). Pure C++, no JUCE, noexcept, no allocation --
// safe on the audio thread and unit-testable headless. Node authoring/editing lives on
// the message thread (bakeShapeTable); the audio thread only ever calls shapeTableValue
// against a table handed over by the lock-free TripleBuffer (see TripleBuffer.h).
namespace lflow {

// A single breakpoint. x,y in [0,1]; curve in [-1,1] bends the segment FOLLOWING this
// node (i.e. the segment from this node to the next one). The last node's curve is
// unused (no segment follows it).
struct ShapeNode
{
    float x { 0.0f };
    float y { 0.0f };
    float curve { 0.0f };
};

inline constexpr int kShapeTableSize = 256;
inline constexpr int kMaxShapeNodes  = 32;

namespace detail {
    inline float clamp01 (float v) noexcept
    {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }
}

// Bakes an ordered breakpoint list into a `outSize`-sample lookup table sampled at
// x = i/outSize for i in [0, outSize). Nodes are assumed pre-sorted by x (caller's
// responsibility -- editor keeps the list sorted); count must be in [2, kMaxShapeNodes]
// for the "normal" path, but every input is handled SAFELY:
//   - out == nullptr or outSize <= 0            -> no-op (nothing to write).
//   - nodes == nullptr or count <= 0            -> table filled with 0.0f (silent/neutral).
//   - count == 1                                -> table filled flat with that node's
//                                                   (clamped) y.
//   - count > kMaxShapeNodes                    -> extra nodes ignored (clamped to
//                                                   kMaxShapeNodes) rather than read OOB.
//   - a zero-width segment (nodes[j].x == nodes[j+1].x)
//                                                -> that segment is skipped/collapsed to
//                                                   the later node's y instead of
//                                                   dividing by zero (no NaN/Inf).
// Outside [nodes[0].x, nodes[count-1].x] the table is flat-extended: nodes[0].y before
// the first node's x, nodes[count-1].y after the last node's x. Between two consecutive
// nodes, the segment is shaped by the earlier node's curve:
//   t        = (x - x1) / (x2 - x1)                        (normalized segment position)
//   f(t)     = t ^ (2 ^ (3 * curve))                        (curve==0 -> f(t)=t, linear)
//   y        = y1 + (y2 - y1) * f(t)
inline void bakeShapeTable (const ShapeNode* nodes, int count, float* out, int outSize) noexcept
{
    if (out == nullptr || outSize <= 0)
        return;

    if (nodes == nullptr || count <= 0)
    {
        for (int i = 0; i < outSize; ++i) out[i] = 0.0f;
        return;
    }

    if (count > kMaxShapeNodes)
        count = kMaxShapeNodes;

    // Defense-in-depth sanitization (Phase 7 Task 1 / QA H2 hardening): the
    // reload path (setStateInformation of crafted/corrupt/old XML -> getNodes,
    // which -- unlike setNodes -- does not clamp) can hand this function
    // non-finite or out-of-range x/y/curve. A NaN curve alone propagates through
    // std::pow into every sample of that node's segment (H1's upstream root
    // cause); a non-finite x breaks the flat-extension/segment-scan comparisons
    // below. Sanitize into a local, bounded (<=kMaxShapeNodes) stack copy before
    // any of that logic runs -- stack-only, no allocation, message-thread-only
    // (never called per audio sample). Non-finite x/y -> that field becomes 0
    // (silence/neutral, and 0 keeps x a stable sort anchor at the start of the
    // range); non-finite curve -> 0 (linear/no bend). Finite values are clamped
    // to their valid range: x/y in [0,1], curve in [-1,1].
    ShapeNode safeNodes[kMaxShapeNodes];
    for (int k = 0; k < count; ++k)
    {
        const float rawX = nodes[k].x;
        const float rawY = nodes[k].y;
        const float rawCurve = nodes[k].curve;

        const float x = std::isfinite (rawX) ? detail::clamp01 (rawX) : 0.0f;
        const float y = std::isfinite (rawY) ? detail::clamp01 (rawY) : 0.0f;

        float curve = std::isfinite (rawCurve) ? rawCurve : 0.0f;
        if (curve < -1.0f) curve = -1.0f;
        else if (curve > 1.0f) curve = 1.0f;

        safeNodes[k] = { x, y, curve };
    }
    nodes = safeNodes; // everything below reads through `nodes`, now sanitized

    if (count == 1)
    {
        const float y = detail::clamp01 (nodes[0].y);
        for (int i = 0; i < outSize; ++i) out[i] = y;
        return;
    }

    const float firstX = nodes[0].x;
    const float lastX  = nodes[count - 1].x;
    const float firstY = detail::clamp01 (nodes[0].y);
    const float lastY  = detail::clamp01 (nodes[count - 1].y);

    for (int i = 0; i < outSize; ++i)
    {
        const float x = static_cast<float> (i) / static_cast<float> (outSize);

        if (x <= firstX) { out[i] = firstY; continue; }
        if (x >= lastX)  { out[i] = lastY;  continue; }

        // Find segment j such that nodes[j].x <= x < nodes[j+1].x (scan is bounded by
        // kMaxShapeNodes -- cheap even at worst case, and this runs on the message
        // thread only, never per audio sample).
        int j = 0;
        for (; j < count - 2; ++j)
            if (x < nodes[j + 1].x)
                break;

        const float x1 = nodes[j].x;
        const float x2 = nodes[j + 1].x;
        const float y1 = detail::clamp01 (nodes[j].y);
        const float y2 = detail::clamp01 (nodes[j + 1].y);
        const float width = x2 - x1;

        if (width <= 1.0e-9f)
        {
            // Degenerate (zero-width) segment: collapse to the later node's y rather
            // than dividing by ~0 -- keeps the table finite and well-defined.
            out[i] = y2;
            continue;
        }

        float t = (x - x1) / width;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;

        const float curve = nodes[j].curve;
        const float exponent = std::pow (2.0f, 3.0f * curve); // exponent in [0.125, 8] for curve in [-1,1]
        const float f = std::pow (t, exponent);

        out[i] = y1 + (y2 - y1) * f;
    }
}

// Reads `table` (size `size`) at `phase`, linearly interpolating between neighbouring
// samples and WRAPPING: phase is folded into [0,1) first, and the last sample
// (table[size-1]) interpolates toward table[0] (not off the end of the array) --
// matching how LfoClock phases behave (always wrapped, cyclic). Safe for
// table == nullptr or size <= 0 (returns 0.0f).
inline float shapeTableValue (const float* table, int size, float phase) noexcept
{
    if (table == nullptr || size <= 0)
        return 0.0f;

    if (size == 1)
        return table[0];

    float p = phase - std::floor (phase); // wrap into [0,1)
    if (p < 0.0f) p = 0.0f;                // guard fp edge cases

    float pos = p * static_cast<float> (size);
    int i0 = static_cast<int> (pos);
    if (i0 >= size) i0 = size - 1;         // guard fp edge at pos==size
    int i1 = (i0 + 1) % size;              // wraps size-1 -> 0

    const float frac = pos - static_cast<float> (i0);
    return table[i0] + (table[i1] - table[i0]) * frac;
}

} // namespace lflow
