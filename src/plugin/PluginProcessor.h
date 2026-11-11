#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SampleFifo.h"

#include <vector>

class AMVScopeAudioProcessor final : public juce::AudioProcessor
{
public:
    AMVScopeAudioProcessor();
    ~AMVScopeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Audio going to the display. The audio thread pushes, the editor pops. */
    amvscope::SampleFifo<amvscope::Frame>& getScopeFifo() noexcept { return scopeFifo; }

    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

    /** Settings saved with the project. Message thread only. */
    juce::ValueTree state { "AMVScope" };

private:
    amvscope::SampleFifo<amvscope::Frame> scopeFifo { 1 << 16 };
    std::vector<amvscope::Frame> scratch;
    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AMVScopeAudioProcessor)
};
