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

    // Phase 7 Task 6 (UX #9): while bypassed, every lane's curve + its live marker render at a
    // flat 40% alpha multiplier (on top of whatever alpha that lane would otherwise use --
    // active/inactive/edit-mode dimming all still apply underneath) and a small "BYPASSED" tag
    // is painted top-right. Markers keep animating (by design -- the processor keeps advancing
    // phase while bypassed, only the audio path is bypassed), so this is display-only, no
    // change to setLanePosition's feed. Compare-guarded: the editor's timerCallback calls this
    // every tick, so a no-op call here must not force a repaint.
    void setBypassed (bool bypassed);

    // Fired once per committed mouse-edit (add/move/bend/delete) with the full updated node
    // list; the editor wires this to ShapeManager::setNodes for the lane under edit.
    std::function<void (std::vector<lflow::ShapeNode>)> onNodesEdited;

    // Phase 7 Task 5 (UX #4): fired when the user picks "Reset curve to triangle" from the
    // right-click menu this component shows itself (mouseDown, edit mode only). The editor
    // wires this to a single undoable ShapeManager::setNodes() transaction -- this component
    // owns the menu (it's the thing being right-clicked) but not undo/ShapeManager access.
    std::function<void()> onResetCurveRequested;

    // Phase 7 Task 3 (UX #1): fired at the START of every edit-mode mouse-down gesture (add,
    // move-node, or bend-segment -- before any hit-testing/mutation happens), never on a plain
    // click/drag when no lane is being edited. The editor wires this to
    // undoManager.beginNewTransaction("Edit shape") so a WHOLE gesture -- possibly many
    // onNodesEdited calls, one per drag frame, until the next mouse-down -- collapses into a
    // single undo/redo step.
    std::function<void()> onGestureStart;

    void paint (juce::Graphics&) override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

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

    // Screen-space center of segment j's bend handle (midpoint between node j and j+1, y =
    // the curve's own evaluated value there -- so the diamond always sits ON the curve).
    juce::Point<float> handleScreenPos (size_t segmentIndex) const noexcept;

    // Phase 7 Task 5 (UX #3): replaces the old "click anywhere near the segment line"
    // hit-test (findSegmentNear) that made spawning a spike-causing node almost unavoidable
    // when trying to bend a segment. Now ONLY the explicit midpoint diamond (kNodeHitRadius,
    // same 8px as a node) is grabbable -- everything else on/near the curve line falls
    // through to "empty space -> add a node" in mouseDown, per the spec's documented hit
    // order (node > handle > empty).
    int findHandleNear (juce::Point<float> screenPos) const noexcept;  // segment (node j) or -1
    float curveValueAt (float x) const noexcept;                        // editTable lookup

    // Shows the edit-mode right-click menu ("Reset curve to triangle") and fires
    // onResetCurveRequested on selection. Owns menu presentation only -- see that
    // callback's doc comment for why the actual reset lives in the editor.
    void showResetCurveMenu();

    LaneState lanes[kNumLanes];

    int editLane { -1 };
    std::vector<lflow::ShapeNode> editNodes;
    float editTable[lflow::kShapeTableSize] {};
    juce::Path editPath;
    bool editPathDirty { true };

    // Phase 7 Task 6 (UX #9): see setBypassed's doc comment above.
    bool bypassed { false };

    int dragNodeIndex { -1 };
    int dragSegmentIndex { -1 };
    float dragStartScreenY { 0.0f };
    float dragStartCurve { 0.0f };

    // Phase 7 Task 5 (UX #3): which segment's bend-handle diamond the mouse is currently
    // hovering (paint()'s cue for hollow-vs-filled -- see the header comment on
    // findHandleNear). -1 = none. Updated from mouseMove/mouseExit, not mouseDrag, so it
    // still reflects true hover after a drag ends.
    int hoverHandleIndex { -1 };

    static constexpr float kNodeHitRadius = 8.0f;
    static constexpr float kHandleHitRadius = 8.0f;
    static constexpr float kNodeHandleSize = 6.0f;
    static constexpr float kSegmentHandleSize = 7.0f; // diamond "radius" (corner-to-center), per spec
};
