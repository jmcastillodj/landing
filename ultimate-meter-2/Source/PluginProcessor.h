// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.
    This project is built with JUCE version 9.

  ==============================================================================
*/

#pragma once

// Macro used for testing.
#define USE_OSC false

#include <JuceHeader.h>
#include "Parameters.h"
#include "Engine/MeterEngine.h"
#include "Engine/LoudnessMeter.h"
#include "Engine/TruePeakDetector.h"
#include "Engine/SampleRingBuffer.h"
#include "Engine/ReferenceManager.h"
#include "Engine/MultibandCorrelator.h"
#include "Engine/VuMeterEngine.h"
#include "Engine/BpmDetector.h"

using namespace juce;

//==============================================================================
class UltimateMeterAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    UltimateMeterAudioProcessor();
    ~UltimateMeterAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Manages the state of all parameters in the audio processor
    juce::AudioProcessorValueTreeState apvts;

    // Measures peak, RMS and correlation on the audio thread
    MeterEngine meterEngine;

    // Measures loudness to ITU-R BS.1770 and EBU R 128 on the audio thread
    LoudnessMeter loudnessMeter;

    // Measures the true peak on the audio thread
    TruePeakDetector truePeakDetector;

    // Restarts the integrated loudness, the loudness range and the maximum true peak
    void resetLoudness();

    // The position of the host's play head in seconds, or -1 if the host gives none. The editor shows it as a time code.
    std::atomic<double> hostPpqAtBlockEnd { -1.0 }; // the musical position at the end of the latest block, which belongs to hostTotalAtBlockEnd
    std::atomic<juce::uint64> hostTotalAtBlockEnd { 0 }; // the count of samples written to the ring buffer when that block ended
    std::atomic<double> hostPpq { -1.0 }, hostBpm { 120.0 }, hostBeatsPerBar { 4.0 }; // the musical position, tempo and bar of the host
    std::atomic<double> hostTimeSeconds { -1.0 };
    // True while the host is stopped and what it sends is silence
    std::atomic<bool> hostIdle { false };

    // Counts the times the host has started to play, so that the editor can start again what measures one listening
    std::atomic<int> playStarts { 0 };

    // Counts the times the host has put a whole state in, so that an open editor can read its layout again
    std::atomic<int> stateGeneration { 0 };

    // Whether the views of these engines are showing. An engine whose view is hidden is not run.
    std::atomic<bool> correlometerShown { false }, vuShown { false };

    // Reads the reference tracks and the mirror setting that the state holds. Message thread.
    void restoreReferences();

    // Carries every sample to the goniometer and the spectrum analyzer
    SampleRingBuffer sampleRingBuffer;
    ReferenceManager references;
    MultibandCorrelator correlator;
    VuMeterEngine vuEngine;
    BpmDetector bpmDetector;
    std::array<std::atomic<float>*, 5> corrParameters {};
    std::array<std::atomic<float>*, 14> vuParameters {}; // the VU meter's settings, in the order of vuIds in the processor
    std::array<std::atomic<float>*, 5> monitorParameters {}; // mode, mute left, mute right, polarity left, polarity right
    std::array<float, 4> monitorMatrix { 1.f, 0.f, 0.f, 1.f }; // the routing of the output, which is moved to its next place in each block // primary, secondary, bands, averaging time, bandwidth

#if USE_OSC
    // Oscillator for generating test signals
    juce::dsp::Oscillator<float> osc {[](float x) { return std::sin(x); }};

    // Gain control for the oscillator output
    juce::dsp::Gain<float> gain;
#endif

private:
    // Applies the settings of a state saved by version 1
    void applyLegacyState(const Parameters::LegacyState& state);

    // The averager duration, which sets the slow correlation's integration time
    std::atomic<float>* averagerDurationParameter = nullptr;

    bool hostWasPlaying = false; // audio thread

    //==============================================================================
    JUCE_DECLARE_WEAK_REFERENCEABLE (UltimateMeterAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltimateMeterAudioProcessor)
};
