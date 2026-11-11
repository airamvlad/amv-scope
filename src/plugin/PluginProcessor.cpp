#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// Keeps trigger-mode pass numbers apart from host-mode ones (floor(ppq / window)).
constexpr std::int64_t triggerPassBase = std::int64_t (1) << 48;
}

AMVScopeAudioProcessor::AMVScopeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AMVScope", amvscope::params::createLayout())
{
    lengthParam = apvts.getRawParameterValue (amvscope::params::length);
    syncParam = apvts.getRawParameterValue (amvscope::params::sync);
    thresholdParam = apvts.getRawParameterValue (amvscope::params::threshold);
    freezeParam = apvts.getRawParameterValue (amvscope::params::freeze);
}

void AMVScopeAudioProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    mono.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    trigger.reset();
    triggerRecording = false;
}

bool AMVScopeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AMVScopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The scope only looks at the audio; it never changes it.
    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumInputChannels();

    if (numChannels == 0 || numSamples == 0 || mono.size() < (size_t) numSamples)
        return;   // a block bigger than promised is skipped rather than allocating here

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

    const float* left = buffer.getReadPointer (0);
    const float* right = buffer.getReadPointer (numChannels > 1 ? 1 : 0);

    for (int i = 0; i < numSamples; ++i)
        mono[(size_t) i] = 0.5f * (left[i] + right[i]);

    if (useHost)
    {
        triggerRecording = false;
        capture.write (mono.data(), (size_t) numSamples, ppq, 1.0 / samplesPerQuarter, window);
    }
    else
    {
        captureTriggered (mono.data(), numSamples, samplesPerQuarter, window);
    }
}

void AMVScopeAudioProcessor::captureTriggered (const float* samples, int numSamples,
                                               double samplesPerQuarter, double window)
{
    const double step = 1.0 / samplesPerQuarter;

    trigger.setThreshold (juce::Decibels::decibelsToGain (thresholdParam->load()));
    trigger.setHoldoff ((size_t) (window * samplesPerQuarter));

    int start = 0;

    // Finish the recording that is already running.
    if (triggerRecording)
    {
        const int remaining = (int) std::ceil ((window - triggerPosition) / step);
        const int n = juce::jlimit (0, numSamples, remaining);
        capture.write (samples, (size_t) n, triggerPosition, step, window, triggerPassBase + triggerCount);
        triggerPosition += step * n;

        if (triggerPosition >= window - step * 0.5)
            triggerRecording = false;

        start = n;
    }

    // The detector sees the whole block so its holdoff and re-arming stay in step.
    const long hit = trigger.process (samples, (size_t) numSamples);

    if (! triggerRecording && hit >= start)
    {
        ++triggerCount;
        triggerRecording = true;
        triggerPosition = 0.0;

        const int n = juce::jmin (numSamples - (int) hit, (int) std::ceil (window / step));
        capture.write (samples + hit, (size_t) n, 0.0, step, window, triggerPassBase + triggerCount);
        triggerPosition = step * n;

        if (triggerPosition >= window - step * 0.5)
            triggerRecording = false;
    }
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
