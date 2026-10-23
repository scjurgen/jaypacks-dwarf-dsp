#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

#include "dsp/Parameters/FixedSmoothing.h"

namespace jp
{

namespace detail
{

/// Position of each listed enumerator among the Ids, found by the enumerator's value; an enumerator that is not
/// listed has no position (c_unlisted). The positions are by declaration order, so the listed sources stay
/// contiguous whatever gaps the enumeration has.
template <typename IdType, IdType... Ids>
class SourceIndex
{
  public:
    static constexpr size_t c_count{sizeof...(Ids)};
    static constexpr size_t c_unlisted{~size_t{0}};

    SourceIndex() noexcept
    {
        m_position.fill(c_unlisted);
        size_t position = 0;
        ((m_position[static_cast<size_t>(Ids)] = position++), ...);
    }

    [[nodiscard]] size_t of(const IdType id) const noexcept
    {
        const auto value = static_cast<size_t>(id);
        return value < m_position.size() ? m_position[value] : c_unlisted;
    }

    [[nodiscard]] static constexpr bool distinct() noexcept
    {
        constexpr std::array<size_t, c_count> values{static_cast<size_t>(Ids)...};
        for (size_t i = 0; i < c_count; ++i)
        {
            for (size_t j = i + 1; j < c_count; ++j)
            {
                if (values[i] == values[j])
                {
                    return false;
                }
            }
        }
        return true;
    }

  private:
    static constexpr size_t c_maxId{std::max({static_cast<size_t>(Ids)...})};

    std::array<size_t, c_maxId + 1> m_position{};
};

}

/**
 * @brief A modulation target: a set of named sources combined by a formula, the result smoothed by FixedSmoothing.
 *
 * Ids lists, as enumerators, the sources this instance accepts (distinct, at least one); set() and setForced()
 * ignore any other enumerator. The formula is a plain function that reads the source values as a fixed-size
 * array in the order of Ids, not in the order of the enumerators' values. set() re-evaluates and ramps to the
 * result in NumSteps calls of get() (a continuous controller moving while a voice sounds); setForced() jumps
 * (a note trigger). The formula is a function pointer, so a captureless lambda with the exact signature does,
 * and nothing is allocated. Real-time safe.
 */
template <size_t NumSteps, typename IdType, IdType... Ids>
class ValueConnector : public FixedSmoothing<NumSteps>
{
    using Index = detail::SourceIndex<IdType, Ids...>;
    static_assert(sizeof...(Ids) > 0, "ValueConnector needs at least one source Id");
    static_assert(Index::distinct(), "the source Ids must be distinct");

  public:
    using Values = std::array<float, sizeof...(Ids)>;
    using Eval = float (*)(const Values&);

    explicit ValueConnector(const Eval evalFunction) noexcept
        : m_evalFunction(evalFunction)
    {
    }

    void set(const IdType id, const float value) noexcept
    {
        if (store(id, value))
        {
            FixedSmoothing<NumSteps>::setTarget(m_evalFunction(m_values));
        }
    }

    void setForced(const IdType id, const float value) noexcept
    {
        if (store(id, value))
        {
            FixedSmoothing<NumSteps>::forceTarget(m_evalFunction(m_values));
        }
    }

    [[nodiscard]] float get() noexcept
    {
        return FixedSmoothing<NumSteps>::getSmoothed();
    }

    [[nodiscard]] bool hasChangedValue() const noexcept
    {
        return FixedSmoothing<NumSteps>::isActive();
    }

  private:
    [[nodiscard]] bool store(const IdType id, const float value) noexcept
    {
        const size_t position = m_index.of(id);
        if (position == Index::c_unlisted)
        {
            return false;
        }
        m_values[position] = value;
        return true;
    }

    Eval m_evalFunction;
    Values m_values{};
    Index m_index;
};

/**
 * @brief ValueConnector with a two-stage formula: a fixed part that presetFast() recomputes, and a variable part
 * that every set() recomputes.
 *
 * Splits the sources that change only at a note trigger (base value, key, velocity) from those that move while
 * the voice sounds (controller dimensions, envelopes, an LFO), so the continuous path does not redo the trigger
 * part. The variable part receives the fixed part's last result. presetFast() evaluates both parts and jumps to
 * the result; set() ramps to the variable part evaluated with the fixed part from the last presetFast(), so
 * values stored by set() before the first presetFast() take effect at that call. There is no forced set():
 * store with set(), then presetFast(). Real-time safe.
 */
template <size_t NumSteps, typename IdType, IdType... Ids>
class ValueConnectorWithPreset : public FixedSmoothing<NumSteps>
{
    using Index = detail::SourceIndex<IdType, Ids...>;
    static_assert(sizeof...(Ids) > 0, "ValueConnectorWithPreset needs at least one source Id");
    static_assert(Index::distinct(), "the source Ids must be distinct");

  public:
    using Values = std::array<float, sizeof...(Ids)>;
    using EvalFixedPart = float (*)(const Values&);
    using EvalVarPart = float (*)(float fixedPart, const Values&);

    ValueConnectorWithPreset(const EvalFixedPart evalFixedPartFunction, const EvalVarPart evalFunction) noexcept
        : m_evalFixedPartFunction(evalFixedPartFunction)
        , m_evalFunction(evalFunction)
    {
    }

    void presetFast() noexcept
    {
        m_fixedValue = m_evalFixedPartFunction(m_values);
        FixedSmoothing<NumSteps>::forceTarget(m_evalFunction(m_fixedValue, m_values));
    }

    void set(const IdType id, const float value) noexcept
    {
        const size_t position = m_index.of(id);
        if (position == Index::c_unlisted)
        {
            return;
        }
        m_values[position] = value;
        FixedSmoothing<NumSteps>::setTarget(m_evalFunction(m_fixedValue, m_values));
    }

    [[nodiscard]] float get() noexcept
    {
        return FixedSmoothing<NumSteps>::getSmoothed();
    }

    [[nodiscard]] bool hasChangedValue() const noexcept
    {
        return FixedSmoothing<NumSteps>::isActive();
    }

  private:
    EvalFixedPart m_evalFixedPartFunction;
    EvalVarPart m_evalFunction;
    float m_fixedValue{0.0f};
    Values m_values{};
    Index m_index;
};

}
