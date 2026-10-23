#pragma once

#include <cstddef>

namespace jp
{

/**
 * @brief Linear ramp from the current value to a target in exactly Steps calls of getSmoothed().
 *
 * setTarget() starts a ramp of Steps equal increments; the value equals the target on the Steps-th call
 * (set exactly, not by accumulation) and the ramp is then inactive. A new target during a ramp restarts it from
 * the current value. forceTarget() jumps at once and cancels a ramp: for a note trigger, where a ramp from the
 * previous voice's leftover value would be audible. The step count is a template argument, so the increment is
 * a compile-time reciprocal; the time per step is whatever rate the caller calls getSmoothed() at.
 * Real-time safe.
 */
template <size_t Steps>
class FixedSmoothing
{
    static_assert(Steps > 0, "a ramp needs at least one step");

  public:
    void setTarget(const float value) noexcept
    {
        m_stepsLeft = Steps;
        m_target = value;
        m_advance = (m_target - m_current) * c_reciprocalSteps;
    }

    void forceTarget(const float value) noexcept
    {
        m_current = value;
        m_stepsLeft = 0;
    }

    [[nodiscard]] float getSmoothed() noexcept
    {
        if (m_stepsLeft != 0)
        {
            --m_stepsLeft;
            m_current = m_stepsLeft == 0 ? m_target : m_current + m_advance;
        }
        return m_current;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return m_stepsLeft != 0;
    }

  private:
    static constexpr float c_reciprocalSteps{1.0f / static_cast<float>(Steps)};

    float m_current{0.0f};
    float m_target{0.0f};
    size_t m_stepsLeft{0};
    float m_advance{0.0f};
};

}
