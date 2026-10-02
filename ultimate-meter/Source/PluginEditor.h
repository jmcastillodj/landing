/*
  ==============================================================================

    The editor of the plugin: a header with a tab for each view, the view itself,
    a column of meters that is always showing, and a bar of controls for the view.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/LookAndFeel.h"
#include "UI/Controls.h"
#include "UI/ControlBar.h"
#include "Views/SpectrumSource.h"
#include "Views/GoniometerView.h"
#include "Views/SpectrumView.h"
#include "Views/SpectrogramView.h"
#include "Views/HistoryView.h"
#include "Views/WaveformView.h"
#include "Views/TonalBalanceView.h"
#include "Views/LoudnessView.h"
#include "Views/LoudnessRadarView.h"
#include "Views/LevelMeters.h"
#include "Views/LoudnessSummary.h"
#include "Views/Timeline.h"
#include "UI/MultiLayout.h"

//==============================================================================
class UltimateMeterAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    UltimateMeterAudioProcessorEditor (UltimateMeterAudioProcessor&);
    ~UltimateMeterAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int defaultWidth = 1100, defaultHeight = 580;
    static constexpr int minWidth = 860, minHeight = 480;
    static constexpr int maxWidth = 2600, maxHeight = 1600;

    // The readings count as silence when no audio has arrived for this long, which is
    // what happens when the host stops calling the processor
    static constexpr double silenceTimeoutSeconds = 0.25;

    // Called before every frame that the display presents, with the time of that frame in seconds.
    // It updates the meters at the refresh rate that the user has chosen, and skips the frames in
    // between. Updates that are in step with the display were measured to cost less than a timer at
    // the same rate. The meters move by the time that has passed, so they behave the same at any rate.
    void vBlank(double timestampSeconds);

    // Reads the measurements made on the audio thread and updates every meter
    void updateMeters(float elapsedSeconds);

    // Shows the view that the main view parameter selects
    void showMainView(int viewId);

    // Several views on screen at once, in rows. Every view is given a row, or none, and the views of a row stand
    // side by side. Between each two views of a row, and between each two rows, there is a divider to drag, so
    // every view can be given any size. The assignment of views to rows is kept in the session; the sizes are
    // what the window gives them when it opens.
    juce::Component* componentOfView(int viewId);
    int multiMask() const;
    void setMultiMode(bool enabled);
    void toggleViewInMulti(int viewId);
    void setViewRow(int viewId, int row);
    void applyLayoutPreset(int preset);
    void showLayoutMenu();
    void closeUpRows();
    void refreshViews();
    void rebuildMultiLayout();
    void layoutViews();
    void saveMultiState();

    // The options of a view, which are in a menu: on the secondary click of the view, and under the Options button
    void buildViewMenu(juce::PopupMenu& menu, int viewId);
    void mouseDown(const juce::MouseEvent&) override;
    void captureSizes();

    // Draws the header and the bottom bar, whose raised parts meet the recessed ones in S-shaped shoulders
    void paintHeader(juce::Graphics& g, juce::Rectangle<int> area);
    void paintName(juce::Graphics& g, juce::Rectangle<float> tab);
    void paintBottomBar(juce::Graphics& g, juce::Rectangle<int> area);

    // Fills the menu of the settings that the meters of the side column share
    void buildMeterSettingsMenu(juce::PopupMenu& menu);

    // The current value of a parameter: as it is, as the index of a choice, or as a switch
    float getValue(const juce::String& parameterID) const;
    int getChoice(const juce::String& parameterID) const;
    bool isOn(const juce::String& parameterID) const;

    // This reference is provided as a quick way for your editor to access the processor object that created it
    UltimateMeterAudioProcessor& audioProcessor;

    UltimateMeterLookAndFeel lookAndFeel;
    TabBar tabs;
    TextButtonQuiet multiButton { "Multi" };
    TextButtonQuiet arrangeButton { "Layout" };


    // The views, of which one is showing. The spectrum and the spectrogram draw the same spectra.
    SpectrumSource spectrumSource;
    GoniometerView goniometerView;
    SpectrumView spectrumView;
    SpectrogramView spectrogramView;
    HistoryView historyView;
    WaveformView waveformView;
    TonalBalanceView balanceView;
    LoudnessView loudnessView;
    LoudnessRadarView radarView;

    // The side column, which is always showing
    LevelMeters levelMeters;
    LoudnessSummary loudnessSummary;
    CorrelationBar correlationBar;

    // The bottom bar
    SettingsButton meterSettingsButton { "Meters" };
    SettingsButton optionsButton { "Options" };

    // Starts every measurement again. It is in the bar with every view, because the readings
    // that it clears are in the side column with every view.
    TextButtonQuiet resetButton { "Reset" };
    void resetMeasurements();
    bool spectrumFrozen = false; // freezing is for a moment's look, so it is not a setting that is saved

    juce::ParameterAttachment mainViewAttachment;

    // Whether the constructor has finished, before which a change of size is not the user's
    bool isConstructed = false;

    static constexpr int maxRows = 4;
    bool multiEnabled = false;
    float heldPeakDb = -200.f; // the highest sample peak since the last reset, for the RMS readout
    std::array<int, 8> viewRow { -1, -1, -1, -1, -1, -1, -1, -1 }; // by view, the row that it is in, or -1 for a view that is not showing
    int currentMainView = 0;
    juce::Rectangle<int> viewArea;
    juce::StretchableLayoutManager rowLayout;
    std::vector<std::unique_ptr<MultiRow>> multiRows;
    std::vector<std::unique_ptr<juce::StretchableLayoutResizerBar>> rowDividers;
    std::vector<juce::Component*> rowItems;
    int multiLayoutKey = -1;     // what the layout was last built for
    std::vector<int> multiRowNumbers; // the row of each of multiRows

    // The share of the height that each row has, and of its row's width that each view has (0 for none yet).
    // They are read from the layout after every change, so that adding a view or taking one away does
    // not undo the sizes that were dragged, and they are kept in the session.
    std::array<double, maxRows> rowWeight {};
    std::array<double, 8> viewWeight {};

    // Set by the reset button and the reset item of the menu, and cleared by the next update
    bool resetTicksRequested = false;

    // The clock of the timeline that the spectrogram, the history and the loudness share
    Timeline::Clock timelineClock;

    // Timing of the updates, in seconds
    double lastUpdateTime = -1.0;
    double lastAudioTime = 0.0;

    // The numbers take new readings ten times a second, and all at the same moment, so that a
    // reading that is shown in two places says the same in both
    double secondsSinceReadout = 0.0;
    juce::uint64 lastTotalWritten = 0;

    // Declared last, so that the callbacks stop before anything that they use is destroyed
    juce::VBlankAttachment vBlankAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UltimateMeterAudioProcessorEditor)
};
