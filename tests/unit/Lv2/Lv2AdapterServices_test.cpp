// The MIDI output and the worker of lv2/Lv2Adapter.h without a plugin binary: recording pedals show where the
// adapter puts the messages a pedal sends and how it passes jobs and answers between pedal and host.

#include <array>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "lv2/Lv2Adapter.h"
#include "lv2/PedalServices.h"
#include "lv2/PortTable.h"

namespace jp::test
{

namespace
{
constexpr LV2_URID c_midiEventUrid{7};
constexpr LV2_URID c_sequenceUrid{8};
constexpr LV2_URID c_otherUrid{9};

struct Message
{
    uint32_t frame{0};
    std::vector<uint8_t> bytes;

    bool operator==(const Message& other) const
    {
        return frame == other.frame && bytes == other.bytes;
    }
};

using Messages = std::vector<Message>;

/// What the pedals of this file are told to do; reset by every Rig.
struct Script
{
    bool thru{false};             ///< midi() sends what it receives at frame 0 of its call
    int processFrame{-1};         ///< process() sends one note on at this frame of its call when it is >= 0
    std::vector<uint8_t> sizes{}; ///< process() sends one message of each size (bytes 1, 2, 3, ...) at frame 0
    bool lastSendWorked{true};
    uint32_t droppedAfter{0};
    std::vector<size_t> processCalls{};
    bool scheduleOnProcess{false};
    bool lastScheduleWorked{false};
    std::vector<std::vector<uint8_t>> workReceived{};
    std::vector<std::vector<uint8_t>> responses{};
    std::vector<std::vector<uint8_t>> workResponses{};
    std::vector<bool> respondWorked{};
    std::string bundlePath{};
    bool bundleBeforeSampleRate{false};
    bool sampleRateSet{false};
};

Script g_script;

template <typename Derived>
class ServicePedal
{
  public:
    void setSampleRate(float) noexcept
    {
        g_script.bundleBeforeSampleRate = !g_script.bundlePath.empty();
        g_script.sampleRateSet = true;
    }

    void reset() noexcept {}

    void setBundlePath(const char* folder)
    {
        g_script.bundlePath = folder;
    }

    void setMidiOutput(MidiOutput& output) noexcept
    {
        m_output = &output;
    }

    void midi(const uint8_t* msg, const uint32_t size) noexcept
    {
        if (g_script.thru)
        {
            g_script.lastSendWorked = m_output->send(0, msg, size);
            g_script.droppedAfter = m_output->dropped();
        }
    }

    void setWorkScheduler(WorkScheduler& scheduler) noexcept
    {
        m_scheduler = &scheduler;
    }

    void work(const void* data, const uint32_t size, WorkResponder& responder) noexcept
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        g_script.workReceived.emplace_back(bytes, bytes + size);
        const std::vector<uint8_t> answer{static_cast<uint8_t>(size), 0xAA};
        g_script.respondWorked.push_back(responder.respond(answer.data(), static_cast<uint32_t>(answer.size())));
    }

    void workResponse(const void* data, const uint32_t size) noexcept
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        g_script.workResponses.emplace_back(bytes, bytes + size);
    }

  protected:
    void act(const size_t n) noexcept
    {
        g_script.processCalls.push_back(n);
        if (g_script.processFrame >= 0)
        {
            const std::array<uint8_t, 3> note{0x90, 60, 100};
            g_script.lastSendWorked = m_output->send(static_cast<uint32_t>(g_script.processFrame), note.data(), 3);
        }
        for (const uint8_t size : g_script.sizes)
        {
            const std::array<uint8_t, 4> bytes{1, 2, 3, 4};
            g_script.lastSendWorked = m_output->send(0, bytes.data(), size);
        }
        g_script.droppedAfter = m_output != nullptr ? m_output->dropped() : 0;
        if (g_script.scheduleOnProcess)
        {
            const std::array<uint8_t, 3> job{1, 2, 3};
            g_script.lastScheduleWorked = m_scheduler->schedule(job.data(), 3);
        }
    }

    MidiOutput* m_output{nullptr};
    WorkScheduler* m_scheduler{nullptr};
};

/// One audio output.
class MonoPedal : public ServicePedal<MonoPedal>
{
  public:
    void process(float*, const size_t n) noexcept
    {
        act(n);
    }
};

/// No audio at all: a MIDI processor.
class SilentPedal : public ServicePedal<SilentPedal>
{
  public:
    void process(const size_t n) noexcept
    {
        act(n);
    }
};

struct MidiThruPorts
{
    using Pedal = MonoPedal;
    static constexpr const char* c_uri{"urn:test:thru"};
    static constexpr uint32_t c_audioInputs{0};
    static constexpr uint32_t c_audioOutputs{1};
    static constexpr std::array<ControlInput<Pedal>, 0> c_controlInputs{};
    static constexpr std::array<ControlOutput<Pedal>, 0> c_controlOutputs{};
    static constexpr bool c_midiInput{true};
    static constexpr uint32_t c_midiIndex{1};
    static constexpr bool c_midiOutput{true};
    static constexpr uint32_t c_midiOutIndex{2};
    static constexpr bool c_worker{false};
};

struct MidiProcessorPorts
{
    using Pedal = SilentPedal;
    static constexpr const char* c_uri{"urn:test:processor"};
    static constexpr uint32_t c_audioInputs{0};
    static constexpr uint32_t c_audioOutputs{0};
    static constexpr std::array<ControlInput<Pedal>, 0> c_controlInputs{};
    static constexpr std::array<ControlOutput<Pedal>, 0> c_controlOutputs{};
    static constexpr bool c_midiInput{true};
    static constexpr uint32_t c_midiIndex{0};
    static constexpr bool c_midiOutput{true};
    static constexpr uint32_t c_midiOutIndex{1};
    static constexpr bool c_worker{false};
};

struct GeneratorPorts
{
    using Pedal = MonoPedal;
    static constexpr const char* c_uri{"urn:test:generator"};
    static constexpr uint32_t c_audioInputs{0};
    static constexpr uint32_t c_audioOutputs{1};
    static constexpr std::array<ControlInput<Pedal>, 0> c_controlInputs{};
    static constexpr std::array<ControlOutput<Pedal>, 0> c_controlOutputs{};
    static constexpr bool c_midiInput{false};
    static constexpr uint32_t c_midiIndex{0};
    static constexpr bool c_midiOutput{true};
    static constexpr uint32_t c_midiOutIndex{1};
    static constexpr bool c_worker{false};
};

struct WorkerPorts
{
    using Pedal = MonoPedal;
    static constexpr const char* c_uri{"urn:test:worker"};
    static constexpr uint32_t c_audioInputs{0};
    static constexpr uint32_t c_audioOutputs{1};
    static constexpr std::array<ControlInput<Pedal>, 0> c_controlInputs{};
    static constexpr std::array<ControlOutput<Pedal>, 0> c_controlOutputs{};
    static constexpr bool c_midiInput{false};
    static constexpr uint32_t c_midiIndex{0};
    static constexpr bool c_midiOutput{false};
    static constexpr uint32_t c_midiOutIndex{0};
    static constexpr bool c_worker{true};
};

struct PlainPorts
{
    using Pedal = MonoPedal;
    static constexpr const char* c_uri{"urn:test:plain"};
    static constexpr uint32_t c_audioInputs{0};
    static constexpr uint32_t c_audioOutputs{1};
    static constexpr std::array<ControlInput<Pedal>, 0> c_controlInputs{};
    static constexpr std::array<ControlOutput<Pedal>, 0> c_controlOutputs{};
    static constexpr bool c_midiInput{false};
    static constexpr uint32_t c_midiIndex{0};
    static constexpr bool c_midiOutput{false};
    static constexpr uint32_t c_midiOutIndex{0};
    static constexpr bool c_worker{false};
};

LV2_URID mapUri(LV2_URID_Map_Handle, const char* uri)
{
    if (std::strcmp(uri, LV2_MIDI__MidiEvent) == 0)
    {
        return c_midiEventUrid;
    }
    return std::strcmp(uri, LV2_ATOM__Sequence) == 0 ? c_sequenceUrid : c_otherUrid;
}

/// What the host did with the worker: the jobs it was given and the answers it was handed.
struct WorkerHost
{
    std::vector<std::vector<uint8_t>> jobs;
    std::vector<std::vector<uint8_t>> answers;
    LV2_Worker_Status scheduleStatus{LV2_WORKER_SUCCESS};
    LV2_Worker_Status respondStatus{LV2_WORKER_SUCCESS};
};

LV2_Worker_Status scheduleWork(LV2_Worker_Schedule_Handle handle, const uint32_t size, const void* data)
{
    auto* host = static_cast<WorkerHost*>(handle);
    const auto* bytes = static_cast<const uint8_t*>(data);
    host->jobs.emplace_back(bytes, bytes + size);
    return host->scheduleStatus;
}

LV2_Worker_Status respondWork(LV2_Worker_Respond_Handle handle, const uint32_t size, const void* data)
{
    auto* host = static_cast<WorkerHost*>(handle);
    const auto* bytes = static_cast<const uint8_t*>(data);
    host->answers.emplace_back(bytes, bytes + size);
    return host->respondStatus;
}

/// An atom sequence in the host's layout: header, then the events padded to 8 bytes.
class Sequence
{
  public:
    explicit Sequence(const size_t bytes = 4096)
        : m_bytes(bytes, 0)
    {
        prepareInput();
    }

    Sequence& add(const int64_t frame, const std::vector<uint8_t>& message)
    {
        auto* event = reinterpret_cast<LV2_Atom_Event*>(m_bytes.data() + m_end);
        event->time.frames = frame;
        event->body.type = c_midiEventUrid;
        event->body.size = static_cast<uint32_t>(message.size());
        std::memcpy(m_bytes.data() + m_end + sizeof(LV2_Atom_Event), message.data(), message.size());
        m_end += (sizeof(LV2_Atom_Event) + message.size() + 7u) & ~size_t{7};
        reinterpret_cast<LV2_Atom_Sequence*>(m_bytes.data())->atom.size =
            static_cast<uint32_t>(m_end - sizeof(LV2_Atom));
        return *this;
    }

    /// As a host presents an output port before run(): the atom size is the capacity.
    void prepareOutput()
    {
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(m_bytes.data());
        sequence->atom.type = 0;
        sequence->atom.size = static_cast<uint32_t>(m_bytes.size() - sizeof(LV2_Atom));
    }

    /// The messages the plugin wrote, in the order of the sequence.
    [[nodiscard]] Messages read() const
    {
        const auto* sequence = reinterpret_cast<const LV2_Atom_Sequence*>(m_bytes.data());
        EXPECT_EQ(sequence->atom.type, c_sequenceUrid);
        EXPECT_EQ(sequence->body.unit, 0u);
        Messages messages;
        const uint8_t* cursor = m_bytes.data() + sizeof(LV2_Atom_Sequence);
        const uint8_t* end = m_bytes.data() + sizeof(LV2_Atom) + sequence->atom.size;
        while (cursor < end)
        {
            const auto* event = reinterpret_cast<const LV2_Atom_Event*>(cursor);
            EXPECT_EQ(event->body.type, c_midiEventUrid);
            const uint8_t* body = cursor + sizeof(LV2_Atom_Event);
            messages.push_back({static_cast<uint32_t>(event->time.frames), {body, body + event->body.size}});
            cursor += (sizeof(LV2_Atom_Event) + event->body.size + 7u) & ~size_t{7};
        }
        EXPECT_EQ(cursor, end);
        return messages;
    }

    [[nodiscard]] void* data()
    {
        return m_bytes.data();
    }

    [[nodiscard]] uint32_t atomSize() const
    {
        return reinterpret_cast<const LV2_Atom_Sequence*>(m_bytes.data())->atom.size;
    }

  private:
    void prepareInput()
    {
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(m_bytes.data());
        sequence->atom.type = c_sequenceUrid;
        sequence->body.unit = 0;
        m_end = sizeof(LV2_Atom_Sequence);
        sequence->atom.size = static_cast<uint32_t>(m_end - sizeof(LV2_Atom));
    }

    std::vector<uint8_t> m_bytes; // heap storage is aligned for any atom
    size_t m_end{0};
};

/// One instance with the host features given, audio and the atom ports connected as asked.
template <typename Ports>
class Rig
{
  public:
    using Adapter = Lv2Adapter<Ports>;

    explicit Rig(WorkerHost* worker = nullptr, const bool withMap = true)
    {
        g_script = Script{};
        m_descriptor = Adapter::descriptor();
        LV2_URID_Map map{nullptr, mapUri};
        const LV2_Feature mapFeature{LV2_URID__map, &map};
        m_schedule = LV2_Worker_Schedule{worker, scheduleWork};
        const LV2_Feature workerFeature{LV2_WORKER__schedule, &m_schedule};
        std::vector<const LV2_Feature*> features;
        if (withMap)
        {
            features.push_back(&mapFeature);
        }
        if (worker != nullptr)
        {
            features.push_back(&workerFeature);
        }
        features.push_back(nullptr);
        m_handle = m_descriptor->instantiate(m_descriptor, 48000.0, "/bundle/test.lv2/", features.data());
        if (m_handle == nullptr)
        {
            return;
        }
        for (uint32_t i = 0; i < Ports::c_audioOutputs; ++i)
        {
            m_descriptor->connect_port(m_handle, i, m_audio.data() + 256 * i);
        }
        m_descriptor->activate(m_handle);
    }

    ~Rig()
    {
        if (m_handle != nullptr)
        {
            m_descriptor->cleanup(m_handle);
        }
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void connectInput(Sequence& sequence)
    {
        m_descriptor->connect_port(m_handle, Ports::c_midiIndex, sequence.data());
    }

    void connectOutput(Sequence& sequence)
    {
        m_descriptor->connect_port(m_handle, Ports::c_midiOutIndex, sequence.data());
    }

    void disconnectOutput()
    {
        m_descriptor->connect_port(m_handle, Ports::c_midiOutIndex, nullptr);
    }

    void run(const uint32_t n)
    {
        g_script.processCalls.clear();
        m_descriptor->run(m_handle, n);
    }

    [[nodiscard]] bool instantiated() const
    {
        return m_handle != nullptr;
    }

    [[nodiscard]] const LV2_Descriptor* descriptor() const
    {
        return m_descriptor;
    }

    [[nodiscard]] LV2_Handle handle() const
    {
        return m_handle;
    }

  private:
    std::array<float, 1024> m_audio{};
    LV2_Worker_Schedule m_schedule{};
    const LV2_Descriptor* m_descriptor{nullptr};
    LV2_Handle m_handle{nullptr};
};

const LV2_Worker_Interface* workerInterface(const LV2_Descriptor* descriptor)
{
    return static_cast<const LV2_Worker_Interface*>(descriptor->extension_data(LV2_WORKER__interface));
}
}

TEST(Lv2AdapterMidiOut, TheInputIsPassedOnAtItsFramesInItsOrder)
{
    Rig<MidiThruPorts> rig;
    g_script.thru = true;
    Sequence in;
    Sequence out;
    in.add(0, {0x90, 60, 100}).add(10, {0x91, 61, 101}).add(10, {0x92, 62, 102}).add(50, {0xD0, 77});
    out.prepareOutput();
    rig.connectInput(in);
    rig.connectOutput(out);
    rig.run(128);
    EXPECT_EQ(out.read(),
              (Messages{{0, {0x90, 60, 100}}, {10, {0x91, 61, 101}}, {10, {0x92, 62, 102}}, {50, {0xD0, 77}}}));
}

TEST(Lv2AdapterMidiOut, AMessageFromProcessIsOffsetByTheStartOfItsCall)
{
    Rig<MidiThruPorts> rig;
    g_script.processFrame = 5;
    Sequence in;
    Sequence out;
    in.add(20, {0xB0, 1, 2});
    out.prepareOutput();
    rig.connectInput(in);
    rig.connectOutput(out);
    rig.run(64);
    EXPECT_EQ(g_script.processCalls, (std::vector<size_t>{20, 44}));
    // process(0..20) wrote at 5, the call after the event starts at 20 and wrote at 25
    EXPECT_EQ(out.read(), (Messages{{5, {0x90, 60, 100}}, {25, {0x90, 60, 100}}}));
}

TEST(Lv2AdapterMidiOut, FramesAreRaisedToThePreviousOneAndLoweredToTheBlock)
{
    Rig<MidiThruPorts> rig;
    g_script.processFrame = 100;
    g_script.thru = true;
    Sequence in;
    Sequence out;
    in.add(30, {0x80, 60, 0});
    out.prepareOutput();
    rig.connectInput(in);
    rig.connectOutput(out);
    rig.run(64);
    // the first call (30 frames) sends frame 100 -> 63; the thru message of the event at 30 may not go back; the
    // second call (34 frames) sends 30 + 100 -> 63
    EXPECT_EQ(out.read(), (Messages{{63, {0x90, 60, 100}}, {63, {0x80, 60, 0}}, {63, {0x90, 60, 100}}}));
}

TEST(Lv2AdapterMidiOut, TheSequenceIsEmptiedAtEveryRun)
{
    Rig<MidiThruPorts> rig;
    g_script.thru = true;
    Sequence in;
    Sequence out;
    in.add(3, {0x90, 60, 100});
    out.prepareOutput();
    rig.connectInput(in);
    rig.connectOutput(out);
    rig.run(32);
    EXPECT_EQ(out.read().size(), 1u);
    Sequence silence;
    rig.connectInput(silence);
    out.prepareOutput();
    rig.run(32);
    EXPECT_TRUE(out.read().empty());
    EXPECT_EQ(out.atomSize(), sizeof(LV2_Atom_Sequence_Body));
}

TEST(Lv2AdapterMidiOut, WhatDoesNotFitIsDroppedAndCounted)
{
    Rig<MidiThruPorts> rig;
    g_script.thru = true;
    Sequence in;
    Sequence out(sizeof(LV2_Atom_Sequence) + 2 * 24); // room for exactly two 3-byte events (24 bytes each)
    for (int64_t i = 0; i < 5; ++i)
    {
        in.add(i, {0x90, static_cast<uint8_t>(60 + i), 100});
    }
    out.prepareOutput();
    rig.connectInput(in);
    rig.connectOutput(out);
    rig.run(16);
    EXPECT_EQ(out.read(), (Messages{{0, {0x90, 60, 100}}, {1, {0x90, 61, 100}}}));
    EXPECT_EQ(g_script.droppedAfter, 3u);
    EXPECT_FALSE(g_script.lastSendWorked);
}

TEST(Lv2AdapterMidiOut, MessagesOfOtherSizesThanOneToThreeBytesAreDropped)
{
    Rig<MidiProcessorPorts> rig;
    g_script.sizes = {0, 1, 2, 3, 4};
    Sequence in;
    Sequence out;
    out.prepareOutput();
    rig.connectInput(in);
    rig.connectOutput(out);
    rig.run(8);
    EXPECT_EQ(out.read(), (Messages{{0, {1}}, {0, {1, 2}}, {0, {1, 2, 3}}}));
    EXPECT_EQ(g_script.droppedAfter, 2u);
}

TEST(Lv2AdapterMidiOut, AnUnconnectedOutputDropsEverythingAndStillRuns)
{
    Rig<MidiThruPorts> rig;
    g_script.thru = true;
    Sequence in;
    in.add(1, {0x90, 60, 100});
    rig.connectInput(in);
    rig.run(16);
    EXPECT_EQ(g_script.processCalls.size(), 2u);
    EXPECT_FALSE(g_script.lastSendWorked);
    EXPECT_EQ(g_script.droppedAfter, 1u);
}

TEST(Lv2AdapterMidiOut, AMidiProcessorWithoutAudioPortsGetsProcessWithTheCallLength)
{
    Rig<MidiProcessorPorts> rig;
    g_script.thru = true;
    Sequence in;
    Sequence out;
    in.add(0, {0x90, 60, 100}).add(40, {0x80, 60, 0});
    out.prepareOutput();
    rig.connectInput(in);
    rig.connectOutput(out);
    rig.run(100);
    EXPECT_EQ(g_script.processCalls.size(), 2u) << "no call for the zero frames before the first event";
    EXPECT_EQ(out.read(), (Messages{{0, {0x90, 60, 100}}, {40, {0x80, 60, 0}}}));
}

TEST(Lv2AdapterMidiOut, AGeneratorWithoutAMidiInputRunsOneCallAndWritesFromIt)
{
    Rig<GeneratorPorts> rig;
    g_script.processFrame = 70;
    Sequence out;
    out.prepareOutput();
    rig.connectOutput(out);
    rig.run(128);
    EXPECT_EQ(g_script.processCalls, (std::vector<size_t>{128}));
    EXPECT_EQ(out.read(), (Messages{{70, {0x90, 60, 100}}}));
}

TEST(Lv2AdapterMidiOut, InstantiateNeedsUridMapForAMidiOutput)
{
    Rig<GeneratorPorts> without(nullptr, false);
    EXPECT_FALSE(without.instantiated());
    Rig<GeneratorPorts> with;
    EXPECT_TRUE(with.instantiated());
}

TEST(Lv2AdapterServices, APedalThatAsksIsToldTheFolderOfItsBundleBeforeTheSampleRate)
{
    Rig<PlainPorts> rig;
    ASSERT_TRUE(rig.instantiated());
    EXPECT_EQ(g_script.bundlePath, "/bundle/test.lv2/");
    EXPECT_TRUE(g_script.bundleBeforeSampleRate);
    EXPECT_TRUE(g_script.sampleRateSet);
}

TEST(Lv2AdapterWorker, InstantiateNeedsTheScheduleFeature)
{
    Rig<WorkerPorts> without;
    EXPECT_FALSE(without.instantiated());
    WorkerHost host;
    Rig<WorkerPorts> with(&host);
    EXPECT_TRUE(with.instantiated());
}

TEST(Lv2AdapterWorker, OnlyAWorkerPluginOffersTheWorkerInterface)
{
    WorkerHost host;
    Rig<WorkerPorts> worker(&host);
    ASSERT_TRUE(worker.instantiated());
    EXPECT_NE(workerInterface(worker.descriptor()), nullptr);
    EXPECT_EQ(worker.descriptor()->extension_data("http://example.org/other"), nullptr);
    EXPECT_EQ(worker.descriptor()->extension_data(nullptr), nullptr);
    Rig<PlainPorts> plain;
    EXPECT_EQ(workerInterface(plain.descriptor()), nullptr);
}

TEST(Lv2AdapterWorker, APedalSchedulesJobsFromRunAndLearnsWhenTheHostRefuses)
{
    WorkerHost host;
    Rig<WorkerPorts> rig(&host);
    g_script.scheduleOnProcess = true;
    rig.run(64);
    EXPECT_EQ(host.jobs, (std::vector<std::vector<uint8_t>>{{1, 2, 3}}));
    EXPECT_TRUE(g_script.lastScheduleWorked);
    host.scheduleStatus = LV2_WORKER_ERR_NO_SPACE;
    rig.run(64);
    EXPECT_EQ(host.jobs.size(), 2u);
    EXPECT_FALSE(g_script.lastScheduleWorked);
}

TEST(Lv2AdapterWorker, WorkRunsThePedalWithThePayloadAndAnswersThroughTheHost)
{
    WorkerHost host;
    Rig<WorkerPorts> rig(&host);
    const LV2_Worker_Interface* worker = workerInterface(rig.descriptor());
    ASSERT_NE(worker, nullptr);
    const std::array<uint8_t, 3> job{9, 8, 7};
    EXPECT_EQ(worker->work(rig.handle(), respondWork, &host, 3, job.data()), LV2_WORKER_SUCCESS);
    EXPECT_EQ(g_script.workReceived, (std::vector<std::vector<uint8_t>>{{9, 8, 7}}));
    EXPECT_EQ(host.answers, (std::vector<std::vector<uint8_t>>{{3, 0xAA}}));
    EXPECT_EQ(g_script.respondWorked, (std::vector<bool>{true}));
    host.respondStatus = LV2_WORKER_ERR_NO_SPACE;
    worker->work(rig.handle(), respondWork, &host, 3, job.data());
    EXPECT_EQ(g_script.respondWorked, (std::vector<bool>{true, false})) << "a full response ring is reported";
}

TEST(Lv2AdapterWorker, TheResponseReachesThePedalAndEndRunSucceeds)
{
    WorkerHost host;
    Rig<WorkerPorts> rig(&host);
    const LV2_Worker_Interface* worker = workerInterface(rig.descriptor());
    ASSERT_NE(worker, nullptr);
    const std::array<uint8_t, 2> answer{4, 5};
    EXPECT_EQ(worker->work_response(rig.handle(), 2, answer.data()), LV2_WORKER_SUCCESS);
    EXPECT_EQ(g_script.workResponses, (std::vector<std::vector<uint8_t>>{{4, 5}}));
    EXPECT_EQ(worker->end_run(rig.handle()), LV2_WORKER_SUCCESS);
}

}
