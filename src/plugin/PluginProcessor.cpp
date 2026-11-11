#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Keeps trigger-mode pass numbers apart from each other's start positions.
constexpr std::int64_t triggerPassBase = std::int64_t (1) << 48;

// Sidechain counts as active until it has been silent this long.
constexpr double sidechainHoldSeconds = 2.0;
}

AMVScopeAudioProcessor::AMVScopeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "AMVScope", amvscope::params::createLayout())
{
    lengthParam = apvts.getRawParameterValue (amvscope::params::length);
    syncParam = apvts.getRawParameterValue (amvscope::params::sync);
    thresholdParam = apvts.getRawParameterValue (amvscope::params::threshold);
    freezeParam = apvts.getRawParameterValue (amvscope::params::freeze);
    sendParam = apvts.getRawParameterValue (amvscope::params::send);
    triggerSourceParam = apvts.getRawParameterValue (amvscope::params::triggerSource);

    ownSource = shared->add();
}

AMVScopeAudioProcessor::~AMVScopeAudioProcessor()
{
    shared->remove (ownSource);
}

void AMVScopeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    const auto n = (size_t) juce::jmax (1, samplesPerBlock);
    mono.assign (n, 0.0f);
    sidechainMono.assign (n, 0.0f);
    bands.assign (n, {});
    sidechainBands.assign (n, {});
    mainSplitter.prepare (sampleRate);
    sidechainSplitter.prepare (sampleRate);
    trigger.reset();
    triggerRecording = false;
}

bool AMVScopeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void AMVScopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The scope only looks at the audio; it never changes it.
    const int numSamples = buffer.getNumSamples();
    auto mainIn = getBusBuffer (buffer, true, 0);
    const int numChannels = mainIn.getNumChannels();

    if (numChannels == 0 || numSamples == 0 || mono.size() < (size_t) numSamples)
        return;   // a block bigger than promised is skipped rather than allocating here

    ownSource->sending.store (sendParam->load() >= 0.5f, std::memory_order_relaxed);

    // Host tempo and position.
    bool playing = false;
    double ppq = 0.0;

    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm(); b && *b > 0.0)
                bpm.store (*b, std::memory_order_relaxed);

            if (auto sig = pos->getTimeSignature(); sig && sig->numerator > 0 && sig->denominator > 0)
                quartersPerBar.store (4.0 * sig->numerator / sig->denominator, std::memory_order_relaxed);

            if (auto p = pos->getPpqPosition())
            {
                ppq = *p;
                playing = pos->getIsPlaying();
            }
        }
    }

    const double window = amvscope::params::lengthInQuarters (
        juce::roundToInt (lengthParam->load()), quartersPerBar.load (std::memory_order_relaxed));
    windowQuarters.store (window, std::memory_order_relaxed);

    const double samplesPerQuarter = sampleRate * 60.0 / bpm.load (std::memory_order_relaxed);
    const bool hostMode = juce::roundToInt (syncParam->load()) == (int) amvscope::params::Sync::host;
    const bool useHost = hostMode && playing;
    followingHost.store (useHost, std::memory_order_relaxed);

    if (freezeParam->load() >= 0.5f)
        return;

    // Main input: mono sum and band levels.
    const float* left = mainIn.getReadPointer (0);
    const float* right = mainIn.getReadPointer (numChannels > 1 ? 1 : 0);

    for (int i = 0; i < numSamples; ++i)
    {
        const float m = 0.5f * (left[i] + right[i]);
        mono[(size_t) i] = m;
        bands[(size_t) i] = mainSplitter.process (m);
    }

    // Sidechain input, if the host connected one.
    bool withSidechain = false;

    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled()
        && getBusBuffer (buffer, true, 1).getNumChannels() > 0)
    {
        auto scIn = getBusBuffer (buffer, true, 1);
        const float* scl = scIn.getReadPointer (0);
        const float* scr = scIn.getReadPointer (scIn.getNumChannels() > 1 ? 1 : 0);
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float m = 0.5f * (scl[i] + scr[i]);
            sidechainMono[(size_t) i] = m;
            sidechainBands[(size_t) i] = sidechainSplitter.process (m);
            peak = juce::jmax (peak, std::abs (m));
        }

        if (peak > 1.0e-4f)
            sidechainSilentSamples = 0;
        else
            sidechainSilentSamples += numSamples;

        withSidechain = sidechainSilentSamples < (int) (sidechainHoldSeconds * sampleRate);
    }

    sidechainActive.store (withSidechain, std::memory_order_relaxed);

    // Recording by host position, used by host sync and by other instances.
    if (playing)
    {
        const double step = 1.0 / samplesPerQuarter;
        ownSource->ring.write (mono.data(), bands.data(), (size_t) numSamples, ppq, step);

        if (withSidechain)
            sidechainRing.write (sidechainMono.data(), sidechainBands.data(), (size_t) numSamples, ppq, step);
    }

    if (useHost)
        triggerRecording = false;
    else
        captureTriggered (numSamples, withSidechain, samplesPerQuarter, window);
}

void AMVScopeAudioProcessor::captureTriggered (int numSamples, bool withSidechain,
                                               double samplesPerQuarter, double window)
{
    const double step = 1.0 / samplesPerQuarter;

    auto write = [&] (int from, int n, double position)
    {
        const auto pass = triggerPassBase + triggerCount;
        capture.write (mono.data() + from, (size_t) n, position, step, window, pass, bands.data() + from);

        if (withSidechain)
            sidechainCapture.write (sidechainMono.data() + from, (size_t) n, position, step, window, pass,
                                    sidechainBands.data() + from);
    };

    trigger.setThreshold (juce::Decibels::decibelsToGain (thresholdParam->load()));
    trigger.setHoldoff ((size_t) (window * samplesPerQuarter));

    int start = 0;

    // Finish the recording that is already running.
    if (triggerRecording)
    {
        const int remaining = (int) std::ceil ((window - triggerPosition) / step);
        const int n = juce::jlimit (0, numSamples, remaining);
        write (0, n, triggerPosition);
        triggerPosition += step * n;

        if (triggerPosition >= window - step * 0.5)
            triggerRecording = false;

        start = n;
    }

    // The detector sees the whole block so its holdoff and re-arming stay in step.
    // With the sidechain as source, e.g. the kick starts the picture of the bass.
    const bool fromSidechain = withSidechain
        && juce::roundToInt (triggerSourceParam->load()) == (int) amvscope::params::TriggerSource::sidechain;
    const long hit = trigger.process (fromSidechain ? sidechainMono.data() : mono.data(), (size_t) numSamples);

    if (! triggerRecording && hit >= start)
    {
        ++triggerCount;
        triggerRecording = true;

        const int n = juce::jmin (numSamples - (int) hit, (int) std::ceil (window / step));
        write ((int) hit, n, 0.0);
        triggerPosition = step * n;

        if (triggerPosition >= window - step * 0.5)
            triggerRecording = false;
    }
}

std::vector<std::shared_ptr<amvscope::Source>> AMVScopeAudioProcessor::getOtherSources() const
{
    auto all = shared->list();
    std::vector<std::shared_ptr<amvscope::Source>> others;

    for (auto& s : all)
        if (s != ownSource && s->sending.load (std::memory_order_relaxed) && s->ring.hasData())
            others.push_back (s);

    return others;
}

void AMVScopeAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    ownSource->setTrack (properties.name.value_or (juce::String()),
                         properties.colourARGB ? juce::Colour (*properties.colourARGB)
                                               : juce::Colours::transparentBlack);
}

juce::AudioProcessorEditor* AMVScopeAudioProcessor::createEditor()
{
    return new AMVScopeAudioProcessorEditor (*this);
}

void AMVScopeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", JucePlugin_VersionString, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AMVScopeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto loaded = juce::ValueTree::fromXml (*xml);

        if (loaded.hasType (apvts.state.getType()))
            apvts.replaceState (loaded);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AMVScopeAudioProcessor();
}
