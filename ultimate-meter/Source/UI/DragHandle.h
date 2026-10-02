#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
// The grip at the top of a view in multi mode. Dragging it lifts the view to another place in the layout.
class DragHandle : public juce::Component
{
public:
    DragHandle()
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void setName(const juce::String& newName) { name = newName; }
    int getIdealWidth() const { return Theme::textWidth(font(), name) + 34; }

    // The positions are in the coordinates of the component that holds the handle
    std::function<void()> onStart;
    std::function<void(juce::Point<int>)> onMove;
    std::function<void(juce::Point<int>)> onEnd;

    void paint(juce::Graphics& g) override
    {
        const bool active = hovered || dragging;
        auto bounds = getLocalBounds().toFloat().reduced(0.5f);

        g.setColour(Theme::chromeInsetTop.withAlpha(active ? 0.96f : 0.72f));
        g.fillRoundedRectangle(bounds, 4.f);
        g.setColour((active ? Theme::accent : Theme::panelEdge).withAlpha(active ? 0.9f : 0.6f));
        g.drawRoundedRectangle(bounds, 4.f, 1.f);

        // Two columns of three dots, as a grip
        g.setColour(active ? Theme::text : Theme::textDim);
        for (int column = 0; column < 2; ++column)
            for (int row = 0; row < 3; ++row)
                g.fillEllipse(8.f + (float)column * 4.f, bounds.getCentreY() - 5.f + (float)row * 4.f, 2.f, 2.f);

        g.setFont(font());
        g.drawText(name, getLocalBounds().withTrimmedLeft(22).withTrimmedRight(6), juce::Justification::centredLeft);
    }

    void mouseEnter(const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hovered = false; repaint(); }

    void mouseDown(const juce::MouseEvent&) override
    {
        dragging = true;
        repaint();
        if (onStart)
            onStart();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (onMove)
            onMove(e.getEventRelativeTo(getParentComponent()).getPosition());
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        dragging = false;
        repaint();
        if (onEnd)
            onEnd(e.getEventRelativeTo(getParentComponent()).getPosition());
    }

private:
    static juce::Font font() { return Theme::font(10.5f, true).withExtraKerningFactor(0.05f); }

    juce::String name;
    bool hovered = false, dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DragHandle)
};

//==============================================================================
// Shows where the view that is being dragged would land: a band in the place that it would take
class DropOverlay : public juce::Component
{
public:
    DropOverlay() { setInterceptsMouseClicks(false, false); }

    void show(juce::Rectangle<int> newZone, const juce::String& newText)
    {
        zone = newZone;
        text = newText;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (zone.isEmpty())
            return;

        auto r = zone.toFloat().reduced(2.f);
        g.setColour(Theme::accent.withAlpha(0.22f));
        g.fillRoundedRectangle(r, 5.f);
        g.setColour(Theme::accent);
        g.drawRoundedRectangle(r, 5.f, 2.f);

        if (text.isNotEmpty())
        {
            g.setFont(Theme::font(12.f, true));
            g.setColour(juce::Colours::white);
            g.drawText(text, zone, juce::Justification::centred);
        }
    }

private:
    juce::Rectangle<int> zone;
    juce::String text;
};
