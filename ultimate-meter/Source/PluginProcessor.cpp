/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.
    This project is built with JUCE version 9.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
UltimateMeterAudioProcessor::UltimateMeterAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
    apvts(
        *this,
        nullptr,
        "Parameters",
        Parameters::createLayout())
{
    averagerDurationParameter = apvts.getRawParameterValue(Parameters::ID::averagerDuration);
}

UltimateMeterAudioProcessor::~UltimateMeterAudioProcessor()
{
}

//==============================================================================
const juce::String UltimateMeterAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool UltimateMeterAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool UltimateMeterAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool UltimateMeterAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double UltimateMeterAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int UltimateMeterAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs
}

int UltimateMeterAudioProcessor::getCurrentProgram()
{
    return 0;
}

void UltimateMeterAudioProcessor::setCurrentProgram (int)
{
}

const juce::String UltimateMeterAudioProcessor::getProgramName (int)
{
    return {};
}

void UltimateMeterAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void UltimateMeterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback initialization
    juce::ignoreUnused(samplesPerBlock);

    meterEngine.prepare(sampleRate);
    loudnessMeter.prepare(sampleRate);
    truePeakDetector.prepare(sampleRate);
    sampleRingBuffer.reset();

    #if USE_OSC
        juce::dsp::ProcessSpec spec;
        spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
        spec.sampleRate = sampleRate;
        spec.numChannels = getTotalNumOutputChannels();
        
        osc.prepare(spec);
        gain.prepare(spec);
    #endif
}

void UltimateMeterAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool UltimateMeterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported
    // In this template code we only support mono or stereo
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void UltimateMeterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any output channels that do not contain input data
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    
    #if USE_OSC
    buffer.clear();
    juce::dsp::AudioBlock<float> audioBlock { buffer };
    
    osc.setFrequency(440.0f);
    //gain.setGainDecibels(6.f);
    gain.setGainDecibels(JUCE_LIVE_CONSTANT(6.f));
    
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float nextOscillatorSample = osc.processSample(0.f);
        audioBlock.setSample(0, sample, nextOscillatorSample);
        audioBlock.setSample(1, sample, nextOscillatorSample);
    }
    
    gain.process(juce::dsp::ProcessContextReplacing<float>(audioBlock));
    #endif
    
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    {
        double seconds = -1.0;
        if (auto* playHead = getPlayHead())
            if (auto position = playHead->getPosition())
                if (auto time = position->getTimeInSeconds())
                    seconds = *time;

        hostTimeSeconds.store(seconds, std::memory_order_relaxed);
    }

    if (numChannels > 0 && numSamples > 0)
    {
        // The meters always analyze a stereo signal, a mono input feeds both sides
        const float* left = buffer.getReadPointer(0);
        const float* right = buffer.getReadPointer(juce::jmin(1, numChannels - 1));

        const int averagerIndex = juce::roundToInt(averagerDurationParameter->load(std::memory_order_relaxed));
        meterEngine.setSlowCorrelationSeconds(Parameters::valueAt(Parameters::averagerDurationsSeconds, averagerIndex));

        // Every sample is measured here, the editor only reads the results
        meterEngine.process(left, right, numSamples);
        loudnessMeter.process(left, right, numSamples);
        truePeakDetector.process(left, right, numSamples);
        sampleRingBuffer.write(left, right, numSamples);
    }

#if USE_OSC
    // Clear the audio buffer if oscillator synthesis is used
    buffer.clear();
#endif
}

void UltimateMeterAudioProcessor::resetLoudness()
{
    // The meters restart themselves at the start of the next block
    loudnessMeter.requestReset();
    truePeakDetector.requestReset();
}

//==============================================================================
bool UltimateMeterAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* UltimateMeterAudioProcessor::createEditor()
{
    return new UltimateMeterAudioProcessorEditor (*this);
}

//==============================================================================
void UltimateMeterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // The state is the parameter tree as XML, tagged with a version number so
    // that later versions can tell how to read it
    auto state = apvts.copyState();
    state.setProperty(Parameters::stateVersionProperty, Parameters::currentStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void UltimateMeterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));

        return;
    }

    // Sessions saved with version 1 hold a raw binary stream instead
    Parameters::LegacyState legacyState;
    if (Parameters::readLegacyState(data, sizeInBytes, legacyState))
        applyLegacyState(legacyState);
}

void UltimateMeterAudioProcessor::applyLegacyState(const Parameters::LegacyState& state)
{
    auto set = [this](const juce::String& parameterID, float value)
    {
        if (auto* parameter = apvts.getParameter(parameterID))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };

    set(Parameters::ID::goniometerScale, state.goniometerScale);
    set(Parameters::ID::decayRate, static_cast<float>(state.decayRate));
    set(Parameters::ID::holdTime, static_cast<float>(state.holdTime));
    set(Parameters::ID::averagerDuration, static_cast<float>(state.averagerDuration));
    set(Parameters::ID::meterView, static_cast<float>(state.meterView));
    set(Parameters::ID::showTick, state.showTick ? 1.f : 0.f);
}

//==============================================================================
// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UltimateMeterAudioProcessor();
}
