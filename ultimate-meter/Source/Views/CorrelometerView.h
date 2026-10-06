#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../UI/Controls.h"
#include "../Engine/MultibandCorrelator.h"

//==============================================================================
// The correlation of two signals in every band of the spectrum, as bars that rise from zero when the signals
// are in phase and fall when they are out of phase. A band that falls shows a phase problem there, which the
// broadband correlation meter hides when the rest of the spectrum is fine.
class CorrelometerView : public juce::Component
{
public:
    CorrelometerView(juce::AudioProcessorValueTreeState& apvts, juce::ValueTree& state);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    // Takes the latest readings, called by the editor once per frame
    void update(const MultibandCorrelator& correlator);

    bool areControlsHidden() const { return controlsHidden; }

    // The room at the top that the grip of the view takes in multi mode, so that the buttons are not under it
    void setTopInset(int inset) { if (inset != topInset) { topInset = inset; resized(); repaint(); } }
    void setControlsHidden(bool shouldHide);

private:
    struct ChoiceButton
    {
        juce::String title, parameterID;
        juce::Rectangle<int> titleArea, buttonArea;
    };

    void showChoiceMenu(const juce::String& parameterID, juce::Rectangle<int> area);
    juce::String choiceText(const juce::String& parameterID) const;
    float yOf(float value) const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::ValueTree& state;

    FloatingKnob averageKnob, bandsKnob;
    std::array<ChoiceButton, 4> choices; // Pri, Sec, Scale, B/width
    juce::Rectangle<int> hideArea, plotArea, barsArea, controlsArea, topArea;

    std::array<float, MultibandCorrelator::maxBands> values {};
    int numBands = 0;
    bool controlsHidden = false;
    bool hovered = false; // the controls show only while the mouse is over the view
    int topInset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorrelometerView)
};
