#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Engine/TonalTargets.h"
#include "SpectrumSource.h"

//==============================================================================
// The tonal balance: the average spectrum of what has been played since the last reset, shown against a
// target, which is a curve with a band around it. The shape of the spectrum is compared, not its level: the
// average over 100 Hz to 10 kHz is taken out of both. Four bands (low, low-mid, high-mid, high) are named
// along the top, each with how far the spectrum is from the target in it.
//
// The target is chosen from built-in ones, from targets of one's own that were measured from a recording
// (a whole file, or a part of it that was chosen), which are kept in the session.
class TonalBalanceView : public juce::Component
{
public:
    static constexpr float rangeDb = 16.f; // the plot shows this far above and below the target
    static constexpr std::array<double, 3> bandEdges { 200.0, 2000.0, 7000.0 };

    TonalBalanceView(SpectrumSource&, juce::ValueTree& sessionState);

    void paint(juce::Graphics&) override;
    void resized() override;

    // Averages the source's newest spectrum into the picture, called by the editor once per frame
    void record(bool hasNewSpectra);

    // Starts the average again
    void clearHistory();

    // Fine follows the spectrum closely; broad smooths it over half an octave
    void setFine(bool shouldBeFine);

    const juce::String& getTargetName() const { return targetName; }

    // Shows the menu of targets, from the component that asked for it
    void showTargetMenu(juce::Component& target);

private:
    struct BandReading
    {
        float deviation = 0.f;   // how far above the target the spectrum is in the band, on average
        float tolerance = 0.f;   // how wide the band of the target is there, on average
        bool valid = false;
    };

    void selectTarget(const juce::String& name);
    void loadCustomTargets();
    void saveCustomTargets();
    void chooseFile();
    void openFile(const juce::File& file);
    const TonalTargets::Target* currentTarget() const;

    float xOf(double frequency) const;
    float yOf(float decibels) const;
    juce::Path bandPath(const std::vector<float>& low, const std::vector<float>& high) const;
    std::vector<float> measuredCurve() const;

    SpectrumSource& source;
    juce::ValueTree& state;

    juce::Rectangle<int> plot;
    SpectrumEngine::Display layout = TonalTargets::display(1.f / 6.f);
    bool fine = true;

    // The power at each point, added up over the frames, and how many frames there have been
    std::vector<double> powerSum;
    int frames = 0;
    std::vector<float> rendered;

    std::vector<TonalTargets::Target> targets; // the built-in ones, then those of one's own
    juce::String targetName = "Modern";

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::Component> dialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TonalBalanceView)
};
