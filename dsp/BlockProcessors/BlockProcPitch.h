#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "dsp/BlockProcessors/BlockProcessorBase.h"
#include "dsp/Delays/PitchFadeWindowDelay.h"
#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Numbers/Random.h"

namespace jp
{

/**
 * @brief Pitch shifter as a block processor: PitchFadeWindowDelay, a 12 kHz one-pole lowpass
 * on the shifted signal, and a pitched/plain mix.
 *
 * The lowpass takes the edge off the interpolation and grain-boundary residue, which sits
 * mostly at high frequencies, and is applied to the shifted signal only. Mix 0 is an
 * exact bypass, mix 1 is the shifted signal alone. The shifter runs with a window and a fade
 * of half its 4800-sample buffer (50 ms each at 48 kHz). Pitch, mix and reverse change at
 * once; real-time safe after construction.
 */
template <size_t BlockSize>
class BlockPitch final : public BlockProcessorBase<BlockSize>
{
  public:
    static constexpr size_t c_bufferSize{4800};
    static constexpr float c_lowpassHz{12000.0f};

    explicit BlockPitch(const float sampleRate, const uint64_t seed = Random::c_defaultSeed)
        : m_shifter(seed)
    {
        m_shifter.setSize(c_bufferSize / 2);
        m_shifter.setFadeTime(c_bufferSize / 2);
        m_lowpass.setPole(onePoleLowpassPole(c_lowpassHz, sampleRate));
        setPitch(0.0f);
    }

    void setPitch(const float semitones) noexcept
    {
        m_shifter.setPitch(semitones);
    }

    /// 0 .. 1: share of the shifted signal.
    void setPitchMix(const float value) noexcept
    {
        m_mixPitch = value;
        m_mixPlain = 1.0f - value;
    }

    void setReverse(const bool reverse) noexcept
    {
        m_shifter.setReverse(reverse);
    }

    void process(std::array<float, BlockSize>& block) noexcept override
    {
        for (auto& x : block)
        {
            const float shifted = m_lowpass.process(m_shifter.step(x));
            x = shifted * m_mixPitch + x * m_mixPlain;
        }
    }

    void reset() noexcept override
    {
        m_shifter.clear();
        m_lowpass.reset();
    }

  private:
    PitchFadeWindowDelay<c_bufferSize> m_shifter;
    OnePoleLowpass m_lowpass;
    float m_mixPitch{0.5f};
    float m_mixPlain{0.5f};
};

}
