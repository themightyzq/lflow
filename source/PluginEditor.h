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

        std::function<void()> onClick;

    private:
        juce::uint32 colour { 0xffffffffu };
        int number { 1 };
        bool editActive { false };
    };

    // One lane's worth of controls + attachments. Attachments always stay connected to their
    // own lane's parameters — link only greys the display, automation still reaches every
    // param.
    struct LaneStrip
    {
        LaneChip chip;
        juce::Label nameLabel;

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

    // Cheap-compare cache so refreshXoverHint() only touches colours/tooltips on change.
    int xoverHiClampedState { -1 }; // -1 = unknown (forces first apply), 0 = no, 1 = yes

    void buildLaneStrip (int laneIndex);
    void layoutLaneStrip (int laneIndex, juce::Rectangle<int> area);
    static void styleRotary (juce::Slider&, int textBoxWidth, int textBoxHeight);

    // Refreshes per-lane enablement + rate/division-slot visibility. Mirrors the Phase 1
    // refreshSyncEnablement pattern: cheap to call every timer tick (setEnabled/setVisible
    // early-out when unchanged), catches automation and preset changes too.
    void refreshEnablement();

    // Enters/exits/switches the display's edit mode for `lane` (-1 = exit). Syncs chip
    // highlights and pushes the lane's current nodes into the display.
    void setEditLane (int lane);

    // Chip click handler: no-op unless that lane's waveform is Custom, else toggles edit mode.
    void onLaneChipClicked (int lane);

    // Folded Phase 3 item: tints the Xover Hi label/readout and extends its tooltip when the
    // engine is clamping it against xoverLow*1.25 (see MultiLaneEngine/xover clamp behaviour).
    void refreshXoverHint();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessorEditor)
};
