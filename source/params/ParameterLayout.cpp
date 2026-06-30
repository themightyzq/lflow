#include "ParameterLayout.h"
#include "ParameterIDs.h"

namespace lflow {

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Bypass — exposed to the host via getBypassParameter() in the processor.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::bypass, 1 }, "Bypass", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::waveform, 1 }, "Waveform",
        StringArray { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold" }, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::sync, 1 }, "Sync", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::rateHz, 1 }, "Rate",
        NormalisableRange<float> (0.01f, 30.0f, 0.01f, 0.3f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::division, 1 }, "Division",
        StringArray { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" }, 2));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::rhythm, 1 }, "Rhythm",
        StringArray { "Straight", "Dotted", "Triplet" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::depth, 1 }, "Depth",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::mode, 1 }, "Mode",
        StringArray { "Tremolo", "Pan" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::smooth, 1 }, "Smooth",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.15f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));

    return layout;
}

} // namespace lflow
