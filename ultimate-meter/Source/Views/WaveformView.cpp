#include "WaveformView.h"

namespace
{
    constexpr double lowCrossoverHz = 250.0, highCrossoverHz = 2500.0;
    constexpr int maxSamplesPerUpdate = 16384;

    // The signals, by number
    enum Signal { signalLeft, signalRight, signalMid, signalSide };

    juce::String dbLabel(float gain)
    {
        const int db = juce::roundToInt(juce::Decibels::gainToDecibels(gain));
        return db == 0 ? juce::String("0 dB") : juce::String(db);
    }

    // What each lane shows: the signals that it takes the extremes of, and its name
    struct LaneSpec
    {
        std::array<int, 2> signals { -1, -1 };
        const char* name = "";
    };

    std::vector<LaneSpec> lanesOf(int channels)
    {
        using namespace Parameters;
        switch (channels)
        {
            case waveformLeft:      return { { { signalLeft, -1 }, "L" } };
            case waveformRight:     return { { { signalRight, -1 }, "R" } };
            case waveformMid:       return { { { signalMid, -1 }, "M" } };
            case waveformSide:      return { { { signalSide, -1 }, "S" } };
            case waveformLeftRight: return { { { signalLeft, -1 }, "L" }, { { signalRight, -1 }, "R" } };
            case waveformMidSide:   return { { { signalMid, -1 }, "M" }, { { signalSide, -1 }, "S" } };
            default:                return { { { signalLeft, signalRight }, "" } }; // both channels as one
        }
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

void WaveformView::setSettings(const Settings& newSettings)
{
    if (!(newSettings == settings))
    {
        // The time code changes in every frame, and is all that does, so only its corner is drawn again
        const bool onlyTime = newSettings.channels == settings.channels && newSettings.colours == settings.colours
            && newSettings.sweep == settings.sweep && newSettings.peakHistory == settings.peakHistory
            && juce::exactlyEqual(newSettings.zoom, settings.zoom) && newSettings.timeCode == settings.timeCode;

        settings = newSettings;

        if (onlyTime)
            repaint(plot.withHeight(24).withWidth(220));
        else
            repaint();
    }
}

void WaveformView::clearHistory()
{
    written = 0;
    minimum.fill(0.f);
    maximum.fill(0.f);
    sumSquares.fill(0.0);
    sumLow = sumMid = sumHigh = 0.0;
    countInColumn = 0;
    phase = 0.0;
    lowState[0] = lowState[1] = highState[0] = highState[1] = 0.0;
    lastTotalWritten = audioProcessor.sampleRingBuffer.getTotalWritten();
    repaint();
}

juce::String WaveformView::formatTimeCode(double seconds)
{
    if (seconds < 0.0)
        return "--:--:--.---";

    const auto total = (juce::int64)std::floor(seconds * 1000.0 + 0.5);
    const int ms = (int)(total % 1000);
    const int s = (int)((total / 1000) % 60);
    const int m = (int)((total / 60000) % 60);
    const int h = (int)(total / 3600000);
    return juce::String::formatted("%02d:%02d:%02d.%03d", h, m, s, ms);
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
    const std::array<float, numSignals> values { left, right, 0.5f * (left + right), 0.5f * (left - right) };

    for (size_t i = 0; i < values.size(); ++i)
    {
        minimum[i] = juce::jmin(minimum[i], values[i]);
        maximum[i] = juce::jmax(maximum[i], values[i]);
        sumSquares[i] += (double)values[i] * (double)values[i];
    }

    const double mono = (double)values[signalMid];

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
        for (size_t i = 0; i < column.rms.size(); ++i)
            column.rms[i] = (float)std::sqrt(sumSquares[i] / n);

        column.low = (float)(sumLow / n);
        column.mid = (float)(sumMid / n);
        column.high = (float)(sumHigh / n);
    }

    columns[(size_t)(written % (juce::uint64)maxColumns)] = column;
    ++written;

    minimum.fill(0.f);
    maximum.fill(0.f);
    sumSquares.fill(0.0);
    sumLow = sumMid = sumHigh = 0.0;
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

    if (numNew > (juce::uint64)numSamples)
    {
        double skipped = (double)(numNew - (juce::uint64)numSamples) + phase;
        while (skipped >= samplesPerColumn)
        {
            skipped -= samplesPerColumn;
            minimum.fill(0.f);
            maximum.fill(0.f);
            finishColumn();
        }
        phase = skipped;
    }

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

    repaint(plot);
}

//==============================================================================
// The lens at the corner of the plot: a minus and a plus, with the zoom between them
juce::Rectangle<int> WaveformView::lensArea() const
{
    return juce::Rectangle<int>(104, 22).withRightX(plot.getRight() - 6).withY(plot.getY() + 5);
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    const auto area = lensArea();
    int hover = 0;
    if (area.contains(e.getPosition()))
        hover = e.x < area.getX() + 38 ? -1 : e.x > area.getRight() - 38 ? 1 : 0;

    if (hover != lensHover)
    {
        lensHover = hover;
        setMouseCursor(hover != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint(area);
    }
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    const auto area = lensArea();
    if (!area.contains(e.getPosition()) || !onZoomStep)
        return;

    if (e.x < area.getX() + 38)
        onZoomStep(-1);
    else if (e.x > area.getRight() - 38)
        onZoomStep(1);
}

//==============================================================================
void WaveformView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    if (plot.isEmpty())
        return;

    const auto lanes = lanesOf(settings.channels);
    const int numLanes = (int)lanes.size();
    const float laneHeight = (float)plot.getHeight() / (float)numLanes;

    // The scale of a lane runs from silence in its middle to full scale, times the zoom, at its edges
    auto laneArea = [&](int lane) { return juce::Rectangle<float>((float)plot.getX(), (float)plot.getY() + (float)lane * laneHeight, (float)plot.getWidth(), laneHeight); };
    auto halfOf = [&](int) { return 0.5f * laneHeight - 2.f; };

    // Lines for full scale, -6 and -12 dB, above and below the middle line of every lane, which is silence
    g.setFont(Theme::font(10.5f));
    for (int lane = 0; lane < numLanes; ++lane)
    {
        const auto area = laneArea(lane);
        const float midY = area.getCentreY();
        const float half = halfOf(lane);

        for (float gain : { 1.f, 0.5f, 0.25f })
        {
            const float h = half * gain * settings.zoom;
            if (h > half + 0.5f)
                continue;

            g.setColour(juce::exactlyEqual(gain, 1.f) ? Theme::gridStrong : Theme::grid);
            g.fillRect(area.getX(), midY - h, area.getWidth(), 1.f);
            g.fillRect(area.getX(), midY + h, area.getWidth(), 1.f);

            g.setColour(Theme::textDim);
            g.drawText(dbLabel(gain), juce::Rectangle<float>(34.f, 12.f).withCentre({ area.getX() - 18.f, midY - h }), juce::Justification::centredRight);
        }

        g.setColour(Theme::gridStrong);
        g.fillRect(area.getX(), midY, area.getWidth(), 1.f);

        if (numLanes > 1)
        {
            g.setColour(Theme::textDim);
            g.setFont(Theme::labelFont());
            g.drawText(lanes[(size_t)lane].name, juce::Rectangle<float>(area.getX() + 8.f, area.getY() + (lane == 0 && settings.timeCode ? 26.f : 4.f), 24.f, 14.f), juce::Justification::centredLeft);
            g.setFont(Theme::font(10.5f));

            if (lane > 0)
            {
                g.setColour(Theme::edge);
                g.fillRect(area.getX(), area.getY() - 1.f, area.getWidth(), 2.f);
            }
        }
    }

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

        // The colours of a level, for the colour map: from the foot of the scale to full scale
        const auto levelGradient = Theme::levelColours(minDb, 0.f, -6.f, -1.f, { 0.f, 0.f }, { 0.f, 1.f });

        std::vector<juce::Path> bandPaths(3);
        std::array<float, 3> smoothedProportion {};
        bool hasSmoothed = false;
        std::array<bool, 3> pathStarted { false, false, false };

        const int numBins = settings.sweep ? (spanColumns + k - 1) / k : (int)(((float)plot.getWidth() + binWidth) / binWidth) + 2;

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto start = newestBinStart - (juce::int64)bin * k;
            const auto end = std::min<juce::int64>(newest, start + k - 1);
            if (end < oldest || start + k - 1 < 0)
                break;

            // Where the bin goes: in a scrolling picture, from the right edge; in a sweep, at its place in the span
            float right;
            if (settings.sweep)
                right = (float)plot.getX() + (float)(((start % spanColumns) + spanColumns) % spanColumns + k) * pixelsPerColumn;
            else
                right = firstRight - (float)bin * binWidth;

            if (right < (float)plot.getX() || right - binWidth > (float)plot.getRight() + 1.f)
                continue;

            float low = 0.f, mid = 0.f, high = 0.f;
            int count = 0;

            // One pass over the columns of the bin, for every lane
            std::array<std::array<Shape, 2>, 2> perLane {}; // [lane][signal]
            for (auto index = std::max<juce::int64>(start, oldest); index <= end; ++index)
            {
                const auto& column = columns[(size_t)(index % maxColumns)];

                for (int lane = 0; lane < numLanes; ++lane)
                    for (size_t part = 0; part < 2; ++part)
                    {
                        const int signal = lanes[(size_t)lane].signals[part];
                        if (signal < 0)
                            continue;

                        auto& shape = perLane[(size_t)lane][part];
                        shape.lowest = juce::jmin(shape.lowest, column.minimum[(size_t)signal]);
                        shape.highest = juce::jmax(shape.highest, column.maximum[(size_t)signal]);
                        shape.rmsSquares += column.rms[(size_t)signal] * column.rms[(size_t)signal];
                    }

                low += column.low;
                mid += column.mid;
                high += column.high;
                ++count;
            }

            if (count == 0)
                continue;

            const float x = right - binWidth;
            const float width = binWidth + 0.5f;

            for (int lane = 0; lane < numLanes; ++lane)
            {
                // The lane takes the extremes of its signals, and the RMS of the two together when it has two
                Shape shape;
                int parts = 0;
                for (size_t part = 0; part < 2; ++part)
                    if (lanes[(size_t)lane].signals[part] >= 0)
                    {
                        shape.lowest = juce::jmin(shape.lowest, perLane[(size_t)lane][part].lowest);
                        shape.highest = juce::jmax(shape.highest, perLane[(size_t)lane][part].highest);
                        shape.rmsSquares += perLane[(size_t)lane][part].rmsSquares;
                        ++parts;
                    }

                const float rmsGain = std::sqrt(shape.rmsSquares / (float)(count * juce::jmax(1, parts)));
                const float peak = juce::jmax(shape.highest, -shape.lowest);

                // The colour of the column
                juce::Colour colour;
                switch (settings.colours)
                {
                    case Parameters::colourStatic:
                        colour = Theme::accent;
                        break;
                    case Parameters::colourMap:
                        colour = levelGradient.getColourAtPosition(juce::jlimit(0.0, 1.0, (double)(juce::Decibels::gainToDecibels(peak, -200.f) - minDb) / (double)(0.f - minDb)));
                        break;
                    default:
                        colour = colourOfBands(low, mid, high);

                        // A peak at full scale turns the column red
                        colour = colour.interpolatedWith(Theme::over, 0.85f * juce::jlimit(0.f, 1.f, (peak - 0.9f) / 0.08f));
                        break;
                }

                const auto area = laneArea(lane);
                const float midY = area.getCentreY();
                const float half = halfOf(lane) * settings.zoom;

                const float top = midY - shape.highest * half;
                const float bottom = midY - shape.lowest * half;

                // The outer shape is the highest and lowest samples, a little dimmer, and the core is the RMS level
                g.setColour(colour.withMultipliedBrightness(0.78f));
                g.fillRect(x, top, width, juce::jmax(1.f, bottom - top));

                const float core = rmsGain * half;
                g.setColour(colour.brighter(0.2f));
                g.fillRect(x, midY - core, width, juce::jmax(1.f, 2.f * core));

                // A sample at full scale or beyond is a clip: its end is marked in red
                const float clipLevel = 0.999f;
                if (shape.highest >= clipLevel)
                {
                    g.setColour(Theme::over);
                    g.fillRect(x, top, width, 3.f);
                }
                if (-shape.lowest >= clipLevel)
                {
                    g.setColour(Theme::over);
                    g.fillRect(x, bottom - 3.f, width, 3.f);
                }

                // The history of the levels of the three bands, as thin lines over the lane
                if (settings.peakHistory)
                {
                    const std::array<float, 3> powers { low / (float)count, mid / (float)count, high / (float)count };
                    for (size_t band = 0; band < 3; ++band)
                    {
                        // The level of the band, from -60 dB at the bottom of the lane to 0 dB at its top
                        const float db = 10.f * std::log10(powers[band] + 1.0e-12f);
                        float proportion = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);

                        // The history is smoothed along the time axis, so that it reads as a level and not as every transient
                        if (lane == 0)
                        {
                            proportion = hasSmoothed ? smoothedProportion[band] + 0.12f * (proportion - smoothedProportion[band]) : proportion;
                            smoothedProportion[band] = proportion;
                            hasSmoothed = hasSmoothed || band == 2;
                        }

                        const float y = area.getBottom() - 3.f - proportion * (area.getHeight() - 8.f);

                        if (lane == 0)
                        {
                            auto& path = bandPaths[band];
                            if (!pathStarted[band])
                            {
                                path.startNewSubPath(x + 0.5f * binWidth, y);
                                pathStarted[band] = true;
                            }
                            else
                            {
                                path.lineTo(x + 0.5f * binWidth, y);
                            }
                        }
                    }
                }
            }
        }

        if (settings.peakHistory)
        {
            const std::array<juce::Colour, 3> bandColours { juce::Colour(0xffff5a4a), juce::Colour(0xff55e08a), juce::Colour(0xff5aa8ff) };
            for (size_t band = 0; band < 3; ++band)
            {
                g.setColour(bandColours[band].withAlpha(0.85f));
                g.strokePath(bandPaths[band], juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // In a sweep, the place that is being written now
        if (settings.sweep)
        {
            const float nowX = (float)plot.getX() + (float)(newest % spanColumns) * pixelsPerColumn;
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.fillRect(nowX, (float)plot.getY(), 1.f, (float)plot.getHeight());
        }
    }

    // The time axis: "now" at the right edge when scrolling, and seconds from the left in a sweep
    if (settings.sweep)
    {
        g.setFont(Theme::font(10.5f));
        const int step = spanSeconds <= 30.f ? 5 : 10;
        for (int seconds = 0; seconds <= juce::roundToInt(spanSeconds); seconds += step)
        {
            const float x = (float)plot.getX() + (float)plot.getWidth() * (float)seconds / spanSeconds;
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.fillRect(x, (float)plot.getY(), 1.f, (float)plot.getHeight());
            g.setColour(Theme::textDim);
            g.drawText(juce::String(seconds) + " s", juce::Rectangle<float>(50.f, 14.f).withCentre({ juce::jlimit((float)plot.getX() + 16.f, (float)plot.getRight() - 12.f, x), (float)plot.getBottom() + 10.f }), juce::Justification::centred);
        }
    }
    else
    {
        Timeline::drawTimeAxis(g, plot, spanSeconds);
    }

    // The time code of the host, top left
    if (settings.timeCode)
    {
        g.setFont(Theme::font(15.f, true));
        g.setColour(Theme::text.withAlpha(0.9f));
        g.drawText(formatTimeCode(settings.hostSeconds), juce::Rectangle<int>(plot.getX() + 10, plot.getY() + 4, 200, 20), juce::Justification::centredLeft);
    }

    // The lens: a minus, a magnifying glass with the zoom beside it, and a plus
    {
        const auto area = lensArea();
        g.setColour(Theme::panel.withAlpha(0.85f));
        g.fillRoundedRectangle(area.toFloat(), 4.f);
        g.setColour(Theme::panelEdge);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.f, 1.f);

        auto icon = area.toFloat().reduced(4.f, 0.f);
        const auto minus = icon.removeFromLeft(24.f);
        const auto plus = icon.removeFromRight(24.f);

        g.setFont(Theme::font(15.f, true));
        g.setColour(lensHover < 0 ? juce::Colours::white : Theme::text);
        g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")), minus, juce::Justification::centred);
        g.setColour(lensHover > 0 ? juce::Colours::white : Theme::text);
        g.drawText("+", plus, juce::Justification::centred);

        // The glass
        g.setColour(Theme::textDim);
        const auto glass = juce::Rectangle<float>(8.f, 8.f).withCentre({ icon.getX() + 7.f, icon.getCentreY() - 1.f });
        g.drawEllipse(glass, 1.4f);
        g.drawLine(glass.getRight() - 1.f, glass.getBottom() - 1.f, glass.getRight() + 3.f, glass.getBottom() + 3.f, 1.6f);

        g.setFont(Theme::font(11.f));
        g.setColour(Theme::text);
        g.drawText(juce::String(settings.zoom, settings.zoom < 1.f ? 1 : 0) + "x", icon.withTrimmedLeft(17.f), juce::Justification::centredLeft);
    }
}
