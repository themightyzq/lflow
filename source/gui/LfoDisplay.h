#pragma once
#include <JuceHeader.h>
#include "dsp/LfoCore.h"

// Overlays all 3 lanes' one-cycle waveform curves plus a live position marker per active
// lane. Each lane's Path is cached in a normalised [0,1]x[0,1] unit square and rebuilt only
// when that lane's waveform or phase offset actually changes; painting scales the cached
// path onto the current bounds via an AffineTransform (no path copy/rebuild per frame).
class LfoDisplay : public juce::Component
{
public:
    static constexpr int kNumLanes = 3;

    // active == that lane's depth > 0. Rebuilds the cached path only if waveform/phaseOffset
    // changed since the last call.
    void setLane (int lane, lflow::Waveform waveform, float phaseOffset01, bool active);

    // phase01 = processor's reported lane phase (includes the lane's phase offset, i.e. the
    // phase actually fed into the waveform generator); value01 = the modulator value there.
    void setLanePosition (int lane, float phase01, float value01);

    void paint (juce::Graphics&) override;

private:
    struct LaneState
    {
        // Live params driving what/where to draw.
        lflow::Waveform waveform { lflow::Waveform::Sine };
        float phaseOffset { 0.0f };
        bool active { false };
        float phase { 0.0f };
        float value { 0.0f };

        // Cached unit-square path + the params it was built from (compare-before-rebuild).
        juce::Path path;
        bool pathValid { false };
        lflow::Waveform pathWaveform { lflow::Waveform::Sine };
        float pathPhaseOffset { -1.0f };
    };

    void rebuildPathIfNeeded (LaneState&);

    LaneState lanes[kNumLanes];
};
