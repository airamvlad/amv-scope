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
    instancesSeeEachOther();
    stateRoundTrip();
    layouts();

    if (failures == 0)
        std::printf ("All plugin tests passed.\n");

    return failures == 0 ? 0 : 1;
}
