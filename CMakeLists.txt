cmake_minimum_required(VERSION 3.22)
project(LFlOw VERSION 0.1.0 LANGUAGES C CXX)

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

# ---- Shared pure-C++ DSP library (no JUCE) ----
add_library(lflow_dsp STATIC source/dsp/ChopperEngine.cpp)
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
    BUNDLE_ID "com.zacharyquarles.lflow"
    PLUGIN_MANUFACTURER_CODE Zqlr
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
    source/PluginEditor.cpp)

target_compile_definitions(LFlOw PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

if(APPLE)
    target_compile_definitions(LFlOw PUBLIC JUCE_JACK=0 JUCE_ALSA=0)
endif()

target_link_libraries(LFlOw PRIVATE
    lflow_dsp
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

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
    tests/LfoClockTests.cpp)
target_include_directories(lflow_tests PRIVATE source/dsp)
target_link_libraries(lflow_tests PRIVATE lflow_dsp Catch2::Catch2WithMain)
target_compile_features(lflow_tests PRIVATE cxx_std_17)

include(CTest)
include(Catch)
catch_discover_tests(lflow_tests)
