#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff15171c };
const juce::Colour panel { 0xff1d2027 };
const juce::Colour grid { 0xff2c313b };
const juce::Colour text { 0xffc8ccd4 };
const juce::Colour wave { 0xff4fd1c5 };

constexpr double windowSeconds = 0.05;   // free-running view for now; tempo sync comes in v0.1
}

AMVScopeAudioProcessorEditor::AMVScopeAudioProcessorEditor (AMVScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), incoming (8192)
{
    setOpaque (true);
    setSize (900, 500);
    startTimerHz (60);
}

void AMVScopeAudioProcessorEditor::timerCallback()
{
    auto& fifo = processor.getScopeFifo();

    for (;;)
    {
        const auto n = fifo.pop (incoming.data(), incoming.size());

        if (n == 0)
            break;

        history.add (incoming.data(), n);
    }

    repaint (scopeArea);
}

void AMVScopeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("AMVScope", getLocalBounds().removeFromTop (40).reduced (16, 0),
                juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (text.withAlpha (0.5f));
    g.drawText ("v" JucePlugin_VersionString, getLocalBounds().removeFromTop (40).reduced (16, 0),
                juce::Justification::centredRight);

    const auto area = scopeArea.toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (area, 6.0f);

    g.setColour (grid);
    for (int i = 1; i < 8; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 8.0f;
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }
    for (int i = 1; i < 4; ++i)
    {
        const float y = area.getY() + area.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }

    drawWaveform (g, area.reduced (2.0f));
}

void AMVScopeAudioProcessorEditor::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto samplesInView = (size_t) juce::jlimit (16.0, (double) history.length(),
                                                      processor.getCurrentSampleRate() * windowSeconds);
    const int columns = juce::jmax (1, (int) area.getWidth());
    const float midY = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.5f;

    juce::Path path;

    for (int x = 0; x < columns; ++x)
    {
        // Oldest on the left, newest on the right.
        const auto ago = (size_t) ((double) (columns - 1 - x) / (double) columns * (double) samplesInView);
        const auto& f = history.fromNewest (ago);
        const float v = juce::jlimit (-1.0f, 1.0f, 0.5f * (f.left + f.right));
        const float y = midY - v * halfHeight;

        if (x == 0)
            path.startNewSubPath (area.getX(), y);
        else
            path.lineTo (area.getX() + (float) x, y);
    }

    g.setColour (wave);
    g.strokePath (path, juce::PathStrokeType (1.5f));
}

void AMVScopeAudioProcessorEditor::resized()
{
    scopeArea = getLocalBounds().withTrimmedTop (40).reduced (16, 0).withTrimmedBottom (16);
}
