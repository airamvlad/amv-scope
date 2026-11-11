#pragma once

// Records audio into a fixed number of bins laid out over one window, used in
// trigger mode: each recording starts at position 0 and fills the window once.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "BinStore.h"

namespace amvscope
{

class SyncedCapture
{
public:
    static constexpr std::size_t numBins = 2048;

    SyncedCapture() : store (numBins) {}

    /** Writer thread.
        position: where the first sample sits, in the same unit as windowLength.
        step:     how far each sample moves the position.
        windowLength: length of the shown window, > 0.
        passBase: added to the pass number, so separate recordings that start at
                  position 0 still replace each other.
        bands:    optional per-sample band levels (may be null). */
    void write (const float* samples, std::size_t count,
                double position, double step, double windowLength,
                std::int64_t passBase = 0, const Bands* bands = nullptr) noexcept
    {
        if (windowLength <= 0.0 || step <= 0.0)
            return;

        for (std::size_t i = 0; i < count; ++i)
        {
            const double pos = (position + step * (double) i) / windowLength;
            const double pass = std::floor (pos);
            const auto index = std::min (numBins - 1, (std::size_t) ((pos - pass) * (double) numBins));
            store.add (index, passBase + (std::int64_t) pass, samples[i], bands != nullptr ? bands[i] : Bands {});
        }

        if (count > 0)
        {
            const double end = (position + step * (double) (count - 1)) / windowLength;
            playhead.store ((float) (end - std::floor (end)), std::memory_order_relaxed);
        }
    }

    /** Any thread. */
    BinData readBin (std::size_t index) const noexcept
    {
        return store.read (std::min (index, numBins - 1));
    }

    /** Merged data for bins [first, last). */
    BinData readRange (std::size_t first, std::size_t last) const noexcept
    {
        BinData out;
        for (auto i = first; i < last && i < numBins; ++i)
            out.merge (store.read (i));
        return out;
    }

    /** Where the last written sample sits in the window, 0..1. */
    float getPlayhead() const noexcept { return playhead.load (std::memory_order_relaxed); }

    void clear() noexcept
    {
        store.clear();
        playhead.store (0.0f, std::memory_order_relaxed);
    }

private:
    BinStore store;
    std::atomic<float> playhead { 0.0f };
};

} // namespace amvscope
