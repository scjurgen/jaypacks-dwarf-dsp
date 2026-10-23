#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace jp::script
{

/**
 * @brief A fixed-size list of items waiting for an absolute sample time, kept in the order of their times, for the
 * audio thread alone.
 *
 * An item that is due inside the call being processed is handed out with the frame it is due at, so a message leaves at
 * its exact sample whatever the host's block size or the adapter's cuts. An item whose time has passed (it arrived too
 * late) is handed out at frame 0 and counted in late(). Items of the same time keep the order in which they were put
 * in. insert() on a full buffer drops the new item and counts it in dropped(); insertEvicting() instead makes room by
 * dropping the item that is due last. No allocation, no locks; the cost of an insert is the number of items due after
 * the new one.
 */
template <typename T, size_t Capacity>
class TimedEventBuffer
{
    static_assert(std::is_trivially_copyable_v<T>, "items are copied byte for byte");

  public:
    /// Puts an item in. False if the buffer was full (the item is dropped and counted).
    bool insert(const uint64_t due, const T& item) noexcept
    {
        if (m_count == Capacity)
        {
            ++m_dropped;
            return false;
        }
        place(due, item);
        return true;
    }

    /// Puts an item in; a full buffer drops the item that is due last (counted) to make room, unless that is the new
    /// item itself. Returns whether the new item is in.
    bool insertEvicting(const uint64_t due, const T& item) noexcept
    {
        if (m_count == Capacity)
        {
            if (due >= m_entries[m_count - 1].due)
            {
                ++m_dropped;
                return false;
            }
            --m_count;
            ++m_dropped;
        }
        place(due, item);
        return true;
    }

    /**
     * Hands out, in order, every item due before start + frames: emit(frame, item) with frame the offset from start
     * (0 for an item that was already due). Items are removed as they are handed out.
     */
    template <typename Emit>
    void emitDue(const uint64_t start, const uint32_t frames, Emit&& emit) noexcept
    {
        const uint64_t end = start + frames;
        size_t taken = 0;
        while (taken < m_count && m_entries[taken].due < end)
        {
            const Entry& entry = m_entries[taken];
            if (entry.due < start)
            {
                ++m_late;
            }
            emit(static_cast<uint32_t>(entry.due < start ? 0 : entry.due - start), entry.item);
            ++taken;
        }
        for (size_t i = taken; i < m_count; ++i)
        {
            m_entries[i - taken] = m_entries[i];
        }
        m_count -= taken;
    }

    /// Takes every item out without handing it out.
    void clear() noexcept
    {
        m_count = 0;
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return m_count;
    }

    /// Items handed out after their time had passed, since the buffer was made.
    [[nodiscard]] uint32_t late() const noexcept
    {
        return m_late;
    }

    /// Items refused or evicted because the buffer was full.
    [[nodiscard]] uint32_t dropped() const noexcept
    {
        return m_dropped;
    }

  private:
    struct Entry
    {
        uint64_t due;
        T item;
    };

    void place(const uint64_t due, const T& item) noexcept
    {
        size_t at = m_count;
        while (at > 0 && m_entries[at - 1].due > due)
        {
            m_entries[at] = m_entries[at - 1];
            --at;
        }
        m_entries[at] = Entry{due, item};
        ++m_count;
    }

    std::array<Entry, Capacity> m_entries{};
    size_t m_count{0};
    uint32_t m_late{0};
    uint32_t m_dropped{0};
};

}
