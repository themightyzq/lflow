#pragma once
#include <JuceHeader.h>
#include "../shapes/ShapeManager.h"
#include <vector>

// Phase 7 Task 4 (UX #2): in-plugin preset system -- 10 code-defined factory presets, user
// presets on disk (~/Library/Audio/Presets/ZQ SFX/LFlOw/*.lflowpreset = plain state XML), and
// two in-memory A/B compare slots. Owned by the processor (so A/B slots and the current preset
// name survive the editor being closed/reopened), driven entirely by the editor's preset bar.
//
// THREADING: message thread only, like ShapeManager -- every public member function here is
// called from UI handlers or the editor's timer. Nothing is ever touched from processBlock():
// preset application goes through setValueNotifyingHost()/ShapeManager::setNodes(), whose
// audio-thread handoff is APVTS's own atomics and the lock-free ShapeTableBuffer respectively.
//
// UNDO: a preset load (factory, user file, or A/B switch) is exactly ONE undo transaction
// ("Load preset <name>" / "Switch to A"/"Switch to B") on the processor's shared UndoManager.
// Parameters are applied via setValueNotifyingHost() and then flushed into the APVTS tree
// synchronously (see applyStateTree() in the .cpp) so every tree write lands inside that
// transaction; shape node lists go through ShapeManager::setNodes() with the same UndoManager.
// One Cmd-Z after a load therefore restores the complete prior state, shapes included.
//
// DIRTY FLAG: compare-based, not listener-based. A deep snapshot of the state tree is kept
// from the moment a preset is loaded/saved; isDirty() re-captures the live state (cheap --
// ~30 params + <=96 shape nodes) and deep-compares against that snapshot. This sidesteps the
// APVTS param->tree flush being timer-deferred (a listener would mis-flag the preset load
// itself as an edit, or miss edits until the flush), and it means undoing back to the loaded
// state clears the star again -- the flag reflects actual difference, not edit history. The
// editor polls it at a throttled rate (a few Hz), not per frame.
namespace lflow {

class PresetManager
{
public:
    static constexpr int kNumSlots = 2; // A and B

    // All three referenced objects must outlive this manager (they're processor members
    // constructed before it). Captures the initial state as the "Init" baseline for both the
    // dirty flag and the two A/B slots.
    PresetManager (juce::AudioProcessorValueTreeState& apvts,
                   ShapeManager& shapeManager,
                   juce::UndoManager& undoManager);
    ~PresetManager();

    // ---- Factory presets (defined in the .cpp as param-id -> value tables + an optional
    // lane-0 shape node list, applied over a full defaults baseline).
    static int getNumFactoryPresets() noexcept;
    static juce::String getFactoryPresetName (int index);
    void loadFactory (int index);

    // ---- User presets: state XML files named <name>.lflowpreset in getUserPresetDir().
    // The directory is created on demand (message-thread file I/O, per the Phase 7 design).
    static juce::File getUserPresetDir();
    static juce::Array<juce::File> getUserPresetFiles(); // sorted by filename, may be empty
    bool saveUserPreset (const juce::String& name);      // false on write failure
    bool loadUserPresetFile (const juce::File& file);    // false on parse failure

    // Result of renameUserPreset(), surfaced to the editor so its AlertWindow can show a
    // specific reason instead of a generic failure.
    enum class RenameOutcome
    {
        Success,
        EmptyName,     // blank/whitespace-only new name
        InvalidName,   // contains a path separator, or an illegal-for-filenames character
        NameClash,     // another *existing* user preset already has this name (case-insensitive)
        FileError      // the on-disk rename itself failed (permissions, file went missing, ...)
    };

    // Renames a user preset's file on disk (LFlOw's .lflowpreset format stores no name inside
    // the file -- see saveUserPreset() -- so the filename IS the name; nothing else needs
    // updating on disk). Validates non-empty, no path separators, and no case-insensitive name
    // clash with a DIFFERENT existing user preset (a pure case change on `file` itself is not a
    // clash and is applied). On success, also updates `currentName`/`slotNames` wherever they
    // still referenced the old name, so the displayed current-preset name and A/B slot labels
    // follow the rename. Does not rescan any cached list (there isn't one -- getUserPresetFiles()
    // always reads the directory live); the caller re-lists afterward if it needs to.
    RenameOutcome renameUserPreset (const juce::File& file, const juce::String& newName);

    // Steps through the combined factory + user preset list (factory first, then user files
    // in filename order), wrapping at both ends. If the current preset name isn't in the list
    // (e.g. "Init" or a deleted user preset), +1 starts at the first entry and -1 at the last.
    void loadNeighbour (int delta);

    // ---- A/B compare. Slot 0 = "A", slot 1 = "B"; both start as the initial state.
    // switchToSlot(other) captures the CURRENT live state into the active slot (so nothing is
    // lost), then applies the other slot as one undo transaction ("Switch to A"/"Switch to B").
    // Switching to the already-active slot is a no-op. copyActiveToOther() overwrites the
    // inactive slot with the current live state -- pure bookkeeping, no live-state change, so
    // it is deliberately NOT an undo transaction. Note undo of a switch restores the live
    // parameter/shape state but not the active-slot highlight itself (slot selection is UI
    // bookkeeping, not plugin state) -- documented trade-off, matches how hosts treat A/B.
    int  getActiveSlot() const noexcept { return activeSlot; }
    void switchToSlot (int slot);
    void copyActiveToOther();

    // ---- Preset bar display state.
    juce::String getCurrentPresetName() const { return currentName; } // "Init" before any load
    bool isDirty(); // non-const: forces the APVTS param->tree flush via copyState()

    // Deep snapshot of the full live state (params + SHAPES subtree). Public so the editor
    // could reuse it, and used internally for A/B slots, user-preset saves, and the dirty
    // snapshot. Forces the param->tree flush first (juce::AudioProcessorValueTreeState::
    // copyState() flushes before copying), so it is never stale.
    juce::ValueTree captureState();

private:
    // Applies `tree` (a PARAMS-rooted state snapshot; missing params fall back to their
    // defaults, missing/invalid SHAPES lanes fall back to the default triangle) as ONE undo
    // transaction named `transactionName`, then re-snapshots the dirty baseline and adopts
    // `presetName` as the current name. See the .cpp for the flush-ordering details.
    void applyStateTree (const juce::ValueTree& tree, const juce::String& presetName,
                         const juce::String& transactionName);

    juce::AudioProcessorValueTreeState& apvts;
    ShapeManager& shapes;
    juce::UndoManager& undoManager;

    juce::String currentName { "Init" };

    // Dirty baseline. Invalid (default-constructed) means "re-snapshot lazily on next
    // isDirty()" -- used both at construction and after a host state reload, where comparing
    // against a pre-reload snapshot would be meaningless. See isDirty().
    juce::ValueTree loadedSnapshot;

    juce::ValueTree slots[kNumSlots];
    juce::String slotNames[kNumSlots] { "Init", "Init" };
    int activeSlot { 0 };

    // Re-arms the lazy dirty baseline after a host state reload (apvts.replaceState() fires
    // valueTreeRedirected on listeners of apvts.state). Nested class rather than inheriting
    // ValueTree::Listener on PresetManager itself, to keep the public surface clean.
    struct RedirectWatcher : public juce::ValueTree::Listener
    {
        explicit RedirectWatcher (PresetManager& o) : owner (o) {}
        void valueTreeRedirected (juce::ValueTree&) override
        {
            // The loaded session state is the user's new baseline: forget the old snapshot
            // (lazily re-captured by the next isDirty() call, AFTER ShapeManager's own
            // redirect handler has re-ensured the SHAPES subtree -- listener order:
            // ShapeManager registers first in the processor ctor).
            owner.loadedSnapshot = juce::ValueTree();
        }
        PresetManager& owner;
    };
    RedirectWatcher redirectWatcher { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace lflow
