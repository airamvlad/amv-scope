#pragma once

// Lock-free single-producer / single-consumer queue.
// The audio thread pushes, the UI thread pops. When the queue is full the
// newest items are dropped, so the audio thread never waits.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace amvscope
{

template <typename T>
class SampleFifo
{
public:
    explicit SampleFifo (std::size_t capacity = 1 << 16)
        : buffer (capacity + 1) {}

    std::size_t capacity() const noexcept { return buffer.size() - 1; }

    /** Audio thread. Returns how many items were stored. */
    std::size_t push (const T* items, std::size_t count) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        const auto r = readPos.load (std::memory_order_acquire);
        const auto n = std::min (count, freeSpace (w, r));

        for (std::size_t i = 0; i < n; ++i)
            buffer[(w + i) % buffer.size()] = items[i];

        writePos.store ((w + n) % buffer.size(), std::memory_order_release);
        return n;
    }

    /** UI thread. Returns how many items were read. */
    std::size_t pop (T* dest, std::size_t maxCount) noexcept
    {
        const auto r = readPos.load (std::memory_order_relaxed);
        const auto w = writePos.load (std::memory_order_acquire);
        const auto n = std::min (maxCount, used (w, r));

        for (std::size_t i = 0; i < n; ++i)
            dest[i] = buffer[(r + i) % buffer.size()];

        readPos.store ((r + n) % buffer.size(), std::memory_order_release);
        return n;
    }

    /** Number of items waiting to be read. */
    std::size_t size() const noexcept
    {
        return used (writePos.load (std::memory_order_acquire),
                     readPos.load (std::memory_order_acquire));
    }

private:
    std::size_t used (std::size_t w, std::size_t r) const noexcept
    {
        return (w + buffer.size() - r) % buffer.size();
    }

    std::size_t freeSpace (std::size_t w, std::size_t r) const noexcept
    {
        return capacity() - used (w, r);
    }

    std::vector<T> buffer;
    std::atomic<std::size_t> writePos { 0 }, readPos { 0 };
};

/** One stereo sample. */
struct Frame
{
    float left = 0.0f;
    float right = 0.0f;
};

} // namespace amvscope
