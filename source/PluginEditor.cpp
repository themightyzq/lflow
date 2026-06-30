#include "PluginEditor.h"
#include "params/ParameterIDs.h"

LFlOwAudioProcessorEditor::LFlOwAudioProcessorEditor (LFlOwAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = processorRef.getAPVTS();

    addAndMakeVisible (display);

    waveformBox.addItemList ({ "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold" }, 1);
    divisionBox.addItemList ({ "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }, 1);
    rhythmBox.addItemList   ({ "Straight", "Dotted", "Triplet" }, 1);
    modeBox.addItemList     ({ "Tremolo", "Pan" }, 1);
    for (auto* b : { &waveformBox, &divisionBox, &rhythmBox, &modeBox }) addAndMakeVisible (b);

    addAndMakeVisible (syncButton);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (LFlOwLookAndFeel::Colors::primary));
    addAndMakeVisible (bypassButton);

    for (auto* s : { &rateSlider, &depthSlider, &mixSlider, &smoothSlider }) { styleRotary (*s); addAndMakeVisible (s); }
    for (auto* l : { &rateLabel, &depthLabel, &mixLabel, &smoothLabel })
    { l->setJustificationType (juce::Justification::centred); addAndMakeVisible (l); }

    // Tooltips — describe what each control DOES (ASCII only).
    waveformBox.setTooltip ("Shape of the LFO motion");
    syncButton.setTooltip  ("Lock the LFO speed to host tempo");
    divisionBox.setTooltip ("Note value per LFO cycle when Sync is on");
    rhythmBox.setTooltip   ("Straight, dotted, or triplet feel for the synced rate");
    modeBox.setTooltip     ("Tremolo modulates volume; Pan sweeps left-right");
    bypassButton.setTooltip ("Bypass the effect, passing audio through unchanged");
    rateSlider.setTooltip  ("LFO speed in Hz when Sync is off");
    depthSlider.setTooltip ("How strongly the LFO affects the signal");
    mixSlider.setTooltip   ("Blend between dry and processed signal");
    smoothSlider.setTooltip ("Rounds off sharp waveform edges to avoid clicks");

    waveformAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, lflow::pid::waveform, waveformBox);
    divisionAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, lflow::pid::division, divisionBox);
    rhythmAtt   = std::make_unique<APVTS::ComboBoxAttachment> (apvts, lflow::pid::rhythm,   rhythmBox);
    modeAtt     = std::make_unique<APVTS::ComboBoxAttachment> (apvts, lflow::pid::mode,     modeBox);
    syncAtt     = std::make_unique<APVTS::ButtonAttachment>   (apvts, lflow::pid::sync,     syncButton);
    bypassAtt   = std::make_unique<APVTS::ButtonAttachment>   (apvts, lflow::pid::bypass,   bypassButton);
    rateAtt     = std::make_unique<APVTS::SliderAttachment>   (apvts, lflow::pid::rateHz,   rateSlider);
    depthAtt    = std::make_unique<APVTS::SliderAttachment>   (apvts, lflow::pid::depth,    depthSlider);
    mixAtt      = std::make_unique<APVTS::SliderAttachment>   (apvts, lflow::pid::mix,      mixSlider);
    smoothAtt   = std::make_unique<APVTS::SliderAttachment>   (apvts, lflow::pid::smooth,   smoothSlider);

    waveformBox.onChange = [this]
    { display.setWaveform (static_cast<lflow::Waveform> (waveformBox.getSelectedItemIndex())); };
    display.setWaveform (static_cast<lflow::Waveform> (juce::jmax (0, waveformBox.getSelectedItemIndex())));

    setSize (560, 440);
    startTimerHz (60);
}

LFlOwAudioProcessorEditor::~LFlOwAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LFlOwAudioProcessorEditor::styleRotary (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);
}

void LFlOwAudioProcessorEditor::timerCallback()
{
    display.setPosition (processorRef.getLfoPhase(), processorRef.getLfoValue());
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

    auto top = area.removeFromTop (28);
    waveformBox.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (8);
    syncButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (8);
    divisionBox.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (6);
    rhythmBox.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (8);
    modeBox.setBounds (top.removeFromLeft (100));

    area.removeFromTop (10);
    display.setBounds (area.removeFromTop (170));

    area.removeFromTop (10);
    area.removeFromBottom (18); // footer
    auto knobs = area;
    const int kw = knobs.getWidth() / 4;
    auto place = [&] (juce::Slider& s, juce::Label& l, juce::Rectangle<int> r)
    {
        l.setBounds (r.removeFromTop (16));
        s.setBounds (r.reduced (6));
    };
    place (rateSlider,   rateLabel,   knobs.removeFromLeft (kw));
    place (depthSlider,  depthLabel,  knobs.removeFromLeft (kw));
    place (mixSlider,    mixLabel,    knobs.removeFromLeft (kw));
    place (smoothSlider, smoothLabel, knobs);
}
