#include "PluginEditor.h"

LFlOwAudioProcessorEditor::LFlOwAudioProcessorEditor (LFlOwAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (520, 360);
}

void LFlOwAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1f));
    g.setColour (juce::Colours::white);
    g.setFont (24.0f);
    g.drawText ("LFlOw", getLocalBounds(), juce::Justification::centred, false);
}

void LFlOwAudioProcessorEditor::resized() {}
