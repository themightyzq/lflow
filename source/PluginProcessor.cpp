#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterLayout.h"
#include "params/ParameterIDs.h"
#include "dsp/SyncRate.h"

namespace {

// Reads one lane's APVTS parameters (atomic loads only) into a LaneParams. Local helper,
// not a member, so it can be a free function taking the id set explicitly per lane.
lflow::LaneParams readLaneParams (const juce::AudioProcessorValueTreeState& apvts,
                                   const char* waveformId, const char* syncId, const char* rateHzId,
                                   const char* divisionId, const char* rhythmId, const char* phaseId,
                                   const char* depthId, const char* destId)
{
    lflow::LaneParams p;
    p.waveform = static_cast<lflow::Waveform> ((int) apvts.getRawParameterValue (waveformId)->load());
    p.sync     = apvts.getRawParameterValue (syncId)->load() > 0.5f;
    p.rateHz   = (double) apvts.getRawParameterValue (rateHzId)->load();
    const auto div = static_cast<lflow::Division> ((int) apvts.getRawParameterValue (divisionId)->load());
    const auto rhy = static_cast<lflow::Rhythm>   ((int) apvts.getRawParameterValue (rhythmId)->load());
    p.cycleBeats = lflow::cycleBeats (div, rhy);
    p.depth      = apvts.getRawParameterValue (depthId)->load();
    p.dest       = static_cast<lflow::Dest> ((int) apvts.getRawParameterValue (destId)->load());
    const float degrees = apvts.getRawParameterValue (phaseId)->load();
    p.phaseOffset = degrees / 360.0f;
    return p;
}

} // namespace

LFlOwAudioProcessor::LFlOwAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", lflow::createParameterLayout())
{
    bypassParam = apvts.getParameter (lflow::pid::bypass);
}

void LFlOwAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);

    // ~30 ms bypass crossfade ramp, click-free. setSize here only — never in processBlock.
    bypassGain.reset (sampleRate, 0.03);
    bypassGain.setCurrentAndTargetValue (1.0f);

    const int scratchChannels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);
    dryScratch.setSize (scratchChannels, samplesPerBlock);
}

bool LFlOwAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void LFlOwAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const bool bypassed = apvts.getRawParameterValue (lflow::pid::bypass)->load() > 0.5f;
    bypassGain.setTargetValue (bypassed ? 0.0f : 1.0f);

    const bool link = apvts.getRawParameterValue (lflow::pid::link)->load() > 0.5f;

    lflow::LaneParams lanes[lflow::MultiLaneEngine::kNumLanes] =
    {
        readLaneParams (apvts, lflow::pid::l1Waveform, lflow::pid::l1Sync, lflow::pid::l1RateHz,
                         lflow::pid::l1Division, lflow::pid::l1Rhythm, lflow::pid::l1Phase,
                         lflow::pid::l1Depth, lflow::pid::l1Dest),
        readLaneParams (apvts, lflow::pid::l2Waveform, lflow::pid::l2Sync, lflow::pid::l2RateHz,
                         lflow::pid::l2Division, lflow::pid::l2Rhythm, lflow::pid::l2Phase,
                         lflow::pid::l2Depth, lflow::pid::l2Dest),
        readLaneParams (apvts, lflow::pid::l3Waveform, lflow::pid::l3Sync, lflow::pid::l3RateHz,
                         lflow::pid::l3Division, lflow::pid::l3Rhythm, lflow::pid::l3Phase,
                         lflow::pid::l3Depth, lflow::pid::l3Dest),
    };

    lflow::resolveLinkedLanes (lanes, lflow::MultiLaneEngine::kNumLanes, link);

    for (int i = 0; i < lflow::MultiLaneEngine::kNumLanes; ++i)
        engine.setLaneParams (i, lanes[i]);

    lflow::GlobalParams g;
    g.mix    = apvts.getRawParameterValue (lflow::pid::mix)->load();
    g.smooth = apvts.getRawParameterValue (lflow::pid::smooth)->load();
    engine.setGlobalParams (g);

    // Transport.
    bool playing = false; double bpm = 120.0, ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto b = pos->getBpm())          bpm = *b;
            if (auto q = pos->getPpqPosition())  ppq = *q;
        }
    engine.setTransport (playing, bpm, ppq);

    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // Snapshot dry into the preallocated scratch buffer (sized in prepareToPlay; no
    // allocation here) so the bypass crossfade below can blend wet/dry per sample.
    for (int c = 0; c < numChannels; ++c)
        dryScratch.copyFrom (c, 0, buffer, c, 0, numSamples);

    // Engine always runs, even fully bypassed, so the UI display keeps animating.
    auto* const* channelData = buffer.getArrayOfWritePointers();
    engine.process (channelData, numChannels, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        const float wetGain = bypassGain.getNextValue();
        const float dryGain = 1.0f - wetGain;
        for (int c = 0; c < numChannels; ++c)
        {
            const float wet = channelData[c][n];
            const float dry = dryScratch.getSample (c, n);
            channelData[c][n] = wet * wetGain + dry * dryGain;
        }
    }

    for (int i = 0; i < lflow::MultiLaneEngine::kNumLanes; ++i)
    {
        lanePhaseAtomic[(size_t) i].store (engine.getLanePhase (i));
        laneValueAtomic[(size_t) i].store (engine.getLaneValue (i));
    }
}

void LFlOwAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void LFlOwAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* LFlOwAudioProcessor::createEditor()
{
    return new LFlOwAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LFlOwAudioProcessor();
}
