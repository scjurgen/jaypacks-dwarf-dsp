#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace jp
{

/// Biquad magnitude in dB at the normalised frequency cf = f / fs, valid over [0, 0.5].
/// Evaluated through the substitution phi = 4 sin^2(pi cf), so no complex arithmetic is needed.
[[nodiscard]] inline float biquadMagnitudeInDb(const float cf, const float b0, const float b1, const float b2,
                                               const float a1, const float a2)
{
    constexpr float pi{3.14159265358979323846f};
    const float sine = std::sin(pi * cf);
    const float phi = 4.0f * sine * sine;
    const float bSum = b0 + b1 + b2;
    const float aSum = 1.0f + a1 + a2;
    return 10.0f * std::log10(bSum * bSum + (b0 * b2 * phi - (b1 * (b0 + b2) + 4.0f * b0 * b2)) * phi) -
           10.0f * std::log10(aSum * aSum + (a2 * phi - (a1 * (1.0f + a2) + 4.0f * a2)) * phi);
}

/// Biquad magnitude as a linear ratio at cf = f / fs. Accumulates in double, so it stays usable
/// in deep notches where the float dB path loses digits.
[[nodiscard]] inline float biquadMagnitudeLinear(const float cf, const float b0, const float b1, const float b2,
                                                 const float a1, const float a2)
{
    constexpr double pi{3.14159265358979323846};
    const double sine = std::sin(pi * static_cast<double>(cf));
    const double phi = 4.0 * sine * sine;
    const auto db0 = static_cast<double>(b0);
    const auto db1 = static_cast<double>(b1);
    const auto db2 = static_cast<double>(b2);
    const auto da1 = static_cast<double>(a1);
    const auto da2 = static_cast<double>(a2);
    const double bSum = db0 + db1 + db2;
    const double numerator = bSum * bSum + (db0 * db2 * phi - (db1 * (db0 + db2) + 4.0 * db0 * db2)) * phi;
    const double aSum = 1.0 + da1 + da2;
    const double denominator = aSum * aSum + (da2 * phi - (da1 * (1.0 + da2) + 4.0 * da2)) * phi;
    return static_cast<float>(std::sqrt(numerator / denominator));
}

/// biquadMagnitudeInDb() as a linear ratio, 10^(dB / 20). biquadMagnitudeLinear() computes the same
/// ratio directly and is preferred near deep notches.
[[nodiscard]] inline float biquadMagnitude(const float cf, const float b0, const float b1, const float b2,
                                           const float a1, const float a2)
{
    return std::pow(10.0f, biquadMagnitudeInDb(cf, b0, b1, b2, a1, a2) / 20.0f);
}

/**
 * @brief Response shape of a biquad section, fixed at compile time by Biquad.
 *
 * LowPass, HighPass, BandPass, Notch and Peak are designed from the prewarped
 * K = tan(pi f / fs); AllPass, LoShelf and HiShelf use the cookbook alpha = sin(w0) / (2 Q)
 * form. Both are bilinear designs, only the algebra differs. FreeCoefficients is not designed:
 * its coefficients are set directly.
 */
enum class BiquadFilterType
{
    LowPass,
    HighPass,
    BandPass,
    Notch,
    Peak,
    LoShelf,
    HiShelf,
    AllPass,
    FreeCoefficients,
};

/// The five coefficients of one section, already divided by a0:
/// H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2).
struct BiquadSection
{
    float b0{1.0f};
    float b1{0.0f};
    float b2{0.0f};
    float a1{0.0f};
    float a2{0.0f};
};

/**
 * @brief Coefficient design and frequency-response evaluation for one biquad section.
 *
 * Coefficients are divided by a0 at design time, so the difference equation never divides.
 * The frequency is clamped to [1 Hz, 0.49 fs] before the prewarp (at fs / 2 the prewarp
 * tan(pi / 2) is infinite). Q is the analog Q, Butterworth at 1 / sqrt(2). peakGain is in dB
 * and is read only by Peak, LoShelf and HiShelf; a Peak cut of -x dB is the exact inverse of a
 * boost of +x dB (the gain factor moves from the numerator to the denominator).
 * FreeCoefficients designs to pass-through.
 * @see https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
 */
class BiquadCoefficients
{
  public:
    void coefficients(const BiquadFilterType type, const float sampleRate, const float frequency, const float q,
                      const float peakGain)
    {
        constexpr float pi{3.14159265358979323846f};
        const float f = std::clamp(frequency, 1.0f, 0.49f * sampleRate);
        const float k = std::tan(pi * f / sampleRate);
        const float kSquare = k * k;
        float norm = 1.0f / (1.0f + k / q + kSquare);
        switch (type)
        {
            case BiquadFilterType::LowPass:
                m_c.b0 = kSquare * norm;
                m_c.b1 = 2.0f * m_c.b0;
                m_c.b2 = m_c.b0;
                m_c.a1 = 2.0f * (kSquare - 1.0f) * norm;
                m_c.a2 = (1.0f - k / q + kSquare) * norm;
                break;
            case BiquadFilterType::HighPass:
                m_c.b0 = norm;
                m_c.b1 = -2.0f * m_c.b0;
                m_c.b2 = m_c.b0;
                m_c.a1 = 2.0f * (kSquare - 1.0f) * norm;
                m_c.a2 = (1.0f - k / q + kSquare) * norm;
                break;
            case BiquadFilterType::BandPass:
                m_c.b0 = k / q * norm;
                m_c.b1 = 0.0f;
                m_c.b2 = -m_c.b0;
                m_c.a1 = 2.0f * (kSquare - 1.0f) * norm;
                m_c.a2 = (1.0f - k / q + kSquare) * norm;
                break;
            case BiquadFilterType::Notch:
                m_c.b0 = (1.0f + kSquare) * norm;
                m_c.b2 = m_c.b0;
                m_c.b1 = 2.0f * (kSquare - 1.0f) * norm;
                m_c.a1 = m_c.b1;
                m_c.a2 = (1.0f - k / q + kSquare) * norm;
                break;
            case BiquadFilterType::Peak:
            {
                float v1 = 1.0f;
                float v = std::pow(10.0f, std::abs(peakGain) / 20.0f);
                if (peakGain < 0.0f)
                {
                    std::swap(v, v1);
                }
                norm = 1.0f / (1.0f + v1 / q * k + kSquare);
                m_c.b0 = (1.0f + v / q * k + kSquare) * norm;
                m_c.b2 = (1.0f - v / q * k + kSquare) * norm;
                m_c.a2 = (1.0f - v1 / q * k + kSquare) * norm;
                m_c.b1 = 2.0f * (kSquare - 1.0f) * norm;
                m_c.a1 = m_c.b1;
                break;
            }
            case BiquadFilterType::LoShelf:
            {
                const float a = std::pow(10.0f, peakGain / 40.0f);
                const float sqrtA = std::sqrt(a);
                const float w0 = 2.0f * pi * f / sampleRate;
                const float cosW0 = std::cos(w0);
                const float alpha = std::sin(w0) / (2.0f * q);
                const float a0 = (a + 1.0f) + (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
                m_c.a1 = (-2.0f * ((a - 1.0f) + (a + 1.0f) * cosW0)) / a0;
                m_c.a2 = ((a + 1.0f) + (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha) / a0;
                m_c.b0 = (a * ((a + 1.0f) - (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha)) / a0;
                m_c.b1 = (2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosW0)) / a0;
                m_c.b2 = (a * ((a + 1.0f) - (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha)) / a0;
                break;
            }
            case BiquadFilterType::HiShelf:
            {
                const float a = std::pow(10.0f, peakGain / 40.0f);
                const float sqrtA = std::sqrt(a);
                const float w0 = 2.0f * pi * f / sampleRate;
                const float cosW0 = std::cos(w0);
                const float alpha = std::sin(w0) / (2.0f * q);
                const float a0 = (a + 1.0f) - (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha;
                m_c.a1 = (2.0f * ((a - 1.0f) - (a + 1.0f) * cosW0)) / a0;
                m_c.a2 = ((a + 1.0f) - (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha) / a0;
                m_c.b0 = (a * ((a + 1.0f) + (a - 1.0f) * cosW0 + 2.0f * sqrtA * alpha)) / a0;
                m_c.b1 = (-2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosW0)) / a0;
                m_c.b2 = (a * ((a + 1.0f) + (a - 1.0f) * cosW0 - 2.0f * sqrtA * alpha)) / a0;
                break;
            }
            case BiquadFilterType::AllPass:
            {
                const float w0 = 2.0f * pi * f / sampleRate;
                const float cosW0 = std::cos(w0);
                const float alpha = std::sin(w0) / (2.0f * q);
                const float a0 = 1.0f + alpha;
                m_c.b0 = (1.0f - alpha) / a0;
                m_c.a2 = m_c.b0;
                m_c.b1 = (-2.0f * cosW0) / a0;
                m_c.a1 = m_c.b1;
                m_c.b2 = (1.0f + alpha) / a0;
                break;
            }
            case BiquadFilterType::FreeCoefficients:
                m_c = BiquadSection{};
                break;
        }
    }

    [[nodiscard]] const BiquadSection& getCoefficients() const noexcept
    {
        return m_c;
    }

    [[nodiscard]] float magnitudeInDb(const float cf) const
    {
        return biquadMagnitudeInDb(cf, m_c.b0, m_c.b1, m_c.b2, m_c.a1, m_c.a2);
    }

    [[nodiscard]] float magnitudeLinear(const float cf) const
    {
        return biquadMagnitudeLinear(cf, m_c.b0, m_c.b1, m_c.b2, m_c.a1, m_c.a2);
    }

    [[nodiscard]] float magnitude(const float cf) const
    {
        return std::pow(10.0f, magnitudeInDb(cf) / 20.0f);
    }

  protected:
    BiquadSection m_c{};
};

/**
 * @brief Mono biquad section in transposed Direct Form II.
 *
 * Two state words; the form usually preferred for floating point (Direct Form I is the safer
 * choice in fixed point). Each designed response gets its own step so that the coefficient
 * identities of its design save multiplies: LowPass and HighPass have b1 = +-2 b0 and b2 = b0,
 * BandPass b1 = 0 and b2 = -b0, Notch b2 = b0 and a1 = b1, Peak and AllPass a1 = b1 (AllPass
 * also a2 = b0). The shelves and FreeCoefficients use the generic step. Only FreeCoefficients
 * accepts coefficients from outside (setCoefficients()), since the other steps rely on these
 * identities. Real-time safe: no allocation, the state is two floats.
 * @see https://ccrma.stanford.edu/~jos/filters/Transposed_Direct_Forms.html
 */
template <BiquadFilterType type>
class Biquad : public BiquadCoefficients
{
  public:
    void computeCoefficients(const float sampleRate, const float frequency, const float q, const float peakGain)
    {
        coefficients(type, sampleRate, frequency, q, peakGain);
    }

    void setCoefficients(const BiquadSection& section) noexcept
    {
        static_assert(type == BiquadFilterType::FreeCoefficients, "only FreeCoefficients takes outside coefficients");
        m_c = section;
    }

    void setCoefficients(const float b0, const float b1, const float b2, const float a1, const float a2) noexcept
    {
        setCoefficients(BiquadSection{b0, b1, b2, a1, a2});
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        if constexpr (type == BiquadFilterType::LowPass)
        {
            const float b0s = in * m_c.b0;
            const float out = b0s + m_z[0];
            m_z[0] = 2.0f * b0s + m_z[1] - m_c.a1 * out;
            m_z[1] = b0s - m_c.a2 * out;
            return out;
        }
        else if constexpr (type == BiquadFilterType::HighPass)
        {
            const float b0s = in * m_c.b0;
            const float out = b0s + m_z[0];
            m_z[0] = -2.0f * b0s + m_z[1] - m_c.a1 * out;
            m_z[1] = b0s - m_c.a2 * out;
            return out;
        }
        else if constexpr (type == BiquadFilterType::BandPass)
        {
            const float b0s = in * m_c.b0;
            const float out = b0s + m_z[0];
            m_z[0] = m_z[1] - m_c.a1 * out;
            m_z[1] = -b0s - m_c.a2 * out;
            return out;
        }
        else if constexpr (type == BiquadFilterType::Notch)
        {
            const float b0s = in * m_c.b0;
            const float out = b0s + m_z[0];
            m_z[0] = m_c.b1 * (in - out) + m_z[1];
            m_z[1] = b0s - m_c.a2 * out;
            return out;
        }
        else if constexpr (type == BiquadFilterType::Peak)
        {
            const float out = in * m_c.b0 + m_z[0];
            m_z[0] = m_c.b1 * (in - out) + m_z[1];
            m_z[1] = in * m_c.b2 - m_c.a2 * out;
            return out;
        }
        else if constexpr (type == BiquadFilterType::AllPass)
        {
            const float out = in * m_c.b0 + m_z[0];
            m_z[0] = m_c.b1 * (in - out) + m_z[1];
            m_z[1] = in * m_c.b2 - m_c.b0 * out;
            return out;
        }
        else
        {
            const float out = in * m_c.b0 + m_z[0];
            m_z[0] = in * m_c.b1 + m_z[1] - m_c.a1 * out;
            m_z[1] = in * m_c.b2 - m_c.a2 * out;
            return out;
        }
    }

    /// In place allowed (in == out).
    void processBlock(const float* in, float* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            out[i] = process(in[i]);
        }
    }

    void reset() noexcept
    {
        m_z.fill(0.0f);
    }

  private:
    std::array<float, 2> m_z{};
};

}
