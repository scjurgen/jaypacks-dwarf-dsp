#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace jp
{

/**
 * @brief Running statistics of a signal: peak, RMS, DC, crest factor, the largest step
 * between neighbouring samples, and counts of non-finite and denormal samples.
 *
 * The maximum delta is the zipper metric for parameter changes: a control that jumps without
 * smoothing shows up as a step far above what the signal itself does from sample to sample.
 * Non-finite samples are counted and left out of every other statistic. Both non-finite values
 * and denormals are detected from the bit pattern, which holds under any floating-point flags:
 * with plain -ffast-math the compiler may assume std::isfinite() is always true (the Dwarf
 * build adds -fno-finite-math-only, but the check should not depend on that), and the caller
 * may flush denormals to zero in arithmetic. Sums are kept in double. Real-time safe.
 */
class SignalStats
{
  public:
    void reset() noexcept
    {
        *this = SignalStats{};
    }

    void add(const float x) noexcept
    {
        if (isNonFinite(x))
        {
            ++m_nonFinite;
            return;
        }
        if (isDenormal(x))
        {
            ++m_denormals;
        }
        const double value = x;
        m_sum += value;
        m_sumSquares += value * value;
        m_peak = std::max(m_peak, std::abs(value));
        if (m_count > 0)
        {
            m_maxDelta = std::max(m_maxDelta, std::abs(value - m_previous));
        }
        m_previous = value;
        ++m_count;
    }

    void add(const float* x, const size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
        {
            add(x[i]);
        }
    }

    /// Finite samples seen.
    [[nodiscard]] uint64_t count() const noexcept
    {
        return m_count;
    }

    [[nodiscard]] double peak() const noexcept
    {
        return m_peak;
    }

    [[nodiscard]] double rms() const noexcept
    {
        return m_count > 0 ? std::sqrt(m_sumSquares / static_cast<double>(m_count)) : 0.0;
    }

    /// Mean value.
    [[nodiscard]] double dc() const noexcept
    {
        return m_count > 0 ? m_sum / static_cast<double>(m_count) : 0.0;
    }

    /// Peak / RMS; 0 for silence.
    [[nodiscard]] double crest() const noexcept
    {
        const double r = rms();
        return r > 0.0 ? m_peak / r : 0.0;
    }

    /// Largest |x[n] - x[n-1]| between finite neighbours.
    [[nodiscard]] double maxDelta() const noexcept
    {
        return m_maxDelta;
    }

    [[nodiscard]] uint64_t nonFinite() const noexcept
    {
        return m_nonFinite;
    }

    [[nodiscard]] uint64_t denormals() const noexcept
    {
        return m_denormals;
    }

    /// NaN or infinity, from the bit pattern (exponent all ones).
    [[nodiscard]] static bool isNonFinite(const float x) noexcept
    {
        return (bitsOf(x) & 0x7f800000u) == 0x7f800000u;
    }

    [[nodiscard]] static bool isDenormal(const float x) noexcept
    {
        const uint32_t bits = bitsOf(x);
        return (bits & 0x7f800000u) == 0u && (bits & 0x007fffffu) != 0u;
    }

  private:
    [[nodiscard]] static uint32_t bitsOf(const float x) noexcept
    {
        uint32_t bits;
        std::memcpy(&bits, &x, sizeof bits);
        return bits;
    }

    double m_sum{0.0};
    double m_sumSquares{0.0};
    double m_peak{0.0};
    double m_maxDelta{0.0};
    double m_previous{0.0};
    uint64_t m_count{0};
    uint64_t m_nonFinite{0};
    uint64_t m_denormals{0};
};

}
