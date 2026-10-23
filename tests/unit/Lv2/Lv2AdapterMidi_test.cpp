// The MIDI input of lv2/Lv2Adapter.h without a plugin binary: a recording pedal shows in which
// order the adapter calls process() and midi() for a block with events.

#include <array>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "lv2/Lv2Adapter.h"
#include "lv2/PortTable.h"

namespace jp::test
{

namespace
{
constexpr LV2_URID c_midiEventUrid{7};
constexpr LV2_URID c_otherUrid{9};

std::vector<std::string> g_log;
const float* g_base{nullptr};

std::string number(const long value)
{
    return std::to_string(value);
}

template <size_t Outputs>
class RecordingPedal
{
  public:
    void setSampleRate(float) noexcept {}
    void reset() noexcept {}

    void process(float* out, const size_t n) noexcept
    {
        g_log.push_back("p" + number(out - g_base) + ":" + number(static_cast<long>(n)));
    }

    void process(const float* const*, float* const* out, const size_t n) noexcept
    {
        std::string entry = "p";
        for (size_t i = 0; i < Outputs; ++i)
        {
            entry += (i == 0 ? "" : "/") + number(out[i] - g_base - static_cast<long>(i) * 256);
        }
        g_log.push_back(entry + ":" + number(static_cast<long>(n)));
    }

    void midi(const uint8_t* msg, const uint32_t size) noexcept
    {
        g_log.push_back("m" + number(msg[0]) + "," + number(msg[1]) + "," + number(msg[2]) + "/" + number(size));
    }
};

struct MonoPorts
{
    using Pedal = RecordingPedal<1>;
    static constexpr const char* c_uri{"urn:test:mono"};
    static constexpr uint32_t c_audioInputs{0};
    static constexpr uint32_t c_audioOutputs{1};
    static constexpr std::array<ControlInput<Pedal>, 0> c_controlInputs{};
    static constexpr std::array<ControlOutput<Pedal>, 0> c_controlOutputs{};
    static constexpr bool c_midiInput{true};
    static constexpr uint32_t c_midiIndex{1};
    static constexpr bool c_midiOutput{false};
    static constexpr uint32_t c_midiOutIndex{0};
    static constexpr bool c_worker{false};
};

struct StereoPorts
{
    using Pedal = RecordingPedal<2>;
    static constexpr const char* c_uri{"urn:test:stereo"};
    static constexpr uint32_t c_audioInputs{0};
    static constexpr uint32_t c_audioOutputs{2};
    static constexpr std::array<ControlInput<Pedal>, 0> c_controlInputs{};
    static constexpr std::array<ControlOutput<Pedal>, 0> c_controlOutputs{};
    static constexpr bool c_midiInput{true};
    static constexpr uint32_t c_midiIndex{2};
    static constexpr bool c_midiOutput{false};
    static constexpr uint32_t c_midiOutIndex{0};
    static constexpr bool c_worker{false};
};

LV2_URID mapUri(LV2_URID_Map_Handle, const char* uri)
{
    return std::strcmp(uri, LV2_MIDI__MidiEvent) == 0 ? c_midiEventUrid : c_otherUrid;
}

/// An atom sequence in the host's layout: header, then the events padded to 8 bytes.
class Sequence
{
  public:
    Sequence()
        : m_bytes(4096, 0)
    {
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(m_bytes.data());
        sequence->atom.type = c_otherUrid;
        sequence->body.unit = 0;
        m_end = sizeof(LV2_Atom_Sequence);
        finish();
    }

    Sequence& add(const int64_t frame, const std::vector<uint8_t>& message, const LV2_URID type = c_midiEventUrid)
    {
        auto* event = reinterpret_cast<LV2_Atom_Event*>(m_bytes.data() + m_end);
        event->time.frames = frame;
        event->body.type = type;
        event->body.size = static_cast<uint32_t>(message.size());
        std::memcpy(m_bytes.data() + m_end + sizeof(LV2_Atom_Event), message.data(), message.size());
        m_end += (sizeof(LV2_Atom_Event) + message.size() + 7u) & ~size_t{7};
        finish();
        return *this;
    }

    [[nodiscard]] void* data()
    {
        return m_bytes.data();
    }

  private:
    void finish()
    {
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(m_bytes.data());
        sequence->atom.size = static_cast<uint32_t>(m_end - sizeof(LV2_Atom));
    }

    std::vector<uint8_t> m_bytes; // heap storage is aligned for any atom
    size_t m_end{0};
};

template <typename Ports>
class Adapted
{
  public:
    using Adapter = Lv2Adapter<Ports>;

    Adapted()
    {
        g_log.clear();
        g_base = m_audio.data();
        const LV2_Descriptor* descriptor = Adapter::descriptor();
        LV2_URID_Map map{nullptr, mapUri};
        const LV2_Feature feature{LV2_URID__map, &map};
        const LV2_Feature* features[2]{&feature, nullptr};
        m_descriptor = descriptor;
        m_handle = descriptor->instantiate(descriptor, 48000.0, ".", features);
        if (m_handle != nullptr)
        {
            for (uint32_t i = 0; i < Ports::c_audioOutputs; ++i)
            {
                descriptor->connect_port(m_handle, i, m_audio.data() + 256 * i);
            }
            descriptor->activate(m_handle);
        }
    }

    ~Adapted()
    {
        if (m_handle != nullptr)
        {
            m_descriptor->cleanup(m_handle);
        }
    }

    void connectMidi(void* sequence)
    {
        m_descriptor->connect_port(m_handle, Ports::c_midiIndex, sequence);
    }

    void run(const uint32_t n)
    {
        g_log.clear();
        m_descriptor->run(m_handle, n);
    }

    [[nodiscard]] bool instantiated() const
    {
        return m_handle != nullptr;
    }

  private:
    std::array<float, 1024> m_audio{};
    const LV2_Descriptor* m_descriptor{nullptr};
    LV2_Handle m_handle{nullptr};
};

using Log = std::vector<std::string>;
}

TEST(Lv2AdapterMidi, EventsCutTheBlockAtTheirFrames)
{
    Adapted<MonoPorts> adapted;
    Sequence sequence;
    sequence.add(0, {0x90, 60, 100}).add(10, {0x91, 61, 101}).add(10, {0x92, 62, 102}).add(50, {0x80, 60, 0});
    adapted.connectMidi(sequence.data());
    adapted.run(128);
    EXPECT_EQ(g_log,
              (Log{"m144,60,100/3", "p0:10", "m145,61,101/3", "m146,62,102/3", "p10:40", "m128,60,0/3", "p50:78"}));
}

TEST(Lv2AdapterMidi, ABlockWithoutEventsIsOneCall)
{
    Adapted<MonoPorts> adapted;
    Sequence sequence;
    adapted.connectMidi(sequence.data());
    adapted.run(128);
    EXPECT_EQ(g_log, (Log{"p0:128"}));
}

TEST(Lv2AdapterMidi, AnUnconnectedMidiPortIsAnEmptySequence)
{
    Adapted<MonoPorts> adapted;
    adapted.run(64);
    EXPECT_EQ(g_log, (Log{"p0:64"}));
}

TEST(Lv2AdapterMidi, FramesOutsideTheBlockMoveToItsEdgesAndOrderIsKept)
{
    Adapted<MonoPorts> adapted;
    Sequence sequence;
    sequence.add(-5, {0x90, 1, 1}).add(40, {0x90, 2, 2}).add(20, {0x90, 3, 3}).add(500, {0x90, 4, 4});
    adapted.connectMidi(sequence.data());
    adapted.run(128);
    EXPECT_EQ(g_log, (Log{"m144,1,1/3", "p0:40", "m144,2,2/3", "m144,3,3/3", "p40:88", "m144,4,4/3"}))
        << "an event earlier than the one before it keeps that position";
}

TEST(Lv2AdapterMidi, ShortMessagesAreZeroPaddedAndLongOnesAndOtherAtomsAreDropped)
{
    Adapted<MonoPorts> adapted;
    Sequence sequence;
    sequence.add(1, {0xD0, 77})
        .add(2, {0xF8})
        .add(3, {0xF0, 1, 2, 3, 0xF7})
        .add(4, {0x90, 60, 100}, c_otherUrid)
        .add(5, {0xC0, 5});
    adapted.connectMidi(sequence.data());
    adapted.run(16);
    EXPECT_EQ(g_log, (Log{"p0:1", "m208,77,0/2", "p1:1", "m248,0,0/1", "p2:3", "m192,5,0/2", "p5:11"}));
}

TEST(Lv2AdapterMidi, EveryAudioOutputIsOffsetAlike)
{
    Adapted<StereoPorts> adapted;
    Sequence sequence;
    sequence.add(0, {0x90, 60, 100}).add(32, {0x80, 60, 0});
    adapted.connectMidi(sequence.data());
    adapted.run(100);
    EXPECT_EQ(g_log, (Log{"m144,60,100/3", "p0/0:32", "m128,60,0/3", "p32/32:68"}));
}

TEST(Lv2AdapterMidi, InstantiateNeedsUridMap)
{
    const LV2_Descriptor* descriptor = Lv2Adapter<MonoPorts>::descriptor();
    const LV2_Feature* none[1]{nullptr};
    EXPECT_EQ(descriptor->instantiate(descriptor, 48000.0, ".", none), nullptr);
    EXPECT_EQ(descriptor->instantiate(descriptor, 48000.0, ".", nullptr), nullptr);
    Adapted<MonoPorts> adapted;
    EXPECT_TRUE(adapted.instantiated());
}

}
