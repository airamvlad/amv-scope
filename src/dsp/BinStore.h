#pragma once

// A row of display slots ("bins"). Each bin keeps the lowest and highest sample
// that landed in it during one pass, plus the peak level of the low, mid and
// high frequency bands (for colouring by frequency).
//
// One thread writes, any number of threads read. Every field is an atomic, so
// reading during a write is safe; a bin may briefly mix two passes, which is
// invisible on screen.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace amvscope
{

/** Peak level of the three bands for one sample (absolute values). */
struct Bands
{
    float low = 0.0f, mid = 0.0f, high = 0.0f;
};

struct BinData
{
    float min = 0.0f, max = 0.0f;
    Bands bands;
    bool filled = false;

    /** Merges another bin into this one (used when several bins share a pixel column). */
    void merge (const BinData& other) noexcept
    {
        if (! other.filled)
            return;

        if (! filled)
        {
            *this = other;
            return;
        }

        min = min < other.min ? min : other.min;
        max = max > other.max ? max : other.max;
        bands.low = bands.low > other.bands.low ? bands.low : other.bands.low;
        bands.mid = bands.mid > other.bands.mid ? bands.mid : other.bands.mid;
        bands.high = bands.high > other.bands.high ? bands.high : other.bands.high;
    }
};

/** One bin as stored. Plain atomics, so a row of them can live in memory shared between processes. */
struct AtomicBin
{
    std::atomic<float> min, max, low, mid, high;
    std::atomic<std::int64_t> pass;
};

class BinStore
{
public:
    static constexpr std::int64_t emptyPass = INT64_MIN;

    /** Owns its bins. */
    explicit BinStore (std::size_t numBins) : owned (numBins), bins (owned.data()), numBinsUsed (numBins) { clear(); }

    /** Uses bins that live elsewhere (e.g. in shared memory); call clear() before first use. */
    BinStore (AtomicBin* storage, std::size_t numBins) noexcept : bins (storage), numBinsUsed (numBins) {}

    BinStore (const BinStore&) = delete;
    BinStore& operator= (const BinStore&) = delete;

    std::size_t size() const noexcept { return numBinsUsed; }

    /** Writer thread. A sample from a new pass replaces what the bin held. */
    void add (std::size_t index, std::int64_t pass, float sample, const Bands& b) noexcept
    {
        auto& bin = bins[index];
        constexpr auto relaxed = std::memory_order_relaxed;

        if (bin.pass.load (relaxed) != pass)
        {
            bin.min.store (sample, relaxed);
            bin.max.store (sample, relaxed);
            bin.low.store (b.low, relaxed);
            bin.mid.store (b.mid, relaxed);
            bin.high.store (b.high, relaxed);
            bin.pass.store (pass, relaxed);
            return;
        }

        storeIf (bin.min, sample, [] (float n, float o) { return n < o; });
        storeIf (bin.max, sample, [] (float n, float o) { return n > o; });
        storeIf (bin.low, b.low, [] (float n, float o) { return n > o; });
        storeIf (bin.mid, b.mid, [] (float n, float o) { return n > o; });
        storeIf (bin.high, b.high, [] (float n, float o) { return n > o; });
    }

    /** Any thread. requiredPass: only return data recorded in that pass
        (emptyPass accepts any pass). */
    BinData read (std::size_t index, std::int64_t requiredPass = emptyPass) const noexcept
    {
        const auto& bin = bins[index];
        constexpr auto relaxed = std::memory_order_relaxed;

        BinData out;
        const auto pass = bin.pass.load (relaxed);

        if (pass == emptyPass || (requiredPass != emptyPass && pass != requiredPass))
            return out;

        out.filled = true;
        out.min = bin.min.load (relaxed);
        out.max = bin.max.load (relaxed);
        out.bands = { bin.low.load (relaxed), bin.mid.load (relaxed), bin.high.load (relaxed) };
        return out;
    }

    void clear() noexcept
    {
        for (std::size_t i = 0; i < numBinsUsed; ++i)
            bins[i].pass.store (emptyPass, std::memory_order_relaxed);
    }

private:
    template <typename Better>
    static void storeIf (std::atomic<float>& a, float v, Better better) noexcept
    {
        if (better (v, a.load (std::memory_order_relaxed)))
            a.store (v, std::memory_order_relaxed);
    }

    std::vector<AtomicBin> owned;
    AtomicBin* bins = nullptr;
    std::size_t numBinsUsed = 0;
};

} // namespace amvscope
