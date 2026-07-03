#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterLayout.h"
#include "params/ParameterIDs.h"
#include "dsp/SyncRate.h"

namespace {

// Reads one lane's parameters (atomic loads only, no string-keyed lookups -- QA L2) into a
// LaneParams from a pre-cached LaneParamPtrs. Local helper, not a member, so it can be a free
// function taking the pointer set explicitly per lane.
lflow::LaneParams readLaneParams (const lflow::LaneParamPtrs& p)
{
    lflow::LaneParams result;
    result.waveform = static_cast<lflow::Waveform> ((int) p.waveform->load());
    result.sync     = p.sync->load() > 0.5f;
    result.rateHz   = (double) p.rateHz->load();
    const auto div  = static_cast<lflow::Division> ((int) p.division->load());
    const auto rhy  = static_cast<lflow::Rhythm>   ((int) p.rhythm->load());
    result.cycleBeats = lflow::cycleBeats (div, rhy);
    result.depth       = p.depth->load();
    result.dest        = static_cast<lflow::Dest> ((int) p.dest->load());
    const float degrees = p.phase->load();
    result.phaseOffset  = degrees / 360.0f;
    return result;
}

// Populates one lane's cached pointers from the given APVTS + id set. Message-thread-only
// (called once, from the ctor). Kept as a free function (mirrors readLaneParams above) rather
// than a member so the id-set-per-lane shape stays visible at each call site.
void cacheLaneParams (juce::AudioProcessorValueTreeState& apvts, lflow::LaneParamPtrs& lp,
                       const char* waveformId, const char* syncId, const char* rateHzId,
                       const char* divisionId, const char* rhythmId, const char* phaseId,
                       const char* depthId, const char* destId)
{
    lp.waveform = apvts.getRawParameterValue (waveformId);
    lp.sync     = apvts.getRawParameterValue (syncId);
    lp.rateHz   = apvts.getRawParameterValue (rateHzId);
    lp.division = apvts.getRawParameterValue (divisionId);
    lp.rhythm   = apvts.getRawParameterValue (rhythmId);
    lp.phase    = apvts.getRawParameterValue (phaseId);
    lp.depth    = apvts.getRawParameterValue (depthId);
    lp.dest     = apvts.getRawParameterValue (destId);
}

} // namespace

LFlOwAudioProcessor::LFlOwAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", lflow::createParameterLayout())
{
    bypassParam = apvts.getParameter (lflow::pid::bypass);

    // Cache every processBlock-read parameter as a raw atomic pointer, once, here (message
    // thread, right after apvts finishes constructing) -- QA L2. See CachedParams' doc comment
    // in PluginProcessor.h for why these pointers stay valid across setStateInformation's later
    // apvts.replaceState() calls.
    cacheLaneParams (apvts, cachedParams.lane[0],
                      lflow::pid::l1Waveform, lflow::pid::l1Sync, lflow::pid::l1RateHz,
                      lflow::pid::l1Division, lflow::pid::l1Rhythm, lflow::pid::l1Phase,
                      lflow::pid::l1Depth, lflow::pid::l1Dest);
    cacheLaneParams (apvts, cachedParams.lane[1],
                      lflow::pid::l2Waveform, lflow::pid::l2Sync, lflow::pid::l2RateHz,
                      lflow::pid::l2Division, lflow::pid::l2Rhythm, lflow::pid::l2Phase,
                      lflow::pid::l2Depth, lflow::pid::l2Dest);
    cacheLaneParams (apvts, cachedParams.lane[2],
                      lflow::pid::l3Waveform, lflow::pid::l3Sync, lflow::pid::l3RateHz,
                      lflow::pid::l3Division, lflow::pid::l3Rhythm, lflow::pid::l3Phase,
                      lflow::pid::l3Depth, lflow::pid::l3Dest);

    cachedParams.bypass    = apvts.getRawParameterValue (lflow::pid::bypass);
    cachedParams.link      = apvts.getRawParameterValue (lflow::pid::link);
    cachedParams.mix       = apvts.getRawParameterValue (lflow::pid::mix);
    cachedParams.smooth    = apvts.getRawParameterValue (lflow::pid::smooth);
    cachedParams.xoverLow  = apvts.getRawParameterValue (lflow::pid::xoverLow);
    cachedParams.xoverHigh = apvts.getRawParameterValue (lflow::pid::xoverHigh);

    // Constructed AFTER apvts: ensures/loads the SHAPES ValueTree subtree and does the
    // initial bake+publish for all 3 lanes (shapeBuffers already default-constructed above).
    shapeManager = std::make_unique<lflow::ShapeManager> (apvts, shapeBuffers);
}

void LFlOwAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);

    // ~30 ms bypass crossfade ramp, click-free. Seed from the ACTUAL bypass state so a
    // session loaded (or re-prepared) while bypassed doesn't leak 30 ms of wet signal.
    const bool bypassedNow = cachedParams.bypass->load() > 0.5f;
    bypassGain.reset (sampleRate, 0.03);
    bypassGain.setCurrentAndTargetValue (bypassedNow ? 0.0f : 1.0f);

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

    const bool bypassed = cachedParams.bypass->load() > 0.5f;
    bypassGain.setTargetValue (bypassed ? 0.0f : 1.0f);

    const bool link = cachedParams.link->load() > 0.5f;

    lflow::LaneParams lanes[lflow::MultiLaneEngine::kNumLanes] =
    {
        readLaneParams (cachedParams.lane[0]),
        readLaneParams (cachedParams.lane[1]),
        readLaneParams (cachedParams.lane[2]),
    };

    lflow::resolveLinkedLanes (lanes, lflow::MultiLaneEngine::kNumLanes, link);

    for (int i = 0; i < lflow::MultiLaneEngine::kNumLanes; ++i)
        engine.setLaneParams (i, lanes[i]);

    lflow::GlobalParams g;
    g.mix    = cachedParams.mix->load();
    g.smooth = cachedParams.smooth->load();
    engine.setGlobalParams (g);

    const double xoverLow  = (double) cachedParams.xoverLow->load();
    const double xoverHigh = (double) cachedParams.xoverHigh->load();
    engine.setCrossovers (xoverLow, xoverHigh);

    // Custom-shape tables: acquire() is wait-free (single atomic exchange at most), so it's
    // safe to call once per lane, per block, right here on the audio thread. Linked-follower
    // routing: when Link is ON, lanes 1-2 (indices 1,2) forward LANE 0's acquired table
    // instead of their own, mirroring resolveLinkedLanes() copying lane 0's waveform choice to
    // the followers above (Phase 4 design: the shape itself isn't a motion field, but
    // `waveform` is, so a linked Custom follower tracks lane 0's shape).
    const lflow::ShapeTable* acquired[lflow::MultiLaneEngine::kNumLanes];
    for (int i = 0; i < lflow::MultiLaneEngine::kNumLanes; ++i)
        acquired[i] = shapeBuffers[i].acquire();

    for (int i = 0; i < lflow::MultiLaneEngine::kNumLanes; ++i)
    {
        const int sourceLane = (link && i != 0) ? 0 : i;
        const auto* table = acquired[sourceLane];
        const bool published = shapeBuffers[sourceLane].hasEverPublished();
        engine.setCustomTable (i, published ? table->data : nullptr, lflow::kShapeTableSize);
    }

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

    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) kMaxChannels);
    const int numSamples  = buffer.getNumSamples();

    // Process in chunks no larger than the preallocated scratch capacity, so a host that
    // hands us a block bigger than prepareToPlay's samplesPerBlock (offline bounces do)
    // can never overrun dryScratch. No allocation on this thread, ever.
    auto* const* channelData = buffer.getArrayOfWritePointers();
    const int scratchCapacity = dryScratch.getNumSamples();

    for (int offset = 0; offset < numSamples;)
    {
        const int chunk = juce::jmin (numSamples - offset, scratchCapacity);

        // Snapshot dry (preallocated scratch; copyFrom never reallocates).
        for (int c = 0; c < numChannels; ++c)
            dryScratch.copyFrom (c, 0, buffer, c, offset, chunk);

        // Engine always runs, even fully bypassed, so the UI display keeps animating.
        float* chunkChans[kMaxChannels];
        for (int c = 0; c < numChannels; ++c)
            chunkChans[c] = channelData[c] + offset;
        engine.process (chunkChans, numChannels, chunk);

        for (int n = 0; n < chunk; ++n)
        {
            const float wetGain = bypassGain.getNextValue();
            const float dryGain = 1.0f - wetGain;
            for (int c = 0; c < numChannels; ++c)
            {
                const float wet = chunkChans[c][n];
                const float dry = dryScratch.getSample (c, n);
                chunkChans[c][n] = wet * wetGain + dry * dryGain;
            }
        }

        offset += chunk;
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
    // QA M1: some hosts call setStateInformation() off the message thread. apvts.replaceState()
    // reassigns apvts.state to a new juce::ValueTree, which fires ShapeManager's
    // valueTreeRedirected() -> ensureShapesTree() -> rebakeAndPublishAll() (mutating the tree,
    // baking, and publishing to the lock-free ShapeTableBuffer) -- and the editor's 60 Hz timer
    // concurrently READS that same tree via getNodes() on the message thread. juce::ValueTree is
    // not thread-safe, so applying replaceState() directly from an arbitrary caller thread would
    // race both. Parsing the XML into a ValueTree is pure/read-only (no APVTS mutation, no
    // shared-tree touch) and safe to do on whichever thread called us; only the actual APPLY
    // (apvts.replaceState()) needs to happen on the message thread.
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        apvts.replaceState (tree);
        return;
    }

    // Off the message thread: marshal the apply. `tree` is captured BY VALUE into the lambda --
    // juce::ValueTree is a lightweight, ref-counted handle (like a shared_ptr to its underlying
    // SharedObject), so this copy is cheap and does NOT touch the live/old apvts.state tree that
    // the message thread may be reading/writing concurrently; it only bumps a refcount on the
    // newly-parsed, not-yet-installed tree. The lambda runs later, asynchronously, on the message
    // thread (per juce::MessageManager::callAsync's contract) -- by which point the host could
    // have destroyed this processor (e.g. a fast plugin-scan load/unload racing the state-set
    // call), since callAsync's completion is not ordered against, or guaranteed to happen before,
    // the processor's destructor. `safeThis` (a juce::WeakReference, backed by
    // JUCE_DECLARE_WEAK_REFERENCEABLE in the header) is checked inside the lambda immediately
    // before touching `apvts`; if the processor is already gone, safeThis.get() returns nullptr
    // and the apply is silently skipped (there is nothing left to apply state to).
    juce::WeakReference<LFlOwAudioProcessor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis, tree]
    {
        if (auto* self = safeThis.get())
            self->apvts.replaceState (tree);
    });
}

juce::AudioProcessorEditor* LFlOwAudioProcessor::createEditor()
{
    return new LFlOwAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LFlOwAudioProcessor();
}
