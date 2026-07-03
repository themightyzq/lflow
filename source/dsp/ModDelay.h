#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace lflow {

// Fractional delay line with 4-point Hermite (Catmull-Rom) interpolation.
// Pure C++ — no JUCE.
//
// *** ALLOCATION NOTICE ***
// prepare() allocates the ring buffer via std::vector. This is the first
// lflow dsp unit that owns a heap buffer. prepare() is NOT real-time safe
// and MUST be called only from prepareToPlay() (or equivalent setup code),
// NEVER from the audio callback. Once prepared, process()/reset()/
// getMaxDelaySamples() perform no allocation and are RT-safe.
//
// Ring + interpolation margin: delaySamples is clamped to
// [4, capacity - 4] on every call so the 4-point kernel always has two
// samples of history and two of already-written "ahead" context around the
// fractional read point, including right at the buffer wrap boundary.
class ModDelay
{
public:
    // Allocates a ring buffer sized to hold at least maxDelaySeconds of audio
    // at sampleRate (rounded up to whole samples), with a minimum capacity of
    // 8 samples so the [4, capacity - 4] clamp range is never empty. NOT
    // real-time safe — call from prepareToPlay, never from the audio thread.
    void prepare (double sampleRate, double maxDelaySeconds)
    {
        const double sr = (sampleRate > 0.0) ? sampleRate : 48000.0;
        const double secs = (maxDelaySeconds > 0.0) ? maxDelaySeconds : 0.0;

        // Defensive upper clamp before the int cast (avoids overflow for
        // pathological maxDelaySeconds; ~35 minutes at 48 kHz is far beyond
        // any modulation-delay use of this class).
        double capD = std::ceil (sr * secs);
        if (capD > 1.0e8)
            capD = 1.0e8;

        int capacitySamples = static_cast<int> (capD);
        if (capacitySamples < kMinCapacity)
            capacitySamples = kMinCapacity;

        capacity = capacitySamples;
        buffer.assign (static_cast<size_t> (capacity), 0.0f);
        writePos = 0;
    }

    // Zeroes the buffer and rewinds the write head. No audio residue survives
    // a reset — subsequent silence in yields silence out immediately.
    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    // Writes `in` at the current head, then returns a Hermite-interpolated
    // read at `delaySamples` behind it. delaySamples is clamped to
    // [4, capacity - 4]; non-finite (NaN/Inf) or out-of-range values are
    // clamped safely rather than crashing or propagating NaN.
    float process (float in, float delaySamples) noexcept
    {
        double d = static_cast<double> (delaySamples);
        const double lo = 4.0;
        const double hi = static_cast<double> (capacity) - 4.0;
        if (! (d >= lo)) d = lo;   // catches NaN (comparison false) and below-range
        if (d > hi) d = hi;

        // Ring-write scrub (Phase 7 Task 1 / QA L3 hardening): a non-finite input
        // sample must never enter the ring -- otherwise it sits there, poisoning
        // every read whose Hermite window touches it, for up to ~ring-read-length
        // calls before self-clearing (bounded, but not immediate). Storing 0.0f
        // instead means nothing non-finite is ever written, so recovery is
        // immediate rather than merely bounded.
        buffer[static_cast<size_t> (writePos)] = std::isfinite (in) ? in : 0.0f;

        const double readPos = static_cast<double> (writePos) - d;
        const double idx1D = std::floor (readPos);
        const float t = static_cast<float> (readPos - idx1D);
        const long idx1 = static_cast<long> (idx1D);

        const int i0 = wrapIndex (idx1 - 1);
        const int i1 = wrapIndex (idx1);
        const int i2 = wrapIndex (idx1 + 1);
        const int i3 = wrapIndex (idx1 + 2);

        const float out = hermite4 (buffer[static_cast<size_t> (i0)],
                                     buffer[static_cast<size_t> (i1)],
                                     buffer[static_cast<size_t> (i2)],
                                     buffer[static_cast<size_t> (i3)],
                                     t);

        writePos = (writePos + 1) % capacity;
        return out;
    }

    // The allocated ring buffer capacity, in samples (as computed in
    // prepare()). This is the raw allocation size, not the clamp-adjusted
    // usable delay range [4, capacity - 4].
    double getMaxDelaySamples() const noexcept
    {
        return static_cast<double> (capacity);
    }

private:
    static constexpr int kMinCapacity = 8;

    int wrapIndex (long i) const noexcept
    {
        const long c = static_cast<long> (capacity);
        long m = i % c;
        if (m < 0) m += c;
        return static_cast<int> (m);
    }

    // 4-point, 3rd-order Hermite (Catmull-Rom) interpolation. At t == 0 this
    // reduces exactly to x1 (no neighbor bleed on integer-aligned delays).
    static float hermite4 (float x0, float x1, float x2, float x3, float t) noexcept
    {
        const float c0 = x1;
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    // Default-sized to kMinCapacity so a default-constructed ModDelay is
    // safe-by-default: process() before prepare() degrades gracefully
    // (minimum-capacity line) instead of writing out of bounds — matching
    // the safe-unprepared convention of Biquad/LR4Crossover. This one
    // construction-time allocation happens at object creation (plugin
    // construction), never on the audio thread.
    std::vector<float> buffer = std::vector<float> (static_cast<size_t> (kMinCapacity), 0.0f);
    int capacity { kMinCapacity };
    int writePos { 0 };
};

} // namespace lflow
