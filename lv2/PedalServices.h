#pragma once

#include <cstdint>
#include <cstring>

#include "lv2/atom/atom.h"
#include "lv2/urid/urid.h"
#include "lv2/worker/worker.h"

namespace jp
{

/**
 * @brief The MIDI output port of a plugin, as the pedal sees it: a sink for messages of 1 to 3 bytes that the
 * adapter turns into atom events of the port's sequence.
 *
 * The adapter empties the sequence at the start of every run() (an output sequence that is not written each cycle
 * would repeat old events), and tells the sink where the current process() or midi() call starts in the block. A
 * frame passed to send() counts from there, so a pedal answering an input event in midi() uses frame 0 and keeps the
 * event's position, and a pedal that decides inside process() may place a message at any later sample of the call or
 * of the block. Events are stored in the order of their frames: a frame before the last one written is raised to it
 * and a frame past the end of the block is lowered to the last sample, so the sequence is always valid. A message
 * that does not fit into the host's buffer, has a size other than 1 to 3 bytes or has no connected port is dropped
 * and counted in dropped(), never blocking. Real-time safe; audio thread only.
 */
class MidiOutput
{
  public:
    /// Empties the sequence (null if the port is not connected) and sets the block length. The size the host stored
    /// in the atom before run() is the capacity of the buffer.
    void begin(LV2_Atom_Sequence* sequence, const LV2_URID sequenceType, const LV2_URID midiEventType,
               const uint32_t blockFrames) noexcept
    {
        m_sequence = sequence;
        m_midiEventType = midiEventType;
        m_blockFrames = blockFrames;
        m_callOffset = 0;
        m_lastFrame = 0;
        m_capacity = 0;
        if (sequence != nullptr)
        {
            m_capacity = sequence->atom.size;
            sequence->atom.type = sequenceType;
            sequence->atom.size = sizeof(LV2_Atom_Sequence_Body);
            sequence->body.unit = 0;
            sequence->body.pad = 0;
        }
    }

    /// Frame of the block at which the call that is about to start begins.
    void setCallOffset(const uint32_t offset) noexcept
    {
        m_callOffset = offset;
    }

    /// Writes a message at frame samples after the start of the current call. Returns false if it was dropped.
    bool send(const uint32_t frame, const uint8_t* message, const uint32_t size) noexcept
    {
        constexpr uint32_t c_eventHeader{static_cast<uint32_t>(sizeof(LV2_Atom_Event))};
        const uint32_t padded = (c_eventHeader + size + 7u) & ~7u;
        if (m_sequence == nullptr || size == 0 || size > 3 || m_blockFrames == 0 ||
            m_capacity < m_sequence->atom.size + padded)
        {
            ++m_dropped;
            return false;
        }
        uint32_t position = m_callOffset + frame;
        position = position >= m_blockFrames ? m_blockFrames - 1 : position;
        position = position < m_lastFrame ? m_lastFrame : position;
        m_lastFrame = position;
        auto* base = reinterpret_cast<uint8_t*>(m_sequence) + sizeof(LV2_Atom) + m_sequence->atom.size;
        auto* event = reinterpret_cast<LV2_Atom_Event*>(base);
        event->time.frames = position;
        event->body.type = m_midiEventType;
        event->body.size = size;
        std::memcpy(base + c_eventHeader, message, size);
        m_sequence->atom.size += padded;
        return true;
    }

    /// Messages dropped since the sink was created.
    [[nodiscard]] uint32_t dropped() const noexcept
    {
        return m_dropped;
    }

  private:
    LV2_Atom_Sequence* m_sequence{nullptr};
    LV2_URID m_midiEventType{0};
    uint32_t m_blockFrames{0};
    uint32_t m_callOffset{0};
    uint32_t m_lastFrame{0};
    uint32_t m_capacity{0};
    uint32_t m_dropped{0};
};

/**
 * @brief Start of a job for the host's worker thread (LV2 worker extension), called from the audio thread.
 *
 * The host copies the job, runs the pedal's work() on its worker thread and hands the answer to workResponse() at
 * the start of a later run() (never in the one that scheduled it). Both rings of the host are small: jobs of a few
 * kB and more are refused, and a worker that finishes many jobs within one period loses the answers beyond a few
 * dozen (measured on the Dwarf, docs/device.md), so a job is a wake-up or a small message, and bulk data travels
 * through queues of the pedal. schedule() returns false if the host refused the job; it does not block.
 */
class WorkScheduler
{
  public:
    void setSchedule(const LV2_Worker_Schedule* schedule) noexcept
    {
        m_schedule = schedule;
    }

    bool schedule(const void* data, const uint32_t size) noexcept
    {
        return m_schedule != nullptr && m_schedule->schedule_work(m_schedule->handle, size, data) == LV2_WORKER_SUCCESS;
    }

  private:
    const LV2_Worker_Schedule* m_schedule{nullptr};
};

/**
 * @brief The answer channel of one work() call: respond() hands a message to the host, which delivers it to the
 * pedal's workResponse() on the audio thread. Returns false if the host's response ring is full (the answer is
 * lost). Valid only inside the work() call it was passed to.
 */
class WorkResponder
{
  public:
    WorkResponder(const LV2_Worker_Respond_Function function, const LV2_Worker_Respond_Handle handle) noexcept
        : m_function(function)
        , m_handle(handle)
    {
    }

    bool respond(const void* data, const uint32_t size) noexcept
    {
        return m_function != nullptr && m_function(m_handle, size, data) == LV2_WORKER_SUCCESS;
    }

  private:
    LV2_Worker_Respond_Function m_function;
    LV2_Worker_Respond_Handle m_handle;
};

}
