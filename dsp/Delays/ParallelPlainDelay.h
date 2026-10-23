#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace jp
{

/**
 * @brief Block-copied multichannel delay: one independent whole-sample length per channel,
 * extra read heads at fixed offsets, and crossfaded length changes.
 *
 * Each channel is its own line; delays are whole samples with no interpolation. The write
 * position advances a block at a time and the first block is mirrored past the end once
 * per cycle, so a read that straddles the wrap is still one contiguous copy and the inner
 * loops carry no bounds test.
 *
 * Head 0 is the main head; heads 1 .. NumReadHeads - 1 sit at an offset relative to it
 * (setRelativeHead(), a positive offset is a shorter delay) and are advanced separately by
 * processHead(), which must be called once per processBlock() for every auxiliary head in
 * use. A delay of 0 returns the block just written.
 *
 * setSize() moves a line at once (a step in the output). startFade() instead keeps the old
 * read positions of every changed line and crossfades linearly from old to new over a
 * number of samples (a multiple of BlockSize), the same ramp on every head; lines whose
 * length does not change are not touched, and a new fade can only start once the running
 * one has ended. While both positions are read the line briefly comb-filters.
 *
 * Each head prefetches the cache lines of its next block (the write position too), which
 * hides memory latency when the lines are larger than the cache.
 *
 * The buffers are allocated in the constructor (maxSize rounded up to a whole number of
 * blocks); processBlock(), processHead(), setSize() and startFade() are real-time safe.
 */
template <size_t BlockSize, size_t Channels, size_t NumReadHeads = 1>
class ParallelPlainDelay
{
  public:
    static_assert(BlockSize > 0 && Channels >= 1 && NumReadHeads >= 1, "invalid geometry");

    using Block = std::array<std::array<float, BlockSize>, Channels>;

    explicit ParallelPlainDelay(const size_t maxSize)
        : m_maxSize(std::max((maxSize + BlockSize - 1) / BlockSize, size_t{2}) * BlockSize)
    {
        for (auto& channel : m_buffer)
        {
            channel.assign(m_maxSize + BlockSize, 0.0f);
        }
        m_width.fill(m_maxSize / 8);
        for (size_t c = 0; c < Channels; ++c)
        {
            placeHeads(c, m_read);
        }
    }

    /// Longest settable delay plus one (the ring length).
    [[nodiscard]] size_t getMaxSize() const noexcept
    {
        return m_maxSize;
    }

    [[nodiscard]] size_t getSize(const size_t channel) const noexcept
    {
        return m_width[channel];
    }

    /// Moves one line to a new length at once, clamped to getMaxSize() - 1. Cancels a fade
    /// running on that line.
    void setSize(const size_t channel, const size_t newSize) noexcept
    {
        m_width[channel] = std::min(newSize, m_maxSize - 1);
        m_fading[channel] = false;
        placeHeads(channel, m_read);
    }

    /// Offsets auxiliary head headIdx (1 .. NumReadHeads - 1) from the main head of one line;
    /// positive values read later-written samples (a shorter delay). The offset survives
    /// later length changes.
    void setRelativeHead(const size_t headIdx, const size_t channel, const int offset) noexcept
    {
        m_offset[headIdx][channel] = offset;
        m_read[headIdx][channel] = wrap(static_cast<long>(m_head) - static_cast<long>(m_width[channel]) + offset);
    }

    /// Starts a crossfade of every line whose entry in newSizes differs from its length
    /// (clamped as in setSize()). Returns false, changing nothing, while a fade is running or
    /// when no length differs. fadeSamples is rounded up to a whole number of blocks.
    bool startFade(const std::array<size_t, Channels>& newSizes, const size_t fadeSamples) noexcept
    {
        finishFadeIfDone();
        if (m_fadeActive)
        {
            return false;
        }
        bool any = false;
        for (size_t c = 0; c < Channels; ++c)
        {
            const size_t target = std::min(newSizes[c], m_maxSize - 1);
            if (target == m_width[c])
            {
                continue;
            }
            for (size_t h = 0; h < NumReadHeads; ++h)
            {
                m_oldRead[h][c] = m_read[h][c];
            }
            m_width[c] = target;
            placeHeads(c, m_read);
            m_fading[c] = true;
            any = true;
        }
        if (any)
        {
            m_fadeLength = std::max((fadeSamples + BlockSize - 1) / BlockSize, size_t{1}) * BlockSize;
            m_fadeDone = 0;
            m_fadeActive = true;
            m_fadeLastBlock = false;
        }
        return any;
    }

    /// True from startFade() until the block that completes the fade has been processed.
    [[nodiscard]] bool isFading() const noexcept
    {
        return m_fadeActive && !m_fadeLastBlock;
    }

    /// Samples of the running fade already processed (0 when none runs).
    [[nodiscard]] size_t getFadeDone() const noexcept
    {
        return m_fadeActive ? m_fadeDone : 0;
    }

    [[nodiscard]] size_t getFadeLength() const noexcept
    {
        return m_fadeLength;
    }

    /// Writes one block, reads the main heads and advances them.
    void processBlock(const Block& src, Block& target) noexcept
    {
        finishFadeIfDone();
        for (size_t c = 0; c < Channels; ++c)
        {
            std::copy(src[c].begin(), src[c].end(), m_buffer[c].begin() + static_cast<long>(m_head));
            if (m_head == 0)
            {
                std::copy_n(m_buffer[c].data(), BlockSize, m_buffer[c].data() + m_maxSize);
            }
        }
        if (m_fadeActive)
        {
            const float step = 1.0f / static_cast<float>(m_fadeLength);
            for (size_t i = 0; i < BlockSize; ++i)
            {
                m_rampIn[i] = static_cast<float>(m_fadeDone + i + 1) * step;
            }
            m_fadeDone += BlockSize;
            m_fadeLastBlock = m_fadeDone >= m_fadeLength;
        }
        readHead(0, target);
        m_head += BlockSize;
        if (m_head >= m_maxSize)
        {
            m_head = 0;
        }
        for (size_t c = 0; c < Channels; ++c)
        {
            prefetchBlock(m_buffer[c].data() + m_head, true);
        }
    }

    /// Reads and advances one auxiliary head; call once per processBlock(), after it.
    void processHead(const size_t headIdx, Block& target) noexcept
    {
        readHead(headIdx, target);
    }

    /// Clears the signal (lengths, offsets and the write position stay; a fade is finished).
    void clear() noexcept
    {
        for (auto& channel : m_buffer)
        {
            std::fill(channel.begin(), channel.end(), 0.0f);
        }
        m_fadeActive = false;
        m_fadeLastBlock = false;
        m_fadeDone = 0;
        m_fading.fill(false);
    }

  private:
    using Heads = std::array<std::array<size_t, Channels>, NumReadHeads>;

    [[nodiscard]] size_t wrap(long position) const noexcept
    {
        const auto ring = static_cast<long>(m_maxSize);
        position %= ring;
        if (position < 0)
        {
            position += ring;
        }
        return static_cast<size_t>(position);
    }

    // Heads are placed relative to the next block to be written: a delay of d returns, in
    // the next processBlock(), the sample written d samples earlier.
    void placeHeads(const size_t channel, Heads& heads) noexcept
    {
        const long base = static_cast<long>(m_head) - static_cast<long>(m_width[channel]);
        heads[0][channel] = wrap(base);
        for (size_t h = 1; h < NumReadHeads; ++h)
        {
            heads[h][channel] = wrap(base + m_offset[h][channel]);
        }
    }

    void readHead(const size_t headIdx, Block& target) noexcept
    {
        for (size_t c = 0; c < Channels; ++c)
        {
            const float* now = m_buffer[c].data() + m_read[headIdx][c];
            if (m_fadeActive && m_fading[c])
            {
                const float* old = m_buffer[c].data() + m_oldRead[headIdx][c];
                for (size_t i = 0; i < BlockSize; ++i)
                {
                    target[c][i] = old[i] + m_rampIn[i] * (now[i] - old[i]);
                }
                advance(m_oldRead[headIdx][c]);
            }
            else
            {
                std::copy_n(now, BlockSize, target[c].begin());
            }
            advance(m_read[headIdx][c]);
            prefetchBlock(m_buffer[c].data() + m_read[headIdx][c], false);
        }
    }

    // The positions of the next block are known one block ahead; with lines larger than the
    // cache every block would otherwise wait for memory on each line and head. A block may
    // straddle two cache lines, so both ends are requested.
    static void prefetchBlock(const float* first, const bool forWriting) noexcept
    {
#if defined(__GNUC__) || defined(__clang__)
        if (forWriting)
        {
            __builtin_prefetch(first, 1);
            __builtin_prefetch(first + BlockSize - 1, 1);
        }
        else
        {
            __builtin_prefetch(first, 0);
            __builtin_prefetch(first + BlockSize - 1, 0);
        }
#else
        static_cast<void>(first);
        static_cast<void>(forWriting);
#endif
    }

    void advance(size_t& position) const noexcept
    {
        position += BlockSize;
        if (position >= m_maxSize)
        {
            position -= m_maxSize;
        }
    }

    void finishFadeIfDone() noexcept
    {
        if (m_fadeActive && m_fadeLastBlock)
        {
            m_fadeActive = false;
            m_fadeLastBlock = false;
            m_fadeDone = 0;
            m_fading.fill(false);
        }
    }

    size_t m_maxSize;
    size_t m_head{0};
    std::array<size_t, Channels> m_width{};
    std::array<std::array<int, Channels>, NumReadHeads> m_offset{};
    Heads m_read{};
    Heads m_oldRead{};
    std::array<bool, Channels> m_fading{};
    std::array<float, BlockSize> m_rampIn{};
    size_t m_fadeLength{BlockSize};
    size_t m_fadeDone{0};
    bool m_fadeActive{false};
    bool m_fadeLastBlock{false};
    std::array<std::vector<float>, Channels> m_buffer;
};

}
