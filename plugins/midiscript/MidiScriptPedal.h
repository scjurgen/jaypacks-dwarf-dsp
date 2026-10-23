#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "lv2/PedalServices.h"
#include "plugins/midiscript/MidiScript.h"
#include "scripting/MidiScheduler.h"
#include "scripting/ScriptDirectory.h"
#include "scripting/ScriptHost.h"
#include "scripting/TempoClock.h"
#include "scripting/Timers.h"

namespace jp
{

/// The entries of the Script port: the plugin's own scripts, then user-1 to user-8 of the user's folder.
struct MidiScriptEntry
{
    script::Origin origin;
    const char* name;
};

inline constexpr std::array<MidiScriptEntry, 14> c_midiScriptEntries{{
    {script::Origin::Factory, "passthrough"},
    {script::Origin::Factory, "arpeggiator"},
    {script::Origin::Factory, "chord-player"},
    {script::Origin::Factory, "scale-quantiser"},
    {script::Origin::Factory, "octave-doubler"},
    {script::Origin::Factory, "cc-remap"},
    {script::Origin::User, "user-1"},
    {script::Origin::User, "user-2"},
    {script::Origin::User, "user-3"},
    {script::Origin::User, "user-4"},
    {script::Origin::User, "user-5"},
    {script::Origin::User, "user-6"},
    {script::Origin::User, "user-7"},
    {script::Origin::User, "user-8"},
}};

/**
 * @brief A MIDI effect whose logic is a Lua script: MIDI in, MIDI out, no audio.
 *
 * The incoming messages go to the hooks of the selected script (OnNoteOn, OnCC ...), the tempo clock to OnTick, the
 * transport to OnTransportStart and OnTransportStop; what the script answers (Note, NoteOn, CC, Pass ...) comes back
 * as commands and leaves at an exact sample: the sample of the event whose hook called it, plus the delay the command
 * names, plus the latency of the plugin. The script runs in the host's worker thread (scripting/ScriptHost.h), a block
 * or two behind the audio; the latency (control, in ms) is what absorbs that, so the output is jitter free; a message
 * that still comes too late goes out at the start of the block it is found in and is counted (Late). Every note on is
 * remembered until its note off has gone out: a new script, a hook that fails, the Panic control and a
 * reactivation end all sounding notes.
 *
 * The clock runs free from activation (Sync = Free run) or while the host's transport rolls (Sync = Transport; the
 * Speed port carries the transport state, the Tempo port the board's tempo). Its first tick comes one latency after
 * the start, 24 ticks per quarter note.
 */
class MidiScriptPedal
{
  public:
    // --- lifecycle ---

    void setSampleRate(const float sampleRate)
    {
        m_sampleRate = sampleRate;
        m_clock.configure(static_cast<double>(sampleRate), midi_script::c_ticksPerQuarter);
        m_clock.setTempo(m_tempo);
        m_midi.setBudget(c_messagesPerWindow, static_cast<uint32_t>(sampleRate * c_windowSeconds));
        script::HostConfig config;
        config.factoryDir = m_factoryDirectory;
        config.userDir = m_userDirectory;
        config.sampleRate = static_cast<double>(sampleRate);
        config.timerCommandBase = midi_script::c_timerCommandBase;
        m_script.configure(config);
        updateLatency();
    }

    void reset()
    {
        m_ready = true;
        m_midi.panic(m_now);
        m_timers.clear();
        if (m_clock.running())
        {
            m_clock.stop();
        }
        m_seenFailures = m_script.hookFailures();
        activateScript();
    }

    // --- controls ---

    /// The Script port: 0 to 5 the plugin's scripts, 6 to 13 the user's user-1 to user-8.
    void setScript(const float value)
    {
        m_scriptIndex =
            static_cast<size_t>(std::clamp(std::lround(value), 0L, static_cast<long>(c_midiScriptEntries.size() - 1)));
        if (m_ready)
        {
            const MidiScriptEntry& entry = c_midiScriptEntries[m_scriptIndex];
            m_script.select(entry.origin, entry.name);
        }
    }

    void setTempo(const float beatsPerMinute)
    {
        m_tempo = beatsPerMinute;
        m_clock.setTempo(static_cast<double>(beatsPerMinute));
    }

    /// 0: the clock runs from activation, 1: only while the transport rolls.
    void setSync(const float value)
    {
        m_followTransport = value >= 0.5f;
    }

    /// The transport state the host reports through the port with the designation time:speed.
    void setSpeed(const float value)
    {
        m_rolling = value > 0.5f;
    }

    void setLatency(const float milliseconds)
    {
        m_latencyMs = milliseconds;
        updateLatency();
    }

    void setPanic(const float value)
    {
        const bool pressed = value >= 0.5f;
        if (pressed && !m_panicPressed)
        {
            panic();
        }
        m_panicPressed = pressed;
    }

    // --- control outputs ---

    /// How the selected script is: 0 idle, 1 ok, 2 file missing, 3 compile error, 4 runtime error, 5 over the budget,
    /// 6 out of memory (script::ScriptState).
    [[nodiscard]] float getScriptStatus() const noexcept
    {
        return static_cast<float>(m_script.state());
    }

    /// Notes that were switched on and are not off yet.
    [[nodiscard]] float getSounding() const noexcept
    {
        return static_cast<float>(m_midi.guard().count());
    }

    /// Messages that left after the sample they were meant for (the latency is too small for this host).
    [[nodiscard]] float getLate() const noexcept
    {
        return static_cast<float>(m_midi.late());
    }

    /// Messages lost: refused by the flood budget, evicted from a full buffer or refused by the host.
    [[nodiscard]] float getDropped() const noexcept
    {
        return static_cast<float>(m_midi.refused() + m_midi.dropped() + (m_out != nullptr ? m_out->dropped() : 0u) +
                                  m_script.droppedEvents() + m_script.droppedCommands());
    }

    // --- services of the adapter ---

    /// The folder of the bundle (with or without the trailing slash): the plugin's scripts are in its scripts folder.
    /// Before setSampleRate().
    void setBundlePath(const char* folder)
    {
        std::string base = folder != nullptr ? folder : "";
        while (base.size() > 1 && base.back() == '/')
        {
            base.pop_back();
        }
        setScriptDirectories(base + "/scripts", script::userScriptDirectory("midiscript"));
    }

    /// The folders of the plugin's and of the user's scripts. Before setSampleRate().
    void setScriptDirectories(const std::string& factory, const std::string& user)
    {
        m_factoryDirectory = factory;
        m_userDirectory = user;
    }

    void setMidiOutput(MidiOutput& output) noexcept
    {
        m_out = &output;
    }

    void setWorkScheduler(WorkScheduler& scheduler) noexcept
    {
        m_script.setWorkScheduler(scheduler);
    }

    /// The host's worker thread: loads and reloads the script and runs its hooks (scripting/ScriptHost.h).
    void work(const void* /*data*/, const uint32_t /*size*/, WorkResponder& /*responder*/) noexcept
    {
        m_script.work();
    }

    void workResponse(const void* /*data*/, const uint32_t /*size*/) noexcept {}

    // --- audio thread ---

    /// A message of the input at the sample the clock is at (the adapter cut the block here).
    void midi(const uint8_t* message, const uint32_t size) noexcept
    {
        MidiScriptEvent event;
        if (!midi_script::eventOf(message, size, event) || !m_script.wantsHook(midi_script::hookOf(event.kind)))
        {
            return;
        }
        m_script.pushEvent(event, m_now);
    }

    void process(const size_t n) noexcept
    {
        const auto frames = static_cast<uint32_t>(n);
        m_script.advance(frames);
        drain();
        runTransport();
        const uint64_t limit = m_now + frames + m_latencyFrames;
        script::announce(m_clock, m_timers, limit, [this](const script::CoreEvent& event) { forward(event); });
        m_midi.emit(*m_out, m_now, frames);
        m_now += frames;
    }

  private:
    static constexpr uint32_t c_messagesPerWindow{128};
    static constexpr double c_windowSeconds{0.0027};
    static constexpr uint8_t c_allNotesOff{123};

    void updateLatency()
    {
        m_latencyFrames = static_cast<uint64_t>(std::lround(static_cast<double>(m_latencyMs) * 0.001 * m_sampleRate));
        m_latencyFrames = std::max<uint64_t>(m_latencyFrames, 1);
    }

    /// Ends every sounding note now, drops what waited, and tells everything downstream (CC 123 on all channels).
    void panic() noexcept
    {
        m_midi.panic(m_now);
        m_timers.clear();
        for (uint8_t channel = 0; channel < 16; ++channel)
        {
            m_midi.schedule(m_now, script::MidiBytes{static_cast<uint8_t>(0xB0 | channel), c_allNotesOff, 0, 3});
        }
    }

    /// The selected script at activation: not in the audio thread, so it is loaded here and now. What its top level and
    /// OnStart called is taken by drain() at the first block.
    void activateScript()
    {
        const MidiScriptEntry& entry = c_midiScriptEntries[m_scriptIndex];
        m_script.select(entry.origin, entry.name);
        m_script.work();
        m_seenGeneration = 0;
    }

    void runTransport() noexcept
    {
        const bool shouldRun = !m_followTransport || m_rolling;
        if (shouldRun && !m_clock.running())
        {
            const uint64_t start = m_now + m_latencyFrames;
            m_clock.start(start);
            forward(script::CoreEvent{script::CoreEvent::Kind::TransportStart, 0, start});
        }
        else if (!shouldRun && m_clock.running())
        {
            m_clock.stop();
            forward(script::CoreEvent{script::CoreEvent::Kind::TransportStop, 0, m_now});
        }
    }

    void forward(const script::CoreEvent& event) noexcept
    {
        if (m_script.wantsCoreHook(event.kind))
        {
            m_script.pushCoreEvent(event);
        }
    }

    /// What the script has called since the last block. A script that took over (a first load, a reload) ends what the
    /// old one left. A hook that failed leaves the script in an unknown state: the notes end, the timers stop, and what
    /// was queued (the commands of the failed call among them) is dropped; the script then goes on with the next event.
    /// A file that does not load changes nothing here: the previous script keeps running (Script status says why).
    void drain() noexcept
    {
        const uint32_t ready = m_script.readyGeneration();
        if (ready != m_seenGeneration)
        {
            m_seenGeneration = ready;
            m_midi.panic(m_now);
            m_timers.clear();
        }
        const uint32_t failures = m_script.hookFailures();
        const bool failed = failures != m_seenFailures;
        if (failed)
        {
            m_seenFailures = failures;
            m_midi.panic(m_now);
            m_timers.clear();
        }

        MidiScriptCommand command;
        uint32_t generation = 0;
        uint64_t time = 0;
        while (m_script.peekCommand(command, generation, time))
        {
            m_script.discardCommand();
            if (!failed)
            {
                apply(command, time != 0 ? time : m_now);
            }
        }
        script::TimerRequest request;
        while (m_script.popTimerRequest(request))
        {
            if (!failed)
            {
                m_timers.apply(request, m_clock, m_now);
            }
        }
    }

    [[nodiscard]] uint64_t span(const float milliseconds, const float ticks) const noexcept
    {
        return m_clock.msToSamples(static_cast<double>(milliseconds)) +
               m_clock.ticksToSamples(static_cast<double>(ticks));
    }

    void apply(const MidiScriptCommand& command, const uint64_t stamp) noexcept
    {
        const uint64_t due = stamp + m_latencyFrames + span(command.delayMs, command.delayTicks);
        const script::MidiBytes message{command.bytes[0], command.bytes[1], command.bytes[2], command.size};
        switch (command.kind)
        {
            case MidiScriptCommand::Kind::Message:
                m_midi.schedule(due, message);
                break;
            case MidiScriptCommand::Kind::Note:
            {
                uint64_t length = span(command.lengthMs, command.lengthTicks);
                length = length == 0 ? m_clock.ticksToSamples(1.0) : length;
                length = std::max<uint64_t>(length, 1);
                if (m_midi.schedule(due, message))
                {
                    m_midi.schedule(
                        due + length,
                        script::MidiBytes{static_cast<uint8_t>(0x80 | (message.status & 0x0F)), message.data1, 64, 3});
                }
                break;
            }
            case MidiScriptCommand::Kind::Panic:
                panic();
                break;
        }
    }

    float m_sampleRate{48000.0f};
    float m_tempo{120.0f};
    float m_latencyMs{12.0f};
    uint64_t m_latencyFrames{576};
    bool m_followTransport{false};
    bool m_rolling{false};
    bool m_panicPressed{false};
    bool m_ready{false};
    size_t m_scriptIndex{0};
    std::string m_factoryDirectory;
    std::string m_userDirectory;
    uint64_t m_now{1};
    uint32_t m_seenGeneration{0};
    uint32_t m_seenFailures{0};
    MidiOutput* m_out{nullptr};
    script::TempoClock m_clock;
    script::TimerBank m_timers;
    script::MidiScheduler<512> m_midi;
    MidiScriptBinder m_binder;
    script::ScriptHost<MidiScriptEvent, MidiScriptCommand, 256, 512> m_script{m_binder};
};

}
