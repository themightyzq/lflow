# LFlOw

LFlOw ("LFO + FLOW", `PRODUCT_NAME "LFlOw"` in CMakeLists.txt) is a
cross-platform multi-lane LFO / modulation FX plugin built on JUCE, by
ZQ SFX. Feature-complete at v0.7.0 (2026-07-03, per `CLAUDE.md`): 3
linked/unlinkable LFO lanes routed to Volume, Pan, 3-band levels (LR4
crossovers, `source/dsp/LR4Crossover.h`), or Pitch (vibrato via mod delay,
`source/dsp/ModDelay.h`); drawable custom shapes with in-display
breakpoint editing (`source/shapes/ShapeManager.cpp`); undo/redo; factory
and user presets with A/B compare; a custom `LFlOwLookAndFeel`; a
resizable UI.

Remaining work is release mechanics (signing/notarization/Soundminer
install — see host-compat docs below) plus roadmap features (retrigger
modes, stereo spread, preset morphing).

## Formats

`VST3` + `Standalone` everywhere; `AU` is added only on Apple:

```cmake
set(LFLOW_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND LFLOW_FORMATS AU)
endif()
```

## Requirements

- CMake 3.22+
- C++17
- JUCE 8.0.14 and Catch2 v3.5.2, both fetched automatically via CMake
  `FetchContent`
- macOS builds are Universal Binary (arm64 + x86_64, forced via
  `CMAKE_OSX_ARCHITECTURES` when `APPLE`)

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target LFlOw_Standalone -j   # standalone only, for quick testing
cmake --build build --target LFlOw_All -j          # all configured formats
```

JUCE is pinned to tag `8.0.14` in `CMakeLists.txt`; bump it deliberately,
not as a side effect of other work.

## Tests

Headless Catch2 tests (`lflow_tests`) run against `lflow_dsp`, a pure
C++ static library (`source/dsp/MultiLaneEngine.cpp`) with no JUCE
dependency, covering biquads, the LR4 crossover, LFO core/clock/sync-rate,
lane params, the multi-lane engine, the shape model, the triple buffer, and
mod delay:

```bash
cmake --build build --target lflow_tests -j && ./build/lflow_tests
```

## Presets

User presets are XML `*.lflowpreset` files written by
`source/presets/PresetManager.cpp` (`PresetManager::getUserPresetDir()`) to:

```
<home>/Library/Audio/Presets/ZQ SFX/LFlOw
```

The code builds this path from JUCE's `File::getSpecialLocation
(userHomeDirectory)` with no platform-specific branch, so the same
`Library/Audio/Presets/ZQ SFX/LFlOw` suffix is appended under the user's
home directory on every platform the plugin runs on, not only macOS.

## Project layout

```
source/PluginProcessor.{h,cpp}, PluginEditor.{h,cpp}   plugin entry points
source/dsp/         pure C++ DSP (no JUCE headers), unit-tested headless
source/gui/         LFlOwLookAndFeel, LfoDisplay
source/params/      ParameterIDs, ParameterLayout (APVTS)
source/presets/     PresetManager (factory + user presets)
source/shapes/      ShapeManager (drawable LFO shapes, message thread)
tests/              lflow_tests (Catch2)
docs/               local copies (JUCE_VST3_UI_UX_BEST_PRACTICES.md, VST3_SOUNDMINER_SETUP.md)
```

## Host-compatibility docs

Host-compatibility guidance (Soundminer setup, VST3/UI best practices) is
canonically maintained at `../docs/` and shared across the ZQ SFX
workspace — see `../docs/VST3_SOUNDMINER_SETUP.md`. Per the workspace's
`../CLAUDE.md`, fix the canonical copy there rather than this project's
local `docs/` copies.

## Identity & contact

`COMPANY_NAME "ZQ SFX"`, `BUNDLE_ID "com.zqsfx.lflow"`,
`PLUGIN_MANUFACTURER_CODE ZQSF`, `PLUGIN_CODE Lflw` (frozen — never
change; hosts key saved sessions on it).

Website: https://www.zq-sfx.com
Contact: connect@zq-sfx.com

## Licence

No LICENSE file exists in this repository yet.

## Version control

**Diversion is the authoritative VCS** for this project (`.diversion/`
present at the project root). A local `.git` repository also exists but
has **no remote configured** — it is not the source of truth; do not treat
it as a publishing target.
