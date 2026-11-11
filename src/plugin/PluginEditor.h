#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

class AMVScopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit AMVScopeAudioProcessorEditor (AMVScopeAudioProcessor&);
    ~AMVScopeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawGrid (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawWaveform (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawOverlays (juce::Graphics&, juce::Rectangle<float> area) const;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    AMVScopeAudioProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    juce::Label lengthLabel, syncLabel, zoomLabel, thresholdLabel;
    juce::ComboBox lengthBox, syncBox;
    juce::Slider zoomSlider, thresholdSlider;
    juce::TextButton freezeButton { "Freeze" };

    std::unique_ptr<ComboAttachment> lengthAttachment, syncAttachment;
    std::unique_ptr<SliderAttachment> zoomAttachment, thresholdAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment;

    juce::Rectangle<int> headerArea, scopeArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AMVScopeAudioProcessorEditor)
};
