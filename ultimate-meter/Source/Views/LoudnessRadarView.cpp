#include "LoudnessRadarView.h"

namespace
{
    juce::String formatLu(float lu, bool valid)
    {
        if (!valid)
            return "-";

        const auto text = juce::String(lu, 1);
        return lu > 0.05f ? "+" + text : text;
    }

    juce::String formatTime(double seconds)
    {
        const int total = (int)seconds;
        return juce::String::formatted("%02d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    }
}

LoudnessRadarView::LoudnessRadarView()
{
    setOpaque(true);
    slots.fill(silenceLevel);
}

void LoudnessRadarView::clearHistory()
{
    slots.fill(silenceLevel);
    writeIndex = 0;
    secondsInSlot = 0.0;
    secondsListened = 0.0;
    ringLevel = ringPeak = silenceLevel;
    repaint();
}

void LoudnessRadarView::update(const LoudnessMeter::Readings& newReadings, float targetLufs, float turnSeconds, bool useShortTerm,
                               float elapsedSeconds, bool readoutDue)
{
    readings = newReadings;
    target = targetLufs;
    turnTime = juce::jmax(1.f, turnSeconds);

    const float value = useShortTerm ? readings.shortTerm : readings.momentary;
    const bool valid = std::isfinite(value) && value > -150.f;
    const double slotSeconds = (double)turnTime / numSlots;

    if (elapsedSeconds > 0.f)
    {
        secondsListened += (double)elapsedSeconds;
        secondsInSlot += (double)elapsedSeconds;

        while (secondsInSlot >= slotSeconds)
        {
            secondsInSlot -= slotSeconds;
            writeIndex = (writeIndex + 1) % numSlots;
            slots[(size_t)writeIndex] = silenceLevel;
        }

        // A slot keeps the loudest the moment had
        if (valid)
            slots[(size_t)writeIndex] = juce::jmax(slots[(size_t)writeIndex], value);

        // The ring of lights follows the momentary loudness, up at once and down at a steady rate
        const float momentary = std::isfinite(readings.momentary) ? readings.momentary : silenceLevel;
        ringLevel = momentary > ringLevel ? momentary : juce::jmax(momentary, ringLevel - 30.f * elapsedSeconds);
        ringPeak = juce::jmax(ringLevel, ringPeak - 6.f * elapsedSeconds);
    }
    else
    {
        ringLevel = ringPeak = silenceLevel;
    }

    if (readoutDue)
        shown = readings;

    repaint();
}

void LoudnessRadarView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    const auto bounds = getLocalBounds().toFloat().reduced(8.f);
    if (bounds.getWidth() < 60.f || bounds.getHeight() < 60.f)
        return;

    const float side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();
    const float ringOuter = side * 0.5f;                    // the lights
    const float ringInner = ringOuter - juce::jmax(5.f, side * 0.028f);
    const float outer = ringInner - juce::jmax(4.f, side * 0.03f); // the radar
    const float inner = outer * 0.17f;                       // the hole in the middle

    const float reference = referenceLufs();
    auto radiusOf = [&](float lu)
    {
        return inner + (outer - inner) * juce::jlimit(0.f, 1.f, (lu - lowestLu) / (highestLu - lowestLu));
    };
    auto pointAt = [&](float radius, float angle)
    {
        return juce::Point<float>(centre.x + radius * std::sin(angle), centre.y - radius * std::cos(angle));
    };

    // The disc, and its rings every 6 LU, with the target's ring stronger
    g.setColour(Theme::display.darker(0.4f));
    g.fillEllipse(centre.x - outer, centre.y - outer, outer * 2.f, outer * 2.f);

    // The shape: from the point after the beam's gap, round to the beam
    const int gap = juce::jmax(4, numSlots / 28);
    juce::Path shape;
    bool started = false;
    std::vector<juce::Point<float>> innerPoints;

    for (int step = gap; step < numSlots; ++step)
    {
        const int index = (writeIndex + 1 + step) % numSlots;
        const float level = slots[(size_t)index];
        const float angle = juce::MathConstants<float>::twoPi * ((float)index + 0.5f) / (float)numSlots;
        const float lu = level <= silenceLevel + 1.f ? lowestLu : level - reference;
        const auto p = pointAt(radiusOf(lu), angle);

        if (!started)
        {
            shape.startNewSubPath(pointAt(inner, angle));
            shape.lineTo(p);
            started = true;
        }
        else
        {
            shape.lineTo(p);
        }
        innerPoints.push_back(pointAt(inner, angle));
    }

    if (started)
    {
        for (auto it = innerPoints.rbegin(); it != innerPoints.rend(); ++it)
            shape.lineTo(*it);
        shape.closeSubPath();

        // Blue where it is quiet, green up to the target, yellow above it, and red well over
        auto stop = [&](float lu) { return (double)juce::jlimit(0.f, 1.f, (radiusOf(lu) - inner) / (outer - inner)); };
        juce::ColourGradient fill(Theme::accentDeep, centre, Theme::over, pointAt(outer, 0.f), true);
        fill.clearColours();
        fill.addColour(0.0, Theme::accentDeep);
        fill.addColour(stop(-24.f), Theme::accent);
        fill.addColour(stop(-12.f), Theme::meterMid);
        fill.addColour(stop(-0.5f), Theme::meterLight);
        fill.addColour(stop(0.5f), Theme::warn);
        fill.addColour(stop(4.f), Theme::second);
        fill.addColour(stop(9.f), Theme::over);
        fill.addColour(1.0, Theme::over);

        // The hole at the centre is cut out of the fill by drawing it over after
        g.setGradientFill(fill);
        g.fillPath(shape);
    }

    // Rings and spokes over the shape, fine and cool
    for (float lu = lowestLu + 6.f; lu <= highestLu + 0.1f; lu += 6.f)
    {
        const bool strong = std::abs(lu) < 0.1f;
        g.setColour((strong ? Theme::accent : Theme::gridStrong).withAlpha(strong ? 0.9f : 0.75f));
        const float r = radiusOf(lu);
        g.drawEllipse(centre.x - r, centre.y - r, r * 2.f, r * 2.f, strong ? 1.6f : 1.f);
    }
    g.setColour(Theme::gridStrong.withAlpha(0.75f));
    for (int spoke = 0; spoke < 12; ++spoke)
    {
        const float angle = juce::MathConstants<float>::twoPi * (float)spoke / 12.f;
        g.drawLine({ pointAt(inner, angle), pointAt(outer, angle) }, 1.f);
    }
    g.drawEllipse(centre.x - outer, centre.y - outer, outer * 2.f, outer * 2.f, 1.f);

    // The hole
    g.setColour(Theme::displayTop.darker(0.6f));
    g.fillEllipse(centre.x - inner, centre.y - inner, inner * 2.f, inner * 2.f);
    g.setColour(Theme::gridStrong);
    g.drawEllipse(centre.x - inner, centre.y - inner, inner * 2.f, inner * 2.f, 1.f);

    // The beam, which is where the turn is now
    const float beamAngle = juce::MathConstants<float>::twoPi * ((float)writeIndex + 1.f) / (float)numSlots;
    g.setColour(Theme::held.withAlpha(0.9f));
    g.drawLine({ pointAt(inner, beamAngle), pointAt(outer, beamAngle) }, 1.6f);

    // A trail behind the beam that fades with the gap
    {
        juce::Path wedge;
        const float trail = juce::MathConstants<float>::twoPi * (float)gap / (float)numSlots;
        wedge.startNewSubPath(pointAt(inner, beamAngle));
        wedge.lineTo(pointAt(outer, beamAngle));
        const int pieces = 12;
        for (int i = 1; i <= pieces; ++i)
            wedge.lineTo(pointAt(outer, beamAngle + trail * (float)i / (float)pieces));
        wedge.lineTo(pointAt(inner, beamAngle + trail));
        wedge.closeSubPath();

        g.setGradientFill(juce::ColourGradient(Theme::accent.withAlpha(0.28f), pointAt(outer, beamAngle), Theme::accent.withAlpha(0.f),
                                               pointAt(outer, beamAngle + trail), false));
        g.fillPath(wedge);
    }

    // The ring of lights: ticks from -36 LU at the bottom to +18 at the right, lit up to the loudness of the moment
    const float stepLu = 0.5f;
    const float lightsLu = std::isfinite(ringLevel) && ringLevel > -150.f ? ringLevel - reference : -1000.f;
    const float peakLu = std::isfinite(ringPeak) && ringPeak > -150.f ? ringPeak - reference : -1000.f;
    const float tickHalf = juce::degreesToRadians(stepLu * 5.f) * 0.32f;

    for (float lu = -36.f; lu < 18.f; lu += stepLu)
    {
        const float angle = ringAngle(lu + stepLu * 0.5f);
        const bool lit = lu + stepLu * 0.5f <= lightsLu;
        const bool isPeak = peakLu > -500.f && peakLu >= lu && peakLu < lu + stepLu;

        juce::Colour colour = lu < -18.f ? Theme::accent : lu < 0.f ? Theme::meterLight : lu < 6.f ? Theme::warn : Theme::over;
        if (!lit && !isPeak)
            colour = Theme::gridStrong.withAlpha(0.8f);
        else if (!lit)
            colour = Theme::held;

        juce::Path tick;
        tick.startNewSubPath(pointAt(ringInner, angle - tickHalf));
        tick.lineTo(pointAt(ringOuter, angle - tickHalf));
        tick.lineTo(pointAt(ringOuter, angle + tickHalf));
        tick.lineTo(pointAt(ringInner, angle + tickHalf));
        tick.closeSubPath();
        g.setColour(colour);
        g.fillPath(tick);
    }

    // The figures of the scale of the ring
    {
        const float fontHeight = juce::jlimit(9.f, 14.f, side * 0.04f);
        g.setFont(Theme::font(fontHeight));
        g.setColour(Theme::textDim);

        for (int lu = -36; lu <= 18; lu += 6)
        {
            if (side < 220.f && lu % 12 != 0)
                continue;

            const auto p = pointAt(ringOuter + fontHeight * 1.05f, ringAngle((float)lu));
            const juce::String text = lu > 0 ? "+" + juce::String(lu) : juce::String(lu);
            const auto box = juce::Rectangle<float>(40.f, fontHeight + 4.f).withCentre(p);

            if (box.getX() >= 0.f && box.getRight() <= (float)getWidth() && box.getY() >= 0.f && box.getBottom() <= (float)getHeight())
                g.drawText(text, box, juce::Justification::centred);
        }
    }

    // The range and the integrated loudness in the corners, in the colours of the signal, and the clock
    const bool hasIntegrated = std::isfinite(shown.integrated) && shown.integrated > -150.f;
    const bool hasRange = shown.range > 0.f;
    const float big = juce::jlimit(16.f, 40.f, side * 0.1f);
    const float small = juce::jlimit(9.f, 13.f, side * 0.032f);
    auto area = getLocalBounds().toFloat().reduced(10.f, 8.f);
    const float cornerWidth = juce::jmax(60.f, juce::jmin(area.getWidth() * 0.5f - side * 0.28f, 150.f));

    if (area.getWidth() > side + 90.f || area.getHeight() > side + 10.f || side > 160.f)
    {
        const float lineHeight = big + small + 6.f;
        const auto bottom = area.removeFromBottom(lineHeight);

        auto left = bottom.withWidth(cornerWidth);
        g.setFont(Theme::font(big));
        g.setColour(Theme::warn);
        g.drawText(hasRange ? juce::String(shown.range, 1) : "-", left.removeFromTop(big + 2.f), juce::Justification::centredLeft);
        g.setFont(Theme::labelFont());
        g.setColour(Theme::textDim);
        g.drawText("LOUDNESS RANGE (LRA)", left, juce::Justification::centredLeft);

        auto right = bottom.withLeft(bottom.getRight() - cornerWidth);
        g.setFont(Theme::font(big));
        g.setColour(Theme::warn);
        g.drawText(formatLu(shown.integrated - reference, hasIntegrated), right.removeFromTop(big + 2.f), juce::Justification::centredRight);
        g.setFont(Theme::labelFont());
        g.setColour(Theme::textDim);
        g.drawText("PROGRAM LOUDNESS (I)", right, juce::Justification::centredRight);
    }

    // Top corners: what the zero is, and the time, with the turn that the radar is on
    {
        const auto top = getLocalBounds().toFloat().reduced(10.f, 8.f).removeFromTop(small * 2.6f);
        g.setFont(Theme::labelFont());
        g.setColour(Theme::textDim);
        g.drawText("0 LU = " + juce::String(reference, 0) + " LUFS", top.withWidth(cornerWidth + 40.f), juce::Justification::topLeft);

        auto right = top.withLeft(top.getRight() - cornerWidth);
        g.setFont(Theme::font(small + 3.f));
        g.setColour(Theme::text);
        g.drawText(formatTime(secondsListened), right.removeFromTop(small + 6.f), juce::Justification::centredRight);
        g.setFont(Theme::labelFont());
        g.setColour(Theme::textDim);
        g.drawText("LU", right, juce::Justification::topRight);
    }
}
