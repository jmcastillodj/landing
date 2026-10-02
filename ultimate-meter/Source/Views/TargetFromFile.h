#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Engine/TonalTargets.h"

//==============================================================================
// A window for making a target of one's own from a recording. It shows the waveform of the file, and the part
// that is to be measured is chosen by dragging across it (the whole file if nothing is chosen). The part is
// analysed when OK is pressed, and the result is handed to the callback.
class TargetFromFile : public juce::Component, private juce::ChangeListener
{
public:
    TargetFromFile(const juce::File& file, std::function<void(TonalTargets::Target)> onDone, std::function<void()> onClose) :
        thumbnail(512, formats, cache), done(std::move(onDone)), close(std::move(onClose))
    {
        formats.registerBasicFormats();
        reader.reset(formats.createReaderFor(file));
        thumbnail.setSource(new juce::FileInputSource(file));
        thumbnail.addChangeListener(this);

        length = reader != nullptr ? (double)reader->lengthInSamples / reader->sampleRate : 0.0;

        nameEditor.setText(file.getFileNameWithoutExtension(), false);
        nameEditor.setFont(Theme::controlFont());
        nameEditor.setColour(juce::TextEditor::backgroundColourId, Theme::track);
        nameEditor.setColour(juce::TextEditor::textColourId, Theme::text);
        nameEditor.setColour(juce::TextEditor::outlineColourId, Theme::panelEdge);
        addAndMakeVisible(nameEditor);

        for (auto* button : { &okButton, &wholeButton, &cancelButton })
            addAndMakeVisible(*button);

        okButton.onClick = [this] { finish(true); };
        wholeButton.onClick = [this] { selectionStart = selectionEnd = 0.0; repaint(); };
        cancelButton.onClick = [this] { if (close) close(); };

        setSize(720, 300);
    }

    ~TargetFromFile() override { thumbnail.removeChangeListener(this); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::panel);

        g.setFont(Theme::labelFont());
        g.setColour(Theme::textDim);
        g.drawText("TARGET FROM A RECORDING", getLocalBounds().reduced(16, 10).removeFromTop(16), juce::Justification::centredLeft);

        g.setFont(Theme::font(12.f));
        g.setColour(Theme::textDim);
        g.drawText("Drag across the waveform to choose the part that the target is measured from, or measure the whole file.",
                   getLocalBounds().reduced(16, 0).withTop(30).withHeight(18), juce::Justification::centredLeft);

        g.setColour(Theme::display);
        g.fillRect(waveArea);

        if (length > 0.0)
        {
            g.setColour(Theme::accent.withAlpha(0.9f));
            thumbnail.drawChannels(g, waveArea.reduced(0, 4), 0.0, length, 1.f);

            const bool hasSelection = selectionEnd > selectionStart;
            if (hasSelection)
            {
                auto selection = waveArea.withLeft(xOf(selectionStart)).withRight(xOf(selectionEnd));
                g.setColour(Theme::second.withAlpha(0.22f));
                g.fillRect(selection);
                g.setColour(Theme::second);
                g.drawRect(selection, 1);
            }

            g.setFont(Theme::font(11.f));
            g.setColour(Theme::text);
            const juce::String dash(juce::CharPointer_UTF8(" \xe2\x80\x93 "));
            const juce::String text = hasSelection ? "Selection: " + formatTime(selectionStart) + dash + formatTime(selectionEnd)
                                                   : "Whole file: " + formatTime(length);
            g.drawText(text, waveArea.getX(), waveArea.getBottom() + 4, 400, 16, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour(Theme::over);
            g.drawText("This file could not be read.", waveArea, juce::Justification::centred);
        }

        g.setColour(Theme::textDim);
        g.setFont(Theme::labelFont());
        g.drawText("NAME", nameEditor.getBounds().withX(16).withWidth(50), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(16);
        bounds.removeFromTop(40);
        auto buttons = bounds.removeFromBottom(30);
        bounds.removeFromBottom(8);
        auto nameRow = bounds.removeFromBottom(26);
        bounds.removeFromBottom(24);
        waveArea = bounds;

        nameEditor.setBounds(nameRow.withTrimmedLeft(54));
        cancelButton.setBounds(buttons.removeFromRight(90));
        buttons.removeFromRight(8);
        okButton.setBounds(buttons.removeFromRight(150));
        buttons.removeFromRight(8);
        wholeButton.setBounds(buttons.removeFromRight(130));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (waveArea.contains(e.getPosition()) && length > 0.0)
        {
            dragFrom = timeAt(e.x);
            selectionStart = selectionEnd = dragFrom;
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (length <= 0.0 || !waveArea.contains(e.getMouseDownPosition()))
            return;

        const double now = timeAt(e.x);
        selectionStart = juce::jmin(dragFrom, now);
        selectionEnd = juce::jmax(dragFrom, now);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        // A click without a drag, or a sliver, is no selection
        if (selectionEnd - selectionStart < 0.5)
            selectionStart = selectionEnd = 0.0;
        repaint();
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }

    double timeAt(int x) const
    {
        return juce::jlimit(0.0, length, length * (double)(x - waveArea.getX()) / (double)juce::jmax(1, waveArea.getWidth()));
    }

    int xOf(double seconds) const { return waveArea.getX() + juce::roundToInt((double)waveArea.getWidth() * seconds / length); }

    static juce::String formatTime(double seconds)
    {
        return juce::String((int)seconds / 60) + ":" + juce::String((int)seconds % 60).paddedLeft('0', 2);
    }

    void finish(bool accepted)
    {
        if (!accepted || reader == nullptr)
        {
            if (close)
                close();
            return;
        }

        const double from = selectionEnd > selectionStart ? selectionStart : 0.0;
        const double to = selectionEnd > selectionStart ? selectionEnd : length;
        const auto start = (juce::int64)(from * reader->sampleRate);
        const auto numSamples = (int)std::min<juce::int64>((juce::int64)((to - from) * reader->sampleRate), reader->lengthInSamples - start);

        if (numSamples < 8192)
        {
            if (close)
                close();
            return;
        }

        juce::AudioBuffer<float> audio(2, numSamples);
        reader->read(&audio, 0, numSamples, start, true, true);
        if (reader->numChannels < 2)
            audio.copyFrom(1, 0, audio, 0, 0, numSamples);

        auto target = TonalTargets::analyse(nameEditor.getText().trim().isEmpty() ? "Custom" : nameEditor.getText().trim(), audio, reader->sampleRate);

        if (done)
            done(std::move(target));

        if (close)
            close();
    }

    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache cache { 2 };
    juce::AudioThumbnail thumbnail;
    std::unique_ptr<juce::AudioFormatReader> reader;
    double length = 0.0, selectionStart = 0.0, selectionEnd = 0.0, dragFrom = 0.0;
    juce::Rectangle<int> waveArea;

    juce::TextEditor nameEditor;
    juce::TextButton okButton { "Make target" }, wholeButton { "Use whole file" }, cancelButton { "Cancel" };

    std::function<void(TonalTargets::Target)> done;
    std::function<void()> close;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TargetFromFile)
};
