#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/BlockProcessors/BlockProcessorBase.h"

namespace jp
{

/**
 * @brief Vibrato by a sine-modulated fractional delay, as a block processor for a delay line.
 *
 * The delay swings around a fixed nominal delay N = largest swing + 2 samples:
 * d(t) = N + D sin(2 pi (f t + phase)), so the read head never passes the write head. The pitch follows the rate of
 * change of the delay: the peak deviation is 1 + 2 pi f D / fs as a frequency ratio, so a depth in cents c needs D =
 * (2^(c / 1200) - 1) fs / (2 pi f) samples (swingSamplesFor()). A new depth ramps linearly across the next block, so
 * the read head never jumps.
 *
 * The fractional delay is a first-order allpass: d = M + delta with M whole and delta in
 * [0.5, 1.5), y[n] = eta (x[n - M] - y[n - 1]) + x[n - M - 1] with eta = (1 - delta) / (1 + delta)
 * (so |eta| <= 1/3, away from the pole-zero cancellation at eta = 1). Its magnitude is 1 at
 * every frequency: in a feedback loop the line loses nothing per pass, where a polynomial
 * interpolator filters it on every pass (Hermite in two of 16 lines took a 10 s decay to 8.2 s
 * in the highs, linear loses 2 dB at 10 kHz per pass halfway between samples). The price is a
 * phase that departs from the ideal delay towards Nyquist, and short transients when delta
 * moves fast (here it moves slowly, 0.5 Hz). At depth 0, delta is 1 and eta 0: an exact delay
 * of N samples. getLatency() is N, the delay the processor adds on average, for a host that
 * keeps a loop length exact (FdnTank). The buffer is allocated in the constructor for the
 * largest swing; process() is real-time safe.
 * @see https://ccrma.stanford.edu/~jos/pasp/Vibrato_Simulation.html
 * @see https://ccrma.stanford.edu/~jos/pasp/First_Order_Allpass_Interpolation.html
 * @see J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", JAES 45(9), 1997
 */
template <size_t BlockSize>
class BlockVibrato final : public BlockProcessorBase<BlockSize>
{
  public:
    BlockVibrato(const float sampleRate, const float maxSwingSeconds)
        : m_sampleRate(sampleRate)
        , m_maxSwing(std::ceil(std::max(maxSwingSeconds, 0.0f) * sampleRate))
        , m_nominal(static_cast<size_t>(m_maxSwing) + 2)
    {
        size_t size = 8;
        while (size < m_nominal + static_cast<size_t>(m_maxSwing) + 4)
        {
            size *= 2;
        }
        m_buffer.assign(size, 0.0f);
        m_mask = size - 1;
    }

    /// Swing in samples for a peak deviation in cents at a modulation rate.
    [[nodiscard]] static float swingSamplesFor(const float cents, const float rateHz, const float sampleRate) noexcept
    {
        constexpr float twoPi{6.283185307179586f};
        if (rateHz <= 0.0f)
        {
            return 0.0f;
        }
        return (std::exp2(std::max(cents, 0.0f) / 1200.0f) - 1.0f) * sampleRate / (twoPi * rateHz);
    }

    /// Modulation rate in Hz (0 holds the phase).
    void setRate(const float hz) noexcept
    {
        m_rateHz = std::max(hz, 0.0f);
        m_increment = static_cast<double>(m_rateHz) / static_cast<double>(m_sampleRate);
        constexpr double twoPi{6.283185307179586476925286766559};
        m_rotationSin = std::sin(twoPi * m_increment);
        m_rotationCos = std::cos(twoPi * m_increment);
        setDepthCents(m_cents);
    }

    /// Start phase in turns (0 to 1), applied by reset().
    void setPhase(const float turns) noexcept
    {
        m_startPhase = static_cast<double>(turns - std::floor(turns));
    }

    /// Peak pitch deviation in cents; limited by the largest swing given at construction.
    void setDepthCents(const float cents) noexcept
    {
        m_cents = std::max(cents, 0.0f);
        m_targetSwing = std::min(swingSamplesFor(m_cents, m_rateHz, m_sampleRate), m_maxSwing);
    }

    [[nodiscard]] size_t getLatency() const noexcept
    {
        return m_nominal;
    }

    /// Swing reached at the end of the last block, samples.
    [[nodiscard]] float getSwing() const noexcept
    {
        return m_swing;
    }

    [[nodiscard]] float getMaxSwing() const noexcept
    {
        return m_maxSwing;
    }

    void process(std::array<float, BlockSize>& block) noexcept override
    {
        // the LFO: exact sine and cosine at the block start from the phase accumulator, then a
        // rotation per sample (std::sin per sample was 80 % of the block's cost on the Dwarf)
        constexpr double twoPi{6.283185307179586476925286766559};
        double sine = std::sin(twoPi * m_phase);
        double cosine = std::cos(twoPi * m_phase);
        m_phase += m_increment * static_cast<double>(BlockSize);
        m_phase -= std::floor(m_phase);
        const float step = (m_targetSwing - m_swing) / static_cast<float>(BlockSize);
        for (size_t i = 0; i < BlockSize; ++i)
        {
            m_buffer[m_write] = block[i];
            m_swing = i + 1 == BlockSize ? m_targetSwing : m_swing + step;
            const float lfo = static_cast<float>(sine);
            const double rotated = sine * m_rotationCos + cosine * m_rotationSin;
            cosine = cosine * m_rotationCos - sine * m_rotationSin;
            sine = rotated;
            const float delay = static_cast<float>(m_nominal) + m_swing * lfo;
            const auto whole = static_cast<size_t>(delay - 0.5f);
            const float delta = delay - static_cast<float>(whole);
            const float eta = (1.0f - delta) / (1.0f + delta);
            const float newer = m_buffer[(m_write + m_buffer.size() - whole) & m_mask];
            const float older = m_buffer[(m_write + m_buffer.size() - whole - 1) & m_mask];
            m_previous = eta * (newer - m_previous) + older;
            block[i] = m_previous;
            m_write = (m_write + 1) & m_mask;
        }
    }

    /// Clears the delay, restarts the phase and jumps to the target depth.
    void reset() noexcept override
    {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_write = 0;
        m_previous = 0.0f;
        m_phase = m_startPhase;
        m_swing = m_targetSwing;
    }

  private:
    float m_sampleRate;
    float m_maxSwing;
    size_t m_nominal;
    std::vector<float> m_buffer;
    size_t m_mask{0};
    size_t m_write{0};
    float m_previous{0.0f};
    double m_phase{0.0};
    double m_startPhase{0.0};
    double m_increment{0.0};
    double m_rotationSin{0.0};
    double m_rotationCos{1.0};
    float m_rateHz{0.0f};
    float m_cents{0.0f};
    float m_swing{0.0f};
    float m_targetSwing{0.0f};
};

}
