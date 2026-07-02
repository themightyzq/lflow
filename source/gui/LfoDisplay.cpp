#include "LfoDisplay.h"
#include "LFlOwLookAndFeel.h"

void LfoDisplay::setLane (int lane, lflow::Waveform waveform, float phaseOffset01, bool active)
{
    if (lane < 0 || lane >= kNumLanes)
        return;

    auto& l = lanes[(size_t) lane];
    l.waveform = waveform;
    l.phaseOffset = phaseOffset01;
    l.active = active;
    repaint();
}

void LfoDisplay::setLanePosition (int lane, float phase01, float value01)
{
    if (lane < 0 || lane >= kNumLanes)
        return;

    auto& l = lanes[(size_t) lane];
    l.phase = phase01;
    l.value = value01;
    repaint();
}

void LfoDisplay::rebuildPathIfNeeded (LaneState& lane)
{
    if (lane.pathValid
        && lane.pathWaveform == lane.waveform
        && juce::approximatelyEqual (lane.pathPhaseOffset, lane.phaseOffset))
        return;

    lflow::LfoCore core;
    core.setWaveform (lane.waveform);
    core.reset (1u);

    juce::Path p;
    constexpr int N = 128;
    for (int i = 0; i < N; ++i)
    {
        const float x = (float) i / (float) (N - 1);
        float ph = x + lane.phaseOffset;
        ph -= std::floor (ph);
        const float v = core.valueAt (ph); // curve(x) = valueAt(frac(x + offset))
        const float y = 1.0f - v;          // unit square: y grows downward, value grows upward
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    lane.path = p;
    lane.pathValid = true;
    lane.pathWaveform = lane.waveform;
    lane.pathPhaseOffset = lane.phaseOffset;
}

void LfoDisplay::paint (juce::Graphics& g)
{
    using C = LFlOwLookAndFeel::Colors;
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (juce::Colour (C::surface));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (C::outline));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    const auto transform = juce::AffineTransform::scale (r.getWidth(), r.getHeight())
                                .translated (r.getX(), r.getY());

    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& lane = lanes[(size_t) i];
        rebuildPathIfNeeded (lane);

        const auto colour = juce::Colour (LFlOwLookAndFeel::laneColour (i));
        g.setColour (lane.active ? colour : colour.withAlpha (0.35f));
        g.strokePath (lane.path, juce::PathStrokeType (lane.active ? 2.0f : 1.0f), transform);

        if (lane.active)
        {
            // Undo the phase offset baked into the reported phase so the marker's x lines up
            // with the same raw-cycle x-axis the curve was drawn against.
            float rawPhase = lane.phase - lane.phaseOffset;
            rawPhase -= std::floor (rawPhase);

            const float mx = r.getX() + rawPhase * r.getWidth();
            const float my = r.getBottom() - lane.value * r.getHeight();
            g.setColour (colour);
            g.fillEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
        }
    }
}
