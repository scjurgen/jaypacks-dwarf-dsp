#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/Numbers/Interpolation.h"

namespace jp
{

/**
 * @brief A delay line of fixed length with a sinusoidally swept read position, no feedback, no filtering: the
 * delay of a chorus.
 *
 * The read position trails the write position by the width (setWidthMs()) and is swept towards the write position
 * by up to the modulation width, so the delay runs between width - 1 - modulation width and width - 1 samples.
 * The sweep is a sawtooth phase in [-1, 1) stepped every 16 samples (2 speed 16 / fs per tick, between ticks the
 * phase holds) shaped as 0.5 (1 - cos(pi phase)): 0 at the wrap, 1 at the edges, with no kink at the turning
 * points. The delay therefore moves in steps once per tick, of at most modulation width * pi / 2 * 2 speed 16 / fs
 * (0.31 samples for 300 samples of sweep at 1 Hz, 0.63 at 2 Hz), not continuously. The read is linear between two
 * samples. A new modulation depth (setModDepth(), depth 1 is 500 samples) waits for the phase to pass 0 on its way up,
 * where the sweep is at its zero, so it is applied without a step; the first depth set is applied after one full period
 * from the start (the phase starts at 0, runs to 1, wraps to -1 and reaches 0 again; before that the width is 0.1
 * sample). The sweep is limited to the width minus 8 samples. With a modulation width below 1e-7 the read is
 * whole-sample, without interpolation and without the extra sample of delay.
 *
 * What AbacDsp's ModulationDelayNoFeedback does beyond this (a width change by crossfade or by a glide of the
 * read rate, a second read head, tap reads) is not ported; the width is set once. The tick rate follows the sample
 * rate (AbacDsp assumes 48 kHz). The buffer is allocated in the constructor; real-time safe afterwards.
 */
template <size_t MaxSize>
class ModulationDelay
{
  public:
    static constexpr size_t c_tickSamples{16};
    static constexpr float c_depthUnitSamples{500.0f};
    static constexpr float c_safetyMarginSamples{8.0f};
    static constexpr size_t c_minWidth{48};
    static constexpr size_t c_padding{6};

    explicit ModulationDelay(const float sampleRate)
        : m_sampleRate(sampleRate)
        , m_buffer(MaxSize + c_padding, 0.0f)
    {
        setWidthSamples(MaxSize / 8);
    }

    /// Delay in milliseconds, limited to 48 samples .. MaxSize - 1; the line is not cleared.
    void setWidthMs(const float milliseconds) noexcept
    {
        setWidthSamples(static_cast<size_t>(std::round(milliseconds * 0.001f * m_sampleRate)));
    }

    void setWidthSamples(const size_t samples) noexcept
    {
        m_width = std::clamp(samples, c_minWidth, MaxSize - 1);
        m_headRead = static_cast<float>(m_headWrite) - static_cast<float>(m_width);
        if (m_headRead < 0.0f)
        {
            m_headRead += static_cast<float>(MaxSize);
        }
    }

    /// Clears the line; the sweep keeps its phase.
    void reset() noexcept
    {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    }

    [[nodiscard]] size_t getWidth() const noexcept
    {
        return m_width;
    }

    /// Sweep depth, 1 being 500 samples; takes effect at the next pass through the zero of the sweep.
    void setModDepth(const float depth) noexcept
    {
        m_newModWidth = depth * c_depthUnitSamples;
        m_modWidthPending = true;
    }

    /// Sweep rate in Hz.
    void setModSpeed(const float speedHz) noexcept
    {
        m_phaseAdvance = 2.0f * speedHz * static_cast<float>(c_tickSamples) / m_sampleRate;
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        if (++m_tick == c_tickSamples)
        {
            m_tick = 0;
            sweepTick();
        }
        write(in);
        return read();
    }

    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            out[i] = process(in[i]);
        }
    }

  private:
    void sweepTick() noexcept
    {
        const float nextPhase = m_phase + m_phaseAdvance;
        if (m_modWidthPending && m_phase < 0.0f && nextPhase >= 0.0f)
        {
            m_modWidth = m_newModWidth;
            m_modWidthPending = false;
        }
        m_phase = nextPhase >= 1.0f ? nextPhase - 2.0f : nextPhase;
    }

    void write(const float in) noexcept
    {
        m_buffer[m_headWrite] = in;
        if (m_headWrite < c_padding)
        {
            m_buffer[m_headWrite + MaxSize] = in;
        }
        if (++m_headWrite >= MaxSize)
        {
            m_headWrite = 0;
        }
    }

    [[nodiscard]] float read() noexcept
    {
        float position = m_headRead;
        float value;
        if (std::abs(m_modWidth) > 1e-7f)
        {
            const float maxSafe = std::max(0.0f, static_cast<float>(m_width) - c_safetyMarginSamples);
            const float width = std::clamp(m_modWidth, -maxSafe, maxSafe);
            const float shaped = 0.5f * (1.0f - std::cos(3.14159265358979323846f * m_phase));
            position += width * shaped + 1.0f;
            if (position >= static_cast<float>(MaxSize))
            {
                position -= static_cast<float>(MaxSize);
            }
            float whole;
            const float fraction = std::modf(position, &whole);
            value = linearPt2(&m_buffer[static_cast<size_t>(whole)], fraction);
        }
        else
        {
            value = m_buffer[static_cast<size_t>(position)];
        }
        m_headRead += 1.0f;
        if (m_headRead >= static_cast<float>(MaxSize))
        {
            m_headRead -= static_cast<float>(MaxSize);
        }
        return value;
    }

    float m_sampleRate;
    std::vector<float> m_buffer;
    size_t m_headWrite{MaxSize / 8};
    float m_headRead{0.0f};
    size_t m_width{MaxSize / 8};
    size_t m_tick{0};
    float m_phase{0.0f};
    float m_phaseAdvance{0.01f};
    float m_modWidth{0.1f};
    float m_newModWidth{0.0f};
    bool m_modWidthPending{false};
};

}
