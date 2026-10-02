#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Engine/ReferenceManager.h"

//==============================================================================
// Compares the mix with reference tracks. Up to four recordings are loaded; the part of one that loops is chosen
// on its waveform (or found as the loudest part), and a click on REFERENCE plays it in place of the mix, at the
// loudness of the mix if level match is on. The peak and the loudness of both are shown side by side, and the
// curve at the bottom is the correction that would bring the tone of the mix to the tone of the reference.
class ReferenceView : public juce::Component
{
public:
    // What is known of the mix, handed over by the editor once per frame
    struct Mix
    {
        float integratedLufs = -200.f;
        float peakDb = -200.f;
        float width = 0.f;
        float plr = 0.f;
        std::vector<float> curve; // empty while there is nothing
    };

    ReferenceView(ReferenceManager& manager, juce::ValueTree& stateTree);
    ~ReferenceView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    void update(const Mix& newMix, float elapsedSeconds);

    // For the menu of the view
    void chooseFile(int slot);
    void removeSelected();
    void smartLoop();
    bool isLevelMatched() const { return levelMatch; }
    void setLevelMatched(bool shouldMatch) { levelMatch = shouldMatch; repaint(); }
    int getSelected() const { return selected; }

private:
    void select(int slot);
    void saveFiles();
    void restoreFiles();
    int sampleAt(float x, const ReferenceManager::Track& track) const;
    float xOfSample(int sample, const ReferenceManager::Track& track) const;
    float xOfFrequency(double frequency) const;
    void computeMatch();

    ReferenceManager& manager;
    juce::ValueTree& state;

    Mix mix;
    float gainDb = 0.f;
    bool levelMatch = true;
    int selected = 0;

    // The part being dragged out on the waveform, in samples, while the mouse is down
    bool dragging = false;
    int dragFrom = 0, dragTo = 0;

    // The correction curve and the score
    std::vector<float> correction;
    float matchPercent = -1.f;

    juce::Rectangle<int> tagsArea, waveArea, slotsArea, middleArea, curveArea, levelMatchArea, originalArea, referenceArea;
    std::array<juce::Rectangle<int>, ReferenceManager::numSlots> slotAreas, removeAreas;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReferenceView)
};
