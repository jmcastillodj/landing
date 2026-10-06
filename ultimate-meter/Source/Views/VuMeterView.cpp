#include "VuMeterView.h"

namespace
{
    juce::String format(float value)
    {
        if (value < -99.f)
            return juce::String(juce::CharPointer_UTF8("\xe2\x88\x92\xe2\x88\x9e"));
        return juce::String(value, 1).replace("-", juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")));
    }
}

VuMeterView::VuMeterView(juce::AudioProcessorValueTreeState& a) : apvts(a)
{
    setOpaque(true);
}

void VuMeterView::scaleRange(int mode, float& low, float& high) const
{
    switch (mode)
    {
        case VuMeterEngine::vu:  low = -20.f; high = 3.f; break;
        case VuMeterEngine::rms: low = -20.f; high = 8.f; break;
        default:                 low = -12.f; high = 12.f; break;
    }
}

void VuMeterView::resized()
{
    auto area = getLocalBounds().reduced(14, 10);
    auto top = area.removeFromTop(24);
    badgeArea = top.removeFromRight(96).withSizeKeepingCentre(96, 22);
    infoArea = top;
    area.removeFromTop(6);

    const int display = juce::roundToInt(parameter(Parameters::ID::vuDisplay));
    numFaces = display == VuMeterEngine::single ? 1 : 2;

    if (numFaces == 1)
    {
        faces[0].bounds = area.withSizeKeepingCentre(juce::jmin(area.getWidth(), (int)((float)area.getHeight() * 1.9f)), area.getHeight());
    }
    else
    {
        auto left = area.removeFromLeft((area.getWidth() - 10) / 2);
        area.removeFromLeft(10);
        faces[0].bounds = left;
        faces[1].bounds = area;
    }
}

void VuMeterView::update(const VuMeterEngine::Readings& readings, float elapsedSeconds)
{
    const int display = juce::roundToInt(parameter(Parameters::ID::vuDisplay));
    const int wanted = display == VuMeterEngine::single ? 1 : 2;
    if (wanted != numFaces)
        resized();

    const float clipLevel = parameter(Parameters::ID::vuClipLevel);

    for (int i = 0; i < 2; ++i)
    {
        auto& face = faces[(size_t)i];
        face.needle = readings.value[(size_t)i];

        // The held needle falls slowly, so that it shows the highest of the last moments
        face.hold = juce::jmax(face.needle, face.hold - 2.f * elapsedSeconds);

        face.heldPeakDb = juce::jmax(readings.peakDb[(size_t)i], face.heldPeakDb);
        face.peakDb = readings.peakDb[(size_t)i];

        face.orangeSeconds = juce::jmax(0.0, face.orangeSeconds - (double)elapsedSeconds);
        face.redSeconds = juce::jmax(0.0, face.redSeconds - (double)elapsedSeconds);

        if (readings.peakDb[(size_t)i] >= clipLevel)
            face.orangeSeconds = 1.5;
        if (readings.peakDb[(size_t)i] >= -0.05f)
            face.redSeconds = 3.0;
    }

    if (isVisible())
        repaint();
}

void VuMeterView::paintFace(juce::Graphics& g, const Face& face, int index)
{
    const int mode = juce::roundToInt(parameter(Parameters::ID::vuMode));
    const float calibration = Parameters::valueAt(Parameters::vuCalibrationsDb, juce::roundToInt(parameter(Parameters::ID::vuCalibration)));
    const bool showHold = parameter(Parameters::ID::vuHold) > 0.5f;
    const bool showNumbers = parameter(Parameters::ID::vuNumbers) > 0.5f;
    const int display = juce::roundToInt(parameter(Parameters::ID::vuDisplay));

    float low, high;
    scaleRange(mode, low, high);

    const auto bounds = face.bounds.toFloat();
    if (bounds.getWidth() < 80.f || bounds.getHeight() < 70.f)
        return;

    // The face
    g.setGradientFill(juce::ColourGradient(Theme::displayTop.brighter(0.1f), 0.f, bounds.getY(), Theme::displayBottom.darker(0.3f), 0.f, bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 8.f);
    g.setColour(Theme::panelEdge.withAlpha(0.7f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.f, 1.f);

    // The pivot of the needle is below the face. The arc takes the height that there is, and the sweep about the vertical
    // is as wide as the face lets it be.
    const float readoutHeight = showNumbers ? juce::jlimit(22.f, 34.f, bounds.getHeight() * 0.14f) : 0.f;
    const float pivotX = bounds.getCentreX();
    const float radius = juce::jmax(40.f, (bounds.getHeight() - readoutHeight - 40.f) * 0.8f / 0.9f);
    const float arcTop = bounds.getY() + 34.f;
    const float pivotY = arcTop + radius * 0.9f;
    const float halfSweep = juce::jlimit(30.f, 58.f, juce::radiansToDegrees(std::asin(juce::jlimit(0.1f, 1.f, 0.44f * bounds.getWidth() / (radius * 0.9f)))));

    auto angleOf = [&](float value)
    {
        const float proportion = juce::jlimit(-0.04f, 1.04f, (value - low) / (high - low));
        return juce::degreesToRadians(-halfSweep + 2.f * halfSweep * proportion);
    };
    auto pointAt = [&](float r, float angle) { return juce::Point<float>(pivotX + r * std::sin(angle), pivotY - r * std::cos(angle)); };

    g.saveState();
    g.reduceClipRegion(bounds.toNearestInt());

    // The arc of the scale, and the red part of it above zero
    const float arcRadius = radius * 0.9f;
    {
        juce::Path arc;
        arc.addCentredArc(pivotX, pivotY, arcRadius, arcRadius, 0.f, angleOf(low), angleOf(0.f), true);
        g.setColour(Theme::text.withAlpha(0.85f));
        g.strokePath(arc, juce::PathStrokeType(1.5f));

        juce::Path red;
        red.addCentredArc(pivotX, pivotY, arcRadius, arcRadius, 0.f, angleOf(0.f), angleOf(high), true);
        g.setColour(Theme::over);
        g.strokePath(red, juce::PathStrokeType(4.5f));
    }

    // The ticks, and the numbers at the main ones
    {
        std::vector<float> majors;
        if (mode == VuMeterEngine::vu)
            majors = { -20.f, -10.f, -7.f, -5.f, -3.f, -2.f, -1.f, 0.f, 1.f, 2.f, 3.f };
        else if (mode == VuMeterEngine::rms)
            majors = { -20.f, -16.f, -12.f, -8.f, -4.f, 0.f, 4.f, 8.f };
        else
            majors = { -12.f, -8.f, -4.f, 0.f, 4.f, 8.f, 12.f };

        g.setFont(Theme::font(juce::jlimit(10.f, 15.f, bounds.getHeight() * 0.075f), true));
        for (float v : majors)
        {
            const float a = angleOf(v);
            g.setColour(v > 0.f ? Theme::over : Theme::text);
            g.drawLine({ pointAt(arcRadius, a), pointAt(arcRadius - radius * 0.09f, a) }, 2.f);

            const bool isTest = juce::approximatelyEqual(v, 0.f) && mode != VuMeterEngine::vu;
            const juce::String text = isTest ? "TEST" : (v > 0.f ? "+" : "") + juce::String((int)v).replace("-", juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")));
            const auto labelPoint = pointAt(arcRadius + radius * 0.075f, a);
            g.drawText(text, juce::Rectangle<float>(44.f, 16.f).withCentre(labelPoint), juce::Justification::centred);
        }

        // Minor ticks between
        g.setColour(Theme::text.withAlpha(0.6f));
        const float step = mode == VuMeterEngine::vu ? 1.f : 2.f;
        for (float v = std::ceil(low / step) * step; v <= high + 0.01f; v += step)
        {
            const float a = angleOf(v);
            g.drawLine({ pointAt(arcRadius, a), pointAt(arcRadius - radius * 0.045f, a) }, 1.f);
        }
    }

    // The name of the scale
    g.setFont(Theme::font(juce::jlimit(11.f, 16.f, bounds.getHeight() * 0.08f), true));
    g.setColour(Theme::textDim);
    const juce::String unit = mode == VuMeterEngine::vu ? "VU" : "dB";
    g.drawText(unit, juce::Rectangle<float>(60.f, 18.f).withCentre({ pivotX, pivotY - radius * 0.38f }), juce::Justification::centred);

    // The channel
    {
        g.setFont(Theme::labelFont());
        g.setColour(Theme::textFaint);
        const juce::String name = display == VuMeterEngine::leftRight ? (index == 0 ? "LEFT" : "RIGHT")
                                : display == VuMeterEngine::midSide ? (index == 0 ? "MID" : "SIDE") : "SUM";
        g.drawText(name, bounds.reduced(10.f, 8.f).removeFromTop(14.f), juce::Justification::centredLeft);
    }

    // The held needle, thin and in the colour of the second signal
    if (showHold && face.hold > low)
    {
        const float a = angleOf(face.hold);
        g.setColour(Theme::second.withAlpha(0.9f));
        g.drawLine({ pointAt(radius * 0.18f, a), pointAt(arcRadius - radius * 0.02f, a) }, 1.2f);
    }

    // The needle, with a shadow that makes it stand off the face
    {
        const float a = angleOf(face.needle);
        const auto tip = pointAt(arcRadius + radius * 0.02f, a);
        const auto base = pointAt(radius * 0.1f, a);
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawLine({ base.translated(3.f, 3.f), tip.translated(3.f, 3.f) }, 2.4f);
        g.setColour(Theme::text);
        g.drawLine({ base, tip }, 2.2f);
    }

    g.restoreState();

    // The lamp for the clip: yellow past the level that is set, red when the signal clips
    {
        const auto lamp = juce::Rectangle<float>(11.f, 11.f).withCentre({ bounds.getRight() - 16.f, bounds.getY() + 15.f });
        const bool red = face.redSeconds > 0.0, orange = face.orangeSeconds > 0.0;
        g.setColour(red ? Theme::over : orange ? Theme::warn : Theme::track);
        g.fillEllipse(lamp);
        g.setColour(Theme::panelEdge);
        g.drawEllipse(lamp, 1.f);
        if (red || orange)
        {
            g.setColour((red ? Theme::over : Theme::warn).withAlpha(0.35f));
            g.fillEllipse(lamp.expanded(5.f));
        }
    }

    // The numbers: the loudest peak at the left, and the reading at the right
    if (showNumbers)
    {
        const auto strip = bounds.reduced(10.f, 8.f).removeFromBottom(readoutHeight);
        const float boxWidth = juce::jmin(78.f, strip.getWidth() * 0.4f);

        auto drawBox = [&](juce::Rectangle<float> box, const juce::String& text, juce::Colour colour)
        {
            g.setColour(Theme::display.darker(0.5f).withAlpha(0.9f));
            g.fillRoundedRectangle(box, 4.f);
            g.setFont(Theme::font(juce::jlimit(13.f, 22.f, readoutHeight * 0.72f), true));
            g.setColour(colour);
            g.drawText(text, box, juce::Justification::centred);
        };

        drawBox(strip.withWidth(boxWidth), format(face.heldPeakDb), Theme::second);
        drawBox(strip.withLeft(strip.getRight() - boxWidth), format(face.needle), Theme::text);
    }

    juce::ignoreUnused(calibration);
}

void VuMeterView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    // The line above: the scale in use and where zero is, and the badge for the options
    {
        const int mode = juce::roundToInt(parameter(Parameters::ID::vuMode));
        const float calibration = Parameters::valueAt(Parameters::vuCalibrationsDb, juce::roundToInt(parameter(Parameters::ID::vuCalibration)));
        const int display = juce::roundToInt(parameter(Parameters::ID::vuDisplay));

        g.setFont(Theme::font(13.f));
        g.setColour(Theme::text);
        g.drawText(Parameters::vuModeShortNames[mode] + "   " + juce::String(calibration, 1).replace("-", juce::String(juce::CharPointer_UTF8("\xe2\x88\x92"))) + " dBFS   " + Parameters::vuDisplayNames[display],
                   infoArea, juce::Justification::centredLeft);

        g.setColour(Theme::track);
        g.fillRoundedRectangle(badgeArea.toFloat(), 4.f);
        g.setColour(Theme::accent);
        g.drawRoundedRectangle(badgeArea.toFloat().reduced(0.5f), 4.f, 1.f);
        g.setFont(Theme::labelFont());
        g.setColour(Theme::accent);
        g.drawText("ADVANCED", badgeArea, juce::Justification::centred);
    }

    for (int i = 0; i < numFaces; ++i)
        paintFace(g, faces[(size_t)i], i);
}

void VuMeterView::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(badgeArea.contains(e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void VuMeterView::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (badgeArea.contains(e.getPosition()))
    {
        if (onAdvanced)
            onAdvanced(badgeArea);
        return;
    }

    // A click on a meter starts the hold and the lamp again
    for (int i = 0; i < numFaces; ++i)
        if (faces[(size_t)i].bounds.contains(e.getPosition()))
        {
            faces[(size_t)i].hold = -120.f;
            faces[(size_t)i].heldPeakDb = -120.f;
            faces[(size_t)i].orangeSeconds = faces[(size_t)i].redSeconds = 0.0;
        }
}
