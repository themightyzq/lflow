#include "ShapeManager.h"
#include "MultiLaneEngine.h"
#include <algorithm>
#include <cmath>

// ValueTree layout (persisted, part of apvts.state -> XML round-trip via get/setStateInformation):
//   <SHAPES>
//     <SHAPE lane="0"> <NODE x="0.0" y="0.0" curve="0.0"/> <NODE .../> ... </SHAPE>
//     <SHAPE lane="1"> ... </SHAPE>
//     <SHAPE lane="2"> ... </SHAPE>
//   </SHAPES>
namespace lflow {

// Folded Phase 4 review hardening item (picked up in Phase 5 Task 2): ShapeManager's
// per-lane node/table bookkeeping and MultiLaneEngine's lane array are two independently
// hardcoded "3 lanes" -- this file is the one place that already includes both headers,
// so it's where a future change to either constant without the other gets caught at
// compile time rather than silently drifting (e.g. a 4th UI lane with no engine lane
// behind it, or vice versa).
static_assert (ShapeManager::kNumLanes == MultiLaneEngine::kNumLanes,
               "ShapeManager and MultiLaneEngine must agree on the number of lanes");

namespace {
const juce::Identifier kShapesType { "SHAPES" };
const juce::Identifier kShapeType  { "SHAPE" };
const juce::Identifier kNodeType   { "NODE" };
const juce::Identifier kLaneProp   { "lane" };
const juce::Identifier kXProp      { "x" };
const juce::Identifier kYProp      { "y" };
const juce::Identifier kCurveProp  { "curve" };
} // namespace

ShapeManager::ShapeManager (juce::AudioProcessorValueTreeState& apvtsIn, ShapeTableBuffer* buffersIn)
    : apvts (apvtsIn), buffers (buffersIn)
{
    jassert (buffers != nullptr);
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Registered on the ROOT state tree (not just the SHAPES child) so that
    // valueTreeRedirected -- which only fires on the exact ValueTree instance that
    // AudioProcessorValueTreeState::replaceState() reassigns (i.e. apvts.state itself) -- is
    // observed. Property/child-add/remove events for the (far more numerous) ordinary
    // parameter nodes elsewhere in the tree also reach us this way (JUCE ValueTree listener
    // notifications bubble up through every ancestor); isShapeType() filters those out cheaply
    // so we don't rebake on every knob tweak.
    apvts.state.addListener (this);

    // ensureShapesTree() rebakes + republishes all lanes itself once the subtree is fully
    // consistent (see its definition below), so no separate explicit call is needed here.
    ensureShapesTree();
}

ShapeManager::~ShapeManager()
{
    apvts.state.removeListener (this);
}

void ShapeManager::ensureShapesTree()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Suppress our own listener for the duration of the (possibly multi-step: create SHAPES,
    // create up to 3 SHAPE children, append default NODE children) mutation below, so no
    // intermediate/partially-populated state ever reaches rebakeAndPublish() -- same defect
    // this guards against as in setNodes() (see its comment). One explicit rebake of every
    // lane happens below, once the subtree is fully consistent -- this also covers the
    // valueTreeRedirected() call path (state reload), so that override doesn't need its own
    // explicit rebake.
    //
    // Phase 7 Task 3 (UX #1): every ValueTree op in this method passes nullptr for its
    // UndoManager*, deliberately and unconditionally -- factory tree creation/repair must NEVER
    // be undoable, or a user hammering Cmd-Z past their own edits could walk undo history back
    // into deleting the SHAPES subtree entirely (leaving the audio thread's shape tables
    // permanently orphaned). setNodes() is the one place user edits become undoable (see its
    // doc comment) -- this method never accepts an UndoManager parameter at all, so there is no
    // call-site that could get this wrong.
    apvts.state.removeListener (this);

    auto& root = apvts.state;
    auto shapes = root.getChildWithName (kShapesType);
    if (! shapes.isValid())
    {
        shapes = juce::ValueTree (kShapesType);
        root.appendChild (shapes, nullptr);
    }

    for (int lane = 0; lane < kNumLanes; ++lane)
    {
        juce::ValueTree shape;
        for (int i = 0; i < shapes.getNumChildren(); ++i)
        {
            auto c = shapes.getChild (i);
            if (c.hasType (kShapeType) && (int) c.getProperty (kLaneProp, -1) == lane)
            {
                shape = c;
                break;
            }
        }

        if (! shape.isValid())
        {
            shape = juce::ValueTree (kShapeType);
            shape.setProperty (kLaneProp, lane, nullptr);
            shapes.appendChild (shape, nullptr);
        }

        if (shape.getNumChildren() == 0)
            writeDefaultNodes (shape);
    }

    shapesTree = shapes;

    apvts.state.addListener (this);

    rebakeAndPublishAll();
}

void ShapeManager::writeDefaultNodes (juce::ValueTree& shapeChild)
{
    // Default rise-fall triangle (0,0)(0.5,1)(1,0) -- visible, audible, obviously editable
    // (Phase 4 design decision).
    const double xs[3]    = { 0.0, 0.5, 1.0 };
    const double ys[3]    = { 0.0, 1.0, 0.0 };

    for (int i = 0; i < 3; ++i)
    {
        juce::ValueTree n (kNodeType);
        n.setProperty (kXProp, xs[i], nullptr);
        n.setProperty (kYProp, ys[i], nullptr);
        n.setProperty (kCurveProp, 0.0, nullptr);
        shapeChild.appendChild (n, nullptr);
    }
}

bool ShapeManager::isShapeType (const juce::ValueTree& tree) noexcept
{
    return tree.hasType (kShapesType) || tree.hasType (kShapeType) || tree.hasType (kNodeType);
}

juce::ValueTree ShapeManager::getShapeChild (int lane) const
{
    for (int i = 0; i < shapesTree.getNumChildren(); ++i)
    {
        auto c = shapesTree.getChild (i);
        if (c.hasType (kShapeType) && (int) c.getProperty (kLaneProp, -1) == lane)
            return c;
    }
    return {};
}

std::vector<ShapeNode> ShapeManager::sanitizeNodes (const std::vector<ShapeNode>& nodesIn)
{
    std::vector<ShapeNode> nodes;
    nodes.reserve (nodesIn.size());

    for (const auto& raw : nodesIn)
    {
        // A non-finite x or y can't be placed anywhere meaningful on the [0,1] domain (and
        // would corrupt the ascending-x sort below), so that node is dropped entirely rather
        // than coerced to some arbitrary in-range value. A non-finite curve, by contrast,
        // leaves the node's position (x,y) perfectly well-defined -- only the bend of the
        // segment following it is unknown -- so that node is KEPT with curve reset to 0.0f
        // (linear, i.e. no bend) instead of being discarded.
        if (! std::isfinite (raw.x) || ! std::isfinite (raw.y))
            continue;

        ShapeNode n;
        n.x     = juce::jlimit (0.0f, 1.0f, raw.x);
        n.y     = juce::jlimit (0.0f, 1.0f, raw.y);
        n.curve = std::isfinite (raw.curve) ? juce::jlimit (-1.0f, 1.0f, raw.curve) : 0.0f;
        nodes.push_back (n);
    }

    // Stable sort keeps caller-supplied relative order for equal-x nodes (harmless -- the
    // baker collapses zero-width segments -- but deterministic order is friendlier to test).
    std::stable_sort (nodes.begin(), nodes.end(),
                       [] (const ShapeNode& a, const ShapeNode& b) { return a.x < b.x; });

    // Cap at kMaxShapeNodes: keep the LAST kMaxShapeNodes after sorting (drop from the low-x
    // end first). Only matters for malformed/oversized input -- normal editing never nears
    // the cap.
    if ((int) nodes.size() > kMaxShapeNodes)
        nodes.erase (nodes.begin(), nodes.end() - kMaxShapeNodes);

    // Fewer than 2 surviving nodes (empty, single-node, or every node dropped above for a
    // non-finite x/y) can't describe a segment -- reject and fall back to the documented
    // default triangle rather than persist/bake a degenerate shape.
    if (nodes.size() < 2)
    {
        nodes = { ShapeNode { 0.0f, 0.0f, 0.0f },
                  ShapeNode { 0.5f, 1.0f, 0.0f },
                  ShapeNode { 1.0f, 0.0f, 0.0f } };
    }

    return nodes;
}

std::vector<ShapeNode> ShapeManager::getNodes (int lane) const
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    std::vector<ShapeNode> result;
    if (lane < 0 || lane >= kNumLanes)
        return result;

    auto shape = getShapeChild (lane);
    if (! shape.isValid())
        return result;

    std::vector<ShapeNode> raw;
    raw.reserve ((size_t) shape.getNumChildren());
    for (int i = 0; i < shape.getNumChildren(); ++i)
    {
        auto n = shape.getChild (i);
        if (! n.hasType (kNodeType))
            continue;

        ShapeNode node;
        node.x     = (float) (double) n.getProperty (kXProp, 0.0);
        node.y     = (float) (double) n.getProperty (kYProp, 0.0);
        node.curve = (float) (double) n.getProperty (kCurveProp, 0.0);
        raw.push_back (node);
    }

    // QA H2: sanitize on the way OUT, identically to setNodes()'s sanitize on the way IN --
    // this is the reload path (setStateInformation -> valueTreeRedirected -> ensureShapesTree
    // -> rebakeAndPublishAll -> getNodes), and the ValueTree may hold whatever a crafted,
    // corrupt, or pre-hardening XML wrote into it with no validation of its own.
    return sanitizeNodes (raw);
}

void ShapeManager::setNodes (int lane, const std::vector<ShapeNode>& nodesIn, juce::UndoManager* undoManagerToUse)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (lane < 0 || lane >= kNumLanes)
        return;

    const std::vector<ShapeNode> nodes = sanitizeNodes (nodesIn);

    auto shape = getShapeChild (lane);
    if (! shape.isValid())
    {
        jassertfalse; // ensureShapesTree() should have guaranteed this exists
        return;
    }

    // Suppress our own listener across the remove-all + re-append below. Without this,
    // removeAllChildren() alone would fire our valueTreeChildRemoved callback with the SHAPE
    // temporarily empty, causing rebakeAndPublish() to publish an all-zero (silent) table to
    // the live, audio-thread-visible ShapeTableBuffer for that lane -- audible as a glitch to
    // silence if transport is running while a node list is edited (and setNodes() is called
    // once per drag-frame by the editor). Re-enable the listener once the tree is back in a
    // consistent state, then rebake + publish exactly once, explicitly.
    //
    // undoManagerToUse is threaded through the two STRUCTURAL ops (removeAllChildren/
    // appendChild) so this edit becomes one or more UndoableActions on the caller's transaction
    // (see the header doc comment) -- undoing/redoing a node add/remove replays OUTSIDE this
    // bracket (it happens later, from a completely separate call stack when the user hits
    // Cmd-Z), so our listener is attached as normal at that point and rebakeAndPublishAll()
    // still runs, which is what makes the undo audible/visible. The freshly-built nodeTree's
    // own property sets stay on nullptr: the node isn't attached to anything yet when they run,
    // so there's nothing meaningful for an undo action to reference -- appendChild() (with
    // undoManagerToUse) is what makes undo/redo restore the WHOLE node, properties included.
    apvts.state.removeListener (this);
    shape.removeAllChildren (undoManagerToUse);
    for (auto& n : nodes)
    {
        juce::ValueTree nodeTree (kNodeType);
        nodeTree.setProperty (kXProp, (double) n.x, nullptr);
        nodeTree.setProperty (kYProp, (double) n.y, nullptr);
        nodeTree.setProperty (kCurveProp, (double) n.curve, nullptr);
        shape.appendChild (nodeTree, undoManagerToUse);
    }
    apvts.state.addListener (this);

    rebakeAndPublish (lane);
}

void ShapeManager::rebakeAndPublish (int lane)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (lane < 0 || lane >= kNumLanes)
        return;

    const auto nodes = getNodes (lane);

    ShapeNode arr[kMaxShapeNodes];
    const int n = juce::jmin ((int) nodes.size(), kMaxShapeNodes);
    for (int i = 0; i < n; ++i)
        arr[i] = nodes[(size_t) i];

    auto& buf = buffers[lane];
    bakeShapeTable (n > 0 ? arr : nullptr, n, buf.writeBuffer().data, kShapeTableSize);
    buf.publish();
}

void ShapeManager::rebakeAndPublishAll()
{
    for (int lane = 0; lane < kNumLanes; ++lane)
        rebakeAndPublish (lane);
}

void ShapeManager::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (isShapeType (tree))
        rebakeAndPublishAll();
}

void ShapeManager::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (isShapeType (parent) || isShapeType (child))
        rebakeAndPublishAll();
}

void ShapeManager::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (isShapeType (parent) || isShapeType (child))
        rebakeAndPublishAll();
}

void ShapeManager::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (isShapeType (parent))
        rebakeAndPublishAll();
}

void ShapeManager::valueTreeRedirected (juce::ValueTree&)
{
    // apvts.state has just been reassigned (setStateInformation -> apvts.replaceState()) to
    // point at a freshly-loaded tree. Re-locate (or create, if the loaded state predates
    // Phase 4 / omits SHAPES) our subtree in the NEW tree; ensureShapesTree() rebakes +
    // republishes every lane itself once that's done, so the audio thread picks up the loaded
    // shapes on the next processBlock() without a separate explicit call here.
    ensureShapesTree();
}

} // namespace lflow
