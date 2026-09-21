#pragma once
#include <JuceHeader.h>
#include <zqsfx_ui/zqsfx_ui.h>

// Central color system — NO hardcoded juce::Colours:: anywhere in editor code. LFlOw now
// shares the ZQ SFX house look (docs/ZQSFX_UI_STYLE_GUIDE.md); every value below is a named
// house token so LFlOw reads as the same family as the other products while keeping its own
// lane-identity meaning (see the per-field comments for which zqsfx::ui token each mirrors).
//
// LFlOwLookAndFeel is now a THIN SUBCLASS of zqsfx::ui::LookAndFeel: the house LookAndFeel
// supplies rotary knobs (CC0 filmstrips, picked by dial size), combo boxes (LCD dropdowns),
// and slider text-box readouts (LCD glass + glow) automatically once its drawRotarySlider /
// drawComboBox / positionComboBoxText / drawLabel are left un-overridden. This subclass keeps
// only the two overrides the house LookAndFeel has no equivalent for: toggle buttons and the
// linear Rate slider, both restyled with house tokens (hard-edged rectangles, no rounded
// pills or corner radius — style guide section 6). Disabled controls still render at a flat
// 35% alpha (disabledAlpha) — a documented LFlOw accessibility decision, kept as-is.
class LFlOwLookAndFeel : public zqsfx::ui::LookAndFeel
{
public:
    struct Colors
    {
        static constexpr juce::uint32 background       = 0xff0a0b0c; // == zqsfx::ui::colour::chassisMid
        static constexpr juce::uint32 surface           = 0xff121416; // == zqsfx::ui::colour::panelBot
        static constexpr juce::uint32 outline           = 0xff22272a; // == zqsfx::ui::colour::ruleTitle
        static constexpr juce::uint32 knobTrack         = 0xff3d4448; // == zqsfx::ui::colour::ledOffRim
        static constexpr juce::uint32 onSurface         = 0xffc9d4d2; // == zqsfx::ui::colour::btnText
        static constexpr juce::uint32 onSurfaceVariant  = 0xff8fb3ae; // == zqsfx::ui::colour::silkLabel
        static constexpr juce::uint32 primary           = 0xffe8622a; // == zqsfx::ui::colour::accent

        // Lane accent colours (index-matched to lane 1/2/3 in the UI and in LfoDisplay): the
        // colour-blind-safe complementary channels, assigned in table order per style guide
        // section 3 ("LFlOw: lane 1 comp.sky, lane 2 comp.yellow, lane 3 comp.purple").
        static constexpr juce::uint32 lane0             = 0xff56b4e9; // == zqsfx::ui::comp::sky
        static constexpr juce::uint32 lane1             = 0xfff0e442; // == zqsfx::ui::comp::yellow
        static constexpr juce::uint32 lane2             = 0xffcc79a7; // == zqsfx::ui::comp::purple

        // NOT in the style guide's base remap table: added so the Xover Hi "clamped by Xover
        // Lo" caution tint (refreshXoverHint()) has a colour of its own now that lane2 means
        // "lane 3's channel colour" rather than the old amber warning tint it used to reuse.
        // Reusing `primary`/accent would be wrong (accent means "active", never a caution), and
        // reusing `lane2` would make the caution read as "this is lane 3's control" instead —
        // exactly the collision this constant avoids. meterHot is the house's own "getting hot"
        // amber (style guide section 2, meter stops).
        static constexpr juce::uint32 warn              = 0xffd9a441; // == zqsfx::ui::colour::meterHot
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

    // ---- Toggle buttons: the house LookAndFeel has no drawToggleButton override (its bound
    // LitToggle/TextToggle components draw themselves instead), so this stays — restyled with
    // house tokens: hard-edged rectangle (no pill, no corner radius), `btn` gradient off-state,
    // `accent` fill + `accentInk` text on-state, matching
    // zqsfx::ui::LookAndFeel::drawButtonBackground's own on/off treatment.
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // ---- Linear sliders (lane Rate sliders only, LinearHorizontal): the house LookAndFeel has
    // no drawLinearSlider override, so this stays — track in `lcdScreenDark` with a `ruleTitle`
    // border, filled portion in the slider's own lane colour (rotarySliderFillColourId, set per
    // lane in buildLaneStrip), slim rectangular thumb — no stock white ball, no rounded pill.
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            const juce::Slider::SliderStyle, juce::Slider&) override;
};
