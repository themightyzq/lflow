#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "MultiLaneEngine.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace lflow;
using Catch::Matchers::WithinAbs;

// Zipper-noise regression tests (portfolio review 2026-09-29, item 18). Depth, phase offset,
// Mix and the two crossover frequencies were applied straight from the parameter, so an
// automation step or a knob jump landed as a one-sample discontinuity. Each test warms an
// engine up with settings A, switches to settings B between two process() calls (what a host
// does when a parameter jumps), and checks that no output sample differs from its neighbour by
// more than the signal's own slope allows. Uses only the public engine API.

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Setup
{
    LaneParams lane;           // lane 0; lanes 1-2 stay at depth 0
    GlobalParams global;
    double xoverLow  { 250.0 };
    double xoverHigh { 2500.0 };
};

// A lane whose modulator is effectively frozen: 0.01 Hz moves the clock by 1e-4 of a cycle
// over the 0.5 s a test runs, so every change in the output comes from the parameter under test.
LaneParams frozenLane (Waveform w, Dest d, float depth, float phaseOffset = 0.0f)
{
    LaneParams p;
    p.waveform = w;
    p.sync = false;
    p.rateHz = 0.01;
    p.cycleBeats = 1.0;
    p.depth = depth;
    p.dest = d;
    p.phaseOffset = phaseOffset;
    return p;
}

void apply (MultiLaneEngine& e, const Setup& s)
{
    e.setLaneParams (0, s.lane);
    e.setLaneParams (1, LaneParams{});
    e.setLaneParams (2, LaneParams{});
    e.setGlobalParams (s.global);
    e.setCrossovers (s.xoverLow, s.xoverHigh);
}

void processChunked (MultiLaneEngine& e, std::vector<float>& buf, size_t from, size_t to, int chunk)
{
    for (size_t pos = from; pos < to;)
    {
        const int n = static_cast<int> (std::min<size_t> (static_cast<size_t> (chunk), to - pos));
        float* chans[1] = { buf.data() + pos };
        e.process (chans, 1, n);
        pos += static_cast<size_t> (n);
    }
}

double maxStep (const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
        m = std::max (m, std::fabs (static_cast<double> (v[i]) - static_cast<double> (v[i - 1])));
    return m;
}

// Largest second difference |y[n+1] - 2 y[n] + y[n-1]|. A pure sinusoid's is at most
// amplitude * w^2 (w = 2 pi f / sr), so it stays tiny for a clean tone, while any
// discontinuity of height J shows up as roughly 2 J. It is the sharper detector for filter
// (crossover) retunes, whose glitch can be smaller than the tone's own first difference.
double maxSecondStep (const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i + 1 < to; ++i)
        m = std::max (m, std::fabs (static_cast<double> (v[i + 1]) - 2.0 * static_cast<double> (v[i])
                                    + static_cast<double> (v[i - 1])));
    return m;
}

struct StepResult
{
    double baseline2;  // largest second difference of the INPUT
    double afterStep2; // largest second difference of the output from the step onward
    double baseline;   // largest sample-to-sample change of the INPUT (the signal's own slope)
    double afterStep;  // largest sample-to-sample change from the last pre-step sample onward
    std::vector<float> out;
    size_t stepIndex;
};

// Input is cos(2 pi f t): the step lands on a sample index where the cosine is at +1, so a
// gain change there is as visible as it can be.
StepResult runStep (double sr, double freqHz, int chunk, const Setup& a, const Setup& b)
{
    const size_t warm  = static_cast<size_t> (std::llround (0.25 * sr));
    const size_t after = static_cast<size_t> (std::llround (0.25 * sr));

    // Round the step point to a whole number of input cycles so the cosine is at its peak.
    const double period = sr / freqHz;
    const size_t stepAt = static_cast<size_t> (std::llround (std::floor (static_cast<double> (warm) / period) * period));

    std::vector<float> buf (stepAt + after);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = static_cast<float> (0.5 * std::cos (2.0 * kPi * freqHz * static_cast<double> (i) / sr));

    // The signal's own slope, measured on the dry input: the processed output can legitimately
    // be silent before the step (a fully gated lane), so it cannot serve as the reference.
    const double inputSlope = maxStep (buf, 1, buf.size());
    const double inputCurve = maxSecondStep (buf, 1, buf.size());

    MultiLaneEngine e;
    e.prepare (sr, chunk);
    e.reset();
    apply (e, a);
    processChunked (e, buf, 0, stepAt, chunk);

    apply (e, b);
    processChunked (e, buf, stepAt, buf.size(), chunk);

    StepResult r;
    r.baseline  = inputSlope;
    r.baseline2 = inputCurve;
    r.afterStep2 = maxSecondStep (buf, stepAt > 1 ? stepAt - 1 : 1, buf.size());
    r.afterStep = maxStep (buf, stepAt, buf.size());
    r.out = buf;
    r.stepIndex = stepAt;
    return r;
}

// A smoothed parameter can only move the output by about the signal's own slope plus the
// (slow) ramp's contribution; a 1.6x margin plus a small constant leaves room for that, while
// an un-smoothed step (the old behaviour) lands as a jump of up to the full signal amplitude.
double allowed (const StepResult& r) { return r.baseline * 1.6 + 0.004; }
double allowed2 (const StepResult& r) { return r.baseline2 * 1.6 + 0.0015; }

} // namespace

TEST_CASE ("smoothing: a Mix step produces no sample-to-sample jump", "[multilane][smoothing]")
{
    const double sr = GENERATE (44100.0, 48000.0, 96000.0);
    const int chunk = GENERATE (64, 193, 1024);

    Setup a; a.lane = frozenLane (Waveform::Square, Dest::Volume, 1.0f); a.global.mix = 1.0f; a.global.smooth = 0.0f;
    Setup b = a; b.global.mix = 0.0f;

    const auto r = runStep (sr, sr / 100.0, chunk, a, b);
    INFO ("baseline " << r.baseline << " afterStep " << r.afterStep);
    REQUIRE (r.afterStep <= allowed (r));
}

TEST_CASE ("smoothing: a Depth step produces no sample-to-sample jump", "[multilane][smoothing]")
{
    const double sr = GENERATE (44100.0, 48000.0, 96000.0);
    const int chunk = GENERATE (64, 193, 1024);
    const float from = GENERATE (0.0f, 0.3f);   // 0.0 also covers a lane waking up from depth 0

    Setup a; a.lane = frozenLane (Waveform::Square, Dest::Volume, from); a.global.smooth = 0.0f;
    Setup b = a; b.lane.depth = 1.0f;

    const auto r = runStep (sr, sr / 100.0, chunk, a, b);
    INFO ("baseline " << r.baseline << " afterStep " << r.afterStep);
    REQUIRE (r.afterStep <= allowed (r));
}

TEST_CASE ("smoothing: a Depth step down to zero produces no jump and ends transparent", "[multilane][smoothing]")
{
    const double sr = 48000.0;

    Setup a; a.lane = frozenLane (Waveform::Square, Dest::Volume, 1.0f); a.global.smooth = 0.0f;
    Setup b = a; b.lane.depth = 0.0f;

    const auto r = runStep (sr, sr / 100.0, 64, a, b);
    INFO ("baseline " << r.baseline << " afterStep " << r.afterStep);
    REQUIRE (r.afterStep <= allowed (r));

    // Once the ramp is over the lane is off and the buffer is the untouched input.
    const size_t settled = r.stepIndex + static_cast<size_t> (0.1 * sr);
    for (size_t i = settled; i < r.out.size(); ++i)
        REQUIRE (r.out[i] == static_cast<float> (0.5 * std::cos (2.0 * kPi * (sr / 100.0) * static_cast<double> (i) / sr)));
}

TEST_CASE ("smoothing: a Phase step produces no sample-to-sample jump", "[multilane][smoothing]")
{
    const double sr = GENERATE (44100.0, 48000.0, 96000.0);
    const int chunk = GENERATE (64, 193, 1024);

    // Sine modulator frozen near phase 0: offset 0 -> gain 1 - 0.5, offset 0.25 -> gain 1 - 1.0.
    Setup a; a.lane = frozenLane (Waveform::Sine, Dest::Volume, 1.0f, 0.0f); a.global.smooth = 0.0f;
    Setup b = a; b.lane.phaseOffset = 0.25f;

    const auto r = runStep (sr, sr / 100.0, chunk, a, b);
    INFO ("baseline " << r.baseline << " afterStep " << r.afterStep);
    REQUIRE (r.afterStep <= allowed (r));
}

TEST_CASE ("smoothing: a Phase change takes the short way round the circle", "[multilane][smoothing]")
{
    // 350 -> 10 degrees must glide +20 degrees. Going the long way would sweep the whole cycle,
    // visible as a lane phase near 0.5 half-way through the ramp.
    const double sr = 48000.0;
    MultiLaneEngine e; e.prepare (sr, 512); e.reset();

    Setup a; a.lane = frozenLane (Waveform::Sine, Dest::Volume, 1.0f, 350.0f / 360.0f); a.global.smooth = 0.0f;
    apply (e, a);

    std::vector<float> buf (4800, 0.0f);
    processChunked (e, buf, 0, buf.size(), 512);

    Setup b = a; b.lane.phaseOffset = 10.0f / 360.0f;
    apply (e, b);

    double worst = 0.0;
    for (int block = 0; block < 12; ++block)   // 12 x 120 samples spans the 30 ms ramp
    {
        std::vector<float> chunk (120, 0.0f);
        float* chans[1] = { chunk.data() };
        e.process (chans, 1, 120);

        double ph = e.getLanePhase (0);          // [0,1); distance to 0/1 is what matters
        const double distFromZero = std::min (ph, 1.0 - ph);
        worst = std::max (worst, distFromZero);
    }
    REQUIRE (worst < 0.06);   // never strays beyond ~21 degrees from 0
}

TEST_CASE ("smoothing: a Crossover-Low step produces no sample-to-sample jump", "[multilane][smoothing]")
{
    const double sr = 48000.0;
    const int chunk = GENERATE (64, 193, 1024);

    // 440 Hz sits in the mid band at a 250 Hz crossover and in the low band at 1 kHz; the Low
    // band is gated, so the tone fades out as the crossover rises past it.
    Setup a; a.lane = frozenLane (Waveform::Square, Dest::Low, 1.0f); a.global.smooth = 0.0f; a.xoverLow = 250.0;
    Setup b = a; b.xoverLow = 1000.0;

    const auto r = runStep (sr, 480.0, chunk, a, b);
    INFO ("baseline " << r.baseline << " afterStep " << r.afterStep
          << " baseline2 " << r.baseline2 << " afterStep2 " << r.afterStep2);
    REQUIRE (r.afterStep <= allowed (r));
    REQUIRE (r.afterStep2 <= allowed2 (r));
}

TEST_CASE ("smoothing: a Crossover-High step produces no sample-to-sample jump", "[multilane][smoothing]")
{
    const double sr = 48000.0;
    const int chunk = GENERATE (64, 193, 1024);

    // 1.2 kHz is in the high band at an 800 Hz crossover and in the mid band at 3 kHz.
    Setup a; a.lane = frozenLane (Waveform::Square, Dest::High, 1.0f); a.global.smooth = 0.0f;
    a.xoverLow = 250.0; a.xoverHigh = 800.0;
    Setup b = a; b.xoverHigh = 3000.0;

    const auto r = runStep (sr, 1200.0, chunk, a, b);
    INFO ("baseline " << r.baseline << " afterStep " << r.afterStep
          << " baseline2 " << r.baseline2 << " afterStep2 " << r.afterStep2);
    REQUIRE (r.afterStep <= allowed (r));
    REQUIRE (r.afterStep2 <= allowed2 (r));
}

TEST_CASE ("smoothing: once the ramp is over the output matches an engine that started at the new value",
           "[multilane][smoothing]")
{
    // No residual offset: smoothing must settle exactly on the target, not near it.
    const double sr = 48000.0;
    const size_t total = static_cast<size_t> (sr);   // 1 s
    const size_t tail  = 2000;

    std::vector<float> input (total);
    for (size_t i = 0; i < total; ++i)
        input[i] = static_cast<float> (0.5 * std::cos (2.0 * kPi * 480.0 * static_cast<double> (i) / sr));

    struct Case { const char* name; Setup a; Setup b; };
    Setup base; base.lane = frozenLane (Waveform::Sine, Dest::Volume, 0.4f, 0.1f); base.global.smooth = 0.0f; base.global.mix = 0.8f;

    Case cases[5];
    cases[0] = { "mix",   base, base };   cases[0].b.global.mix = 0.3f;
    cases[1] = { "depth", base, base };   cases[1].b.lane.depth = 0.9f;
    cases[2] = { "phase", base, base };   cases[2].b.lane.phaseOffset = 0.6f;
    cases[3] = { "xlow",  base, base };   cases[3].a.lane.dest = cases[3].b.lane.dest = Dest::Mid; cases[3].b.xoverLow = 700.0;
    cases[4] = { "xhigh", base, base };   cases[4].a.lane.dest = cases[4].b.lane.dest = Dest::Mid; cases[4].b.xoverHigh = 6000.0;

    for (const auto& c : cases)
    {
        std::vector<float> stepped = input;
        {
            MultiLaneEngine e; e.prepare (sr, 512); e.reset();
            apply (e, c.a);
            processChunked (e, stepped, 0, 4800, 512);
            apply (e, c.b);
            processChunked (e, stepped, 4800, total, 512);
        }

        std::vector<float> reference = input;
        {
            MultiLaneEngine e; e.prepare (sr, 512); e.reset();
            apply (e, c.b);
            processChunked (e, reference, 0, total, 512);
        }

        INFO (c.name);
        for (size_t i = total - tail; i < total; ++i)
            REQUIRE_THAT (stepped[i], WithinAbs (reference[i], 2e-4));
    }
}

TEST_CASE ("smoothing: a backward Phase glide does not re-roll a Sample & Hold lane every sample",
           "[multilane][smoothing]")
{
    // Sample & Hold draws a new random step when it sees the phase wrap (phase decreasing).
    // The phase ramp moves the offset backward when Phase is automated downward; that must not
    // look like a wrap. A spurious roll or two is tolerable (the old code could produce one on
    // the step itself); a roll per sample for the whole 30 ms is a noise burst.
    const double sr = 48000.0;
    MultiLaneEngine e; e.prepare (sr, 64); e.reset();

    Setup a;
    a.lane = frozenLane (Waveform::SampleHold, Dest::Volume, 1.0f, 0.5f);
    a.lane.rateHz = 1.0;   // a real S&H rate: the clock advances 1/48000 per sample
    a.global.smooth = 0.0f;
    apply (e, a);

    auto runOne = [&e]
    {
        float s = 1.0f;
        float* chans[1] = { &s };
        e.process (chans, 1, 1);
    };

    for (int i = 0; i < 4800; ++i) runOne();   // 100 ms warm-up, well clear of any wrap

    Setup b = a; b.lane.phaseOffset = 0.3f;    // -0.2 turns: a backward glide
    apply (e, b);

    int changes = 0;
    float last = e.getLaneValue (0);
    for (int i = 0; i < 2000; ++i)             // spans the 30 ms (1440 sample) ramp and beyond
    {
        runOne();
        const float v = e.getLaneValue (0);
        if (v != last) ++changes;
        last = v;
    }
    INFO ("S&H value changes during a backward phase ramp: " << changes);
    REQUIRE (changes <= 2);
}

TEST_CASE ("smoothing (documentation): Rate and Smooth steps are click-free without any ramp",
           "[multilane][smoothing]")
{
    // Rate and the Smooth amount are NOT ramped. This pins down why: a step in either changes
    // how fast / how round the modulator moves, never the output sample itself, so the same
    // jump check passes on the engine with and without smoothing.
    const double sr = 48000.0;
    const int chunk = 64;

    Setup a; a.lane = frozenLane (Waveform::Sine, Dest::Volume, 1.0f, 0.1f); a.global.smooth = 0.0f;
    a.lane.rateHz = 1.0;

    SECTION ("rate 1 Hz -> 8 Hz")
    {
        Setup b = a; b.lane.rateHz = 8.0;
        const auto r = runStep (sr, sr / 100.0, chunk, a, b);
        INFO ("baseline " << r.baseline << " afterStep " << r.afterStep);
        REQUIRE (r.afterStep <= allowed (r));
    }

    SECTION ("smooth 0 -> 0.8")
    {
        Setup c = a; c.lane.waveform = Waveform::Square; c.lane.rateHz = 8.0;
        Setup d = c; d.global.smooth = 0.8f;
        const auto r = runStep (sr, sr / 100.0, chunk, c, d);
        INFO ("baseline " << r.baseline << " afterStep " << r.afterStep);
        REQUIRE (r.afterStep <= allowed (r));
    }
}
