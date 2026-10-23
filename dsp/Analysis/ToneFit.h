#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace jp
{

/**
 * @brief Least-squares fit of DC plus one sine of known frequency to a signal, and what is
 * left over.
 *
 * Fits x[n] ~ d + a cos(w n) + b sin(w n) over all samples since reset() by solving the 3x3
 * normal equations, so the result is exact for any window length (no whole number of periods
 * needed, no window function, no leakage from the tone itself). The residual is the energy the
 * fit does not explain: noise, distortion, other tones, and any change of amplitude or phase
 * during the window (zipper noise from a parameter that steps, modulation from a frequency
 * that moves). The reference oscillator runs as a rotation in double and is renormalised every
 * c_renormaliseInterval samples. Real-time safe.
 */
class ToneFit
{
  public:
    static constexpr uint32_t c_renormaliseInterval{4096};

    ToneFit() noexcept = default;

    ToneFit(const double frequencyHz, const double sampleRate) noexcept
    {
        setFrequency(frequencyHz, sampleRate);
    }

    /// Sets the tone frequency and starts a new fit.
    void setFrequency(const double frequencyHz, const double sampleRate) noexcept
    {
        constexpr double twoPi{6.283185307179586476925286766559};
        const double omega = twoPi * frequencyHz / sampleRate;
        m_rotateCos = std::cos(omega);
        m_rotateSin = std::sin(omega);
        reset();
    }

    void reset() noexcept
    {
        m_cos = 1.0;
        m_sin = 0.0;
        m_gram = {};
        m_projection = {};
        m_energy = 0.0;
        m_count = 0;
    }

    void add(const float sample) noexcept
    {
        const double x = sample;
        const std::array<double, 3> basis{1.0, m_cos, m_sin};
        for (size_t i = 0; i < 3; ++i)
        {
            m_projection[i] += x * basis[i];
            for (size_t j = i; j < 3; ++j)
            {
                m_gram[i * 3 + j] += basis[i] * basis[j];
            }
        }
        m_energy += x * x;
        ++m_count;
        const double c = m_cos * m_rotateCos - m_sin * m_rotateSin;
        m_sin = m_sin * m_rotateCos + m_cos * m_rotateSin;
        m_cos = c;
        if (m_count % c_renormaliseInterval == 0)
        {
            const double norm = 1.0 / std::sqrt(m_cos * m_cos + m_sin * m_sin);
            m_cos *= norm;
            m_sin *= norm;
        }
    }

    void add(const float* x, const size_t n) noexcept
    {
        for (size_t i = 0; i < n; ++i)
        {
            add(x[i]);
        }
    }

    struct Result
    {
        double amplitude{0.0}; ///< of the fitted sine
        double phase{0.0};     ///< x ~ dc + amplitude sin(w n + phase), n counted from reset(), radians
        double dc{0.0};
        double residualRms{0.0}; ///< RMS of x minus the fit
    };

    /// The fit over the samples so far; all zero with fewer than 3 samples or a singular
    /// system (frequency 0 or Nyquist).
    [[nodiscard]] Result result() const noexcept
    {
        Result r;
        const auto& g = m_gram;
        // symmetric G = [[g0 g1 g2] [g1 g4 g5] [g2 g5 g8]] (upper triangle stored)
        const double c00 = g[4] * g[8] - g[5] * g[5];
        const double c01 = g[2] * g[5] - g[1] * g[8];
        const double c02 = g[1] * g[5] - g[2] * g[4];
        const double determinant = g[0] * c00 + g[1] * c01 + g[2] * c02;
        const auto n = static_cast<double>(m_count);
        if (m_count < 3 || std::abs(determinant) < 1e-12 * n * n * n) // det grows with n^3
        {
            return r;
        }
        const double c11 = g[0] * g[8] - g[2] * g[2];
        const double c12 = g[1] * g[2] - g[0] * g[5];
        const double c22 = g[0] * g[4] - g[1] * g[1];
        const auto& p = m_projection;
        const double d = (c00 * p[0] + c01 * p[1] + c02 * p[2]) / determinant;
        const double a = (c01 * p[0] + c11 * p[1] + c12 * p[2]) / determinant;
        const double b = (c02 * p[0] + c12 * p[1] + c22 * p[2]) / determinant;
        r.dc = d;
        r.amplitude = std::sqrt(a * a + b * b);
        r.phase = std::atan2(a, b);
        const double residual = m_energy - (d * p[0] + a * p[1] + b * p[2]);
        r.residualRms = std::sqrt(std::max(residual, 0.0) / static_cast<double>(m_count));
        return r;
    }

    [[nodiscard]] uint64_t count() const noexcept
    {
        return m_count;
    }

  private:
    double m_rotateCos{1.0};
    double m_rotateSin{0.0};
    double m_cos{1.0};
    double m_sin{0.0};
    std::array<double, 9> m_gram{};
    std::array<double, 3> m_projection{};
    double m_energy{0.0};
    uint64_t m_count{0};
};

}
