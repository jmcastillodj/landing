#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Parameters.h"

//==============================================================================
// The monitor section, which is always at hand in the side column. It changes what is heard and leaves what the meters
// measure as it is: the channels as they are (LR), swapped (RL), only the left, the right, the mid or the side, with
// a mute and a polarity switch for each channel.
class MonitorStrip : public juce::Component
{
public:
    explicit MonitorStrip(juce::AudioProcessorValueTreeState& a) : apvts(a)
    {
        setOpaque(true);
        setRepaintsOnMouseActivity(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Theme::displayBottom);

        const int mode = juce::roundToInt(apvts.getRawParameterValue(Parameters::ID::monMode)->load());

        g.setFont(Theme::labelFont());
        g.setColour(mode == 0 ? Theme::textDim : Theme::warn);
        g.drawText(mode == 0 ? "MONITOR" : "MONITOR - NOT STEREO", getLocalBounds().reduced(10, 2).removeFromTop(14), juce::Justification::centredLeft);

        for (int i = 0; i < (int)areas.size(); ++i)
        {
            const bool lit = i < 6 ? mode == i : apvts.getRawParameterValue(boolIds[(size_t)i - 6])->load() > 0.5f;
            const bool hover = areas[(size_t)i].contains(getMouseXYRelative());

            g.setColour(lit ? Theme::accentDeep : Theme::track);
            g.fillRoundedRectangle(areas[(size_t)i].toFloat(), 3.f);
            g.setColour(lit ? Theme::accent : hover ? Theme::textDim : Theme::panelEdge);
            g.drawRoundedRectangle(areas[(size_t)i].toFloat().reduced(0.5f), 3.f, 1.f);

            g.setFont(Theme::font(11.5f, true));
            g.setColour(lit ? juce::Colours::white : Theme::text);
            g.drawText(labels[(size_t)i], areas[(size_t)i], juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8, 3);
        area.removeFromTop(16);

        auto top = area.removeFromTop(area.getHeight() / 2).withTrimmedBottom(3);
        auto bottom = area.withTrimmedTop(1);

        const int topWidth = top.getWidth() / 6;
        for (int i = 0; i < 6; ++i)
            areas[(size_t)i] = top.removeFromLeft(topWidth).reduced(1, 0);

        const int bottomWidth = bottom.getWidth() / 4;
        for (int i = 6; i < 10; ++i)
            areas[(size_t)i] = bottom.removeFromLeft(bottomWidth).reduced(1, 0);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int)areas.size(); ++i)
            if (areas[(size_t)i].contains(e.getPosition()))
            {
                const auto id = i < 6 ? Parameters::ID::monMode : boolIds[(size_t)i - 6];
                if (auto* parameter = apvts.getParameter(id))
                {
                    parameter->beginChangeGesture();
                    if (i < 6)
                        parameter->setValueNotifyingHost(parameter->convertTo0to1((float)i));
                    else
                        parameter->setValueNotifyingHost(parameter->getValue() > 0.5f ? 0.f : 1.f);
                    parameter->endChangeGesture();
                }
                repaint();
                return;
            }
    }

    void mouseMove(const juce::MouseEvent&) override { repaint(); }

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::array<juce::Rectangle<int>, 10> areas;

    const std::array<juce::String, 10> labels { "LR", "RL", "L", "R", "M", "S", "MUTE L", "MUTE R", juce::String(juce::CharPointer_UTF8("\xc3\x98 L")), juce::String(juce::CharPointer_UTF8("\xc3\x98 R")) };
    const std::array<juce::String, 4> boolIds { Parameters::ID::monMuteL, Parameters::ID::monMuteR, Parameters::ID::monPolL, Parameters::ID::monPolR };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitorStrip)
};
