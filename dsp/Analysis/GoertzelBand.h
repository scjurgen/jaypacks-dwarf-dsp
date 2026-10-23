#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace jp
{

/**
 * @brief Spectrum at one frequency, block by block: a Hann-windowed Goertzel filter over
 * blocks of fixed length, with the power averaged over the blocks (Welch's method for one
 * frequency).
 *
 * Each block of N samples gives the windowed DTFT X at the frequency (phase referenced to the
 * block start). The window is the periodic Hann, w[n] = 0.5 - 0.5 cos(2 pi n / N), computed
 * by a rotation in double; it lowers the leakage of tones away from the frequency at the cost
 * of a wider main lobe (+-2 bins) and, for a tone between bin centres, a lower reading (up to
 * 1.42 dB at half a bin). amplitude() is scaled so that a steady sine exactly on a bin centre
 * reads its amplitude. Real-time safe.
 */
class GoertzelBand
{
  public:
    GoertzelBand() = default;

    GoertzelBand(const double frequencyHz, const double sampleRate, const uint32_t blockSize) noexcept
    {
        setup(frequencyHz, sampleRate, blockSize);
    }

    /// Frequency, sample rate and block length (at least 2); starts afresh.
    void setup(const double frequencyHz, const double sampleRate, const uint32_t blockSize) noexcept
    {
        constexpr double twoPi{6.283185307179586476925286766559};
        m_blockSize = blockSize < 2 ? 2 : blockSize;
        const double omega = twoPi * frequencyHz / sampleRate;
        m_coefficient = 2.0 * std::cos(omega);
        m_twiddle = std::polar(1.0, -omega);
        m_blockRotation = std::polar(1.0, -omega * static_cast<double>(m_blockSize - 1));
        m_windowRotateCos = std::cos(twoPi / static_cast<double>(m_blockSize));
        m_windowRotateSin = std::sin(twoPi / static_cast<double>(m_blockSize));
        reset();
    }

    void reset() noexcept
    {
        startBlock();
        m_last = {0.0, 0.0};
        m_powerSum = 0.0;
        m_blocks = 0;
    }

    /// Adds one sample; returns true when it completed a block (its value is lastBlock()).
    bool add(const float x) noexcept
    {
        const double window = 0.5 - 0.5 * m_windowCos;
        const double s = window * static_cast<double>(x) + m_coefficient * m_s1 - m_s2;
        m_s2 = m_s1;
        m_s1 = s;
        const double c = m_windowCos * m_windowRotateCos - m_windowSin * m_windowRotateSin;
        m_windowSin = m_windowSin * m_windowRotateCos + m_windowCos * m_windowRotateSin;
        m_windowCos = c;
        if (++m_position < m_blockSize)
        {
            return false;
        }
        m_last = m_blockRotation * (m_s1 - m_twiddle * m_s2);
        m_powerSum += std::norm(m_last);
        ++m_blocks;
        startBlock();
        return true;
    }

    /// Windowed DTFT of the last completed block.
    [[nodiscard]] std::complex<double> lastBlock() const noexcept
    {
        return m_last;
    }

    [[nodiscard]] uint64_t blocks() const noexcept
    {
        return m_blocks;
    }

    /// Mean |X|^2 over the completed blocks.
    [[nodiscard]] double meanPower() const noexcept
    {
        return m_blocks > 0 ? m_powerSum / static_cast<double>(m_blocks) : 0.0;
    }

    /// Amplitude of a sine that would give meanPower() on a bin centre: 2 sqrt(P) / sum(w),
    /// sum(w) = N / 2.
    [[nodiscard]] double amplitude() const noexcept
    {
        return 4.0 * std::sqrt(meanPower()) / static_cast<double>(m_blockSize);
    }

    [[nodiscard]] uint32_t blockSize() const noexcept
    {
        return m_blockSize;
    }

  private:
    void startBlock() noexcept
    {
        m_s1 = 0.0;
        m_s2 = 0.0;
        m_position = 0;
        m_windowCos = 1.0;
        m_windowSin = 0.0;
    }

    uint32_t m_blockSize{2};
    double m_coefficient{2.0};
    std::complex<double> m_twiddle{1.0, 0.0};
    std::complex<double> m_blockRotation{1.0, 0.0};
    double m_windowRotateCos{1.0};
    double m_windowRotateSin{0.0};
    double m_windowCos{1.0};
    double m_windowSin{0.0};
    double m_s1{0.0};
    double m_s2{0.0};
    uint32_t m_position{0};
    std::complex<double> m_last{0.0, 0.0};
    double m_powerSum{0.0};
    uint64_t m_blocks{0};
};

/**
 * @brief Transfer function at one frequency from an input and an output signal: the H1
 * estimate Sxy / Sxx over Hann-windowed blocks (GoertzelBand), with the coherence.
 *
 * Averaging the cross spectrum makes the estimate independent of the input's spectrum, so a
 * noise input gives the gain of a linear system within a fraction of a dB after some blocks,
 * where the ratio of two band powers would scatter by several dB. Window effects cancel
 * between input and output as long as the system's impulse response is short against the
 * block. The coherence |Sxy|^2 / (Sxx Syy) is 1 for a linear, time-invariant path without
 * added noise and drops when the system moves or adds signal. Real-time safe.
 */
class TransferBand
{
  public:
    TransferBand() = default;

    TransferBand(const double frequencyHz, const double sampleRate, const uint32_t blockSize) noexcept
    {
        setup(frequencyHz, sampleRate, blockSize);
    }

    void setup(const double frequencyHz, const double sampleRate, const uint32_t blockSize) noexcept
    {
        m_input.setup(frequencyHz, sampleRate, blockSize);
        m_output.setup(frequencyHz, sampleRate, blockSize);
        reset();
    }

    void reset() noexcept
    {
        m_input.reset();
        m_output.reset();
        m_sxx = 0.0;
        m_syy = 0.0;
        m_sxy = {0.0, 0.0};
    }

    /// Adds one input and one output sample.
    void add(const float input, const float output) noexcept
    {
        const bool done = m_input.add(input);
        m_output.add(output);
        if (done)
        {
            const std::complex<double> x = m_input.lastBlock();
            const std::complex<double> y = m_output.lastBlock();
            m_sxx += std::norm(x);
            m_syy += std::norm(y);
            m_sxy += std::conj(x) * y;
        }
    }

    [[nodiscard]] uint64_t blocks() const noexcept
    {
        return m_input.blocks();
    }

    /// |H| (0 before the first block or without input at this frequency).
    [[nodiscard]] double gain() const noexcept
    {
        return m_sxx > 0.0 ? std::abs(m_sxy) / m_sxx : 0.0;
    }

    /// Amplitudes of the input and the output at the frequency (GoertzelBand::amplitude()).
    [[nodiscard]] double inputAmplitude() const noexcept
    {
        return m_input.amplitude();
    }

    [[nodiscard]] double outputAmplitude() const noexcept
    {
        return m_output.amplitude();
    }

    /// arg H in radians.
    [[nodiscard]] double phase() const noexcept
    {
        return std::arg(m_sxy);
    }

    [[nodiscard]] double coherence() const noexcept
    {
        return m_sxx > 0.0 && m_syy > 0.0 ? std::norm(m_sxy) / (m_sxx * m_syy) : 0.0;
    }

  private:
    GoertzelBand m_input;
    GoertzelBand m_output;
    double m_sxx{0.0};
    double m_syy{0.0};
    std::complex<double> m_sxy{0.0, 0.0};
};

}
