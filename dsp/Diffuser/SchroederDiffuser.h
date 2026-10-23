#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include "dsp/Diffuser/SchroederAllpass.h"
#include "dsp/Numbers/BulgeControl.h"
#include "dsp/Numbers/PrimeDispatcher.h"

namespace jp
{

/**
 * @brief Series chain of NumElements Schroeder allpass sections with distinct prime lengths.
 *
 * Each section multiplies the echo density of the one before it, so a chain reaches a
 * density that a single section could only approach with an impractically high gain and
 * audible ringing; the chain as a whole stays allpass. The lengths are spread between a
 * bottom and a top bound along a Bulge curve (default bulge 0.6, bounds 100 and 1000
 * samples) and then snapped to distinct primes (at least 11), so no two sections share a
 * period. All sections use the same gain.
 *
 * Setters re-spread the lengths; each section crossfades to its new length (see
 * SchroederAllPassSoftTransition). snapSizes() moves all sections at once (for setup).
 * The constructor allocates; everything else is real-time safe.
 * @see https://ccrma.stanford.edu/~jos/pasp/Schroeder_Allpass_Sections.html
 */
template <size_t MaxDelayLength, size_t NumElements, size_t BlockSize>
class SchroederDiffuser
{
  public:
    using Stage = SchroederAllPassSoftTransition<MaxDelayLength, BlockSize>;

    explicit SchroederDiffuser(const float sampleRate)
        : m_stages(makeStages(sampleRate, std::make_index_sequence<NumElements>{}))
    {
        Bulge::fillNormalizedTable(m_ratios.data(), NumElements, m_bulge);
        scaleDiffuser(true);
    }

    void setBulge(const float value) noexcept
    {
        m_bulge = value;
        Bulge::fillNormalizedTable(m_ratios.data(), NumElements, m_bulge);
        scaleDiffuser(false);
    }

    /// Longest section in samples (before prime snapping).
    void setTopSize(const size_t value) noexcept
    {
        m_topSize = value;
        scaleDiffuser(false);
    }

    /// Shortest section in samples (before prime snapping).
    void setBottomSize(const size_t value) noexcept
    {
        m_bottomSize = value;
        scaleDiffuser(false);
    }

    /// Both bounds in samples with one re-spread (one fade per section instead of two).
    void setSizes(const size_t bottom, const size_t top) noexcept
    {
        m_bottomSize = bottom;
        m_topSize = top;
        scaleDiffuser(false);
    }

    void setFeedback(const float newFeedback) noexcept
    {
        m_feedback = newFeedback;
        for (auto& stage : m_stages)
        {
            stage.setGain(m_feedback);
        }
    }

    /// Moves every section to its current target length at once.
    void snapSizes() noexcept
    {
        scaleDiffuser(true);
    }

    [[nodiscard]] size_t getStageSize(const size_t index) const noexcept
    {
        return m_stages[index].size();
    }

    void clear() noexcept
    {
        for (auto& stage : m_stages)
        {
            stage.clear();
        }
    }

    [[nodiscard]] float step(const float value) noexcept
    {
        float x = value;
        for (auto& stage : m_stages)
        {
            x = stage.step(x);
        }
        return x;
    }

    void processBlock(const float* source, float* target) noexcept
    {
        std::copy_n(source, BlockSize, target);
        for (auto& stage : m_stages)
        {
            stage.processBlockInplace(target);
        }
    }

  private:
    template <size_t... I>
    static std::array<Stage, NumElements> makeStages(const float sampleRate, std::index_sequence<I...>)
    {
        return {{(static_cast<void>(I), Stage{sampleRate})...}};
    }

    void scaleDiffuser(const bool fast) noexcept
    {
        std::array<size_t, NumElements> sourceSizes{};
        std::array<size_t, NumElements> primeValues{};
        for (size_t i = 0; i < NumElements; ++i)
        {
            const float span = static_cast<float>(m_topSize) - static_cast<float>(m_bottomSize);
            const float size = static_cast<float>(m_bottomSize) + span * m_ratios[i];
            sourceSizes[i] = size > 0.0f ? static_cast<size_t>(size) : size_t{0};
        }
        generateUniquePrimeSet<11>(sourceSizes.data(), primeValues.data(), NumElements);
        for (size_t i = 0; i < NumElements; ++i)
        {
            m_stages[i].setSize(primeValues[i], fast);
        }
    }

    static_assert(NumElements > 0, "a diffuser needs at least one section");

    size_t m_bottomSize{100};
    size_t m_topSize{1000};
    float m_feedback{0.0f};
    float m_bulge{0.6f};
    std::array<float, NumElements> m_ratios{};
    std::array<Stage, NumElements> m_stages;
};

}
