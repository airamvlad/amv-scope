#pragma once

// Finds the moment the signal rises above a threshold, used to start the
// display when the host transport is not running (like a hardware scope).

#include <cmath>
#include <cstddef>
#include <limits>

namespace amvscope
{

class TriggerDetector
{
public:
    /** threshold: linear level, > 0. holdoff: samples to ignore after a trigger. */
    void setThreshold (float linear) noexcept { threshold = linear; }
    void setHoldoff (std::size_t samples) noexcept { holdoff = samples; }

    void reset() noexcept
    {
        armed = false;
        sinceTrigger = std::numeric_limits<std::size_t>::max();   // ready right away
    }

    /** Returns the index of the trigger in the block, or -1. At most one trigger
        per block; a later crossing in the same block waits for the next block. */
    long process (const float* samples, std::size_t count) noexcept
    {
        long found = -1;

        for (std::size_t i = 0; i < count; ++i)
        {
            const float level = std::abs (samples[i]);

            if (sinceTrigger < holdoff)
                ++sinceTrigger;

            // Re-arm once the signal has been below half the threshold (hysteresis).
            if (level < threshold * 0.5f)
                armed = true;

            if (found < 0 && armed && level >= threshold && sinceTrigger >= holdoff)
            {
                armed = false;
                sinceTrigger = 0;
                found = (long) i;
            }
        }

        return found;
    }

private:
    float threshold = 0.1f;
    std::size_t holdoff = 0;
    std::size_t sinceTrigger = std::numeric_limits<std::size_t>::max();
    bool armed = false;
};

} // namespace amvscope
