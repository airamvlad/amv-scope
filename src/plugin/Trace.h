#pragma once

#include <juce_graphics/juce_graphics.h>

#include "dsp/BinStore.h"

#include <vector>

namespace amvscope
{

/** One signal on screen: this track, the sidechain, or another instance. */
struct Trace
{
    juce::String key;     // stable id, used to remember hidden traces
    juce::String label;
    juce::Colour colour;
    bool available = true;   // false: listed but cannot be drawn in this mode
    std::vector<BinData> columns;
    float scale = 1.0f;      // display gain: the trace's own gain times the automatic level
};

} // namespace amvscope
