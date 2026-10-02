#include "WaveformView.h"

namespace
{
    constexpr int numPoints = 96;
    constexpr double lowEdgeHz = 250.0, midEdgeHz = 2500.0;

    float heightOf(float gain)
    {
        // The height is a level in decibels, so that quiet passages still have a shape
        const float db = juce::Decibels::gainToDecibels(gain, -200.f);
        return juce::jlimit(0.f, 1.f, (db - WaveformView::minDb) / (0.f - WaveformView::minDb));
    }
}

WaveformView::WaveformView(SpectrumSource& spectrumSource) : source(spectrumSource)
{
    setOpaque(true);

    display.numPoints = numPoints;
    display.minFrequency = 30.0;
    display.maxFrequency = 16000.0;

    const double octaves = std::log(display.maxFrequency / display.minFrequency);
    auto pointOf = [&](double hz) { return juce::roundToInt((double)numPoints * std::log(hz / display.minFrequency) / octaves); };
    lowPoints = pointOf(lowEdgeHz);
    midPoints = pointOf(midEdgeHz);
}

void WaveformView::resized()
{
    // The same margins as the spectrogram and the history, so that a moment is at the same place in all
    plot = getLocalBounds().withTrimmedLeft(34).withTrimmedRight(34).withTrimmedTop(8).withTrimmedBottom(20);
}

void WaveformView::setSpan(float seconds)
{
    if (!juce::exactlyEqual(seconds, spanSeconds))
    {
        spanSeconds = seconds;
        repaint();
    }
}

void WaveformView::clearHistory()
{
    history.clear();
    pending = {};
    lastBands = {};
    repaint();
}

juce::Colour WaveformView::colourOfBands(float low, float mid, float high)
{
    // The bands of music are not equally strong: the lows carry most of the power. Each is weighed so that
    // a typical mix comes out white-ish and a sound with more of one band than usual takes its colour.
    const float r = std::sqrt(juce::jmax(0.f, low) * 0.35f);
    const float g = std::sqrt(juce::jmax(0.f, mid) * 1.4f);
    const float b = std::sqrt(juce::jmax(0.f, high) * 5.f);

    const float top = juce::jmax(r, g, b);
    if (top <= 1.0e-9f)
        return Theme::textFaint;

    // Normalise to the strongest band, then stretch the contrast so that the colours are clear
    auto channel = [&](float v) { return std::pow(v / top, 1.6f); };
    return juce::Colour::fromFloatRGBA(0.18f + 0.82f * channel(r), 0.18f + 0.82f * channel(g), 0.18f + 0.82f * channel(b), 1.f);
}

void WaveformView::record(int numNewSlots, bool hasNewSpectra, float peak, float rms)
{
    pending.peak = juce::jmax(pending.peak, peak);
    pending.rms = juce::jmax(pending.rms, rms);

    if (hasNewSpectra)
    {
        source.getEngine().render(SpectrumEngine::Curve::mid, display, source.getSampleRate(), spectrum);

        float low = 0.f, mid = 0.f, high = 0.f;
        for (int point = 0; point < (int)spectrum.size(); ++point)
        {
            // Power of the point, relative to a very quiet floor so that silence adds nothing
            const float power = std::pow(10.f, juce::jmax(spectrum[(size_t)point], -100.f) / 10.f);
            (point < lowPoints ? low : point < midPoints ? mid : high) += power;
        }

        // A band is shown by the loudest it was in the slot
        pending.low = juce::jmax(pending.low, low);
        pending.mid = juce::jmax(pending.mid, mid);
        pending.high = juce::jmax(pending.high, high);
    }

    if (numNewSlots <= 0)
        return;

    auto slot = pending;

    // A slot that no new spectrum came in keeps the colour that came before it
    if (slot.low + slot.mid + slot.high > 0.f)
        lastBands = slot;
    else
    {
        slot.low = lastBands.low;
        slot.mid = lastBands.mid;
        slot.high = lastBands.high;
    }

    for (int i = 0; i < numNewSlots; ++i)
        history.push(slot);

    pending = {};
    repaint(plot);
}

void WaveformView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    if (plot.isEmpty())
        return;

    const float midY = (float)plot.getCentreY();
    const float halfHeight = 0.5f * (float)plot.getHeight() - 2.f;
    const int numSlots = Timeline::slotsIn(spanSeconds);
    const float slotWidth = (float)plot.getWidth() / (float)numSlots;

    // The middle line, and the lines for -12 and -24 dB above and below it
    g.setFont(Theme::font(10.5f));
    for (int decibels : { 0, -12, -24 })
    {
        const float h = halfHeight * heightOf(juce::Decibels::decibelsToGain((float)decibels));
        g.setColour(decibels == 0 ? Theme::gridStrong : Theme::grid);
        g.fillRect((float)plot.getX(), midY - h, (float)plot.getWidth(), 1.f);
        g.fillRect((float)plot.getX(), midY + h, (float)plot.getWidth(), 1.f);

        g.setColour(Theme::textDim);
        g.drawText(decibels == 0 ? "0" : juce::String(decibels), juce::Rectangle<float>(30.f, 12.f).withCentre({ (float)plot.getX() - 17.f, midY - h }), juce::Justification::centredRight);
    }

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(plot.expanded(0, 2));

        for (int age = numSlots - 1; age >= 0; --age)
        {
            const auto* slot = history.fromNewest(age);
            if (slot == nullptr)
                continue;

            const float x = (float)plot.getRight() - slotWidth * (float)(age + 1);
            const float width = juce::jmax(1.f, slotWidth);

            auto colour = colourOfBands(slot->low, slot->mid, slot->high);

            // Close to full scale, the column goes red
            const float hotness = juce::jlimit(0.f, 1.f, (juce::Decibels::gainToDecibels(slot->peak, -200.f) + 3.f) / 3.f);
            colour = colour.interpolatedWith(Theme::over, 0.8f * hotness);

            const float peakHeight = halfHeight * heightOf(slot->peak);
            const float rmsHeight = halfHeight * heightOf(slot->rms);

            // The outer shape is the peak, a little dimmer, and the core is the RMS, brighter
            g.setColour(colour.withMultipliedBrightness(0.8f));
            g.fillRect(x, midY - peakHeight, width, 2.f * peakHeight + 1.f);

            g.setColour(colour.brighter(0.25f));
            g.fillRect(x, midY - rmsHeight, width, 2.f * rmsHeight + 1.f);
        }
    }

    Timeline::drawTimeAxis(g, plot, spanSeconds);
}
