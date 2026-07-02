#pragma once
#include "LfoCore.h"

namespace lflow {

// Volume/Pan keep indices 0/1 for Phase 1/2 session compat; Low/Mid/High are an
// append for Phase 3 band-level lane destinations (see docs/superpowers/specs/
// 2026-07-02-lflow-phase3-multiband-design.md). Pitch is a further append for
// Phase 5 (LFO-modulated delay / vibrato -- see docs/superpowers/specs/
// 2026-07-02-lflow-phase5-pitch-vibrato-design.md).
enum class Dest { Volume = 0, Pan, Low, Mid, High, Pitch };

// Per-lane parameters for MultiLaneEngine. Pure data, no JUCE.
struct LaneParams
{
    Waveform waveform { Waveform::Sine };
    bool   sync        { false };
    double rateHz      { 1.0 };
    double cycleBeats  { 1.0 };
    float  depth       { 0.0f };   // 0..1
    Dest   dest        { Dest::Volume };
    float  phaseOffset { 0.0f };   // [0,1)
};

// Link ganging: when link is true, copies the "motion" fields (waveform, sync, rateHz,
// cycleBeats) from lanes[0] onto lanes[1..numLanes-1]. depth/dest/phaseOffset are each
// lane's own "identity" and are left untouched. No-op when link is false.
inline void resolveLinkedLanes (LaneParams* lanes, int numLanes, bool link) noexcept
{
    if (! link || lanes == nullptr || numLanes < 2)
        return;

    const LaneParams& src = lanes[0];
    for (int i = 1; i < numLanes; ++i)
    {
        lanes[i].waveform   = src.waveform;
        lanes[i].sync       = src.sync;
        lanes[i].rateHz     = src.rateHz;
        lanes[i].cycleBeats = src.cycleBeats;
    }
}

} // namespace lflow
