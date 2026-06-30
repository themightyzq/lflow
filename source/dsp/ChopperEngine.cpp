#include "ChopperEngine.h"
#include <cmath>

namespace lflow {

void ChopperEngine::prepare (double sr, int /*maxBlock*/) noexcept
{
    sampleRate = (sr > 0.0) ? sr : 44100.0;
    clock.prepare (sampleRate);
    reset();
}

void ChopperEngine::reset() noexcept
{
    clock.reset (0.0);
    lfo.reset();
    smoothL = smoothR = 0.0f;
    currentValue = 0.0f;
}

void ChopperEngine::setTransport (bool playing, double bpm, double ppq) noexcept
{
    hostPlaying = playing;
    if (bpm > 0.0) hostBpm = bpm;
    hostPpq = ppq;
}

// smooth in [0,1] -> one-pole coefficient. 0 = instant; larger = rounder edges.
float ChopperEngine::onePole (float target, float& state) const noexcept
{
    if (params.smooth <= 0.0f) { state = target; return target; }
    const float s = params.smooth;
    const float tc = 0.0005f * static_cast<float> (sampleRate) * (s * s) + 1.0f;
    const float coeff = std::exp (-1.0f / tc);
    state = target + coeff * (state - target);
    return state;
}

void ChopperEngine::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    lfo.setWaveform (params.waveform);

    const double rate = params.sync ? LfoClock::syncedHz (hostBpm, params.cycleBeats)
                                    : params.rateHz;
    if (params.sync && hostPlaying)
        clock.setPhaseFromPpq (hostPpq, params.cycleBeats);

    const float depth = params.depth;
    const float mix   = params.mix;
    const bool  doPan = (params.mode == Mode::Pan) && numChannels >= 2;

    for (int n = 0; n < numSamples; ++n)
    {
        const float ph = static_cast<float> (clock.getPhase());

        const float modL = onePole (lfo.valueAt (ph), smoothL);
        currentValue = modL;

        if (doPan)
        {
            const float phR  = (ph < 0.5f) ? ph + 0.5f : ph - 0.5f;
            const float modR = onePole (lfo.valueAt (phR), smoothR);
            const float gL = 1.0f - depth * modL;
            const float gR = 1.0f - depth * modR;

            float* L = channels[0];
            float* R = channels[1];
            const float dL = L[n], dR = R[n];
            L[n] = dL * (1.0f - mix) + (dL * gL) * mix;
            R[n] = dR * (1.0f - mix) + (dR * gR) * mix;

            for (int c = 2; c < numChannels; ++c)
            {
                float* d = channels[c];
                const float dry = d[n];
                const float g = 1.0f - depth * modL;
                d[n] = dry * (1.0f - mix) + (dry * g) * mix;
            }
        }
        else
        {
            const float g = 1.0f - depth * modL;
            for (int c = 0; c < numChannels; ++c)
            {
                float* d = channels[c];
                const float dry = d[n];
                d[n] = dry * (1.0f - mix) + (dry * g) * mix;
            }
        }

        clock.advance (rate);
    }
}

} // namespace lflow
