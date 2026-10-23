#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "scripting/TimedEventBuffer.h"

namespace jp::script
{

/// A MIDI message of 1 to 3 bytes.
struct MidiBytes
{
    uint8_t status{0};
    uint8_t data1{0};
    uint8_t data2{0};
    uint8_t size{3};

    [[nodiscard]] bool isNoteOn() const noexcept
    {
        return (status & 0xF0) == 0x90 && data2 > 0;
    }

    /// A note off, or a note on with velocity 0.
    [[nodiscard]] bool isNoteOff() const noexcept
    {
        return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0);
    }

    /// CC 123 (all notes off) or CC 120 (all sound off).
    [[nodiscard]] bool isAllOff() const noexcept
    {
        return (status & 0xF0) == 0xB0 && (data1 == 123 || data1 == 120);
    }

    [[nodiscard]] uint8_t channel() const noexcept
    {
        return status & 0x0F;
    }
};

/**
 * @brief Which notes a producer of MIDI has switched on and not off yet, per channel, so that none can be left
 * hanging.
 */
class NoteGuard
{
  public:
    void noteOn(const uint8_t channel, const uint8_t note) noexcept
    {
        m_bits[channel & 15][(note & 127) >> 6] |= uint64_t{1} << (note & 63);
    }

    void noteOff(const uint8_t channel, const uint8_t note) noexcept
    {
        m_bits[channel & 15][(note & 127) >> 6] &= ~(uint64_t{1} << (note & 63));
    }

    void allOff(const uint8_t channel) noexcept
    {
        m_bits[channel & 15] = {};
    }

    [[nodiscard]] bool sounding(const uint8_t channel, const uint8_t note) const noexcept
    {
        return ((m_bits[channel & 15][(note & 127) >> 6] >> (note & 63)) & 1u) != 0;
    }

    [[nodiscard]] size_t count() const noexcept
    {
        size_t total = 0;
        for (const auto& channel : m_bits)
        {
            for (const uint64_t word : channel)
            {
                total += static_cast<size_t>(__builtin_popcountll(word));
            }
        }
        return total;
    }

    /// Calls visit(channel, note) for every sounding note (the notes stay marked).
    template <typename Visit>
    void forEach(Visit&& visit) const noexcept
    {
        for (uint8_t channel = 0; channel < 16; ++channel)
        {
            for (uint8_t note = 0; note < 128; ++note)
            {
                if (sounding(channel, note))
                {
                    visit(channel, note);
                }
            }
        }
    }

  private:
    std::array<std::array<uint64_t, 2>, 16> m_bits{};
};

/**
 * @brief MIDI that leaves a plugin at the sample it is meant for, never leaves a note hanging and never floods, for the
 * audio thread alone.
 *
 * schedule() takes a message with its absolute due sample (a script's answer: the time of the event it answers plus its
 * own offset plus the latency of the plugin); emit() is called for each stretch of audio the pedal processes and writes
 * what is due in it at the right frame through anything with send(frame, bytes, size) (the MidiOutput of the adapter).
 * Every note on that goes out is remembered until its note off has gone out too (a note on with velocity 0 counts as
 * one; CC 123 and CC 120 clear the channel), and panic() ends all of them at once, dropping what was still waiting: for
 * a script that was reloaded or failed, for bypass or a deactivation. A note off is never refused: it may push a later
 * message out of a full buffer, and the per-window budget of messages does not apply to it. Other messages beyond the
 * budget (a script in a loop) are dropped and counted. No allocation, no locks.
 */
template <size_t Capacity = 512>
class MidiScheduler
{
  public:
    /// At most `messages` messages other than note offs per `windowFrames` samples; the default is 128 per 128.
    void setBudget(const uint32_t messages, const uint32_t windowFrames) noexcept
    {
        m_budget = messages;
        m_windowFrames = windowFrames > 0 ? windowFrames : 1;
    }

    /// False if the message was dropped because the buffer was full (a note off always finds room).
    bool schedule(const uint64_t due, const MidiBytes& message) noexcept
    {
        return message.isNoteOff() ? m_buffer.insertEvicting(due, message) : m_buffer.insert(due, message);
    }

    /// Ends every sounding note at the sample `now` and drops everything that was waiting.
    void panic(const uint64_t now) noexcept
    {
        m_buffer.clear();
        m_guard.forEach([this, now](const uint8_t channel, const uint8_t note)
                        { m_buffer.insertEvicting(now, MidiBytes{static_cast<uint8_t>(0x80 | channel), note, 0, 3}); });
    }

    template <typename Output>
    void emit(Output& output, const uint64_t start, const uint32_t frames) noexcept
    {
        if (start >= m_windowStart + m_windowFrames)
        {
            m_windowStart = start;
            m_sent = 0;
        }
        m_buffer.emitDue(start, frames,
                         [this, &output](const uint32_t frame, const MidiBytes& message)
                         {
                             const bool off = message.isNoteOff();
                             if (!off && m_sent >= m_budget)
                             {
                                 ++m_refused;
                                 return;
                             }
                             const std::array<uint8_t, 3> bytes{message.status, message.data1, message.data2};
                             if (!output.send(frame, bytes.data(), message.size))
                             {
                                 return; // stays in the guard if it was a note on that went out before
                             }
                             if (!off)
                             {
                                 ++m_sent;
                             }
                             if (message.isNoteOn())
                             {
                                 m_guard.noteOn(message.channel(), message.data1);
                             }
                             else if (off)
                             {
                                 m_guard.noteOff(message.channel(), message.data1);
                             }
                             else if (message.isAllOff())
                             {
                                 m_guard.allOff(message.channel());
                             }
                         });
    }

    [[nodiscard]] const NoteGuard& guard() const noexcept
    {
        return m_guard;
    }

    [[nodiscard]] size_t pending() const noexcept
    {
        return m_buffer.size();
    }

    /// Messages that arrived after their time, were dropped by a full buffer, or were refused by the budget.
    [[nodiscard]] uint32_t late() const noexcept
    {
        return m_buffer.late();
    }

    [[nodiscard]] uint32_t dropped() const noexcept
    {
        return m_buffer.dropped();
    }

    [[nodiscard]] uint32_t refused() const noexcept
    {
        return m_refused;
    }

  private:
    TimedEventBuffer<MidiBytes, Capacity> m_buffer;
    NoteGuard m_guard;
    uint32_t m_budget{128};
    uint32_t m_windowFrames{128};
    uint64_t m_windowStart{0};
    uint32_t m_sent{0};
    uint32_t m_refused{0};
};

}
