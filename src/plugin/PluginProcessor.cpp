#include "PluginProcessor.h"
#include "PluginEditor.h"

AMVScopeAudioProcessor::AMVScopeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    state.setProperty ("version", JucePlugin_VersionString, nullptr);
}

void AMVScopeAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    scratch.assign ((size_t) juce::jmax (1, samplesPerBlock), {});
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

    if (numChannels == 0 || numSamples == 0)
        return;

    if (scratch.size() < (size_t) numSamples)
        return;   // host sent a bigger block than promised; skip drawing it rather than allocate

    const float* left = buffer.getReadPointer (0);
    const float* right = buffer.getReadPointer (numChannels > 1 ? 1 : 0);

    for (int i = 0; i < numSamples; ++i)
        scratch[(size_t) i] = { left[i], right[i] };

    scopeFifo.push (scratch.data(), (size_t) numSamples);
}

juce::AudioProcessorEditor* AMVScopeAudioProcessor::createEditor()
{
    return new AMVScopeAudioProcessorEditor (*this);
}

void AMVScopeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AMVScopeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto loaded = juce::ValueTree::fromXml (*xml);

        if (loaded.hasType (state.getType()))
            state.copyPropertiesAndChildrenFrom (loaded, nullptr);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AMVScopeAudioProcessor();
}
