#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/BlockProcessors/BlockProcessorBase.h"
#include "dsp/Delays/ParallelPlainDelay.h"
#include "dsp/Numbers/BulgeControl.h"
#include "dsp/Numbers/PrimeDispatcher.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Reverbs/WalshHadamard.h"

namespace jp
{

/**
 * @brief Feedback delay network of Order lines with a Walsh-Hadamard feedback matrix, block
 * processing, per-line processor hooks, stereo taps and crossfaded size changes.
 *
 * Every line receives the same input; the line outputs are mixed by the Hadamard matrix
 * scaled by 1 / sqrt(Order) (orthogonal, so lossless before the line gains) and fed back
 * with a per-line gain 0.001^(length / (fs T60)), which makes every line, whatever its
 * length, decay by 60 dB in the set time. The loop of a line is exactly its length: the
 * delay line itself is 2 blocks shorter, the feedback path supplies the 2 blocks.
 *
 * Line lengths are spread between a minimum and a maximum size in metres (sound at
 * 333.3 m/s) along a Bulge curve (index 0 shortest when max > min), snapped to primes and,
 * by default, made distinct. The lengths are limited to the line ceiling given at
 * construction and to at least 2 blocks plus the largest stereo offset plus one sample; at
 * the ceiling, lengths may coincide or not be prime.
 *
 * A processor registered for a line (setDelayCallback(), non-owning) transforms that line's
 * output block before it enters the matrix; the stereo taps read the line before it. A
 * processor that delays the line (a vibrato around a nominal delay) states that latency at
 * registration: the line's delay is shortened by it, so the loop, and the line gain computed
 * from it, stays at the designed length (the line's stereo taps then sound that much earlier).
 * The line keeps at least its stereo offset plus one sample of delay.
 *
 * Stereo: every line has a left and a right read tap, one of them at the line's delay and
 * the other up to 0.33 ms earlier (a random interaural offset per line from a seeded
 * Random); left and right are the sums of all taps.
 *
 * Size changes: setters only record the new values; the lengths are recomputed once at
 * the start of the next block. Before the first block after construction or reset() they
 * apply at once; later every changed line crossfades from its old to its new length
 * (setFadeTime(), 50 ms by default, see ParallelPlainDelay), and the feedback gain of a
 * changed line ramps from its old to its new value per block over the same time. Changes
 * that arrive during a fade wait until it has ended; then the newest values start the next
 * fade. A gain change (decay, fade) moves linearly across the next block, sample by sample.
 *
 * The constructor allocates (Order lines of the ceiling length); setters and the process
 * calls are real-time safe (a size change costs a pow() and a prime search per line).
 * @see https://ccrma.stanford.edu/~jos/pasp/FDN_Reverberation.html
 */
template <size_t Order, size_t BlockSize>
class FdnTank
{
  public:
    static_assert(Order == 4 || Order == 8 || Order == 16 || Order == 32, "order must be 4, 8, 16 or 32");

    static constexpr float c_speedOfSound{333.3f};
    static constexpr float c_maxItdSeconds{0.00033f};
    static constexpr float c_infiniteDecayMs{99999.0f};

    using Delay = ParallelPlainDelay<BlockSize, Order, 3>;
    using Processor = BlockProcessorBase<BlockSize>;
    using Rows = std::array<std::array<float, BlockSize>, Order>;

    explicit FdnTank(const float sampleRate, const float maxLineSeconds = 0.5f,
                     const uint64_t seed = Random::c_defaultSeed)
        : m_sampleRate(sampleRate)
        , m_maxItd(static_cast<size_t>(std::round(c_maxItdSeconds * sampleRate)))
        , m_minLine(2 * BlockSize + m_maxItd + 1)
        , m_maxLine(std::max(static_cast<size_t>(std::round(maxLineSeconds * sampleRate)), m_minLine + 1))
        , m_delay(m_maxLine)
        , m_random(seed)
    {
        initRandomItd();
        setFadeTime(0.05f);
        m_dirty = true;
        updateSizes();
    }

    // --- processors -----------------------------------------------------------------------

    /// Registers a processor for a line, with the delay in samples it adds (subtracted from
    /// the line's delay). Register before processing: the line length changes at once.
    void setDelayCallback(const size_t lineIndex, Processor* processor, const size_t latency = 0) noexcept
    {
        m_callbacks.setCallback(lineIndex, processor);
        if (lineIndex < Order)
        {
            m_latency[lineIndex] = processor != nullptr ? latency : 0;
            if (m_length[lineIndex] > 0) // before the first sizing the latency applies with it
            {
                m_delay.setSize(lineIndex, delayWidth(lineIndex, m_length[lineIndex]));
            }
        }
    }

    void removeDelayCallback(const size_t lineIndex) noexcept
    {
        setDelayCallback(lineIndex, nullptr);
    }

    /// Length of a line's delay in samples: the loop length minus the 2-block feedback path
    /// minus the registered latency.
    [[nodiscard]] size_t getDelayWidth(const size_t index) const noexcept
    {
        return delayWidth(index, m_length[index]);
    }

    // --- sizes and decay ------------------------------------------------------------------

    void setMinSize(const float meters) noexcept
    {
        m_minSize = meters;
        m_dirty = true;
    }

    void setMaxSize(const float meters) noexcept
    {
        m_maxSize = meters;
        m_dirty = true;
    }

    /// -1 .. 1, see Bulge.
    void setSpreadBulge(const float value) noexcept
    {
        m_bulge = value;
        m_dirty = true;
    }

    void setUniqueDelay(const bool value) noexcept
    {
        m_uniqueDelay = value;
        m_dirty = true;
    }

    /// Crossfade time of a size change, rounded up to whole blocks.
    void setFadeTime(const float seconds) noexcept
    {
        const auto samples = static_cast<size_t>(std::max(seconds, 0.0f) * m_sampleRate);
        m_fadeSamples = std::max((samples + BlockSize - 1) / BlockSize, size_t{1}) * BlockSize;
    }

    /// T60 in milliseconds; c_infiniteDecayMs and above hold the energy (gain 1 / sqrt(Order)).
    void setDecay(const float msecs) noexcept
    {
        m_msecs = msecs;
        for (size_t o = 0; o < Order; ++o)
        {
            m_gainNew[o] = gainFor(m_length[o]);
            m_gainOld[o] = gainFor(m_oldLength[o]);
            if (!m_fadeRunning)
            {
                m_gain[o] = m_gainNew[o];
            }
        }
    }

    /// Sets one line's loop length in samples at once (clamped as described above).
    void setDirectSize(const size_t index, const size_t samples) noexcept
    {
        if (index >= Order)
        {
            return;
        }
        m_length[index] = std::clamp(samples, m_minLine, m_maxLine);
        m_oldLength[index] = m_length[index];
        m_delay.setSize(index, delayWidth(index, m_length[index]));
        m_gainNew[index] = gainFor(m_length[index]);
        m_gainOld[index] = m_gainNew[index];
        m_gain[index] = m_gainNew[index];
        m_appliedGain[index] = m_gain[index];
    }

    /// Loop length in samples for a size in metres: clamped, then the next prime.
    [[nodiscard]] size_t computeSizeFromMeters(const float meters) const noexcept
    {
        const float raw = std::max(m_sampleRate * meters / c_speedOfSound, 0.0f);
        auto w = static_cast<size_t>(raw);
        w = std::clamp(w, std::max(size_t{11}, m_minLine), m_maxLine);
        return std::min(getUsefulPrime<11>(w), m_maxLine);
    }

    [[nodiscard]] size_t getLineLength(const size_t index) const noexcept
    {
        return m_length[index];
    }

    [[nodiscard]] float getLineGain(const size_t index) const noexcept
    {
        return m_gain[index];
    }

    [[nodiscard]] size_t getMinLineLength() const noexcept
    {
        return m_minLine;
    }

    [[nodiscard]] size_t getMaxLineLength() const noexcept
    {
        return m_maxLine;
    }

    /// Offsets (samples earlier than the line delay) of the left and right tap of a line.
    [[nodiscard]] std::array<size_t, 2> getItdTaps(const size_t index) const noexcept
    {
        return {m_itdTaps[0][index], m_itdTaps[1][index]};
    }

    [[nodiscard]] bool isFading() const noexcept
    {
        return m_fadeRunning;
    }

    /// Clears the signal and the processors' state; pending size changes apply at once.
    void reset() noexcept
    {
        m_delay.clear();
        for (auto& rows : m_lineOut)
        {
            for (auto& row : rows)
            {
                row.fill(0.0f);
            }
        }
        for (auto& row : m_feedValue)
        {
            row.fill(0.0f);
        }
        m_callbacks.reset();
        m_fadeRunning = false;
        m_oldLength = m_length;
        for (size_t o = 0; o < Order; ++o)
        {
            m_gainNew[o] = gainFor(m_length[o]);
            m_gainOld[o] = m_gainNew[o];
            m_gain[o] = m_gainNew[o];
        }
        m_appliedGain = m_gain;
        m_started = false;
    }

    // --- processing -----------------------------------------------------------------------

    void processBlockSplit(const float* in, float* left, float* right) noexcept
    {
        std::fill_n(left, BlockSize, 0.0f);
        std::fill_n(right, BlockSize, 0.0f);
        processBlockSplitAdd(in, left, right);
    }

    /// Adds one block of the stereo output to left and right.
    void processBlockSplitAdd(const float* in, float* left, float* right) noexcept
    {
        updateSizes();
        if (m_fadeRunning)
        {
            const float progress =
                static_cast<float>(m_delay.getFadeDone() + BlockSize) / static_cast<float>(m_delay.getFadeLength());
            for (size_t o = 0; o < Order; ++o)
            {
                m_gain[o] = m_gainOld[o] + std::min(progress, 1.0f) * (m_gainNew[o] - m_gainOld[o]);
            }
        }

        // a local copy: in may point anywhere, which would keep the compiler from vectorising
        // the loops below
        std::array<float, BlockSize> input;
        std::copy_n(in, BlockSize, input.begin());
        // the gain moves linearly across the block from the value the previous block ended
        // with, so a gain change has no step; a constant gain is applied exactly
        for (size_t o = 0; o < Order; ++o)
        {
            const float start = m_appliedGain[o];
            const float step = (m_gain[o] - start) / static_cast<float>(BlockSize);
            if (step == 0.0f)
            {
                for (size_t s = 0; s < BlockSize; ++s)
                {
                    m_inValue[o][s] = input[s] - m_feedValue[o][s] * start;
                }
            }
            else
            {
                for (size_t s = 0; s < BlockSize; ++s)
                {
                    m_inValue[o][s] = input[s] - m_feedValue[o][s] * (start + step * static_cast<float>(s + 1));
                }
            }
            m_appliedGain[o] = m_gain[o];
        }
        Rows& lineOut = m_lineOut[m_current];
        m_delay.processBlock(m_inValue, lineOut);
        m_callbacks.processCallbacks(lineOut);
        m_delay.processHead(1, m_tapLeft);
        m_delay.processHead(2, m_tapRight);

        // feedback for the next block from the line outputs of the previous one (the 2-block
        // delay of the feedback path), mixed row by row; the two output buffers alternate
        walshHadamardRows<Order, BlockSize>(m_lineOut[1 - m_current], m_feedValue);
        m_current = 1 - m_current;

        std::array<float, BlockSize> sumLeft{};
        std::array<float, BlockSize> sumRight{};
        for (size_t o = 0; o < Order; ++o)
        {
            for (size_t s = 0; s < BlockSize; ++s)
            {
                sumLeft[s] += m_tapLeft[o][s];
                sumRight[s] += m_tapRight[o][s];
            }
        }
        for (size_t s = 0; s < BlockSize; ++s)
        {
            left[s] += sumLeft[s];
            right[s] += sumRight[s];
        }

        if (m_fadeRunning && !m_delay.isFading())
        {
            m_fadeRunning = false;
            m_oldLength = m_length;
            m_gainOld = m_gainNew;
            m_gain = m_gainNew;
        }
        m_started = true;
    }

  private:
    [[nodiscard]] float gainFor(const size_t length) const noexcept
    {
        if (m_msecs >= c_infiniteDecayMs)
        {
            return m_feedbackGain;
        }
        const float seconds = static_cast<float>(length) / m_sampleRate;
        return std::pow(0.001f, seconds / (m_msecs / 1000.0f)) * m_feedbackGain;
    }

    void initRandomItd() noexcept
    {
        const auto span = static_cast<uint64_t>(2 * m_maxItd + 1);
        for (size_t o = 0; o < Order; ++o)
        {
            // top bits: the low bits of xoshiro256** are its weakest
            const auto itd = static_cast<long>((m_random.next() >> 32) % span) - static_cast<long>(m_maxItd);
            m_itdTaps[0][o] = itd < 0 ? static_cast<size_t>(-itd) : 0;
            m_itdTaps[1][o] = itd > 0 ? static_cast<size_t>(itd) : 0;
            m_delay.setRelativeHead(1, o, static_cast<int>(m_itdTaps[0][o]));
            m_delay.setRelativeHead(2, o, static_cast<int>(m_itdTaps[1][o]));
        }
    }

    [[nodiscard]] size_t delayWidth(const size_t index, const size_t length) const noexcept
    {
        const size_t width = length - 2 * BlockSize;
        const size_t shortest = m_maxItd + 1;
        return width > shortest + m_latency[index] ? width - m_latency[index] : std::min(width, shortest);
    }

    void computeTargetLengths(std::array<size_t, Order>& target) const noexcept
    {
        const float low = std::min(m_minSize, m_maxSize);
        const float high = std::max(m_minSize, m_maxSize);
        for (size_t i = 0; i < Order; ++i)
        {
            const float frac = static_cast<float>(i) / static_cast<float>(Order - 1);
            const float meters = m_minSize + Bulge::getValue(frac, m_bulge) * (m_maxSize - m_minSize);
            target[i] = computeSizeFromMeters(std::clamp(meters, low, high));
        }
        if (m_uniqueDelay)
        {
            ensureUniqueDiscreteSize(target.data(), Order);
        }
        for (auto& length : target)
        {
            length = std::clamp(length, m_minLine, m_maxLine);
        }
    }

    void updateSizes() noexcept
    {
        if (!m_dirty || (m_started && m_delay.isFading()))
        {
            return;
        }
        m_dirty = false;
        std::array<size_t, Order> target{};
        computeTargetLengths(target);
        if (!m_started)
        {
            for (size_t o = 0; o < Order; ++o)
            {
                setDirectSize(o, target[o]);
            }
            return;
        }
        if (target == m_length)
        {
            return;
        }
        std::array<size_t, Order> widths{};
        for (size_t o = 0; o < Order; ++o)
        {
            widths[o] = delayWidth(o, target[o]);
        }
        m_oldLength = m_length;
        m_gainOld = m_gain;
        m_length = target;
        for (size_t o = 0; o < Order; ++o)
        {
            m_gainNew[o] = gainFor(m_length[o]);
        }
        m_fadeRunning = m_delay.startFade(widths, m_fadeSamples);
        if (!m_fadeRunning)
        {
            m_gain = m_gainNew;
        }
    }

    float m_sampleRate;
    size_t m_maxItd;
    size_t m_minLine;
    size_t m_maxLine;
    float m_feedbackGain{1.0f / std::sqrt(static_cast<float>(Order))};
    float m_msecs{100.0f};
    float m_minSize{10.0f};
    float m_maxSize{20.0f};
    float m_bulge{-0.6f};
    bool m_uniqueDelay{true};
    bool m_dirty{false};
    bool m_started{false};
    bool m_fadeRunning{false};
    size_t m_fadeSamples{BlockSize};

    std::array<size_t, Order> m_length{};
    std::array<size_t, Order> m_oldLength{};
    std::array<size_t, Order> m_latency{};
    std::array<float, Order> m_gain{};
    std::array<float, Order> m_appliedGain{};
    std::array<float, Order> m_gainOld{};
    std::array<float, Order> m_gainNew{};
    std::array<std::array<size_t, Order>, 2> m_itdTaps{};

    Rows m_inValue{};
    std::array<Rows, 2> m_lineOut{};
    size_t m_current{0};
    Rows m_tapLeft{};
    Rows m_tapRight{};
    Rows m_feedValue{};

    Delay m_delay;
    Random m_random;
    CallbackManager<Order, BlockSize> m_callbacks{};
};

}
