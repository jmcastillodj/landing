#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "Timeline.h"

//==============================================================================
// The waveform in real time, drawn from the samples themselves. Every sample that arrives is folded into
// a column of 1/columnsPerSecond of a second, which keeps the true highest and lowest values of left, right,
// mid and side in it (so a peak is never lost, and the shape is the signed waveform, not an envelope), and
// their RMS levels, for the brighter core. The vertical scale is linear in amplitude, with full scale at
// the top and bottom edges before the zoom. A column that reaches full scale gets a red cap, which is how
// clipping shows.
//
// What is drawn can be chosen: both channels as one, one channel, or two channels in lanes of their own;
// the colours (by frequency band, one colour, or by level); a history of the levels of the three bands
// over the waveform; scrolling, or a static picture that is swept from the left; and the time code of
// the host. The columns are kept in absolute time, so nothing shimmers as the picture scrolls.
//
// The colour of a column in multi-band mode tells what the sound in it is made of: the samples go through
// a pair of crossover filters, and the power of the low band (under 250 Hz) is red, of the middle (to 2.5
// kHz) green, and of the highs blue, mixed as the three are mixed in the sound.
class WaveformView : public juce::Component
{
public:
    static constexpr int columnsPerSecond = 240;
    static constexpr int maxColumns = columnsPerSecond * Timeline::maxSeconds;
    static constexpr float minDb = -48.f;

    struct Settings
    {
        int channels = Parameters::waveformStereo;
        int colours = Parameters::colourMultiBand;
        bool sweep = false;
        float zoom = 1.f;
        bool peakHistory = false;
        bool timeCode = false;
        double hostSeconds = -1.0;

        bool operator==(const Settings& other) const
        {
            return channels == other.channels && colours == other.colours && sweep == other.sweep && peakHistory == other.peakHistory
                && timeCode == other.timeCode && juce::exactlyEqual(zoom, other.zoom) && juce::exactlyEqual(hostSeconds, other.hostSeconds);
        }
    };

    explicit WaveformView(UltimateMeterAudioProcessor& processor);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    void clearHistory();

    // Folds the samples that have arrived since the last call into columns. Called once per frame by
    // the editor, whichever view is showing, so that nothing is missed while it is hidden.
    void update();

    void setSpan(float seconds);
    void setSettings(const Settings& newSettings);

    // Called with -1 or +1 when the lens at the corner is used to make the waveform smaller or larger
    std::function<void(int)> onZoomStep;

    // Gives the colour of a column from the powers of its low, middle and high bands
    static juce::Colour colourOfBands(float low, float mid, float high);

    // Formats a time in seconds as hours, minutes, seconds and milliseconds
    static juce::String formatTimeCode(double seconds);

private:
    // The four signals that are kept: left, right, mid and side
    static constexpr int numSignals = 4;

    struct Column
    {
        std::array<float, numSignals> minimum {}, maximum {}, rms {};
        float low = 0.f, mid = 0.f, high = 0.f; // mean power of each band of the mono signal
    };

    // What one lane shows
    struct Shape
    {
        float lowest = 0.f, highest = 0.f, rmsSquares = 0.f;
    };

    void prepareFilters(double sampleRate);
    void addSample(float left, float right);
    void finishColumn();
    juce::Rectangle<int> lensArea() const;

    UltimateMeterAudioProcessor& audioProcessor;

    juce::Rectangle<int> plot;
    float spanSeconds = 30.f;
    Settings settings;
    int lensHover = 0; // -1 over the minus, +1 over the plus

    // What has been recorded: the newest column is number written - 1
    std::vector<Column> columns;
    juce::uint64 written = 0;

    // The column that is being filled
    std::array<float, numSignals> minimum {}, maximum {};
    std::array<double, numSignals> sumSquares {};
    double sumLow = 0.0, sumMid = 0.0, sumHigh = 0.0;
    int countInColumn = 0;
    double samplesPerColumn = 200.0, phase = 0.0;

    // Two one-pole stages for each crossover frequency
    double filterRate = 0.0, lowCoefficient = 0.0, highCoefficient = 0.0;
    double lowState[2] {}, highState[2] {};

    juce::uint64 lastTotalWritten = 0;
    juce::AudioBuffer<float> samples;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
