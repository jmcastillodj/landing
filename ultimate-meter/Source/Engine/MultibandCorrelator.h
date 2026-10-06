#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
// An analog-style multi-band correlation meter. Two signals, chosen from the left, the right, the mid and the side
// of the input, are split into bands by band-pass filters, and the correlation of the two is estimated in every band:
// +1 for signals that are in phase, -1 for signals that are out of phase, and 0 for signals with nothing in common.
// The bands are spaced evenly in octaves from 20 Hz to 20 kHz, and the width of each is a share of that spacing.
class MultibandCorrelator
{
public:
    static constexpr int maxBands = 64;
    static constexpr double lowestHz = 20.0;
    static constexpr double highestHz = 20000.0;

    // Settings. Safe to change from any thread.
    std::atomic<int> primary { 0 }, secondary { 1 };       // 0 left, 1 right, 2 mid, 3 side
    std::atomic<int> numBands { 32 };
    std::atomic<float> bandwidthFactor { 1.6f };           // the width of a band, in spacings of bands
    std::atomic<float> averagingMs { 1000.f };

    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        designed = false;
    }

    // Measures one block. Called on the audio thread only.
    void process(const float* left, const float* right, int numSamples)
    {
        const int bands = juce::jlimit(4, maxBands, numBands.load(std::memory_order_relaxed));
        const float factor = bandwidthFactor.load(std::memory_order_relaxed);
        const float averaging = juce::jmax(10.f, averagingMs.load(std::memory_order_relaxed));

        if (!designed || bands != designedBands || !juce::approximatelyEqual(factor, designedFactor))
            design(bands, factor);

        // Correlation by half of the time changes by 80%, as the averaging time is the whole time to settle
        const double tau = (double)averaging * 0.001 * 0.5 / std::log(5.0);
        const double k = 1.0 - std::exp(-1.0 / (tau * sampleRate));

        const int a = primary.load(std::memory_order_relaxed);
        const int b = secondary.load(std::memory_order_relaxed);

        auto pick = [](int source, float l, float r)
        {
            switch (source)
            {
                case 0:  return l;
                case 1:  return r;
                case 2:  return 0.5f * (l + r);
                default: return 0.5f * (l - r);
            }
        };

        for (int i = 0; i < numSamples; ++i)
        {
            const double x = (double)pick(a, left[i], right[i]);
            const double y = (double)pick(b, left[i], right[i]);

            for (int band = 0; band < designedBands; ++band)
            {
                auto& f = filters[(size_t)band];
                const double fx = f.x.process(x, f.c);
                const double fy = f.y.process(y, f.c);

                f.xx += k * (fx * fx - f.xx);
                f.yy += k * (fy * fy - f.yy);
                f.xy += k * (fx * fy - f.xy);
            }
        }

        for (int band = 0; band < designedBands; ++band)
        {
            const auto& f = filters[(size_t)band];
            const double energy = f.xx * f.yy;
            const float value = energy > 1.0e-20 ? (float)std::clamp(f.xy / std::sqrt(energy), -1.0, 1.0) : 0.f;
            correlation[(size_t)band].store(value, std::memory_order_relaxed);
        }

        shownBands.store(designedBands, std::memory_order_relaxed);
        sameSource.store(a == b, std::memory_order_relaxed);
    }

    // The latest readings, on the GUI thread. Returns the number of bands.
    int read(std::array<float, maxBands>& out) const
    {
        const int bands = shownBands.load(std::memory_order_relaxed);
        const bool same = sameSource.load(std::memory_order_relaxed);

        for (int band = 0; band < bands; ++band)
            out[(size_t)band] = same ? 1.f : correlation[(size_t)band].load(std::memory_order_relaxed);

        return bands;
    }

    // The centre of a band, in Hz
    static double centreOf(int band, int bands)
    {
        return lowestHz * std::pow(highestHz / lowestHz, ((double)band + 0.5) / (double)bands);
    }

private:
    // A band-pass filter with a constant peak gain of 0 dB
    struct Biquad
    {
        double b0 = 0, b2 = 0, a1 = 0, a2 = 0;
    };

    struct Section
    {
        double process(double in, const Biquad& c)
        {
            // Transposed direct form II, with b1 = 0 for a band-pass
            const double out = c.b0 * in + z1;
            z1 = z2 - c.a1 * out;
            z2 = c.b2 * in - c.a2 * out;
            return out;
        }

        double z1 = 0.0, z2 = 0.0;
    };

    struct Band
    {
        Biquad c;
        Section x, y;
        double xx = 0.0, yy = 0.0, xy = 0.0;
    };

    void design(int bands, float factor)
    {
        designedBands = bands;
        designedFactor = factor;
        designed = true;

        // The spacing of the bands, in octaves, and the width of a band as a share of it
        const double spacing = std::log2(highestHz / lowestHz) / (double)bands;
        const double width = juce::jmax(0.05, spacing * (double)factor);

        for (int band = 0; band < bands; ++band)
        {
            const double centre = juce::jmin(centreOf(band, bands), 0.45 * sampleRate);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * centre / sampleRate;
            const double alpha = std::sin(w0) * std::sinh(0.5 * std::log(2.0) * width * w0 / std::sin(w0));

            const double a0 = 1.0 + alpha;
            auto& f = filters[(size_t)band];
            f = {};
            f.c.b0 = alpha / a0;
            f.c.b2 = -alpha / a0;
            f.c.a1 = -2.0 * std::cos(w0) / a0;
            f.c.a2 = (1.0 - alpha) / a0;
        }
    }

    double sampleRate = 44100.0;
    bool designed = false;
    int designedBands = 0;
    float designedFactor = 0.f;
    std::array<Band, maxBands> filters;

    std::array<std::atomic<float>, maxBands> correlation {};
    std::atomic<int> shownBands { 0 };
    std::atomic<bool> sameSource { false };
};
