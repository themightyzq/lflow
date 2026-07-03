#include "LFlOwLookAndFeel.h"

LFlOwLookAndFeel::LFlOwLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (Colors::primary));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (Colors::outline));
    setColour (juce::Slider::thumbColourId,               juce::Colour (Colors::onSurface));

    // Readouts: one style everywhere — borderless, no background, quiet text. Values are
    // secondary to controls (REVIEW-DESIGN.md finding #6's inconsistent boxed look).
    setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colour (0x00000000));
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colour (0x00000000));
    setColour (juce::Slider::textBoxTextColourId,         juce::Colour (Colors::onSurfaceVariant));

    setColour (juce::ComboBox::backgroundColourId,        juce::Colour (Colors::surface));
    setColour (juce::ComboBox::textColourId,              juce::Colour (Colors::onSurface));
    setColour (juce::ComboBox::outlineColourId,           juce::Colour (Colors::outline));
    setColour (juce::PopupMenu::backgroundColourId,       juce::Colour (Colors::surface));

    setColour (juce::ToggleButton::textColourId,          juce::Colour (Colors::onSurfaceVariant));
    setColour (juce::ToggleButton::tickColourId,          juce::Colour (Colors::primary));

    setColour (juce::Label::textColourId,                 juce::Colour (Colors::onSurfaceVariant));
    setColour (juce::TooltipWindow::backgroundColourId,   juce::Colour (Colors::surface));
    setColour (juce::TooltipWindow::textColourId,         juce::Colour (Colors::onSurface));
}

// ---------------------------------------------------------------------- Rotary sliders

void LFlOwLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const float alphaMul = slider.isEnabled() ? 1.0f : disabledAlpha;

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    constexpr float lineW = 3.0f;
    const auto arcRadius = radius - lineW * 0.5f;
    const auto toAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Track (full 270-degree sweep), hollow center — no fill, no ball thumb.
    juce::Path track;
    track.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius,
                          0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (Colors::outline).withAlpha (alphaMul));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value fill, in the control's own accent (lane colour or primary — caller sets it via
    // Slider::rotarySliderFillColourId).
    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    if (sliderPosProportional > 0.0f)
    {
        juce::Path valueArc;
        valueArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius,
                                 0.0f, rotaryStartAngle, toAngle, true);
        g.setColour (fillColour.withAlpha (alphaMul));
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Small filled dot at the value endpoint (replaces the stock ball thumb).
    constexpr float dotDiameter = 5.0f;
    const juce::Point<float> dotCentre (
        bounds.getCentreX() + arcRadius * std::cos (toAngle - juce::MathConstants<float>::halfPi),
        bounds.getCentreY() + arcRadius * std::sin (toAngle - juce::MathConstants<float>::halfPi));
    g.setColour (fillColour.withAlpha (alphaMul));
    g.fillEllipse (juce::Rectangle<float> (dotDiameter, dotDiameter).withCentre (dotCentre));
}

// ---------------------------------------------------------------------- Combo boxes

void LFlOwLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                     int buttonX, int buttonY, int buttonW, int buttonH,
                                     juce::ComboBox& box)
{
    const float alphaMul = box.isEnabled() ? 1.0f : disabledAlpha;
    constexpr float cornerRadius = 4.0f;

    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (juce::Colour (Colors::surface).withAlpha (alphaMul));
    g.fillRoundedRectangle (bounds, cornerRadius);
    g.setColour (juce::Colour (Colors::outline).withAlpha (alphaMul));
    g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);

    // Custom two-stroke chevron (a "v" made of two joined line segments), right-aligned in
    // the button zone reserved by positionComboBoxText().
    const auto chevronCentre = juce::Rectangle<float> ((float) buttonX, (float) buttonY,
                                                        (float) buttonW, (float) buttonH).getCentre();
    constexpr float chevronHalfWidth = 4.0f;
    constexpr float chevronHeight = 3.0f;
    juce::Path chevron;
    chevron.startNewSubPath (chevronCentre.x - chevronHalfWidth, chevronCentre.y - chevronHeight * 0.5f);
    chevron.lineTo (chevronCentre.x, chevronCentre.y + chevronHeight * 0.5f);
    chevron.lineTo (chevronCentre.x + chevronHalfWidth, chevronCentre.y - chevronHeight * 0.5f);

    g.setColour (juce::Colour (Colors::onSurfaceVariant).withAlpha (alphaMul));
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LFlOwLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    constexpr int arrowZoneWidth = 20;
    constexpr int leftPad = 8;

    label.setBounds (leftPad, 0, juce::jmax (0, box.getWidth() - arrowZoneWidth - leftPad), box.getHeight());
    label.setJustificationType (juce::Justification::centredLeft);
    label.setFont (getComboBoxFont (box));
}

// ---------------------------------------------------------------------- Toggle buttons (pills)

void LFlOwLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool /*shouldDrawButtonAsHighlighted*/, bool /*shouldDrawButtonAsDown*/)
{
    const float alphaMul = button.isEnabled() ? 1.0f : disabledAlpha;
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto cornerRadius = bounds.getHeight() * 0.5f;
    const bool on = button.getToggleState();

    if (on)
    {
        g.setColour (button.findColour (juce::ToggleButton::tickColourId).withAlpha (alphaMul));
        g.fillRoundedRectangle (bounds, cornerRadius);
    }
    else
    {
        g.setColour (juce::Colour (Colors::outline).withAlpha (alphaMul));
        g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
    }

    const auto textColour = on ? juce::Colour (Colors::background)
                               : button.findColour (juce::ToggleButton::textColourId);
    g.setColour (textColour.withAlpha (alphaMul));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------- Linear sliders (Rate)

void LFlOwLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                         const juce::Slider::SliderStyle /*style*/, juce::Slider& slider)
{
    const float alphaMul = slider.isEnabled() ? 1.0f : disabledAlpha;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();

    constexpr float trackH = 4.0f;
    const auto trackY = bounds.getCentreY() - trackH * 0.5f;
    const juce::Rectangle<float> track (bounds.getX(), trackY, bounds.getWidth(), trackH);

    g.setColour (juce::Colour (Colors::outline).withAlpha (alphaMul));
    g.fillRoundedRectangle (track, trackH * 0.5f);

    // Filled portion, in the control's own accent (lane colour — see buildLaneStrip).
    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const float fillW = juce::jlimit (0.0f, bounds.getWidth(), sliderPos - bounds.getX());
    if (fillW > 0.0f)
    {
        const juce::Rectangle<float> fill (bounds.getX(), trackY, fillW, trackH);
        g.setColour (fillColour.withAlpha (alphaMul));
        g.fillRoundedRectangle (fill, trackH * 0.5f);
    }

    // Slim rounded-capsule thumb — replaces the stock white ball.
    constexpr float thumbW = 6.0f;
    constexpr float thumbH = 14.0f;
    const juce::Rectangle<float> thumb (sliderPos - thumbW * 0.5f, bounds.getCentreY() - thumbH * 0.5f,
                                         thumbW, thumbH);
    g.setColour (juce::Colour (Colors::onSurface).withAlpha (alphaMul));
    g.fillRoundedRectangle (thumb, thumbW * 0.5f);
}

// ---------------------------------------------------------------------- Labels (readouts)

void LFlOwLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (label.isBeingEdited())
        return;

    const float alphaMul = label.isEnabled() ? 1.0f : disabledAlpha;
    const auto font = getLabelFont (label);

    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alphaMul));
    g.setFont (font);

    auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
    g.drawFittedText (label.getText(), textArea, label.getJustificationType(),
                      juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight())),
                      label.getMinimumHorizontalScale());
}
