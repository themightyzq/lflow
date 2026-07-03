#pragma once
#include <JuceHeader.h>
#include "ShapeModel.h"
#include "TripleBuffer.h"
#include <vector>

// Message-thread owner of the SHAPES ValueTree subtree (per-lane, per-lane-drawn breakpoint
// node lists persisted alongside the rest of the plugin's APVTS state) and the bridge that
// bakes those node lists into the lock-free ShapeTableBuffer lookup tables consumed by the
// audio thread. JUCE is used freely here (ValueTree, listeners, XML round-trip) -- this is
// explicitly message/main-thread code, mirroring the rest of the JUCE-side plugin glue.
//
// THREADING: every public member function, and every juce::ValueTree::Listener callback, is
// message-thread only (the same thread APVTS itself is driven from: parameter changes,
// getStateInformation/setStateInformation, and UI edits). Nothing here is ever called from
// processBlock(). The audio thread only ever touches ShapeTableBuffer via its own acquire()
// (see TripleBuffer.h) -- ShapeManager is the sole producer/publisher for those buffers.
namespace lflow {

class ShapeManager : private juce::ValueTree::Listener
{
public:
    static constexpr int kNumLanes = 3;

    // `buffersIn` must point to an array of at least kNumLanes ShapeTableBuffer objects, owned
    // by (and outliving) the processor. `apvtsIn` must outlive this ShapeManager. Ensures the
    // SHAPES/SHAPE/NODE subtree exists (creating per-lane defaults if absent) and bakes +
    // publishes all 3 lanes' initial tables before returning, so the very first processBlock()
    // already has a real, valid table via hasEverPublished().
    ShapeManager (juce::AudioProcessorValueTreeState& apvtsIn, ShapeTableBuffer* buffersIn);
    ~ShapeManager() override;

    // Returns lane's current node list, read from the ValueTree and then run through the same
    // sanitize() pass as setNodes() (QA H2: this is the reload path -- setStateInformation of a
    // crafted/corrupt/old XML lands non-finite/out-of-range x/y/curve straight in the ValueTree
    // with no clamping of its own, and this used to be the ONE place that read those fields back
    // out unclamped, feeding a NaN curve into bakeShapeTable's pow() call). The returned vector
    // is always valid input for bakeShapeTable: x/y in [0,1], curve in [-1,1], size in
    // [2, kMaxShapeNodes]. Does NOT write the sanitized result back into the ValueTree (this
    // stays a pure/const read) -- bakeShapeTable's own defense-in-depth sanitization (Phase 7
    // Task 1) is the second layer if a caller somehow bypasses this one. Message thread only.
    std::vector<ShapeNode> getNodes (int lane) const;

    // Replaces lane's node list in the ValueTree. The remove-all + re-append mutation is done
    // with this ShapeManager's own listener temporarily detached (so the momentarily-empty
    // intermediate state is never observed/baked/published -- see .cpp), followed by exactly
    // one explicit rebake + publish of this lane. Validation is done by the shared sanitize()
    // helper (see .cpp) -- same rules getNodes() applies on read:
    //   - a node whose x or y is non-finite is DROPPED entirely (can't be placed anywhere
    //     meaningful on the [0,1] domain); a node whose curve alone is non-finite is KEPT with
    //     curve reset to 0 (linear/no bend -- the node's position is still well-defined);
    //   - surviving nodes' x/y clamped to [0,1], curve clamped to [-1,1];
    //   - sorted by ascending x (stable, so caller-supplied order of same-x nodes is kept);
    //   - capped at kMaxShapeNodes -- if more are supplied, the LAST (kMaxShapeNodes) after
    //     sorting are kept (i.e. nodes with the smallest x are dropped first), since losing
    //     nodes from the flat low end changes the shape less than losing nodes from the high
    //     end for a typically-left-to-right-drawn shape... in practice this only matters for
    //     malformed/oversized input; normal editing never exceeds the cap.
    //   - if fewer than 2 nodes survive (empty, single-node, or all-dropped input), the request
    //     is REJECTED and the default rise-fall triangle (0,0)(0.5,1)(1,0) is written instead --
    //     a shape needs at least 2 breakpoints to describe a segment at all, so rather than
    //     persist a degenerate table we fall back to something well-defined and audible.
    // Message thread only.
    void setNodes (int lane, const std::vector<ShapeNode>& nodes);

private:
    // Shared validation pass used by BOTH setNodes() (on write) and getNodes() (on read, QA H2)
    // so the reload path can never see anything setNodes() itself would have rejected. See
    // setNodes()'s doc comment above for the exact rules. Pure function, no ValueTree/JUCE
    // threading concerns -- easy to unit-test in isolation if ever pulled out of this JUCE-coupled
    // class.
    static std::vector<ShapeNode> sanitizeNodes (const std::vector<ShapeNode>& nodesIn);

    // juce::ValueTree::Listener overrides. All fire on the message thread. Note that
    // setNodes() and ensureShapesTree() detach this listener for the duration of their own
    // tree mutations (so intermediate/partially-populated states are never rebaked -- each
    // does exactly one explicit rebake instead once it's done); these callbacks now mainly
    // observe changes made elsewhere (e.g. JUCE calling apvts.replaceState() during
    // setStateInformation(), which fires valueTreeRedirected() below). On any change touching
    // our SHAPES subtree, every lane is rebaked + republished (see rebakeAndPublishAll();
    // cheap enough -- 3 x 256 floats -- to not bother diffing which lane actually changed).
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int oldIndex, int newIndex) override;
    void valueTreeRedirected (juce::ValueTree& treeWhichHasBeenChanged) override;

    // Locates (or creates, with per-lane defaults) the SHAPES child of apvts.state and its 3
    // SHAPE children. Safe to call repeatedly / after a redirect -- idempotent.
    void ensureShapesTree();

    // True if `tree` (or one of its ancestors, up to but excluding apvts.state itself) is part
    // of our SHAPES subtree -- i.e. tree's type is SHAPES, SHAPE, or NODE. Used to ignore the
    // (far more frequent) property-changed callbacks for ordinary parameter value nodes, since
    // ShapeManager's listener is registered on the whole apvts.state tree (required to observe
    // valueTreeRedirected, which only fires on the exact ValueTree instance reassigned by
    // AudioProcessorValueTreeState::replaceState()).
    static bool isShapeType (const juce::ValueTree& tree) noexcept;

    juce::ValueTree getShapeChild (int lane) const;
    static void writeDefaultNodes (juce::ValueTree& shapeChild);
    void rebakeAndPublish (int lane);
    void rebakeAndPublishAll();

    juce::AudioProcessorValueTreeState& apvts;
    ShapeTableBuffer* buffers;
    juce::ValueTree shapesTree;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeManager)
};

} // namespace lflow
