#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

#include "dsp/Filters/BilinearTransform.h"
#include "dsp/Filters/Biquad.h"
#include "dsp/Filters/RampedBiquad.h"

namespace jp
{

/// Component values of the passive treble/mid/bass network (the Fender and Marshall "TMB" stack).
/// r1 treble pot, r2 bass pot, r3 mid pot, r4 slope resistor (ohm); c1 treble cap, c2 bass
/// cap, c3 mid cap (farad).
struct ToneStackComponents
{
    double r1;
    double r2;
    double r3;
    double r4;
    double c1;
    double c2;
    double c3;
};

/// Fender AB763 Twin Reverb, both channels: treble 250k, bass 250k, mid 10k (all log pots),
/// slope 100k, 250 pF, 0.1 uF, 0.047 uF.
/// @see https://robrobinette.com/AB763_Model_Differences.htm
inline constexpr ToneStackComponents c_toneStackTwinReverbAB763{250e3, 250e3, 10e3, 100e3, 250e-12, 100e-9, 47e-9};

/// H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3) of the tone stack, driven by
/// an ideal source and unloaded.
struct ToneStackAnalog
{
    double b1;
    double b2;
    double b3;
    double a1;
    double a2;
    double a3;
};

/**
 * Closed-form coefficients of the TMB stack after Yeh and Smith. t, m and l are the pot
 * fractions in [0, 1]: t the part of the treble pot between wiper and bass pot, m the part of
 * the mid pot between wiper and ground (the other 1 - m lies between wiper and bass pot), l
 * the bass pot used as a variable resistor between treble pot and mid pot. The tests check
 * the formulas against a nodal analysis of that circuit.
 * @see D. T. Yeh, J. O. Smith, "Discretization of the '59 Fender Bassman Tone Stack", DAFx-06,
 *      https://ccrma.stanford.edu/~dtyeh/tonestack/
 */
[[nodiscard]] inline ToneStackAnalog toneStackAnalog(const ToneStackComponents& k, const double t, const double m,
                                                     const double l) noexcept
{
    const double r1 = k.r1;
    const double r2 = k.r2;
    const double r3 = k.r3;
    const double r4 = k.r4;
    const double c1 = k.c1;
    const double c2 = k.c2;
    const double c3 = k.c3;
    ToneStackAnalog h{};
    h.b1 = t * c1 * r1 + m * c3 * r3 + l * (c1 * r2 + c2 * r2) + (c1 * r3 + c2 * r3);
    h.b2 = t * (c1 * c2 * r1 * r4 + c1 * c3 * r1 * r4) - m * m * (c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3) +
           m * (c1 * c3 * r1 * r3 + c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3) +
           l * (c1 * c2 * r1 * r2 + c1 * c2 * r2 * r4 + c1 * c3 * r2 * r4) +
           l * m * (c1 * c3 * r2 * r3 + c2 * c3 * r2 * r3) +
           (c1 * c2 * r1 * r3 + c1 * c2 * r3 * r4 + c1 * c3 * r3 * r4);
    const double c123 = c1 * c2 * c3;
    h.b3 = l * m * c123 * (r1 * r2 * r3 + r2 * r3 * r4) - m * m * c123 * (r1 * r3 * r3 + r3 * r3 * r4) +
           m * c123 * (r1 * r3 * r3 + r3 * r3 * r4) + t * c123 * r1 * r3 * r4 - t * m * c123 * r1 * r3 * r4 +
           t * l * c123 * r1 * r2 * r4;
    h.a1 = (c1 * r1 + c1 * r3 + c2 * r3 + c2 * r4 + c3 * r4) + m * c3 * r3 + l * (c1 * r2 + c2 * r2);
    h.a2 = m * (c1 * c3 * r1 * r3 - c2 * c3 * r3 * r4 + c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3) +
           l * m * (c1 * c3 * r2 * r3 + c2 * c3 * r2 * r3) - m * m * (c1 * c3 * r3 * r3 + c2 * c3 * r3 * r3) +
           l * (c1 * c2 * r2 * r4 + c1 * c2 * r1 * r2 + c1 * c3 * r2 * r4 + c2 * c3 * r2 * r4) +
           (c1 * c2 * r1 * r4 + c1 * c3 * r1 * r4 + c1 * c2 * r3 * r4 + c1 * c2 * r1 * r3 + c1 * c3 * r3 * r4 +
            c2 * c3 * r3 * r4);
    h.a3 = l * m * c123 * (r1 * r2 * r3 + r2 * r3 * r4) - m * m * c123 * (r1 * r3 * r3 + r3 * r3 * r4) +
           m * c123 * (r3 * r3 * r4 + r1 * r3 * r3 - r1 * r3 * r4) + l * c123 * r1 * r2 * r4 + c123 * r1 * r3 * r4;
    return h;
}

/// H(j 2 pi f) of the analog stack, for analysis and tests.
[[nodiscard]] inline std::complex<double> toneStackAnalogResponse(const ToneStackAnalog& h, const double frequencyHz)
{
    constexpr double twoPi{6.283185307179586476925286766559};
    const std::complex<double> s{0.0, twoPi * frequencyHz};
    return s * (h.b1 + s * (h.b2 + s * h.b3)) / (1.0 + s * (h.a1 + s * (h.a2 + s * h.a3)));
}

/// Audio (log) pot taper: the resistance fraction at a travel position in [0, 1], modelled as
/// (81^p - 1) / 80, which gives 10 % at mid travel (an approximation of the usual "A" taper,
/// whose catalogue curves put mid travel at 10 to 15 %).
[[nodiscard]] inline double audioTaper(const double position) noexcept
{
    const double p = std::clamp(position, 0.0, 1.0);
    return (std::pow(81.0, p) - 1.0) / 80.0;
}

/**
 * @brief Fender TMB tone stack as a first-order section followed by a biquad.
 *
 * The third-order H(s) of toneStackAnalog() is split before the bilinear transform: an RC
 * network has three real negative poles, the numerator is s (b1 + b2 s + b3 s^2). The pole
 * nearest to DC is found by Newton's method from s = 0 (for a polynomial with only real roots
 * the iteration from outside the roots converges monotonically), divided out from the constant
 * end (stable for the smallest root), and paired with the zero at DC; the remaining quadratic
 * and the numerator quadratic form the biquad, its poles mapped to z one by one and kept inside
 * the unit circle (with bass 0 and mid 10, a3 = 0 and one pole sits at z = -1). Both run in
 * float as transposed Direct Form II, which avoids the third-order direct form that loses
 * precision in float when the poles crowd towards z = 1 at high sample rates.
 *
 * setControls() takes the knob travel in [0, 1] and applies audioTaper() to all three pots, as
 * on the AB763 (all log pots); setPotFractions() takes the fractions directly. The design is
 * done in double and costs a few hundred flops: call it at control rate, not per sample, and
 * let setRampSamples() spread each new design over the samples up to the next one
 * (RampedBiquad); applied at once every 16 samples, a fast treble move made steps 13 times
 * the steady ones (the treble moves only the numerator, and near 10 the audio taper changes
 * the high-frequency gain by 7 % per design). The gain is the circuit's own (about -15 to
 * -25 dB in the middle band); no makeup is applied here. The filter state is kept across
 * redesigns; setSampleRate() and reset() apply the current design at once. Real-time safe.
 */
class FenderToneStack
{
  public:
    void setSampleRate(const float sampleRate) noexcept
    {
        m_sampleRate = static_cast<double>(sampleRate);
        design();
        snapToDesign();
    }

    /// Samples over which a new design is reached (0: at once, the default).
    void setRampSamples(const size_t samples) noexcept
    {
        m_firstOrder.setRampSamples(samples);
        m_secondOrder.setRampSamples(samples);
    }

    void setComponents(const ToneStackComponents& components) noexcept
    {
        m_components = components;
        design();
    }

    void setControls(const double bass, const double mid, const double treble) noexcept
    {
        setPotFractions(audioTaper(bass), audioTaper(mid), audioTaper(treble));
    }

    void setPotFractions(const double bass, const double mid, const double treble) noexcept
    {
        m_bass = std::clamp(bass, 0.0, 1.0);
        m_mid = std::clamp(mid, 0.0, 1.0);
        m_treble = std::clamp(treble, 0.0, 1.0);
        design();
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        return m_secondOrder.process(m_firstOrder.process(in));
    }

    /// Clears the filter state and ends a running ramp.
    void reset() noexcept
    {
        m_firstOrder.reset();
        m_secondOrder.reset();
        snapToDesign();
    }

    [[nodiscard]] const ToneStackAnalog& getAnalog() const noexcept
    {
        return m_analog;
    }

    /// The designed sections (where a ramp ends).
    [[nodiscard]] const BiquadSection& getFirstOrderSection() const noexcept
    {
        return m_firstOrder.getTarget();
    }

    [[nodiscard]] const BiquadSection& getSecondOrderSection() const noexcept
    {
        return m_secondOrder.getTarget();
    }

    /// Digital response of the designed sections (float coefficients, evaluated in double).
    [[nodiscard]] std::complex<double> response(const double frequencyHz) const
    {
        return biquadSectionResponse(getFirstOrderSection(), frequencyHz, m_sampleRate) *
               biquadSectionResponse(getSecondOrderSection(), frequencyHz, m_sampleRate);
    }

  private:
    void snapToDesign() noexcept
    {
        m_firstOrder.snapToTarget();
        m_secondOrder.snapToTarget();
    }

    void design() noexcept
    {
        m_analog = toneStackAnalog(m_components, m_treble, m_mid, m_bass);
        const ToneStackAnalog& h = m_analog;
        // smallest pole of 1 + a1 s + a2 s^2 + a3 s^3
        double p = 0.0;
        for (int i = 0; i < 60; ++i)
        {
            const double value = 1.0 + p * (h.a1 + p * (h.a2 + p * h.a3));
            const double slope = h.a1 + p * (2.0 * h.a2 + p * 3.0 * h.a3);
            const double step = value / slope;
            p -= step;
            if (std::abs(step) <= 1e-13 * std::abs(p))
            {
                break;
            }
        }
        // 1 + a1 s + a2 s^2 + a3 s^3 = (s - p)(d0 + d1 s + d2 s^2), divided from the constant end
        const double d0 = -1.0 / p;
        const double d1 = (d0 - h.a1) / p;
        const double d2 = (d1 - h.a2) / p;
        const double c = 2.0 * m_sampleRate;
        m_firstOrder.setTarget(bilinearSection(AnalogSection{0.0, 1.0, 0.0, -p, 1.0, 0.0}, c));
        // The biquad's denominator from its two poles mapped to z = (c + s) / (c - s), written so
        // that d2 -> 0 (a3 = 0 at bass 0 and mid 10: the stack is second order there) gives the
        // pole z = -1 without dividing by d2; both poles are clamped inside the unit circle so
        // that rounding cannot push one outside.
        const double q = -0.5 * (d1 + std::sqrt(std::max(d1 * d1 - 4.0 * d0 * d2, 0.0)));
        constexpr double maxRadius{0.99999};
        const double z1 = std::clamp((c * d2 + q) / (c * d2 - q), -maxRadius, maxRadius);
        const double z2 = std::clamp((c * q + d0) / (c * q - d0), -maxRadius, maxRadius);
        const double norm = 1.0 / (d0 + d1 * c + d2 * c * c);
        const double c2 = c * c;
        m_secondOrder.setTarget(BiquadSection{static_cast<float>((h.b1 + h.b2 * c + h.b3 * c2) * norm),
                                              static_cast<float>(2.0 * (h.b1 - h.b3 * c2) * norm),
                                              static_cast<float>((h.b1 - h.b2 * c + h.b3 * c2) * norm),
                                              static_cast<float>(-(z1 + z2)), static_cast<float>(z1 * z2)});
    }

    double m_sampleRate{48000.0};
    ToneStackComponents m_components{c_toneStackTwinReverbAB763};
    double m_bass{0.1};
    double m_mid{0.1};
    double m_treble{0.1};
    ToneStackAnalog m_analog{};
    RampedBiquad m_firstOrder;
    RampedBiquad m_secondOrder;
};

}
