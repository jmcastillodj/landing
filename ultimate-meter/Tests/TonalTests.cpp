#include <JuceHeader.h>
#include "../Source/Engine/TonalTargets.h"

//==============================================================================
struct TonalTargetTests : juce::UnitTest
{
    TonalTargetTests() : juce::UnitTest("TonalTargets") {}

    // The value of a target's centre at a frequency
    static float at(const std::vector<float>& curve, double frequency)
    {
        const double proportion = std::log(frequency / TonalTargets::minFrequency) / std::log(TonalTargets::maxFrequency / TonalTargets::minFrequency);
        return curve[(size_t) juce::jlimit(0, TonalTargets::numPoints - 1, juce::roundToInt(proportion * (TonalTargets::numPoints - 1)))];
    }

    void runTest() override
    {
        beginTest("The analysis of white noise rises at the tilt of the display");
        {
            // White noise has the same power at every frequency, so in a display that adds 4.5 dB per octave it rises
            // by that much: from 100 Hz to 10 kHz is 6.64 octaves
            juce::Random random(7);
            juce::AudioBuffer<float> noise(2, 48000 * 6);
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < noise.getNumSamples(); ++i)
                    noise.setSample(channel, i, 0.25f * (random.nextFloat() * 2.f - 1.f));

            const auto target = TonalTargets::analyse("noise", noise, 48000.0);
            const float rise = at(target.centre, 10000.0) - at(target.centre, 100.0);
            expectWithinAbsoluteError(rise, 4.5f * (float) std::log2(100.0), 2.5f);
            expect(! target.builtIn);
        }

        beginTest("A target has the average over 100 Hz to 10 kHz taken out");
        for (auto& target : TonalTargets::builtIn())
        {
            double sum = 0.0;
            int count = 0;
            for (int point = 0; point < TonalTargets::numPoints; ++point)
            {
                const double frequency = TonalTargets::frequencyOf(point);
                if (frequency >= 100.0 && frequency <= 10000.0)
                {
                    sum += (double) target.centre[(size_t) point];
                    ++count;
                }
            }

            expectWithinAbsoluteError((float) (sum / count), 0.f, 0.01f);
            expectEquals((int) target.tolerance.size(), TonalTargets::numPoints);
        }

        beginTest("The pink noise target rises by 1.5 dB per octave");
        {
            for (auto& target : TonalTargets::builtIn())
                if (target.name == "Pink noise")
                    expectWithinAbsoluteError(at(target.centre, 10000.0) - at(target.centre, 100.0), 1.5f * (float) std::log2(100.0), 0.5f);
        }

        beginTest("A target kept as text comes back the same");
        {
            auto original = TonalTargets::builtIn().front();
            original.name = "My | target";
            original.builtIn = false;

            const auto parsed = TonalTargets::parse(TonalTargets::serialise(original));
            expect(parsed.has_value());

            if (parsed.has_value())
            {
                expect(parsed->name == "My   target");
                for (int point = 0; point < TonalTargets::numPoints; ++point)
                    expectWithinAbsoluteError(parsed->centre[(size_t) point], original.centre[(size_t) point], 0.01f);
            }

            expect(! TonalTargets::parse("broken|1,2,3|4,5,6").has_value());
        }
    }
};

static TonalTargetTests tonalTargetTests;
