#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "Timeline.h"

//==============================================================================
// The waveform in real time, drawn from the samples themselves.
//
// Two histories are kept. The samples of the last ten seconds or so are kept as they came, and while the
// view shows no more than that, every pixel is the true highest and lowest sample of the stretch of time that it
// stands for: close up, one sees the shape of the wave itself, as in an audio editor. For longer spans, the
// samples are folded into columns of 1/columnsPerSecond of a second that keep the same extremes (and the RMS),
// so a peak is never lost however far out one looks.
//
// The vertical scale is linear in amplitude, full scale at the top and bottom edges before the zoom. The
// zoom of the amplitude goes in steps of a tenth, and the span of time has a zoom of its own. A sample at
// full scale gets a red cap, which is how clipping shows.
//
// What is drawn can be chosen: both channels as one, one channel, or two channels in lanes of their own;
// the colours (by frequency band, one colour, or by level); a history of the levels of the three bands
// over the waveform; scrolling, or a static picture that is swept from the left; and the time code of the host.
//
// In multi-band mode the colour of a stretch tells what the sound in it is made of: the samples go through a pair
// of crossover filters, and the power of the low band (under 250 Hz) is red, of the middle (to 2.5 kHz) green, and
// of the highs blue, mixed as the three are mixed in the sound, and smoothed over time so that it does not flicker.
class WaveformView : public juce::Component
{
public:
    static constexpr int columnsPerSecond = 240;
    static constexpr int maxColumns = columnsPerSecond * Timeline::maxSeconds;
    static constexpr int rawCapacity = 1 << 19;
    static constexpr float minDb = -48.f;
    static constexpr float minZoom = 0.1f, maxZoom = 8.f, zoomStep = 0.1f;

    struct Settings
    {
        int channels = Parameters::waveformStereo;
        int colours = Parameters::colourMultiBand;
        bool sweep = false;
        float zoom = 1.f;           // of the amplitude
        float spanSeconds = 5.f;    // of the time
        bool peakHistory = false;
        bool timeCode = false;
        double hostSeconds = -1.0;

        // Musical time: with it the span is a note or some bars, and the grid follows the beats of the host
        bool musical = false;
        double spanBeats = 0.0, bpm = 120.0, beatsPerBar = 4.0, ppq = -1.0;
        juce::String spanLabel;

        // The guide: a level that is marked across the picture, to line the peaks up with
        // Grid lock: the picture is fixed to the bars of the music, from the left, and the pieces of music that come in
        // fall in the same places each time. ppqAtLatest is the musical position at the end of the latest samples, which
        // are the ringTotalAtLatest-th that the plugin has had.
        bool gridLocked = false;
        double ppqAtLatest = -1.0, ringTotalAtLatest = 0.0;

        bool guideOn = false;
        float guideDb = -6.f;

        bool operator==(const Settings& other) const
        {
            return channels == other.channels && colours == other.colours && sweep == other.sweep && peakHistory == other.peakHistory
                && timeCode == other.timeCode && juce::exactlyEqual(zoom, other.zoom) && juce::exactlyEqual(spanSeconds, other.spanSeconds)
                && juce::exactlyEqual(hostSeconds, other.hostSeconds) && musical == other.musical && juce::exactlyEqual(spanBeats, other.spanBeats)
                && juce::exactlyEqual(bpm, other.bpm) && juce::exactlyEqual(beatsPerBar, other.beatsPerBar) && juce::exactlyEqual(ppq, other.ppq)
                && spanLabel == other.spanLabel && guideOn == other.guideOn && juce::exactlyEqual(guideDb, other.guideDb) && gridLocked == other.gridLocked
                && juce::exactlyEqual(ppqAtLatest, other.ppqAtLatest) && juce::exactlyEqual(ringTotalAtLatest, other.ringTotalAtLatest);
        }
    };

    explicit WaveformView(UltimateMeterAudioProcessor& processor);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void clearHistory();

    // Takes in the samples that have arrived since the last call. Called once per frame by the editor,
    // whichever view is showing, so that nothing is missed while it is hidden.
    void update();

    void setSettings(const Settings& newSettings);

    // The lenses at the corner. A step is +1 to see the wave larger (vertical) or closer in time (horizontal), and -1 for
    // the other way. A coarse step is a whole unit of zoom instead of a tenth, or two choices of span.
    std::function<void(int step, bool coarse)> onVerticalStep, onHorizontalStep;
    std::function<void()> onVerticalReset, onHorizontalReset;
    std::function<void(bool on, float db)> onGuideChanged; // the guide was switched, or moved to a level

    // Gives the colour of a stretch from the powers of its low, middle and high bands
    static juce::Colour colourOfBands(float low, float mid, float high);

    // Formats a time in seconds as hours, minutes, seconds and milliseconds
    static juce::String formatTimeCode(double seconds);

    // Formats a span of time: 0.25 s, 5 s
    static juce::String formatSpan(float seconds);

private:
    // The four signals that are kept: left, right, mid and side
    static constexpr int numSignals = 4;

    struct Column
    {
        std::array<float, numSignals> minimum {}, maximum {}, rms {};
        float low = 0.f, mid = 0.f, high = 0.f; // mean power of each band of the mono signal
    };

    // What one pixel of the picture stands for
    struct Bin
    {
        std::array<float, numSignals> lowest {}, highest {}, meanSquare {};
        float low = 0.f, mid = 0.f, high = 0.f;
        juce::int64 index = 0; // the number of the bin since the start, which does not change
    };

    // What one lane shows
    struct Shape
    {
        float lowest = 0.f, highest = 0.f, meanSquare = 0.f;
    };

    void prepareFilters(double sampleRate);
    void addSample(float left, float right);
    void finishColumn();
    void pushRaw(float left, float right);
    double rawMaxSpan() const;

    // Makes the bins of the picture, from the newest, and says where the newest one ends and how wide each is
    void buildBins(std::vector<Bin>& bins, double& newestRight, double& binWidth, int& binsInSpan, juce::int64& originBin) const;
    void restartSweep(const Settings& newSettings);

    // The guide at the corner, which is dragged to the level that is wanted
    juce::Rectangle<int> guideButtonArea() const { return juce::Rectangle<int>(66, 22).withPosition(plot.getX() + 6, plot.getY() + 5); }
    float guideDbAt(int y) const;
    float guideYOfLane(int lane, float db) const;
    bool nearGuide(juce::Point<int> position) const;
    bool draggingGuide = false;

    // Where a sweep began, as a count of samples and a position in the music, which is the left edge of the picture
    juce::int64 sweepOriginSample = 0;
    double sweepOriginPpq = 0.0;
    double gridOriginPpq = 0.0; // the start of the bar where the picture last began, for a picture that is locked to the grid

    // Between a position in the music and a count of samples of the picture, from the position that the host has given
    bool hasMusicalPosition() const { return settings.ppqAtLatest >= 0.0 && settings.bpm > 0.0; }
    double ppqOfViewSample(double sample) const;
    double viewSampleOfPpq(double ppq) const;

    // For a picture that is locked to the grid, the last sample that belongs to it: where the host was when it last played
    double gridEndSample() const;
    bool isGridLocked() const { return settings.gridLocked && settings.sweep && settings.musical && settings.spanBeats > 0.0 && hasMusicalPosition(); }
    int stagnantFrames = 0;

    bool viewHovered = false;
    float wheelAccumulator = 0.f;
    juce::uint32 lastWheelStep = 0;

    static constexpr int numControls = 2; // the vertical lens, the horizontal lens
    juce::Rectangle<int> controlArea(int control) const;
    void hitTestControl(juce::Point<int> position, int& control, int& part) const;

    UltimateMeterAudioProcessor& audioProcessor;

    juce::Rectangle<int> plot;
    Settings settings;
    int hoverControl = -1, hoverPart = 0; // part: -1 the minus, 0 the value, +1 the plus

    // The columns: the newest is number written - 1
    std::vector<Column> columns;
    juce::uint64 written = 0;

    // The samples as they came, in a ring, and how many have come
    std::vector<float> rawLeft, rawRight;
    juce::uint64 rawWritten = 0;

    // The column that is being filled
    std::array<float, numSignals> minimum {}, maximum {};
    std::array<double, numSignals> sumSquares {};
    double sumLow = 0.0, sumMid = 0.0, sumHigh = 0.0;
    int countInColumn = 0;
    double samplesPerColumn = 200.0, phase = 0.0, sampleRate = 44100.0;

    // Two one-pole stages for each crossover frequency
    double filterRate = 0.0, lowCoefficient = 0.0, highCoefficient = 0.0;
    double lowState[2] {}, highState[2] {};

    juce::uint64 lastTotalWritten = 0;
    juce::AudioBuffer<float> samples;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
