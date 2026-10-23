#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace jp::script
{

/**
 * @brief Lock-free queue for exactly one producer thread and one consumer thread, with a fixed capacity.
 *
 * Capacity is a power of two and holds Capacity - 1 items (one slot tells full from empty). push() and pop() never
 * block, never allocate and are wait-free: a full queue refuses the item and counts it in dropped(), so a producer
 * that outruns its consumer loses items instead of waiting. The producer publishes an item with a release store of the
 * write index, the consumer acquires it, so an item is seen complete or not at all. T is copied byte for byte.
 */
template <typename T, size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "the capacity is a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "items are copied byte for byte");

  public:
    /// Producer thread only. Returns false (and counts a drop) if the queue is full.
    bool push(const T& item) noexcept
    {
        const size_t write = m_write.load(std::memory_order_relaxed);
        const size_t next = (write + 1) & c_mask;
        if (next == m_read.load(std::memory_order_acquire))
        {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        m_items[write] = item;
        m_write.store(next, std::memory_order_release);
        return true;
    }

    /// Consumer thread only. Returns false if the queue is empty.
    bool pop(T& item) noexcept
    {
        const size_t read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire))
        {
            return false;
        }
        item = m_items[read];
        m_read.store((read + 1) & c_mask, std::memory_order_release);
        return true;
    }

    /// Consumer thread only. Copies the oldest item without taking it; false if the queue is empty.
    bool peek(T& item) const noexcept
    {
        const size_t read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire))
        {
            return false;
        }
        item = m_items[read];
        return true;
    }

    /// Consumer thread only. Takes the oldest item away; does nothing if the queue is empty.
    void discard() noexcept
    {
        const size_t read = m_read.load(std::memory_order_relaxed);
        if (read != m_write.load(std::memory_order_acquire))
        {
            m_read.store((read + 1) & c_mask, std::memory_order_release);
        }
    }

    /// Items refused because the queue was full, since the queue was made.
    [[nodiscard]] uint32_t dropped() const noexcept
    {
        return m_dropped.load(std::memory_order_relaxed);
    }

    /// Items in the queue; exact only when no other thread is using it.
    [[nodiscard]] size_t size() const noexcept
    {
        return (m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_acquire)) & c_mask;
    }

  private:
    static constexpr size_t c_mask{Capacity - 1};

    std::array<T, Capacity> m_items{};
    alignas(64) std::atomic<size_t> m_write{0};
    alignas(64) std::atomic<size_t> m_read{0};
    std::atomic<uint32_t> m_dropped{0};
};

}
