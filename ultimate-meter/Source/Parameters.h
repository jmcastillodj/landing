#pragma once

#include <JuceHeader.h>
#include <array>
#include <limits>

//==============================================================================
// Every setting of the plugin is a parameter in the AudioProcessorValueTreeState,
// so that it is saved with the session. The display settings are not automatable,
// which keeps them out of the host's automation lists.
namespace Parameters
{
    namespace ID
    {
        // "Scale Knob" is the ID that version 1 used, so it has to stay as it is
        inline const juce::String goniometerScale { "Scale Knob" };
        inline const juce::String decayRate { "decayRate" };
        inline const juce::String holdTime { "holdTime" };
        inline const juce::String averagerDuration { "averagerDuration" };
        inline const juce::String meterView { "meterView" };
        inline const juce::String showTick { "showTick" };
        inline const juce::String mainView { "mainView" };
        inline const juce::String goniometerMode { "goniometerMode" };
        inline const juce::String goniometerPersistence { "goniometerPersistence" };
        inline const juce::String spectrumChannels { "spectrumChannels" };
        inline const juce::String spectrumReference { "spectrumReference" };
        inline const juce::String spectrogramColours { "spectrogramColours" };
        inline const juce::String waveformChannels { "waveformChannels" };
        inline const juce::String waveformColours { "waveformColours" };
        inline const juce::String waveformMode { "waveformMode" };
        inline const juce::String waveformZoom { "waveformVZoom" };
        inline const juce::String waveformSpan { "waveformSpan" };
        inline const juce::String waveformPeakHistory { "waveformPeakHistory" };
        inline const juce::String waveformTimecode { "waveformTimecode" };
        inline const juce::String summaryMode { "summaryMode" };
        inline const juce::String balanceDetail { "balanceDetail" };
        inline const juce::String balanceAverage { "balanceAverage" };
        inline const juce::String radarSpeed { "radarSpeed" };
        inline const juce::String radarSource { "radarSource" };
        inline const juce::String spectrumStyle { "spectrumStyle" };
        inline const juce::String spectrumBars { "spectrumBars" };
        inline const juce::String spectrumSpeed { "spectrumSpeed" };
        // The ID changed when the default became 0 dB/oct, so that sessions saved with the old default of 4.5 open at the new one
        inline const juce::String spectrumTilt { "spectrumTiltNatural" };
        inline const juce::String spectrumSmoothing { "spectrumSmoothing" };
        inline const juce::String spectrumResolution { "spectrumResolution" };
        inline const juce::String spectrumPeakHold { "spectrumPeakHold" };
        inline const juce::String loudnessTarget { "loudnessTarget" };
        inline const juce::String refreshRate { "refreshRate" };
        inline const juce::String timeSpan { "timeSpan" };
        inline const juce::String historyShow { "historyShow" };
    }

    // The options of each choice parameter, and the values that they stand for
    inline const juce::StringArray decayRateNames { "-3dB/s", "-6dB/s", "-12dB/s", "-24dB/s", "-36dB/s" };
    inline constexpr std::array<float, 5> decayRatesDbPerSecond { 3.f, 6.f, 12.f, 24.f, 36.f };

    inline const juce::StringArray holdTimeNames { "0s", "0.5s", "2s", "4s", "6s", "inf" };
    inline constexpr std::array<float, 6> holdTimesSeconds { 0.f, 0.5f, 2.f, 4.f, 6.f, std::numeric_limits<float>::infinity() };

    inline const juce::StringArray averagerDurationNames { "100ms", "250ms", "500ms", "1000ms", "2000ms" };
    inline constexpr std::array<float, 5> averagerDurationsSeconds { 0.1f, 0.25f, 0.5f, 1.f, 2.f };

    // What the level bars show, and, as a setting of its own, what the history of the levels shows
    inline const juce::StringArray meterViewNames { "Peak + RMS", "Peak", "RMS" };
    inline const juce::StringArray mainViewNames { "Goniometer", "Spectrum", "Spectrogram", "History", "Loudness", "Waveform", "Balance", "Loudness Round" };

    enum MeterView
    {
        peakAndRmsMeters,
        peakMeters,
        rmsMeters
    };

    enum MainView
    {
        viewGoniometer,
        viewSpectrum,
        viewSpectrogram,
        viewHistory,
        viewLoudness,
        viewWaveform, // added after the others, so that sessions saved before them keep their view
        viewBalance,
        viewLoudnessRound
    };

    inline const juce::StringArray goniometerModeNames { "Lissajous", "Polar" };

    enum GoniometerMode
    {
        lissajousMode,
        polarMode
    };

    inline const juce::StringArray goniometerPersistenceNames { "Off", "Short", "Long" };
    inline constexpr std::array<float, 3> goniometerPersistenceSeconds { 0.f, 0.15f, 0.6f };

    inline const juce::StringArray spectrumChannelsNames { "Left / Right", "Mid / Side" };

    // A reference curve that the spectrum can show: a straight line of a given slope in decibels per octave,
    // placed at the average level of the signal so that the shape of the two can be compared. The first
    // choice is none, and its slope is not used.
    inline const juce::StringArray spectrumReferenceNames { "Off", "Pink noise (-3 dB/oct)", "White noise (0 dB/oct)", "Brown noise (-6 dB/oct)", "Typical mix (-4.5 dB/oct)", "Blue noise (+3 dB/oct)" };
    inline const juce::StringArray spectrumReferenceShortNames { "", "PINK NOISE", "WHITE NOISE", "BROWN NOISE", "TYPICAL MIX", "BLUE NOISE" };
    inline constexpr std::array<float, 6> spectrumReferenceSlopesDbPerOctave { 0.f, -3.f, 0.f, -6.f, -4.5f, 3.f };

    // The colours of the spectrogram, as gradients from silence to the loudest
    inline const juce::StringArray spectrogramColourNames { "Studio", "Magma", "Viridis", "Ice", "Rainbow", "Grayscale" };

    // What the waveform shows: both channels as one (the loudest of the two at every moment), one channel,
    // or two channels in lanes of their own
    inline const juce::StringArray waveformChannelNames { "Stereo", "Left", "Right", "Mid", "Side", "Left / Right", "Mid / Side" };
    enum WaveformChannels { waveformStereo, waveformLeft, waveformRight, waveformMid, waveformSide, waveformLeftRight, waveformMidSide };
    inline const juce::StringArray waveformColourNames { "Multi-band", "Static", "Color map" };
    enum WaveformColours { colourMultiBand, colourStatic, colourMap };
    inline const juce::StringArray waveformModeNames { "Scrolling", "Static sweep" };

    // The span of time that the waveform shows. It has its own, since close up it shows the wave itself.
    inline const juce::StringArray waveformSpanNames { "10 ms", "25 ms", "50 ms", "100 ms", "250 ms", "500 ms", "1 s", "2 s", "5 s", "10 s", "15 s", "30 s", "60 s" };
    inline constexpr std::array<float, 13> waveformSpansSeconds { 0.01f, 0.025f, 0.05f, 0.1f, 0.25f, 0.5f, 1.f, 2.f, 5.f, 10.f, 15.f, 30.f, 60.f };
    inline constexpr int defaultWaveformSpan = 8;

    // What the numbers in the side column are: the loudness, or the RMS levels
    inline const juce::StringArray balanceDetailNames { "Broad", "Fine" };
    // How long the tonal balance remembers: the average moves with the music over this time, or covers everything since the reset
    inline const juce::StringArray balanceAverageNames { "10 s", "30 s", "1 min", "Whole programme" };
    inline constexpr std::array<float, 4> balanceAverageSeconds { 10.f, 30.f, 60.f, 0.f };

    // The radar of the loudness: the time one turn takes, and the reading that it draws
    inline const juce::StringArray radarSpeedNames { "30 s", "1 min", "2 min", "5 min" };
    inline constexpr std::array<float, 4> radarSpeedsSeconds { 30.f, 60.f, 120.f, 300.f };
    inline const juce::StringArray radarSourceNames { "Momentary", "Short-term" };
    inline const juce::StringArray summaryModeNames { "Loudness", "RMS" };

    // How the spectrum is drawn: as a curve, or as bars, whose number says how fine the analysis is
    inline const juce::StringArray spectrumStyleNames { "Curve", "Bars" };
    inline const juce::StringArray spectrumBarsNames { "16", "24", "31", "48", "64", "96" };
    inline constexpr std::array<int, 6> spectrumBarCounts { 16, 24, 31, 48, 64, 96 };
    inline const juce::StringArray spectrumSpeedNames { "Slow", "Medium", "Fast" };
    inline constexpr std::array<float, 3> spectrumReleaseSeconds { 0.9f, 0.3f, 0.1f };

    inline const juce::StringArray spectrumTiltNames { "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };
    inline constexpr std::array<float, 4> spectrumTiltsDbPerOctave { 0.f, 3.f, 4.5f, 6.f };

    inline const juce::StringArray spectrumSmoothingNames { "Off", "1/12 oct", "1/6 oct", "1/3 oct" };
    inline constexpr std::array<float, 4> spectrumSmoothingOctaves { 0.f, 1.f / 12.f, 1.f / 6.f, 1.f / 3.f };

    inline const juce::StringArray spectrumResolutionNames { "2048", "4096", "8192", "16384" };
    inline constexpr std::array<int, 4> spectrumResolutionOrders { 11, 12, 13, 14 };

    // The loudness that common platforms and standards ask for. 0 stands for no target.
    inline const juce::StringArray loudnessTargetNames { "Off", "-14 LUFS Streaming", "-16 LUFS Podcast", "-23 LUFS EBU R 128", "-24 LKFS ATSC A/85" };
    inline constexpr std::array<float, 5> loudnessTargetsLufs { 0.f, -14.f, -16.f, -23.f, -24.f };

    // How much time the spectrogram, the history and the loudness show. They share one timeline.
    inline const juce::StringArray timeSpanNames { "15 s", "30 s", "60 s" };
    inline constexpr std::array<float, 3> timeSpansSeconds { 15.f, 30.f, 60.f };

    // How often the editor redraws. On macOS every frame costs a flush of the whole window, whatever
    // is drawn in it, so the frame rate is what decides how much of the CPU the editor takes. It is
    // 60 fps unless it is changed, which is what a meter ought to look like, and 30 fps takes about
    // two thirds of the CPU time.
    inline const juce::StringArray refreshRateNames { "30 fps", "60 fps" };
    inline constexpr std::array<int, 2> refreshRatesHz { 30, 60 };

    // Looks up the value of a choice parameter, whatever index it is given
    template<typename Table>
    auto valueAt(const Table& table, int index)
    {
        return table[static_cast<size_t>(juce::jlimit(0, static_cast<int>(table.size()) - 1, index))];
    }

    //==============================================================================
    // The saved state is the APVTS tree as XML, tagged with this version number.
    // Raise it whenever the meaning of a saved value changes, and migrate older
    // states in UltimateMeterAudioProcessor::setStateInformation().
    inline constexpr int currentStateVersion = 2;
    inline const juce::Identifier stateVersionProperty { "stateVersion" };

    //==============================================================================
    // Version 1 saved its settings as a raw binary stream rather than as XML.
    // These are the settings it held, as parameter values.
    struct LegacyState
    {
        float goniometerScale = 100.f;
        int decayRate = 0, holdTime = 2, averagerDuration = 0, meterView = 0;
        bool showTick = true;
    };

    // The size of the version 1 stream: a float, two ints, a bool, and three ints
    inline constexpr int legacyStateSizeInBytes = 4 + 4 + 4 + 1 + 4 + 4 + 4;

    // Reads a version 1 state. Returns false if the data is not one.
    inline bool readLegacyState(const void* data, int sizeInBytes, LegacyState& state)
    {
        if (data == nullptr || sizeInBytes != legacyStateSizeInBytes)
            return false;

        juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);

        const float scale = stream.readFloat();
        const int decayId = stream.readInt();
        const int holdId = stream.readInt();
        const bool tick = stream.readBool();
        const int averagerId = stream.readInt();
        const int meterViewId = stream.readInt();
        stream.readInt(); // how the two histograms were laid out, which the one history graph has no use for

        // Version 1 could save uninitialized values, so anything out of range
        // falls back to the default, as its editor did. The combo box IDs
        // started at 1, and the view IDs at 0.
        auto inRange = [](int value, int low, int high) { return value >= low && value <= high; };

        state.goniometerScale = (scale >= 50.f && scale <= 200.f) ? scale : 100.f;
        state.decayRate = inRange(decayId, 1, 5) ? decayId - 1 : 0;
        state.holdTime = inRange(holdId, 1, 6) ? holdId - 1 : 2;
        state.averagerDuration = inRange(averagerId, 1, 5) ? averagerId - 1 : 0;
        state.meterView = inRange(meterViewId, 0, 2) ? meterViewId : 0;
        state.showTick = tick;
        return true;
    }

    //==============================================================================
    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using Choice = juce::AudioParameterChoice;
        const auto display = juce::AudioParameterChoiceAttributes().withAutomatable(false);

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        // The scale runs from half size to double on a logarithmic scale, so that 100% is in the middle of
        // the knob's travel, and halving and doubling are the same turn. Version 1 gave this range a skew of 0.1, which put
        // 50.0 to 50.1% on the first half of the knob and everything else on the last tenth of it. The
        // saved state holds the value itself, not the position of the knob, so old sessions are unaffected.
        const juce::NormalisableRange<float> scaleRange(50.f, 200.f,
            [](float low, float high, float position) { return low * std::pow(high / low, position); },
            [](float low, float high, float value)    { return std::log(value / low) / std::log(high / low); },
            [](float low, float high, float value)    { return juce::jlimit(low, high, std::round(value)); });

        // Version hint 1 marks the parameter from version 1, and 2 the ones added in 2.0
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ID::goniometerScale, 1 },
            "Goniometer Scale", scaleRange, 100.f));

        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::decayRate, 2 }, "Tick Decay", decayRateNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::holdTime, 2 }, "Tick Hold", holdTimeNames, 2, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::averagerDuration, 2 }, "Correlation Time", averagerDurationNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::meterView, 2 }, "Level Meters", meterViewNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::mainView, 2 }, "View", mainViewNames, viewSpectrum, display));

        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::goniometerMode, 2 }, "Goniometer Mode", goniometerModeNames, lissajousMode, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::goniometerPersistence, 2 }, "Goniometer Persistence", goniometerPersistenceNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumChannels, 2 }, "Spectrum Channels", spectrumChannelsNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumReference, 2 }, "Spectrum Reference", spectrumReferenceNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrogramColours, 2 }, "Spectrogram Colours", spectrogramColourNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::waveformChannels, 2 }, "Waveform Channels", waveformChannelNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::waveformColours, 2 }, "Waveform Colours", waveformColourNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::waveformMode, 2 }, "Waveform Mode", waveformModeNames, 0, display));
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { ID::waveformZoom, 2 }, "Waveform Zoom",
            juce::NormalisableRange<float>(0.1f, 8.f, 0.1f), 1.f, juce::AudioParameterFloatAttributes().withAutomatable(false)));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::waveformSpan, 2 }, "Waveform Span", waveformSpanNames, defaultWaveformSpan, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::balanceDetail, 2 }, "Balance Detail", balanceDetailNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::balanceAverage, 2 }, "Balance Average", balanceAverageNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::radarSpeed, 2 }, "Radar Speed", radarSpeedNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::radarSource, 2 }, "Radar Reading", radarSourceNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::summaryMode, 2 }, "Readout", summaryModeNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumStyle, 2 }, "Spectrum Style", spectrumStyleNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumBars, 2 }, "Spectrum Bars", spectrumBarsNames, 2, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumSpeed, 2 }, "Spectrum Speed", spectrumSpeedNames, 1, display));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ID::waveformPeakHistory, 2 }, "Waveform Peak History", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ID::waveformTimecode, 2 }, "Waveform Time Code", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumTilt, 2 }, "Spectrum Tilt", spectrumTiltNames, 0, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumSmoothing, 2 }, "Spectrum Smoothing", spectrumSmoothingNames, 2, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::spectrumResolution, 2 }, "Spectrum Resolution", spectrumResolutionNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::loudnessTarget, 2 }, "Loudness Target", loudnessTargetNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::refreshRate, 2 }, "Refresh Rate", refreshRateNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::timeSpan, 2 }, "Time Span", timeSpanNames, 1, display));
        layout.add(std::make_unique<Choice>(juce::ParameterID { ID::historyShow, 2 }, "History Shows", meterViewNames, peakAndRmsMeters, display));

        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ID::spectrumPeakHold, 2 }, "Spectrum Peak Hold", false,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));

        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { ID::showTick, 2 }, "Ticks", true,
            juce::AudioParameterBoolAttributes().withAutomatable(false)));

        return layout;
    }
}
