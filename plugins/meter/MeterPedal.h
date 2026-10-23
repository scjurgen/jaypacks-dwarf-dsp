#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/Analysis/GoertzelBand.h"
#include "dsp/Analysis/SignalStats.h"
#include "dsp/Analysis/ToneFit.h"
#include "dsp/Analysis/ZeroCrossingFrequency.h"

namespace jp
{

/**
 * @brief Test meter: measures a signal and its relation to a reference in windows and holds
 * the results of the last finished window for the host to read from control outputs.
 *
 * Measurement protocol: windows of `window` seconds run back to back. A new value on
 * `measure` starts a new window at the next block, with the settings the ports have then;
 * window length, tone and band frequencies set without a new `measure` value take effect at
 * the next window. When a window ends, every result is replaced at once and resultId reports
 * the `measure` value the window started with, so a client that sets its inputs, then a new
 * `measure` value, and waits for resultId to show it, gets a result measured entirely after its
 * settings. Output port updates can arrive late or be skipped (mod-ui sends them about every
 * 12.5 ms); results held for a whole window make that harmless.
 *
 * Per window, with the analysis blocks of dsp/Analysis/ (signal = input 1, reference = input 2):
 *   SignalStats of the signal: peak, RMS, DC, crest, non-finite and denormal counts, maxStep
 *   ZeroCrossingFrequency of the signal (hysteresis c_zeroCrossingHysteresis)
 *   ToneFit at toneHz: level and residual RMS of the signal
 *   RMS of the reference, and peak of signal - reference (0 when they are equal)
 *   c_bandCount TransferBands: signal level at the band frequency, gain reference -> signal
 *   and coherence, over Hann blocks of c_bandBlockSize samples (step 8: 4096 samples leak too
 *   much at 63 Hz on steep slopes); a block cut by the window end is dropped, so a window
 *   shorter than one block gives no band result (level and gain at the floor, coherence 0).
 * Levels are reported in dBFS with the floor c_floorDb, because mod-ui sends values as text
 * with six decimals, which keeps small levels in dB but not in linear units; every output is
 * clamped to the range of its port. Band frequencies above 0.49 fs are measured at 0.49 fs.
 * Non-finite samples are counted and enter the other measurements as 0.
 *
 * The signal is passed through to the output unchanged (one sample read before it is written,
 * so any buffer sharing by the host is fine). Real-time safe: setup at a window start costs a
 * few trig calls per band, a window end one 3x3 solve and some logarithms.
 */
class MeterPedal
{
  public:
    static constexpr size_t c_bandCount{8};
    static constexpr uint32_t c_bandBlockSize{16384};
    static constexpr float c_floorDb{-240.0f};
    static constexpr float c_zeroCrossingHysteresis{1e-3f};

    void setSampleRate(const float sampleRate) noexcept
    {
        m_sampleRate = static_cast<double>(sampleRate);
    }

    void reset() noexcept
    {
        m_results = Results{};
        m_startPending = false;
        startWindow();
    }

    void setMeasure(const float value) noexcept
    {
        if (value != m_measure)
        {
            m_measure = value;
            m_startPending = true;
        }
    }

    void setWindow(const float seconds) noexcept
    {
        m_windowSeconds = static_cast<double>(seconds);
    }

    void setToneHz(const float hz) noexcept
    {
        m_toneHz = static_cast<double>(hz);
    }

    void setBand1Hz(const float hz) noexcept
    {
        m_bandHz[0] = static_cast<double>(hz);
    }

    void setBand2Hz(const float hz) noexcept
    {
        m_bandHz[1] = static_cast<double>(hz);
    }

    void setBand3Hz(const float hz) noexcept
    {
        m_bandHz[2] = static_cast<double>(hz);
    }

    void setBand4Hz(const float hz) noexcept
    {
        m_bandHz[3] = static_cast<double>(hz);
    }

    void setBand5Hz(const float hz) noexcept
    {
        m_bandHz[4] = static_cast<double>(hz);
    }

    void setBand6Hz(const float hz) noexcept
    {
        m_bandHz[5] = static_cast<double>(hz);
    }

    void setBand7Hz(const float hz) noexcept
    {
        m_bandHz[6] = static_cast<double>(hz);
    }

    void setBand8Hz(const float hz) noexcept
    {
        m_bandHz[7] = static_cast<double>(hz);
    }

    void process(const float* const* in, float* const* out, const size_t n) noexcept
    {
        if (m_startPending)
        {
            m_startPending = false;
            startWindow();
        }
        for (size_t i = 0; i < n; ++i)
        {
            const float signal = in[0][i];
            const float reference = in[1][i];
            analyse(signal, reference);
            out[0][i] = signal;
            if (++m_position == m_windowLength)
            {
                finishWindow();
                startWindow();
            }
        }
    }

    [[nodiscard]] float getResultId() const noexcept
    {
        return m_results.resultId;
    }

    [[nodiscard]] float getPeak() const noexcept
    {
        return m_results.peak;
    }

    [[nodiscard]] float getRms() const noexcept
    {
        return m_results.rms;
    }

    [[nodiscard]] float getDc() const noexcept
    {
        return m_results.dc;
    }

    [[nodiscard]] float getCrest() const noexcept
    {
        return m_results.crest;
    }

    [[nodiscard]] float getNonFinite() const noexcept
    {
        return m_results.nonFinite;
    }

    [[nodiscard]] float getDenormals() const noexcept
    {
        return m_results.denormals;
    }

    [[nodiscard]] float getMaxStep() const noexcept
    {
        return m_results.maxStep;
    }

    [[nodiscard]] float getFrequency() const noexcept
    {
        return m_results.frequency;
    }

    [[nodiscard]] float getToneLevel() const noexcept
    {
        return m_results.toneLevel;
    }

    [[nodiscard]] float getToneResidual() const noexcept
    {
        return m_results.toneResidual;
    }

    [[nodiscard]] float getRefRms() const noexcept
    {
        return m_results.refRms;
    }

    [[nodiscard]] float getDiffPeak() const noexcept
    {
        return m_results.diffPeak;
    }

    [[nodiscard]] float getBand1Level() const noexcept
    {
        return m_results.bandLevel[0];
    }

    [[nodiscard]] float getBand1Gain() const noexcept
    {
        return m_results.bandGain[0];
    }

    [[nodiscard]] float getBand1Coherence() const noexcept
    {
        return m_results.bandCoherence[0];
    }

    [[nodiscard]] float getBand2Level() const noexcept
    {
        return m_results.bandLevel[1];
    }

    [[nodiscard]] float getBand2Gain() const noexcept
    {
        return m_results.bandGain[1];
    }

    [[nodiscard]] float getBand2Coherence() const noexcept
    {
        return m_results.bandCoherence[1];
    }

    [[nodiscard]] float getBand3Level() const noexcept
    {
        return m_results.bandLevel[2];
    }

    [[nodiscard]] float getBand3Gain() const noexcept
    {
        return m_results.bandGain[2];
    }

    [[nodiscard]] float getBand3Coherence() const noexcept
    {
        return m_results.bandCoherence[2];
    }

    [[nodiscard]] float getBand4Level() const noexcept
    {
        return m_results.bandLevel[3];
    }

    [[nodiscard]] float getBand4Gain() const noexcept
    {
        return m_results.bandGain[3];
    }

    [[nodiscard]] float getBand4Coherence() const noexcept
    {
        return m_results.bandCoherence[3];
    }

    [[nodiscard]] float getBand5Level() const noexcept
    {
        return m_results.bandLevel[4];
    }

    [[nodiscard]] float getBand5Gain() const noexcept
    {
        return m_results.bandGain[4];
    }

    [[nodiscard]] float getBand5Coherence() const noexcept
    {
        return m_results.bandCoherence[4];
    }

    [[nodiscard]] float getBand6Level() const noexcept
    {
        return m_results.bandLevel[5];
    }

    [[nodiscard]] float getBand6Gain() const noexcept
    {
        return m_results.bandGain[5];
    }

    [[nodiscard]] float getBand6Coherence() const noexcept
    {
        return m_results.bandCoherence[5];
    }

    [[nodiscard]] float getBand7Level() const noexcept
    {
        return m_results.bandLevel[6];
    }

    [[nodiscard]] float getBand7Gain() const noexcept
    {
        return m_results.bandGain[6];
    }

    [[nodiscard]] float getBand7Coherence() const noexcept
    {
        return m_results.bandCoherence[6];
    }

    [[nodiscard]] float getBand8Level() const noexcept
    {
        return m_results.bandLevel[7];
    }

    [[nodiscard]] float getBand8Gain() const noexcept
    {
        return m_results.bandGain[7];
    }

    [[nodiscard]] float getBand8Coherence() const noexcept
    {
        return m_results.bandCoherence[7];
    }

  private:
    struct Results
    {
        float resultId{0.0f};
        float peak{c_floorDb};
        float rms{c_floorDb};
        float dc{0.0f};
        float crest{0.0f};
        float nonFinite{0.0f};
        float denormals{0.0f};
        float maxStep{c_floorDb};
        float frequency{0.0f};
        float toneLevel{c_floorDb};
        float toneResidual{c_floorDb};
        float refRms{c_floorDb};
        float diffPeak{c_floorDb};
        std::array<float, c_bandCount> bandLevel{filled(c_floorDb)};
        std::array<float, c_bandCount> bandGain{filled(c_floorDb)};
        std::array<float, c_bandCount> bandCoherence{};
    };

    static constexpr std::array<float, c_bandCount> filled(const float value) noexcept
    {
        std::array<float, c_bandCount> values{};
        for (auto& v : values)
        {
            v = value;
        }
        return values;
    }

    /// 20 log10(x) clamped to [c_floorDb, high]; the floor for x <= 0.
    static float toDb(const double x, const float high) noexcept
    {
        if (!(x > 0.0))
        {
            return c_floorDb;
        }
        return static_cast<float>(
            std::clamp(20.0 * std::log10(x), static_cast<double>(c_floorDb), static_cast<double>(high)));
    }

    static float clampTo(const double x, const float low, const float high) noexcept
    {
        return static_cast<float>(std::clamp(x, static_cast<double>(low), static_cast<double>(high)));
    }

    void startWindow() noexcept
    {
        m_windowId = m_measure;
        m_windowLength = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(m_windowSeconds * m_sampleRate)));
        m_position = 0;
        m_signal.reset();
        m_reference.reset();
        m_difference.reset();
        m_zeroCrossings = ZeroCrossingFrequency(m_sampleRate, c_zeroCrossingHysteresis);
        m_tone.setFrequency(m_toneHz, m_sampleRate);
        const double highest = 0.49 * m_sampleRate;
        for (size_t k = 0; k < c_bandCount; ++k)
        {
            m_bands[k].setup(std::min(m_bandHz[k], highest), m_sampleRate, c_bandBlockSize);
        }
    }

    void analyse(const float signal, const float reference) noexcept
    {
        m_signal.add(signal);
        m_reference.add(reference);
        const float x = SignalStats::isNonFinite(signal) ? 0.0f : signal;
        const float r = SignalStats::isNonFinite(reference) ? 0.0f : reference;
        m_difference.add(x - r);
        m_zeroCrossings.add(x);
        m_tone.add(x);
        for (auto& band : m_bands)
        {
            band.add(r, x);
        }
    }

    void finishWindow() noexcept
    {
        constexpr float countMax{16777215.0f};
        Results results;
        results.resultId = m_windowId;
        results.peak = toDb(m_signal.peak(), 40.0f);
        results.rms = toDb(m_signal.rms(), 40.0f);
        results.dc = clampTo(m_signal.dc(), -10.0f, 10.0f);
        results.crest = m_signal.crest() > 0.0 ? std::max(toDb(m_signal.crest(), 240.0f), 0.0f) : 0.0f;
        results.nonFinite = clampTo(static_cast<double>(m_signal.nonFinite()), 0.0f, countMax);
        results.denormals = clampTo(static_cast<double>(m_signal.denormals()), 0.0f, countMax);
        results.maxStep = toDb(m_signal.maxDelta(), 40.0f);
        results.frequency = clampTo(m_zeroCrossings.frequency(), 0.0f, 100000.0f);
        const ToneFit::Result tone = m_tone.result();
        results.toneLevel = toDb(tone.amplitude, 40.0f);
        results.toneResidual = toDb(tone.residualRms, 40.0f);
        results.refRms = toDb(m_reference.rms(), 40.0f);
        results.diffPeak = toDb(m_difference.peak(), 40.0f);
        for (size_t k = 0; k < c_bandCount; ++k)
        {
            const TransferBand& band = m_bands[k];
            if (band.blocks() == 0)
            {
                continue; // stays at the floor, coherence 0
            }
            results.bandLevel[k] = toDb(band.outputAmplitude(), 40.0f);
            results.bandGain[k] = toDb(band.gain(), 240.0f);
            results.bandCoherence[k] = clampTo(band.coherence(), 0.0f, 1.0f);
        }
        m_results = results;
    }

    double m_sampleRate{48000.0};
    float m_measure{0.0f};
    bool m_startPending{false};
    double m_windowSeconds{0.5};
    double m_toneHz{1000.0};
    std::array<double, c_bandCount> m_bandHz{63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};

    float m_windowId{0.0f};
    uint64_t m_windowLength{24000};
    uint64_t m_position{0};
    SignalStats m_signal;
    SignalStats m_reference;
    SignalStats m_difference;
    ZeroCrossingFrequency m_zeroCrossings;
    ToneFit m_tone;
    std::array<TransferBand, c_bandCount> m_bands{};

    Results m_results;
};

}
