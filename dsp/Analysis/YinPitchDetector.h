#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/Filters/BiquadCascade.h"

namespace jp
{

/// Cost option of YinPitchDetector (see there).
struct YinPitchOptions
{
    size_t decimation{1}; ///< 1 or a larger integer; the cost falls with its square
};

/**
 * @brief Monophonic pitch detector after YIN: the cumulative mean normalised difference function (CMNDF) with an
 * absolute threshold and parabolic interpolation.
 *
 * Plain autocorrelation peaks as strongly at twice the period as at the period itself, which is where its octave errors
 * come from. Dividing the difference function by its own running mean makes the later dips shallower than the first
 * one, so the first dip below the threshold is the period (steps 1 to 5 of the paper; step 6, the best local estimate,
 * is not done). Confidence is 1 - CMNDF at the chosen lag: near 1 for a clean periodic signal, and a measure of
 * periodicity, not a probability.
 *
 * The analysis window is the newest 2 * maxLag samples (maxLag = fs / minFrequency, at the decimated rate): the
 * difference function integrates over the first maxLag of them and looks up to maxLag further. Gate (RMS of the whole
 * window), difference function and level use that one window; its centre lies getLatencySamples() input samples before
 * the newest one, and getLastWindowEnd() is the count of input samples consumed when it was taken, so a reading can be
 * stamped with its centre. A change of pitch is complete in the result one window plus one hop later. The decimating
 * lowpass adds a group delay (a few input samples) that is not counted.
 *
 * The cumulative sum starts at lag 1 as in the paper; a constant added to it would dominate for quiet signals and make
 * noise look periodic. The difference function is computed lag by lag and stops at the first dip that meets the
 * threshold, which gives the same result as computing all lags; only when no dip qualifies (noise, unvoiced) are all
 * lags computed, and that is the worst case for the cost.
 *
 * A reading is made once per hop and counted in input samples, so the result does not depend on how the input is cut
 * into blocks. Decimation (YinPitchOptions) puts a 4th-order Butterworth lowpass at 0.4 of the decimated rate in front
 * and keeps every n-th sample: the arithmetic falls with n squared, and the lowpass removes broadband noise that
 * would otherwise lift the dips above the threshold. The price is resolution (n = 4: 12 samples per period at 1 kHz).
 * All buffers are sized in the constructor; step() and process() allocate nothing.
 *
 * Measured (documentation/YinPitch/): a sine over 80 Hz to 1 kHz is read within 0.34 cent at full rate, 1.4 cent
 * decimated by 2 and 5.9 cent decimated by 4 (at 987 Hz). On the Cortex-A35 at a hop rate of 50 Hz the cost per block
 * of 128 frames is 144 us at full rate, 45 us decimated by 2 and 19 us decimated by 4 on average, and 1.3 ms, 0.5 ms
 * and 0.14 ms in the block that holds a reading (99th percentile, 82 Hz tone, the worst case with noise); the 2.67 ms
 * block period allows decimation by 4 only. Broadband noise of +-0.5 on a sine of amplitude 1 makes 76 % of the
 * full rate readings wrong by more than 50 cent and none of the decimated ones.
 * @see de Cheveigne and Kawahara, "YIN, a fundamental frequency estimator for speech and music", JASA 111(4), 2002,
 *      https://doi.org/10.1121/1.1458024
 */
class YinPitchDetector
{
  public:
    static constexpr float c_threshold{0.1f};
    static constexpr float c_minimumQuality{0.8f};
    static constexpr float c_silenceRms{1e-6f};
    static constexpr float c_minCumulativeSum{1e-10f};

    explicit YinPitchDetector(const float sampleRate, const float minFreq = 80.0f, const float maxFreq = 1000.0f,
                              const float hopFrequency = 50.0f, const YinPitchOptions options = {})
        : m_decimation(std::max<size_t>(options.decimation, 1))
        , m_decimatedRate(sampleRate / static_cast<float>(m_decimation))
        , m_minLag(std::max<size_t>(2, static_cast<size_t>(m_decimatedRate / maxFreq)))
        , m_maxLag(std::max<size_t>(m_minLag + 3, static_cast<size_t>(m_decimatedRate / minFreq) + 2))
        , m_windowSize(2 * m_maxLag)
        , m_hopSize(std::max<size_t>(1, static_cast<size_t>(sampleRate / hopFrequency)))
        , m_ring(2 * m_windowSize, 0.0f)
        , m_cmndf(m_maxLag + 1, 1.0f)
    {
        if (m_decimation > 1)
        {
            const float cutoff = 0.4f * m_decimatedRate;
            m_antiAlias.setSection(0, designBiquad({BiquadFilterType::LowPass, cutoff, 0.5412f, 0.0f}, sampleRate));
            m_antiAlias.setSection(1, designBiquad({BiquadFilterType::LowPass, cutoff, 1.3066f, 0.0f}, sampleRate));
        }
    }

    /// Clears the window, the filter and the result.
    void reset() noexcept
    {
        std::fill(m_ring.begin(), m_ring.end(), 0.0f);
        m_antiAlias.reset();
        m_writeIndex = 0;
        m_decimationPhase = 0;
        m_hopCounter = 0;
        m_inputCount = 0;
        m_newPitch = false;
        m_pitch = 0.0f;
        m_confidence = 0.0f;
        m_level = 0.0f;
        m_lastWindowEnd = 0;
    }

    /// True on the sample after which a new reading (voiced or not) is available.
    [[nodiscard]] bool hasNewPitch() const noexcept
    {
        return m_newPitch;
    }

    /// Takes one input sample; returns the latest pitch in Hz (0 when the last reading was unvoiced).
    float step(const float in) noexcept
    {
        m_newPitch = false;
        ++m_inputCount;
        pushSample(in);
        if (++m_hopCounter >= m_hopSize)
        {
            m_hopCounter = 0;
            analyse();
        }
        return m_pitch;
    }

    /// Runs a block, writing the latest pitch after every sample.
    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            out[i] = step(in[i]);
        }
    }

    [[nodiscard]] float getCurrentPitch() const noexcept
    {
        return m_pitch;
    }

    /// 1 - CMNDF at the chosen lag, 0 when the reading was unvoiced.
    [[nodiscard]] float getLastConfidence() const noexcept
    {
        return m_confidence;
    }

    /// RMS of the window of the last reading, linear.
    [[nodiscard]] float getLastLevel() const noexcept
    {
        return m_level;
    }

    /// Input samples consumed when the window of the last reading was taken (the window ends at the newest of them).
    [[nodiscard]] uint64_t getLastWindowEnd() const noexcept
    {
        return m_lastWindowEnd;
    }

    /// Input samples from the newest sample of a window to its centre.
    [[nodiscard]] size_t getLatencySamples() const noexcept
    {
        return m_maxLag * m_decimation;
    }

    /// Input samples between two readings.
    [[nodiscard]] size_t getHopSize() const noexcept
    {
        return m_hopSize;
    }

    /// Samples in the analysis window, at the decimated rate.
    [[nodiscard]] size_t getWindowSize() const noexcept
    {
        return m_windowSize;
    }

    [[nodiscard]] size_t getMaxLag() const noexcept
    {
        return m_maxLag;
    }

  private:
    void pushSample(const float in) noexcept
    {
        float x = in;
        if (m_decimation > 1)
        {
            x = m_antiAlias.process(in);
            if (++m_decimationPhase < m_decimation)
            {
                return;
            }
            m_decimationPhase = 0;
        }
        // double-written ring: the newest m_windowSize samples are contiguous from m_writeIndex (the oldest)
        m_ring[m_writeIndex] = x;
        m_ring[m_writeIndex + m_windowSize] = x;
        m_writeIndex = m_writeIndex + 1 == m_windowSize ? 0 : m_writeIndex + 1;
    }

    /// Takes the window at the hop and runs the whole analysis on it.
    void analyse() noexcept
    {
        m_lastWindowEnd = m_inputCount;
        m_source = &m_ring[m_writeIndex];
        float sumSquares = 0.0f;
        for (size_t i = 0; i < m_windowSize; ++i)
        {
            sumSquares += m_source[i] * m_source[i];
        }
        m_level = std::sqrt(sumSquares / static_cast<float>(m_windowSize));
        if (m_level <= c_silenceRms)
        {
            publish(0.0f, 0.0f);
            return;
        }
        m_cmndf[0] = 1.0f;
        float cumulativeSum = 0.0f;
        for (size_t lag = 1; lag <= m_maxLag; ++lag)
        {
            const float difference = differenceAt(lag);
            cumulativeSum += difference;
            m_cmndf[lag] =
                cumulativeSum > c_minCumulativeSum ? difference * static_cast<float>(lag) / cumulativeSum : 1.0f;
            // the dip at lag - 1 is known to be a local minimum once its upper neighbour exists
            const size_t candidate = lag - 1;
            if (candidate >= m_minLag && candidate < m_maxLag && m_cmndf[candidate] < c_threshold &&
                m_cmndf[candidate] <= m_cmndf[candidate - 1] && m_cmndf[candidate] <= m_cmndf[lag])
            {
                finish(candidate);
                return;
            }
        }
        const auto first = m_cmndf.begin() + static_cast<std::ptrdiff_t>(m_minLag);
        const auto last = m_cmndf.begin() + static_cast<std::ptrdiff_t>(m_maxLag);
        const auto minimum = std::min_element(first, last);
        if (minimum != last && *minimum < c_minimumQuality)
        {
            finish(static_cast<size_t>(minimum - m_cmndf.begin()));
        }
        else
        {
            publish(0.0f, 0.0f);
        }
    }

    /// The difference function at one lag over the first m_maxLag samples; four partial sums so the loop can be
    /// vectorised without reordering floating point sums.
    [[nodiscard]] float differenceAt(const size_t lag) const noexcept
    {
        const float* a = m_source;
        const float* b = m_source + lag;
        float s0 = 0.0f;
        float s1 = 0.0f;
        float s2 = 0.0f;
        float s3 = 0.0f;
        size_t j = 0;
        for (; j + 4 <= m_maxLag; j += 4)
        {
            const float e0 = a[j] - b[j];
            const float e1 = a[j + 1] - b[j + 1];
            const float e2 = a[j + 2] - b[j + 2];
            const float e3 = a[j + 3] - b[j + 3];
            s0 += e0 * e0;
            s1 += e1 * e1;
            s2 += e2 * e2;
            s3 += e3 * e3;
        }
        for (; j < m_maxLag; ++j)
        {
            const float e = a[j] - b[j];
            s0 += e * e;
        }
        return (s0 + s1) + (s2 + s3);
    }

    void finish(const size_t lag) noexcept
    {
        const float confidence = std::clamp(1.0f - m_cmndf[lag], 0.0f, 1.0f);
        const float refined = interpolate(lag);
        publish(m_decimatedRate / refined, confidence);
    }

    /// Parabolic interpolation of the minimum, clamped to half a sample: a flat curve around the lag would otherwise
    /// send the estimate far off.
    [[nodiscard]] float interpolate(const size_t lag) const noexcept
    {
        if (lag == 0 || lag >= m_maxLag)
        {
            return static_cast<float>(lag);
        }
        const float y1 = m_cmndf[lag - 1];
        const float y2 = m_cmndf[lag];
        const float y3 = m_cmndf[lag + 1];
        const float a = (y1 - 2.0f * y2 + y3) / 2.0f;
        const float b = (y3 - y1) / 2.0f;
        if (std::abs(a) < 1e-6f)
        {
            return static_cast<float>(lag);
        }
        return static_cast<float>(lag) + std::clamp(-b / (2.0f * a), -0.5f, 0.5f);
    }

    void publish(const float pitch, const float confidence) noexcept
    {
        m_pitch = pitch;
        m_confidence = confidence;
        m_newPitch = true;
    }

    const size_t m_decimation;
    const float m_decimatedRate;
    const size_t m_minLag;
    const size_t m_maxLag;
    const size_t m_windowSize;
    const size_t m_hopSize;

    BiquadCascade<2> m_antiAlias;
    std::vector<float> m_ring;
    std::vector<float> m_cmndf;

    const float* m_source{nullptr};
    size_t m_writeIndex{0};
    size_t m_decimationPhase{0};
    size_t m_hopCounter{0};
    uint64_t m_inputCount{0};
    uint64_t m_lastWindowEnd{0};
    bool m_newPitch{false};
    float m_pitch{0.0f};
    float m_confidence{0.0f};
    float m_level{0.0f};
};

}
