#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "dsp/Analysis/PitchNoteTracker.h"
#include "dsp/Analysis/YinPitchDetector.h"
#include "lv2/PedalServices.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"
#include "plugins/shipahoy/ShipAhoyScript.h"
#include "scripting/ScriptDirectory.h"
#include "scripting/ScriptHost.h"
#include "scripting/TempoClock.h"
#include "scripting/TimedEventBuffer.h"
#include "scripting/Timers.h"

namespace jp
{

/// The entries of the Script port: the plugin's own scripts, then user-1 to user-8 of the user's folder.
struct ShipAhoyScriptEntry
{
    script::Origin origin;
    const char* name;
};

inline constexpr std::array<ShipAhoyScriptEntry, 17> c_shipAhoyScriptEntries{{
    {script::Origin::Factory, "hop"},
    {script::Origin::Factory, "sequence"},
    {script::Origin::Factory, "random-hold"},
    {script::Origin::Factory, "alternating"},
    {script::Origin::Factory, "follow"},
    {script::Origin::Factory, "answer"},
    {script::Origin::Factory, "echo"},
    {script::Origin::Factory, "slow-drift"},
    {script::Origin::Factory, "toggle-trill"},
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
 * @brief The Ship Ahoy plugin: a pole-mixing filter (ShipAhoyEngine) whose frequency, resonance, type and drift a Lua
 * script decides, in time with a tempo clock.
 *
 * The script is the engine: the plugin has no running state in which the filter moves without one. The face has Mix,
 * Tempo, Script (which script), X and Y; X and Y have no meaning of their own, each script decides what they do and
 * says so in its header comment (read with GetX() and GetY(), or told by OnControl(x, y)).
 *
 * One timing model for the clock:
 *   steps per second = tempo / 60 * (96 / step ticks) * speed ratio
 * The tempo is the Tempo port (with Sync = Transport the board's tempo, port Board tempo), the step the Step port (8/1
 * down to 1/64, 1/16 by default), the speed ratio a script's SpeedRatio command (1 otherwise). The clock runs from
 * activation (Sync = Free run) or while the transport rolls (Sync = Transport) and ticks 96 times per quarter note;
 * every step ticks apart it makes a step, the hook OnStep.
 *
 * The script runs in the host's worker thread, a block or two behind the audio. Like the clock of MIDI Script, this one
 * is a lead ahead of the audio: its events are announced early, a command comes back with the sample of the event
 * whose hook called it, and everything that command asks for happens at that sample plus the delay plus the lead. The
 * lead is not a setting: it is the round trip of the worker, which depends on the block size only, so it is
 * c_leadBlocks times the largest block the host has called run() with (the first block sets it, a larger one raises
 * it, it never shrinks until the next activation). All the clock's events are shifted by the same lead, so the steps
 * keep their distances; only where the first step falls depends on the block size. A command that still comes too late
 * (the first larger block of a host with changing block sizes, a worker that was not scheduled in time) is applied at
 * the start of the block it is found in and counted (Late).
 *
 * External events do not wait for the lead: a played note (OnNewPitch, OnPitchEnd), a move of X or Y (OnControl) and
 * the commands of a script's load are answered as fast as possible, at the first block boundary after the reply reaches
 * the audio thread (the stamp of the event, or now if that has passed), plus the command's own delay. Their timing thus
 * depends on the block size, by design.
 *
 * Played notes: while the selected script defines OnNewPitch or OnPitchEnd the input is also read by a YIN pitch
 * detector (80 Hz to 1 kHz, decimated by 4, a reading every 20 ms) and a PitchNoteTracker, and an accepted note becomes
 * OnNewPitch, the end of a note OnPitchEnd (documentation/YinPitch/). Without such a hook the detector does not run and
 * costs nothing. The input is read before the filter touches the buffer, so in place processing is safe. A pitch event
 * is stamped with the sample after which the reading was available; the note itself began about 80 ms before that.
 *
 * X and Y: the port values are stored for the getters at once; OnControl(x, y) is called at most once per block and
 * only when one of them changed, and only while the script defines it.
 *
 * A new script (selected, or reloaded from its file) starts with the defaults of resonance, type and drift and takes
 * over the frequency where it is. If a hook fails (an error, over its budget) the filter keeps its frequency, the
 * queued commands and the timers are dropped, and the script goes on with the next event. A file that does not load
 * changes nothing: the previous script keeps running.
 */
class ShipAhoyPedal
{
  public:
    static constexpr float c_defaultTempo{105.0f}; ///< 7 steps per second at 1/16
    /// The lead of the clock in blocks: the script runs a block or two behind the audio.
    static constexpr uint32_t c_leadBlocks{2};

    explicit ShipAhoyPedal(const uint64_t seedValue = Random::c_defaultSeed) noexcept
        : m_engine(seedValue)
    {
        m_clock.configure(static_cast<double>(m_sampleRate), ship_ahoy::c_ticksPerQuarter);
        updateClockTempo();
    }

    // --- lifecycle ---

    void setSampleRate(const float sampleRate)
    {
        m_sampleRate = sampleRate;
        m_engine.setSampleRate(sampleRate);
        m_clock.configure(static_cast<double>(sampleRate), ship_ahoy::c_ticksPerQuarter);
        updateClockTempo();
        m_detector.emplace(sampleRate, c_lowestHz, c_highestHz, c_pitchHopHz, YinPitchOptions{c_pitchDecimation});
        resetPitch();
        script::HostConfig config;
        config.factoryDir = m_factoryDirectory;
        config.userDir = m_userDirectory;
        config.sampleRate = static_cast<double>(sampleRate);
        config.timerCommandBase = ship_ahoy::c_timerCommandBase;
        m_script.configure(config);
    }

    /// Restarts the random sequences from a seed (see ShipAhoyEngine).
    void seed(const uint64_t seedValue) noexcept
    {
        m_engine.seed(seedValue);
    }

    void reset()
    {
        m_ready = true;
        m_engine.setScriptDefaults();
        m_engine.reset();
        m_events.clear();
        m_ratios.clear();
        m_timers.clear();
        if (m_clock.running())
        {
            m_clock.stop();
        }
        m_ratio = 1.0;
        updateClockTempo();
        m_announced = m_now;
        m_largestBlock = 0;
        noteBlock(m_expectedBlock);
        m_stepCount = 0;
        m_sentX = m_x; // OnControl tells about moves after the activation, not about the values it starts with
        m_sentY = m_y;
        m_seenFailures = m_script.hookFailures();
        m_listening = false;
        activateScript();
    }

    // --- controls of the engine ---

    void setMix(const float percent) noexcept
    {
        m_engine.setMix(percent);
    }

    // --- controls of the clock and the script ---

    /// Tempo in beats per minute: the face knob, the tempo of the clock with Sync = Free run. It has no designation:
    /// the host drives a port with the designation time:beatsPerMinute from the pedalboard, which would override a
    /// knob.
    void setTempo(const float beatsPerMinute) noexcept
    {
        m_tempo = static_cast<double>(beatsPerMinute);
        updateClockTempo();
    }

    /// The board's tempo, from the port with the designation time:beatsPerMinute: the tempo of the clock with
    /// Sync = Transport.
    void setHostTempo(const float beatsPerMinute) noexcept
    {
        m_hostTempo = static_cast<double>(beatsPerMinute);
        updateClockTempo();
    }

    /// The length of a step (ship_ahoy::c_stepLabels), as menu index.
    void setStep(const float index) noexcept
    {
        const float clamped = std::clamp(index, 0.0f, static_cast<float>(ship_ahoy::c_stepCount - 1));
        m_stepIndex = static_cast<size_t>(std::lround(clamped));
    }

    /// 0: the clock runs from activation, 1: only while the transport rolls.
    void setSync(const float value) noexcept
    {
        m_followTransport = value >= 0.5f;
        updateClockTempo();
    }

    /// The transport state the host reports through the port with the designation time:speed.
    void setTransport(const float value) noexcept
    {
        m_rolling = value > 0.5f;
    }

    /// The X knob, 0 to 100: stored for GetX() and, if it changed, told to OnControl at the next block.
    void setX(const float percent) noexcept
    {
        m_x = std::clamp(percent, 0.0f, 100.0f);
        m_script.setValue(ship_ahoy::c_getX, static_cast<double>(m_x));
    }

    /// The Y knob, 0 to 100 (see setX).
    void setY(const float percent) noexcept
    {
        m_y = std::clamp(percent, 0.0f, 100.0f);
        m_script.setValue(ship_ahoy::c_getY, static_cast<double>(m_y));
    }

    /// The Script port: 0 to 8 the plugin's scripts, 9 to 16 the user's user-1 to user-8.
    void setScript(const float value)
    {
        const size_t index = static_cast<size_t>(
            std::clamp(std::lround(value), 0L, static_cast<long>(c_shipAhoyScriptEntries.size() - 1)));
        const bool changed = index != m_scriptIndex;
        m_scriptIndex = index;
        if (!m_ready || !changed)
        {
            return;
        }
        const ShipAhoyScriptEntry& entry = c_shipAhoyScriptEntries[index];
        m_script.select(entry.origin, entry.name);
    }

    // --- control outputs ---

    /// How the selected script is: 0 idle, 1 ok, 2 file missing, 3 compile error, 4 runtime error, 5 over the budget,
    /// 6 out of memory (script::ScriptState).
    [[nodiscard]] float getScriptStatus() const noexcept
    {
        return static_cast<float>(m_script.state());
    }

    /// Commands that were applied after the sample they were meant for (the lead was too small for this host).
    [[nodiscard]] float getLate() const noexcept
    {
        return static_cast<float>(m_events.late() + m_ratios.late());
    }

    /// Events and commands lost: queues or buffers full.
    [[nodiscard]] float getDropped() const noexcept
    {
        return static_cast<float>(m_events.dropped() + m_ratios.dropped() + m_script.droppedEvents() +
                                  m_script.droppedCommands());
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
        setScriptDirectories(base + "/scripts", script::userScriptDirectory("shipahoy"));
    }

    /// The folders of the plugin's and of the user's scripts. Before setSampleRate().
    void setScriptDirectories(const std::string& factory, const std::string& user)
    {
        m_factoryDirectory = factory;
        m_userDirectory = user;
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

    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        const auto frames = static_cast<uint32_t>(numSamples);
        noteBlock(frames);
        listen(in, numSamples);
        m_script.advance(frames);
        drain();
        publishControl();
        runTransport();
        announceTo(m_now + frames + m_leadFrames);

        // what is due inside this block, cut at its frame: the engine runs up to it, the item is applied, and on
        // it goes (the engine never changes the buffer)
        size_t position = 0;
        m_events.emitDue(m_now, frames,
                         [&](const uint32_t frame, const Item& item)
                         {
                             m_engine.process(in + position, out + position, frame - position);
                             position = frame;
                             apply(item);
                         });
        m_engine.process(in + position, out + position, numSamples - position);
        m_now += frames;
    }

    // --- for tests and tools ---

    /// A floor for the largest block, kept over activations: the lead starts at c_leadBlocks times it. For a host whose
    /// block size is known beforehand (and for tests that need a lead that does not depend on how they run).
    void setExpectedBlock(const uint32_t frames) noexcept
    {
        m_expectedBlock = frames;
        noteBlock(frames);
    }

    /// The lead of the clock in samples (c_leadBlocks times the largest block seen).
    [[nodiscard]] uint64_t leadFrames() const noexcept
    {
        return m_leadFrames;
    }

    /// Whether the pitch detector is running (the script defines OnNewPitch or OnPitchEnd), as of the last block.
    [[nodiscard]] bool isListening() const noexcept
    {
        return m_listening;
    }

    [[nodiscard]] const ShipAhoyEngine& engine() const noexcept
    {
        return m_engine;
    }

    [[nodiscard]] const script::TempoClock& clock() const noexcept
    {
        return m_clock;
    }

    [[nodiscard]] const script::ScriptHost<ShipAhoyEvent, ShipAhoyCommand, 256, 512>& host() const noexcept
    {
        return m_script;
    }

    /// Steps per second at the present tempo, step and speed ratio.
    [[nodiscard]] double stepRate() const noexcept
    {
        return m_clock.tempo() / 60.0 * static_cast<double>(ship_ahoy::c_ticksPerQuarter) /
               static_cast<double>(stepTicks());
    }

  private:
    static constexpr size_t c_eventCapacity{128};
    /// The range, hop and decimation of the pitch detector: the figures of documentation/YinPitch/ (4 costs 0.7 points
    /// of the Dwarf's DSP load, 5 % of a block at worst, within 3 cent below 760 Hz).
    static constexpr float c_lowestHz{80.0f};
    static constexpr float c_highestHz{1000.0f};
    static constexpr float c_pitchHopHz{50.0f};
    static constexpr size_t c_pitchDecimation{4};

    /// Something that happens to the engine at a sample.
    struct Item
    {
        enum class Kind : uint8_t
        {
            Cutoff,     ///< a script's SetCutoff
            Resonance,  ///< SetResonance
            FilterType, ///< SetFilterType
            Drift       ///< SetDrift
        };

        Kind kind{Kind::Cutoff};
        float note{0.0f};
        float value{0.0f};
        float rate{1.0f};
        int64_t glideSamples{0};
    };

    [[nodiscard]] uint32_t stepTicks() const noexcept
    {
        return ship_ahoy::c_stepTicks[m_stepIndex];
    }

    void resetPitch() noexcept
    {
        if (m_detector)
        {
            m_detector->reset();
        }
        m_tracker.reset();
    }

    /// Feeds the block to the pitch detector if the script wants played notes, before anything changes the buffer. A
    /// script that begins to listen or has been replaced starts from an empty window, before the samples of the block
    /// (so the readings are at the same samples for every block size).
    void listen(const float* in, const size_t numSamples) noexcept
    {
        const bool wantsNote = m_script.wantsHook(ship_ahoy::c_pitchHook);
        const bool wantsEnd = m_script.wantsHook(ship_ahoy::c_pitchEndHook);
        const bool listening = (wantsNote || wantsEnd) && m_detector.has_value();
        const uint32_t generation = m_script.readyGeneration();
        if (listening != m_listening || generation != m_listenGeneration)
        {
            m_listening = listening;
            m_listenGeneration = generation;
            resetPitch(); // at the start of a block: the hops count from here, whatever the block size
        }
        if (!listening)
        {
            return;
        }
        for (size_t i = 0; i < numSamples; ++i)
        {
            m_detector->step(in[i]);
            if (m_detector->hasNewPitch())
            {
                report(m_now + i + 1, wantsNote, wantsEnd);
            }
        }
    }

    /// The reading the detector has just finished, through the tracker, to the hooks the script defines.
    void report(const uint64_t time, const bool wantsNote, const bool wantsEnd) noexcept
    {
        const PitchObservation reading{m_detector->getCurrentPitch(), m_detector->getLastConfidence(),
                                       m_detector->getLastLevel(), m_detector->getLastWindowEnd()};
        const std::optional<PitchNote> note = m_tracker.update(reading);
        if (!note.has_value() || (note->released ? !wantsEnd : !wantsNote))
        {
            return;
        }
        ShipAhoyEvent event;
        event.kind = note->released ? ShipAhoyEvent::Kind::PitchEnd : ShipAhoyEvent::Kind::Pitch;
        event.note = static_cast<float>(note->note);
        event.hz = note->hz;
        event.pitch = note->pitch;
        event.periodicity = note->confidence;
        event.level = note->level;
        m_script.pushEvent(event, time);
    }

    /// The lead follows the largest block seen since the activation.
    void noteBlock(const uint32_t frames) noexcept
    {
        if (frames > m_largestBlock)
        {
            m_largestBlock = frames;
            m_leadFrames = static_cast<uint64_t>(c_leadBlocks) * frames;
        }
    }

    void updateClockTempo() noexcept
    {
        m_clock.setTempo((m_followTransport ? m_hostTempo : m_tempo) * m_ratio);
    }

    /// The selected script at activation: not in the audio thread, so it is loaded here and now. What its top level and
    /// OnStart called is taken by drain() at the first block.
    void activateScript()
    {
        m_seenGeneration = 0;
        const ShipAhoyScriptEntry& entry = c_shipAhoyScriptEntries[m_scriptIndex];
        m_script.select(entry.origin, entry.name);
        m_script.work();
    }

    /// What a script left that must not outlive it: the timers, the commands and ratios still waiting, the ratio.
    void dropScript() noexcept
    {
        m_events.clear();
        m_ratios.clear();
        m_timers.clear();
        if (m_ratio != 1.0)
        {
            m_ratio = 1.0;
            updateClockTempo();
        }
    }

    void runTransport() noexcept
    {
        const bool shouldRun = !m_followTransport || m_rolling;
        if (shouldRun && !m_clock.running())
        {
            const uint64_t start = m_now + m_leadFrames;
            m_clock.start(start);
            m_stepCount = 0;
            m_announced = std::max(m_announced, m_now);
            forward(script::CoreEvent{script::CoreEvent::Kind::TransportStart, 0, start});
        }
        else if (!shouldRun && m_clock.running())
        {
            m_clock.stop();
            forward(script::CoreEvent{script::CoreEvent::Kind::TransportStop, 0, m_now});
        }
    }

    /// Makes the ticks and timers due before `limit`, the speed ratios in between at their time.
    void announceTo(const uint64_t limit) noexcept
    {
        const auto push = [this](const script::CoreEvent& event) { forward(event); };
        const uint64_t start = m_announced;
        if (limit > start)
        {
            m_ratios.emitDue(start, static_cast<uint32_t>(limit - start),
                             [&](const uint32_t frame, const double ratio)
                             {
                                 script::announce(m_clock, m_timers, start + frame, push);
                                 m_ratio = ratio;
                                 updateClockTempo();
                             });
        }
        script::announce(m_clock, m_timers, limit, push);
        m_announced = std::max(m_announced, limit);
    }

    void forward(const script::CoreEvent& event) noexcept
    {
        if (event.kind == script::CoreEvent::Kind::Tick && event.value % stepTicks() == 0)
        {
            step(event.time);
        }
        if (m_script.wantsCoreHook(event.kind))
        {
            m_script.pushCoreEvent(event);
        }
    }

    /// A step of the clock at the sample `time` of the clock: the hook OnStep, and in knob mode the engine's hop.
    void step(const uint64_t time) noexcept
    {
        const uint32_t index = m_stepCount++;
        if (m_script.wantsHook(ship_ahoy::c_stepHook))
        {
            m_script.pushEvent(ShipAhoyEvent{ShipAhoyEvent::Kind::Step, index}, time);
        }
    }

    /// Tells OnControl that X or Y moved, at most once per block; the values are kept up to date whether or not the
    /// script listens, so a script that starts to listen is not told about a change from before.
    void publishControl() noexcept
    {
        if (m_x == m_sentX && m_y == m_sentY)
        {
            return;
        }
        m_sentX = m_x;
        m_sentY = m_y;
        if (m_script.wantsHook(ship_ahoy::c_controlHook))
        {
            ShipAhoyEvent event;
            event.kind = ShipAhoyEvent::Kind::Control;
            event.x = m_x;
            event.y = m_y;
            m_script.pushEvent(event, m_now);
        }
    }

    /// What the script has called since the last block. A script that took over (a first load, a reload) owns the
    /// frequency from where it is; a hook that failed leaves the script in an unknown state, so what was queued is
    /// dropped and the filter holds. A file that does not load changes nothing here: the previous script keeps running
    /// (Script status says why).
    void drain() noexcept
    {
        const uint32_t ready = m_script.readyGeneration();
        const uint32_t failures = m_script.hookFailures();
        const bool failed = failures != m_seenFailures;
        if (ready != m_seenGeneration)
        {
            m_seenGeneration = ready;
            dropScript();
            m_engine.setScriptDefaults();
        }
        if (failed)
        {
            m_seenFailures = failures;
            dropScript();
        }

        ShipAhoyCommand command;
        uint32_t generation = 0;
        uint64_t time = 0;
        while (m_script.peekCommand(command, generation, time))
        {
            m_script.discardCommand();
            if (!failed)
            {
                command.external = command.external || time == 0; // the commands of a load carry no stamp
                queue(command, time != 0 ? time : m_now);
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

    /// A command of a clock hook is due one lead after the sample of its event; one of an external event (a played
    /// note, X or Y, the load of a script) at the first block boundary after it arrives, but not before its event.
    void queue(const ShipAhoyCommand& command, const uint64_t stamp) noexcept
    {
        const uint64_t base = command.external ? std::max(stamp, m_now) : stamp + m_leadFrames;
        const uint64_t due = base + span(command.delayMs, command.delayTicks);
        Item item;
        switch (command.kind)
        {
            case ShipAhoyCommand::Kind::SpeedRatio:
                m_ratios.insert(due, static_cast<double>(command.ratio));
                return;
            case ShipAhoyCommand::Kind::Cutoff:
            {
                uint64_t length = span(command.ms, command.ticks);
                if (length == 0)
                {
                    length = m_clock.ticksToSamples(static_cast<double>(stepTicks()));
                }
                item.kind = Item::Kind::Cutoff;
                item.note = command.note;
                item.glideSamples = command.glide > 0.0f
                                        ? std::llround(static_cast<double>(command.glide) * static_cast<double>(length))
                                        : 0;
                break;
            }
            case ShipAhoyCommand::Kind::Resonance:
                item.kind = Item::Kind::Resonance;
                item.value = command.value;
                break;
            case ShipAhoyCommand::Kind::FilterType:
                item.kind = Item::Kind::FilterType;
                item.value = command.value;
                break;
            case ShipAhoyCommand::Kind::Drift:
                item.kind = Item::Kind::Drift;
                item.value = command.value;
                item.rate = command.rate;
                break;
        }
        m_events.insert(due, item);
    }

    void apply(const Item& item) noexcept
    {
        switch (item.kind)
        {
            case Item::Kind::Cutoff:
                m_engine.cutoff(item.note, item.glideSamples);
                break;
            case Item::Kind::Resonance:
                m_engine.setResonance(item.value);
                break;
            case Item::Kind::FilterType:
                m_engine.setFilterType(item.value);
                break;
            case Item::Kind::Drift:
                m_engine.setModDepth(item.value);
                m_engine.setModRate(item.rate);
                break;
        }
    }

    float m_sampleRate{48000.0f};
    double m_tempo{c_defaultTempo};
    double m_hostTempo{120.0};
    double m_ratio{1.0};
    uint32_t m_expectedBlock{0};
    uint32_t m_largestBlock{0};
    uint64_t m_leadFrames{0};
    size_t m_stepIndex{ship_ahoy::c_defaultStep};
    bool m_followTransport{false};
    bool m_rolling{false};
    bool m_ready{false};
    bool m_listening{false};
    size_t m_scriptIndex{0};
    float m_x{0.0f};
    float m_y{0.0f};
    float m_sentX{0.0f};
    float m_sentY{0.0f};
    std::string m_factoryDirectory;
    std::string m_userDirectory;
    uint64_t m_now{1};
    uint64_t m_announced{1};
    uint32_t m_stepCount{0};
    uint32_t m_seenGeneration{0};
    uint32_t m_seenFailures{0};
    uint32_t m_listenGeneration{0};
    ShipAhoyEngine m_engine;
    std::optional<YinPitchDetector> m_detector;
    PitchNoteTracker m_tracker;
    script::TempoClock m_clock;
    script::TimerBank m_timers;
    script::TimedEventBuffer<Item, c_eventCapacity> m_events;
    script::TimedEventBuffer<double, 16> m_ratios;
    ShipAhoyBinder m_binder;
    script::ScriptHost<ShipAhoyEvent, ShipAhoyCommand, 256, 512> m_script{m_binder};
};
}
