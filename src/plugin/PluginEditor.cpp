#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff15171c };
const juce::Colour panel { 0xff1d2027 };
const juce::Colour gridMinor { 0xff262a33 };
const juce::Colour gridMajor { 0xff363c48 };
const juce::Colour text { 0xffc8ccd4 };
const juce::Colour wave { 0xff4fd1c5 };
const juce::Colour accent { 0xffffa94d };

void setupLabel (juce::Label& label, const juce::String& name)
{
    label.setText (name, juce::dontSendNotification);
    label.setFont (juce::FontOptions (12.0f));
    label.setColour (juce::Label::textColourId, text.withAlpha (0.7f));
    label.setJustificationType (juce::Justification::centredRight);
}

void setupSlider (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
}
} // namespace

AMVScopeAudioProcessorEditor::AMVScopeAudioProcessorEditor (AMVScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, panel);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, gridMajor);
    lookAndFeel.setColour (juce::Slider::trackColourId, wave.withAlpha (0.6f));
    lookAndFeel.setColour (juce::Slider::thumbColourId, wave);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, panel);
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.8f));
    lookAndFeel.setColour (juce::TextButton::textColourOnId, background);
    setLookAndFeel (&lookAndFeel);

    setupLabel (lengthLabel, "Length");
    setupLabel (syncLabel, "Sync");
    setupLabel (zoomLabel, "Zoom");
    setupLabel (thresholdLabel, "Trigger");

    lengthBox.addItemList (amvscope::params::lengthNames, 1);
    syncBox.addItemList (amvscope::params::syncNames, 1);
    setupSlider (zoomSlider);
    setupSlider (thresholdSlider);
    freezeButton.setClickingTogglesState (true);

    syncBox.setTooltip ("Host: follows Bitwig's tempo while playing, triggers on level when stopped. "
                        "Trigger: always starts on level.");
    thresholdSlider.setTooltip ("Level that starts the picture in trigger mode");

    for (auto* c : std::initializer_list<juce::Component*> {
             &lengthLabel, &syncLabel, &zoomLabel, &thresholdLabel, &lengthBox, &syncBox,
             &zoomSlider, &thresholdSlider, &freezeButton })
        addAndMakeVisible (c);

    auto& apvts = processor.apvts;
    lengthAttachment = std::make_unique<ComboAttachment> (apvts, amvscope::params::length, lengthBox);
    syncAttachment = std::make_unique<ComboAttachment> (apvts, amvscope::params::sync, syncBox);
    zoomAttachment = std::make_unique<SliderAttachment> (apvts, amvscope::params::zoom, zoomSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, amvscope::params::threshold, thresholdSlider);
    freezeAttachment = std::make_unique<ButtonAttachment> (apvts, amvscope::params::freeze, freezeButton);

    setOpaque (true);
    setSize (960, 540);
    startTimerHz (60);
}

AMVScopeAudioProcessorEditor::~AMVScopeAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void AMVScopeAudioProcessorEditor::timerCallback()
{
    repaint (scopeArea);
}

void AMVScopeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16, 0);
    headerArea = bounds.removeFromTop (48);
    scopeArea = bounds.withTrimmedBottom (16);

    auto row = headerArea.withTrimmedLeft (110).withSizeKeepingCentre (headerArea.getWidth() - 110, 26);

    auto place = [&row] (juce::Label& label, juce::Component& c, int labelWidth, int width)
    {
        label.setBounds (row.removeFromLeft (labelWidth));
        row.removeFromLeft (6);
        c.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (14);
    };

    place (lengthLabel, lengthBox, 48, 86);
    place (syncLabel, syncBox, 34, 86);
    place (zoomLabel, zoomSlider, 38, 150);
    place (thresholdLabel, thresholdSlider, 48, 160);
    freezeButton.setBounds (row.removeFromRight (72));
}

void AMVScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("AMVScope", headerArea, juce::Justification::centredLeft);

    const auto area = scopeArea.toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);

    const auto inner = area.reduced (1.0f, 8.0f);
    drawGrid (g, inner);
    drawWaveform (g, inner);
    drawOverlays (g, inner);
}

void AMVScopeAudioProcessorEditor::drawGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const double window = processor.getWindowQuarters();
    const double bar = processor.getQuartersPerBar();

    // Pick a line spacing that gives a readable number of lines.
    double minor = 0.25;   // 1/16 note
    if (window > 2.0) minor = 1.0;
    if (window > 8.0) minor = bar;
    const double major = window > 8.0 ? bar : (window > 2.0 ? bar : 1.0);

    const int count = juce::roundToInt (window / minor);

    for (int i = 1; i < count; ++i)
    {
        const double q = minor * i;
        const bool isMajor = std::abs (std::fmod (q + 1e-9, major)) < 1e-6;
        const float x = area.getX() + area.getWidth() * (float) (q / window);
        g.setColour (isMajor ? gridMajor : gridMinor);
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    g.setColour (gridMajor);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    g.setColour (gridMinor);
    g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * 0.25f), area.getX(), area.getRight());
    g.drawHorizontalLine (juce::roundToInt (area.getY() + area.getHeight() * 0.75f), area.getX(), area.getRight());
}

void AMVScopeAudioProcessorEditor::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using Capture = amvscope::SyncedCapture;
    const auto& capture = processor.getCapture();

    const float zoom = processor.apvts.getRawParameterValue (amvscope::params::zoom)->load();
    const float midY = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.5f;
    const int columns = juce::jmax (1, (int) area.getWidth());

    auto toY = [&] (float v) { return midY - juce::jlimit (-1.0f, 1.0f, v * zoom) * halfHeight; };

    g.setColour (wave);

    for (int x = 0; x < columns; ++x)
    {
        const auto first = (size_t) ((double) x / columns * Capture::numBins);
        const auto last = juce::jmax (first + 1, (size_t) ((double) (x + 1) / columns * Capture::numBins));

        float lo = 0.0f, hi = 0.0f;
        bool any = false;

        for (auto b = first; b < last && b < Capture::numBins; ++b)
        {
            const auto bin = capture.readBin (b);

            if (! bin.filled)
                continue;

            lo = any ? juce::jmin (lo, bin.min) : bin.min;
            hi = any ? juce::jmax (hi, bin.max) : bin.max;
            any = true;
        }

        if (! any)
            continue;

        const float top = toY (hi);
        const float bottom = toY (lo);
        g.fillRect (area.getX() + (float) x, top, 1.0f, juce::jmax (1.5f, bottom - top));
    }
}

void AMVScopeAudioProcessorEditor::drawOverlays (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const bool following = processor.isFollowingHost();
    const bool frozen = processor.apvts.getRawParameterValue (amvscope::params::freeze)->load() >= 0.5f;

    if (following && ! frozen)
    {
        const float x = area.getX() + area.getWidth() * processor.getCapture().getPlayhead();
        g.setColour (text.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    if (! following)
    {
        // Trigger level, both polarities.
        const float zoom = processor.apvts.getRawParameterValue (amvscope::params::zoom)->load();
        const float db = processor.apvts.getRawParameterValue (amvscope::params::threshold)->load();
        const float level = juce::jlimit (0.0f, 1.0f, juce::Decibels::decibelsToGain (db) * zoom);
        const float dy = level * area.getHeight() * 0.5f;
        const float dashes[] { 4.0f, 4.0f };

        g.setColour (accent.withAlpha (0.6f));
        for (float y : { area.getCentreY() - dy, area.getCentreY() + dy })
            g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dashes, 2, 1.0f);
    }

    juce::String status = following ? "Host  " + juce::String (processor.getBpm(), 1) + " BPM" : "Trigger";
    if (frozen)
        status << "  |  Frozen";

    g.setColour (following ? text.withAlpha (0.6f) : accent);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (status, area.reduced (8.0f, 2.0f).toNearestInt(), juce::Justification::topRight);
}
