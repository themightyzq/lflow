#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "gui/LFlOwLookAndFeel.h"
#include "gui/LfoDisplay.h"

class LFlOwAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit LFlOwAudioProcessorEditor (LFlOwAudioProcessor&);
    ~LFlOwAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    LFlOwAudioProcessor& processorRef;
    LFlOwLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 500 };

    LfoDisplay display;

    juce::ComboBox waveformBox, divisionBox, rhythmBox, modeBox;
    juce::ToggleButton syncButton { "Sync" };
    juce::TextButton bypassButton { "Bypass" };
    juce::Slider rateSlider, depthSlider, mixSlider, smoothSlider;

    std::unique_ptr<APVTS::ComboBoxAttachment>  waveformAtt, divisionAtt, rhythmAtt, modeAtt;
    std::unique_ptr<APVTS::ButtonAttachment>    syncAtt, bypassAtt;
    std::unique_ptr<APVTS::SliderAttachment>    rateAtt, depthAtt, mixAtt, smoothAtt;

    juce::Label rateLabel { {}, "Rate" }, depthLabel { {}, "Depth" },
                mixLabel  { {}, "Mix" },  smoothLabel { {}, "Smooth" };

    void styleRotary (juce::Slider&);
    // Enable only the rate controls that apply to the current Sync state:
    // Rate (Hz) when Sync is off; Division + Rhythm when Sync is on.
    void refreshSyncEnablement();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessorEditor)
};
