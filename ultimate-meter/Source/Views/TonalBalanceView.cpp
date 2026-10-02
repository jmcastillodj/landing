#include "TonalBalanceView.h"
#include "TargetFromFile.h"

namespace
{
    const juce::Identifier targetNameProperty { "tonalTarget" };
    const juce::Identifier customTargetsProperty { "tonalCustomTargets" };

    const juce::Colour targetColour { 0xff2ec4b6 };
    const juce::Colour measuredColour { 0xffe3eaf1 };

    }

TonalBalanceView::TonalBalanceView(SpectrumSource& spectrumSource, juce::ValueTree& sessionState) : source(spectrumSource), state(sessionState)
{
    setOpaque(true);
    powerSum.assign((size_t)TonalTargets::numPoints, 0.0);
    targets = TonalTargets::builtIn();

    loadCustomTargets();

    const auto saved = state.getProperty(targetNameProperty, "Modern").toString();
    targetName = std::any_of(targets.begin(), targets.end(), [&](auto& t) { return t.name == saved; }) ? saved : "Modern";
}

//==============================================================================
void TonalBalanceView::loadCustomTargets()
{
    for (auto& line : juce::StringArray::fromLines(state.getProperty(customTargetsProperty, "").toString()))
        if (auto target = TonalTargets::parse(line))
            targets.push_back(std::move(*target));
}

void TonalBalanceView::saveCustomTargets()
{
    juce::StringArray lines;
    for (auto& target : targets)
        if (!target.builtIn)
            lines.add(TonalTargets::serialise(target));

    state.setProperty(customTargetsProperty, lines.joinIntoString("\n"), nullptr);
}

const TonalTargets::Target* TonalBalanceView::currentTarget() const
{
    for (auto& target : targets)
        if (target.name == targetName)
            return &target;

    return targets.empty() ? nullptr : &targets.front();
}

void TonalBalanceView::selectTarget(const juce::String& name)
{
    targetName = name;
    state.setProperty(targetNameProperty, name, nullptr);
    repaint();
}

void TonalBalanceView::addTargetItems(juce::PopupMenu& menu)
{
    juce::Component::SafePointer<TonalBalanceView> safe(this);

    menu.addSectionHeader("Target");
    for (auto& target : targets)
        if (target.builtIn)
            menu.addItem(target.name, true, target.name == targetName, [safe, name = target.name] { if (safe != nullptr) safe->selectTarget(name); });

    bool hasCustom = false;
    for (auto& target : targets)
        if (!target.builtIn)
        {
            if (!hasCustom)
            {
                menu.addSectionHeader("Your targets");
                hasCustom = true;
            }

            menu.addItem(target.name, true, target.name == targetName, [safe, name = target.name] { if (safe != nullptr) safe->selectTarget(name); });
        }

    menu.addSectionHeader("Make a target");
    menu.addItem("From an audio file...", [safe] { if (safe != nullptr) safe->chooseFile(); });

    if (hasCustom)
    {
        juce::PopupMenu removeMenu;
        for (auto& target : targets)
            if (!target.builtIn)
                removeMenu.addItem(target.name, [safe, name = target.name]
                {
                    if (safe == nullptr)
                        return;

                    const bool wasSelected = safe->targetName == name;
                    safe->targets.erase(std::remove_if(safe->targets.begin(), safe->targets.end(), [&](auto& t) { return !t.builtIn && t.name == name; }), safe->targets.end());
                    safe->saveCustomTargets();
                    if (wasSelected)
                        safe->selectTarget("Modern");
                    safe->repaint();
                });

        menu.addSubMenu("Remove", removeMenu);
    }
}

void TonalBalanceView::mouseDown(const juce::MouseEvent& e)
{
    // A click starts the average again. The secondary click is for the menu, which the editor shows.
    if (!e.mods.isPopupMenu())
        clearHistory();
}

void TonalBalanceView::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser>("Choose a recording to make a target from", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<TonalBalanceView>(this)](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe != nullptr && file.existsAsFile())
                safe->openFile(file);
        });
}

void TonalBalanceView::openFile(const juce::File& file)
{
    auto content = std::make_unique<TargetFromFile>(file,
        [safe = juce::Component::SafePointer<TonalBalanceView>(this)](TonalTargets::Target target)
        {
            if (safe == nullptr)
                return;

            // A name that is taken gets a number
            juce::String name = target.name;
            for (int n = 2; std::any_of(safe->targets.begin(), safe->targets.end(), [&](auto& t) { return t.name == name; }); ++n)
                name = target.name + " " + juce::String(n);

            target.name = name;
            safe->targets.push_back(std::move(target));
            safe->saveCustomTargets();
            safe->selectTarget(name);
        },
        [safe = juce::Component::SafePointer<TonalBalanceView>(this)]
        {
            if (safe != nullptr && safe->dialog != nullptr)
                if (auto* window = dynamic_cast<juce::DialogWindow*>(safe->dialog.getComponent()))
                    window->exitModalState(0);
        });

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content.release());
    options.dialogTitle = "Target from a recording";
    options.dialogBackgroundColour = Theme::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    dialog = options.launchAsync();
}

//==============================================================================
void TonalBalanceView::setFine(bool shouldBeFine)
{
    if (shouldBeFine != fine)
    {
        fine = shouldBeFine;
        layout = TonalTargets::display(fine ? 1.f / 3.f : 1.f);
        repaint();
    }
}

void TonalBalanceView::clearHistory()
{
    std::fill(powerSum.begin(), powerSum.end(), 0.0);
    frames = 0.0;
    pendingSeconds = 0.0;
    repaint();
}

void TonalBalanceView::record(bool hasNewSpectra, float elapsedSeconds, float averageSeconds)
{
    pendingSeconds += (double)elapsedSeconds;
    if (!hasNewSpectra)
        return;

    // With a span the older frames fade out, so that the balance follows the music instead of settling for good
    const double decay = averageSeconds > 0.f ? std::exp(-pendingSeconds / (double)averageSeconds) : 1.0;
    pendingSeconds = 0.0;

    source.getEngine().render(SpectrumEngine::Curve::mid, layout, source.getSampleRate(), rendered);

    // Silence adds nothing to the balance
    float loudest = -200.f;
    for (float value : rendered)
        loudest = juce::jmax(loudest, value);

    if (loudest < -85.f || rendered.size() != powerSum.size())
        return;

    for (size_t point = 0; point < rendered.size(); ++point)
        powerSum[point] = powerSum[point] * decay + std::pow(10.0, (double)juce::jmax(rendered[point], -150.f) / 10.0);

    frames = frames * decay + 1.0;

    if (++framesSincePaint >= 3)
    {
        framesSincePaint = 0;
        repaint();
    }
}

std::vector<float> TonalBalanceView::measuredCurve() const
{
    std::vector<float> curve(powerSum.size(), 0.f);
    if (frames <= 0.0)
        return curve;

    for (size_t point = 0; point < curve.size(); ++point)
        curve[point] = (float)(10.0 * std::log10(powerSum[point] / frames + 1.0e-15));

    // A few passes of a small average smooth the line out, so that it follows the shape of the spectrum and not its ripple
    for (int pass = 0; pass < 3; ++pass)
    {
        auto previous = curve;
        for (size_t point = 1; point + 1 < curve.size(); ++point)
            curve[point] = 0.25f * previous[point - 1] + 0.5f * previous[point] + 0.25f * previous[point + 1];
    }

    TonalTargets::removeAverage(curve);
    return curve;
}

//==============================================================================
void TonalBalanceView::resized()
{
    plot = getLocalBounds().withTrimmedTop(46).withTrimmedBottom(22).withTrimmedLeft(8).withTrimmedRight(40);
}

float TonalBalanceView::xOf(double frequency) const
{
    const double proportion = std::log(frequency / TonalTargets::minFrequency) / std::log(TonalTargets::maxFrequency / TonalTargets::minFrequency);
    return (float)plot.getX() + (float)proportion * (float)plot.getWidth();
}

float TonalBalanceView::yOf(float decibels) const
{
    return juce::jmap(juce::jlimit(bottomDb, topDb, decibels), bottomDb, topDb, (float)plot.getBottom(), (float)plot.getY());
}

juce::Path TonalBalanceView::bandPath(const std::vector<float>& low, const std::vector<float>& high) const
{
    juce::Path path;
    const size_t n = low.size();
    if (n < 2)
        return path;

    const float step = (float)plot.getWidth() / (float)(n - 1);
    for (size_t point = 0; point < n; ++point)
    {
        const float x = (float)plot.getX() + step * (float)point;
        if (point == 0)
            path.startNewSubPath(x, yOf(high[point]));
        else
            path.lineTo(x, yOf(high[point]));
    }

    for (size_t point = n; point-- > 0;)
        path.lineTo((float)plot.getX() + step * (float)point, yOf(low[point]));

    path.closeSubPath();
    return path;
}

void TonalBalanceView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    if (plot.isEmpty())
        return;

    // The slope that is put back in the curves, at each of their points
    auto shift = [](size_t point) { return naturalSlopeDbPerOctave * (float)std::log2(TonalTargets::frequencyOf((int)point) / 1000.0); };

    // The grid: the frequencies, and a line for every 12 dB
    g.setFont(Theme::font(10.5f));
    for (double frequency : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xOf(frequency);
        g.setColour(Theme::grid);
        g.fillRect(x, (float)plot.getY(), 1.f, (float)plot.getHeight());
        g.setColour(Theme::textDim);
        const auto label = frequency < 1000.0 ? juce::String((int)frequency) : juce::String((int)(frequency / 1000.0)) + "k";
        g.drawText(label, juce::Rectangle<float>(40.f, 14.f).withCentre({ juce::jlimit((float)plot.getX() + 14.f, (float)plot.getRight() - 14.f, x), (float)plot.getBottom() + 11.f }), juce::Justification::centred);
    }

    for (int db = -48; db <= 24; db += 12)
    {
        g.setColour(db == 0 ? Theme::gridStrong : Theme::grid);
        g.fillRect((float)plot.getX(), yOf((float)db), (float)plot.getWidth(), 1.f);
        g.setColour(Theme::textFaint);
        g.drawText((db > 0 ? "+" : "") + juce::String(db), juce::Rectangle<float>((float)plot.getRight() + 4.f, yOf((float)db) - 7.f, 34.f, 14.f), juce::Justification::centredLeft);
    }

    // The bands, with the edges between them
    const std::array<const char*, 4> bandNames { "Low", "Low-Mid", "High-Mid", "High" };
    const std::array<double, 5> edges { TonalTargets::minFrequency, bandEdges[0], bandEdges[1], bandEdges[2], TonalTargets::maxFrequency };

    for (size_t band = 1; band < 4; ++band)
    {
        g.setColour(Theme::gridStrong);
        g.fillRect(xOf(edges[band]), (float)plot.getY() - 14.f, 1.f, (float)plot.getHeight() + 14.f);
    }

    const auto* target = currentTarget();
    const auto measured = measuredCurve();
    std::array<BandReading, 4> readings;

    if (target != nullptr && target->centre.size() == measured.size())
    {
        // The band around the target, with a line on each edge, and a line through its middle
        std::vector<float> low(measured.size()), high(measured.size());
        for (size_t point = 0; point < measured.size(); ++point)
        {
            low[point] = target->centre[point] - target->tolerance[point] + shift(point);
            high[point] = target->centre[point] + target->tolerance[point] + shift(point);
        }

        const auto area = bandPath(low, high);
        const auto bounds = area.getBounds();
        g.setGradientFill(juce::ColourGradient(targetColour.withAlpha(0.10f), 0.f, bounds.getY(), targetColour.withAlpha(0.32f), 0.f, bounds.getCentreY(), false));
        g.fillPath(area);

        const float step = (float)plot.getWidth() / (float)(measured.size() - 1);
        for (auto* edge : { &low, &high })
        {
            juce::Path line;
            for (size_t point = 0; point < edge->size(); ++point)
            {
                const float x = (float)plot.getX() + step * (float)point;
                if (point == 0)
                    line.startNewSubPath(x, yOf((*edge)[point]));
                else
                    line.lineTo(x, yOf((*edge)[point]));
            }
            g.setColour(targetColour.withAlpha(0.5f));
            g.strokePath(line, juce::PathStrokeType(1.f));
        }

        // How far the spectrum is from the target in each band
        for (size_t point = 0; point < measured.size(); ++point)
        {
            const double frequency = TonalTargets::frequencyOf((int)point);
            for (size_t band = 0; band < 4; ++band)
                if (frequency >= edges[band] && frequency < edges[band + 1] + (band == 3 ? 1.0 : 0.0))
                {
                    readings[band].deviation += measured[point] - target->centre[point];
                    readings[band].tolerance += target->tolerance[point];
                    readings[band].valid = true;
                    break;
                }
        }

        // Count the points of each band
        std::array<int, 4> counts {};
        for (size_t point = 0; point < measured.size(); ++point)
        {
            const double frequency = TonalTargets::frequencyOf((int)point);
            for (size_t band = 0; band < 4; ++band)
                if (frequency >= edges[band] && frequency < edges[band + 1] + (band == 3 ? 1.0 : 0.0))
                {
                    ++counts[band];
                    break;
                }
        }

        for (size_t band = 0; band < 4; ++band)
            if (counts[band] > 0)
            {
                readings[band].deviation /= (float)counts[band];
                readings[band].tolerance /= (float)counts[band];
            }
    }

    // The spectrum, as a line over the target, once there is anything to show
    if (frames > 0)
    {
        juce::Path line;
        const float step = (float)plot.getWidth() / (float)(measured.size() - 1);
        for (size_t point = 0; point < measured.size(); ++point)
        {
            const float x = (float)plot.getX() + step * (float)point;
            if (point == 0)
                line.startNewSubPath(x, yOf(measured[point] + shift(point)));
            else
                line.lineTo(x, yOf(measured[point] + shift(point)));
        }

        g.setColour(measuredColour.withAlpha(0.18f));
        g.strokePath(line, juce::PathStrokeType(6.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(measuredColour);
        g.strokePath(line, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        g.setFont(Theme::font(13.f));
        g.setColour(Theme::textDim);
        g.drawText("Play your mix: the balance is the average since the last reset.", plot, juce::Justification::centred);
    }

    // The names of the bands along the top, each with how far from the target the spectrum is in it
    for (size_t band = 0; band < 4; ++band)
    {
        const float left = xOf(edges[band]), right = xOf(edges[band + 1]);
        auto area = juce::Rectangle<float>(left, (float)plot.getY() - 40.f, right - left, 38.f);

        g.setFont(Theme::font(12.f));
        g.setColour(Theme::text);
        g.drawText(bandNames[band], area.removeFromTop(18.f), juce::Justification::centred);

        if (frames > 0 && readings[band].valid)
        {
            const float deviation = readings[band].deviation;
            const bool inside = std::abs(deviation) <= readings[band].tolerance;
            g.setFont(Theme::font(11.f, true));
            g.setColour(inside ? Theme::good : (std::abs(deviation) > 1.6f * readings[band].tolerance ? Theme::over : Theme::warn));
            g.drawText(Theme::formatDb(deviation) + " dB", area, juce::Justification::centred);
        }
    }

    // A reminder of what a click does
    g.setFont(Theme::labelFont());
    g.setColour(Theme::textFaint);
    g.drawText("CLICK TO START THE AVERAGE AGAIN", juce::Rectangle<int>(plot.getRight() - 300, plot.getBottom() - 20, 296, 16), juce::Justification::centredRight);

    // The name of the target, bottom right of the top bar
    g.setFont(Theme::labelFont());
    g.setColour(targetColour);
    g.drawText("TARGET: " + targetName.toUpperCase(), getLocalBounds().withHeight(16).reduced(10, 0).withY(2), juce::Justification::centredLeft);
}
