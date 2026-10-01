# Changelog

Earlier releases are listed at https://github.com/themightyzq/lflow/releases.

## 0.8.0 - 2026-10-01

### Changed (audible)
- Mix, each lane's Depth, each lane's Phase and both crossover frequencies (Xover Lo,
  Xover Hi) now ramp to a new value over 30 ms instead of stepping. Automating or jumping
  any of them no longer produces zipper noise or clicks. A knob you turn by hand sounds the
  same; a stepped value (preset load, automation lane with hard steps, typed entry) now
  glides in over 30 ms. Phase takes the short way round the circle (350 to 10 degrees is a
  20 degree glide). Rate and Smooth are not ramped: changing them cannot cause a click. The
  parameter values themselves, their ranges, and saved sessions are unchanged.
- A lane whose Depth goes from 0 up fades in over 30 ms, and a lane whose Depth goes to 0
  fades out before it becomes transparent.

### Fixed
- Sample & Hold lanes draw a new random value only when the LFO phase wraps around. Before,
  any small backward move of the phase also counted as a wrap.
- Save As no longer overwrites an existing user preset silently. If a preset with that name
  exists (names that differ only in case count as the same on macOS and Windows), a Replace
  or Cancel prompt appears; Cancel is the default. A failed write is reported instead of
  being ignored.
- User presets are now stored in each OS's conventional per-user location: macOS
  ~/Library/Audio/Presets/ZQ SFX/LFlOw (unchanged), Windows %APPDATA%\ZQ SFX\LFlOw, Linux
  ~/.config/ZQ SFX/LFlOw. Windows and Linux builds used the macOS-style path under the home
  folder before. On first launch after the update, presets found at that old path are copied
  (never moved, never overwriting a preset already in the new folder) into the new folder.
  The old folder is left in place.
