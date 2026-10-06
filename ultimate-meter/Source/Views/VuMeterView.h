#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Parameters.h"
#include "../Engine/VuMeterEngine.h"

//==============================================================================
// Needle meters for the level of the signal: VU, RMS and the four PPM scales. Each shows its reading as a needle over a
// scale, with a second needle that holds the highest, the numbers for the loudest peak and for the reading, and a
// lamp that lights when the signal goes past a level that is set, and turns red when it clips. The basic meter is
// what is on show; ADVANCED opens every option.
class VuMeterView : public juce::Component
{
public:
    explicit VuMeterView(juce::AudioProcessorValueTreeState& apvts);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    // Called by the editor once per frame
    void update(const VuMeterEngine::Readings& readings, float elapsedSeconds);

    // Opens the menu with every option. Set by the editor.
    std::function<void(juce::Rectangle<int>)> onAdvanced;

private:
    struct Face
    {
        juce::Rectangle<int> bounds;
        float needle = -120.f, hold = -120.f, peakDb = -120.f;
        double orangeSeconds = 0.0, redSeconds = 0.0;
        float heldPeakDb = -120.f;
    };

    void paintFace(juce::Graphics& g, const Face& face, int index);
    float parameter(const juce::String& id) const { return apvts.getRawParameterValue(id)->load(); }
    void scaleRange(int mode, float& low, float& high) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::array<Face, 2> faces;
    int numFaces = 2;
    juce::Rectangle<int> badgeArea, infoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeterView)
};
