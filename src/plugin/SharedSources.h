#pragma once

// Lets AMVScope instances see each other's signals.
//
// Each instance owns one Source: its main input recorded by host position,
// plus its track name, colour and display settings. The sources live in a
// block of memory shared by all processes of this user, so instances see each
// other in every plug-in hosting mode of the DAW, also when each plugin runs
// in its own process.
//
// The block holds a fixed number of slots. An instance claims a free slot,
// keeps a heartbeat in it while it lives and frees it when it is deleted. A
// slot whose heartbeat stopped (its process crashed) is hidden from the
// others and reused later.

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include "SharedMemory.h"
#include "dsp/PpqRing.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace amvscope
{

/** One instance's place in the shared block. Fixed layout, plain atomics only. */
struct SharedSlot
{
    enum State : std::uint32_t { unused = 0, claiming = 1, active = 2 };

    std::atomic<std::uint32_t> state;
    std::atomic<std::int64_t> heartbeatMs;    // wall clock, refreshed by the owner about once a second
    std::atomic<std::uint32_t> sending;
    std::atomic<float> offsetMs;              // display-only shift, positive = later
    std::atomic<std::uint32_t> inverted;
    std::atomic<std::int32_t> channel;        // params::Channel
    std::atomic<std::uint32_t> colour;        // ARGB; 0 = host did not say
    std::atomic<std::uint32_t> nameVersion;   // odd while the name is being written
    char name[128];                           // UTF-8, zero terminated
    RingStorage ring;
};

struct SharedBlock
{
    static constexpr std::size_t numSlots = 16;
    static constexpr std::uint32_t layoutId = 0x414d5601;   // "AMV", layout 1

    std::atomic<std::uint32_t> layout;
    SharedSlot slots[numSlots];
};

static_assert (std::atomic<float>::is_always_lock_free && std::atomic<double>::is_always_lock_free
                   && std::atomic<std::int64_t>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free,
               "the shared block needs lock-free atomics");

/** Name of the shared block. Tests change it so they never meet a running DAW. */
inline std::string& sharedBlockName()
{
    static std::string name = "amvscope.shared.v1";
    return name;
}

/** A slot is taken as dead when its owner has not refreshed it for this long. */
inline constexpr std::int64_t heartbeatTimeoutMs = 5000;
inline constexpr std::int64_t heartbeatReuseMs = 15000;

/** A view of one slot. Holds the memory alive, so it stays safe to read after its owner is gone. */
struct Source
{
    Source (std::shared_ptr<SharedMemory> memoryIn, SharedSlot& slotIn, int idIn)
        : id (idIn), slot (slotIn), ring (slotIn.ring), sending (slotIn.sending), offsetMs (slotIn.offsetMs),
          inverted (slotIn.inverted), channel (slotIn.channel), memory (std::move (memoryIn)) {}

    const int id;        // slot number from 1; stable while the instance lives
    SharedSlot& slot;
    PpqRing ring;

    std::atomic<std::uint32_t>& sending;
    std::atomic<float>& offsetMs;
    std::atomic<std::uint32_t>& inverted;
    std::atomic<std::int32_t>& channel;

    void setTrack (const juce::String& name, juce::Colour colour)
    {
        // One writer (the owner); readers retry while the version is odd or changes.
        const auto version = slot.nameVersion.load (std::memory_order_relaxed);
        slot.nameVersion.store (version + 1, std::memory_order_release);
        name.copyToUTF8 (slot.name, sizeof (slot.name));
        slot.nameVersion.store (version + 2, std::memory_order_release);
        slot.colour.store (colour.getARGB(), std::memory_order_relaxed);
    }

    juce::String getTrackName() const
    {
        char copy[sizeof (slot.name)];

        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const auto before = slot.nameVersion.load (std::memory_order_acquire);
            std::memcpy (copy, slot.name, sizeof (copy));
            copy[sizeof (copy) - 1] = 0;

            if ((before & 1u) == 0 && slot.nameVersion.load (std::memory_order_acquire) == before)
                return juce::String::fromUTF8 (copy);
        }

        return {};
    }

    /** Transparent: the host did not give a colour. */
    juce::Colour getTrackColour() const { return juce::Colour (slot.colour.load (std::memory_order_relaxed)); }

    void beat() noexcept { slot.heartbeatMs.store (juce::Time::currentTimeMillis(), std::memory_order_relaxed); }

private:
    std::shared_ptr<SharedMemory> memory;
};

class SharedSources
{
public:
    SharedSources()
        : memory (std::make_shared<SharedMemory> (sharedBlockName(), sizeof (SharedBlock))),
          block (*std::launder (reinterpret_cast<SharedBlock*> (memory->data())))
    {
        // The first process to arrive stamps the layout; a different stamp means
        // some other program owns this name, and we keep to ourselves.
        std::uint32_t expected = 0;

        if (! block.layout.compare_exchange_strong (expected, SharedBlock::layoutId)
            && expected != SharedBlock::layoutId)
            usable = false;
    }

    /** True when instances in other processes can be seen. */
    bool isShared() const noexcept { return usable && memory->isShared(); }

    /** Claims a slot for a new instance. If all are taken, the instance gets a
        private one: it works, but the others cannot see it. */
    std::shared_ptr<Source> add()
    {
        const auto now = juce::Time::currentTimeMillis();

        if (usable)
        {
            // First a free slot, then one whose owner died long ago.
            for (int pass = 0; pass < 2; ++pass)
            {
                for (std::size_t i = 0; i < SharedBlock::numSlots; ++i)
                {
                    auto& slot = block.slots[i];
                    std::uint32_t expected = SharedSlot::unused;

                    if (pass == 1)
                    {
                        // Dead for a long time, whether it was running or just starting.
                        if (now - slot.heartbeatMs.load (std::memory_order_relaxed) < heartbeatReuseMs)
                            continue;

                        expected = slot.state.load (std::memory_order_relaxed);
                    }

                    if (slot.state.compare_exchange_strong (expected, SharedSlot::claiming))
                        return start (memory, slot, (int) i + 1);
                }
            }
        }

        auto privateMemory = std::make_shared<SharedMemory> (std::string(), sizeof (SharedSlot));
        auto& slot = *std::launder (reinterpret_cast<SharedSlot*> (privateMemory->data()));
        return start (std::move (privateMemory), slot, 0);
    }

    void remove (const std::shared_ptr<Source>& s)
    {
        if (s != nullptr)
            s->slot.state.store (SharedSlot::unused, std::memory_order_release);
    }

    /** Views of the live instances other than `own`, in this process or any other. */
    std::vector<std::shared_ptr<Source>> listOthers (const Source& own) const
    {
        std::vector<std::shared_ptr<Source>> result;

        if (! usable)
            return result;

        const auto now = juce::Time::currentTimeMillis();

        for (std::size_t i = 0; i < SharedBlock::numSlots; ++i)
        {
            auto& slot = block.slots[i];

            if (&slot != &own.slot && slot.state.load (std::memory_order_acquire) == SharedSlot::active
                && now - slot.heartbeatMs.load (std::memory_order_relaxed) < heartbeatTimeoutMs)
                result.push_back (std::make_shared<Source> (memory, slot, (int) i + 1));
        }

        return result;
    }

private:
    static std::shared_ptr<Source> start (std::shared_ptr<SharedMemory> memoryIn, SharedSlot& slot, int id)
    {
        auto source = std::make_shared<Source> (std::move (memoryIn), slot, id);
        source->beat();   // first, so nobody takes a slot that is being set up for a dead one
        source->ring.clear();
        slot.sending.store (1, std::memory_order_relaxed);
        slot.offsetMs.store (0.0f, std::memory_order_relaxed);
        slot.inverted.store (0, std::memory_order_relaxed);
        slot.channel.store (0, std::memory_order_relaxed);
        source->setTrack ({}, juce::Colours::transparentBlack);
        source->beat();
        slot.state.store (SharedSlot::active, std::memory_order_release);
        return source;
    }

    std::shared_ptr<SharedMemory> memory;
    SharedBlock& block;
    bool usable = true;
};

} // namespace amvscope
