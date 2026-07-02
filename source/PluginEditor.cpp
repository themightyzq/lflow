#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace
{
// Per-lane parameter ids, index-matched to LfoDisplay/LaneStrip lane index (0..2).
struct LaneIds
{
    const char* waveform;
    const char* sync;
    const char* rateHz;
    const char* division;
    const char* rhythm;
    const char* phase;
    const char* depth;
    const char* dest;
};

constexpr LaneIds laneIds[3] = {
    { lflow::pid::l1Waveform, lflow::pid::l1Sync, lflow::pid::l1RateHz, lflow::pid::l1Division,
      lflow::pid::l1Rhythm, lflow::pid::l1Phase, lflow::pid::l1Depth, lflow::pid::l1Dest },
    { lflow::pid::l2Waveform, lflow::pid::l2Sync, lflow::pid::l2RateHz, lflow::pid::l2Division,
      lflow::pid::l2Rhythm, lflow::pid::l2Phase, lflow::pid::l2Depth, lflow::pid::l2Dest },
    { lflow::pid::l3Waveform, lflow::pid::l3Sync, lflow::pid::l3RateHz, lflow::pid::l3Division,
      lflow::pid::l3Rhythm, lflow::pid::l3Phase, lflow::pid::l3Depth, lflow::pid::l3Dest },
};

constexpr juce::uint32 laneColour (int lane)
{
    using C = LFlOwLookAndFeel::Colors;
    switch (lane)
    {
        case 0:  return C::lane0;
        case 1:  return C::lane1;
        default: return C::lane2;
    }
}
} // namespace

// ---------------------------------------------------------------------- LaneChip

void LFlOwAudioProcessorEditor::LaneChip::setup (juce::uint32 colourArgb, int laneNumber)
{
    colour = colourArgb;
    number = laneNumber;
    repaint();
}

void LFlOwAudioProcessorEditor::LaneChip::paint (juce::Graphics& g)
{
    using C = LFlOwLookAndFeel::Colors;
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (colour));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colour (C::background));
    g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    g.drawText (juce::String (number), getLocalBounds(), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------- Editor

LFlOwAudioProcessorEditor::LFlOwAudioProcessorEditor (LFlOwAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = processorRef.getAPVTS();

    addAndMakeVisible (display);

    for (int i = 0; i < 3; ++i)
        buildLaneStrip (i);

    linkButton.setTooltip ("Gang lanes 2 and 3 to lane 1's waveform, sync, rate, division, "
                            "and rhythm (phase offset, depth, and destination stay per-lane)");
    addAndMakeVisible (linkButton);
    linkAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, lflow::pid::link, linkButton);

    styleRotary (mixSlider, 46, 16);
    styleRotary (smoothSlider, 46, 16);
    mixSlider.setTooltip ("Blend between dry and processed signal");
    smoothSlider.setTooltip ("Rounds off sharp waveform edges to avoid clicks");
    for (auto* s : { &mixSlider, &smoothSlider }) addAndMakeVisible (s);
    for (auto* l : { &mixLabel, &smoothLabel })
    { l->setJustificationType (juce::Justification::centred); addAndMakeVisible (l); }
    mixAtt    = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::mix,    mixSlider);
    smoothAtt = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::smooth, smoothSlider);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (LFlOwLookAndFeel::Colors::primary));
    bypassButton.setTooltip ("Bypass the effect, passing audio through unchanged");
    addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, lflow::pid::bypass, bypassButton);

    refreshEnablement();

    setSize (560, 640);
    startTimerHz (60);
}

LFlOwAudioProcessorEditor::~LFlOwAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LFlOwAudioProcessorEditor::styleRotary (juce::Slider& s, int textBoxWidth, int textBoxHeight)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, textBoxHeight);
}

void LFlOwAudioProcessorEditor::buildLaneStrip (int i)
{
    auto& apvts = processorRef.getAPVTS();
    auto& s = laneStrips[(size_t) i];
    const auto& ids = laneIds[i];

    s.syncId = ids.sync;
    s.waveformId = ids.waveform;
    s.phaseId = ids.phase;
    s.depthId = ids.depth;

    const auto colour = laneColour (i);
    s.chip.setup (colour, i + 1);
    addAndMakeVisible (s.chip);

    const juce::String laneName = "Lane " + juce::String (i + 1);
    s.nameLabel.setText (laneName, juce::dontSendNotification);
    s.nameLabel.setColour (juce::Label::textColourId, juce::Colour (colour));
    s.nameLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    addAndMakeVisible (s.nameLabel);

    s.waveformBox.addItemList ({ "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold" }, 1);
    s.divisionBox.addItemList ({ "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }, 1);
    s.rhythmBox.addItemList   ({ "Straight", "Dotted", "Triplet" }, 1);
    s.destBox.addItemList     ({ "Volume", "Pan" }, 1);
    for (auto* b : { &s.waveformBox, &s.divisionBox, &s.rhythmBox, &s.destBox })
        addAndMakeVisible (b);

    addAndMakeVisible (s.syncButton);

    s.rateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    s.rateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    addAndMakeVisible (s.rateSlider);

    styleRotary (s.phaseSlider, 40, 14);
    styleRotary (s.depthSlider, 42, 14);
    addAndMakeVisible (s.phaseSlider);
    addAndMakeVisible (s.depthSlider);

    s.waveformBox.setTooltip (laneName + ": shape of the LFO motion");
    s.syncButton.setTooltip  (laneName + ": lock this lane's speed to host tempo");
    s.rateSlider.setTooltip  (laneName + ": LFO speed in Hz when Sync is off");
    s.divisionBox.setTooltip (laneName + ": note value per LFO cycle when Sync is on");
    s.rhythmBox.setTooltip   (laneName + ": straight, dotted, or triplet feel for the synced rate");
    s.phaseSlider.setTooltip (laneName + ": phase offset in degrees, relative to the other lanes");
    s.destBox.setTooltip     (laneName + ": what this lane modulates, Volume or Pan");
    s.depthSlider.setTooltip (laneName + ": how strongly this lane affects the signal");

    s.waveformAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.waveform, s.waveformBox);
    s.divisionAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.division, s.divisionBox);
    s.rhythmAtt   = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.rhythm,   s.rhythmBox);
    s.destAtt     = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.dest,     s.destBox);
    s.syncAtt     = std::make_unique<APVTS::ButtonAttachment>   (apvts, ids.sync,     s.syncButton);
    s.rateAtt     = std::make_unique<APVTS::SliderAttachment>   (apvts, ids.rateHz,   s.rateSlider);
    s.phaseAtt    = std::make_unique<APVTS::SliderAttachment>   (apvts, ids.phase,    s.phaseSlider);
    s.depthAtt    = std::make_unique<APVTS::SliderAttachment>   (apvts, ids.depth,    s.depthSlider);
}

void LFlOwAudioProcessorEditor::refreshEnablement()
{
    auto& apvts = processorRef.getAPVTS();
    auto raw = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    const bool link = raw (lflow::pid::link) > 0.5f;
    const bool lane1Sync = raw (laneIds[0].sync) > 0.5f;

    for (int i = 0; i < 3; ++i)
    {
        auto& s = laneStrips[(size_t) i];
        const bool isLane1 = (i == 0);
        // Lane 1 is always enabled; lanes 2-3's motion controls grey out when linked.
        const bool motionEnabled = isLane1 || ! link;
        // When linked, lanes 2-3's effective sync (for the rate-vs-division decision) is
        // lane 1's sync, per the Phase 2 link-gangs-motion design.
        const bool effectiveSync = link ? lane1Sync : (raw (s.syncId) > 0.5f);

        s.waveformBox.setEnabled (motionEnabled);
        s.syncButton.setEnabled (motionEnabled);

        s.rateSlider.setEnabled (motionEnabled && ! effectiveSync);
        s.rateSlider.setVisible (! effectiveSync);

        const bool divRhythmEnabled = motionEnabled && effectiveSync;
        s.divisionBox.setEnabled (divRhythmEnabled);
        s.divisionBox.setVisible (effectiveSync);
        s.rhythmBox.setEnabled (divRhythmEnabled);
        s.rhythmBox.setVisible (effectiveSync);

        // Phase/Dest/Depth are never disabled by link.
    }
}

void LFlOwAudioProcessorEditor::timerCallback()
{
    auto& apvts = processorRef.getAPVTS();
    auto raw = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    const bool link = raw (lflow::pid::link) > 0.5f;
    const auto lane1Waveform = static_cast<lflow::Waveform> ((int) raw (laneIds[0].waveform));

    for (int i = 0; i < 3; ++i)
    {
        const auto& ids = laneIds[i];
        const bool followsLane1Motion = (i != 0) && link;
        const auto waveform = followsLane1Motion
            ? lane1Waveform
            : static_cast<lflow::Waveform> ((int) raw (ids.waveform));

        const float phaseDeg = raw (ids.phase); // phase offset is never linked
        const float depth = raw (ids.depth);

        display.setLane (i, waveform, phaseDeg / 360.0f, depth > 0.0f);
        display.setLanePosition (i, processorRef.getLanePhase (i), processorRef.getLaneValue (i));
    }

    refreshEnablement();
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
    area.removeFromTop (40); // header block (accent + title + brand, drawn in paint)

    auto header = area.removeFromTop (28);
    bypassButton.setBounds (header.removeFromRight (80));

    area.removeFromTop (10);
    area.removeFromBottom (18); // footer (drawn in paint)

    display.setBounds (area.removeFromTop (150));
    area.removeFromTop (8);

    for (int i = 0; i < 3; ++i)
    {
        layoutLaneStrip (i, area.removeFromTop (85));
        if (i < 2) area.removeFromTop (6);
    }

    area.removeFromTop (10);
    auto globalRow = area; // remaining space for the global controls row

    auto linkArea = globalRow.removeFromLeft (72);
    linkButton.setBounds (linkArea.withSizeKeepingCentre (66, 26));

    globalRow.removeFromLeft (12);
    auto mixArea = globalRow.removeFromLeft (70);
    mixLabel.setBounds (mixArea.removeFromTop (16));
    mixSlider.setBounds (mixArea);

    globalRow.removeFromLeft (12);
    auto smoothArea = globalRow.removeFromLeft (70);
    smoothLabel.setBounds (smoothArea.removeFromTop (16));
    smoothSlider.setBounds (smoothArea);
}

void LFlOwAudioProcessorEditor::layoutLaneStrip (int i, juce::Rectangle<int> area)
{
    auto& s = laneStrips[(size_t) i];
    constexpr int gap = 5;
    const int stripHeight = area.getHeight();

    auto chipArea = area.removeFromLeft (20);
    s.chip.setBounds (chipArea.withSizeKeepingCentre (18, 18));
    area.removeFromLeft (gap);

    auto labelArea = area.removeFromLeft (46);
    s.nameLabel.setBounds (labelArea.withSizeKeepingCentre (46, 16));
    area.removeFromLeft (gap);

    auto waveformArea = area.removeFromLeft (92);
    s.waveformBox.setBounds (waveformArea.withSizeKeepingCentre (92, 24));
    area.removeFromLeft (gap);

    auto syncArea = area.removeFromLeft (46);
    s.syncButton.setBounds (syncArea.withSizeKeepingCentre (46, 24));
    area.removeFromLeft (gap);

    // Rate slider and Division+Rhythm boxes occupy the same slot; refreshEnablement()
    // shows/enables whichever pair matches this lane's effective sync state.
    auto rateSlotArea = area.removeFromLeft (140);
    auto rateBounds = rateSlotArea.withSizeKeepingCentre (140, 24);
    s.rateSlider.setBounds (rateBounds);
    auto divRhythm = rateBounds;
    s.divisionBox.setBounds (divRhythm.removeFromLeft (68));
    divRhythm.removeFromLeft (4);
    s.rhythmBox.setBounds (divRhythm);
    area.removeFromLeft (gap);

    auto phaseArea = area.removeFromLeft (42);
    s.phaseSlider.setBounds (phaseArea.withSizeKeepingCentre (42, stripHeight));
    area.removeFromLeft (gap);

    auto destArea = area.removeFromLeft (62);
    s.destBox.setBounds (destArea.withSizeKeepingCentre (62, 24));
    area.removeFromLeft (gap);

    // Depth rotary takes the remainder of the strip's width.
    s.depthSlider.setBounds (area.withSizeKeepingCentre (juce::jmin (46, area.getWidth()), stripHeight));
}
