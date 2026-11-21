// This file is part of ULTIMATE METER 2, by Jm Castillo (jmcastillo.es).
// Copyright (C) 2026 Jm Castillo. ULTIMATE METER 2 is a modified version of Grisey, Copyright (C) Yulania,
// changed in 2026. It is free software under the GNU General Public License, version 3: see LICENSE.md.

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

        // What a part of the track measures
        struct Measure
        {
            float lufs = -70.f;    // integrated loudness
            float peakDb = -100.f; // sample peak
            float width = 0.f;     // 0 for mono to 1 for a side as strong as the mid
            float plr = 0.f;       // peak to loudness ratio
            TonalTargets::Target tonal;
            juce::StringArray tags;
        };

        // The part that loops, and the whole track. Written on the message thread only.
        Measure region, whole;

        // The integrated loudness and the peak of everything up to each second, which is what a mix that has played
        // from the start has to be compared with when the track follows the host
        std::vector<float> lufsUpTo, peakUpTo;

        const Measure& measureFor(bool mirror) const { return mirror ? whole : region; }

        float lufsAt(int sample, bool mirror) const
        {
            if (!mirror || lufsUpTo.empty())
                return measureFor(mirror).lufs;
            const auto second = (size_t)juce::jlimit(0, (int)lufsUpTo.size() - 1, (int)((double)sample / sampleRate));
            return lufsUpTo[second] > -69.f ? lufsUpTo[second] : whole.lufs;
        }

        float peakAt(int sample, bool mirror) const
        {
            if (!mirror || peakUpTo.empty())
                return measureFor(mirror).peakDb;
            return peakUpTo[(size_t)juce::jlimit(0, (int)peakUpTo.size() - 1, (int)((double)sample / sampleRate))];
        }

        int length() const { return audio.getNumSamples(); }
    };

    ReferenceManager() : pool(1) {}

    ~ReferenceManager()
    {
        onChange = nullptr;
        onFilesChanged = nullptr;
        pool.removeAllJobs(true, 20000);
        masterReference.clear();
    }

    // Called on the message thread when a track has come in or gone out
    std::function<void()> onChange;

    // Called on the message thread when the file of a slot changes, which is at once, before the file has been read.
    // The processor keeps the files in the session with it.
    std::function<void()> onFilesChanged;

    // The file of each slot, whether it has been read yet or not. Message thread.
    juce::File getFile(int slot) const { return slot >= 0 && slot < numSlots ? files[(size_t)slot] : juce::File(); }

    // Puts these files in the slots, reading the ones that are not there yet and emptying the slots that have none
    void setFiles(const std::array<juce::File, (size_t)numSlots>& wanted)
    {
        for (int slot = 0; slot < numSlots; ++slot)
        {
            const auto& file = wanted[(size_t)slot];
            if (file == files[(size_t)slot])
                continue;

            if (file.existsAsFile())
                load(slot, file);
            else if (files[(size_t)slot] != juce::File())
                remove(slot);
        }
    }

    // Called when the rate of the host is known. Tracks that were read for another rate are read again, as a track
    // only plays at the rate it was made for. Any thread.
    void setHostRate(double rate)
    {
        if (std::abs(hostRate.exchange(rate) - rate) < 1.0)
            return;

        juce::WeakReference<ReferenceManager> weak(this);
        juce::MessageManager::callAsync([weak]
        {
            if (weak == nullptr)
                return;

            for (int slot = 0; slot < numSlots; ++slot)
            {
                auto track = weak->getTrack(slot);
                if (track != nullptr && std::abs(track->sampleRate - weak->hostRate.load()) >= 1.0 && track->file.existsAsFile())
                    weak->load(slot, track->file);
            }
        });
    }

    // The rate of the host, set when playback is prepared
    std::atomic<double> hostRate { 44100.0 };

    // What is heard: the mix, or the track of one of the slots, and how much louder to play it
    std::atomic<bool> monitoring { false };
    std::atomic<int> activeSlot { 0 };
    std::atomic<float> gainDb { 0.f };
    std::atomic<int> playPosition { 0 };

    // Mirror: the track plays at the position of the host, so that a rough mix of the same song stays alongside the mix.
    // The offset moves it, in samples, to put the two in line.
    std::atomic<bool> mirror { false };
    std::atomic<int> mirrorOffset { 0 };

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

        files[(size_t)slot] = file;
        if (onFilesChanged)
            onFilesChanged();

        pool.addJob([weak, slot, file, rate]
        {
            auto track = ReferenceManager::decode(file, rate);

            juce::MessageManager::callAsync([weak, slot, file, track]
            {
                if (weak == nullptr)
                    return;

                --weak->loading;

                // Another file may have been chosen for the slot while this one was being read
                if (weak->files[(size_t)slot] != file)
                    return;

                if (track != nullptr)
                {
                    weak->install(slot, track);
                    return;
                }

                // A file that cannot be read does not stay in the session
                weak->files[(size_t)slot] = juce::File();
                if (weak->onFilesChanged)
                    weak->onFilesChanged();
                if (weak->onChange)
                    weak->onChange();
            });
        });
    }

    bool isLoading() const { return loading > 0; }

    void remove(int slot)
    {
        if (slot >= 0 && slot < numSlots)
        {
            files[(size_t)slot] = juce::File();
            if (onFilesChanged)
                onFilesChanged();
            install(slot, Track::Ptr());
        }
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
    // It is not allowed while the host renders offline, so that a reference is never printed into a bounce.
    void process(juce::AudioBuffer<float>& buffer, double rate, juce::int64 hostSample, bool hostPlaying, const std::array<float, 4>& monitor = { 1.f, 0.f, 0.f, 1.f }, bool allowed = true)
    {
        const bool wanted = allowed && monitoring.load(std::memory_order_relaxed);
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

        const bool following = mirror.load(std::memory_order_relaxed);
        int position = playPosition.load(std::memory_order_relaxed);
        if (!following && (position < start || position >= end))
            position = start;

        // Following the host, the track is where the host is, moved by the offset, and silent where the host is not playing
        const juce::int64 mirrorStart = hostSample + (juce::int64)mirrorOffset.load(std::memory_order_relaxed);

        const float step = (float)(1.0 / (0.02 * rate));

        for (int i = 0; i < numSamples; ++i)
        {
            fade = target > fade ? juce::jmin(target, fade + step) : juce::jmax(target, fade - step);
            gain += (targetGain - gain) * 0.0015f;

            float referenceLeft = 0.f, referenceRight = 0.f;
            if (left != nullptr && fade > 0.f)
            {
                if (following)
                {
                    const juce::int64 at = mirrorStart + i;
                    position = (int)juce::jlimit<juce::int64>(0, length, at);
                    if (hostPlaying && hostSample >= 0 && at >= 0 && at < length)
                    {
                        referenceLeft = left[at];
                        referenceRight = right[at];
                    }
                }
                else
                {
                    referenceLeft = left[position];
                    referenceRight = right[position];
                    if (++position >= end)
                        position = start;
                }
            }

            // The reference is monitored as the mix is: in the mid, in the side, in one channel
            const float monitoredLeft = monitor[0] * referenceLeft + monitor[1] * referenceRight;
            const float monitoredRight = monitor[2] * referenceLeft + monitor[3] * referenceRight;
            referenceLeft = monitoredLeft;
            referenceRight = monitoredRight;

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
            resample(raw, reader->sampleRate, rate, track->audio);
        }

        makeThumbnail(*track);
        findLoudestRegion(*track);
        measureRegion(*track);
        measureWhole(*track);
        return track;
    }

    // Brings a recording to another rate with a windowed sinc, which keeps the top of the spectrum as it is, so that the
    // tone of the reference can be compared with the mix. Going down in rate, what the new rate cannot hold is filtered
    // out first, forwards and backwards so that nothing moves in time.
    static void resample(juce::AudioBuffer<float>& source, double sourceRate, double targetRate, juce::AudioBuffer<float>& result)
    {
        const double ratio = sourceRate / targetRate;
        const int length = source.getNumSamples();
        const int outLength = juce::jmax(1, (int)std::floor((double)length / ratio));
        result.setSize(2, outLength);

        constexpr int padding = 256; // silence after the end, for the interpolator to read past the last sample
        std::vector<float> input((size_t)(length + padding + (int)std::ceil(ratio) + 8), 0.f);

        for (int channel = 0; channel < 2; ++channel)
        {
            std::copy(source.getReadPointer(channel), source.getReadPointer(channel) + length, input.begin());

            if (ratio > 1.0)
            {
                const auto sections = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod((float)(0.45 * targetRate), sourceRate, 8);
                for (int pass = 0; pass < 2; ++pass)
                {
                    for (const auto& coefficients : sections)
                    {
                        juce::dsp::IIR::Filter<float> filter(coefficients);
                        for (int i = 0; i < length + padding; ++i)
                            input[(size_t)i] = filter.processSample(input[(size_t)i]);
                    }

                    std::reverse(input.begin(), input.begin() + length + padding);
                }
            }

            // The interpolator gives its output late by a fixed number of input samples, which are left out
            juce::WindowedSincInterpolator interpolator;
            const int skip = (int)std::lround((double)interpolator.getBaseLatency() / ratio);
            std::vector<float> output((size_t)(outLength + skip), 0.f);
            const int room = (int)std::floor((double)(length + padding) / ratio);
            interpolator.process(ratio, input.data(), output.data(), juce::jmin(outLength + skip, room));
            std::copy(output.begin() + skip, output.begin() + skip + outLength, result.getWritePointer(channel));
        }
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

    // Measures the part that loops, and the whole track
    static void measureRegion(Track& track)
    {
        track.region = measure(track, track.regionStart.load(), track.regionEnd.load(), nullptr, nullptr);
    }

    static void measureWhole(Track& track)
    {
        track.whole = measure(track, 0, track.length(), &track.lufsUpTo, &track.peakUpTo);
    }

    // Measures a part of a track. For a whole track it also takes the loudness and the peak up to every second.
    static Track::Measure measure(const Track& track, int start, int end, std::vector<float>* lufsTrail, std::vector<float>* peakTrail)
    {
        Track::Measure result;
        const int length = juce::jmax(0, end - start);
        if (length < 1024)
            return result;

        const float* left = track.audio.getReadPointer(0) + start;
        const float* right = track.audio.getReadPointer(1) + start;

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
        result.width = mid + side > 1.0e-9 ? (float)juce::jlimit(0.0, 1.0, 2.0 * side / (mid + side)) : 0.f;
        result.peakDb = juce::Decibels::gainToDecibels(peak, -100.f);

        // Loudness, with the filters that the meter uses
        {
            LoudnessMeter meter;
            meter.prepare(track.sampleRate);
            constexpr int chunk = 4096;
            const int perSecond = juce::jmax(1, (int)track.sampleRate);
            float runningPeak = 0.f;
            int nextSecond = perSecond;

            for (int i = 0; i < length; i += chunk)
            {
                const int n = juce::jmin(chunk, length - i);
                meter.process(left + i, right + i, n);

                if (lufsTrail != nullptr)
                {
                    for (int k = i; k < i + n; ++k)
                        runningPeak = juce::jmax(runningPeak, std::abs(left[k]), std::abs(right[k]));

                    while (i + n >= nextSecond)
                    {
                        const float integrated = meter.read().integrated;
                        lufsTrail->push_back(std::isfinite(integrated) && integrated > -100.f ? integrated : -70.f);
                        peakTrail->push_back(juce::Decibels::gainToDecibels(runningPeak, -100.f));
                        nextSecond += perSecond;
                    }
                }
            }

            const float integrated = meter.read().integrated;
            result.lufs = std::isfinite(integrated) && integrated > -100.f ? integrated : -70.f;
        }

        result.plr = result.peakDb - result.lufs;

        juce::AudioBuffer<float> part(2, length);
        part.copyFrom(0, 0, track.audio, 0, start, length);
        part.copyFrom(1, 0, track.audio, 1, start, length);
        result.tonal = TonalTargets::analyse(track.name, part, track.sampleRate);
        result.tags = describe(result);
        return result;
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
    static juce::StringArray describe(const Track::Measure& track)
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
    std::array<juce::File, (size_t)numSlots> files;
    std::array<std::atomic<Track*>, (size_t)numSlots> audioSlots { {} };
    std::vector<Track::Ptr> graveyard;
    std::atomic<int> loading { 0 };
    juce::ThreadPool pool;

    // Audio thread only
    float fade = 0.f, gain = 1.f;

    JUCE_DECLARE_WEAK_REFERENCEABLE(ReferenceManager)
    JUCE_DECLARE_NON_COPYABLE(ReferenceManager)
};
