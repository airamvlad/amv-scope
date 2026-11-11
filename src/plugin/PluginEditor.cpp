#include "PluginEditor.h"
#include <iterator>

#include "dsp/Correlation.h"

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
    setupLabel (sourceLabel, "Trig from");
    setupLabel (offsetLabel, "Offset");
    setupLabel (viewLabel, "View");
    setupLabel (channelLabel, "Channel");
    channelBox.addItemList (amvscope::params::channelNames, 1);
    channelBox.setTooltip ("Which part of this stereo track is shown. Mid is what stays in mono; "
                           "Side is the difference between left and right. A bass that is truly mono shows a flat line on Side.");
    viewBox.addItemList (amvscope::params::viewNames, 1);
    viewBox.setTooltip ("Wave: the waveform. Bands: the level of lows (below 200 Hz), mids and highs "
                        "(above 2.5 kHz) in three rows, so you can see where each signal has its energy.");
    setupSlider (offsetSlider);
    offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    offsetSlider.setDoubleClickReturnValue (true, 0.0);
    offsetSlider.setTooltip ("Moves this track on screen (ms, positive = later) to try out a delay. "
                             "Display only: apply the same delay in the DAW to change the sound.");
    invertButton.setTooltip ("Flips this track's polarity on screen");
    alignButton.setTooltip ("Sets Offset and Invert to the best match with the compared signal");
    alignButton.onClick = [this] { alignToSuggestion(); };
    ghostButton.setTooltip ("Remembers the picture as it is now and keeps it faintly behind the live signal, "
                            "to compare before and after a change. Click again to remove it. (G)");
    ghostButton.onClick = [this] { toggleGhost(); };
    ghostButton.setToggleState (hasGhost(), juce::dontSendNotification);
    autoLevelButton.setTooltip ("Draws every signal at the same height, so a quiet bass is as easy to read as a "
                                "loud kick. Mouse wheel on a name below sets that signal's height by hand. (A)");

    lengthBox.addItemList (amvscope::params::lengthNames, 1);
    syncBox.addItemList (amvscope::params::syncNames, 1);
    colourBox.addItemList (amvscope::params::colourModeNames, 1);
    sourceBox.addItemList (amvscope::params::triggerSourceNames, 1);
    sourceBox.setTooltip ("Which signal starts the picture in trigger mode. Sidechain: e.g. the kick starts the view of the bass.");
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
             &lengthLabel, &syncLabel, &zoomLabel, &thresholdLabel, &colourLabel, &sourceLabel, &lengthBox,
             &syncBox, &colourBox, &sourceBox, &offsetLabel, &offsetSlider, &invertButton, &alignButton, &viewLabel, &viewBox, &zoomSlider, &thresholdSlider, &freezeButton, &sendButton, &othersButton,
             &ghostButton, &autoLevelButton, &channelLabel, &channelBox })
        addAndMakeVisible (c);

    auto& apvts = processor.apvts;
    namespace ids = amvscope::params;
    lengthAttachment = std::make_unique<ComboAttachment> (apvts, ids::length, lengthBox);
    syncAttachment = std::make_unique<ComboAttachment> (apvts, ids::sync, syncBox);
    colourAttachment = std::make_unique<ComboAttachment> (apvts, ids::colourMode, colourBox);
    sourceAttachment = std::make_unique<ComboAttachment> (apvts, ids::triggerSource, sourceBox);
    viewAttachment = std::make_unique<ComboAttachment> (apvts, ids::view, viewBox);
    channelAttachment = std::make_unique<ComboAttachment> (apvts, ids::channel, channelBox);
    zoomAttachment = std::make_unique<SliderAttachment> (apvts, ids::zoom, zoomSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, ids::threshold, thresholdSlider);
    freezeAttachment = std::make_unique<ButtonAttachment> (apvts, ids::freeze, freezeButton);
    sendAttachment = std::make_unique<ButtonAttachment> (apvts, ids::send, sendButton);
    othersAttachment = std::make_unique<ButtonAttachment> (apvts, ids::showOthers, othersButton);
    offsetAttachment = std::make_unique<SliderAttachment> (apvts, ids::offset, offsetSlider);
    invertAttachment = std::make_unique<ButtonAttachment> (apvts, ids::invert, invertButton);
    autoLevelAttachment = std::make_unique<ButtonAttachment> (apvts, ids::autoLevel, autoLevelButton);

    // Controls must not keep the keyboard, or the shortcuts stop after one click.
    for (auto* c : std::initializer_list<juce::Component*> { &lengthBox, &syncBox, &colourBox, &sourceBox,
                                                              &freezeButton, &sendButton, &othersButton,
                                                              &invertButton, &alignButton, &viewBox,
                                                              &ghostButton, &autoLevelButton, &channelBox })
        c->setWantsKeyboardFocus (false);

    for (const auto& key : juce::StringArray::fromTokens (apvts.state.getProperty ("hidden").toString(), ",", ""))
        hidden[key] = true;

    // Saved as "own=6,sidechain=-3".
    for (const auto& item : juce::StringArray::fromTokens (apvts.state.getProperty ("gains").toString(), ",", ""))
        if (item.containsChar ('='))
            gains[item.upToFirstOccurrenceOf ("=", false, false)]
                = juce::jlimit (-36.0f, 36.0f, item.fromFirstOccurrenceOf ("=", false, false).getFloatValue());

    setOpaque (true);
    setWantsKeyboardFocus (true);
    // Read the saved size first: setting the limits already resizes the window.
    const int savedWidth = juce::jlimit (1200, 3840, (int) apvts.state.getProperty ("editorWidth", 1240));
    const int savedHeight = juce::jlimit (460, 2160, (int) apvts.state.getProperty ("editorHeight", 620));
    setResizable (true, true);
    setResizeLimits (1200, 460, 3840, 2160);
    setSize (savedWidth, savedHeight);
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

    if (toastCountdown > 0 && --toastCountdown == 0)
        toast.clear();

    repaint (scopeArea.getUnion (legendArea).getUnion (phaseArea));
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
    struct Pending
    {
        Trace trace;
        const amvscope::PpqRing* ring;
        const amvscope::SyncedCapture* capture;
        float offsetMs = 0.0f;   // display shift, positive = later
        bool inverted = false;
    };
    std::vector<Pending> pending;

    const auto& own = processor.getOwnSource();
    // "Bass (Side)": say which part of the stereo signal a trace shows, unless it is the usual Mid.
    const auto withChannel = [] (const juce::String& name, int channel)
    {
        const auto shortName = amvscope::params::channelShortNames[channel];
        return shortName.isEmpty() ? name : name + " (" + shortName + ")";
    };

    const auto ownName = own.getTrackName();
    pending.push_back ({ { "own", withChannel (ownName.isNotEmpty() ? ownName : "This track",
                                               juce::roundToInt (processor.apvts.getRawParameterValue (amvscope::params::channel)->load())),
                           hasColour (own.getTrackColour()) ? own.getTrackColour() : ownColour, true, {} },
                         &own.ring, &processor.getCapture(),
                         processor.apvts.getRawParameterValue (amvscope::params::offset)->load(),
                         processor.apvts.getRawParameterValue (amvscope::params::invert)->load() >= 0.5f });

    if (processor.isSidechainActive())
        pending.push_back ({ { "sidechain", "Sidechain", sidechainColour, true, {} },
                             &processor.getSidechainRing(), &processor.getSidechainCapture(), 0.0f, false });

    // Keep the other sources alive while we read them.
    const auto others = showOthers ? processor.getOtherSources()
                                   : std::vector<std::shared_ptr<amvscope::Source>> {};

    for (const auto& s : others)
    {
        const auto name = s->getTrackName();
        const auto colour = hasColour (s->getTrackColour())
                                ? s->getTrackColour()
                                : otherColours[(size_t) s->id % std::size (otherColours)];
        pending.push_back ({ { "src" + juce::String (s->id),
                               withChannel (name.isNotEmpty() ? name : "AMVScope " + juce::String (s->id),
                                            s->channel.load (std::memory_order_relaxed)),
                               colour, following, {} },
                             &s->ring, nullptr, s->offsetMs.load (std::memory_order_relaxed),
                             s->inverted.load (std::memory_order_relaxed) != 0 });
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

        // A trace shifted later by its offset shows, at each place, what it held that much earlier.
        const double offsetWindows = (double) p.offsetMs / windowMilliseconds();
        const double offsetQuarters = offsetWindows * window;

        for (int x = 0; x < columns; ++x)
        {
            if (following)
            {
                double from = windowStart + window * x / columns - offsetQuarters;
                double to = windowStart + window * (x + 1) / columns - offsetQuarters;

                if (to > head)
                {
                    from -= window;
                    to -= window;
                }

                t.columns[(size_t) x] = p.ring->read (from, to);
            }
            else
            {
                const double bins = (double) amvscope::SyncedCapture::numBins;
                const double firstPos = ((double) x / columns - offsetWindows) * bins;
                const double lastPos = ((double) (x + 1) / columns - offsetWindows) * bins;

                if (firstPos < 0.0 || lastPos > bins)
                {
                    t.columns[(size_t) x] = {};
                    continue;
                }

                const auto first = (size_t) firstPos;
                t.columns[(size_t) x] = p.capture->readRange (first, juce::jmax (first + 1, (size_t) lastPos));
            }

            if (p.inverted)
            {
                auto& c = t.columns[(size_t) x];
                const float oldMin = c.min;
                c.min = -c.max;
                c.max = -oldMin;
            }
        }

        next.push_back (std::move (t));
    }

    // Height of each trace: its own gain, and with Auto level the gain that
    // brings its peak to the same height as all the others.
    const bool autoLevel = processor.apvts.getRawParameterValue (amvscope::params::autoLevel)->load() >= 0.5f;

    for (auto& t : next)
    {
        t.scale = juce::Decibels::decibelsToGain (getTraceGain (t.key));

        if (! autoLevel || t.columns.empty())
            continue;

        float peak = 0.0f;
        for (const auto& c : t.columns)
            if (c.filled)
                peak = juce::jmax (peak, std::abs (c.min), std::abs (c.max));

        // Rise at once, fall slowly, so the picture does not pump.
        const auto held = heldPeaks.find (t.key);
        if (held != heldPeaks.end() && peak < held->second)
            peak = held->second + (peak - held->second) * 0.05f;

        heldPeaks[t.key] = peak;

        if (peak > 1.0e-4f)
            t.scale *= juce::jmin (0.9f / peak, 250.0f);
    }

    traces = std::move (next);

    // The phase readout does not need 60 updates a second.
    if (--phaseCountdown <= 0)
    {
        phaseCountdown = 6;
        updatePhase();
        alignButton.setEnabled (phase.valid && phase.related);
    }
}

void AMVScopeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16, 0);
    headerArea = bounds.removeFromTop (48);
    footerArea = bounds.removeFromBottom (80);
    scopeArea = bounds;

    auto row = headerArea.withTrimmedLeft (110).withSizeKeepingCentre (headerArea.getWidth() - 110, 26);

    auto place = [] (juce::Rectangle<int>& r, juce::Label& label, juce::Component& c, int labelWidth, int width)
    {
        label.setBounds (r.removeFromLeft (labelWidth));
        r.removeFromLeft (6);
        c.setBounds (r.removeFromLeft (width));
        r.removeFromLeft (14);
    };

    // The two sliders share whatever width is left.
    const int fixed = (48 + 6 + 86 + 14) + (34 + 6 + 86 + 14) + (36 + 6 + 90 + 14) + (38 + 6 + 14) + (48 + 6 + 14) + 72 + 8 + 72;
    const int sliderWidth = juce::jlimit (110, 260, (row.getWidth() - fixed) / 2);

    place (row, lengthLabel, lengthBox, 48, 86);
    place (row, syncLabel, syncBox, 34, 86);
    place (row, viewLabel, viewBox, 36, 90);
    place (row, zoomLabel, zoomSlider, 38, sliderWidth);
    place (row, thresholdLabel, thresholdSlider, 48, sliderWidth);
    freezeButton.setBounds (row.removeFromRight (72));
    row.removeFromRight (8);
    ghostButton.setBounds (row.removeFromRight (72));

    processor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
    processor.apvts.state.setProperty ("editorHeight", getHeight(), nullptr);

    // Bottom row: phase tools.
    auto phaseRow = footerArea.removeFromBottom (38).withTrimmedBottom (10).withTrimmedTop (2);
    alignButton.setBounds (phaseRow.removeFromRight (64));
    phaseRow.removeFromRight (10);
    invertButton.setBounds (phaseRow.removeFromRight (76));
    phaseRow.removeFromRight (8);
    offsetSlider.setBounds (phaseRow.removeFromRight (230));
    phaseRow.removeFromRight (6);
    offsetLabel.setBounds (phaseRow.removeFromRight (44));
    phaseRow.removeFromRight (14);
    phaseArea = phaseRow;

    auto foot = footerArea.withSizeKeepingCentre (footerArea.getWidth(), 26);
    othersButton.setBounds (foot.removeFromRight (110));
    foot.removeFromRight (8);
    sendButton.setBounds (foot.removeFromRight (70));
    foot.removeFromRight (14);
    colourBox.setBounds (foot.removeFromRight (100));
    foot.removeFromRight (6);
    colourLabel.setBounds (foot.removeFromRight (46));
    foot.removeFromRight (14);
    sourceBox.setBounds (foot.removeFromRight (106));
    foot.removeFromRight (6);
    sourceLabel.setBounds (foot.removeFromRight (58));
    foot.removeFromRight (14);
    channelBox.setBounds (foot.removeFromRight (104));
    foot.removeFromRight (6);
    channelLabel.setBounds (foot.removeFromRight (52));
    foot.removeFromRight (10);
    autoLevelButton.setBounds (foot.removeFromRight (98));
    foot.removeFromRight (10);
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
            saveHidden();
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

    if (isBandsView())
    {
        drawBands (g, inner);
    }
    else
    {
        if (ghostFitsWindow())
            for (auto it = processor.ghostTraces.rbegin(); it != processor.ghostTraces.rend(); ++it)
                drawTrace (g, inner, *it, false, 0.3f);

        // Others first, then the sidechain, then this track on top.
        // Frequency colours apply to this track only, so the others stay easy to tell apart.
        for (auto it = traces.rbegin(); it != traces.rend(); ++it)
            if (it->available && ! hidden[it->key])
                drawTrace (g, inner, *it, frequency && it->key == "own", it->key == "own" ? 1.0f : 0.85f);
    }

    drawOverlays (g, inner);
    drawMeasure (g, inner);
    drawLegend (g);
    drawPhase (g, phaseArea.toFloat());
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

    if (isBandsView())
        return;   // the bands view draws its own row lines

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

    const float gain = zoom * t.scale;
    auto toY = [&] (float v) { return midY - juce::jlimit (-1.0f, 1.0f, v * gain) * halfHeight; };

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
        const double position = head / window - std::floor (head / window);
        const float x = area.getX() + area.getWidth() * (float) position;
        g.setColour (text.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }

    if (! following && ! isBandsView())
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

    // Where the phase is read: the stretch in which both signals sound.
    if (phase.valid && ! overlapColumns.empty())
    {
        const float columnWidth = area.getWidth() / (float) overlapColumns.size();
        g.setColour (text.withAlpha (0.55f));

        for (size_t x = 0; x < overlapColumns.size(); ++x)
            if (overlapColumns[x])
                g.fillRect (area.getX() + (float) x * columnWidth, area.getBottom() - 2.0f,
                            juce::jmax (1.0f, columnWidth), 2.0f);
    }

    juce::String status = following ? "Host  " + juce::String (processor.getBpm(), 1) + " BPM" : "Trigger";
    if (frozen)
        status << "  |  Frozen";
    if (ghostFitsWindow())
        status << "  |  Ghost";

    g.setColour (text.withAlpha (following ? 0.6f : 0.9f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (status, area.reduced (8.0f, 2.0f).toNearestInt(), juce::Justification::topRight);

    if (toast.isNotEmpty())
    {
        const int width = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (12.0f)), toast) + 20;
        const auto box = juce::Rectangle<float> ((float) width, 24.0f)
                             .withCentre ({ area.getCentreX(), area.getBottom() - 24.0f });
        g.setColour (background.withAlpha (0.9f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (text);
        g.drawText (toast, box.toNearestInt(), juce::Justification::centred);
    }
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
        auto label = t.available ? t.label : t.label + " (host sync only)";

        // Show how much the trace is raised or lowered on screen.
        const float db = juce::Decibels::gainToDecibels (t.scale);
        if (t.available && ! isHidden && std::abs (db) >= 0.05f)
            label << "  " << (db > 0.0f ? "+" : "") << juce::String (db, 1) << " dB";
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

//==============================================================================
// Mouse readout, keys, saved view

double AMVScopeAudioProcessorEditor::windowMilliseconds() const
{
    return processor.getWindowQuarters() * 60000.0 / juce::jmax (1.0, processor.getBpm());
}

void AMVScopeAudioProcessorEditor::setParam (const char* id, float value)
{
    if (auto* param = processor.apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (value));
        param->endChangeGesture();
    }
}

void AMVScopeAudioProcessorEditor::toggleParam (const char* id)
{
    setParam (id, processor.apvts.getRawParameterValue (id)->load() >= 0.5f ? 0.0f : 1.0f);
}

void AMVScopeAudioProcessorEditor::saveHidden()
{
    // Only this track and the sidechain: other instances get new ids in every session.
    juce::StringArray keys;
    for (const auto* key : { "own", "sidechain" })
        if (hidden[key])
            keys.add (key);

    processor.apvts.state.setProperty ("hidden", keys.joinIntoString (","), nullptr);
}

juce::File AMVScopeAudioProcessorEditor::savePicture (const juce::File& folder)
{
    toast.clear();   // keep an older message out of the picture
    const auto image = createComponentSnapshot (getLocalBounds(), true, 2.0f);
    const auto file = folder.getNonexistentChildFile (
        "AMVScope " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"), ".png", false);

    bool saved = false;

    if (folder.createDirectory().wasOk())
    {
        juce::FileOutputStream stream (file);
        saved = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    toast = saved ? "Saved: " + file.getFullPathName() : "Could not save the picture to " + folder.getFullPathName();
    toastCountdown = 60 * 4;
    repaint();
    return saved ? file : juce::File();
}

void AMVScopeAudioProcessorEditor::saveGains()
{
    juce::StringArray items;
    for (const auto* key : { "own", "sidechain" })
        if (std::abs (getTraceGain (key)) >= 0.05f)
            items.add (juce::String (key) + "=" + juce::String (getTraceGain (key), 1));

    processor.apvts.state.setProperty ("gains", items.joinIntoString (","), nullptr);
}

float AMVScopeAudioProcessorEditor::getTraceGain (const juce::String& key) const
{
    const auto it = gains.find (key);
    return it != gains.end() ? it->second : 0.0f;
}

void AMVScopeAudioProcessorEditor::setTraceGain (const juce::String& key, float db)
{
    gains[key] = juce::jlimit (-36.0f, 36.0f, db);
    saveGains();
    frozenView = false;
    refresh();
    repaint();
}

const juce::String* AMVScopeAudioProcessorEditor::legendKeyAt (juce::Point<int> position) const
{
    for (const auto& [rect, key] : legendHits)
        if (rect.contains (position))
            return &key;

    return nullptr;
}

bool AMVScopeAudioProcessorEditor::ghostFitsWindow() const
{
    // A ghost of another Length would not line up with the grid.
    return hasGhost() && std::abs (processor.ghostWindowQuarters - processor.getWindowQuarters()) < 1.0e-9;
}

void AMVScopeAudioProcessorEditor::toggleGhost()
{
    if (hasGhost())
    {
        processor.ghostTraces.clear();
    }
    else
    {
        for (const auto& t : traces)
        {
            if (! t.available || hidden[t.key] || t.columns.empty())
                continue;

            auto copy = t;
            copy.colour = t.colour.withMultipliedSaturation (0.55f);
            processor.ghostTraces.push_back (std::move (copy));
        }

        processor.ghostWindowQuarters = processor.getWindowQuarters();
    }

    ghostButton.setToggleState (hasGhost(), juce::dontSendNotification);
    repaint();
}

void AMVScopeAudioProcessorEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto plot = getPlotArea();
    hovering = plot.contains (e.position);

    if (hovering)
        hoverPos = { (e.position.x - plot.getX()) / plot.getWidth(), (e.position.y - plot.getY()) / plot.getHeight() };
}

void AMVScopeAudioProcessorEditor::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
}

void AMVScopeAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    const auto plot = getPlotArea();

    if (plot.contains (e.position) && e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save picture (P)");
        menu.addItem (2, hasGhost() ? "Remove ghost (G)" : "Remember as ghost (G)");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safe = juce::Component::SafePointer<AMVScopeAudioProcessorEditor> (this)] (int choice)
                            {
                                if (safe == nullptr)
                                    return;

                                if (choice == 1)
                                    safe->savePicture (juce::File::getSpecialLocation (juce::File::userPicturesDirectory)
                                                           .getChildFile ("AMVScope"));
                                else if (choice == 2)
                                    safe->toggleGhost();
                            });
        return;
    }

    if (plot.contains (e.position))
    {
        // Start a new time measurement; a plain click clears the old one.
        measuring = false;
        measureFrom = measureTo = (e.position.x - plot.getX()) / plot.getWidth();
    }
}

void AMVScopeAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto plot = getPlotArea();

    if (! plot.contains (e.mouseDownPosition))
        return;

    measureTo = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
    measuring = std::abs (measureTo - measureFrom) * plot.getWidth() >= 2.0f;
    mouseMove (e);
}

void AMVScopeAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double click on a name in the legend: its height back to normal.
    // (The two clicks already hid and showed it again.)
    if (const auto* key = legendKeyAt (e.getPosition()))
    {
        setTraceGain (*key, 0.0f);
        return;
    }

    // Double click on the plot: zoom back to 1x.
    if (getPlotArea().contains (e.position))
        setParam (amvscope::params::zoom, 1.0f);
}

void AMVScopeAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Wheel on a name in the legend: height of that signal, 1 dB per step.
    if (const auto* key = legendKeyAt (e.getPosition()))
    {
        if (wheel.deltaY > 0.0f || wheel.deltaY < 0.0f)
            setTraceGain (*key, getTraceGain (*key) + (wheel.deltaY > 0.0f ? 1.0f : -1.0f));
        return;
    }

    if (! getPlotArea().contains (e.position))
        return;

    const float zoom = processor.apvts.getRawParameterValue (amvscope::params::zoom)->load();
    setParam (amvscope::params::zoom, juce::jlimit (1.0f, 32.0f, zoom * std::pow (2.0f, wheel.deltaY * 2.0f)));
}

bool AMVScopeAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    namespace ids = amvscope::params;
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const float zoom = processor.apvts.getRawParameterValue (ids::zoom)->load();

    if (c >= '1' && c <= '7')                         setParam (ids::length, (float) (c - '1'));
    else if (c == 'f' || key == juce::KeyPress::spaceKey) toggleParam (ids::freeze);
    else if (c == 's')                                toggleParam (ids::sync);
    else if (c == 'c')                                toggleParam (ids::colourMode);
    else if (c == 'o')                                toggleParam (ids::showOthers);
    else if (c == 'b')                                toggleParam (ids::view);
    else if (c == 'a')                                toggleParam (ids::autoLevel);
    else if (c == 'g')                                toggleGhost();
    else if (c == 'p')                                savePicture (juce::File::getSpecialLocation (juce::File::userPicturesDirectory)
                                                                       .getChildFile ("AMVScope"));
    else if (c == '+' || c == '=' || key == juce::KeyPress::upKey)   setParam (ids::zoom, juce::jmin (32.0f, zoom * 1.5f));
    else if (c == '-' || key == juce::KeyPress::downKey)             setParam (ids::zoom, juce::jmax (1.0f, zoom / 1.5f));
    else if (key == juce::KeyPress::escapeKey)        measuring = false;
    else                                              return false;

    return true;
}

void AMVScopeAudioProcessorEditor::drawMeasure (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const double windowMs = windowMilliseconds();
    g.setFont (juce::FontOptions (12.0f));

    auto label = [&] (const juce::String& s, float x, float y)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (12.0f)), s) + 12;
        auto box = juce::Rectangle<float> (x + 8.0f, y, (float) w, 20.0f);

        if (box.getRight() > area.getRight())
            box.setX (x - 8.0f - (float) w);

        box.setY (juce::jlimit (area.getY(), area.getBottom() - 20.0f, y));
        g.setColour (background.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (text);
        g.drawText (s, box.toNearestInt(), juce::Justification::centred);
    };

    if (measuring)
    {
        const float a = juce::jmin (measureFrom, measureTo), b = juce::jmax (measureFrom, measureTo);
        const float xa = area.getX() + a * area.getWidth(), xb = area.getX() + b * area.getWidth();

        g.setColour (text.withAlpha (0.08f));
        g.fillRect (xa, area.getY(), xb - xa, area.getHeight());
        g.setColour (text.withAlpha (0.6f));
        g.drawVerticalLine (juce::roundToInt (xa), area.getY(), area.getBottom());
        g.drawVerticalLine (juce::roundToInt (xb), area.getY(), area.getBottom());

        const double ms = (double) (b - a) * windowMs;
        label (juce::String (ms, 1) + " ms  =  " + juce::String (1000.0 / juce::jmax (0.001, ms), 1) + " Hz",
               xb, area.getY() + 24.0f);
    }

    if (hovering)
    {
        const float x = area.getX() + hoverPos.x * area.getWidth();
        const float y = area.getY() + hoverPos.y * area.getHeight();
        const float zoom = processor.apvts.getRawParameterValue (amvscope::params::zoom)->load();
        // In the bands view each of the three rows has its own zero line at the bottom.
        const float rowPos = hoverPos.y * 3.0f - std::floor (juce::jmin (2.999f, hoverPos.y * 3.0f));
        const float level = (isBandsView() ? 1.0f - rowPos : std::abs (1.0f - 2.0f * hoverPos.y)) / zoom;

        g.setColour (text.withAlpha (0.25f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());

        // The level under the mouse is only true while the traces are drawn at their real height.
        bool trueHeight = true;
        for (const auto& t : traces)
            if (! t.columns.empty() && std::abs (t.scale - 1.0f) >= 0.01f)
                trueHeight = false;

        juce::String readout = juce::String ((double) hoverPos.x * windowMs, 1) + " ms";
        if (trueHeight)
            readout << "   " << (level < 1.0e-4f ? juce::String ("-inf") : juce::String (juce::Decibels::gainToDecibels (level), 1))
                    << " dB";

        label (readout, x, y - 26.0f);
    }
}

//==============================================================================
// Phase

void AMVScopeAudioProcessorEditor::updatePhase()
{
    PhaseInfo info;
    overlapColumns.clear();

    const Trace* own = nullptr;
    const Trace* partner = nullptr;

    for (const auto& t : traces)
    {
        if (! t.available || hidden[t.key] || t.columns.empty())
            continue;

        if (t.key == "own")
            own = &t;
        else if (partner == nullptr)
            partner = &t;
    }

    const auto columns = own != nullptr ? own->columns.size() : 0;
    const double msPerColumn = columns > 0 ? windowMilliseconds() / (double) columns : 0.0;

    // Longer windows squeeze a bass cycle into a pixel or two; the number would mean nothing.
    if (own == nullptr || partner == nullptr || partner->columns.size() != columns || msPerColumn > 1.5)
    {
        phase = info;
        return;
    }

    std::vector<float> a (columns), b (columns);
    std::vector<bool> valid (columns);
    int numValid = 0;

    for (size_t i = 0; i < columns; ++i)
    {
        const auto& pc = partner->columns[i];
        const auto& oc = own->columns[i];
        valid[i] = pc.filled && oc.filled;
        a[i] = 0.5f * (pc.min + pc.max);
        b[i] = 0.5f * (oc.min + oc.max);
        numValid += valid[i] ? 1 : 0;
    }

    if (numValid < (int) columns / 4)
    {
        phase = info;
        return;
    }

    // Kick and bass often share only a short stretch (the tail of the kick under
    // the start of the bass). Read the phase only where both sound: where each is
    // above a tenth of its own peak (8 ms gaps are bridged, to span zero crossings).
    {
        const auto envelope = [&] (const Trace& t)
        {
            std::vector<float> level (columns, 0.0f), env (columns, 0.0f);
            float peak = 0.0f;

            for (size_t i = 0; i < columns; ++i)
            {
                const auto& c = t.columns[i];
                level[i] = c.filled ? juce::jmax (std::abs (c.min), std::abs (c.max)) : 0.0f;
                peak = juce::jmax (peak, level[i]);
            }

            const auto reach = (size_t) std::ceil (8.0 / msPerColumn);

            for (size_t i = 0; i < columns; ++i)
                for (size_t j = i > reach ? i - reach : 0; j < juce::jmin (columns, i + reach + 1); ++j)
                    env[i] = juce::jmax (env[i], level[j]);

            // Shrink back by the same amount, so the envelope ends where the sound ends.
            std::vector<float> closed (columns, 0.0f);

            for (size_t i = 0; i < columns; ++i)
            {
                float lowest = env[i];
                for (size_t j = i > reach ? i - reach : 0; j < juce::jmin (columns, i + reach + 1); ++j)
                    lowest = juce::jmin (lowest, env[j]);

                closed[i] = peak > 1.0e-5f ? lowest / peak : 0.0f;
            }

            return closed;
        };

        const auto envA = envelope (*partner), envB = envelope (*own);
        int overlap = 0;

        for (size_t i = 0; i < columns; ++i)
        {
            valid[i] = valid[i] && envA[i] > 0.1f && envB[i] > 0.1f;
            overlap += valid[i] ? 1 : 0;
        }

        info.overlapMs = (float) ((double) overlap * msPerColumn);
        overlapColumns = valid;
    }

    info.valid = true;
    info.partner = partner->label;

    if (info.overlapMs < 10.0f)
    {
        overlapColumns.clear();
        phase = info;   // shown as "do not overlap"
        return;
    }

    const float offsetNow = processor.apvts.getRawParameterValue (amvscope::params::offset)->load();

    // A shifted track wraps around the window edge, where it meets the wrong
    // beat of the other signal; leave that strip out of the comparison.
    const auto edge = juce::jmin (columns / 4, (size_t) std::ceil (std::abs ((double) offsetNow) / msPerColumn));
    for (size_t i = 0; i < edge; ++i)
        valid[i] = valid[columns - 1 - i] = false;

    const bool invertNow = processor.apvts.getRawParameterValue (amvscope::params::invert)->load() >= 0.5f;

    info.correlation = amvscope::correlation (a, b, valid);

    // Search +-20 ms around where the track is now, but no further than a quarter
    // of the shared stretch: beyond that too little is left to compare.
    const double searchMs = juce::jmin (20.0, (double) info.overlapMs * 0.25);
    const auto best = amvscope::findBestShift (a, b, valid, juce::jmax (1, (int) (searchMs / msPerColumn)));
    info.suggestedOffsetMs = juce::jlimit (-50.0f, 50.0f, offsetNow - (float) ((double) best.exactShift * msPerColumn));
    info.suggestedInvert = best.inverted ? ! invertNow : invertNow;
    info.suggestedCorrelation = std::abs (best.correlation);

    // When even the best shift barely matches, the signals do not play at the
    // same time (e.g. a bass that starts after the kick), and a suggestion would be noise.
    info.related = info.suggestedCorrelation >= 0.2f;

    phase = info;
}

void AMVScopeAudioProcessorEditor::alignToSuggestion()
{
    if (! phase.valid || ! phase.related)
        return;

    setParam (amvscope::params::offset, phase.suggestedOffsetMs);
    setParam (amvscope::params::invert, phase.suggestedInvert ? 1.0f : 0.0f);
    frozenView = false;
    phaseCountdown = 0;
}

void AMVScopeAudioProcessorEditor::drawPhase (juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setFont (juce::FontOptions (13.0f));

    if (! phase.valid)
    {
        g.setColour (text.withAlpha (0.4f));
        g.drawText ("Phase: needs a second signal and Length of 1/2 or shorter", area.toNearestInt(),
                    juce::Justification::centredLeft);
        return;
    }

    const float c = phase.correlation;
    const auto colour = c > 0.3f ? midColour : (c < -0.3f ? lowColour : juce::Colour (0xffffd43b));

    // Meter from -1 (left) to +1 (right), filled from the centre.
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (text.withAlpha (0.5f));
    g.drawText ("-1", area.removeFromLeft (16.0f).toNearestInt(), juce::Justification::centredLeft);
    auto meter = area.removeFromLeft (104.0f).withSizeKeepingCentre (104.0f, 8.0f);
    g.drawText ("+1", area.removeFromLeft (22.0f).toNearestInt(), juce::Justification::centredRight);
    g.setFont (juce::FontOptions (13.0f));
    g.setColour (gridMajor);
    g.fillRoundedRectangle (meter, 4.0f);
    const float centre = meter.getCentreX();
    const float end = centre + c * meter.getWidth() * 0.5f;
    g.setColour (colour);
    g.fillRoundedRectangle (juce::jmin (centre, end), meter.getY(), std::abs (end - centre), meter.getHeight(), 3.0f);
    g.setColour (text.withAlpha (0.6f));
    g.fillRect (centre - 0.5f, meter.getY() - 3.0f, 1.0f, meter.getHeight() + 6.0f);

    area.removeFromLeft (10.0f);
    juce::String s = "Phase vs " + phase.partner;
    if (phase.related)
        s << " (overlap " << juce::String (juce::roundToInt (phase.overlapMs)) << " ms)";
    s << ":  " << (c >= 0.0f ? "+" : "") << juce::String (c, 2);
    if (phase.related)
        s << "     Best: " << juce::String (phase.suggestedOffsetMs, 1) << " ms"
          << (phase.suggestedInvert ? ", inverted" : "")
          << "  (" << juce::String (phase.suggestedCorrelation, 2) << ")";
    else
        s << "     The signals do not overlap, nothing to align";

    g.setColour (text.withAlpha (0.85f));
    g.drawText (s, area.toNearestInt(), juce::Justification::centredLeft);
}

//==============================================================================
// Bands view

bool AMVScopeAudioProcessorEditor::isBandsView() const
{
    return juce::roundToInt (processor.apvts.getRawParameterValue (amvscope::params::view)->load())
           == (int) amvscope::params::View::bands;
}

void AMVScopeAudioProcessorEditor::drawBands (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const float zoom = processor.apvts.getRawParameterValue (amvscope::params::zoom)->load();
    const char* names[] { "High  > 2.5 kHz", "Mid", "Low  < 200 Hz" };
    const float rowHeight = area.getHeight() / 3.0f;

    for (int row = 0; row < 3; ++row)
    {
        const auto rowArea = juce::Rectangle<float> (area.getX(), area.getY() + rowHeight * (float) row,
                                                     area.getWidth(), rowHeight).reduced (0.0f, 3.0f);
        const float bottom = rowArea.getBottom();

        // Each signal as a filled level curve; a ghost as its outline only.
        const auto drawOne = [&] (const Trace& t, bool ghost)
        {
            const auto columns = t.columns.size();
            const float columnWidth = columns > 0 ? rowArea.getWidth() / (float) columns : 1.0f;
            const float gain = zoom * t.scale;
            juce::Path outline;
            bool open = false;

            g.setColour (t.colour.withAlpha (t.key == "own" ? 0.45f : 0.3f));

            for (size_t x = 0; x < columns; ++x)
            {
                const auto& c = t.columns[x];

                if (! c.filled)
                {
                    open = false;
                    continue;
                }

                const float level = row == 0 ? c.bands.high : (row == 1 ? c.bands.mid : c.bands.low);
                const float h = juce::jlimit (0.0f, 1.0f, level * gain) * rowArea.getHeight();
                const float px = rowArea.getX() + (float) x * columnWidth;

                if (! ghost)
                    g.fillRect (px, bottom - h, juce::jmax (1.0f, columnWidth), h);

                if (open)
                    outline.lineTo (px, bottom - h);
                else
                    outline.startNewSubPath (px, bottom - h);

                open = true;
            }

            g.setColour (t.colour.withAlpha (ghost ? 0.45f : 1.0f));
            g.strokePath (outline, juce::PathStrokeType (ghost ? 1.0f : 1.2f));
        };

        if (ghostFitsWindow())
            for (auto it = processor.ghostTraces.rbegin(); it != processor.ghostTraces.rend(); ++it)
                drawOne (*it, true);

        // Others first, this track on top.
        for (auto it = traces.rbegin(); it != traces.rend(); ++it)
            if (it->available && ! (hidden.count (it->key) > 0 && hidden.at (it->key)))
                drawOne (*it, false);

        g.setColour (gridMajor);
        g.drawHorizontalLine (juce::roundToInt (bottom), area.getX(), area.getRight());
        g.setColour (text.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (names[row], rowArea.reduced (8.0f, 2.0f).toNearestInt(), juce::Justification::topLeft);
    }
}
