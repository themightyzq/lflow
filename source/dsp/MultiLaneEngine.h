#pragma once
#include "LaneParams.h"
#include "LfoCore.h"
#include "LfoClock.h"
#include "LR4Crossover.h"
#include "ModDelay.h"

namespace lflow {

struct GlobalParams
{
    float mix    { 1.0f };  // 0..1
    float smooth { 0.15f }; // 0..1
};

// Orchestrates kNumLanes independent LFO lanes (each Volume- or Pan-assignable) and
// applies their combined gain + dry/wet to a buffer. Pure C++, RT-safe: no allocation,
// no locks. Operates in place on raw channel pointers. Replaces the Phase 1
// single-lane engine.
class MultiLaneEngine
{
public:
    static constexpr int kNumLanes = 3;

    // Phase 5 Pitch-dest (vibrato) constants -- exposed so tests can reference the
    // exact same values the engine uses (no magic-number drift between .cpp and
    // tests). See docs/superpowers/specs/2026-07-02-lflow-phase5-pitch-vibrato-design.md.
    static constexpr double kCenterMs   = 12.0; // center delay at mod == 0.5 (rest position)
    static constexpr double kMaxSwingMs = 10.0; // +/- swing at full combined depth, post-clamp

    void prepare (double sampleRate, int maxBlock) noexcept;
    void reset() noexcept;

    void setTransport (bool playing, double bpm, double ppqPosition) noexcept;
    void setLaneParams (int lane, const LaneParams& p) noexcept;
    void setGlobalParams (const GlobalParams& g) noexcept;

    // Band-split crossover points for the Low/Mid/High lane destinations. Defaults
    // 250/2500 Hz. Enforces effectiveHigh = max(highHz, lowHz*1.25) so the bands can't
    // invert; recomputes filter coefficients only for the split(s) whose value actually
    // changed (cheap compare-before-set, RT-safe: arithmetic only, no allocation).
    void setCrossovers (double lowHz, double highHz) noexcept;
    double getEffectiveHighHz() const noexcept { return xoverHighHz; }

    // Feeds a Custom-waveform lookup table (see ShapeModel.h/TripleBuffer.h) to a
    // lane's BOTH LfoCores (L/mono and R/pan share the same table pointer -- one
    // shape per lane, matching the Phase 4 design). data/size are borrowed, not
    // copied (RT-safe, no allocation); pass nullptr to unset (falls back to Sine
    // per LfoCore::valueAt). No-op for an out-of-range lane index.
    void setCustomTable (int lane, const float* data, int size) noexcept;

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    // Clock phase (including the lane's phase offset) that pairs with the last smoothed
    // L-channel modulator value returned by getLaneValue(), snapshotted at the same sample.
    float getLanePhase (int lane) const noexcept;
    float getLaneValue (int lane) const noexcept;

private:
    struct Lane
    {
        LaneParams params;
        LfoClock clock;
        LfoCore  lfo;   // left / mono channel
        LfoCore  lfoR;  // right channel for Pan dest (independent S&H state)

        float smoothL { 0.0f };
        float smoothR { 0.0f };

        float lastPhase { 0.0f }; // last evaluated phase incl. offset, for the UI
        float lastValue { 0.0f }; // last smoothed L-mod value, for the UI

        // Whether this lane was active (depth>0) as of the end of the previous
        // process() call. Used to detect the inactive->active edge for the L1
        // smoother-snap hardening fix (see process()): reactivating a lane
        // snaps smoothL/R straight to the first target instead of gliding from
        // a stale, time-elapsed value.
        bool wasActive { false };
    };

    float onePole (float target, float& state) const noexcept;

    Lane lanes[kNumLanes];
    GlobalParams globalParams;

    double sampleRate  { 44100.0 };
    bool   hostPlaying { false };
    double hostBpm     { 120.0 };
    double hostPpq     { 0.0 };

    // Band-split state (Phase 3). Hardcoded to 2 channels internally: process()
    // clamps/ignores channels beyond index 1 for band splitting (mono uses only
    // index 0's chain), but Volume/Pan lanes still apply to every channel as before.
    static constexpr int kNumBandChannels = 2;
    LR4Crossover lowSplit[kNumBandChannels];  // split @ xoverLowHz: (low, rest)
    LR4Crossover highSplit[kNumBandChannels]; // split @ xoverHighHz on `rest`: (mid, high)
    double xoverLowHz  { 250.0 };
    double xoverHighHz { 2500.0 }; // already clamped (effective) value
    bool   bandWasActive { false };

    // Pitch destination (Phase 5): per-channel modulated delay for LFO vibrato. Runs
    // strictly AFTER the band split/sum above and BEFORE the Volume/Pan lane gains
    // (see process()'s per-channel loop). Only channels 0/1 get a delay line -- same
    // 2-channel limitation as the band split above. Capacity allocated once in
    // prepare(); process()/reset() are RT-safe (see ModDelay.h's allocation notice).
    static constexpr double kPitchDelayCapacitySeconds = 0.064; // 64 ms, per design doc
    ModDelay pitchDelay[kNumBandChannels];
    bool     pitchWasActive { false };
};

} // namespace lflow
