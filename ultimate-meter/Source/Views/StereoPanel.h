#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"

//==============================================================================
// The stereo image in three parts, from the top: a triangle on a half circle that leans to the side that the
// sound is on and widens as the stereo image does, the correlation of left and right, and the deviation of
// the mono sum from the stereo source. Without room for the triangle only the two bars show.
class StereoPanel : public juce::Component
{
public:
    StereoPanel() { setOpaque(true); }

    void paint(juce::Graphics& g) override;

    // Shows new readings, called by the editor once per frame
    //  - balanceDb:       right over left, in dB
    //  - width:           0 for mono to 1 for wide
    //  - correlation:     -1 to +1
    //  - monoDeviationDb: the mono sum against the stereo source
    void update(float balanceDb, float width, float correlation, float monoDeviationDb);

    // Whether the stereo panel can show its triangle at the height that it has
    static constexpr int barsHeight = 112;
    static constexpr int fullHeight = 236;

private:
    void paintTriangle(juce::Graphics& g, juce::Rectangle<float> area);
    void paintBar(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, const juce::String& unit, bool isCorrelation);

    float balance = 0.f; // -1 to +1
    float widthShown = 0.f;
    float correlationShown = 0.f;
    float deviationShown = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoPanel)
};
