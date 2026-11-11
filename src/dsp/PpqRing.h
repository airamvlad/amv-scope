#pragma once

// Remembers the last 32 quarter notes of a signal, placed by musical position
// (host PPQ). Because every AMVScope instance uses the same host position,
// rings from different tracks line up exactly, so one window can draw them all.
//
// Two resolutions are kept: a fine one for short windows and a coarse one for
// long windows, so drawing never has to merge thousands of bins per pixel.
//
// One thread writes (the owner's audio thread); any thread reads.

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "BinStore.h"

namespace amvscope
{

class PpqRing
{
public:
    static constexpr double ringQuarters = 32.0;              // enough for 4 bars of 8/4
    static constexpr std::int64_t fineBinsPerQuarter = 2048;  // 512 per 1/16 note
    static constexpr std::int64_t coarseBinsPerQuarter = 256;

    PpqRing()
        : fine ((std::size_t) (ringQuarters * fineBinsPerQuarter)),
          coarse ((std::size_t) (ringQuarters * coarseBinsPerQuarter)) {}

    /** Writer thread. ppq: position of the first sample; step: quarter notes per sample. */
    void write (const float* samples, const Bands* bands, std::size_t count, double ppq, double step) noexcept
    {
        if (step <= 0.0 || count == 0)
            return;

        for (std::size_t i = 0; i < count; ++i)
        {
            const double pos = ppq + step * (double) i;
            const Bands b = bands != nullptr ? bands[i] : Bands {};
            add (fine, fineBinsPerQuarter, pos, samples[i], b);
            add (coarse, coarseBinsPerQuarter, pos, samples[i], b);
        }

        head.store (ppq + step * (double) (count - 1), std::memory_order_release);
        everWritten.store (true, std::memory_order_release);
    }

    /** Position of the newest sample, in quarter notes. */
    double getHead() const noexcept { return head.load (std::memory_order_acquire); }
    bool hasData() const noexcept { return everWritten.load (std::memory_order_acquire); }

    /** Merged data for [fromQ, toQ). Bins written in a different lap of the ring
        (older or newer than this range) are ignored. */
    BinData read (double fromQ, double toQ) const noexcept
    {
        // Use fine bins unless that would mean merging lots of them.
        const bool useFine = (toQ - fromQ) * (double) fineBinsPerQuarter <= 32.0;
        const auto& store = useFine ? fine : coarse;
        const auto perQuarter = useFine ? fineBinsPerQuarter : coarseBinsPerQuarter;
        const auto size = (std::int64_t) store.size();

        // Round both ends to the nearest bin edge, so neighbouring ranges tile
        // without overlapping and a shifted read stays the same width.
        auto first = (std::int64_t) std::floor (fromQ * (double) perQuarter + 0.5);
        auto last = (std::int64_t) std::floor (toQ * (double) perQuarter + 0.5);
        if (last <= first)
            last = first + 1;

        BinData out;
        for (auto n = first; n < last; ++n)
        {
            const auto lap = floorDiv (n, size);
            out.merge (store.read ((std::size_t) (n - lap * size), lap));
        }
        return out;
    }

    void clear() noexcept
    {
        fine.clear();
        coarse.clear();
        everWritten.store (false, std::memory_order_release);
    }

private:
    static std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    static void add (BinStore& store, std::int64_t perQuarter, double pos, float sample, const Bands& b) noexcept
    {
        const auto n = (std::int64_t) std::floor (pos * (double) perQuarter);
        const auto size = (std::int64_t) store.size();
        const auto lap = floorDiv (n, size);
        store.add ((std::size_t) (n - lap * size), lap, sample, b);
    }

    BinStore fine, coarse;
    std::atomic<double> head { 0.0 };
    std::atomic<bool> everWritten { false };
};

} // namespace amvscope
