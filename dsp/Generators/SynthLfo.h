#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/Generators/BrownModulation.h"
#include "dsp/Numbers/Random.h"

namespace jp
{

enum class LfoType
{
    Sine,
    Triangle,
    Saw,
    Square,
    Noise,
    SampleHoldNoise,
    SampleHoldFlipFlop,
    BrownNoise
};

/**
 * @brief Naive (not band-limited) low-frequency oscillator with eight waveforms, stepped at a rate the caller
 * chooses.
 *
 * step() is one tick: the phase advances by frequency / sampleRate, where sampleRate is the rate step() is
 * called at (a control rate need not be the audio rate). The phase is a double, so the rate is exact. Output in
 * [-1, 1]: Sine, Triangle (starting at -1 and rising), Saw (rising), Square (+1 for the first half). Noise is white,
 * a new value at every tick. SampleHoldNoise and SampleHoldFlipFlop hold a new uniform value for half a cycle (two
 * values per cycle); the flip-flop is the same signal, its flip counter is never read. BrownNoise is
 * BrownModulation centred on the frequency. Band limiting is not needed: an LFO is always smoothed or filtered
 * before it can alias. A frequency of 0 or less stops the oscillator, which then repeats its last value;
 * setting a positive frequency (also the same one) restarts the held value at 0 but does not reset the phase. The
 * frequency is limited to half the tick rate.
 * The noise comes from a seeded jp::Random. No allocation.
 */
class LfoGenerators
{
  public:
    explicit LfoGenerators(const float sampleRate, const uint64_t seed = Random::c_defaultSeed) noexcept
        : m_invSampleRate(1.0 / static_cast<double>(sampleRate))
        , m_random(seed)
        , m_brown(sampleRate, seed ^ 0x42524f574eULL)
    {
    }

    void setWaveForm(const LfoType type) noexcept
    {
        m_type = type;
    }

    void setFrequency(const float frequency) noexcept
    {
        if (frequency > 0.0f)
        {
            m_advance = std::min(static_cast<double>(frequency) * m_invSampleRate, 0.5);
            m_brown.setCenterFrequency(frequency);
            m_playing = true;
            m_lastValue = 0.0f;
        }
        else
        {
            m_playing = false;
        }
    }

    /// Sets the phase in degrees, wrapped into 0 to 360 (negative values and values above 360 land on their
    /// in-range equivalent); a value that is not finite is ignored.
    void setPhase(const float degrees) noexcept
    {
        if (!std::isfinite(degrees))
        {
            return;
        }
        double wrapped = std::fmod(static_cast<double>(degrees), 360.0);
        wrapped += wrapped < 0.0 ? 360.0 : 0.0;
        m_phase = wrapped / 360.0;
        m_phase = m_phase >= 1.0 ? 0.0 : m_phase;
    }

    [[nodiscard]] float step() noexcept
    {
        if (!m_playing)
        {
            return m_lastValue;
        }
        m_phase += m_advance;
        m_phase -= m_phase >= 1.0 ? 1.0 : 0.0;
        const auto phase = static_cast<float>(m_phase);
        switch (m_type)
        {
            case LfoType::Sine:
                m_lastValue = std::sin(phase * 6.28318530717958647692f);
                break;
            case LfoType::Triangle:
                m_lastValue = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
                break;
            case LfoType::Saw:
                m_lastValue = 2.0f * phase - 1.0f;
                break;
            case LfoType::Square:
                m_lastValue = phase < 0.5f ? 1.0f : -1.0f;
                break;
            case LfoType::Noise:
                m_lastValue = uniformSigned();
                break;
            case LfoType::SampleHoldNoise:
            case LfoType::SampleHoldFlipFlop:
                holdNoise(2.0);
                break;
            case LfoType::BrownNoise:
                m_lastValue = m_brown.step();
                break;
        }
        return m_lastValue;
    }

    void play() noexcept
    {
        m_playing = true;
    }

    void pause() noexcept
    {
        m_playing = false;
    }

    /// Stops and resets the phase and the held value to 0.
    void stop() noexcept
    {
        m_playing = false;
        m_phase = 0.0;
        m_lastValue = 0.0f;
        m_noisePhase = 0.0;
    }

  private:
    [[nodiscard]] float uniformSigned() noexcept
    {
        return 2.0f * m_random.uniform() - 1.0f;
    }

    void holdNoise(const double valuesPerCycle) noexcept
    {
        m_noisePhase += m_advance * valuesPerCycle;
        while (m_noisePhase >= 1.0)
        {
            m_noisePhase -= 1.0;
            m_lastValue = uniformSigned();
        }
    }

    LfoType m_type{LfoType::Sine};
    double m_invSampleRate;
    double m_advance{0.009167};
    double m_phase{0.0};
    double m_noisePhase{0.0};
    float m_lastValue{0.0f};
    bool m_playing{true};
    Random m_random;
    BrownModulation m_brown;
};

/**
 * @brief LfoGenerators with a one-pole lowpass over the output whose corner follows the speed.
 *
 * The corner lies at r f, f being the speed and r = 5 + 10 log10(10 f) / 3 the ratio: 5 at 0.1 Hz, 15 at 100 Hz,
 * on the same line outside that (f clamped to 0.01 Hz up to a quarter of the tick rate). A sine passes almost
 * unchanged, a square keeps its edges a few ticks wide, and the stair steps of a slow LFO, or of a noise
 * waveform, are rounded off. The pole is exp(-2 pi r f / fs), fs being the tick rate.
 */
class SynthLfo : public LfoGenerators
{
  public:
    explicit SynthLfo(const float sampleRate, const uint64_t seed = Random::c_defaultSeed) noexcept
        : LfoGenerators(sampleRate, seed)
        , m_sampleRate(sampleRate)
    {
    }

    void setSpeed(const float hz) noexcept
    {
        setFrequency(hz);
        constexpr float ratioLow = 5.0f;
        constexpr float ratioHigh = 15.0f;
        const float clamped = std::clamp(hz, 0.01f, m_sampleRate / 4.0f);
        const float ratio = ratioLow + (ratioHigh - ratioLow) * std::log10(clamped * 10.0f) / std::log10(1000.0f);
        m_lowpassPole = std::exp(-6.28318530717958647692f * clamped * ratio / m_sampleRate);
    }

    [[nodiscard]] float getValue() noexcept
    {
        const float raw = step();
        m_smoothed = raw + m_lowpassPole * (m_smoothed - raw);
        return m_smoothed;
    }

  private:
    float m_sampleRate;
    float m_lowpassPole{0.9f};
    float m_smoothed{0.0f};
};

}
