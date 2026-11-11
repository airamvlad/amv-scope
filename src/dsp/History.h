#pragma once

// Keeps the most recent N frames for drawing. UI thread only.

#include "SampleFifo.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace amvscope
{

class History
{
public:
    explicit History (std::size_t length = 1 << 17) : frames (length) {}

    std::size_t length() const noexcept { return frames.size(); }

    void add (const Frame* items, std::size_t count) noexcept
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            frames[head] = items[i];
            head = (head + 1) % frames.size();
        }
    }

    /** ago = 0 is the newest frame, ago = length() - 1 the oldest. */
    const Frame& fromNewest (std::size_t ago) const noexcept
    {
        const auto n = frames.size();
        return frames[(head + n - 1 - (ago % n)) % n];
    }

    void clear() noexcept
    {
        std::fill (frames.begin(), frames.end(), Frame {});
        head = 0;
    }

private:
    std::vector<Frame> frames;
    std::size_t head = 0;
};

} // namespace amvscope
