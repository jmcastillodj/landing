#include "ReferenceView.h"

namespace
{
    const juce::Identifier filesProperty { "referenceFiles" };
    constexpr float curveRangeDb = 6.f;
    constexpr float peakFloorDb = -36.f;
    constexpr float lufsFloor = -30.f;

    juce::String formatDb(float value, int decimals = 1)
    {
        const auto text = juce::String(value, decimals).replace("-", juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")));
        return value > 0.05f ? "+" + text : text;
    }
}

ReferenceView::ReferenceView(ReferenceManager& m, juce::ValueTree& stateTree) : manager(m), state(stateTree)
{
    setOpaque(true);
    selected = juce::jlimit(0, ReferenceManager::numSlots - 1, manager.activeSlot.load());
    manager.onChange = [this] { repaint(); };
    restoreFiles();
}

ReferenceView::~ReferenceView()
{
    manager.onChange = nullptr;
}

//==============================================================================
void ReferenceView::restoreFiles()
{
    const auto paths = juce::StringArray::fromTokens(state.getProperty(filesProperty, "").toString(), "|", "");
    for (int slot = 0; slot < ReferenceManager::numSlots && slot < paths.size(); ++slot)
        if (paths[slot].isNotEmpty() && manager.getTrack(slot) == nullptr)
        {
            const juce::File file(paths[slot]);
            if (file.existsAsFile())
                manager.load(slot, file);
        }
}

void ReferenceView::saveFiles()
{
    juce::StringArray paths;
    for (int slot = 0; slot < ReferenceManager::numSlots; ++slot)
    {
        auto track = manager.getTrack(slot);
        paths.add(track != nullptr ? track->file.getFullPathName() : juce::String());
    }
    state.setProperty(filesProperty, paths.joinIntoString("|"), nullptr);
}

void ReferenceView::select(int slot)
{
    selected = slot;
    manager.activeSlot.store(slot);
    manager.playPosition.store(0);
    repaint();
}

void ReferenceView::chooseFile(int slot)
{
    chooser = std::make_unique<juce::FileChooser>("Choose a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<ReferenceView>(this), slot](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe == nullptr || !file.existsAsFile())
                return;

            safe->manager.load(slot, file);
            safe->select(slot);

            // The file is kept in the session once the track has been read, which the manager reports
            juce::Timer::callAfterDelay(400, [safe] { if (safe != nullptr) safe->saveFiles(); });
        });
}

void ReferenceView::removeSelected()
{
    manager.remove(selected);
    saveFiles();
}

void ReferenceView::smartLoop()
{
    manager.smartLoop(selected);
}

//==============================================================================
void ReferenceView::update(const Mix& newMix, float)
{
    mix = newMix;

    // Level match plays the reference as loud as the mix is, once there is a loudness for the mix
    auto track = manager.getTrack(selected);
    float wanted = 0.f;
    if (track != nullptr && levelMatch && mix.integratedLufs > -100.f)
        wanted = juce::jlimit(-24.f, 24.f, mix.integratedLufs - track->lufs);
    else if (track != nullptr && levelMatch)
        wanted = gainDb;

    gainDb = wanted;
    manager.gainDb.store(gainDb);

    computeMatch();

    // The playhead moves, and the numbers change, so the view is drawn again with the frame
    if (isVisible())
        repaint();
}

void ReferenceView::computeMatch()
{
    correction.clear();
    matchPercent = -1.f;

    auto track = manager.getTrack(selected);
    if (track == nullptr || mix.curve.size() != track->tonal.centre.size() || mix.curve.empty())
        return;

    // The mix has been smoothed more than the track was when it was measured, so the track is brought to the same smoothness
    auto referenceCurve = track->tonal.centre;
    for (int pass = 0; pass < 4; ++pass)
    {
        auto previous = referenceCurve;
        for (size_t i = 1; i + 1 < referenceCurve.size(); ++i)
            referenceCurve[i] = 0.25f * previous[i - 1] + 0.5f * previous[i] + 0.25f * previous[i + 1];
    }

    correction.resize(mix.curve.size());
    for (size_t i = 0; i < correction.size(); ++i)
        correction[i] = referenceCurve[i] - mix.curve[i];

    for (int pass = 0; pass < 2; ++pass)
    {
        auto previous = correction;
        for (size_t i = 1; i + 1 < correction.size(); ++i)
            correction[i] = 0.25f * previous[i - 1] + 0.5f * previous[i] + 0.25f * previous[i + 1];
    }

    // The tone counts for most, then the width and the dynamics
    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < correction.size(); ++i)
    {
        const double frequency = TonalTargets::frequencyOf((int)i);
        if (frequency >= 100.0 && frequency <= 10000.0)
        {
            sum += (double)correction[i] * correction[i];
            ++count;
        }
    }

    const float rms = count > 0 ? (float)std::sqrt(sum / count) : 0.f;
    const float tone = 1.f / (1.f + (rms / 3.f) * (rms / 3.f));
    const float width = 1.f - juce::jmin(1.f, std::abs(mix.width - track->width) / 0.5f);
    const float dynamics = mix.plr > 0.f ? 1.f - juce::jmin(1.f, std::abs(mix.plr - track->plr) / 8.f) : 0.5f;

    matchPercent = 100.f * (0.6f * tone + 0.2f * width + 0.2f * dynamics);
}

//==============================================================================
void ReferenceView::resized()
{
    auto area = getLocalBounds().reduced(14, 10);
    const int total = area.getHeight();

    tagsArea = area.removeFromTop(22);
    area.removeFromTop(4);
    waveArea = area.removeFromTop(juce::jlimit(46, 150, (int)(total * 0.26f)));
    area.removeFromTop(6);
    slotsArea = area.removeFromTop(30);
    area.removeFromTop(8);
    middleArea = area.removeFromTop(juce::jlimit(66, 120, (int)(total * 0.26f)));
    area.removeFromTop(8);
    curveArea = area;

    levelMatchArea = tagsArea.removeFromRight(110).withSizeKeepingCentre(110, 20);

    auto slots = slotsArea;
    const int cell = slots.getWidth() / ReferenceManager::numSlots;
    for (int i = 0; i < ReferenceManager::numSlots; ++i)
    {
        slotAreas[(size_t)i] = slots.removeFromLeft(cell).reduced(2, 0);
        removeAreas[(size_t)i] = slotAreas[(size_t)i].removeFromRight(26);
    }

    auto centre = middleArea.withSizeKeepingCentre(juce::jlimit(150, 240, middleArea.getWidth() / 4), middleArea.getHeight());
    originalArea = centre.removeFromTop(centre.getHeight() / 2).reduced(0, 2);
    referenceArea = centre.reduced(0, 2);
}

int ReferenceView::sampleAt(float x, const ReferenceManager::Track& track) const
{
    const float proportion = juce::jlimit(0.f, 1.f, (x - (float)waveArea.getX()) / (float)juce::jmax(1, waveArea.getWidth()));
    return (int)(proportion * (float)track.length());
}

float ReferenceView::xOfSample(int sample, const ReferenceManager::Track& track) const
{
    return (float)waveArea.getX() + (float)waveArea.getWidth() * (float)sample / (float)juce::jmax(1, track.length());
}

float ReferenceView::xOfFrequency(double frequency) const
{
    const double proportion = std::log(frequency / TonalTargets::minFrequency) / std::log(TonalTargets::maxFrequency / TonalTargets::minFrequency);
    return (float)curveArea.getX() + 38.f + (float)proportion * (float)(curveArea.getWidth() - 46);
}

//==============================================================================
void ReferenceView::paint(juce::Graphics& g)
{
    Theme::fillDisplay(g, getLocalBounds());

    auto track = manager.getTrack(selected);
    const bool listening = manager.monitoring.load();

    // The words that describe the track, and the level match switch
    {
        g.setFont(Theme::font(13.f));
        g.setColour(track != nullptr ? Theme::text : Theme::textDim);
        const auto words = track != nullptr ? track->tags.joinIntoString(",  ") : juce::String("Load a reference track to compare your mix with it");
        g.drawText(words, tagsArea, juce::Justification::centredLeft);

        const bool on = levelMatch;
        g.setColour(on ? Theme::accentDeep : Theme::track);
        g.fillRoundedRectangle(levelMatchArea.toFloat(), 4.f);
        g.setColour(on ? Theme::accent : Theme::panelEdge);
        g.drawRoundedRectangle(levelMatchArea.toFloat().reduced(0.5f), 4.f, 1.f);
        g.setFont(Theme::labelFont());
        g.setColour(on ? juce::Colours::white : Theme::textDim);
        g.drawText(on ? "LEVEL MATCH " + formatDb(gainDb) : juce::String("LEVEL MATCH"), levelMatchArea, juce::Justification::centred);
    }

    // The waveform of the track, and the part that loops
    {
        g.setColour(Theme::display.darker(0.3f));
        g.fillRect(waveArea);

        if (track == nullptr)
        {
            g.setFont(Theme::font(13.f));
            g.setColour(Theme::textFaint);
            g.drawText(manager.isLoading() ? "Reading the track..." : "Click here to load a reference track", waveArea, juce::Justification::centred);
        }
        else
        {
            const float midY = (float)waveArea.getCentreY();
            const float half = (float)waveArea.getHeight() * 0.46f;
            const int columns = (int)track->thumbLow.size();

            juce::Path outline;
            for (int x = 0; x < waveArea.getWidth(); ++x)
            {
                const int column = juce::jlimit(0, columns - 1, x * columns / juce::jmax(1, waveArea.getWidth()));
                const float top = midY - track->thumbHigh[(size_t)column] * half;
                const float bottom = midY - track->thumbLow[(size_t)column] * half;
                g.setColour(Theme::accentDeep.withAlpha(0.9f));
                g.fillRect((float)(waveArea.getX() + x), top, 1.f, juce::jmax(1.f, bottom - top));
            }

            const int start = dragging ? juce::jmin(dragFrom, dragTo) : track->regionStart.load();
            const int end = dragging ? juce::jmax(dragFrom, dragTo) : track->regionEnd.load();
            const auto region = juce::Rectangle<float>(xOfSample(start, *track), (float)waveArea.getY(), xOfSample(end, *track) - xOfSample(start, *track), (float)waveArea.getHeight());

            // The part that loops is brighter, over the dim wave
            g.saveState();
            g.reduceClipRegion(region.toNearestInt());
            for (int x = 0; x < waveArea.getWidth(); ++x)
            {
                const int column = juce::jlimit(0, columns - 1, x * columns / juce::jmax(1, waveArea.getWidth()));
                const float top = midY - track->thumbHigh[(size_t)column] * half;
                const float bottom = midY - track->thumbLow[(size_t)column] * half;
                g.setColour(Theme::accent);
                g.fillRect((float)(waveArea.getX() + x), top, 1.f, juce::jmax(1.f, bottom - top));
            }
            g.restoreState();

            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillRect(region.getX(), region.getY(), 1.f, region.getHeight());
            g.fillRect(region.getRight(), region.getY(), 1.f, region.getHeight());

            // The playhead, while the reference is being listened to
            if (listening)
            {
                const float x = xOfSample(manager.playPosition.load(), *track);
                g.setColour(Theme::held);
                g.fillRect(x - 0.5f, (float)waveArea.getY(), 1.5f, (float)waveArea.getHeight());
            }

            g.setFont(Theme::font(10.5f));
            g.setColour(Theme::textDim);
            const auto seconds = (double)(end - start) / track->sampleRate;
            g.drawText("LOOP " + juce::String(seconds, 1) + " s  -  drag to choose, double click for the loudest part",
                       waveArea.reduced(6, 3), juce::Justification::bottomLeft);
        }
    }

    // The four slots
    for (int i = 0; i < ReferenceManager::numSlots; ++i)
    {
        auto slotTrack = manager.getTrack(i);
        const auto bounds = slotAreas[(size_t)i].toFloat();
        const bool isSelected = i == selected;

        g.setGradientFill(juce::ColourGradient(isSelected ? Theme::accentDeep : Theme::track, bounds.getX(), 0.f,
                                               Theme::track, bounds.getRight(), 0.f, false));
        g.fillRoundedRectangle(bounds.withTrimmedRight(-(float)removeAreas[(size_t)i].getWidth()), 3.f);

        g.setFont(Theme::font(12.f, isSelected));
        g.setColour(isSelected ? juce::Colours::white : slotTrack != nullptr ? Theme::text : Theme::textFaint);
        g.drawText(slotTrack != nullptr ? slotTrack->name.toUpperCase() : "+ REFERENCE " + juce::String(i + 1), slotAreas[(size_t)i].reduced(8, 0), juce::Justification::centredLeft, true);

        if (slotTrack != nullptr)
        {
            g.setColour(Theme::textDim);
            g.drawText(juce::String(juce::CharPointer_UTF8("\xc3\x97")), removeAreas[(size_t)i], juce::Justification::centred);
        }
    }

    // The peak and the loudness of the mix and of the reference
    {
        auto left = middleArea.withWidth((middleArea.getWidth() - originalArea.getWidth()) / 2).reduced(0, 0);
        auto right = middleArea.withLeft(middleArea.getRight() - left.getWidth());
        left.removeFromRight(16);
        right.removeFromLeft(16);

        auto drawBars = [&](juce::Rectangle<int> area, const juce::String& title, float mixValue, float referenceValue, float floorDb, float topDb, bool hasMix, bool hasReference)
        {
            g.setFont(Theme::labelFont());
            g.setColour(Theme::textDim);
            g.drawText(title, area.removeFromTop(16), juce::Justification::centredLeft);

            area.removeFromRight(46);
            const int barHeight = juce::jmax(8, juce::jmin(18, (area.getHeight() - 8) / 2));

            auto drawBar = [&](juce::Rectangle<int> bar, float value, bool valid, juce::Colour colour, const juce::String& caption)
            {
                g.setColour(Theme::track);
                g.fillRect(bar);
                if (valid)
                {
                    const float proportion = juce::jlimit(0.f, 1.f, (value - floorDb) / (topDb - floorDb));
                    g.setColour(colour);
                    g.fillRect(bar.withWidth(juce::roundToInt(proportion * (float)bar.getWidth())));
                }

                g.setFont(Theme::font(12.f));
                g.setColour(valid ? Theme::text : Theme::textFaint);
                g.drawText(valid ? formatDb(value) : juce::String("-"), bar.withX(bar.getRight() + 4).withWidth(46), juce::Justification::centredLeft);
                g.setFont(Theme::font(9.5f, true));
                g.setColour(Theme::text.withAlpha(0.85f));
                g.drawText(caption, bar.reduced(4, 0), juce::Justification::centredLeft);
            };

            drawBar(area.removeFromTop(barHeight), mixValue, hasMix, Theme::accent, "MIX");
            area.removeFromTop(4);
            drawBar(area.removeFromTop(barHeight), referenceValue, hasReference, Theme::second, "REF");
        };

        const bool hasReference = track != nullptr;
        drawBars(left, "PEAK", mix.peakDb, hasReference ? track->peakDb + gainDb : 0.f, peakFloorDb, 0.f, mix.peakDb > -150.f, hasReference);
        drawBars(right, "INTEGRATED LUFS", mix.integratedLufs, hasReference ? track->lufs + gainDb : 0.f, lufsFloor, 0.f, mix.integratedLufs > -150.f, hasReference);

        // The switch between the mix and the reference
        auto drawSide = [&](juce::Rectangle<int> area, const juce::String& text, bool active, bool second)
        {
            const auto colour = second ? Theme::second : Theme::accent;
            g.setColour(active ? colour.withAlpha(0.22f) : Theme::track);
            g.fillRoundedRectangle(area.toFloat(), 5.f);
            g.setColour(active ? colour : Theme::panelEdge);
            g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 5.f, active ? 2.f : 1.f);
            g.setFont(Theme::font(juce::jlimit(13.f, 22.f, (float)area.getHeight() * 0.5f), true).withExtraKerningFactor(0.06f));
            g.setColour(active ? colour : Theme::textDim);
            g.drawText(text, area, juce::Justification::centred);
        };

        drawSide(originalArea, "ORIGINAL", !listening, false);
        drawSide(referenceArea, "REFERENCE", listening, true);
    }

    // The correction curve and the score
    {
        g.setColour(Theme::display.darker(0.3f));
        g.fillRect(curveArea);

        auto yOf = [&](float db) { return juce::jmap(juce::jlimit(-curveRangeDb, curveRangeDb, db), -curveRangeDb, curveRangeDb, (float)curveArea.getBottom() - 18.f, (float)curveArea.getY() + 14.f); };

        g.setFont(Theme::font(10.5f));
        for (float db : { -5.f, 0.f, 5.f })
        {
            g.setColour(db == 0.f ? Theme::gridStrong : Theme::grid);
            g.fillRect((float)curveArea.getX() + 36.f, yOf(db), (float)curveArea.getWidth() - 44.f, 1.f);
            g.setColour(Theme::textDim);
            g.drawText(db == 0.f ? "0 dB" : formatDb(db, 0), curveArea.getX(), (int)yOf(db) - 7, 34, 14, juce::Justification::centredRight);
        }

        for (double frequency : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
        {
            const float x = xOfFrequency(frequency);
            g.setColour(Theme::grid);
            g.fillRect(x, (float)curveArea.getY() + 14.f, 1.f, (float)curveArea.getHeight() - 32.f);
            g.setColour(Theme::textDim);
            const juce::String text = frequency >= 1000.0 ? juce::String((int)(frequency / 1000.0)) + "k" : juce::String((int)frequency);
            g.drawText(text, juce::Rectangle<int>(40, 14).withCentre({ (int)x, curveArea.getBottom() - 9 }), juce::Justification::centred);
        }

        if (!correction.empty())
        {
            juce::Path line, fill;
            const float zero = yOf(0.f);
            for (size_t i = 0; i < correction.size(); ++i)
            {
                const float x = xOfFrequency(TonalTargets::frequencyOf((int)i));
                const float y = yOf(correction[i]);
                if (i == 0)
                {
                    line.startNewSubPath(x, y);
                    fill.startNewSubPath(x, zero);
                    fill.lineTo(x, y);
                }
                else
                {
                    line.lineTo(x, y);
                    fill.lineTo(x, y);
                }
            }
            fill.lineTo(xOfFrequency(TonalTargets::maxFrequency), zero);
            fill.closeSubPath();

            g.setColour(Theme::second.withAlpha(0.18f));
            g.fillPath(fill);
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.strokePath(line, juce::PathStrokeType(2.f, juce::PathStrokeType::curved));
        }
        else
        {
            g.setFont(Theme::font(12.f));
            g.setColour(Theme::textFaint);
            g.drawText(track == nullptr ? "The correction that brings your mix to the reference appears here" : "Play your mix to see the correction", curveArea, juce::Justification::centred);
        }

        g.setFont(Theme::font(10.f, true));
        g.setColour(Theme::textDim);
        g.drawText("CORRECTION: REFERENCE MINUS MIX", curveArea.reduced(40, 1).removeFromTop(13), juce::Justification::centredLeft);

        if (matchPercent >= 0.f)
        {
            g.setFont(Theme::font(15.f, true));
            g.setColour(matchPercent >= 80.f ? Theme::good : matchPercent >= 55.f ? Theme::warn : Theme::over);
            g.drawText("MATCH: " + juce::String(juce::roundToInt(matchPercent)) + "%", curveArea.reduced(40, 1).removeFromTop(16), juce::Justification::centredRight);
        }
    }
}

//==============================================================================
void ReferenceView::mouseMove(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const bool hand = levelMatchArea.contains(p) || originalArea.contains(p) || referenceArea.contains(p) || slotsArea.contains(p) || waveArea.contains(p);
    setMouseCursor(hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void ReferenceView::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    const auto p = e.getPosition();

    if (levelMatchArea.contains(p))
    {
        levelMatch = !levelMatch;
        if (!levelMatch)
            gainDb = 0.f;
        repaint();
        return;
    }

    if (originalArea.contains(p))
    {
        manager.monitoring.store(false);
        repaint();
        return;
    }

    if (referenceArea.contains(p))
    {
        if (manager.getTrack(selected) == nullptr)
            chooseFile(selected);
        else
            manager.monitoring.store(true);
        repaint();
        return;
    }

    for (int i = 0; i < ReferenceManager::numSlots; ++i)
    {
        if (removeAreas[(size_t)i].contains(p) && manager.getTrack(i) != nullptr)
        {
            manager.remove(i);
            saveFiles();
            if (manager.getTrack(selected) == nullptr)
                manager.monitoring.store(false);
            return;
        }

        if (slotAreas[(size_t)i].contains(p))
        {
            if (manager.getTrack(i) == nullptr)
                chooseFile(i);
            else
                select(i);
            return;
        }
    }

    if (waveArea.contains(p))
    {
        auto track = manager.getTrack(selected);
        if (track == nullptr)
        {
            chooseFile(selected);
            return;
        }

        dragging = true;
        dragFrom = dragTo = sampleAt((float)p.x, *track);
        repaint();
    }
}

void ReferenceView::mouseDrag(const juce::MouseEvent& e)
{
    auto track = manager.getTrack(selected);
    if (dragging && track != nullptr)
    {
        dragTo = sampleAt((float)e.getPosition().x, *track);
        repaint();
    }
}

void ReferenceView::mouseUp(const juce::MouseEvent&)
{
    if (!dragging)
        return;

    dragging = false;
    auto track = manager.getTrack(selected);
    if (track != nullptr && std::abs(dragTo - dragFrom) > (int)(0.5 * track->sampleRate))
        manager.setRegion(selected, juce::jmin(dragFrom, dragTo), juce::jmax(dragFrom, dragTo));

    repaint();
}

void ReferenceView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (waveArea.contains(e.getPosition()))
    {
        dragging = false;
        smartLoop();
    }
}
