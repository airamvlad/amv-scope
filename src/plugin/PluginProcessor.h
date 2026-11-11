#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "dsp/SyncedCapture.h"
#include "dsp/TriggerDetector.h"

#include <atomic>
#include <cstdint>
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

    juce::AudioProcessorValueTreeState apvts;

    /** What the display draws. Safe to read from the UI thread. */
    const amvscope::SyncedCapture& getCapture() const noexcept { return capture; }

    /** True while the picture follows the host transport, false while it is triggered by level. */
    bool isFollowingHost() const noexcept { return followingHost.load (std::memory_order_relaxed); }

    /** Window length in quarter notes, as last used by the audio thread. */
    double getWindowQuarters() const noexcept { return windowQuarters.load (std::memory_order_relaxed); }

    /** Quarter notes per bar (4 for 4/4), as last reported by the host. */
    double getQuartersPerBar() const noexcept { return quartersPerBar.load (std::memory_order_relaxed); }

    double getBpm() const noexcept { return bpm.load (std::memory_order_relaxed); }

private:
    void captureTriggered (const float* mono, int numSamples, double samplesPerQuarter, double window);

    amvscope::SyncedCapture capture;
    amvscope::TriggerDetector trigger;
    std::vector<float> mono;

    double sampleRate = 48000.0;

    // Trigger mode: where the current recording is, in quarter notes since the trigger.
    double triggerPosition = 0.0;
    bool triggerRecording = false;
    std::int64_t triggerCount = 0;

    std::atomic<bool> followingHost { false };
    std::atomic<double> windowQuarters { 1.0 };
    std::atomic<double> quartersPerBar { 4.0 };
    std::atomic<double> bpm { 120.0 };

    std::atomic<float>* lengthParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AMVScopeAudioProcessor)
};
