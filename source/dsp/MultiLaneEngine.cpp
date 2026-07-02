#include "MultiLaneEngine.h"
#include <cmath>
#include <cstdint>

namespace lflow {

void MultiLaneEngine::prepare (double sr, int /*maxBlock*/) noexcept
{
    sampleRate = (sr > 0.0) ? sr : 44100.0;
    for (auto& lane : lanes)
        lane.clock.prepare (sampleRate);
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

    for (int i = 0; i < kNumLanes; ++i)
    {
        Lane& lane = lanes[i];
        active[i] = lane.params.depth > 0.0f;
        rates[i] = 0.0;
        doPan[i] = false;
        if (! active[i])
            continue;

        anyActive = true;

        lane.lfo.setWaveform (lane.params.waveform);
        lane.lfoR.setWaveform (lane.params.waveform);

        rates[i] = lane.params.sync ? LfoClock::syncedHz (hostBpm, lane.params.cycleBeats)
                                     : lane.params.rateHz;
        if (lane.params.sync && hostPlaying)
            lane.clock.setPhaseFromPpq (hostPpq, lane.params.cycleBeats);

        doPan[i] = (lane.params.dest == Dest::Pan) && numChannels >= 2;
    }

    // Depth-0 lanes are exact no-ops. If every lane is inactive, skip the buffer
    // entirely so it stays bit-identical no matter what mix/smooth are set to.
    if (! anyActive)
        return;

    const float mix = globalParams.mix;

    for (int n = 0; n < numSamples; ++n)
    {
        float gainL = 1.0f;
        float gainR = 1.0f;

        for (int i = 0; i < kNumLanes; ++i)
        {
            if (! active[i]) continue;
            Lane& lane = lanes[i];

            float ph = static_cast<float> (lane.clock.getPhase()) + lane.params.phaseOffset;
            ph -= std::floor (ph);

            const float modL = onePole (lane.lfo.valueAt (ph), lane.smoothL);
            const float depth = lane.params.depth;
            const float gL = 1.0f - depth * modL;
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

            lane.lastPhase = ph;
            lane.lastValue = modL;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            float* d = channels[c];
            const float dry = d[n];
            const float g = (c == 1) ? gainR : gainL;
            d[n] = dry * (1.0f - mix) + (dry * g) * mix;
        }

        for (int i = 0; i < kNumLanes; ++i)
            if (active[i])
                lanes[i].clock.advance (rates[i]);
    }
}

} // namespace lflow
