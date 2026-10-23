#pragma once

#include <cmath>
#include <complex>

#include "dsp/Filters/Biquad.h"

namespace jp
{

/// Analog section H(s) = (b0 + b1 s + b2 s^2) / (a0 + a1 s + a2 s^2); a first-order section has
/// b2 = a2 = 0.
struct AnalogSection
{
    double b0{1.0};
    double b1{0.0};
    double b2{0.0};
    double a0{1.0};
    double a1{0.0};
    double a2{0.0};
};

/// Bilinear transform s = c (1 - z^-1) / (1 + z^-1) of an analog section, normalised by the
/// resulting a0. c = 2 fs gives the plain transform (exact at DC, the frequency axis warps
/// towards Nyquist as f_analog = fs / pi tan(pi f / fs)). A first-order section maps to
/// b2 = a2 = 0. Computed in double; the caller keeps the float result.
[[nodiscard]] inline BiquadSection bilinearSection(const AnalogSection& h, const double c) noexcept
{
    if (h.b2 == 0.0 && h.a2 == 0.0)
    {
        // multiplied by (1 + z^-1) once, not twice: no common factor in the result
        const double d0 = h.a0 + h.a1 * c;
        return BiquadSection{static_cast<float>((h.b0 + h.b1 * c) / d0), static_cast<float>((h.b0 - h.b1 * c) / d0),
                             0.0f, static_cast<float>((h.a0 - h.a1 * c) / d0), 0.0f};
    }
    const double c2 = c * c;
    const double n0 = h.b0 + h.b1 * c + h.b2 * c2;
    const double n1 = 2.0 * (h.b0 - h.b2 * c2);
    const double n2 = h.b0 - h.b1 * c + h.b2 * c2;
    const double d0 = h.a0 + h.a1 * c + h.a2 * c2;
    const double d1 = 2.0 * (h.a0 - h.a2 * c2);
    const double d2 = h.a0 - h.a1 * c + h.a2 * c2;
    return BiquadSection{static_cast<float>(n0 / d0), static_cast<float>(n1 / d0), static_cast<float>(n2 / d0),
                         static_cast<float>(d1 / d0), static_cast<float>(d2 / d0)};
}

/// Analog response H(j 2 pi f) of a section, for analysis and tests.
[[nodiscard]] inline std::complex<double> analogSectionResponse(const AnalogSection& h, const double frequencyHz)
{
    constexpr double twoPi{6.283185307179586476925286766559};
    const std::complex<double> s{0.0, twoPi * frequencyHz};
    return (h.b0 + s * (h.b1 + s * h.b2)) / (h.a0 + s * (h.a1 + s * h.a2));
}

/// Digital response of a section at a frequency, evaluated in double on the stored float
/// coefficients, for analysis and tests.
[[nodiscard]] inline std::complex<double> biquadSectionResponse(const BiquadSection& c, const double frequencyHz,
                                                                const double sampleRate)
{
    constexpr double twoPi{6.283185307179586476925286766559};
    const std::complex<double> zInv = std::polar(1.0, -twoPi * frequencyHz / sampleRate);
    const std::complex<double> num =
        static_cast<double>(c.b0) + zInv * (static_cast<double>(c.b1) + zInv * static_cast<double>(c.b2));
    const std::complex<double> den = 1.0 + zInv * (static_cast<double>(c.a1) + zInv * static_cast<double>(c.a2));
    return num / den;
}

/// First-order highpass s / (s + w) with w prewarped so that the -3 dB point of the digital
/// filter lies exactly at cutoffHz.
[[nodiscard]] inline BiquadSection firstOrderHighpassSection(const double cutoffHz, const double sampleRate) noexcept
{
    constexpr double pi{3.14159265358979323846};
    const double c = 2.0 * sampleRate;
    const double w = c * std::tan(pi * cutoffHz / sampleRate);
    return bilinearSection(AnalogSection{0.0, 1.0, 0.0, w, 1.0, 0.0}, c);
}

}
