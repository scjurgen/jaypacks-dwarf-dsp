#pragma once

#include <cstddef>

#include "dsp/Filters/Biquad.h"

namespace jp
{

/**
 * @brief Biquad (FreeCoefficients) whose coefficients move linearly, sample by sample, to a new
 * target over a set number of samples.
 *
 * For filters that are redesigned at control rate: a new design applied at once changes the
 * output instantly by the coefficient change times the state and input (a treble jump of the
 * Fender tone stack, designed every 16 samples, made steps 13 times the steady ones); spread
 * over the samples up to the next design the change is gradual. The interpolation keeps a
 * stable filter stable: the stable (a1, a2) of a second-order section form a triangle and the
 * stable a1 of a first-order section an interval, both convex, so every point between two stable
 * designs is stable. At the end of a ramp the coefficients are exactly the target. A target set
 * during a ramp starts a new ramp from where the coefficients are. Ramp length 0 applies
 * targets at once. Real-time safe.
 */
class RampedBiquad
{
  public:
    void setRampSamples(const size_t samples) noexcept
    {
        m_rampSamples = samples;
    }

    void setTarget(const BiquadSection& target) noexcept
    {
        m_target = target;
        if (m_rampSamples == 0)
        {
            snapToTarget();
            return;
        }
        const BiquadSection& c = m_filter.getCoefficients();
        const float steps = static_cast<float>(m_rampSamples);
        m_increment = BiquadSection{(target.b0 - c.b0) / steps, (target.b1 - c.b1) / steps, (target.b2 - c.b2) / steps,
                                    (target.a1 - c.a1) / steps, (target.a2 - c.a2) / steps};
        m_remaining = m_rampSamples;
    }

    /// Ends a ramp: the coefficients jump to the target.
    void snapToTarget() noexcept
    {
        m_filter.setCoefficients(m_target);
        m_remaining = 0;
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        if (m_remaining > 0)
        {
            --m_remaining;
            if (m_remaining == 0)
            {
                m_filter.setCoefficients(m_target);
            }
            else
            {
                const BiquadSection& c = m_filter.getCoefficients();
                m_filter.setCoefficients(BiquadSection{c.b0 + m_increment.b0, c.b1 + m_increment.b1,
                                                       c.b2 + m_increment.b2, c.a1 + m_increment.a1,
                                                       c.a2 + m_increment.a2});
            }
        }
        return m_filter.process(in);
    }

    /// Clears the state (the coefficients stay as they are).
    void reset() noexcept
    {
        m_filter.reset();
    }

    [[nodiscard]] const BiquadSection& getTarget() const noexcept
    {
        return m_target;
    }

    [[nodiscard]] const BiquadSection& getCurrent() const noexcept
    {
        return m_filter.getCoefficients();
    }

    [[nodiscard]] bool isRamping() const noexcept
    {
        return m_remaining > 0;
    }

  private:
    Biquad<BiquadFilterType::FreeCoefficients> m_filter;
    BiquadSection m_target{};
    BiquadSection m_increment{};
    size_t m_rampSamples{0};
    size_t m_remaining{0};
};

}
