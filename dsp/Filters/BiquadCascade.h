#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

#include "dsp/Filters/BilinearTransform.h"
#include "dsp/Filters/Biquad.h"

namespace jp
{

/// One designed section of a cascade: a cookbook response (BiquadCoefficients) with its
/// frequency, Q and gain in dB (read only by Peak and the shelves).
struct BiquadDesign
{
    BiquadFilterType type;
    float frequency;
    float q;
    float gainDb;
};

/// Coefficients of a designed section at a sample rate.
[[nodiscard]] inline BiquadSection designBiquad(const BiquadDesign& d, const float sampleRate)
{
    BiquadCoefficients coefficients;
    coefficients.coefficients(d.type, sampleRate, d.frequency, d.q, d.gainDb);
    return coefficients.getCoefficients();
}

/**
 * @brief Fixed chain of N biquad sections in series, for a fixed response such as a speaker.
 *
 * Every section runs the generic transposed Direct Form II step, so a chain can mix designed
 * cookbook sections (designBiquad()) with first-order or hand-made ones. The sections are set
 * once outside the audio path; the response is the product of the sections, its magnitude in
 * dB the sum. Real-time safe, 2 N floats of state.
 */
template <size_t N>
class BiquadCascade
{
  public:
    void setSection(const size_t index, const BiquadSection& section) noexcept
    {
        m_sections[index].setCoefficients(section);
    }

    [[nodiscard]] const BiquadSection& getSection(const size_t index) const noexcept
    {
        return m_sections[index].getCoefficients();
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        float x = in;
        for (auto& section : m_sections)
        {
            x = section.process(x);
        }
        return x;
    }

    void reset() noexcept
    {
        for (auto& section : m_sections)
        {
            section.reset();
        }
    }

    /// Response of the chain (float coefficients, evaluated in double), for analysis and tests.
    [[nodiscard]] std::complex<double> response(const double frequencyHz, const double sampleRate) const
    {
        std::complex<double> h{1.0, 0.0};
        for (const auto& section : m_sections)
        {
            h *= biquadSectionResponse(section.getCoefficients(), frequencyHz, sampleRate);
        }
        return h;
    }

  private:
    std::array<Biquad<BiquadFilterType::FreeCoefficients>, N> m_sections{};
};

}
