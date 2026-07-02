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

    // Small decorative chip painted in a lane's accent color with its lane number. Purely
    // visual (not interactive), so it carries no tooltip.
    class LaneChip : public juce::Component
    {
    public:
        void setup (juce::uint32 colourArgb, int laneNumber);
        void paint (juce::Graphics&) override;

    private:
        juce::uint32 colour { 0xffffffffu };
        int number { 1 };
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
        const char* waveformId { nullptr };
        const char* phaseId { nullptr };
        const char* depthId { nullptr };
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

    void buildLaneStrip (int laneIndex);
    void layoutLaneStrip (int laneIndex, juce::Rectangle<int> area);
    static void styleRotary (juce::Slider&, int textBoxWidth, int textBoxHeight);

    // Refreshes per-lane enablement + rate/division-slot visibility. Mirrors the Phase 1
    // refreshSyncEnablement pattern: cheap to call every timer tick (setEnabled/setVisible
    // early-out when unchanged), catches automation and preset changes too.
    void refreshEnablement();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessorEditor)
};
