// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../Engine/SpectrumEngine.h"

//==============================================================================
// Runs the SpectrumEngine on the newest audio, once per frame. The analyzer and
// the spectrogram both draw from it, so the FFTs are taken once for both.
class SpectrumSource
{
public:
    explicit SpectrumSource(UltimateMeterAudioProcessor& p) : audioProcessor(p) {}

    // Analyzes the audio that has arrived since the last call, with FFTs of 2^order samples that overlap by three
    // quarters, so that every sample is measured and counts the same, as in a real-time analyzer. Returns false, and
    // leaves the spectra as they were, if no block was due.
    bool update(float, int order)
    {
        if (order != engine.getOrder())
        {
            engine.setOrder(order);
            lastAnalysed = 0;
        }

        auto& ringBuffer = audioProcessor.sampleRingBuffer;
        const auto totalWritten = ringBuffer.getTotalWritten();
        const auto size = (juce::uint64)engine.getSize();
        const auto hop = size / 4;

        // The buffer starts again when the host prepares the plugin, and after a long pause of the window what was
        // missed is let go, since the buffer no longer holds it
        if (totalWritten < lastAnalysed || totalWritten - lastAnalysed > maxBlocksPerUpdate * hop)
            lastAnalysed = totalWritten > hop ? totalWritten - hop : 0;

        if (totalWritten < size)
            return false;

        analysisBuffer.setSize(2, engine.getSize(), false, false, true);
        const float hopSeconds = (float)((double)hop / getSampleRate());
        bool analysed = false;

        while (lastAnalysed + hop <= totalWritten)
        {
            const auto end = lastAnalysed + hop;
            lastAnalysed = end;

            if (end < size || !ringBuffer.readEndingAt(analysisBuffer.getWritePointer(0), analysisBuffer.getWritePointer(1), engine.getSize(), end))
                continue;

            engine.analyze(analysisBuffer.getReadPointer(0), analysisBuffer.getReadPointer(1), hopSeconds);
            analysed = true;
        }

        return analysed;
    }

    SpectrumEngine& getEngine() { return engine; }

    double getSampleRate() const
    {
        const double sampleRate = audioProcessor.getSampleRate();
        return sampleRate > 0.0 ? sampleRate : 44100.0;
    }

private:
    UltimateMeterAudioProcessor& audioProcessor;
    SpectrumEngine engine;

    // The most recent samples of both channels, which the FFTs analyze
    juce::AudioBuffer<float> analysisBuffer;

    // The count of samples at the end of the last block that was analyzed
    juce::uint64 lastAnalysed = 0;
    static constexpr juce::uint64 maxBlocksPerUpdate = 12;
};
