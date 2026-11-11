#pragma once

// Records audio into a fixed number of display slots ("bins") laid out over a
// musical window, e.g. one beat or one bar. Each incoming sample lands in the
// bin that matches its position inside the window, so the picture stays still
// as long as the music repeats.
//
// The audio thread calls write(); the UI thread calls readBin(). Every value is
// an atomic, so reading while the audio thread writes is safe (a bin may show
// a mix of this pass and the previous one for one frame, which is invisible).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace amvscope
{

class SyncedCapture
{
public:
    static constexpr std::size_t numBins = 2048;

    struct Bin
    {
        float min = 0.0f;
        float max = 0.0f;
        bool filled = false;   // false: nothing recorded in this pass yet
    };

    SyncedCapture() { clear(); }

    /** Audio thread.
        position: where the first sample sits, in the same unit as windowLength
                  (quarter notes for host sync).
        step:     how far each sample moves the position (quarter notes per sample).
        windowLength: length of the shown window, > 0.
        passBase: added to the pass number, so separate recordings that start at
                  position 0 (trigger mode) still replace each other. */
    void write (const float* samples, std::size_t count,
                double position, double step, double windowLength,
                std::int64_t passBase = 0) noexcept
    {
        if (windowLength <= 0.0 || step <= 0.0)
            return;

        for (std::size_t i = 0; i < count; ++i)
        {
            const double pos = position + step * (double) i;
            const double pass = std::floor (pos / windowLength);
            const double phase = pos / windowLength - pass;
            const auto index = std::min (numBins - 1, (std::size_t) (phase * (double) numBins));
            const auto passId = passBase + (std::int64_t) pass;

            auto& b = bins[index];
            const float v = samples[i];

            if (b.pass.load (std::memory_order_relaxed) != passId)
            {
                // First sample of a new pass in this bin: start over.
                b.min.store (v, std::memory_order_relaxed);
                b.max.store (v, std::memory_order_relaxed);
                b.pass.store (passId, std::memory_order_relaxed);
            }
            else
            {
                if (v < b.min.load (std::memory_order_relaxed)) b.min.store (v, std::memory_order_relaxed);
                if (v > b.max.load (std::memory_order_relaxed)) b.max.store (v, std::memory_order_relaxed);
            }
        }

        if (count > 0)
        {
            const double end = position + step * (double) (count - 1);
            playhead.store ((float) (end / windowLength - std::floor (end / windowLength)),
                            std::memory_order_relaxed);
        }
    }

    /** Any thread. */
    Bin readBin (std::size_t index) const noexcept
    {
        const auto& b = bins[std::min (index, numBins - 1)];
        Bin out;
        out.filled = b.pass.load (std::memory_order_relaxed) != emptyPass;
        out.min = b.min.load (std::memory_order_relaxed);
        out.max = b.max.load (std::memory_order_relaxed);
        return out;
    }

    /** Where the last written sample sits in the window, 0..1. */
    float getPlayhead() const noexcept { return playhead.load (std::memory_order_relaxed); }

    /** Not real-time safe against write(); call when the audio thread is not writing,
        or accept that a few bins may keep old data. */
    void clear() noexcept
    {
        for (auto& b : bins)
        {
            b.min.store (0.0f, std::memory_order_relaxed);
            b.max.store (0.0f, std::memory_order_relaxed);
            b.pass.store (emptyPass, std::memory_order_relaxed);
        }
        playhead.store (0.0f, std::memory_order_relaxed);
    }

private:
    static constexpr std::int64_t emptyPass = INT64_MIN;

    struct AtomicBin
    {
        std::atomic<float> min { 0.0f }, max { 0.0f };
        std::atomic<std::int64_t> pass { emptyPass };
    };

    std::array<AtomicBin, numBins> bins;
    std::atomic<float> playhead { 0.0f };
};

} // namespace amvscope
