#pragma once

#include <JuceHeader.h>
#include "../UI/Theme.h"
#include "../Engine/LoudnessMeter.h"

//==============================================================================
// The loudness as a radar: a beam turns around the centre, and behind it the loudness of every moment is drawn
// outwards from the middle, so that a whole programme is seen at once as a shape. The rings are in LU from the
// target, and the colours change at the target. Around it runs a ring of lights that follows the loudness
// of the moment, with the range and the integrated loudness in the corners.
class LoudnessRadarView : public juce::Component
{
public:
    LoudnessRadarView();

    void paint(juce::Graphics& g) override;

    // Called by the editor once per frame
    //  - targetLufs:     the loudness to aim for, or 0 for none, in which case -23 LUFS stands in
    //  - turnSeconds:    the time that one turn takes
    //  - useShortTerm:   whether the shape follows the short-term loudness rather than the momentary
    //  - elapsedSeconds: the time since the last call, or 0 while there is no audio
    void update(const LoudnessMeter::Readings& readings, float targetLufs, float turnSeconds, bool useShortTerm,
                float elapsedSeconds, bool readoutDue);

    void clearHistory();

private:
    static constexpr int numSlots = 360;
    static constexpr float lowestLu = -36.f;
    static constexpr float highestLu = 12.f;
    static constexpr float silenceLevel = -1000.f;

    float referenceLufs() const { return juce::exactlyEqual(target, 0.f) ? -23.f : target; }

    // The angle of a reading on the ring of lights: 0 LU at the top, -36 at the bottom by way of the left, +18 on the right
    static float ringAngle(float lu) { return juce::degreesToRadians(juce::jlimit(-36.f, 18.f, lu) * 5.f); }

    std::array<float, numSlots> slots;
    int writeIndex = 0;
    double secondsInSlot = 0.0;
    double secondsListened = 0.0;

    LoudnessMeter::Readings readings, shown;
    float target = -14.f;
    float turnTime = 60.f;
    float ringLevel = silenceLevel; // the light ring, which falls back by itself
    float ringPeak = silenceLevel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoudnessRadarView)
};
