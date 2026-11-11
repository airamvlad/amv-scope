#pragma once

// Lets AMVScope instances in the same process see each other's signals.
// Each instance owns one Source (its main input, recorded by host position)
// and registers it here; any editor can list the others and draw them.
//
// Works when the host runs the plugins in one process. In Bitwig that depends on
// the plug-in hosting mode (Settings > Plug-ins): any mode that groups AMVScope
// instances together works; with "Individually" each instance only sees itself.

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include "dsp/PpqRing.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace amvscope
{

struct Source
{
    explicit Source (int idIn) : id (idIn) {}

    const int id;
    PpqRing ring;
    std::atomic<bool> sending { true };

    // Display-only shift (ms, positive = later) and polarity flip of this track,
    // so every window shows it the same way.
    std::atomic<float> offsetMs { 0.0f };
    std::atomic<bool> inverted { false };

    void setTrack (const juce::String& name, juce::Colour colour)
    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        trackName = name;
        trackColour = colour;
    }

    juce::String getTrackName() const
    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        return trackName;
    }

    juce::Colour getTrackColour() const
    {
        const juce::SpinLock::ScopedLockType lock (trackLock);
        return trackColour;
    }

private:
    mutable juce::SpinLock trackLock;
    juce::String trackName;
    juce::Colour trackColour { juce::Colours::transparentBlack };   // transparent = host did not say
};

class SharedSources
{
public:
    std::shared_ptr<Source> add()
    {
        const juce::ScopedLock lock (mutex);
        auto s = std::make_shared<Source> (++nextId);
        sources.push_back (s);
        return s;
    }

    void remove (const std::shared_ptr<Source>& s)
    {
        const juce::ScopedLock lock (mutex);
        sources.erase (std::remove (sources.begin(), sources.end(), s), sources.end());
    }

    /** Copies of the registered sources; safe to use after other instances are deleted. */
    std::vector<std::shared_ptr<Source>> list() const
    {
        const juce::ScopedLock lock (mutex);
        return sources;
    }

private:
    juce::CriticalSection mutex;
    std::vector<std::shared_ptr<Source>> sources;
    int nextId = 0;
};

} // namespace amvscope
