#pragma once
#include <JuceHeader.h>

// Central color system — NO hardcoded juce::Colours:: anywhere in editor code.
// LFlOw is a Modulation plugin: primary accent is pink (#ff6bb5).
class LFlOwLookAndFeel : public juce::LookAndFeel_V4
{
public:
    struct Colors
    {
        static constexpr juce::uint32 background      = 0xff1a1a1f;
        static constexpr juce::uint32 surface         = 0xff111116;
        static constexpr juce::uint32 outline         = 0xff2a2a33;
        static constexpr juce::uint32 onSurface       = 0xffe8e8ee;
        static constexpr juce::uint32 onSurfaceVariant= 0xff9a9aa6;
        static constexpr juce::uint32 primary         = 0xffff6bb5; // modulation pink

        // Lane accent colors (index-matched to lane 1/2/3 in the UI and in LfoDisplay).
        static constexpr juce::uint32 lane0            = 0xffff6bb5; // lane 1 — pink (== primary)
        static constexpr juce::uint32 lane1            = 0xff9b7adb; // lane 2 — purple
        static constexpr juce::uint32 lane2            = 0xffffab00; // lane 3 — amber
    };

    LFlOwLookAndFeel()
    {
        setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (Colors::primary));
        setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (Colors::outline));
        setColour (juce::Slider::thumbColourId,               juce::Colour (Colors::onSurface));
        setColour (juce::Slider::textBoxTextColourId,         juce::Colour (Colors::onSurface));
        setColour (juce::Slider::textBoxOutlineColourId,      juce::Colour (0x00000000));
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
};
