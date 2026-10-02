#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "SpectrumSource.h"
#include "Timeline.h"

//==============================================================================
// The waveform in real time, scrolling from right to left and mirrored about its middle line, as
// the waveform of a DJ player is. Each slot of the shared timeline is a column whose height is the
// loudest peak of the slot, with a brighter core for its RMS level, and whose colour tells what the
// sound is made of: low frequencies are red, the middle is green, and the highs are blue, mixed
// as the three bands are mixed in the sound. A peak close to full scale turns the column red.
// It is on the shared timeline, so it records whether it is showing or not.
class WaveformView : public juce::Component
{
public:
    static constexpr float minDb = -48.f;

    explicit WaveformView(SpectrumSource& spectrumSource);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void clearHistory();

    // Records the levels (linear gain, the louder channel) and, if new spectra have arrived, the
    // balance of frequencies. Called once per frame by the editor. numNewSlots is how many slots of
    // the timeline have been completed since the last call.
    void record(int numNewSlots, bool hasNewSpectra, float peak, float rms);

    void setSpan(float seconds);

    // Where the colour of a column comes from: the powers of the low, middle and high bands
    // (any common scale) give a colour. Public so that it can be tested.
    static juce::Colour colourOfBands(float low, float mid, float high);

private:
    struct Slot
    {
        float peak = 0.f, rms = 0.f;
        float low = 0.f, mid = 0.f, high = 0.f;
    };

    SpectrumSource& source;
    SpectrumEngine::Display display;
    std::vector<float> spectrum;
    int lowPoints = 0, midPoints = 0; // the points of the display that end the low band and the middle band

    juce::Rectangle<int> plot;
    float spanSeconds = 30.f;

    Timeline::History<Slot> history;
    Slot pending, lastBands;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformView)
};
