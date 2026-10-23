#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jp
{

/**
 * @brief The base trajectory of a filter frequency: holds a value and glides linearly to a new target in a number of
 * samples, per sample and without dependence on how the audio is split into blocks.
 *
 * The unit is the caller's: the value is linear in whatever it is made of, so a glide is linear in semitones (or in
 * octaves, or in a normalised position that is mapped exponentially onto a frequency) and never in Hz. Modulation is
 * added by the caller to value() and is not part of the trajectory, so a new target never starts from, or folds in,
 * a modulated value.
 *
 * retarget() starts from the present value, not from the previous destination, so an interrupted glide continues
 * without a jump. A glide of n >= 1 samples makes the n-th call of next() return exactly the target: the value is
 * measured back from the target (target - increment * remaining), so float steps cannot accumulate into a jump at the
 * end. A glide of 0 samples (or less) puts the target in place at once. Real-time safe, no state outside the object.
 */
class CutoffTrajectory
{
  public:
    /// Puts the value in place and ends any glide.
    void setValue(const float value) noexcept
    {
        m_value = value;
        m_target = value;
        m_increment = 0.0f;
        m_remaining = 0;
    }

    /// Glides from the present value to target in `samples` samples; zero or fewer samples jump at once.
    void retarget(const float target, const int64_t samples) noexcept
    {
        m_target = target;
        if (samples <= 0)
        {
            m_value = target;
            m_increment = 0.0f;
            m_remaining = 0;
            return;
        }
        m_remaining = samples;
        m_increment = (target - m_value) / static_cast<float>(samples);
    }

    /// Advances one sample and returns the new value.
    float next() noexcept
    {
        if (m_remaining > 0)
        {
            --m_remaining;
            m_value = m_target - m_increment * static_cast<float>(m_remaining);
        }
        return m_value;
    }

    [[nodiscard]] float value() const noexcept
    {
        return m_value;
    }

    [[nodiscard]] float target() const noexcept
    {
        return m_target;
    }

    [[nodiscard]] bool isGliding() const noexcept
    {
        return m_remaining > 0;
    }

    [[nodiscard]] int64_t remaining() const noexcept
    {
        return m_remaining;
    }

  private:
    float m_value{0.0f};
    float m_target{0.0f};
    float m_increment{0.0f};
    int64_t m_remaining{0};
};

}
