#include "WaveformView.h"

namespace
{
    constexpr double lowCrossoverHz = 250.0, highCrossoverHz = 2500.0;
    constexpr int maxSamplesPerUpdate = 16384;
    constexpr int controlWidth = 124, controlHeight = 22, buttonWidth = 30;

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

WaveformView::WaveformView(UltimateMeterAudioProcessor& processor) :
    audioProcessor(processor), columns((size_t)maxColumns), rawLeft((size_t)rawCapacity, 0.f), rawRight((size_t)rawCapacity, 0.f)
{
    setOpaque(true);
    samples.setSize(2, maxSamplesPerUpdate);
}

void WaveformView::resized()
{
    // The same margins as the spectrogram and the history, so that a moment is at the same place in all
    plot = getLocalBounds().withTrimmedLeft(34).withTrimmedRight(34).withTrimmedTop(8).withTrimmedBottom(20);
}

// A sweep begins again at the left edge when the host starts to play, and when it jumps back, as it does at the end
// of a loop, so that a loop is seen from its beginning
void WaveformView::restartSweep(const Settings& newSettings)
{
    sweepOriginSample = (juce::int64)rawWritten;
    sweepOriginPpq = newSettings.ppq >= 0.0 ? newSettings.ppq : newSettings.hostSeconds * newSettings.bpm / 60.0;
}

void WaveformView::setSettings(const Settings& newSettings)
{
    if (newSettings.hostSeconds >= 0.0 && settings.hostSeconds >= 0.0)
    {
        const double delta = newSettings.hostSeconds - settings.hostSeconds;
        if (delta < -0.05)
        {
            restartSweep(newSettings);
            stagnantFrames = 0;
        }
        else if (std::abs(delta) < 1.0e-9)
        {
            ++stagnantFrames;
        }
        else
        {
            if (stagnantFrames >= 3 && delta > 0.0)
                restartSweep(newSettings);
            stagnantFrames = 0;
        }
    }

    if (!(newSettings == settings))
    {
        // The time code changes in every frame, and is all that does, so only its corner is drawn again
        const bool onlyTime = newSettings.channels == settings.channels && newSettings.colours == settings.colours
            && newSettings.sweep == settings.sweep && newSettings.peakHistory == settings.peakHistory
            && juce::exactlyEqual(newSettings.zoom, settings.zoom) && juce::exactlyEqual(newSettings.spanSeconds, settings.spanSeconds)
            && newSettings.timeCode == settings.timeCode;

        settings = newSettings;

        if (onlyTime)
            repaint(plot.withHeight(26).withWidth(220));
        else
            repaint();
    }
}

void WaveformView::clearHistory()
{
    written = 0;
    rawWritten = 0;
    minimum.fill(0.f);
    maximum.fill(0.f);
    sumSquares.fill(0.0);
    sumLow = sumMid = sumHigh = 0.0;
    sweepOriginSample = 0;
    sweepOriginPpq = 0.0;
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

juce::String WaveformView::formatSpan(float seconds)
{
    if (seconds < 1.f)
        return juce::String(juce::roundToInt(seconds * 1000.f)) + " ms";

    return (juce::approximatelyEqual(seconds, std::round(seconds)) ? juce::String(juce::roundToInt(seconds)) : juce::String(seconds, 1)) + " s";
}

juce::Colour WaveformView::colourOfBands(float low, float mid, float high)
{
    // The bands of music are not equally strong: the lows carry most of the power, so each is weighed to make up for it.
    const float wl = std::sqrt(juce::jmax(0.f, low)) * 0.6f;
    const float wm = std::sqrt(juce::jmax(0.f, mid));
    const float wh = std::sqrt(juce::jmax(0.f, high)) * 2.5f;

    const float total = wl + wm + wh;
    if (total <= 1.0e-7f)
        return Theme::textFaint;

    // Where the sound lies between the lows and the highs, from 0 to 1. A typical mix is in the lower half, so the range
    // is stretched to make use of every colour.
    const float centre = (wm * 0.5f + wh) / total;
    const float position = juce::jlimit(0.f, 1.f, (centre - 0.15f) / 0.55f);

    // Red and orange for the lows, through yellow and green for the mids, to blue for the highs
    const float hue = 0.0f + position * 0.62f;
    return juce::Colour::fromHSV(hue, 0.72f, 0.96f, 1.f);
}

void WaveformView::prepareFilters(double newSampleRate)
{
    sampleRate = newSampleRate;
    filterRate = newSampleRate;
    lowCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * lowCrossoverHz / newSampleRate);
    highCoefficient = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * highCrossoverHz / newSampleRate);
    samplesPerColumn = newSampleRate / (double)columnsPerSecond;
}

double WaveformView::rawMaxSpan() const
{
    // The samples that are kept, less what one update may add and a margin
    return ((double)rawCapacity - 4.0 * maxSamplesPerUpdate) / sampleRate;
}

void WaveformView::pushRaw(float left, float right)
{
    const auto index = (size_t)(rawWritten & (juce::uint64)(rawCapacity - 1));
    rawLeft[index] = left;
    rawRight[index] = right;
    ++rawWritten;
}

void WaveformView::addSample(float left, float right)
{
    pushRaw(left, right);

    const std::array<float, numSignals> values { left, right, 0.5f * (left + right), 0.5f * (left - right) };

    for (size_t i = 0; i < values.size(); ++i)
    {
        minimum[i] = countInColumn == 0 ? values[i] : juce::jmin(minimum[i], values[i]);
        maximum[i] = countInColumn == 0 ? values[i] : juce::jmax(maximum[i], values[i]);
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
    const double rate = audioProcessor.getSampleRate() > 0.0 ? audioProcessor.getSampleRate() : 44100.0;
    if (!juce::approximatelyEqual(rate, filterRate))
        prepareFilters(rate);

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
        const auto skippedSamples = numNew - (juce::uint64)numSamples;
        const auto numZeros = std::min<juce::uint64>(skippedSamples, (juce::uint64)rawCapacity);
        for (juce::uint64 i = 0; i < numZeros; ++i)
            pushRaw(0.f, 0.f);

        double skipped = (double)skippedSamples + phase;
        while (skipped >= samplesPerColumn)
        {
            skipped -= samplesPerColumn;
            minimum.fill(0.f);
            maximum.fill(0.f);
            countInColumn = 0;
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
// The pixels of the picture, from the newest
void WaveformView::buildBins(std::vector<Bin>& bins, double& newestRight, double& binWidth, int& binsInSpan, juce::int64& originBin) const
{
    bins.clear();
    originBin = 0;
    const int width = juce::jmax(1, plot.getWidth());
    const double span = (double)settings.spanSeconds;

    // The span of time is first compared with what the samples as they came can cover
    if (span <= rawMaxSpan() && rawWritten > 0)
    {
        // One bin a pixel. A bin is a stretch of samples, whose edges are at fixed places in the count of samples, so that
        // what a bin holds does not change as the picture scrolls.
        const double samplesPerBin = span * sampleRate / (double)width;
        binWidth = 1.0;
        binsInSpan = width;
        originBin = (juce::int64)std::floor((double)sweepOriginSample / samplesPerBin);

        const auto newestSample = (juce::int64)rawWritten - 1;
        const auto oldestSample = std::max<juce::int64>(0, (juce::int64)rawWritten - rawCapacity + 4096);
        const auto newestBin = (juce::int64)std::floor((double)newestSample / samplesPerBin);
        const auto newestBinStart = (juce::int64)std::floor((double)newestBin * samplesPerBin);
        const double filled = (double)(newestSample - newestBinStart + 1) / samplesPerBin;
        newestRight = (double)plot.getRight() + (1.0 - juce::jmin(1.0, filled)) * binWidth;

        const auto columnsNewest = (juce::int64)written - 1;
        const auto columnsOldest = std::max<juce::int64>(0, (juce::int64)written - maxColumns);
        constexpr int mask = rawCapacity - 1;

        for (int b = 0; b < width + 2; ++b)
        {
            const auto index = newestBin - b;
            if (index < 0)
                break;

            auto start = (juce::int64)std::floor((double)index * samplesPerBin);
            auto end = std::min(newestSample, (juce::int64)std::floor((double)(index + 1) * samplesPerBin) - 1);
            if (end < oldestSample)
                break;

            start = std::max(start, oldestSample);
            end = std::max(end, start);

            Bin bin;
            bin.index = index;
            bin.lowest.fill(1.0e9f);
            bin.highest.fill(-1.0e9f);

            for (auto s = start; s <= end; ++s)
            {
                const float l = rawLeft[(size_t)(s & mask)], r = rawRight[(size_t)(s & mask)];
                const std::array<float, numSignals> values { l, r, 0.5f * (l + r), 0.5f * (l - r) };
                for (size_t i = 0; i < values.size(); ++i)
                {
                    bin.lowest[i] = juce::jmin(bin.lowest[i], values[i]);
                    bin.highest[i] = juce::jmax(bin.highest[i], values[i]);
                    bin.meanSquare[i] += values[i] * values[i];
                }
            }

            const float count = (float)(end - start + 1);
            for (auto& value : bin.meanSquare)
                value /= count;

            // The colours come from the columns that cover the same time
            const auto firstColumn = std::max(columnsOldest, (juce::int64)((double)start / samplesPerColumn));
            const auto lastColumn = std::min(columnsNewest, (juce::int64)((double)end / samplesPerColumn));
            if (lastColumn >= firstColumn)
            {
                const auto stride = std::max<juce::int64>(1, (lastColumn - firstColumn) / 8);
                int used = 0;
                for (auto c = firstColumn; c <= lastColumn; c += stride)
                {
                    const auto& column = columns[(size_t)(c % maxColumns)];
                    bin.low += column.low;
                    bin.mid += column.mid;
                    bin.high += column.high;
                    ++used;
                }
                bin.low /= (float)used;
                bin.mid /= (float)used;
                bin.high /= (float)used;
            }

            bins.push_back(bin);
        }

        return;
    }

    // Further out, the columns: a bin of k columns is about a pixel wide
    const int spanColumns = juce::jmax(1, juce::roundToInt(span * columnsPerSecond));
    const double pixelsPerColumn = (double)width / (double)spanColumns;
    const int k = juce::jmax(1, juce::roundToInt((double)spanColumns / (double)width));
    binWidth = pixelsPerColumn * k;
    binsInSpan = (spanColumns + k - 1) / k;
    originBin = (juce::int64)((double)sweepOriginSample / samplesPerColumn) / k;

    if (written == 0)
        return;

    const auto newest = (juce::int64)written - 1;
    const auto oldest = std::max<juce::int64>(0, (juce::int64)written - maxColumns);
    const int filled = (int)(newest % k) + 1;
    const auto newestBinStart = newest - filled + 1;
    newestRight = (double)plot.getRight() + (double)(k - filled) * pixelsPerColumn;

    const int numBins = (int)(((double)width + binWidth) / binWidth) + 2;

    for (int b = 0; b < numBins; ++b)
    {
        const auto start = newestBinStart - (juce::int64)b * k;
        const auto end = std::min<juce::int64>(newest, start + k - 1);
        if (end < oldest || start + k - 1 < 0)
            break;

        Bin bin;
        bin.index = start / k;
        bin.lowest.fill(1.0e9f);
        bin.highest.fill(-1.0e9f);
        int count = 0;

        for (auto index = std::max<juce::int64>(start, oldest); index <= end; ++index)
        {
            const auto& column = columns[(size_t)(index % maxColumns)];
            for (size_t i = 0; i < numSignals; ++i)
            {
                bin.lowest[i] = juce::jmin(bin.lowest[i], column.minimum[i]);
                bin.highest[i] = juce::jmax(bin.highest[i], column.maximum[i]);
                bin.meanSquare[i] += column.rms[i] * column.rms[i];
            }

            bin.low += column.low;
            bin.mid += column.mid;
            bin.high += column.high;
            ++count;
        }

        if (count == 0)
            continue;

        for (auto& value : bin.meanSquare)
            value /= (float)count;

        bin.low /= (float)count;
        bin.mid /= (float)count;
        bin.high /= (float)count;
        bins.push_back(bin);
    }
}

//==============================================================================
// The level in dB that a height in the picture stands for, in the lane that it falls in
float WaveformView::guideDbAt(int y) const
{
    const int numLanes = (int)lanesOf(settings.channels).size();
    const float laneHeight = (float)plot.getHeight() / (float)numLanes;
    const int lane = juce::jlimit(0, numLanes - 1, (int)((float)(y - plot.getY()) / laneHeight));
    const float midY = (float)plot.getY() + ((float)lane + 0.5f) * laneHeight;
    const float half = 0.5f * laneHeight - 2.f;

    const float amplitude = std::abs((float)y - midY) / juce::jmax(1.f, half * settings.zoom);
    return juce::jlimit(-48.f, 0.f, juce::Decibels::gainToDecibels(amplitude, -48.f));
}

// The height of the guide above the middle of a lane
float WaveformView::guideYOfLane(int lane, float db) const
{
    const int numLanes = (int)lanesOf(settings.channels).size();
    const float laneHeight = (float)plot.getHeight() / (float)numLanes;
    const float midY = (float)plot.getY() + ((float)lane + 0.5f) * laneHeight;
    const float half = 0.5f * laneHeight - 2.f;
    return midY - half * settings.zoom * juce::Decibels::decibelsToGain(db);
}

bool WaveformView::nearGuide(juce::Point<int> position) const
{
    if (!settings.guideOn || !plot.contains(position))
        return false;

    const int numLanes = (int)lanesOf(settings.channels).size();
    const float laneHeight = (float)plot.getHeight() / (float)numLanes;
    const int lane = juce::jlimit(0, numLanes - 1, (int)((float)(position.y - plot.getY()) / laneHeight));
    const float upper = guideYOfLane(lane, settings.guideDb);
    const float midY = (float)plot.getY() + ((float)lane + 0.5f) * laneHeight;
    const float lower = 2.f * midY - upper;

    return std::abs((float)position.y - upper) < 6.f || std::abs((float)position.y - lower) < 6.f;
}

//==============================================================================
// The lenses at the corner of the plot, each a minus, a value, and a plus
juce::Rectangle<int> WaveformView::controlArea(int control) const
{
    return juce::Rectangle<int>(controlWidth, controlHeight).withRightX(plot.getRight() - 6 - control * 0 - (control == 0 ? 0 : controlWidth + 8)).withY(plot.getY() + 5);
}

void WaveformView::hitTestControl(juce::Point<int> position, int& control, int& part) const
{
    control = -1;
    part = 0;

    for (int c = 0; c < numControls; ++c)
    {
        const auto area = controlArea(c);
        if (area.contains(position))
        {
            control = c;
            part = position.x < area.getX() + buttonWidth ? -1 : position.x >= area.getRight() - buttonWidth ? 1 : 0;
            return;
        }
    }
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    int control, part;
    hitTestControl(e.getPosition(), control, part);

    // The guide can be taken by its line, and the button at the corner is for a click
    const bool onGuide = nearGuide(e.getPosition());
    const bool onButton = viewHovered && guideButtonArea().contains(e.getPosition());
    if (control < 0)
        setMouseCursor(onGuide ? juce::MouseCursor::UpDownResizeCursor : onButton ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);

    if (control != hoverControl || part != hoverPart)
    {
        hoverControl = control;
        hoverPart = part;
        setMouseCursor(control >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint(controlArea(0).getUnion(controlArea(1)).expanded(2));
    }
}

void WaveformView::mouseEnter(const juce::MouseEvent&)
{
    // The lenses show only while the mouse is over the view
    viewHovered = true;
    repaint(controlArea(0).getUnion(controlArea(1)).expanded(2));
}

void WaveformView::mouseExit(const juce::MouseEvent&)
{
    viewHovered = false;
    repaint(controlArea(0).getUnion(controlArea(1)).expanded(2));

    if (hoverControl >= 0)
    {
        hoverControl = -1;
        hoverPart = 0;
        repaint(controlArea(0).getUnion(controlArea(1)).expanded(2));
    }
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    int control, part;
    hitTestControl(e.getPosition(), control, part);

    if (control < 0 && !e.mods.isPopupMenu())
    {
        // The button at the corner puts the guide up, and takes it away if it is up
        if (guideButtonArea().contains(e.getPosition()))
        {
            if (settings.guideOn)
            {
                if (onGuideChanged)
                    onGuideChanged(false, settings.guideDb);
            }
            else
            {
                // It comes back at the level that it was left at, to be dragged by its line
                if (onGuideChanged)
                    onGuideChanged(true, settings.guideDb);
            }
            return;
        }

        if (nearGuide(e.getPosition()))
        {
            draggingGuide = true;
            return;
        }
    }

    if (control < 0)
        return;

    const bool coarse = e.mods.isShiftDown();

    if (control == 0)
    {
        if (part != 0 && onVerticalStep)
            onVerticalStep(part, coarse);
        else if (part == 0 && onVerticalReset)
            onVerticalReset();
    }
    else
    {
        if (part != 0 && onHorizontalStep)
            onHorizontalStep(part, coarse);
        else if (part == 0 && onHorizontalReset)
            onHorizontalReset();
    }
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingGuide && onGuideChanged)
        onGuideChanged(true, guideDbAt(e.y));
}

void WaveformView::mouseUp(const juce::MouseEvent&)
{
    draggingGuide = false;
}

void WaveformView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // A trackpad sends a stream of tiny movements, and a mouse sends one notch at a time. The movements are added up,
    // and one step is taken for each notch's worth, but never more than one every so often, so that the zoom does not race
    const bool horizontal = e.mods.isCtrlDown() || e.mods.isCommandDown();
    const float delta = wheel.deltaY != 0.f ? wheel.deltaY : wheel.deltaX;
    if (delta == 0.f)
        return;

    wheelAccumulator += wheel.isReversed ? -delta : delta;

    const auto now = juce::Time::getMillisecondCounter();
    if (wheelAccumulator > -0.4f && wheelAccumulator < 0.4f)
        return;
    if (now - lastWheelStep < 90)
        return;

    const int step = wheelAccumulator > 0.f ? 1 : -1;
    wheelAccumulator = 0.f;
    lastWheelStep = now;

    // The wheel zooms the amplitude, and with the control or command key, the time
    if (horizontal)
    {
        if (onHorizontalStep)
            onHorizontalStep(step, false);
    }
    else if (onVerticalStep)
    {
        onVerticalStep(step, false);
    }
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
    const float half = 0.5f * laneHeight - 2.f;

    // Lines for full scale, -6 and -12 dB, above and below the middle line of every lane, which is silence
    g.setFont(Theme::font(10.5f));
    for (int lane = 0; lane < numLanes; ++lane)
    {
        const auto area = laneArea(lane);
        const float midY = area.getCentreY();

        for (float gain : { 1.f, 0.5f, 0.25f })
        {
            const float h = half * gain * settings.zoom;
            if (h > half + 0.5f || h < 6.f)
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

    // The pixels of the picture
    std::vector<Bin> bins;
    double newestRight = (double)plot.getRight(), binWidth = 1.0;
    int binsInSpan = plot.getWidth();
    juce::int64 originBin = 0;
    buildBins(bins, newestRight, binWidth, binsInSpan, originBin);

    if (!bins.empty())
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(plot.expanded(0, 2));

        // The colours of the bands change slowly, whatever the bins do: each takes the mean of its neighbours, so that
        // the colour belongs to a stretch of sound and does not flicker with every transient
        const int numBins = (int)bins.size();
        std::vector<float> smoothLow((size_t)numBins), smoothMid((size_t)numBins), smoothHigh((size_t)numBins);
        const int radius = juce::jlimit(2, 8, juce::roundToInt(4.0 / binWidth));
        for (int i = 0; i < numBins; ++i)
        {
            float low = 0.f, mid = 0.f, high = 0.f;
            int count = 0;
            for (int j = juce::jmax(0, i - radius); j <= juce::jmin(numBins - 1, i + radius); ++j)
            {
                low += bins[(size_t)j].low;
                mid += bins[(size_t)j].mid;
                high += bins[(size_t)j].high;
                ++count;
            }
            smoothLow[(size_t)i] = low / (float)count;
            smoothMid[(size_t)i] = mid / (float)count;
            smoothHigh[(size_t)i] = high / (float)count;
        }

        // The colours of a level, for the colour map: from the foot of the scale to full scale
        const auto levelGradient = Theme::levelColours(minDb, 0.f, -6.f, -1.f, { 0.f, 0.f }, { 0.f, 1.f });

        std::vector<juce::Path> bandPaths(3);
        std::array<float, 3> smoothedProportion {};
        std::array<bool, 3> pathStarted { false, false, false };
        bool hasSmoothed = false;

        const int numToDraw = settings.sweep ? juce::jmin(numBins, binsInSpan) : numBins;

        for (int b = 0; b < numToDraw; ++b)
        {
            const auto& bin = bins[(size_t)b];

            // Where the bin goes: in a scrolling picture, from the right edge; in a sweep, at its place in the span
            double right;
            if (settings.sweep)
                right = (double)plot.getX() + (double)((((bin.index - originBin) % binsInSpan) + binsInSpan) % binsInSpan + 1) * binWidth;
            else
                right = newestRight - (double)b * binWidth;

            if (right < (double)plot.getX() || right - binWidth > (double)plot.getRight() + 1.0)
                continue;

            const float x = (float)(right - binWidth);
            const float width = (float)binWidth + 0.4f;

            for (int lane = 0; lane < numLanes; ++lane)
            {
                // The lane takes the extremes of its signals, and the RMS of the signals together
                Shape shape;
                shape.lowest = 1.0e9f;
                shape.highest = -1.0e9f;
                int parts = 0;
                for (size_t part = 0; part < 2; ++part)
                {
                    const int signal = lanes[(size_t)lane].signals[part];
                    if (signal < 0)
                        continue;

                    shape.lowest = juce::jmin(shape.lowest, bin.lowest[(size_t)signal]);
                    shape.highest = juce::jmax(shape.highest, bin.highest[(size_t)signal]);
                    shape.meanSquare += bin.meanSquare[(size_t)signal];
                    ++parts;
                }
                shape.meanSquare /= (float)juce::jmax(1, parts);

                const float rmsGain = std::sqrt(shape.meanSquare);
                const float peak = juce::jmax(shape.highest, -shape.lowest);

                // The colour of the pixel
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
                        colour = colourOfBands(smoothLow[(size_t)b], smoothMid[(size_t)b], smoothHigh[(size_t)b]);

                        // Only a peak at full scale turns the pixel red
                        colour = colour.interpolatedWith(Theme::over, 0.7f * juce::jlimit(0.f, 1.f, (peak - 0.985f) / 0.014f));
                        break;
                }

                const auto area = laneArea(lane);
                const float midY = area.getCentreY();
                const float scale = half * settings.zoom;

                const float top = midY - shape.highest * scale;
                const float bottom = midY - shape.lowest * scale;

                // The outer shape is the highest and lowest samples, a little dimmer, and the core is the RMS level
                g.setColour(colour.withMultipliedBrightness(0.78f));
                g.fillRect(x, top, width, juce::jmax(1.f, bottom - top));

                const float core = juce::jmin(rmsGain * scale, juce::jmax(0.f, juce::jmin(midY - top, bottom - midY)));
                g.setColour(colour.brighter(0.2f));
                g.fillRect(x, midY - core, width, juce::jmax(1.f, 2.f * core));

                // A sample at full scale or beyond is a clip: its end is marked in red
                constexpr float clipLevel = 0.999f;
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

                // The history of the levels of the three bands, as thin lines over the first lane
                if (settings.peakHistory && lane == 0)
                {
                    const std::array<float, 3> powers { smoothLow[(size_t)b], smoothMid[(size_t)b], smoothHigh[(size_t)b] };
                    for (size_t band = 0; band < 3; ++band)
                    {
                        // The level of the band, from -60 dB at the bottom of the lane to 0 dB at its top
                        const float db = 10.f * std::log10(powers[band] + 1.0e-12f);
                        float proportion = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);

                        proportion = hasSmoothed ? smoothedProportion[band] + 0.12f * (proportion - smoothedProportion[band]) : proportion;
                        smoothedProportion[band] = proportion;

                        const float y = area.getBottom() - 3.f - proportion * (area.getHeight() - 8.f);
                        auto& path = bandPaths[band];

                        if (!pathStarted[band])
                        {
                            path.startNewSubPath(x + 0.5f * (float)binWidth, y);
                            pathStarted[band] = true;
                        }
                        else
                        {
                            path.lineTo(x + 0.5f * (float)binWidth, y);
                        }
                    }
                    hasSmoothed = true;
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
            const double nowX = (double)plot.getX() + (double)((((bins.front().index - originBin) % binsInSpan) + binsInSpan) % binsInSpan + 1) * binWidth;
            g.setColour(juce::Colours::white.withAlpha(0.6f));
            g.fillRect((float)nowX, (float)plot.getY(), 1.f, (float)plot.getHeight());
        }
    }

    // The guide: a level across every lane, above and below the middle, with its value at the right
    if (settings.guideOn)
    {
        const float laneHeightGuide = (float)plot.getHeight() / (float)numLanes;
        g.setFont(Theme::font(10.5f, true));
        for (int lane = 0; lane < numLanes; ++lane)
        {
            const float upper = guideYOfLane(lane, settings.guideDb);
            const float midY = (float)plot.getY() + ((float)lane + 0.5f) * laneHeightGuide;
            const float lower = 2.f * midY - upper;

            g.setColour(Theme::second.withAlpha(0.07f));
            g.fillRect((float)plot.getX(), upper, (float)plot.getWidth(), lower - upper);

            for (float y : { upper, lower })
            {
                if (y < (float)plot.getY() || y > (float)plot.getBottom())
                    continue;

                g.setColour(Theme::second.withAlpha(0.95f));
                for (float x = (float)plot.getX(); x < (float)plot.getRight(); x += 10.f)
                    g.fillRect(x, y - 0.75f, 6.f, 1.5f);
            }

            g.setColour(Theme::second);
            const auto labelBox = juce::Rectangle<float>(74.f, 15.f).withRightX((float)plot.getRight() - 4.f).withBottomY(juce::jmax(upper - 2.f, (float)plot.getY() + 15.f));
            g.drawText(juce::String(settings.guideDb, 1).replace("-", juce::String(juce::CharPointer_UTF8("\xe2\x88\x92"))) + " dB", labelBox, juce::Justification::centredRight);
        }
    }

    // The button of the guide at the corner, which shows with the lenses
    if (viewHovered)
    {
        const auto area = guideButtonArea();
        g.setColour(Theme::panel.withAlpha(0.88f));
        g.fillRoundedRectangle(area.toFloat(), 4.f);
        g.setColour(settings.guideOn ? Theme::second : Theme::panelEdge);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.f, 1.f);

        // Two dashes at the sides of a line, as the sign of the guide
        g.setColour(settings.guideOn ? Theme::second : Theme::textDim);
        const auto icon = area.toFloat().withWidth(22.f).reduced(5.f, 0.f);
        for (float dy : { -4.f, 4.f })
            g.fillRect(icon.getX(), icon.getCentreY() + dy - 0.75f, icon.getWidth(), 1.5f);

        g.setFont(Theme::labelFont());
        g.setColour(settings.guideOn ? Theme::second : Theme::text);
        g.drawText("GUIDE", area.withTrimmedLeft(24), juce::Justification::centredLeft);
    }

    // The time axis: "now" at the right edge when scrolling, and the time from the left in a sweep. In musical time it is
    // the grid of the music instead, with lines at the beats or their parts, and bars and beats at the foot.
    if (settings.musical && settings.spanBeats > 0.0)
    {
        const double spanBeats = settings.spanBeats;
        const double bpb = juce::jmax(1.0, settings.beatsPerBar);

        // The finest step that leaves no more than sixteen lines
        double step = 4.0 * bpb;
        for (double candidate : { 0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, bpb, 2.0 * bpb, 4.0 * bpb })
            if (spanBeats / candidate <= 16.0)
            {
                step = candidate;
                break;
            }

        // The position in the music of the right edge (scrolling) or of the left edge (sweep)
        const double nowPpq = settings.ppq >= 0.0 ? settings.ppq : juce::jmax(0.0, settings.hostSeconds) * settings.bpm / 60.0;
        const double leftPpq = settings.sweep ? sweepOriginPpq : nowPpq - spanBeats;

        g.setFont(Theme::font(10.5f));
        const double firstLine = std::ceil(leftPpq / step - 1.0e-9) * step;

        for (double line = firstLine; line <= leftPpq + spanBeats + 1.0e-9; line += step)
        {
            const float x = (float)plot.getX() + (float)plot.getWidth() * (float)((line - leftPpq) / spanBeats);

            const bool onBar = std::abs(std::fmod(line, bpb)) < 1.0e-6 || std::abs(std::fmod(line, bpb) - bpb) < 1.0e-6;
            const bool onBeat = std::abs(line - std::round(line)) < 1.0e-6;

            g.setColour(juce::Colours::white.withAlpha(onBar ? 0.22f : onBeat ? 0.12f : 0.06f));
            g.fillRect(x, (float)plot.getY(), 1.f, (float)plot.getHeight());

            if (onBeat && line > -1.0e-6)
            {
                const int bar = (int)std::floor(line / bpb + 1.0e-9) + 1;
                const int beat = (int)std::floor(std::fmod(line, bpb) + 1.0e-6) + 1;
                g.setColour(onBar ? Theme::text : Theme::textDim);
                g.drawText(onBar ? juce::String(bar) : juce::String(bar) + "." + juce::String(beat),
                           juce::Rectangle<float>(40.f, 14.f).withCentre({ juce::jlimit((float)plot.getX() + 14.f, (float)plot.getRight() - 14.f, x), (float)plot.getBottom() + 10.f }),
                           juce::Justification::centred);
            }
        }
    }
    else
    {
        const double span = (double)settings.spanSeconds;
        double step = 10.0;
        for (double candidate : { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0 })
            if (span / candidate <= 8.0)
            {
                step = candidate;
                break;
            }

        g.setFont(Theme::font(10.5f));
        for (double t = 0.0; t <= span + 1.0e-9; t += step)
        {
            const float x = settings.sweep ? (float)plot.getX() + (float)plot.getWidth() * (float)(t / span)
                                           : (float)plot.getRight() - (float)plot.getWidth() * (float)(t / span);

            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.fillRect(x, (float)plot.getY(), 1.f, (float)plot.getHeight());

            g.setColour(Theme::textDim);
            const auto label = formatSpan((float)t);
            const auto text = settings.sweep ? label : (t < 1.0e-9 ? juce::String("now") : juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) + label);
            g.drawText(text, juce::Rectangle<float>(56.f, 14.f).withCentre({ juce::jlimit((float)plot.getX() + 18.f, (float)plot.getRight() - 14.f, x), (float)plot.getBottom() + 10.f }), juce::Justification::centred);
        }
    }

    // The time code of the host, top left
    if (settings.timeCode)
    {
        g.setFont(Theme::font(15.f, true));
        g.setColour(Theme::text.withAlpha(0.9f));
        g.drawText(formatTimeCode(settings.hostSeconds), juce::Rectangle<int>(plot.getX() + 10, plot.getY() + 4, 200, 20), juce::Justification::centredLeft);
    }

    // The lenses: for the amplitude, and for the time
    for (int control = 0; control < numControls && viewHovered; ++control)
    {
        const auto area = controlArea(control);
        g.setColour(Theme::panel.withAlpha(0.88f));
        g.fillRoundedRectangle(area.toFloat(), 4.f);
        g.setColour(Theme::panelEdge);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.f, 1.f);

        auto icon = area.toFloat();
        const auto minus = icon.removeFromLeft((float)buttonWidth);
        const auto plus = icon.removeFromRight((float)buttonWidth);
        const bool hovered = hoverControl == control;

        g.setFont(Theme::font(15.f, true));
        g.setColour(hovered && hoverPart < 0 ? juce::Colours::white : Theme::text);
        g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")), minus, juce::Justification::centred);
        g.setColour(hovered && hoverPart > 0 ? juce::Colours::white : Theme::text);
        g.drawText("+", plus, juce::Justification::centred);

        // The arrows say which way the lens works: up and down for the amplitude, left and right for the time
        g.setColour(Theme::textDim);
        const auto centre = icon.getCentre().translated(-icon.getWidth() * 0.5f + 7.f, 0.f);
        juce::Path arrows;
        if (control == 0)
        {
            arrows.addTriangle(centre.x, centre.y - 6.f, centre.x - 3.5f, centre.y - 2.f, centre.x + 3.5f, centre.y - 2.f);
            arrows.addTriangle(centre.x, centre.y + 6.f, centre.x - 3.5f, centre.y + 2.f, centre.x + 3.5f, centre.y + 2.f);
        }
        else
        {
            arrows.addTriangle(centre.x - 6.f, centre.y, centre.x - 2.f, centre.y - 3.5f, centre.x - 2.f, centre.y + 3.5f);
            arrows.addTriangle(centre.x + 6.f, centre.y, centre.x + 2.f, centre.y - 3.5f, centre.x + 2.f, centre.y + 3.5f);
        }
        g.fillPath(arrows);

        g.setFont(Theme::font(11.f));
        g.setColour(hovered && hoverPart == 0 ? juce::Colours::white : Theme::text);
        const auto value = control == 0 ? juce::String(settings.zoom, 1) + "x" : (settings.spanLabel.isNotEmpty() ? settings.spanLabel : formatSpan(settings.spanSeconds));
        g.drawText(value, icon.withTrimmedLeft(16.f), juce::Justification::centred);
    }
}
