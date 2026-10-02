#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>
#include "SpectrumEngine.h"

//==============================================================================
// Targets for the tonal balance: the shape that the long-term spectrum of a mix is meant to have, as a
// curve with a band around it that says how far a mix may stray from it. A target is stored as numPoints
// values along a logarithmic frequency axis, in decibels with the average over 100 Hz to 10 kHz taken
// out, so it describes the shape and not the level. The shape is that of the plugin's spectrum with a
// tilt of 4.5 dB per octave, as music is flat in it.
//
// The built-in targets are approximate, typical shapes of finished music of each kind. They are not
// measurements of any product or catalogue. A target of one's own is measured from a recording.
namespace TonalTargets
{
    inline constexpr int numPoints = 128;
    inline constexpr double minFrequency = 20.0;
    inline constexpr double maxFrequency = 20000.0;
    inline constexpr float displayTiltDbPerOctave = 4.5f;

    struct Target
    {
        juce::String name;
        std::vector<float> centre, tolerance; // numPoints each, in decibels
        bool builtIn = true;
    };

    inline double frequencyOf(int point)
    {
        return minFrequency * std::pow(maxFrequency / minFrequency, (double)point / (double)(numPoints - 1));
    }

    // The layout that targets are measured and shown in
    inline SpectrumEngine::Display display(float smoothingOctaves)
    {
        SpectrumEngine::Display d;
        d.numPoints = numPoints;
        d.minFrequency = minFrequency;
        d.maxFrequency = maxFrequency;
        d.tiltDbPerOctave = displayTiltDbPerOctave;
        d.smoothingOctaves = smoothingOctaves;
        return d;
    }

    // Takes the average over 100 Hz to 10 kHz out of a curve
    inline void removeAverage(std::vector<float>& curve)
    {
        double sum = 0.0;
        int count = 0;
        for (int point = 0; point < (int)curve.size(); ++point)
        {
            const double frequency = frequencyOf(point);
            if (frequency >= 100.0 && frequency <= 10000.0)
            {
                sum += (double)curve[(size_t)point];
                ++count;
            }
        }

        const float average = count > 0 ? (float)(sum / (double)count) : 0.f;
        for (auto& value : curve)
            value -= average;
    }

    // A curve through points of (frequency, decibels), straight between them on a logarithmic frequency axis
    inline std::vector<float> throughPoints(const std::vector<std::pair<double, float>>& anchors)
    {
        std::vector<float> curve((size_t)numPoints);
        for (int point = 0; point < numPoints; ++point)
        {
            const double frequency = frequencyOf(point);
            size_t upper = 0;
            while (upper < anchors.size() - 1 && anchors[upper].first < frequency)
                ++upper;

            const size_t lower = upper > 0 ? upper - 1 : 0;
            const double span = std::log(anchors[upper].first / anchors[lower].first);
            const double along = span > 0.0 ? juce::jlimit(0.0, 1.0, std::log(frequency / anchors[lower].first) / span) : 0.0;
            curve[(size_t)point] = anchors[lower].second + (float)along * (anchors[upper].second - anchors[lower].second);
        }
        return curve;
    }

    inline std::vector<Target> builtIn()
    {
        // How far a mix may stray: little in the middle, more at the ends of the spectrum, where mixes differ most
        const auto tolerance = throughPoints({ { 20, 8.f }, { 40, 6.f }, { 80, 4.5f }, { 200, 3.5f }, { 500, 2.5f }, { 2000, 2.5f },
                                               { 4000, 3.f }, { 8000, 4.f }, { 12000, 6.f }, { 20000, 9.f } });

        struct Shape { const char* name; std::vector<std::pair<double, float>> anchors; };
        const std::vector<Shape> shapes {
            { "Modern", { { 20, -18 }, { 30, -10 }, { 40, -4 }, { 60, 2 }, { 100, 3 }, { 200, 1 }, { 400, 0 }, { 1000, 0 }, { 2000, -0.5f },
                          { 4000, -1.5f }, { 6000, -3 }, { 8000, -4.5f }, { 10000, -6.5f }, { 12000, -9 }, { 16000, -16 }, { 20000, -26 } } },
            { "Pop / Rock", { { 20, -20 }, { 40, -6 }, { 60, 1 }, { 100, 2 }, { 200, 1 }, { 500, 0 }, { 1000, 0 }, { 2000, -1 }, { 4000, -2.5f },
                              { 8000, -6 }, { 12000, -11 }, { 16000, -18 }, { 20000, -28 } } },
            { "Hip-Hop / Trap", { { 20, -6 }, { 30, 0 }, { 40, 5 }, { 60, 7 }, { 100, 4 }, { 200, 0 }, { 500, -1 }, { 1000, -1 }, { 2000, -2 },
                                  { 4000, -3.5f }, { 8000, -7 }, { 12000, -12 }, { 16000, -19 }, { 20000, -28 } } },
            { "Electronic", { { 20, -10 }, { 40, 2 }, { 60, 5 }, { 100, 3 }, { 200, 0 }, { 500, -1 }, { 1000, 0 }, { 2000, 0 }, { 4000, -1 },
                              { 8000, -3.5f }, { 12000, -7 }, { 16000, -13 }, { 20000, -22 } } },
            { "Acoustic / Jazz", { { 20, -24 }, { 40, -10 }, { 80, -1 }, { 150, 1 }, { 300, 1 }, { 600, 0.5f }, { 1000, 0 }, { 2000, -1.5f },
                                   { 4000, -3.5f }, { 8000, -8 }, { 12000, -14 }, { 16000, -22 }, { 20000, -32 } } },
            // Pink noise has the same power in every octave, which is 3 dB per octave below a flat spectrum, so in
            // a display that adds 4.5 dB per octave it rises by 1.5 dB per octave
            { "Pink noise", { { 20, -8.5f }, { 1000, 0 }, { 20000, 6.5f } } },
            { "Flat (white noise)", { { 20, -25.4f }, { 1000, 0 }, { 20000, 19.4f } } },
        };

        std::vector<Target> targets;
        for (auto& shape : shapes)
        {
            Target target;
            target.name = shape.name;
            target.centre = throughPoints(shape.anchors);
            removeAverage(target.centre);
            target.tolerance = tolerance;
            targets.push_back(std::move(target));
        }
        return targets;
    }

    // Measures a target from stereo audio: the average spectrum of the parts that are not quiet, with the band
    // around it as wide as the spectrum varies from one moment to the next
    inline Target analyse(const juce::String& name, const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        constexpr int order = 12, size = 1 << order, hop = size / 2;

        SpectrumEngine engine;
        engine.setOrder(order);
        const auto layout = display(1.f / 6.f);

        const int numSamples = audio.getNumSamples();
        const float* left = audio.getReadPointer(0);
        const float* right = audio.getReadPointer(audio.getNumChannels() > 1 ? 1 : 0);

        struct Frame { std::vector<float> decibels; float level; };
        std::vector<Frame> frames;
        std::vector<float> rendered;

        for (int start = 0; start + size <= numSamples; start += hop)
        {
            // An elapsed time of ten seconds leaves nothing of the previous frame in the average
            engine.analyze(left + start, right + start, 10.f);
            engine.render(SpectrumEngine::Curve::mid, layout, sampleRate, rendered);

            double energy = 0.0;
            for (int n = 0; n < size; ++n)
                energy += 0.5 * ((double)left[start + n] * left[start + n] + (double)right[start + n] * right[start + n]);

            frames.push_back({ rendered, (float)(10.0 * std::log10(energy / (double)size + 1.0e-12)) });
        }

        Target target;
        target.name = name;
        target.builtIn = false;
        target.centre.assign((size_t)numPoints, 0.f);
        target.tolerance.assign((size_t)numPoints, 3.f);

        if (frames.empty())
            return target;

        // Quiet passages say nothing of the balance, so only the frames within 35 dB of the loudest are used
        float loudest = -200.f;
        for (auto& frame : frames)
            loudest = juce::jmax(loudest, frame.level);

        std::vector<const Frame*> used;
        for (auto& frame : frames)
            if (frame.level >= loudest - 35.f)
                used.push_back(&frame);

        const double count = (double)used.size();
        std::vector<float> deviation((size_t)numPoints, 3.f);

        for (int point = 0; point < numPoints; ++point)
        {
            double power = 0.0, sum = 0.0, sumSquares = 0.0;
            for (auto* frame : used)
            {
                const double db = (double)juce::jmax(frame->decibels[(size_t)point], -120.f);
                power += std::pow(10.0, db / 10.0);
                sum += db;
                sumSquares += db * db;
            }

            target.centre[(size_t)point] = (float)(10.0 * std::log10(power / count + 1.0e-15));
            const double mean = sum / count;
            deviation[(size_t)point] = (float)std::sqrt(juce::jmax(0.0, sumSquares / count - mean * mean));
        }

        removeAverage(target.centre);

        // The band is a little narrower than the spread, kept between limits and smoothed
        for (int pass = 0; pass < 3; ++pass)
        {
            auto smoothed = deviation;
            for (int point = 1; point < numPoints - 1; ++point)
                smoothed[(size_t)point] = 0.25f * deviation[(size_t)point - 1] + 0.5f * deviation[(size_t)point] + 0.25f * deviation[(size_t)point + 1];
            deviation = smoothed;
        }

        for (int point = 0; point < numPoints; ++point)
            target.tolerance[(size_t)point] = juce::jlimit(2.f, 6.5f, 0.9f * deviation[(size_t)point]);

        return target;
    }

    // A target of one's own as text, to be kept with the session: name|centre values|tolerance values
    inline juce::String serialise(const Target& target)
    {
        auto join = [](const std::vector<float>& values)
        {
            juce::StringArray parts;
            for (float value : values)
                parts.add(juce::String(value, 2));
            return parts.joinIntoString(",");
        };

        return target.name.replaceCharacters("|\n", "  ") + "|" + join(target.centre) + "|" + join(target.tolerance);
    }

    inline std::optional<Target> parse(const juce::String& text)
    {
        const auto parts = juce::StringArray::fromTokens(text, "|", "");
        if (parts.size() != 3)
            return std::nullopt;

        auto split = [](const juce::String& list)
        {
            std::vector<float> values;
            for (auto& item : juce::StringArray::fromTokens(list, ",", ""))
                values.push_back(item.getFloatValue());
            return values;
        };

        Target target;
        target.name = parts[0];
        target.builtIn = false;
        target.centre = split(parts[1]);
        target.tolerance = split(parts[2]);

        if (target.centre.size() != (size_t)numPoints || target.tolerance.size() != (size_t)numPoints)
            return std::nullopt;

        return target;
    }
}
