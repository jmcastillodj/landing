// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

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

    // The width is measured when the name is set, not every time it is asked for, which is every frame
    void setName(const juce::String& newName) { name = newName; idealWidth = Theme::textWidth(font(), name) + 34; }
    int getIdealWidth() const { return idealWidth; }

    // The positions are in the coordinates of the component that holds the handle
    std::function<void()> onStart;
    std::function<void(juce::Point<int>)> onMove;
    std::function<void(juce::Point<int>)> onEnd;
    std::function<void()> onCancel; // a click that was not a drag

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

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Only the primary button lifts a view
        if (e.mods.isPopupMenu() || !e.mods.isLeftButtonDown())
            return;

        dragging = true;
        repaint();
        if (onStart)
            onStart();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging && onMove)
            onMove(e.getEventRelativeTo(getParentComponent()).getPosition());
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!dragging)
            return;

        dragging = false;
        repaint();

        // A click that did not move leaves everything as it was
        if (!e.mouseWasDraggedSinceMouseDown() || e.getDistanceFromDragStart() < 6)
        {
            if (onCancel)
                onCancel();
            return;
        }

        if (onEnd)
            onEnd(e.getEventRelativeTo(getParentComponent()).getPosition());
    }

private:
    static juce::Font font() { return Theme::font(10.5f, true).withExtraKerningFactor(0.05f); }

    juce::String name;
    int idealWidth = 34;
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
