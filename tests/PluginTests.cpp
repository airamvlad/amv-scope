// End-to-end checks of the plugin processor, without a DAW.

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

/** A host transport we can move by hand. */
struct TestPlayHead final : juce::AudioPlayHead
{
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setPpqPosition (ppq);
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        info.setTimeSignature (TimeSignature { 4, 4 });
        return info;
    }
};

void setParam (AMVScopeAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void prepare (AMVScopeAudioProcessor& p)
{
    p.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

/** Feeds blocks where every sample is produced by gen(n), moving the play head along. */
template <typename Gen>
void run (AMVScopeAudioProcessor& p, TestPlayHead& head, int totalSamples, Gen gen)
{
    juce::AudioBuffer<float> buffer (p.getTotalNumInputChannels(), blockSize);
    juce::MidiBuffer midi;
    const double quartersPerSample = head.bpm / 60.0 / sampleRate;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = gen (start + i);
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);

            // Sidechain (channels 2-3, when enabled): the signal half a beat later, inverted.
            for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, -0.5f * gen (start + i + 12000));
        }

        p.processBlock (buffer, midi);

        if (head.playing)
            head.ppq += quartersPerSample * blockSize;
    }
}

/** Peak of the own ring over [from, to) quarter notes. */
float ringMax (const amvscope::PpqRing& ring, double from, double to)
{
    return ring.read (from, to).max;
}

int filledBins (const AMVScopeAudioProcessor& p)
{
    int n = 0;
    for (std::size_t i = 0; i < amvscope::SyncedCapture::numBins; ++i)
        n += p.getCapture().readBin (i).filled ? 1 : 0;
    return n;
}

void audioPassesThroughUnchanged()
{
    AMVScopeAudioProcessor p;
    prepare (p);

    juce::AudioBuffer<float> buffer (2, blockSize);
    for (int i = 0; i < blockSize; ++i)
    {
        buffer.setSample (0, i, std::sin (0.01f * (float) i));
        buffer.setSample (1, i, 0.5f * std::cos (0.02f * (float) i));
    }

    juce::AudioBuffer<float> original (buffer);
    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);

    bool same = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < blockSize; ++i)
            same = same && juce::exactlyEqual (buffer.getSample (ch, i), original.getSample (ch, i));

    check (same, "audio is not changed");
}

void hostSyncPlacesBeatsInPlace()
{
    // 1/4 window at 120 BPM = 24000 samples. A click on every beat must land in bin 0.
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    p.setPlayHead (&head);
    prepare (p);
    setParam (p, amvscope::params::length, 2.0f);   // 1/4

    const int samplesPerBeat = 24000;
    run (p, head, samplesPerBeat * 4, [&] (int n) { return n % samplesPerBeat < 10 ? 0.9f : 0.0f; });

    const auto& ring = p.getOwnSource().ring;
    check (p.isFollowingHost(), "follows the host while playing");
    check (std::abs (p.getWindowQuarters() - 1.0) < 1e-9, "window is one quarter note");
    check (ringMax (ring, 3.0, 3.001) > 0.8f, "beat click recorded at the beat position");
    check (ringMax (ring, 3.5, 3.501) < 0.01f, "middle of the beat is silent");
    check (std::abs (ring.getHead() - 4.0) < 0.02, "ring head follows the play position");
    check (filledBins (p) == 0, "trigger capture unused while following the host");
}

void barLengthFollowsTimeSignature()
{
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    p.setPlayHead (&head);
    prepare (p);
    setParam (p, amvscope::params::length, 5.0f);   // 2 bars
    run (p, head, blockSize, [] (int) { return 0.0f; });
    check (std::abs (p.getWindowQuarters() - 8.0) < 1e-9, "2 bars of 4/4 = 8 quarters");
}

void triggerWhenStopped()
{
    // Transport stopped: nothing is drawn until the level crosses the trigger,
    // then exactly one window is recorded starting at the hit.
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    head.playing = false;
    p.setPlayHead (&head);
    prepare (p);
    setParam (p, amvscope::params::length, 0.0f);      // 1/16 = 6000 samples at 120 BPM
    setParam (p, amvscope::params::threshold, -12.0f);

    run (p, head, 4096, [] (int) { return 0.05f; });
    check (! p.isFollowingHost(), "trigger mode while stopped");
    check (filledBins (p) == 0, "nothing drawn below the trigger level");

    const int hit = 4096 + 700;
    run (p, head, 12288, [&] (int n) {
        const int t = n + 4096 - hit;   // run() restarts n at 0
        return t >= 0 && t < 6000 ? (t < 50 ? 0.9f : 0.2f) : 0.0f;
    });

    check (p.getCapture().readBin (0).max > 0.8f, "trigger hit is at the left edge");
    check (filledBins (p) == (int) amvscope::SyncedCapture::numBins, "one full window recorded");
    check (p.getCapture().readBin (1500).max > 0.15f, "rest of the window recorded");
}

void triggerModeWhilePlaying()
{
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    p.setPlayHead (&head);
    prepare (p);
    setParam (p, amvscope::params::sync, 1.0f);   // Trigger
    run (p, head, blockSize, [] (int) { return 0.0f; });
    check (! p.isFollowingHost(), "Trigger sync ignores the transport");
}

void freezeStopsRecording()
{
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    p.setPlayHead (&head);
    prepare (p);
    setParam (p, amvscope::params::freeze, 1.0f);
    run (p, head, 48000, [] (int) { return 0.5f; });
    check (filledBins (p) == 0, "frozen display does not change");
    check (! p.getOwnSource().ring.hasData(), "frozen: nothing recorded");
}

void sidechainIsRecorded()
{
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    p.setPlayHead (&head);
    check (p.enableAllBuses(), "sidechain bus can be enabled");
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);

    const int samplesPerBeat = 24000;
    run (p, head, samplesPerBeat * 4, [&] (int n) { return n % samplesPerBeat < 10 ? 0.9f : 0.0f; });

    check (p.isSidechainActive(), "sidechain marked active");
    const auto& sc = p.getSidechainRing();
    check (sc.read (2.5, 2.501).min < -0.4f, "sidechain click recorded half a beat later");
    check (sc.read (3.0, 3.001).min > -0.01f, "sidechain silent on the beat");
}

void sidechainInTriggerMode()
{
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    head.playing = false;
    p.setPlayHead (&head);
    p.enableAllBuses();
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    setParam (p, amvscope::params::length, 2.0f);   // 1/4 = 24000 samples

    // Click every 30000 samples: main triggers, sidechain lands 12000 samples before the next click.
    run (p, head, 60000, [] (int n) { return n % 30000 < 10 ? 0.9f : 0.0f; });
    check (p.getCapture().readBin (0).max > 0.8f, "main trigger at the left edge");
    check (p.getSidechainCapture().readRange (0, amvscope::SyncedCapture::numBins).min < -0.4f,
           "sidechain recorded in the same window");
}

void triggerFromSidechain()
{
    // Main input is a steady quiet tone that never crosses the trigger level;
    // the sidechain click (half a beat after each main "beat") starts the picture.
    AMVScopeAudioProcessor p;
    TestPlayHead head;
    head.playing = false;
    p.setPlayHead (&head);
    p.enableAllBuses();
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
    setParam (p, amvscope::params::length, 0.0f);          // 1/16 = 6000 samples
    setParam (p, amvscope::params::threshold, -12.0f);     // 0.25; sidechain click is 0.45

    auto gen = [] (int n) { return n % 30000 < 10 ? 0.9f : 0.0f; };

    setParam (p, amvscope::params::triggerSource, 1.0f);
    run (p, head, 60000, gen);
    check (p.getSidechainCapture().readBin (0).min < -0.4f, "sidechain click at the left edge");
    check (p.getCapture().readBin (0).max < 0.01f, "main is silent where the sidechain triggered");
}

void editorKeysAndSize()
{
    AMVScopeAudioProcessor p;
    prepare (p);
    juce::MemoryBlock data;

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        check (ed->isResizable(), "window is resizable");
        ed->setSize (1280, 720);

        check (ed->keyPressed (juce::KeyPress ('f', {}, 'f')), "F handled");
        check (p.apvts.getRawParameterValue (amvscope::params::freeze)->load() >= 0.5f, "F freezes");
        ed->keyPressed (juce::KeyPress ('5', {}, '5'));
        check (juce::roundToInt (p.apvts.getRawParameterValue (amvscope::params::length)->load()) == 4, "5 picks 1 bar");
        ed->keyPressed (juce::KeyPress ('+', {}, '+'));
        check (p.apvts.getRawParameterValue (amvscope::params::zoom)->load() > 1.4f, "+ zooms in");
        check (! ed->keyPressed (juce::KeyPress ('q', {}, 'q')), "other keys are left to the host");
        ed->keyPressed (juce::KeyPress ('b', {}, 'b'));
        check (juce::roundToInt (p.apvts.getRawParameterValue (amvscope::params::view)->load()) == 1, "B switches to the bands view");

        // Painting both views must not crash without any audio.
        ed->createComponentSnapshot (ed->getLocalBounds());
        ed->keyPressed (juce::KeyPress ('b', {}, 'b'));
        ed->createComponentSnapshot (ed->getLocalBounds());

        p.getStateInformation (data);
    }

    AMVScopeAudioProcessor q;
    q.setStateInformation (data.getData(), (int) data.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
    check (ed->getWidth() == 1280 && ed->getHeight() == 720, "window size restored");
}

void phaseSuggestionAlignsTracks()
{
    // A plays a kick; B plays the same kick 240 samples (5 ms) late.
    // B's window must suggest Offset = -5 ms, and Align must bring them together.
    TestPlayHead headA, headB;
    AMVScopeAudioProcessor a, b;
    a.setPlayHead (&headA);
    b.setPlayHead (&headB);
    prepare (a);
    prepare (b);

    auto kick = [] (int n)
    {
        if (n < 0) return 0.0f;
        const double t = (double) (n % 24000) / sampleRate;
        return (float) (0.9 * std::exp (-t * 14.0) * std::sin (6.283185307 * (50.0 + 150.0 * std::exp (-t * 40.0)) * t));
    };

    run (a, headA, 24000 * 3, kick);
    run (b, headB, 24000 * 3, [&] (int n) { return kick (n - 240); });

    std::unique_ptr<juce::AudioProcessorEditor> ed (b.createEditor());
    auto* editor = dynamic_cast<AMVScopeAudioProcessorEditor*> (ed.get());
    editor->refresh();

    const auto before = editor->getPhaseInfo();
    check (before.valid, "phase readout available with two tracks");
    check (before.correlation < 0.9f, "delayed copy is not fully in phase");
    check (std::abs (before.suggestedOffsetMs + 5.0f) < 0.8f, "suggests moving the late track 5 ms earlier");
    check (! before.suggestedInvert, "no polarity flip suggested");

    editor->alignToSuggestion();
    run (b, headB, blockSize, [&] (int n) { return kick (n + 24000 * 3 - 240); });   // lets the offset reach the source
    editor->refresh();
    const auto after = editor->getPhaseInfo();
    check (after.correlation > 0.97f, "aligned tracks are in phase");
    check (std::abs (a.getOtherSources().front()->offsetMs.load() + 5.0f) < 0.8f, "offset is shared with other windows");

    // Flipping the polarity turns the reading negative.
    setParam (b, amvscope::params::invert, 1.0f);
    for (int i = 0; i < 6; ++i) editor->refresh();
    check (editor->getPhaseInfo().correlation < -0.97f, "inverted track reads negative");
    check (editor->getPhaseInfo().suggestedInvert == false, "suggests removing the flip");
}

void instancesSeeEachOther()
{
    TestPlayHead headA, headB;
    AMVScopeAudioProcessor b;
    b.setPlayHead (&headB);
    prepare (b);

    {
        AMVScopeAudioProcessor a;
        a.setPlayHead (&headA);
        prepare (a);

        check (b.getOtherSources().empty(), "silent instance not listed before it plays");

        run (a, headA, blockSize * 4, [] (int) { return 0.3f; });
        auto others = b.getOtherSources();
        check (others.size() == 1, "B sees A");
        check (! others.empty() && others[0]->ring.read (0.0, 0.01).max > 0.25f, "B reads A's signal");
        check (a.getOtherSources().empty(), "A does not see B before B plays");

        setParam (a, amvscope::params::send, 0.0f);
        run (a, headA, blockSize, [] (int) { return 0.3f; });
        check (b.getOtherSources().empty(), "Send off hides A");

        setParam (a, amvscope::params::send, 1.0f);
        run (a, headA, blockSize, [] (int) { return 0.3f; });

        juce::AudioProcessor::TrackProperties props;
        props.name = "Kick";
        props.colourARGB = 0xffff0000;
        a.updateTrackProperties (props);
        check (b.getOtherSources().front()->getTrackName() == "Kick", "track name shared");

        // The editor of B lists this track and A.
        run (b, headB, blockSize * 4, [] (int) { return 0.1f; });
        std::unique_ptr<juce::AudioProcessorEditor> ed (b.createEditor());
        auto* editor = dynamic_cast<AMVScopeAudioProcessorEditor*> (ed.get());
        editor->refresh();
        const auto& traces = editor->getTraces();
        check (traces.size() == 2, "editor shows two traces");
        check (traces.size() == 2 && traces[1].label == "Kick", "other trace named after its track");

        bool drawn = false;
        for (const auto& c : traces.back().columns)
            drawn = drawn || (c.filled && c.max > 0.25f);
        check (drawn, "other trace has data on screen");
    }

    check (b.getOtherSources().empty(), "deleted instance disappears");
}

void stateRoundTrip()
{
    AMVScopeAudioProcessor a;
    setParam (a, amvscope::params::length, 4.0f);
    setParam (a, amvscope::params::zoom, 8.0f);

    juce::MemoryBlock data;
    a.getStateInformation (data);

    AMVScopeAudioProcessor b;
    b.setStateInformation (data.getData(), (int) data.getSize());
    check (juce::roundToInt (b.apvts.getRawParameterValue (amvscope::params::length)->load()) == 4,
           "length restored");
    check (std::abs (b.apvts.getRawParameterValue (amvscope::params::zoom)->load() - 8.0f) < 0.01f,
           "zoom restored");
}

void layouts()
{
    AMVScopeAudioProcessor p;
    juce::AudioProcessor::BusesLayout stereo, mono, mismatch, withSidechain;
    withSidechain.inputBuses.add (juce::AudioChannelSet::stereo());
    withSidechain.inputBuses.add (juce::AudioChannelSet::mono());
    withSidechain.outputBuses.add (juce::AudioChannelSet::stereo());
    check (p.isBusesLayoutSupported (withSidechain), "mono sidechain supported");
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::mono());
    mismatch.inputBuses.add (juce::AudioChannelSet::mono());
    mismatch.outputBuses.add (juce::AudioChannelSet::stereo());

    check (p.isBusesLayoutSupported (stereo), "stereo supported");
    check (p.isBusesLayoutSupported (mono), "mono supported");
    check (! p.isBusesLayoutSupported (mismatch), "mono in / stereo out rejected");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    audioPassesThroughUnchanged();
    hostSyncPlacesBeatsInPlace();
    barLengthFollowsTimeSignature();
    triggerWhenStopped();
    triggerModeWhilePlaying();
    freezeStopsRecording();
    sidechainIsRecorded();
    sidechainInTriggerMode();
    triggerFromSidechain();
    editorKeysAndSize();
    phaseSuggestionAlignsTracks();
    instancesSeeEachOther();
    stateRoundTrip();
    layouts();

    if (failures == 0)
        std::printf ("All plugin tests passed.\n");

    return failures == 0 ? 0 : 1;
}
