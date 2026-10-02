#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include "LoudnessMeter.h"
#include "TonalTargets.h"

//==============================================================================
// Reference tracks to compare a mix with: up to four recordings are kept in memory at the sample rate of the host,
// and one of them can be heard in place of the mix, looping a part of its own that is chosen on its waveform.
// The meters keep measuring the mix whichever is playing. A reference is measured when it is loaded and when its
// part changes: its loudness, its peak, its stereo width and its tonal shape, from which the plugin writes a few
// words that describe it.
class ReferenceManager
{
public:
    static constexpr int numSlots = 4;
    static constexpr double maxSeconds = 600.0;
    static constexpr double maxRegionSeconds = 90.0;
    static constexpr double minRegionSeconds = 1.0;
    static constexpr int thumbnailColumns = 900;

    struct Track : public juce::ReferenceCountedObject
    {
        using Ptr = juce::ReferenceCountedObjectPtr<Track>;

        juce::String name;
        juce::File file;
        double sampleRate = 44100.0;
        juce::AudioBuffer<float> audio; // two channels

        // The lowest and highest sample of each column, for drawing the waveform
        std::vector<float> thumbLow, thumbHigh;

        // The part that loops, in samples. Read by the audio thread.
        std::atomic<int> regionStart { 0 }, regionEnd { 0 };

        // What the part measures. Written on the message thread only.
        float lufs = -70.f;   // integrated loudness
        float peakDb = -100.f; // sample peak
        float width = 0.f;    // 0 for mono to 1 for a side as strong as the mid
        float plr = 0.f;      // peak to loudness ratio
        TonalTargets::Target tonal;
        juce::StringArray tags;

        int length() const { return audio.getNumSamples(); }
    };

    ReferenceManager() : pool(1) {}

    ~ReferenceManager()
    {
        onChange = nullptr;
        pool.removeAllJobs(true, 20000);
        masterReference.clear();
    }

    // Called on the message thread when a track has come in or gone out
    std::function<void()> onChange;

    // The rate of the host, set when playback is prepared
    std::atomic<double> hostRate { 44100.0 };

    // What is heard: the mix, or the track of one of the slots, and how much louder to play it
    std::atomic<bool> monitoring { false };
    std::atomic<int> activeSlot { 0 };
    std::atomic<float> gainDb { 0.f };
    std::atomic<int> playPosition { 0 };

    //==============================================================================
    // Message thread
    Track::Ptr getTrack(int slot) const
    {
        return slot >= 0 && slot < numSlots ? uiSlots[(size_t)slot] : Track::Ptr();
    }

    // Reads, resamples and measures the file in the background, and puts it in the slot when it is ready
    void load(int slot, const juce::File& file)
    {
        if (slot < 0 || slot >= numSlots)
            return;

        ++loading;
        const double rate = hostRate.load();
        juce::WeakReference<ReferenceManager> weak(this);

        pool.addJob([weak, slot, file, rate]
        {
            auto track = ReferenceManager::decode(file, rate);

            juce::MessageManager::callAsync([weak, slot, track]
            {
                if (weak == nullptr)
                    return;

                --weak->loading;
                if (track != nullptr)
                    weak->install(slot, track);
                else if (weak->onChange)
                    weak->onChange();
            });
        });
    }

    bool isLoading() const { return loading > 0; }

    void remove(int slot)
    {
        if (slot >= 0 && slot < numSlots)
            install(slot, Track::Ptr());
    }

    // Chooses the part that loops, and measures it
    void setRegion(int slot, int start, int end)
    {
        auto track = getTrack(slot);
        if (track == nullptr)
            return;

        const int length = track->length();
        const int minLength = (int)(minRegionSeconds * track->sampleRate);
        const int maxLength = (int)(maxRegionSeconds * track->sampleRate);

        start = juce::jlimit(0, juce::jmax(0, length - minLength), start);
        end = juce::jlimit(start + minLength, juce::jmin(length, start + maxLength), end);

        track->regionStart.store(start);
        track->regionEnd.store(juce::jmax(start + 1, end));
        measureRegion(*track);

        if (onChange)
            onChange();
    }

    // Finds the loudest ten seconds and loops them
    void smartLoop(int slot)
    {
        auto track = getTrack(slot);
        if (track == nullptr)
            return;

        findLoudestRegion(*track);
        measureRegion(*track);

        if (onChange)
            onChange();
    }

    //==============================================================================
    // Audio thread: puts the track in place of the mix when it is being listened to, with a short fade between the two
    void process(juce::AudioBuffer<float>& buffer, double rate)
    {
        const bool wanted = monitoring.load(std::memory_order_relaxed);
        const int slot = juce::jlimit(0, numSlots - 1, activeSlot.load(std::memory_order_relaxed));
        Track* track = audioSlots[(size_t)slot].load(std::memory_order_acquire);

        const bool usable = wanted && track != nullptr && track->length() > 0 && std::abs(track->sampleRate - rate) < 1.0;
        const float target = usable ? 1.f : 0.f;

        if (fade <= 0.f && target <= 0.f)
            return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const float targetGain = juce::Decibels::decibelsToGain(gainDb.load(std::memory_order_relaxed));

        // Starting from the mix, the gain is not swept from wherever it was
        if (fade <= 0.f)
            gain = targetGain;

        int start = 0, end = 0, length = 0;
        const float* left = nullptr;
        const float* right = nullptr;

        if (track != nullptr && track->length() > 0)
        {
            length = track->length();
            start = juce::jlimit(0, length - 1, track->regionStart.load(std::memory_order_relaxed));
            end = juce::jlimit(start + 1, length, track->regionEnd.load(std::memory_order_relaxed));
            left = track->audio.getReadPointer(0);
            right = track->audio.getReadPointer(1);
        }

        int position = playPosition.load(std::memory_order_relaxed);
        if (position < start || position >= end)
            position = start;

        const float step = (float)(1.0 / (0.02 * rate));

        for (int i = 0; i < numSamples; ++i)
        {
            fade = target > fade ? juce::jmin(target, fade + step) : juce::jmax(target, fade - step);
            gain += (targetGain - gain) * 0.0015f;

            float referenceLeft = 0.f, referenceRight = 0.f;
            if (left != nullptr && fade > 0.f)
            {
                referenceLeft = left[position];
                referenceRight = right[position];
                if (++position >= end)
                    position = start;
            }

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* data = buffer.getWritePointer(channel);
                const float reference = channel == 0 ? referenceLeft : referenceRight;
                data[i] = data[i] * (1.f - fade) + reference * gain * fade;
            }
        }

        playPosition.store(position, std::memory_order_relaxed);
    }

private:
    //==============================================================================
    // Message thread: puts a track in a slot, and keeps the one that it replaces until the audio thread is sure to be done with it
    void install(int slot, const Track::Ptr& track)
    {
        auto old = uiSlots[(size_t)slot];
        uiSlots[(size_t)slot] = track;
        audioSlots[(size_t)slot].store(track.get(), std::memory_order_release);

        if (old != nullptr)
        {
            graveyard.push_back(old);
            juce::WeakReference<ReferenceManager> weak(this);
            juce::Timer::callAfterDelay(1000, [weak]
            {
                if (weak != nullptr)
                    weak->graveyard.clear();
            });
        }

        playPosition.store(0);

        if (onChange)
            onChange();
    }

    // Reads a file, brings it to the rate of the host, and measures it. On a background thread.
    static Track::Ptr decode(const juce::File& file, double rate)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->lengthInSamples < 2048 || reader->sampleRate <= 0.0)
            return {};

        const auto length = (int)std::min<juce::int64>(reader->lengthInSamples, (juce::int64)(maxSeconds * reader->sampleRate));

        juce::AudioBuffer<float> raw(2, length);
        raw.clear();
        if (!reader->read(&raw, 0, length, 0, true, true))
            return {};

        // A mono file plays in both channels
        if (reader->numChannels < 2)
            raw.copyFrom(1, 0, raw, 0, 0, length);

        Track::Ptr track = new Track();
        track->name = file.getFileNameWithoutExtension();
        track->file = file;
        track->sampleRate = rate;

        if (std::abs(reader->sampleRate - rate) < 1.0)
        {
            track->audio = std::move(raw);
        }
        else
        {
            const double ratio = reader->sampleRate / rate;
            const int outLength = (int)std::ceil((double)length / ratio);
            track->audio.setSize(2, outLength);

            for (int channel = 0; channel < 2; ++channel)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.process(ratio, raw.getReadPointer(channel), track->audio.getWritePointer(channel), outLength);
            }
        }

        makeThumbnail(*track);
        findLoudestRegion(*track);
        measureRegion(*track);
        return track;
    }

    static void makeThumbnail(Track& track)
    {
        const int length = track.length();
        track.thumbLow.assign((size_t)thumbnailColumns, 0.f);
        track.thumbHigh.assign((size_t)thumbnailColumns, 0.f);

        const float* left = track.audio.getReadPointer(0);
        const float* right = track.audio.getReadPointer(1);

        for (int column = 0; column < thumbnailColumns; ++column)
        {
            const int from = (int)((juce::int64)column * length / thumbnailColumns);
            const int to = juce::jmax(from + 1, (int)((juce::int64)(column + 1) * length / thumbnailColumns));
            float low = 0.f, high = 0.f;
            for (int i = from; i < to && i < length; ++i)
            {
                const float mono = 0.5f * (left[i] + right[i]);
                low = juce::jmin(low, mono);
                high = juce::jmax(high, mono);
            }
            track.thumbLow[(size_t)column] = low;
            track.thumbHigh[(size_t)column] = high;
        }
    }

    // The ten seconds with the most energy
    static void findLoudestRegion(Track& track)
    {
        const int length = track.length();
        const int hop = juce::jmax(1, (int)(0.5 * track.sampleRate));
        const int window = juce::jmin(length, (int)(10.0 * track.sampleRate));

        std::vector<double> energy;
        const float* left = track.audio.getReadPointer(0);
        const float* right = track.audio.getReadPointer(1);

        for (int start = 0; start < length; start += hop)
        {
            double sum = 0.0;
            for (int i = start; i < juce::jmin(length, start + hop); ++i)
                sum += (double)left[i] * left[i] + (double)right[i] * right[i];
            energy.push_back(sum);
        }

        const int blocksInWindow = juce::jmax(1, window / hop);
        double running = 0.0, best = -1.0;
        int bestBlock = 0;

        for (int block = 0; block < (int)energy.size(); ++block)
        {
            running += energy[(size_t)block];
            if (block >= blocksInWindow)
                running -= energy[(size_t)(block - blocksInWindow)];

            if (block >= blocksInWindow - 1 && running > best)
            {
                best = running;
                bestBlock = block - blocksInWindow + 1;
            }
        }

        const int start = juce::jlimit(0, juce::jmax(0, length - window), bestBlock * hop);
        track.regionStart.store(start);
        track.regionEnd.store(juce::jmin(length, start + window));
    }

    // Measures the part that loops
    static void measureRegion(Track& track)
    {
        const int start = track.regionStart.load();
        const int end = track.regionEnd.load();
        const int length = juce::jmax(0, end - start);
        if (length < 1024)
            return;

        juce::AudioBuffer<float> part(2, length);
        part.copyFrom(0, 0, track.audio, 0, start, length);
        part.copyFrom(1, 0, track.audio, 1, start, length);

        const float* left = part.getReadPointer(0);
        const float* right = part.getReadPointer(1);

        // Peak and width
        float peak = 0.f;
        double midEnergy = 0.0, sideEnergy = 0.0;
        for (int i = 0; i < length; ++i)
        {
            peak = juce::jmax(peak, std::abs(left[i]), std::abs(right[i]));
            const double mid = 0.5 * ((double)left[i] + right[i]);
            const double side = 0.5 * ((double)left[i] - right[i]);
            midEnergy += mid * mid;
            sideEnergy += side * side;
        }

        const double mid = std::sqrt(midEnergy / length), side = std::sqrt(sideEnergy / length);
        track.width = mid + side > 1.0e-9 ? (float)juce::jlimit(0.0, 1.0, 2.0 * side / (mid + side)) : 0.f;
        track.peakDb = juce::Decibels::gainToDecibels(peak, -100.f);

        // Loudness, with the filters that the meter uses
        {
            LoudnessMeter meter;
            meter.prepare(track.sampleRate);
            constexpr int chunk = 4096;
            for (int i = 0; i < length; i += chunk)
                meter.process(left + i, right + i, juce::jmin(chunk, length - i));

            const float integrated = meter.read().integrated;
            track.lufs = std::isfinite(integrated) && integrated > -100.f ? integrated : -70.f;
        }

        track.plr = track.peakDb - track.lufs;
        track.tonal = TonalTargets::analyse(track.name, part, track.sampleRate);
        track.tags = describe(track);
    }

    // The mean of a curve between two frequencies
    static float meanBetween(const std::vector<float>& curve, double from, double to)
    {
        double sum = 0.0;
        int count = 0;
        for (int point = 0; point < (int)curve.size(); ++point)
        {
            const double frequency = TonalTargets::frequencyOf(point);
            if (frequency >= from && frequency <= to)
            {
                sum += (double)curve[(size_t)point];
                ++count;
            }
        }
        return count > 0 ? (float)(sum / count) : 0.f;
    }

    // A few words on the tone, the width, the dynamics and the loudness of a track
    static juce::StringArray describe(const Track& track)
    {
        juce::StringArray words;

        // The tone, against the shape of a typical modern master
        const auto typical = TonalTargets::builtIn().front();
        auto lean = [&](double lowFrom, double lowTo, double highFrom, double highTo)
        {
            return (meanBetween(track.tonal.centre, highFrom, highTo) - meanBetween(track.tonal.centre, lowFrom, lowTo))
                   - (meanBetween(typical.centre, highFrom, highTo) - meanBetween(typical.centre, lowFrom, lowTo));
        };

        const float brightness = lean(300.0, 2000.0, 5000.0, 16000.0);
        const float bass = lean(300.0, 2000.0, 40.0, 120.0);
        words.add(brightness > 3.f ? "Bright" : brightness < -3.f ? "Dark" : "Balanced tone");
        if (bass > 3.f)
            words.add("Heavy bass");
        else if (bass < -3.f)
            words.add("Light bass");

        words.add(track.width < 0.1f ? "Mono" : track.width < 0.3f ? "Focused width" : track.width < 0.65f ? "Open width" : "Wide");
        words.add(track.plr >= 14.f ? "Dynamic" : track.plr >= 10.f ? "Punchy" : track.plr >= 7.f ? "Controlled" : "Compressed");
        words.add(track.lufs > -9.f ? "Loud" : track.lufs < -15.f ? "Quiet" : "Medium loudness");
        return words;
    }

    //==============================================================================
    std::array<Track::Ptr, (size_t)numSlots> uiSlots;
    std::array<std::atomic<Track*>, (size_t)numSlots> audioSlots { {} };
    std::vector<Track::Ptr> graveyard;
    std::atomic<int> loading { 0 };
    juce::ThreadPool pool;

    // Audio thread only
    float fade = 0.f, gain = 1.f;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ReferenceManager)
    JUCE_DECLARE_NON_COPYABLE(ReferenceManager)
};
