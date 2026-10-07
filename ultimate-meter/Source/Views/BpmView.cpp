#include "BpmView.h"

BpmView::BpmView(BpmDetector& d) : detector(d)
{
    setOpaque(true);
}

void BpmView::update(double newHostBpm)
{
    hostBpm = newHostBpm;
    repaint();
}

void BpmView::resized()
{
    auto area = getLocalBounds().reduced(14, 10);
    area.removeFromTop(topInset);

    footerArea = area.removeFromBottom(18);
    area.removeFromBottom(6);

    // The row of buttons, kept at a size that fits the buttons whatever the size of the view
    auto row = area.removeFromBottom(30);
    area.removeFromBottom(8);
    confidenceArea = area.removeFromBottom(24);
    area.removeFromBottom(8);
    lcdArea = area;

    const int gap = 8;
    const int width = juce::jlimit(44, 78, (row.getWidth() - 4 * gap) / (int)numButtons);
    const int total = width * (int)numButtons + gap * ((int)numButtons - 1);
    auto strip = row.withSizeKeepingCentre(total, row.getHeight());
    for (int i = 0; i < (int)numButtons; ++i)
    {
        buttons[(size_t)i] = strip.removeFromLeft(width);
        strip.removeFromLeft(gap);
    }
}

void BpmView::press(int button)
{
    switch (button)
    {
        case halfButton:   detector.multiplyTempo(-1); break;
        case doubleButton: detector.multiplyTempo(+1); break;
        case tapButton:    detector.tap(); break;
        case holdButton:   detector.setHeld(!detector.isHeld()); break;
        case resetButton:  detector.reset(); break;
        default: break;
    }
    repaint();
}

void BpmView::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    for (int i = 0; i < (int)numButtons; ++i)
        if (buttons[(size_t)i].contains(e.getPosition()))
            press(i);
}

void BpmView::mouseMove(const juce::MouseEvent& e)
{
    int now = -1;
    for (int i = 0; i < (int)numButtons; ++i)
        if (buttons[(size_t)i].contains(e.getPosition()))
            now = i;

    if (now != hovered)
    {
        hovered = now;
        setMouseCursor(now >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BpmView::mouseExit(const juce::MouseEvent&)
{
    if (hovered != -1)
    {
        hovered = -1;
        repaint();
    }
}

void BpmView::drawButton(juce::Graphics& g, int button, const juce::String& text, bool lit)
{
    auto r = buttons[(size_t)button].toFloat();
    g.setGradientFill(juce::ColourGradient(lit ? Theme::accentDeep.brighter(0.25f) : Theme::chromeTop, 0.f, r.getY(),
                                           lit ? Theme::accentDeep : Theme::chromeBottom, 0.f, r.getBottom(), false));
    g.fillRoundedRectangle(r, 4.f);
    if (hovered == button)
    {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(r, 4.f);
    }
    g.setColour(lit ? Theme::accent : Theme::edge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.f, 1.f);
    g.setColour(lit ? Theme::text : Theme::knobLabel);
    g.setFont(Theme::font(12.f, true));
    g.drawText(text, buttons[(size_t)button], juce::Justification::centred);
}

void BpmView::paint(juce::Graphics& g)
{
    g.fillAll(Theme::window);

    const bool has = detector.hasResult();
    const float confidence = has ? detector.getConfidence() : 0.f;
    const double seconds = detector.getSecondsAnalysed();

    // The display
    auto lcd = lcdArea.toFloat();
    g.setGradientFill(juce::ColourGradient(Theme::displayTop, 0.f, lcd.getY(), Theme::displayBottom, 0.f, lcd.getBottom(), false));
    g.fillRect(lcd);

    // The onsets of the last four seconds, running behind the number
    if (lcdArea.getWidth() > 10)
    {
        const int cols = lcdArea.getWidth();
        const int count = juce::jmax(1, (int)(4.0 * detector.getEnvelopeRate()));
        envelope.assign((size_t)count, 0.f);
        detector.copyRecentEnvelope(envelope.data(), count);
        columns.assign((size_t)cols, 0.f);

        float peak = 0.f;
        for (int i = 0; i < count; ++i)
        {
            auto& column = columns[(size_t)((juce::int64)i * cols / count)];
            column = juce::jmax(column, envelope[(size_t)i]);
            peak = juce::jmax(peak, envelope[(size_t)i]);
        }
        envelopeMax = juce::jmax(peak, envelopeMax * 0.985f);

        const float scale = 1.f / juce::jmax(envelopeMax, 0.05f);
        const float base = lcd.getBottom() - 4.f;
        g.setColour(Theme::accent.withAlpha(0.22f));
        for (int x = 0; x < cols; ++x)
        {
            const float h = juce::jmin(1.f, columns[(size_t)x] * scale) * lcd.getHeight() * 0.55f;
            if (h > 0.5f)
                g.fillRect(lcd.getX() + (float)x, base - h, 1.f, h);
        }
    }

    g.setColour(Theme::edge);
    g.drawRect(lcd, 1.f);

    // The number
    const juce::String digits = has ? juce::String(detector.getDisplayBpm(), 1) : "---.-";
    const auto colour = has ? Theme::accent.interpolatedWith(juce::Colours::white, 0.55f).withAlpha(0.55f + 0.45f * confidence)
                            : Theme::textFaint;
    auto text = lcdArea.reduced(10, 0).withTrimmedBottom(16);
    const float fontHeight = (float)juce::jlimit(30, 160, juce::jmin(text.getHeight(), text.getWidth() / 3));
    g.setFont(Theme::font(fontHeight));
    g.setColour(colour.withAlpha(0.18f));
    g.drawText(digits, text.expanded(2), juce::Justification::centred);
    g.setColour(colour);
    g.drawText(digits, text, juce::Justification::centred);

    g.setFont(Theme::font(11.f, true));
    g.setColour(Theme::textDim);
    g.drawText("BPM", lcdArea.reduced(10, 6), juce::Justification::bottomRight);

    // What the display is showing
    juce::String status;
    juce::Colour led = Theme::textFaint;
    if (detector.isTapActive())        { status = "TAP";                led = Theme::accent; }
    else if (detector.isHeld())        { status = "HOLD";               led = Theme::warn; }
    else if (has)                      { status = "LOCKED";             led = Theme::good; }
    else if (seconds > 0.5)            { status = "ANALYZING";          led = Theme::accent; }
    else                               { status = "WAITING FOR AUDIO"; }

    auto statusArea = lcdArea.reduced(10, 8).removeFromTop(16);
    g.setColour(led);
    g.fillEllipse((float)statusArea.getX(), (float)statusArea.getY() + 3.f, 9.f, 9.f);
    g.setColour(Theme::textDim);
    g.drawText(status, statusArea.withTrimmedLeft(16), juce::Justification::centredLeft);

    const int shift = detector.getOctaveShift();
    if (has && shift != 0)
    {
        const auto sign = shift > 0 ? juce::String(juce::CharPointer_UTF8("\xc3\x97")) + juce::String(1 << shift)
                                    : juce::String(juce::CharPointer_UTF8("\xc3\xb7")) + juce::String(1 << -shift);
        g.drawText("DETECTED " + juce::String(detector.getDetectedBpm(), 1) + "   " + sign,
                   lcdArea.reduced(10, 6), juce::Justification::bottomLeft);
    }

    // The confidence
    {
        g.setColour(Theme::textDim);
        g.setFont(Theme::font(10.f, true));
        g.drawText("CONFIDENCE", confidenceArea.withHeight(12), juce::Justification::centredLeft);

        auto bar = confidenceArea.toFloat().withTrimmedTop(15.f).withHeight(8.f);
        const int segments = 24;
        const float segmentWidth = (bar.getWidth() - (float)(segments - 1) * 2.f) / (float)segments;
        for (int i = 0; i < segments; ++i)
        {
            const bool lit = (float)(i + 1) / (float)segments <= confidence + 0.001f;
            g.setColour(lit ? Theme::meterLight : Theme::track);
            g.fillRect(bar.getX() + (float)i * (segmentWidth + 2.f), bar.getY(), segmentWidth, bar.getHeight());
        }
    }

    // The buttons
    drawButton(g, halfButton, juce::String(juce::CharPointer_UTF8("\xc3\xb7")) + "2", false);
    drawButton(g, doubleButton, juce::String(juce::CharPointer_UTF8("\xc3\x97")) + "2", false);
    drawButton(g, tapButton, "TAP", detector.isTapActive());
    drawButton(g, holdButton, "HOLD", detector.isHeld());
    drawButton(g, resetButton, "RESET", false);

    // The foot
    g.setFont(Theme::font(11.f));
    g.setColour(Theme::textDim);
    g.drawText(has ? "Analysed: " + juce::String(juce::roundToInt((float)juce::jmin(seconds, BpmAnalyzer::maxSeconds))) + " s"
                   : "Play the track to measure its tempo",
               footerArea, juce::Justification::centredLeft);
    if (hostBpm > 0.0)
        g.drawText("Host: " + juce::String(hostBpm, 1) + " BPM", footerArea, juce::Justification::centredRight);
}
