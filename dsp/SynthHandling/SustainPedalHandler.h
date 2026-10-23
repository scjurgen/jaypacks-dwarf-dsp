#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace jp
{

/// Receiver of the note events a SustainPedalHandler lets through.
class NoteListener
{
  public:
    virtual ~NoteListener() = default;
    virtual void noteOn(int channel, int note, int velocity) = 0;
    virtual void noteOff(int channel, int note, int velocity) = 0;
};

/**
 * @brief Tracks the notes that are held and defers their note-offs while the sustain pedal is down, for at most
 * MaxNotes notes at once.
 *
 * A note released under the pedal keeps sounding and ends when the pedal lifts, so its note-off is recorded
 * with its velocity and replayed then. Rules:
 *  - note on of a (channel, note) that is still active (no note-off in between, pedal or not) first sends the
 *    note-off of the old instance and frees its slot: otherwise the old slot stays active for ever, or a pedal
 *    lift would cut a note that was struck again after its first release;
 *  - note on with all slots in use sends the note-off of the oldest note (lowest count) and takes its slot;
 *  - note off without the pedal sends the note-off and frees the slot, under the pedal it marks the slot;
 *  - the pedal going down changes nothing, going up sends the marked note-offs (notes still held are kept);
 *  - allNotesOff() sends every note-off, clears the slots and the pedal.
 * The note on of the listener is called last, after any note-offs it caused. A note-off velocity that was never
 * given (a stolen note, allNotesOff) is the note-on velocity. A note-off of an unknown note is dropped. Notes are told
 * apart by channel and note number, so one note on two MPE channels is two notes. No allocation, no locks; the listener
 * is not owned and may be null (events are then only tracked).
 */
template <size_t MaxNotes>
class SustainPedalHandler
{
    static_assert(MaxNotes > 0, "at least one note");

  public:
    void setListener(NoteListener* listener) noexcept
    {
        m_listener = listener;
    }

    void noteOn(const int channel, const int note, const int velocity) noexcept
    {
        size_t slotIndex = findActive(channel, note);
        if (slotIndex != c_none)
        {
            release(slotIndex, m_slots[slotIndex].noteOffVelocity);
        }
        else
        {
            slotIndex = findFree();
            if (slotIndex == c_none)
            {
                slotIndex = findOldest();
                release(slotIndex, m_slots[slotIndex].noteOffVelocity);
            }
        }
        m_slots[slotIndex] = Slot{true, false, channel, note, velocity, m_counter++};
        if (m_listener != nullptr)
        {
            m_listener->noteOn(channel, note, velocity);
        }
    }

    void noteOff(const int channel, const int note, const int velocity) noexcept
    {
        const size_t slotIndex = findActive(channel, note);
        if (slotIndex == c_none)
        {
            return;
        }
        if (!m_sustainActive)
        {
            release(slotIndex, velocity);
        }
        else
        {
            m_slots[slotIndex].isScheduledForOff = true;
            m_slots[slotIndex].noteOffVelocity = velocity;
        }
    }

    void setSustain(const bool sustainOn) noexcept
    {
        if (m_sustainActive && !sustainOn)
        {
            for (size_t i = 0; i < MaxNotes; ++i)
            {
                if (m_slots[i].isUsed && m_slots[i].isScheduledForOff)
                {
                    release(i, m_slots[i].noteOffVelocity);
                }
            }
        }
        m_sustainActive = sustainOn;
    }

    [[nodiscard]] bool isSustaining() const noexcept
    {
        return m_sustainActive;
    }

    void allNotesOff() noexcept
    {
        for (size_t i = 0; i < MaxNotes; ++i)
        {
            if (m_slots[i].isUsed)
            {
                release(i, m_slots[i].noteOffVelocity);
            }
        }
        m_sustainActive = false;
        m_counter = 0;
    }

    /// Notes that are active: held, or released under the pedal.
    [[nodiscard]] size_t activeCount() const noexcept
    {
        size_t count = 0;
        for (const Slot& slot : m_slots)
        {
            count += slot.isUsed ? 1 : 0;
        }
        return count;
    }

  private:
    static constexpr size_t c_none{~size_t{0}};

    struct Slot
    {
        bool isUsed{false};
        bool isScheduledForOff{false};
        int channel{0};
        int note{0};
        int noteOffVelocity{0};
        uint64_t count{0};
    };

    [[nodiscard]] size_t findActive(const int channel, const int note) const noexcept
    {
        for (size_t i = 0; i < MaxNotes; ++i)
        {
            if (m_slots[i].isUsed && m_slots[i].channel == channel && m_slots[i].note == note)
            {
                return i;
            }
        }
        return c_none;
    }

    [[nodiscard]] size_t findFree() const noexcept
    {
        for (size_t i = 0; i < MaxNotes; ++i)
        {
            if (!m_slots[i].isUsed)
            {
                return i;
            }
        }
        return c_none;
    }

    /// The active slot with the lowest count; only called with every slot in use.
    [[nodiscard]] size_t findOldest() const noexcept
    {
        size_t oldest = 0;
        uint64_t lowest = std::numeric_limits<uint64_t>::max();
        for (size_t i = 0; i < MaxNotes; ++i)
        {
            if (m_slots[i].isUsed && m_slots[i].count < lowest)
            {
                lowest = m_slots[i].count;
                oldest = i;
            }
        }
        return oldest;
    }

    void release(const size_t slotIndex, const int velocity) noexcept
    {
        Slot& slot = m_slots[slotIndex];
        if (m_listener != nullptr)
        {
            m_listener->noteOff(slot.channel, slot.note, velocity);
        }
        slot.isUsed = false;
        slot.isScheduledForOff = false;
    }

    std::array<Slot, MaxNotes> m_slots{};
    NoteListener* m_listener{nullptr};
    bool m_sustainActive{false};
    uint64_t m_counter{0};
};

}
