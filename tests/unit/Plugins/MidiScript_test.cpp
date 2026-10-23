// MIDI Script: the bindings, the pedal driven the way the LV2 adapter drives it (blocks cut at the input events, the
// host's worker run between blocks), and every factory script against an exact list of output messages.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "lv2/PedalServices.h"
#include "lv2/atom/util.h"
#include "plugins/midiscript/MidiScriptPedal.h"
#include "scripting/BindingCheck.h"

namespace jp::test
{

namespace fs = std::filesystem;

namespace
{

constexpr float c_rate{48000.0f};
constexpr uint32_t c_sequenceType{1};
constexpr uint32_t c_midiType{2};

struct Message
{
    uint64_t sample;
    std::array<uint8_t, 3> bytes;
    uint8_t size;

    bool operator==(const Message& other) const
    {
        return sample == other.sample && bytes == other.bytes && size == other.size;
    }
};

Message msg(const uint64_t sample, const uint8_t status, const uint8_t data1 = 0, const uint8_t data2 = 0,
            const uint8_t size = 3)
{
    return Message{sample, {status, data1, data2}, size};
}

/// The host's wake-up job as the pedal sees it.
struct WorkerStub
{
    WorkerStub()
    {
        schedule.handle = this;
        schedule.schedule_work = [](LV2_Worker_Schedule_Handle handle, uint32_t, const void*)
        {
            ++static_cast<WorkerStub*>(handle)->jobs;
            return LV2_WORKER_SUCCESS;
        };
    }

    LV2_Worker_Schedule schedule{};
    int jobs{0};
};

/// A pedal, its scripts folders and the time. run() does what the LV2 adapter and the host do: it cuts the blocks at
/// the input messages, and runs the pedal's work() (the host's worker thread) after every block.
class Rig
{
  public:
    explicit Rig(const std::string& name, const float latencyMs = 10.0f, const bool activate = true)
        : m_folder(fs::current_path() / "midiscript-test" / (name + "-" + std::to_string(::getpid())))
        , m_pedal(std::make_unique<MidiScriptPedal>())
    {
        fs::remove_all(m_folder);
        fs::create_directories(m_folder / "user");
        m_pedal->setScriptDirectories(std::string(JP_SOURCE_DIR) + "/plugins/midiscript/scripts",
                                      (m_folder / "user").string());
        m_scheduler.setSchedule(&m_worker.schedule);
        m_pedal->setWorkScheduler(m_scheduler);
        m_pedal->setMidiOutput(m_out);
        m_pedal->setSampleRate(c_rate);
        m_pedal->setLatency(latencyMs);
        m_buffer.resize(1 << 16);
        if (activate)
        {
            this->activate();
        }
    }

    ~Rig()
    {
        m_pedal.reset();
        fs::remove_all(m_folder);
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void activate()
    {
        m_pedal->reset();
    }

    MidiScriptPedal& pedal()
    {
        return *m_pedal;
    }

    /// Selects an entry of the Script port: 0 to 5 are the plugin's, 6 + n - 1 is user-n.
    void select(const int entry)
    {
        m_pedal->setScript(static_cast<float>(entry));
    }

    void writeUser(const int slot, const std::string& text)
    {
        const fs::path path = m_folder / "user" / ("user-" + std::to_string(slot) + ".lua");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(++m_edits));
    }

    /// A shipped script with one line of its settings replaced.
    static std::string shipped(const std::string& name, const std::string& from = "", const std::string& to = "")
    {
        std::ifstream in(std::string(JP_SOURCE_DIR) + "/plugins/midiscript/scripts/" + name + ".lua");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!from.empty())
        {
            const size_t at = text.find(from);
            EXPECT_NE(at, std::string::npos) << from;
            text.replace(at, from.size(), to);
        }
        return text;
    }

    /// An input message at a sample counted from the start of the rig (in the order of the samples).
    void in(const uint64_t sample, const uint8_t status, const uint8_t data1 = 0, const uint8_t data2 = 0,
            const uint8_t size = 3)
    {
        m_input.push_back(msg(sample, status, data1, data2, size));
        std::stable_sort(m_input.begin(), m_input.end(),
                         [](const Message& a, const Message& b) { return a.sample < b.sample; });
    }

    void noteOn(const uint64_t sample, const int channel, const int note, const int velocity = 100)
    {
        in(sample, static_cast<uint8_t>(0x90 | (channel - 1)), static_cast<uint8_t>(note),
           static_cast<uint8_t>(velocity));
    }

    void noteOff(const uint64_t sample, const int channel, const int note)
    {
        in(sample, static_cast<uint8_t>(0x80 | (channel - 1)), static_cast<uint8_t>(note), 64);
    }

    /// Runs up to the sample `until` in blocks of `block`.
    void run(const uint64_t until, const uint32_t block = 128)
    {
        while (m_sample < until)
        {
            const auto n = static_cast<uint32_t>(std::min<uint64_t>(block, until - m_sample));
            auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(m_buffer.data());
            sequence->atom.size = static_cast<uint32_t>(m_buffer.size() - sizeof(LV2_Atom));
            m_out.begin(sequence, c_sequenceType, c_midiType, n);
            uint32_t done = 0;
            while (m_next < m_input.size() && m_input[m_next].sample < m_sample + n)
            {
                const Message& message = m_input[m_next++];
                const auto frame = static_cast<uint32_t>(message.sample - m_sample);
                if (frame > done)
                {
                    m_out.setCallOffset(done);
                    m_pedal->process(frame - done);
                    done = frame;
                }
                m_out.setCallOffset(frame);
                m_pedal->midi(message.bytes.data(), message.size);
            }
            if (n > done)
            {
                m_out.setCallOffset(done);
                m_pedal->process(n - done);
            }
            LV2_ATOM_SEQUENCE_FOREACH(sequence, event)
            {
                Message out{
                    m_sample + static_cast<uint64_t>(event->time.frames), {}, static_cast<uint8_t>(event->body.size)};
                std::copy_n(static_cast<const uint8_t*>(LV2_ATOM_BODY_CONST(&event->body)), event->body.size,
                            out.bytes.begin());
                m_sent.push_back(out);
            }
            WorkResponder responder(nullptr, nullptr);
            m_pedal->work(nullptr, 0, responder);
            m_sample += n;
        }
    }

    [[nodiscard]] const std::vector<Message>& sent() const
    {
        return m_sent;
    }

    /// What was sent from sample `from` on, with the status byte `status` (high nibble and channel).
    [[nodiscard]] std::vector<Message> sentWith(const uint8_t statusMask, const uint8_t status,
                                                const uint64_t from = 0) const
    {
        std::vector<Message> found;
        for (const Message& message : m_sent)
        {
            if (message.sample >= from && (message.bytes[0] & statusMask) == status)
            {
                found.push_back(message);
            }
        }
        return found;
    }

    [[nodiscard]] uint64_t sample() const
    {
        return m_sample;
    }

  private:
    fs::path m_folder;
    std::unique_ptr<MidiScriptPedal> m_pedal;
    WorkerStub m_worker;
    WorkScheduler m_scheduler;
    MidiOutput m_out;
    std::vector<uint64_t> m_buffer;
    std::vector<Message> m_input;
    std::vector<Message> m_sent;
    size_t m_next{0};
    uint64_t m_sample{0};
    int m_edits{0};
};

constexpr uint64_t c_latency{480}; // 10 ms at 48 kHz

/// The messages of an arpeggio of `notes` that starts at driver sample `first`: a note on every `period` samples, the
/// note off `gate` samples later.
std::vector<Message> arpeggio(const std::vector<int>& notes, const uint64_t first, const uint64_t period,
                              const uint64_t gate, const size_t count, const uint8_t velocity = 100,
                              const uint8_t channel = 0)
{
    std::vector<Message> list;
    for (size_t i = 0; i < count; ++i)
    {
        const auto note = static_cast<uint8_t>(notes[i % notes.size()]);
        list.push_back(msg(first + period * i, static_cast<uint8_t>(0x90 | channel), note, velocity));
        list.push_back(msg(first + period * i + gate, static_cast<uint8_t>(0x80 | channel), note, 64));
    }
    return list;
}

/// The first `count` note messages (note ons and note offs) of a run, in order.
std::vector<Message> firstNotes(const std::vector<Message>& all, const size_t count)
{
    std::vector<Message> notes;
    for (const Message& message : all)
    {
        if (((message.bytes[0] & 0xF0) == 0x90 || (message.bytes[0] & 0xF0) == 0x80) && notes.size() < count)
        {
            notes.push_back(message);
        }
    }
    return notes;
}

}

TEST(MidiScriptBindings, TheTablesHaveNoMistake)
{
    EXPECT_EQ(script::checkBindings(midi_script::bindings()), "");
    EXPECT_STREQ(midi_script::c_commands[midi_script::c_timerCommandBase].name, "SetTimer");
    EXPECT_STREQ(midi_script::c_commands[midi_script::c_timerCommandBase + 1].name, "StopTimer");
    EXPECT_EQ(midi_script::c_commands.size(), midi_script::c_timerCommandBase + 2);
}

TEST(MidiScriptBindings, EachHookIsAtTheIndexOfItsKind)
{
    using Kind = MidiScriptEvent::Kind;
    EXPECT_STREQ(midi_script::c_hooks[midi_script::hookOf(Kind::NoteOn)].name, "OnNoteOn");
    EXPECT_STREQ(midi_script::c_hooks[midi_script::hookOf(Kind::NoteOff)].name, "OnNoteOff");
    EXPECT_STREQ(midi_script::c_hooks[midi_script::hookOf(Kind::ControlChange)].name, "OnCC");
    EXPECT_STREQ(midi_script::c_hooks[midi_script::hookOf(Kind::PitchBend)].name, "OnPitchBend");
    EXPECT_STREQ(midi_script::c_hooks[midi_script::hookOf(Kind::ProgramChange)].name, "OnProgramChange");
    EXPECT_STREQ(midi_script::c_hooks[midi_script::hookOf(Kind::Other)].name, "OnOther");
    EXPECT_STREQ(midi_script::c_hooks[0].name, "OnStart");
    EXPECT_STREQ(midi_script::c_hooks[7].name, "OnTick");
}

TEST(MidiScriptBindings, AMessageBecomesTheEventOfItsHook)
{
    using Kind = MidiScriptEvent::Kind;
    MidiScriptEvent event;
    const std::array<uint8_t, 3> on{0x93, 60, 100};
    ASSERT_TRUE(midi_script::eventOf(on.data(), 3, event));
    EXPECT_EQ(event.kind, Kind::NoteOn);
    const std::array<uint8_t, 3> zero{0x90, 60, 0};
    ASSERT_TRUE(midi_script::eventOf(zero.data(), 3, event));
    EXPECT_EQ(event.kind, Kind::NoteOff) << "a note on with velocity 0 is a note off";
    const std::array<uint8_t, 3> off{0x80, 60, 20};
    ASSERT_TRUE(midi_script::eventOf(off.data(), 3, event));
    EXPECT_EQ(event.kind, Kind::NoteOff);
    const std::array<uint8_t, 3> cc{0xB5, 1, 64};
    ASSERT_TRUE(midi_script::eventOf(cc.data(), 3, event));
    EXPECT_EQ(event.kind, Kind::ControlChange);
    const std::array<uint8_t, 3> bend{0xE0, 0, 64};
    ASSERT_TRUE(midi_script::eventOf(bend.data(), 3, event));
    EXPECT_EQ(event.kind, Kind::PitchBend);
    const std::array<uint8_t, 3> program{0xC2, 5, 0};
    ASSERT_TRUE(midi_script::eventOf(program.data(), 2, event));
    EXPECT_EQ(event.kind, Kind::ProgramChange);
    EXPECT_EQ(event.size, 2);
    const std::array<uint8_t, 3> pressure{0xD0, 40, 0};
    ASSERT_TRUE(midi_script::eventOf(pressure.data(), 2, event));
    EXPECT_EQ(event.kind, Kind::Other);
}

TEST(MidiScriptBindings, RealTimeMessagesDataBytesAndTruncatedMessagesHaveNoHook)
{
    MidiScriptEvent event;
    for (const uint8_t status : {0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF})
    {
        const std::array<uint8_t, 3> message{status, 0, 0};
        EXPECT_FALSE(midi_script::eventOf(message.data(), 1, event)) << static_cast<int>(status);
    }
    const std::array<uint8_t, 3> data{0x40, 0, 0};
    EXPECT_FALSE(midi_script::eventOf(data.data(), 1, event));
    const std::array<uint8_t, 3> truncated{0x90, 60, 0};
    EXPECT_FALSE(midi_script::eventOf(truncated.data(), 2, event));
    EXPECT_FALSE(midi_script::eventOf(truncated.data(), 0, event));
}

TEST(MidiScriptBindings, ACommandBecomesTheMessageWithTheChannelFrom1)
{
    MidiScriptBinder binder;
    script::CommandCall call;
    call.command = 1; // NoteOn
    call.number = {16, 127, 5};
    MidiScriptCommand command;
    char error[64];
    ASSERT_TRUE(binder.makeCommand(call, command, error, sizeof(error)));
    EXPECT_EQ(command.bytes, (std::array<uint8_t, 3>{0x9F, 127, 5}));
    call.command = 4; // PitchBend
    call.number = {1, -8192};
    ASSERT_TRUE(binder.makeCommand(call, command, error, sizeof(error)));
    EXPECT_EQ(command.bytes, (std::array<uint8_t, 3>{0xE0, 0, 0}));
    call.number = {1, 8191};
    ASSERT_TRUE(binder.makeCommand(call, command, error, sizeof(error)));
    EXPECT_EQ(command.bytes, (std::array<uint8_t, 3>{0xE0, 0x7F, 0x7F}));
    call.number = {2, 0};
    ASSERT_TRUE(binder.makeCommand(call, command, error, sizeof(error)));
    EXPECT_EQ(command.bytes, (std::array<uint8_t, 3>{0xE1, 0, 0x40}));
    call.command = 5; // ProgramChange
    call.number = {3, 9};
    ASSERT_TRUE(binder.makeCommand(call, command, error, sizeof(error)));
    EXPECT_EQ(command.bytes[0], 0xC2);
    EXPECT_EQ(command.bytes[1], 9);
    EXPECT_EQ(command.size, 2);
}

TEST(MidiScriptBindings, PassOutsideAHookForIncomingMidiIsRefused)
{
    MidiScriptBinder binder;
    script::CommandCall call;
    call.command = 6;
    MidiScriptCommand command;
    char error[96] = {};
    EXPECT_FALSE(binder.makeCommand(call, command, error, sizeof(error)));
    EXPECT_NE(std::string(error).find("Pass()"), std::string::npos);
}

TEST(MidiScript, PassthroughSendsEveryMessageUnchangedOneLatencyLater)
{
    for (const uint32_t block : {16u, 64u, 128u, 256u, 512u, 1024u})
    {
        Rig rig("pass", 50.0f);
        std::vector<Message> expected;
        uint64_t at = 100;
        for (int channel = 1; channel <= 16; ++channel)
        {
            rig.noteOn(at, channel, 40 + channel, 90);
            rig.noteOff(at + 300, channel, 40 + channel);
            rig.in(at + 50, static_cast<uint8_t>(0xB0 | (channel - 1)), 74, static_cast<uint8_t>(channel * 7));
            rig.in(at + 60, static_cast<uint8_t>(0xE0 | (channel - 1)), 12, 100);
            rig.in(at + 70, static_cast<uint8_t>(0xC0 | (channel - 1)), static_cast<uint8_t>(channel), 0, 2);
            rig.in(at + 80, static_cast<uint8_t>(0xD0 | (channel - 1)), 33, 0, 2);
            rig.in(at + 90, static_cast<uint8_t>(0x90 | (channel - 1)), static_cast<uint8_t>(80), 0);
            at += 700;
        }
        rig.run(at + 4000, block);
        // the same messages again, as they must come out
        std::vector<Message> input;
        uint64_t t = 100;
        for (int channel = 1; channel <= 16; ++channel)
        {
            const auto ch = static_cast<uint8_t>(channel - 1);
            input.push_back(msg(t, 0x90 | ch, static_cast<uint8_t>(40 + channel), 90));
            input.push_back(msg(t + 50, 0xB0 | ch, 74, static_cast<uint8_t>(channel * 7)));
            input.push_back(msg(t + 60, 0xE0 | ch, 12, 100));
            input.push_back(msg(t + 70, 0xC0 | ch, static_cast<uint8_t>(channel), 0, 2));
            input.push_back(msg(t + 80, 0xD0 | ch, 33, 0, 2));
            input.push_back(msg(t + 90, 0x90 | ch, 80, 0));
            input.push_back(msg(t + 300, 0x80 | ch, static_cast<uint8_t>(40 + channel), 64));
            t += 700;
        }
        const uint64_t delay = 2400; // 50 ms
        for (Message& message : input)
        {
            message.sample += delay;
        }
        std::vector<Message> sent = rig.sent();
        // messages at one sample keep the order they came in; compare as sorted lists
        const auto byTime = [](const Message& a, const Message& b)
        { return a.sample != b.sample ? a.sample < b.sample : a.bytes < b.bytes; };
        std::sort(input.begin(), input.end(), byTime);
        std::sort(sent.begin(), sent.end(), byTime);
        EXPECT_EQ(sent, input) << "block " << block;
        EXPECT_EQ(rig.pedal().getLate(), 0.0f) << "block " << block;
        EXPECT_EQ(rig.pedal().getSounding(), 0.0f);
        EXPECT_EQ(rig.pedal().getDropped(), 0.0f);
    }
}

TEST(MidiScript, TheArpeggiatorPlaysTheHeldNotesOnTheClockWhateverTheBlockLength)
{
    // 120 BPM, 24 ticks per quarter: a tick is 1000 samples, the step (6 ticks) 6000, the gate half of it. The clock
    // starts one latency into the run, the first tick is at 1 + L, and what it answers leaves one more latency later.
    for (const uint32_t block : {16u, 64u, 128u, 256u, 512u, 1024u})
    {
        const uint64_t latency = 2400; // 50 ms
        Rig rig("arp", 50.0f);
        rig.select(1);
        rig.activate();
        rig.noteOn(0, 1, 64);
        rig.noteOn(0, 1, 60);
        rig.noteOn(0, 1, 67);
        rig.run(2 * latency + 6000 * 12 + 4000, block);
        const auto got = firstNotes(rig.sent(), 24);
        EXPECT_EQ(got, arpeggio({60, 64, 67}, 2 * latency, 6000, 3000, 12)) << "block " << block;
        EXPECT_EQ(rig.pedal().getLate(), 0.0f) << "block " << block;
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f);
    }
}

TEST(MidiScript, TheArpeggiatorModesPlayTheirOrder)
{
    const struct
    {
        const char* mode;
        std::vector<int> order;
    } cases[] = {
        {"down", {67, 64, 60, 67, 64, 60}},
        {"updown", {60, 64, 67, 64, 60, 64, 67, 64}},
    };
    for (const auto& item : cases)
    {
        Rig rig(std::string("arp-") + item.mode);
        rig.writeUser(
            1, Rig::shipped("arpeggiator", "local mode = \"up\"", std::string("local mode = \"") + item.mode + "\""));
        rig.select(6);
        rig.activate();
        rig.noteOn(0, 1, 64);
        rig.noteOn(0, 1, 60);
        rig.noteOn(0, 1, 67);
        rig.run(2 * c_latency + 6000 * item.order.size() + 2000);
        EXPECT_EQ(firstNotes(rig.sent(), 2 * item.order.size()),
                  arpeggio(item.order, 2 * c_latency, 6000, 3000, item.order.size()))
            << item.mode;
    }
}

TEST(MidiScript, TheRandomArpeggioIsTheSameInEveryRunAndUsesOnlyTheHeldNotes)
{
    std::vector<Message> first;
    for (int run = 0; run < 2; ++run)
    {
        Rig rig("arp-random");
        rig.writeUser(1, Rig::shipped("arpeggiator", "local mode = \"up\"", "local mode = \"random\""));
        rig.select(6);
        rig.activate();
        rig.noteOn(0, 1, 60);
        rig.noteOn(0, 1, 64);
        rig.noteOn(0, 1, 67);
        rig.run(2 * c_latency + 6000 * 16);
        const auto notes = rig.sentWith(0xF0, 0x90);
        ASSERT_GE(notes.size(), 14u);
        for (const Message& message : notes)
        {
            EXPECT_TRUE(message.bytes[1] == 60 || message.bytes[1] == 64 || message.bytes[1] == 67);
        }
        std::vector<int> distinct;
        for (const Message& message : notes)
        {
            if (std::find(distinct.begin(), distinct.end(), message.bytes[1]) == distinct.end())
            {
                distinct.push_back(message.bytes[1]);
            }
        }
        EXPECT_GE(distinct.size(), 2u) << "not one note over and over";
        if (run == 0)
        {
            first = rig.sent();
        }
        else
        {
            EXPECT_EQ(rig.sent(), first);
        }
    }
}

TEST(MidiScript, TheArpeggiatorClimbsOctavesSwingsAndFollowsTheTempo)
{
    {
        Rig rig("arp-oct");
        rig.writeUser(1, Rig::shipped("arpeggiator", "local octaves = 1", "local octaves = 2"));
        rig.select(6);
        rig.activate();
        rig.noteOn(0, 1, 60);
        rig.noteOn(0, 1, 64);
        rig.run(2 * c_latency + 6000 * 4 + 1000);
        EXPECT_EQ(firstNotes(rig.sent(), 8), arpeggio({60, 64, 72, 76}, 2 * c_latency, 6000, 3000, 4));
    }
    {
        Rig rig("arp-swing");
        rig.writeUser(1, Rig::shipped("arpeggiator", "local swing = 0.0", "local swing = 0.5"));
        rig.select(6);
        rig.activate();
        rig.noteOn(0, 1, 60);
        rig.run(2 * c_latency + 23000);
        const auto on = rig.sentWith(0xF0, 0x90);
        ASSERT_EQ(on.size(), 4u);
        // the odd steps are half a step (3 ticks) late
        EXPECT_EQ(on[0].sample, 2 * c_latency);
        EXPECT_EQ(on[1].sample, 2 * c_latency + 6000 + 3000);
        EXPECT_EQ(on[2].sample, 2 * c_latency + 12000);
        EXPECT_EQ(on[3].sample, 2 * c_latency + 18000 + 3000);
    }
    {
        Rig rig("arp-tempo");
        rig.pedal().setTempo(240.0f);
        rig.select(1);
        rig.activate();
        rig.noteOn(0, 1, 60);
        rig.run(2 * c_latency + 3000 * 3 + 1000);
        EXPECT_EQ(firstNotes(rig.sent(), 6), arpeggio({60}, 2 * c_latency, 3000, 1500, 3)) << "a tick is 500 samples";
    }
}

TEST(MidiScript, ReleasingTheKeysStopsTheArpeggioAndLeavesNoNoteOn)
{
    Rig rig("arp-release");
    rig.select(1);
    rig.activate();
    rig.noteOn(0, 1, 60);
    rig.noteOn(0, 1, 64);
    rig.noteOff(20000, 1, 60);
    rig.noteOff(20000, 1, 64);
    rig.run(60000);
    const auto on = rig.sentWith(0xF0, 0x90);
    const auto off = rig.sentWith(0xF0, 0x80);
    ASSERT_FALSE(on.empty());
    EXPECT_EQ(on.size(), off.size());
    EXPECT_LT(on.back().sample, 20000 + 2 * c_latency + 128 + 6000) << "no step after the keys went up";
    EXPECT_EQ(rig.pedal().getSounding(), 0.0f);
}

TEST(MidiScript, TheChordPlayerSoundsTheChordOnEveryKeyAndEndsItWithTheKey)
{
    Rig rig("chord");
    rig.select(2);
    rig.activate();
    rig.noteOn(1000, 1, 60, 80);
    rig.noteOff(5000, 1, 60);
    rig.noteOn(7000, 3, 50, 100);
    rig.noteOff(9000, 3, 50);
    rig.run(12000);
    const uint64_t t = c_latency;
    const std::vector<Message> expected{
        msg(1000 + t, 0x90, 60, 80),  msg(1000 + t, 0x90, 64, 80),  msg(1000 + t, 0x90, 67, 80),
        msg(5000 + t, 0x80, 60, 64),  msg(5000 + t, 0x80, 64, 64),  msg(5000 + t, 0x80, 67, 64),
        msg(7000 + t, 0x92, 50, 100), msg(7000 + t, 0x92, 54, 100), msg(7000 + t, 0x92, 57, 100),
        msg(9000 + t, 0x82, 50, 64),  msg(9000 + t, 0x82, 54, 64),  msg(9000 + t, 0x82, 57, 64),
    };
    EXPECT_EQ(rig.sent(), expected);
}

TEST(MidiScript, TheChordPlayerStrumsAndInverts)
{
    Rig rig("chord-strum");
    rig.writeUser(1, Rig::shipped("chord-player", "local strumMs = 0", "local strumMs = 10"));
    rig.writeUser(2, Rig::shipped("chord-player", "local inversion = 0", "local inversion = 1"));
    rig.select(6);
    rig.activate();
    rig.noteOn(1000, 1, 60);
    rig.run(4000);
    const uint64_t t = c_latency;
    const std::vector<Message> strum{msg(1000 + t, 0x90, 60, 100), msg(1000 + t + 480, 0x90, 64, 100),
                                     msg(1000 + t + 960, 0x90, 67, 100)};
    EXPECT_EQ(rig.sent(), strum);

    Rig inverted("chord-invert");
    inverted.writeUser(1, Rig::shipped("chord-player", "local inversion = 0", "local inversion = 1"));
    inverted.select(6);
    inverted.activate();
    inverted.noteOn(1000, 1, 60);
    inverted.run(4000);
    const std::vector<Message> first{msg(1000 + t, 0x90, 64, 100), msg(1000 + t, 0x90, 67, 100),
                                     msg(1000 + t, 0x90, 72, 100)};
    EXPECT_EQ(inverted.sent(), first);
}

TEST(MidiScript, TheScaleQuantiserMovesEveryNoteIntoTheScaleAndTheNoteOffFollows)
{
    Rig rig("quantise");
    rig.select(3);
    rig.activate();
    uint64_t at = 100;
    std::vector<Message> expected;
    const int major[] = {0, 2, 4, 5, 7, 9, 11};
    for (int note = 0; note < 128; ++note)
    {
        rig.noteOn(at, 1, note);
        rig.noteOff(at + 200, 1, note);
        int out = note;
        for (int distance = 0; distance <= 6; ++distance)
        {
            const auto inScale = [&](const int n)
            { return std::find(std::begin(major), std::end(major), n % 12) != std::end(major); };
            if (inScale(note - distance))
            {
                out = note - distance;
                break;
            }
            if (inScale(note + distance))
            {
                out = note + distance;
                break;
            }
        }
        expected.push_back(msg(at + c_latency, 0x90, static_cast<uint8_t>(out), 100));
        expected.push_back(msg(at + 200 + c_latency, 0x80, static_cast<uint8_t>(out), 64));
        at += 400;
    }
    rig.run(at + 1000);
    EXPECT_EQ(rig.sent(), expected);
    EXPECT_EQ(rig.pedal().getSounding(), 0.0f);
    // examples from the scale: C# goes down to C, D# to D, F# to F
    EXPECT_EQ(rig.sent()[2 * 61].bytes[1], 60);
    EXPECT_EQ(rig.sent()[2 * 63].bytes[1], 62);
    EXPECT_EQ(rig.sent()[2 * 66].bytes[1], 65);
}

TEST(MidiScript, TheOctaveDoublerAddsTheNoteAnOctaveUpAndSkipsWhatDoesNotExist)
{
    Rig rig("doubler");
    rig.select(4);
    rig.activate();
    rig.noteOn(1000, 2, 60, 100);
    rig.noteOff(3000, 2, 60);
    rig.noteOn(4000, 2, 120, 100);
    rig.noteOff(6000, 2, 120);
    rig.run(8000);
    const uint64_t t = c_latency;
    const std::vector<Message> expected{
        msg(1000 + t, 0x91, 60, 100), msg(1000 + t, 0x91, 72, 70),   msg(3000 + t, 0x81, 60, 64),
        msg(3000 + t, 0x81, 72, 64),  msg(4000 + t, 0x91, 120, 100), msg(6000 + t, 0x81, 120, 64),
    };
    EXPECT_EQ(rig.sent(), expected);
}

TEST(MidiScript, TheCcRemapTurnsControllersIntoOthersAndPassesTheRest)
{
    Rig rig("remap");
    rig.select(5);
    rig.activate();
    rig.in(1000, 0xB0, 1, 100);
    rig.in(1100, 0xB2, 11, 64);
    rig.in(1200, 0xB0, 64, 127);
    rig.noteOn(1300, 1, 60);
    rig.in(1400, 0xE0, 5, 70);
    rig.run(4000);
    const uint64_t t = c_latency;
    const std::vector<Message> expected{
        msg(1000 + t, 0xB0, 74, 100), msg(1100 + t, 0xB2, 7, 64), msg(1200 + t, 0xB0, 64, 127),
        msg(1300 + t, 0x90, 60, 100), msg(1400 + t, 0xE0, 5, 70),
    };
    EXPECT_EQ(rig.sent(), expected);
}

TEST(MidiScript, ANoteOnWithVelocityZeroReachesOnNoteOffWithVelocityZero)
{
    Rig rig("zero");
    rig.writeUser(1, "function OnNoteOff(channel, note, velocity) CC(channel, 20, velocity + 5) end\n"
                     "function OnNoteOn(channel, note, velocity) CC(channel, 21, velocity) end\n");
    rig.select(6);
    rig.activate();
    rig.noteOn(1000, 1, 60, 99);
    rig.noteOn(2000, 1, 60, 0);
    rig.noteOff(3000, 1, 60);
    rig.run(5000);
    const uint64_t t = c_latency;
    const std::vector<Message> expected{msg(1000 + t, 0xB0, 21, 99), msg(2000 + t, 0xB0, 20, 5),
                                        msg(3000 + t, 0xB0, 20, 69)};
    EXPECT_EQ(rig.sent(), expected);
}

TEST(MidiScript, AReloadEndsTheNotesTheOldScriptLeftOn)
{
    Rig rig("reload");
    rig.writeUser(1, "function OnNoteOn(channel, note, velocity) NoteOn(channel, note, velocity) end\n");
    rig.select(6);
    rig.activate();
    rig.noteOn(1000, 1, 60);
    rig.noteOn(1100, 2, 61);
    rig.noteOn(1200, 3, 62);
    rig.run(10000);
    ASSERT_EQ(rig.pedal().getSounding(), 3.0f);
    rig.writeUser(1, "function OnNoteOn(channel, note, velocity) end\n");
    rig.run(40000); // the file is looked at twice a second
    EXPECT_EQ(rig.pedal().getSounding(), 0.0f);
    const auto off = rig.sentWith(0xF0, 0x80);
    ASSERT_EQ(off.size(), 3u);
    std::vector<int> notes;
    for (const Message& message : off)
    {
        notes.push_back(message.bytes[1]);
    }
    std::sort(notes.begin(), notes.end());
    EXPECT_EQ(notes, (std::vector<int>{60, 61, 62}));
    EXPECT_EQ(rig.sentWith(0xF0, 0x90).size(), 3u) << "the new script does not play";
}

TEST(MidiScript, AnErrorInAHookEndsTheNotesDropsThatCallAndTheScriptGoesOn)
{
    Rig rig("error");
    rig.writeUser(1, "local n = 0\n"
                     "function OnNoteOn(channel, note, velocity)\n"
                     "    n = n + 1\n"
                     "    NoteOn(channel, note, velocity)\n"
                     "    if n == 3 then error('boom') end\n"
                     "end\n");
    rig.select(6);
    rig.activate();
    rig.noteOn(1000, 1, 60);
    rig.noteOn(3000, 1, 61);
    rig.noteOn(5000, 1, 62);
    rig.noteOn(7000, 1, 63);
    rig.run(12000);
    EXPECT_EQ(rig.pedal().getScriptStatus(), 4.0f);
    const auto on = rig.sentWith(0xF0, 0x90);
    ASSERT_EQ(on.size(), 3u) << "the call that failed says nothing";
    EXPECT_EQ(on[0].bytes[1], 60);
    EXPECT_EQ(on[1].bytes[1], 61);
    EXPECT_EQ(on[2].bytes[1], 63) << "the next event runs";
    const auto off = rig.sentWith(0xF0, 0x80);
    ASSERT_EQ(off.size(), 2u) << "the error ended the two notes that were on";
    EXPECT_EQ(rig.pedal().getSounding(), 1.0f) << "and the note after it sounds";
}

TEST(MidiScript, AFileThatDoesNotLoadChangesNothingThePreviousScriptKeepsPlaying)
{
    Rig rig("keeps");
    rig.writeUser(1, "function OnNoteOn(channel, note, velocity) NoteOn(channel, note, velocity) end\n");
    rig.select(6);
    rig.activate();
    rig.noteOn(1000, 1, 60);
    rig.run(4000);
    ASSERT_EQ(rig.sentWith(0xF0, 0x90).size(), 1u);
    rig.writeUser(1, "function OnNoteOn( oops\n");
    rig.run(30000);
    EXPECT_EQ(rig.pedal().getScriptStatus(), 3.0f);
    EXPECT_EQ(rig.pedal().getSounding(), 1.0f) << "no notes were ended";
    rig.noteOn(31000, 1, 61);
    rig.run(34000);
    EXPECT_EQ(rig.sentWith(0xF0, 0x90).size(), 2u) << "the old script still answers";
}

TEST(MidiScript, APanicEndsEverySoundingNoteAndTellsAllChannels)
{
    Rig rig("panic");
    rig.select(1);
    rig.activate();
    rig.noteOn(0, 1, 60);
    rig.run(2 * c_latency + 6000 + 1000);
    rig.pedal().setPanic(1.0f);
    rig.run(rig.sample() + 3000);
    rig.pedal().setPanic(0.0f);
    int channelsTold = 0;
    for (const Message& message : rig.sent())
    {
        channelsTold += ((message.bytes[0] & 0xF0) == 0xB0 && message.bytes[1] == 123) ? 1 : 0;
    }
    EXPECT_EQ(channelsTold, 16);
    EXPECT_EQ(rig.pedal().getSounding(), 0.0f);
}

TEST(MidiScript, AScriptCannotFloodTheOutputAndAPanicClearsWhatItStarted)
{
    Rig rig("flood");
    rig.writeUser(1, "function OnNoteOn(channel, note, velocity)\n"
                     "    for i = 1, 300 do NoteOn(channel, i % 100 + 1, velocity) end\n"
                     "end\n");
    rig.select(6);
    rig.activate();
    for (int i = 0; i < 10; ++i)
    {
        rig.noteOn(1000 + 500 * i, 1, 60);
    }
    rig.run(12000);
    EXPECT_GT(rig.pedal().getDropped(), 0.0f);
    // never more than 128 messages in a window of 0.0027 s (129 samples)
    const auto on = rig.sentWith(0xF0, 0x90);
    ASSERT_FALSE(on.empty());
    EXPECT_LT(on.size(), 3000u);
    rig.pedal().setPanic(1.0f);
    rig.run(rig.sample() + 1000);
    EXPECT_EQ(rig.pedal().getSounding(), 0.0f);
}

TEST(MidiScript, AReactivationEndsTheNotesThatSound)
{
    Rig rig("reactivate");
    rig.writeUser(1, "function OnNoteOn(channel, note, velocity) NoteOn(channel, note, velocity) end\n");
    rig.select(6);
    rig.activate();
    rig.noteOn(1000, 1, 60);
    rig.run(4000);
    ASSERT_EQ(rig.pedal().getSounding(), 1.0f);
    rig.activate();
    rig.run(8000);
    EXPECT_EQ(rig.pedal().getSounding(), 0.0f);
    EXPECT_EQ(rig.sentWith(0xF0, 0x80).size(), 1u);
}

TEST(MidiScript, ARepeatingTimerFiresOnItsExactSamples)
{
    Rig rig("timer");
    rig.writeUser(1, "function OnTransportStart() SetTimer(0, {ms = 10, repeating = true}) end\n"
                     "function OnTimer(id) NoteOn(1, 70 + id, 100) end\n");
    rig.select(6);
    rig.activate();
    rig.run(480 * 20 + 2000);
    const auto on = rig.sentWith(0xF0, 0x90);
    ASSERT_GE(on.size(), 18u);
    // the transport starts at 1 + L, the timer counts from there: 480 k after it, plus the latency of the output
    for (size_t k = 0; k < 18; ++k)
    {
        EXPECT_EQ(on[k].sample, 481 + 480 * (k + 1) + c_latency - 1) << k;
    }
    EXPECT_EQ(rig.pedal().getLate(), 0.0f);
}

TEST(MidiScript, SyncToTheTransportRunsTheClockOnlyWhileTheTransportRolls)
{
    Rig rig("transport");
    rig.writeUser(1, "function OnTransportStart() CC(1, 30, 1) end\n"
                     "function OnTransportStop() CC(1, 30, 0) end\n"
                     "function OnTick(tick) if tick % 24 == 0 then CC(1, 31, 1) end end\n");
    rig.pedal().setSync(1.0f);
    rig.select(6);
    rig.activate();
    rig.run(5000);
    EXPECT_TRUE(rig.sent().empty()) << "the transport is stopped: no clock";
    rig.pedal().setSpeed(1.0f);
    rig.run(5000 + 24000 * 2 + 1000);
    const auto cc = rig.sentWith(0xF0, 0xB0);
    ASSERT_GE(cc.size(), 3u);
    EXPECT_EQ(cc[0].bytes[1], 30);
    EXPECT_EQ(cc[0].bytes[2], 1);
    EXPECT_EQ(cc[0].sample, 5000 + 1 + c_latency + c_latency - 1);
    EXPECT_EQ(cc[1].bytes[1], 31);
    EXPECT_EQ(cc[2].sample - cc[1].sample, 24000u) << "a beat of 24 ticks at 120 BPM";
    rig.pedal().setSpeed(0.0f);
    rig.run(rig.sample() + 3000);
    const auto stop = rig.sentWith(0xFF, 0xB0, 5000 + 48000);
    ASSERT_FALSE(stop.empty());
    EXPECT_EQ(stop.back().bytes[1], 30);
    EXPECT_EQ(stop.back().bytes[2], 0);
}

TEST(MidiScript, TheScriptStatusShowsAMissingFileAndASyntaxError)
{
    Rig rig("status");
    rig.select(6);
    rig.activate();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 2.0f);
    rig.writeUser(1, "function OnNoteOn(\n");
    rig.run(30000);
    EXPECT_EQ(rig.pedal().getScriptStatus(), 3.0f);
    rig.writeUser(1, "function OnNoteOn(c, n, v) Pass() end\n");
    rig.run(60000);
    EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f);
}

TEST(MidiScript, EveryShippedScriptLoads)
{
    for (int entry = 0; entry < 6; ++entry)
    {
        Rig rig("loads-" + std::to_string(entry));
        rig.select(entry);
        rig.activate();
        rig.noteOn(100, 1, 60);
        rig.noteOff(4000, 1, 60);
        rig.run(30000);
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << entry;
        EXPECT_EQ(rig.pedal().getSounding(), 0.0f) << entry;
    }
}

TEST(MidiScript, EveryExampleLoadsRunsAndLeavesNoNoteHanging)
{
    const fs::path folder = std::string(JP_SOURCE_DIR) + "/plugins/midiscript/scripts/examples";
    std::vector<fs::path> files;
    for (const auto& file : fs::directory_iterator(folder))
    {
        if (file.path().extension() == ".lua")
        {
            files.push_back(file.path());
        }
    }
    std::sort(files.begin(), files.end());
    ASSERT_GE(files.size(), 7u);
    for (const fs::path& file : files)
    {
        std::ifstream in(file);
        ASSERT_TRUE(in.good()) << file;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Rig rig("example-" + file.stem().string());
        rig.writeUser(1, text);
        rig.select(6);
        rig.activate();
        // after the start of the clock (one latency into the run), so that a timer set by a key has a clock
        rig.noteOn(3000, 1, 60);
        rig.noteOn(3100, 1, 64, 80);
        rig.in(3200, 0xB0, 1, 64);
        rig.in(3300, 0xB0, 7, 100);
        rig.in(3400, 0xE0, 0, 80);
        rig.in(3500, 0xC0, 3, 0, 2);
        rig.noteOff(60000, 1, 60);
        rig.noteOff(60100, 1, 64);
        rig.run(90000);
        EXPECT_FALSE(rig.sent().empty()) << file.filename() << " sent nothing";
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << file.filename();
        EXPECT_EQ(rig.pedal().getSounding(), 0.0f) << file.filename();
        EXPECT_EQ(rig.pedal().getLate(), 0.0f) << file.filename();
        EXPECT_EQ(rig.pedal().getDropped(), 0.0f) << file.filename();
    }
}

}
