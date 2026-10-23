#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace jp
{

/**
 * @brief Four cascaded one-pole lowpass stages with fixed mixing weights {1, -4, 12, -16, 8}: eight poles of phase
 * shift summed with the dry signal, the 4-pole "Phaser" mix of the pole-mixing family (Dixon's double notch).
 *
 * The loop input is f = atan(x - r v4) with v4 the output of stage 4 from the previous sample and r four times the
 * resonance value; the atan saturates the input even without resonance (x = 1 leaves at 0.785), and bounds the loop
 * above unity feedback. The output is f - 4 v1 + 12 v2 - 16 v3 + 8 v4. Without resonance the gain is 1 at DC and
 * passes through notches (for a cutoff of 1 kHz at 48 kHz: -8 dB at 300 Hz, -10.6 dB at 3 kHz, -2.8 dB at 8 kHz, small
 * signal); the feedback is negative and takes the gain away at low frequencies (-5 dB at 20 Hz for a resonance value
 * of 0.2) while it raises the response around the cutoff (+1.7 dB at 1 kHz for 0.2).
 *
 * The cutoff is warped onto the frequency the discrete cascade actually responds at (a cubic, fitted by AbacDsp for
 * 44.1, 48, 96, 192 and 384 kHz; every other rate is not corrected) and limited to [10 Hz, 22 kHz]. A cutoff change
 * moves the pole in equal steps over setSmoothingSteps() samples and then sets it exactly; a request equal to the
 * last one is ignored. Zero steps jump at once. Real-time safe, no allocation. The state decays towards denormals on
 * a silent input without resonance: handling is up to the caller.
 * @see https://expeditionelectronics.com/Diy/Polemixing
 */
class Phaser24
{
  public:
    explicit Phaser24(const float sampleRate) noexcept
        : m_sampleRate(sampleRate)
    {
        setCutoff(1000.0f);
        m_stepsLeft = 0;
        m_pole = m_targetPole;
    }

    /// Samples over which a cutoff change moves the pole (0: at once).
    void setSmoothingSteps(const size_t steps) noexcept
    {
        m_smoothingSteps = steps;
    }

    /// Feedback amount; 0 none, the loop gain is four times this value.
    void setResonance(const float value) noexcept
    {
        m_resonance = value * 4.0f;
    }

    void setCutoff(const float cutoffHz) noexcept
    {
        if (cutoffHz == m_lastCutoff)
        {
            return;
        }
        m_lastCutoff = cutoffHz;
        const float warped = std::clamp(warpedCutoff(cutoffHz), 10.0f, 22000.0f);
        m_targetPole = std::exp(-2.0f * 3.14159265358979323846f * warped / m_sampleRate);
        if (m_smoothingSteps == 0)
        {
            m_pole = m_targetPole;
        }
        else
        {
            m_poleStep = (m_targetPole - m_pole) / static_cast<float>(m_smoothingSteps);
        }
        m_stepsLeft = m_smoothingSteps;
    }

    [[nodiscard]] float getPole() const noexcept
    {
        return m_pole;
    }

    void reset() noexcept
    {
        m_v = {};
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        const float feed = std::atan(in - m_v[3] * m_resonance);
        m_v[0] = feed + m_pole * (m_v[0] - feed);
        m_v[1] = m_v[0] + m_pole * (m_v[1] - m_v[0]);
        m_v[2] = m_v[1] + m_pole * (m_v[2] - m_v[1]);
        m_v[3] = m_v[2] + m_pole * (m_v[3] - m_v[2]);
        return feed - 4.0f * m_v[0] + 12.0f * m_v[1] - 16.0f * m_v[2] + 8.0f * m_v[3];
    }

    /// A block (in and out may be the same buffer); a running pole ramp advances one step per sample.
    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        size_t index = 0;
        if (m_stepsLeft > 0)
        {
            const size_t rampSamples = std::min(m_stepsLeft, numSamples);
            m_stepsLeft -= rampSamples;
            for (; index < rampSamples; ++index)
            {
                m_pole += m_poleStep;
                out[index] = process(in[index]);
            }
            if (m_stepsLeft == 0)
            {
                m_pole = m_targetPole;
            }
        }
        for (; index < numSamples; ++index)
        {
            out[index] = process(in[index]);
        }
    }

  private:
    [[nodiscard]] float warpedCutoff(const float x) const noexcept
    {
        if (m_sampleRate == 44100.0f)
        {
            return 0.1070741493f + 1.000163615f * x + -6.77430211e-05f * x * x + 3.441634626e-09f * x * x * x;
        }
        if (m_sampleRate == 48000.0f)
        {
            return 0.1409743683f + 0.9999793344f * x + -6.203395634e-05f * x * x + 2.855230937e-09f * x * x * x;
        }
        if (m_sampleRate == 96000.0f)
        {
            return 0.2635405566f + 1.000099839f * x + -3.105817148e-05f * x * x + 7.1736266e-10f * x * x * x;
        }
        if (m_sampleRate == 192000.0f)
        {
            return -0.01065210969f + 1.001839706f * x + -1.61077305e-05f * x * x + 2.229968241e-10f * x * x * x;
        }
        if (m_sampleRate == 384000.0f)
        {
            return -0.1060540674f + 1.002323759f * x + -8.204298197e-06f * x * x + 6.640562956e-11f * x * x * x;
        }
        return x;
    }

    float m_sampleRate;
    float m_lastCutoff{1.0f};
    float m_targetPole{0.5f};
    float m_pole{0.5f};
    float m_poleStep{0.0f};
    size_t m_stepsLeft{0};
    size_t m_smoothingSteps{0};
    float m_resonance{0.0f};
    std::array<float, 4> m_v{};
};

}
