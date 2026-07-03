#include "LfoDisplay.h"
#include "LFlOwLookAndFeel.h"

namespace
{
// ShapeModel.h is deliberately JUCE-free (audio-thread safe), so the GUI does the
// compare-before-rebuild check here, mirroring setLane's juce::approximatelyEqual use for
// phaseOffset -- avoids both a raw float== warning and touching the audio-thread header.
bool laneNodesEqual (const std::vector<lflow::ShapeNode>& a, const std::vector<lflow::ShapeNode>& b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
        if (! juce::approximatelyEqual (a[i].x, b[i].x)
            || ! juce::approximatelyEqual (a[i].y, b[i].y)
            || ! juce::approximatelyEqual (a[i].curve, b[i].curve))
            return false;

    return true;
}
} // namespace

void LfoDisplay::setLane (int lane, lflow::Waveform waveform, float phaseOffset01, bool active)
{
    if (lane < 0 || lane >= kNumLanes)
        return;

    auto& l = lanes[(size_t) lane];
    if (lane == editLane && ! juce::approximatelyEqual (l.phaseOffset, phaseOffset01))
        editPathDirty = true;

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

void LfoDisplay::setLaneNodes (int lane, std::vector<lflow::ShapeNode> nodes)
{
    if (lane < 0 || lane >= kNumLanes)
        return;

    auto& l = lanes[(size_t) lane];
    if (laneNodesEqual (nodes, l.nodes))
        return; // no real change -- avoid rebuild churn from the ~3x/tick feed in the editor

    l.nodes = std::move (nodes);
    l.pathValid = false; // force rebuildPathIfNeeded to re-evaluate this lane next paint
    repaint();
}

void LfoDisplay::setEditLane (int laneOrMinus1)
{
    if (laneOrMinus1 == editLane)
        return;

    editLane = laneOrMinus1;
    dragNodeIndex = -1;
    dragSegmentIndex = -1;
    editPathDirty = true;
    repaint();
}

void LfoDisplay::setEditNodes (const std::vector<lflow::ShapeNode>& nodes)
{
    if (laneNodesEqual (nodes, editNodes))
        return; // no real change -- avoid rebake/repaint churn from the 60Hz feed in the editor

    editNodes = nodes;
    rebakeEditTable();
    editPathDirty = true;
    repaint();
}

void LfoDisplay::rebakeEditTable()
{
    lflow::bakeShapeTable (editNodes.empty() ? nullptr : editNodes.data(),
                            (int) editNodes.size(), editTable, lflow::kShapeTableSize);
}

void LfoDisplay::rebuildEditPath()
{
    if (editLane < 0)
        return;

    // Unshifted shape space: no phase offset baked in here, so this curve lines up with
    // nodeToScreen/findNodeNear/findSegmentNear, which all key off raw node x. See paint()'s
    // marker-x branch for the edit lane, which matches this by using the reported phase directly.
    juce::Path p;
    constexpr int N = 128;
    for (int i = 0; i < N; ++i)
    {
        const float x = (float) i / (float) (N - 1);
        const float v = lflow::shapeTableValue (editTable, lflow::kShapeTableSize, x);
        const float y = 1.0f - v;
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }
    editPath = p;
}

void LfoDisplay::rebuildPathIfNeeded (LaneState& lane)
{
    if (lane.pathValid
        && lane.pathWaveform == lane.waveform
        && juce::approximatelyEqual (lane.pathPhaseOffset, lane.phaseOffset))
        return;

    // A Custom lane with a real drawn shape renders straight from its node model (same
    // piecewise power-curve baked table used for edit-mode rendering) rather than LfoCore --
    // LfoCore has no table for Waveform::Custom and would otherwise silently fall back to
    // Sine, which is exactly the bug this lane-nodes plumbing exists to fix.
    const bool useNodeModel = (lane.waveform == lflow::Waveform::Custom && lane.nodes.size() >= 2);

    float table[lflow::kShapeTableSize] {};
    lflow::LfoCore core;
    if (useNodeModel)
        lflow::bakeShapeTable (lane.nodes.data(), (int) lane.nodes.size(), table, lflow::kShapeTableSize);
    else
    {
        core.setWaveform (lane.waveform);
        core.reset (1u);
    }

    juce::Path p;
    constexpr int N = 128;
    for (int i = 0; i < N; ++i)
    {
        const float x = (float) i / (float) (N - 1);
        float ph = x + lane.phaseOffset;
        ph -= std::floor (ph);
        const float v = useNodeModel
            ? lflow::shapeTableValue (table, lflow::kShapeTableSize, ph)
            : core.valueAt (ph); // curve(x) = valueAt(frac(x + offset))
        const float y = 1.0f - v;          // unit square: y grows downward, value grows upward
        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    lane.path = p;
    lane.pathValid = true;
    lane.pathWaveform = lane.waveform;
    lane.pathPhaseOffset = lane.phaseOffset;
}

juce::Rectangle<float> LfoDisplay::displayArea() const noexcept
{
    return getLocalBounds().toFloat().reduced (6.0f);
}

juce::Point<float> LfoDisplay::nodeToScreen (const lflow::ShapeNode& n) const noexcept
{
    const auto r = displayArea();
    return { r.getX() + n.x * r.getWidth(), r.getY() + (1.0f - n.y) * r.getHeight() };
}

int LfoDisplay::findNodeNear (juce::Point<float> screenPos) const noexcept
{
    int best = -1;
    float bestDist = kNodeHitRadius;
    for (size_t i = 0; i < editNodes.size(); ++i)
    {
        const float d = nodeToScreen (editNodes[i]).getDistanceFrom (screenPos);
        if (d <= bestDist)
        {
            bestDist = d;
            best = (int) i;
        }
    }
    return best;
}

float LfoDisplay::curveValueAt (float x) const noexcept
{
    // Avoid shapeTableValue's phase-wrap kicking in exactly at x == 1.0 (which would fold
    // back to the table's start rather than the flat-extended end value).
    x = juce::jlimit (0.0f, 0.999999f, x);
    return lflow::shapeTableValue (editTable, lflow::kShapeTableSize, x);
}

int LfoDisplay::findSegmentNear (juce::Point<float> screenPos) const noexcept
{
    if (editNodes.size() < 2)
        return -1;

    const auto r = displayArea();
    if (r.getWidth() <= 0.0f)
        return -1;

    const float x = (screenPos.x - r.getX()) / r.getWidth();
    const float firstX = editNodes.front().x;
    const float lastX  = editNodes.back().x;
    if (x < firstX || x > lastX)
        return -1; // flat extension either side of the drawn nodes -- not a bendable segment

    for (size_t j = 0; j + 1 < editNodes.size(); ++j)
    {
        if (x >= editNodes[j].x && x <= editNodes[j + 1].x)
        {
            const float v = curveValueAt (x);
            const float y = r.getBottom() - v * r.getHeight();
            if (std::abs (y - screenPos.y) <= kSegmentHitTolerance)
                return (int) j;
            return -1;
        }
    }
    return -1;
}

void LfoDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragNodeIndex = -1;
    dragSegmentIndex = -1;

    if (editLane < 0)
        return;

    const int hitNode = findNodeNear (e.position);
    if (hitNode >= 0)
    {
        dragNodeIndex = hitNode;
        return;
    }

    const int hitSeg = findSegmentNear (e.position);
    if (hitSeg >= 0)
    {
        dragSegmentIndex = hitSeg;
        dragStartScreenY = e.position.y;
        dragStartCurve = editNodes[(size_t) hitSeg].curve;
        return;
    }

    // Empty space: add a node here (ignored past the 32-node cap).
    if ((int) editNodes.size() >= lflow::kMaxShapeNodes)
        return;

    const auto r = displayArea();
    if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f)
        return;

    float x = (e.position.x - r.getX()) / r.getWidth();
    float y = 1.0f - (e.position.y - r.getY()) / r.getHeight();
    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);

    size_t insertAt = editNodes.size();
    for (size_t i = 0; i < editNodes.size(); ++i)
    {
        if (x < editNodes[i].x)
        {
            insertAt = i;
            break;
        }
    }

    editNodes.insert (editNodes.begin() + (long) insertAt, lflow::ShapeNode { x, y, 0.0f });
    dragNodeIndex = (int) insertAt; // lets the same gesture drag the just-added node into place
    rebakeEditTable();
    editPathDirty = true;
    repaint();
    if (onNodesEdited)
        onNodesEdited (editNodes);
}

void LfoDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (editLane < 0)
        return;

    const auto r = displayArea();
    if (r.getWidth() <= 0.0f || r.getHeight() <= 0.0f)
        return;

    if (dragNodeIndex >= 0 && dragNodeIndex < (int) editNodes.size())
    {
        auto& n = editNodes[(size_t) dragNodeIndex];

        const float leftBound  = (dragNodeIndex > 0)
            ? editNodes[(size_t) dragNodeIndex - 1].x : 0.0f;
        const float rightBound = (dragNodeIndex + 1 < (int) editNodes.size())
            ? editNodes[(size_t) dragNodeIndex + 1].x : 1.0f;

        const float x = (e.position.x - r.getX()) / r.getWidth();
        const float y = 1.0f - (e.position.y - r.getY()) / r.getHeight();

        n.x = juce::jlimit (leftBound, rightBound, x);
        n.y = juce::jlimit (0.0f, 1.0f, y);

        rebakeEditTable();
        editPathDirty = true;
        repaint();
        if (onNodesEdited)
            onNodesEdited (editNodes);
        return;
    }

    if (dragSegmentIndex >= 0 && dragSegmentIndex < (int) editNodes.size())
    {
        const float deltaY = dragStartScreenY - e.position.y; // dragging up increases curve
        const float curveDelta = deltaY / r.getHeight();      // full display height => +-1
        editNodes[(size_t) dragSegmentIndex].curve =
            juce::jlimit (-1.0f, 1.0f, dragStartCurve + curveDelta);

        rebakeEditTable();
        editPathDirty = true;
        repaint();
        if (onNodesEdited)
            onNodesEdited (editNodes);
    }
}

void LfoDisplay::mouseUp (const juce::MouseEvent&)
{
    dragNodeIndex = -1;
    dragSegmentIndex = -1;
}

void LfoDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (editLane < 0)
        return;

    const int hit = findNodeNear (e.position);
    if (hit < 0 || editNodes.size() <= 2)
        return; // at least 2 nodes must remain

    editNodes.erase (editNodes.begin() + hit);
    dragNodeIndex = -1;
    dragSegmentIndex = -1;
    rebakeEditTable();
    editPathDirty = true;
    repaint();
    if (onNodesEdited)
        onNodesEdited (editNodes);
}

void LfoDisplay::paint (juce::Graphics& g)
{
    using C = LFlOwLookAndFeel::Colors;
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (juce::Colour (C::surface));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (C::outline));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    // Gridlines (finding #8): faint quarter-cycle verticals + a dotted 50% center line, both
    // free legibility aids for phase offsets and drawn shapes. Drawn behind the lane curves.
    {
        g.setColour (juce::Colour (C::outline).withAlpha (0.5f));
        for (float frac : { 0.25f, 0.5f, 0.75f })
        {
            const float x = r.getX() + frac * r.getWidth();
            g.drawLine (x, r.getY(), x, r.getBottom(), 1.0f);
        }

        juce::Path centerLine;
        centerLine.startNewSubPath (r.getX(), r.getCentreY());
        centerLine.lineTo (r.getRight(), r.getCentreY());
        float dashLengths[] = { 2.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, centerLine, dashLengths, 2);
        g.setColour (juce::Colour (C::outline).withAlpha (0.7f));
        g.fillPath (dashed);
    }

    const auto transform = juce::AffineTransform::scale (r.getWidth(), r.getHeight())
                                .translated (r.getX(), r.getY());

    if (editLane >= 0 && editPathDirty)
    {
        rebuildEditPath();
        editPathDirty = false;
    }

    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& lane = lanes[(size_t) i];
        const bool isEditLane = (i == editLane);
        const auto colour = juce::Colour (LFlOwLookAndFeel::laneColour (i));

        if (isEditLane)
        {
            g.setColour (colour);
            g.strokePath (editPath, juce::PathStrokeType (2.5f), transform);

            for (auto& n : editNodes)
            {
                const auto p = nodeToScreen (n);
                g.setColour (colour);
                g.fillRect (juce::Rectangle<float> (kNodeHandleSize, kNodeHandleSize)
                                .withCentre (p));
            }
        }
        else
        {
            rebuildPathIfNeeded (lane);
            // Other lanes dim harder than usual while another lane is in edit mode.
            const float alpha = (editLane >= 0) ? 0.12f : (lane.active ? 1.0f : 0.35f);
            g.setColour (colour.withAlpha (alpha));
            g.strokePath (lane.path, juce::PathStrokeType (lane.active ? 2.0f : 1.0f), transform);
        }

        if (lane.active)
        {
            float markerX;
            if (isEditLane)
            {
                // Edit lane's curve is unshifted shape space (see rebuildEditPath), and
                // shape(reportedPhase) is exactly what's being played, so the reported phase
                // IS the marker's x here -- no de-offsetting.
                markerX = lane.phase;
            }
            else
            {
                // Non-edit lanes' curves are baked with the phase offset applied (see
                // rebuildPathIfNeeded), so undo it here to land back on that same x-axis.
                markerX = lane.phase - lane.phaseOffset;
            }
            markerX -= std::floor (markerX);

            const float mx = r.getX() + markerX * r.getWidth();
            const float my = r.getBottom() - lane.value * r.getHeight();
            g.setColour (colour);
            g.fillEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
        }
    }
}
