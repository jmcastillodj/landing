#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Engine/BpmDetector.h"

//==============================================================================
// The tempo of the music in big digits, found by BPM Detective's algorithm: a few seconds after the music starts the tempo
// shows with one decimal, with the onsets that it is made from running behind the number and a meter of how sure it is.
// x2 and :2 correct a tempo found at twice or half the real one, TAP sets a tempo by hand, HOLD freezes the reading.
class BpmView : public juce::Component
{
public:
    explicit BpmView(BpmDetector& detector);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // Called by the editor once per frame, with the tempo of the host (0 if it has none)
    void update(double hostBpm);

    // The room at the top that the grip of the view takes in multi mode
    void setTopInset(int inset) { if (inset != topInset) { topInset = inset; resized(); repaint(); } }

private:
    enum Button { halfButton, doubleButton, tapButton, holdButton, resetButton, numButtons };

    void press(int button);
    void drawButton(juce::Graphics& g, int button, const juce::String& text, bool lit);

    BpmDetector& detector;
    double hostBpm = 0.0;
    int topInset = 0, hovered = -1;
    float envelopeMax = 0.f;

    std::array<juce::Rectangle<int>, numButtons> buttons;
    juce::Rectangle<int> lcdArea, confidenceArea, footerArea;
    std::vector<float> envelope, columns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BpmView)
};
