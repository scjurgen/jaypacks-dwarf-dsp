#pragma once

#include <complex>

#include "dsp/Filters/OnePoleLowpass.h"

namespace jp
{

/// Transfer function H(z) = (1 + p) / 2 (1 - z^-1) / (1 - p z^-1) evaluated at a frequency, for
/// analysis and tests. Not used in the audio path.
[[nodiscard]] inline std::complex<double> onePoleHighpassResponse(const double pole, const double frequencyHz,
                                                                  const double sampleRate) noexcept
{
    constexpr double twoPi{6.283185307179586476925286766559};
    const std::complex<double> zInv = std::polar(1.0, -twoPi * frequencyHz / sampleRate);
    return 0.5 * (1.0 + pole) * (1.0 - zInv) / (1.0 - pole * zInv);
}

/**
 * @brief One-pole, one-zero highpass, y[n] = (1 + p) / 2 (x[n] - x[n-1]) + p y[n-1].
 *
 * Zero at DC, 6 dB per octave below the cutoff, and exactly unity gain at Nyquist (the
 * (1 + p) / 2 factor). The pole is the lowpass pole of onePoleLowpassPole() (matched z,
 * p = exp(-2 pi fc / fs)): the gain at fc is -3.01 dB within 0.005 dB up to fc = 500 Hz at
 * 48 kHz, and the -3 dB point moves below fc as fc / fs grows. Two floats of state; real-time safe. Unlike x -
 * lowpass(x), the Nyquist gain does not fall below unity at high cutoffs.
 * @see https://ccrma.stanford.edu/~jos/filters/DC_Blocker.html
 */
class OnePoleHighpass
{
  public:
    void setPole(const float pole) noexcept
    {
        m_pole = pole;
        m_gain = 0.5f * (1.0f + pole);
    }

    [[nodiscard]] float getPole() const noexcept
    {
        return m_pole;
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        m_state = m_gain * (in - m_previousIn) + m_pole * m_state;
        m_previousIn = in;
        return m_state;
    }

    void reset() noexcept
    {
        m_state = 0.0f;
        m_previousIn = 0.0f;
    }

  private:
    float m_pole{0.0f};
    float m_gain{0.5f};
    float m_state{0.0f};
    float m_previousIn{0.0f};
};

}
