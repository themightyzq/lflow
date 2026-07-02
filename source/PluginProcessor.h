#pragma once
#include <JuceHeader.h>
#include "dsp/MultiLaneEngine.h"
#include <atomic>

class LFlOwAudioProcessor : public juce::AudioProcessor
{
public:
    LFlOwAudioProcessor();
    ~LFlOwAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "LFlOw"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    float getLanePhase (int lane) const noexcept
    {
        return (lane >= 0 && lane < lflow::MultiLaneEngine::kNumLanes) ? lanePhaseAtomic[(size_t) lane].load() : 0.0f;
    }
    float getLaneValue (int lane) const noexcept
    {
        return (lane >= 0 && lane < lflow::MultiLaneEngine::kNumLanes) ? laneValueAtomic[(size_t) lane].load() : 0.0f;
    }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorParameter* bypassParam { nullptr };
    lflow::MultiLaneEngine engine;

    // Bus layout is restricted to mono/stereo (isBusesLayoutSupported); 8 is safe headroom
    // for the chunked-processing channel-pointer array in processBlock.
    static constexpr int kMaxChannels = 8;

    juce::SmoothedValue<float> bypassGain;
    juce::AudioBuffer<float> dryScratch;

    std::atomic<float> lanePhaseAtomic[lflow::MultiLaneEngine::kNumLanes] {};
    std::atomic<float> laneValueAtomic[lflow::MultiLaneEngine::kNumLanes] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessor)
};
