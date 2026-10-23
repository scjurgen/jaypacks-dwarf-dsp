#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jp::script
{

/**
 * @brief A tempo clock in samples, for the audio thread alone: ticks at a given number per quarter note, at the tempo
 * that is set when they are made, with their due time as an absolute sample.
 *
 * advance(limit, onTick) makes every tick due before the sample `limit` that was not made yet and calls
 * onTick(index, due) for it. A pedal passes the end of the block plus a lookahead as the limit, so a tick is announced
 * a little before it is due and the answer to it (which needs a round trip to a worker thread) can still be scheduled
 * for its exact sample. Ticks do not depend on how the host cuts its blocks: the same ticks come out for blocks of 1
 * sample and of 4096. A tempo change takes effect from the first tick after the ones already made, a lookahead later
 * than it was set. The due time of a tick is rounded down from the time of an anchor tick plus a number of periods, in
 * double precision (within one sample of the exact time over millions of ticks). start() begins at tick 0 at a given
 * sample, stop() halts, a later start() begins again at tick 0.
 */
class TempoClock
{
  public:
    static constexpr double c_minTempo{1.0};
    static constexpr double c_maxTempo{3000.0};

    void configure(const double sampleRate, const uint32_t ticksPerQuarter) noexcept
    {
        m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        m_ticksPerQuarter = std::max<uint32_t>(ticksPerQuarter, 1);
        m_period = periodOf(m_tempo);
    }

    void setTempo(const double beatsPerMinute) noexcept
    {
        if (!std::isfinite(beatsPerMinute))
        {
            return;
        }
        if (m_running && m_nextTick > m_anchorTick)
        {
            // the ticks made so far keep their times: the new period counts from the last of them
            m_anchorDue = dueOf(m_nextTick - 1);
            m_anchorTick = m_nextTick - 1;
        }
        m_tempo = std::clamp(beatsPerMinute, c_minTempo, c_maxTempo);
        m_period = periodOf(m_tempo);
    }

    [[nodiscard]] double tempo() const noexcept
    {
        return m_tempo;
    }

    /// Tick 0 is due at the sample `now`.
    void start(const uint64_t now) noexcept
    {
        m_running = true;
        m_nextTick = 0;
        m_anchorTick = 0;
        m_anchorDue = static_cast<double>(now);
    }

    void stop() noexcept
    {
        m_running = false;
    }

    [[nodiscard]] bool running() const noexcept
    {
        return m_running;
    }

    [[nodiscard]] double samplesPerTick() const noexcept
    {
        return m_period;
    }

    [[nodiscard]] uint32_t ticksPerQuarter() const noexcept
    {
        return m_ticksPerQuarter;
    }

    /// A length in ticks (quarter notes times ticksPerQuarter) in samples at the present tempo.
    [[nodiscard]] uint64_t ticksToSamples(const double ticks) const noexcept
    {
        return toSamples(ticks * samplesPerTick());
    }

    [[nodiscard]] uint64_t msToSamples(const double milliseconds) const noexcept
    {
        return toSamples(milliseconds * 0.001 * m_sampleRate);
    }

    template <typename OnTick>
    void advance(const uint64_t limit, OnTick&& onTick) noexcept
    {
        while (m_running && dueOf(m_nextTick) < static_cast<double>(limit))
        {
            onTick(m_nextTick, static_cast<uint64_t>(dueOf(m_nextTick)));
            ++m_nextTick;
        }
    }

  private:
    [[nodiscard]] double periodOf(const double tempo) const noexcept
    {
        return m_sampleRate * 60.0 / (tempo * static_cast<double>(m_ticksPerQuarter));
    }

    /// Computed from the anchor, not by adding up periods, so that the error does not grow with the number of ticks.
    [[nodiscard]] double dueOf(const uint64_t tick) const noexcept
    {
        return m_anchorDue + static_cast<double>(tick - m_anchorTick) * m_period;
    }

    static uint64_t toSamples(const double samples) noexcept
    {
        return samples > 0.0 && std::isfinite(samples) ? static_cast<uint64_t>(samples + 0.5) : 0;
    }

    double m_sampleRate{48000.0};
    uint32_t m_ticksPerQuarter{24};
    double m_tempo{120.0};
    bool m_running{false};
    double m_period{1000.0};
    uint64_t m_nextTick{0};
    uint64_t m_anchorTick{0};
    double m_anchorDue{0.0};
};

}
