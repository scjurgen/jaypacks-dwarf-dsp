#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/Numbers/Random.h"

namespace jp
{

/**
 * @brief Test signal source: silence, sine, square, saw, white noise, impulses, log sweep and
 * step, at a peak level in dBFS, reproducible sample for sample.
 *
 * Every waveform is a function of a sample count since the last restart, a phase accumulator
 * in double (frequency / fs per sample) and a seeded Random, so the output does not depend on
 * how the host splits the blocks, and a restart (a new value on the restart port, or reset())
 * repeats it exactly: phase 0, sweep at its start, noise from the same seed. Waveforms
 * at phase p in [0, 1):
 *   sine      level sin(2 pi p), starting at 0 and rising
 *   square    +level for p < 0.5, -level after; saw 2 level p - level (neither band limited)
 *   noise     level (2 u - 1), u uniform from Random (2^24 steps)
 *   impulses  level on the first sample of every period, 0 elsewhere
 *   sweep     sine whose frequency moves exponentially from frequency to sweepEnd in sweepTime,
 *             f(k) = f0 (f1 / f0)^(k / N) for sample k of N, then starts again at phase 0
 *   step      0 for one period of the frequency, then level
 * A frequency change keeps the phase, so tones change without a jump; level and on/off take
 * effect at the next sample without smoothing (a test source must not add its own ramps).
 * Off writes silence while the generator keeps running. rms reports the output RMS of the last
 * complete second in dBFS (floor c_floorDb, which it also shows until the first second ends).
 * Real-time safe: no allocation; one exp per sample only while sweeping.
 */
class SigGenPedal
{
  public:
    enum class Waveform
    {
        Silence,
        Sine,
        Square,
        Saw,
        WhiteNoise,
        Impulses,
        LogSweep,
        Step,
    };
    static constexpr size_t c_waveformCount{8};
    static constexpr uint64_t c_seed{0x7369676e616c3031ULL};
    static constexpr float c_floorDb{-240.0f};

    void setSampleRate(const float sampleRate) noexcept
    {
        m_sampleRate = static_cast<double>(sampleRate);
        m_rmsLength = static_cast<uint64_t>(std::lround(m_sampleRate));
        updateIncrement();
        updateSweep();
    }

    void reset() noexcept
    {
        restart();
        m_rmsSum = 0.0;
        m_rmsCount = 0;
        m_rmsDb = c_floorDb;
    }

    void setWaveform(const float value) noexcept
    {
        const auto index = static_cast<size_t>(std::clamp(value, 0.0f, static_cast<float>(c_waveformCount - 1)));
        m_waveform = static_cast<Waveform>(index);
    }

    void setFrequency(const float hz) noexcept
    {
        m_frequency = static_cast<double>(hz);
        updateIncrement();
        updateSweep();
    }

    void setLevel(const float db) noexcept
    {
        m_level = std::pow(10.0, static_cast<double>(db) / 20.0);
    }

    void setSweepEnd(const float hz) noexcept
    {
        m_sweepEnd = static_cast<double>(hz);
        updateSweep();
    }

    void setSweepTime(const float seconds) noexcept
    {
        m_sweepTime = static_cast<double>(seconds);
        updateSweep();
    }

    void setOn(const float value) noexcept
    {
        m_on = value >= 0.5f;
    }

    /// Any value different from the last one applied restarts the waveform.
    void setRestart(const float value) noexcept
    {
        if (value != m_restartId)
        {
            m_restartId = value;
            restart();
        }
    }

    void process(float* out, const size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
        {
            const double generated = next();
            const double x = m_on ? generated : 0.0;
            out[i] = static_cast<float>(x);
            m_rmsSum += x * x;
            if (++m_rmsCount == m_rmsLength)
            {
                const double meanSquare = m_rmsSum / static_cast<double>(m_rmsLength);
                m_rmsDb = meanSquare > 0.0 ? std::max(static_cast<float>(10.0 * std::log10(meanSquare)), c_floorDb)
                                           : c_floorDb;
                m_rmsSum = 0.0;
                m_rmsCount = 0;
            }
        }
    }

    [[nodiscard]] float getRestartId() const noexcept
    {
        return m_restartId;
    }

    [[nodiscard]] float getRms() const noexcept
    {
        return m_rmsDb;
    }

  private:
    void restart() noexcept
    {
        m_phase = 0.0;
        m_count = 0;
        m_random.seed(c_seed);
    }

    void updateIncrement() noexcept
    {
        m_increment = m_frequency / m_sampleRate;
    }

    void updateSweep() noexcept
    {
        m_sweepLength = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(m_sweepTime * m_sampleRate)));
        m_sweepLogRatio = std::log(m_sweepEnd / m_frequency) / static_cast<double>(m_sweepLength);
    }

    /// Next sample at full level, advancing count and phase.
    double next() noexcept
    {
        constexpr double twoPi{6.283185307179586476925286766559};
        double x = 0.0;
        double increment = m_increment;
        switch (m_waveform)
        {
            case Waveform::Silence:
                break;
            case Waveform::Sine:
                x = std::sin(twoPi * m_phase);
                break;
            case Waveform::Square:
                x = m_phase < 0.5 ? 1.0 : -1.0;
                break;
            case Waveform::Saw:
                x = 2.0 * m_phase - 1.0;
                break;
            case Waveform::WhiteNoise:
                x = 2.0 * static_cast<double>(m_random.uniform()) - 1.0;
                break;
            case Waveform::Impulses:
                x = m_phase < m_increment ? 1.0 : 0.0;
                break;
            case Waveform::LogSweep:
                if (m_count >= m_sweepLength)
                {
                    m_count = 0;
                    m_phase = 0.0;
                }
                x = std::sin(twoPi * m_phase);
                increment = m_increment * std::exp(m_sweepLogRatio * static_cast<double>(m_count));
                break;
            case Waveform::Step:
                x = static_cast<double>(m_count) * m_increment >= 1.0 ? 1.0 : 0.0;
                break;
        }
        ++m_count;
        m_phase += increment;
        m_phase -= std::floor(m_phase);
        return m_level * x;
    }

    double m_sampleRate{48000.0};
    Waveform m_waveform{Waveform::LogSweep};
    double m_frequency{1000.0};
    double m_level{0.1};
    double m_sweepEnd{20000.0};
    double m_sweepTime{5.0};
    bool m_on{true};
    float m_restartId{0.0f};

    double m_increment{1000.0 / 48000.0};
    uint64_t m_sweepLength{240000};
    double m_sweepLogRatio{0.0};
    double m_phase{0.0};
    uint64_t m_count{0};
    Random m_random{c_seed};

    uint64_t m_rmsLength{48000};
    double m_rmsSum{0.0};
    uint64_t m_rmsCount{0};
    float m_rmsDb{c_floorDb};
};

}
