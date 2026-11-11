#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <map>
#include <memory>
#include <vector>

class AMVScopeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit AMVScopeAudioProcessorEditor (AMVScopeAudioProcessor&);
    ~AMVScopeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** One signal on screen: this track, the sidechain, or another instance. */
    struct Trace
    {
        juce::String key;     // stable id, used to remember hidden traces
        juce::String label;
        juce::Colour colour;
        bool available = true;   // false: listed but cannot be drawn in this mode
        std::vector<amvscope::BinData> columns;
    };

    /** Rebuilds the traces from the processor. Called by the timer; public for tests. */
    void refresh();
    const std::vector<Trace>& getTraces() const noexcept { return traces; }

private:
    void timerCallback() override;
    void drawGrid (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawTrace (juce::Graphics&, juce::Rectangle<float> area, const Trace&, bool frequencyColours,
                    float alpha) const;
    void drawOverlays (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawLegend (juce::Graphics&);
    void drawMeasure (juce::Graphics&, juce::Rectangle<float> area) const;

    juce::Rectangle<float> getPlotArea() const { return scopeArea.toFloat().reduced (1.0f, 8.0f); }
    double windowMilliseconds() const;
    void setParam (const char* id, float value);
    void toggleParam (const char* id);
    void saveHidden();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    AMVScopeAudioProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    juce::Label lengthLabel, syncLabel, zoomLabel, thresholdLabel, colourLabel, sourceLabel;
    juce::ComboBox lengthBox, syncBox, colourBox, sourceBox;
    juce::Slider zoomSlider, thresholdSlider;
    juce::TextButton freezeButton { "Freeze" };
    juce::ToggleButton sendButton { "Send" }, othersButton { "Show others" };

    std::unique_ptr<ComboAttachment> lengthAttachment, syncAttachment, colourAttachment, sourceAttachment;
    std::unique_ptr<SliderAttachment> zoomAttachment, thresholdAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment, sendAttachment, othersAttachment;

    juce::Rectangle<int> headerArea, footerArea, scopeArea, legendArea;

    std::vector<Trace> traces;
    std::map<juce::String, bool> hidden;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> legendHits;
    bool frozenView = false;

    // Mouse readout: positions are fractions of the plot (0..1 across, 0..1 down).
    bool hovering = false, measuring = false;
    juce::Point<float> hoverPos;
    float measureFrom = 0.0f, measureTo = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AMVScopeAudioProcessorEditor)
};
