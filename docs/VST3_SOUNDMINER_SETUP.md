# VST3 Plugin Setup for Soundminer Compatibility

## Overview

This guide documents how to configure a JUCE-based VST3 plugin for compatibility with Soundminer and other professional audio hosts. These instructions are based on the HyperPrism Reimagined project setup.

---

## Claude Code Prompt (Copy/Paste This)

```
Help me configure my JUCE VST3 plugin for Soundminer compatibility. Please ensure the following:

### 1. CMakeLists.txt - Plugin Format Configuration

Set up VST3-only builds (no Audio Unit):

```cmake
# Define plugin formats to build (VST3 only - no AU)
if(APPLE)
    set(PLUGIN_FORMATS VST3 Standalone)
elseif(WIN32)
    set(PLUGIN_FORMATS VST3 Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone)
endif()
```

### 2. Critical Compile Definitions

Add these compile definitions to your target:

```cmake
target_compile_definitions(${target_name}
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0      # Important: prevents VST2 replacement issues
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

# Disable JACK/ALSA on macOS for professional DAW compatibility
if(APPLE)
    target_compile_definitions(${target_name} PUBLIC JUCE_JACK=0 JUCE_ALSA=0)
endif()
```

### 3. Plugin Metadata Configuration

Configure juce_add_plugin() with these essential parameters:

```cmake
juce_add_plugin(MyPluginName
    COMPANY_NAME "Your Company"                    # Vendor name shown in hosts
    IS_SYNTH FALSE                                 # FALSE for effects
    NEEDS_MIDI_INPUT FALSE                         # Disable unless needed
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE              # Allows host keyboard shortcuts
    COPY_PLUGIN_AFTER_BUILD TRUE                   # Auto-copy after build
    PLUGIN_MANUFACTURER_CODE Xxxx                  # 4-char vendor code (UNIQUE)
    PLUGIN_CODE Yyyy                               # 4-char plugin code (UNIQUE)
    BUNDLE_ID "com.yourcompany.yourplugin"         # Unique bundle identifier
    FORMATS ${PLUGIN_FORMATS}                      # VST3 + Standalone
    PRODUCT_NAME "Your Plugin Name"                # Display name in DAW
    VST3_CATEGORIES "Fx" "Filter"                  # Appropriate VST3 categories
)
```

### 4. Build for Universal Binary (macOS)

Build command for both Intel and Apple Silicon:

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      ..
cmake --build . --config Release -j$(sysctl -n hw.ncpu)
```

### 5. Code Signing (Required for Soundminer)

Sign the plugin with your Developer ID:

```bash
# Basic signing
codesign --force --deep --sign "YOUR_TEAM_ID" path/to/plugin.vst3

# For distribution (with hardened runtime and timestamp)
codesign --force --deep --options runtime --timestamp --sign "Developer ID Application: Your Name (TEAM_ID)" path/to/plugin.vst3
```

### 6. Notarization (Required for macOS distribution)

```bash
# Create zip for notarization
ditto -c -k --keepParent "plugin.vst3" "plugin.zip"

# Submit for notarization
xcrun notarytool submit plugin.zip --apple-id "your@email.com" --team-id "TEAM_ID" --password "app-specific-password" --wait

# Staple the ticket
xcrun stapler staple "plugin.vst3"

# Verify
xcrun stapler validate "plugin.vst3"
```

### 7. Installation Location

Copy to the system VST3 folder for Soundminer to find:

```bash
sudo cp -R "MyPlugin.vst3" /Library/Audio/Plug-Ins/VST3/
```

Please implement these configurations in my project.
```

---

## Key Configuration Details Explained

### Why These Settings Matter for Soundminer

| Setting | Purpose |
|---------|---------|
| `JUCE_VST3_CAN_REPLACE_VST2=0` | Prevents conflicts with existing VST2 versions |
| `EDITOR_WANTS_KEYBOARD_FOCUS FALSE` | Host keyboard shortcuts work while plugin UI is open |
| `IS_MIDI_EFFECT FALSE` | Correct audio routing in Soundminer |
| `NEEDS_MIDI_INPUT/OUTPUT FALSE` | Audio effects don't need MIDI |
| Universal Binary | Works on both Intel and Apple Silicon Macs |
| Code Signing | Required for Soundminer to load the plugin |

### Unique Identifiers (Critical!)

Each plugin MUST have unique:
- **PLUGIN_MANUFACTURER_CODE**: 4 characters identifying your company (e.g., `ZQFX`)
- **PLUGIN_CODE**: 4 characters identifying this specific plugin (e.g., `Hdly`)
- **BUNDLE_ID**: Reverse-domain format (e.g., `com.yourcompany.myplugin`)

### VST3 Categories

Common categories for Soundminer effects:
- `"Fx"` - General effect
- `"Delay"` - Delay effects
- `"Reverb"` - Reverb effects
- `"Filter"` - Filter effects
- `"EQ"` - Equalizers
- `"Dynamics"` - Compressors, limiters, gates
- `"Distortion"` - Saturation, overdrive
- `"Modulation"` - Chorus, flanger, phaser
- `"Pitch Shift"` - Pitch changers
- `"Spatial"` - Stereo/surround effects

---

## Expected VST3 Bundle Structure

After building, your plugin should have this structure:

```
YourPlugin.vst3/
├── Contents/
│   ├── Info.plist              # Bundle metadata (auto-generated)
│   ├── PkgInfo                 # Package type
│   ├── MacOS/
│   │   └── YourPlugin          # Universal binary (arm64 + x86_64)
│   ├── Resources/
│   │   └── moduleinfo.json     # VST3 metadata (auto-generated)
│   └── _CodeSignature/
│       └── CodeResources       # Code signature (after signing)
```

---

## Verification Steps

### 1. Check Binary Architecture
```bash
file "YourPlugin.vst3/Contents/MacOS/YourPlugin"
# Should show: Mach-O universal binary with 2 architectures: [x86_64, arm64]
```

### 2. Verify Code Signature
```bash
codesign -dv --verbose=4 "YourPlugin.vst3"
```

### 3. Validate VST3 Structure
```bash
# Using VST3 SDK's moduleinfotool (if available)
moduleinfotool -validate -path "YourPlugin.vst3"
```

### 4. Test in Soundminer
1. Copy plugin to `/Library/Audio/Plug-Ins/VST3/`
2. Launch Soundminer
3. Go to Preferences > Plugins or Plugin Manager
4. Rescan if necessary
5. Plugin should appear in effects list

---

## Troubleshooting

### Plugin Not Appearing in Soundminer

1. **Check location**: Must be in `/Library/Audio/Plug-Ins/VST3/`
2. **Verify signing**: `codesign -v "plugin.vst3"` should return no errors
3. **Check architecture**: Must be Universal Binary or match your Mac's architecture
4. **Clear plugin cache**: Some hosts cache plugin info; try rescanning

### Plugin Loads But Crashes

1. **Check Info.plist**: Ensure `CFBundleIdentifier` matches your BUNDLE_ID
2. **Verify dependencies**: Ensure no missing frameworks
3. **Check console logs**: `Console.app` > search for your plugin name

### Plugin Parameters Don't Automate

1. **Use AudioProcessorValueTreeState**: Standard JUCE parameter system
2. **Expose parameters properly**: All automatable params need unique IDs
3. **Avoid thread-unsafe operations**: Parameter changes can come from any thread

---

## Quick Reference Commands

```bash
# Build
cd build && cmake --build . --config Release -j$(sysctl -n hw.ncpu)

# Sign
codesign --force --deep --sign "TEAM_ID" plugin.vst3

# Install
sudo cp -R plugin.vst3 /Library/Audio/Plug-Ins/VST3/

# Verify
codesign -v plugin.vst3 && echo "Signature OK"

# Check architecture
file plugin.vst3/Contents/MacOS/*
```
