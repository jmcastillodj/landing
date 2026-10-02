/*
  ==============================================================================

    The editor of the plugin: a header with a tab for each view, the view itself,
    a column of meters that is always showing, and a bar of controls for the view.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <set>

namespace
{
    // The size of the editor is kept in the state, beside the parameters, so that it is saved with the session
    const juce::Identifier editorWidthProperty { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };
    const juce::Identifier multiEnabledProperty { "multiEnabled" };
    const juce::Identifier multiRowsProperty { "multiRows" };
    const juce::Identifier multiRowWeightsProperty { "multiRowWeights" };
    const juce::Identifier multiViewWeightsProperty { "multiViewWeights" };
    const juce::Identifier multiSlotsProperty { "multiSlots" };
}

namespace
{
    // The order of the tabs. The view that opens first comes first, the two views of the spectrum are
    // side by side, and so are the three views of the timeline, of which the loudness matters most.
    // The goniometer, the only view of the stereo image, is at the end, beside the correlation. The
    // values of the parameter are in another order, which is that of version 2's first builds, and
    // cannot change without saved sessions opening on the wrong view.
    constexpr std::array<int, 8> tabOrder { Parameters::viewSpectrum, Parameters::viewBalance, Parameters::viewSpectrogram, Parameters::viewWaveform, Parameters::viewLoudness, Parameters::viewLoudnessRound,
                                            Parameters::viewHistory, Parameters::viewGoniometer };
}

//==============================================================================
UltimateMeterAudioProcessorEditor::UltimateMeterAudioProcessorEditor(UltimateMeterAudioProcessor& p) :
    AudioProcessorEditor(&p),
    audioProcessor(p),
    spectrumSource(audioProcessor),
    goniometerView(audioProcessor.apvts, Parameters::ID::goniometerScale),
    spectrumView(spectrumSource),
    spectrogramView(spectrumSource),
    waveformView(p),
    balanceView(spectrumSource, p.apvts.state),
    mainViewAttachment(*audioProcessor.apvts.getParameter(Parameters::ID::mainView),
        [this](float value) { showMainView(juce::roundToInt(value)); }),
    vBlankAttachment(this, [this](double timestampSeconds) { vBlank(timestampSeconds); })
{
    using namespace Parameters;
    auto& apvts = audioProcessor.apvts;

    setLookAndFeel(&lookAndFeel);
    setOpaque(true);

    // The tabs change between the views
    addAndMakeVisible(tabs);
    juce::StringArray tabNames;
    for (int viewId : tabOrder)
        tabNames.add(mainViewNames[viewId]);

    tabs.setTabs(tabNames);
    tabs.onChange = [this](int tab)
    {
        if (multiEnabled)
            toggleViewInMulti(tabOrder[(size_t)tab]);
        else
            mainViewAttachment.setValueAsCompleteGesture((float)tabOrder[(size_t)tab]);
    };

    // Multi mode puts several views on screen together, each of them resizable
    addAndMakeVisible(multiButton);
    multiButton.setClickingTogglesState(true);
    multiButton.setTooltip("Show several views at once");
    multiButton.onClick = [this] { setMultiMode(multiButton.getToggleState()); };

    addChildComponent(arrangeButton);
    arrangeButton.setTooltip("Choose which views are showing and in which row");
    arrangeButton.onClick = [this] { showLayoutMenu(); };

    addChildComponent(goniometerView);
    addChildComponent(spectrumView);
    addChildComponent(spectrogramView);
    addChildComponent(historyView);
    addChildComponent(waveformView);
    addChildComponent(balanceView);
    addChildComponent(loudnessView);
    addChildComponent(radarView);

    // The lenses of the waveform: the zoom of the amplitude goes by tenths, and the span of time through its choices
    waveformView.onVerticalStep = [this](int step, bool coarse)
    {
        if (auto* parameter = audioProcessor.apvts.getParameter(Parameters::ID::waveformZoom))
        {
            const float next = juce::jlimit(WaveformView::minZoom, WaveformView::maxZoom,
                                            std::round((getValue(Parameters::ID::waveformZoom) + (float)step * (coarse ? 1.f : WaveformView::zoomStep)) * 10.f) / 10.f);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(next));
            parameter->endChangeGesture();
        }
    };
    waveformView.onVerticalReset = [this]
    {
        if (auto* parameter = audioProcessor.apvts.getParameter(Parameters::ID::waveformZoom))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(1.f));
            parameter->endChangeGesture();
        }
    };

    auto setSpanChoice = [this](int index)
    {
        if (auto* parameter = audioProcessor.apvts.getParameter(Parameters::ID::waveformSpan))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)juce::jlimit(0, (int)Parameters::waveformSpanNames.size() - 1, index)));
            parameter->endChangeGesture();
        }
    };
    waveformView.onHorizontalStep = [this, setSpanChoice](int step, bool coarse)
    {
        // A step towards closer is a shorter span
        setSpanChoice(getChoice(Parameters::ID::waveformSpan) - step * (coarse ? 2 : 1));
    };
    waveformView.onHorizontalReset = [setSpanChoice] { setSpanChoice(Parameters::defaultWaveformSpan); };

    addAndMakeVisible(levelMeters);
    addAndMakeVisible(loudnessSummary);

    // A click on the true peak starts it again, as the reading is the highest since the last reset
    loudnessSummary.onTruePeakClicked = [this]
    {
        if (getChoice(Parameters::ID::summaryMode) == 1)
            heldPeakDb = -200.f;
        else
            audioProcessor.truePeakDetector.requestReset();
    };

    // A click on the distance to the target opens the targets to choose from
    loudnessSummary.onTargetClicked = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("Loudness target");
        addChoiceItems(menu, *audioProcessor.apvts.getParameter(Parameters::ID::loudnessTarget));
        menu.setLookAndFeel(&lookAndFeel);
        menu.showMenuAsync(juce::PopupMenu::Options());
    };

    // A click on the title of the numbers changes them between the loudness and the RMS levels
    loudnessSummary.onTitleClicked = [this]
    {
        if (auto* parameter = audioProcessor.apvts.getParameter(Parameters::ID::summaryMode))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(getChoice(Parameters::ID::summaryMode) == 1 ? 0.f : 1.f);
            parameter->endChangeGesture();
        }
    };
    addAndMakeVisible(stereoPanel);

    // The options of the views are in a menu, which opens on the secondary click of a view, and from this button
    addAndMakeVisible(optionsButton);
    optionsButton.buildMenu = [this](juce::PopupMenu& menu)
    {
        if (!multiEnabled)
        {
            buildViewMenu(menu, currentMainView);
            return;
        }

        // With several views showing, each has its menu
        for (int viewId : tabOrder)
            if (viewRow[(size_t)viewId] >= 0)
            {
                juce::PopupMenu viewMenu;
                buildViewMenu(viewMenu, viewId);
                menu.addSubMenu(Parameters::mainViewNames[viewId], viewMenu);
            }
    };

    // The grips of the views in multi mode, to drag a view to another place
    addChildComponent(dropOverlay);
    for (int viewId = 0; viewId < (int)dragHandles.size(); ++viewId)
    {
        auto& handle = dragHandles[(size_t)viewId];
        handle.setName(mainViewNames[viewId]);
        addChildComponent(handle);

        handle.onStart = [this, viewId]
        {
            captureSizes();
            draggedView = viewId;
            currentDrop = {};
            dropOverlay.setBounds(getLocalBounds());
            dropOverlay.show({}, {});
            dropOverlay.setVisible(true);
            dropOverlay.toFront(false);
        };
        handle.onMove = [this, viewId](juce::Point<int> position)
        {
            currentDrop = dropAt(position, viewId);
            const char* labels[] { "", "Place it before this view", "Place it after this view", "A row of its own above", "A row of its own below" };
            dropOverlay.show(currentDrop.zone, labels[(int)currentDrop.kind]);
        };
        handle.onEnd = [this, viewId](juce::Point<int> position)
        {
            const auto drop = dropAt(position, viewId);
            dropOverlay.show({}, {});
            dropOverlay.setVisible(false);
            draggedView = -1;

            if (drop.kind != Drop::none)
                moveView(viewId, drop);
        };
    }

    // The presets: the settings and the layout, kept as files, and one of them as the default of new instances
    addAndMakeVisible(presetsButton);
    presetsButton.buildMenu = [this](juce::PopupMenu& menu) { buildPresetsMenu(menu); };

    for (auto* view : std::initializer_list<juce::Component*> { &goniometerView, &spectrumView, &spectrogramView, &historyView, &waveformView, &balanceView, &loudnessView, &radarView })
        view->addMouseListener(this, true);

    // One button starts every measurement again, as the reset of a loudness meter does. Nothing
    // is cleared by a click on a view, where a click that was meant for something else can land.
    addAndMakeVisible(resetButton);
    resetButton.onClick = [this] { resetMeasurements(); };

    // The settings of the side column's meters share one menu
    addAndMakeVisible(meterSettingsButton);
    meterSettingsButton.buildMenu = [this](juce::PopupMenu& menu) { buildMeterSettingsMenu(menu); };

    // The editor opens at the size that it was last given. The size is read first, because setting
    // the limits already resizes the editor, which would otherwise be taken for the user's choice.
    const int savedWidth = apvts.state.getProperty(editorWidthProperty, defaultWidth);
    const int savedHeight = apvts.state.getProperty(editorHeightProperty, defaultHeight);

    setResizable(true, true);
    setResizeLimits(minWidth, minHeight, maxWidth, maxHeight);
    setSize(juce::jlimit(minWidth, maxWidth, savedWidth), juce::jlimit(minHeight, maxHeight, savedHeight));
    isConstructed = true;

    // The views of multi mode that were on screen when the session was saved
    readLayoutFromState();

    // Show the view that the parameter selects, now that the layout is known
    mainViewAttachment.sendInitialUpdate();
    refreshViews();

    // Discard the peaks that built up while the editor was closed
    audioProcessor.meterEngine.read();
    audioProcessor.truePeakDetector.read();
}

UltimateMeterAudioProcessorEditor::~UltimateMeterAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void UltimateMeterAudioProcessorEditor::paint(juce::Graphics& g)
{
    // The views are opaque, so this only shows as the hairlines between the displays, and under the chrome
    g.fillAll(Theme::edge);

    auto bounds = getLocalBounds();
    paintHeader(g, bounds.removeFromTop(Theme::headerHeight));
    paintBottomBar(g, bounds.removeFromBottom(Theme::bottomBarHeight));
}

namespace
{
    // The raised tab that holds the name starts at the left edge of the window and is as wide as the
    // name with this much on either side. It ends in a shoulder of this width.
    constexpr float namePaddingLeft = 16.f;
    constexpr float namePaddingRight = 14.f;
    constexpr float shoulderWidth = 34.f;

    const juce::String productName { "ULTIMATE METER" };
    const juce::String makerLine { "by Jm Castillo" };

    juce::Font nameFont()  { return Theme::font(17.f, true).withExtraKerningFactor(0.14f); }
    juce::Font makerFont() { return Theme::font(11.f).withExtraKerningFactor(0.04f); }

    // The width of the tab's content: the name, a gap, and the maker's line
    float nameLineWidth()
    {
        return (float)Theme::textWidth(nameFont(), productName) + 10.f + (float)Theme::textWidth(makerFont(), makerLine);
    }

    // Where the flat part of the tab ends and its shoulder begins
    float nameTabRight()
    {
        return namePaddingLeft + nameLineWidth() + namePaddingRight;
    }

    // The thickness of the raised edge that runs along the rest of the chrome
    constexpr float rimThickness = 4.f;

    // Adds an S-shaped shoulder to a path, from where the path is to a point
    void shoulderTo(juce::Path& path, juce::Point<float> to)
    {
        const auto from = path.getCurrentPosition();
        const float middle = 0.5f * (from.x + to.x);
        path.cubicTo(middle, from.y, middle, to.y, to.x, to.y);
    }
}

void UltimateMeterAudioProcessorEditor::paintHeader(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto bounds = area.toFloat();

    // The recessed strip, which holds the tabs
    g.setGradientFill(juce::ColourGradient(Theme::chromeInsetTop, 0.f, bounds.getY(), Theme::chromeInsetBottom, 0.f, bounds.getBottom(), false));
    g.fillRect(bounds);

    // The raised part: a rim along the top, which drops into a tab for the name at the left edge of the window
    const float rim = bounds.getY() + rimThickness;
    const float tabRight = bounds.getX() + nameTabRight();

    juce::Path raised;
    raised.startNewSubPath(bounds.getTopLeft());
    raised.lineTo(bounds.getTopRight());
    raised.lineTo(bounds.getRight(), rim);
    raised.lineTo(tabRight + shoulderWidth, rim);
    shoulderTo(raised, { tabRight, bounds.getBottom() });
    raised.lineTo(bounds.getBottomLeft());
    raised.closeSubPath();

    g.setGradientFill(juce::ColourGradient(Theme::chromeTop, 0.f, bounds.getY(), Theme::chromeBottom, 0.f, bounds.getBottom(), false));
    g.fillPath(raised);

    // A light edge where the raised part ends, which is what makes it look raised
    g.setColour(Theme::panelEdge.withAlpha(0.7f));
    g.strokePath(raised, juce::PathStrokeType(1.f));
    g.setColour(Theme::edge);
    g.fillRect(bounds.withTop(bounds.getBottom() - 1.f));

    paintName(g, bounds.withWidth(nameTabRight()).withTrimmedLeft(namePaddingLeft));
}

void UltimateMeterAudioProcessorEditor::paintName(juce::Graphics& g, juce::Rectangle<float> tab)
{
    // The name in white, with a blue bar before it as the sections of Studio One's windows have, and the
    // maker's line after it, in grey, on the same baseline
    const float nameWidth = (float)Theme::textWidth(nameFont(), productName);

    g.setColour(Theme::accent);
    g.fillRoundedRectangle(juce::Rectangle<float>(3.f, 16.f).withCentre({ tab.getX() - 8.f, tab.getCentreY() + 1.f }), 1.5f);

    g.setFont(nameFont());
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawText(productName, juce::Rectangle<float>(tab.getX(), tab.getY() + 1.f, nameWidth + 4.f, tab.getHeight()), juce::Justification::centredLeft);
    g.setColour(Theme::wordmarkTop);
    g.drawText(productName, juce::Rectangle<float>(tab.getX(), tab.getY(), nameWidth + 4.f, tab.getHeight()), juce::Justification::centredLeft);

    g.setFont(makerFont());
    g.setColour(Theme::textDim);
    g.drawText(makerLine, juce::Rectangle<float>(tab.getX() + nameWidth + 10.f, tab.getY() + 1.5f, tab.getWidth(), tab.getHeight()), juce::Justification::centredLeft);
}

void UltimateMeterAudioProcessorEditor::paintBottomBar(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto bounds = area.toFloat();

    // What shows where the bar dips away is the bottom of the displays
    g.setColour(Theme::displayBottom);
    g.fillRect(bounds);

    // The bar is raised along its whole length, except for a dip between the controls of the view
    // and the settings of the meters, if the window is wide enough to leave room for one
    const float dipLeft = (float)presetsButton.getRight() + 24.f;
    const float dipRight = (float)resetButton.getX() - 12.f;
    const bool hasDip = dipRight - dipLeft > 2.f * shoulderWidth + 20.f;
    const float rim = bounds.getBottom() - rimThickness;

    juce::Path raised;
    raised.startNewSubPath(bounds.getTopLeft());

    if (hasDip)
    {
        raised.lineTo(dipLeft, bounds.getY());
        shoulderTo(raised, { dipLeft + shoulderWidth, rim });
        raised.lineTo(dipRight - shoulderWidth, rim);
        shoulderTo(raised, { dipRight, bounds.getY() });
    }

    raised.lineTo(bounds.getTopRight());
    raised.lineTo(bounds.getBottomRight());
    raised.lineTo(bounds.getBottomLeft());
    raised.closeSubPath();

    g.setGradientFill(juce::ColourGradient(Theme::footerTop.brighter(0.08f), 0.f, bounds.getY(), Theme::footerBottom, 0.f, bounds.getBottom(), false));
    g.fillPath(raised);
    g.setColour(Theme::panelEdge.withAlpha(0.7f));
    g.strokePath(raised, juce::PathStrokeType(1.f));
}

void UltimateMeterAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // The tabs start where the shoulder of the name's tab has come up to the rim
    auto header = bounds.removeFromTop(Theme::headerHeight);
    header.removeFromLeft(juce::roundToInt(nameTabRight() + shoulderWidth) + 4);
    header.removeFromTop(juce::roundToInt(rimThickness));
    header.removeFromRight(8);
    multiButton.setBounds(header.removeFromRight(multiButton.getIdealWidth()).withTrimmedTop(2));
    arrangeButton.setBounds(header.removeFromRight(arrangeButton.getIdealWidth()).withTrimmedTop(2));
    tabs.setBounds(header.removeFromLeft(juce::jmin(header.getWidth(), tabs.getIdealWidth())));

    auto bottomBar = bounds.removeFromBottom(Theme::bottomBarHeight);
    bottomBar.removeFromRight(18); // the corner resizer
    meterSettingsButton.setBounds(bottomBar.removeFromRight(meterSettingsButton.getIdealWidth()));
    resetButton.setBounds(bottomBar.removeFromRight(resetButton.getIdealWidth()));
    optionsButton.setBounds(bottomBar.removeFromLeft(optionsButton.getIdealWidth() + 8).withTrimmedLeft(8));
    presetsButton.setBounds(bottomBar.removeFromLeft(presetsButton.getIdealWidth() + 4));

    // The side column, from the bottom up: the correlation, the loudness, and the bars in what is left
    bounds.removeFromTop(Theme::gap);
    bounds.removeFromBottom(Theme::gap);
    auto side = bounds.removeFromRight(Theme::sideColumnWidth);
    bounds.removeFromRight(Theme::gap);

    // The stereo panel takes all the room that it can without leaving the level meters too short
    {
        const int room = side.getHeight() - 132 - 2 * Theme::gap - 150;
        const int stereoHeight = room >= StereoPanel::fullHeight ? StereoPanel::fullHeight
                               : room >= StereoPanel::barsHeight + 90 ? room : StereoPanel::barsHeight;
        stereoPanel.setBounds(side.removeFromBottom(stereoHeight));
    }
    side.removeFromBottom(Theme::gap);
    loudnessSummary.setBounds(side.removeFromBottom(132));
    side.removeFromBottom(Theme::gap);
    levelMeters.setBounds(side);

    viewArea = bounds;
    layoutViews();

    // Keep the size for the next time that the editor opens
    if (isConstructed)
    {
        audioProcessor.apvts.state.setProperty(editorWidthProperty, getWidth(), nullptr);
        audioProcessor.apvts.state.setProperty(editorHeightProperty, getHeight(), nullptr);
    }
}

//==============================================================================
void UltimateMeterAudioProcessorEditor::vBlank(double timestampSeconds)
{
    if (lastUpdateTime < 0.0)
    {
        lastUpdateTime = timestampSeconds;
        lastAudioTime = timestampSeconds;
        return;
    }

    // Skip the frames of the display that come sooner than the refresh rate asks for.
    // The small tolerance keeps the timing jitter of the display from dropping a frame that is due.
    const double rateHz = Parameters::valueAt(Parameters::refreshRatesHz, getChoice(Parameters::ID::refreshRate));
    const double elapsedSeconds = timestampSeconds - lastUpdateTime;
    if (elapsedSeconds < 0.9 / rateHz)
        return;

    lastUpdateTime = timestampSeconds;

    // After a long pause, such as the window being hidden, the meters carry on rather than jump
    updateMeters((float)juce::jmin(elapsedSeconds, 0.1));
}

void UltimateMeterAudioProcessorEditor::updateMeters(float elapsedSeconds)
{
    using namespace Parameters;

    // The measurements were made on the audio thread from every sample
    auto readings = audioProcessor.meterEngine.read();
    auto loudness = audioProcessor.loudnessMeter.read();
    const auto truePeak = audioProcessor.truePeakDetector.read();

    // When the host stops calling the processor the last readings would stay forever,
    // so they are replaced with silence once no audio has arrived for a while
    const auto totalWritten = audioProcessor.sampleRingBuffer.getTotalWritten();
    if (totalWritten != lastTotalWritten)
    {
        lastTotalWritten = totalWritten;
        lastAudioTime = lastUpdateTime;
    }

    const bool audioRunning = lastUpdateTime - lastAudioTime <= silenceTimeoutSeconds;
    if (!audioRunning)
    {
        readings = {};

        // The momentary and short-term loudness describe the present, so they go quiet with the audio.
        // The integrated loudness and the range describe the programme so far, so they stay.
        loudness.momentary = loudness.shortTerm = LoudnessMeter::silence;
    }

    auto toDecibels = [](float gain) { return juce::Decibels::gainToDecibels(gain, -200.f); };
    const float target = valueAt(loudnessTargetsLufs, getChoice(ID::loudnessTarget));

    updateHandles();

    // The side column
    LevelMeters::Levels levels;
    for (size_t channel = 0; channel < 2; ++channel)
    {
        levels.peakDb[channel] = toDecibels(readings.peak[channel]);
        levels.rmsDb[channel] = toDecibels(readings.rms[channel]);
    }
    levels.momentaryLufs = loudness.momentary;
    levels.shortTermLufs = loudness.shortTerm;
    levels.integratedLufs = loudness.integrated;
    levels.targetLufs = target;

    LevelMeters::Settings meterSettings;
    meterSettings.tickDecayDbPerSecond = valueAt(decayRatesDbPerSecond, getChoice(ID::decayRate));
    meterSettings.tickHoldSeconds = valueAt(holdTimesSeconds, getChoice(ID::holdTime));
    meterSettings.showPeak = getChoice(ID::meterView) != rmsMeters;
    meterSettings.showRms = getChoice(ID::meterView) != peakMeters;
    meterSettings.showTicks = isOn(ID::showTick);
    meterSettings.resetTicks = resetTicksRequested;
    resetTicksRequested = false;

    levelMeters.update(levels, meterSettings, elapsedSeconds);
    stereoPanel.update(readings.balanceDb, readings.width, readings.correlationSlow, readings.monoDeviationDb);

    const float truePeakDb = audioRunning ? toDecibels(juce::jmax(truePeak.peak[0], truePeak.peak[1])) : -200.f;
    const float maxTruePeakDb = toDecibels(juce::jmax(truePeak.maxPeak[0], truePeak.maxPeak[1]));

    secondsSinceReadout += (double)elapsedSeconds;
    const bool readoutDue = secondsSinceReadout >= Theme::readoutIntervalSeconds;
    if (readoutDue)
        secondsSinceReadout = 0.0;

    loudnessSummary.update(loudness, maxTruePeakDb, target, readoutDue);

    heldPeakDb = juce::jmax(heldPeakDb, juce::jmax(levels.peakDb[0], levels.peakDb[1]));
    loudnessSummary.setMode(getChoice(ID::summaryMode) == 1 ? LoudnessSummary::Mode::rms : LoudnessSummary::Mode::loudness);
    loudnessSummary.updateRms(levels.rmsDb[0], levels.rmsDb[1], heldPeakDb, readoutDue);

    // The spectrogram, the history and the loudness share one timeline. They all record in every
    // update, whichever view is showing, so that the same moment is in the same place in all of them.
    // While no audio arrives the clock stands still, and so do all three.
    const int numNewSlots = timelineClock.advance(elapsedSeconds, audioRunning);
    const float timeSpan = valueAt(timeSpansSeconds, getChoice(ID::timeSpan));

    loudnessView.setSpan(timeSpan);
    historyView.setSpan(timeSpan);

    // The waveform is made of the samples themselves, which it reads whichever view is showing
    waveformView.update();

    {
        WaveformView::Settings waveformSettings;
        waveformSettings.channels = getChoice(ID::waveformChannels);
        waveformSettings.colours = getChoice(ID::waveformColours);
        waveformSettings.sweep = getChoice(ID::waveformMode) == 1;
        waveformSettings.zoom = getValue(ID::waveformZoom);
        waveformSettings.spanSeconds = valueAt(waveformSpansSeconds, getChoice(ID::waveformSpan));
        waveformSettings.peakHistory = isOn(ID::waveformPeakHistory);
        waveformSettings.timeCode = isOn(ID::waveformTimecode);
        waveformSettings.hostSeconds = audioProcessor.hostTimeSeconds.load(std::memory_order_relaxed);
        waveformView.setSettings(waveformSettings);
    }

    spectrogramView.setPalette(getChoice(ID::spectrogramColours));
    historyView.setShown(getChoice(ID::historyShow) != rmsMeters, getChoice(ID::historyShow) != peakMeters);
    spectrogramView.setSpan(timeSpan);

    loudnessView.update(loudness, truePeakDb, maxTruePeakDb, target, numNewSlots, elapsedSeconds, readoutDue);
    radarView.update(loudness, target, valueAt(radarSpeedsSeconds, getChoice(ID::radarSpeed)), getChoice(ID::radarSource) == 1,
                     audioRunning ? elapsedSeconds : 0.f, readoutDue);
    historyView.record(numNewSlots, juce::jmax(levels.peakDb[0], levels.peakDb[1]), juce::jmax(levels.rmsDb[0], levels.rmsDb[1]));

    // Only the visible view needs the samples themselves
    if (goniometerView.isVisible())
    {
        const auto mode = getChoice(ID::goniometerMode) == polarMode ? GoniometerView::polar : GoniometerView::lissajous;
        const float persistence = valueAt(goniometerPersistenceSeconds, getChoice(ID::goniometerPersistence));
        goniometerView.update(audioProcessor.sampleRingBuffer, elapsedSeconds, mode, persistence, getValue(ID::goniometerScale) / 100.f);
    }

    // The spectra are analyzed in every update, because the spectrogram records them whether it is
    // showing or not. Two FFTs cost very little beside drawing a frame.
    {
        const int order = valueAt(spectrumResolutionOrders, getChoice(ID::spectrumResolution));
        const float tilt = valueAt(spectrumTiltsDbPerOctave, getChoice(ID::spectrumTilt));
        const bool hasNewSpectra = spectrumSource.update(elapsedSeconds, order);

        // While frozen the pictures stand still, but the recording carries on underneath
        const bool frozen = spectrumFrozen;
        spectrogramView.record(numNewSlots, hasNewSpectra, tilt, frozen);

        balanceView.setFine(getChoice(ID::balanceDetail) == 1);
        balanceView.record(hasNewSpectra);


        if (spectrumView.isVisible())
        {
            SpectrumView::Settings spectrumSettings;
            spectrumSettings.midSide = getChoice(ID::spectrumChannels) == 1;
            spectrumSettings.tiltDbPerOctave = tilt;
            spectrumSettings.smoothingOctaves = valueAt(spectrumSmoothingOctaves, getChoice(ID::spectrumSmoothing));
            spectrumSettings.peakHold = isOn(ID::spectrumPeakHold);
            spectrumSettings.bars = getChoice(ID::spectrumStyle) == 1;
            spectrumSettings.numBars = valueAt(spectrumBarCounts, getChoice(ID::spectrumBars));
            spectrumSettings.releaseSeconds = valueAt(spectrumReleaseSeconds, getChoice(ID::spectrumSpeed));

            const int reference = getChoice(ID::spectrumReference);
            spectrumSettings.hasReference = reference > 0;
            spectrumSettings.referenceSlopeDbPerOctave = valueAt(spectrumReferenceSlopesDbPerOctave, reference);
            spectrumSettings.referenceName = spectrumReferenceShortNames[reference];
            spectrumView.update(hasNewSpectra, spectrumSettings, elapsedSeconds, frozen);
        }
    }
}

//==============================================================================
void UltimateMeterAudioProcessorEditor::showMainView(int viewId)
{
    currentMainView = viewId;
    refreshViews();
}

juce::Component* UltimateMeterAudioProcessorEditor::componentOfView(int viewId)
{
    switch (viewId)
    {
        case Parameters::viewGoniometer:  return &goniometerView;
        case Parameters::viewSpectrum:    return &spectrumView;
        case Parameters::viewSpectrogram: return &spectrogramView;
        case Parameters::viewHistory:     return &historyView;
        case Parameters::viewLoudness:    return &loudnessView;
        case Parameters::viewWaveform:    return &waveformView;
        case Parameters::viewBalance:     return &balanceView;
        case Parameters::viewLoudnessRound: return &radarView;
        default:                          return nullptr;
    }
}

int UltimateMeterAudioProcessorEditor::multiMask() const
{
    int mask = 0;
    for (int view = 0; view < (int)viewRow.size(); ++view)
        if (viewRow[(size_t)view] >= 0)
            mask |= 1 << view;
    return mask;
}

// Reads the multi mode layout from the saved state: which views are on, their rows, and their sizes
void UltimateMeterAudioProcessorEditor::readLayoutFromState()
{
    auto& apvts = audioProcessor.apvts;
    multiEnabled = (bool)apvts.state.getProperty(multiEnabledProperty, false);
    viewRow.fill(-1);
    rowWeight.fill(0.0);
    viewWeight.fill(0.0);
    {
        const auto rows = juce::StringArray::fromTokens(apvts.state.getProperty(multiRowsProperty, "").toString(), ",", "");
        for (int view = 0; view < (int)viewRow.size(); ++view)
            viewRow[(size_t)view] = view < rows.size() ? juce::jlimit(-1, maxRows - 1, rows[view].getIntValue()) : -1;
    }

    {
        const auto rowWeights = juce::StringArray::fromTokens(apvts.state.getProperty(multiRowWeightsProperty, "").toString(), ",", "");
        const auto viewWeights = juce::StringArray::fromTokens(apvts.state.getProperty(multiViewWeightsProperty, "").toString(), ",", "");
        for (int i = 0; i < (int)rowWeight.size() && i < rowWeights.size(); ++i)
            rowWeight[(size_t)i] = rowWeights[i].getDoubleValue();
        for (int i = 0; i < (int)viewWeight.size() && i < viewWeights.size(); ++i)
            viewWeight[(size_t)i] = viewWeights[i].getDoubleValue();
    }

    {
        const auto slots = juce::StringArray::fromTokens(apvts.state.getProperty(multiSlotsProperty, "").toString(), ",", "");
        for (int i = 0; i < (int)viewSlot.size(); ++i)
            viewSlot[(size_t)i] = i < slots.size() ? slots[i].getIntValue() : i;
    }

    if (multiMask() == 0)
        multiEnabled = false;
    multiButton.setToggleState(multiEnabled, juce::dontSendNotification);
}

void UltimateMeterAudioProcessorEditor::saveMultiState()
{
    auto& state = audioProcessor.apvts.state;
    state.setProperty(multiEnabledProperty, multiEnabled, nullptr);

    juce::StringArray rows;
    for (int row : viewRow)
        rows.add(juce::String(row));
    state.setProperty(multiRowsProperty, rows.joinIntoString(","), nullptr);

    juce::StringArray rowWeights, viewWeights;
    for (double weight : rowWeight)
        rowWeights.add(juce::String(weight, 4));
    for (double weight : viewWeight)
        viewWeights.add(juce::String(weight, 4));
    state.setProperty(multiRowWeightsProperty, rowWeights.joinIntoString(","), nullptr);
    state.setProperty(multiViewWeightsProperty, viewWeights.joinIntoString(","), nullptr);

    juce::StringArray slots;
    for (int slot : viewSlot)
        slots.add(juce::String(slot));
    state.setProperty(multiSlotsProperty, slots.joinIntoString(","), nullptr);
}

// Reads the sizes that the layout has given, as shares
void UltimateMeterAudioProcessorEditor::captureSizes()
{
    if (multiRows.empty() || multiRows.size() != multiRowNumbers.size())
        return;

    double totalHeight = 0.0;
    for (auto& row : multiRows)
        totalHeight += row->getHeight();

    if (totalHeight <= 0.0)
        return;

    for (size_t i = 0; i < multiRows.size(); ++i)
    {
        rowWeight[(size_t)multiRowNumbers[i]] = (double)multiRows[i]->getHeight() / totalHeight;

        double totalWidth = 0.0;
        for (auto* view : multiRows[i]->getViews())
            totalWidth += view->getWidth();

        if (totalWidth > 0.0)
            for (int viewId = 0; viewId < (int)viewRow.size(); ++viewId)
                for (auto* view : multiRows[i]->getViews())
                    if (view == componentOfView(viewId))
                        viewWeight[(size_t)viewId] = (double)view->getWidth() / totalWidth;
    }

    saveMultiState();
}

// The rows that are in use are numbered from the top without a gap, so that no row is empty
void UltimateMeterAudioProcessorEditor::closeUpRows()
{
    std::set<int> used;
    for (int row : viewRow)
        if (row >= 0)
            used.insert(row);

    // The sizes of the rows follow their rows to the new numbers
    auto oldWeights = rowWeight;
    std::fill(rowWeight.begin(), rowWeight.end(), 0.0);

    for (int& row : viewRow)
        if (row >= 0)
        {
            const int newRow = (int)std::distance(used.begin(), used.find(row));
            rowWeight[(size_t)newRow] = oldWeights[(size_t)row];
            row = newRow;
        }
}

void UltimateMeterAudioProcessorEditor::setMultiMode(bool enabled)
{
    if (enabled && multiMask() == 0)
    {
        // The first time, the view that is open comes up with its natural companion in a row of its own
        viewRow[(size_t)currentMainView] = 0;
        viewRow[(size_t)(currentMainView == Parameters::viewSpectrum ? Parameters::viewLoudness : Parameters::viewSpectrum)] = 1;
    }

    multiEnabled = enabled;
    multiButton.setToggleState(enabled, juce::dontSendNotification);
    saveMultiState();
    refreshViews();
}

void UltimateMeterAudioProcessorEditor::toggleViewInMulti(int viewId)
{
    // The sizes the user dragged to are read first, so that a view that comes in or goes out leaves the others as they are
    captureSizes();
    auto& row = viewRow[(size_t)viewId];

    if (row >= 0)
    {
        // At least one view stays on
        if (std::count_if(viewRow.begin(), viewRow.end(), [](int r) { return r >= 0; }) <= 1)
            return;

        row = -1;
    }
    else
    {
        // A view that comes in gets a row of its own below the others, or joins the last row if there is no more room
        row = juce::jmin(maxRows - 1, 1 + *std::max_element(viewRow.begin(), viewRow.end()));
        currentMainView = viewId;
    }

    closeUpRows();
    saveMultiState();
    refreshViews();
}

void UltimateMeterAudioProcessorEditor::setViewRow(int viewId, int row)
{
    captureSizes();
    // The last view cannot be taken away
    if (row < 0 && std::count_if(viewRow.begin(), viewRow.end(), [](int r) { return r >= 0; }) <= 1 && viewRow[(size_t)viewId] >= 0)
        return;

    viewRow[(size_t)viewId] = row;
    closeUpRows();
    saveMultiState();
    refreshViews();
}

// 0: one view to a row. 1: all the views in one row. 2: two views to a row.
void UltimateMeterAudioProcessorEditor::applyLayoutPreset(int preset)
{
    // A new arrangement starts with equal shares
    rowWeight.fill(0.0);
    viewWeight.fill(0.0);
    int position = 0;
    for (int viewId : tabOrder)
    {
        if (viewRow[(size_t)viewId] < 0)
            continue;

        viewRow[(size_t)viewId] = preset == 1 ? 0 : juce::jmin(maxRows - 1, preset == 2 ? position / 2 : position);
        ++position;
    }

    closeUpRows();
    saveMultiState();
    refreshViews();
}

void UltimateMeterAudioProcessorEditor::showLayoutMenu()
{
    juce::PopupMenu menu;

    menu.addSectionHeader("Arrange");
    menu.addItem(1, "One view per row");
    menu.addItem(2, "All in one row");
    menu.addItem(3, "Two views per row");

    menu.addSectionHeader("Views");
    for (int viewId : tabOrder)
    {
        juce::PopupMenu rowMenu;
        const int current = viewRow[(size_t)viewId];

        rowMenu.addItem(1000 + viewId * 10, "Hidden", true, current < 0);
        for (int row = 0; row < maxRows; ++row)
            rowMenu.addItem(1000 + viewId * 10 + row + 1, "Row " + juce::String(row + 1), true, current == row);

        menu.addSubMenu(Parameters::mainViewNames[viewId], rowMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&arrangeButton).withMinimumWidth(180),
        [safe = juce::Component::SafePointer<UltimateMeterAudioProcessorEditor>(this)](int result)
        {
            if (safe == nullptr || result == 0)
                return;

            if (result < 1000)
                safe->applyLayoutPreset(result - 1);
            else
                safe->setViewRow((result - 1000) / 10, (result - 1000) % 10 - 1);
        });
}

void UltimateMeterAudioProcessorEditor::refreshViews()
{
    const int viewMask = multiEnabled ? multiMask() : (1 << currentMainView);

    for (int viewId = 0; viewId < (int)tabOrder.size(); ++viewId)
        if (auto* view = componentOfView(viewId))
            view->setVisible((viewMask & (1 << viewId)) != 0);

    // The tabs show the views that are on, as bits of their own order
    int tabMask = 0;
    for (size_t tab = 0; tab < tabOrder.size(); ++tab)
        if ((viewMask & (1 << tabOrder[tab])) != 0)
            tabMask |= 1 << tab;

    tabs.setMultiMode(multiEnabled);
    tabs.setSelectionMask(tabMask);
    tabs.setSelection((int)std::distance(tabOrder.begin(), std::find(tabOrder.begin(), tabOrder.end(), currentMainView)));
    arrangeButton.setVisible(multiEnabled);

    // The dip in the bottom bar follows the width of the controls
    repaint(getLocalBounds().removeFromBottom(Theme::bottomBarHeight));

    multiLayoutKey = -1;
    layoutViews();
}

void UltimateMeterAudioProcessorEditor::rebuildMultiLayout()
{
    // Every view comes back to the editor, so that no row takes one with it when it goes
    for (int viewId = 0; viewId < (int)viewRow.size(); ++viewId)
        if (auto* view = componentOfView(viewId))
            addChildComponent(*view);

    rowDividers.clear();
    multiRows.clear();
    multiRowNumbers.clear();
    rowItems.clear();
    rowLayout.clearAllItems();

    int numRows = 0;
    for (int row : viewRow)
        numRows = juce::jmax(numRows, row + 1);

    constexpr int dividerThickness = 7;
    constexpr int minRowHeight = 90;
    int index = 0;

    // The rows that already had a size keep it, and a row that is new takes an equal share of what the others
    // would have if they were all equal
    int numUsed = 0;
    double knownTotal = 0.0;
    int numKnown = 0;
    for (int row = 0; row < numRows; ++row)
    {
        const bool used = std::any_of(tabOrder.begin(), tabOrder.end(), [&](int viewId) { return viewRow[(size_t)viewId] == row; });
        numUsed += used ? 1 : 0;
        if (used && rowWeight[(size_t)row] > 0.0)
        {
            knownTotal += rowWeight[(size_t)row];
            ++numKnown;
        }
    }

    const double newRowShare = numKnown > 0 ? knownTotal / (double)numKnown : 1.0 / (double)juce::jmax(1, numUsed);
    double totalWeight = 0.0;
    for (int row = 0; row < numRows; ++row)
    {
        const bool used = std::any_of(tabOrder.begin(), tabOrder.end(), [&](int viewId) { return viewRow[(size_t)viewId] == row; });
        if (used)
            totalWeight += rowWeight[(size_t)row] > 0.0 ? rowWeight[(size_t)row] : newRowShare;
    }

    for (int row = 0; row < numRows; ++row)
    {
        std::vector<juce::Component*> views;
        std::vector<double> weights;
        double known = 0.0;
        int unknown = 0;

        for (int viewId : viewsInRow(row))
        {
            views.push_back(componentOfView(viewId));
            weights.push_back(viewWeight[(size_t)viewId]);
            known += viewWeight[(size_t)viewId];
            unknown += viewWeight[(size_t)viewId] > 0.0 ? 0 : 1;
        }

        if (views.empty())
            continue;

        // A view without a size takes an equal share of the row, and the shares are made to add up to one
        const double fill = known > 0.0 ? known / (double)(views.size() - (size_t)unknown) : 1.0 / (double)views.size();
        double sum = 0.0;
        for (auto& weight : weights)
        {
            weight = weight > 0.0 ? weight : fill;
            sum += weight;
        }
        for (auto& weight : weights)
            weight /= sum;

        if (!rowItems.empty())
        {
            rowLayout.setItemLayout(index, dividerThickness, dividerThickness, dividerThickness);
            rowDividers.push_back(std::make_unique<juce::StretchableLayoutResizerBar>(&rowLayout, index, false));
            addAndMakeVisible(*rowDividers.back());
            rowItems.push_back(rowDividers.back().get());
            ++index;
        }

        multiRows.push_back(std::make_unique<MultiRow>());
        multiRowNumbers.push_back(row);
        addAndMakeVisible(*multiRows.back());
        multiRows.back()->setViews(views, weights);

        // A row can be dragged to any share above its minimum
        const double share = (rowWeight[(size_t)row] > 0.0 ? rowWeight[(size_t)row] : newRowShare) / totalWeight;
        rowLayout.setItemLayout(index++, minRowHeight, -1.0, -share);
        rowItems.push_back(multiRows.back().get());
    }

    // Keep the dividers on top of the rows
    for (auto& divider : rowDividers)
        divider->toFront(false);

    for (auto& handle : dragHandles)
        handle.toFront(false);
    dropOverlay.toFront(false);
}

void UltimateMeterAudioProcessorEditor::layoutViews()
{
    if (viewArea.isEmpty())
        return;

    if (!multiEnabled)
    {
        if (!multiRows.empty() || !rowDividers.empty())
        {
            for (int viewId = 0; viewId < (int)viewRow.size(); ++viewId)
                if (auto* view = componentOfView(viewId))
                    addChildComponent(*view);

            rowDividers.clear();
            multiRows.clear();
            multiRowNumbers.clear();
            rowItems.clear();
        }

        multiLayoutKey = -1;

        if (auto* view = componentOfView(currentMainView))
            view->setBounds(viewArea);
        updateHandles();
        return;
    }

    // The layout is built again only when the views or their rows change, so that a dragged
    // divider keeps its place as the window is resized
    int key = 0;
    for (size_t viewId = 0; viewId < viewRow.size(); ++viewId)
        key = key * 31 + (viewRow[viewId] + 1) * 8 + viewSlot[viewId];

    if (key != multiLayoutKey)
    {
        rebuildMultiLayout();
        multiLayoutKey = key;
    }

    rowLayout.layOutComponents(rowItems.data(), (int)rowItems.size(), viewArea.getX(), viewArea.getY(), viewArea.getWidth(),
                               viewArea.getHeight(), true, true);

    captureSizes();
    updateHandles();
}

void UltimateMeterAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // The secondary click, or a click with the control key, on a view opens the menu of its options
    if (!e.mods.isPopupMenu())
        return;

    for (auto* component = e.eventComponent; component != nullptr && component != this; component = component->getParentComponent())
        for (int viewId = 0; viewId < (int)viewRow.size(); ++viewId)
            if (component == componentOfView(viewId))
            {
                juce::PopupMenu menu;
                menu.addSectionHeader(Parameters::mainViewNames[viewId]);
                buildViewMenu(menu, viewId);
                menu.setLookAndFeel(&lookAndFeel);
                menu.showMenuAsync(juce::PopupMenu::Options());
                return;
            }
}

void UltimateMeterAudioProcessorEditor::buildViewMenu(juce::PopupMenu& menu, int viewId)
{
    using namespace Parameters;
    auto& apvts = audioProcessor.apvts;

    auto addChoice = [&](const juce::String& title, const juce::String& parameterID)
    {
        juce::PopupMenu subMenu;
        addChoiceItems(subMenu, *apvts.getParameter(parameterID));
        menu.addSubMenu(title, subMenu);
    };

    auto addSwitch = [&](const juce::String& title, const juce::String& parameterID)
    {
        menu.addItem(title, true, isOn(parameterID), [&apvts, parameterID]
        {
            auto* parameter = apvts.getParameter(parameterID);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->getValue() > 0.5f ? 0.f : 1.f);
            parameter->endChangeGesture();
        });
    };

    auto addFreeze = [&]
    {
        menu.addItem("Freeze", true, spectrumFrozen, [this] { spectrumFrozen = !spectrumFrozen; });
    };

    switch (viewId)
    {
        case viewGoniometer:
        {
            addChoice("Mode", ID::goniometerMode);
            addChoice("Persistence", ID::goniometerPersistence);

            juce::PopupMenu scaleMenu;
            for (float scale : { 50.f, 75.f, 100.f, 125.f, 150.f, 200.f })
                scaleMenu.addItem(juce::String((int)scale) + "%", true, std::abs(getValue(ID::goniometerScale) - scale) < 1.f, [&apvts, scale]
                {
                    auto* parameter = apvts.getParameter(ID::goniometerScale);
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(scale));
                    parameter->endChangeGesture();
                });
            menu.addSubMenu("Scale", scaleMenu);
            break;
        }

        case viewSpectrum:
            addChoice("Channels", ID::spectrumChannels);
            addChoice("Style", ID::spectrumStyle);
            addChoice("Bars", ID::spectrumBars);
            addChoice("Speed", ID::spectrumSpeed);
            addChoice("Reference", ID::spectrumReference);
            addChoice("Tilt", ID::spectrumTilt);
            addChoice("Smoothing", ID::spectrumSmoothing);
            addChoice("FFT size", ID::spectrumResolution);
            addSwitch("Peak hold", ID::spectrumPeakHold);
            addFreeze();
            break;

        case viewSpectrogram:
            addChoice("Colours", ID::spectrogramColours);
            addChoice("Tilt", ID::spectrumTilt);
            addChoice("FFT size", ID::spectrumResolution);
            addChoice("Time span", ID::timeSpan);
            addFreeze();
            break;

        case viewWaveform:
            addChoice("Channels", ID::waveformChannels);
            addChoice("Colours", ID::waveformColours);
            addChoice("Span", ID::waveformSpan);
            addChoice("Mode", ID::waveformMode);
            addSwitch("Peak history", ID::waveformPeakHistory);
            addSwitch("Time code", ID::waveformTimecode);
            break;

        case viewBalance:
            balanceView.addTargetItems(menu);
            {
                juce::PopupMenu detailMenu;
                addChoiceItems(detailMenu, *apvts.getParameter(ID::balanceDetail));
                menu.addSubMenu("Detail", detailMenu);
            }
            menu.addItem("Start the average again", [this] { balanceView.clearHistory(); });
            break;

        case viewLoudness:
            addChoice("Target", ID::loudnessTarget);
            addChoice("Time span", ID::timeSpan);
            break;

        case viewLoudnessRound:
            addChoice("Reading", ID::radarSource);
            addChoice("Turn takes", ID::radarSpeed);
            addChoice("Target", ID::loudnessTarget);
            menu.addItem("Clear the radar", [this] { radarView.clearHistory(); });
            break;

        case viewHistory:
            addChoice("Show", ID::historyShow);
            addChoice("Time span", ID::timeSpan);
            break;

        default:
            break;
    }
}

void UltimateMeterAudioProcessorEditor::resetMeasurements()
{
    // The loudness and the true peak restart on the audio thread, at the start of its next block
    audioProcessor.resetLoudness();

    // The three views of the timeline are cleared together, as they are recorded together
    spectrogramView.clearHistory();
    historyView.clearHistory();
    waveformView.clearHistory();
    balanceView.clearHistory();
    loudnessView.clearHistory();
    radarView.clearHistory();

    spectrumView.resetPeakHold();
    resetTicksRequested = true;
    heldPeakDb = -200.f;
}

void UltimateMeterAudioProcessorEditor::buildMeterSettingsMenu(juce::PopupMenu& menu)
{
    using namespace Parameters;
    auto& apvts = audioProcessor.apvts;

    auto addSubMenu = [&](const juce::String& title, const juce::String& parameterID)
    {
        juce::PopupMenu subMenu;
        addChoiceItems(subMenu, *apvts.getParameter(parameterID));
        menu.addSubMenu(title, subMenu);
    };

    menu.addSectionHeader("Level meters");
    addSubMenu("Show", ID::meterView);

    menu.addSectionHeader("Peak ticks");
    menu.addItem("Show ticks", true, isOn(ID::showTick), [&apvts]
    {
        auto* parameter = apvts.getParameter(ID::showTick);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->getValue() > 0.5f ? 0.f : 1.f);
        parameter->endChangeGesture();
    });
    addSubMenu("Hold for", ID::holdTime);
    addSubMenu("Then fall at", ID::decayRate);
    menu.addItem("Reset ticks", [this] { resetTicksRequested = true; });

    menu.addSectionHeader("Correlation");
    addSubMenu("Slow reading over", ID::averagerDuration);

    menu.addSectionHeader("Display");
    addSubMenu("Redraw at", ID::refreshRate);
}

//==============================================================================
float UltimateMeterAudioProcessorEditor::getValue(const juce::String& parameterID) const
{
    return audioProcessor.apvts.getRawParameterValue(parameterID)->load();
}

int UltimateMeterAudioProcessorEditor::getChoice(const juce::String& parameterID) const
{
    return juce::roundToInt(getValue(parameterID));
}

bool UltimateMeterAudioProcessorEditor::isOn(const juce::String& parameterID) const
{
    return getValue(parameterID) > 0.5f;
}

//==============================================================================
void UltimateMeterAudioProcessorEditor::buildPresetsMenu(juce::PopupMenu& menu)
{
    menu.addItem("Save as a new preset...", [this] { askForPresetName(); });

    const auto names = Presets::names();
    if (!names.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader("Presets");
        for (const auto& name : names)
            menu.addItem(name, [this, name] { loadPresetFile(Presets::fileOf(name)); });
    }

    menu.addSeparator();
    menu.addItem("Save the current settings as the default", [this]
    {
        captureSizes();
        saveMultiState();
        Presets::save(audioProcessor.apvts.state, Presets::defaultFile());
    });
    menu.addItem("Forget the default", Presets::defaultFile().existsAsFile(), false, [] { Presets::defaultFile().deleteFile(); });

    if (!names.isEmpty())
    {
        juce::PopupMenu deleteMenu;
        for (const auto& name : names)
            deleteMenu.addItem(name, [name] { Presets::fileOf(name).deleteFile(); });
        menu.addSubMenu("Delete a preset", deleteMenu);
    }

    menu.addItem("Show the presets folder", [] { Presets::folder().createDirectory(); Presets::folder().revealToUser(); });
}

void UltimateMeterAudioProcessorEditor::askForPresetName()
{
    auto* window = new juce::AlertWindow("Save preset", "Name of the preset", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", "", "");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->setLookAndFeel(&lookAndFeel);

    window->enterModalState(true, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<UltimateMeterAudioProcessorEditor>(this), window](int result)
    {
        if (result != 1 || safe == nullptr)
            return;

        const auto name = window->getTextEditorContents("name").trim();
        if (name.isEmpty())
            return;

        safe->captureSizes();
        safe->saveMultiState();
        Presets::save(safe->audioProcessor.apvts.state, Presets::fileOf(name));
    }), true);
}

void UltimateMeterAudioProcessorEditor::loadPresetFile(const juce::File& file)
{
    if (!Presets::load(audioProcessor.apvts, file))
        return;

    // The parameters have moved the controls and the views by themselves, but the layout is read again
    readLayoutFromState();
    mainViewAttachment.sendInitialUpdate();
    refreshViews();
}

//==============================================================================
// The views of a row from the left
std::vector<int> UltimateMeterAudioProcessorEditor::viewsInRow(int row) const
{
    std::vector<int> result;
    for (int viewId = 0; viewId < (int)viewRow.size(); ++viewId)
        if (viewRow[(size_t)viewId] == row)
            result.push_back(viewId);

    std::stable_sort(result.begin(), result.end(), [this](int a, int b) { return viewSlot[(size_t)a] < viewSlot[(size_t)b]; });
    return result;
}

// The grips sit at the top of every view, when several views are showing
void UltimateMeterAudioProcessorEditor::updateHandles()
{
    for (int viewId = 0; viewId < (int)dragHandles.size(); ++viewId)
    {
        auto& handle = dragHandles[(size_t)viewId];
        auto* view = componentOfView(viewId);
        const bool shown = multiEnabled && !multiRows.empty() && viewRow[(size_t)viewId] >= 0 && view != nullptr
                           && view->getParentComponent() != nullptr && view->getParentComponent() != this;

        if (!shown)
        {
            handle.setVisible(false);
            continue;
        }

        const auto rect = getLocalArea(view->getParentComponent(), view->getBounds());
        const int width = juce::jmin(handle.getIdealWidth(), rect.getWidth() - 16);
        const auto bounds = juce::Rectangle<int>(width, 16).withCentre({ rect.getCentreX(), rect.getY() + 12 });

        if (handle.getBounds() != bounds)
            handle.setBounds(bounds);
        handle.setVisible(width >= 40);
    }
}

// Where a view that is dragged to a point would land: in the top fifth of another view, in a row above its row, in
// the bottom fifth in a row below, and in the middle beside it, on its left half or its right half
UltimateMeterAudioProcessorEditor::Drop UltimateMeterAudioProcessorEditor::dropAt(juce::Point<int> position, int dragged) const
{
    Drop drop;

    for (int target = 0; target < (int)viewRow.size(); ++target)
    {
        auto* view = const_cast<UltimateMeterAudioProcessorEditor*>(this)->componentOfView(target);
        if (viewRow[(size_t)target] < 0 || view == nullptr || view->getParentComponent() == nullptr || view->getParentComponent() == this)
            continue;

        const auto rect = getLocalArea(view->getParentComponent(), view->getBounds());
        if (!rect.contains(position))
            continue;

        // The room of the whole row, for a band across it
        juce::Rectangle<int> rowRect = rect;
        for (size_t i = 0; i < multiRows.size(); ++i)
            if (multiRowNumbers[i] == viewRow[(size_t)target])
                rowRect = getLocalArea(multiRows[i].get(), multiRows[i]->getLocalBounds());

        const float fy = (float)(position.y - rect.getY()) / (float)juce::jmax(1, rect.getHeight());
        const bool alone = viewsInRow(viewRow[(size_t)dragged]).size() == 1;
        const bool sameRow = viewRow[(size_t)target] == viewRow[(size_t)dragged];

        drop.targetView = target;

        if (fy < 0.2f)
        {
            if (!(alone && sameRow))
            {
                drop.kind = Drop::rowAbove;
                drop.zone = rowRect.withHeight(juce::jmax(36, rowRect.getHeight() / 5));
            }
        }
        else if (fy > 0.8f)
        {
            if (!(alone && sameRow))
            {
                drop.kind = Drop::rowBelow;
                drop.zone = rowRect.withTrimmedTop(rowRect.getHeight() - juce::jmax(36, rowRect.getHeight() / 5));
            }
        }
        else if (target != dragged)
        {
            const bool left = position.x < rect.getCentreX();
            drop.kind = left ? Drop::before : Drop::after;
            drop.zone = left ? rect.withWidth(rect.getWidth() / 2) : rect.withTrimmedLeft(rect.getWidth() / 2);
        }

        return drop;
    }

    return drop;
}

void UltimateMeterAudioProcessorEditor::moveView(int viewId, const Drop& drop)
{
    const int target = drop.targetView;
    if (target < 0 || (target == viewId && (drop.kind == Drop::before || drop.kind == Drop::after)))
        return;

    captureSizes();

    if (drop.kind == Drop::before || drop.kind == Drop::after)
    {
        const int row = viewRow[(size_t)target];
        viewRow[(size_t)viewId] = row;

        auto list = viewsInRow(row);
        list.erase(std::remove(list.begin(), list.end(), viewId), list.end());
        const auto at = std::find(list.begin(), list.end(), target);
        list.insert(drop.kind == Drop::before ? at : at + 1, viewId);

        // The views of the row share it equally again, as one has come in
        for (int i = 0; i < (int)list.size(); ++i)
        {
            viewSlot[(size_t)list[(size_t)i]] = i;
            viewWeight[(size_t)list[(size_t)i]] = 0.0;
        }
    }
    else
    {
        // Out of its row first, which closes the row if it was alone in it
        viewRow[(size_t)viewId] = -1;
        closeUpRows();

        int usedRows = 0;
        for (int row : viewRow)
            usedRows = juce::jmax(usedRows, row + 1);

        if (usedRows >= maxRows)
        {
            // No row is left to open, so it goes in beside the view that it was dropped on
            const int row = viewRow[(size_t)target];
            viewRow[(size_t)viewId] = row;
            auto list = viewsInRow(row);
            list.erase(std::remove(list.begin(), list.end(), viewId), list.end());
            list.push_back(viewId);
            for (int i = 0; i < (int)list.size(); ++i)
            {
                viewSlot[(size_t)list[(size_t)i]] = i;
                viewWeight[(size_t)list[(size_t)i]] = 0.0;
            }
        }
        else
        {
            const int newRow = viewRow[(size_t)target] + (drop.kind == Drop::rowBelow ? 1 : 0);
            for (int& row : viewRow)
                if (row >= newRow)
                    ++row;
            for (int row = maxRows - 1; row > newRow; --row)
                rowWeight[(size_t)row] = rowWeight[(size_t)row - 1];

            rowWeight[(size_t)newRow] = 0.0;
            viewRow[(size_t)viewId] = newRow;
            viewSlot[(size_t)viewId] = 0;
            viewWeight[(size_t)viewId] = 0.0;
        }
    }

    closeUpRows();
    saveMultiState();
    refreshViews();
}
