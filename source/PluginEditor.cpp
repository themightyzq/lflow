#include "PluginEditor.h"
#include "params/ParameterIDs.h"

LFlOwAudioProcessorEditor::LFlOwAudioProcessorEditor (LFlOwAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = processorRef.getAPVTS();

    addAndMakeVisible (display);
    // Placeholder: show lane 1's motion only. Task 3 replaces this with the full
    // 3-lane-overlay display driven by all lanes' waveforms/phases/values.
    display.setWaveform (lflow::Waveform::Sine);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (LFlOwLookAndFeel::Colors::primary));
    bypassButton.setTooltip ("Bypass the effect, passing audio through unchanged");
    addAndMakeVisible (bypassButton);

    bypassAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, lflow::pid::bypass, bypassButton);

    setSize (560, 440);
    startTimerHz (60);
}

LFlOwAudioProcessorEditor::~LFlOwAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LFlOwAudioProcessorEditor::timerCallback()
{
    display.setPosition (processorRef.getLanePhase (0), processorRef.getLaneValue (0));
}

void LFlOwAudioProcessorEditor::paint (juce::Graphics& g)
{
    using C = LFlOwLookAndFeel::Colors;
    g.fillAll (juce::Colour (C::background));

    // Header accent line (2px, primary at 0.4 alpha).
    g.setColour (juce::Colour (C::primary).withAlpha (0.4f));
    g.fillRect (12, 4, getWidth() - 24, 2);

    // Title (16pt Bold) + brand (10pt), generic fonts — never name "Arial".
    g.setColour (juce::Colour (C::onSurface));
    g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    g.drawText ("LFlOw", 12, 12, getWidth() - 24, 20, juce::Justification::centred, false);
    g.setColour (juce::Colour (C::onSurfaceVariant));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("LFO + FLOW", 12, 32, getWidth() - 24, 14, juce::Justification::centred, false);

    // Version footer (bottom-right).
    g.setColour (juce::Colour (C::outline));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("v0.1.0", getLocalBounds().removeFromBottom (18).removeFromRight (70),
                juce::Justification::centredRight, false);
}

void LFlOwAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (40); // header (accent + title + brand)

    auto header = area.removeFromTop (28);
    bypassButton.setBounds (header.removeFromRight (80));

    area.removeFromTop (10);
    area.removeFromBottom (18); // footer
    display.setBounds (area);
}
