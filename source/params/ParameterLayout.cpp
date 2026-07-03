#include "ParameterLayout.h"
#include "ParameterIDs.h"

namespace lflow {

namespace {

using namespace juce;

// Choice lists shared across all 3 lanes (Phase 1 order).
// "Custom" appended (index 6, Waveform::Custom) -- Phase 4 drawable shapes. Layout only: the
// editor's waveform combo literal gains the matching entry in Task 3. Appending (rather than
// inserting) keeps every existing preset's saved choice index stable.
const StringArray waveformChoices { "Sine", "Triangle", "Square", "Saw Up", "Saw Down", "Sample & Hold", "Custom" };
const StringArray divisionChoices { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
const StringArray rhythmChoices   { "Straight", "Dotted", "Triplet" };
const StringArray destChoices     { "Volume", "Pan", "Low", "Mid", "High", "Pitch" };

// Phase 7 Task 6 (new bug found in Task 4): every %-formatted param (depth x3, mix, smooth)
// had a withStringFromValueFunction (0..1 -> "NN%") but no matching withValueFromStringFunction,
// so typed text entry fell back to AudioParameterFloat's DEFAULT parser -- range.convertTo0to1
// (text.getFloatValue()) -- which reads the raw 0..1 NORMALISED range, not the displayed percent.
// Typing "70" parsed as the literal value 70.0, which convertTo0to1 clamps to this range's max
// (1.0), landing on 100% instead of 70%. Fix: parse the leading number (String::getFloatValue()
// already stops at the first non-numeric character, so "70", "70%", and "70 %" all parse to
// 70.0f the same way), divide by 100, clamp to [0,1].
float percentTextToNormalised (const String& text)
{
    return jlimit (0.0f, 1.0f, text.trim().getFloatValue() / 100.0f);
}

// Adds the 8 lane-indexed parameters for one lane. destDefault/depthDefault vary per lane
// per the Phase 2 defaults (lane1 Volume @ 50%; lane2 Pan @ 0%; lane3 Volume @ 0%).
void addLaneParams (AudioProcessorValueTreeState::ParameterLayout& layout,
                     const char* waveformId, const char* syncId, const char* rateHzId,
                     const char* divisionId, const char* rhythmId, const char* phaseId,
                     const char* depthId, const char* destId,
                     const String& namePrefix, float depthDefault, int destDefault)
{
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { waveformId, 2 }, namePrefix + " Waveform", waveformChoices, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { syncId, 2 }, namePrefix + " Sync", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { rateHzId, 2 }, namePrefix + " Rate",
        NormalisableRange<float> (0.01f, 30.0f, 0.01f, 0.3f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 2) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { divisionId, 2 }, namePrefix + " Division", divisionChoices, 2));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { rhythmId, 2 }, namePrefix + " Rhythm", rhythmChoices, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { phaseId, 2 }, namePrefix + " Phase",
        NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " deg"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { depthId, 2 }, namePrefix + " Depth",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), depthDefault,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction (percentTextToNormalised)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { destId, 2 }, namePrefix + " Dest", destChoices, destDefault));
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Bypass — exposed to the host via getBypassParameter() in the processor.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::bypass, 2 }, "Bypass", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::link, 2 }, "Link", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::xoverLow, 2 }, "Xover Lo",
        NormalisableRange<float> (40.0f, 2000.0f, 1.0f, 0.35f), 250.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::xoverHigh, 2 }, "Xover Hi",
        NormalisableRange<float> (500.0f, 12000.0f, 1.0f, 0.35f), 2500.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

    addLaneParams (layout,
        pid::l1Waveform, pid::l1Sync, pid::l1RateHz, pid::l1Division, pid::l1Rhythm,
        pid::l1Phase, pid::l1Depth, pid::l1Dest, "Lane 1", 0.5f, 0 /* Volume */);

    addLaneParams (layout,
        pid::l2Waveform, pid::l2Sync, pid::l2RateHz, pid::l2Division, pid::l2Rhythm,
        pid::l2Phase, pid::l2Depth, pid::l2Dest, "Lane 2", 0.0f, 1 /* Pan */);

    addLaneParams (layout,
        pid::l3Waveform, pid::l3Sync, pid::l3RateHz, pid::l3Division, pid::l3Rhythm,
        pid::l3Phase, pid::l3Depth, pid::l3Dest, "Lane 3", 0.0f, 0 /* Volume */);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::mix, 2 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction (percentTextToNormalised)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::smooth, 2 }, "Smooth",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.15f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction (percentTextToNormalised)));

    return layout;
}

} // namespace lflow
