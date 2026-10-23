#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace jp
{

/**
 * @brief Schroeder allpass section (one delay line of M samples between a feedforward gain
 * -g and a feedback gain g), with a crossfaded length change.
 *
 * Flat magnitude response with a dense, decaying impulse response, the standard building
 * block for smearing an impulse into diffusion without colouring the spectrum. The impulse
 * response starts with -g, then (1 - g^2) g^(k - 1) every M samples. The gain sets echo
 * density against ringing; beyond about 0.7 a section starts to sound tonal. Gains are
 * clamped to +/-0.99999.
 *
 * A length change does not jump: a second read head is placed at the new length and the
 * two are crossfaded linearly over 2 x the old length, at least 256 and at most 8192
 * samples. A request during a fade is held and applied when the fade ends (the newest held
 * request wins). setSize(n, true) jumps at once and drops any running or held fade (for
 * setup, not while audio runs).
 *
 * The buffer holds MaxSize48kHz samples scaled to the sample rate and is allocated in the
 * constructor; everything else is real-time safe.
 * @see https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html
 */
template <size_t MaxSize48kHz, size_t BlockSize>
class SchroederAllPassSoftTransition
{
  public:
    static constexpr size_t c_minDelaySize{1};

    explicit SchroederAllPassSoftTransition(const float sampleRate)
        : m_maxBufferSize(std::max(
              static_cast<size_t>(std::round(static_cast<float>(MaxSize48kHz) * sampleRate / 48000.0f)), size_t{2}))
        , m_buffer(m_maxBufferSize, 0.0f)
    {
        m_currentDelayWidth = std::clamp<size_t>(MaxSize48kHz / 8, c_minDelaySize, m_maxBufferSize - 1);
        m_headRead[0] = m_maxBufferSize - m_currentDelayWidth;
    }

    void clear() noexcept
    {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    }

    void setGain(const float gain) noexcept
    {
        m_feedback = std::clamp(gain, -0.99999f, 0.99999f);
    }

    /// Length in samples, clamped to 1 .. buffer size - 1; crossfaded unless fast.
    void setSize(const size_t newSize, const bool fast = false) noexcept
    {
        const auto clampedSize = std::clamp<size_t>(newSize, c_minDelaySize, m_maxBufferSize - 1);
        if (fast)
        {
            m_currentDelayWidth = clampedSize;
            m_newFadeSizeScheduled = 0;
            m_newFadeSize = 0;
            m_fadeSteps = 0;
            m_headWrite = m_currentDelayWidth;
            m_headRead[0] = 0;
            return;
        }
        if (clampedSize == m_currentDelayWidth)
        {
            return;
        }
        if (m_newFadeSize != 0)
        {
            m_newFadeSizeScheduled = clampedSize;
        }
        else
        {
            m_newFadeSize = clampedSize;
        }
    }

    [[nodiscard]] float step(const float in) noexcept
    {
        newFadeIfNeeded();
        float delayed;
        if (m_fadeSteps != 0)
        {
            const float outValue = nextHeadRead(0) * m_fadeFactorOut;
            const float inValue = nextHeadRead(1) * m_fadeFactorIn;
            m_fadeFactorOut -= m_fadeAdvance;
            m_fadeFactorIn += m_fadeAdvance;
            if (--m_fadeSteps == 0)
            {
                m_headRead[0] = m_headRead[1];
                m_newFadeSize = m_newFadeSizeScheduled;
                m_newFadeSizeScheduled = 0;
            }
            delayed = outValue + inValue;
        }
        else
        {
            delayed = nextHeadRead(0);
        }
        const float output = delayed - m_feedback * in;
        feedWrite(in + m_feedback * output);
        return output;
    }

    void processBlock(const float* source, float* target) noexcept
    {
        // Without a fade, with at least a block of delay and no wrap inside the block, the
        // block reads only samples written before it: one straight loop, the same arithmetic
        // as step() sample by sample.
        const size_t delay = (m_headWrite + m_maxBufferSize - m_headRead[0]) % m_maxBufferSize;
        if (m_fadeSteps == 0 && m_newFadeSize == 0 && delay >= BlockSize &&
            m_headRead[0] + BlockSize <= m_maxBufferSize && m_headWrite + BlockSize <= m_maxBufferSize)
        {
            const float* read = m_buffer.data() + m_headRead[0];
            float* write = m_buffer.data() + m_headWrite;
            for (size_t i = 0; i < BlockSize; ++i)
            {
                const float in = source[i];
                const float output = read[i] - m_feedback * in;
                write[i] = in + m_feedback * output;
                target[i] = output;
            }
            m_headRead[0] += BlockSize;
            if (m_headRead[0] >= m_maxBufferSize)
            {
                m_headRead[0] = 0;
            }
            m_headWrite += BlockSize;
            if (m_headWrite >= m_maxBufferSize)
            {
                m_headWrite = 0;
            }
            return;
        }
        for (size_t i = 0; i < BlockSize; ++i)
        {
            target[i] = step(source[i]);
        }
    }

    void processBlockInplace(float* inplace) noexcept
    {
        processBlock(inplace, inplace);
    }

    /// Current length (during a fade: the length faded to).
    [[nodiscard]] size_t size() const noexcept
    {
        return m_currentDelayWidth;
    }

  private:
    void newFadeIfNeeded() noexcept
    {
        if (m_newFadeSize != 0 && m_fadeSteps == 0)
        {
            m_fadeFactorIn = 0.0f;
            m_fadeFactorOut = 1.0f;
            m_fadeSteps = std::clamp<size_t>(m_currentDelayWidth * 2, 256, 8192);
            m_fadeAdvance = 1.0f / static_cast<float>(m_fadeSteps);
            m_headRead[1] = (m_headWrite + m_maxBufferSize - m_newFadeSize) % m_maxBufferSize;
            m_currentDelayWidth = m_newFadeSize;
        }
    }

    void feedWrite(const float in) noexcept
    {
        m_buffer[m_headWrite] = in;
        if (++m_headWrite >= m_maxBufferSize)
        {
            m_headWrite = 0;
        }
    }

    [[nodiscard]] float nextHeadRead(const size_t index) noexcept
    {
        const float value = m_buffer[m_headRead[index]];
        if (++m_headRead[index] >= m_maxBufferSize)
        {
            m_headRead[index] = 0;
        }
        return value;
    }

    size_t m_maxBufferSize;
    std::vector<float> m_buffer;
    float m_feedback{0.0f};
    size_t m_headWrite{0};
    std::array<size_t, 2> m_headRead{0, 0};
    size_t m_fadeSteps{0};
    float m_fadeFactorIn{0.0f};
    float m_fadeFactorOut{0.0f};
    float m_fadeAdvance{0.0f};
    size_t m_newFadeSize{0};
    size_t m_newFadeSizeScheduled{0};
    size_t m_currentDelayWidth{1};
};

}
