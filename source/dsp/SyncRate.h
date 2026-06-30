#pragma once

namespace lflow {

enum class Division { D1_1 = 0, D1_2, D1_4, D1_8, D1_16, D1_32 };
enum class Rhythm   { Straight = 0, Dotted, Triplet };

// Length of one LFO cycle in quarter-note beats.
inline double cycleBeats (Division d, Rhythm r) noexcept
{
    double base = 1.0;
    switch (d)
    {
        case Division::D1_1:  base = 4.0;   break;
        case Division::D1_2:  base = 2.0;   break;
        case Division::D1_4:  base = 1.0;   break;
        case Division::D1_8:  base = 0.5;   break;
        case Division::D1_16: base = 0.25;  break;
        case Division::D1_32: base = 0.125; break;
    }
    switch (r)
    {
        case Rhythm::Straight: return base;
        case Rhythm::Dotted:   return base * 1.5;
        case Rhythm::Triplet:  return base * (2.0 / 3.0);
    }
    return base;
}

} // namespace lflow
