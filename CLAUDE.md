# LFlOw — Project Contract

LFlOw (LFO + FLOW) is a cross-platform VST3/AU LFO modulation plugin built on JUCE, by ZQ SFX.
Feature-complete at v0.7.0 (2026-07-03): 3 linked/unlinkable LFO lanes routed to Volume, Pan,
3-band levels (LR4 crossovers), or Pitch (vibrato via mod delay); drawable custom shapes with
in-display breakpoint editing; undo/redo; factory+user presets with A/B; custom LookAndFeel,
resizable UI. Per-phase specs/plans live in `docs/superpowers/specs|plans/`; build history and
deferred-item backlog in `.superpowers/sdd/progress.md` (gitignored scratch — recoverable from
git log). Remaining work is release mechanics (signing/notarization/Soundminer install — see
`docs/VST3_SOUNDMINER_SETUP.md`) plus roadmap features (retrigger modes, stereo spread,
preset morphing).

## Standing working principles (non-negotiable)
- **Verify your own work; do not trust that it worked.** Re-read the changed file, run the
  build, run the tests, reproduce the case. An edit is not done because it was written.
- **Investigate before answering.** Base claims on the actual code/output; flag inferences.
- **Root-cause fixes only.** No bandaids, no papering over.
- **Stay in scope.** Note adjacent issues; don't fix them in the same pass.

## Real-time audio thread is sacred
`processBlock` and everything it calls run on a real-time thread. A missed deadline is an
audible glitch. On that thread: `juce::ScopedNoDenormals` at the top; NO heap allocation,
locks, logging, file/network I/O, or `std::shared_ptr` on audio-thread objects. Never assume
a fixed buffer size or sample rate. Cross-thread communication uses lock-free `std::atomic`
or APVTS only. Keep `AudioProcessor` (audio thread) and `AudioProcessorEditor` (message
thread) strictly separated.

## Architecture rules
- DSP units in `source/dsp/` are **pure C++ and must not include any JUCE header.** They are
  unit-tested headless via `lflow_tests`. This boundary is load-bearing for testability and
  for reuse by later phases (multi-lane, multiband).
- Parameters live in APVTS; UI binds via attachments. No parameter state outside APVTS.
- Shape nodes live in the SHAPES child of apvts.state (ShapeManager, message thread only);
  audio thread gets baked tables via lock-free TripleBuffer. User presets: XML at
  ~/Library/Audio/Presets/ZQ SFX/LFlOw/*.lflowpreset.

## UI & build house standards (binding)
Follow `docs/JUCE_VST3_UI_UX_BEST_PRACTICES.md` and `docs/VST3_SOUNDMINER_SETUP.md`:
- All colors come from `LFlOwLookAndFeel::Colors` (LFlOw = Modulation → pink `#ff6bb5`); never
  hardcode `juce::Colours::` in editor/gui code.
- Generic fonts only (`juce::FontOptions(size)`, never "Arial"); ASCII-only displayed strings
  (no `deg`/`->`/`-inf` Unicode); every interactive control has a tooltip; editor holds a
  `juce::TooltipWindow`.
- Parameter formatting is single-source (APVTS `stringFromValue` lambda; no `setTextValueSuffix`
  in the editor); every numeric range has an explicit step size.
- A `bypass` parameter exists, is honored in `processBlock`, and is exposed via
  `getBypassParameter()`.
- Build config: `BUNDLE_ID`, `VST3_CATEGORIES "Fx" "Modulation"`, `EDITOR_WANTS_KEYBOARD_FOCUS
  FALSE`, macOS universal binary, `JUCE_DISPLAY_SPLASH_SCREEN=0`/`JUCE_REPORT_APP_USAGE=0`
  (license held). Signing/notarization are deferred to release.

## Build, run, test
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Debug`
- Build plugin (standalone for testing): `cmake --build build --target LFlOw_Standalone -j`
- Build all formats: `cmake --build build --target LFlOw_All -j`
- Build + run tests: `cmake --build build --target lflow_tests -j && ./build/lflow_tests`
- JUCE is pinned to tag `8.0.14` in CMakeLists.txt; bump deliberately.

## Definition of done (a change is done when)
- It builds on the target platforms it touches.
- DSP changes have headless Catch2 tests that pass.
- No allocations or locks added to the audio thread.
- For audible changes: verified by ear in the standalone build.

## Platforms & distribution
macOS (VST3 + AU), Windows (VST3), Linux (VST3). Standalone is a dev/test convenience.
Versioning: semver in CMake `project(... VERSION ...)`.

## Version control: Diversion (NOT git)
This project uses **Diversion** (`dv` CLI at `~/.diversion/bin/dv`), not git. Do not run
`git commit`/branch/PR flows for project work. Use Diversion's workflow (`dv status`,
`dv commit`, etc.). (A stray local `.git` repo from early setup may exist; it is not the
source of truth.)

## Steering layers
Always-on rules → this file. Reusable playbooks → skills. Context-isolating/parallel work →
subagents. Deterministic enforcement → hooks.

## Repo hygiene (deferrable — never blocks feature work)
CI setup, code formatting config, and signing/notarization are deferred. Track them in the
roadmap, not mid-feature.
