#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../PluginProcessor.h"
#include "Timeline.h"

//==============================================================================
// The waveform in real time, scrolling from right to left, drawn from the samples themselves. Every
// sample that arrives is folded into a column of 1/columnsPerSecond of a second, which keeps the true
// highest and lowest values of both channels in it (so a peak is never lost, and the shape is the
// signed waveform, not an envelope), and the RMS level, for the brighter core. The vertical scale is
// linear in amplitude, with full scale at the top and bottom edges.
//
// The colour of a column tells what the sound in it is made of: the samples go through a pair of
// crossover filters, and the power of the low band (under 250 Hz) is red, of the middle (to 2.5 kHz)
// green, and of the highs blue, mixed as the three are mixed in the sound. A peak at full scale turns
// the column red. The columns are kept in absolute time, so nothing shimmers as the picture scrolls.
class WaveformView : public juce::Component
{
public:
    static constexpr int columnsPerSecond = 240;
    static constexpr int maxColumns = columnsPerSecond * Timeline::maxSeconds;

    explicit WaveformView(UltimateMeterAudioProcessor& processor);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void clearHistory();

    // Folds the samples that have arrived since the last call into columns. Called once per frame by
    // the editor, whichever view is showing, so that nothing is missed while it is hidden.
    void update();

    void setSpan(float seconds);

    // Gives the colour of a column from the powers of its low, middle and high bands
    static juce::Colour colourOfBands(float low, float mid, float high);

private:
    struct Column
    {
        float minimum = 0.f, maximum = 0.f, rms = 0.f;
        float low = 0.f, mid = 0.f, high = 0.f; // mean power of each band
    };

    void prepareFilters(double sampleRate);
    void addSample(float left, float right);
    void finishColumn();

    UltimateMeterAudioProcessor& audioProcessor;

    juce::Rectangle<int> plot;
    float spanSeconds = 30.f;

    // What has been recorded: the newest column is number written - 1
    std::vector<Column> columns;
    juce::uint64 written = 0;

    // The column that is being filled
    float minimum = 0.f, maximum = 0.f;
    double sumSquares = 0.0, sumLow = 0.0, sumMid = 0.0, sumHigh = 0.0;
    int countInColumn = 0;
    double samplesPerColumn = 200.0, phase = 0.0;

    // Two one-pole stages for each crossover frequency
    double filterRate = 0.0, lowCoefficient = 0.0, highCoefficient = 0.0;
    double lowState[2] {}, highState[2] {};

    juce::uint64 lastTotalWritten = 0;
    juce::AudioBuffer<float> samples;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
