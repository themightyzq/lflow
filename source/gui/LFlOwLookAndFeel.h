#pragma once
#include <JuceHeader.h>

// Central color system — NO hardcoded juce::Colours:: anywhere in editor code.
// LFlOw is a Modulation plugin: primary accent is pink (#ff6bb5).
//
// This is also the identity pass (Phase 6 Task 1): every widget the eye lands on is drawn
// here rather than left as a stock JUCE default — 270-degree arc rotaries (no ball thumb),
// flat combo boxes with a custom chevron, pill toggle buttons, and borderless readouts.
// Disabled controls always render at a flat 35% alpha so the Link-greying state (and any
// other disablement) is unmistakable rather than the barely-there JUCE default.
class LFlOwLookAndFeel : public juce::LookAndFeel_V4
{
public:
    struct Colors
    {
        static constexpr juce::uint32 background      = 0xff1a1a1f;
        static constexpr juce::uint32 surface         = 0xff111116;
        static constexpr juce::uint32 outline         = 0xff2a2a33;
        static constexpr juce::uint32 knobTrack       = 0xff3f3f4d; // rotary track only: outline
                                                                    // is invisible over background
                                                                    // at 0% (empty-looking knobs)
        static constexpr juce::uint32 onSurface       = 0xffe8e8ee;
        static constexpr juce::uint32 onSurfaceVariant= 0xff9a9aa6;
        static constexpr juce::uint32 primary         = 0xffff6bb5; // modulation pink

        // Lane accent colors (index-matched to lane 1/2/3 in the UI and in LfoDisplay).
        static constexpr juce::uint32 lane0            = 0xffff6bb5; // lane 1 — pink (== primary)
        static constexpr juce::uint32 lane1            = 0xff9b7adb; // lane 2 — purple
        static constexpr juce::uint32 lane2            = 0xffffab00; // lane 3 — amber
    };

    // Disabled controls render at this alpha everywhere in this LookAndFeel (finding #5:
    // JUCE's default disabled dimming is too subtle to notice against a dark theme).
    static constexpr float disabledAlpha = 0.35f;

    LFlOwLookAndFeel();

    // Lane accent colour by index (0/1/2), shared by the editor's LaneChip/labels and the
    // LfoDisplay overlay so both stay in lockstep with a single source of truth.
    static constexpr juce::uint32 laneColour (int laneIndex)
    {
        using C = Colors;
        switch (laneIndex)
        {
            case 0:  return C::lane0;
            case 1:  return C::lane1;
            default: return C::lane2;
        }
    }

    // ---- Rotary knobs: 270-degree arc, 3px round-capped stroke, small endpoint dot, no
    // ball thumb, hollow center. Track in Colors::outline, value fill in the slider's own
    // rotarySliderFillColourId (lane knobs get lane colours, globals inherit primary).
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    // ---- Combo boxes: flat surface fill, 1px outline border, 4px radius, custom two-stroke
    // chevron, left-padded text.
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                        int buttonX, int buttonY, int buttonW, int buttonH,
                        juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    // ---- Toggle buttons: pill shape. Off = outline pill / onSurfaceVariant text. On =
    // filled with the button's tickColourId / background text. Centered label, no tick mark.
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // ---- Labels (also the mechanism behind every slider's readout textbox and combo box
    // text): borderless/backgroundless, single disabled-alpha standard.
    void drawLabel (juce::Graphics&, juce::Label&) override;

    // ---- Linear sliders (lane Rate sliders only, LinearHorizontal): flat rounded track in
    // `outline`, filled portion in the slider's own accent (rotarySliderFillColourId, set
    // per-lane in buildLaneStrip like the rotary knobs), slim rounded-capsule thumb — no
    // stock white ball. Disabled = 35% alpha, same standard as every other control here.
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            const juce::Slider::SliderStyle, juce::Slider&) override;
};
