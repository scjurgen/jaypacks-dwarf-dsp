#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "lv2/PedalServices.h"

namespace jp
{

/**
 * @brief Test plugin that only checks the host's paths: audio passes through unchanged, echoOut
 * repeats the echoIn value, seconds counts the audio time since reset(), and the MIDI input is
 * counted.
 *
 * With it a test on the device sees whether control output values reach the client, in which
 * message format and at which rate, how long a value set on an input takes to come back, and
 * which MIDI messages arrive: their number, the channels seen (bit n set for channel n + 1, so a
 * controller on MPE member channels 2 to 16 shows as 0xFFFE) and the last message. Messages of
 * a system kind (status 0xF0 and up) count as events but belong to no channel. seconds is the
 * sample count divided by the sample rate, kept as an integer count so it does not drift; as a float
 * it resolves about 60 us after 1000 s.
 *
 * Every MIDI message is passed on to the MIDI output at the frame it came in (midiSent counts them, midiDropped
 * those the host's buffer could not take), so a chain of two probes shows that MIDI leaves a plugin and arrives in
 * the next one. Every note on also becomes a job for the host's worker thread: work() adds one to workerJobs and
 * answers with the note, workResponse() on the audio thread adds one to workerAnswers and keeps the note
 * (workerNote), so the output counters show the way to the worker and back. Real-time safe (work() runs on the
 * worker thread and uses only atomics).
 */
class ProbePedal
{
  public:
    void setSampleRate(const float sampleRate) noexcept
    {
        m_sampleRate = sampleRate;
    }

    void reset() noexcept
    {
        m_samples = 0;
        m_midiEvents = 0;
        m_midiChannels = 0;
        m_midiBytes = {};
        m_midiSent = 0;
        m_workerJobs = 0;
        m_workerAnswers = 0;
        m_workerNote = 0;
    }

    void setMidiOutput(MidiOutput& output) noexcept
    {
        m_output = &output;
    }

    void setWorkScheduler(WorkScheduler& scheduler) noexcept
    {
        m_scheduler = &scheduler;
    }

    /// msg points at 3 readable bytes, those beyond size are 0 (lv2/Lv2Adapter.h).
    void midi(const uint8_t* msg, const uint32_t size) noexcept
    {
        ++m_midiEvents;
        if (msg[0] >= 0x80 && msg[0] < 0xF0)
        {
            m_midiChannels |= static_cast<uint32_t>(1u << (msg[0] & 0x0Fu));
        }
        m_midiBytes = {msg[0], msg[1], msg[2]};
        if (m_output->send(0, msg, size))
        {
            ++m_midiSent;
        }
        if ((msg[0] & 0xF0u) == 0x90u && msg[2] > 0)
        {
            const std::array<uint8_t, 3> job{msg[0], msg[1], msg[2]};
            m_scheduler->schedule(job.data(), static_cast<uint32_t>(job.size()));
        }
    }

    /// Worker thread: counts the job and answers with the note and the count so far.
    void work(const void* data, const uint32_t size, WorkResponder& responder) noexcept
    {
        std::array<uint8_t, 4> answer{};
        if (size >= 2)
        {
            answer[0] = static_cast<const uint8_t*>(data)[1];
        }
        answer[1] = static_cast<uint8_t>(m_workerJobs.fetch_add(1, std::memory_order_relaxed) + 1);
        responder.respond(answer.data(), static_cast<uint32_t>(answer.size()));
    }

    /// Audio thread, at the start of a later run().
    void workResponse(const void* data, const uint32_t size) noexcept
    {
        if (size >= 1)
        {
            m_workerNote = static_cast<const uint8_t*>(data)[0];
        }
        ++m_workerAnswers;
    }

    void setEchoIn(const float value) noexcept
    {
        m_echo = value;
    }

    void process(const float* in, float* out, const size_t n) noexcept
    {
        if (in != out)
        {
            std::copy(in, in + n, out);
        }
        m_samples += n;
    }

    [[nodiscard]] float getEchoOut() const noexcept
    {
        return m_echo;
    }

    [[nodiscard]] float getSeconds() const noexcept
    {
        return static_cast<float>(static_cast<double>(m_samples) / static_cast<double>(m_sampleRate));
    }

    [[nodiscard]] float getMidiEvents() const noexcept
    {
        return static_cast<float>(m_midiEvents);
    }

    [[nodiscard]] float getMidiChannels() const noexcept
    {
        return static_cast<float>(m_midiChannels);
    }

    [[nodiscard]] float getMidiStatus() const noexcept
    {
        return static_cast<float>(m_midiBytes[0]);
    }

    [[nodiscard]] float getMidiData1() const noexcept
    {
        return static_cast<float>(m_midiBytes[1]);
    }

    [[nodiscard]] float getMidiData2() const noexcept
    {
        return static_cast<float>(m_midiBytes[2]);
    }

    [[nodiscard]] float getMidiSent() const noexcept
    {
        return static_cast<float>(m_midiSent);
    }

    [[nodiscard]] float getMidiDropped() const noexcept
    {
        return static_cast<float>(m_output->dropped());
    }

    [[nodiscard]] float getWorkerJobs() const noexcept
    {
        return static_cast<float>(m_workerJobs.load(std::memory_order_relaxed));
    }

    [[nodiscard]] float getWorkerAnswers() const noexcept
    {
        return static_cast<float>(m_workerAnswers);
    }

    [[nodiscard]] float getWorkerNote() const noexcept
    {
        return static_cast<float>(m_workerNote);
    }

  private:
    float m_sampleRate{48000.0f};
    float m_echo{0.0f};
    uint64_t m_samples{0};
    uint32_t m_midiEvents{0};
    uint32_t m_midiChannels{0};
    std::array<uint8_t, 3> m_midiBytes{};
    MidiOutput* m_output{nullptr};
    WorkScheduler* m_scheduler{nullptr};
    uint32_t m_midiSent{0};
    std::atomic<uint32_t> m_workerJobs{0};
    uint32_t m_workerAnswers{0};
    uint32_t m_workerNote{0};
};

}
