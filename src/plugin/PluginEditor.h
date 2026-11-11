#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/History.h"

#include <vector>

class AMVScopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit AMVScopeAudioProcessorEditor (AMVScopeAudioProcessor&);
    ~AMVScopeAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawWaveform (juce::Graphics&, juce::Rectangle<float> area) const;

    AMVScopeAudioProcessor& processor;
    amvscope::History history;
    std::vector<amvscope::Frame> incoming;

    juce::Rectangle<int> scopeArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AMVScopeAudioProcessorEditor)
};
