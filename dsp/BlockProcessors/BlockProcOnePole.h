#pragma once

#include <array>
#include <cstddef>

#include "dsp/BlockProcessors/BlockProcessorBase.h"
#include "dsp/Filters/OnePoleHighpass.h"
#include "dsp/Filters/OnePoleLowpass.h"

namespace jp
{

/// Block form of OnePoleLowpass (filter contract there), with the cutoff in Hz.
template <size_t BlockSize>
class BlockLowpass final : public BlockProcessorBase<BlockSize>
{
  public:
    explicit BlockLowpass(const float sampleRate, const float cutoffHz = 1000.0f) noexcept
        : m_sampleRate(sampleRate)
    {
        setCutoff(cutoffHz);
    }

    void setCutoff(const float cutoffHz) noexcept
    {
        m_filter.setPole(onePoleLowpassPole(cutoffHz, m_sampleRate));
    }

    void process(std::array<float, BlockSize>& block) noexcept override
    {
        for (auto& x : block)
        {
            x = m_filter.process(x);
        }
    }

    void reset() noexcept override
    {
        m_filter.reset();
    }

  private:
    float m_sampleRate;
    OnePoleLowpass m_filter;
};

/// Block form of OnePoleHighpass (filter contract there), with the cutoff in Hz.
template <size_t BlockSize>
class BlockHighpass final : public BlockProcessorBase<BlockSize>
{
  public:
    explicit BlockHighpass(const float sampleRate, const float cutoffHz = 20.0f) noexcept
        : m_sampleRate(sampleRate)
    {
        setCutoff(cutoffHz);
    }

    void setCutoff(const float cutoffHz) noexcept
    {
        m_filter.setPole(onePoleLowpassPole(cutoffHz, m_sampleRate));
    }

    void process(std::array<float, BlockSize>& block) noexcept override
    {
        for (auto& x : block)
        {
            x = m_filter.process(x);
        }
    }

    void reset() noexcept override
    {
        m_filter.reset();
    }

  private:
    float m_sampleRate;
    OnePoleHighpass m_filter;
};

}
