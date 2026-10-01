#pragma once
#include <juce_core/juce_core.h>

// User-preset folder location, name-clash lookup, and the one-time migration from the old
// location. Header-only and juce_core-only (no APVTS, no JuceHeader.h) so PresetFolderTests can
// exercise all of it against temp directories without linking the plugin.
namespace lflow::presetfolder {

constexpr auto kExtension = ".lflowpreset";

// Dropped into the new folder once a migration has run, so a preset the user later deletes there
// is not resurrected from the legacy folder on the next launch.
constexpr auto kMarkerFileName = ".migrated-from-legacy-location";

enum class Platform { Mac, Windows, Linux };

inline constexpr Platform currentPlatform() noexcept
{
   #if JUCE_MAC
    return Platform::Mac;
   #elif JUCE_WINDOWS
    return Platform::Windows;
   #else
    return Platform::Linux;
   #endif
}

// The house location on macOS (~/Library/Audio/Presets/ZQ SFX/LFlOw, the same family as
// Broken's), and each OS's conventional per-user application-data folder elsewhere: %APPDATA%
// on Windows, ~/.config (or $XDG_CONFIG_HOME) on Linux. `appData` is
// juce::File::userApplicationDataDirectory; both roots are parameters so tests can point at
// temp directories.
inline juce::File userPresetDir (Platform platform, const juce::File& home, const juce::File& appData)
{
    if (platform == Platform::Mac)
        return home.getChildFile ("Library/Audio/Presets/ZQ SFX/LFlOw");

    return appData.getChildFile ("ZQ SFX").getChildFile ("LFlOw");
}

// Where every build before the OS-aware fix kept user presets, on every OS. On macOS this is the
// current location, so nothing migrates there.
inline juce::File legacyUserPresetDir (const juce::File& home)
{
    return home.getChildFile ("Library/Audio/Presets/ZQ SFX/LFlOw");
}

// Returns the existing preset file in `dir` whose name (without extension) equals `name`, or an
// invalid File when there is none. Matching ignores case on case-insensitive filesystems
// (macOS, Windows), where "Foo" and "foo" are the same file on disk, and is exact on
// case-sensitive ones (Linux). `ignore` is skipped, so a rename can exclude the file being
// renamed. Only `*.lflowpreset` files take part: the marker file and anything else in the
// folder never clash.
inline juce::File findClashingPreset (const juce::File& dir, const juce::String& name,
                                      bool caseSensitive = juce::File::areFileNamesCaseSensitive(),
                                      const juce::File& ignore = {})
{
    for (const auto& existing : dir.findChildFiles (juce::File::findFiles, false,
                                                    juce::String ("*") + kExtension))
    {
        if (ignore != juce::File() && existing == ignore)
            continue;

        const auto stem = existing.getFileNameWithoutExtension();
        if (caseSensitive ? stem == name : stem.equalsIgnoreCase (name))
            return existing;
    }
    return {};
}

// Copies every preset in legacyDir that does not already exist (by file name) in newDir.
// Copies, never moves: the legacy folder is left untouched, so an older build keeps working and a
// failed copy loses nothing. A file already in newDir always wins and is never overwritten.
// Idempotent through the marker file, which is written only after every copy succeeded, so a
// partial failure is retried (just the missing files) on the next launch.
//
// Does nothing, and writes nothing, when legacyDir is absent (a fresh install, and every
// launch after a migration that removed it) or is the same folder as newDir (macOS). Returns the
// number of files copied.
inline int migrateLegacyUserPresets (const juce::File& legacyDir, const juce::File& newDir)
{
    if (legacyDir == newDir || ! legacyDir.isDirectory())
        return 0;

    const auto marker = newDir.getChildFile (kMarkerFileName);
    if (marker.existsAsFile())
        return 0;

    if (! newDir.createDirectory().wasOk())
        return 0;   // destination unusable: leave no marker, try again next launch

    int copied = 0;
    bool allOk = true;
    for (const auto& entry : legacyDir.findChildFiles (juce::File::findFiles, false,
                                                       juce::String ("*") + kExtension))
    {
        const auto target = newDir.getChildFile (entry.getFileName());
        if (target.exists())
            continue;

        if (entry.copyFileTo (target))
            ++copied;
        else
            allOk = false;
    }

    if (allOk)
        marker.replaceWithText ("Presets copied from " + legacyDir.getFullPathName() + "\n");

    return copied;
}

} // namespace lflow::presetfolder
