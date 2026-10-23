#pragma once

#include <cmath>
#include <cstdint>

#include "dsp/Numbers/Random.h"

namespace jp
{

/**
 * @brief Band-limited brown noise: a leaky random walk with a DC-blocking highpass.
 *
 * White noise through a one-pole lowpass (the leaky integration) and a one-pole highpass (which removes
 * the drift), both set by one centre frequency f: the lowpass pole is exp(-2 pi f sqrt(200) / fs), the
 * highpass pole exp(-2 pi f / (sqrt(200) fs)), a fixed ratio of 200, so one control moves the whole
 * passband. The output gain 0.25 sqrt((1 + a) / (1 - a)), a being the lowpass pole, keeps the level roughly
 * constant as f changes. The white input is uniform in [-1, 1) from a seeded jp::Random. No allocation.
 */
class BrownModulation
{
  public:
    explicit BrownModulation(const float sampleRate, const uint64_t seedValue = Random::c_defaultSeed) noexcept
        : m_sampleRate(sampleRate)
        , m_random(seedValue)
    {
        updateCutoffs(1.0f);
    }

    void setCenterFrequency(const float centerFrequency) noexcept
    {
        updateCutoffs(centerFrequency);
    }

    [[nodiscard]] float step() noexcept
    {
        const float white = 2.0f * m_random.uniform() - 1.0f;
        m_brown = white + m_alphaLowpass * (m_brown - white);
        const float output = m_brown - m_dcState;
        m_dcState = m_brown + m_alphaHighpass * (m_dcState - m_brown);
        return m_compensationGain * output;
    }

    void seed(const uint64_t seedValue) noexcept
    {
        m_random.seed(seedValue);
    }

  private:
    static constexpr float c_lowHighRatio{200.0f};

    void updateCutoffs(const float centerFrequency) noexcept
    {
        constexpr float twoPi{6.28318530717958647692f};
        const float sqrtRatio = std::sqrt(c_lowHighRatio);
        const float factor = -twoPi / m_sampleRate * centerFrequency;
        m_alphaLowpass = std::exp(factor * sqrtRatio);
        m_alphaHighpass = std::exp(factor / sqrtRatio);
        m_compensationGain = 0.25f * std::sqrt((1.0f + m_alphaLowpass) / (1.0f - m_alphaLowpass));
    }

    float m_sampleRate;
    float m_alphaLowpass{0.0f};
    float m_alphaHighpass{0.0f};
    float m_compensationGain{1.0f};
    float m_brown{0.0f};
    float m_dcState{0.0f};
    Random m_random;
};

}
