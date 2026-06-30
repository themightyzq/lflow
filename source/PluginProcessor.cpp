#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterLayout.h"
#include "params/ParameterIDs.h"
#include "dsp/SyncRate.h"

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

    // Hard bypass: pass input through unchanged.
    if (apvts.getRawParameterValue (lflow::pid::bypass)->load() > 0.5f)
        return;

    // Read parameters (atomic loads) into the engine's param struct.
    lflow::ChopperParams p;
    p.waveform = static_cast<lflow::Waveform> ((int) apvts.getRawParameterValue (lflow::pid::waveform)->load());
    p.sync     = apvts.getRawParameterValue (lflow::pid::sync)->load() > 0.5f;
    p.rateHz   = (double) apvts.getRawParameterValue (lflow::pid::rateHz)->load();
    const auto div = static_cast<lflow::Division> ((int) apvts.getRawParameterValue (lflow::pid::division)->load());
    const auto rhy = static_cast<lflow::Rhythm>   ((int) apvts.getRawParameterValue (lflow::pid::rhythm)->load());
    p.cycleBeats = lflow::cycleBeats (div, rhy);
    p.depth    = apvts.getRawParameterValue (lflow::pid::depth)->load();
    p.mode     = static_cast<lflow::Mode> ((int) apvts.getRawParameterValue (lflow::pid::mode)->load());
    p.mix      = apvts.getRawParameterValue (lflow::pid::mix)->load();
    p.smooth   = apvts.getRawParameterValue (lflow::pid::smooth)->load();
    // setParams runs on the audio thread only (no cross-thread access to the engine's params).
    engine.setParams (p);

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

    engine.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());

    lfoPhaseAtomic.store (engine.getCurrentPhase());
    lfoValueAtomic.store (engine.getCurrentValue());
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
