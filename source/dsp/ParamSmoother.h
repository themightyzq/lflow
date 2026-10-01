#pragma once

namespace lflow {

// Linear-ramp parameter smoother: the pure C++ counterpart of juce::SmoothedValue<> with
// ValueSmoothingTypes::Linear. source/dsp/ must not include any JUCE header (project rule: the
// DSP units are unit-tested headless), so the engine cannot use juce::SmoothedValue directly.
//
// A new target starts a ramp of exactly `rampSamples` steps from the current value, and the
// last step lands on the target EXACTLY (no asymptotic tail), so a settled smoother is
// bit-identical to the un-smoothed parameter. Setting the target it already has is a no-op, so
// re-publishing the same host value every block never restarts a ramp. With rampSamples == 0 the
// smoother is a plain pass-through (the target applies immediately).
//
// The smoother lives inside the DSP engine and only shapes the value the engine uses; it never
// writes back to the plugin parameter. RT-safe: arithmetic only, no allocation.
class ParamSmoother
{
public:
    void setRampSamples (int n) noexcept { rampSamples = n > 0 ? n : 0; }

    // Jump straight to v, cancelling any ramp in progress.
    void snapTo (double v) noexcept
    {
        current = target = v;
        stepsLeft = 0;
        step = 0.0;
    }

    void setTarget (double v) noexcept
    {
        if (! (v < target) && ! (v > target))
            return;   // same target (written without ==, which -Wfloat-equal rejects)

        target = v;
        if (rampSamples <= 0)
        {
            current = v;
            stepsLeft = 0;
            step = 0.0;
            return;
        }

        stepsLeft = rampSamples;
        step = (target - current) / static_cast<double> (rampSamples);
    }

    // Advances one sample and returns the new value.
    double next() noexcept
    {
        if (stepsLeft > 0)
        {
            if (--stepsLeft == 0)
                current = target;   // land exactly, never an asymptote
            else
                current += step;
        }
        return current;
    }

    double getCurrent() const noexcept { return current; }
    double getTarget() const noexcept { return target; }
    bool isSmoothing() const noexcept { return stepsLeft > 0; }

private:
    int    rampSamples { 0 };
    int    stepsLeft   { 0 };
    double current     { 0.0 };
    double target      { 0.0 };
    double step        { 0.0 };
};

} // namespace lflow
