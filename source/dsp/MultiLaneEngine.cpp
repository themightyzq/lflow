#include "MultiLaneEngine.h"
#include <cmath>
#include <cstdint>

namespace lflow {

void MultiLaneEngine::prepare (double sr, int /*maxBlock*/) noexcept
{
    sampleRate = (sr > 0.0) ? sr : 44100.0;
    for (auto& lane : lanes)
        lane.clock.prepare (sampleRate);

    // LR4Crossover::prepare() resets to its own internal default frequency, so
    // (re)apply our current xoverLow/HighHz unconditionally here -- independent of
    // setCrossovers()'s change-compare, which only guards against redundant
    // recomputation for identical Hz values already applied.
    for (int c = 0; c < kNumBandChannels; ++c)
    {
        lowSplit[c].prepare (sampleRate);
        highSplit[c].prepare (sampleRate);
        lowSplit[c].setFrequency (xoverLowHz);
        highSplit[c].setFrequency (xoverHighHz);
    }

    reset();
}

void MultiLaneEngine::reset() noexcept
{
    // Distinct seeds per lane and per channel so S&H lanes don't lock-step together
    // (preserves the Phase 1 fix at 3x scale).
    static constexpr std::uint32_t seedsL[kNumLanes] = { 0x1234567u, 0x2468ACEu, 0x3579BDFu };
    static constexpr std::uint32_t seedsR[kNumLanes] = { 0xA5A5A5u,  0x5A5A5Au,  0xC3C3C3u };

    for (int i = 0; i < kNumLanes; ++i)
    {
        Lane& lane = lanes[i];
        lane.clock.reset (0.0);
        lane.lfo.reset (seedsL[i]);
        lane.lfoR.reset (seedsR[i]);
        lane.smoothL = lane.smoothR = 0.0f;
        lane.lastPhase = 0.0f;
        lane.lastValue = 0.0f;
    }

    for (int c = 0; c < kNumBandChannels; ++c)
    {
        lowSplit[c].reset();
        highSplit[c].reset();
    }
    bandWasActive = false;
}

void MultiLaneEngine::setCrossovers (double lowHz, double highHz) noexcept
{
    const double newLow  = lowHz;
    const double newHigh = (highHz > lowHz * 1.25) ? highHz : lowHz * 1.25;

    const bool lowChanged  = (newLow  != xoverLowHz);
    const bool highChanged = (newHigh != xoverHighHz);

    if (! lowChanged && ! highChanged)
        return; // cheap compare-before-set: no coefficient recompute needed

    xoverLowHz  = newLow;
    xoverHighHz = newHigh;

    if (lowChanged)
        for (int c = 0; c < kNumBandChannels; ++c)
            lowSplit[c].setFrequency (xoverLowHz);

    if (highChanged)
        for (int c = 0; c < kNumBandChannels; ++c)
            highSplit[c].setFrequency (xoverHighHz);
}

void MultiLaneEngine::setTransport (bool playing, double bpm, double ppq) noexcept
{
    hostPlaying = playing;
    if (bpm > 0.0) hostBpm = bpm;
    hostPpq = ppq;
}

void MultiLaneEngine::setLaneParams (int lane, const LaneParams& p) noexcept
{
    if (lane < 0 || lane >= kNumLanes) return;
    lanes[lane].params = p;
}

void MultiLaneEngine::setGlobalParams (const GlobalParams& g) noexcept
{
    globalParams = g;
}

float MultiLaneEngine::getLanePhase (int lane) const noexcept
{
    if (lane < 0 || lane >= kNumLanes) return 0.0f;
    return lanes[lane].lastPhase;
}

float MultiLaneEngine::getLaneValue (int lane) const noexcept
{
    if (lane < 0 || lane >= kNumLanes) return 0.0f;
    return lanes[lane].lastValue;
}

// smooth in [0,1] -> one-pole coefficient. 0 = instant; larger = rounder edges.
// Identical math to Phase 1 ChopperEngine::onePole; smooth is a global control here.
float MultiLaneEngine::onePole (float target, float& state) const noexcept
{
    if (globalParams.smooth <= 0.0f) { state = target; return target; }
    const float s = globalParams.smooth;
    const float tc = 0.0005f * static_cast<float> (sampleRate) * (s * s) + 1.0f;
    const float coeff = std::exp (-1.0f / tc);
    state = target + coeff * (state - target);
    return state;
}

void MultiLaneEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    bool   active[kNumLanes];
    bool   doPan[kNumLanes];
    double rates[kNumLanes];
    bool   anyActive = false;
    bool   bandActive = false;

    for (int i = 0; i < kNumLanes; ++i)
    {
        Lane& lane = lanes[i];
        active[i] = lane.params.depth > 0.0f;
        doPan[i]  = false;

        // Link lockstep fix (folded from Phase 2 final review): every lane's clock
        // rate and sync-phase-reset are computed/applied regardless of depth, so a
        // lane sitting at depth 0 for a while and later reactivated resumes at the
        // phase it WOULD have had (Link-linked lanes stay in lockstep during
        // free-run; unlinked lanes correctly reflect elapsed time). Only waveform
        // evaluation, smoothing, and gain application (below) are still gated on
        // depth>0 -- the depth-0 bit-transparency guarantee holds because clock
        // advance never touches the audio buffer. Note: a SampleHold lane's own
        // wrap-detection state was frozen while inactive, so it may fire one
        // spurious "roll" right at reactivation -- acceptable, cosmetic.
        rates[i] = lane.params.sync ? LfoClock::syncedHz (hostBpm, lane.params.cycleBeats)
                                     : lane.params.rateHz;
        if (lane.params.sync && hostPlaying)
            lane.clock.setPhaseFromPpq (hostPpq, lane.params.cycleBeats);

        if (! active[i])
            continue;

        anyActive = true;

        lane.lfo.setWaveform (lane.params.waveform);
        lane.lfoR.setWaveform (lane.params.waveform);

        doPan[i] = (lane.params.dest == Dest::Pan) && numChannels >= 2;

        if (lane.params.dest == Dest::Low || lane.params.dest == Dest::Mid || lane.params.dest == Dest::High)
            bandActive = true;
    }

    // Engaged -> disengaged transition: clear crossover filter memory. A one-time
    // transient from this reset is acceptable/inaudible at gain parity (see the
    // Phase 3 design doc) -- far cheaper than keeping the split running dry.
    if (bandWasActive && ! bandActive)
        for (int c = 0; c < kNumBandChannels; ++c)
        {
            lowSplit[c].reset();
            highSplit[c].reset();
        }
    bandWasActive = bandActive;

    // Depth-0-everywhere: the buffer is an exact no-op (bit-identical regardless of
    // mix/smooth), but lane clocks still advance below per the lockstep fix above.
    if (! anyActive)
    {
        for (int n = 0; n < numSamples; ++n)
            for (int i = 0; i < kNumLanes; ++i)
                lanes[i].clock.advance (rates[i]);
        return;
    }

    const float mix = globalParams.mix;
    const int chLimit = (numChannels < kNumBandChannels) ? numChannels : kNumBandChannels;

    for (int n = 0; n < numSamples; ++n)
    {
        float gainL = 1.0f;
        float gainR = 1.0f;
        float bandGain[3] = { 1.0f, 1.0f, 1.0f }; // Low, Mid, High (mono, applied to both channels)

        for (int i = 0; i < kNumLanes; ++i)
        {
            if (! active[i]) continue;
            Lane& lane = lanes[i];

            float ph = static_cast<float> (lane.clock.getPhase()) + lane.params.phaseOffset;
            ph -= std::floor (ph);

            const float modL = onePole (lane.lfo.valueAt (ph), lane.smoothL);
            const float depth = lane.params.depth;
            const float gL = 1.0f - depth * modL;

            switch (lane.params.dest)
            {
                case Dest::Pan:
                    gainL *= gL;
                    if (doPan[i])
                    {
                        float phR = ph + 0.5f;
                        phR -= std::floor (phR);
                        const float modR = onePole (lane.lfoR.valueAt (phR), lane.smoothR);
                        gainR *= (1.0f - depth * modR);
                    }
                    else
                    {
                        gainR *= gL;
                    }
                    break;

                case Dest::Low:  bandGain[0] *= gL; break;
                case Dest::Mid:  bandGain[1] *= gL; break;
                case Dest::High: bandGain[2] *= gL; break;

                case Dest::Volume:
                default:
                    gainL *= gL;
                    gainR *= gL;
                    break;
            }

            lane.lastPhase = ph;
            lane.lastValue = modL;
        }

        // Band split -> per-band gain -> sum. Only runs when >=1 lane targets a band
        // with depth>0 (bandActive); otherwise the full-band path below is untouched,
        // keeping it byte-identical to Phase 2. numChannels==1 (mono) uses only
        // chain index 0; channels beyond index 1 are ignored for splitting (Volume/
        // Pan lanes still apply to them via the loop below, same as Phase 2).
        float bandSummed[kNumBandChannels] = { 0.0f, 0.0f };
        if (bandActive)
        {
            for (int c = 0; c < chLimit; ++c)
            {
                const float in = channels[c][n];
                float low = 0.0f, rest = 0.0f, mid = 0.0f, high = 0.0f;
                lowSplit[c].split (in, low, rest);
                highSplit[c].split (rest, mid, high);
                bandSummed[c] = low * bandGain[0] + mid * bandGain[1] + high * bandGain[2];
            }
        }

        for (int c = 0; c < numChannels; ++c)
        {
            float* d = channels[c];
            const float dry = d[n];
            const float g = (c == 1) ? gainR : gainL;
            const float wetSource = (bandActive && c < kNumBandChannels) ? bandSummed[c] : dry;
            d[n] = dry * (1.0f - mix) + (wetSource * g) * mix;
        }

        for (int i = 0; i < kNumLanes; ++i)
            lanes[i].clock.advance (rates[i]);
    }
}

} // namespace lflow
