#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

namespace jp::test
{

constexpr double c_pi{3.14159265358979323846};

[[nodiscard]] inline double toDb(const double gain)
{
    return 20.0 * std::log10(std::max(std::abs(gain), 1e-12));
}

/// Complex amplitude of one frequency in a signal (Hann-windowed single-bin DFT), scaled so
/// that a sine of amplitude A gives |result| = A.
[[nodiscard]] inline std::complex<double> toneAmplitude(const std::vector<float>& signal, const double frequencyHz,
                                                        const double sampleRate)
{
    std::complex<double> sum{0.0, 0.0};
    double windowSum = 0.0;
    const size_t n = signal.size();
    for (size_t i = 0; i < n; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos(2.0 * c_pi * static_cast<double>(i) / static_cast<double>(n));
        const double phase = -2.0 * c_pi * frequencyHz * static_cast<double>(i) / sampleRate;
        sum += window * static_cast<double>(signal[i]) * std::polar(1.0, phase);
        windowSum += window;
    }
    return 2.0 * sum / windowSum;
}

/// Steady-state gain of a per-sample processor for a sine: feeds the sine for settleSeconds,
/// then measures the output amplitude over measureSeconds and divides by the input amplitude.
template <typename Processor>
[[nodiscard]] double measureSineGain(Processor&& process, const double frequencyHz, const double sampleRate,
                                     const double amplitude = 0.01, const double settleSeconds = 0.5,
                                     const double measureSeconds = 1.0)
{
    const auto settle = static_cast<size_t>(settleSeconds * sampleRate);
    const auto measure = static_cast<size_t>(measureSeconds * sampleRate);
    std::vector<float> out;
    out.reserve(measure);
    const double omega = 2.0 * c_pi * frequencyHz / sampleRate;
    for (size_t i = 0; i < settle + measure; ++i)
    {
        const auto x = static_cast<float>(amplitude * std::sin(omega * static_cast<double>(i)));
        const float y = process(x);
        if (i >= settle)
        {
            out.push_back(y);
        }
    }
    return std::abs(toneAmplitude(out, frequencyHz, sampleRate)) / amplitude;
}

/// Magnitude spectrum (bins 0 .. n / 2) of a Hann-windowed signal by an iterative radix-2 FFT.
/// The length must be a power of two.
[[nodiscard]] inline std::vector<double> hannMagnitudeSpectrum(const std::vector<float>& signal)
{
    const size_t n = signal.size();
    std::vector<std::complex<double>> x(n);
    for (size_t i = 0; i < n; ++i)
    {
        const double window = 0.5 - 0.5 * std::cos(2.0 * c_pi * static_cast<double>(i) / static_cast<double>(n));
        x[i] = window * static_cast<double>(signal[i]);
    }
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
        {
            j ^= bit;
        }
        j ^= bit;
        if (i < j)
        {
            std::swap(x[i], x[j]);
        }
    }
    for (size_t length = 2; length <= n; length <<= 1)
    {
        const std::complex<double> unit = std::polar(1.0, -2.0 * c_pi / static_cast<double>(length));
        for (size_t start = 0; start < n; start += length)
        {
            std::complex<double> w{1.0, 0.0};
            for (size_t k = 0; k < length / 2; ++k)
            {
                const auto a = x[start + k];
                const auto b = x[start + k + length / 2] * w;
                x[start + k] = a + b;
                x[start + k + length / 2] = a - b;
                w *= unit;
            }
        }
    }
    std::vector<double> magnitude(n / 2 + 1);
    for (size_t i = 0; i <= n / 2; ++i)
    {
        magnitude[i] = std::abs(x[i]);
    }
    return magnitude;
}

/// Power-weighted mean frequency (spectral centroid) between lowHz and highHz (Hann window,
/// FFT over the whole signal, a power-of-two length). For a signal whose energy is spread
/// around a carrier with random fine structure (granular pitch shifting), this follows the
/// centre of the spread, where the strongest single bin does not.
[[nodiscard]] inline double spectralCentroid(const std::vector<float>& signal, const double sampleRate,
                                             const double lowHz, const double highHz)
{
    const auto magnitude = hannMagnitudeSpectrum(signal);
    const double binHz = sampleRate / static_cast<double>(signal.size());
    double weighted = 0.0;
    double total = 0.0;
    for (size_t i = static_cast<size_t>(lowHz / binHz);
         i < magnitude.size() && static_cast<double>(i) * binHz <= highHz; ++i)
    {
        const double power = magnitude[i] * magnitude[i];
        weighted += power * static_cast<double>(i) * binHz;
        total += power;
    }
    return total > 0.0 ? weighted / total : 0.0;
}

/// Energy above cutoffHz relative to the total energy of a signal, in dB (Hann window, FFT
/// over the whole signal, a power-of-two length). A click spreads thinly over many bins, so
/// the band is summed instead of looking for a peak.
[[nodiscard]] inline double highBandEnergyDb(const std::vector<float>& signal, const double cutoffHz,
                                             const double sampleRate)
{
    const auto magnitude = hannMagnitudeSpectrum(signal);
    const double binHz = sampleRate / static_cast<double>(signal.size());
    const auto firstHighBin = static_cast<size_t>(cutoffHz / binHz);
    double total = 0.0;
    double high = 0.0;
    for (size_t i = 0; i < magnitude.size(); ++i)
    {
        const double energy = magnitude[i] * magnitude[i];
        total += energy;
        if (i >= firstHighBin)
        {
            high += energy;
        }
    }
    return 10.0 * std::log10(std::max(high, 1e-300) / std::max(total, 1e-300));
}

}
