#include "WaveformView.h"

namespace
{
    constexpr double lowCrossoverHz = 250.0, highCrossoverHz = 2500.0;
    constexpr int maxSamplesPerUpdate = 16384;

    juce::String dbLabel(float gain)
    {
        const int db = juce::roundToInt(juce::Decibels::gainToDecibels(gain));
        return db == 0 ? juce::String("0 dB") : juce::String(db);
    }
}

WaveformView::WaveformView(UltimateMeterAudioProcessor& processor) : audioProcessor(processor), columns((size_t)maxColumns)
{
    setOpaque(true);
    samples.setSize(2, maxSamplesPerUpdate);
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
    written = 0;
    minimum = maximum = 0.f;
    sumSquares = sumLow = sumMid = sumHigh = 0.0;
    countInColumn = 0;
    phase = 0.0;
    lowState[0] = lowState[1] = highState[0] = highState[1] = 0.0;
    lastTotalWritten = audioProcessor.sampleRingBuffer.getTotalWritten();
    repaint();
}

juce::Colour WaveformView::colourOfBands(float low, float mid, float high)
{
    // The bands of music are not equally strong: the lows carry most of the power. Each is weighed so that
    // a typical mix comes out white-ish and a sound with more of one band than usual takes its colour.
    const float r = std::sqrt(juce::jmax(0.f, low)) * 0.6f;
    const float g = std::sqrt(juce::jmax(0.f, mid));
    const float b = std::sqrt(juce::jmax(0.f, high)) * 2.5f;

    const float top = juce::jmax(r, g, b);
    if (top <= 1.0e-7f)
        return Theme::textFaint;

    // Normalise to the strongest band, then stretch the contrast so that the colours are clear
    auto channel = [&](float v) { return std::pow(v / top, 1.6f); };
    return juce::Colour::fromFloatRGBA(0.18f + 0.82f * channel(r), 0.18f + 0.82f * channel(g), 0.18f + 0.82f * channel(b), 1.f);
}

void WaveformView::prepareFilters(double sampleRate)
{
    filterRate = sampleRate;
    lowCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * lowCrossoverHz / sampleRate);
    highCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * highCrossoverHz / sampleRate);
    samplesPerColumn = sampleRate / (double)columnsPerSecond;
}

void WaveformView::addSample(float left, float right)
{
    // The extremes of both channels, so that the loudest sample of either is never left out
    minimum = juce::jmin(minimum, left, right);
    maximum = juce::jmax(maximum, left, right);

    const double mono = 0.5 * ((double)left + (double)right);
    sumSquares += 0.5 * ((double)left * left + (double)right * right);

    // Two one-pole stages in a row make each crossover steeper
    lowState[0] += lowCoefficient * (mono - lowState[0]);
    lowState[1] += lowCoefficient * (lowState[0] - lowState[1]);
    highState[0] += highCoefficient * (mono - highState[0]);
    highState[1] += highCoefficient * (highState[0] - highState[1]);

    const double low = lowState[1];
    const double mid = highState[1] - lowState[1];
    const double high = mono - highState[1];
    sumLow += low * low;
    sumMid += mid * mid;
    sumHigh += high * high;
    ++countInColumn;
}

void WaveformView::finishColumn()
{
    Column column;
    column.minimum = minimum;
    column.maximum = maximum;

    if (countInColumn > 0)
    {
        const double n = (double)countInColumn;
        column.rms = (float)std::sqrt(sumSquares / n);
        column.low = (float)(sumLow / n);
        column.mid = (float)(sumMid / n);
        column.high = (float)(sumHigh / n);
    }

    columns[(size_t)(written % (juce::uint64)maxColumns)] = column;
    ++written;

    minimum = maximum = 0.f;
    sumSquares = sumLow = sumMid = sumHigh = 0.0;
    countInColumn = 0;
}

void WaveformView::update()
{
    auto& ringBuffer = audioProcessor.sampleRingBuffer;
    const double sampleRate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 44100.0;
    if (!juce::approximatelyEqual(sampleRate, filterRate))
        prepareFilters(sampleRate);

    const auto totalWritten = ringBuffer.getTotalWritten();

    // The buffer starts again from nothing when the host prepares the plugin
    if (totalWritten < lastTotalWritten)
        lastTotalWritten = 0;

    const auto numNew = totalWritten - lastTotalWritten;
    if (numNew == 0)
        return;

    // If more arrived than one frame can take, the oldest are left out. The time they covered is
    // filled with silence, so that the waveform keeps its place in time.
    const int numSamples = (int)std::min<juce::uint64>(numNew, (juce::uint64)maxSamplesPerUpdate);

    if (!ringBuffer.readLatest(samples.getWritePointer(0), samples.getWritePointer(1), numSamples))
        return;

    lastTotalWritten = totalWritten;

    const auto* left = samples.getReadPointer(0);
    const auto* right = samples.getReadPointer(1);
    const bool isLong = numNew > (juce::uint64)numSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        if (!std::isfinite(left[i]) || !std::isfinite(right[i]))
            addSample(0.f, 0.f);
        else
            addSample(left[i], right[i]);

        phase += 1.0;
        if (phase >= samplesPerColumn)
        {
            phase -= samplesPerColumn;
            finishColumn();
        }
    }

    if (isLong)
    {
        // The samples that were left out count as silence of their length
        double skipped = (double)(numNew - (juce::uint64)numSamples) + phase;
        while (skipped >= samplesPerColumn)
        {
            skipped -= samplesPerColumn;
            minimum = maximum = 0.f;
            finishColumn();
        }
        phase = skipped;
    }

    repaint(plot);
}

void WaveformView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    if (plot.isEmpty())
        return;

    const float midY = (float)plot.getCentreY();
    const float half = 0.5f * (float)plot.getHeight() - 2.f;

    // Lines for full scale, -6 and -12 dB, above and below the middle line, which is silence
    g.setFont(Theme::font(10.5f));
    for (float gain : { 1.f, 0.5f, 0.25f })
    {
        const float h = half * gain;
        g.setColour(juce::exactlyEqual(gain, 1.f) ? Theme::gridStrong : Theme::grid);
        g.fillRect((float)plot.getX(), midY - h, (float)plot.getWidth(), 1.f);
        g.fillRect((float)plot.getX(), midY + h, (float)plot.getWidth(), 1.f);

        g.setColour(Theme::textDim);
        g.drawText(dbLabel(gain), juce::Rectangle<float>(34.f, 12.f).withCentre({ (float)plot.getX() - 18.f, midY - h }), juce::Justification::centredRight);
    }
    g.setColour(Theme::gridStrong);
    g.fillRect((float)plot.getX(), midY, (float)plot.getWidth(), 1.f);

    if (written > 0)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(plot.expanded(0, 2));

        // A bin of k columns is about a pixel wide. Bins are tied to the absolute number of the column, so that
        // an old bin always holds the same columns and does not change as the picture scrolls, and the newest
        // bin's growth is shown as a smooth shift of the whole picture.
        const int spanColumns = juce::jmax(1, juce::roundToInt(spanSeconds * (float)columnsPerSecond));
        const float pixelsPerColumn = (float)plot.getWidth() / (float)spanColumns;
        const int k = juce::jmax(1, juce::roundToInt((float)spanColumns / (float)plot.getWidth()));
        const float binWidth = pixelsPerColumn * (float)k;

        const auto newest = (juce::int64)written - 1;
        const auto oldest = std::max<juce::int64>(0, (juce::int64)written - maxColumns);
        const int filled = (int)(newest % k) + 1;
        const auto newestBinStart = newest - filled + 1;
        const float firstRight = (float)plot.getRight() + (float)(k - filled) * pixelsPerColumn;

        for (int bin = 0;; ++bin)
        {
            const float right = firstRight - (float)bin * binWidth;
            if (right < (float)plot.getX())
                break;

            const auto start = newestBinStart - (juce::int64)bin * k;
            const auto end = std::min<juce::int64>(newest, start + k - 1);
            if (end < oldest)
                break;

            float lowest = 0.f, highest = 0.f, rmsSquares = 0.f, low = 0.f, mid = 0.f, high = 0.f;
            int count = 0;

            for (auto index = std::max<juce::int64>(start, oldest); index <= end; ++index)
            {
                const auto& column = columns[(size_t)(index % maxColumns)];
                lowest = juce::jmin(lowest, column.minimum);
                highest = juce::jmax(highest, column.maximum);
                rmsSquares += column.rms * column.rms;
                low += column.low;
                mid += column.mid;
                high += column.high;
                ++count;
            }

            if (count == 0)
                continue;

            auto colour = colourOfBands(low, mid, high);

            // A peak at full scale turns the column red
            const float peak = juce::jmax(highest, -lowest);
            colour = colour.interpolatedWith(Theme::over, 0.85f * juce::jlimit(0.f, 1.f, (peak - 0.9f) / 0.08f));

            const float x = right - binWidth;
            const float width = binWidth + 0.5f;
            const float top = midY - juce::jlimit(0.f, 1.f, highest) * half;
            const float bottom = midY - juce::jlimit(-1.f, 0.f, lowest) * half;

            // The outer shape is the highest and lowest samples, a little dimmer, and the core is the RMS level
            g.setColour(colour.withMultipliedBrightness(0.78f));
            g.fillRect(x, top, width, juce::jmax(1.f, bottom - top));

            const float core = juce::jmin(1.f, std::sqrt(rmsSquares / (float)count)) * half;
            g.setColour(colour.brighter(0.2f));
            g.fillRect(x, midY - core, width, juce::jmax(1.f, 2.f * core));
        }
    }

    Timeline::drawTimeAxis(g, plot, spanSeconds);
}
