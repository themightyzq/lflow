#pragma once
#include <cmath>

namespace lflow {

// Phase accumulator in [0,1). Free (Hz) or tempo-synced. Pure C++, RT-safe.
class LfoClock
{
public:
    void prepare (double sampleRateHz) noexcept
    {
        sampleRate = (sampleRateHz > 0.0) ? sampleRateHz : 44100.0;
    }
    void reset (double startPhase = 0.0) noexcept
    {
        phase = startPhase - std::floor (startPhase);
    }

    double getPhase() const noexcept { return phase; }

    // Cycles per second for a synced cycle of `cycleBeats` quarter-notes.
    static double syncedHz (double bpm, double cycleBeats) noexcept
    {
        if (bpm <= 0.0 || cycleBeats <= 0.0) return 0.0;
        return (bpm / 60.0) / cycleBeats;
    }

    // Advance one sample at rateHz; returns the new phase.
    double advance (double rateHz) noexcept
    {
        phase += rateHz / sampleRate;
        phase -= std::floor (phase + 1e-14);  // +epsilon handles floating-point accumulation errors
        if (phase < 0.0) phase = 0.0;
        return phase;
    }

    // Absolute phase from host timeline (beat-aligned).
    void setPhaseFromPpq (double ppqPosition, double cycleBeats) noexcept
    {
        if (cycleBeats <= 0.0) return;
        const double p = ppqPosition / cycleBeats;
        phase = p - std::floor (p);
    }

private:
    double sampleRate { 44100.0 };
    double phase { 0.0 };
};

} // namespace lflow
