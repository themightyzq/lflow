#pragma once
#include <JuceHeader.h>
#include "dsp/MultiLaneEngine.h"
#include "dsp/TripleBuffer.h"
#include "shapes/ShapeManager.h"
#include <atomic>
#include <memory>

namespace lflow {

// Cached raw-parameter pointers for a single modulation lane's ~8 processBlock-read
// parameters. Populated once (message thread, ctor) and read every processBlock() call
// (audio thread) instead of doing a string-keyed AudioProcessorValueTreeState::
// getRawParameterValue() hash lookup per field per block (QA L2).
struct LaneParamPtrs
{
    std::atomic<float>* waveform { nullptr };
    std::atomic<float>* sync     { nullptr };
    std::atomic<float>* rateHz   { nullptr };
    std::atomic<float>* division { nullptr };
    std::atomic<float>* rhythm   { nullptr };
    std::atomic<float>* phase    { nullptr };
    std::atomic<float>* depth    { nullptr };
    std::atomic<float>* dest     { nullptr };
};

// Every parameter processBlock() (or code it calls, e.g. readLaneParams) reads, cached as
// raw atomic pointers: 3 lanes x 8 (LaneParamPtrs) + 6 globals = ~30 total (QA L2). The
// pointers returned by getRawParameterValue() are stable for the lifetime of the owning
// AudioProcessorValueTreeState/parameter (JUCE never reallocates/moves a parameter's backing
// atomic once created), so caching them once in the ctor is safe even though setStateInformation
// later calls apvts.replaceState() -- replaceState() only swaps the ValueTree's *values*, it does
// not recreate the AudioProcessorParameter objects or their backing atomics.
struct CachedParams
{
    LaneParamPtrs lane[MultiLaneEngine::kNumLanes];
    std::atomic<float>* bypass    { nullptr };
    std::atomic<float>* link      { nullptr };
    std::atomic<float>* mix       { nullptr };
    std::atomic<float>* smooth    { nullptr };
    std::atomic<float>* xoverLow  { nullptr };
    std::atomic<float>* xoverHigh { nullptr };
};

} // namespace lflow

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

    // Message-thread accessor for the editor (Task 3) to read/edit per-lane shape node lists.
    lflow::ShapeManager& getShapeManager() noexcept { return *shapeManager; }

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

    // Populated once in the ctor body (after apvts is fully constructed) -- see CachedParams'
    // doc comment above. processBlock() and everything it calls (readLaneParams) read ONLY
    // through this; no string-keyed getRawParameterValue() lookups remain in the audio path.
    lflow::CachedParams cachedParams;

    lflow::MultiLaneEngine engine;

    // Per-lane custom-shape lookup tables, published lock-free by shapeManager (message
    // thread) and acquired once per block in processBlock() (audio thread). Declared BEFORE
    // shapeManager so they're fully constructed by the time its ctor runs (and outlive it).
    lflow::ShapeTableBuffer shapeBuffers[lflow::MultiLaneEngine::kNumLanes];

    // Constructed AFTER apvts (needs a live APVTS reference) in the ctor init list; owns the
    // SHAPES ValueTree subtree and bakes+publishes into shapeBuffers on any shape change
    // (including state reloads). Message-thread only -- see ShapeManager.h.
    std::unique_ptr<lflow::ShapeManager> shapeManager;

    // Bus layout is restricted to mono/stereo (isBusesLayoutSupported); 8 is safe headroom
    // for the chunked-processing channel-pointer array in processBlock.
    static constexpr int kMaxChannels = 8;

    juce::SmoothedValue<float> bypassGain;
    juce::AudioBuffer<float> dryScratch;

    std::atomic<float> lanePhaseAtomic[lflow::MultiLaneEngine::kNumLanes] {};
    std::atomic<float> laneValueAtomic[lflow::MultiLaneEngine::kNumLanes] {};

    // QA M1: setStateInformation() can be called by a host off the message thread. It marshals
    // the actual apvts.replaceState() apply over to the message thread via
    // juce::MessageManager::callAsync() when that happens (see .cpp) -- the async lambda must be
    // able to check that this processor is still alive before touching `apvts`, since the host
    // could destroy the processor before the callback runs (e.g. a fast plugin-scan unload
    // racing setStateInformation). JUCE_DECLARE_WEAK_REFERENCEABLE adds the master reference +
    // getWeakReference() this needs; it costs one extra pointer-sized member, nothing on the
    // audio thread (WeakReference is never touched from processBlock()).
    JUCE_DECLARE_WEAK_REFERENCEABLE (LFlOwAudioProcessor)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LFlOwAudioProcessor)
};
