#pragma once
#include "LfoCore.h"
#include "LfoClock.h"

namespace lflow {

enum class Mode { Tremolo = 0, Pan };

struct ChopperParams
{
    Waveform waveform { Waveform::Sine };
    bool   sync       { true };
    double rateHz     { 1.0 };   // used when !sync
    double cycleBeats { 1.0 };   // used when sync (from SyncRate::cycleBeats)
    float  depth      { 0.5f };  // 0..1
    Mode   mode       { Mode::Tremolo };
    float  mix        { 1.0f };  // 0..1
    float  smooth     { 0.15f }; // 0..1
};

// Orchestrates LfoCore + LfoClock and applies tremolo/pan + dry/wet to a buffer.
// Pure C++, RT-safe: no allocation, no locks. Operates in place on raw channel pointers.
class ChopperEngine
{
public:
    void prepare (double sampleRate, int maxBlock) noexcept;
    void reset() noexcept;

    void setTransport (bool playing, double bpm, double ppqPosition) noexcept;
    void setParams (const ChopperParams& p) noexcept { params = p; }

    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    float getCurrentValue() const noexcept { return currentValue; }
    float getCurrentPhase() const noexcept { return static_cast<float> (clock.getPhase()); }

private:
    float onePole (float target, float& state) const noexcept;

    LfoCore  lfo;
    LfoClock clock;
    ChopperParams params;

    double sampleRate { 44100.0 };
    bool   hostPlaying { false };
    double hostBpm     { 120.0 };
    double hostPpq     { 0.0 };

    float smoothL { 0.0f };
    float smoothR { 0.0f };
    float currentValue { 0.0f };
};

} // namespace lflow
