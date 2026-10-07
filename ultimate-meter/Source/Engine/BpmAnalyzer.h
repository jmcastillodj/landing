#pragma once

// From BPM Detective, by the same author. The algorithm is unchanged.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

/*
    BpmAnalyzer - detector de tempo sin dependencias de JUCE.

    Pipeline:
      1. (hilo de audio) El audio mono se divide en 3 bandas (graves / medios / agudos).
         Cada ~5 ms se calcula la energia en cada banda, se comprime en escala log y se
         guarda su aumento positivo (flujo de energia) -> "envolvente de onsets".
      2. (hilo de analisis) Se quita la tendencia de la envolvente y se calcula un
         tempograma de Fourier: para cada BPM candidato se suman las magnitudes de la
         envolvente en la frecuencia del pulso y en sus armonicos (corcheas, semicorcheas...).
      3. Se pondera con un prior suave alrededor de 120 BPM, se buscan los mejores picos
         en un barrido grueso (0.2 BPM) y se refina el mejor con paso de 0.01 BPM.
*/
// M_PI no existe en MSVC
constexpr double kPi = 3.14159265358979323846;

class BpmAnalyzer
{
public:
    struct Result
    {
        bool valid = false;
        double bpm = 0.0;
        float confidence = 0.0f; // 0..1
    };

    static constexpr double minBpm = 60.0;
    static constexpr double maxBpm = 190.0;
    static constexpr double minSeconds = 6.0;   // datos minimos antes de dar resultado
    static constexpr double maxSeconds = 60.0;  // ventana maxima de analisis
    static constexpr double targetEnvRate = 200.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        hop = std::max (1, (int) std::lround (sr / targetEnvRate));
        envRate = sr / hop;
        ringSize = 1 << 15;
        ring.assign ((size_t) ringSize, 0.0f);
        writeCount.store (0);
        windowStart.store (0);
        lpLow = lpMid = 0.0f;
        aLow = onePoleCoef (200.0);
        aMid = onePoleCoef (2500.0);
        hopPos = 0;
        eLow = eMid = eHigh = 0.0;
        prevLow = prevMid = prevHigh = 0.0f;
        silentHops = 0;
        silenceReset.store (false);
    }

    double getEnvelopeRate() const { return envRate; }

    // Hilo de audio. No reserva memoria ni bloquea.
    void processBlock (const float* mono, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = mono[i];
            lpLow += aLow * (x - lpLow);
            lpMid += aMid * (x - lpMid);
            const float low = lpLow, mid = lpMid - lpLow, high = x - lpMid;
            eLow += (double) low * low;
            eMid += (double) mid * mid;
            eHigh += (double) high * high;

            if (++hopPos >= hop)
                finishHop();
        }
    }

    // Empieza una ventana de analisis nueva (el hilo de audio puede llamarlo).
    void resetWindow()
    {
        windowStart.store (writeCount.load (std::memory_order_acquire), std::memory_order_release);
    }

    // true una vez si se detecto un silencio largo (la ventana ya se reinicio).
    bool consumeSilenceReset() { return silenceReset.exchange (false); }

    double getSecondsAvailable() const
    {
        return (double) (writeCount.load (std::memory_order_acquire) - windowStart.load (std::memory_order_acquire)) / envRate;
    }

    // Hilo de analisis / UI: copia los ultimos n valores de la envolvente (para dibujarla).
    void copyRecent (float* dst, int n) const
    {
        const uint64_t w = writeCount.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const int64_t idx = (int64_t) w - n + i;
            dst[i] = idx >= 0 ? ring[(size_t) ((uint64_t) idx & (uint64_t) (ringSize - 1))] : 0.0f;
        }
    }

    // Hilo de analisis.
    Result analyze() const
    {
        const uint64_t w = writeCount.load (std::memory_order_acquire);
        const uint64_t start = windowStart.load (std::memory_order_acquire);
        const size_t maxN = (size_t) (maxSeconds * envRate);
        size_t n = (size_t) std::min<uint64_t> (w - start, std::min<uint64_t> (maxN, (uint64_t) ringSize / 2));
        std::vector<float> env (n);
        for (size_t i = 0; i < n; ++i)
            env[i] = ring[(size_t) ((w - n + i) & (uint64_t) (ringSize - 1))];
        return analyzeEnvelope (env, envRate);
    }

    static Result analyzeEnvelope (const std::vector<float>& env, double rate)
    {
        Result res;
        const size_t n = env.size();
        if ((double) n < minSeconds * rate)
            return res;

        // --- quitar tendencia (media movil ~1 s), rectificar, quitar media, ventana Hann
        std::vector<double> x (n);
        {
            const size_t half = (size_t) (0.5 * rate);
            std::vector<double> prefix (n + 1, 0.0);
            for (size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + env[i];
            double mean = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                const size_t a = i > half ? i - half : 0, b = std::min (n, i + half + 1);
                const double ma = (prefix[b] - prefix[a]) / (double) (b - a);
                x[i] = std::max (0.0, env[i] - ma);
                mean += x[i];
            }
            mean /= (double) n;
            double energy = 0.0;
            for (size_t i = 0; i < n; ++i)
            {
                const double hann = 0.5 - 0.5 * std::cos (2.0 * kPi * ((double) i + 0.5) / (double) n);
                x[i] = (x[i] - mean) * hann;
                energy += x[i] * x[i];
            }
            if (energy < 1e-9) // silencio / sin ritmo
                return res;
        }

        // --- barrido grueso
        const double coarse = 0.2;
        const int nc = (int) ((maxBpm - minBpm) / coarse) + 1;
        std::vector<double> score ((size_t) nc);
        double sum = 0.0;
        for (int k = 0; k < nc; ++k)
        {
            score[(size_t) k] = weightedScore (x, rate, minBpm + k * coarse);
            sum += score[(size_t) k];
        }
        const double mean = sum / nc;

        // --- mejores picos locales
        struct Peak { double bpm, s; };
        std::vector<Peak> peaks;
        for (int k = 1; k < nc - 1; ++k)
        {
            const double s = score[(size_t) k];
            if (s > score[(size_t) k - 1] && s >= score[(size_t) k + 1])
                peaks.push_back ({ minBpm + k * coarse, s });
        }
        if (peaks.empty())
            return res;
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.s > b.s; });
        if (peaks.size() > 4) peaks.resize (4);

        // --- refinado fino alrededor de cada pico
        double bestBpm = 0.0, bestScore = -1.0;
        for (const auto& p : peaks)
        {
            for (double b = p.bpm - 0.5; b <= p.bpm + 0.5 + 1e-9; b += 0.01)
            {
                if (b < minBpm || b > maxBpm) continue;
                const double s = weightedScore (x, rate, b);
                if (s > bestScore) { bestScore = s; bestBpm = b; }
            }
        }

        res.valid = true;
        res.bpm = bestBpm;
        const double ratio = bestScore / std::max (mean, 1e-12);
        res.confidence = (float) std::clamp ((ratio - 1.5) / 4.0, 0.0, 1.0);
        return res;
    }

private:
    float onePoleCoef (double fc) const { return (float) (1.0 - std::exp (-2.0 * kPi * fc / sr)); }

    void finishHop()
    {
        const float inv = 1.0f / (float) hop;
        const float cLow = compress ((float) std::sqrt (eLow * inv));
        const float cMid = compress ((float) std::sqrt (eMid * inv));
        const float cHigh = compress ((float) std::sqrt (eHigh * inv));
        const bool silent = (eLow + eMid + eHigh) * inv < 1e-10; // < -100 dBFS
        const float flux = 1.5f * std::max (0.0f, cLow - prevLow)
                         + 1.0f * std::max (0.0f, cMid - prevMid)
                         + 0.7f * std::max (0.0f, cHigh - prevHigh);
        prevLow = cLow; prevMid = cMid; prevHigh = cHigh;
        eLow = eMid = eHigh = 0.0;
        hopPos = 0;

        // Silencio prolongado (>1.5 s): ventana nueva, para no mezclar fases distintas.
        if (silent)
        {
            if (++silentHops == (int) (1.5 * envRate))
            {
                resetWindow();
                silenceReset.store (true);
            }
        }
        else
            silentHops = 0;

        const uint64_t w = writeCount.load (std::memory_order_relaxed);
        ring[(size_t) (w & (uint64_t) (ringSize - 1))] = flux;
        writeCount.store (w + 1, std::memory_order_release);
    }

    static float compress (float rms) { return std::log1p (1000.0f * rms); }

    static double weightedScore (const std::vector<double>& x, double rate, double bpm)
    {
        static const double w[4] = { 1.0, 0.7, 0.5, 0.35 };
        double s = 0.0;
        for (int h = 1; h <= 4; ++h)
        {
            const double f = bpm / 60.0 * h / rate; // ciclos por muestra de envolvente
            const double nyq = 0.45;
            if (f > nyq) break;
            const std::complex<double> step = std::polar (1.0, -2.0 * kPi * f);
            std::complex<double> ph (1.0, 0.0), acc (0.0, 0.0);
            for (size_t i = 0; i < x.size(); ++i)
            {
                acc += x[i] * ph;
                ph *= step;
            }
            s += w[h - 1] * std::abs (acc);
        }
        const double o = std::log2 (bpm / 125.0) / 1.0;
        return s * std::exp (-0.5 * o * o); // prior suave en torno a 125 BPM
    }

    double sr = 44100.0, envRate = 200.0;
    int hop = 220;
    int ringSize = 1 << 15;
    std::vector<float> ring;
    std::atomic<uint64_t> writeCount { 0 }, windowStart { 0 };
    std::atomic<bool> silenceReset { false };

    float lpLow = 0, lpMid = 0, aLow = 0, aMid = 0;
    int hopPos = 0, silentHops = 0;
    double eLow = 0, eMid = 0, eHigh = 0;
    float prevLow = 0, prevMid = 0, prevHigh = 0;
};
