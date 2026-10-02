#pragma once

#include <JuceHeader.h>

//==============================================================================
// One row of the multi-view layout: some views side by side, with a divider between each two that can
// be dragged to give one more room and the other less. The rows themselves are stacked by the editor
// with dividers of their own, so the views can be arranged in rows and columns of any size.
class MultiRow : public juce::Component
{
public:
    static constexpr int dividerThickness = 7;
    static constexpr int minViewWidth = 170;

    MultiRow() = default;

    // Takes the views into the row, in order. The row does not own them.
    void setViews(const std::vector<juce::Component*>& newViews)
    {
        dividers.clear();
        items.clear();
        layout.clearAllItems();

        int index = 0;
        for (size_t i = 0; i < newViews.size(); ++i)
        {
            addAndMakeVisible(*newViews[i]);

            // Every view starts with an equal share, and can be dragged to any share above its minimum
            layout.setItemLayout(index++, minViewWidth, -1.0, -1.0 / (double)newViews.size());
            items.push_back(newViews[i]);

            if (i + 1 < newViews.size())
            {
                layout.setItemLayout(index, dividerThickness, dividerThickness, dividerThickness);
                dividers.push_back(std::make_unique<juce::StretchableLayoutResizerBar>(&layout, index, true));
                addAndMakeVisible(*dividers.back());
                items.push_back(dividers.back().get());
                ++index;
            }
        }

        resized();
    }

    void resized() override
    {
        if (!items.empty() && !getLocalBounds().isEmpty())
            layout.layOutComponents(items.data(), (int)items.size(), 0, 0, getWidth(), getHeight(), false, true);
    }

private:
    juce::StretchableLayoutManager layout;
    std::vector<std::unique_ptr<juce::StretchableLayoutResizerBar>> dividers;
    std::vector<juce::Component*> items;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiRow)
};
