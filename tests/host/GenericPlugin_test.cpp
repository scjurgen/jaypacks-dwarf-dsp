// Generic host tests for every bundle the build produced (<build>/lv2/jaypacks-*.lv2): the
// TTL matches the binary, and the plugin behaves for any sample rate, block size and control
// value; control outputs stay finite and inside their range. Plugin-specific behaviour is tested in
// tests/host/<Name>_test.cpp.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Lv2Host.h"
#include "dsp/Numbers/Random.h"

namespace jp::host
{

namespace
{
// a sanitizer build (the mac-asan preset: Debug and ASan/UBSan) runs many times slower, so
// a real-time budget means nothing there
#if defined(__SANITIZE_ADDRESS__)
constexpr bool c_sanitized{true};
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
constexpr bool c_sanitized{true};
#else
constexpr bool c_sanitized{false};
#endif
#else
constexpr bool c_sanitized{false};
#endif

std::vector<float> noise(const size_t length, const float amplitude = 0.5f, const uint64_t seed = 1u)
{
    jp::Random random(seed);
    std::vector<float> out(length);
    for (auto& x : out)
    {
        x = amplitude * (2.0f * random.uniform() - 1.0f);
    }
    return out;
}

bool allFinite(const std::vector<float>& signal)
{
    return std::all_of(signal.begin(), signal.end(), [](const float x) { return std::isfinite(x); });
}

float peak(const std::vector<float>& signal)
{
    float result = 0.0f;
    for (const float x : signal)
    {
        result = std::max(result, std::abs(x));
    }
    return result;
}

std::vector<PortInfo> controlInputs(const BundleInfo& bundle)
{
    std::vector<PortInfo> controls;
    for (const PortInfo& port : bundle.ports)
    {
        if (!port.audio && !port.atom && port.input)
        {
            controls.push_back(port);
        }
    }
    return controls;
}

std::vector<PortInfo> controlOutputs(const BundleInfo& bundle)
{
    std::vector<PortInfo> outputs;
    for (const PortInfo& port : bundle.ports)
    {
        if (!port.audio && !port.atom && !port.input)
        {
            outputs.push_back(port);
        }
    }
    return outputs;
}

/// First control output outside its range or not finite, empty if all are fine.
std::string badOutput(const BundleInfo& bundle, const Instance& instance)
{
    for (const PortInfo& port : controlOutputs(bundle))
    {
        const float value = instance.output(port.symbol);
        if (!std::isfinite(value) || value < port.minimum || value > port.maximum)
        {
            return port.symbol + " = " + std::to_string(value);
        }
    }
    return {};
}
}

class GenericPlugin : public ::testing::TestWithParam<fs::path>
{
  protected:
    void SetUp() override
    {
        m_bundle = readBundle(GetParam());
        m_library = std::make_unique<Library>(m_bundle);
    }

    BundleInfo m_bundle;
    std::unique_ptr<Library> m_library;
};

TEST(GenericPluginSetup, TheBuildProducedBundles)
{
    EXPECT_FALSE(findBundles(JP_LV2_DIR).empty()) << JP_LV2_DIR;
}

TEST_P(GenericPlugin, ManifestTtlAndBinaryAgree)
{
    ASSERT_NE(m_library->descriptor(), nullptr);
    EXPECT_STREQ(m_library->descriptor()->URI, m_bundle.uri.c_str());
    EXPECT_EQ(m_library->secondDescriptor(), nullptr) << "one plugin per binary";
    const std::string name = GetParam().stem().string(); // jaypacks-<name>
    EXPECT_EQ(m_bundle.binary, name + ".so");
    for (const std::string& file : m_bundle.seeAlso)
    {
        EXPECT_TRUE(fs::exists(m_bundle.dir / file)) << file;
    }
    ASSERT_FALSE(m_bundle.ports.empty());
    for (size_t i = 0; i < m_bundle.ports.size(); ++i)
    {
        EXPECT_EQ(m_bundle.ports[i].index, i) << "port indices are 0..n-1";
        EXPECT_FALSE(m_bundle.ports[i].symbol.empty());
    }
    for (const PortInfo& port : controlInputs(m_bundle))
    {
        EXPECT_LT(port.minimum, port.maximum) << port.symbol;
        EXPECT_GE(port.defaultValue, port.minimum) << port.symbol;
        EXPECT_LE(port.defaultValue, port.maximum) << port.symbol;
    }
    for (const PortInfo& port : controlOutputs(m_bundle))
    {
        EXPECT_LT(port.minimum, port.maximum) << port.symbol;
    }
}

TEST_P(GenericPlugin, ControlOutputsStayInTheirRange)
{
    Instance instance(m_library->descriptor(), m_bundle, 48000.0);
    EXPECT_EQ(badOutput(m_bundle, instance), "") << "after activate()";
    const auto input = noise(48000 * 3);
    std::string problem;
    std::ignore = instance.process(input, 128, false,
                                   [&](const size_t)
                                   {
                                       if (problem.empty())
                                       {
                                           problem = badOutput(m_bundle, instance);
                                       }
                                   });
    EXPECT_EQ(problem, "");
    EXPECT_EQ(badOutput(m_bundle, instance), "");
}

TEST_P(GenericPlugin, SilenceAndNoiseGiveFiniteOutputAtEverySampleRate)
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        Instance instance(m_library->descriptor(), m_bundle, rate);
        const auto length = static_cast<size_t>(rate);
        const auto silent = instance.process(std::vector<float>(length, 0.0f), 128);
        EXPECT_TRUE(allFinite(silent)) << rate;
        const auto noisy = instance.process(noise(length), 128);
        EXPECT_TRUE(allFinite(noisy)) << rate;
        EXPECT_LT(peak(noisy), 100.0f) << rate;
    }
}

TEST_P(GenericPlugin, EveryControlAtItsMinimumAndMaximumStaysFinite)
{
    const auto input = noise(24000);
    for (const PortInfo& port : controlInputs(m_bundle))
    {
        for (const float value : {port.minimum, port.maximum})
        {
            Instance instance(m_library->descriptor(), m_bundle, 48000.0);
            instance.setControl(port.symbol, value);
            const auto out = instance.process(input, 128);
            EXPECT_TRUE(allFinite(out)) << port.symbol << " = " << value;
            EXPECT_LT(peak(out), 100.0f) << port.symbol << " = " << value;
            EXPECT_EQ(badOutput(m_bundle, instance), "") << port.symbol << " = " << value;
        }
    }
    for (const bool high : {false, true})
    {
        Instance instance(m_library->descriptor(), m_bundle, 48000.0);
        for (const PortInfo& port : controlInputs(m_bundle))
        {
            instance.setControl(port.symbol, high ? port.maximum : port.minimum);
        }
        const auto out = instance.process(input, 128);
        EXPECT_TRUE(allFinite(out)) << (high ? "all at maximum" : "all at minimum");
    }
}

/// A plugin with a MIDI input gets a few notes, so that a synth sounds and its controls can show: a note on channel 1,
/// a pitch bend on it, and a second note on channel 3 (another voice, a glide target, a member channel of a zone).
void sendStimulus(Instance& instance)
{
    if (instance.hasMidiInput())
    {
        instance.sendMidi(0, 0x90, 60, 100);
        instance.sendMidi(100, 0xE0, 0x7F, 0x7F);
        instance.sendMidi(4000, 0x92, 67, 100);
    }
}

TEST_P(GenericPlugin, EveryControlReachesThePlugin)
{
    // the extreme farther from the default must change the audio output or a control output
    // for a noise input (and, for a plugin with a MIDI input, for the notes of sendStimulus())
    const auto input = noise(48000);
    Instance referenceInstance(m_library->descriptor(), m_bundle, 48000.0);
    sendStimulus(referenceInstance);
    const auto reference = referenceInstance.process(input, 128);
    const auto referenceOutputs = referenceInstance.outputs();
    const auto referenceMidi = referenceInstance.takeMidiOut();
    for (const PortInfo& port : controlInputs(m_bundle))
    {
        // morphexsynth's Pressure scales only the slots of a patch that read Z, which the default patch has none of:
        // tests/host/Morphexsynth_test.cpp checks it with one that has
        if (m_bundle.uri.find("/morphexsynth") != std::string::npos && port.symbol == "pressure")
        {
            continue;
        }
        // morphexsynth's Release acts after a note off, which the stimulus never sends: Morphexsynth_test.cpp has one
        if (m_bundle.uri.find("/morphexsynth") != std::string::npos && port.symbol == "release")
        {
            continue;
        }
        // midiscript's tempo, sync, speed and panic act on the clock and on sounding notes, which the default script
        // (Passthrough) has none of: tests/unit/Plugins/MidiScript_test.cpp checks them with scripts that use the clock
        if (m_bundle.uri.find("/midiscript") != std::string::npos &&
            (port.symbol == "tempo" || port.symbol == "sync" || port.symbol == "speed" || port.symbol == "panic"))
        {
            continue;
        }
        // shipahoy's transport state acts on the clock only with Sync = Transport
        // (tests/unit/Plugins/ShipAhoyPedal_test.cpp)
        if (m_bundle.uri.find("/shipahoy") != std::string::npos &&
            (port.symbol == "transport" || port.symbol == "hostTempo"))
        {
            continue;
        }
        // shipahoy's drift rate acts only with a drift depth above 0 (tests/host/ShipAhoy_test.cpp)
        if (m_bundle.uri.find("/shipahoy") != std::string::npos && port.symbol == "modRate")
        {
            continue;
        }
        const bool useMinimum = port.defaultValue - port.minimum > port.maximum - port.defaultValue;
        Instance instance(m_library->descriptor(), m_bundle, 48000.0);
        instance.setControl(port.symbol, useMinimum ? port.minimum : port.maximum);
        sendStimulus(instance);
        const bool audioDiffers = instance.process(input, 128) != reference;
        const bool midiDiffers = instance.takeMidiOut() != referenceMidi;
        EXPECT_TRUE(audioDiffers || midiDiffers || instance.outputs() != referenceOutputs) << port.symbol;
    }
}

TEST_P(GenericPlugin, OutOfRangeAndNanControlsAreHarmless)
{
    const auto input = noise(24000);
    for (const PortInfo& port : controlInputs(m_bundle))
    {
        for (const float value : {-1e9f, 1e9f, std::numeric_limits<float>::quiet_NaN()})
        {
            Instance instance(m_library->descriptor(), m_bundle, 48000.0);
            instance.setControl(port.symbol, value);
            const auto out = instance.process(input, 128);
            EXPECT_TRUE(allFinite(out)) << port.symbol << " = " << value;
        }
    }
}

TEST_P(GenericPlugin, OutputDoesNotDependOnTheBlockSize)
{
    // shipahoy's clock runs a lead ahead of the audio, two blocks of the largest block it is called with, so where its
    // steps fall depends on the block size by design; tests/unit/Plugins/ShipAhoyPedal_test.cpp runs it with a fixed
    // lead through blocks of 1 to 4096
    if (m_bundle.uri.find("/shipahoy") != std::string::npos)
    {
        GTEST_SKIP() << "the lead of the clock follows the block size";
    }
    const auto input = noise(48000 * 2);
    std::vector<float> reference;
    std::vector<float> referenceOutputs;
    for (const size_t blockSize : {1u, 7u, 16u, 128u, 4096u})
    {
        Instance instance(m_library->descriptor(), m_bundle, 48000.0);
        const auto out = instance.process(input, blockSize);
        if (reference.empty())
        {
            reference = out;
            referenceOutputs = instance.outputs();
        }
        else
        {
            EXPECT_EQ(out, reference) << "block size " << blockSize;
            EXPECT_EQ(instance.outputs(), referenceOutputs) << "control outputs, block size " << blockSize;
        }
    }
}

TEST_P(GenericPlugin, MidiEventsActAtTheirSampleWhateverTheBlockSize)
{
    if (!Instance(m_library->descriptor(), m_bundle, 48000.0).hasMidiInput())
    {
        GTEST_SKIP() << "no MIDI input";
    }
    // channel voice messages of every kind on several channels, at positions that fall inside
    // and on the edges of the blocks; the output must not depend on how the blocks cut them
    const auto input = noise(48000);
    std::vector<MidiMessage> messages;
    jp::Random random(3u);
    for (uint64_t sample = 0; sample < 40000; sample += 1 + static_cast<uint64_t>(random.uniform() * 700.0f))
    {
        const auto channel = static_cast<uint8_t>(random.uniform() * 16.0f);
        const auto data1 = static_cast<uint8_t>(random.uniform() * 128.0f);
        const auto data2 = static_cast<uint8_t>(random.uniform() * 128.0f);
        const uint8_t kinds[]{0x90, 0x80, 0xB0, 0xE0, 0xD0, 0xA0, 0xC0};
        const uint8_t kind = kinds[static_cast<size_t>(random.uniform() * 7.0f) % 7];
        const bool twoBytes = kind == 0xD0 || kind == 0xC0;
        messages.push_back(
            {sample, static_cast<uint8_t>(kind | channel), data1, twoBytes ? uint8_t{0} : data2, twoBytes ? 2u : 3u});
    }
    std::vector<float> reference;
    std::vector<float> referenceOutputs;
    std::vector<MidiMessage> referenceMidi;
    bool haveReference = false;
    // midiscript answers through its worker, which the default latency of 12 ms covers for blocks up to 256 frames
    // (a longer block makes messages late, as documented; tests/unit/Plugins/MidiScript_test.cpp has the longer ones)
    const bool scripted = m_bundle.uri.find("/midiscript") != std::string::npos;
    const std::vector<size_t> blockSizes =
        scripted ? std::vector<size_t>{1u, 7u, 16u, 128u, 256u} : std::vector<size_t>{1u, 7u, 16u, 128u, 4096u};
    for (const size_t blockSize : blockSizes)
    {
        Instance instance(m_library->descriptor(), m_bundle, 48000.0);
        for (const MidiMessage& message : messages)
        {
            instance.sendMidi(message.sample, message.status, message.data1, message.data2, message.size);
        }
        const auto out = instance.process(input, blockSize);
        const auto midiOut = instance.takeMidiOut();
        if (!haveReference)
        {
            haveReference = true;
            reference = out;
            referenceOutputs = instance.outputs();
            referenceMidi = midiOut;
        }
        else
        {
            EXPECT_EQ(out, reference) << "block size " << blockSize;
            EXPECT_EQ(instance.outputs(), referenceOutputs) << "control outputs, block size " << blockSize;
            EXPECT_EQ(midiOut, referenceMidi) << "MIDI output, block size " << blockSize;
        }
    }
}

TEST_P(GenericPlugin, InPlaceProcessingEqualsSeparateBuffers)
{
    if (Instance(m_library->descriptor(), m_bundle, 48000.0).audioInputs() == 0)
    {
        GTEST_SKIP() << "no audio input (signal source)";
    }
    const auto input = noise(48000);
    Instance separate(m_library->descriptor(), m_bundle, 48000.0);
    Instance inPlace(m_library->descriptor(), m_bundle, 48000.0);
    EXPECT_EQ(inPlace.process(input, 128, true), separate.process(input, 128, false));
}

TEST_P(GenericPlugin, ActivateClearsTheState)
{
    Instance instance(m_library->descriptor(), m_bundle, 48000.0);
    if (instance.audioInputs() == 0)
    {
        GTEST_SKIP() << "no audio input (signal source): silence in, silence out does not apply";
    }
    std::ignore = instance.process(noise(48000, 1.0f), 128);
    instance.reactivate();
    const auto out = instance.process(std::vector<float>(4800, 0.0f), 128);
    EXPECT_EQ(peak(out), 0.0f) << "silence in, silence out after activate()";
}

TEST_P(GenericPlugin, CpuTimePerBlockIsSmall)
{
    Instance instance(m_library->descriptor(), m_bundle, 48000.0);
    const auto input = noise(48000 * 10);
    const auto start = std::chrono::steady_clock::now();
    std::ignore = instance.process(input, 128);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double blocks = static_cast<double>(input.size()) / 128.0;
    const double microsecondsPerBlock = seconds * 1e6 / blocks;
    const double budgetMicroseconds = 128.0 / 48000.0 * 1e6; // 2667 us of real time per block
    std::cout << "[ cpu      ] " << GetParam().filename().string() << ": " << microsecondsPerBlock
              << " us per 128 frames, " << 100.0 * microsecondsPerBlock / budgetMicroseconds
              << " % of real time on this machine\n";
    RecordProperty("microseconds_per_128_frames", std::to_string(microsecondsPerBlock));
    if (c_sanitized)
    {
        GTEST_SKIP() << "sanitizer build: the time is printed, not judged";
    }
    EXPECT_LT(microsecondsPerBlock, 0.1 * budgetMicroseconds);
}

INSTANTIATE_TEST_SUITE_P(Bundles, GenericPlugin, ::testing::ValuesIn(findBundles(JP_LV2_DIR)),
                         [](const ::testing::TestParamInfo<fs::path>& info)
                         {
                             std::string name = info.param.stem().string();
                             std::replace(name.begin(), name.end(), '-', '_');
                             return name;
                         });

}
