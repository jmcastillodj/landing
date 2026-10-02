#include "SpectrogramView.h"

//==============================================================================
SpectrogramView::SpectrogramView(SpectrumSource& spectrumSource) : source(spectrumSource)
{
    setOpaque(true);
    display.numPoints = numRows;
    pending.fill(SpectrumEngine::silenceDb);

    setPalette(0);
}

void SpectrogramView::setPalette(int index)
{
    if (index == paletteIndex)
        return;

    paletteIndex = index;

    juce::ColourGradient gradient;
    auto stops = [&](std::initializer_list<std::pair<double, juce::uint32>> list)
    {
        for (auto& stop : list)
            gradient.addColour(stop.first, juce::Colour(stop.second));
    };

    switch (index)
    {
        case 1: // Magma: black through purple and red to a pale yellow
            stops({ { 0.0, 0xff000004 }, { 0.13, 0xff180f3e }, { 0.25, 0xff451077 }, { 0.38, 0xff721f81 }, { 0.5, 0xff9f2f7f },
                    { 0.63, 0xffcd4071 }, { 0.75, 0xfff1605d }, { 0.88, 0xfffd9567 }, { 0.95, 0xfffeca8d }, { 1.0, 0xfffcfdbf } });
            break;
        case 2: // Viridis: purple through teal and green to yellow
            stops({ { 0.0, 0xff440154 }, { 0.25, 0xff3b528b }, { 0.5, 0xff21908c }, { 0.75, 0xff5dc863 }, { 1.0, 0xfffde725 } });
            break;
        case 3: // Ice: black and navy, cyan, then white
            stops({ { 0.0, 0xff02060c }, { 0.3, 0xff0b2a55 }, { 0.6, 0xff1f7fc4 }, { 0.85, 0xff7fe0ff }, { 1.0, 0xffffffff } });
            break;
        case 4: // Rainbow: the colours of a spectrum, as the spectrograms of audio editors have them
            stops({ { 0.0, 0xff000000 }, { 0.12, 0xff1a0a5c }, { 0.28, 0xff1d3fd0 }, { 0.42, 0xff12b4c8 }, { 0.56, 0xff2fd26a },
                    { 0.7, 0xffe8e03a }, { 0.84, 0xfff0772a }, { 0.94, 0xffe5301f }, { 1.0, 0xffffe0d8 } });
            break;
        case 5: // Grayscale
            stops({ { 0.0, 0xff000000 }, { 1.0, 0xffffffff } });
            break;
        default:
            // The picture is dark, and what is loud is a light in it. What is quiet is a hint of the blue of the
            // meters, and then the level rises through the amber of the signal to its yellow and on to a warm white,
            // so that the lightness rises with the level all the way. The blue meets the amber while both are dark:
            // a bright blue beside a bright yellow makes a grey green of everything between them.
            stops({ { 0.0, Theme::displayTop.getARGB() }, { 0.2, 0xff0a1626 }, { 0.4, 0xff0e2c4d }, { 0.56, 0xff50381a },
                    { 0.74, 0xffbd7d20 }, { 0.88, Theme::warn.getARGB() }, { 1.0, 0xfffff1c8 } });
            break;
    }

    for (size_t i = 0; i < colourTable.size(); ++i)
        colourTable[i] = gradient.getColourAtPosition((double)i / (double)(colourTable.size() - 1));

    rebuildImage();
    repaint();
}

void SpectrogramView::mouseDown(const juce::MouseEvent& e)
{
    if ((e.mods.isPopupMenu() || e.mods.isCtrlDown()) && onContextMenu)
        onContextMenu();
}

float SpectrogramView::yOf(double frequency) const
{
    const double proportion = std::log(frequency / display.minFrequency) / std::log(display.maxFrequency / display.minFrequency);
    return (float)plot.getBottom() - (float)proportion * (float)plot.getHeight();
}

void SpectrogramView::resized()
{
    // The same margins as the history, so that a moment is at the same place in both
    plot = getLocalBounds().withTrimmedLeft(34).withTrimmedRight(34).withTrimmedTop(8).withTrimmedBottom(20);
    rebuildImage();
}

void SpectrogramView::setSpan(float seconds)
{
    if (!juce::exactlyEqual(seconds, spanSeconds))
    {
        spanSeconds = seconds;
        rebuildImage();
        repaint();
    }
}

void SpectrogramView::record(int numNewSlots, bool hasNewSpectra, float tiltDbPerOctave, bool frozen)
{
    // Releasing the picture brings it up to date with what was recorded while it stood still
    if (isFrozen && !frozen)
    {
        isFrozen = false;
        rebuildImage();
        repaint();
    }

    isFrozen = frozen;

    if (hasNewSpectra)
    {
        display.tiltDbPerOctave = tiltDbPerOctave;
        source.getEngine().render(SpectrumEngine::Curve::mid, display, source.getSampleRate(), spectrum);

        // A slot shows the highest level that each frequency reached during it
        for (size_t row = 0; row < pending.size(); ++row)
            pending[row] = juce::jmax(pending[row], spectrum[row]);

        hasPending = true;
    }

    if (numNewSlots <= 0)
        return;

    Column column = lastColumn;

    if (hasPending)
    {
        for (size_t row = 0; row < column.size(); ++row)
        {
            const float proportion = juce::jlimit(0.f, 1.f, (pending[row] - minDecibels) / (maxDecibels - minDecibels));
            column[row] = (juce::uint8)juce::roundToInt(std::pow(proportion, contrast) * 255.f);
        }

        lastColumn = column;
    }

    // A frame that took long completes more than one slot, which all show the same spectrum
    for (int slot = 0; slot < numNewSlots; ++slot)
    {
        history.push(column);

        if (!isFrozen)
            addColumn(column);
    }

    pending.fill(SpectrumEngine::silenceDb);
    hasPending = false;

    if (!isFrozen)
        repaint(plot);
}

void SpectrogramView::addColumn(const Column& column)
{
    // The first recorded row is the lowest frequency, which belongs at the bottom of the image
    const int height = image.getHeight();
    const float rowsPerPixel = height > 1 ? (float)(numRows - 1) / (float)(height - 1) : 0.f;

    image.addColumn([&](int pixel)
    {
        const auto row = (size_t)juce::jlimit(0, numRows - 1, juce::roundToInt((float)(height - 1 - pixel) * rowsPerPixel));
        return colourTable[column[row]];
    });
}

void SpectrogramView::rebuildImage()
{
    if (plot.isEmpty())
        return;

    // One column for every slot of the span, from the oldest to the newest. A slot that
    // nothing was recorded in is the color of silence.
    const int numSlots = Timeline::slotsIn(spanSeconds);
    image.setSize(numSlots, plot.getHeight(), Theme::displayTop);

    Column silence;
    silence.fill(0);

    for (int age = numSlots - 1; age >= 0; --age)
    {
        const auto* column = history.fromNewest(age);
        addColumn(column != nullptr ? *column : silence);
    }
}

void SpectrogramView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());
    image.draw(g, plot);

    // Mark the frequency axis, stronger at the decades
    g.setFont(Theme::font(10.5f));
    for (const auto& [frequency, text] : { std::pair<double, const char*> { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                                          { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
    {
        const bool isDecade = juce::exactlyEqual(frequency, 100.0) || juce::exactlyEqual(frequency, 1000.0) || juce::exactlyEqual(frequency, 10000.0);
        const float y = yOf(frequency);

        g.setColour(juce::Colours::white.withAlpha(isDecade ? 0.14f : 0.06f));
        g.fillRect((float)plot.getX(), y, (float)plot.getWidth(), 1.f);
        g.setColour(isDecade ? Theme::textDim : Theme::textFaint);
        g.drawText(text, juce::Rectangle<float>(0.f, y - 7.f, (float)plot.getX() - 6.f, 14.f), juce::Justification::centredRight);
    }

    Timeline::drawTimeAxis(g, plot, spanSeconds);

    if (hoverY.has_value())
    {
        const double proportion = (double)(plot.getBottom() - *hoverY) / (double)juce::jmax(1, plot.getHeight());
        const double frequency = display.minFrequency * std::pow(display.maxFrequency / display.minFrequency, proportion);

        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.fillRect((float)plot.getX(), (float)*hoverY, (float)plot.getWidth(), 1.f);

        const auto text = Theme::formatFrequency(frequency) + "    " + Theme::formatNote(frequency);
        auto box = juce::Rectangle<int>(Theme::textWidth(Theme::controlFont(), text) + 20, 24)
                       .withPosition(plot.getX() + 10, juce::jlimit(plot.getY(), plot.getBottom() - 24, *hoverY - 30));

        Theme::drawPanel(g, box.toFloat());
        g.setFont(Theme::controlFont());
        g.setColour(Theme::text);
        g.drawText(text, box, juce::Justification::centred);
    }
}

void SpectrogramView::mouseMove(const juce::MouseEvent& e)
{
    const std::optional<int> newHover = plot.contains(e.getPosition()) ? std::optional(e.getPosition().y) : std::nullopt;
    if (newHover != hoverY)
    {
        hoverY = newHover;
        repaint();
    }
}

void SpectrogramView::mouseExit(const juce::MouseEvent&)
{
    if (hoverY.has_value())
    {
        hoverY.reset();
        repaint();
    }
}

void SpectrogramView::clearHistory()
{
    lastColumn.fill(0);
    history.clear();
    rebuildImage();
    repaint();
}
