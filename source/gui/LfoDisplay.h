#pragma once
#include <JuceHeader.h>
#include "dsp/LfoCore.h"

// Draws one cycle of the selected waveform with a marker at the live LFO position.
class LfoDisplay : public juce::Component
{
public:
    void setWaveform (lflow::Waveform w) { waveform = w; repaint(); }
    void setPosition (float phase01, float value01) { phase = phase01; value = value01; repaint(); }
    void paint (juce::Graphics&) override;

private:
    lflow::Waveform waveform { lflow::Waveform::Sine };
    float phase { 0.0f };
    float value { 0.0f };
};
