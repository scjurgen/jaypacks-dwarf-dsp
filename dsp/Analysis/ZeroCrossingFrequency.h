#pragma once

#include <cstddef>
#include <cstdint>

namespace jp
{

/**
 * @brief Frequency of a periodic signal from its upward zero crossings.
 *
 * A crossing counts when the signal rises through zero after it has been below -hysteresis,
 * so noise around zero gives one crossing per period, not several. Crossing times are
 * interpolated linearly between the two samples, which makes the estimate far finer than
 * the sample grid: (crossings - 1) periods over the time from the first to the last crossing.
 * Meant for signals with one dominant frequency (a sine, a ringing resonance); harmonics
 * strong enough to add zero crossings make it read too high. Real-time safe.
 */
class ZeroCrossingFrequency
{
  public:
    ZeroCrossingFrequency() noexcept = default;

    ZeroCrossingFrequency(const double sampleRate, const float hysteresis) noexcept
        : m_sampleRate(sampleRate)
        , m_hysteresis(hysteresis)
    {
    }

    void reset() noexcept
    {
        m_previous = 0.0f;
        m_armed = false;
        m_index = 0;
        m_crossings = 0;
        m_first = 0.0;
        m_last = 0.0;
    }

    void add(const float x) noexcept
    {
        if (x < -m_hysteresis)
        {
            m_armed = true;
        }
        else if (m_armed && m_previous < 0.0f && x >= 0.0f)
        {
            const double fraction = static_cast<double>(m_previous) / static_cast<double>(m_previous - x);
            const double time = static_cast<double>(m_index - 1) + fraction;
            if (m_crossings == 0)
            {
                m_first = time;
            }
            m_last = time;
            ++m_crossings;
            m_armed = false;
        }
        m_previous = x;
        ++m_index;
    }

    void add(const float* x, const size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
        {
            add(x[i]);
        }
    }

    [[nodiscard]] uint64_t crossings() const noexcept
    {
        return m_crossings;
    }

    /// Hz; 0 with fewer than two crossings.
    [[nodiscard]] double frequency() const noexcept
    {
        if (m_crossings < 2 || m_last <= m_first)
        {
            return 0.0;
        }
        return static_cast<double>(m_crossings - 1) * m_sampleRate / (m_last - m_first);
    }

  private:
    double m_sampleRate{48000.0};
    float m_hysteresis{0.0f};
    float m_previous{0.0f};
    bool m_armed{false};
    uint64_t m_index{0};
    uint64_t m_crossings{0};
    double m_first{0.0};
    double m_last{0.0};
};

}
