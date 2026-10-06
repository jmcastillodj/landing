/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.
    This project is built with JUCE version 9.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "Presets.h"
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

    {
        using namespace Parameters;
        const juce::String ids[] { ID::corrPrimary, ID::corrSecondary, ID::corrBands, ID::corrAvgTime, ID::corrBandwidth };
        for (size_t i = 0; i < corrParameters.size(); ++i)
            corrParameters[i] = apvts.getRawParameterValue(ids[i]);

        const juce::String vuIds[] { ID::vuMode, ID::vuBallistics, ID::vuOvershoot, ID::vuSpeed, ID::vuRmsWindow, ID::vuAes17, ID::vuWeighting,
                                     ID::vuCalibration, ID::vuClipLevel, ID::vuHold, ID::vuNumbers, ID::vuDisplay, ID::vuTrimL, ID::vuTrimR };
        for (size_t i = 0; i < vuParameters.size(); ++i)
            vuParameters[i] = apvts.getRawParameterValue(vuIds[i]);

        const juce::String monitorIds[] { ID::monMode, ID::monMuteL, ID::monMuteR, ID::monPolL, ID::monPolR };
        for (size_t i = 0; i < monitorParameters.size(); ++i)
            monitorParameters[i] = apvts.getRawParameterValue(monitorIds[i]);
    }

    // A new instance opens with the settings that were saved as the default, if any. A session that is
    // being restored overwrites them afterwards, as the host sets its own state.
    if (Presets::defaultFile().existsAsFile())
        Presets::load(apvts, Presets::defaultFile());
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

    references.hostRate.store(sampleRate);
    correlator.prepare(sampleRate);
    vuEngine.prepare(sampleRate);
    monitorMatrix = { 1.f, 0.f, 0.f, 1.f };
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

    juce::int64 hostSample = -1;
    bool hostPlaying = false;
    double blockPpq = -1.0, blockBpm = 120.0;
    {
        double seconds = -1.0;
        if (auto* playHead = getPlayHead())
            if (auto position = playHead->getPosition())
            {
                if (auto time = position->getTimeInSeconds())
                    seconds = *time;
                if (auto samples = position->getTimeInSamples())
                    hostSample = *samples;
                hostPlaying = position->getIsPlaying();

                blockPpq = position->getPpqPosition().orFallback(-1.0);
                hostPpq.store(blockPpq, std::memory_order_relaxed);
                if (auto bpm = position->getBpm())
                {
                    blockBpm = *bpm;
                    hostBpm.store(*bpm, std::memory_order_relaxed);
                }
                if (auto signature = position->getTimeSignature())
                    hostBeatsPerBar.store((double)signature->numerator * 4.0 / (double)juce::jmax(1, signature->denominator), std::memory_order_relaxed);
            }

        hostTimeSeconds.store(seconds, std::memory_order_relaxed);
    }

    // The monitor section comes first, before every measurement, so that the meters, the spectrum and the rest all read
    // what is being monitored: only the side, or the mono sum, or one channel, as it is chosen
    if (numChannels >= 2 && numSamples > 0)
    {
        const int mode = juce::roundToInt(monitorParameters[0]->load(std::memory_order_relaxed));
        const float muteL = monitorParameters[1]->load(std::memory_order_relaxed) > 0.5f ? 0.f : 1.f;
        const float muteR = monitorParameters[2]->load(std::memory_order_relaxed) > 0.5f ? 0.f : 1.f;
        const float polL = monitorParameters[3]->load(std::memory_order_relaxed) > 0.5f ? -1.f : 1.f;
        const float polR = monitorParameters[4]->load(std::memory_order_relaxed) > 0.5f ? -1.f : 1.f;

        // The routing, from the input (left, right) to the output (left, right), as { LL, LR, RL, RR }
        std::array<float, 4> route { 1.f, 0.f, 0.f, 1.f };
        switch (mode)
        {
            case 1: route = { 0.f, 1.f, 1.f, 0.f }; break;      // swapped
            case 2: route = { 1.f, 0.f, 1.f, 0.f }; break;      // left in both
            case 3: route = { 0.f, 1.f, 0.f, 1.f }; break;      // right in both
            case 4: route = { 0.5f, 0.5f, 0.5f, 0.5f }; break;  // mid
            case 5: route = { 0.5f, -0.5f, 0.5f, -0.5f }; break; // side
            default: break;
        }

        const std::array<float, 4> target { route[0] * polL * muteL, route[1] * polR * muteL, route[2] * polL * muteR, route[3] * polR * muteR };

        const bool unchanged = juce::approximatelyEqual(target[0], 1.f) && juce::approximatelyEqual(target[3], 1.f)
                               && juce::approximatelyEqual(target[1], 0.f) && juce::approximatelyEqual(target[2], 0.f)
                               && juce::approximatelyEqual(monitorMatrix[0], 1.f) && juce::approximatelyEqual(monitorMatrix[3], 1.f)
                               && juce::approximatelyEqual(monitorMatrix[1], 0.f) && juce::approximatelyEqual(monitorMatrix[2], 0.f);

        if (!unchanged)
        {
            auto* l = buffer.getWritePointer(0);
            auto* r = buffer.getWritePointer(1);
            std::array<float, 4> step;
            for (size_t i = 0; i < 4; ++i)
                step[i] = (target[i] - monitorMatrix[i]) / (float)numSamples;

            for (int i = 0; i < numSamples; ++i)
            {
                const float inL = l[i], inR = r[i];
                l[i] = monitorMatrix[0] * inL + monitorMatrix[1] * inR;
                r[i] = monitorMatrix[2] * inL + monitorMatrix[3] * inR;
                for (size_t k = 0; k < 4; ++k)
                    monitorMatrix[k] += step[k];
            }

            monitorMatrix = target;
        }
    }

    if (numChannels > 0 && numSamples > 0)
    {
        // The meters always analyze a stereo signal, a mono input feeds both sides
        const float* left = buffer.getReadPointer(0);
        const float* right = buffer.getReadPointer(juce::jmin(1, numChannels - 1));

        const int averagerIndex = juce::roundToInt(averagerDurationParameter->load(std::memory_order_relaxed));
        meterEngine.setSlowCorrelationSeconds(Parameters::valueAt(Parameters::averagerDurationsSeconds, averagerIndex));

        // The correlometer takes its settings from the parameters
        {
            using namespace Parameters;
            auto read = [this](size_t i) { return corrParameters[i]->load(std::memory_order_relaxed); };
            correlator.primary.store(juce::roundToInt(read(0)), std::memory_order_relaxed);
            correlator.secondary.store(juce::roundToInt(read(1)), std::memory_order_relaxed);
            correlator.numBands.store(juce::roundToInt(read(2)), std::memory_order_relaxed);
            correlator.averagingMs.store(read(3), std::memory_order_relaxed);
            correlator.bandwidthFactor.store(valueAt(corrBandwidthFactors, juce::roundToInt(read(4))), std::memory_order_relaxed);
        }
        correlator.process(left, right, numSamples);

        // The VU meter: indices 0 mode, 1 detector, 2 overshoot, 3 speed, 4 window, 5 AES17, 6 weighting, 7 calibration, 11 display, 12-13 trims
        {
            using namespace Parameters;
            auto read = [this](size_t i) { return vuParameters[i]->load(std::memory_order_relaxed); };
            vuEngine.mode.store(juce::roundToInt(read(0)), std::memory_order_relaxed);
            vuEngine.ballistics.store(juce::roundToInt(read(1)), std::memory_order_relaxed);
            vuEngine.overshootPercent.store(read(2), std::memory_order_relaxed);
            vuEngine.speed.store(read(3), std::memory_order_relaxed);
            vuEngine.rmsWindowMs.store(read(4), std::memory_order_relaxed);
            vuEngine.aes17.store(read(5) > 0.5f, std::memory_order_relaxed);
            vuEngine.weighting.store(juce::roundToInt(read(6)), std::memory_order_relaxed);
            vuEngine.calibrationDb.store(valueAt(vuCalibrationsDb, juce::roundToInt(read(7))), std::memory_order_relaxed);
            vuEngine.display.store(juce::roundToInt(read(11)), std::memory_order_relaxed);
            vuEngine.trimLeftDb.store(read(12), std::memory_order_relaxed);
            vuEngine.trimRightDb.store(read(13), std::memory_order_relaxed);
        }
        vuEngine.process(left, right, numSamples);

        // Every sample is measured here, the editor only reads the results
        meterEngine.process(left, right, numSamples);
        loudnessMeter.process(left, right, numSamples);
        truePeakDetector.process(left, right, numSamples);
        sampleRingBuffer.write(left, right, numSamples);

        // Where in the music the samples that have just been written end, so that the waveform can put them on the grid
        hostPpqAtBlockEnd.store(blockPpq >= 0.0 ? blockPpq + (double)numSamples * blockBpm / (60.0 * getSampleRate()) : -1.0, std::memory_order_relaxed);
        hostTotalAtBlockEnd.store(sampleRingBuffer.getTotalWritten(), std::memory_order_relaxed);
    }

    // A reference track can be heard in place of the mix. The meters have measured the mix before this.
    references.process(buffer, getSampleRate(), hostSample, hostPlaying, monitorMatrix);



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
