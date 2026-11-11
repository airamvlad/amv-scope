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

    using Trace = amvscope::Trace;

    /** How this track lines up with another signal on screen. */
    struct PhaseInfo
    {
        bool valid = false;            // false: nothing to compare with, or the window is too long
        juce::String partner;          // name of the signal this track is compared with
        float correlation = 0.0f;      // -1..+1 as shown now
        float suggestedOffsetMs = 0.0f;   // value for Offset that gives the best match
        bool suggestedInvert = false;
        float suggestedCorrelation = 0.0f;
    };

    const PhaseInfo& getPhaseInfo() const noexcept { return phase; }

    /** Sets Offset and Invert to the suggested values. */
    void alignToSuggestion();

    /** Rebuilds the traces from the processor. Called by the timer; public for tests. */
    void refresh();
    const std::vector<Trace>& getTraces() const noexcept { return traces; }

    /** Remembers the picture as it is now and keeps it behind the live one; again to remove it. */
    void toggleGhost();
    bool hasGhost() const noexcept { return ! processor.ghostTraces.empty(); }

    /** Display gain of one trace in dB ("own", "sidechain", "src<id>"). Only changes the picture. */
    void setTraceGain (const juce::String& key, float db);
    float getTraceGain (const juce::String& key) const;

private:
    void timerCallback() override;
    void drawGrid (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawTrace (juce::Graphics&, juce::Rectangle<float> area, const Trace&, bool frequencyColours,
                    float alpha) const;
    void drawOverlays (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawLegend (juce::Graphics&);
    void drawMeasure (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawPhase (juce::Graphics&, juce::Rectangle<float> area) const;
    void drawBands (juce::Graphics&, juce::Rectangle<float> area) const;
    bool isBandsView() const;
    void updatePhase();

    juce::Rectangle<float> getPlotArea() const { return scopeArea.toFloat().reduced (1.0f, 8.0f); }
    double windowMilliseconds() const;
    void setParam (const char* id, float value);
    void toggleParam (const char* id);
    void saveHidden();
    void saveGains();
    bool ghostFitsWindow() const;
    const juce::String* legendKeyAt (juce::Point<int>) const;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    AMVScopeAudioProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    juce::Label lengthLabel, syncLabel, zoomLabel, thresholdLabel, colourLabel, sourceLabel, offsetLabel, viewLabel;
    juce::ComboBox lengthBox, syncBox, colourBox, sourceBox, viewBox;
    juce::Slider zoomSlider, thresholdSlider, offsetSlider;
    juce::TextButton freezeButton { "Freeze" };
    juce::ToggleButton sendButton { "Send" }, othersButton { "Show others" }, invertButton { "Invert" },
                       autoLevelButton { "Auto level" };
    juce::TextButton alignButton { "Align" }, ghostButton { "Ghost" };

    std::unique_ptr<ComboAttachment> lengthAttachment, syncAttachment, colourAttachment, sourceAttachment, viewAttachment;
    std::unique_ptr<SliderAttachment> zoomAttachment, thresholdAttachment, offsetAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment, sendAttachment, othersAttachment, invertAttachment,
                                      autoLevelAttachment;

    juce::Rectangle<int> headerArea, footerArea, scopeArea, legendArea, phaseArea;

    std::vector<Trace> traces;
    std::map<juce::String, bool> hidden;
    std::map<juce::String, float> gains;       // dB, set with the mouse wheel on a name in the legend
    std::map<juce::String, float> heldPeaks;   // smoothed peak per trace, for Auto level
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> legendHits;
    bool frozenView = false;

    PhaseInfo phase;
    int phaseCountdown = 0;

    // Mouse readout: positions are fractions of the plot (0..1 across, 0..1 down).
    bool hovering = false, measuring = false;
    juce::Point<float> hoverPos;
    float measureFrom = 0.0f, measureTo = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AMVScopeAudioProcessorEditor)
};
