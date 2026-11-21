// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"

namespace
{
    constexpr double rate = 48000.0;

    std::vector<float> sine(double frequency, double peakDb, double seconds, double phase = 0.0)
    {
        std::vector<float> out((size_t)(seconds * rate));
        const double amplitude = std::pow(10.0, peakDb / 20.0);
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = (float)(amplitude * std::sin(phase + juce::MathConstants<double>::twoPi * frequency * (double)i / rate));
        return out;
    }

    template <typename Engine>
    void feed(Engine& engine, const std::vector<float>& left, const std::vector<float>& right)
    {
        for (size_t start = 0; start < left.size(); start += 512)
            engine.process(left.data() + start, right.data() + start, (int)juce::jmin<size_t>(512, left.size() - start));
    }

    // A host that renders offline, or plays, as the test wants
    struct TestHost : juce::AudioPlayHead
    {
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying(playing);
            info.setTimeInSamples(0);
            info.setTimeInSeconds(0.0);
            info.setBpm(120.0);
            info.setPpqPosition(0.0);
            return info;
        }
    };
}

//==============================================================================
struct VuMeterEngineTests : juce::UnitTest
{
    VuMeterEngineTests() : juce::UnitTest("VuMeterEngine") {}

    void runTest() override
    {
        beginTest("A sine at the calibration level reads 0 VU");
        {
            for (float calibration : { -18.f, -14.f, -20.f })
            {
                VuMeterEngine engine;
                engine.prepare(rate);
                engine.calibrationDb.store(calibration);
                const auto tone = sine(1000.0, (double)calibration, 4.0);
                feed(engine, tone, tone);
                expectWithinAbsoluteError(engine.read().value[0], 0.f, 0.1f);
            }
        }

        beginTest("The VU reaches 99% of its reading in about 300 ms");
        {
            VuMeterEngine engine;
            engine.prepare(rate);
            const auto tone = sine(1000.0, -18.0, 0.3);
            feed(engine, tone, tone);
            const float reading = engine.read().value[0];
            expect(reading > -1.0f && reading < 0.5f, "read " + juce::String(reading));
        }

        beginTest("RMS reads the RMS level of a sine, and 3 dB more with AES17");
        {
            VuMeterEngine engine;
            engine.prepare(rate);
            engine.mode.store(VuMeterEngine::rms);
            const auto tone = sine(1000.0, -18.0, 4.0);
            feed(engine, tone, tone);
            expectWithinAbsoluteError(engine.read().value[0], -3.01f, 0.1f);

            engine.aes17.store(true);
            feed(engine, tone, tone);
            expectWithinAbsoluteError(engine.read().value[0], 0.f, 0.1f);
        }
    }
};

static VuMeterEngineTests vuMeterEngineTests;

//==============================================================================
struct MultibandCorrelatorTests : juce::UnitTest
{
    MultibandCorrelatorTests() : juce::UnitTest("MultibandCorrelator") {}

    void runTest() override
    {
        beginTest("The same signal in both channels reads +1, and its inverse -1, in the band of the tone");
        {
            for (float sign : { 1.f, -1.f })
            {
                MultibandCorrelator correlator;
                correlator.prepare(rate);
                correlator.numBands.store(16);

                auto left = sine(1000.0, -12.0, 3.0);
                auto right = left;
                for (auto& sample : right)
                    sample *= sign;
                feed(correlator, left, right);

                std::array<float, MultibandCorrelator::maxBands> values {};
                const int bands = correlator.read(values);
                expectEquals(bands, 16);

                int nearest = 0;
                for (int band = 1; band < bands; ++band)
                    if (std::abs(std::log(MultibandCorrelator::centreOf(band, bands) / 1000.0)) < std::abs(std::log(MultibandCorrelator::centreOf(nearest, bands) / 1000.0)))
                        nearest = band;

                expectWithinAbsoluteError(values[(size_t)nearest], sign, 0.02f);
            }
        }

        beginTest("A tone a quarter of a cycle apart in the two channels reads 0");
        {
            MultibandCorrelator correlator;
            correlator.prepare(rate);
            correlator.numBands.store(16);
            feed(correlator, sine(1000.0, -12.0, 3.0), sine(1000.0, -12.0, 3.0, juce::MathConstants<double>::halfPi));

            std::array<float, MultibandCorrelator::maxBands> values {};
            const int bands = correlator.read(values);
            int nearest = 0;
            for (int band = 1; band < bands; ++band)
                if (std::abs(std::log(MultibandCorrelator::centreOf(band, bands) / 1000.0)) < std::abs(std::log(MultibandCorrelator::centreOf(nearest, bands) / 1000.0)))
                    nearest = band;
            expectWithinAbsoluteError(values[(size_t)nearest], 0.f, 0.05f);
        }
    }
};

static MultibandCorrelatorTests multibandCorrelatorTests;

//==============================================================================
struct BpmAnalyzerTests : juce::UnitTest
{
    BpmAnalyzerTests() : juce::UnitTest("BpmAnalyzer") {}

    void runTest() override
    {
        beginTest("Finds the tempo of a click track, or its double or half");
        {
            for (double bpm : { 100.0, 124.0, 140.0 })
            {
                // A short burst of a decaying tone on every beat
                std::vector<float> track((size_t)(20.0 * rate), 0.f);
                const double beat = 60.0 / bpm * rate;
                for (double at = 0.0; at < (double)track.size() - 2400.0; at += beat)
                    for (int i = 0; i < 2400; ++i)
                        track[(size_t)at + (size_t)i] += (float)(0.8 * std::exp(-(double)i / 400.0) * std::sin(juce::MathConstants<double>::twoPi * 90.0 * i / rate));

                BpmAnalyzer analyzer;
                analyzer.prepare(rate);
                for (size_t start = 0; start < track.size(); start += 512)
                    analyzer.processBlock(track.data() + start, (int)juce::jmin<size_t>(512, track.size() - start));

                const auto result = analyzer.analyze();
                expect(result.valid);
                const double ratio = result.bpm / bpm;
                expect(std::abs(ratio - 1.0) < 0.005 || std::abs(ratio - 2.0) < 0.01 || std::abs(ratio - 0.5) < 0.0025,
                       "wanted " + juce::String(bpm) + ", found " + juce::String(result.bpm));
            }
        }
    }
};

static BpmAnalyzerTests bpmAnalyzerTests;

//==============================================================================
struct MonitorTests : juce::UnitTest
{
    MonitorTests() : juce::UnitTest("Monitor section") {}

    static void set(UltimateMeterAudioProcessor& processor, const juce::String& id, float value)
    {
        if (auto* parameter = processor.apvts.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }

    // Runs some blocks of a signal that is only in the left channel, and returns the last block
    static juce::AudioBuffer<float> run(UltimateMeterAudioProcessor& processor)
    {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        for (int block = 0; block < 8; ++block)
        {
            for (int i = 0; i < 512; ++i)
            {
                buffer.setSample(0, i, 0.5f);
                buffer.setSample(1, i, 0.f);
            }
            processor.processBlock(buffer, midi);
        }
        return buffer;
    }

    void runTest() override
    {
        beginTest("Mono is heard while playing, and is kept out of an offline render");
        {
            UltimateMeterAudioProcessor processor;
            TestHost host;
            processor.setPlayHead(&host);
            processor.setPlayConfigDetails(2, 2, rate, 512);
            processor.prepareToPlay(rate, 512);
            set(processor, Parameters::ID::monMode, 4.f); // mid

            auto heard = run(processor);
            expectWithinAbsoluteError(heard.getSample(0, 511), 0.25f, 1.0e-4f);
            expectWithinAbsoluteError(heard.getSample(1, 511), 0.25f, 1.0e-4f);

            processor.setNonRealtime(true);
            auto rendered = run(processor);
            expectWithinAbsoluteError(rendered.getSample(0, 511), 0.5f, 1.0e-4f);
            expectWithinAbsoluteError(rendered.getSample(1, 511), 0.f, 1.0e-4f);
            processor.setPlayHead(nullptr);
        }

        beginTest("A preset does not hold the monitor section, and does not change it when it is loaded");
        {
            const auto file = juce::File::createTempFile(".xml");

            UltimateMeterAudioProcessor source;
            set(source, Parameters::ID::monMode, 5.f);
            set(source, Parameters::ID::monMuteL, 1.f);
            set(source, Parameters::ID::spectrumStyle, 2.f);
            expect(Presets::save(source.apvts.copyState(), file));

            UltimateMeterAudioProcessor target;
            expect(Presets::load(target.apvts, file));
            expectEquals(juce::roundToInt(target.apvts.getRawParameterValue(Parameters::ID::spectrumStyle)->load()), 2);
            expectEquals(juce::roundToInt(target.apvts.getRawParameterValue(Parameters::ID::monMode)->load()), 0);
            expectEquals(juce::roundToInt(target.apvts.getRawParameterValue(Parameters::ID::monMuteL)->load()), 0);
            file.deleteFile();
        }

        beginTest("The count of the host's starts rises once for each start");
        {
            UltimateMeterAudioProcessor processor;
            TestHost host;
            host.playing = false;
            processor.setPlayHead(&host);
            processor.setPlayConfigDetails(2, 2, rate, 512);
            processor.prepareToPlay(rate, 512);
            run(processor);
            expectEquals(processor.playStarts.load(), 0);
            host.playing = true;
            run(processor);
            host.playing = false;
            run(processor);
            host.playing = true;
            run(processor);
            expectEquals(processor.playStarts.load(), 2);
            processor.setPlayHead(nullptr);
        }
    }
};

static MonitorTests monitorTests;
