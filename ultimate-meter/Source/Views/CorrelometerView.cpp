#include "CorrelometerView.h"
#include "../Parameters.h"

namespace
{
    const juce::Identifier hideProperty { "corrHideControls" };

    juce::String formatFrequency(double hz)
    {
        if (hz >= 1000.0)
        {
            const double k = hz / 1000.0;
            return (k >= 10.0 ? juce::String((int)std::round(k)) : juce::String(k, 1)) + "K";
        }
        return juce::String((int)std::round(hz));
    }
}

CorrelometerView::CorrelometerView(juce::AudioProcessorValueTreeState& a, juce::ValueTree& s) :
    apvts(a), state(s),
    averageKnob(a, Parameters::ID::corrAvgTime, "Avg Time", " ms"),
    bandsKnob(a, Parameters::ID::corrBands, "Bands", "")
{
    setOpaque(true);

    choices[0] = { "Pri", Parameters::ID::corrPrimary, {}, {} };
    choices[1] = { "Sec", Parameters::ID::corrSecondary, {}, {} };
    choices[2] = { "Scale", Parameters::ID::corrScale, {}, {} };
    choices[3] = { "B/width", Parameters::ID::corrBandwidth, {}, {} };

    addAndMakeVisible(averageKnob);
    addAndMakeVisible(bandsKnob);
    averageKnob.setAlpha(1.f);
    bandsKnob.setAlpha(1.f);

    controlsHidden = (bool)state.getProperty(hideProperty, false);
}

void CorrelometerView::setControlsHidden(bool shouldHide)
{
    controlsHidden = shouldHide;
    state.setProperty(hideProperty, shouldHide, nullptr);
    resized();
    repaint();
}

juce::String CorrelometerView::choiceText(const juce::String& parameterID) const
{
    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(parameterID)))
        return parameter->getCurrentChoiceName().toUpperCase();
    return {};
}

void CorrelometerView::update(const MultibandCorrelator& correlator)
{
    numBands = correlator.read(values);
    if (isVisible())
        repaint();
}

//==============================================================================
void CorrelometerView::resized()
{
    auto area = getLocalBounds().reduced(10, 8);
    topArea = area.removeFromTop(28);
    area.removeFromTop(6);

    // The buttons along the top: the title and the switch that hides the controls at the left, the selectors at the right
    const auto font = Theme::font(12.f);
    hideArea = juce::Rectangle<int>(Theme::textWidth(font, "HIDE CONTROLS") + 22, 22).withPosition(topArea.getX() + 118, topArea.getY() + 3);

    auto right = topArea;
    for (int i = 2; i >= 0; --i)
    {
        auto& c = choices[(size_t)i];
        const int buttonWidth = 74;
        c.buttonArea = right.removeFromRight(buttonWidth).withSizeKeepingCentre(buttonWidth, 22);
        const int titleWidth = Theme::textWidth(font, c.title) + 10;
        c.titleArea = right.removeFromRight(titleWidth);
        right.removeFromRight(6);
    }

    if (controlsHidden)
    {
        controlsArea = {};
        averageKnob.setVisible(false);
        bandsKnob.setVisible(false);
    }
    else
    {
        controlsArea = area.removeFromRight(124);
        area.removeFromRight(6);

        auto column = controlsArea;
        const int knobHeight = 118;
        averageKnob.setBounds(column.removeFromTop(knobHeight).withSizeKeepingCentre(108 + 2 * FloatingKnob::shadowMargin - 20, knobHeight));
        bandsKnob.setBounds(column.removeFromTop(knobHeight).withSizeKeepingCentre(108 + 2 * FloatingKnob::shadowMargin - 20, knobHeight));

        auto& bandwidth = choices[3];
        bandwidth.titleArea = column.removeFromTop(20);
        bandwidth.buttonArea = column.removeFromTop(26).reduced(8, 0);
        averageKnob.setVisible(true);
        bandsKnob.setVisible(true);
    }

    plotArea = area;
    barsArea = plotArea.withTrimmedLeft(40).withTrimmedBottom(22).withTrimmedTop(4).withTrimmedRight(2);
}

float CorrelometerView::yOf(float value) const
{
    const int scale = juce::jlimit(0, 3, (int)(apvts.getRawParameterValue(Parameters::ID::corrScale)->load()));
    const float low = Parameters::corrScaleLows[(size_t)scale];
    const float high = Parameters::corrScaleHighs[(size_t)scale];
    return juce::jmap(juce::jlimit(low, high, value), low, high, (float)barsArea.getBottom(), (float)barsArea.getY());
}

//==============================================================================
void CorrelometerView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    const int scale = juce::jlimit(0, 3, (int)(apvts.getRawParameterValue(Parameters::ID::corrScale)->load()));
    const float low = Parameters::corrScaleLows[(size_t)scale];
    const float high = Parameters::corrScaleHighs[(size_t)scale];

    // The top: the name of the meter, the switch for the controls, and the selectors
    g.setFont(Theme::font(14.f));
    g.setColour(Theme::text);
    g.drawText("Correlation", topArea.withWidth(112), juce::Justification::centredLeft);

    auto drawButton = [&](juce::Rectangle<int> area, const juce::String& text, bool lit)
    {
        g.setColour(Theme::track);
        g.fillRoundedRectangle(area.toFloat(), 4.f);
        g.setColour(lit ? Theme::accent : Theme::panelEdge);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.f, 1.f);
        g.setFont(Theme::labelFont());
        g.setColour(lit ? Theme::accent : Theme::text);
        g.drawText(text, area, juce::Justification::centred);
    };

    drawButton(hideArea, controlsHidden ? "SHOW CONTROLS" : "HIDE CONTROLS", false);

    for (int i = 0; i < 3; ++i)
    {
        g.setFont(Theme::font(12.f));
        g.setColour(Theme::textDim);
        g.drawText(choices[(size_t)i].title, choices[(size_t)i].titleArea.withTrimmedRight(6), juce::Justification::centredRight);
        drawButton(choices[(size_t)i].buttonArea, choiceText(choices[(size_t)i].parameterID), false);
    }

    if (!controlsHidden)
    {
        auto& c = choices[3];
        g.setFont(Theme::font(12.f));
        g.setColour(Theme::textDim);
        g.drawText(c.title, c.titleArea, juce::Justification::centred);
        drawButton(c.buttonArea, choiceText(c.parameterID), false);
    }

    // The field: dark green above the line of zero and dark red below it
    {
        const float zero = yOf(juce::jlimit(low, high, 0.f));
        g.setColour(Theme::display.darker(0.5f));
        g.fillRect(barsArea);
        g.setGradientFill(juce::ColourGradient(Theme::meterDeep.withAlpha(0.22f), 0.f, (float)barsArea.getY(), Theme::meterDeep.withAlpha(0.f), 0.f, zero, false));
        g.fillRect(barsArea.toFloat().withBottom(zero));
        if (zero < (float)barsArea.getBottom())
        {
            g.setGradientFill(juce::ColourGradient(Theme::over.withAlpha(0.f), 0.f, zero, Theme::over.withAlpha(0.16f), 0.f, (float)barsArea.getBottom(), false));
            g.fillRect(barsArea.toFloat().withTop(zero));
        }
    }

    // The scale at the left, and the lines across
    {
        const float step = scale == 3 ? 0.05f : 0.25f;
        g.setFont(Theme::font(11.f));
        for (float v = std::ceil(low / step) * step; v <= high + 0.001f; v += step)
        {
            const float y = yOf(v);
            const bool isZero = std::abs(v) < 0.001f;
            g.setColour(isZero ? Theme::gridStrong : Theme::grid);
            g.fillRect((float)barsArea.getX(), y, (float)barsArea.getWidth(), 1.f);
            g.setColour(Theme::textDim);
            g.drawText(juce::String(v, 2), plotArea.getX(), (int)y - 7, 36, 14, juce::Justification::centredRight);
        }
    }

    if (numBands <= 0)
        return;

    // The bars: yellow at the line of zero, green towards +1, and red towards -1, in segments
    const float bandWidth = (float)barsArea.getWidth() / (float)numBands;
    const float zero = yOf(0.f);

    juce::ColourGradient positive(Theme::warn, 0.f, zero, Theme::meterLight, 0.f, yOf(1.f), false);
    juce::ColourGradient negative(Theme::warn, 0.f, zero, Theme::over, 0.f, yOf(-1.f), false);

    for (int band = 0; band < numBands; ++band)
    {
        const float x = (float)barsArea.getX() + (float)band * bandWidth;
        const float value = juce::jlimit(-1.f, 1.f, values[(size_t)band]);
        const float y = yOf(value);

        g.setGradientFill(value >= 0.f ? positive : negative);
        const auto bar = juce::Rectangle<float>(x + 0.5f, juce::jmin(y, zero), juce::jmax(1.f, bandWidth - 1.f), std::abs(y - zero));
        g.fillRect(bar);
    }

    // The segments are fine lines across the bars
    g.setColour(Theme::display.darker(0.7f).withAlpha(0.55f));
    for (float y = (float)barsArea.getBottom(); y > (float)barsArea.getY(); y -= 4.f)
        g.fillRect((float)barsArea.getX(), y, (float)barsArea.getWidth(), 1.f);

    g.setColour(Theme::display.darker(0.7f).withAlpha(0.5f));
    for (int band = 1; band < numBands; ++band)
        g.fillRect((float)barsArea.getX() + (float)band * bandWidth - 0.5f, (float)barsArea.getY(), 1.f, (float)barsArea.getHeight());

    // The frequencies of the bands, at as many of them as there is room for
    g.setFont(Theme::font(11.f));
    g.setColour(Theme::text);
    const int step = juce::jmax(1, (int)std::ceil(34.f / bandWidth));
    for (int band = 0; band < numBands; band += step)
    {
        const float centre = (float)barsArea.getX() + ((float)band + 0.5f) * bandWidth;
        g.drawText(formatFrequency(MultibandCorrelator::centreOf(band, numBands)), juce::Rectangle<int>(48, 16).withCentre({ (int)centre, barsArea.getBottom() + 11 }), juce::Justification::centred);
    }
}

//==============================================================================
void CorrelometerView::showChoiceMenu(const juce::String& parameterID, juce::Rectangle<int> area)
{
    auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(parameterID));
    if (parameter == nullptr)
        return;

    juce::PopupMenu menu;
    for (int i = 0; i < parameter->choices.size(); ++i)
        menu.addItem(parameter->choices[i], true, parameter->getIndex() == i, [parameter, i]
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)i));
            parameter->endChangeGesture();
        });

    menu.setLookAndFeel(&getLookAndFeel());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(area)));
}

void CorrelometerView::mouseMove(const juce::MouseEvent& e)
{
    bool hand = hideArea.contains(e.getPosition());
    for (size_t i = 0; i < choices.size(); ++i)
        if (!(i == 3 && controlsHidden) && choices[i].buttonArea.contains(e.getPosition()))
            hand = true;

    setMouseCursor(hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void CorrelometerView::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (hideArea.contains(e.getPosition()))
    {
        setControlsHidden(!controlsHidden);
        return;
    }

    for (size_t i = 0; i < choices.size(); ++i)
        if (!(i == 3 && controlsHidden) && choices[i].buttonArea.contains(e.getPosition()))
        {
            showChoiceMenu(choices[i].parameterID, choices[i].buttonArea);
            return;
        }
}
