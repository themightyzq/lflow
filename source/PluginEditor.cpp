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
    const float alpha = isEnabled() ? 1.0f : 0.35f; // grey out when Link disables this chip
    if (editActive)
    {
        g.setColour (juce::Colour (C::onSurface).withAlpha (alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 2.0f);
    }
    // Following a linked lane 1 (finding #5): grey fill instead of lane colour, ON TOP of the
    // existing disabled-alpha dimming above -- the number stays legible either way.
    const juce::uint32 fillColour = isFollowing ? C::onSurfaceVariant : colour;
    g.setColour (juce::Colour (fillColour).withAlpha (alpha));
    g.fillRoundedRectangle (r.reduced (editActive ? 2.0f : 0.0f), 4.0f);
    g.setColour (juce::Colour (C::background).withAlpha (alpha));
    g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    g.drawText (juce::String (number), getLocalBounds(), juce::Justification::centred, false);
}

void LFlOwAudioProcessorEditor::LaneChip::mouseDown (const juce::MouseEvent&)
{
    if (onClick && isEnabled())
        onClick();
}

void LFlOwAudioProcessorEditor::LaneChip::setEditActive (bool active)
{
    if (editActive == active)
        return;
    editActive = active;
    repaint();
}

void LFlOwAudioProcessorEditor::LaneChip::setFollowing (bool following)
{
    if (isFollowing == following)
        return;
    isFollowing = following;
    repaint();
}

// ---------------------------------------------------------------------- Editor

LFlOwAudioProcessorEditor::LFlOwAudioProcessorEditor (LFlOwAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& apvts = processorRef.getAPVTS();

    addAndMakeVisible (display);
    display.setTooltip ("When a lane is in edit mode (its chip highlighted): click empty space "
                         "to add a node, drag a node to move it, drag a segment vertically to "
                         "bend it, double-click a node to delete it (2 minimum)");
    display.onNodesEdited = [this] (std::vector<lflow::ShapeNode> nodes)
    {
        if (editLane >= 0)
            processorRef.getShapeManager().setNodes (editLane, std::move (nodes));
    };

    for (int i = 0; i < 3; ++i)
        buildLaneStrip (i);

    linkButton.setTooltip ("Gang lanes 2 and 3 to lane 1's waveform, sync, rate, division, "
                            "and rhythm (phase offset, depth, and destination stay per-lane)");
    addAndMakeVisible (linkButton);
    linkAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, lflow::pid::link, linkButton);

    styleRotary (mixSlider, 46, 16);
    styleRotary (smoothSlider, 46, 16);
    styleRotary (xoverLowSlider, 62, 16);  // wide enough for "2000 Hz" (finding #1)
    styleRotary (xoverHighSlider, 62, 16); // wide enough for "2500 Hz" (finding #1)
    mixSlider.setTooltip ("Blend between dry and processed signal");
    smoothSlider.setTooltip ("Rounds off sharp waveform edges to avoid clicks");
    xoverLowSlider.setTooltip ("Crossover between the Low and Mid bands");
    xoverHighSlider.setTooltip ("Crossover between the Mid and High bands");
    for (auto* s : { &mixSlider, &smoothSlider, &xoverLowSlider, &xoverHighSlider }) addAndMakeVisible (s);
    for (auto* l : { &mixLabel, &smoothLabel, &xoverLowLabel, &xoverHighLabel })
    { l->setJustificationType (juce::Justification::centred); addAndMakeVisible (l); }
    mixAtt       = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::mix,       mixSlider);
    smoothAtt    = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::smooth,    smoothSlider);
    xoverLowAtt  = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::xoverLow,  xoverLowSlider);
    xoverHighAtt = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::xoverHigh, xoverHighSlider);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (LFlOwLookAndFeel::Colors::primary));
    bypassButton.setTooltip ("Bypass the effect, passing audio through unchanged");
    addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, lflow::pid::bypass, bypassButton);

    refreshEnablement();
    refreshXoverHint();

    setResizable (true, true);
    setResizeLimits (620, 560, 1000, 900);
    setSize (700, 620);
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
    const juce::String laneName = "Lane " + juce::String (i + 1);

    const auto colour = LFlOwLookAndFeel::laneColour (i);
    s.chip.setup (colour, i + 1);
    s.chip.setTooltip (laneName + ": when this lane's waveform is Custom, click to enter "
                        "or exit its breakpoint editor in the display above");
    s.chip.onClick = [this, i] { onLaneChipClicked (i); };
    addAndMakeVisible (s.chip);

    s.nameLabel.setText (laneName, juce::dontSendNotification);
    s.nameLabel.setColour (juce::Label::textColourId, juce::Colour (colour));
    s.nameLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    addAndMakeVisible (s.nameLabel);

    // Edit pill (finding #3): shares the nameLabel's column/bounds (see layoutLaneStrip) and is
    // mutually exclusive with it (refreshEnablement toggles setVisible on whichever applies) --
    // this borrows real estate from an unlabeled, spec-free column instead of widening the
    // strip's shared column layout (which WOULD risk the min-width no-truncation guarantee for
    // the labeled WAVE/SYNC/RATE/PHASE/DEST/DEPTH columns). setClickingTogglesState(false): the
    // pill's on/off state is driven by setEditLane, not by its own click (same as the chip).
    s.editButton.setClickingTogglesState (false);
    s.editButton.setColour (juce::ToggleButton::tickColourId, juce::Colour (colour));
    s.editButton.setTooltip (laneName + ": draw this lane's shape");
    s.editButton.onClick = [this, i] { onLaneChipClicked (i); };
    addChildComponent (s.editButton); // hidden until refreshEnablement() shows it

    s.waveformBox.addItemList ({ "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold", "Custom" }, 1);
    s.divisionBox.addItemList ({ "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }, 1);
    s.rhythmBox.addItemList   ({ "Straight", "Dotted", "Triplet" }, 1);
    s.destBox.addItemList     ({ "Volume", "Pan", "Low", "Mid", "High", "Pitch" }, 1);
    for (auto* b : { &s.waveformBox, &s.divisionBox, &s.rhythmBox, &s.destBox })
        addAndMakeVisible (b);

    addAndMakeVisible (s.syncButton);

    s.rateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    s.rateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    // Lane knobs (and now the Rate slider's fill) carry lane identity (the LnF default fill
    // is primary, reserved for global controls) — see drawLinearSlider/drawRotarySlider.
    s.rateSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (colour));
    addAndMakeVisible (s.rateSlider);

    styleRotary (s.phaseSlider, 40, 14);
    styleRotary (s.depthSlider, 44, 14);
    s.phaseSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (colour));
    s.depthSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (colour));
    addAndMakeVisible (s.phaseSlider);
    addAndMakeVisible (s.depthSlider);

    s.waveformBox.setTooltip (laneName + ": shape of the LFO motion");
    s.syncButton.setTooltip  (laneName + ": lock this lane's speed to host tempo");
    s.rateSlider.setTooltip  (laneName + ": LFO speed in Hz when Sync is off");
    s.divisionBox.setTooltip (laneName + ": note value per LFO cycle when Sync is on");
    s.rhythmBox.setTooltip   (laneName + ": straight, dotted, or triplet feel for the synced rate");
    s.phaseSlider.setTooltip (laneName + ": phase offset in degrees, relative to the other lanes");
    s.destBox.setTooltip     (laneName + ": what this lane modulates, Volume, Pan, a frequency band, or Pitch");
    s.depthSlider.setTooltip (laneName + ": how strongly this lane affects the signal");

    s.waveformAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.waveform, s.waveformBox);
    s.divisionAtt = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.division, s.divisionBox);
    s.rhythmAtt   = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.rhythm,   s.rhythmBox);
    s.destAtt     = std::make_unique<APVTS::ComboBoxAttachment> (apvts, ids.dest,     s.destBox);
    s.syncAtt     = std::make_unique<APVTS::ButtonAttachment>   (apvts, ids.sync,     s.syncButton);
    s.rateAtt     = std::make_unique<APVTS::SliderAttachment>   (apvts, ids.rateHz,   s.rateSlider);
    s.phaseAtt    = std::make_unique<APVTS::SliderAttachment>   (apvts, ids.phase,    s.phaseSlider);
    s.depthAtt    = std::make_unique<APVTS::SliderAttachment>   (apvts, ids.depth,    s.depthSlider);

    // lastWaveform is seeded AFTER waveformAtt's construction (its sendInitialUpdate already
    // synced the combo to whatever the processor's current/restored state holds) and onChange
    // is hooked up AFTER that seed, so a plugin/state load that starts a lane on Custom does
    // NOT read as "just changed to Custom" and auto-enter edit mode -- only a genuine later
    // change fires onWaveformSelected. See onWaveformSelected()'s doc comment (header) for the
    // auto-enter rule and its accepted trade-off.
    lastWaveform[(size_t) i] = static_cast<lflow::Waveform> (s.waveformBox.getSelectedItemIndex());
    s.waveformBox.onChange = [this, i] { onWaveformSelected (i); };
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

        // A linked follower (lanes 2-3 while Link is on) is exactly !motionEnabled here.
        const bool isFollower = ! motionEnabled;

        s.waveformBox.setEnabled (motionEnabled);
        s.syncButton.setEnabled (motionEnabled);
        // Mirrors waveformBox/syncButton: a linked follower's chip is a dead affordance (its
        // own edit mode can never open, see onLaneChipClicked/effectiveWaveform), so grey it
        // out visibly instead of leaving it clickable-but-inert.
        s.chip.setEnabled (motionEnabled);
        // Finding #5: grey fill (not just dimmed lane colour) while following, so the Link
        // relationship reads at a glance rather than requiring the user to notice a subtle
        // alpha difference.
        s.chip.setFollowing (isFollower);

        // Edit pill (finding #3): visible only when this lane's EFFECTIVE waveform is Custom
        // and it isn't a linked follower (a follower's own Custom shape, if any, isn't what's
        // playing -- entering its editor would silently edit a hidden shape, same gate as
        // onLaneChipClicked). Mutually exclusive with nameLabel -- they share one column.
        const bool showEdit = ! isFollower && effectiveWaveform (i) == lflow::Waveform::Custom;
        s.editButton.setVisible (showEdit);
        s.nameLabel.setVisible (! showEdit);

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

    for (int i = 0; i < 3; ++i)
    {
        const auto& ids = laneIds[i];
        const bool followsLane1Motion = (i != 0) && link;
        const auto waveform = effectiveWaveform (i);

        const float phaseDeg = raw (ids.phase); // phase offset is never linked
        const float depth = raw (ids.depth);

        display.setLane (i, waveform, phaseDeg / 360.0f, depth > 0.0f);
        display.setLanePosition (i, processorRef.getLanePhase (i), processorRef.getLaneValue (i));

        // Non-edited Custom lanes must render their REAL drawn shape, not LfoCore's Sine
        // fallback (LfoCore has no table for Waveform::Custom). Linked followers (i > 0
        // while link is on) show lane 0's shape, matching the audio routing (followsLane1Motion
        // above). setLaneNodes compares before invalidating, so this cheap ~3x/tick feed
        // (<=32 nodes each) doesn't churn the cached path when nothing changed. The edit lane
        // is fed separately via setEditNodes below -- skip it here so the two rendering paths
        // never fight over the same lane's node list.
        if (waveform == lflow::Waveform::Custom && i != editLane)
        {
            const int sourceLane = followsLane1Motion ? 0 : i;
            display.setLaneNodes (i, processorRef.getShapeManager().getNodes (sourceLane));
        }
    }

    if (editLane >= 0)
    {
        // Exit edit mode if the edited lane is now a linked follower (its own Custom shape,
        // if any, is no longer what's playing -- lane 0's is) or its effective waveform moved
        // away from Custom (e.g. via automation or a preset load); otherwise keep the display's
        // node list current in case it changed externally (undo, state reload) -- cheap,
        // <=32 nodes, and setEditNodes compares before rebaking/repainting.
        const bool editLaneIsFollower = (editLane != 0) && link;
        if (editLaneIsFollower || effectiveWaveform (editLane) != lflow::Waveform::Custom)
            setEditLane (-1);
        else
            display.setEditNodes (processorRef.getShapeManager().getNodes (editLane));
    }

    refreshEnablement();
    refreshXoverHint();
}

lflow::Waveform LFlOwAudioProcessorEditor::effectiveWaveform (int lane) const
{
    auto& apvts = processorRef.getAPVTS();
    auto raw = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    const bool link = raw (lflow::pid::link) > 0.5f;
    const bool followsLane1Motion = (lane != 0) && link;
    const char* waveformId = followsLane1Motion ? laneIds[0].waveform : laneIds[lane].waveform;
    return static_cast<lflow::Waveform> ((int) raw (waveformId));
}

void LFlOwAudioProcessorEditor::setEditLane (int lane)
{
    if (lane == editLane)
    {
        lane = -1; // clicking the already-active lane's chip exits edit mode
    }

    editLane = lane;
    for (int i = 0; i < 3; ++i)
    {
        auto& s = laneStrips[(size_t) i];
        const bool active = (i == editLane);
        s.chip.setEditActive (active);
        // Edit pill mirrors the chip exactly (same gate, same toggle) -- "Done" + lane-colour
        // fill while this lane is being edited, "Edit" otherwise (see drawToggleButton's
        // on-state using tickColourId, set to this lane's colour in buildLaneStrip).
        s.editButton.setToggleState (active, juce::dontSendNotification);
        s.editButton.setButtonText (active ? "Done" : "Edit");
    }

    display.setEditLane (editLane);
    if (editLane >= 0)
        display.setEditNodes (processorRef.getShapeManager().getNodes (editLane));
}

void LFlOwAudioProcessorEditor::onLaneChipClicked (int lane)
{
    auto& apvts = processorRef.getAPVTS();
    const bool link = apvts.getRawParameterValue (lflow::pid::link)->load() > 0.5f;

    // Linked followers (lanes 2-3 while Link is on) play lane 0's waveform regardless of their
    // own -- entering edit mode would silently edit the follower's own hidden shape while the
    // display/audio keep showing/playing lane 0's, which is exactly the confusion this gate
    // exists to prevent. Their chips are disabled in refreshEnablement() too.
    if (link && lane != 0)
        return;

    if (effectiveWaveform (lane) != lflow::Waveform::Custom)
        return; // non-Custom lanes' chips are a no-op for edit mode

    setEditLane (lane);
}

void LFlOwAudioProcessorEditor::onWaveformSelected (int lane)
{
    auto& s = laneStrips[(size_t) lane];
    const auto newWaveform = static_cast<lflow::Waveform> (s.waveformBox.getSelectedItemIndex());
    const bool changedToCustom = newWaveform == lflow::Waveform::Custom
                                  && lastWaveform[(size_t) lane] != lflow::Waveform::Custom;
    lastWaveform[(size_t) lane] = newWaveform;

    if (! changedToCustom || lane == editLane)
        return;

    // Linked followers can't enter edit mode (their own Custom shape, if any, isn't what's
    // playing -- see onLaneChipClicked's identical gate).
    const bool link = processorRef.getAPVTS().getRawParameterValue (lflow::pid::link)->load() > 0.5f;
    if (link && lane != 0)
        return;

    setEditLane (lane);
}

void LFlOwAudioProcessorEditor::refreshXoverHint()
{
    auto& apvts = processorRef.getAPVTS();
    const float low  = apvts.getRawParameterValue (lflow::pid::xoverLow)->load();
    const float high = apvts.getRawParameterValue (lflow::pid::xoverHigh)->load();
    const bool clamped = high < low * 1.25f;
    // Mirrors MultiLaneEngine::setXoverLow/setXoverHigh's own clamp formula (source/dsp/
    // MultiLaneEngine.cpp) so the tooltip's "effective" Hz always matches what's actually
    // playing -- this is a display-only mirror, not a second source of truth for the engine.
    const float effective = juce::jmax (high, low * 1.25f);
    const int effectiveHz = juce::roundToInt (effective);

    // STANDARDS-CLEAN choice (documented per Task 3 brief): the slider's own textFromValue
    // formatting stays untouched (single-source: the APVTS stringFromValue lambda remains the
    // only place that formats this parameter's displayed value, for hosts and editor alike) --
    // only the existing tint and the tooltip text change. The tooltip is made dynamic instead.
    const int stateKey = clamped ? (1000000 + effectiveHz) : 0;
    if (stateKey == xoverHiClampedState)
        return;
    xoverHiClampedState = stateKey;

    using C = LFlOwLookAndFeel::Colors;
    const auto colour = juce::Colour (clamped ? C::onSurfaceVariant : C::onSurface);
    xoverHighLabel.setColour (juce::Label::textColourId, colour);
    xoverHighSlider.setColour (juce::Slider::textBoxTextColourId, colour);

    juce::String tip = "Crossover between the Mid and High bands";
    if (clamped)
        tip << " (clamped by Low crossover - effective " << effectiveHz << " Hz)";
    xoverHighSlider.setTooltip (tip);
}

void LFlOwAudioProcessorEditor::paint (juce::Graphics& g)
{
    using C = LFlOwLookAndFeel::Colors;
    g.fillAll (juce::Colour (C::background));

    constexpr int margin = 12;

    // Header accent line (2px, primary at 0.4 alpha).
    g.setColour (juce::Colour (C::primary).withAlpha (0.4f));
    g.fillRect (margin, 4, getWidth() - margin * 2, 2);

    // Consolidated header line (finding #4/#8): "LFlOw" 16pt bold + "LFO + FLOW" 10pt in a
    // compact left lockup, Bypass right-aligned in the SAME line (bounds set in resized()) —
    // the old 40px brand block and Bypass's private row are gone.
    auto headerLine = getLocalBounds().reduced (margin).removeFromTop (32);

    juce::Font titleFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    g.setFont (titleFont);
    g.setColour (juce::Colour (C::onSurface));
    const int titleW = (int) std::ceil (juce::TextLayout::getStringWidth (titleFont, "LFlOw")) + 6;
    g.drawText ("LFlOw", headerLine.withWidth (titleW), juce::Justification::centredLeft, false);

    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.setColour (juce::Colour (C::onSurfaceVariant));
    g.drawText ("LFO + FLOW", headerLine.withTrimmedLeft (titleW + 8),
                juce::Justification::centredLeft, false);

    // Painted column-header row (finding #2): "WAVE SYNC RATE PHASE DEST DEPTH", x-aligned to
    // the SAME laneGrid columns layoutLaneStrip() uses (computed once in resized()).
    g.setColour (juce::Colour (C::onSurfaceVariant));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    auto drawColumnLabel = [&] (const ColumnSlot& c, const char* text)
    {
        juce::Rectangle<int> r (c.x, laneGrid.headerRow.getY(), c.w, laneGrid.headerRow.getHeight());
        g.drawText (text, r, juce::Justification::centred, false);
    };
    drawColumnLabel (laneGrid.wave,  "WAVE");
    drawColumnLabel (laneGrid.sync,  "SYNC");
    drawColumnLabel (laneGrid.rate,  "RATE");
    drawColumnLabel (laneGrid.phase, "PHASE");
    drawColumnLabel (laneGrid.dest,  "DEST");
    drawColumnLabel (laneGrid.depth, "DEPTH");

    // Global row grouping (finding #8): thin divider + "BANDS" micro-label scope the crossover
    // knobs, so they read as a subordinate group rather than equal-weight controls.
    g.setColour (juce::Colour (C::outline));
    g.fillRect (globalGrid.dividerLine);
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("BANDS", globalGrid.bandsLabel, juce::Justification::centred, false);

    // Version footer (bottom-right).
    g.setColour (juce::Colour (C::outline));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText ("v0.5.0", getLocalBounds().removeFromBottom (18).removeFromRight (70),
                juce::Justification::centredRight, false);
}

void LFlOwAudioProcessorEditor::resized()
{
    constexpr int margin = 12;
    auto area = getLocalBounds().reduced (margin);

    // Header line (~32px): title lockup (painted) + Bypass (component), right-aligned, both
    // in this one line — see paint().
    auto headerLine = area.removeFromTop (32);
    bypassButton.setBounds (headerLine.removeFromRight (80).withSizeKeepingCentre (80, 24));
    area.removeFromTop (4);

    // Footer (painted) claims its space first.
    area.removeFromBottom (18);

    // The global row and the 3 lane strips are fixed-height and anchored to the bottom; the
    // column-header row sits directly above the strips it labels; the DISPLAY absorbs
    // whatever vertical space is left between the header and that block (design system:
    // "vertical growth goes to the display").
    constexpr int globalRowHeight = 70;
    auto globalRow = area.removeFromBottom (globalRowHeight);
    area.removeFromBottom (14); // gap holding the painted "BANDS" micro-label above globalRow

    constexpr int stripHeight = 64;
    constexpr int stripGap = 6;
    auto stripsBlock = area.removeFromBottom (stripHeight * 3 + stripGap * 2);
    area.removeFromBottom (4);

    laneGrid.headerRow = area.removeFromBottom (14);
    area.removeFromBottom (2);

    // Column x-positions/widths are a function of width only, shared by the header row above
    // and every strip below (single source of truth — no duplicated magic x's).
    computeLaneColumns (laneGrid.headerRow);

    // Whatever's left grows with the window (>=200px at the 700x620 default per the design
    // system; horizontal growth stretches the display along with the strips' rate slot).
    display.setBounds (area);

    for (int i = 0; i < 3; ++i)
    {
        layoutLaneStrip (i, stripsBlock.removeFromTop (stripHeight));
        if (i < 2) stripsBlock.removeFromTop (stripGap);
    }

    // ---- Global row: Link pill | Mix, Smooth knobs | divider + "BANDS" | Xover Lo, Xover Hi.
    // This group's content is a fixed width (doesn't stretch — the design system only grows
    // the rate slot and the display) but is centered in the row rather than left-packed, so it
    // doesn't strand a lopsided gap on the right as the window widens.
    constexpr int gapG = 8;
    constexpr int knobColW = 60;
    constexpr int globalContentW = 60 + gapG + knobColW + gapG + knobColW + gapG
                                    + 16 + gapG + knobColW + gapG + 76;
    globalRow = globalRow.withSizeKeepingCentre (juce::jmin (globalContentW, globalRow.getWidth()),
                                                  globalRow.getHeight());

    auto linkArea = globalRow.removeFromLeft (60);
    linkButton.setBounds (linkArea.withSizeKeepingCentre (56, 26));

    globalRow.removeFromLeft (gapG);
    auto mixArea = globalRow.removeFromLeft (knobColW);
    mixLabel.setBounds (mixArea.removeFromTop (14));
    mixSlider.setBounds (mixArea);

    globalRow.removeFromLeft (gapG);
    auto smoothArea = globalRow.removeFromLeft (knobColW);
    smoothLabel.setBounds (smoothArea.removeFromTop (14));
    smoothSlider.setBounds (smoothArea);

    globalRow.removeFromLeft (gapG);
    auto dividerZone = globalRow.removeFromLeft (16);
    globalGrid.dividerLine = juce::Rectangle<int> (dividerZone.getCentreX(), globalRow.getY(),
                                                    1, globalRow.getHeight());

    globalRow.removeFromLeft (gapG);
    auto xoverLowArea = globalRow.removeFromLeft (knobColW);
    xoverLowLabel.setBounds (xoverLowArea.removeFromTop (14));
    xoverLowSlider.setBounds (xoverLowArea);

    globalRow.removeFromLeft (gapG);
    auto xoverHighArea = globalRow.removeFromLeft (76); // extra width: textbox needs "2500 Hz"
    xoverHighLabel.setBounds (xoverHighArea.removeFromTop (14));
    xoverHighSlider.setBounds (xoverHighArea);

    globalGrid.bandsLabel = juce::Rectangle<int> (xoverLowArea.getX(), globalRow.getY() - 12,
                                                   xoverHighArea.getRight() - xoverLowArea.getX(), 12);
}

void LFlOwAudioProcessorEditor::computeLaneColumns (juce::Rectangle<int> rowBounds)
{
    // Spec minimums as floors (Phase 6 design system); the rate slot absorbs any extra width
    // as the window widens, everything else stays at its floor.
    constexpr int gap = 4;
    constexpr int chipW = 20, nameW = 48, waveW = 120, syncW = 52, rateFloor = 150,
                  phaseW = 48, destW = 78, depthW = 52;
    constexpr int numGaps = 7;

    const int x0 = rowBounds.getX();
    const int totalW = rowBounds.getWidth();
    const int fixedSum = chipW + nameW + waveW + syncW + phaseW + destW + depthW;
    const int floorTotal = fixedSum + rateFloor + numGaps * gap;
    const int rateW = rateFloor + juce::jmax (0, totalW - floorTotal);

    int x = x0;
    auto place = [&] (ColumnSlot& slot, int w)
    {
        slot.x = x;
        slot.w = w;
        x += w + gap;
    };

    place (laneGrid.chip,  chipW);
    place (laneGrid.name,  nameW);
    place (laneGrid.wave,  waveW);
    place (laneGrid.sync,  syncW);
    place (laneGrid.rate,  rateW);
    place (laneGrid.phase, phaseW);
    place (laneGrid.dest,  destW);
    place (laneGrid.depth, depthW); // last column, trailing gap unused
}

void LFlOwAudioProcessorEditor::layoutLaneStrip (int i, juce::Rectangle<int> rowArea)
{
    auto& s = laneStrips[(size_t) i];
    const int y = rowArea.getY();
    const int h = rowArea.getHeight();

    // One shared control center line per strip (finding #4): flat controls (combo/pill/rate
    // slider) center on it directly; rotary knobs center their ARC on it too, with the value
    // textbox stacked tight underneath as part of the same Slider component (not floating).
    constexpr int centerLineY = 24;  // relative to the strip's top
    constexpr int flatH = 24;        // combo/pill/rate-slider box height
    constexpr int knobRegionH = 30;  // rotary arc region height (before its textbox)
    constexpr int textBoxH = 14;

    auto col = [&] (const ColumnSlot& c) { return juce::Rectangle<int> (c.x, y, c.w, h); };
    auto placeFlat = [&] (const ColumnSlot& c, int w)
    {
        return col (c).withSizeKeepingCentre (w, flatH).withY (y + centerLineY - flatH / 2);
    };
    auto placeKnob = [&] (juce::Slider& slider, const ColumnSlot& c)
    {
        slider.setBounds (col (c).withY (y + centerLineY - knobRegionH / 2)
                                  .withHeight (knobRegionH + textBoxH));
    };

    s.chip.setBounds (placeFlat (laneGrid.chip, 18));
    // editButton shares nameLabel's exact bounds -- only one of the two is ever visible
    // (refreshEnablement), so there is no layout cost to reserving this slot for both.
    const auto nameBounds = placeFlat (laneGrid.name, laneGrid.name.w);
    s.nameLabel.setBounds (nameBounds);
    s.editButton.setBounds (nameBounds);
    s.waveformBox.setBounds (placeFlat (laneGrid.wave, laneGrid.wave.w));
    s.syncButton.setBounds (placeFlat (laneGrid.sync, laneGrid.sync.w));

    // Rate slider and Division+Rhythm boxes occupy the same slot; refreshEnablement() shows/
    // enables whichever pair matches this lane's effective sync state.
    auto rateBounds = placeFlat (laneGrid.rate, laneGrid.rate.w);
    s.rateSlider.setBounds (rateBounds);
    auto divRhythm = rateBounds;
    constexpr int divisionW = 70;
    s.divisionBox.setBounds (divRhythm.removeFromLeft (divisionW));
    divRhythm.removeFromLeft (4);
    s.rhythmBox.setBounds (divRhythm);

    placeKnob (s.phaseSlider, laneGrid.phase);
    s.destBox.setBounds (placeFlat (laneGrid.dest, laneGrid.dest.w));
    placeKnob (s.depthSlider, laneGrid.depth);
}
