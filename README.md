# LFlOw

LFlOw (LFO + Flow) is a multi-lane modulation plugin. It gives you three LFO
lanes, each routable to volume, pan, one of three frequency bands, or pitch
for vibrato. Lanes can run independently or be linked together. Shapes can
be drawn by hand right in the display, with breakpoint editing for fine
control, and undo/redo covers every edit. It ships VST3, AU, and Standalone
on macOS, and VST3 on Windows and Linux.

## Install

Binary releases are available for download from the
[v0.9.0 release](https://github.com/themightyzq/lflow/releases/tag/v0.9.0)
(LFlOw-macOS-VST3.zip, LFlOw-macOS-AU-Standalone.zip, LFlOw-Windows-VST3.zip,
LFlOw-Linux-VST3.zip). On macOS, the built plugin and standalone app are
unsigned, so first launch needs right-click, Open.

Alternatively, build from source (below).

Requires macOS 11.0 or later.

## Use

1. Pick a lane (1, 2, or 3) and choose its destination: Volume, Pan, one of
   the three frequency bands, or Pitch (vibrato).
2. Draw or pick a shape for the lane in the display; drag its breakpoints
   to reshape it.
3. Link lanes together, or leave them independent, depending on whether you
   want them moving in lockstep.
4. Undo and redo cover every edit, and A/B compare lets you flip between
   two states while you dial things in.

### Presets

Factory presets are ready to load, and you can save your own alongside
them. Save As asks before it replaces a preset that already exists. User
presets are XML files (.lflowpreset) written to:

```
macOS:    ~/Library/Audio/Presets/ZQ SFX/LFlOw/
Windows:  %APPDATA%\ZQ SFX\LFlOw\
Linux:    ~/.config/ZQ SFX/LFlOw/
```

Earlier Windows and Linux builds kept presets under
`~/Library/Audio/Presets/ZQ SFX/LFlOw/` in the home folder. Those presets are
copied to the folder above the first time the plugin loads; the old folder is
left as it is.

## Build from source

Requirements: CMake 3.22+, a C++17 compiler, JUCE 8.0.14 and Catch2
(fetched automatically by CMake). macOS builds are Universal Binary
(arm64 + x86_64).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target LFlOw_Standalone -j   # standalone only, for quick testing
cmake --build build --target LFlOw_All -j          # all configured formats
```

Run the unit tests with:

```bash
cmake --build build --target lflow_tests lflow_preset_tests -j
(cd build && ctest)
```

## Licence

GPL-3.0-or-later. See LICENSE. Built with JUCE.

ZQ SFX, https://www.zq-sfx.com, connect@zq-sfx.com.
