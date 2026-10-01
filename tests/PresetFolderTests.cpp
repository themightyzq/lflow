#include <catch2/catch_test_macros.hpp>

#include "PresetFolder.h"

#include <juce_core/juce_core.h>

using namespace lflow::presetfolder;

// Coverage for the user-preset folder work from the 2026-09-29 portfolio review (item 18):
//   - Save As must detect a name clash before it can overwrite a user's preset;
//   - the preset folder is the house location on macOS and the conventional per-user data
//     folder on Windows and Linux;
//   - presets saved at the old location are copied (never moved or overwritten) to the new one.
// Everything runs against temp directories; the real per-user folders are never touched.

namespace
{
    struct TempRoot
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("lflow_preset_folder_test_" + juce::Uuid().toString());

        TempRoot() { dir.deleteRecursively(); dir.createDirectory(); }
        ~TempRoot() { dir.deleteRecursively(); }
    };

    juce::File makePreset (const juce::File& dir, const juce::String& name, const juce::String& body)
    {
        dir.createDirectory();
        auto f = dir.getChildFile (name + kExtension);
        REQUIRE (f.replaceWithText (body));
        return f;
    }

    juce::String contentsOf (const juce::File& dir, const juce::String& name)
    {
        return dir.getChildFile (name + kExtension).loadFileAsString();
    }
}

// ---- name-clash detection -----------------------------------------------------------------

TEST_CASE ("clash: an existing preset with the same name is found", "[presets][clash]")
{
    TempRoot root;
    const auto mine = makePreset (root.dir, "My Preset", "x");

    REQUIRE (findClashingPreset (root.dir, "My Preset", true) == mine);
    REQUIRE (findClashingPreset (root.dir, "My Preset", false) == mine);
}

TEST_CASE ("clash: a name that differs only in case clashes on a case-insensitive filesystem",
           "[presets][clash]")
{
    TempRoot root;
    const auto mine = makePreset (root.dir, "Warm Pad", "x");

    REQUIRE (findClashingPreset (root.dir, "warm pad", false) == mine);
    REQUIRE (findClashingPreset (root.dir, "WARM PAD", false) == mine);

    // Case-sensitive filesystem (Linux): a differently-cased name is a different file.
    REQUIRE (findClashingPreset (root.dir, "warm pad", true) == juce::File());
}

TEST_CASE ("clash: the default follows the real filesystem", "[presets][clash]")
{
    TempRoot root;
    makePreset (root.dir, "Warm Pad", "x");

    const bool found = findClashingPreset (root.dir, "warm pad") != juce::File();
    REQUIRE (found == ! juce::File::areFileNamesCaseSensitive());
}

TEST_CASE ("clash: no clash when the name is free, the folder is empty, or the folder is missing",
           "[presets][clash]")
{
    TempRoot root;
    makePreset (root.dir, "Other", "x");

    REQUIRE (findClashingPreset (root.dir, "My Preset", false) == juce::File());
    REQUIRE (findClashingPreset (root.dir.getChildFile ("empty"), "My Preset", false) == juce::File());
    REQUIRE (findClashingPreset (root.dir.getChildFile ("missing"), "My Preset", false) == juce::File());
}

TEST_CASE ("clash: files without the preset extension and the migration marker never clash",
           "[presets][clash]")
{
    TempRoot root;
    root.dir.getChildFile ("My Preset.txt").replaceWithText ("x");
    root.dir.getChildFile ("My Preset").replaceWithText ("x");
    root.dir.getChildFile (kMarkerFileName).replaceWithText ("x");
    root.dir.getChildFile ("My Preset" + juce::String (kExtension) + ".bak").replaceWithText ("x");

    REQUIRE (findClashingPreset (root.dir, "My Preset", false) == juce::File());
    REQUIRE (findClashingPreset (root.dir, "My Preset", true) == juce::File());
    REQUIRE (findClashingPreset (root.dir, kMarkerFileName, false) == juce::File());
}

TEST_CASE ("clash: the file being renamed is ignored, a different file is not", "[presets][clash]")
{
    TempRoot root;
    const auto a = makePreset (root.dir, "Alpha", "x");
    const auto b = makePreset (root.dir, "Beta", "x");

    // Case-only rename of Alpha onto itself is not a clash...
    REQUIRE (findClashingPreset (root.dir, "alpha", false, a) == juce::File());
    // ...but renaming Alpha to Beta is.
    REQUIRE (findClashingPreset (root.dir, "Beta", false, a) == b);
}

// ---- OS-appropriate folder ----------------------------------------------------------------

TEST_CASE ("folder: macOS uses the house location, Windows and Linux the per-user app-data folder",
           "[presets][folder]")
{
    TempRoot root;
    const auto home    = root.dir.getChildFile ("home");
    const auto appData = root.dir.getChildFile ("appdata");

    REQUIRE (userPresetDir (Platform::Mac, home, appData)
             == home.getChildFile ("Library/Audio/Presets/ZQ SFX/LFlOw"));

    const auto expected = appData.getChildFile ("ZQ SFX").getChildFile ("LFlOw");
    REQUIRE (userPresetDir (Platform::Windows, home, appData) == expected);
    REQUIRE (userPresetDir (Platform::Linux, home, appData) == expected);
}

TEST_CASE ("folder: the legacy location is the macOS location, so nothing migrates there",
           "[presets][folder]")
{
    TempRoot root;
    const auto home    = root.dir.getChildFile ("home");
    const auto appData = root.dir.getChildFile ("appdata");

    REQUIRE (legacyUserPresetDir (home) == userPresetDir (Platform::Mac, home, appData));
    REQUIRE (legacyUserPresetDir (home) != userPresetDir (Platform::Windows, home, appData));
    REQUIRE (legacyUserPresetDir (home) != userPresetDir (Platform::Linux, home, appData));
}

// ---- migration ----------------------------------------------------------------------------

TEST_CASE ("migration: a missing legacy folder copies nothing and creates nothing", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (! fresh.exists());
}

TEST_CASE ("migration: the same folder on both sides is left alone (macOS)", "[presets][migration]")
{
    TempRoot root;
    const auto dir = root.dir.getChildFile ("same");
    makePreset (dir, "A", "body A");

    REQUIRE (migrateLegacyUserPresets (dir, dir) == 0);
    REQUIRE (! dir.getChildFile (kMarkerFileName).exists());
    REQUIRE (contentsOf (dir, "A") == "body A");
}

TEST_CASE ("migration: presets are copied, the legacy folder is untouched", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "body A");
    makePreset (legacy, "B", "body B");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 2);

    REQUIRE (contentsOf (fresh, "A") == "body A");
    REQUIRE (contentsOf (fresh, "B") == "body B");
    REQUIRE (fresh.getChildFile (kMarkerFileName).existsAsFile());

    // Copy, not move.
    REQUIRE (contentsOf (legacy, "A") == "body A");
    REQUIRE (contentsOf (legacy, "B") == "body B");
}

TEST_CASE ("migration: a preset already in the new folder is never overwritten", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "Same", "OLD content");
    makePreset (legacy, "Only Old", "old only");
    makePreset (fresh, "Same", "NEW content the user kept");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);

    REQUIRE (contentsOf (fresh, "Same") == "NEW content the user kept");
    REQUIRE (contentsOf (fresh, "Only Old") == "old only");
}

TEST_CASE ("migration: runs once; a preset deleted afterwards is not resurrected", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "body A");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);
    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);   // marker present: idempotent

    fresh.getChildFile ("A" + juce::String (kExtension)).deleteFile();
    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (! fresh.getChildFile ("A" + juce::String (kExtension)).exists());
}

TEST_CASE ("migration: only preset files are copied, and the marker is not a listed preset",
           "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "body A");
    legacy.getChildFile ("notes.txt").replaceWithText ("not a preset");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);
    REQUIRE (! fresh.getChildFile ("notes.txt").exists());

    // The preset list (PresetManager::getUserPresetFiles) is the *.lflowpreset wildcard: the
    // dotfile marker must not appear in it.
    const auto listed = fresh.findChildFiles (juce::File::findFiles, false,
                                              juce::String ("*") + kExtension);
    REQUIRE (listed.size() == 1);
    REQUIRE (listed[0].getFileNameWithoutExtension() == "A");
}

TEST_CASE ("migration: an empty legacy folder still completes and is not retried", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    legacy.createDirectory();

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (fresh.getChildFile (kMarkerFileName).existsAsFile());
}

TEST_CASE ("migration: a failed copy leaves no marker so the next launch retries", "[presets][migration]")
{
    TempRoot root;
    const auto legacy = root.dir.getChildFile ("legacy");
    const auto fresh  = root.dir.getChildFile ("fresh");
    makePreset (legacy, "A", "body A");

    // Make the destination unusable: a regular FILE where the new folder should be.
    root.dir.getChildFile ("fresh").replaceWithText ("i am a file, not a folder");

    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 0);
    REQUIRE (! fresh.isDirectory());

    // Once the obstruction is gone the migration completes.
    fresh.deleteFile();
    REQUIRE (migrateLegacyUserPresets (legacy, fresh) == 1);
    REQUIRE (contentsOf (fresh, "A") == "body A");
    REQUIRE (fresh.getChildFile (kMarkerFileName).existsAsFile());
}
