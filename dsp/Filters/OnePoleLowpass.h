#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace jp
{

/// Pole of the one-pole lowpass for a cutoff, p = exp(-2 pi fc / fs) (impulse-invariant
/// mapping). Negative cutoffs are treated as 0 (p = 1, the stage holds its value).
[[nodiscard]] inline float onePoleLowpassPole(const float cutoffHz, const float sampleRate) noexcept
{
    constexpr float twoPi{6.28318530717958647692f};
    return std::exp(-twoPi * std::max(cutoffHz, 0.0f) / sampleRate);
}

/// Transfer function H(z) = (1 - p) / (1 - p z^-1) evaluated at a frequency, for analysis
/// and tests. Not used in the audio path.
[[nodiscard]] inline std::complex<double> onePoleLowpassResponse(const double pole, const double frequencyHz,
                                                                 const double sampleRate) noexcept
{
    constexpr double twoPi{6.283185307179586476925286766559};
    const std::complex<double> zInv = std::polar(1.0, -twoPi * frequencyHz / sampleRate);
    return (1.0 - pole) / (1.0 - pole * zInv);
}

/**
 * @brief One-pole (first-order IIR) lowpass, y[n] = x[n] + p (y[n-1] - x[n]).
 *
 * Unity gain at DC, 6 dB per octave above the cutoff. The pole comes from the
 * impulse-invariant mapping p = exp(-2 pi fc / fs): cheap and exact at low cutoffs, but the
 * -3 dB point moves above fc as fc approaches Nyquist (+1.3 % at fs / 16, +5.6 % at fs / 8,
 * +34 % at fs / 4), and the gain at Nyquist is (1 - p) / (1 + p), not zero. The pole is set directly so several
 * stages can share one exp() call.
 *
 * processClamped() limits the state after each step. Inside a cascade with feedback this
 * acts as a hard limiter on every stage: it bounds the output for any resonance and adds
 * the stage clipping that is part of the sound; it is not a transparent safety net.
 * @see https://ccrma.stanford.edu/~jos/filters/One_Pole.html
 */
class OnePoleLowpass
{
  public:
    void setPole(const float pole) noexcept
    {
        m_pole = pole;
    }

    [[nodiscard]] float getPole() const noexcept
    {
        return m_pole;
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        m_state = in + m_pole * (m_state - in);
        return m_state;
    }

    /// As process(), with the state limited to [-limit, limit] after the step.
    [[nodiscard]] float processClamped(const float in, const float limit) noexcept
    {
        m_state = std::clamp(in + m_pole * (m_state - in), -limit, limit);
        return m_state;
    }

    void reset() noexcept
    {
        m_state = 0.0f;
    }

  private:
    float m_pole{0.0f};
    float m_state{0.0f};
};

}
