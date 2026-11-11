// End-to-end checks of the plugin processor, without a DAW.

#include "plugin/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

void audioPassesThroughUnchanged()
{
    AMVScopeAudioProcessor p;
    p.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
        buffer.setSample (0, i, std::sin (0.01f * (float) i));
        buffer.setSample (1, i, 0.5f * std::cos (0.02f * (float) i));
    }

    juce::AudioBuffer<float> original (buffer);
    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);

    bool same = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < blockSize; ++i)
            same = same && buffer.getSample (ch, i) == original.getSample (ch, i);

    check (same, "audio is not changed");

    std::vector<amvscope::Frame> frames (blockSize);
    check (p.getScopeFifo().pop (frames.data(), frames.size()) == (size_t) blockSize,
           "scope receives every sample");
    check (frames[100].left == original.getSample (0, 100)
               && frames[100].right == original.getSample (1, 100),
           "scope receives left and right");
}

void stateRoundTrip()
{
    AMVScopeAudioProcessor a;
    a.state.setProperty ("testValue", 42, nullptr);

    juce::MemoryBlock data;
    a.getStateInformation (data);

    AMVScopeAudioProcessor b;
    b.setStateInformation (data.getData(), (int) data.getSize());
    check ((int) b.state.getProperty ("testValue") == 42, "state is restored");
}

void layouts()
{
    AMVScopeAudioProcessor p;
    juce::AudioProcessor::BusesLayout stereo, mono, mismatch;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    mismatch.inputBuses.add (juce::AudioChannelSet::mono());
    mismatch.outputBuses.add (juce::AudioChannelSet::stereo());

    check (p.isBusesLayoutSupported (stereo), "stereo supported");
    check (p.isBusesLayoutSupported (mono), "mono supported");
    check (! p.isBusesLayoutSupported (mismatch), "mono in / stereo out rejected");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    audioPassesThroughUnchanged();
    stateRoundTrip();
    layouts();

    if (failures == 0)
        std::printf ("All plugin tests passed.\n");

    return failures == 0 ? 0 : 1;
}
