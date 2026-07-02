#pragma once
#include <JuceHeader.h>
#include "dsp/LfoCore.h"
#include "dsp/ShapeModel.h"
#include <functional>
#include <vector>

// Overlays all 3 lanes' one-cycle waveform curves plus a live position marker per active
// lane. Each lane's Path is cached in a normalised [0,1]x[0,1] unit square and rebuilt only
// when that lane's waveform or phase offset actually changes; painting scales the cached
// path onto the current bounds via an AffineTransform (no path copy/rebuild per frame).
//
// Phase 4: also doubles as the in-place breakpoint editor for whichever lane is in edit mode
// (see setEditLane/setEditNodes/onNodesEdited). The edited lane's curve is rendered straight
// from its ShapeNode list (baked locally into a ShapeModel table -- see rebakeEditTable/
// rebuildEditPath) rather than from the audio thread's own table, so edits feel immediate;
// mouse hit-testing/dragging lives here too, editor-side ShapeManager wiring does not.
class LfoDisplay : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    static constexpr int kNumLanes = 3;

    // active == that lane's depth > 0. Rebuilds the cached path only if waveform/phaseOffset
    // changed since the last call.
    void setLane (int lane, lflow::Waveform waveform, float phaseOffset01, bool active);

    // phase01 = processor's reported lane phase (includes the lane's phase offset, i.e. the
    // phase actually fed into the waveform generator); value01 = the modulator value there.
    void setLanePosition (int lane, float phase01, float value01);

    // Backs a NON-edited Custom lane's cached-path render with its real drawn shape instead
    // of the Sine LfoCore fallback (LfoCore has no table for Waveform::Custom). Store +
    // invalidate that lane's cached path only when the node list actually changed
    // (compare-before-rebuild, same pattern as setLane's waveform/offset check). The edit
    // lane keeps rendering from editNodes/editPath as before; this only affects lanes drawn
    // via rebuildPathIfNeeded's "not the edit lane" branch.
    void setLaneNodes (int lane, std::vector<lflow::ShapeNode> nodes);

    // -1 = no lane being edited. When >= 0, that lane's curve renders from editNodes (full
    // opacity + node handles) instead of its cached waveform path, other lanes dim harder, and
    // mouse input on this component edits the shape (see class comment).
    void setEditLane (int laneOrMinus1);

    // Replaces the node list backing the edited lane's render/hit-test. Compares against the
    // current list first (same laneNodesEqual helper as setLaneNodes) and only rebakes the
    // local ShapeModel table + invalidates editPath when the nodes actually changed -- the
    // editor's timerCallback feeds this unconditionally at 60Hz, so this guard is what keeps
    // an unchanged edit lane from rebaking/repainting every tick. Nodes are expected pre-sorted
    // by x (ShapeManager's own contract).
    void setEditNodes (const std::vector<lflow::ShapeNode>& nodes);

    // Fired once per committed mouse-edit (add/move/bend/delete) with the full updated node
    // list; the editor wires this to ShapeManager::setNodes for the lane under edit.
    std::function<void (std::vector<lflow::ShapeNode>)> onNodesEdited;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct LaneState
    {
        // Live params driving what/where to draw.
        lflow::Waveform waveform { lflow::Waveform::Sine };
        float phaseOffset { 0.0f };
        bool active { false };
        float phase { 0.0f };
        float value { 0.0f };
        std::vector<lflow::ShapeNode> nodes; // only meaningful when waveform == Custom

        // Cached unit-square path + the params it was built from (compare-before-rebuild).
        juce::Path path;
        bool pathValid { false };
        lflow::Waveform pathWaveform { lflow::Waveform::Sine };
        float pathPhaseOffset { -1.0f };
    };

    void rebuildPathIfNeeded (LaneState&);

    // Edit-mode rendering: rebakes editTable from editNodes (called on every edit, cheap --
    // <=32 nodes into a 256-float table) and rebuilds editPath from editTable in unshifted
    // shape space -- no phase offset baked in, so it lines up with nodeToScreen/hit-testing
    // (called lazily from paint(), only when dirty).
    void rebakeEditTable();
    void rebuildEditPath();

    // Edit-mode hit-testing helpers, all in screen space (the component's own local bounds).
    juce::Rectangle<float> displayArea() const noexcept;
    juce::Point<float> nodeToScreen (const lflow::ShapeNode&) const noexcept;
    int findNodeNear (juce::Point<float> screenPos) const noexcept;     // node index or -1
    int findSegmentNear (juce::Point<float> screenPos) const noexcept;  // segment (node j) or -1
    float curveValueAt (float x) const noexcept;                        // editTable lookup

    LaneState lanes[kNumLanes];

    int editLane { -1 };
    std::vector<lflow::ShapeNode> editNodes;
    float editTable[lflow::kShapeTableSize] {};
    juce::Path editPath;
    bool editPathDirty { true };

    int dragNodeIndex { -1 };
    int dragSegmentIndex { -1 };
    float dragStartScreenY { 0.0f };
    float dragStartCurve { 0.0f };

    static constexpr float kNodeHitRadius = 8.0f;
    static constexpr float kSegmentHitTolerance = 8.0f;
    static constexpr float kNodeHandleSize = 6.0f;
};
