#include "PluginEditor.h"
#include <iterator>

namespace
{
const juce::Colour background { 0xff15171c };
const juce::Colour panel { 0xff1d2027 };
const juce::Colour gridMinor { 0xff262a33 };
const juce::Colour gridMajor { 0xff363c48 };
const juce::Colour text { 0xffc8ccd4 };
const juce::Colour ownColour { 0xff4fd1c5 };
const juce::Colour sidechainColour { 0xffffa94d };

const juce::Colour otherColours[] { juce::Colour (0xfff06595), juce::Colour (0xff74c0fc),
                                    juce::Colour (0xffb197fc), juce::Colour (0xffffd43b),
                                    juce::Colour (0xff69db7c), juce::Colour (0xffff8787) };

// Frequency colours: low red, mid green, high blue.
const juce::Colour lowColour { 0xffff4d4d };
const juce::Colour midColour { 0xff5ce07a };
const juce::Colour highColour { 0xff5aa0ff };

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

juce::Colour frequencyColour (const amvscope::Bands& b)
{
    // Squared, so the strongest band clearly wins the colour.
    const float lo = b.low * b.low, mi = b.mid * b.mid, hi = b.high * b.high;
    const float sum = lo + mi + hi;

    if (sum < 1.0e-9f)
        return text;

    const float l = lo / sum, m = mi / sum, h = hi / sum;
    const auto mix = [&] (int shift)
    {
        const auto channel = [shift] (juce::Colour c) { return (float) ((c.getARGB() >> shift) & 0xff); };
        return (juce::uint8) juce::jlimit (0.0f, 255.0f,
                                           l * channel (lowColour) + m * channel (midColour) + h * channel (highColour));
    };
    return juce::Colour (mix (16), mix (8), mix (0));
}

bool hasColour (juce::Colour c) { return c.getAlpha() > 0; }
} // namespace

AMVScopeAudioProcessorEditor::AMVScopeAudioProcessorEditor (AMVScopeAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    lookAndFeel.setColour (juce::ComboBox::backgroundColourId, panel);
    lookAndFeel.setColour (juce::ComboBox::outlineColourId, gridMajor);
    lookAndFeel.setColour (juce::Slider::trackColourId, ownColour.withAlpha (0.6f));
    lookAndFeel.setColour (juce::Slider::thumbColourId, ownColour);
    lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lookAndFeel.setColour (juce::TextButton::buttonColourId, panel);
    lookAndFeel.setColour (juce::TextButton::buttonOnColourId, sidechainColour.withAlpha (0.8f));
    lookAndFeel.setColour (juce::TextButton::textColourOnId, background);
    lookAndFeel.setColour (juce::ToggleButton::textColourId, text.withAlpha (0.8f));
    lookAndFeel.setColour (juce::ToggleButton::tickColourId, ownColour);
    setLookAndFeel (&lookAndFeel);

    setupLabel (lengthLabel, "Length");
    setupLabel (syncLabel, "Sync");
    setupLabel (zoomLabel, "Zoom");
    setupLabel (thresholdLabel, "Trigger");
    setupLabel (colourLabel, "Colour");

    lengthBox.addItemList (amvscope::params::lengthNames, 1);
    syncBox.addItemList (amvscope::params::syncNames, 1);
    colourBox.addItemList (amvscope::params::colourModeNames, 1);
    setupSlider (zoomSlider);
    setupSlider (thresholdSlider);
    freezeButton.setClickingTogglesState (true);

    syncBox.setTooltip ("Host: follows the tempo while playing, triggers on level when stopped. "
                        "Trigger: always starts on level.");
    thresholdSlider.setTooltip ("Level that starts the picture in trigger mode");
    colourBox.setTooltip ("Track: each signal in its track colour. Frequency: this track coloured by its content "
                          "(red = lows, green = mids, blue = highs); other signals keep their colours.");
    sendButton.setTooltip ("Let other AMVScope instances show this track");
    othersButton.setTooltip ("Show the tracks of other AMVScope instances (host sync only)");

    for (auto* c : std::initializer_list<juce::Component*> {
             &lengthLabel, &syncLabel, &zoomLabel, &thresholdLabel, &colourLabel, &lengthBox, &syncBox,
             &colourBox, &zoomSlider, &thresholdSlider, &freezeButton, &sendButton, &othersButton })
        addAndMakeVisible (c);

    auto& apvts = processor.apvts;
    namespace ids = amvscope::params;
    lengthAttachment = std::make_unique<ComboAttachment> (apvts, ids::length, lengthBox);
    syncAttachment = std::make_unique<ComboAttachment> (apvts, ids::sync, syncBox);
    colourAttachment = std::make_unique<ComboAttachment> (apvts, ids::colourMode, colourBox);
    zoomAttachment = std::make_unique<SliderAttachment> (apvts, ids::zoom, zoomSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, ids::threshold, thresholdSlider);
    freezeAttachment = std::make_unique<ButtonAttachment> (apvts, ids::freeze, freezeButton);
    sendAttachment = std::make_unique<ButtonAttachment> (apvts, ids::send, sendButton);
    othersAttachment = std::make_unique<ButtonAttachment> (apvts, ids::showOthers, othersButton);

    setOpaque (true);
    setSize (960, 580);
    refresh();
    startTimerHz (60);
}

AMVScopeAudioProcessorEditor::~AMVScopeAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void AMVScopeAudioProcessorEditor::timerCallback()
{
    refresh();
    repaint (scopeArea.getUnion (legendArea));
}

void AMVScopeAudioProcessorEditor::refresh()
{
    const bool frozen = processor.apvts.getRawParameterValue (amvscope::params::freeze)->load() >= 0.5f;

    // Frozen: keep the columns we have, so the picture stays exactly as it was.
    if (frozen && frozenView)
        return;

    frozenView = frozen;

    const int columns = juce::jmax (1, scopeArea.getWidth() - 2);
    const bool following = processor.isFollowingHost();
    const bool showOthers = processor.apvts.getRawParameterValue (amvscope::params::showOthers)->load() >= 0.5f;

    std::vector<Trace> next;

    // Each trace: where it comes from in host mode (a ring) or trigger mode (a capture).
    struct Pending { Trace trace; const amvscope::PpqRing* ring; const amvscope::SyncedCapture* capture; };
    std::vector<Pending> pending;

    const auto& own = processor.getOwnSource();
    const auto ownName = own.getTrackName();
    pending.push_back ({ { "own", ownName.isNotEmpty() ? ownName : "This track",
                           hasColour (own.getTrackColour()) ? own.getTrackColour() : ownColour, true, {} },
                         &own.ring, &processor.getCapture() });

    if (processor.isSidechainActive())
        pending.push_back ({ { "sidechain", "Sidechain", sidechainColour, true, {} },
                             &processor.getSidechainRing(), &processor.getSidechainCapture() });

    // Keep the other sources alive while we read them.
    const auto others = showOthers ? processor.getOtherSources()
                                   : std::vector<std::shared_ptr<amvscope::Source>> {};

    for (const auto& s : others)
    {
        const auto name = s->getTrackName();
        const auto colour = hasColour (s->getTrackColour())
                                ? s->getTrackColour()
                                : otherColours[(size_t) s->id % std::size (otherColours)];
        pending.push_back ({ { "src" + juce::String (s->id), name.isNotEmpty() ? name : "AMVScope " + juce::String (s->id),
                               colour, following, {} },
                             &s->ring, nullptr });
    }

    // Host mode: the window that holds the play position. Parts of it not
    // played yet in this pass are taken from the previous pass.
    const double window = processor.getWindowQuarters();
    const double head = own.ring.getHead();
    const double windowStart = std::floor (head / window) * window;

    for (auto& p : pending)
    {
        auto& t = p.trace;

        if (! t.available || hidden[t.key])
        {
            next.push_back (std::move (t));
            continue;
        }

        t.columns.resize ((size_t) columns);

        for (int x = 0; x < columns; ++x)
        {
            if (following)
            {
                double from = windowStart + window * x / columns;
                double to = windowStart + window * (x + 1) / columns;

                if (to > head)
                {
                    from -= window;
                    to -= window;
                }

                t.columns[(size_t) x] = p.ring->read (from, to);
            }
            else
            {
                const auto first = (size_t) ((double) x / columns * amvscope::SyncedCapture::numBins);
                const auto last = juce::jmax (first + 1, (size_t) ((double) (x + 1) / columns
                                                                    * amvscope::SyncedCapture::numBins));
                t.columns[(size_t) x] = p.capture->readRange (first, last);
            }
        }

        next.push_back (std::move (t));
    }

    traces = std::move (next);
}

void AMVScopeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16, 0);
    headerArea = bounds.removeFromTop (48);
    footerArea = bounds.removeFromBottom (44);
    scopeArea = bounds;

    auto row = headerArea.withTrimmedLeft (110).withSizeKeepingCentre (headerArea.getWidth() - 110, 26);

    auto place = [] (juce::Rectangle<int>& r, juce::Label& label, juce::Component& c, int labelWidth, int width)
    {
        label.setBounds (r.removeFromLeft (labelWidth));
        r.removeFromLeft (6);
        c.setBounds (r.removeFromLeft (width));
        r.removeFromLeft (14);
    };

    place (row, lengthLabel, lengthBox, 48, 86);
    place (row, syncLabel, syncBox, 34, 86);
    place (row, zoomLabel, zoomSlider, 38, 150);
    place (row, thresholdLabel, thresholdSlider, 48, 160);
    freezeButton.setBounds (row.removeFromRight (72));

    auto foot = footerArea.withSizeKeepingCentre (footerArea.getWidth(), 26);
    othersButton.setBounds (foot.removeFromRight (110));
    foot.removeFromRight (8);
    sendButton.setBounds (foot.removeFromRight (70));
    foot.removeFromRight (14);
    colourBox.setBounds (foot.removeFromRight (100));
    foot.removeFromRight (6);
    colourLabel.setBounds (foot.removeFromRight (46));
    foot.removeFromRight (14);
    legendArea = foot;
}

void AMVScopeAudioProcessorEditor::mouseUp (const juce::MouseEvent& e)
{
    // Clicking a name in the legend hides or shows that signal.
    for (const auto& [rect, key] : legendHits)
    {
        if (rect.contains (e.getPosition()))
        {
            hidden[key] = ! hidden[key];
            frozenView = false;
            refresh();
            repaint();
            return;
        }
    }
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

    const bool frequency = juce::roundToInt (processor.apvts.getRawParameterValue (amvscope::params::colourMode)->load())
                           == (int) amvscope::params::ColourMode::frequency;

    // Others first, then the sidechain, then this track on top.
    // Frequency colours apply to this track only, so the others stay easy to tell apart.
    for (auto it = traces.rbegin(); it != traces.rend(); ++it)
        if (it->available && ! hidden[it->key])
            drawTrace (g, inner, *it, frequency && it->key == "own", it->key == "own" ? 1.0f : 0.85f);

    drawOverlays (g, inner);
    drawLegend (g);
}

void AMVScopeAudioProcessorEditor::drawGrid (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const double window = processor.getWindowQuarters();
    const double bar = processor.getQuartersPerBar();

    // Pick a line spacing that gives a readable number of lines.
    double minor = 0.25;   // 1/16 note
    if (window > 2.0) minor = 1.0;
    if (window > 8.0) minor = bar;
    const double major = window > 2.0 ? bar : 1.0;

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

void AMVScopeAudioProcessorEditor::drawTrace (juce::Graphics& g, juce::Rectangle<float> area, const Trace& t,
                                              bool frequencyColours, float alpha) const
{
    const float zoom = processor.apvts.getRawParameterValue (amvscope::params::zoom)->load();
    const float midY = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.5f;
    const auto columns = t.columns.size();
    const float columnWidth = columns > 0 ? area.getWidth() / (float) columns : 1.0f;

    auto toY = [&] (float v) { return midY - juce::jlimit (-1.0f, 1.0f, v * zoom) * halfHeight; };

    g.setColour (t.colour.withAlpha (alpha));

    for (size_t x = 0; x < columns; ++x)
    {
        const auto& c = t.columns[x];

        if (! c.filled)
            continue;

        if (frequencyColours)
            g.setColour (frequencyColour (c.bands).withAlpha (alpha));

        const float top = toY (c.max);
        const float bottom = toY (c.min);
        g.fillRect (area.getX() + (float) x * columnWidth, top, juce::jmax (1.0f, columnWidth),
                    juce::jmax (1.5f, bottom - top));
    }
}

void AMVScopeAudioProcessorEditor::drawOverlays (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const bool following = processor.isFollowingHost();
    const bool frozen = processor.apvts.getRawParameterValue (amvscope::params::freeze)->load() >= 0.5f;

    if (following && ! frozen)
    {
        const double window = processor.getWindowQuarters();
        const double head = processor.getOwnSource().ring.getHead();
        const double phase = head / window - std::floor (head / window);
        const float x = area.getX() + area.getWidth() * (float) phase;
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

        g.setColour (text.withAlpha (0.45f));
        for (float y : { area.getCentreY() - dy, area.getCentreY() + dy })
            g.drawDashedLine ({ area.getX(), y, area.getRight(), y }, dashes, 2, 1.0f);
    }

    juce::String status = following ? "Host  " + juce::String (processor.getBpm(), 1) + " BPM" : "Trigger";
    if (frozen)
        status << "  |  Frozen";

    g.setColour (text.withAlpha (following ? 0.6f : 0.9f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (status, area.reduced (8.0f, 2.0f).toNearestInt(), juce::Justification::topRight);
}

void AMVScopeAudioProcessorEditor::drawLegend (juce::Graphics& g)
{
    legendHits.clear();

    auto area = legendArea;
    const juce::FontOptions font (13.0f);
    g.setFont (font);

    for (const auto& t : traces)
    {
        const bool isHidden = hidden[t.key];
        const auto label = t.available ? t.label : t.label + " (host sync only)";
        const int width = 26 + juce::GlyphArrangement::getStringWidthInt (juce::Font (font), label) + 16;

        if (width > area.getWidth())
            break;

        auto chip = area.removeFromLeft (width).withSizeKeepingCentre (width, 24);
        legendHits.push_back ({ chip, t.key });

        const float dim = (isHidden || ! t.available) ? 0.35f : 1.0f;
        g.setColour (t.colour.withAlpha (dim));
        g.fillEllipse (chip.getX() + 6.0f, chip.getCentreY() - 5.0f, 10.0f, 10.0f);
        g.setColour (text.withAlpha (dim));
        g.drawText (label, chip.withTrimmedLeft (24), juce::Justification::centredLeft);

        if (isHidden)
            g.drawHorizontalLine (chip.getCentreY(), (float) chip.getX() + 22.0f, (float) chip.getRight() - 14.0f);
    }
}
