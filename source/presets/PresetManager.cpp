#include "PresetManager.h"

namespace lflow {

namespace {

// APVTS state-tree identifiers. These mirror juce::AudioProcessorValueTreeState's own private
// `valueType`/`valuePropertyID`/`idPropertyID` constants ("PARAM"/"value"/"id" -- fixed since
// JUCE 5 and load-bearing for every saved LFlOw session, so they can't drift), and
// ShapeManager.cpp's SHAPES subtree layout (documented at the top of that file).
const juce::Identifier kParamType { "PARAM" };
const juce::Identifier kIdProp    { "id" };
const juce::Identifier kValueProp { "value" };

const juce::Identifier kShapesType { "SHAPES" };
const juce::Identifier kShapeType  { "SHAPE" };
const juce::Identifier kNodeType   { "NODE" };
const juce::Identifier kLaneProp   { "lane" };
const juce::Identifier kXProp      { "x" };
const juce::Identifier kYProp      { "y" };
const juce::Identifier kCurveProp  { "curve" };

// ---- Factory preset definitions -----------------------------------------------------------
//
// Each factory preset is a small param-id -> plain (denormalised) value table applied over a
// full defaults baseline: every parameter NOT listed here is reset to its ParameterLayout
// default (lane 1 Sine/1 Hz/Volume/50%, lanes 2-3 depth 0, Link ON, Mix 100%, Smooth 15%,
// xovers 250/2500 Hz, Bypass off), and every lane's shape is reset to the default triangle
// unless the preset defines its own nodes. Choice/bool params use their INDEX/0-1 as the
// value (Waveform: Sine 0, Triangle 1, Square 2, Saw Up 3, Saw Down 4, S&H 5, Custom 6;
// Division: 1/1..1/32 = 0..5; Rhythm: Straight 0, Dotted 1, Triplet 2; Dest: Volume 0,
// Pan 1, Low 2, Mid 3, High 4, Pitch 5).
//
// All ten drive LANE 1 with depth > 0 and leave Link at its ON default, so every preset makes
// sound (or motion) out of the box with lanes 2-3 silent followers -- one lane, one idea.
struct FactoryParam
{
    const char* paramId;
    float value;
};

struct FactoryPreset
{
    const char* name;
    std::vector<FactoryParam> params;
    std::vector<ShapeNode> lane0Nodes; // empty = default triangle
};

const std::vector<FactoryPreset>& factoryPresets()
{
    // "Drawn Stutter"'s lane-1 shape: an uneven three-burst gate pattern over one cycle --
    // the kind of shape the drawable editor exists for, undrawable with the stock waveforms.
    static const std::vector<ShapeNode> stutterNodes = {
        { 0.00f, 1.0f, 0.0f }, { 0.20f, 1.0f, 0.0f }, { 0.22f, 0.0f, 0.0f },
        { 0.30f, 0.0f, 0.0f }, { 0.32f, 1.0f, 0.0f }, { 0.45f, 1.0f, 0.0f },
        { 0.47f, 0.0f, 0.0f }, { 0.55f, 0.0f, 0.0f }, { 0.57f, 1.0f, 0.0f },
        { 0.70f, 1.0f, 0.0f }, { 0.72f, 0.0f, 0.0f }, { 0.90f, 0.0f, 0.0f },
        { 0.92f, 1.0f, 0.0f }, { 1.00f, 1.0f, 0.0f },
    };

    static const std::vector<FactoryPreset> presets = {
        { "Gentle Tremolo",
          { { "l1RateHz", 2.5f }, { "l1Depth", 0.35f }, { "smooth", 0.35f } }, {} },

        { "Classic Chopper 1/8",
          { { "l1Waveform", 2.0f /* Square */ }, { "l1Sync", 1.0f }, { "l1Division", 3.0f /* 1/8 */ },
            { "l1Depth", 0.9f }, { "smooth", 0.10f } }, {} },

        { "Auto-Pan Slow",
          { { "l1Dest", 1.0f /* Pan */ }, { "l1RateHz", 0.25f }, { "l1Depth", 0.85f },
            { "smooth", 0.25f } }, {} },

        { "Spectral Pulse (Highs)",
          { { "l1Dest", 4.0f /* High */ }, { "l1Waveform", 2.0f /* Square */ }, { "l1Sync", 1.0f },
            { "l1Division", 4.0f /* 1/16 */ }, { "l1Depth", 0.85f }, { "xoverHigh", 3000.0f },
            { "smooth", 0.12f } }, {} },

        { "Sub Swell (Lows)",
          { { "l1Dest", 2.0f /* Low */ }, { "l1Sync", 1.0f }, { "l1Division", 0.0f /* 1/1 */ },
            { "l1Depth", 0.75f }, { "xoverLow", 120.0f }, { "smooth", 0.40f } }, {} },

        { "Tape Warble",
          { { "l1Dest", 5.0f /* Pitch */ }, { "l1RateHz", 1.2f }, { "l1Depth", 0.18f },
            { "smooth", 0.30f } }, {} },

        { "Vibrato Light",
          { { "l1Dest", 5.0f /* Pitch */ }, { "l1RateHz", 5.5f }, { "l1Depth", 0.12f },
            { "smooth", 0.20f } }, {} },

        { "S&H Filterish",
          { { "l1Waveform", 5.0f /* S&H */ }, { "l1Dest", 3.0f /* Mid */ }, { "l1Sync", 1.0f },
            { "l1Division", 3.0f /* 1/8 */ }, { "l1Depth", 0.8f }, { "smooth", 0.25f } }, {} },

        { "Drawn Stutter",
          { { "l1Waveform", 6.0f /* Custom */ }, { "l1Sync", 1.0f }, { "l1Division", 2.0f /* 1/4 */ },
            { "l1Depth", 1.0f }, { "smooth", 0.05f } }, stutterNodes },

        { "Triplet Pump",
          { { "l1Waveform", 3.0f /* Saw Up */ }, { "l1Sync", 1.0f }, { "l1Division", 2.0f /* 1/4 */ },
            { "l1Rhythm", 2.0f /* Triplet */ }, { "l1Depth", 0.85f }, { "smooth", 0.30f } }, {} },
    };
    return presets;
}

// Builds a PARAMS-rooted snapshot tree from a factory table: PARAM children for the listed
// overrides only (applyStateTree() falls back to parameter defaults for everything absent)
// plus a SHAPES subtree for lane 0 when the preset defines nodes (absent lanes fall back to
// the default triangle in applyStateTree()).
juce::ValueTree buildFactoryTree (const FactoryPreset& preset)
{
    juce::ValueTree tree ("PARAMS");

    for (const auto& p : preset.params)
    {
        juce::ValueTree param (kParamType);
        param.setProperty (kIdProp, juce::String (p.paramId), nullptr);
        param.setProperty (kValueProp, (double) p.value, nullptr);
        tree.appendChild (param, nullptr);
    }

    if (! preset.lane0Nodes.empty())
    {
        juce::ValueTree shapes (kShapesType);
        juce::ValueTree shape (kShapeType);
        shape.setProperty (kLaneProp, 0, nullptr);
        for (const auto& n : preset.lane0Nodes)
        {
            juce::ValueTree node (kNodeType);
            node.setProperty (kXProp, (double) n.x, nullptr);
            node.setProperty (kYProp, (double) n.y, nullptr);
            node.setProperty (kCurveProp, (double) n.curve, nullptr);
            shape.appendChild (node, nullptr);
        }
        shapes.appendChild (shape, nullptr);
        tree.appendChild (shapes, nullptr);
    }

    return tree;
}

// Extracts lane `lane`'s node list from a snapshot tree's SHAPES subtree. Returns an empty
// vector when the tree/subtree/lane is absent -- ShapeManager::setNodes() maps that to the
// default triangle (its documented <2-nodes fallback), which is exactly the "reset to
// baseline" behaviour a full-state preset load wants.
std::vector<ShapeNode> nodesForLane (const juce::ValueTree& tree, int lane)
{
    std::vector<ShapeNode> nodes;
    if (! tree.isValid())
        return nodes;

    auto shapes = tree.getChildWithName (kShapesType);
    if (! shapes.isValid())
        return nodes;

    for (int i = 0; i < shapes.getNumChildren(); ++i)
    {
        auto shape = shapes.getChild (i);
        if (! shape.hasType (kShapeType) || (int) shape.getProperty (kLaneProp, -1) != lane)
            continue;

        for (int j = 0; j < shape.getNumChildren(); ++j)
        {
            auto n = shape.getChild (j);
            if (! n.hasType (kNodeType))
                continue;
            nodes.push_back ({ (float) (double) n.getProperty (kXProp, 0.0),
                               (float) (double) n.getProperty (kYProp, 0.0),
                               (float) (double) n.getProperty (kCurveProp, 0.0) });
        }
        break;
    }
    return nodes;
}

} // namespace

// ---- PresetManager -------------------------------------------------------------------------

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& apvtsIn,
                              ShapeManager& shapeManagerIn,
                              juce::UndoManager& undoManagerIn)
    : apvts (apvtsIn), shapes (shapeManagerIn), undoManager (undoManagerIn)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Both A/B slots start as the construction-time state (the ParameterLayout defaults, in
    // the normal case -- a host session restore arrives later via setStateInformation and is
    // deliberately NOT folded back into the slots: A/B is a live comparison tool, not a
    // persistence mechanism). loadedSnapshot stays invalid: isDirty() lazily captures the
    // baseline on first call, which also transparently covers the post-restore case (see
    // RedirectWatcher).
    slots[0] = captureState();
    slots[1] = slots[0].createCopy();

    apvts.state.addListener (&redirectWatcher);
}

PresetManager::~PresetManager()
{
    apvts.state.removeListener (&redirectWatcher);
}

int PresetManager::getNumFactoryPresets() noexcept
{
    return (int) factoryPresets().size();
}

juce::String PresetManager::getFactoryPresetName (int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return {};
    return presets[(size_t) index].name;
}

void PresetManager::loadFactory (int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= (int) presets.size())
        return;

    const auto& preset = presets[(size_t) index];
    applyStateTree (buildFactoryTree (preset), preset.name,
                    "Load preset " + juce::String (preset.name));
}

juce::File PresetManager::getUserPresetDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/Audio/Presets/ZQ SFX/LFlOw");
    if (! dir.isDirectory())
        dir.createDirectory(); // on-demand; failure surfaces as an empty list / failed save
    return dir;
}

juce::Array<juce::File> PresetManager::getUserPresetFiles()
{
    auto files = getUserPresetDir().findChildFiles (juce::File::findFiles, false, "*.lflowpreset");
    files.sort();
    return files;
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    const auto legal = juce::File::createLegalFileName (name.trim());
    if (legal.isEmpty())
        return false;

    auto xml = captureState().createXml();
    if (xml == nullptr)
        return false;

    const auto file = getUserPresetDir().getChildFile (legal + ".lflowpreset");
    if (! xml->writeTo (file))
        return false;

    // Saving adopts the preset identity: name shown un-starred, and the just-saved state
    // becomes the dirty baseline (it IS the file's content, by construction).
    currentName = legal;
    loadedSnapshot = captureState();
    slotNames[activeSlot] = currentName;
    return true;
}

bool PresetManager::loadUserPresetFile (const juce::File& file)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return false;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return false;

    const auto name = file.getFileNameWithoutExtension();
    applyStateTree (tree, name, "Load preset " + name);
    return true;
}

void PresetManager::loadNeighbour (int delta)
{
    // Combined ordered list: factory presets first, then user preset files (filename order).
    const int numFactory = getNumFactoryPresets();
    const auto userFiles = getUserPresetFiles();
    const int total = numFactory + userFiles.size();
    if (total == 0)
        return;

    int current = -1;
    for (int i = 0; i < numFactory; ++i)
        if (getFactoryPresetName (i) == currentName)
        {
            current = i;
            break;
        }
    if (current < 0)
        for (int i = 0; i < userFiles.size(); ++i)
            if (userFiles[i].getFileNameWithoutExtension() == currentName)
            {
                current = numFactory + i;
                break;
            }

    // Unknown current name ("Init", or a since-deleted user preset): step INTO the list from
    // its edge rather than skipping an entry -- next lands on the first preset, prev on the last.
    const int target = current < 0
                           ? (delta >= 0 ? 0 : total - 1)
                           : ((current + delta) % total + total) % total;

    if (target < numFactory)
        loadFactory (target);
    else
        loadUserPresetFile (userFiles[target - numFactory]);
}

void PresetManager::switchToSlot (int slot)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (slot == activeSlot || slot < 0 || slot >= kNumSlots)
        return;

    // Nothing is lost on a switch: the CURRENT live state (including unsaved tweaks) is
    // captured into the slot being left before the other slot is recalled.
    slots[activeSlot] = captureState();
    slotNames[activeSlot] = currentName;

    activeSlot = slot;
    applyStateTree (slots[slot], slotNames[slot],
                    juce::String ("Switch to ") + (slot == 0 ? "A" : "B"));
}

void PresetManager::copyActiveToOther()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    const int other = 1 - activeSlot;
    slots[other] = captureState();
    slotNames[other] = currentName;
    // No live-state change and no undo transaction -- this only overwrites the inactive
    // in-memory slot (see the header doc comment).
}

juce::ValueTree PresetManager::captureState()
{
    // copyState() flushes any pending param->tree updates before deep-copying, so this
    // snapshot (params + SHAPES subtree, which lives inside apvts.state) is never stale.
    return apvts.copyState();
}

bool PresetManager::isDirty()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Lazy baseline: first call after construction or after a host state reload (see
    // RedirectWatcher) adopts the CURRENT state as "clean". This is what makes a freshly
    // restored session read "Init" instead of "Init*".
    if (! loadedSnapshot.isValid())
    {
        loadedSnapshot = captureState();
        return false;
    }

    // Deep compare against the snapshot. captureState() forces the APVTS param->tree flush,
    // so an edit shows up here at the editor's poll rate rather than whenever APVTS's own
    // deferred flush timer would have run. Cheap: ~30 PARAM children + <=96 shape nodes.
    // Bonus of compare-over-history: undoing back to the loaded state clears the star.
    return ! captureState().isEquivalentTo (loadedSnapshot);
}

void PresetManager::applyStateTree (const juce::ValueTree& tree, const juce::String& presetName,
                                    const juce::String& transactionName)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    // ONE undo transaction for the whole load (params + shapes) -- the spec's "preset load =
    // one undo transaction". Everything below funnels its undoable writes into this.
    undoManager.beginNewTransaction (transactionName);

    // Parameters: every parameter is set (absent-from-tree ones to their default), so a preset
    // is always a FULL state snapshot -- no leakage from whatever was loaded before. Values go
    // through setValueNotifyingHost (bracketed by a gesture, host-etiquette for a programmatic
    // jump) which updates the audio-thread atomics immediately; the APVTS param->tree flush is
    // timer-DEFERRED, though, so it is forced synchronously right after the loop (below) to
    // guarantee the resulting undoable tree writes land inside THIS transaction and the
    // snapshot taken at the end isn't stale.
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr)
            continue; // APVTS-managed plugins only have RangedAudioParameters; belt-and-braces

        float value = rp->convertFrom0to1 (rp->getDefaultValue());

        if (tree.isValid())
            for (const auto& child : tree)
                if (child.hasType (kParamType)
                    && child.getProperty (kIdProp).toString() == rp->paramID)
                {
                    if (child.hasProperty (kValueProp))
                        value = (float) (double) child.getProperty (kValueProp);
                    break;
                }

        rp->beginChangeGesture();
        rp->setValueNotifyingHost (rp->convertTo0to1 (value));
        rp->endChangeGesture();
    }

    // Force the param->tree flush NOW, inside this transaction. copyState() is the one public
    // APVTS entry point that calls the (private) flushParameterValuesToValueTree() -- the
    // returned copy is discarded; the flush's undoable setProperty calls are the point.
    (void) captureState();

    // Shapes: full-state semantics for all lanes -- lanes absent from the tree get an empty
    // node list, which ShapeManager::setNodes() maps to its default triangle. Same shared
    // UndoManager, same (still-open) transaction.
    for (int lane = 0; lane < ShapeManager::kNumLanes; ++lane)
        shapes.setNodes (lane, nodesForLane (tree, lane), &undoManager);

    currentName = presetName;
    slotNames[activeSlot] = presetName;
    loadedSnapshot = captureState();
}

} // namespace lflow
