#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "gui/LFlOwLookAndFeel.h"
#include "gui/LfoDisplay.h"

// Minimal placeholder editor for Phase 2 Task 2 — keeps the plugin compiling and showing
// something after the lane-indexed parameter break. Task 3 rebuilds the full lane-strip UI.
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

    juce::TextButton bypassButton { "Bypass" };
    std::unique_ptr<APVTS::ButtonAttachment> bypassAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessorEditor)
};
