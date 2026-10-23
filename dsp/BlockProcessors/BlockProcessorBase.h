#pragma once

#include <array>
#include <cstddef>

namespace jp
{

/**
 * @brief Interface for a processor that transforms a block of samples in place.
 *
 * The block arrives as std::array<float, BlockSize>, so the loop bound is a compile-time
 * constant and the body stays vectorisable; the virtual call is paid once per block, not
 * per sample. process() must not allocate, lock or throw. reset() clears accumulated
 * state and defaults to a no-op, which is correct for stateless processors.
 */
template <size_t BlockSize>
class BlockProcessorBase
{
  public:
    virtual ~BlockProcessorBase() = default;
    virtual void process(std::array<float, BlockSize>& block) noexcept = 0;
    virtual void reset() noexcept {}
};

/**
 * @brief Fixed array of Order optional processor slots, applied in index order to the
 * matching rows of a block set.
 *
 * Slots hold non-owning pointers: the owner keeps the processors alive for as long as they
 * are registered. An empty slot costs one null check. One processor in several slots
 * shares its state between them. Slot assignment is not synchronised with
 * processCallbacks(): assign from the thread that processes, or before processing starts.
 */
template <size_t Order, size_t BlockSize>
class CallbackManager
{
  public:
    using Processor = BlockProcessorBase<BlockSize>;

    /// Out-of-range indices are ignored.
    void setCallback(const size_t index, Processor* processor) noexcept
    {
        if (index < Order)
        {
            m_processors[index] = processor;
        }
    }

    void removeCallback(const size_t index) noexcept
    {
        setCallback(index, nullptr);
    }

    [[nodiscard]] bool hasCallback(const size_t index) const noexcept
    {
        return index < Order && m_processors[index] != nullptr;
    }

    void processCallbacks(std::array<std::array<float, BlockSize>, Order>& rows) noexcept
    {
        for (size_t i = 0; i < Order; ++i)
        {
            if (m_processors[i] != nullptr)
            {
                m_processors[i]->process(rows[i]);
            }
        }
    }

    void reset() noexcept
    {
        for (auto* processor : m_processors)
        {
            if (processor != nullptr)
            {
                processor->reset();
            }
        }
    }

  private:
    std::array<Processor*, Order> m_processors{};
};

}
