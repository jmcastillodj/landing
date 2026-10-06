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
    const juce::Identifier multiSlotsProperty { "multiSlots" };   // the column of each view
    const juce::Identifier multiStacksProperty { "multiStacks" }; // the place of each view in its column
    const juce::Identifier multiHeightsProperty { "multiHeights" };
}

namespace
{
    // The order of the tabs. The view that opens first comes first, the two views of the spectrum are
    // side by side, and so are the three views of the timeline, of which the loudness matters most.
    // The goniometer, the only view of the stereo image, is at the end, beside the correlation. The
    // values of the parameter are in another order, which is that of version 2's first builds, and
    // cannot change without saved sessions opening on the wrong view.
    constexpr std::array<int, 11> tabOrder { Parameters::viewSpectrum, Parameters::viewBalance, Parameters::viewSpectrogram, Parameters::viewWaveform, Parameters::viewLoudness, Parameters::viewLoudnessRound, Parameters::viewReference, Parameters::viewCorrelometer, Parameters::viewVu,
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
    referenceView(p.references, p.apvts.state),
    correlometerView(p.apvts, p.apvts.state),
    vuView(p.apvts),
    monitorStrip(p.apvts),
    mainViewAttachment(*audioProcessor.apvts.getParameter(Parameters::ID::mainView),
        [this](float value) { showMainView(juce::roundToInt(value)); }),
    themeAttachment(*p.apvts.getParameter(Parameters::ID::theme), [this](float value) { applyTheme(juce::roundToInt(value)); }),
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

    // The colour scheme
    addAndMakeVisible(themeButton);
    themeButton.setTooltip("Choose the colours of the meter");
    themeButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addSectionHeader("Theme");
        for (int i = 0; i < Theme::themeNames.size(); ++i)
            menu.addItem(Theme::themeNames[i], true, appliedTheme == i, [this, i]
            {
                if (auto* parameter = audioProcessor.apvts.getParameter(Parameters::ID::theme))
                {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(parameter->convertTo0to1((float)i));
                    parameter->endChangeGesture();
                }
            });
        menu.setLookAndFeel(&lookAndFeel);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&themeButton));
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
    addChildComponent(referenceView);
    addChildComponent(correlometerView);
    addChildComponent(vuView);
    addAndMakeVisible(monitorStrip);
    vuView.onAdvanced = [this](juce::Rectangle<int> area)
    {
        juce::PopupMenu menu;
        buildVuMenu(menu);
        menu.setLookAndFeel(&lookAndFeel);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(vuView.localAreaToGlobal(area)));
    };

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
    // The guide of the waveform: on or off, and the level that it is at
    waveformView.onGuideChanged = [this](bool on, float db)
    {
        auto& apvts = audioProcessor.apvts;
        if (auto* switchParameter = apvts.getParameter(Parameters::ID::waveformGuideOn))
            if ((switchParameter->getValue() > 0.5f) != on)
            {
                switchParameter->beginChangeGesture();
                switchParameter->setValueNotifyingHost(on ? 1.f : 0.f);
                switchParameter->endChangeGesture();
            }

        if (auto* level = apvts.getParameter(Parameters::ID::waveformGuideDb))
        {
            level->beginChangeGesture();
            level->setValueNotifyingHost(level->convertTo0to1(db));
            level->endChangeGesture();
        }
    };

    // The same for the span in musical time, which is another list of choices
    auto setMusicalChoice = [this](int index)
    {
        if (auto* parameter = audioProcessor.apvts.getParameter(Parameters::ID::waveformSpanMusical))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)juce::jlimit(0, (int)Parameters::waveformSpanMusicalNames.size() - 1, index)));
            parameter->endChangeGesture();
        }
    };
    waveformView.onHorizontalStep = [this, setSpanChoice, setMusicalChoice](int step, bool coarse)
    {
        // A step towards closer is a shorter span
        if (getChoice(Parameters::ID::waveformSpanUnit) == 1)
            setMusicalChoice(getChoice(Parameters::ID::waveformSpanMusical) - step * (coarse ? 2 : 1));
        else
            setSpanChoice(getChoice(Parameters::ID::waveformSpan) - step * (coarse ? 2 : 1));
    };
    waveformView.onHorizontalReset = [this, setSpanChoice, setMusicalChoice]
    {
        if (getChoice(Parameters::ID::waveformSpanUnit) == 1)
            setMusicalChoice(Parameters::defaultWaveformSpanMusical);
        else
            setSpanChoice(Parameters::defaultWaveformSpan);
    };

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
        fillTargetItems(menu);
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
            const char* labels[] { "", "A column to the left", "A column to the right", "A row of its own above", "A row of its own below", "Stack it above this view", "Stack it below this view" };
            dropOverlay.show(currentDrop.zone, labels[(int)currentDrop.kind]);
        };
        handle.onCancel = [this]
        {
            dropOverlay.show({}, {});
            dropOverlay.setVisible(false);
            draggedView = -1;
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

    for (auto* view : std::initializer_list<juce::Component*> { &goniometerView, &spectrumView, &spectrogramView, &historyView, &waveformView, &balanceView, &loudnessView, &radarView, &referenceView, &correlometerView, &vuView })
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
    themeAttachment.sendInitialUpdate();
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

// The buttons at the right of the header, from the left: the colour scheme, the layout of the views (in multi mode), and multi mode
void UltimateMeterAudioProcessorEditor::layoutHeaderButtons()
{
    auto header = getLocalBounds().removeFromTop(Theme::headerHeight);
    header.removeFromLeft(juce::roundToInt(nameTabRight() + shoulderWidth) + 4);
    header.removeFromTop(juce::roundToInt(rimThickness));
    header.removeFromRight(8);
    multiButton.setBounds(header.removeFromRight(multiButton.getIdealWidth()).withTrimmedTop(2));
    if (arrangeButton.isVisible())
        arrangeButton.setBounds(header.removeFromRight(arrangeButton.getIdealWidth()).withTrimmedTop(2));
    themeButton.setBounds(header.removeFromRight(themeButton.getIdealWidth()).withTrimmedTop(2));
    tabs.setBounds(header.removeFromLeft(juce::jmin(header.getWidth(), tabs.getIdealWidth())));
}

void UltimateMeterAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // The tabs start where the shoulder of the name's tab has come up to the rim
    auto header = bounds.removeFromTop(Theme::headerHeight);
    header.removeFromLeft(juce::roundToInt(nameTabRight() + shoulderWidth) + 4);
    header.removeFromTop(juce::roundToInt(rimThickness));
    (void)header;
    layoutHeaderButtons();

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
        const int room = side.getHeight() - 132 - 2 * Theme::gap - 150 - 76;
        const int stereoHeight = room >= StereoPanel::fullHeight ? StereoPanel::fullHeight
                               : room >= StereoPanel::barsHeight + 90 ? room : StereoPanel::barsHeight;
        stereoPanel.setBounds(side.removeFromBottom(stereoHeight));
    }
    side.removeFromBottom(Theme::gap);
    loudnessSummary.setBounds(side.removeFromBottom(132));
    side.removeFromBottom(Theme::gap);
    monitorStrip.setBounds(side.removeFromBottom(74));
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

    // The colours are shared by every editor that is open, so the one that the mouse is over takes its own back
    if (appliedTheme >= 0 && Theme::currentTheme != appliedTheme && isMouseOver(true))
        applyTheme(appliedTheme);

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

    const bool audioRunning = lastUpdateTime - lastAudioTime <= silenceTimeoutSeconds
                              && !audioProcessor.hostIdle.load(std::memory_order_relaxed);
    if (!audioRunning)
    {
        readings = {};

        // The momentary and short-term loudness describe the present, so they go quiet with the audio.
        // The integrated loudness and the range describe the programme so far, so they stay.
        loudness.momentary = loudness.shortTerm = LoudnessMeter::silence;
    }

    auto toDecibels = [](float gain) { return juce::Decibels::gainToDecibels(gain, -200.f); };
    const float target = currentTargetLufs();

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
        waveformSettings.sweep = getChoice(ID::waveformMode) >= 1;
        waveformSettings.gridLocked = getChoice(ID::waveformMode) == 2;
        waveformSettings.zoom = getValue(ID::waveformZoom);
        waveformSettings.spanSeconds = valueAt(waveformSpansSeconds, getChoice(ID::waveformSpan));

        // In musical time the span is a note or some bars, at the tempo of the host
        waveformSettings.bpm = juce::jmax(20.0, audioProcessor.hostBpm.load(std::memory_order_relaxed));
        waveformSettings.beatsPerBar = audioProcessor.hostBeatsPerBar.load(std::memory_order_relaxed);
        waveformSettings.ppq = audioProcessor.hostPpq.load(std::memory_order_relaxed);
        waveformSettings.ppqAtLatest = audioProcessor.hostPpqAtBlockEnd.load(std::memory_order_relaxed);
        waveformSettings.ringTotalAtLatest = (double)audioProcessor.hostTotalAtBlockEnd.load(std::memory_order_relaxed);
        if (getChoice(ID::waveformSpanUnit) == 1)
        {
            const int choice = getChoice(ID::waveformSpanMusical);
            const double entry = waveformSpanMusicalBeats[(size_t)juce::jlimit(0, (int)waveformSpanMusicalBeats.size() - 1, choice)];
            waveformSettings.musical = true;
            waveformSettings.spanBeats = entry > 0.0 ? entry : -entry * waveformSettings.beatsPerBar;
            waveformSettings.spanSeconds = (float)(waveformSettings.spanBeats * 60.0 / waveformSettings.bpm);
            waveformSettings.spanLabel = waveformSpanMusicalNames[choice];
        }
        else if (waveformSettings.gridLocked)
        {
            // Locked to the grid, a span in time is counted in beats too, so that the bars show
            waveformSettings.musical = true;
            waveformSettings.spanBeats = (double)waveformSettings.spanSeconds * waveformSettings.bpm / 60.0;
        }
        waveformSettings.peakHistory = isOn(ID::waveformPeakHistory);
        waveformSettings.timeCode = isOn(ID::waveformTimecode);
        waveformSettings.guideOn = isOn(ID::waveformGuideOn);
        waveformSettings.guideDb = getValue(ID::waveformGuideDb);
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

    referenceView.setCompact(multiEnabled);
    correlometerView.update(audioProcessor.correlator);
    vuView.update(audioProcessor.vuEngine.read(), elapsedSeconds);

    // What the reference view compares its tracks with: the mix, as it has been playing
    {
        if (audioRunning)
            widthAverage += (readings.width - widthAverage) * (1.f - std::exp(-elapsedSeconds / 15.f));

        ReferenceView::Mix mixData;
        mixData.integratedLufs = loudness.integrated;
        mixData.peakDb = maxTruePeakDb;
        mixData.width = widthAverage;
        mixData.plr = std::isfinite(loudness.integrated) && loudness.integrated > -100.f && maxTruePeakDb > -150.f ? maxTruePeakDb - loudness.integrated : 0.f;
        balanceView.getAverageCurve(mixData.curve);
        referenceView.update(mixData, elapsedSeconds);
    }

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
        // Following the host, the mix is compared with the whole of the track, so the balance is the average of everything since the reset
        const float averageSeconds = audioProcessor.references.mirror.load() ? 0.f : valueAt(balanceAverageSeconds, getChoice(ID::balanceAverage));
        balanceView.record(hasNewSpectra, elapsedSeconds, averageSeconds);


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
        case Parameters::viewReference:   return &referenceView;
        case Parameters::viewCorrelometer: return &correlometerView;
        case Parameters::viewVu:          return &vuView;
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
        for (int i = 0; i < (int)viewCell.size(); ++i)
            viewCell[(size_t)i] = i < slots.size() ? slots[i].getIntValue() : i;

        const auto stacks = juce::StringArray::fromTokens(apvts.state.getProperty(multiStacksProperty, "").toString(), ",", "");
        const auto heights = juce::StringArray::fromTokens(apvts.state.getProperty(multiHeightsProperty, "").toString(), ",", "");
        viewHeight.fill(0.0);
        for (int i = 0; i < (int)viewStack.size(); ++i)
        {
            viewStack[(size_t)i] = i < stacks.size() ? stacks[i].getIntValue() : 0;
            viewHeight[(size_t)i] = i < heights.size() ? heights[i].getDoubleValue() : 0.0;
        }

        normaliseRows();
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
    for (int slot : viewCell)
        slots.add(juce::String(slot));
    state.setProperty(multiSlotsProperty, slots.joinIntoString(","), nullptr);

    juce::StringArray stacks, heights;
    for (int stack : viewStack)
        stacks.add(juce::String(stack));
    for (double height : viewHeight)
        heights.add(juce::String(height, 4));
    state.setProperty(multiStacksProperty, stacks.joinIntoString(","), nullptr);
    state.setProperty(multiHeightsProperty, heights.joinIntoString(","), nullptr);
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

    auto viewIdOf = [this](juce::Component* component)
    {
        for (int viewId = 0; viewId < (int)viewRow.size(); ++viewId)
            if (component == componentOfView(viewId))
                return viewId;
        return -1;
    };

    for (size_t i = 0; i < multiRows.size(); ++i)
    {
        rowWeight[(size_t)multiRowNumbers[i]] = (double)multiRows[i]->getHeight() / totalHeight;

        double totalWidth = 0.0;
        for (auto& cell : multiRows[i]->getCells())
            totalWidth += cell->getWidth();

        for (auto& cell : multiRows[i]->getCells())
        {
            double cellHeight = 0.0;
            for (auto* view : cell->getViews())
                cellHeight += view->getHeight();

            // Every view of a column has the width of the column, and its own share of the column's height
            for (auto* view : cell->getViews())
            {
                const int viewId = viewIdOf(view);
                if (viewId < 0)
                    continue;

                if (totalWidth > 0.0)
                    viewWeight[(size_t)viewId] = (double)cell->getWidth() / totalWidth;
                if (cellHeight > 0.0)
                    viewHeight[(size_t)viewId] = (double)view->getHeight() / cellHeight;
            }
        }
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

    normaliseRows();
}

// The columns of a row are numbered from the left without a gap, and the views of a column from the top
void UltimateMeterAudioProcessorEditor::normaliseRows()
{
    for (int row = 0; row < maxRows; ++row)
    {
        int cell = -1, lastOriginal = -1000000;
        int stack = 0;

        for (int viewId : viewsInRow(row))
        {
            const int original = viewCell[(size_t)viewId];
            if (original != lastOriginal)
            {
                ++cell;
                lastOriginal = original;
                stack = 0;
            }

            viewCell[(size_t)viewId] = cell;
            viewStack[(size_t)viewId] = stack++;
        }
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
        viewCell[(size_t)viewId] = 1000; // after the others of its row, in a column of its own
        viewStack[(size_t)viewId] = 0;
        viewWeight[(size_t)viewId] = viewHeight[(size_t)viewId] = 0.0;
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
    viewCell[(size_t)viewId] = 1000; // a column of its own, after the others
    viewStack[(size_t)viewId] = 0;
    viewWeight[(size_t)viewId] = viewHeight[(size_t)viewId] = 0.0;
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
    viewHeight.fill(0.0);
    int position = 0;
    for (int viewId : tabOrder)
    {
        if (viewRow[(size_t)viewId] < 0)
            continue;

        viewRow[(size_t)viewId] = preset == 1 ? 0 : juce::jmin(maxRows - 1, preset == 2 ? position / 2 : position);
        viewCell[(size_t)viewId] = preset == 1 ? position : preset == 2 ? position % 2 : 0;
        viewStack[(size_t)viewId] = 0;
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
    layoutHeaderButtons();

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
    constexpr int minRowHeight = 70;
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
        std::vector<std::vector<juce::Component*>> cellViews;
        std::vector<double> widths;
        std::vector<std::vector<double>> heights;

        for (const auto& cell : cellsInRow(row))
        {
            cellViews.emplace_back();
            heights.emplace_back();
            for (int viewId : cell)
            {
                cellViews.back().push_back(componentOfView(viewId));
                heights.back().push_back(viewHeight[(size_t)viewId]);
            }
            widths.push_back(viewWeight[(size_t)cell.front()]);
        }

        if (cellViews.empty())
            continue;

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
        multiRows.back()->setCells(cellViews, widths, heights);

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
    juce::uint64 hash = 1469598103934665603ull;
    for (size_t viewId = 0; viewId < viewRow.size(); ++viewId)
        hash = (hash ^ (juce::uint64)((viewRow[viewId] + 1) * 4096 + viewCell[viewId] * 64 + viewStack[viewId])) * 1099511628211ull;
    const auto key = (juce::int64)(hash & 0x7fffffffffffffffull);

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
            {
                // The span, in time or in musical time. Choosing one of either list switches to that kind of span.
                auto setBoth = [&apvts](const juce::String& unitValueID, int index, const juce::String& listID, int unit)
                {
                    juce::ignoreUnused(unitValueID);
                    if (auto* list = apvts.getParameter(listID))
                    {
                        list->beginChangeGesture();
                        list->setValueNotifyingHost(list->convertTo0to1((float)index));
                        list->endChangeGesture();
                    }
                    if (auto* unitParameter = apvts.getParameter(ID::waveformSpanUnit))
                    {
                        unitParameter->beginChangeGesture();
                        unitParameter->setValueNotifyingHost(unitParameter->convertTo0to1((float)unit));
                        unitParameter->endChangeGesture();
                    }
                };

                const bool musicalNow = getChoice(ID::waveformSpanUnit) == 1;

                juce::PopupMenu timeMenu;
                for (int i = 0; i < waveformSpanNames.size(); ++i)
                    timeMenu.addItem(waveformSpanNames[i], true, !musicalNow && getChoice(ID::waveformSpan) == i, [setBoth, i] { setBoth({}, i, ID::waveformSpan, 0); });
                menu.addSubMenu("Span in time", timeMenu);

                juce::PopupMenu musicalMenu;
                for (int i = 0; i < waveformSpanMusicalNames.size(); ++i)
                    musicalMenu.addItem(waveformSpanMusicalNames[i], true, musicalNow && getChoice(ID::waveformSpanMusical) == i, [setBoth, i] { setBoth({}, i, ID::waveformSpanMusical, 1); });
                menu.addSubMenu("Span in bars and notes", musicalMenu);
            }
            addChoice("Mode", ID::waveformMode);
            addSwitch("Peak history", ID::waveformPeakHistory);
            addSwitch("Time code", ID::waveformTimecode);
            addSwitch("Level guide", ID::waveformGuideOn);
            break;

        case viewBalance:
            balanceView.addTargetItems(menu);
            {
                juce::PopupMenu detailMenu;
                addChoiceItems(detailMenu, *apvts.getParameter(ID::balanceDetail));
                menu.addSubMenu("Detail", detailMenu);
            }
            {
                juce::PopupMenu averageMenu;
                addChoiceItems(averageMenu, *apvts.getParameter(ID::balanceAverage));
                menu.addSubMenu("Average over", averageMenu);
            }
            menu.addItem("Start the average again", [this] { balanceView.clearHistory(); });
            break;

        case viewLoudness:
            {
                juce::PopupMenu targetMenu;
                fillTargetItems(targetMenu);
                menu.addSubMenu("Target", targetMenu);
            }
            addChoice("Time span", ID::timeSpan);
            break;

        case viewLoudnessRound:
            addChoice("Reading", ID::radarSource);
            addChoice("Turn takes", ID::radarSpeed);
            {
                juce::PopupMenu targetMenu;
                fillTargetItems(targetMenu);
                menu.addSubMenu("Target", targetMenu);
            }
            menu.addItem("Clear the radar", [this] { radarView.clearHistory(); });
            break;

        case viewVu:
            buildVuMenu(menu);
            break;

        case viewCorrelometer:
            addChoice("Primary", ID::corrPrimary);
            addChoice("Secondary", ID::corrSecondary);
            addChoice("Scale", ID::corrScale);
            addChoice("Bandwidth", ID::corrBandwidth);
            menu.addItem("Hide the controls", true, correlometerView.areControlsHidden(), [this] { correlometerView.setControlsHidden(!correlometerView.areControlsHidden()); });
            break;

        case viewReference:
            for (int slot = 0; slot < ReferenceManager::numSlots; ++slot)
                menu.addItem("Load into reference " + juce::String(slot + 1) + "...", [this, slot] { referenceView.chooseFile(slot); });
            menu.addSeparator();
            menu.addItem("Remove the selected reference", [this] { referenceView.removeSelected(); });
            menu.addItem("Loop the loudest part", [this] { referenceView.smartLoop(); });
            menu.addItem("Mirror the DAW position", true, referenceView.isMirror(), [this] { referenceView.setMirror(!referenceView.isMirror()); });
            {
                juce::PopupMenu offsetMenu;
                const double rate = audioProcessor.references.hostRate.load();
                for (int ms : { -100, -10, -1, 1, 10, 100 })
                    offsetMenu.addItem((ms > 0 ? "+" : "") + juce::String(ms) + " ms", [this, ms, rate] { referenceView.nudge((int)(rate * ms / 1000.0)); });
                offsetMenu.addSeparator();
                offsetMenu.addItem("No offset", [this] { referenceView.resetOffset(); });
                menu.addSubMenu("Move the reference in time", offsetMenu);
            }
            menu.addItem("Level match", true, referenceView.isLevelMatched(), [this] { referenceView.setLevelMatched(!referenceView.isLevelMatched()); });
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
    menu.addItem("Import a preset from a file...", [this] { importPreset(); });
    menu.addItem("Export the current settings to a file...", [this] { exportCurrentSettings(); });

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

        juce::PopupMenu exportMenu;
        for (const auto& name : names)
            exportMenu.addItem(name, [this, name] { exportPreset(Presets::fileOf(name)); });
        menu.addSubMenu("Export a preset to a file", exportMenu);
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

    std::stable_sort(result.begin(), result.end(), [this](int a, int b)
    {
        return viewCell[(size_t)a] != viewCell[(size_t)b] ? viewCell[(size_t)a] < viewCell[(size_t)b] : viewStack[(size_t)a] < viewStack[(size_t)b];
    });
    return result;
}

// The columns of a row from the left, each with its views from the top
std::vector<std::vector<int>> UltimateMeterAudioProcessorEditor::cellsInRow(int row) const
{
    std::vector<std::vector<int>> cells;
    int lastCell = -1000000;

    for (int viewId : viewsInRow(row))
    {
        if (viewCell[(size_t)viewId] != lastCell)
        {
            cells.emplace_back();
            lastCell = viewCell[(size_t)viewId];
        }
        cells.back().push_back(viewId);
    }

    return cells;
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

    // The correlometer keeps its buttons clear of its grip
    correlometerView.setTopInset(dragHandles[(size_t)Parameters::viewCorrelometer].isVisible() ? 22 : 0);
}

// Where a view that is dragged to a point would land, in the view that is under it: at its left or its right edge in a
// column of its own beside the view, in its upper or lower half stacked with it, and at the very top or bottom of a column
// in a row of its own above or below the row
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

        const float fx = (float)(position.x - rect.getX()) / (float)juce::jmax(1, rect.getWidth());
        const float fy = (float)(position.y - rect.getY()) / (float)juce::jmax(1, rect.getHeight());

        // Whether the view is at the top or the bottom of its column
        bool isTop = true, isBottom = true;
        for (const auto& cell : cellsInRow(viewRow[(size_t)target]))
            if (std::find(cell.begin(), cell.end(), target) != cell.end())
            {
                isTop = cell.front() == target;
                isBottom = cell.back() == target;
            }

        const bool alone = viewsInRow(viewRow[(size_t)dragged]).size() == 1;
        const bool onItself = target == dragged;
        const int bandHeight = juce::jmax(36, rowRect.getHeight() / 6);

        drop.targetView = target;

        if (fy < 0.12f && isTop)
        {
            if (!(alone && viewRow[(size_t)target] == viewRow[(size_t)dragged]))
            {
                drop.kind = Drop::rowAbove;
                drop.zone = rowRect.withHeight(bandHeight);
            }
        }
        else if (fy > 0.88f && isBottom)
        {
            if (!(alone && viewRow[(size_t)target] == viewRow[(size_t)dragged]))
            {
                drop.kind = Drop::rowBelow;
                drop.zone = rowRect.withTrimmedTop(rowRect.getHeight() - bandHeight);
            }
        }
        else if (!onItself)
        {
            if (fx < 0.22f)
            {
                drop.kind = Drop::before;
                drop.zone = rect.withWidth(juce::jmax(40, rect.getWidth() * 22 / 100));
            }
            else if (fx > 0.78f)
            {
                drop.kind = Drop::after;
                drop.zone = rect.withTrimmedLeft(rect.getWidth() - juce::jmax(40, rect.getWidth() * 22 / 100));
            }
            else if (fy < 0.5f)
            {
                drop.kind = Drop::stackAbove;
                drop.zone = rect.withHeight(rect.getHeight() / 2).reduced(rect.getWidth() * 22 / 100, 0);
            }
            else
            {
                drop.kind = Drop::stackBelow;
                drop.zone = rect.withTrimmedTop(rect.getHeight() / 2).reduced(rect.getWidth() * 22 / 100, 0);
            }
        }

        return drop;
    }

    return drop;
}

void UltimateMeterAudioProcessorEditor::moveView(int viewId, const Drop& drop)
{
    const int target = drop.targetView;
    const bool newRowKind = drop.kind == Drop::rowAbove || drop.kind == Drop::rowBelow;
    if (target < 0 || drop.kind == Drop::none || (target == viewId && !newRowKind))
        return;

    captureSizes();

    if (!newRowKind)
    {
        // The view goes into the columns of the row of the target, which are rebuilt as lists with it in its new place
        const int row = viewRow[(size_t)target];
        viewRow[(size_t)viewId] = -1;

        auto cells = cellsInRow(row);
        size_t cellIndex = 0, stackIndex = 0;
        for (size_t c = 0; c < cells.size(); ++c)
            for (size_t k = 0; k < cells[c].size(); ++k)
                if (cells[c][k] == target)
                {
                    cellIndex = c;
                    stackIndex = k;
                }

        switch (drop.kind)
        {
            case Drop::before:     cells.insert(cells.begin() + (std::ptrdiff_t)cellIndex, std::vector<int> { viewId }); break;
            case Drop::after:      cells.insert(cells.begin() + (std::ptrdiff_t)cellIndex + 1, std::vector<int> { viewId }); break;
            case Drop::stackAbove: cells[cellIndex].insert(cells[cellIndex].begin() + (std::ptrdiff_t)stackIndex, viewId); break;
            case Drop::stackBelow: cells[cellIndex].insert(cells[cellIndex].begin() + (std::ptrdiff_t)stackIndex + 1, viewId); break;
            default: break;
        }

        for (size_t c = 0; c < cells.size(); ++c)
            for (size_t k = 0; k < cells[c].size(); ++k)
            {
                const int id = cells[c][k];
                viewRow[(size_t)id] = row;
                viewCell[(size_t)id] = (int)c;
                viewStack[(size_t)id] = (int)k;
            }

        // What has come in shares fairly: the columns of the row, or the views of the column, are equal again
        if (drop.kind == Drop::before || drop.kind == Drop::after)
        {
            for (const auto& cell : cells)
                for (int id : cell)
                    viewWeight[(size_t)id] = 0.0;
        }
        else
        {
            for (int id : cells[cellIndex])
                viewHeight[(size_t)id] = 0.0;
        }

        viewWeight[(size_t)viewId] = viewHeight[(size_t)viewId] = 0.0;
    }
    else
    {
        // A view dropped on its own edge, in a row that it shares, comes out into a row of its own beside that row
        const bool onItself = target == viewId;
        if (onItself && viewsInRow(viewRow[(size_t)viewId]).size() < 2)
            return;

        const int ownRow = viewRow[(size_t)viewId];

        // Out of its row first, which closes the row if it was alone in it
        viewRow[(size_t)viewId] = -1;
        closeUpRows();

        int usedRows = 0;
        for (int row : viewRow)
            usedRows = juce::jmax(usedRows, row + 1);

        auto putBesideTarget = [&]
        {
            // No row is left to open, so it goes back in as a column beside the view that it was dropped on
            const int row = onItself ? ownRow : viewRow[(size_t)target];
            viewRow[(size_t)viewId] = row;
            viewCell[(size_t)viewId] = 1000;
            viewStack[(size_t)viewId] = 0;
            for (int id : viewsInRow(row))
                viewWeight[(size_t)id] = 0.0;
        };

        if (usedRows >= maxRows)
        {
            putBesideTarget();
        }
        else
        {
            const int newRow = (onItself ? ownRow : viewRow[(size_t)target]) + (drop.kind == Drop::rowBelow ? 1 : 0);
            for (int& row : viewRow)
                if (row >= newRow)
                    ++row;
            for (int row = maxRows - 1; row > newRow; --row)
                rowWeight[(size_t)row] = rowWeight[(size_t)row - 1];

            rowWeight[(size_t)newRow] = 0.0;
            viewRow[(size_t)viewId] = newRow;
            viewCell[(size_t)viewId] = 0;
            viewStack[(size_t)viewId] = 0;
            viewWeight[(size_t)viewId] = viewHeight[(size_t)viewId] = 0.0;
        }
    }

    closeUpRows();
    saveMultiState();
    refreshViews();
}

//==============================================================================
// The loudness to aim for: one of the standards, or the number that was typed in
float UltimateMeterAudioProcessorEditor::currentTargetLufs() const
{
    const int choice = getChoice(Parameters::ID::loudnessTarget);
    if (choice == Parameters::customTargetChoice)
        return getValue(Parameters::ID::loudnessCustomTarget);

    return Parameters::valueAt(Parameters::loudnessTargetsLufs, choice);
}

void UltimateMeterAudioProcessorEditor::fillTargetItems(juce::PopupMenu& menu)
{
    using namespace Parameters;
    auto& apvts = audioProcessor.apvts;
    const int current = getChoice(ID::loudnessTarget);

    for (int choice = 0; choice < customTargetChoice; ++choice)
        menu.addItem(loudnessTargetNames[choice], true, current == choice, [&apvts, choice]
        {
            auto* parameter = apvts.getParameter(ID::loudnessTarget);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1((float)choice));
            parameter->endChangeGesture();
        });

    menu.addSeparator();
    const auto custom = juce::String(getValue(ID::loudnessCustomTarget), 1).replace("-", juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")));
    menu.addItem("Custom: " + custom + " LUFS...", true, current == customTargetChoice, [this] { askForCustomTarget(); });
}

void UltimateMeterAudioProcessorEditor::askForCustomTarget()
{
    using namespace Parameters;

    auto* window = new juce::AlertWindow("Custom loudness target", "The loudness to aim for, in LUFS (for example -8, or -9.5)", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("lufs", juce::String(getValue(ID::loudnessCustomTarget), 1), "LUFS");
    window->addButton("Set", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->setLookAndFeel(&lookAndFeel);

    window->enterModalState(true, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<UltimateMeterAudioProcessorEditor>(this), window](int result)
    {
        if (result != 1 || safe == nullptr)
            return;

        // A comma is taken for a decimal point, and whatever is not a number is ignored
        auto text = window->getTextEditorContents("lufs").replace(",", ".").retainCharacters("-0123456789.");
        if (text.isEmpty() || !text.containsAnyOf("0123456789"))
            return;

        float value = text.getFloatValue();
        if (value > 0.f)
            value = -value; // 8 means -8
        value = juce::jlimit(minCustomTarget, maxCustomTarget, std::round(value * 10.f) / 10.f);

        auto& apvts = safe->audioProcessor.apvts;
        auto* custom = apvts.getParameter(ID::loudnessCustomTarget);
        custom->beginChangeGesture();
        custom->setValueNotifyingHost(custom->convertTo0to1(value));
        custom->endChangeGesture();

        auto* choice = apvts.getParameter(ID::loudnessTarget);
        choice->beginChangeGesture();
        choice->setValueNotifyingHost(choice->convertTo0to1((float)customTargetChoice));
        choice->endChangeGesture();
    }), true);
}

//==============================================================================
// Presets travel as files: one can be taken out of the plugin to keep or to send, and one that comes from someone else
// is read in, applied, and kept with the others
void UltimateMeterAudioProcessorEditor::importPreset()
{
    presetChooser = std::make_unique<juce::FileChooser>("Choose a preset to import", juce::File(), "*.umpreset;*.xml");

    presetChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<UltimateMeterAudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe == nullptr || !file.existsAsFile())
                return;

            if (!Presets::load(safe->audioProcessor.apvts, file))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Import a preset",
                                                       "That file is not a preset of ULTIMATE METER.");
                return;
            }

            // Kept with the others, under the name of the file
            const auto target = Presets::fileOf(file.getFileNameWithoutExtension());
            target.getParentDirectory().createDirectory();
            file.copyFileTo(target);

            safe->readLayoutFromState();
            safe->mainViewAttachment.sendInitialUpdate();
            safe->refreshViews();
        });
}

void UltimateMeterAudioProcessorEditor::exportPreset(const juce::File& source)
{
    const auto suggested = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(source.getFileNameWithoutExtension() + ".umpreset");
    presetChooser = std::make_unique<juce::FileChooser>("Export the preset", suggested, "*.umpreset");

    presetChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [source](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;

            file = file.withFileExtension("umpreset");
            source.copyFileTo(file);
        });
}

void UltimateMeterAudioProcessorEditor::exportCurrentSettings()
{
    captureSizes();
    saveMultiState();

    const auto suggested = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ULTIMATE METER settings.umpreset");
    presetChooser = std::make_unique<juce::FileChooser>("Export the current settings", suggested, "*.umpreset");

    presetChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<UltimateMeterAudioProcessorEditor>(this)](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (safe == nullptr || file == juce::File())
                return;

            Presets::save(safe->audioProcessor.apvts.state, file.withFileExtension("umpreset"));
        });
}

//==============================================================================
// Changes the colours of the whole interface. The colours are shared by every editor of the plugin that is open,
// so an editor takes its own scheme back when the mouse comes to it.
void UltimateMeterAudioProcessorEditor::applyTheme(int index)
{
    appliedTheme = index;
    Theme::apply(index);
    lookAndFeel.refreshColours();
    repaint();
}

//==============================================================================
// Every option of the VU meter, behind the badge of the advanced options
void UltimateMeterAudioProcessorEditor::buildVuMenu(juce::PopupMenu& menu)
{
    using namespace Parameters;
    auto& apvts = audioProcessor.apvts;

    auto setParameter = [&apvts](const juce::String& id, float actual)
    {
        if (auto* parameter = apvts.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(actual));
            parameter->endChangeGesture();
        }
    };

    auto addChoice = [&](const juce::String& title, const juce::String& id)
    {
        juce::PopupMenu subMenu;
        addChoiceItems(subMenu, *apvts.getParameter(id));
        menu.addSubMenu(title, subMenu);
    };

    auto addValues = [&](const juce::String& title, const juce::String& id, std::vector<std::pair<juce::String, float>> options)
    {
        juce::PopupMenu subMenu;
        const float current = getValue(id);
        for (const auto& option : options)
            subMenu.addItem(option.first, true, std::abs(current - option.second) < 0.051f, [setParameter, id, value = option.second] { setParameter(id, value); });
        menu.addSubMenu(title, subMenu);
    };

    auto addSwitch = [&](const juce::String& title, const juce::String& id)
    {
        menu.addItem(title, true, isOn(id), [&apvts, id]
        {
            auto* parameter = apvts.getParameter(id);
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->getValue() > 0.5f ? 0.f : 1.f);
            parameter->endChangeGesture();
        });
    };

    menu.addSectionHeader("Meter");
    addChoice("Type", ID::vuMode);
    addChoice("Display", ID::vuDisplay);
    addChoice("Zero is at", ID::vuCalibration);

    menu.addSectionHeader("VU");
    addChoice("VU detector", ID::vuBallistics);
    addValues("Overshoot", ID::vuOvershoot, { { "None (0.3 %)", 0.3f }, { "1.5 %", 1.5f }, { "3 %", 3.f }, { "5 %", 5.f }, { "10 %", 10.f }, { "15 %", 15.f } });
    addValues("Rise and fall time", ID::vuSpeed, { { "Slow (50 %)", 0.5f }, { "75 %", 0.75f }, { "Standard (100 %)", 1.f }, { "150 %", 1.5f }, { "Fast (200 %)", 2.f } });

    menu.addSectionHeader("RMS and K");
    addValues("RMS window", ID::vuRmsWindow, { { "50 ms", 50.f }, { "100 ms", 100.f }, { "300 ms", 300.f }, { "600 ms", 600.f }, { "1000 ms", 1000.f } });
    addSwitch("+3 dB (AES-17)", ID::vuAes17);
    addChoice("Weighting", ID::vuWeighting);

    menu.addSectionHeader("Display");
    addSwitch("Hold needle", ID::vuHold);
    addSwitch("Numbers", ID::vuNumbers);
    addValues("Clip lamp lights from", ID::vuClipLevel, { { "-12 dBFS", -12.f }, { "-6 dBFS", -6.f }, { "-3 dBFS", -3.f }, { "-1 dBFS", -1.f }, { "-0.5 dBFS", -0.5f } });

    menu.addSectionHeader("Trim of the reading");
    addValues("Left", ID::vuTrimL, { { "-6 dB", -6.f }, { "-3 dB", -3.f }, { "0 dB", 0.f }, { "+3 dB", 3.f }, { "+6 dB", 6.f } });
    addValues("Right", ID::vuTrimR, { { "-6 dB", -6.f }, { "-3 dB", -3.f }, { "0 dB", 0.f }, { "+3 dB", 3.f }, { "+6 dB", 6.f } });
}
