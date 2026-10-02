/*
  ==============================================================================

    The editor of the plugin: a header with a tab for each view, the view itself,
    a column of meters that is always showing, and a bar of controls for the view.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // The size of the editor is kept in the state, beside the parameters, so that it is saved with the session
    const juce::Identifier editorWidthProperty { "editorWidth" };
    const juce::Identifier editorHeightProperty { "editorHeight" };
    const juce::Identifier multiEnabledProperty { "multiEnabled" };
    const juce::Identifier multiMaskProperty { "multiMask" };
    const juce::Identifier multiSideBySideProperty { "multiSideBySide" };
}

namespace
{
    // The order of the tabs. The view that opens first comes first, the two views of the spectrum are
    // side by side, and so are the three views of the timeline, of which the loudness matters most.
    // The goniometer, the only view of the stereo image, is at the end, beside the correlation. The
    // values of the parameter are in another order, which is that of version 2's first builds, and
    // cannot change without saved sessions opening on the wrong view.
    constexpr std::array<int, 6> tabOrder { Parameters::viewSpectrum, Parameters::viewSpectrogram, Parameters::viewWaveform, Parameters::viewLoudness,
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
    waveformView(spectrumSource),
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
    arrangeButton.setTooltip("Stack the views, or put them side by side");
    arrangeButton.onClick = [this] { arrangeMulti(!multiSideBySide); };

    addChildComponent(goniometerView);
    addChildComponent(spectrumView);
    addChildComponent(spectrogramView);
    addChildComponent(historyView);
    addChildComponent(waveformView);
    addChildComponent(loudnessView);

    addAndMakeVisible(levelMeters);
    addAndMakeVisible(loudnessSummary);
    addAndMakeVisible(correlationBar);

    // The controls of the views, each of which shows with the views that it belongs to
    addAndMakeVisible(controlBar);

    controlBar.addMenu(ControlBar::views({ viewGoniometer }), apvts, ID::goniometerMode, "Mode:");
    controlBar.addMenu(ControlBar::views({ viewGoniometer }), apvts, ID::goniometerPersistence, "Persistence:");

    controlBar.addMenu(ControlBar::views({ viewSpectrum }), apvts, ID::spectrumChannels, "Channels:");
    controlBar.addMenu(ControlBar::views({ viewSpectrum, viewSpectrogram }), apvts, ID::spectrumTilt, "Tilt:");
    controlBar.addMenu(ControlBar::views({ viewSpectrum }), apvts, ID::spectrumSmoothing, "Smoothing:");
    controlBar.addMenu(ControlBar::views({ viewSpectrum, viewSpectrogram }), apvts, ID::spectrumResolution, "FFT:");
    controlBar.addToggle(ControlBar::views({ viewSpectrum }), apvts, ID::spectrumPeakHold, "Peak hold");

    // Freezing is for a moment's look, so it is not a setting that is saved
    freezeButton = &controlBar.addButton(ControlBar::views({ viewSpectrum, viewSpectrogram }), "Freeze", true, [] {});

    controlBar.addMenu(ControlBar::views({ viewHistory }), apvts, ID::historyShow, "Show:");

    // The views that show time share one timeline, so they share its span
    controlBar.addMenu(ControlBar::views({ viewSpectrogram, viewHistory, viewLoudness, viewWaveform }), apvts, ID::timeSpan, "Time span:");

    controlBar.addMenu(ControlBar::views({ viewLoudness }), apvts, ID::loudnessTarget, "Target:");

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
    multiEnabled = (bool)apvts.state.getProperty(multiEnabledProperty, false);
    multiMask = (int)apvts.state.getProperty(multiMaskProperty, 0) & ((1 << (int)tabOrder.size()) - 1);
    multiSideBySide = (bool)apvts.state.getProperty(multiSideBySideProperty, false);
    if (multiMask == 0)
        multiEnabled = false;
    multiButton.setToggleState(multiEnabled, juce::dontSendNotification);
    arrangeButton.setButtonText(multiSideBySide ? "Side by side" : "Stacked");

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
    const float dipLeft = (float)controlBar.getX() + (float)controlBar.getUsedWidth() + 24.f;
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
    controlBar.setBounds(bottomBar.withTrimmedLeft(8));

    // The side column, from the bottom up: the correlation, the loudness, and the bars in what is left
    bounds.removeFromTop(Theme::gap);
    bounds.removeFromBottom(Theme::gap);
    auto side = bounds.removeFromRight(Theme::sideColumnWidth);
    bounds.removeFromRight(Theme::gap);

    correlationBar.setBounds(side.removeFromBottom(58));
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
    correlationBar.update(readings.correlationFast, readings.correlationSlow);

    const float truePeakDb = audioRunning ? toDecibels(juce::jmax(truePeak.peak[0], truePeak.peak[1])) : -200.f;
    const float maxTruePeakDb = toDecibels(juce::jmax(truePeak.maxPeak[0], truePeak.maxPeak[1]));

    secondsSinceReadout += (double)elapsedSeconds;
    const bool readoutDue = secondsSinceReadout >= Theme::readoutIntervalSeconds;
    if (readoutDue)
        secondsSinceReadout = 0.0;

    loudnessSummary.update(loudness, maxTruePeakDb, target, readoutDue);

    // The spectrogram, the history and the loudness share one timeline. They all record in every
    // update, whichever view is showing, so that the same moment is in the same place in all of them.
    // While no audio arrives the clock stands still, and so do all three.
    const int numNewSlots = timelineClock.advance(elapsedSeconds, audioRunning);
    const float timeSpan = valueAt(timeSpansSeconds, getChoice(ID::timeSpan));

    loudnessView.setSpan(timeSpan);
    historyView.setSpan(timeSpan);
    waveformView.setSpan(timeSpan);
    historyView.setShown(getChoice(ID::historyShow) != rmsMeters, getChoice(ID::historyShow) != peakMeters);
    spectrogramView.setSpan(timeSpan);

    loudnessView.update(loudness, truePeakDb, maxTruePeakDb, target, numNewSlots, elapsedSeconds, readoutDue);
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
        const bool frozen = freezeButton != nullptr && freezeButton->getToggleState();
        spectrogramView.record(numNewSlots, hasNewSpectra, tilt, frozen);
        waveformView.record(numNewSlots, hasNewSpectra, juce::jmax(readings.peak[0], readings.peak[1]), juce::jmax(readings.rms[0], readings.rms[1]));

        if (spectrumView.isVisible())
        {
            SpectrumView::Settings spectrumSettings;
            spectrumSettings.midSide = getChoice(ID::spectrumChannels) == 1;
            spectrumSettings.tiltDbPerOctave = tilt;
            spectrumSettings.smoothingOctaves = valueAt(spectrumSmoothingOctaves, getChoice(ID::spectrumSmoothing));
            spectrumSettings.peakHold = isOn(ID::spectrumPeakHold);
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
        default:                          return nullptr;
    }
}

void UltimateMeterAudioProcessorEditor::saveMultiState()
{
    auto& state = audioProcessor.apvts.state;
    state.setProperty(multiEnabledProperty, multiEnabled, nullptr);
    state.setProperty(multiMaskProperty, multiMask, nullptr);
    state.setProperty(multiSideBySideProperty, multiSideBySide, nullptr);
}

void UltimateMeterAudioProcessorEditor::setMultiMode(bool enabled)
{
    if (enabled && multiMask == 0)
    {
        // The first time, the view that is open comes up with its natural companion
        multiMask = (1 << currentMainView) | (1 << (currentMainView == Parameters::viewSpectrum ? Parameters::viewLoudness : Parameters::viewSpectrum));
    }

    multiEnabled = enabled;
    multiButton.setToggleState(enabled, juce::dontSendNotification);
    saveMultiState();
    refreshViews();
}

void UltimateMeterAudioProcessorEditor::toggleViewInMulti(int viewId)
{
    const int mask = multiMask ^ (1 << viewId);

    // At least one view stays on
    if (mask == 0)
        return;

    multiMask = mask;
    if ((mask & (1 << viewId)) != 0)
        currentMainView = viewId;

    saveMultiState();
    refreshViews();
}

void UltimateMeterAudioProcessorEditor::arrangeMulti(bool sideBySide)
{
    multiSideBySide = sideBySide;
    arrangeButton.setButtonText(sideBySide ? "Side by side" : "Stacked");
    resized();
    saveMultiState();
    refreshViews();
}

void UltimateMeterAudioProcessorEditor::refreshViews()
{
    const int viewMask = multiEnabled ? multiMask : (1 << currentMainView);

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

    // One bar of controls for all the views that are showing
    controlBar.showViews(viewMask);

    // The dip in the bottom bar follows the width of the controls
    repaint(getLocalBounds().removeFromBottom(Theme::bottomBarHeight));

    multiLayoutKey = -1;
    layoutViews();
}

void UltimateMeterAudioProcessorEditor::rebuildMultiLayout()
{
    multiBars.clear();
    multiItems.clear();
    multiLayout.clearAllItems();

    std::vector<juce::Component*> views;
    for (int tab = 0; tab < (int)tabOrder.size(); ++tab)
        if ((multiMask & (1 << tabOrder[(size_t)tab])) != 0)
            views.push_back(componentOfView(tabOrder[(size_t)tab]));

    constexpr int barThickness = 7;
    const int minSize = multiSideBySide ? 180 : 90;
    int index = 0;

    for (size_t i = 0; i < views.size(); ++i)
    {
        // Every view starts with an equal share, and can be dragged to any share above its minimum
        multiLayout.setItemLayout(index++, minSize, -1.0, -1.0 / (double)views.size());
        multiItems.push_back(views[i]);

        if (i + 1 < views.size())
        {
            multiLayout.setItemLayout(index, barThickness, barThickness, barThickness);
            multiBars.push_back(std::make_unique<juce::StretchableLayoutResizerBar>(&multiLayout, index, multiSideBySide));
            addAndMakeVisible(*multiBars.back());
            multiItems.push_back(multiBars.back().get());
            ++index;
        }
    }
}

void UltimateMeterAudioProcessorEditor::layoutViews()
{
    if (viewArea.isEmpty())
        return;

    if (!multiEnabled)
    {
        multiBars.clear();
        multiItems.clear();
        multiLayoutKey = -1;

        if (auto* view = componentOfView(currentMainView))
            view->setBounds(viewArea);
        return;
    }

    // The layout is built again only when the views or their arrangement change, so that a dragged
    // divider keeps its place as the window is resized
    const int key = multiMask | (multiSideBySide ? 1 << 16 : 0);
    if (key != multiLayoutKey)
    {
        rebuildMultiLayout();
        multiLayoutKey = key;
    }

    multiLayout.layOutComponents(multiItems.data(), (int)multiItems.size(), viewArea.getX(), viewArea.getY(), viewArea.getWidth(),
                                 viewArea.getHeight(), !multiSideBySide, true);
}

void UltimateMeterAudioProcessorEditor::resetMeasurements()
{
    // The loudness and the true peak restart on the audio thread, at the start of its next block
    audioProcessor.resetLoudness();

    // The three views of the timeline are cleared together, as they are recorded together
    spectrogramView.clearHistory();
    historyView.clearHistory();
    waveformView.clearHistory();
    loudnessView.clearHistory();

    spectrumView.resetPeakHold();
    resetTicksRequested = true;
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
