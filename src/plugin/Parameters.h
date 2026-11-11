#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace amvscope::params
{
inline constexpr auto length = "length";
inline constexpr auto sync = "sync";
inline constexpr auto zoom = "zoom";
inline constexpr auto threshold = "threshold";
inline constexpr auto freeze = "freeze";
inline constexpr auto send = "send";
inline constexpr auto showOthers = "showOthers";
inline constexpr auto colourMode = "colourMode";
inline constexpr auto triggerSource = "triggerSource";
inline constexpr auto offset = "offset";
inline constexpr auto invert = "invert";
inline constexpr auto view = "view";
inline constexpr auto autoLevel = "autoLevel";

/** Display window lengths, in the order of the Length menu. */
inline const juce::StringArray lengthNames { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
inline constexpr int defaultLength = 2;   // 1/4: one beat, kick plus the bass after it

/** Window length in quarter notes. beatsPerBar is in quarter notes too (4 for 4/4, 3 for 6/8). */
inline double lengthInQuarters (int index, double quartersPerBar)
{
    switch (index)
    {
        case 0:  return 0.25;
        case 1:  return 0.5;
        case 2:  return 1.0;
        case 3:  return 2.0;
        case 4:  return quartersPerBar;
        case 5:  return 2.0 * quartersPerBar;
        default: return 4.0 * quartersPerBar;
    }
}

enum class Sync { host = 0, trigger = 1 };
inline const juce::StringArray syncNames { "Host", "Trigger" };

enum class View { waveform = 0, bands = 1 };
inline const juce::StringArray viewNames { "Wave", "Bands" };

enum class TriggerSource { main = 0, sidechain = 1 };
inline const juce::StringArray triggerSourceNames { "This track", "Sidechain" };

enum class ColourMode { track = 0, frequency = 1 };
inline const juce::StringArray colourModeNames { "Track", "Frequency" };

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { length, 1 }, "Length",
                                                        lengthNames, defaultLength));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { sync, 1 }, "Sync", syncNames, 0));

    NormalisableRange<float> zoomRange (1.0f, 32.0f);
    zoomRange.setSkewForCentre (4.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { zoom, 1 }, "Zoom", zoomRange, 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + "x"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { threshold, 1 }, "Trigger level", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { freeze, 1 }, "Freeze", false));

    // v0.2
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { send, 2 }, "Send to other instances", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { showOthers, 2 }, "Show other instances", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { colourMode, 2 }, "Colour",
                                                        colourModeNames, 0));

    // v0.3
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { triggerSource, 3 }, "Trigger source",
                                                        triggerSourceNames, 0));

    // v0.4: display-only shift and polarity of this track, for checking phase.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { offset, 4 }, "Offset", NormalisableRange<float> (-50.0f, 50.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { invert, 4 }, "Invert", false));

    // v0.5
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { view, 5 }, "View", viewNames, 0));

    // v0.6: draw every signal at the same height, to compare shapes of loud and quiet tracks.
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { autoLevel, 6 }, "Auto level", false));

    return layout;
}
} // namespace amvscope::params
