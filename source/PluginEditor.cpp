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

// Phase 7 Task 5: every knob's setDoubleClickReturnValue() reads its default straight off the
// live APVTS parameter (AudioProcessorParameter::getDefaultValue(), which is normalized [0,1])
// rather than a hardcoded literal, so this can never drift from ParameterLayout.cpp. Returns
// 0.0f (a harmless no-op default) if the id is somehow unknown -- jassert catches that in debug.
float paramDefault (juce::AudioProcessorValueTreeState& apvts, const char* paramId)
{
    auto* p = apvts.getParameter (paramId);
    jassert (p != nullptr);
    return p != nullptr ? p->convertFrom0to1 (p->getDefaultValue()) : 0.0f;
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
    // House silk face (Barlow Condensed) via whatever LookAndFeel is active, with a plain
    // generic-font fallback so this never fails to draw the lane number.
    if (auto* houseLnF = dynamic_cast<zqsfx::ui::LookAndFeel*> (&getLookAndFeel()))
        g.setFont (houseLnF->silkFont (11.0f, true));
    else
        g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    g.drawText (juce::String (number), getLocalBounds(), juce::Justification::centred, false);
}

void LFlOwAudioProcessorEditor::LaneChip::mouseDown (const juce::MouseEvent& e)
{
    // Phase 7 Task 5 (UX #4): right-click -> reset menu, deliberately NOT gated by
    // isEnabled() -- a linked follower's own params/curve can still be reset even while its
    // motion controls are greyed out (its left-click edit-mode toggle stays isEnabled()-gated
    // below, unchanged).
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick();
        return;
    }

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
    // Editor size persistence: read the saved size back BEFORE anything below can move it.
    // setResizeLimits() (further down) clamps the editor's bounds to its new minimum as a side
    // effect (AudioProcessorEditor::setResizeLimits() -> setBoundsConstrained(getBounds()), and
    // the editor starts at the Component default of 0x0) -- that clamp is itself a real size
    // change and fires resized(), which (see below) writes the CURRENT size back to the
    // processor. Reading the stored size here, first, means that transient 620x560 write can
    // never be mistaken for a genuine saved session size.
    const int storedWidth = processorRef.getEditorWidth();
    const int storedHeight = processorRef.getEditorHeight();

    setLookAndFeel (&lookAndFeel);
    auto& apvts = processorRef.getAPVTS();

    // Phase 7 Task 3 (UX #1): makes the editor itself a valid keyboard-focus target, so Cmd-Z /
    // Cmd-Shift-Z (keyPressed, below) always has somewhere to dispatch from -- see mouseDown's
    // doc comment (header) for how focus actually lands here after a click.
    setWantsKeyboardFocus (true);

    addAndMakeVisible (display);
    display.setTooltip ("When a lane is in edit mode (its chip highlighted): click empty space "
                         "to add a node, drag a node to move it, drag a segment's diamond handle "
                         "vertically to bend it, double-click a node to delete it (2 minimum). "
                         "Hold Shift while dragging a node to snap to the grid. Right-click for "
                         "reset options.");
    // Accessibility floor (style guide section 8): a custom component (not a stock JUCE
    // control) needs its own setAccessible/title/description -- a screen reader has no useful
    // name for a bare Component otherwise.
    display.setTitle ("LFO shape display");
    display.setDescription ("Overlaid one-cycle curves for all 3 lanes plus each active lane's "
                             "live position marker; also the in-place breakpoint editor for "
                             "whichever lane is in edit mode.");
    display.setAccessible (true);

    // The ZQ SFX mark (style guide section 5): header row, far right; also the About-box
    // trigger (LogoMark sets its own tooltip/title/description to "About LFlOw" already).
    logo.onClick = [this] { showAboutBox(); };
    addAndMakeVisible (logo);
    display.onNodesEdited = [this] (std::vector<lflow::ShapeNode> nodes)
    {
        if (editLane >= 0)
            processorRef.getShapeManager().setNodes (editLane, std::move (nodes), &processorRef.getUndoManager());
    };
    // Phase 7 Task 3 (UX #1): one undo transaction per curve-editor gesture (see
    // LfoDisplay::onGestureStart's doc comment) -- a whole node drag is many onNodesEdited
    // calls but exactly one beginNewTransaction() call, so it undoes/redoes as one step.
    display.onGestureStart = [this]
    {
        processorRef.getUndoManager().beginNewTransaction ("Edit shape");
    };
    // Phase 7 Task 5 (UX #4): the display's own right-click "Reset curve to triangle" menu
    // (edit mode only, so editLane is always valid here -- see LfoDisplay::mouseDown, which
    // only shows that menu when editLane >= 0).
    display.onResetCurveRequested = [this]
    {
        if (editLane >= 0)
            resetLaneCurve (editLane);
    };

    for (int i = 0; i < 3; ++i)
        buildLaneStrip (i);

    linkButton.setTooltip ("Gang lanes 2 and 3 to lane 1's waveform, sync, rate, division, "
                            "and rhythm (phase offset, depth, and destination stay per-lane)");
    linkButton.setTitle ("Link");
    linkButton.setDescription (linkButton.getTooltip());
    addAndMakeVisible (linkButton);
    linkAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, lflow::pid::link, linkButton);

    styleRotary (mixSlider, 46, 16);
    styleRotary (smoothSlider, 46, 16);
    styleRotary (xoverLowSlider, 62, 16);  // wide enough for "2000 Hz" (finding #1)
    styleRotary (xoverHighSlider, 62, 16); // wide enough for "2500 Hz" (finding #1)
    // Phase 7 Task 5 (UX #4): double-click-to-default on every global rotary, value pulled
    // live from the APVTS parameter's own default (paramDefault helper, above) rather than
    // hardcoded -- see that helper's doc comment.
    mixSlider.setDoubleClickReturnValue (true, paramDefault (apvts, lflow::pid::mix));
    smoothSlider.setDoubleClickReturnValue (true, paramDefault (apvts, lflow::pid::smooth));
    xoverLowSlider.setDoubleClickReturnValue (true, paramDefault (apvts, lflow::pid::xoverLow));
    xoverHighSlider.setDoubleClickReturnValue (true, paramDefault (apvts, lflow::pid::xoverHigh));
    mixSlider.setTooltip ("Blend between dry and processed signal");
    smoothSlider.setTooltip ("Rounds off sharp waveform edges to avoid clicks");
    xoverLowSlider.setTooltip ("Crossover between the Low and Mid bands");
    xoverHighSlider.setTooltip ("Crossover between the Mid and High bands");
    mixSlider.setTitle ("Mix");             mixSlider.setDescription (mixSlider.getTooltip());
    smoothSlider.setTitle ("Smooth");       smoothSlider.setDescription (smoothSlider.getTooltip());
    xoverLowSlider.setTitle ("Xover Lo");   xoverLowSlider.setDescription (xoverLowSlider.getTooltip());
    xoverHighSlider.setTitle ("Xover Hi");  xoverHighSlider.setDescription (xoverHighSlider.getTooltip());
    for (auto* s : { &mixSlider, &smoothSlider, &xoverLowSlider, &xoverHighSlider }) addAndMakeVisible (s);
    for (auto* l : { &mixLabel, &smoothLabel, &xoverLowLabel, &xoverHighLabel })
    { l->setJustificationType (juce::Justification::centred); addAndMakeVisible (l); }
    mixAtt       = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::mix,       mixSlider);
    smoothAtt    = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::smooth,    smoothSlider);
    xoverLowAtt  = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::xoverLow,  xoverLowSlider);
    xoverHighAtt = std::make_unique<APVTS::SliderAttachment> (apvts, lflow::pid::xoverHigh, xoverHighSlider);

    // buttonOnColourId is no longer set here: the house drawButtonBackground always paints an
    // "on" TextButton as colour::accent regardless of this colour id (and Colors::primary is
    // now that exact same accent value), so the old override had become a no-op.
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass the effect, passing audio through unchanged");
    bypassButton.setTitle ("Bypass");
    bypassButton.setDescription (bypassButton.getTooltip());
    addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<APVTS::ButtonAttachment> (apvts, lflow::pid::bypass, bypassButton);

    // Phase 7 Task 3 (UX #1): momentary undo/redo pills. Enabled state is refreshed in
    // timerCallback(); Component::setEnabled() itself early-outs when the value doesn't change,
    // so no extra caching is needed here to keep the 60Hz refresh cheap.
    undoButton.setTooltip ("Undo (Cmd-Z)");
    redoButton.setTooltip ("Redo (Cmd-Shift-Z)");
    undoButton.setTitle ("Undo");   undoButton.setDescription (undoButton.getTooltip());
    redoButton.setTitle ("Redo");   redoButton.setDescription (redoButton.getTooltip());
    undoButton.onClick = [this] { processorRef.getUndoManager().undo(); };
    redoButton.onClick = [this] { processorRef.getUndoManager().redo(); };
    undoButton.setEnabled (false);
    redoButton.setEnabled (false);
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    // Phase 7 Task 4 (UX #2): preset bar row under the header.
    buildPresetBar();

    refreshEnablement();
    refreshXoverHint();
    refreshPresetBar();

    setResizable (true, true);
    setResizeLimits (620, 560, 1000, 900);

    // Editor size persistence: apply the size captured at the top of this ctor (see the comment
    // there) if it looks like a genuine saved session size -- non-zero and within the
    // constrainer just installed by setResizeLimits() above (guards against a stale/corrupt
    // saved value or a resize-limits change since it was saved). Otherwise fall back to the
    // plugin's normal default size. This setSize() is the last word on the constructor's initial
    // size; resized() (below) then persists whatever size actually results.
    int initialWidth = 700, initialHeight = 620;
    if (storedWidth > 0 && storedHeight > 0)
    {
        if (auto* c = getConstrainer())
        {
            if (storedWidth >= c->getMinimumWidth() && storedWidth <= c->getMaximumWidth()
                && storedHeight >= c->getMinimumHeight() && storedHeight <= c->getMaximumHeight())
            {
                initialWidth = storedWidth;
                initialHeight = storedHeight;
            }
        }
    }
    setSize (initialWidth, initialHeight);
    startTimerHz (60);

    // NOTE (Phase 7 final review): no explicit grabKeyboardFocus() anywhere --
    // EDITOR_WANTS_KEYBOARD_FOCUS is FALSE for Soundminer compat (host shortcuts like
    // spacebar preview must keep working after clicking the plugin). setWantsKeyboardFocus
    // (true) alone provides standard click-to-focus on the editor background, which covers
    // Cmd-Z in the standalone; in hosts the Undo/Redo pills are the guaranteed path.
}

LFlOwAudioProcessorEditor::~LFlOwAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void LFlOwAudioProcessorEditor::mouseDown (const juce::MouseEvent&)
{
    // Background clicks: nothing to force here. Click-to-focus is handled by JUCE via
    // setWantsKeyboardFocus(true); we deliberately do NOT force-grab focus (host compat).
}

bool LFlOwAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    auto& um = processorRef.getUndoManager();

    // Consume the key ONLY when we actually act on it; otherwise return false so the host
    // keeps its own shortcuts working (Phase 7 final-review fix).
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        return um.canUndo() && um.undo();

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
        return um.canRedo() && um.redo();

    return false;
}

void LFlOwAudioProcessorEditor::showAboutBox()
{
    // ASCII-only (project rule); product name + version from the real build (JucePlugin_
    // VersionString, generated from CMakeLists.txt's project(... VERSION ...)), not a
    // hand-maintained literal that could drift from it.
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "About LFlOw",
        juce::String ("LFlOw ") + JucePlugin_VersionString +
            "\n\nZQ SFX - https://www.zq-sfx.com - connect@zq-sfx.com\n"
            "Free software under GPL-3.0-or-later. Built with JUCE.\n"
            "Fonts: Barlow Condensed, VT323, IBM Plex Mono (SIL OFL).\n"
            "Knobs: CC0 designs from the g200kg KnobGallery.",
        "Close", this);
}

// ---------------------------------------------------------------- Preset bar (P7 T4, UX #2)

void LFlOwAudioProcessorEditor::buildPresetBar()
{
    presetPrevButton.setTooltip ("Load the previous preset (factory list first, then your saved presets)");
    presetNextButton.setTooltip ("Load the next preset (factory list first, then your saved presets)");
    presetMenuButton.setTooltip ("Preset menu: factory presets, your saved presets, Save As, open presets folder");
    presetPrevButton.setTitle ("Previous preset"); presetPrevButton.setDescription (presetPrevButton.getTooltip());
    presetNextButton.setTitle ("Next preset");     presetNextButton.setDescription (presetNextButton.getTooltip());
    presetMenuButton.setTitle ("Preset menu");     presetMenuButton.setDescription (presetMenuButton.getTooltip());
    presetPrevButton.onClick = [this] { processorRef.getPresetManager().loadNeighbour (-1); refreshPresetBar(); };
    presetNextButton.onClick = [this] { processorRef.getPresetManager().loadNeighbour (+1); refreshPresetBar(); };
    presetMenuButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetPrevButton);
    addAndMakeVisible (presetNextButton);
    addAndMakeVisible (presetMenuButton);

    presetNameLabel.setJustificationType (juce::Justification::centred);
    presetNameLabel.setColour (juce::Label::textColourId, juce::Colour (LFlOwLookAndFeel::Colors::onSurface));
    presetNameLabel.setTooltip ("Current preset -- a trailing * means settings have changed since it was loaded");
    addAndMakeVisible (presetNameLabel);

    // A/B pills: toggle look driven manually from PresetManager::getActiveSlot() (same
    // pattern as the lane Edit pills -- the click acts on the manager, refreshPresetBar()
    // reflects the result). No buttonOnColourId override needed: the house drawButtonBackground
    // always paints an "on" TextButton as colour::accent regardless of that colour id.
    for (auto* b : { &slotAButton, &slotBButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (b);
    }
    slotAButton.setTooltip ("Switch to compare slot A -- the current settings are kept in slot B "
                             "so you can flip between the two (one undo step)");
    slotBButton.setTooltip ("Switch to compare slot B -- the current settings are kept in slot A "
                             "so you can flip between the two (one undo step)");
    slotAButton.setTitle ("Slot A"); slotAButton.setDescription (slotAButton.getTooltip());
    slotBButton.setTitle ("Slot B"); slotBButton.setDescription (slotBButton.getTooltip());
    slotAButton.onClick = [this] { processorRef.getPresetManager().switchToSlot (0); refreshPresetBar(); };
    slotBButton.onClick = [this] { processorRef.getPresetManager().switchToSlot (1); refreshPresetBar(); };

    copySlotButton.setTooltip ("Copy the current settings into the inactive compare slot");
    copySlotButton.setTitle ("Copy");
    copySlotButton.setDescription (copySlotButton.getTooltip());
    copySlotButton.onClick = [this] { processorRef.getPresetManager().copyActiveToOther(); };
    addAndMakeVisible (copySlotButton);

    // Rename (USER presets only) -- enablement + the state-dependent half of the tooltip are
    // set from refreshPresetBar(); title/description are fixed. Disabled by default until the
    // first refreshPresetBar() call below runs (factory/"Init" is the safe starting state).
    renameButton.setTitle ("Rename preset");
    renameButton.setDescription ("Rename the currently loaded user preset");
    renameButton.onClick = [this] { startRenameDialog(); };
    renameButton.setEnabled (false);
    addAndMakeVisible (renameButton);
}

void LFlOwAudioProcessorEditor::refreshPresetBar()
{
    auto& manager = processorRef.getPresetManager();

    // setText()/setToggleState() early-out when unchanged, so calling this from the throttled
    // timer path never repaints a quiescent bar.
    const auto text = manager.getCurrentPresetName() + (manager.isDirty() ? "*" : "");
    presetNameLabel.setText (text, juce::dontSendNotification);

    const int active = manager.getActiveSlot();
    slotAButton.setToggleState (active == 0, juce::dontSendNotification);
    slotBButton.setToggleState (active == 1, juce::dontSendNotification);

    // Rename is a USER-preset-only affordance: enabled iff the current preset name resolves to
    // an actual file under getUserPresetDir() (factory presets and "Init" have no backing file).
    const bool isUserPreset = currentUserPresetFile().existsAsFile();
    renameButton.setEnabled (isUserPreset);
    renameButton.setTooltip (isUserPreset
        ? "Rename the current user preset"
        : "Factory presets can't be renamed -- select or save a user preset first");
}

void LFlOwAudioProcessorEditor::showPresetMenu()
{
    auto& manager = processorRef.getPresetManager();
    const auto current = manager.getCurrentPresetName();

    // Item ids: 1..N factory, 1000+i user files (the file Array is captured by the callback
    // so indices stay valid even if the folder changes while the menu is open), 2000 Save As,
    // 2001 Open folder.
    juce::PopupMenu menu;
    const int numFactory = lflow::PresetManager::getNumFactoryPresets();
    for (int i = 0; i < numFactory; ++i)
    {
        const auto name = lflow::PresetManager::getFactoryPresetName (i);
        menu.addItem (1 + i, name, true, name == current);
    }

    const auto userFiles = lflow::PresetManager::getUserPresetFiles();
    if (! userFiles.isEmpty())
    {
        menu.addSeparator();
        for (int i = 0; i < userFiles.size(); ++i)
        {
            const auto name = userFiles[i].getFileNameWithoutExtension();
            menu.addItem (1000 + i, name, true, name == current);
        }
    }

    menu.addSeparator();
    menu.addItem (2000, "Save As...");
    menu.addItem (2001, "Open presets folder");

    // Async, per JUCE 8 non-modal house rules. SafePointer: the host can destroy the editor
    // while the menu is open (window closed); the callback then simply does nothing.
    juce::Component::SafePointer<LFlOwAudioProcessorEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetMenuButton),
        [safeThis, userFiles] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            auto& mgr = safeThis->processorRef.getPresetManager();
            if (result >= 1 && result < 1000)
                mgr.loadFactory (result - 1);
            else if (result >= 1000 && result < 2000)
                mgr.loadUserPresetFile (userFiles[result - 1000]);
            else if (result == 2000)
            {
                safeThis->startSaveAsDialog();
                return; // bar refreshes when the dialog completes
            }
            else if (result == 2001)
                lflow::PresetManager::getUserPresetDir().revealToUser();

            safeThis->refreshPresetBar();
        });
}

void LFlOwAudioProcessorEditor::startSaveAsDialog()
{
    // Non-modal (JUCE 8): enterModalState + ModalCallbackFunction, never runModalLoop. The
    // window is owned by the `saveDialog` member and destroyed in the completion callback
    // (moved out first so a re-entrant Save As can't double-free).
    saveDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Preset name:",
                                                       juce::MessageBoxIconType::NoIcon, this);
    saveDialog->setLookAndFeel (&lookAndFeel);

    const auto current = processorRef.getPresetManager().getCurrentPresetName();
    saveDialog->addTextEditor ("name", current == "Init" ? juce::String ("My Preset") : current);
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LFlOwAudioProcessorEditor> safeThis (this);
    saveDialog->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis] (int result)
        {
            if (safeThis == nullptr)
                return;

            auto dialog = std::move (safeThis->saveDialog);
            if (dialog == nullptr)
                return;
            dialog->setLookAndFeel (nullptr);
            dialog->setVisible (false);

            if (result == 1)
            {
                const auto name = dialog->getTextEditorContents ("name").trim();
                if (name.isNotEmpty())
                    safeThis->processorRef.getPresetManager().saveUserPreset (name);
            }
            safeThis->refreshPresetBar();
        }),
        false); // deleteWhenDismissed=false: the member unique_ptr owns the window
}

juce::File LFlOwAudioProcessorEditor::currentUserPresetFile() const
{
    const auto current = processorRef.getPresetManager().getCurrentPresetName();
    for (auto& f : lflow::PresetManager::getUserPresetFiles())
        if (f.getFileNameWithoutExtension() == current)
            return f;
    return {}; // invalid/default File -- not a user preset (factory preset, or "Init")
}

void LFlOwAudioProcessorEditor::startRenameDialog()
{
    const auto file = currentUserPresetFile();
    if (! file.existsAsFile())
        return; // renameButton should already be disabled in this case; belt-and-braces.

    // Non-modal (JUCE 8), same pattern/lifetime as startSaveAsDialog() above.
    renameDialog = std::make_unique<juce::AlertWindow> ("Rename preset", "New name:",
                                                         juce::MessageBoxIconType::NoIcon, this);
    renameDialog->setLookAndFeel (&lookAndFeel);
    renameDialog->addTextEditor ("name", file.getFileNameWithoutExtension());
    renameDialog->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    renameDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LFlOwAudioProcessorEditor> safeThis (this);
    renameDialog->enterModalState (true,
        juce::ModalCallbackFunction::create ([safeThis, file] (int result)
        {
            if (safeThis == nullptr)
                return;

            auto dialog = std::move (safeThis->renameDialog);
            if (dialog == nullptr)
                return;
            dialog->setLookAndFeel (nullptr);
            dialog->setVisible (false);

            if (result == 1)
            {
                const auto newName = dialog->getTextEditorContents ("name");
                const auto outcome = safeThis->processorRef.getPresetManager()
                                          .renameUserPreset (file, newName);
                if (outcome != lflow::PresetManager::RenameOutcome::Success)
                {
                    juce::String message;
                    switch (outcome)
                    {
                        case lflow::PresetManager::RenameOutcome::EmptyName:
                            message = "Preset name can't be empty.";
                            break;
                        case lflow::PresetManager::RenameOutcome::InvalidName:
                            message = "That name isn't valid for a file (no / or \\ characters).";
                            break;
                        case lflow::PresetManager::RenameOutcome::NameClash:
                            message = "Another user preset already has that name.";
                            break;
                        case lflow::PresetManager::RenameOutcome::FileError:
                            message = "The preset file couldn't be renamed on disk.";
                            break;
                        case lflow::PresetManager::RenameOutcome::Success:
                            break; // unreachable (guarded above)
                    }
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                             "Rename failed", message);
                }
            }
            safeThis->refreshPresetBar();
        }),
        false); // deleteWhenDismissed=false: the member unique_ptr owns the window
}

void LFlOwAudioProcessorEditor::styleRotary (juce::Slider& s, int textBoxWidth, int textBoxHeight)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, textBoxHeight);

    // House-LookAndFeel migration: the old finding #10 fix (forcing Slider::textBoxTextColourId
    // here) is gone. The house LookAndFeel's drawLabel gives every Slider's text box the LCD
    // phosphor-glass treatment (drawScreen + drawLcdText, always in colour::lcdText) for
    // Slider-parented Labels specifically, ignoring textBoxTextColourId entirely -- so setting
    // it here would now be dead code, not a fix. Every rotary's readout is styled by the
    // LookAndFeel alone, with no per-instance colour call needed (or possible) any more.
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
                        "or exit its breakpoint editor in the display above; right-click for "
                        "reset options");
    s.chip.setTitle (laneName);
    s.chip.setDescription (s.chip.getTooltip());
    s.chip.onClick = [this, i] { onLaneChipClicked (i); };
    s.chip.onRightClick = [this, i] { showLaneChipMenu (i); };
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
    // pill's on/off state is driven by setEditLane, not by its own click (same as the chip). Its
    // on-state fill/text now come from the house drawToggleButton (accent fill, accentInk text)
    // via getToggleState(); tickColourId is unused by that override, so it is no longer set here.
    s.editButton.setClickingTogglesState (false);
    s.editButton.setTooltip (laneName + ": draw this lane's shape");
    s.editButton.setTitle (laneName + " edit");
    s.editButton.setDescription (s.editButton.getTooltip());
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
    // Lane identity: the Rate slider is the one remaining slider styled with a per-lane fill
    // colour (drawLinearSlider, kept in LFlOwLookAndFeel since the house has no linear-slider
    // override) -- the rotary knobs below no longer take a fill colour at all, see next comment.
    s.rateSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (colour));
    addAndMakeVisible (s.rateSlider);

    styleRotary (s.phaseSlider, 40, 14);
    styleRotary (s.depthSlider, 44, 14);
    // No rotarySliderFillColourId here any more: the house's filmstrip knobs draw their own
    // pointer and consult no per-slider colour (drawRotarySlider/drawVectorKnob), so the old
    // per-lane fill that used to feed the hand-drawn arc is now dead weight. Lane identity for
    // these two stays on the chip/label/Rate slider instead (style guide + migration spec).
    addAndMakeVisible (s.phaseSlider);
    addAndMakeVisible (s.depthSlider);

    // Phase 7 Task 5 (UX #4): double-click-to-default on this lane's rotaries + the Rate
    // slider, each pulled from that lane's OWN APVTS parameter default (paramDefault helper) --
    // depth's default differs per lane (Lane 1 50%, Lanes 2-3 0%, see ParameterLayout.cpp), so
    // hardcoding a single literal here would be wrong for 2 of the 3 lanes.
    s.phaseSlider.setDoubleClickReturnValue (true, paramDefault (apvts, ids.phase));
    s.depthSlider.setDoubleClickReturnValue (true, paramDefault (apvts, ids.depth));
    s.rateSlider.setDoubleClickReturnValue (true, paramDefault (apvts, ids.rateHz));

    s.waveformBox.setTooltip (laneName + ": shape of the LFO motion");
    s.syncButton.setTooltip  (laneName + ": lock this lane's speed to host tempo");
    s.rateSlider.setTooltip  (laneName + ": LFO speed in Hz when Sync is off");
    // Phase 7 Task 6 (UX #8): sync BPM-fallback hint appended to the existing copy (ASCII).
    s.divisionBox.setTooltip (laneName + ": note value per LFO cycle when Sync is on. "
                              "Synced to host tempo; 120 BPM fallback when no host transport "
                              "(e.g. standalone).");
    s.rhythmBox.setTooltip   (laneName + ": straight, dotted, or triplet feel for the synced rate");
    s.phaseSlider.setTooltip (laneName + ": phase offset in degrees, relative to the other lanes");
    s.destBox.setTooltip     (laneName + ": what this lane modulates, Volume, Pan, a frequency band, or Pitch");
    // Depth's tooltip is destination-aware (UX #5) -- seeded below via refreshDepthTooltip(),
    // once destAtt exists, so it reads the lane's real initial Dest rather than a hardcoded guess.

    // Accessibility floor (style guide section 8): visible label text as the accessible title,
    // the tooltip text as the description, for every interactive control.
    s.waveformBox.setTitle (laneName + " Waveform"); s.waveformBox.setDescription (s.waveformBox.getTooltip());
    s.syncButton.setTitle  (laneName + " Sync");     s.syncButton.setDescription  (s.syncButton.getTooltip());
    s.rateSlider.setTitle  (laneName + " Rate");     s.rateSlider.setDescription  (s.rateSlider.getTooltip());
    s.divisionBox.setTitle (laneName + " Division"); s.divisionBox.setDescription (s.divisionBox.getTooltip());
    s.rhythmBox.setTitle   (laneName + " Rhythm");   s.rhythmBox.setDescription   (s.rhythmBox.getTooltip());
    s.phaseSlider.setTitle (laneName + " Phase");    s.phaseSlider.setDescription (s.phaseSlider.getTooltip());
    s.destBox.setTitle     (laneName + " Destination"); s.destBox.setDescription  (s.destBox.getTooltip());
    s.depthSlider.setTitle (laneName + " Depth"); // description seeded by refreshDepthTooltip() below

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

    // Phase 7 Task 6 (UX #5): seeds this lane's Depth tooltip from its real initial Dest (now
    // that destAtt has synced destBox) rather than a hardcoded guess; timerCallback() keeps it
    // current thereafter.
    refreshDepthTooltip (i);
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

        // Phase 7 Task 6 (UX #6 + opp #5): the "L1" badge is painted in THIS editor's own
        // paint() (next to the chip's own bounds), not inside LaneChip itself, so a follow-state
        // change needs its own compare-guarded repaint() trigger -- setFollowing() above only
        // repaints the chip component, not the editor.
        if (laneFollowing[(size_t) i] != isFollower)
        {
            laneFollowing[(size_t) i] = isFollower;
            repaint();
        }

        // Edit pill (finding #3): visible only when this lane's EFFECTIVE waveform is Custom
        // and it isn't a linked follower (a follower's own Custom shape, if any, isn't what's
        // playing -- entering its editor would silently edit a hidden shape, same gate as
        // onLaneChipClicked). Mutually exclusive with nameLabel -- they share one column.
        const bool showEdit = ! isFollower && effectiveWaveform (i) == lflow::Waveform::Custom;
        s.editButton.setVisible (showEdit);
        s.nameLabel.setVisible (! showEdit);
        // Phase 7 Task 6 (UX #6): make room for the "L1" badge (painted in this editor's own
        // paint(), just right of the chip) by trimming the name label's left edge only while
        // following -- see laneNameBounds' doc comment (header).
        s.nameLabel.setBounds (isFollower ? laneNameBounds[(size_t) i].withTrimmedLeft (14)
                                           : laneNameBounds[(size_t) i]);

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

        // Phase 7 Task 6 (UX #5): dest-aware Depth tooltip, refreshed only when this lane's
        // Dest actually changed (compare-guard lives inside refreshDepthTooltip itself).
        refreshDepthTooltip (i);

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

    // Phase 7 Task 3 (UX #1): cheap -- setEnabled() itself early-outs when unchanged.
    auto& um = processorRef.getUndoManager();
    undoButton.setEnabled (um.canUndo());
    redoButton.setEnabled (um.canRedo());

    // Phase 7 Task 4 (UX #2): throttled preset-bar refresh -- the dirty check deep-compares
    // the state tree (see PresetManager::isDirty()), so it runs at ~6 Hz, not 60. Catches
    // edits from any source: knobs, automation, undo/redo, shape drawing.
    if (++presetBarPollCounter >= kPresetBarPollTicks)
    {
        presetBarPollCounter = 0;
        refreshPresetBar();
    }

    refreshEnablement();
    refreshXoverHint();
    refreshBandEmphasis();

    // Phase 7 Task 6 (UX #9): display dimming + "BYPASSED" tag; compare-guarded inside
    // LfoDisplay::setBypassed itself, so this cheap read+call is safe every tick.
    display.setBypassed (raw (lflow::pid::bypass) > 0.5f);
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

void LFlOwAudioProcessorEditor::showLaneChipMenu (int lane)
{
    if (lane < 0 || lane >= 3)
        return;

    // "Reset curve to triangle" only makes sense (and is only shown) when this lane's
    // EFFECTIVE waveform is Custom -- same rule the chip's own edit-mode gate uses. Note
    // this deliberately does NOT check Link/follower status the way onLaneChipClicked does:
    // resetLaneToDefaults() always acts on lane `lane`'s OWN 8 parameters (never a follower's
    // effective ones), so it's meaningful even while linked.
    const bool showCurveReset = effectiveWaveform (lane) == lflow::Waveform::Custom;

    juce::PopupMenu menu;
    menu.addItem (1, "Reset lane to defaults");
    if (showCurveReset)
        menu.addItem (2, "Reset curve to triangle");

    juce::Component::SafePointer<LFlOwAudioProcessorEditor> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options(), [safeThis, lane] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        if (result == 1)
            safeThis->resetLaneToDefaults (lane);
        else if (result == 2)
            safeThis->resetLaneCurve (lane);
    });
}

void LFlOwAudioProcessorEditor::resetLaneToDefaults (int lane)
{
    if (lane < 0 || lane >= 3)
        return;

    auto& apvts = processorRef.getAPVTS();
    auto& um = processorRef.getUndoManager();
    const auto& ids = laneIds[lane];

    // One named transaction for all 8 params (Cmd-Z undoes the whole reset in one step) --
    // same beginChangeGesture/setValueNotifyingHost/endChangeGesture pattern PresetManager
    // uses for its own programmatic parameter jumps (see PresetManager::applyStateTree).
    um.beginNewTransaction ("Reset lane " + juce::String (lane + 1) + " to defaults");

    for (const char* paramId : { ids.waveform, ids.sync, ids.rateHz, ids.division, ids.rhythm,
                                  ids.phase, ids.depth, ids.dest })
    {
        auto* p = apvts.getParameter (paramId);
        if (p == nullptr)
            continue;

        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }

    // Forces the APVTS param->tree flush now (JUCE otherwise defers it), so the resulting
    // undoable ValueTree property writes land inside THIS transaction rather than a later,
    // separate one (same reasoning as PresetManager::applyStateTree's own capture call).
    (void) apvts.copyState();
}

void LFlOwAudioProcessorEditor::resetLaneCurve (int lane)
{
    if (lane < 0 || lane >= 3)
        return;

    // Empty node list is ShapeManager::setNodes()'s own documented "fewer than 2 survive"
    // fallback -- it writes the default rise-fall triangle (0,0)(0.5,1)(1,0) instead of
    // rejecting the call, so this is the same one-line reset PresetManager relies on.
    processorRef.getUndoManager().beginNewTransaction ("Reset lane " + juce::String (lane + 1) + " curve");
    processorRef.getShapeManager().setNodes (lane, {}, &processorRef.getUndoManager());
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

    // Normal state matches every other global label (onSurfaceVariant); the CLAMPED state is
    // the one that stands out -- Colors::warn (the house's meterHot amber), never Colors::lane2
    // (that now means "lane 3's channel colour", a different signal entirely -- see
    // LFlOwLookAndFeel.h's Colors::warn doc comment) and never Colors::primary/accent (accent
    // means "active", not "caution"). Only the caption label's colour carries this any more:
    // the slider's own LCD readout digits are always drawn in the fixed house lcdText green by
    // the house LookAndFeel (drawLabel's Slider-parented branch does not consult
    // Slider::textBoxTextColourId at all), so that old per-instance colour set is gone as
    // dead code -- see docs/ui_migration_report.md for this trade-off.
    using C = LFlOwLookAndFeel::Colors;
    const auto colour = juce::Colour (clamped ? C::warn : C::onSurfaceVariant);
    xoverHighLabel.setColour (juce::Label::textColourId, colour);

    juce::String tip = "Crossover between the Mid and High bands";
    if (clamped)
        tip << " (clamped by Low crossover - effective " << effectiveHz << " Hz)";
    xoverHighSlider.setTooltip (tip);
    xoverHighSlider.setDescription (tip);
}

void LFlOwAudioProcessorEditor::refreshBandEmphasis()
{
    auto& apvts = processorRef.getAPVTS();
    auto raw = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    // Dest indices per destChoices in ParameterLayout.cpp: 0 Volume, 1 Pan, 2 Low, 3 Mid,
    // 4 High, 5 Pitch -- 2/3/4 are the band destinations the Xover knobs actually affect.
    bool anyBandLive = false;
    for (int i = 0; i < 3 && ! anyBandLive; ++i)
    {
        const auto& ids = laneIds[i];
        const int destIndex = (int) raw (ids.dest);
        const bool isBandDest = destIndex >= 2 && destIndex <= 4;
        anyBandLive = isBandDest && raw (ids.depth) > 0.0f;
    }

    const int state = anyBandLive ? 1 : 0;
    if (state == bandsLiveState)
        return;
    bandsLiveState = state;

    // Present-but-quiet at 50% when idle, full when a band lane is live -- component alpha only
    // (NOT setEnabled), so the knobs stay fully draggable either way (spec requirement).
    const float alpha = anyBandLive ? 1.0f : 0.5f;
    xoverLowSlider.setAlpha (alpha);
    xoverHighSlider.setAlpha (alpha);
    xoverLowLabel.setAlpha (alpha);
    xoverHighLabel.setAlpha (alpha);

    // The painted "BANDS" micro-label isn't a Component (it's drawn directly in paint()), so it
    // can't use setAlpha -- this flag is the paint-time equivalent, applied there.
    bandsCaptionQuiet = ! anyBandLive;
    repaint();
}

void LFlOwAudioProcessorEditor::refreshDepthTooltip (int lane)
{
    if (lane < 0 || lane >= 3)
        return;

    auto& apvts = processorRef.getAPVTS();
    const auto& ids = laneIds[lane];
    const int destIndex = (int) apvts.getRawParameterValue (ids.dest)->load();

    if (destIndex == lastDepthTooltipDest[(size_t) lane])
        return;
    lastDepthTooltipDest[(size_t) lane] = destIndex;

    const juce::String laneName = "Lane " + juce::String (lane + 1);
    juce::String tip;
    // Dest indices per destChoices in ParameterLayout.cpp: 0 Volume, 1 Pan, 2 Low, 3 Mid,
    // 4 High, 5 Pitch.
    switch (destIndex)
    {
        case 0:  tip = laneName + ": Tremolo depth"; break;
        case 1:  tip = laneName + ": Auto-pan width"; break;
        case 2:  tip = laneName + ": Band pulse depth (Low band)"; break;
        case 3:  tip = laneName + ": Band pulse depth (Mid band)"; break;
        case 4:  tip = laneName + ": Band pulse depth (High band)"; break;
        default: tip = laneName + ": Vibrato depth (wobble amount scales with rate)"; break; // Pitch
    }

    auto& depthSlider = laneStrips[(size_t) lane].depthSlider;
    depthSlider.setTooltip (tip);
    depthSlider.setDescription (tip); // accessibility floor: description tracks the tooltip
}

void LFlOwAudioProcessorEditor::paint (juce::Graphics& g)
{
    using C = LFlOwLookAndFeel::Colors;

    // Window background = the house chassis gradient (style guide section 2 / migration spec),
    // in place of the old flat Colors::background fill.
    g.setGradientFill (zqsfx::ui::gradients::chassis (getLocalBounds().toFloat()));
    g.fillAll();

    constexpr int margin = 12;

    // Header accent line (2px, primary at 0.4 alpha).
    g.setColour (juce::Colour (C::primary).withAlpha (0.4f));
    g.fillRect (margin, 4, getWidth() - margin * 2, 2);

    // Consolidated header line (finding #4/#8): "LFlOw" 16pt bold + "LFO + FLOW" 10pt in a
    // compact left lockup, Undo/Redo/Bypass/the ZQ SFX mark right-aligned in the SAME line
    // (bounds set in resized()). The wordmark keeps its own bespoke bold treatment (style guide
    // section 1: "logo and wordmark treatment" stays with the product); the subtitle next to it
    // is a plain label and goes through the house silk font like every other caption.
    auto headerLine = getLocalBounds().reduced (margin).removeFromTop (32);

    juce::Font titleFont (juce::FontOptions (16.0f).withStyle ("Bold"));
    g.setFont (titleFont);
    g.setColour (juce::Colour (C::onSurface));
    const int titleW = (int) std::ceil (juce::TextLayout::getStringWidth (titleFont, "LFlOw")) + 6;
    g.drawText ("LFlOw", headerLine.withWidth (titleW), juce::Justification::centredLeft, false);

    g.setFont (lookAndFeel.silkFont (10.0f));
    g.setColour (juce::Colour (C::onSurfaceVariant));
    g.drawText ("LFO + FLOW", headerLine.withTrimmedLeft (titleW + 8),
                juce::Justification::centredLeft, false);

    // Painted column-header row (finding #2): "WAVE SYNC RATE PHASE DEST DEPTH", x-aligned to
    // the SAME laneGrid columns layoutLaneStrip() uses (computed once in resized()).
    g.setColour (juce::Colour (C::onSurfaceVariant));
    g.setFont (lookAndFeel.silkFont (10.0f, true));
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
    g.setFont (lookAndFeel.silkFont (9.0f, true));
    // Phase 7 Task 6 (UX #7 + opp #5): quiet (50%) when no lane has a band destination with
    // depth > 0, matching the Xover Lo/Hi knobs' own setAlpha (refreshBandEmphasis) -- this
    // caption is paint()-drawn text, not a Component, so it needs its own alpha here rather
    // than setAlpha.
    g.setColour (juce::Colour (C::outline).withAlpha (bandsCaptionQuiet ? 0.5f : 1.0f));
    g.drawText ("BANDS", globalGrid.bandsLabel, juce::Justification::centred, false);

    // Phase 7 Task 6 (UX #6 + opp #5): "L1" badge, lane-1's channel colour, immediately right of
    // a following lane's chip -- "ganged to Lane 1" at a glance instead of a flat grey-out (the
    // tooltip on the chip itself still spells out exactly what stays per-lane). Uses the
    // chip's own actual bounds (already laid out in layoutLaneStrip/computeLaneColumns) rather
    // than recomputing geometry here.
    g.setFont (lookAndFeel.silkFont (8.0f, true));
    for (int i = 0; i < 3; ++i)
    {
        if (! laneFollowing[(size_t) i])
            continue;

        // Sized to fit the 14px gap refreshEnablement() trims from the name label's left edge
        // for a following lane, so "L1" and "Lane N" never overlap.
        auto chipBounds = laneStrips[(size_t) i].chip.getBounds();
        juce::Rectangle<int> badgeArea (chipBounds.getRight() + 1, chipBounds.getY(),
                                         14, chipBounds.getHeight());
        g.setColour (juce::Colour (LFlOwLookAndFeel::Colors::lane0)); // lane 1's channel colour
        g.drawText ("L1", badgeArea, juce::Justification::centred, false);
    }

    // Version footer (bottom-right).
    g.setColour (juce::Colour (C::outline));
    g.setFont (lookAndFeel.silkFont (9.0f));
    g.drawText ("v0.7.0", getLocalBounds().removeFromBottom (18).removeFromRight (70),
                juce::Justification::centredRight, false);
}

void LFlOwAudioProcessorEditor::resized()
{
    constexpr int margin = 12;
    auto area = getLocalBounds().reduced (margin);

    // Header line (~32px): title lockup (painted) + Bypass (component), right-aligned, both
    // in this one line — see paint().
    auto headerLine = area.removeFromTop (32);

    // ZQ SFX mark (style guide section 5): header row, far right of everything else, at least
    // 24 px tall. Reserved FIRST so it is always the rightmost element; the brand lockup's
    // painted text (paint()) only ever measures its own string width, so this never needs to
    // "shrink the title area" at the 620 px minimum width -- there is still ~380px of slack
    // left for "LFlOw" + "LFO + FLOW" once the mark, Undo/Redo, and Bypass are all carved out
    // (see the Undo/Redo comment below for the rest of the arithmetic).
    logo.setBounds (headerLine.removeFromRight (34).withSizeKeepingCentre (28, 28));
    headerLine.removeFromRight (6);
    bypassButton.setBounds (headerLine.removeFromRight (80).withSizeKeepingCentre (80, 24));

    // Phase 7 Task 3 (UX #1): Undo/Redo pills, right of the brand lockup / left of Bypass. This
    // fixed-width group (2 x 44px + 6+4px gaps = 98px, plus Bypass's 80px + the logo's 34+6px =
    // 218px total) leaves well over 200px of the brand lockup's left-aligned, painted-not-laid-
    // out text free even at the 620px resize floor (620 - 24 margin - 218 = 378px), so it never
    // truncates.
    headerLine.removeFromRight (6);
    redoButton.setBounds (headerLine.removeFromRight (44).withSizeKeepingCentre (44, 22));
    headerLine.removeFromRight (4);
    undoButton.setBounds (headerLine.removeFromRight (44).withSizeKeepingCentre (44, 22));

    area.removeFromTop (4);

    // Phase 7 Task 4 (UX #2): preset bar row -- [<] [name] [>] [v] ... [A] [B] [Copy] [Rename].
    // All widths fixed except the name label, which absorbs the slack: at the 620px floor that
    // is 620 - 24 (margins) - 88 (prev/next/menu + gaps) - 12 (group gap) - 172 (A/B/Copy/Rename)
    // = 324px of name space -- no truncation risk for any factory or sane user preset name.
    auto presetBar = area.removeFromTop (24);
    auto abGroup = presetBar.removeFromRight (24 + 4 + 24 + 4 + 48 + 8 + 60);
    slotAButton.setBounds (abGroup.removeFromLeft (24));
    abGroup.removeFromLeft (4);
    slotBButton.setBounds (abGroup.removeFromLeft (24));
    abGroup.removeFromLeft (4);
    copySlotButton.setBounds (abGroup.removeFromLeft (48));
    abGroup.removeFromLeft (8);
    renameButton.setBounds (abGroup); // remaining 60px -- >=22px hit target on both axes
    presetBar.removeFromRight (12);

    presetPrevButton.setBounds (presetBar.removeFromLeft (24));
    presetBar.removeFromLeft (4);
    presetMenuButton.setBounds (presetBar.removeFromRight (24));
    presetBar.removeFromRight (4);
    presetNextButton.setBounds (presetBar.removeFromRight (24));
    presetBar.removeFromRight (4);
    presetNameLabel.setBounds (presetBar);

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

    // layoutLaneStrip() above just reset every lane's name label to its untrimmed bounds, which
    // puts a following lane's name back under its "L1" badge until the next timer tick re-trims
    // it (a one-frame overlap on every resize, and permanent in a headless render).
    refreshEnablement();

    // Editor size persistence: record the current size on the processor on every resize (drag-
    // resize, host-driven resize, or the constructor's own initial setSize() above) so it rides
    // the next getStateInformation() call. Cheap atomic stores; see the processor's
    // setEditorSize() doc comment for why this must not touch apvts.state.
    processorRef.setEditorSize (getWidth(), getHeight());
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
    laneNameBounds[(size_t) i] = nameBounds; // Phase 7 Task 6 (UX #6): see header doc comment.
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
