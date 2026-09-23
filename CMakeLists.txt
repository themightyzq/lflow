cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version (universal binaries cannot go below 11.0)")
endif()

project(LFlOw VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS universal binary (Intel + Apple Silicon) — per VST3_SOUNDMINER_SETUP.md.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The ZQ SFX house UI (tokens, LookAndFeel, bound controls, embedded OFL fonts). Must come
# AFTER JUCE. Pinned by tag: bump deliberately, then re-run lflow_ui_snapshot and compare.
FetchContent_Declare(zqsfx_ui
    GIT_REPOSITORY https://github.com/themightyzq/zqsfx_ui.git
    GIT_TAG v0.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zqsfx_ui)

# ---- Shared pure-C++ DSP library (no JUCE) ----
add_library(lflow_dsp STATIC source/dsp/MultiLaneEngine.cpp)
target_include_directories(lflow_dsp PUBLIC source/dsp)
target_compile_features(lflow_dsp PUBLIC cxx_std_17)

# ---- Plugin formats ----
set(LFLOW_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND LFLOW_FORMATS AU)
endif()

juce_add_plugin(LFlOw
    PRODUCT_NAME "LFlOw"
    COMPANY_NAME "ZQ SFX"
    COMPANY_WEBSITE "https://www.zq-sfx.com"
    COMPANY_EMAIL "connect@zq-sfx.com"
    COMPANY_COPYRIGHT "Copyright (c) 2026 ZQ SFX"
    BUNDLE_ID "com.zqsfx.lflow"
    PLUGIN_MANUFACTURER_CODE ZQSF
    PLUGIN_CODE Lflw
    FORMATS ${LFLOW_FORMATS}
    VST3_CATEGORIES "Fx" "Modulation"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

juce_generate_juce_header(LFlOw)

target_sources(LFlOw PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/params/ParameterLayout.cpp
    source/gui/LFlOwLookAndFeel.cpp
    source/gui/LfoDisplay.cpp
    source/shapes/ShapeManager.cpp
    source/presets/PresetManager.cpp)

target_include_directories(LFlOw PRIVATE source source/params source/dsp source/gui source/shapes source/presets)

target_compile_definitions(LFlOw PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

if(UNIX AND NOT APPLE)
    target_compile_definitions(LFlOw PUBLIC JUCE_JACK=0 JUCE_ALSA=0)
endif()

target_link_libraries(LFlOw PRIVATE
    lflow_dsp
    zqsfx::ui
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- UI snapshot tool (headless before/after render gate for the zqsfx_ui migration) ----
# pamplejuce pattern: link against the plugin's own shared-code target ("LFlOw", produced by
# juce_add_plugin above) instead of recompiling PluginProcessor.cpp/PluginEditor.cpp a second
# time. juce_generate_juce_header(LFlOw)'s generated JuceHeader.h is only added to LFlOw's OWN
# include path as PRIVATE (see JUCEUtils.cmake), so it does not propagate through
# target_link_libraries; it is added back explicitly below via JUCE_GENERATED_SOURCES_DIRECTORY.
juce_add_console_app(lflow_ui_snapshot PRODUCT_NAME "lflow_ui_snapshot")
target_sources(lflow_ui_snapshot PRIVATE tools/ui_snapshot/main.cpp)
target_include_directories(lflow_ui_snapshot PRIVATE
    source source/params source/dsp source/gui source/shapes source/presets
    $<TARGET_PROPERTY:LFlOw,JUCE_GENERATED_SOURCES_DIRECTORY>)
target_link_libraries(lflow_ui_snapshot PRIVATE
    LFlOw
    lflow_dsp
    zqsfx::ui
    juce::juce_audio_utils
    juce::juce_dsp)

# ---- Tests (added in Task 3) ----
enable_testing()

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(lflow_tests
    tests/SanityTests.cpp
    tests/LfoCoreTests.cpp
    tests/SyncRateTests.cpp
    tests/LfoClockTests.cpp
    tests/LaneParamsTests.cpp
    tests/MultiLaneEngineTests.cpp
    tests/BiquadTests.cpp
    tests/LR4CrossoverTests.cpp
    tests/ShapeModelTests.cpp
    tests/TripleBufferTests.cpp
    tests/ModDelayTests.cpp)
target_include_directories(lflow_tests PRIVATE source/dsp)
find_package(Threads REQUIRED)
target_link_libraries(lflow_tests PRIVATE lflow_dsp Catch2::Catch2WithMain Threads::Threads)
target_compile_features(lflow_tests PRIVATE cxx_std_17)

include(CTest)
include(Catch)
catch_discover_tests(lflow_tests)
