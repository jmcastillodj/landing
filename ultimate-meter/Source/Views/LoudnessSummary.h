#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Engine/LoudnessMeter.h"

//==============================================================================
// The loudness readings that matter most, as numbers in the side column, where they
// can be seen whichever view is showing. The Loudness view has all of them.
class LoudnessSummary : public juce::Component
{
public:
    // Most platforms ask for true peaks no higher than this
    static constexpr float truePeakLimitDb = -1.f;

    LoudnessSummary() { setOpaque(true); }

    void paint(juce::Graphics& g) override;

    // The numbers are the loudness, or the RMS levels. A click on the title asks for the other.
    enum class Mode { loudness, rms };
    void setMode(Mode newMode);
    std::function<void()> onTitleClicked;

    // Shows the RMS levels of the two channels and the highest sample peak since the last reset, in decibels. Like
    // update(), only when a readout is due.
    void updateRms(float leftDb, float rightDb, float peakHoldDb, bool readoutDue);

    // Clicking the last row, which holds the highest value since the last reset (the true peak, or in RMS
    // mode the sample peak), restarts it
    std::function<void()> onTruePeakClicked;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // Shows new readings, called by the editor once per frame. The numbers only take the new
    // readings when a readout is due, which the editor decides for every number at once, and
    // are only drawn again when what they say has changed. targetLufs is 0 for no target.
    void update(const LoudnessMeter::Readings& readings, float maxTruePeakDb, float targetLufs, bool readoutDue);

private:
    Mode mode = Mode::loudness;
    juce::Rectangle<int> titleArea;
    juce::String rmsLoudest, rmsLeft, rmsRight, peakHold;
    bool peakHoldIsOver = false;
    juce::String integrated, difference, shortTerm, range, truePeak;
    bool truePeakIsOver = false;
    juce::Rectangle<int> truePeakRow;
    bool truePeakHovered = false, titleHovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessSummary)
};

//==============================================================================
// The correlation of left and right as a bar from the center: to the right and blue when the
// channels are in phase, to the left and red when they are out of phase. The bar is the slow
// reading, and the line over it is the fast one.
class CorrelationBar : public juce::Component
{
public:
    CorrelationBar() { setOpaque(true); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Shows new readings, called by the editor once per frame
    void update(float fastCorrelation, float slowCorrelation);

private:
    float xOf(float correlation) const;

    float fast = 0.f, slow = 0.f;
    juce::Rectangle<float> track;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorrelationBar)
};
