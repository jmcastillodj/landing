#include "StereoPanel.h"

namespace
{
    constexpr int numSegments = 41; // an odd number, so that one segment is the middle
    constexpr float deviationRange = 4.f;
    constexpr float balanceRangeDb = 9.f; // the balance at which the triangle leans as far as it goes
}

void StereoPanel::update(float balanceDb, float width, float correlation, float monoDeviationDb)
{
    const float targetBalance = juce::jlimit(-1.f, 1.f, balanceDb / balanceRangeDb);
    const float targetDeviation = juce::jlimit(-deviationRange, deviationRange, monoDeviationDb);

    // A little smoothing, as the triangle would jitter at the rate of the frames otherwise
    constexpr float k = 0.35f;
    const float newBalance = balance + k * (targetBalance - balance);
    const float newWidth = widthShown + k * (width - widthShown);
    const float newCorrelation = correlationShown + k * (correlation - correlationShown);
    const float newDeviation = deviationShown + k * (targetDeviation - deviationShown);

    const bool changed = std::abs(newBalance - balance) > 0.002f || std::abs(newWidth - widthShown) > 0.002f
                         || std::abs(newCorrelation - correlationShown) > 0.004f || std::abs(newDeviation - deviationShown) > 0.02f;

    balance = newBalance;
    widthShown = newWidth;
    correlationShown = newCorrelation;
    deviationShown = newDeviation;

    if (changed)
        repaint();
}

void StereoPanel::paint(juce::Graphics& g)
{
    g.fillAll(Theme::displayBottom);

    auto area = getLocalBounds().toFloat().reduced(12.f, 6.f);
    const float barHeight = 50.f;

    auto deviationArea = area.removeFromBottom(barHeight);
    area.removeFromBottom(4.f);
    auto correlationArea = area.removeFromBottom(barHeight);
    area.removeFromBottom(4.f);

    if (area.getHeight() >= 80.f)
        paintTriangle(g, area);

    paintBar(g, correlationArea, "Correlation", "L/R", true);
    paintBar(g, deviationArea, "Mono Deviation (S)", "LU", false);
}

void StereoPanel::paintTriangle(juce::Graphics& g, juce::Rectangle<float> area)
{
    // The half circle: a circle whose lower part is cut off by a flat base
    // Its height is the circle down to the base, 1.8 radii, with a margin above and a line of text below
    const float radius = juce::jmin((area.getHeight() - 26.f) / 1.8f, area.getWidth() * 0.34f);
    if (radius < 20.f)
        return;

    const auto centre = juce::Point<float>(area.getCentreX(), area.getY() + radius + 8.f);
    const float baseY = centre.y + radius * 0.8f;

    if (baseY > area.getBottom() + 1.f)
        return;

    const float cutAngle = std::acos(0.8f); // where the circle meets the base, from straight down

    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.f, cutAngle, juce::MathConstants<float>::twoPi - cutAngle, true);

    g.setColour(Theme::gridStrong);
    g.strokePath(arc, juce::PathStrokeType(1.f));
    g.fillRect(centre.x - radius * 0.6f, baseY, radius * 1.2f, 1.f);

    // The tick at the top, where the triangle points when the sound is in the middle
    g.fillRect(centre.x - 0.5f, centre.y - radius - 3.f, 1.f, 5.f);

    g.setFont(Theme::font(juce::jlimit(10.f, 13.f, radius * 0.22f)));
    g.setColour(Theme::textDim);
    const float fontHeight = juce::jlimit(10.f, 13.f, radius * 0.22f);
    g.drawText("Left", juce::Rectangle<float>(area.getX(), centre.y - fontHeight, centre.x - radius - area.getX() - 2.f, fontHeight * 2.f), juce::Justification::centredRight);
    g.drawText("Right", juce::Rectangle<float>(centre.x + radius + 2.f, centre.y - fontHeight, area.getRight() - centre.x - radius - 2.f, fontHeight * 2.f), juce::Justification::centredLeft);
    g.drawText("Mid/Side", juce::Rectangle<float>(centre.x - 40.f, baseY + 1.f, 80.f, fontHeight + 2.f), juce::Justification::centred);

    // The apex goes round the circle with the balance, up to 60 degrees to either side, and the base takes the width
    const float angle = balance * juce::degreesToRadians(60.f);
    const juce::Point<float> apex(centre.x + radius * std::sin(angle), centre.y - radius * std::cos(angle));
    const float halfBase = juce::jmax(2.f, widthShown * radius * 0.78f);
    const float baseCentre = centre.x + balance * radius * 0.12f;

    juce::Path triangle;
    triangle.startNewSubPath(apex);
    triangle.lineTo(baseCentre + halfBase, baseY - 1.f);
    triangle.lineTo(baseCentre - halfBase, baseY - 1.f);
    triangle.closeSubPath();

    g.setColour(Theme::accent.withAlpha(0.14f));
    g.fillPath(triangle);
    g.setColour(Theme::accent);
    g.strokePath(triangle, juce::PathStrokeType(2.f, juce::PathStrokeType::mitered));
}

void StereoPanel::paintBar(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, const juce::String& unit, bool isCorrelation)
{
    g.setFont(Theme::font(13.f));
    g.setColour(Theme::text);
    g.drawText(title, area.removeFromTop(16.f), juce::Justification::centredLeft);

    auto unitArea = area.removeFromRight(26.f);
    auto scale = area.removeFromTop(16.f);
    auto bar = area.withHeight(12.f);

    g.setFont(Theme::font(11.f));
    g.setColour(Theme::textDim);
    g.drawText(unit, unitArea.withY(bar.getY() - 2.f).withHeight(16.f), juce::Justification::centredRight);

    const float segmentWidth = bar.getWidth() / (float)numSegments;
    const int middle = numSegments / 2;

    auto centreOf = [&](int segment) { return bar.getX() + ((float)segment + 0.5f) * segmentWidth; };

    // The scale: labels where the numbers are, and a dot for the steps in between
    if (isCorrelation)
    {
        const int labelAt[] { 0, middle, numSegments - 1 };
        const juce::String labels[] { juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")), "0", "+" };
        for (int i = 0; i < 3; ++i)
            g.drawText(labels[i], juce::Rectangle<float>(40.f, 12.f).withCentre({ centreOf(labelAt[i]), scale.getY() + 5.f }), juce::Justification::centred);

        for (int segment = 0; segment < numSegments; segment += 5)
            g.fillRect(centreOf(segment) - 0.75f, scale.getBottom() - (segment == middle ? 5.f : 3.f), 1.5f, segment == middle ? 5.f : 3.f);
    }
    else
    {
        for (int step = -4; step <= 4; ++step)
        {
            const int segment = middle + step * 5;
            const bool labelled = step % 2 == 0;
            if (labelled)
            {
                const juce::String text = step == 0 ? "0" : step > 0 ? "+" + juce::String(step) : juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) + juce::String(-step);
                g.drawText(text, juce::Rectangle<float>(40.f, 12.f).withCentre({ centreOf(segment), scale.getY() + 5.f }), juce::Justification::centred);
            }
            g.fillRect(centreOf(segment) - 0.75f, scale.getBottom() - (step == 0 ? 5.f : 3.f), 1.5f, step == 0 ? 5.f : 3.f);
        }
    }

    // The segments
    const int valueSegment = isCorrelation ? middle + juce::roundToInt(correlationShown * (float)middle)
                                           : middle + juce::roundToInt(deviationShown / deviationRange * (float)middle);

    for (int segment = 0; segment < numSegments; ++segment)
    {
        const auto rect = juce::Rectangle<float>(bar.getX() + (float)segment * segmentWidth + 0.5f, bar.getY(), segmentWidth - 1.f, bar.getHeight());
        juce::Colour colour = Theme::track;

        if (segment == middle)
        {
            colour = Theme::held;
        }
        else if (isCorrelation)
        {
            const bool between = valueSegment >= middle ? (segment > middle && segment <= valueSegment) : (segment < middle && segment >= valueSegment);
            if (between)
                colour = segment > middle ? Theme::accent : Theme::second;
        }
        else if (segment == valueSegment)
        {
            colour = deviationShown < -3.f ? Theme::over : deviationShown < -1.5f ? Theme::warn : Theme::meterLight;
        }

        g.setColour(colour);
        g.fillRect(rect);
    }
}
