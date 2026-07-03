#pragma once
#include <JuceHeader.h>
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

    private:
        juce::uint32 colour { 0xffffffffu };
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

    void buildLaneStrip (int laneIndex);

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

    // Folded Phase 3 item: tints the Xover Hi label/readout and extends its tooltip when the
    // engine is clamping it against xoverLow*1.25 (see MultiLaneEngine/xover clamp behaviour).
    void refreshXoverHint();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessorEditor)
};
