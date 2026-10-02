#include "LoudnessSummary.h"

//==============================================================================
void LoudnessSummary::update(const LoudnessMeter::Readings& readings, float maxTruePeakDb, float targetLufs, bool readoutDue)
{
    if (!readoutDue)
        return;

    const bool hasIntegrated = std::isfinite(readings.integrated);
    const auto newIntegrated = Theme::formatDb(readings.integrated, -200.f);
    const auto newShortTerm = Theme::formatDb(readings.shortTerm, -200.f);
    const auto newRange = std::isfinite(readings.rangeLow) ? juce::String(readings.range, 1) : Theme::formatDb(-300.f);
    const auto newTruePeak = Theme::formatDb(maxTruePeakDb, -150.f);
    const bool newIsOver = maxTruePeakDb > truePeakLimitDb;

    juce::String newDifference;
    if (hasIntegrated && targetLufs < 0.f)
        newDifference = Theme::formatDb(readings.integrated - targetLufs, -200.f) + " LU to " + juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) + juce::String(juce::roundToInt(std::abs(targetLufs))) + " LUFS";

    if (newIntegrated != integrated || newDifference != difference || newShortTerm != shortTerm
        || newRange != range || newTruePeak != truePeak || newIsOver != truePeakIsOver)
    {
        integrated = newIntegrated;
        difference = newDifference;
        shortTerm = newShortTerm;
        range = newRange;
        truePeak = newTruePeak;
        truePeakIsOver = newIsOver;
        repaint();
    }
}

void LoudnessSummary::setMode(Mode newMode)
{
    if (newMode != mode)
    {
        mode = newMode;
        repaint();
    }
}

void LoudnessSummary::updateRms(float leftDb, float rightDb, float peakHoldDb, bool readoutDue)
{
    if (!readoutDue)
        return;

    const auto newLoudest = Theme::formatDb(juce::jmax(leftDb, rightDb), -100.f);
    const auto newLeft = Theme::formatDb(leftDb, -100.f);
    const auto newRight = Theme::formatDb(rightDb, -100.f);
    const auto newPeak = Theme::formatDb(peakHoldDb, -100.f);
    const bool newOver = peakHoldDb >= 0.f;

    if (newLoudest != rmsLoudest || newLeft != rmsLeft || newRight != rmsRight || newPeak != peakHold || newOver != peakHoldIsOver)
    {
        rmsLoudest = newLoudest;
        rmsLeft = newLeft;
        rmsRight = newRight;
        peakHold = newPeak;
        peakHoldIsOver = newOver;

        if (mode == Mode::rms)
            repaint();
    }
}

void LoudnessSummary::paint(juce::Graphics& g)
{
    g.fillAll(Theme::displayBottom);

    auto bounds = getLocalBounds().reduced(14, 8);

    if (mode == Mode::rms)
    {
        // The RMS levels: the louder channel large, the two channels and the highest peak in a table
        auto top = bounds.removeFromTop(46);
        titleArea = top.removeFromTop(13);
        g.setFont(Theme::labelFont());
        g.setColour(titleHovered ? juce::Colours::white : Theme::textDim);
        g.drawText("RMS", titleArea, juce::Justification::centredLeft);
        g.setColour(Theme::textFaint);
        g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x87\x84")) + " LOUDNESS", titleArea, juce::Justification::centredRight);

        auto numberRow = top;
        g.setFont(Theme::font(30.f));
        g.setColour(Theme::accent);
        const int width = Theme::textWidth(Theme::font(30.f), rmsLoudest) + 6;
        g.drawText(rmsLoudest, numberRow.removeFromLeft(width), juce::Justification::centredLeft);
        g.setFont(Theme::labelFont());
        g.setColour(Theme::textDim);
        g.drawText("dBFS", numberRow.removeFromTop(numberRow.getHeight() / 2 + 4), juce::Justification::bottomLeft);

        struct RmsRow { const char* name; const juce::String& value; const char* unit; bool warn; };
        const RmsRow rmsRows[] {
            { "RMS L", rmsLeft, "dBFS", false },
            { "RMS R", rmsRight, "dBFS", false },
            { "PEAK", peakHold, "dBFS", peakHoldIsOver },
        };

        bounds.removeFromTop(4);
        const int height = bounds.getHeight() / (int)std::size(rmsRows);

        for (auto& row : rmsRows)
        {
            auto line = bounds.removeFromTop(height);
            const bool isPeak = &row == &rmsRows[2];
            if (isPeak)
                truePeakRow = line;

            g.setFont(Theme::labelFont());
            g.setColour(isPeak && truePeakHovered ? juce::Colours::white : Theme::textDim);
            g.drawText(isPeak && truePeakHovered ? "RESET" : row.name, line.removeFromLeft(74), juce::Justification::centredLeft);
            g.setColour(Theme::textFaint);
            g.drawText(row.unit, line.removeFromRight(32), juce::Justification::centredLeft);

            g.setFont(Theme::font(14.f));
            g.setColour(row.warn ? Theme::over : Theme::text);
            g.drawText(row.value, line.withTrimmedRight(6), juce::Justification::centredRight);
        }

        return;
    }

    // The integrated loudness is the reading that a delivery is judged by, so it is the largest
    auto top = bounds.removeFromTop(46);
    titleArea = top.removeFromTop(13);
    g.setFont(Theme::labelFont());
    g.setColour(titleHovered ? juce::Colours::white : Theme::textDim);
    g.drawText("INTEGRATED", titleArea, juce::Justification::centredLeft);
    g.setColour(Theme::textFaint);
    g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x87\x84")) + " RMS", titleArea, juce::Justification::centredRight);

    auto numberRow = top;
    g.setFont(Theme::font(30.f));
    g.setColour(Theme::accent);
    const int numberWidth = Theme::textWidth(Theme::font(30.f), integrated) + 6;
    g.drawText(integrated, numberRow.removeFromLeft(numberWidth), juce::Justification::centredLeft);

    g.setFont(Theme::labelFont());
    g.setColour(Theme::textDim);
    g.drawText("LUFS", numberRow.removeFromTop(numberRow.getHeight() / 2 + 4), juce::Justification::bottomLeft);
    g.setFont(Theme::font(11.f));
    g.setColour(targetHovered ? juce::Colours::white : Theme::text);
    targetArea = numberRow.expanded(40, 0).withX(numberRow.getX());
    g.drawText(difference.isEmpty() ? juce::String("no target (click)") : difference, targetArea, juce::Justification::centredLeft);

    // The other readings share a small table
    struct Row { const char* name; const juce::String& value; const char* unit; bool warn; };
    const Row rows[] {
        { "SHORT TERM", shortTerm, "LUFS", false },
        { "RANGE", range, "LU", false },
        { "TRUE PEAK", truePeak, "dBTP", truePeakIsOver },
    };

    bounds.removeFromTop(4);
    const int rowHeight = bounds.getHeight() / (int)std::size(rows);

    for (auto& row : rows)
    {
        auto line = bounds.removeFromTop(rowHeight);
        const bool isTruePeak = &row == &rows[2];
        if (isTruePeak)
            truePeakRow = line;

        g.setFont(Theme::labelFont());
        g.setColour(isTruePeak && truePeakHovered ? juce::Colours::white : Theme::textDim);
        g.drawText(isTruePeak && truePeakHovered ? "RESET" : row.name, line.removeFromLeft(74), juce::Justification::centredLeft);
        g.setColour(Theme::textFaint);
        g.drawText(row.unit, line.removeFromRight(32), juce::Justification::centredLeft);

        g.setFont(Theme::font(14.f));
        g.setColour(row.warn ? Theme::over : Theme::text);
        g.drawText(row.value, line.withTrimmedRight(6), juce::Justification::centredRight);
    }
}

void LoudnessSummary::mouseDown(const juce::MouseEvent& e)
{
    if (titleArea.contains(e.getPosition()) && onTitleClicked)
        onTitleClicked();
    else if (mode == Mode::loudness && targetArea.contains(e.getPosition()) && onTargetClicked)
        onTargetClicked();
    else if (truePeakRow.contains(e.getPosition()) && onTruePeakClicked)
        onTruePeakClicked();
}

void LoudnessSummary::mouseMove(const juce::MouseEvent& e)
{
    const bool over = truePeakRow.contains(e.getPosition());
    const bool overTitle = titleArea.contains(e.getPosition());
    const bool overTarget = mode == Mode::loudness && targetArea.contains(e.getPosition());
    if (over != truePeakHovered || overTitle != titleHovered || overTarget != targetHovered)
    {
        truePeakHovered = over;
        titleHovered = overTitle;
        targetHovered = overTarget;
        setMouseCursor(over || overTitle || overTarget ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void LoudnessSummary::mouseExit(const juce::MouseEvent&)
{
    if (truePeakHovered || titleHovered || targetHovered)
    {
        truePeakHovered = false;
        titleHovered = false;
        targetHovered = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

//==============================================================================
