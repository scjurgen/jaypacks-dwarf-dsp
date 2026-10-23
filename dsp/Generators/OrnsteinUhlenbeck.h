#pragma once

#include <cmath>
#include <cstdint>

#include "dsp/Numbers/Random.h"

namespace jp
{

/**
 * @brief Ornstein-Uhlenbeck process: a random walk pulled back towards a centre,
 * dx = theta (mu - x) dt + sigma_w dW.
 *
 * A plain random walk drifts without bound; the restoring force proportional to the
 * displacement keeps the walk near mu while it stays random. Its power spectrum is flat
 * below the reversion rate theta / (2 pi) and falls by 6 dB per octave above it, and the
 * stationary distribution is Gaussian with mean mu and standard deviation
 * sigma_w / sqrt(2 theta), where sigma_w = sigma / 2.33 is the effective noise scale.
 *
 * theta = 20 sigma + 1 and mu = sigma are derived from sigma, so one control moves amplitude,
 * centre and reversion rate together and the process cannot be set to a state where it stops
 * reverting.
 *
 * Each step uses the exact transition of the process over dt = 1 / stepRate,
 *   x' = mu + (x - mu) e^(-theta dt) + sigma_w sqrt((1 - e^(-2 theta dt)) / (2 theta)) N(0, 1),
 * instead of an Euler-Maruyama step. The two agree when theta dt is small (per-sample
 * stepping), but the Euler step overshoots for theta dt > 1 and diverges for theta dt > 2,
 * which happens when the process is stepped only a few times per second. The exact update is
 * stable and statistically correct for any step rate.
 *
 * Deterministic per seed through jp::Random; real-time safe.
 * @see https://en.wikipedia.org/wiki/Ornstein%E2%80%93Uhlenbeck_process
 */
class OrnsteinUhlenbeck
{
  public:
    /// Ratio between sigma and the standard deviation of the driving noise.
    static constexpr float c_noiseDivisor{2.33f};

    explicit OrnsteinUhlenbeck(const float stepRate, const uint64_t seedValue = Random::c_defaultSeed) noexcept
        : m_random(seedValue)
    {
        setStepRate(stepRate);
    }

    /// Steps per second; dt = 1 / stepRate.
    void setStepRate(const float stepRate) noexcept
    {
        m_dt = 1.0f / stepRate;
        updateTransition();
    }

    void setSigma(const float sigma) noexcept
    {
        m_sigma = sigma;
        m_theta = sigma * 20.0f + 1.0f;
        m_mu = sigma;
        updateTransition();
    }

    [[nodiscard]] float getMu() const noexcept
    {
        return m_mu;
    }

    [[nodiscard]] float getTheta() const noexcept
    {
        return m_theta;
    }

    /// Standard deviation of the stationary distribution, sigma_w / sqrt(2 theta).
    [[nodiscard]] float getStationaryDeviation() const noexcept
    {
        return m_sigma / c_noiseDivisor / std::sqrt(2.0f * m_theta);
    }

    void seed(const uint64_t seedValue) noexcept
    {
        m_random.seed(seedValue);
    }

    /// Advances by dt and returns the new value.
    [[nodiscard]] float step() noexcept
    {
        m_x = m_mu + (m_x - m_mu) * m_decay + m_noiseGain * m_random.gaussian();
        return m_x;
    }

    [[nodiscard]] float getValue() const noexcept
    {
        return m_x;
    }

    /// Sets the current value, e.g. to mu to start in the stationary centre without a transient.
    void setValue(const float value) noexcept
    {
        m_x = value;
    }

    /// Sets the value to 0; the random sequence continues.
    void reset() noexcept
    {
        m_x = 0.0f;
    }

    /// Sets the value to 0 and restarts the random sequence from a seed.
    void reset(const uint64_t seedValue) noexcept
    {
        m_x = 0.0f;
        m_random.seed(seedValue);
    }

  private:
    void updateTransition() noexcept
    {
        m_decay = std::exp(-m_theta * m_dt);
        m_noiseGain = m_sigma / c_noiseDivisor * std::sqrt((1.0f - m_decay * m_decay) / (2.0f * m_theta));
    }

    Random m_random;
    float m_dt{1.0f};
    float m_sigma{0.0f};
    float m_theta{1.0f};
    float m_mu{0.0f};
    float m_decay{0.0f};
    float m_noiseGain{0.0f};
    float m_x{0.0f};
};

}
