#pragma once
#include "LaneParams.h"
#include "LfoCore.h"
#include "LfoClock.h"
#include "LR4Crossover.h"

namespace lflow {

struct GlobalParams
{
    float mix    { 1.0f };  // 0..1
    float smooth { 0.15f }; // 0..1
};

// Orchestrates kNumLanes independent LFO lanes (each Volume- or Pan-assignable) and
// applies their combined gain + dry/wet to a buffer. Pure C++, RT-safe: no allocation,
// no locks. Operates in place on raw channel pointers. Replaces the Phase 1
// single-lane ChopperEngine.
class MultiLaneEngine
{
public:
    static constexpr int kNumLanes = 3;

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
};

} // namespace lflow
