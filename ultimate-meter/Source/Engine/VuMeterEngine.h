#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include "LoudnessMeter.h"

//==============================================================================
// The detectors of the VU meter, which run on the audio thread over every sample. Two meters are measured (left and
// right, mid and side, or one of the sum), each by the detector that is chosen:
//  - VU:    the ballistics of a classic VU meter, a second order system that reaches full reading in 300 ms and
//           overshoots a little. The overshoot and the speed can be changed, and so can what is detected: the
//           average of the rectified signal, its mean square, or the two together.
//  - RMS:   the mean square over a window that can be set, with the choice of 3 dB more for sines that read 0 dB, and
//           of a frequency weighting.
//  - PPM:   quasi-peak detectors that rise in a few milliseconds and fall at a steady rate: Nordic and DIN (type I),
//           BBC (type IIa) and EBU (type IIb).
// The readings are in decibels from the calibration level, which is the level that the meter shows as zero.
class VuMeterEngine
{
public:
    enum Mode { vu, rms, nordic, din, bbc, ebu };
    enum Display { leftRight, midSide, single };
    enum Weighting { flat, aWeighting, cWeighting, kWeighting };

    // Settings. Safe to change from any thread.
    std::atomic<int> mode { vu }, ballistics { 0 }, display { leftRight }, weighting { flat };
    std::atomic<float> overshootPercent { 1.5f }, speed { 1.f }, rmsWindowMs { 300.f }, calibrationDb { -18.f };
    std::atomic<float> trimLeftDb { 0.f }, trimRightDb { 0.f };
    std::atomic<bool> aes17 { false };

    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
        designed = false;
        for (auto& meter : meters)
            meter = {};
    }

    // Measures one block. Called on the audio thread only.
    void process(const float* left, const float* right, int numSamples)
    {
        if (numSamples <= 0)
            return;

        designIfNeeded();

        const int currentMode = mode.load(std::memory_order_relaxed);
        const int currentDisplay = display.load(std::memory_order_relaxed);
        const double trimL = std::pow(10.0, (double)trimLeftDb.load(std::memory_order_relaxed) / 20.0);
        const double trimR = std::pow(10.0, (double)trimRightDb.load(std::memory_order_relaxed) / 20.0);
        const int variant = ballistics.load(std::memory_order_relaxed);
        const bool plus3 = aes17.load(std::memory_order_relaxed);
        const bool useWeighting = currentMode == rms;
        const int numMeters = currentDisplay == single ? 1 : 2;

        for (int i = 0; i < numSamples; ++i)
        {
            const double a = (double)left[i] * trimL;
            const double b = (double)right[i] * trimR;

            std::array<double, 2> input;
            if (currentDisplay == midSide)
                input = { 0.5 * (a + b), 0.5 * (a - b) };
            else if (currentDisplay == single)
                input = { 0.5 * (a + b), 0.0 };
            else
                input = { a, b };

            for (int m = 0; m < numMeters; ++m)
            {
                auto& meter = meters[(size_t)m];
                const double x = input[(size_t)m];

                meter.peak = std::max(meter.peak, std::abs(x));

                const double weighted = useWeighting ? meter.weight(x, sections) : x;

                switch (currentMode)
                {
                    case vu:
                    {
                        // The average of the rectified signal reads a sine at its RMS level, which is 1.1107 times as much
                        const double average = vuFilter.process(meter.vuAverage, std::abs(x) * 1.1107);
                        const double squares = vuFilter.process(meter.vuSquares, x * x);
                        const double fromSquares = std::sqrt(std::max(0.0, squares));
                        meter.level = variant == 0 ? average : variant == 1 ? fromSquares : 0.5 * (average + fromSquares);
                        break;
                    }

                    case rms:
                        meter.meanSquare += rmsCoefficient * (weighted * weighted - meter.meanSquare);
                        meter.level = std::sqrt(std::max(0.0, meter.meanSquare)) * (plus3 ? 1.41421356 : 1.0);
                        break;

                    default:
                    {
                        const double rectified = std::abs(x);
                        if (rectified > meter.envelope)
                            meter.envelope += ppmAttack * (rectified - meter.envelope);
                        else
                            meter.envelope *= ppmRelease;
                        meter.level = meter.envelope;
                        break;
                    }
                }
            }
        }

        const double calibration = (double)calibrationDb.load(std::memory_order_relaxed);
        for (int m = 0; m < 2; ++m)
        {
            auto& meter = meters[(size_t)m];
            const double level = m < numMeters ? meter.level : 0.0;
            const float db = level > 1.0e-7 ? (float)(20.0 * std::log10(level) - calibration) : -120.f;
            value[(size_t)m].store(db, std::memory_order_relaxed);

            // The highest sample since the last reading, which the editor reads in turn
            float highest = (float)meter.peak;
            meter.peak = 0.0;
            float previous = peak[(size_t)m].load(std::memory_order_relaxed);
            while (highest > previous && !peak[(size_t)m].compare_exchange_weak(previous, highest, std::memory_order_relaxed)) {}
        }

        metersInUse.store(numMeters, std::memory_order_relaxed);
    }

    struct Readings
    {
        std::array<float, 2> value { -120.f, -120.f }; // dB from the calibration level
        std::array<float, 2> peakDb { -120.f, -120.f }; // the highest sample since the last reading, in dBFS
        int numMeters = 2;
    };

    // The latest readings, on the GUI thread
    Readings read()
    {
        Readings readings;
        readings.numMeters = metersInUse.load(std::memory_order_relaxed);
        for (size_t m = 0; m < 2; ++m)
        {
            readings.value[m] = value[m].load(std::memory_order_relaxed);
            readings.peakDb[m] = juce::Decibels::gainToDecibels(peak[m].exchange(0.f, std::memory_order_relaxed), -120.f);
        }
        return readings;
    }

private:
    struct Section
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };

    struct SectionState
    {
        double z1 = 0, z2 = 0;
    };

    // A second order low-pass in transposed direct form II, with the state kept apart so that it can serve both meters
    struct LowPass
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

        double process(SectionState& state, double in) const
        {
            const double out = b0 * in + state.z1;
            state.z1 = b1 * in - a1 * out + state.z2;
            state.z2 = b2 * in - a2 * out;
            return out;
        }
    };

    static constexpr int maxSections = 4;

    struct Meter
    {
        SectionState vuAverage, vuSquares;
        std::array<SectionState, maxSections> weightingStates {};
        double meanSquare = 0.0, envelope = 0.0, level = 0.0, peak = 0.0;

        double weight(double x, const std::array<Section, maxSections>& sections)
        {
            double y = x;
            for (size_t i = 0; i < sections.size(); ++i)
            {
                const auto& c = sections[i];
                auto& s = weightingStates[i];
                const double out = c.b0 * y + s.z1;
                s.z1 = c.b1 * y - c.a1 * out + s.z2;
                s.z2 = c.b2 * y - c.a2 * out;
                y = out;
            }
            return y;
        }
    };

    // An analog section, (b2 s^2 + b1 s + b0) / (a2 s^2 + a1 s + a0), made digital by the bilinear transform
    static Section bilinear(double b2, double b1, double b0, double a2, double a1, double a0, double sampleRate)
    {
        const double k = 2.0 * sampleRate;
        const double n0 = b2 * k * k + b1 * k + b0, n1 = -2.0 * b2 * k * k + 2.0 * b0, n2 = b2 * k * k - b1 * k + b0;
        const double d0 = a2 * k * k + a1 * k + a0, d1 = -2.0 * a2 * k * k + 2.0 * a0, d2 = a2 * k * k - a1 * k + a0;

        Section section;
        section.b0 = n0 / d0;
        section.b1 = n1 / d0;
        section.b2 = n2 / d0;
        section.a1 = d1 / d0;
        section.a2 = d2 / d0;
        return section;
    }

    static double magnitudeAt(const std::array<Section, maxSections>& sections, double frequency, double sampleRate)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
        std::complex<double> response = 1.0;
        for (const auto& c : sections)
            response *= (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
        return std::abs(response);
    }

    void designIfNeeded()
    {
        const float overshoot = juce::jlimit(0.3f, 20.f, overshootPercent.load(std::memory_order_relaxed));
        const float scale = juce::jlimit(0.25f, 4.f, speed.load(std::memory_order_relaxed));
        const float window = juce::jmax(10.f, rmsWindowMs.load(std::memory_order_relaxed));
        const int selectedMode = mode.load(std::memory_order_relaxed);
        const int selectedWeighting = weighting.load(std::memory_order_relaxed);

        if (designed && juce::approximatelyEqual(overshoot, designedOvershoot) && juce::approximatelyEqual(scale, designedSpeed)
            && juce::approximatelyEqual(window, designedWindow) && selectedMode == designedMode && selectedWeighting == designedWeighting)
            return;

        designed = true;
        designedOvershoot = overshoot;
        designedSpeed = scale;
        designedWindow = window;
        designedMode = selectedMode;
        designedWeighting = selectedWeighting;

        // The VU: the damping from the overshoot, and the natural frequency from the 300 ms that it takes to reach the reading
        {
            const double os = (double)overshoot / 100.0;
            const double zeta = -std::log(os) / std::sqrt(juce::MathConstants<double>::pi * juce::MathConstants<double>::pi + std::log(os) * std::log(os));
            const double riseSeconds = 0.31 / (double)scale;
            const double wn = (juce::MathConstants<double>::pi - std::acos(zeta)) / (riseSeconds * std::sqrt(1.0 - zeta * zeta));

            const double k = 2.0 * sampleRate;
            const double d0 = k * k + 2.0 * zeta * wn * k + wn * wn;
            vuFilter.b0 = wn * wn / d0;
            vuFilter.b1 = 2.0 * wn * wn / d0;
            vuFilter.b2 = wn * wn / d0;
            vuFilter.a1 = 2.0 * (wn * wn - k * k) / d0;
            vuFilter.a2 = (k * k - 2.0 * zeta * wn * k + wn * wn) / d0;
        }

        // The RMS window: the mean square follows with a time constant that gets it most of the way in the window
        rmsCoefficient = 1.0 - std::exp(-1.0 / (((double)window * 0.001 / 2.3) * sampleRate));

        // The PPM detectors: the time to reach 80% of a tone, and the rate of fall in decibels per second
        {
            double integrationMs = 5.0, fallDbPerSecond = 20.0 / 1.5;
            switch (selectedMode)
            {
                case nordic: integrationMs = 5.0;  fallDbPerSecond = 20.0 / 1.5; break;
                case din:    integrationMs = 5.0;  fallDbPerSecond = 20.0 / 1.7; break;
                case bbc:    integrationMs = 10.0; fallDbPerSecond = 24.0 / 2.8; break;
                case ebu:    integrationMs = 10.0; fallDbPerSecond = 24.0 / 2.8; break;
                default: break;
            }

            const double rise = integrationMs * 0.001 / (double)scale;
            ppmAttack = 1.0 - std::exp(-std::log(5.0) / (rise * sampleRate));
            ppmRelease = std::pow(10.0, -(fallDbPerSecond * (double)scale) / (20.0 * sampleRate));
        }

        // The weighting curves, as sections that are made to read 0 dB at 1 kHz
        sections = {};
        const double pi = juce::MathConstants<double>::pi;
        const double w1 = 2.0 * pi * 20.598997, w2 = 2.0 * pi * 107.65265, w3 = 2.0 * pi * 737.86223, w4 = 2.0 * pi * 12194.217;

        if (selectedWeighting == aWeighting)
        {
            sections[0] = bilinear(1, 0, 0, 1, 2 * w1, w1 * w1, sampleRate);
            sections[1] = bilinear(1, 0, 0, 1, w2 + w3, w2 * w3, sampleRate);
            sections[2] = bilinear(0, 0, w4 * w4, 1, 2 * w4, w4 * w4, sampleRate);
        }
        else if (selectedWeighting == cWeighting)
        {
            sections[0] = bilinear(1, 0, 0, 1, 2 * w1, w1 * w1, sampleRate);
            sections[1] = bilinear(0, 0, w4 * w4, 1, 2 * w4, w4 * w4, sampleRate);
        }
        else if (selectedWeighting == kWeighting)
        {
            const auto shelf = LoudnessMeter::makeShelf(sampleRate);
            const auto highPass = LoudnessMeter::makeHighPass(sampleRate);
            sections[0] = { shelf.b0, shelf.b1, shelf.b2, shelf.a1, shelf.a2 };
            sections[1] = { highPass.b0, highPass.b1, highPass.b2, highPass.a1, highPass.a2 };
        }

        if (selectedWeighting != flat)
        {
            const double gain = 1.0 / magnitudeAt(sections, 1000.0, sampleRate);
            sections[0].b0 *= gain;
            sections[0].b1 *= gain;
            sections[0].b2 *= gain;
        }
        else
        {
            sections[0] = {};
        }
    }

    double sampleRate = 44100.0;
    bool designed = false;
    float designedOvershoot = 0.f, designedSpeed = 0.f, designedWindow = 0.f;
    int designedMode = -1, designedWeighting = -1;

    LowPass vuFilter;
    double rmsCoefficient = 0.0, ppmAttack = 0.0, ppmRelease = 0.0;
    std::array<Section, maxSections> sections;
    std::array<Meter, 2> meters;

    std::array<std::atomic<float>, 2> value {}, peak {};
    std::atomic<int> metersInUse { 2 };
};
