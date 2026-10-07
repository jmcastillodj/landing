#pragma once

#include <JuceHeader.h>
#include "BpmAnalyzer.h"

//==============================================================================
// The tempo of the music, as BPM Detective measures it: the audio thread feeds the analyzer with the mono sum,
// and a low priority thread reads the analyzer twice a second and keeps the best tempo. The readings are atomics
// that the editor reads. The audio is not changed.
class BpmDetector
{
public:
    BpmDetector() : thread(*this) {}
    ~BpmDetector() { thread.stopThread(2000); }

    // Not on the audio thread. The analyzer's memory is made again, so the thread stops while it is.
    void prepare(double sampleRate)
    {
        thread.stopThread(2000);
        analyzer.prepare(sampleRate);
        thread.startThread(juce::Thread::Priority::low);
    }

    // Audio thread. `left` and `right` may be the same channel.
    void process(const float* left, const float* right, int numSamples, bool hostPlaying)
    {
        if (hostPlaying && !wasPlaying)
            analyzer.resetWindow(); // a new playing, a new window
        wasPlaying = hostPlaying;

        if (clearRequested.exchange(false))
            analyzer.resetWindow();

        if (numSamples <= 0)
            return;

        if ((int)mono.size() < numSamples)
            mono.resize((size_t)numSamples); // only if the host sends more than it announced

        for (int i = 0; i < numSamples; ++i)
            mono[(size_t)i] = 0.5f * (left[i] + right[i]);

        analyzer.processBlock(mono.data(), numSamples);
    }

    //==============================================================================
    // For the editor
    bool hasResult() const { return tapActive.load() || detectedBpm.load() > 0.0; }
    bool isTapActive() const { return tapActive.load(); }
    double getDetectedBpm() const { return tapActive.load() ? tapBpm.load() : detectedBpm.load(); } // without the x2 and :2

    // Rounded to one decimal before it is multiplied, so that x2 and :2 give exactly the double and the half of what is shown
    double getDisplayBpm() const { return std::round(getDetectedBpm() * 10.0) / 10.0 * std::pow(2.0, octaveShift.load()); }
    float getConfidence() const { return tapActive.load() ? tapConfidence.load() : confidence.load(); }
    double getSecondsAnalysed() const { return analyzer.getSecondsAvailable(); }
    int getOctaveShift() const { return octaveShift.load(); }
    void multiplyTempo(int octaves) { octaveShift.store(juce::jlimit(-3, 3, octaveShift.load() + octaves)); }
    bool isHeld() const { return hold.load(); }
    void setHeld(bool shouldHold) { hold.store(shouldHold); }
    void copyRecentEnvelope(float* destination, int count) const { analyzer.copyRecent(destination, count); }
    double getEnvelopeRate() const { return analyzer.getEnvelopeRate(); }

    void reset()
    {
        detectedBpm.store(0.0);
        confidence.store(0.0f);
        octaveShift.store(0);
        tapActive.store(false);
        tapTimes.clear();
        hold.store(false);
        clearRequested.store(true);
    }

    // Message thread
    void tap()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (!tapTimes.empty() && now - tapTimes.back() > 2500.0)
            tapTimes.clear(); // a long pause: start again
        tapTimes.push_back(now);
        if (tapTimes.size() > 12)
            tapTimes.erase(tapTimes.begin());

        if (tapTimes.size() >= 2)
        {
            const double averageMs = (tapTimes.back() - tapTimes.front()) / (double)(tapTimes.size() - 1);
            tapBpm.store(60000.0 / averageMs);
            tapConfidence.store(juce::jmin(1.0f, (float)tapTimes.size() / 8.0f));
            tapActive.store(true);
        }
    }

private:
    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread(BpmDetector& o) : juce::Thread("Tempo analysis"), owner(o) {}

        void run() override
        {
            while (!threadShouldExit())
            {
                wait(500);
                if (threadShouldExit())
                    break;

                if (owner.hold.load())
                    continue;

                const auto result = owner.analyzer.analyze();
                if (!result.valid)
                    continue;

                // Hysteresis, so that noise does not move the last decimal
                const double current = owner.detectedBpm.load();
                if (current <= 0.0 || std::abs(result.bpm - current) >= 0.06)
                    owner.detectedBpm.store(result.bpm);
                owner.confidence.store(result.confidence);
            }
        }

    private:
        BpmDetector& owner;
    };

    BpmAnalyzer analyzer;
    AnalysisThread thread;
    std::vector<float> mono;

    std::atomic<double> detectedBpm { 0.0 }, tapBpm { 0.0 };
    std::atomic<float> confidence { 0.0f }, tapConfidence { 0.0f };
    std::atomic<bool> tapActive { false }, hold { false }, clearRequested { false };
    std::atomic<int> octaveShift { 0 };
    std::vector<double> tapTimes; // in ms, on the message thread only
    bool wasPlaying = false;
};
