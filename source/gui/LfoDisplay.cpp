#include "LfoDisplay.h"
#include "LFlOwLookAndFeel.h"

void LfoDisplay::paint (juce::Graphics& g)
{
    using C = LFlOwLookAndFeel::Colors;
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (juce::Colour (C::surface));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (C::outline));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    lflow::LfoCore core; core.setWaveform (waveform); core.reset (1u);

    juce::Path path;
    const int N = juce::jmax (2, (int) r.getWidth());
    for (int i = 0; i < N; ++i)
    {
        const float ph = (float) i / (float) (N - 1);
        const float v  = core.valueAt (ph);            // 0..1
        const float x  = r.getX() + ph * r.getWidth();
        const float y  = r.getBottom() - v * r.getHeight();
        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }
    g.setColour (juce::Colour (C::primary));
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // live marker
    const float mx = r.getX() + phase * r.getWidth();
    const float my = r.getBottom() - value * r.getHeight();
    g.setColour (juce::Colour (C::onSurface));
    g.fillEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
    g.setColour (juce::Colour (C::onSurface).withAlpha (0.2f));
    g.drawVerticalLine ((int) mx, r.getY(), r.getBottom());
}
