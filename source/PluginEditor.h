#pragma once
#include <JuceHeader.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "PluginProcessor.h"
#include "gui/LFlOwLookAndFeel.h"
#include "gui/LfoDisplay.h"

// Full Phase 2 editor: header (unchanged from Task 2) + 3-lane overlay display + one strip
// of controls per lane + a global row (Link/Mix/Smooth). Mirrors the Phase 1 patterns:
// attachments as unique_ptr members, LookAndFeel declared before components, tooltip on
// every interactive control, setLookAndFeel(nullptr) in the dtor, 60fps timer that also
// refreshes greying state.
class LFlOwAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit LFlOwAudioProcessorEditor (LFlOwAudioProcessor&);
    ~LFlOwAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // Phase 7 Task 3 (UX #1): Cmd-Z / Cmd-Shift-Z, dispatched here regardless of which child
    // control currently has keyboard focus -- JUCE bubbles an unhandled key press up the parent
    // chain from the focused component (see juce::ComponentPeer::handleKeyPress), so this fires
    // even while e.g. a slider or combo box owns focus, as long as that control doesn't itself
    // consume the 'z' key (none of ours do).
    bool keyPressed (const juce::KeyPress&) override;

    // Ensures SOME component within the editor holds keyboard focus after any click on the
    // editor's own background (gaps between controls) -- clicks that land on a child control
    // already grab focus automatically via JUCE's own Component::internalMouseDown (see
    // grabKeyboardFocusInternal), this just covers the rest. setWantsKeyboardFocus(true) (ctor)
    // makes the editor itself a valid focus target so keyPressed above always has somewhere to
    // dispatch from, even before the user has clicked any specific control.
    void mouseDown (const juce::MouseEvent&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    // Chip painted in a lane's accent color with its lane number. Also the lane's edit-mode
    // toggle: when that lane's waveform is Custom, clicking it enters/exits breakpoint editing
    // for this lane in the display above (see onClick/setEditActive); for non-Custom lanes the
    // click is a no-op (handled by the editor, not here).
    class LaneChip : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        void setup (juce::uint32 colourArgb, int laneNumber);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        // Highlights the chip while this lane is being edited.
        void setEditActive (bool active);

        // Finding #5 (state legibility): while this lane follows Link, its chip fills grey
        // instead of lane colour so the follower state reads at a glance, on top of the usual
        // disabled-alpha dimming (setEnabled) already applied in refreshEnablement().
        void setFollowing (bool following);

        std::function<void()> onClick;

        // Phase 7 Task 5 (UX #4): right-click -> "Reset lane to defaults" / "Reset curve to
        // triangle" menu, fired regardless of isEnabled() (a linked follower can still be
        // reset; only its left-click edit-mode toggle is gated by isEnabled(), same as
        // before). The editor owns menu content/undo -- this is purely input routing.
        std::function<void()> onRightClick;

    private:
        // Overwritten by setup() before this chip is ever painted; a real house token (rather
        // than a raw 0xff literal) so the migration's "no colour literal outside
        // LFlOwLookAndFeel.h" grep gate stays honest even about placeholder values.
        juce::uint32 colour { LFlOwLookAndFeel::Colors::onSurface };
        int number { 1 };
        bool editActive { false };
        bool isFollowing { false };
    };

    // One lane's worth of controls + attachments. Attachments always stay connected to their
    // own lane's parameters — link only greys the display, automation still reaches every
    // param.
    struct LaneStrip
    {
        LaneChip chip;
        juce::Label nameLabel;

        // Edit affordance (finding #3): a pill that appears in the SAME slot as nameLabel
        // (they're mutually exclusive -- see refreshEnablement) only when this lane's effective
        // waveform is Custom and it isn't a linked follower. Built as a ToggleButton (not a
        // plain TextButton) so it reuses the pill LookAndFeel::drawToggleButton already
        // established for Sync/Link (tickColourId fill when "on"/editing) rather than adding a
        // second, one-off pill-drawing path for a literal TextButton. Driven manually
        // (setClickingTogglesState(false)) through the SAME code path as the chip
        // (onLaneChipClicked/setEditLane) -- see buildLaneStrip.
        juce::ToggleButton editButton { "Edit" };

        juce::ComboBox waveformBox;
        juce::ToggleButton syncButton { "Sync" };
        juce::Slider rateSlider;
        juce::ComboBox divisionBox;
        juce::ComboBox rhythmBox;
        juce::Slider phaseSlider;
        juce::ComboBox destBox;
        juce::Slider depthSlider;

        std::unique_ptr<APVTS::ComboBoxAttachment> waveformAtt, divisionAtt, rhythmAtt, destAtt;
        std::unique_ptr<APVTS::ButtonAttachment>   syncAtt;
        std::unique_ptr<APVTS::SliderAttachment>   rateAtt, phaseAtt, depthAtt;

        const char* syncId { nullptr };
    };

    // A single column x-position/width per lane-grid column, shared by the painted column-
    // header row (paint()) and every lane strip's control layout (layoutLaneStrip()) so the
    // two can never drift out of alignment ("no duplicated magic x's" per Phase 6 Task 2).
    // Only x/width are meaningful here — each strip supplies its own row's y/height.
    struct ColumnSlot { int x = 0, w = 0; };
    struct LaneGridLayout
    {
        juce::Rectangle<int> headerRow; // the painted "WAVE SYNC RATE..." row's bounds
        ColumnSlot chip, name, wave, sync, rate, phase, dest, depth;
    };
    LaneGridLayout laneGrid;

    // Phase 7 Task 6 (UX #6): each lane's FULL nameLabel rectangle as laid out by
    // layoutLaneStrip() (same slot editButton shares). refreshEnablement() trims this from the
    // left for a following lane (isFollower) so the "L1" badge painted just right of the chip
    // (see paint()) has room without overlapping "Lane N" text; a non-follower gets the full
    // rect back. Stored here (not recomputed) so refreshEnablement doesn't need resized()'s
    // column math.
    juce::Rectangle<int> laneNameBounds[3];

    // Divider + "BANDS" micro-label painted over the global row's crossover group (finding
    // #8's grouping), computed in resized() and painted in paint().
    struct GlobalRowLayout
    {
        juce::Rectangle<int> dividerLine;
        juce::Rectangle<int> bandsLabel;
    };
    GlobalRowLayout globalGrid;

    LFlOwAudioProcessor& processorRef;
    LFlOwLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltipWindow { this, 500 };

    // The ZQ SFX mark (style guide section 5): header row, far right, doubles as the About-box
    // trigger. logo.onClick wires to showAboutBox() in the ctor.
    zqsfx::ui::LogoMark logo { "LFlOw" };
    void showAboutBox();

    LfoDisplay display;

    LaneStrip laneStrips[3];

    juce::ToggleButton linkButton { "Link" };
    juce::Slider mixSlider, smoothSlider, xoverLowSlider, xoverHighSlider;
    juce::Label mixLabel { {}, "Mix" }, smoothLabel { {}, "Smooth" };
    juce::Label xoverLowLabel { {}, "Xover Lo" }, xoverHighLabel { {}, "Xover Hi" };
    std::unique_ptr<APVTS::ButtonAttachment> linkAtt;
    std::unique_ptr<APVTS::SliderAttachment> mixAtt, smoothAtt, xoverLowAtt, xoverHighAtt;

    juce::TextButton bypassButton { "Bypass" };
    std::unique_ptr<APVTS::ButtonAttachment> bypassAtt;

    // Phase 7 Task 3 (UX #1): header pills, right of the brand lockup / left of Bypass. Plain
    // (non-toggle) TextButtons -- momentary actions, matching Bypass's own unstyled-by-LnF look
    // (LFlOwLookAndFeel doesn't override TextButton drawing; Bypass only tints its ON state via
    // buttonOnColourId, which doesn't apply here since these aren't toggles). Enabled state
    // tracks canUndo()/canRedo(), refreshed each timerCallback tick.
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };

    // Phase 7 Task 4 (UX #2): slim preset bar directly under the header line --
    // [<] [name] [>] [v] ... [A] [B] [Copy]. ASCII only. All state (current name, dirty
    // baseline, A/B slots) lives in the processor's PresetManager; this row is a dumb view
    // over it, refreshed at a throttled rate from timerCallback() (refreshPresetBar). The
    // A/B pills are TextButtons whose toggle state is driven manually from the manager's
    // active slot (setClickingTogglesState(false), like the lane Edit pills) with
    // buttonOnColourId = primary, matching Bypass's ON tint.
    juce::TextButton presetPrevButton { "<" }, presetNextButton { ">" }, presetMenuButton { "v" };
    juce::Label presetNameLabel;
    juce::TextButton slotAButton { "A" }, slotBButton { "B" }, copySlotButton { "Copy" };

    // Rename control for the currently loaded USER preset only -- disabled (with an explanatory
    // tooltip) when the current preset is a factory preset or "Init", since those have no
    // backing file to rename. Enablement/tooltip refreshed alongside the rest of the preset bar
    // in refreshPresetBar(); see currentUserPresetFile() and PresetManager::renameUserPreset().
    juce::TextButton renameButton { "Rename" };

    // Save As dialog, JUCE 8 non-modal pattern (enterModalState + ModalCallbackFunction, no
    // runModalLoop) -- kept as a member so it outlives the ctor scope; reused per invocation.
    std::unique_ptr<juce::AlertWindow> saveDialog;

    // Rename dialog, same non-modal pattern/lifetime as saveDialog above.
    std::unique_ptr<juce::AlertWindow> renameDialog;

    // Throttle counter for refreshPresetBar(): the dirty check deep-compares the state tree
    // (see PresetManager::isDirty()), which is cheap but not 60-Hz-free-cheap, so the bar
    // refreshes every kPresetBarPollTicks timer ticks (~6 Hz) instead of every tick.
    static constexpr int kPresetBarPollTicks = 10;
    int presetBarPollCounter { 0 };

    // Which lane (0-2), if any, is currently being edited in the display; -1 = none. Owned
    // here (not in LfoDisplay) since it also drives chip highlighting and the ShapeManager
    // wiring; LfoDisplay just renders/hit-tests whatever setEditLane/setEditNodes give it.
    int editLane { -1 };

    // Last waveform index seen per lane's OWN combo (index-matched to LaneStrip), tracked so
    // onWaveformSelected() can tell "just changed to Custom" from "already was Custom" --
    // ComboBoxAttachment's parameter->UI sync path fires onChange too (JUCE calls it via
    // sendNotificationSync), so onChange alone can't distinguish user selection from a state/
    // preset reload. See onWaveformSelected()'s doc comment for the accepted trade-off.
    lflow::Waveform lastWaveform[3] { lflow::Waveform::Sine, lflow::Waveform::Sine, lflow::Waveform::Sine };

    // Cheap-compare cache so refreshXoverHint() only touches colours/tooltips on change. Encodes
    // BOTH the clamped flag and the rounded effective Hz (0 = not clamped, else 1000000+Hz) so a
    // still-clamped drag of Xover Lo (which moves the effective value without flipping the flag)
    // still refreshes the tooltip text, not just the on/off transition.
    int xoverHiClampedState { -1 }; // -1 = unknown (forces first apply)

    // Phase 7 Task 6 (UX #7 + opp #5): band-knob emphasis. -1 = unknown (forces first apply),
    // else 0/1 = no/some band lane live (dest in {Low,Mid,High} AND depth > 0). Drives
    // Xover Lo/Hi's + their labels' Component::setAlpha (still fully draggable/enabled -- alpha
    // only, no setEnabled) and bandsCaptionQuiet below, refreshed each timer tick via
    // refreshBandEmphasis(), compare-guarded so an unchanged state never touches alpha/repaints.
    int bandsLiveState { -1 };

    // Read by paint() to alpha the painted "BANDS" micro-label (not a Component, so it can't use
    // setAlpha -- this flag is the paint-time equivalent, kept in lockstep with bandsLiveState by
    // refreshBandEmphasis()).
    bool bandsCaptionQuiet { true };

    // Phase 7 Task 6 (UX #6 + opp #5): per-lane follower state ("ganged to Lane 1"), tracked so
    // the "L1" badge painted next to a following lane's chip (in this editor's own paint(), using
    // that lane's chip's actual bounds) only triggers a repaint() on a genuine transition, not
    // every timer tick. Updated in refreshEnablement() (which already computes this per lane).
    bool laneFollowing[3] { false, false, false };

    // Phase 7 Task 6 (UX #5): per-lane Dest index last used to set that lane's Depth-slider
    // tooltip (0=Volume..5=Pitch, see destChoices in ParameterLayout.cpp) -- -1 forces the first
    // apply. Compare-guards refreshDepthTooltip() so an unchanged Dest never touches the tooltip
    // string every tick.
    int lastDepthTooltipDest[3] { -1, -1, -1 };

    void buildLaneStrip (int laneIndex);

    // Phase 7 Task 4 (UX #2) preset bar plumbing. buildPresetBar() = ctor setup;
    // refreshPresetBar() syncs name label ("<name>" + "*" when edited since load), A/B pill
    // highlight -- called from the ctor, after every preset-bar action, and (throttled) from
    // timerCallback(). showPresetMenu() opens the async popup (factory list, user presets,
    // Save As..., Open presets folder); startSaveAsDialog() runs the non-modal name prompt.
    void buildPresetBar();
    void refreshPresetBar();
    void showPresetMenu();
    void startSaveAsDialog();

    // Rename control plumbing (USER presets only). currentUserPresetFile() resolves the
    // currently-loaded preset name to its file under PresetManager::getUserPresetDir(), or an
    // invalid (default) juce::File when the current preset is factory/"Init" -- shared by
    // refreshPresetBar() (button enablement) and startRenameDialog() (which file to rename).
    // startRenameDialog() opens the non-modal prompt, prefilled with the current name, and
    // reports any PresetManager::RenameOutcome failure via an AlertWindow message box.
    juce::File currentUserPresetFile() const;
    void startRenameDialog();

    // Computes laneGrid's column x-positions/widths from the available row bounds (spec
    // minimums as floors; the rate slot absorbs any extra width as the window widens). Called
    // once per resized() before the column-header row and the 3 strips are laid out.
    void computeLaneColumns (juce::Rectangle<int> rowBounds);

    void layoutLaneStrip (int laneIndex, juce::Rectangle<int> rowArea);
    static void styleRotary (juce::Slider&, int textBoxWidth, int textBoxHeight);

    // Refreshes per-lane enablement + rate/division-slot visibility. Mirrors the Phase 1
    // refreshSyncEnablement pattern: cheap to call every timer tick (setEnabled/setVisible
    // early-out when unchanged), catches automation and preset changes too.
    void refreshEnablement();

    // Enters/exits/switches the display's edit mode for `lane` (-1 = exit). Syncs chip
    // highlights and pushes the lane's current nodes into the display.
    void setEditLane (int lane);

    // Chip click handler: no-op unless that lane's waveform is Custom, else toggles edit mode.
    // Also the Edit/Done pill's click handler (buildLaneStrip) -- same gate, same effect.
    void onLaneChipClicked (int lane);

    // Waveform combo onChange handler (finding #3's "selecting Custom auto-enters edit mode").
    // Auto-enter rule chosen: fire only when the combo's selection actually CHANGES TO Custom
    // (tracked via lastWaveform[lane], since onChange can't otherwise tell a user pick from a
    // parameter-driven sync -- see lastWaveform's doc comment) and the lane isn't a linked
    // follower and isn't already the edit lane. Accepted trade-off: a state/preset reload that
    // restores a lane to Custom from a different prior value will also briefly auto-enter edit
    // mode -- mildly surprising but harmless (the timerCallback watchdog exits edit mode again
    // the instant that lane stops being effectively Custom or becomes a follower).
    void onWaveformSelected (int lane);

    // Resolves lane `lane`'s EFFECTIVE waveform: when Link is on, lanes 1-2 follow lane 0's
    // waveform (the processor routes lane 0's table to followers), so a follower's own raw
    // waveform is not what's actually playing. Returns lane 0's raw waveform for a linked
    // follower, otherwise the lane's own raw waveform. Shared by the chip-click gate and the
    // edit-mode watchdog so neither can be fooled by a follower's hidden Custom shape.
    lflow::Waveform effectiveWaveform (int lane) const;

    // Phase 7 Task 5 (UX #4): scoped, undoable resets. showLaneChipMenu() is the lane chip's
    // right-click handler (async popup: "Reset lane to defaults" always, "Reset curve to
    // triangle" only when that lane's effective waveform is Custom). resetLaneToDefaults()
    // sets that lane's 8 own parameters (not the follower-effective ones) back to their APVTS
    // layout defaults via setValueNotifyingHost, one named undo transaction. resetLaneCurve()
    // resets ONE lane's drawn shape to the default triangle via ShapeManager::setNodes (an
    // empty node list is its documented "invalid input" fallback -- see setNodes()'s doc
    // comment), also one named undo transaction. Both are wired to the lane chip AND (for the
    // currently-edited lane's curve) the display's own right-click menu.
    void showLaneChipMenu (int lane);
    void resetLaneToDefaults (int lane);
    void resetLaneCurve (int lane);

    // Folded Phase 3 item: tints the Xover Hi label/readout and extends its tooltip when the
    // engine is clamping it against xoverLow*1.25 (see MultiLaneEngine/xover clamp behaviour).
    void refreshXoverHint();

    // Phase 7 Task 6 (UX #7 + opp #5): sets Xover Lo/Hi + their labels to 50% alpha (present,
    // quiet, still fully draggable) when no lane has a band destination (Low/Mid/High) with
    // depth > 0, full alpha when one does; also updates bandsCaptionQuiet for paint()'s painted
    // "BANDS" micro-label. Compare-guarded via bandsLiveState. Called from timerCallback().
    void refreshBandEmphasis();

    // Phase 7 Task 6 (UX #5): sets lane `lane`'s Depth slider tooltip to a destination-aware
    // string (Volume/Pan/Low/Mid/High/Pitch each read differently -- see .cpp for the exact
    // copy) when that lane's Dest actually changed since the last call (lastDepthTooltipDest
    // compare-guard). Called once per lane from timerCallback() and once from buildLaneStrip()
    // (seeded with that lane's initial Dest) so the tooltip is never stale between construction
    // and the first timer tick.
    void refreshDepthTooltip (int lane);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessorEditor)
};
