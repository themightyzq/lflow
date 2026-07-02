#pragma once
#include <cmath>
#include <cstdint>
#include "ShapeModel.h"

namespace lflow {

// Custom is appended after SampleHold (index 6) -- Phase 4 drawable shapes. Existing
// indices are untouched (session/param compatibility).
enum class Waveform { Sine = 0, Triangle, Square, SawUp, SawDown, SampleHold, Custom };

// Pure waveform generator. Given a phase in [0,1), returns a unipolar value in [0,1].
// No JUCE dependency — unit-testable headless. RT-safe (no allocation, no locks).
class LfoCore
{
public:
    void setWaveform (Waveform w) noexcept { waveform = w; }
    Waveform getWaveform() const noexcept { return waveform; }

    // Sets the lookup table backing Waveform::Custom. data/size are just borrowed
    // (no copy, no ownership, no allocation) -- caller (MultiLaneEngine) must keep the
    // pointer valid for as long as it's set here. Pass nullptr (size ignored) to unset;
    // Custom then falls back to Sine (see valueAt).
    void setCustomTable (const float* data, int size) noexcept
    {
        customTable = data;
        customTableSize = (data != nullptr) ? size : 0;
    }

    // Reset S&H state and RNG. seed must be non-zero.
    void reset (std::uint32_t seed = 0x1234567u) noexcept
    {
        rngState = (seed != 0u) ? seed : 0x1234567u;
        lastPhase = 2.0f;          // force a fresh step on first S&H eval
    }

    float valueAt (float phase) noexcept
    {
        switch (waveform)
        {
            case Waveform::Sine:     return 0.5f + 0.5f * std::sin (twoPi * phase);
            case Waveform::Triangle: return 1.0f - std::fabs (2.0f * phase - 1.0f);
            case Waveform::Square:   return phase < 0.5f ? 1.0f : 0.0f;
            case Waveform::SawUp:    return phase;
            case Waveform::SawDown:  return 1.0f - phase;
            case Waveform::SampleHold:
                if (phase < lastPhase) currentStep = nextRandom(); // wrap detected
                lastPhase = phase;
                return currentStep;
            case Waveform::Custom:
                if (customTable != nullptr && customTableSize > 0)
                    return shapeTableValue (customTable, customTableSize, phase);
                // Defensive fallback: no table set yet -> silent Sine instead of 0/NaN.
                return 0.5f + 0.5f * std::sin (twoPi * phase);
        }
        return 0.0f;
    }

private:
    float nextRandom() noexcept
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (rngState & 0xFFFFFFu) / static_cast<float> (0xFFFFFF);
    }

    static constexpr float twoPi = 6.28318530717958647692f;
    Waveform waveform { Waveform::Sine };
    std::uint32_t rngState { 0x1234567u };
    float lastPhase { 2.0f };
    float currentStep { 0.0f };
    const float* customTable { nullptr };
    int customTableSize { 0 };
};

} // namespace lflow
