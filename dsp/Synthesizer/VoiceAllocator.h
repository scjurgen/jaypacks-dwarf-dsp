#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace jp
{

/// Which voice a note was given to, and the note that was played before it (for glide).
struct VoiceAssignment
{
    size_t voice{0};
    int previousNote{0};
};

/**
 * @brief Assigns notes to a fixed pool of Voices voices and remembers which (channel, note) each one plays.
 *
 * allocate() takes the lowest-numbered voice that is silent, else steals the one assigned longest ago; the
 * caller starts the voice, which should ramp from its current level rather than jump (the amplitude envelope's
 * retrigger does). Whether a voice is silent is the caller's knowledge, passed as a function of the index, and
 * includes its release tail: a voice in its release is not free. The assignment is recorded with a running
 * number, so ages are exact however long the plugin runs. release and per-channel dispatch hand out the voices
 * by (channel, note) or by channel (an MPE controller moves every note of its channel); a retrigger during a
 * release leaves two voices with the same (channel, note), and both are visited. previousNote is the last note
 * allocated (the glide start); it begins at 60 and returns there on clear(). No allocation.
 */
template <size_t Voices>
class VoiceAllocator
{
    static_assert(Voices > 0, "at least one voice");

  public:
    template <typename IsSilent>
    [[nodiscard]] VoiceAssignment allocate(const int channel, const int note, IsSilent&& isSilent) noexcept
    {
        size_t target = 0;
        uint64_t oldest = m_counter;
        for (size_t i = 0; i < Voices; ++i)
        {
            if (isSilent(i))
            {
                target = i;
                break;
            }
            if (m_state[i].assignedAt < oldest)
            {
                oldest = m_state[i].assignedAt;
                target = i;
            }
        }
        const VoiceAssignment assignment{target, m_lastNote};
        m_state[target] = State{channel, note, m_counter++};
        m_lastNote = note;
        return assignment;
    }

    /// Calls visit(voiceIndex) for every voice tagged with this channel and note.
    template <typename Visit>
    void forNote(const int channel, const int note, Visit&& visit) const
    {
        for (size_t i = 0; i < Voices; ++i)
        {
            if (m_state[i].channel == channel && m_state[i].note == note)
            {
                visit(i);
            }
        }
    }

    /// Calls visit(voiceIndex) for every voice tagged with this channel.
    template <typename Visit>
    void forChannel(const int channel, Visit&& visit) const
    {
        for (size_t i = 0; i < Voices; ++i)
        {
            if (m_state[i].channel == channel)
            {
                visit(i);
            }
        }
    }

    [[nodiscard]] int channelOf(const size_t voice) const noexcept
    {
        return m_state[voice].channel;
    }

    [[nodiscard]] int noteOf(const size_t voice) const noexcept
    {
        return m_state[voice].note;
    }

    void clear() noexcept
    {
        m_state = {};
        m_counter = 0;
        m_lastNote = 60;
    }

  private:
    struct State
    {
        int channel{-1};
        int note{-1};
        uint64_t assignedAt{0};
    };

    std::array<State, Voices> m_state{};
    uint64_t m_counter{0};
    int m_lastNote{60};
};

}
