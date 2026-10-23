#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "scripting/Binding.h"
#include "scripting/TempoClock.h"

namespace jp::script
{

/// How many timers a script has.
inline constexpr size_t c_timerCount{8};

/// An event the core itself sends to the hooks of a script: a tick of the clock, the transport starting or stopping, a
/// timer that is due. `time` is the sample the thing is due at (it is announced a little before that).
struct CoreEvent
{
    enum class Kind : uint8_t
    {
        Tick,           ///< OnTick(tick)
        TransportStart, ///< OnTransportStart()
        TransportStop,  ///< OnTransportStop()
        Timer           ///< OnTimer(id)
    };

    Kind kind{Kind::Tick};
    uint32_t value{0}; ///< the tick index (low 32 bits) or the id of the timer
    uint64_t time{0};
};

/// What a script asked of a timer, as the worker passes it on to the audio thread.
struct TimerRequest
{
    uint8_t id{0};
    bool stop{false};
    bool repeat{false};
    double milliseconds{0.0};
    double ticks{0.0};
    uint64_t base{0};       ///< the sample of the event whose hook asked (0: not asked from a hook)
    uint32_t generation{0}; ///< the script that asked (ScriptHost::generation() when it did)
};

// -- the commands and hooks a plugin adds to its own tables to give its scripts timers and the clock
// ---------------------

inline constexpr std::array<ValueSpec, 1> c_timerIdArgs{
    {{"id", ValueKind::Integer, 0.0, static_cast<double>(c_timerCount - 1), 0.0, "which timer, 0 to 7"}}};

inline constexpr std::array<ValueSpec, 3> c_timerFields{{
    {"ms", ValueKind::Number, 0.0, 600000.0, 0.0, "delay in milliseconds"},
    {"ticks", ValueKind::Number, 0.0, 100000.0, 0.0,
     "delay in clock ticks, added to ms (the length of a tick at the present tempo)"},
    {"repeating", ValueKind::Flag, 0.0, 1.0, 0.0, "fire again and again after the same delay"},
}};

/// SetTimer and StopTimer: the last two entries of a plugin's command table, with HostConfig::timerCommandBase the
/// index of the first.
inline constexpr std::array<CommandSpec, 2> c_timerCommands{{
    {"SetTimer",
     "Starts (or restarts) a timer; its hook OnTimer(id) is called when it is due. The delay is the length of ms and "
     "ticks "
     "together and is fixed when the timer is set. It counts from the event whose hook sets it.",
     c_timerIdArgs.data(), 1, c_timerFields.data(), 3},
    {"StopTimer", "Stops a timer.", c_timerIdArgs.data(), 1, nullptr, 0},
}};

inline constexpr std::array<const char*, 1> c_tickArguments{"tick"};
inline constexpr std::array<const char*, 1> c_timerArguments{"id"};

/// OnTick, OnTransportStart, OnTransportStop and OnTimer, in the order of CoreEvent::Kind: append them to a plugin's
/// hook table to document them. They are called with times relative to their own sample: what a hook schedules is
/// relative to it.
inline constexpr std::array<HookSpec, 4> c_coreHooks{{
    {"OnTick", "A tick of the tempo clock (while the transport runs); tick counts from 0 at the start.",
     c_tickArguments.data(), 1},
    {"OnTransportStart", "The transport started; the clock begins with tick 0.", nullptr, 0},
    {"OnTransportStop", "The transport stopped.", nullptr, 0},
    {"OnTimer", "A timer set with SetTimer is due.", c_timerArguments.data(), 1},
}};

/**
 * @brief The timers of a script, run on the audio thread against the sample clock so that they are exact: a timer is
 * due at an absolute sample, announced (advance()) a little before it, as the ticks of the clock are.
 *
 * A repeating timer is due again one delay after the time it was due (it does not drift). A delay of less than 1 ms
 * is made 1 ms for a repeating timer, so a script cannot flood the worker. A timer set again is restarted, a timer that
 * is stopped does not fire. At most one announcement per timer and 64 per call, so a late call cannot loop.
 */
class TimerBank
{
  public:
    void apply(const TimerRequest& request, const TempoClock& clock, const uint64_t now) noexcept
    {
        Timer& timer = m_timers[request.id % c_timerCount];
        if (request.stop)
        {
            timer.active = false;
            return;
        }
        uint64_t delay = clock.msToSamples(request.milliseconds) + clock.ticksToSamples(request.ticks);
        if (request.repeat)
        {
            delay = std::max(delay, clock.msToSamples(1.0));
            delay = std::max<uint64_t>(delay, 1);
        }
        timer.active = true;
        timer.repeat = request.repeat;
        timer.period = delay;
        timer.due = (request.base != 0 ? request.base : now) + delay;
    }

    /// Calls fire(id, due) in the order of the times for every timer due before `limit`.
    template <typename Fire>
    void advance(const uint64_t limit, Fire&& fire) noexcept
    {
        for (size_t guard = 0; guard < 64; ++guard)
        {
            Timer* next = nullptr;
            for (Timer& timer : m_timers)
            {
                if (timer.active && timer.due < limit && (next == nullptr || timer.due < next->due))
                {
                    next = &timer;
                }
            }
            if (next == nullptr)
            {
                return;
            }
            fire(static_cast<uint8_t>(next - m_timers.data()), next->due);
            if (next->repeat)
            {
                next->due += next->period;
            }
            else
            {
                next->active = false;
            }
        }
    }

    /// Stops every timer (a new script took over).
    void clear() noexcept
    {
        for (Timer& timer : m_timers)
        {
            timer.active = false;
        }
    }

    [[nodiscard]] size_t active() const noexcept
    {
        return static_cast<size_t>(
            std::count_if(m_timers.begin(), m_timers.end(), [](const Timer& timer) { return timer.active; }));
    }

  private:
    struct Timer
    {
        bool active{false};
        bool repeat{false};
        uint64_t period{0};
        uint64_t due{0};
    };

    std::array<Timer, c_timerCount> m_timers{};
};


/**
 * Makes the ticks of the clock and the timers that are due before the sample `limit` and hands them to push() as
 * CoreEvents in the order of their times (a tick and a timer of the same time: the tick first), so that they can be
 * queued for a ScriptHost, whose queue of core events must be in that order. A pedal calls it once per stretch of audio
 * with the end of the stretch plus its lookahead as the limit. At most 256 events per call; more are lost (a limit that
 * far ahead is a bug).
 */
template <typename Push>
void announce(TempoClock& clock, TimerBank& timers, const uint64_t limit, Push&& push) noexcept
{
    std::array<CoreEvent, 256> events;
    size_t count = 0;
    const auto add = [&](const CoreEvent::Kind kind, const uint32_t value, const uint64_t time)
    {
        if (count == events.size())
        {
            return;
        }
        size_t at = count++;
        while (at > 0 && events[at - 1].time > time)
        {
            events[at] = events[at - 1];
            --at;
        }
        events[at] = CoreEvent{kind, value, time};
    };
    clock.advance(limit, [&](const uint64_t index, const uint64_t due)
                  { add(CoreEvent::Kind::Tick, static_cast<uint32_t>(index), due); });
    timers.advance(limit, [&](const uint8_t id, const uint64_t due) { add(CoreEvent::Kind::Timer, id, due); });
    for (size_t i = 0; i < count; ++i)
    {
        push(events[i]);
    }
}

}
