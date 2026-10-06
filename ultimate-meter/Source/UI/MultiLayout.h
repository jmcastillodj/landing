#pragma once

#include <JuceHeader.h>

//==============================================================================
// A column of the multi-view layout: one view, or several stacked, with a divider between each two that
// can be dragged to give one more height and the other less.
class MultiCell : public juce::Component
{
public:
    static constexpr int dividerThickness = 7;
    static constexpr int minViewHeight = 70;

    MultiCell() = default;

    // Takes the views into the cell, from the top. Heights say how much of the cell each view gets, relative
    // to the others; a view without one (0) gets an equal share.
    void setViews(const std::vector<juce::Component*>& newViews, const std::vector<double>& heights = {})
    {
        views = newViews;
        dividers.clear();
        items.clear();
        layout.clearAllItems();

        const auto shares = sharesOf(heights, newViews.size());

        int index = 0;
        for (size_t i = 0; i < newViews.size(); ++i)
        {
            addAndMakeVisible(*newViews[i]);
            layout.setItemLayout(index++, minViewHeight, -1.0, -shares[i]);
            items.push_back(newViews[i]);

            if (i + 1 < newViews.size())
            {
                layout.setItemLayout(index, dividerThickness, dividerThickness, dividerThickness);
                dividers.push_back(std::make_unique<juce::StretchableLayoutResizerBar>(&layout, index, false));
                addAndMakeVisible(*dividers.back());
                items.push_back(dividers.back().get());
                ++index;
            }
        }

        resized();
    }

    const std::vector<juce::Component*>& getViews() const { return views; }

    // Raw weights made into shares that add up to one. A weight of 0 is not known yet, and takes the average of the others.
    static std::vector<double> sharesOf(const std::vector<double>& raw, size_t count)
    {
        std::vector<double> shares(count, 0.0);
        double known = 0.0;
        size_t numKnown = 0;

        for (size_t i = 0; i < count; ++i)
            if (i < raw.size() && raw[i] > 0.0)
            {
                shares[i] = raw[i];
                known += raw[i];
                ++numKnown;
            }

        const double fill = numKnown > 0 ? known / (double)numKnown : 1.0;
        double sum = 0.0;
        for (auto& share : shares)
        {
            if (share <= 0.0)
                share = fill;
            sum += share;
        }
        for (auto& share : shares)
            share /= sum;

        return shares;
    }

    void resized() override
    {
        if (!items.empty() && !getLocalBounds().isEmpty())
            layout.layOutComponents(items.data(), (int)items.size(), 0, 0, getWidth(), getHeight(), true, true);
    }

private:
    juce::StretchableLayoutManager layout;
    std::vector<std::unique_ptr<juce::StretchableLayoutResizerBar>> dividers;
    std::vector<juce::Component*> items, views;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiCell)
};

//==============================================================================
// One row of the multi-view layout: some columns side by side, each a view or a stack of views, with a divider
// between each two columns that can be dragged to give one more room and the other less. The rows themselves
// are stacked by the editor with dividers of their own, so the views can be arranged in any grid of any size.
class MultiRow : public juce::Component
{
public:
    static constexpr int dividerThickness = 7;
    static constexpr int minViewWidth = 170;

    MultiRow() = default;

    // widths say how much of the row each column gets, and heights how much of its column each view of it gets
    void setCells(const std::vector<std::vector<juce::Component*>>& cellViews, const std::vector<double>& widths,
                  const std::vector<std::vector<double>>& heights)
    {
        cells.clear();
        dividers.clear();
        items.clear();
        layout.clearAllItems();

        const auto shares = MultiCell::sharesOf(widths, cellViews.size());

        int index = 0;
        for (size_t i = 0; i < cellViews.size(); ++i)
        {
            cells.push_back(std::make_unique<MultiCell>());
            addAndMakeVisible(*cells.back());
            cells.back()->setViews(cellViews[i], i < heights.size() ? heights[i] : std::vector<double> {});

            layout.setItemLayout(index++, minViewWidth, -1.0, -shares[i]);
            items.push_back(cells.back().get());

            if (i + 1 < cellViews.size())
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

    const std::vector<std::unique_ptr<MultiCell>>& getCells() const { return cells; }

    void resized() override
    {
        if (!items.empty() && !getLocalBounds().isEmpty())
            layout.layOutComponents(items.data(), (int)items.size(), 0, 0, getWidth(), getHeight(), false, true);
    }

private:
    juce::StretchableLayoutManager layout;
    std::vector<std::unique_ptr<MultiCell>> cells;
    std::vector<std::unique_ptr<juce::StretchableLayoutResizerBar>> dividers;
    std::vector<juce::Component*> items;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiRow)
};
