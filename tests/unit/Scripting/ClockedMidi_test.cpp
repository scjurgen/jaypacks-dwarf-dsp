// The parts of the core that make timed MIDI together, in a simulated plugin: the clock announces ticks a lookahead
// ahead, the host's worker runs the script's hooks after each block, the commands come back with the time of the tick
// they answer and the scheduler writes them at their exact sample. Nothing of any real plugin in it.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "lv2/PedalServices.h"
#include "scripting/Binding.h"
#include "scripting/BindingCheck.h"
#include "scripting/MidiScheduler.h"
#include "scripting/ScriptHost.h"
#include "scripting/TempoClock.h"
#include "scripting/Timers.h"

namespace jp::script::test
{

namespace fs = std::filesystem;

namespace
{

struct NoEvent
{
    uint8_t unused{0};
};

struct MidiCommand
{
    bool noteOn{true};
    uint8_t channel{0};
    uint8_t note{0};
    uint8_t velocity{0};
    float delayTicks{0.0f};
};

inline constexpr std::array<ValueSpec, 3> c_onArgs{{
    {"channel", ValueKind::Integer, 0.0, 15.0, 0.0, "channel"},
    {"note", ValueKind::Integer, 0.0, 127.0, 0.0, "note"},
    {"velocity", ValueKind::Integer, 1.0, 127.0, 100.0, "velocity"},
}};
inline constexpr std::array<ValueSpec, 2> c_offArgs{{
    {"channel", ValueKind::Integer, 0.0, 15.0, 0.0, "channel"},
    {"note", ValueKind::Integer, 0.0, 127.0, 0.0, "note"},
}};
inline constexpr std::array<ValueSpec, 1> c_delayField{
    {{"delayTicks", ValueKind::Number, 0.0, 96000.0, 0.0, "when, in ticks after the event"}}};
inline constexpr size_t c_timerBase{2};
inline constexpr std::array<CommandSpec, 4> c_commands{{
    {"NoteOn", "Sends a note on.", c_onArgs.data(), 3, c_delayField.data(), 1},
    {"NoteOff", "Sends a note off.", c_offArgs.data(), 2, c_delayField.data(), 1},
    c_timerCommands[0],
    c_timerCommands[1],
}};
inline constexpr std::array<HookSpec, 4> c_hooks{{c_coreHooks[0], c_coreHooks[1], c_coreHooks[2], c_coreHooks[3]}};

const BindingSet& bindings()
{
    static const BindingSet set{
        "Clocked", c_commands.data(), c_commands.size(), c_hooks.data(), c_hooks.size(), nullptr, 0};
    return set;
}

class Binder : public ScriptBinder<NoEvent, MidiCommand>
{
  public:
    [[nodiscard]] const BindingSet& bindings() const noexcept override
    {
        return test::bindings();
    }

    bool makeCommand(const CommandCall& call, MidiCommand& command, char*, size_t) noexcept override
    {
        command.noteOn = call.command == 0;
        command.channel = static_cast<uint8_t>(call.integer(0));
        command.note = static_cast<uint8_t>(call.integer(1));
        command.velocity = command.noteOn ? static_cast<uint8_t>(call.integer(2)) : 0;
        command.delayTicks = call.real(command.noteOn ? 3 : 2);
        return true;
    }

    Status dispatch(Runtime&, const NoEvent&) override
    {
        return Status::Ok;
    }
};

struct Sent
{
    uint64_t sample;
    uint8_t status;
    uint8_t note;

    bool operator==(const Sent& other) const
    {
        return sample == other.sample && status == other.status && note == other.note;
    }
};

struct Output
{
    bool send(const uint32_t frame, const uint8_t* bytes, uint32_t)
    {
        sent.push_back({start + frame, bytes[0], bytes[1]});
        return true;
    }

    uint64_t start{0};
    std::vector<Sent> sent;
};

/// A plugin that only does what the core gives it: a clock, a host with its worker (run after every block, as the host
/// harness does), the timers, the scheduler. The pedal's rules: commands are taken at the start of a block, a new
/// script ends what the old one left, an error ends it too.
class Sim
{
  public:
    using Host = ScriptHost<NoEvent, MidiCommand, 16, 1024>;

    Sim(const std::string& name, const uint32_t block, const uint64_t lookahead, const std::string& script)
        : m_folder(fs::current_path() / "clocked-midi-test" / (name + "-" + std::to_string(::getpid())))
        , m_block(block)
        , m_lookahead(lookahead)
        , m_host(m_binder)
    {
        fs::remove_all(m_folder);
        fs::create_directories(m_folder / "factory");
        HostConfig config;
        config.factoryDir = (m_folder / "factory").string();
        config.timerCommandBase = c_timerBase;
        config.pollSeconds = 0.25;
        m_host.configure(config);
        m_workerStub.schedule.handle = this;
        m_workerStub.schedule.schedule_work = [](LV2_Worker_Schedule_Handle handle, uint32_t, const void*)
        {
            ++static_cast<Sim*>(handle)->m_jobs;
            return LV2_WORKER_SUCCESS;
        };
        m_scheduler.setSchedule(&m_workerStub.schedule);
        m_host.setWorkScheduler(m_scheduler);
        m_clock.configure(48000.0, 24);
        m_clock.setTempo(120.0);
        write(script);
        m_host.select(Origin::Factory, "arp");
        pumpWorker();
        m_seen = m_host.readyGeneration();
        m_midi.setBudget(128, 128);
    }

    ~Sim()
    {
        fs::remove_all(m_folder);
    }

    Sim(const Sim&) = delete;
    Sim& operator=(const Sim&) = delete;

    void write(const std::string& text)
    {
        const fs::path path = m_folder / "factory" / "arp.lua";
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(++m_edits));
    }

    /// The transport starts at an absolute sample; the first tick is due then.
    void playAt(const uint64_t sample)
    {
        m_playAt = sample;
    }

    void stopAt(const uint64_t sample)
    {
        m_stopAt = sample;
    }

    void run(const uint64_t until)
    {
        while (m_now < until)
        {
            block();
        }
    }

    [[nodiscard]] const std::vector<Sent>& sent() const
    {
        return m_output.sent;
    }

    [[nodiscard]] uint64_t now() const
    {
        return m_now;
    }

    MidiScheduler<512>& midi()
    {
        return m_midi;
    }

    Host& host()
    {
        return m_host;
    }

    uint32_t panics() const
    {
        return m_panics;
    }

  private:
    void block()
    {
        // the audio thread: first what the worker made after the last block
        if (m_host.readyGeneration() != m_seen)
        {
            m_seen = m_host.readyGeneration();
            m_midi.panic(m_now);
            m_timers.clear();
            ++m_panics;
        }
        const uint32_t failures = m_host.hookFailures();
        const bool failed = failures != m_seenFailures;
        if (failed)
        {
            m_seenFailures = failures;
            m_midi.panic(m_now);
            m_timers.clear();
            ++m_panics;
        }
        MidiCommand command;
        uint32_t generation = 0;
        uint64_t time = 0;
        while (m_host.peekCommand(command, generation, time))
        {
            m_host.discardCommand();
            if (failed)
            {
                continue; // the call that failed says nothing, nor does what was queued before it was noticed
            }
            const uint64_t due = (time != 0 ? time : m_now) + m_clock.ticksToSamples(command.delayTicks);
            m_midi.schedule(due, MidiBytes{static_cast<uint8_t>((command.noteOn ? 0x90 : 0x80) | command.channel),
                                           command.note, command.velocity, 3});
        }
        TimerRequest request;
        while (m_host.popTimerRequest(request))
        {
            m_timers.apply(request, m_clock, m_now);
        }
        // the transport
        if (!m_clock.running() && m_playAt != 0 && m_now + m_block + m_lookahead > m_playAt && m_now <= m_playAt)
        {
            m_clock.start(m_playAt);
            m_host.pushCoreEvent({CoreEvent::Kind::TransportStart, 0, m_playAt});
            m_playAt = 0;
        }
        if (m_clock.running() && m_stopAt != 0 && m_now + m_block + m_lookahead > m_stopAt)
        {
            m_clock.stop();
            m_host.pushCoreEvent({CoreEvent::Kind::TransportStop, 0, m_stopAt});
            m_stopAt = 0;
        }
        announce(m_clock, m_timers, m_now + m_block + m_lookahead,
                 [this](const CoreEvent& event) { m_host.pushCoreEvent(event); });
        // the MIDI that is due in this block
        m_output.start = m_now;
        m_midi.emit(m_output, m_now, m_block);
        m_host.advance(m_block);
        m_now += m_block;
        pumpWorker();
    }

    void pumpWorker()
    {
        if (m_jobs > 0 || m_host.state() == ScriptState::Idle)
        {
            m_jobs = 0;
            m_host.work();
        }
    }

    fs::path m_folder;
    uint32_t m_block;
    uint64_t m_lookahead;
    Binder m_binder;
    Host m_host;
    WorkScheduler m_scheduler;
    struct WorkerStub
    {
        LV2_Worker_Schedule schedule{};
    } m_workerStub;
    int m_jobs{0};
    TempoClock m_clock;
    TimerBank m_timers;
    MidiScheduler<512> m_midi;
    Output m_output;
    uint64_t m_now{0};
    uint64_t m_playAt{0};
    uint64_t m_stopAt{0};
    uint32_t m_seen{0};
    uint32_t m_panics{0};
    uint32_t m_seenFailures{0};
    int m_edits{0};
};

const char* c_arp = "function OnTick(t)\n"
                    "    if t % 6 == 0 then\n"
                    "        local note = 60 + (t // 6) % 4\n"
                    "        NoteOn(0, note, 100)\n"
                    "        NoteOff(0, note, {delayTicks = 3})\n"
                    "    end\n"
                    "end\n";

}

TEST(ClockedMidi, TheBindingTablesOfTheSimulationAreFine)
{
    EXPECT_EQ(checkBindings(bindings()), "");
}

TEST(ClockedMidi, AnArpeggioFromTheClockLandsOnTheExactSamplesWhateverTheBlockLength)
{
    constexpr uint64_t c_start = 40000;
    std::vector<Sent> wanted;
    for (uint64_t i = 0; i < 10; ++i)
    {
        const auto note = static_cast<uint8_t>(60 + i % 4);
        wanted.push_back({c_start + 6000 * i, 0x90, note});
        wanted.push_back({c_start + 6000 * i + 3000, 0x80, note});
    }
    for (const uint32_t block : {16u, 64u, 128u, 513u, 1024u, 4096u})
    {
        Sim sim("arp", block, 2ull * block + 256, c_arp);
        sim.playAt(c_start);
        sim.run(c_start + 62000);
        std::vector<Sent> got;
        for (const Sent& message : sim.sent())
        {
            if (message.sample < c_start + 60000)
            {
                got.push_back(message);
            }
        }
        EXPECT_EQ(got, wanted) << "block " << block;
        EXPECT_EQ(sim.midi().late(), 0u) << "block " << block;
    }
}

TEST(ClockedMidi, ATimerStartedByTheTransportFiresOnItsExactSamples)
{
    const char* script = "function OnTransportStart() SetTimer(0, {ms = 10, repeating = true}) end\n"
                         "function OnTimer(id) NoteOn(0, 70 + id, 100) end\n";
    constexpr uint64_t c_start = 40000;
    for (const uint32_t block : {32u, 128u, 1000u, 4096u})
    {
        Sim sim("timer", block, 2ull * block + 256, script);
        sim.playAt(c_start);
        sim.run(c_start + 30000);
        ASSERT_GE(sim.sent().size(), 30u) << block;
        for (size_t k = 0; k < 30; ++k)
        {
            EXPECT_EQ(sim.sent()[k], (Sent{c_start + 480 * (k + 1), 0x90, 70})) << "block " << block << " k " << k;
        }
        EXPECT_EQ(sim.midi().late(), 0u);
    }
}

TEST(ClockedMidi, ATimerThatAScriptStopsStopsBeforeItsNextFiring)
{
    const char* script = "local n = 0\n"
                         "function OnTransportStart() SetTimer(0, {ms = 10, repeating = true}) end\n"
                         "function OnTimer(id)\n"
                         "    n = n + 1\n"
                         "    NoteOn(0, 70, 100)\n"
                         "    if n == 3 then StopTimer(0) end\n"
                         "end\n";
    Sim sim("stop", 64, 128, script);
    sim.playAt(40000);
    sim.run(40000 + 20000);
    size_t ons = 0;
    for (const Sent& message : sim.sent())
    {
        ons += message.status == 0x90 ? 1 : 0;
    }
    EXPECT_EQ(ons, 3u);
}

TEST(ClockedMidi, AReloadEndsWhatTheOldScriptLeftHangingAndThenOnlyTheNewOneSpeaks)
{
    // never switches a note off
    const char* hanging = "function OnTick(t) if t % 6 == 0 then NoteOn(0, 60 + (t // 6) % 8, 100) end end\n";
    Sim sim("reload", 128, 512, hanging);
    sim.playAt(40000);
    sim.run(40000 + 40000);
    ASSERT_GE(sim.midi().guard().count(), 5u) << "notes are hanging";
    const size_t before = sim.sent().size();
    sim.write("function OnTick(t) if t % 6 == 0 then NoteOn(1, 100, 100) NoteOff(1, 100, {delayTicks = 2}) end end\n");
    sim.run(sim.now() + 24000); // the file is checked every quarter second
    EXPECT_GE(sim.panics(), 1u);
    size_t offsOfOld = 0;
    size_t onsOfNew = 0;
    for (size_t i = before; i < sim.sent().size(); ++i)
    {
        const Sent& message = sim.sent()[i];
        offsOfOld += (message.status == 0x80 && (message.note >= 60 && message.note < 68)) ? 1 : 0;
        onsOfNew += (message.status == 0x91) ? 1 : 0;
    }
    EXPECT_GE(offsOfOld, 5u) << "every hanging note got its note off";
    EXPECT_GE(onsOfNew, 2u) << "and the new script plays";
    sim.midi().panic(sim.now());
    sim.run(sim.now() + 128);
    EXPECT_EQ(sim.midi().guard().count(), 0u);
}

TEST(ClockedMidi, AnErrorInAHookEndsTheNotesAndTheHostCountsIt)
{
    const char* script = "function OnTick(t)\n"
                         "    if t == 18 then error('boom') end\n"
                         "    if t % 6 == 0 and t < 18 then NoteOn(0, 60 + t // 6, 100) end\n"
                         "end\n";
    Sim sim("error", 128, 512, script);
    sim.playAt(40000);
    sim.run(40000 + 30000);
    EXPECT_EQ(sim.host().state(), ScriptState::RuntimeError);
    EXPECT_EQ(sim.host().hookFailures(), 1u);
    EXPECT_GE(sim.panics(), 1u);
    EXPECT_EQ(sim.midi().guard().count(), 0u) << "the notes that were sounding ended with the error";
    size_t ons = 0;
    size_t offs = 0;
    for (const Sent& message : sim.sent())
    {
        ons += message.status == 0x90 ? 1 : 0;
        offs += message.status == 0x80 ? 1 : 0;
    }
    EXPECT_EQ(ons, 3u) << "the script played before it failed";
    EXPECT_EQ(ons, offs) << "and every note it began was ended";
}

TEST(ClockedMidi, AScriptInALoopCannotFloodTheOutput)
{
    const char* script = "function OnTick(t) for i = 1, 200 do NoteOn(0, i % 100, 100) end end\n";
    Sim sim("flood", 128, 512, script);
    sim.playAt(40000);
    sim.run(40000 + 12000);
    ASSERT_FALSE(sim.sent().empty());
    // never more than the budget of 128 messages in a window of 128 samples
    for (size_t first = 0; first < sim.sent().size(); ++first)
    {
        size_t inWindow = 0;
        for (size_t k = first; k < sim.sent().size() && sim.sent()[k].sample < sim.sent()[first].sample + 128; ++k)
        {
            ++inWindow;
        }
        EXPECT_LE(inWindow, 2u * 128u) << "two windows at most can touch a stretch of 128 samples";
    }
    EXPECT_GT(sim.midi().refused() + sim.midi().dropped() + sim.host().droppedCommands(), 0u);
    sim.midi().panic(sim.now());
    sim.run(sim.now() + 128);
    EXPECT_EQ(sim.midi().guard().count(), 0u);
}

}
