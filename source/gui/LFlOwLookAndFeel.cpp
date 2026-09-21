#include "LFlOwLookAndFeel.h"

LFlOwLookAndFeel::LFlOwLookAndFeel()
{
    // The base zqsfx::ui::LookAndFeel constructor already set the house colours this class
    // used to set itself: ComboBox/PopupMenu -> LCD glass, Slider textbox -> LCD glass + glow,
    // TextButton -> btn gradient / accent-on, TooltipWindow/AlertWindow/TextEditor -> house
    // tokens. Nothing here needs to re-set or override any of that. rotarySliderFillColourId /
    // rotarySliderOutlineColourId / thumbColourId are gone too: the house's filmstrip knobs
    // carry their own pointer and consult no per-slider colour at all (see
    // zqsfx::ui::LookAndFeel::drawRotarySlider / drawVectorKnob).
    setColour (juce::ToggleButton::textColourId, juce::Colour (Colors::onSurfaceVariant));
    setColour (juce::ToggleButton::tickColourId, juce::Colour (Colors::primary));
}

// ---------------------------------------------------------------------- Toggle buttons

void LFlOwLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/)
{
    namespace colour = zqsfx::ui::colour;

    const float alphaMul = button.isEnabled() ? 1.0f : disabledAlpha;
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    // Hard-edged rectangle — no rounded pill, no corner radius (style guide section 6).
    if (on)
    {
        g.setColour (colour::accent.withAlpha (alphaMul));
        g.fillRect (bounds);
        g.setColour (juce::Colours::black.withAlpha (0.35f * alphaMul));
        g.fillRect (bounds.withTop (bounds.getBottom() - 2.0f));
    }
    else
    {
        g.setGradientFill (zqsfx::ui::gradients::button (bounds, button.isEnabled()));
        g.fillRect (bounds);
        g.setColour (juce::Colours::white.withAlpha (button.isEnabled() ? 0.07f : 0.0f));
        g.fillRect (bounds.removeFromTop (1.0f));
    }
    g.setColour (colour::btnBorder.withAlpha (alphaMul));
    g.drawRect (button.getLocalBounds().toFloat(), 1.0f);

    const auto textColour = ! button.isEnabled() ? colour::silkCaption
                           : on                   ? colour::accentInk
                                                   : button.findColour (juce::ToggleButton::textColourId);
    g.setColour (textColour.withAlpha (alphaMul));
    g.setFont (silkFont (12.0f, true));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------- Linear sliders (Rate)

void LFlOwLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                         const juce::Slider::SliderStyle /*style*/, juce::Slider& slider)
{
    namespace colour = zqsfx::ui::colour;

    const float alphaMul = slider.isEnabled() ? 1.0f : disabledAlpha;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    // Screen-glass track, ruleTitle border — hard rectangle, no rounded caps.
    constexpr float trackH = 4.0f;
    const auto trackY = bounds.getCentreY() - trackH * 0.5f;
    const juce::Rectangle<float> track (bounds.getX(), trackY, bounds.getWidth(), trackH);
    g.setColour (colour::lcdScreenDark.withAlpha (alphaMul));
    g.fillRect (track);
    g.setColour (colour::ruleTitle.withAlpha (alphaMul));
    g.drawRect (track, 1.0f);

    // Filled portion, in the control's own lane colour (rotarySliderFillColourId — see
    // buildLaneStrip).
    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const float fillW = juce::jlimit (0.0f, bounds.getWidth(), sliderPos - bounds.getX());
    if (fillW > 0.0f)
    {
        const juce::Rectangle<float> fill (bounds.getX(), trackY, fillW, trackH);
        g.setColour (fillColour.withAlpha (alphaMul));
        g.fillRect (fill);
    }

    // Slim rectangular thumb — no stock white ball, no rounded capsule.
    constexpr float thumbW = 5.0f;
    constexpr float thumbH = 14.0f;
    const juce::Rectangle<float> thumb (sliderPos - thumbW * 0.5f, bounds.getCentreY() - thumbH * 0.5f,
                                         thumbW, thumbH);
    g.setColour (colour::pointer.withAlpha (alphaMul));
    g.fillRect (thumb);
}
